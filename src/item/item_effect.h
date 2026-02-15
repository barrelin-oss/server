#pragma once

// item_effect.h
// Item effect application and calculation

#include "item/item.h"
#include "player/stats.h"

namespace hb::item
{

// Apply item effects to stat modifiers
inline void apply_item_effects(const item& itm, hb::player::stat_modifiers& mods)
{
    for (const auto& effect : itm.effects)
    {
        if (effect.is_empty())
            continue;

        switch (effect.type)
        {
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
inline void apply_item_base_stats(const item& itm, hb::player::stat_modifiers& mods)
{
    if (itm.is_broken())
        return; // Broken items give no stats

    mods.attack_power += itm.attack_power;
    mods.magic_power += itm.magic_power;
    mods.defense += itm.defense;
    mods.magic_defense += itm.magic_defense;

    // Apply effects on top
    apply_item_effects(itm, mods);
}

// Apply per-instance attribute bonuses to stat modifiers
// Called after apply_item_base_stats() to layer enchantment effects
inline void apply_item_attribute(const item& itm, hb::player::stat_modifiers& mods)
{
    const auto& attr = itm.attribute;
    if (attr.is_empty())
        return;
    if (itm.is_broken())
        return;

    // Upgrade level bonuses
    if (attr.upgrade_level > 0)
    {
        auto n = static_cast<int16_t>(attr.upgrade_level);

        if (itm.type == item_type::weapon)
        {
            // Weapons: +N attack power, +N magic power
            mods.attack_power += n;
            mods.magic_power += n;
        }
        else if (itm.type == item_type::armor)
        {
            // Armor/shields: +N*5 defense, +N% physical absorption
            mods.defense += static_cast<int16_t>(n * 5);
            mods.physical_absorption += n;
        }
        else if (itm.type == item_type::accessory)
        {
            // Accessories (rings, capes, amulets): scale existing base effects
            // Each upgrade level adds ~6.67% of base effect values
            for (const auto& effect : itm.effects)
            {
                if (effect.is_empty())
                    continue;
                int16_t bonus = static_cast<int16_t>(effect.value * n / 15);
                if (bonus == 0 && n > 0)
                    bonus = 1; // Minimum +1 per upgrade

                switch (effect.type)
                {
                case item_effect_type::hp_bonus:
                    mods.hp_bonus += bonus;
                    break;
                case item_effect_type::mp_bonus:
                    mods.mp_bonus += bonus;
                    break;
                case item_effect_type::sp_bonus:
                    mods.sp_bonus += bonus;
                    break;
                case item_effect_type::str_bonus:
                    mods.strength += bonus;
                    break;
                case item_effect_type::dex_bonus:
                    mods.dexterity += bonus;
                    break;
                case item_effect_type::vit_bonus:
                    mods.vitality += bonus;
                    break;
                case item_effect_type::int_bonus:
                    mods.intelligence += bonus;
                    break;
                case item_effect_type::mag_bonus:
                    mods.magic += bonus;
                    break;
                case item_effect_type::chr_bonus:
                    mods.charisma += bonus;
                    break;
                case item_effect_type::attack_bonus:
                    mods.attack_power += bonus;
                    break;
                case item_effect_type::defense_bonus:
                    mods.defense += bonus;
                    break;
                case item_effect_type::magic_attack_bonus:
                    mods.magic_power += bonus;
                    break;
                case item_effect_type::magic_defense_bonus:
                    mods.magic_defense += bonus;
                    break;
                case item_effect_type::hit_bonus:
                    mods.hit_bonus += bonus;
                    break;
                case item_effect_type::dodge_bonus:
                    mods.dodge_bonus += bonus;
                    break;
                case item_effect_type::critical_bonus:
                    mods.critical_rate += bonus;
                    break;
                case item_effect_type::damage_reduction:
                    mods.physical_resist += bonus;
                    break;
                default:
                    break;
                }
            }
        }
    }

    // Main enchantment stat bonuses (non-combat ones applied here; combat ones in Phase 3)
    switch (attr.main_type)
    {
    case enchantment_type::sharp:
        mods.weapon_dice_bonus += 1;
        break;
    case enchantment_type::ancient:
        mods.weapon_dice_bonus += 2;
        break;
    case enchantment_type::mana_conversion:
        mods.mana_conversion += attr.main_value;
        break;
    case enchantment_type::charge_critical:
        mods.charge_critical += attr.main_value;
        break;
    default:
        break;
    }

    // Sub enchantment stat bonuses
    auto sv = static_cast<int16_t>(attr.sub_value);
    switch (attr.sub_type)
    {
    case sub_enchantment_type::physical_resist:
        mods.physical_resist += static_cast<int16_t>(sv * 7);
        break;
    case sub_enchantment_type::attack_rating:
        mods.hit_bonus += static_cast<int16_t>(sv * 7);
        break;
    case sub_enchantment_type::defense_rating:
        mods.defense += static_cast<int16_t>(sv * 7);
        break;
    case sub_enchantment_type::hp_recovery:
        mods.hp_bonus += static_cast<int16_t>(sv * 7);
        break;
    case sub_enchantment_type::sp_recovery:
        mods.sp_bonus += static_cast<int16_t>(sv * 7);
        break;
    case sub_enchantment_type::mp_recovery:
        mods.mp_bonus += static_cast<int16_t>(sv * 7);
        break;
    case sub_enchantment_type::magic_resist:
        mods.magic_resist += static_cast<int16_t>(sv * 7);
        break;
    case sub_enchantment_type::physical_absorption:
        mods.physical_absorption += static_cast<int16_t>(sv * 3);
        break;
    case sub_enchantment_type::magic_absorption:
        mods.magic_absorption += static_cast<int16_t>(sv * 3);
        break;
    case sub_enchantment_type::critical_damage:
        mods.critical_damage += sv;
        break;
    case sub_enchantment_type::exp_bonus:
        mods.exp_bonus_percent += static_cast<int16_t>(sv * 10);
        break;
    case sub_enchantment_type::gold_bonus:
        mods.gold_bonus_percent += static_cast<int16_t>(sv * 10);
        break;
    case sub_enchantment_type::none:
        break;
    }
}

// Check if player meets item requirements
struct requirement_check
{
    bool meets_level{true};
    bool meets_str{true};
    bool meets_dex{true};
    bool meets_int{true};
    bool meets_mag{true};

    [[nodiscard]] auto can_use() const -> bool
    {
        return meets_level && meets_str && meets_dex && meets_int && meets_mag;
    }
};

inline auto check_requirements(const item& itm, int level, int str, int dex, int intel, int mag) -> requirement_check
{
    requirement_check result;
    // Custom-made items bypass level requirement
    result.meets_level = itm.attribute.custom_made || (level >= itm.level_requirement);
    result.meets_str = str >= itm.str_requirement;
    result.meets_dex = dex >= itm.dex_requirement;
    result.meets_int = intel >= itm.int_requirement;
    result.meets_mag = mag >= itm.mag_requirement;
    return result;
}

} // namespace hb::item
