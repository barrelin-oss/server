#pragma once

// fishing_config.h
// Data structures for the fishing system

#include "core/types.h"

#include <string>
#include <chrono>
#include <cstdint>

namespace hb::crafting {

// Single fish type configuration (loaded from YAML)
struct fish_type_config
{
    int32_t type_id{};          // 1-N
    std::string name;           // "Red Carp", "Dagger+2", etc.
    std::string item_name;      // Item to award (resolved via item_registry)
    int32_t template_id{};      // Resolved template ID
    int16_t difficulty{};       // Subtracted from skill for success roll
    int32_t lifespan_min{20};   // Base lifespan in minutes
    uint8_t visual_type{2};     // 2=fish sprite, 3=fishobject sprite
    int32_t weight{10};         // Relative spawn weight
};

// Template ID for the fishing rod item
inline constexpr int32_t fishing_rod_template_id = 105;

// Max simultaneous fishers per node
inline constexpr int32_t max_engaging_fish = 30;

// Max fish nodes across all maps
inline constexpr size_t max_fish_nodes = 200;

// Player fishing engagement state
struct fishing_state
{
    uint32_t fish_node_index{};     // Which fish node (0 = not fishing)
    int32_t catch_chance{1};        // Current % chance (1-99)
    std::chrono::steady_clock::time_point last_update{};
};

// Fish catch result
enum class catch_result : uint8_t
{
    success,            // Caught fish
    failure,            // Failed roll, can try again
    canceled_moved,     // Player moved/damaged
    canceled_stolen,    // Another player caught it first
    canceled_timeout,   // Fish despawned
    no_fish,            // No fish node engaged
    no_rod,             // Missing fishing rod
    rod_broken          // Rod durability at 0
};

struct fish_catch_result
{
    catch_result result{};
    std::string item_name;
    int32_t template_id{};
};

}  // namespace hb::crafting
