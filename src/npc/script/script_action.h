#pragma once

// script_action.h
// Scripted action types for NPC sequences

#include "core/types.h"
#include "world/position.h"

#include <cstdint>
#include <string>
#include <vector>

namespace hb::npc::script {

enum class action_type : uint8_t
{
    wait,             // Pause for duration_ms
    move_to,          // Move to position
    face,             // Face a direction
    say,              // NPC chat message
    broadcast,        // Server-wide message
    emote,            // Play animation
    spawn_npc,        // Spawn an NPC at position
    despawn,          // Despawn self
    set_state,        // Change AI state
    set_flag,         // Set/clear ai_flag
    apply_buff,       // Apply status effect to targets in range
    damage_area,      // AoE damage at position
    heal_self,        // Heal self by amount/percent
    teleport,         // Teleport to position
    set_aggro_range,  // Change aggro range
    set_invincible,   // Toggle invincibility
    play_effect,      // Visual/sound effect at position
    call_script,      // Execute another script (subroutine)
};

struct script_action
{
    action_type type{};

    // Universal parameters (set based on type)
    int32_t int_param{0};        // duration_ms, damage, hp amount, etc.
    int32_t int_param2{0};       // radius, count, etc.
    hb::world::position pos{};   // target position
    hb::world::direction dir{};  // facing direction
    std::string str_param;       // message, script name, effect name, etc.
    npc_id npc_template{};       // for spawn_npc
    bool bool_param{false};      // for toggles (set_invincible, set_flag)
};

struct npc_script
{
    std::string name;
    std::vector<script_action> actions;
    bool loop{false};
};

}  // namespace hb::npc::script
