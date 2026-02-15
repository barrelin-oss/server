#include <gtest/gtest.h>

#include "combat/weapon_effect.h"
#include "combat/combat_events.h"
#include "item/item_attribute.h"

using namespace hb::combat;
using namespace hb::item;

// --- None enchantment ---

TEST(weapon_effect_test, none_enchantment_produces_no_effect)
{
    auto result = process_weapon_effect(enchantment_type::none, 0, 100, 50, 200);
    EXPECT_FALSE(result.apply_poison);
    EXPECT_FALSE(result.apply_critical_bonus);
    EXPECT_FALSE(result.trigger_spell);
    EXPECT_EQ(result.mana_gained, 0);
    EXPECT_FALSE(result.gained_super_charge);
}

// --- Poison ---

TEST(weapon_effect_test, poison_sets_flag_and_level)
{
    auto result = process_weapon_effect(enchantment_type::poison, 5, 100, 50, 200);
    EXPECT_TRUE(result.apply_poison);
    EXPECT_EQ(result.poison_level, 5);
}

TEST(weapon_effect_test, poison_minimum_level_one)
{
    auto result = process_weapon_effect(enchantment_type::poison, 0, 100, 50, 200);
    EXPECT_TRUE(result.apply_poison);
    EXPECT_GE(result.poison_level, 1);
}

// --- Critical bonus ---

TEST(weapon_effect_test, critical_bonus_sets_value)
{
    auto result = process_weapon_effect(enchantment_type::critical_bonus, 10, 100, 50, 200);
    EXPECT_TRUE(result.apply_critical_bonus);
    EXPECT_EQ(result.critical_bonus, 10);
}

// --- Righteous ---

TEST(weapon_effect_test, righteous_sets_bonus)
{
    auto result = process_weapon_effect(enchantment_type::righteous, 7, 100, 50, 200);
    EXPECT_TRUE(result.apply_righteous);
    EXPECT_EQ(result.righteous_bonus, 7);
}

// --- Spell on hit ---

TEST(weapon_effect_test, spell_on_hit_triggers)
{
    auto result = process_weapon_effect(enchantment_type::spell_on_hit, 3, 100, 50, 200);
    EXPECT_TRUE(result.trigger_spell);
    EXPECT_EQ(result.spell_value, 3);
}

// --- Damage reduction ---

TEST(weapon_effect_test, damage_reduction_sets_value)
{
    auto result = process_weapon_effect(enchantment_type::damage_reduction, 8, 100, 50, 200);
    EXPECT_TRUE(result.apply_damage_reduction);
    EXPECT_EQ(result.damage_reduction, 8);
}

// --- Fire ---

TEST(weapon_effect_test, fire_sets_bonus)
{
    auto result = process_weapon_effect(enchantment_type::fire, 4, 100, 50, 200);
    EXPECT_TRUE(result.apply_fire);
    EXPECT_EQ(result.fire_bonus, 4);
}

// --- Magic damage ---

TEST(weapon_effect_test, magic_damage_sets_bonus)
{
    auto result = process_weapon_effect(enchantment_type::magic_damage, 6, 100, 50, 200);
    EXPECT_TRUE(result.apply_magic_damage);
    EXPECT_EQ(result.magic_damage_bonus, 6);
}

// --- Light ---

TEST(weapon_effect_test, light_has_no_combat_effect)
{
    auto result = process_weapon_effect(enchantment_type::light, 1, 100, 50, 200);
    EXPECT_FALSE(result.apply_poison);
    EXPECT_FALSE(result.apply_critical_bonus);
    EXPECT_FALSE(result.trigger_spell);
    EXPECT_EQ(result.mana_gained, 0);
    EXPECT_FALSE(result.gained_super_charge);
}

// --- Sharp / Ancient ---

TEST(weapon_effect_test, sharp_has_no_combat_effect)
{
    auto result = process_weapon_effect(enchantment_type::sharp, 1, 100, 50, 200);
    // Dice bonus is handled in stat application, not on-hit
    EXPECT_FALSE(result.apply_poison);
    EXPECT_EQ(result.mana_gained, 0);
}

TEST(weapon_effect_test, ancient_has_no_combat_effect)
{
    auto result = process_weapon_effect(enchantment_type::ancient, 1, 100, 50, 200);
    EXPECT_FALSE(result.apply_poison);
    EXPECT_EQ(result.mana_gained, 0);
}

// --- Mana conversion ---

TEST(weapon_effect_test, mana_conversion_calculates_gain)
{
    // 10% of 200 damage = 20 mana, mp=50, max=200 → room for 150
    auto result = process_weapon_effect(enchantment_type::mana_conversion, 10, 200, 50, 200);
    EXPECT_EQ(result.mana_gained, 20);
}

TEST(weapon_effect_test, mana_conversion_capped_at_20_percent)
{
    // value=30 → capped to 20%, 20% of 100 = 20
    auto result = process_weapon_effect(enchantment_type::mana_conversion, 30, 100, 0, 200);
    EXPECT_EQ(result.mana_gained, 20);
}

TEST(weapon_effect_test, mana_conversion_capped_by_max_mp)
{
    // 10% of 100 = 10, but only 5 room
    auto result = process_weapon_effect(enchantment_type::mana_conversion, 10, 100, 195, 200);
    EXPECT_EQ(result.mana_gained, 5);
}

TEST(weapon_effect_test, mana_conversion_zero_when_full)
{
    auto result = process_weapon_effect(enchantment_type::mana_conversion, 10, 100, 200, 200);
    EXPECT_EQ(result.mana_gained, 0);
}

TEST(weapon_effect_test, mana_conversion_zero_damage_zero_mana)
{
    auto result = process_weapon_effect(enchantment_type::mana_conversion, 10, 0, 50, 200);
    EXPECT_EQ(result.mana_gained, 0);
}

// --- Charge critical ---

TEST(weapon_effect_test, charge_critical_probabilistic)
{
    // Run many trials with 20% chance
    int charges = 0;
    const int trials = 10000;
    for (int i = 0; i < trials; ++i)
    {
        auto result = process_weapon_effect(enchantment_type::charge_critical, 20, 100, 50, 200);
        if (result.gained_super_charge)
            ++charges;
    }
    // Should be roughly 20% ± tolerance
    double rate = static_cast<double>(charges) / trials;
    EXPECT_GT(rate, 0.12);
    EXPECT_LT(rate, 0.28);
}

TEST(weapon_effect_test, charge_critical_zero_chance_never_triggers)
{
    for (int i = 0; i < 1000; ++i)
    {
        auto result = process_weapon_effect(enchantment_type::charge_critical, 0, 100, 50, 200);
        EXPECT_FALSE(result.gained_super_charge);
    }
}

TEST(weapon_effect_test, charge_critical_capped_at_20)
{
    // value=30 → capped to 20%, test distribution is sane
    int charges = 0;
    const int trials = 10000;
    for (int i = 0; i < trials; ++i)
    {
        auto result = process_weapon_effect(enchantment_type::charge_critical, 30, 100, 50, 200);
        if (result.gained_super_charge)
            ++charges;
    }
    double rate = static_cast<double>(charges) / trials;
    EXPECT_GT(rate, 0.12); // Should match 20% cap, not 30%
    EXPECT_LT(rate, 0.28);
}

// --- Combat context includes enchantment fields ---

TEST(weapon_effect_test, combat_context_has_enchantment_fields)
{
    combat_context ctx;
    ctx.weapon_enchantment = enchantment_type::poison;
    ctx.weapon_enchantment_value = 5;
    EXPECT_EQ(ctx.weapon_enchantment, enchantment_type::poison);
    EXPECT_EQ(ctx.weapon_enchantment_value, 5);
}

TEST(weapon_effect_test, combat_context_default_no_enchantment)
{
    combat_context ctx;
    EXPECT_EQ(ctx.weapon_enchantment, enchantment_type::none);
    EXPECT_EQ(ctx.weapon_enchantment_value, 0);
}
