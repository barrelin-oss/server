// database_system.cpp
// PostgreSQL database subsystem implementation

#include "database/database_system.h"
#include "core/logger.h"
#include "perf/perf_stats.h"

namespace hb::database {

// query_result implementation

query_result::query_result(pqxx::result result)
    : result_(std::move(result))
{
}

auto query_result::empty() const -> bool {
    return result_.empty();
}

auto query_result::size() const -> size_t {
    return result_.size();
}

auto query_result::affected_rows() const -> size_t {
    return result_.affected_rows();
}

auto query_result::operator[](size_t index) const -> pqxx::row {
    return result_[static_cast<pqxx::result::size_type>(index)];
}

auto query_result::begin() const -> pqxx::result::const_iterator {
    return result_.begin();
}

auto query_result::end() const -> pqxx::result::const_iterator {
    return result_.end();
}

// database_system implementation

database_system::database_system() = default;

database_system::~database_system() = default;

void database_system::initialize() {
    LOG_INFO(database, "Initializing database system");

    pool_config pool_cfg{
        .host = config_.host,
        .port = config_.port,
        .database = config_.database,
        .username = config_.username,
        .password = config_.password,
        .pool_size = config_.pool_size,
        .connection_timeout = config_.connection_timeout,
        .acquire_timeout = config_.query_timeout
    };

    auto init_result = pool_.initialize(pool_cfg);
    if (init_result.is_err()) {
        LOG_ERROR(database, "Failed to initialize database pool: {}", init_result.error());
        throw std::runtime_error("Database initialization failed: " + init_result.error());
    }

    set_initialized(true);
    LOG_INFO(database, "Database system initialized successfully");
}

void database_system::shutdown() {
    LOG_INFO(database, "Shutting down database system");
    pool_.shutdown();
    set_initialized(false);
    LOG_INFO(database, "Database system shut down");
}

void database_system::set_config(const database_config& config) {
    config_ = config;
}

auto database_system::is_connected() const -> bool {
    return pool_.is_initialized() && pool_.available_connections() > 0;
}

auto database_system::reconnect() -> result<void, std::string> {
    LOG_INFO(database, "Attempting to reconnect database");

    pool_.shutdown();

    pool_config pool_cfg{
        .host = config_.host,
        .port = config_.port,
        .database = config_.database,
        .username = config_.username,
        .password = config_.password,
        .pool_size = config_.pool_size,
        .connection_timeout = config_.connection_timeout,
        .acquire_timeout = config_.query_timeout
    };

    return pool_.initialize(pool_cfg);
}

auto database_system::execute_unsafe(std::string_view query)
    -> hb::result<query_result, std::string>
{
    PERF_TIMER(perf_stats_, perf::metric_category::db_query);

    auto conn = pool_.acquire();
    if (conn.is_err()) {
        return hb::result<query_result, std::string>::err(conn.error());
    }

    try {
        pqxx::work txn{*conn.value()};
        auto query_res = txn.exec(std::string(query));
        txn.commit();
        return hb::result<query_result, std::string>::ok(query_result{std::move(query_res)});
    } catch (const pqxx::sql_error& ex) {
        LOG_ERROR(database, "SQL error: {} (query: {})", ex.what(), ex.query());
        return hb::result<query_result, std::string>::err(std::string("SQL error: ") + ex.what());
    } catch (const std::exception& ex) {
        LOG_ERROR(database, "Query execution failed: {}", ex.what());
        return hb::result<query_result, std::string>::err(std::string("Query failed: ") + ex.what());
    }
}

#ifdef __GNUC__
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
#endif
#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable: 4996)
#endif

auto database_system::execute(std::string_view query)
    -> hb::result<query_result, std::string>
{
    return execute_unsafe(query);
}

#ifdef _MSC_VER
#pragma warning(pop)
#endif
#ifdef __GNUC__
#pragma GCC diagnostic pop
#endif

auto database_system::execute_transaction(transaction_func func)
    -> hb::result<void, std::string>
{
    PERF_TIMER(perf_stats_, perf::metric_category::db_query);

    auto conn = pool_.acquire();
    if (conn.is_err()) {
        return hb::result<void, std::string>::err(conn.error());
    }

    try {
        pqxx::work txn{*conn.value()};
        auto func_result = func(txn);
        if (func_result.is_err()) {
            // Transaction will be rolled back when txn goes out of scope
            return func_result;
        }
        txn.commit();
        return hb::result<void, std::string>::ok();
    } catch (const pqxx::sql_error& ex) {
        LOG_ERROR(database, "Transaction SQL error: {} (query: {})", ex.what(), ex.query());
        return hb::result<void, std::string>::err(std::string("Transaction failed: ") + ex.what());
    } catch (const std::exception& ex) {
        LOG_ERROR(database, "Transaction failed: {}", ex.what());
        return hb::result<void, std::string>::err(std::string("Transaction failed: ") + ex.what());
    }
}

auto database_system::prepare(std::string_view name, std::string_view query)
    -> hb::result<void, std::string>
{
    auto conn = pool_.acquire();
    if (conn.is_err()) {
        return hb::result<void, std::string>::err(conn.error());
    }

    try {
        conn.value()->prepare(std::string(name), std::string(query));
        return hb::result<void, std::string>::ok();
    } catch (const std::exception& ex) {
        return hb::result<void, std::string>::err(std::string("Failed to prepare statement: ") + ex.what());
    }
}

auto database_system::quote(std::string_view str) -> std::string {
    auto conn = pool_.try_acquire();
    if (!conn) {
        // Fallback to manual escaping if no connection available
        std::string escaped;
        escaped.reserve(str.size() + 2);
        escaped += '\'';
        for (char c : str) {
            if (c == '\'') {
                escaped += "''";
            } else {
                escaped += c;
            }
        }
        escaped += '\'';
        return escaped;
    }

    return (*conn)->quote(std::string(str));
}

auto database_system::quote_name(std::string_view name) -> std::string {
    auto conn = pool_.try_acquire();
    if (!conn) {
        // Fallback to manual quoting
        std::string quoted;
        quoted.reserve(name.size() + 2);
        quoted += '"';
        for (char c : name) {
            if (c == '"') {
                quoted += "\"\"";
            } else {
                quoted += c;
            }
        }
        quoted += '"';
        return quoted;
    }

    return (*conn)->quote_name(std::string(name));
}

auto database_system::available_connections() const -> size_t {
    return pool_.available_connections();
}

auto database_system::total_connections() const -> size_t {
    return pool_.total_connections();
}

}  // namespace hb::database
