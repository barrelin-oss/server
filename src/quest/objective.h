#pragma once

// objective.h
// Quest objective definitions and tracking

#include "core/types.h"

#include <string>
#include <cstdint>
#include <variant>

namespace hb::quest {

// Objective types
enum class objective_type : uint8_t {
    kill_monster = 0,      // Kill N monsters of type X
    kill_player = 1,       // Kill N enemy players
    collect_item = 2,      // Collect N items of type X
    deliver_item = 3,      // Deliver items to NPC
    visit_location = 4,    // Visit specific location
    talk_to_npc = 5,       // Talk to specific NPC
    craft_item = 6,        // Craft N items
    gather_resource = 7,   // Gather N resources (mining, fishing, etc.)
    reach_level = 8,       // Reach character level N
    reach_skill_level = 9, // Reach skill level N
    survive_time = 10,     // Survive for N seconds
    escort_npc = 11,       // Escort NPC from A to B
    defend_location = 12,  // Defend location for N seconds
};

// Objective status
enum class objective_status : uint8_t {
    incomplete = 0,
    complete = 1,
    failed = 2,
};

// Kill objective data
struct kill_objective_data {
    npc_id target_type{};       // NPC template ID to kill (0 = any)
    int32_t required_count{1};
    bool player_kills{false};   // Kill players instead of NPCs
};

// Collect/Deliver objective data
struct collect_objective_data {
    item_id item_type{};
    int32_t required_count{1};
    npc_id deliver_to{};        // For deliver objectives, NPC to deliver to (0 = just collect)
};

// Location objective data
struct location_objective_data {
    map_id target_map{};
    int16_t target_x{0};
    int16_t target_y{0};
    int16_t radius{5};          // How close player needs to be
};

// NPC interaction objective data
struct npc_objective_data {
    npc_id target_npc{};
    map_id escort_destination_map{};  // For escort quests
    int16_t escort_x{0};
    int16_t escort_y{0};
};

// Crafting objective data
struct craft_objective_data {
    item_id item_type{};
    int32_t required_count{1};
};

// Gathering objective data
struct gather_objective_data {
    uint8_t skill_type{};       // Mining, fishing, farming, etc.
    int32_t required_count{1};
};

// Level objective data
struct level_objective_data {
    int16_t target_level{1};
    uint8_t skill_type{0};      // 0 = character level, otherwise skill type
};

// Time-based objective data
struct time_objective_data {
    int32_t duration_seconds{60};
    map_id location_map{};      // Optional - specific map for defend/survive
    int16_t location_x{0};
    int16_t location_y{0};
    int16_t location_radius{10};
};

// Objective data variant
using objective_data = std::variant<
    kill_objective_data,
    collect_objective_data,
    location_objective_data,
    npc_objective_data,
    craft_objective_data,
    gather_objective_data,
    level_objective_data,
    time_objective_data
>;

// Quest objective definition (immutable template)
struct objective_template {
    uint16_t id{0};             // Objective ID within quest
    objective_type type{objective_type::kill_monster};
    std::string description;
    objective_data data;
    bool optional{false};       // Optional objectives don't block completion
};

// Quest objective state (mutable progress)
struct objective_state {
    uint16_t template_id{0};
    objective_status status{objective_status::incomplete};
    int32_t current_count{0};   // Progress toward goal
    int32_t required_count{1};  // Goal to reach
    int32_t time_elapsed{0};    // For time-based objectives

    [[nodiscard]] auto is_complete() const -> bool {
        return status == objective_status::complete;
    }

    [[nodiscard]] auto is_failed() const -> bool {
        return status == objective_status::failed;
    }

    [[nodiscard]] auto progress_percent() const -> float {
        if (required_count <= 0) return 100.0f;
        return (static_cast<float>(current_count) / static_cast<float>(required_count)) * 100.0f;
    }

    // Add progress toward completion
    auto add_progress(int32_t amount) -> bool {
        if (status != objective_status::incomplete) return false;

        current_count += amount;
        if (current_count >= required_count) {
            current_count = required_count;
            status = objective_status::complete;
            return true;  // Objective just completed
        }
        return false;
    }

    // Mark as failed
    void fail() {
        status = objective_status::failed;
    }

    // Reset objective state
    void reset() {
        status = objective_status::incomplete;
        current_count = 0;
        time_elapsed = 0;
    }
};

}  // namespace hb::quest
