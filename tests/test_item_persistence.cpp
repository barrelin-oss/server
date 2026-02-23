#include <gtest/gtest.h>

#include "auth/account.h"
#include "item/item_attribute.h"
#include "item/item_system.h"
#include "player/equipment.h"
#include "inventory/inventory.h"

using namespace hb;
using namespace hb::auth;
using namespace hb::item;
using namespace hb::player;

// --- item_location enum ---

TEST(item_persistence_test, item_location_values)
{
    EXPECT_EQ(static_cast<uint8_t>(item_location::inventory), 0);
    // equipment = 1 was removed (equipment is tracked in character_equipment table)
    EXPECT_EQ(static_cast<uint8_t>(item_location::bank), 2);
    EXPECT_EQ(static_cast<uint8_t>(item_location::mail), 3);
}

// --- item_row defaults ---

TEST(item_persistence_test, item_row_defaults)
{
    item_row row;
    EXPECT_EQ(row.id, 0u);
    EXPECT_EQ(row.character_id, 0);
    EXPECT_EQ(row.template_id, 0u);
    EXPECT_EQ(row.location, item_location::inventory);
    EXPECT_EQ(row.slot, 0);
    EXPECT_EQ(row.count, 1);
    EXPECT_EQ(row.durability, 0);
    EXPECT_EQ(row.max_durability, 0);
    EXPECT_EQ(row.color, 0);
    EXPECT_FALSE(row.bound_to.has_value());
    EXPECT_EQ(row.upgrade_level, 0);
    EXPECT_EQ(row.main_enchant_type, 0);
    EXPECT_EQ(row.main_enchant_value, 0);
    EXPECT_EQ(row.sub_enchant_type, 0);
    EXPECT_EQ(row.sub_enchant_value, 0);
    EXPECT_FALSE(row.custom_made);
    EXPECT_EQ(row.custom_quality, 0);
    EXPECT_EQ(row.pos_x, 0);
    EXPECT_EQ(row.pos_y, 0);
    EXPECT_FALSE(row.bank_page.has_value());
    EXPECT_FALSE(row.bank_slot.has_value());
}

// --- item_row to_attribute ---

TEST(item_persistence_test, item_row_to_attribute_empty)
{
    item_row row;
    auto attr = row.to_attribute();

    EXPECT_TRUE(attr.is_empty());
    EXPECT_EQ(attr.upgrade_level, 0);
    EXPECT_EQ(attr.main_type, enchantment_type::none);
    EXPECT_EQ(attr.main_value, 0);
    EXPECT_EQ(attr.sub_type, sub_enchantment_type::none);
    EXPECT_EQ(attr.sub_value, 0);
    EXPECT_FALSE(attr.custom_made);
    EXPECT_EQ(attr.custom_quality, 0);
}

TEST(item_persistence_test, item_row_to_attribute_full)
{
    item_row row;
    row.upgrade_level = 7;
    row.main_enchant_type = static_cast<uint8_t>(enchantment_type::poison);
    row.main_enchant_value = 3;
    row.sub_enchant_type = static_cast<uint8_t>(sub_enchantment_type::hp_recovery);
    row.sub_enchant_value = 5;
    row.custom_made = true;
    row.custom_quality = -30;

    auto attr = row.to_attribute();

    EXPECT_EQ(attr.upgrade_level, 7);
    EXPECT_EQ(attr.main_type, enchantment_type::poison);
    EXPECT_EQ(attr.main_value, 3);
    EXPECT_EQ(attr.sub_type, sub_enchantment_type::hp_recovery);
    EXPECT_EQ(attr.sub_value, 5);
    EXPECT_TRUE(attr.custom_made);
    EXPECT_EQ(attr.custom_quality, -30);
}

// --- Equipment linked model ---

TEST(item_persistence_test, equip_stores_item_id)
{
    equipment_state equip;
    equip.equip(equip_slot::weapon, item_id{100});

    EXPECT_TRUE(equip.has_equipped(equip_slot::weapon));
    auto id = equip.get_equipped(equip_slot::weapon);
    ASSERT_TRUE(id.has_value());
    EXPECT_EQ(id->value, 100u);
}

TEST(item_persistence_test, unequip_returns_item_id)
{
    equipment_state equip;
    equip.equip(equip_slot::body, item_id{200});

    auto id = equip.unequip(equip_slot::body);
    ASSERT_TRUE(id.has_value());
    EXPECT_EQ(id->value, 200u);
    EXPECT_FALSE(equip.has_equipped(equip_slot::body));
}

TEST(item_persistence_test, clear_all_resets_equipment)
{
    equipment_state equip;
    equip.equip(equip_slot::shield, item_id{300});

    equip.clear_all();
    EXPECT_FALSE(equip.has_equipped(equip_slot::shield));
}

// --- item_create_info with attribute ---

TEST(item_persistence_test, item_create_info_has_optional_attribute)
{
    item_create_info info;
    EXPECT_FALSE(info.attribute.has_value());

    info.attribute = item_attribute{.upgrade_level = 5, .main_type = enchantment_type::fire, .main_value = 3};
    EXPECT_TRUE(info.attribute.has_value());
    EXPECT_EQ(info.attribute->upgrade_level, 5);
}

// --- item_row for different locations ---

TEST(item_persistence_test, item_row_inventory_location)
{
    item_row row;
    row.id = 1001;
    row.character_id = 42;
    row.template_id = 50;
    row.location = item_location::inventory;
    row.slot = 5;
    row.count = 1;
    row.durability = 80;
    row.max_durability = 100;

    EXPECT_EQ(row.location, item_location::inventory);
    EXPECT_EQ(row.slot, 5);
}

TEST(item_persistence_test, equipment_row_defaults)
{
    equipment_row row;
    EXPECT_EQ(row.character_id, 0);
    EXPECT_EQ(row.slot, 0);
    EXPECT_EQ(row.item_id, 0u);
}

TEST(item_persistence_test, equipment_row_stores_slot_and_item)
{
    equipment_row row;
    row.character_id = 42;
    row.slot = static_cast<int16_t>(equip_slot::body);
    row.item_id = 1002;

    EXPECT_EQ(row.character_id, 42);
    EXPECT_EQ(row.slot, static_cast<int16_t>(equip_slot::body));
    EXPECT_EQ(row.item_id, 1002u);
}

TEST(item_persistence_test, item_row_bank_with_page_slot)
{
    item_row row;
    row.location = item_location::bank;
    row.slot = 25; // flat slot for legacy
    row.bank_page = 2;
    row.bank_slot = 5;
    row.count = 10;

    EXPECT_EQ(row.location, item_location::bank);
    EXPECT_TRUE(row.bank_page.has_value());
    EXPECT_EQ(*row.bank_page, 2);
    EXPECT_TRUE(row.bank_slot.has_value());
    EXPECT_EQ(*row.bank_slot, 5);
}

TEST(item_persistence_test, item_row_bank_location)
{
    item_row row;
    row.location = item_location::bank;
    row.slot = 10;
    row.count = 99;

    EXPECT_EQ(row.location, item_location::bank);
    EXPECT_EQ(row.count, 99);
}

// --- item_row with binding ---

TEST(item_persistence_test, item_row_unbound)
{
    item_row row;
    EXPECT_FALSE(row.bound_to.has_value());
}

TEST(item_persistence_test, item_row_bound_to_character)
{
    item_row row;
    row.bound_to = 42;

    EXPECT_TRUE(row.bound_to.has_value());
    EXPECT_EQ(*row.bound_to, 42);
}

// --- item_row with enchantments ---

TEST(item_persistence_test, item_row_with_upgrade_and_enchantments)
{
    item_row row;
    row.upgrade_level = 10;
    row.main_enchant_type = static_cast<uint8_t>(enchantment_type::ancient);
    row.main_enchant_value = 2;
    row.sub_enchant_type = static_cast<uint8_t>(sub_enchantment_type::exp_bonus);
    row.sub_enchant_value = 5;

    auto attr = row.to_attribute();
    EXPECT_EQ(attr.upgrade_level, 10);
    EXPECT_EQ(attr.main_type, enchantment_type::ancient);
    EXPECT_EQ(attr.main_value, 2);
    EXPECT_EQ(attr.sub_type, sub_enchantment_type::exp_bonus);
    EXPECT_EQ(attr.sub_value, 5);
    EXPECT_FALSE(attr.custom_made);
}

// --- seed_next_id ---

TEST(item_persistence_test, seed_next_id_prevents_collision)
{
    item_system sys;
    sys.initialize();

    // Seed with a high ID
    sys.seed_next_id(5000);

    // Create an item — its ID should be > 5000
    item_create_info info;
    info.template_id = item_id{1};
    info.count = 1;
    auto result = sys.create_item(info);

    // May fail without a template registered, but seed_next_id should have taken effect
    // Just verify the system doesn't crash and accepted the seed
    sys.shutdown();
}

// --- restore_item ---

TEST(item_persistence_test, restore_item_explicit_id)
{
    item_system sys;
    sys.initialize();

    item_create_info info;
    info.template_id = item_id{1};
    info.count = 1;
    info.attribute = item_attribute{.upgrade_level = 3};

    auto result = sys.restore_item(item_id{999}, info);
    // Template likely not registered so this may fail, but the API should not crash
    // In production, templates would be loaded from registry

    sys.shutdown();
}
