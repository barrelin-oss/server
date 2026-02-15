#include <gtest/gtest.h>

#include "item/item.h"
#include "item/item_effect.h"
#include "player/stats.h"

using namespace hb::item;
using namespace hb::player;

// Helper to create a weapon item
static auto make_weapon(int16_t attack = 20) -> item
{
    item itm;
    itm.type = item_type::weapon;
    itm.attack_power = attack;
    itm.durability = 100;
    itm.max_durability = 100;
    return itm;
}

// Helper to create an armor item
static auto make_armor(int16_t defense = 30) -> item
{
    item itm;
    itm.type = item_type::armor;
    itm.defense = defense;
    itm.durability = 100;
    itm.max_durability = 100;
    return itm;
}

// Helper to create an accessory with an effect
static auto make_accessory(item_effect_type etype, int16_t value) -> item
{
    item itm;
    itm.type = item_type::accessory;
    itm.durability = 100;
    itm.max_durability = 100;
    itm.effects[0] = {etype, value};
    return itm;
}

// --- Upgrade level on weapons ---

TEST(item_attribute_stats_test, weapon_upgrade_adds_attack_power)
{
    auto itm = make_weapon(20);
    itm.attribute.upgrade_level = 5;

    stat_modifiers mods;
    apply_item_base_stats(itm, mods);
    apply_item_attribute(itm, mods);

    // base 20 + upgrade 5
    EXPECT_EQ(mods.attack_power, 25);
    EXPECT_EQ(mods.magic_power, 5); // +N magic power too
}

TEST(item_attribute_stats_test, weapon_upgrade_max)
{
    auto itm = make_weapon(10);
    itm.attribute.upgrade_level = 15;

    stat_modifiers mods;
    apply_item_base_stats(itm, mods);
    apply_item_attribute(itm, mods);

    EXPECT_EQ(mods.attack_power, 25); // 10 + 15
    EXPECT_EQ(mods.magic_power, 15);
}

TEST(item_attribute_stats_test, weapon_upgrade_zero_no_change)
{
    auto itm = make_weapon(20);
    // upgrade_level defaults to 0

    stat_modifiers mods;
    apply_item_base_stats(itm, mods);
    apply_item_attribute(itm, mods);

    EXPECT_EQ(mods.attack_power, 20);
    EXPECT_EQ(mods.magic_power, 0);
}

// --- Upgrade level on armor ---

TEST(item_attribute_stats_test, armor_upgrade_adds_defense_and_absorption)
{
    auto itm = make_armor(30);
    itm.attribute.upgrade_level = 3;

    stat_modifiers mods;
    apply_item_base_stats(itm, mods);
    apply_item_attribute(itm, mods);

    EXPECT_EQ(mods.defense, 45); // 30 + 3*5
    EXPECT_EQ(mods.physical_absorption, 3);
}

TEST(item_attribute_stats_test, armor_upgrade_max)
{
    auto itm = make_armor(10);
    itm.attribute.upgrade_level = 15;

    stat_modifiers mods;
    apply_item_base_stats(itm, mods);
    apply_item_attribute(itm, mods);

    EXPECT_EQ(mods.defense, 85); // 10 + 15*5
    EXPECT_EQ(mods.physical_absorption, 15);
}

// --- Upgrade level on accessories ---

TEST(item_attribute_stats_test, accessory_upgrade_scales_effects)
{
    auto itm = make_accessory(item_effect_type::hp_bonus, 30);
    itm.attribute.upgrade_level = 15;

    stat_modifiers mods;
    apply_item_base_stats(itm, mods);
    apply_item_attribute(itm, mods);

    // Base 30 + scaled: 30*15/15 = 30
    EXPECT_EQ(mods.hp_bonus, 60);
}

TEST(item_attribute_stats_test, accessory_upgrade_low_level_gives_minimum_bonus)
{
    auto itm = make_accessory(item_effect_type::str_bonus, 5);
    itm.attribute.upgrade_level = 1;

    stat_modifiers mods;
    apply_item_base_stats(itm, mods);
    apply_item_attribute(itm, mods);

    // Base 5 + scaled: 5*1/15=0 → minimum 1
    EXPECT_EQ(mods.strength, 6);
}

// --- Sharp enchantment ---

TEST(item_attribute_stats_test, sharp_adds_weapon_dice_bonus)
{
    auto itm = make_weapon(20);
    itm.attribute.main_type = enchantment_type::sharp;

    stat_modifiers mods;
    apply_item_base_stats(itm, mods);
    apply_item_attribute(itm, mods);

    EXPECT_EQ(mods.weapon_dice_bonus, 1);
}

// --- Ancient enchantment ---

TEST(item_attribute_stats_test, ancient_adds_two_weapon_dice_bonus)
{
    auto itm = make_weapon(20);
    itm.attribute.main_type = enchantment_type::ancient;

    stat_modifiers mods;
    apply_item_base_stats(itm, mods);
    apply_item_attribute(itm, mods);

    EXPECT_EQ(mods.weapon_dice_bonus, 2);
}

// --- Mana conversion ---

TEST(item_attribute_stats_test, mana_conversion_sets_value)
{
    auto itm = make_weapon(20);
    itm.attribute.main_type = enchantment_type::mana_conversion;
    itm.attribute.main_value = 10;

    stat_modifiers mods;
    apply_item_base_stats(itm, mods);
    apply_item_attribute(itm, mods);

    EXPECT_EQ(mods.mana_conversion, 10);
}

// --- Charge critical ---

TEST(item_attribute_stats_test, charge_critical_sets_value)
{
    auto itm = make_weapon(20);
    itm.attribute.main_type = enchantment_type::charge_critical;
    itm.attribute.main_value = 8;

    stat_modifiers mods;
    apply_item_base_stats(itm, mods);
    apply_item_attribute(itm, mods);

    EXPECT_EQ(mods.charge_critical, 8);
}

// --- Sub enchantments ---

TEST(item_attribute_stats_test, sub_physical_resist)
{
    auto itm = make_armor(10);
    itm.attribute.sub_type = sub_enchantment_type::physical_resist;
    itm.attribute.sub_value = 3;

    stat_modifiers mods;
    apply_item_base_stats(itm, mods);
    apply_item_attribute(itm, mods);

    EXPECT_EQ(mods.physical_resist, 21); // 3 * 7
}

TEST(item_attribute_stats_test, sub_attack_rating)
{
    auto itm = make_weapon(10);
    itm.attribute.sub_type = sub_enchantment_type::attack_rating;
    itm.attribute.sub_value = 5;

    stat_modifiers mods;
    apply_item_base_stats(itm, mods);
    apply_item_attribute(itm, mods);

    EXPECT_EQ(mods.hit_bonus, 35); // 5 * 7
}

TEST(item_attribute_stats_test, sub_defense_rating)
{
    auto itm = make_armor(10);
    itm.attribute.sub_type = sub_enchantment_type::defense_rating;
    itm.attribute.sub_value = 2;

    stat_modifiers mods;
    apply_item_base_stats(itm, mods);
    apply_item_attribute(itm, mods);

    EXPECT_EQ(mods.defense, 24); // 10 + 2*7
}

TEST(item_attribute_stats_test, sub_hp_recovery)
{
    auto itm = make_armor(10);
    itm.attribute.sub_type = sub_enchantment_type::hp_recovery;
    itm.attribute.sub_value = 4;

    stat_modifiers mods;
    apply_item_base_stats(itm, mods);
    apply_item_attribute(itm, mods);

    EXPECT_EQ(mods.hp_bonus, 28); // 4 * 7
}

TEST(item_attribute_stats_test, sub_magic_absorption)
{
    auto itm = make_armor(10);
    itm.attribute.sub_type = sub_enchantment_type::magic_absorption;
    itm.attribute.sub_value = 5;

    stat_modifiers mods;
    apply_item_base_stats(itm, mods);
    apply_item_attribute(itm, mods);

    EXPECT_EQ(mods.magic_absorption, 15); // 5 * 3
}

TEST(item_attribute_stats_test, sub_exp_bonus)
{
    auto itm = make_armor(10);
    itm.attribute.sub_type = sub_enchantment_type::exp_bonus;
    itm.attribute.sub_value = 2;

    stat_modifiers mods;
    apply_item_base_stats(itm, mods);
    apply_item_attribute(itm, mods);

    EXPECT_EQ(mods.exp_bonus_percent, 20); // 2 * 10
}

TEST(item_attribute_stats_test, sub_gold_bonus)
{
    auto itm = make_armor(10);
    itm.attribute.sub_type = sub_enchantment_type::gold_bonus;
    itm.attribute.sub_value = 3;

    stat_modifiers mods;
    apply_item_base_stats(itm, mods);
    apply_item_attribute(itm, mods);

    EXPECT_EQ(mods.gold_bonus_percent, 30); // 3 * 10
}

TEST(item_attribute_stats_test, sub_critical_damage)
{
    auto itm = make_weapon(10);
    itm.attribute.sub_type = sub_enchantment_type::critical_damage;
    itm.attribute.sub_value = 5;

    stat_modifiers mods;
    apply_item_base_stats(itm, mods);
    apply_item_attribute(itm, mods);

    EXPECT_EQ(mods.critical_damage, 5);
}

// --- Custom-made bypasses level requirement ---

TEST(item_attribute_stats_test, custom_made_bypasses_level)
{
    item itm;
    itm.level_requirement = 100;
    itm.attribute.custom_made = true;

    auto req = check_requirements(itm, 1, 100, 100, 100, 100);
    EXPECT_TRUE(req.meets_level);
    EXPECT_TRUE(req.can_use());
}

TEST(item_attribute_stats_test, non_custom_fails_level_check)
{
    item itm;
    itm.level_requirement = 100;

    auto req = check_requirements(itm, 1, 100, 100, 100, 100);
    EXPECT_FALSE(req.meets_level);
    EXPECT_FALSE(req.can_use());
}

// --- Multiple items stack correctly ---

TEST(item_attribute_stats_test, multiple_items_stack)
{
    auto weapon = make_weapon(20);
    weapon.attribute.upgrade_level = 3;
    weapon.attribute.main_type = enchantment_type::sharp;
    weapon.attribute.sub_type = sub_enchantment_type::attack_rating;
    weapon.attribute.sub_value = 2;

    auto armor = make_armor(30);
    armor.attribute.upgrade_level = 5;
    armor.attribute.sub_type = sub_enchantment_type::hp_recovery;
    armor.attribute.sub_value = 3;

    stat_modifiers mods;
    apply_item_base_stats(weapon, mods);
    apply_item_attribute(weapon, mods);
    apply_item_base_stats(armor, mods);
    apply_item_attribute(armor, mods);

    // Weapon: 20 attack + 3 upgrade = 23
    EXPECT_EQ(mods.attack_power, 23);
    // Weapon: 3 magic from upgrade
    EXPECT_EQ(mods.magic_power, 3);
    // Armor: 30 defense + 5*5 upgrade = 55
    EXPECT_EQ(mods.defense, 55);
    // Weapon: sharp → +1 dice
    EXPECT_EQ(mods.weapon_dice_bonus, 1);
    // Weapon: attack_rating sub 2*7 = 14
    EXPECT_EQ(mods.hit_bonus, 14);
    // Armor: hp_recovery sub 3*7 = 21
    EXPECT_EQ(mods.hp_bonus, 21);
    // Armor: upgrade 5 → 5% physical absorption
    EXPECT_EQ(mods.physical_absorption, 5);
}

// --- Broken items give no attribute bonuses ---

TEST(item_attribute_stats_test, broken_item_no_attribute_bonus)
{
    auto itm = make_weapon(20);
    itm.attribute.upgrade_level = 10;
    itm.attribute.main_type = enchantment_type::sharp;
    itm.durability = 0; // broken

    stat_modifiers mods;
    apply_item_base_stats(itm, mods);
    apply_item_attribute(itm, mods);

    EXPECT_EQ(mods.attack_power, 0); // broken = no stats
    EXPECT_EQ(mods.weapon_dice_bonus, 0);
}

// --- Computed stats clamp new fields ---

TEST(item_attribute_stats_test, computed_stats_clamp_absorption)
{
    stat_modifiers mods;
    mods.physical_absorption = 100; // over cap
    mods.magic_absorption = 100;    // over cap
    mods.charge_critical = 30;      // over cap
    mods.mana_conversion = 30;      // over cap

    base_stats base;
    computed_stats computed;
    computed.compute(base, mods);

    EXPECT_EQ(computed.physical_absorption, 80);
    EXPECT_EQ(computed.magic_absorption, 80);
    EXPECT_EQ(computed.charge_critical, 20);
    EXPECT_EQ(computed.mana_conversion, 20);
}

// --- stat_modifiers operator+ includes new fields ---

TEST(item_attribute_stats_test, stat_modifiers_add_includes_new_fields)
{
    stat_modifiers a;
    a.physical_absorption = 5;
    a.magic_absorption = 3;
    a.exp_bonus_percent = 10;
    a.gold_bonus_percent = 20;
    a.weapon_dice_bonus = 1;
    a.charge_critical = 5;
    a.mana_conversion = 3;

    stat_modifiers b;
    b.physical_absorption = 3;
    b.magic_absorption = 2;
    b.exp_bonus_percent = 5;
    b.gold_bonus_percent = 10;
    b.weapon_dice_bonus = 2;
    b.charge_critical = 3;
    b.mana_conversion = 2;

    auto result = a + b;
    EXPECT_EQ(result.physical_absorption, 8);
    EXPECT_EQ(result.magic_absorption, 5);
    EXPECT_EQ(result.exp_bonus_percent, 15);
    EXPECT_EQ(result.gold_bonus_percent, 30);
    EXPECT_EQ(result.weapon_dice_bonus, 3);
    EXPECT_EQ(result.charge_critical, 8);
    EXPECT_EQ(result.mana_conversion, 5);
}

// --- clear() zeroes new fields ---

TEST(item_attribute_stats_test, stat_modifiers_clear_zeroes_new_fields)
{
    stat_modifiers mods;
    mods.physical_absorption = 10;
    mods.exp_bonus_percent = 20;
    mods.weapon_dice_bonus = 2;
    mods.charge_critical = 5;
    mods.mana_conversion = 3;

    mods.clear();

    EXPECT_EQ(mods.physical_absorption, 0);
    EXPECT_EQ(mods.exp_bonus_percent, 0);
    EXPECT_EQ(mods.weapon_dice_bonus, 0);
    EXPECT_EQ(mods.charge_critical, 0);
    EXPECT_EQ(mods.mana_conversion, 0);
}

// --- Empty attribute is no-op ---

TEST(item_attribute_stats_test, empty_attribute_no_change)
{
    auto itm = make_weapon(20);
    // attribute is default empty

    stat_modifiers mods;
    apply_item_base_stats(itm, mods);

    stat_modifiers before = mods;
    apply_item_attribute(itm, mods);

    EXPECT_EQ(mods.attack_power, before.attack_power);
    EXPECT_EQ(mods.defense, before.defense);
}
