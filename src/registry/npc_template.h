#pragma once

// npc_template.h
// Read-only NPC template data structure

#include "core/types.h"
#include "core/enums.h"

#include <string>
#include <vector>
#include <array>
#include <cstdint>

namespace hb {

// NPC types (must be defined before npc_template uses it)
enum class npc_type : uint8_t {
    monster = 0,
    npc = 1,
    guard = 2,
    pet = 3,
    summon = 4,
    boss = 5,
    event = 6
};

// Drop entry for NPC loot table
struct npc_drop {
    item_id item{0};
    int16_t chance{0};      // Per 10000 (100.00%)
    int16_t count_min{1};
    int16_t count_max{1};
};

// Spell that NPC can cast
struct npc_spell {
    spell_id spell{0};
    int16_t chance{0};      // Per 100
    int16_t cooldown_ms{0};
};

// NPC template - read-only data loaded from config
struct npc_template {
    // Identity
    npc_id id{0};
    std::string name;
    std::string sprite;

    // Classification
    npc_type type{npc_type::monster};
    npc_behavior default_behavior{npc_behavior::stop};
    npc_move_type move_type{npc_move_type::random};

    // Stats
    int32_t hp{0};
    int32_t mp{0};
    int16_t level{0};

    // Combat stats
    int16_t attack_dice{0};
    int16_t attack_sides{0};
    int16_t attack_bonus{0};
    int16_t defense{0};
    int16_t hit_rate{0};
    int16_t dodge_rate{0};
    int16_t magic_resist{0};
    int16_t crit_chance{0};

    // Damage resistances (percentage reduction)
    int8_t resist_physical{0};
    int8_t resist_magic{0};
    int8_t resist_fire{0};
    int8_t resist_ice{0};
    int8_t resist_lightning{0};

    // Movement
    int16_t move_speed{0};
    int16_t attack_speed{0};
    int16_t attack_range{1};
    int16_t sight_range{10};

    // Rewards
    int32_t exp_reward{0};
    int32_t gold_min{0};
    int32_t gold_max{0};

    // Drop table
    std::vector<npc_drop> drops;

    // Spells
    std::vector<npc_spell> spells;

    // Flags
    bool is_aggressive{false};     // Attacks players on sight
    bool is_boss{false};           // Boss monster
    bool is_undead{false};         // Affected by resurrection magic
    bool is_summoned{false};       // Can be summoned
    bool can_talk{false};          // Has dialogue
    bool is_merchant{false};       // Can trade
    bool gives_quest{false};       // Has quests
    bool respawns{true};           // Respawns after death

    // Helper methods
    [[nodiscard]] auto average_damage() const -> float {
        if (attack_sides <= 0) return static_cast<float>(attack_bonus);
        return static_cast<float>(attack_dice) * (static_cast<float>(attack_sides + 1) / 2.0f) +
               static_cast<float>(attack_bonus);
    }

    [[nodiscard]] auto is_hostile() const -> bool {
        return type == npc_type::monster && is_aggressive;
    }
};

}  // namespace hb
