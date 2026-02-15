#pragma once

// boss_phase.h
// Phase definitions for boss encounters

#include <cstdint>
#include <string>
#include <vector>

namespace hb::npc::boss
{

enum class phase_trigger_type : uint8_t
{
    hp_below,  // Trigger when HP drops below threshold
    hp_above,  // Trigger when HP rises above threshold
    timer,     // Trigger after N seconds in current phase
    adds_dead, // Trigger when all spawned adds are dead
};

struct phase_trigger
{
    phase_trigger_type type{phase_trigger_type::hp_below};
    float hp_threshold{0.0f}; // For hp_below/hp_above (0.0 - 1.0)
    int32_t timer_ms{0};      // For timer trigger
};

struct boss_phase
{
    std::string name;
    phase_trigger enter_trigger;
    std::string behavior_tree;   // BT to use in this phase (optional)
    std::string on_enter_script; // Script to run on phase entry (optional)
    std::string on_exit_script;  // Script to run on phase exit (optional)

    // Stat modifiers for this phase
    float damage_multiplier{1.0f};
    float speed_multiplier{1.0f};
    float defense_multiplier{1.0f};
    int16_t override_aggro_range{-1};  // -1 = no override
    int16_t override_attack_range{-1}; // -1 = no override
};

struct boss_config
{
    std::string name;
    std::vector<boss_phase> phases;
    std::string on_spawn_script;
    std::string on_death_script;
    bool enrage_enabled{false};
    int32_t enrage_timer_ms{0}; // Time until enrage
};

} // namespace hb::npc::boss
