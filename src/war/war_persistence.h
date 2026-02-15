#pragma once

// war_persistence.h
// Database operations for war history and participant rewards

#include "core/types.h"
#include "core/result.h"
#include "war/war_types.h"

#include <nlohmann/json.hpp>
#include <string>
#include <vector>
#include <optional>

namespace hb::database
{
class database_system;
}

namespace hb::war
{

// Stored war history row
struct war_history_row
{
    int32_t id{0};
    war_type type{war_type::crusade};
    std::string started_at;
    std::string ended_at;
    int32_t duration_seconds{0};
    war_faction winner{war_faction::neutral};
    int32_t aresden_score{0};
    int32_t elvine_score{0};
    nlohmann::json metadata;
};

// Stored participant row
struct war_participant_row
{
    int32_t id{0};
    int32_t war_id{0};
    int32_t character_id{0};
    war_faction faction{war_faction::neutral};
    uint8_t duty{0};
    int32_t kills{0};
    int32_t deaths{0};
    int32_t assists{0};
    int32_t damage_dealt{0};
    int32_t healing_done{0};
    int32_t contribution{0};
    int64_t reward_exp{0};
    int32_t reward_gold{0};
    int32_t reward_contribution{0};
    bool reward_claimed{false};
};

// War persistence operations
class war_persistence
{
public:
    explicit war_persistence(database::database_system* db);

    // Save a completed war to history, returns the DB id
    auto save_war_result(const war_result& result) -> hb::result<int32_t, std::string>;

    // Save participant stats for a war (war_db_id from save_war_result)
    auto save_participant(int32_t war_db_id,
                          int32_t character_id,
                          const war_participant& participant,
                          uint8_t duty,
                          const war_rewards& rewards) -> hb::result<void, std::string>;

    // Load war history (most recent first)
    auto load_war_history(int32_t limit = 20,
                          int32_t offset = 0) -> hb::result<std::vector<war_history_row>, std::string>;

    // Load war history filtered by type
    auto load_war_history_by_type(war_type type,
                                  int32_t limit = 20,
                                  int32_t offset = 0) -> hb::result<std::vector<war_history_row>, std::string>;

    // Load participants for a specific war
    auto load_war_participants(int32_t war_db_id) -> hb::result<std::vector<war_participant_row>, std::string>;

    // Count total wars
    auto count_wars() -> hb::result<int32_t, std::string>;

    // Count wars by type
    auto count_wars_by_type(war_type type) -> hb::result<int32_t, std::string>;

    // Load the winner of the most recent completed war of a given type
    auto load_last_winner(war_type type) -> hb::result<war_faction, std::string>;

    // Load crusade advantage from most recent crusade war history metadata
    auto load_crusade_advantage() -> hb::result<int8_t, std::string>;

    // Get unclaimed rewards for a character (for deferred reward delivery at login)
    auto get_unclaimed_rewards(int32_t character_id) -> hb::result<std::vector<war_participant_row>, std::string>;

    // Mark specific participant rewards as claimed
    auto mark_rewards_claimed(int32_t participant_id) -> hb::result<void, std::string>;

    // Save participant with explicit reward_claimed flag
    auto save_participant_with_claimed(int32_t war_db_id,
                                       int32_t character_id,
                                       const war_participant& participant,
                                       uint8_t duty,
                                       const war_rewards& rewards,
                                       bool claimed) -> hb::result<void, std::string>;

private:
    database::database_system* db_{nullptr};
};

} // namespace hb::war
