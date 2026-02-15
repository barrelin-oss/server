#include "audit/item_audit_system.h"
#include "database/database_system.h"
#include "core/logger.h"

#include <pqxx/pqxx>

#include <sstream>

namespace hb::audit
{

void item_audit_system::initialize()
{
    LOG_INFO(general, "Item audit system initializing...");
    set_initialized(true);
    LOG_INFO(general, "Item audit system initialized");
}

void item_audit_system::shutdown()
{
    LOG_INFO(general, "Item audit system shutting down...");
    flush();
    set_initialized(false);
    LOG_INFO(general, "Item audit system shutdown complete");
}

void item_audit_system::log_item(int32_t character_id,
                                 std::string_view item_name,
                                 int32_t item_instance_id,
                                 item_log_type action,
                                 int32_t quantity,
                                 int32_t other_char_id,
                                 std::string_view map_name,
                                 int16_t pos_x,
                                 int16_t pos_y,
                                 const nlohmann::json& details)
{
    item_audit_entry entry;
    entry.character_id = character_id;
    entry.item_name = std::string(item_name);
    entry.item_instance_id = item_instance_id;
    entry.action = action;
    entry.quantity = quantity;
    entry.gold_amount = 0;
    entry.other_char_id = other_char_id;
    entry.map_name = std::string(map_name);
    entry.pos_x = pos_x;
    entry.pos_y = pos_y;
    entry.details = details;
    add_entry(std::move(entry));
}

void item_audit_system::log_gold(int32_t character_id,
                                 item_log_type action,
                                 int32_t gold_amount,
                                 int32_t other_char_id,
                                 std::string_view map_name,
                                 int16_t pos_x,
                                 int16_t pos_y,
                                 const nlohmann::json& details)
{
    item_audit_entry entry;
    entry.character_id = character_id;
    entry.item_name = "gold";
    entry.item_instance_id = 0;
    entry.action = action;
    entry.quantity = 0;
    entry.gold_amount = gold_amount;
    entry.other_char_id = other_char_id;
    entry.map_name = std::string(map_name);
    entry.pos_x = pos_x;
    entry.pos_y = pos_y;
    entry.details = details;
    add_entry(std::move(entry));
}

void item_audit_system::add_entry(item_audit_entry entry)
{
    std::vector<item_audit_entry> to_flush;

    {
        std::lock_guard<std::mutex> lock(buffer_mutex_);
        buffer_.push_back(std::move(entry));

        if (buffer_.size() >= flush_threshold)
        {
            to_flush = std::move(buffer_);
            buffer_.clear();
        }
    }

    if (!to_flush.empty())
    {
        flush_locked(to_flush);
    }
}

void item_audit_system::flush()
{
    std::vector<item_audit_entry> to_flush;

    {
        std::lock_guard<std::mutex> lock(buffer_mutex_);
        if (buffer_.empty())
            return;
        to_flush = std::move(buffer_);
        buffer_.clear();
    }

    flush_locked(to_flush);
}

void item_audit_system::flush_locked(std::vector<item_audit_entry>& entries)
{
    if (!db_ || entries.empty())
        return;

    auto result = db_->execute_transaction(
        [&](pqxx::work& txn) -> hb::result<void, std::string>
        {
            for (const auto& e : entries)
            {
                std::string details_str = e.details.is_null() ? "{}" : e.details.dump();

                txn.exec_params(
                    "INSERT INTO item_log (character_id, item_name, item_id, action_type, "
                    "quantity, other_char_id, gold_amount, map_name, pos_x, pos_y, details) "
                    "VALUES ($1, $2, $3, $4, $5, $6, $7, $8, $9, $10, $11::jsonb)",
                    e.character_id,
                    e.item_name,
                    e.item_instance_id,
                    static_cast<int16_t>(e.action),
                    e.quantity,
                    e.other_char_id == 0 ? std::optional<int32_t>{} : std::optional<int32_t>{e.other_char_id},
                    e.gold_amount,
                    e.map_name.empty() ? std::optional<std::string>{} : std::optional<std::string>{e.map_name},
                    e.pos_x,
                    e.pos_y,
                    details_str);
            }

            return hb::result<void, std::string>::ok();
        });

    if (result.is_err())
    {
        LOG_ERROR(general, "Failed to flush {} item audit entries: {}", entries.size(), result.error());
    }
    else
    {
        LOG_DEBUG(general, "Flushed {} item audit entries", entries.size());
    }
}

auto item_audit_system::query_logs(const item_log_query& query) -> std::vector<item_log_result>
{
    if (!db_)
        return {};

    // Build parameterized query with optional filters
    std::ostringstream sql;
    sql << "SELECT il.id, il.character_id, COALESCE(c1.name, '') AS character_name, "
        << "il.item_name, il.item_id, il.action_type, il.quantity, " << "COALESCE(il.gold_amount, 0) AS gold_amount, "
        << "il.other_char_id, COALESCE(c2.name, '') AS other_char_name, " << "COALESCE(il.map_name, '') AS map_name, "
        << "COALESCE(il.pos_x, 0) AS pos_x, COALESCE(il.pos_y, 0) AS pos_y, "
        << "il.timestamp, COALESCE(il.details, '{}'::jsonb) AS details " << "FROM item_log il "
        << "LEFT JOIN characters c1 ON il.character_id = c1.id "
        << "LEFT JOIN characters c2 ON il.other_char_id = c2.id " << "WHERE 1=1 ";

    std::vector<std::string> params;
    int param_idx = 1;

    if (query.character_id > 0)
    {
        sql << "AND il.character_id = $" << param_idx++ << " ";
        params.push_back(std::to_string(query.character_id));
    }

    if (!query.item_name.empty())
    {
        sql << "AND il.item_name ILIKE $" << param_idx++ << " ";
        params.push_back("%" + query.item_name + "%");
    }

    if (query.action_type > 0)
    {
        sql << "AND il.action_type = $" << param_idx++ << " ";
        params.push_back(std::to_string(query.action_type));
    }

    sql << "ORDER BY il.timestamp DESC ";

    int32_t limit = std::clamp(query.limit, 1, 500);
    int32_t offset = std::max(query.offset, 0);
    int limit_param = param_idx++;
    int offset_param = param_idx++;
    sql << "LIMIT $" << limit_param << " OFFSET $" << offset_param << " ";
    params.push_back(std::to_string(limit));
    params.push_back(std::to_string(offset));

    // Execute with dynamic params using exec_params0 through the transaction interface
    std::vector<item_log_result> results;

    auto txn_result = db_->execute_transaction(
        [&](pqxx::work& txn) -> hb::result<void, std::string>
        {
            // Build a params string for pqxx
            pqxx::params p;
            for (const auto& param : params)
            {
                p.append(param);
            }

            auto rows = txn.exec_params(sql.str(), p);

            for (const auto& row : rows)
            {
                item_log_result r;
                r.id = row[0].as<int32_t>();
                r.character_id = row[1].is_null() ? 0 : row[1].as<int32_t>();
                r.character_name = row[2].as<std::string>();
                r.item_name = row[3].as<std::string>();
                r.item_id = row[4].is_null() ? 0 : row[4].as<int32_t>();
                r.action_type = row[5].as<int32_t>();
                r.quantity = row[6].is_null() ? 0 : row[6].as<int32_t>();
                r.gold_amount = row[7].as<int32_t>();
                r.other_char_id = row[8].is_null() ? 0 : row[8].as<int32_t>();
                r.other_char_name = row[9].as<std::string>();
                r.map_name = row[10].as<std::string>();
                r.pos_x = static_cast<int16_t>(row[11].as<int32_t>());
                r.pos_y = static_cast<int16_t>(row[12].as<int32_t>());
                r.timestamp = row[13].as<std::string>();
                r.details = nlohmann::json::parse(row[14].as<std::string>(), nullptr, false);
                results.push_back(std::move(r));
            }

            return hb::result<void, std::string>::ok();
        });

    if (txn_result.is_err())
    {
        LOG_ERROR(general, "Failed to query item audit logs: {}", txn_result.error());
    }

    return results;
}

auto item_audit_system::buffer_size() const -> size_t
{
    std::lock_guard<std::mutex> lock(buffer_mutex_);
    return buffer_.size();
}

} // namespace hb::audit
