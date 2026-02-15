#pragma once

// database_system.h
// PostgreSQL database subsystem for game persistence

#include "core/subsystem.h"
#include "core/types.h"
#include "core/result.h"
#include "database/connection_pool.h"

#include <string>
#include <string_view>
#include <vector>
#include <optional>
#include <functional>
#include <chrono>

namespace hb::perf
{
class perf_stats_system;
}

namespace hb::database
{

// Database system configuration
struct database_config
{
    std::string host = "localhost";
    uint16_t port = 5432;
    std::string database = "helbreath";
    std::string username = "hgserver";
    std::string password;
    uint32_t pool_size = 10;
    std::chrono::milliseconds connection_timeout{5000};
    std::chrono::milliseconds query_timeout{30000};
};

// Query result wrapper for convenient access
class query_result
{
public:
    query_result() = default;
    explicit query_result(pqxx::result result);

    [[nodiscard]] auto empty() const -> bool;
    [[nodiscard]] auto size() const -> size_t;
    [[nodiscard]] auto affected_rows() const -> size_t;

    // Access rows
    [[nodiscard]] auto operator[](size_t index) const -> pqxx::row;
    [[nodiscard]] auto begin() const -> pqxx::result::const_iterator;
    [[nodiscard]] auto end() const -> pqxx::result::const_iterator;

    // Get single value
    template<typename T> [[nodiscard]] auto get(size_t row, size_t col) const -> T { return result_[static_cast<int>(row)][static_cast<int>(col)].as<T>(); }

    template<typename T> [[nodiscard]] auto get(size_t row, std::string_view col_name) const -> T
    {
        return result_[static_cast<int>(row)][std::string(col_name)].as<T>();
    }

    // Get optional value (handles NULL)
    template<typename T> [[nodiscard]] auto get_optional(size_t row, size_t col) const -> std::optional<T>
    {
        if (result_[static_cast<int>(row)][static_cast<int>(col)].is_null())
        {
            return std::nullopt;
        }
        return result_[static_cast<int>(row)][static_cast<int>(col)].as<T>();
    }

    template<typename T>
    [[nodiscard]] auto get_optional(size_t row, std::string_view col_name) const -> std::optional<T>
    {
        auto field = result_[static_cast<int>(row)][std::string(col_name)];
        if (field.is_null())
        {
            return std::nullopt;
        }
        return field.as<T>();
    }

private:
    pqxx::result result_;
};

// Database system - manages PostgreSQL connections and queries
class database_system : public subsystem
{
public:
    database_system();
    ~database_system() override;

    // Subsystem interface
    [[nodiscard]] auto name() const -> std::string_view override { return "database"; }
    void initialize() override;
    void shutdown() override;

    // Configuration
    void set_config(const database_config& config);
    [[nodiscard]] auto config() const -> const database_config& { return config_; }

    // Connection management
    [[nodiscard]] auto is_connected() const -> bool;
    [[nodiscard]] auto reconnect() -> hb::result<void, std::string>;

    // Execute raw query (INTERNAL USE ONLY - prefer execute_params for safety)
    // WARNING: This method is vulnerable to SQL injection if user input is concatenated.
    // Only use for static queries or internal operations. Use execute_params() for all
    // queries involving external data.
    [[deprecated("Use execute_params() instead to prevent SQL injection")]] [[nodiscard]] auto
    execute(std::string_view query) -> hb::result<query_result, std::string>;

    // Execute raw query - unsafe version for internal migrations/setup only
    [[nodiscard]] auto execute_unsafe(std::string_view query) -> hb::result<query_result, std::string>;

    // Execute query with parameters (prevents SQL injection)
    template<typename... Args>
    [[nodiscard]] auto execute_params(std::string_view query, Args&&... args) -> hb::result<query_result, std::string>
    {
        auto conn = pool_.acquire();
        if (conn.is_err())
        {
            return hb::result<query_result, std::string>::err(conn.error());
        }

        try
        {
            pqxx::work txn{*conn.value()};
            auto query_res = txn.exec_params(std::string(query), std::forward<Args>(args)...);
            txn.commit();
            return hb::result<query_result, std::string>::ok(query_result{std::move(query_res)});
        }
        catch (const std::exception& ex)
        {
            return hb::result<query_result, std::string>::err(std::string("Query failed: ") + ex.what());
        }
    }

    // Execute with transaction (for multiple queries)
    using transaction_func = std::function<hb::result<void, std::string>(pqxx::work&)>;
    [[nodiscard]] auto execute_transaction(transaction_func func) -> hb::result<void, std::string>;

    // Execute prepared statement
    [[nodiscard]] auto prepare(std::string_view name, std::string_view query) -> hb::result<void, std::string>;

    template<typename... Args>
    [[nodiscard]] auto execute_prepared(std::string_view name, Args&&... args) -> hb::result<query_result, std::string>
    {
        auto conn = pool_.acquire();
        if (conn.is_err())
        {
            return hb::result<query_result, std::string>::err(conn.error());
        }

        try
        {
            pqxx::work txn{*conn.value()};
            auto query_res = txn.exec_prepared(std::string(name), std::forward<Args>(args)...);
            txn.commit();
            return hb::result<query_result, std::string>::ok(query_result{std::move(query_res)});
        }
        catch (const std::exception& ex)
        {
            return hb::result<query_result, std::string>::err(std::string("Prepared query failed: ") + ex.what());
        }
    }

    // Utility methods
    [[nodiscard]] auto quote(std::string_view str) -> std::string;
    [[nodiscard]] auto quote_name(std::string_view name) -> std::string;

    // Pool statistics
    [[nodiscard]] auto available_connections() const -> size_t;
    [[nodiscard]] auto total_connections() const -> size_t;

    // Get raw pool access (for advanced use)
    [[nodiscard]] auto pool() -> connection_pool& { return pool_; }

    // Performance statistics
    void set_perf_stats(hb::perf::perf_stats_system* perf) { perf_stats_ = perf; }

private:
    database_config config_;
    connection_pool pool_;
    hb::perf::perf_stats_system* perf_stats_{nullptr};
};

} // namespace hb::database
