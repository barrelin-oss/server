#pragma once

// spawn_point.h
// NPC spawn point configuration

#include "core/types.h"
#include "world/position.h"

#include <chrono>
#include <vector>

namespace hb::npc {

// Spawn point for NPCs
struct spawn_point {
    npc_id npc_type{};               // NPC template ID
    map_id map{};                     // Map to spawn on
    hb::world::position center{};    // Center of spawn area
    int16_t radius{5};            // Spawn radius from center
    int16_t max_count{1};         // Max spawned at once
    int32_t respawn_time_ms{60000}; // Respawn delay

    int16_t current_count{0};     // Currently alive
    std::chrono::steady_clock::time_point next_spawn_time{};

    [[nodiscard]] auto can_spawn() const -> bool {
        if (current_count >= max_count) return false;
        auto now = std::chrono::steady_clock::now();
        return now >= next_spawn_time;
    }

    void on_spawn() {
        ++current_count;
    }

    void on_death() {
        --current_count;
        if (current_count < max_count) {
            next_spawn_time = std::chrono::steady_clock::now() +
                std::chrono::milliseconds(respawn_time_ms);
        }
    }

    [[nodiscard]] auto get_spawn_position() const -> hb::world::position {
        // Random position within radius (simplified)
        // Real implementation would use proper random
        return center;
    }
};

// Spawn group - multiple spawn points that work together
struct spawn_group {
    std::vector<spawn_point> points;
    bool active{true};

    void activate() { active = true; }
    void deactivate() { active = false; }
};

}  // namespace hb::npc
