#pragma once

// item_effect.h
// Item effect application and calculation

#include "item/item.h"
#include "player/stats.h"

namespace hb::item {

// Apply item effects to stat modifiers
inline void apply_item_effects(const item& itm, hb::player::stat_modifiers& mods) {
    for (const auto& effect : itm.effects) {
        if (effect.is_empty()) continue;

        switch (effect.type) {
            case item_effect_type::hp_bonus:
                mods.hp_bonus += effect.value;
                break;
            case item_effect_type::mp_bonus:
                mods.mp_bonus += effect.value;
                break;
            case item_effect_type::sp_bonus:
                mods.sp_bonus += effect.value;
                break;
            case item_effect_type::str_bonus:
                mods.strength += effect.value;
                break;
            case item_effect_type::dex_bonus:
                mods.dexterity += effect.value;
                break;
            case item_effect_type::vit_bonus:
                mods.vitality += effect.value;
                break;
            case item_effect_type::int_bonus:
                mods.intelligence += effect.value;
                break;
            case item_effect_type::mag_bonus:
                mods.magic += effect.value;
                break;
            case item_effect_type::chr_bonus:
                mods.charisma += effect.value;
                break;
            case item_effect_type::attack_bonus:
                mods.attack_power += effect.value;
                break;
            case item_effect_type::defense_bonus:
                mods.defense += effect.value;
                break;
            case item_effect_type::magic_attack_bonus:
                mods.magic_power += effect.value;
                break;
            case item_effect_type::magic_defense_bonus:
                mods.magic_defense += effect.value;
                break;
            case item_effect_type::hit_bonus:
                mods.hit_bonus += effect.value;
                break;
            case item_effect_type::dodge_bonus:
                mods.dodge_bonus += effect.value;
                break;
            case item_effect_type::critical_bonus:
                mods.critical_rate += effect.value;
                break;
            case item_effect_type::damage_reduction:
                mods.physical_resist += effect.value;
                break;
            default:
                break;
        }
    }
}

// Calculate modifiers from item base stats
inline void apply_item_base_stats(const item& itm, hb::player::stat_modifiers& mods) {
    if (itm.is_broken()) return;  // Broken items give no stats

    mods.attack_power += itm.attack_power;
    mods.magic_power += itm.magic_power;
    mods.defense += itm.defense;
    mods.magic_defense += itm.magic_defense;

    // Apply effects on top
    apply_item_effects(itm, mods);
}

// Check if player meets item requirements
struct requirement_check {
    bool meets_level{true};
    bool meets_str{true};
    bool meets_dex{true};
    bool meets_int{true};
    bool meets_mag{true};

    [[nodiscard]] auto can_use() const -> bool {
        return meets_level && meets_str && meets_dex && meets_int && meets_mag;
    }
};

inline auto check_requirements(const item& itm, int level, int str, int dex, int intel, int mag)
    -> requirement_check
{
    requirement_check result;
    result.meets_level = level >= itm.level_requirement;
    result.meets_str = str >= itm.str_requirement;
    result.meets_dex = dex >= itm.dex_requirement;
    result.meets_int = intel >= itm.int_requirement;
    result.meets_mag = mag >= itm.mag_requirement;
    return result;
}

}  // namespace hb::item
