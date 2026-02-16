#pragma once

// item_audit_system.h
// Buffered audit logging for item and gold transactions

#include "core/subsystem.h"
#include "core/enums.h"

#include <nlohmann/json.hpp>

#include <cstdint>
#include <mutex>
#include <string>
#include <string_view>
#include <vector>

namespace hb::database
{
class database_system;
}

namespace hb::audit
{

// A single audit log entry (buffered in memory before flush)
struct item_audit_entry
{
    int32_t character_id{0};
    std::string item_name;
    int32_t item_instance_id{0};
    item_log_type action{};
    int32_t quantity{1};
    int32_t gold_amount{0};
    int32_t other_char_id{0};
    std::string map_name;
    int16_t pos_x{0};
    int16_t pos_y{0};
    nlohmann::json details;
};

// Query filters for admin API
struct item_log_query
{
    int32_t character_id{0}; // 0 = any
    std::string item_name;   // empty = any
    int32_t action_type{0};  // 0 = any
    int32_t limit{50};
    int32_t offset{0};
};

// Query result row
struct item_log_result
{
    int32_t id{0};
    int32_t character_id{0};
    std::string character_name;
    std::string item_name;
    int32_t item_id{0};
    int32_t action_type{0};
    int32_t quantity{0};
    int32_t gold_amount{0};
    int32_t other_char_id{0};
    std::string other_char_name;
    std::string map_name;
    int16_t pos_x{0};
    int16_t pos_y{0};
    std::string timestamp;
    nlohmann::json details;
};

class item_audit_system : public subsystem
{
public:
    item_audit_system() = default;

    // Subsystem interface
    [[nodiscard]] auto name() const -> std::string_view override { return "item_audit"; }
    void initialize() override;
    void shutdown() override;

    // Dependencies
    void set_database(database::database_system* db) { db_ = db; }

    // Log an item transaction (caller should check item.audited first)
    void log_item(int32_t character_id,
                  std::string_view item_name,
                  int32_t item_instance_id,
                  item_log_type action,
                  int32_t quantity,
                  int32_t other_char_id = 0,
                  std::string_view map_name = {},
                  int16_t pos_x = 0,
                  int16_t pos_y = 0,
                  const nlohmann::json& details = {});

    // Log a gold transaction (always logs, no audit flag check)
    void log_gold(int32_t character_id,
                  item_log_type action,
                  int32_t gold_amount,
                  int32_t other_char_id = 0,
                  std::string_view map_name = {},
                  int16_t pos_x = 0,
                  int16_t pos_y = 0,
                  const nlohmann::json& details = {});

    // Flush buffered entries to database
    void flush();

    // Query logs (for admin API)
    auto query_logs(const item_log_query& query) -> std::vector<item_log_result>;

    // Buffer size (for testing)
    [[nodiscard]] auto buffer_size() const -> size_t;

private:
    static constexpr size_t flush_threshold = 50;
    static constexpr size_t max_buffer_size = 5000;

    database::database_system* db_{nullptr};

    mutable std::mutex buffer_mutex_;
    std::vector<item_audit_entry> buffer_;

    void add_entry(item_audit_entry entry);
    void flush_locked(std::vector<item_audit_entry>& entries);
};

} // namespace hb::audit
