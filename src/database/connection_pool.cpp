// connection_pool.cpp
// PostgreSQL connection pool implementation

#include "database/connection_pool.h"
#include "core/logger.h"
#include "perf/perf_stats.h"

#include <sstream>

namespace hb::database
{

// pooled_connection implementation

pooled_connection::pooled_connection(std::shared_ptr<pqxx::connection> conn,
                                     std::function<void(std::shared_ptr<pqxx::connection>)> release_fn)
    : connection_(std::move(conn))
    , release_fn_(std::move(release_fn))
{
}

pooled_connection::~pooled_connection()
{
    release();
}

pooled_connection::pooled_connection(pooled_connection&& other) noexcept
    : connection_(std::move(other.connection_))
    , release_fn_(std::move(other.release_fn_))
{
    other.connection_ = nullptr;
    other.release_fn_ = nullptr;
}

auto pooled_connection::operator=(pooled_connection&& other) noexcept -> pooled_connection&
{
    if (this != &other)
    {
        release();
        connection_ = std::move(other.connection_);
        release_fn_ = std::move(other.release_fn_);
        other.connection_ = nullptr;
        other.release_fn_ = nullptr;
    }
    return *this;
}

auto pooled_connection::get() -> pqxx::connection*
{
    return connection_.get();
}

auto pooled_connection::get() const -> const pqxx::connection*
{
    return connection_.get();
}

auto pooled_connection::operator->() -> pqxx::connection*
{
    return connection_.get();
}

auto pooled_connection::operator->() const -> const pqxx::connection*
{
    return connection_.get();
}

auto pooled_connection::operator*() -> pqxx::connection&
{
    return *connection_;
}

auto pooled_connection::operator*() const -> const pqxx::connection&
{
    return *connection_;
}

pooled_connection::operator bool() const
{
    return is_valid();
}

auto pooled_connection::is_valid() const -> bool
{
    return connection_ && connection_->is_open();
}

void pooled_connection::release()
{
    if (connection_ && release_fn_)
    {
        release_fn_(std::move(connection_));
        connection_ = nullptr;
        release_fn_ = nullptr;
    }
}

// connection_pool implementation

connection_pool::connection_pool() = default;

connection_pool::connection_pool(const pool_config& config) : config_(config) {}

connection_pool::~connection_pool()
{
    shutdown();
}

auto connection_pool::initialize(const pool_config& config) -> result<void, std::string>
{
    std::lock_guard lock{mutex_};

    if (initialized_)
    {
        return result<void, std::string>::err("Connection pool already initialized");
    }

    config_ = config;
    shutdown_ = false;

    LOG_INFO(database,
             "Initializing connection pool with {} connections to {}:{}",
             config_.pool_size,
             config_.host,
             config_.port);

    // Create initial connections
    for (uint32_t i = 0; i < config_.pool_size; ++i)
    {
        auto conn_result = create_connection();
        if (conn_result.is_err())
        {
            // Clean up any created connections
            active_count_ = 0;
            while (!available_.empty())
            {
                available_.pop();
            }
            return result<void, std::string>::err("Failed to create connection " + std::to_string(i + 1) + ": " +
                                                  conn_result.error());
        }

        available_.push(std::move(conn_result).value());
        ++active_count_;
    }

    initialized_ = true;
    LOG_INFO(database, "Connection pool initialized with {} connections", active_count_);

    return result<void, std::string>::ok();
}

void connection_pool::shutdown()
{
    std::lock_guard lock{mutex_};

    if (!initialized_)
    {
        return;
    }

    LOG_INFO(database, "Shutting down connection pool");

    shutdown_ = true;
    initialized_ = false;

    // Close and drain available connections
    while (!available_.empty())
    {
        auto& conn = available_.front();
        if (conn && conn->is_open())
        {
            try
            {
                conn->close();
            }
            catch (const std::exception& e)
            {
                LOG_WARN(database, "Error closing connection: {}", e.what());
            }
        }
        available_.pop();
    }

    active_count_ = 0;
    cv_.notify_all();

    LOG_INFO(database, "Connection pool shut down");
}

auto connection_pool::acquire() -> result<pooled_connection, std::string>
{
    return acquire(config_.acquire_timeout);
}

auto connection_pool::acquire(std::chrono::milliseconds timeout) -> result<pooled_connection, std::string>
{
    PERF_TIMER(perf_stats_, perf::metric_category::db_pool_acquire);

    std::unique_lock lock{mutex_};

    if (!initialized_)
    {
        return result<pooled_connection, std::string>::err("Connection pool not initialized");
    }

    if (shutdown_)
    {
        return result<pooled_connection, std::string>::err("Connection pool is shutting down");
    }

    // Wait for an available connection
    auto deadline = std::chrono::steady_clock::now() + timeout;

    while (available_.empty() && !shutdown_)
    {
        if (cv_.wait_until(lock, deadline) == std::cv_status::timeout)
        {
            return result<pooled_connection, std::string>::err("Timeout waiting for database connection");
        }
    }

    if (shutdown_ || available_.empty())
    {
        return result<pooled_connection, std::string>::err("Connection pool is shutting down");
    }

    auto conn = available_.front();
    available_.pop();

    // Verify the connection is still valid
    if (!conn || !conn->is_open())
    {
        LOG_WARN(database, "Acquired stale connection, attempting reconnect");

        auto new_conn_result = create_connection();
        if (new_conn_result.is_err())
        {
            --active_count_;
            LOG_ERROR(database, "Failed to replace dead connection, pool size now {}", active_count_);
            return result<pooled_connection, std::string>::err("Failed to reconnect: " + new_conn_result.error());
        }

        conn = std::move(new_conn_result).value();
    }

    // Create the pooled connection with release callback
    auto release_fn = [this](std::shared_ptr<pqxx::connection> c)
    {
        return_connection(std::move(c));
    };

    return result<pooled_connection, std::string>::ok(pooled_connection{std::move(conn), std::move(release_fn)});
}

auto connection_pool::try_acquire() -> std::optional<pooled_connection>
{
    std::lock_guard lock{mutex_};

    if (!initialized_ || shutdown_ || available_.empty())
    {
        return std::nullopt;
    }

    auto conn = available_.front();
    available_.pop();

    if (!conn || !conn->is_open())
    {
        LOG_WARN(database, "Popped stale connection in try_acquire, attempting replacement");

        auto new_conn_result = create_connection();
        if (new_conn_result.is_err())
        {
            --active_count_;
            LOG_ERROR(database, "Failed to replace dead connection, pool size now {}", active_count_);
            return std::nullopt;
        }

        conn = std::move(new_conn_result).value();
    }

    auto release_fn = [this](std::shared_ptr<pqxx::connection> c)
    {
        return_connection(std::move(c));
    };

    return pooled_connection{std::move(conn), std::move(release_fn)};
}

auto connection_pool::available_connections() const -> size_t
{
    std::lock_guard lock{mutex_};
    return available_.size();
}

auto connection_pool::total_connections() const -> size_t
{
    std::lock_guard lock{mutex_};
    return active_count_;
}

auto connection_pool::is_initialized() const -> bool
{
    std::lock_guard lock{mutex_};
    return initialized_;
}

auto connection_pool::connection_string() const -> std::string
{
    return build_connection_string();
}

auto connection_pool::create_connection() -> result<std::shared_ptr<pqxx::connection>, std::string>
{
    try
    {
        auto conn_str = build_connection_string();
        auto conn = std::make_shared<pqxx::connection>(conn_str);

        if (!conn->is_open())
        {
            return result<std::shared_ptr<pqxx::connection>, std::string>::err("Failed to open connection");
        }

        LOG_DEBUG(database, "Created new database connection");
        return result<std::shared_ptr<pqxx::connection>, std::string>::ok(std::move(conn));
    }
    catch (const pqxx::broken_connection& e)
    {
        return result<std::shared_ptr<pqxx::connection>, std::string>::err(std::string("Connection failed: ") +
                                                                           e.what());
    }
    catch (const std::exception& e)
    {
        return result<std::shared_ptr<pqxx::connection>, std::string>::err(
            std::string("Exception creating connection: ") + e.what());
    }
}

void connection_pool::return_connection(std::shared_ptr<pqxx::connection> conn)
{
    if (!conn)
    {
        return;
    }

    std::lock_guard lock{mutex_};

    if (shutdown_)
    {
        // Pool is shutting down, close the connection
        if (conn->is_open())
        {
            try
            {
                conn->close();
            }
            catch (...)
            {
                // Ignore errors during shutdown
            }
        }
        return;
    }

    // Check if connection is still valid
    if (conn->is_open())
    {
        available_.push(std::move(conn));
        cv_.notify_one();
    }
    else
    {
        LOG_WARN(database, "Returned connection is closed, attempting replacement");

        auto new_conn_result = create_connection();
        if (new_conn_result.is_ok())
        {
            available_.push(std::move(new_conn_result).value());
            cv_.notify_one();
        }
        else
        {
            --active_count_;
            LOG_ERROR(database, "Failed to replace dead connection, pool size now {}", active_count_);
        }
    }
}

auto connection_pool::build_connection_string() const -> std::string
{
    std::ostringstream ss;
    ss << "host=" << config_.host << " port=" << config_.port << " dbname=" << config_.database
       << " user=" << config_.username;

    if (!config_.password.empty())
    {
        ss << " password=" << config_.password;
    }

    ss << " connect_timeout=" << (config_.connection_timeout.count() / 1000);

    return ss.str();
}

} // namespace hb::database
