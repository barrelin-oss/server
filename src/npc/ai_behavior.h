#pragma once

// ai_behavior.h
// NPC AI state machine and behavior

#include "core/enums.h"
#include "entity/entity.h"
#include "world/position.h"

#include <chrono>
#include <string>

namespace hb::npc {

// AI state machine states
enum class ai_state : uint8_t {
    idle = 0,       // Standing still, waiting
    wander = 1,     // Random movement in spawn area
    chase = 2,      // Pursuing a target
    attack = 3,     // In combat, attacking
    flee = 4,       // Running away (low HP)
    return_home = 5, // Returning to spawn point
    dead = 6,       // Dead, waiting for respawn
    scripted = 7,   // Following a script/path
};

// AI behavior flags
enum class ai_flags : uint16_t {
    none = 0,
    aggressive = 1 << 0,    // Attacks players on sight
    peaceful = 1 << 1,      // Never attacks unless attacked
    cowardly = 1 << 2,      // Flees at low HP
    stationary = 1 << 3,    // Never moves
    no_target_switch = 1 << 4, // Stays on same target
    pursues_far = 1 << 5,   // Chases long distance
    calls_help = 1 << 6,    // Alerts nearby allies
    social = 1 << 7,        // Groups with same type
    boss = 1 << 8,          // Boss monster behavior
    guard = 1 << 9,         // Town guard behavior
    merchant = 1 << 10,     // Shop NPC
    detect_invisible = 1 << 11, // Can see invisible players
};

inline auto operator|(ai_flags a, ai_flags b) -> ai_flags {
    return static_cast<ai_flags>(static_cast<uint16_t>(a) | static_cast<uint16_t>(b));
}

inline auto operator&(ai_flags a, ai_flags b) -> ai_flags {
    return static_cast<ai_flags>(static_cast<uint16_t>(a) & static_cast<uint16_t>(b));
}

// AI behavior configuration
struct ai_config {
    ai_flags flags{ai_flags::none};
    int16_t aggro_range{8};      // Range to detect enemies
    int16_t chase_range{15};     // Max chase distance from spawn
    int16_t attack_range{1};     // Attack range (1 = melee)
    int16_t flee_hp_percent{15}; // HP% to start fleeing
    int16_t wander_range{5};     // Wander distance from spawn
    int32_t think_interval_ms{500}; // AI update interval
    hb::npc_move_type move_type{hb::npc_move_type::random}; // Movement behavior type

    // Behavior tree name (empty = use state machine)
    std::string behavior_tree;

    [[nodiscard]] auto has_flag(ai_flags flag) const -> bool {
        return (flags & flag) != ai_flags::none;
    }
};

// NPC behavior state (runtime state for AI)
struct ai_runtime_state {
    ai_state state{ai_state::idle};
    entity::entity target{};
    hb::world::position spawn_point{};
    hb::world::position target_position{};

    // Pack behavior
    entity::entity pack_leader{};  // Leader we're following (if social/pack member)

    std::chrono::steady_clock::time_point last_think_time{};
    std::chrono::steady_clock::time_point last_attack_time{};
    std::chrono::steady_clock::time_point last_move_time{};
    std::chrono::steady_clock::time_point state_enter_time{};
    std::chrono::steady_clock::time_point death_time{};

    int32_t stuck_count{0};      // Pathfinding stuck counter
    int32_t aggro_level{0};      // Hate/threat level

    void set_state(ai_state new_state) {
        state = new_state;
        state_enter_time = std::chrono::steady_clock::now();
    }

    [[nodiscard]] auto time_in_state_ms() const -> int64_t {
        auto now = std::chrono::steady_clock::now();
        return std::chrono::duration_cast<std::chrono::milliseconds>(now - state_enter_time).count();
    }

    [[nodiscard]] auto time_since_death_ms() const -> int64_t {
        auto now = std::chrono::steady_clock::now();
        return std::chrono::duration_cast<std::chrono::milliseconds>(now - death_time).count();
    }

    void clear_target() {
        target = entity::entity::null();
        aggro_level = 0;
    }
};

}  // namespace hb::npc
