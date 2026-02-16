#pragma once

// npc_template.h
// Read-only NPC template data structure

#include "core/types.h"
#include "core/enums.h"

#include <string>
#include <vector>
#include <array>
#include <cstdint>

namespace hb
{

// NPC types (must be defined before npc_template uses it)
enum class npc_type : uint8_t
{
    monster = 0,
    npc = 1,
    guard = 2,
    pet = 3,
    summon = 4,
    boss = 5,
    event = 6
};

// Drop entry for NPC loot table
struct npc_drop
{
    item_id item{0};
    int16_t chance{0}; // Per 10000 (100.00%)
    int16_t count_min{1};
    int16_t count_max{1};
};

// Spell that NPC can cast
struct npc_spell
{
    spell_id spell{0};
    int16_t chance{0}; // Per 100
    int16_t cooldown_ms{0};
};

// NPC template - read-only data loaded from config
struct npc_template
{
    // Identity
    npc_id id{0};
    std::string name;
    std::string sprite;
    int16_t sprite_id{0}; // Legacy m_sType - used for drop level tiers

    // Classification
    npc_type type{npc_type::monster};
    npc_behavior default_behavior{npc_behavior::stop};
    npc_move_type move_type{npc_move_type::random};

    // Raw cfg fields (from Npc.cfg columns)
    int16_t hit_dice{0};    // HP column - used to compute HP at spawn
    int16_t hit_ratio{0};   // HR column - hit accuracy, also used as level
    int16_t min_bravery{0}; // MinBrvy column - flee threshold (0 = never flee)
    int16_t action_limit{
        0}; // ActLimit column - behavior type (0=normal, 2=townNPC, 3=dummy, 4=sphere, 5=war, 6=quest, 8=gate)
    int16_t magic_level{0};     // Magic column - NPC spellcasting level
    int16_t day_of_week{0};     // DayWeek column - spawn day restriction (10=always)
    int16_t chat_msg{0};        // Chat column - NPC chat message index
    int32_t regen_time{5000};   // RegenTime column - HP regen interval (ms)
    int16_t attribute{0};       // Attr column - NPC attribute flags
    int16_t abs_damage{0};      // AbsM column - damage absorption (%)
    int16_t body_size{0};       // Size column - 0=small, 1=medium, 2=large
    int16_t magic_hit_ratio{0}; // MagicR column - magic accuracy
    int16_t area{0};            // Area column - AoE attack range

    // Special ability spawn config (from legacy MobGenerator table)
    int16_t sa_prob{0}; // Probability % of getting a special ability at spawn (0-100)
    int16_t sa_pool{0}; // Pool ID to draw from (references special_abilities.yaml pools)

    // Derived/computed stats (set by YAML loader from raw cfg fields)
    int32_t hp{0};    // Computed from hit_dice at load or spawn
    int32_t mp{0};    // From max_mana (MaxMana column)
    int16_t level{0}; // Set to hit_ratio

    // Combat stats
    int16_t attack_dice{0};  // ADT column
    int16_t attack_sides{0}; // ADR column
    int16_t attack_bonus{0};
    int16_t defense{0};  // DR column
    int16_t hit_rate{0}; // Set to hit_ratio
    int16_t dodge_rate{0};
    int16_t magic_resist{0}; // Set from resist_magic (RestM column)
    int16_t crit_chance{0};

    // Damage resistances (percentage reduction) - not from cfg, kept for future use
    int8_t resist_physical{0};
    int8_t resist_fire{0};
    int8_t resist_ice{0};
    int8_t resist_lightning{0};

    // Movement & Timing
    int16_t attack_range{1};   // AtkRng column (tile range)
    int16_t sight_range{10};   // Search column (detection_range)
    int32_t action_time{1000}; // Atime column - base AI action interval (ms)

    // Rewards
    int32_t exp_reward{0}; // EXP column
    int32_t gold_min{0};   // Not in cfg - from loot tables
    int32_t gold_max{0};   // Not in cfg - from loot tables

    // Drop table
    std::vector<npc_drop> drops;

    // Spells
    std::vector<npc_spell> spells;

    // Behavior tree name (empty = default state machine)
    std::string behavior_tree;

    // Flags
    bool is_aggressive{false}; // Attacks players on sight
    bool is_boss{false};       // Boss monster
    bool is_undead{false};     // Affected by resurrection magic
    bool is_summoned{false};   // Can be summoned
    bool can_talk{false};      // Has dialogue
    bool is_merchant{false};   // Can trade
    bool gives_quest{false};   // Has quests
    bool respawns{true};       // Respawns after death

    // Helper methods
    [[nodiscard]] auto average_damage() const -> float
    {
        if (attack_sides <= 0)
            return static_cast<float>(attack_bonus);
        return static_cast<float>(attack_dice) * (static_cast<float>(attack_sides + 1) / 2.0f) +
               static_cast<float>(attack_bonus);
    }

    [[nodiscard]] auto is_hostile() const -> bool { return type == npc_type::monster && is_aggressive; }
};

} // namespace hb
