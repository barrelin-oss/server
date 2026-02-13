// war_persistence.cpp
// Database operations for war history and participant rewards

#include "war/war_persistence.h"
#include "database/database_system.h"
#include "core/logger.h"

#include <nlohmann/json.hpp>
#include <chrono>
#include <ctime>
#include <iomanip>
#include <sstream>

namespace hb::war {

war_persistence::war_persistence(database::database_system* db)
    : db_(db)
{
}

auto war_persistence::save_war_result(const war_result& result) -> hb::result<int32_t, std::string>
{
    if (!db_) return hb::result<int32_t, std::string>::err("No database system");

    // Format timestamps as ISO 8601
    auto format_time = [](std::chrono::system_clock::time_point tp) -> std::string {
        auto time_t_val = std::chrono::system_clock::to_time_t(tp);
        std::tm tm_val{};
#ifdef _WIN32
        gmtime_s(&tm_val, &time_t_val);
#else
        gmtime_r(&time_t_val, &tm_val);
#endif
        std::ostringstream oss;
        oss << std::put_time(&tm_val, "%Y-%m-%dT%H:%M:%S+00:00");
        return oss.str();
    };

    auto started_str = format_time(result.started_at);
    auto ended_str = format_time(result.ended_at);

    nlohmann::json metadata;
    metadata["draw"] = result.draw;

    auto query_result = db_->execute_params(
        "INSERT INTO war_history (war_type, started_at, ended_at, duration_seconds, "
        "winner_faction, aresden_score, elvine_score, metadata) "
        "VALUES ($1, $2, $3, $4, $5, $6, $7, $8) "
        "RETURNING id",
        static_cast<int16_t>(result.type),
        started_str,
        ended_str,
        result.duration_seconds,
        static_cast<int16_t>(result.winner),
        result.aresden_score.total_score,
        result.elvine_score.total_score,
        metadata.dump()
    );

    if (query_result.is_err())
    {
        LOG_ERROR(general, "Failed to save war result: {}", query_result.error());
        return hb::result<int32_t, std::string>::err(query_result.error());
    }

    auto db_id = query_result.value().get<int32_t>(0, 0);
    LOG_INFO(general, "Saved war result id={} type={} winner={}",
        db_id, static_cast<int>(result.type), static_cast<int>(result.winner));

    return hb::result<int32_t, std::string>::ok(db_id);
}

auto war_persistence::save_participant(int32_t war_db_id,
                                       int32_t character_id,
                                       const war_participant& participant,
                                       uint8_t duty,
                                       const war_rewards& rewards)
    -> hb::result<void, std::string>
{
    if (!db_) return hb::result<void, std::string>::err("No database system");

    auto query_result = db_->execute_params(
        "INSERT INTO war_participants (war_id, character_id, faction, duty, "
        "kills, deaths, assists, damage_dealt, healing_done, contribution, "
        "reward_exp, reward_gold, reward_contribution, reward_claimed) "
        "VALUES ($1, $2, $3, $4, $5, $6, $7, $8, $9, $10, $11, $12, $13, $14) "
        "ON CONFLICT (war_id, character_id) DO NOTHING",
        war_db_id,
        character_id,
        static_cast<int16_t>(participant.faction),
        static_cast<int16_t>(duty),
        participant.kills,
        participant.deaths,
        participant.assists,
        participant.damage_dealt,
        participant.healing_done,
        participant.contribution_score,
        rewards.experience,
        static_cast<int32_t>(rewards.gold),
        rewards.contribution_points,
        false
    );

    if (query_result.is_err())
    {
        LOG_ERROR(general, "Failed to save war participant: {}", query_result.error());
        return hb::result<void, std::string>::err(query_result.error());
    }

    return hb::result<void, std::string>::ok();
}

auto war_persistence::load_war_history(int32_t limit, int32_t offset)
    -> hb::result<std::vector<war_history_row>, std::string>
{
    if (!db_) return hb::result<std::vector<war_history_row>, std::string>::err("No database system");

    auto query_result = db_->execute_params(
        "SELECT id, war_type, started_at, ended_at, duration_seconds, "
        "winner_faction, aresden_score, elvine_score, metadata "
        "FROM war_history ORDER BY started_at DESC LIMIT $1 OFFSET $2",
        limit, offset
    );

    if (query_result.is_err())
    {
        return hb::result<std::vector<war_history_row>, std::string>::err(query_result.error());
    }

    std::vector<war_history_row> rows;
    for (const auto& row : query_result.value())
    {
        war_history_row r;
        r.id = row["id"].as<int32_t>();
        r.type = static_cast<war_type>(row["war_type"].as<int16_t>());
        r.started_at = row["started_at"].as<std::string>();
        r.ended_at = row["ended_at"].as<std::string>();
        r.duration_seconds = row["duration_seconds"].as<int32_t>();
        r.winner = static_cast<war_faction>(row["winner_faction"].as<int16_t>());
        r.aresden_score = row["aresden_score"].as<int32_t>();
        r.elvine_score = row["elvine_score"].as<int32_t>();
        auto meta_str = row["metadata"].as<std::string>();
        r.metadata = nlohmann::json::parse(meta_str, nullptr, false);
        rows.push_back(std::move(r));
    }

    return hb::result<std::vector<war_history_row>, std::string>::ok(std::move(rows));
}

auto war_persistence::load_war_history_by_type(war_type type, int32_t limit, int32_t offset)
    -> hb::result<std::vector<war_history_row>, std::string>
{
    if (!db_) return hb::result<std::vector<war_history_row>, std::string>::err("No database system");

    auto query_result = db_->execute_params(
        "SELECT id, war_type, started_at, ended_at, duration_seconds, "
        "winner_faction, aresden_score, elvine_score, metadata "
        "FROM war_history WHERE war_type = $1 ORDER BY started_at DESC LIMIT $2 OFFSET $3",
        static_cast<int16_t>(type), limit, offset
    );

    if (query_result.is_err())
    {
        return hb::result<std::vector<war_history_row>, std::string>::err(query_result.error());
    }

    std::vector<war_history_row> rows;
    for (const auto& row : query_result.value())
    {
        war_history_row r;
        r.id = row["id"].as<int32_t>();
        r.type = static_cast<war_type>(row["war_type"].as<int16_t>());
        r.started_at = row["started_at"].as<std::string>();
        r.ended_at = row["ended_at"].as<std::string>();
        r.duration_seconds = row["duration_seconds"].as<int32_t>();
        r.winner = static_cast<war_faction>(row["winner_faction"].as<int16_t>());
        r.aresden_score = row["aresden_score"].as<int32_t>();
        r.elvine_score = row["elvine_score"].as<int32_t>();
        auto meta_str = row["metadata"].as<std::string>();
        r.metadata = nlohmann::json::parse(meta_str, nullptr, false);
        rows.push_back(std::move(r));
    }

    return hb::result<std::vector<war_history_row>, std::string>::ok(std::move(rows));
}

auto war_persistence::load_war_participants(int32_t war_db_id)
    -> hb::result<std::vector<war_participant_row>, std::string>
{
    if (!db_) return hb::result<std::vector<war_participant_row>, std::string>::err("No database system");

    auto query_result = db_->execute_params(
        "SELECT wp.id, wp.war_id, wp.character_id, wp.faction, wp.duty, "
        "wp.kills, wp.deaths, wp.assists, wp.damage_dealt, wp.healing_done, "
        "wp.contribution, wp.reward_exp, wp.reward_gold, wp.reward_contribution, "
        "wp.reward_claimed "
        "FROM war_participants wp "
        "WHERE wp.war_id = $1 ORDER BY wp.contribution DESC",
        war_db_id
    );

    if (query_result.is_err())
    {
        return hb::result<std::vector<war_participant_row>, std::string>::err(query_result.error());
    }

    std::vector<war_participant_row> rows;
    for (const auto& row : query_result.value())
    {
        war_participant_row r;
        r.id = row["id"].as<int32_t>();
        r.war_id = row["war_id"].as<int32_t>();
        r.character_id = row["character_id"].as<int32_t>();
        r.faction = static_cast<war_faction>(row["faction"].as<int16_t>());
        r.duty = static_cast<uint8_t>(row["duty"].as<int16_t>());
        r.kills = row["kills"].as<int32_t>();
        r.deaths = row["deaths"].as<int32_t>();
        r.assists = row["assists"].as<int32_t>();
        r.damage_dealt = row["damage_dealt"].as<int32_t>();
        r.healing_done = row["healing_done"].as<int32_t>();
        r.contribution = row["contribution"].as<int32_t>();
        r.reward_exp = row["reward_exp"].as<int64_t>();
        r.reward_gold = row["reward_gold"].as<int32_t>();
        r.reward_contribution = row["reward_contribution"].as<int32_t>();
        r.reward_claimed = row["reward_claimed"].as<bool>();
        rows.push_back(std::move(r));
    }

    return hb::result<std::vector<war_participant_row>, std::string>::ok(std::move(rows));
}

auto war_persistence::count_wars() -> hb::result<int32_t, std::string>
{
    if (!db_) return hb::result<int32_t, std::string>::err("No database system");

    auto query_result = db_->execute_params("SELECT COUNT(*) FROM war_history");
    if (query_result.is_err())
    {
        return hb::result<int32_t, std::string>::err(query_result.error());
    }

    return hb::result<int32_t, std::string>::ok(query_result.value().get<int32_t>(0, 0));
}

auto war_persistence::count_wars_by_type(war_type type) -> hb::result<int32_t, std::string>
{
    if (!db_) return hb::result<int32_t, std::string>::err("No database system");

    auto query_result = db_->execute_params(
        "SELECT COUNT(*) FROM war_history WHERE war_type = $1",
        static_cast<int16_t>(type)
    );

    if (query_result.is_err())
    {
        return hb::result<int32_t, std::string>::err(query_result.error());
    }

    return hb::result<int32_t, std::string>::ok(query_result.value().get<int32_t>(0, 0));
}

auto war_persistence::get_unclaimed_rewards(int32_t character_id)
    -> hb::result<std::vector<war_participant_row>, std::string>
{
    if (!db_) return hb::result<std::vector<war_participant_row>, std::string>::err("No database system");

    auto query_result = db_->execute_params(
        "SELECT wp.id, wp.war_id, wp.character_id, wp.faction, wp.duty, "
        "wp.kills, wp.deaths, wp.assists, wp.damage_dealt, wp.healing_done, "
        "wp.contribution, wp.reward_exp, wp.reward_gold, wp.reward_contribution, "
        "wp.reward_claimed "
        "FROM war_participants wp "
        "WHERE wp.character_id = $1 AND wp.reward_claimed = FALSE "
        "ORDER BY wp.id ASC",
        character_id
    );

    if (query_result.is_err())
    {
        return hb::result<std::vector<war_participant_row>, std::string>::err(query_result.error());
    }

    std::vector<war_participant_row> rows;
    for (const auto& row : query_result.value())
    {
        war_participant_row r;
        r.id = row["id"].as<int32_t>();
        r.war_id = row["war_id"].as<int32_t>();
        r.character_id = row["character_id"].as<int32_t>();
        r.faction = static_cast<war_faction>(row["faction"].as<int16_t>());
        r.duty = static_cast<uint8_t>(row["duty"].as<int16_t>());
        r.kills = row["kills"].as<int32_t>();
        r.deaths = row["deaths"].as<int32_t>();
        r.assists = row["assists"].as<int32_t>();
        r.damage_dealt = row["damage_dealt"].as<int32_t>();
        r.healing_done = row["healing_done"].as<int32_t>();
        r.contribution = row["contribution"].as<int32_t>();
        r.reward_exp = row["reward_exp"].as<int64_t>();
        r.reward_gold = row["reward_gold"].as<int32_t>();
        r.reward_contribution = row["reward_contribution"].as<int32_t>();
        r.reward_claimed = row["reward_claimed"].as<bool>();
        rows.push_back(std::move(r));
    }

    return hb::result<std::vector<war_participant_row>, std::string>::ok(std::move(rows));
}

auto war_persistence::mark_rewards_claimed(int32_t participant_id)
    -> hb::result<void, std::string>
{
    if (!db_) return hb::result<void, std::string>::err("No database system");

    auto query_result = db_->execute_params(
        "UPDATE war_participants SET reward_claimed = TRUE WHERE id = $1",
        participant_id
    );

    if (query_result.is_err())
    {
        return hb::result<void, std::string>::err(query_result.error());
    }

    return hb::result<void, std::string>::ok();
}

auto war_persistence::save_participant_with_claimed(int32_t war_db_id,
                                                     int32_t character_id,
                                                     const war_participant& participant,
                                                     uint8_t duty,
                                                     const war_rewards& rewards,
                                                     bool claimed)
    -> hb::result<void, std::string>
{
    if (!db_) return hb::result<void, std::string>::err("No database system");

    auto query_result = db_->execute_params(
        "INSERT INTO war_participants (war_id, character_id, faction, duty, "
        "kills, deaths, assists, damage_dealt, healing_done, contribution, "
        "reward_exp, reward_gold, reward_contribution, reward_claimed) "
        "VALUES ($1, $2, $3, $4, $5, $6, $7, $8, $9, $10, $11, $12, $13, $14) "
        "ON CONFLICT (war_id, character_id) DO NOTHING",
        war_db_id,
        character_id,
        static_cast<int16_t>(participant.faction),
        static_cast<int16_t>(duty),
        participant.kills,
        participant.deaths,
        participant.assists,
        participant.damage_dealt,
        participant.healing_done,
        participant.contribution_score,
        rewards.experience,
        static_cast<int32_t>(rewards.gold),
        rewards.contribution_points,
        claimed
    );

    if (query_result.is_err())
    {
        LOG_ERROR(general, "Failed to save war participant: {}", query_result.error());
        return hb::result<void, std::string>::err(query_result.error());
    }

    return hb::result<void, std::string>::ok();
}

}  // namespace hb::war
