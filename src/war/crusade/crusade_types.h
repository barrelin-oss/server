#pragma once

// crusade_types.h
// Type definitions for the Crusade warfare system

#include "core/types.h"
#include "entity/entity.h"
#include "war/war_types.h"

#include <string>
#include <vector>
#include <chrono>
#include <cstdint>

namespace hb::war
{

// Crusade duty roles
enum class crusade_duty : uint8_t
{
    none = 0,
    fighter = 1,     // Earns construction points through combat
    constructor = 2, // Summons war structures using pooled points
    commander = 3,   // Receives points from guild fighters, sees map status
};

// Crusade schedule entry — defines when a crusade can start
struct crusade_schedule_entry
{
    uint8_t day_of_week{0}; // 0=Sunday, 6=Saturday
    uint8_t hour{0};
    uint8_t minute{0};
};

// Strike point — destructible faction target
struct strike_point
{
    uint16_t id{0};
    std::string map_name;
    int16_t x{0};
    int16_t y{0};
    int32_t hp{0};
    int32_t max_hp{100};
    war_faction faction{war_faction::neutral};
    std::string linked_map; // Map disabled when this point is destroyed (legacy: m_bIsDisabled)

    [[nodiscard]] auto is_destroyed() const -> bool { return hp <= 0; }
};

// Per-player crusade state (stored in crusade_system, not on player struct)
struct crusade_player_data
{
    player_id pid{};
    war_faction faction{war_faction::neutral};
    crusade_duty duty{crusade_duty::none};
    int32_t construction_points{0};
    int32_t war_contribution{0};
    uint64_t crusade_guid{0};
    int32_t character_id{0}; // DB character ID, captured at join time for offline reward persistence
    int32_t level{1};        // Player level, captured at join time for offline reward calculation
};

// Crusade timing configuration
struct crusade_timing_config
{
    int32_t preparation_seconds{300};     // 5 minutes prep phase
    int32_t duration_seconds{5400};       // 90 minutes max
    int32_t ending_seconds{60};           // 1 minute ending phase
    int32_t status_broadcast_seconds{30}; // Periodic status update interval
};

// War structure types that can be summoned by constructors
enum class war_unit_type : uint8_t
{
    agt = 1,            // Archer Guard Tower (NPC 36)
    cgt = 2,            // Cannon Guard Tower (NPC 37)
    mana_collector = 3, // Collects mana stones for meteor (NPC 38)
    detector = 4,       // Reveals invisible players (NPC 39)
    lwb = 5,            // Light War Beetle (NPC 43)
    catapult = 6,       // Siege weapon (NPC 51)
    esg = 7,            // Energy Shield Generator (NPC 40) — pre-placed only
    ghk = 8,            // Guard House Knight (NPC 44)
    ghkabs = 9,         // Guard House Knight Absolute (NPC 45)
    tk = 10,            // Tower Knight (NPC 46)
    bg = 11,            // Battle Golem (NPC 47)
};

// Construction cost for a war unit
struct construction_cost
{
    war_unit_type type{};
    int32_t cost{0};
    uint16_t npc_type_id{0}; // NPC type to spawn
};

// Construction costs — structures are free (location-restricted), mobile units have explicit costs
inline auto get_construction_cost(war_unit_type type) -> int32_t
{
    switch (type)
    {
    // Structures: zero cost (restricted by guild construct location)
    case war_unit_type::agt:
        return 0;
    case war_unit_type::cgt:
        return 0;
    case war_unit_type::mana_collector:
        return 0;
    case war_unit_type::detector:
        return 0;

    // Mobile units: legacy costs
    case war_unit_type::lwb:
        return 1000;
    case war_unit_type::ghk:
        return 2000;
    case war_unit_type::ghkabs:
        return 3000;
    case war_unit_type::tk:
        return 2000;
    case war_unit_type::bg:
        return 3000;
    case war_unit_type::catapult:
        return 1500;

    // ESG: pre-placed only, not player-summonable
    case war_unit_type::esg:
        return -1;
    }
    return -1;
}

// Tracks a spawned war structure during crusade
struct war_structure_instance
{
    entity::entity eid{};
    war_unit_type type{};
    war_faction faction{war_faction::neutral};
    player_id summoner{};
    std::string map_name;
    int16_t x{0};
    int16_t y{0};
};

// Configuration for construction point earning
struct construction_point_config
{
    int32_t commander_bonus_points{3000};    // Bonus for previous-winner guild commander
    float transfer_contribution_ratio{0.1f}; // 1/10 of transferred points become contribution
    int32_t transfer_interval_seconds{30};   // How often auto-transfer runs
};

// Pre-placed structure spawned at crusade start from config
struct initial_structure
{
    std::string map_name;
    uint16_t npc_type{0}; // NPC type ID: 36=AGT, 37=CGT, 40=ESG, 41=GMG, 42=ManaStone
    int16_t x{0};
    int16_t y{0};
    war_faction faction{war_faction::neutral};
};

// Guild construct location — where a guild's commander has designated building
struct guild_construct_location
{
    uint32_t guild_id{0};
    std::string map_name;
    int16_t x{0};
    int16_t y{0};
    int32_t structure_count{0};
};

// Crusade configuration
struct crusade_config
{
    bool enabled{true};
    crusade_timing_config timing;
    construction_point_config construction;
    std::vector<crusade_schedule_entry> schedule;
    std::vector<strike_point> aresden_strike_points;
    std::vector<strike_point> elvine_strike_points;
    std::vector<std::string> war_maps;                 // Map names involved in crusade
    int32_t mana_stone_count{10};                      // Pre-placed mana stones on war maps
    std::vector<initial_structure> initial_structures; // Pre-placed structures at war start
};

// Guild teleport location — commander-set warp destination for guild members during crusade
struct guild_teleport_location
{
    uint32_t guild_id{0};
    std::string map_name;
    int16_t x{0};
    int16_t y{0};
};

// Build system constants
inline constexpr int32_t max_constructions_per_guild = 10;
inline constexpr int32_t construct_location_radius = 10;
inline constexpr int32_t guard_tower_min_distance = 2;
inline constexpr int16_t guard_tower_min_y = 33;
inline constexpr int16_t guard_tower_max_y = 782;

// Cap constants
inline constexpr int32_t max_construction_points = 30000;
inline constexpr int32_t max_war_contribution = 200000;

// Crusade reward calculation result
struct crusade_reward
{
    int64_t experience{0};
    int32_t war_contribution_used{0};
    bool is_winner{false};
    bool is_draw{false};
};

// Calculate crusade reward using legacy formula.
// Level bonus is added to contribution before EXP calculation.
// Winner: full adjusted contribution. Draw: 1/6. Loser: 1/10.
inline auto calculate_crusade_reward(int32_t war_contribution,
                                     int32_t player_level,
                                     war_faction player_faction,
                                     war_faction winner) -> crusade_reward
{
    crusade_reward reward;
    reward.war_contribution_used = war_contribution;

    int64_t contribution = war_contribution;

    // Level-based bonus (legacy formula)
    if (player_level <= 80)
        contribution += static_cast<int64_t>(player_level) * 100;
    else if (player_level <= 100)
        contribution += static_cast<int64_t>(player_level) * 40;
    else
        contribution += static_cast<int64_t>(player_level) * 1;

    if (winner == war_faction::neutral)
    {
        // Draw: 1/6 of adjusted contribution
        reward.experience = contribution / 6;
        reward.is_draw = true;
    }
    else if (winner == player_faction)
    {
        // Winner: full adjusted contribution
        reward.experience = contribution;
        reward.is_winner = true;
    }
    else
    {
        // Loser: 1/10 of adjusted contribution
        reward.experience = contribution / 10;
    }

    return reward;
}

// Map war_unit_type + faction to sequential NPC IDs (1-indexed YAML position).
// Faction-specific units have separate Aresden/Elvine variants.
// Shared units (ghk, ghkabs, tk, bg) use the same NPC ID for both factions.
inline auto get_npc_id_for_unit(war_unit_type type, war_faction faction) -> uint16_t
{
    bool is_elvine = (faction == war_faction::elvine);
    switch (type)
    {
    case war_unit_type::agt:
        return is_elvine ? 65 : 64;
    case war_unit_type::cgt:
        return is_elvine ? 67 : 66;
    case war_unit_type::mana_collector:
        return is_elvine ? 69 : 68;
    case war_unit_type::detector:
        return is_elvine ? 71 : 70;
    case war_unit_type::esg:
        return is_elvine ? 73 : 72;
    case war_unit_type::lwb:
        return is_elvine ? 78 : 77;
    case war_unit_type::ghk:
        return 79;
    case war_unit_type::ghkabs:
        return 80;
    case war_unit_type::tk:
        return 81;
    case war_unit_type::bg:
        return 82;
    case war_unit_type::catapult:
        return is_elvine ? 86 : 85;
    }
    return 0;
}

// Helper to classify unit types
inline auto is_structure_type(war_unit_type type) -> bool
{
    return type == war_unit_type::agt || type == war_unit_type::cgt || type == war_unit_type::mana_collector ||
           type == war_unit_type::detector;
}

inline auto is_mobile_unit_type(war_unit_type type) -> bool
{
    return type == war_unit_type::lwb || type == war_unit_type::ghk || type == war_unit_type::ghkabs ||
           type == war_unit_type::tk || type == war_unit_type::bg || type == war_unit_type::catapult;
}

inline auto is_guard_tower_type(war_unit_type type) -> bool
{
    return type == war_unit_type::agt || type == war_unit_type::cgt;
}

} // namespace hb::war
