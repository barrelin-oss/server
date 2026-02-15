#pragma once

// connection_pool.h
// PostgreSQL connection pool for async database operations

#include "core/result.h"

#include <pqxx/pqxx>
#include <memory>
#include <queue>
#include <mutex>
#include <condition_variable>
#include <chrono>
#include <string>
#include <string_view>
#include <functional>
#include <optional>

namespace hb::perf
{
class perf_stats_system;
}

namespace hb::database
{

// Connection pool configuration
struct pool_config
{
    std::string host = "localhost";
    uint16_t port = 5432;
    std::string database = "helbreath";
    std::string username = "hgserver";
    std::string password;
    uint32_t pool_size = 10;
    std::chrono::milliseconds connection_timeout{5000};
    std::chrono::milliseconds acquire_timeout{30000};
};

// RAII wrapper for a pooled connection
class pooled_connection
{
public:
    pooled_connection() = default;
    pooled_connection(std::shared_ptr<pqxx::connection> conn,
                      std::function<void(std::shared_ptr<pqxx::connection>)> release_fn);
    ~pooled_connection();

    // Non-copyable, but movable
    pooled_connection(const pooled_connection&) = delete;
    auto operator=(const pooled_connection&) -> pooled_connection& = delete;
    pooled_connection(pooled_connection&& other) noexcept;
    auto operator=(pooled_connection&& other) noexcept -> pooled_connection&;

    // Access the underlying connection
    [[nodiscard]] auto get() -> pqxx::connection*;
    [[nodiscard]] auto get() const -> const pqxx::connection*;
    [[nodiscard]] auto operator->() -> pqxx::connection*;
    [[nodiscard]] auto operator->() const -> const pqxx::connection*;
    [[nodiscard]] auto operator*() -> pqxx::connection&;
    [[nodiscard]] auto operator*() const -> const pqxx::connection&;

    // Check if connection is valid
    [[nodiscard]] explicit operator bool() const;
    [[nodiscard]] auto is_valid() const -> bool;

    // Release connection back to pool early
    void release();

private:
    std::shared_ptr<pqxx::connection> connection_;
    std::function<void(std::shared_ptr<pqxx::connection>)> release_fn_;
};

// Database connection pool
class connection_pool
{
public:
    connection_pool();
    explicit connection_pool(const pool_config& config);
    ~connection_pool();

    // Non-copyable, non-movable
    connection_pool(const connection_pool&) = delete;
    auto operator=(const connection_pool&) -> connection_pool& = delete;
    connection_pool(connection_pool&&) = delete;
    auto operator=(connection_pool&&) -> connection_pool& = delete;

    // Initialize the pool with given configuration
    auto initialize(const pool_config& config) -> result<void, std::string>;

    // Shutdown the pool and close all connections
    void shutdown();

    // Acquire a connection from the pool
    // Blocks until a connection is available or timeout
    [[nodiscard]] auto acquire() -> result<pooled_connection, std::string>;

    // Acquire with custom timeout
    [[nodiscard]] auto acquire(std::chrono::milliseconds timeout) -> result<pooled_connection, std::string>;

    // Try to acquire without blocking
    [[nodiscard]] auto try_acquire() -> std::optional<pooled_connection>;

    // Pool statistics
    [[nodiscard]] auto available_connections() const -> size_t;
    [[nodiscard]] auto total_connections() const -> size_t;
    [[nodiscard]] auto is_initialized() const -> bool;

    // Get connection string (for diagnostic purposes)
    [[nodiscard]] auto connection_string() const -> std::string;

    // Performance statistics
    void set_perf_stats(hb::perf::perf_stats_system* perf) { perf_stats_ = perf; }

private:
    auto create_connection() -> result<std::shared_ptr<pqxx::connection>, std::string>;
    void return_connection(std::shared_ptr<pqxx::connection> conn);
    auto build_connection_string() const -> std::string;

    pool_config config_;
    std::queue<std::shared_ptr<pqxx::connection>> available_;
    std::vector<std::shared_ptr<pqxx::connection>> all_connections_;
    mutable std::mutex mutex_;
    std::condition_variable cv_;
    bool initialized_{false};
    bool shutdown_{false};
    hb::perf::perf_stats_system* perf_stats_{nullptr};
};

} // namespace hb::database
