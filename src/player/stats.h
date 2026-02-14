#pragma once

// stats.h
// Player attribute stats

#include <cstdint>
#include <algorithm>

namespace hb::player {

// Base attribute stats (STR, DEX, VIT, INT, MAG, CHR)
struct base_stats {
    int16_t strength{10};
    int16_t dexterity{10};
    int16_t vitality{10};
    int16_t intelligence{10};
    int16_t magic{10};
    int16_t charisma{10};

    // Calculate derived values
    [[nodiscard]] auto max_hp() const -> int32_t {
        return 3 * vitality + strength + 2 * level_bonus;
    }

    [[nodiscard]] auto max_mp() const -> int32_t {
        return 2 * magic + intelligence + level_bonus;
    }

    [[nodiscard]] auto max_sp() const -> int32_t {
        return strength + 2 * dexterity + level_bonus;
    }

    [[nodiscard]] auto physical_attack() const -> int32_t {
        return strength + dexterity / 2;
    }

    [[nodiscard]] auto magic_attack() const -> int32_t {
        return intelligence + magic / 2;
    }

    [[nodiscard]] auto hit_rate() const -> int32_t {
        return dexterity + level_bonus;
    }

    [[nodiscard]] auto dodge_rate() const -> int32_t {
        return dexterity / 2 + level_bonus / 2;
    }

    int16_t level_bonus{0};  // Bonus from level for calculations
};

// Stat modifiers from equipment, buffs, etc.
struct stat_modifiers {
    // Flat bonuses
    int16_t strength{0};
    int16_t dexterity{0};
    int16_t vitality{0};
    int16_t intelligence{0};
    int16_t magic{0};
    int16_t charisma{0};

    // Combat bonuses
    int16_t attack_power{0};
    int16_t magic_power{0};
    int16_t defense{0};
    int16_t magic_defense{0};
    int16_t hit_bonus{0};
    int16_t dodge_bonus{0};
    int16_t critical_rate{0};
    int16_t critical_damage{0};

    // Speed bonuses
    int16_t attack_speed{0};
    int16_t move_speed{0};
    int16_t cast_speed{0};

    // Resistance bonuses (percentage)
    int16_t physical_resist{0};
    int16_t magic_resist{0};
    int16_t poison_resist{0};
    int16_t paralyze_resist{0};

    // Resource bonuses
    int16_t hp_bonus{0};
    int16_t mp_bonus{0};
    int16_t sp_bonus{0};
    int16_t hp_regen{0};
    int16_t mp_regen{0};
    int16_t sp_regen{0};

    // Attribute-derived bonuses (from item enchantments)
    int16_t physical_absorption{0};   // % physical damage absorbed (cap 80)
    int16_t magic_absorption{0};      // % magic damage absorbed (cap 80)
    int16_t exp_bonus_percent{0};     // % bonus experience
    int16_t gold_bonus_percent{0};    // % bonus gold
    int16_t weapon_dice_bonus{0};     // +N weapon dice range (sharp/ancient)
    int16_t charge_critical{0};       // Charge critical % (cap 20)
    int16_t mana_conversion{0};       // Damage-to-mana % (cap 20)

    // Combine two modifier sets
    auto operator+(const stat_modifiers& other) const -> stat_modifiers {
        stat_modifiers result;
        result.strength = strength + other.strength;
        result.dexterity = dexterity + other.dexterity;
        result.vitality = vitality + other.vitality;
        result.intelligence = intelligence + other.intelligence;
        result.magic = magic + other.magic;
        result.charisma = charisma + other.charisma;
        result.attack_power = attack_power + other.attack_power;
        result.magic_power = magic_power + other.magic_power;
        result.defense = defense + other.defense;
        result.magic_defense = magic_defense + other.magic_defense;
        result.hit_bonus = hit_bonus + other.hit_bonus;
        result.dodge_bonus = dodge_bonus + other.dodge_bonus;
        result.critical_rate = critical_rate + other.critical_rate;
        result.critical_damage = critical_damage + other.critical_damage;
        result.attack_speed = attack_speed + other.attack_speed;
        result.move_speed = move_speed + other.move_speed;
        result.cast_speed = cast_speed + other.cast_speed;
        result.physical_resist = physical_resist + other.physical_resist;
        result.magic_resist = magic_resist + other.magic_resist;
        result.poison_resist = poison_resist + other.poison_resist;
        result.paralyze_resist = paralyze_resist + other.paralyze_resist;
        result.hp_bonus = hp_bonus + other.hp_bonus;
        result.mp_bonus = mp_bonus + other.mp_bonus;
        result.sp_bonus = sp_bonus + other.sp_bonus;
        result.hp_regen = hp_regen + other.hp_regen;
        result.mp_regen = mp_regen + other.mp_regen;
        result.sp_regen = sp_regen + other.sp_regen;
        result.physical_absorption = physical_absorption + other.physical_absorption;
        result.magic_absorption = magic_absorption + other.magic_absorption;
        result.exp_bonus_percent = exp_bonus_percent + other.exp_bonus_percent;
        result.gold_bonus_percent = gold_bonus_percent + other.gold_bonus_percent;
        result.weapon_dice_bonus = weapon_dice_bonus + other.weapon_dice_bonus;
        result.charge_critical = charge_critical + other.charge_critical;
        result.mana_conversion = mana_conversion + other.mana_conversion;
        return result;
    }

    auto operator+=(const stat_modifiers& other) -> stat_modifiers& {
        *this = *this + other;
        return *this;
    }

    void clear() {
        *this = stat_modifiers{};
    }
};

// Final computed stats after all modifiers
struct computed_stats {
    // Base + modifiers
    int16_t strength{0};
    int16_t dexterity{0};
    int16_t vitality{0};
    int16_t intelligence{0};
    int16_t magic{0};
    int16_t charisma{0};

    // Combat stats
    int32_t max_hp{0};
    int32_t max_mp{0};
    int32_t max_sp{0};
    int32_t attack_power{0};
    int32_t magic_power{0};
    int32_t defense{0};
    int32_t magic_defense{0};
    int32_t hit_rate{0};
    int32_t dodge_rate{0};
    int32_t critical_rate{0};
    int32_t critical_damage{0};

    // Speed
    int32_t attack_speed{0};
    int32_t move_speed{0};
    int32_t cast_speed{0};

    // Resistances
    int32_t physical_resist{0};
    int32_t magic_resist{0};
    int32_t poison_resist{0};
    int32_t paralyze_resist{0};

    // Regen rates (per tick)
    int32_t hp_regen{0};
    int32_t mp_regen{0};
    int32_t sp_regen{0};

    // Attribute-derived stats
    int32_t physical_absorption{0};   // % physical damage absorbed (cap 80)
    int32_t magic_absorption{0};      // % magic damage absorbed (cap 80)
    int32_t exp_bonus_percent{0};     // % bonus experience
    int32_t gold_bonus_percent{0};    // % bonus gold
    int32_t weapon_dice_bonus{0};     // +N weapon dice range
    int32_t charge_critical{0};       // % chance (cap 20)
    int32_t mana_conversion{0};       // % (cap 20)

    // Compute from base + modifiers
    void compute(const base_stats& base, const stat_modifiers& mods) {
        strength = base.strength + mods.strength;
        dexterity = base.dexterity + mods.dexterity;
        vitality = base.vitality + mods.vitality;
        intelligence = base.intelligence + mods.intelligence;
        magic = base.magic + mods.magic;
        charisma = base.charisma + mods.charisma;

        // Create modified base for calculations
        base_stats effective = base;
        effective.strength = strength;
        effective.dexterity = dexterity;
        effective.vitality = vitality;
        effective.intelligence = intelligence;
        effective.magic = magic;
        effective.charisma = charisma;

        max_hp = effective.max_hp() + mods.hp_bonus;
        max_mp = effective.max_mp() + mods.mp_bonus;
        max_sp = effective.max_sp() + mods.sp_bonus;
        attack_power = effective.physical_attack() + mods.attack_power;
        magic_power = effective.magic_attack() + mods.magic_power;
        defense = mods.defense;
        magic_defense = mods.magic_defense;
        hit_rate = effective.hit_rate() + mods.hit_bonus;
        dodge_rate = effective.dodge_rate() + mods.dodge_bonus;
        critical_rate = 10 + mods.critical_rate;  // Base 10% crit
        critical_damage = 150 + mods.critical_damage;  // Base 150% crit damage

        attack_speed = 100 + mods.attack_speed;
        move_speed = 100 + mods.move_speed;
        cast_speed = 100 + mods.cast_speed;

        physical_resist = std::clamp<int32_t>(mods.physical_resist, 0, 80);
        magic_resist = std::clamp<int32_t>(mods.magic_resist, 0, 80);
        poison_resist = std::clamp<int32_t>(mods.poison_resist, 0, 100);
        paralyze_resist = std::clamp<int32_t>(mods.paralyze_resist, 0, 100);

        hp_regen = 1 + vitality / 10 + mods.hp_regen;
        mp_regen = 1 + magic / 10 + mods.mp_regen;
        sp_regen = 1 + dexterity / 10 + mods.sp_regen;

        physical_absorption = std::clamp<int32_t>(mods.physical_absorption, 0, 80);
        magic_absorption = std::clamp<int32_t>(mods.magic_absorption, 0, 80);
        exp_bonus_percent = mods.exp_bonus_percent;
        gold_bonus_percent = mods.gold_bonus_percent;
        weapon_dice_bonus = mods.weapon_dice_bonus;
        charge_critical = std::clamp<int32_t>(mods.charge_critical, 0, 20);
        mana_conversion = std::clamp<int32_t>(mods.mana_conversion, 0, 20);
    }
};

}  // namespace hb::player
