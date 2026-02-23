#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

#include "item/item.h"
#include "item/item_serialization.h"

using hb::item_id;
using hb::player_id;
using namespace hb::item;
using json = nlohmann::json;

// --- Helper: build a basic weapon item ---

static auto make_weapon() -> item
{
    item itm;
    itm.id = item_id(12345);
    itm.template_id = item_id(100);
    itm.name = "Barbarian Sword";
    itm.type = item_type::weapon;
    itm.equip_position = equip_pos::weapon;
    itm.weapon = weapon_type::sword;
    itm.rarity = item_rarity::rare;
    itm.count = 1;
    itm.weight = 800;
    itm.price = 15000;
    itm.damage_min = 18;
    itm.damage_max = 66;
    itm.defense = 0;
    itm.magic_defense = 0;
    itm.durability = 85;
    itm.max_durability = 100;
    itm.level_requirement = 30;
    itm.str_requirement = 50;
    itm.dex_requirement = 20;
    itm.int_requirement = 0;
    itm.mag_requirement = 0;
    itm.color = 0;
    itm.tradeable = true;
    itm.droppable = true;
    itm.two_handed = false;
    return itm;
}

// --- Helper: build a basic armor item ---

static auto make_armor() -> item
{
    item itm;
    itm.id = item_id(50000);
    itm.template_id = item_id(200);
    itm.name = "Plate Mail";
    itm.type = item_type::armor;
    itm.equip_position = equip_pos::body;
    itm.weapon = weapon_type::none;
    itm.rarity = item_rarity::uncommon;
    itm.count = 1;
    itm.weight = 1200;
    itm.price = 8000;
    itm.damage_min = 0;
    itm.damage_max = 0;
    itm.defense = 25;
    itm.magic_defense = 5;
    itm.durability = 200;
    itm.max_durability = 200;
    itm.level_requirement = 20;
    itm.str_requirement = 40;
    itm.dex_requirement = 0;
    itm.int_requirement = 0;
    itm.mag_requirement = 0;
    itm.color = 3;
    itm.tradeable = true;
    itm.droppable = true;
    itm.two_handed = false;
    return itm;
}

// ============================================================
// 1. Serialize a basic weapon and verify all JSON fields
// ============================================================

TEST(item_serialization_test, serialize_weapon_all_fields)
{
    auto itm = make_weapon();
    itm.effects[0] = {item_effect_type::str_bonus, 3};
    itm.effects[1] = {item_effect_type::hit_bonus, 5};

    auto j = serialize_item(itm);

    EXPECT_EQ(j["item_id"], 12345);
    EXPECT_EQ(j["template_id"], 100);
    EXPECT_EQ(j["name"], "Barbarian Sword");
    EXPECT_EQ(j["type"], "weapon");
    EXPECT_EQ(j["equip_pos"], "weapon");
    EXPECT_EQ(j["weapon_type"], "sword");
    EXPECT_EQ(j["rarity"], "rare");
    EXPECT_EQ(j["count"], 1);
    EXPECT_EQ(j["weight"], 800);
    EXPECT_EQ(j["price"], 15000);
    EXPECT_EQ(j["damage_min"], 18);
    EXPECT_EQ(j["damage_max"], 66);
    EXPECT_EQ(j["defense"], 0);
    EXPECT_EQ(j["magic_defense"], 0);
    EXPECT_EQ(j["durability"], 85);
    EXPECT_EQ(j["max_durability"], 100);
    EXPECT_EQ(j["level_req"], 30);
    EXPECT_EQ(j["str_req"], 50);
    EXPECT_EQ(j["dex_req"], 20);
    EXPECT_EQ(j["int_req"], 0);
    EXPECT_EQ(j["mag_req"], 0);
    EXPECT_EQ(j["color"], 0);
    EXPECT_EQ(j["tradeable"], true);
    EXPECT_EQ(j["droppable"], true);
    EXPECT_EQ(j["two_handed"], false);

    // Effects array
    ASSERT_EQ(j["effects"].size(), 2u);
    EXPECT_EQ(j["effects"][0]["type"], "str_bonus");
    EXPECT_EQ(j["effects"][0]["value"], 3);
    EXPECT_EQ(j["effects"][1]["type"], "hit_bonus");
    EXPECT_EQ(j["effects"][1]["value"], 5);

    // No attribute since default is empty
    EXPECT_FALSE(j.contains("attribute"));

    // No bound_to since default is nullopt
    EXPECT_FALSE(j.contains("bound_to"));
}

// ============================================================
// 2. Armor: weapon_type field is omitted
// ============================================================

TEST(item_serialization_test, serialize_armor_no_weapon_type)
{
    auto itm = make_armor();
    auto j = serialize_item(itm);

    EXPECT_EQ(j["type"], "armor");
    EXPECT_EQ(j["equip_pos"], "body");
    EXPECT_FALSE(j.contains("weapon_type"));
    EXPECT_EQ(j["defense"], 25);
    EXPECT_EQ(j["magic_defense"], 5);
}

// ============================================================
// 3. Serialize item with attribute data
// ============================================================

TEST(item_serialization_test, serialize_with_attribute)
{
    auto itm = make_weapon();
    itm.attribute.upgrade_level = 3;
    itm.attribute.main_type = enchantment_type::sharp;
    itm.attribute.main_value = 2;

    auto j = serialize_item(itm);

    // Name gets "+3" suffix
    EXPECT_EQ(j["name"], "Barbarian Sword +3");

    ASSERT_TRUE(j.contains("attribute"));
    auto& attr = j["attribute"];
    EXPECT_EQ(attr["upgrade_level"], 3);

    ASSERT_TRUE(attr.contains("main_enchant"));
    EXPECT_FALSE(attr["main_enchant"].is_null());
    EXPECT_EQ(attr["main_enchant"]["type"], "sharp");
    EXPECT_EQ(attr["main_enchant"]["value"], 2);

    // sub_enchant is null (none)
    EXPECT_TRUE(attr["sub_enchant"].is_null());

    EXPECT_EQ(attr["custom_made"], false);
}

TEST(item_serialization_test, serialize_attribute_with_sub_enchant)
{
    auto itm = make_armor();
    itm.attribute.upgrade_level = 5;
    itm.attribute.sub_type = sub_enchantment_type::hp_recovery;
    itm.attribute.sub_value = 4;

    auto j = serialize_item(itm);

    ASSERT_TRUE(j.contains("attribute"));
    auto& attr = j["attribute"];
    EXPECT_EQ(attr["upgrade_level"], 5);
    EXPECT_TRUE(attr["main_enchant"].is_null());
    EXPECT_EQ(attr["sub_enchant"]["type"], "hp_recovery");
    EXPECT_EQ(attr["sub_enchant"]["value"], 4);
}

TEST(item_serialization_test, serialize_attribute_custom_made)
{
    auto itm = make_weapon();
    itm.attribute.custom_made = true;

    auto j = serialize_item(itm);

    ASSERT_TRUE(j.contains("attribute"));
    EXPECT_EQ(j["attribute"]["custom_made"], true);
}

// ============================================================
// 4. No attribute when empty
// ============================================================

TEST(item_serialization_test, serialize_no_attribute_when_empty)
{
    auto itm = make_weapon();
    // attribute is default-constructed (empty)
    auto j = serialize_item(itm);
    EXPECT_FALSE(j.contains("attribute"));
}

// ============================================================
// 5. Effects array
// ============================================================

TEST(item_serialization_test, serialize_effects_only_nonempty)
{
    auto itm = make_armor();
    itm.effects[0] = {item_effect_type::defense_bonus, 10};
    itm.effects[1] = {item_effect_type::none, 0}; // empty, should be skipped
    itm.effects[2] = {item_effect_type::hp_bonus, 20};

    auto j = serialize_item(itm);

    ASSERT_EQ(j["effects"].size(), 2u);
    EXPECT_EQ(j["effects"][0]["type"], "defense_bonus");
    EXPECT_EQ(j["effects"][0]["value"], 10);
    EXPECT_EQ(j["effects"][1]["type"], "hp_bonus");
    EXPECT_EQ(j["effects"][1]["value"], 20);
}

TEST(item_serialization_test, serialize_no_effects_produces_empty_array)
{
    auto itm = make_weapon();
    // All effects are default (none)
    auto j = serialize_item(itm);
    EXPECT_TRUE(j["effects"].is_array());
    EXPECT_EQ(j["effects"].size(), 0u);
}

// ============================================================
// 6. Bound-to field
// ============================================================

TEST(item_serialization_test, serialize_bound_item)
{
    auto itm = make_weapon();
    itm.bound_to = player_id(42);

    auto j = serialize_item(itm);

    ASSERT_TRUE(j.contains("bound_to"));
    EXPECT_EQ(j["bound_to"], 42);
}

TEST(item_serialization_test, serialize_unbound_item_omits_bound_to)
{
    auto itm = make_weapon();
    // bound_to is nullopt by default
    auto j = serialize_item(itm);
    EXPECT_FALSE(j.contains("bound_to"));
}

// ============================================================
// Enum converter tests
// ============================================================

TEST(item_serialization_test, item_type_to_string_all)
{
    EXPECT_EQ(item_type_to_string(item_type::none), "none");
    EXPECT_EQ(item_type_to_string(item_type::weapon), "weapon");
    EXPECT_EQ(item_type_to_string(item_type::armor), "armor");
    EXPECT_EQ(item_type_to_string(item_type::accessory), "accessory");
    EXPECT_EQ(item_type_to_string(item_type::consumable), "consumable");
    EXPECT_EQ(item_type_to_string(item_type::material), "material");
    EXPECT_EQ(item_type_to_string(item_type::quest), "quest");
    EXPECT_EQ(item_type_to_string(item_type::gold), "gold");
}

TEST(item_serialization_test, equip_pos_to_string_all)
{
    EXPECT_EQ(equip_pos_to_string(equip_pos::none), "none");
    EXPECT_EQ(equip_pos_to_string(equip_pos::head), "head");
    EXPECT_EQ(equip_pos_to_string(equip_pos::body), "body");
    EXPECT_EQ(equip_pos_to_string(equip_pos::arms), "arms");
    EXPECT_EQ(equip_pos_to_string(equip_pos::pants), "pants");
    EXPECT_EQ(equip_pos_to_string(equip_pos::boots), "boots");
    EXPECT_EQ(equip_pos_to_string(equip_pos::weapon), "weapon");
    EXPECT_EQ(equip_pos_to_string(equip_pos::shield), "shield");
    EXPECT_EQ(equip_pos_to_string(equip_pos::twohand), "twohand");
    EXPECT_EQ(equip_pos_to_string(equip_pos::ring_left), "ring_left");
    EXPECT_EQ(equip_pos_to_string(equip_pos::ring_right), "ring_right");
    EXPECT_EQ(equip_pos_to_string(equip_pos::amulet), "amulet");
    EXPECT_EQ(equip_pos_to_string(equip_pos::cape), "cape");
    EXPECT_EQ(equip_pos_to_string(equip_pos::angel), "angel");
    EXPECT_EQ(equip_pos_to_string(equip_pos::fullbody), "fullbody");
}

TEST(item_serialization_test, weapon_type_to_string_all)
{
    EXPECT_EQ(weapon_type_to_string(weapon_type::none), "none");
    EXPECT_EQ(weapon_type_to_string(weapon_type::sword), "sword");
    EXPECT_EQ(weapon_type_to_string(weapon_type::axe), "axe");
    EXPECT_EQ(weapon_type_to_string(weapon_type::hammer), "hammer");
    EXPECT_EQ(weapon_type_to_string(weapon_type::staff), "staff");
    EXPECT_EQ(weapon_type_to_string(weapon_type::wand), "wand");
    EXPECT_EQ(weapon_type_to_string(weapon_type::bow), "bow");
    EXPECT_EQ(weapon_type_to_string(weapon_type::dagger), "dagger");
    EXPECT_EQ(weapon_type_to_string(weapon_type::fist), "fist");
}

TEST(item_serialization_test, rarity_to_string_all)
{
    EXPECT_EQ(rarity_to_string(item_rarity::common), "common");
    EXPECT_EQ(rarity_to_string(item_rarity::uncommon), "uncommon");
    EXPECT_EQ(rarity_to_string(item_rarity::rare), "rare");
    EXPECT_EQ(rarity_to_string(item_rarity::epic), "epic");
    EXPECT_EQ(rarity_to_string(item_rarity::legendary), "legendary");
    EXPECT_EQ(rarity_to_string(item_rarity::ancient), "ancient");
}

TEST(item_serialization_test, effect_type_to_string_all)
{
    EXPECT_EQ(effect_type_to_string(item_effect_type::none), "none");
    EXPECT_EQ(effect_type_to_string(item_effect_type::hp_bonus), "hp_bonus");
    EXPECT_EQ(effect_type_to_string(item_effect_type::mp_bonus), "mp_bonus");
    EXPECT_EQ(effect_type_to_string(item_effect_type::sp_bonus), "sp_bonus");
    EXPECT_EQ(effect_type_to_string(item_effect_type::str_bonus), "str_bonus");
    EXPECT_EQ(effect_type_to_string(item_effect_type::dex_bonus), "dex_bonus");
    EXPECT_EQ(effect_type_to_string(item_effect_type::vit_bonus), "vit_bonus");
    EXPECT_EQ(effect_type_to_string(item_effect_type::int_bonus), "int_bonus");
    EXPECT_EQ(effect_type_to_string(item_effect_type::mag_bonus), "mag_bonus");
    EXPECT_EQ(effect_type_to_string(item_effect_type::chr_bonus), "chr_bonus");
    EXPECT_EQ(effect_type_to_string(item_effect_type::attack_bonus), "attack_bonus");
    EXPECT_EQ(effect_type_to_string(item_effect_type::defense_bonus), "defense_bonus");
    EXPECT_EQ(effect_type_to_string(item_effect_type::magic_attack_bonus), "magic_attack_bonus");
    EXPECT_EQ(effect_type_to_string(item_effect_type::magic_defense_bonus), "magic_defense_bonus");
    EXPECT_EQ(effect_type_to_string(item_effect_type::hit_bonus), "hit_bonus");
    EXPECT_EQ(effect_type_to_string(item_effect_type::dodge_bonus), "dodge_bonus");
    EXPECT_EQ(effect_type_to_string(item_effect_type::critical_bonus), "critical_bonus");
    EXPECT_EQ(effect_type_to_string(item_effect_type::hp_steal), "hp_steal");
    EXPECT_EQ(effect_type_to_string(item_effect_type::mp_steal), "mp_steal");
    EXPECT_EQ(effect_type_to_string(item_effect_type::damage_reduction), "damage_reduction");
    EXPECT_EQ(effect_type_to_string(item_effect_type::exp_bonus), "exp_bonus");
    EXPECT_EQ(effect_type_to_string(item_effect_type::gold_bonus), "gold_bonus");
    EXPECT_EQ(effect_type_to_string(item_effect_type::poison_damage), "poison_damage");
    EXPECT_EQ(effect_type_to_string(item_effect_type::fire_damage), "fire_damage");
    EXPECT_EQ(effect_type_to_string(item_effect_type::ice_damage), "ice_damage");
    EXPECT_EQ(effect_type_to_string(item_effect_type::lightning_damage), "lightning_damage");
}

TEST(item_serialization_test, special_ability_type_to_string_all)
{
    EXPECT_EQ(special_ability_type_to_string(special_ability_type::none), "none");
    EXPECT_EQ(special_ability_type_to_string(special_ability_type::hp_halve), "hp_halve");
    EXPECT_EQ(special_ability_type_to_string(special_ability_type::poison), "poison");
    EXPECT_EQ(special_ability_type_to_string(special_ability_type::paralyze), "paralyze");
    EXPECT_EQ(special_ability_type_to_string(special_ability_type::warrior_boost), "warrior_boost");
    EXPECT_EQ(special_ability_type_to_string(special_ability_type::life_drain), "life_drain");
    EXPECT_EQ(special_ability_type_to_string(special_ability_type::spell_immunity), "spell_immunity");
    EXPECT_EQ(special_ability_type_to_string(special_ability_type::attack_block), "attack_block");
    EXPECT_EQ(special_ability_type_to_string(special_ability_type::high_spell_immunity), "high_spell_immunity");
}

TEST(item_serialization_test, enchantment_type_to_string_all)
{
    EXPECT_EQ(enchantment_type_to_string(enchantment_type::none), "none");
    EXPECT_EQ(enchantment_type_to_string(enchantment_type::critical_bonus), "critical_bonus");
    EXPECT_EQ(enchantment_type_to_string(enchantment_type::poison), "poison");
    EXPECT_EQ(enchantment_type_to_string(enchantment_type::righteous), "righteous");
    EXPECT_EQ(enchantment_type_to_string(enchantment_type::spell_on_hit), "spell_on_hit");
    EXPECT_EQ(enchantment_type_to_string(enchantment_type::damage_reduction), "damage_reduction");
    EXPECT_EQ(enchantment_type_to_string(enchantment_type::light), "light");
    EXPECT_EQ(enchantment_type_to_string(enchantment_type::sharp), "sharp");
    EXPECT_EQ(enchantment_type_to_string(enchantment_type::fire), "fire");
    EXPECT_EQ(enchantment_type_to_string(enchantment_type::ancient), "ancient");
    EXPECT_EQ(enchantment_type_to_string(enchantment_type::magic_damage), "magic_damage");
    EXPECT_EQ(enchantment_type_to_string(enchantment_type::mana_conversion), "mana_conversion");
    EXPECT_EQ(enchantment_type_to_string(enchantment_type::charge_critical), "charge_critical");
}

TEST(item_serialization_test, sub_enchantment_type_to_string_all)
{
    EXPECT_EQ(sub_enchantment_type_to_string(sub_enchantment_type::none), "none");
    EXPECT_EQ(sub_enchantment_type_to_string(sub_enchantment_type::physical_resist), "physical_resist");
    EXPECT_EQ(sub_enchantment_type_to_string(sub_enchantment_type::attack_rating), "attack_rating");
    EXPECT_EQ(sub_enchantment_type_to_string(sub_enchantment_type::defense_rating), "defense_rating");
    EXPECT_EQ(sub_enchantment_type_to_string(sub_enchantment_type::hp_recovery), "hp_recovery");
    EXPECT_EQ(sub_enchantment_type_to_string(sub_enchantment_type::sp_recovery), "sp_recovery");
    EXPECT_EQ(sub_enchantment_type_to_string(sub_enchantment_type::mp_recovery), "mp_recovery");
    EXPECT_EQ(sub_enchantment_type_to_string(sub_enchantment_type::magic_resist), "magic_resist");
    EXPECT_EQ(sub_enchantment_type_to_string(sub_enchantment_type::physical_absorption), "physical_absorption");
    EXPECT_EQ(sub_enchantment_type_to_string(sub_enchantment_type::magic_absorption), "magic_absorption");
    EXPECT_EQ(sub_enchantment_type_to_string(sub_enchantment_type::critical_damage), "critical_damage");
    EXPECT_EQ(sub_enchantment_type_to_string(sub_enchantment_type::exp_bonus), "exp_bonus");
    EXPECT_EQ(sub_enchantment_type_to_string(sub_enchantment_type::gold_bonus), "gold_bonus");
}

// ============================================================
// Edge cases
// ============================================================

TEST(item_serialization_test, serialize_two_handed_weapon)
{
    auto itm = make_weapon();
    itm.equip_position = equip_pos::twohand;
    itm.two_handed = true;

    auto j = serialize_item(itm);

    EXPECT_EQ(j["equip_pos"], "twohand");
    EXPECT_EQ(j["two_handed"], true);
}

TEST(item_serialization_test, serialize_consumable_no_weapon_type)
{
    item itm;
    itm.id = item_id(99);
    itm.template_id = item_id(500);
    itm.name = "Health Potion";
    itm.type = item_type::consumable;
    itm.count = 5;

    auto j = serialize_item(itm);

    EXPECT_EQ(j["type"], "consumable");
    EXPECT_FALSE(j.contains("weapon_type"));
    EXPECT_EQ(j["count"], 5);
}

TEST(item_serialization_test, serialize_attribute_both_enchants)
{
    auto itm = make_weapon();
    itm.attribute.upgrade_level = 7;
    itm.attribute.main_type = enchantment_type::fire;
    itm.attribute.main_value = 5;
    itm.attribute.sub_type = sub_enchantment_type::exp_bonus;
    itm.attribute.sub_value = 3;
    itm.attribute.custom_made = true;

    auto j = serialize_item(itm);

    EXPECT_EQ(j["name"], "Barbarian Sword +7");

    auto& attr = j["attribute"];
    EXPECT_EQ(attr["upgrade_level"], 7);
    EXPECT_EQ(attr["main_enchant"]["type"], "fire");
    EXPECT_EQ(attr["main_enchant"]["value"], 5);
    EXPECT_EQ(attr["sub_enchant"]["type"], "exp_bonus");
    EXPECT_EQ(attr["sub_enchant"]["value"], 3);
    EXPECT_EQ(attr["custom_made"], true);
}

TEST(item_serialization_test, serialize_damage_min_max_zero_for_non_weapon)
{
    auto itm = make_armor();
    auto j = serialize_item(itm);

    EXPECT_EQ(j["damage_min"], 0);
    EXPECT_EQ(j["damage_max"], 0);
}
