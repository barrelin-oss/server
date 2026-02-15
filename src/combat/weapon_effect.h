#pragma once

// weapon_effect.h
// Weapon enchantment on-hit effects

#include "item/item_attribute.h"

#include <cstdint>

namespace hb::combat
{

// Result of processing a weapon enchantment after a successful hit
struct weapon_on_hit_result
{
    bool apply_poison{false};
    int32_t poison_level{0}; // Poison severity/damage per tick
    bool apply_critical_bonus{false};
    int32_t critical_bonus{0}; // Extra critical chance
    bool apply_righteous{false};
    int32_t righteous_bonus{0}; // Rep-based bonus damage
    bool trigger_spell{false};
    int32_t spell_value{0}; // Spell effect value from enchantment
    bool apply_damage_reduction{false};
    int32_t damage_reduction{0}; // % damage reduction
    bool apply_fire{false};
    int32_t fire_bonus{0}; // Fire damage bonus
    bool apply_magic_damage{false};
    int32_t magic_damage_bonus{0};   // Bonus magic damage
    int32_t mana_gained{0};          // Mana gained from mana_conversion
    bool gained_super_charge{false}; // Gained a super attack charge
};

// Process weapon enchantment effect after a successful hit
// Returns what effects to apply based on the weapon's main enchantment
auto process_weapon_effect(item::enchantment_type type,
                           uint8_t value,
                           int32_t damage_dealt,
                           int32_t attacker_mp,
                           int32_t attacker_max_mp) -> weapon_on_hit_result;

} // namespace hb::combat
