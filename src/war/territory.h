#pragma once

// territory.h
// Territory definitions for faction warfare

#include "core/types.h"
#include "war/war_types.h"

#include <string>
#include <vector>
#include <chrono>

namespace hb::war
{

// Territory type
enum class territory_type : uint8_t
{
    town = 0,     // Main town/city
    outpost = 1,  // Small strategic point
    resource = 2, // Resource gathering area
    dungeon = 3,  // Dungeon entrance
    castle = 4,   // Faction castle
    bridge = 5,   // Strategic choke point
};

// Territory bonus type
enum class territory_bonus : uint8_t
{
    none = 0,
    exp_bonus = 1,         // +X% experience in territory
    gold_bonus = 2,        // +X% gold in territory
    drop_bonus = 3,        // +X% drop rate
    respawn_reduction = 4, // Faster respawn
    tax_income = 5,        // Gold income for faction
};

// Territory bonus entry
struct territory_bonus_entry
{
    territory_bonus type{territory_bonus::none};
    int32_t value{0}; // Percentage or flat value
};

// Territory state
struct territory
{
    territory_id id{};
    std::string name;
    territory_type type{territory_type::outpost};
    map_id map{};

    // Bounds
    int16_t min_x{0};
    int16_t min_y{0};
    int16_t max_x{0};
    int16_t max_y{0};

    // Control
    war_faction controlling_faction{war_faction::neutral};
    std::chrono::system_clock::time_point captured_at{};
    int32_t capture_count{0}; // Times captured total

    // Strategic value
    int32_t strategic_value{100}; // Points for controlling
    std::vector<territory_bonus_entry> bonuses;

    // Adjacent territories
    std::vector<territory_id> adjacent_territories;

    // War objectives in this territory
    std::vector<uint16_t> objective_ids;

    [[nodiscard]] auto contains(int16_t x, int16_t y) const -> bool
    {
        return x >= min_x && x <= max_x && y >= min_y && y <= max_y;
    }

    [[nodiscard]] auto is_controlled() const -> bool { return controlling_faction != war_faction::neutral; }

    [[nodiscard]] auto is_contested() const -> bool
    {
        // Would check if enemy players are present
        return false;
    }

    [[nodiscard]] auto area() const -> int32_t { return (max_x - min_x) * (max_y - min_y); }
};

// Territory control record (for history)
struct territory_control_record
{
    territory_id territory{};
    war_faction faction{war_faction::neutral};
    std::chrono::system_clock::time_point captured_at{};
    std::chrono::system_clock::time_point lost_at{};
    int32_t duration_seconds{0};
};

// Faction territory summary
struct faction_territory_summary
{
    war_faction faction{war_faction::neutral};
    int32_t total_territories{0};
    int32_t towns{0};
    int32_t outposts{0};
    int32_t resources{0};
    int32_t dungeons{0};
    int32_t castles{0};
    int32_t total_strategic_value{0};

    void add_territory(const territory& t)
    {
        ++total_territories;
        total_strategic_value += t.strategic_value;

        switch (t.type)
        {
        case territory_type::town:
            ++towns;
            break;
        case territory_type::outpost:
            ++outposts;
            break;
        case territory_type::resource:
            ++resources;
            break;
        case territory_type::dungeon:
            ++dungeons;
            break;
        case territory_type::castle:
            ++castles;
            break;
        default:
            break;
        }
    }
};

} // namespace hb::war
