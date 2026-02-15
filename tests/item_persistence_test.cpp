#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

#include "auth/character_serialization.h"
#include "item/item_attribute.h"
#include "item/item_system.h"
#include "player/equipment.h"
#include "inventory/inventory.h"

using namespace hb;
using namespace hb::auth;
using namespace hb::item;
using namespace hb::player;

// --- Equipment serialization with attributes ---

TEST(item_persistence_test, serialize_equipment_with_no_attributes)
{
    equipment_state equip;
    equip.equip(equip_slot::weapon, item_id{100}, 80, 100);

    auto json = serialize_equipment_with_attributes(equip, {});
    auto result = deserialize_equipment_with_attributes(json);

    EXPECT_TRUE(result.equipment.has_equipped(equip_slot::weapon));
    EXPECT_EQ(result.equipment.weapon().id.value, 100u);
    EXPECT_TRUE(result.attributes.empty());
}

TEST(item_persistence_test, serialize_equipment_with_attributes)
{
    equipment_state equip;
    equip.equip(equip_slot::weapon, item_id{100}, 80, 100);
    equip.equip(equip_slot::body, item_id{200}, 90, 100);

    std::vector<equipment_slot_attribute> attrs;
    attrs.push_back({.slot = static_cast<size_t>(equip_slot::weapon),
                     .attribute = {.upgrade_level = 7,
                                   .main_type = enchantment_type::sharp,
                                   .main_value = 1,
                                   .sub_type = sub_enchantment_type::physical_resist,
                                   .sub_value = 3,
                                   .custom_made = false,
                                   .custom_quality = 0}});
    attrs.push_back({.slot = static_cast<size_t>(equip_slot::body),
                     .attribute = {.upgrade_level = 5,
                                   .main_type = enchantment_type::none,
                                   .main_value = 0,
                                   .sub_type = sub_enchantment_type::hp_recovery,
                                   .sub_value = 4,
                                   .custom_made = true,
                                   .custom_quality = 42}});

    auto json = serialize_equipment_with_attributes(equip, attrs);
    auto result = deserialize_equipment_with_attributes(json);

    EXPECT_TRUE(result.equipment.has_equipped(equip_slot::weapon));
    EXPECT_TRUE(result.equipment.has_equipped(equip_slot::body));

    ASSERT_EQ(result.attributes.size(), 2u);

    // Find weapon attribute
    const equipment_slot_attribute* weapon_attr = nullptr;
    const equipment_slot_attribute* body_attr = nullptr;
    for (const auto& a : result.attributes)
    {
        if (a.slot == static_cast<size_t>(equip_slot::weapon))
            weapon_attr = &a;
        if (a.slot == static_cast<size_t>(equip_slot::body))
            body_attr = &a;
    }

    ASSERT_NE(weapon_attr, nullptr);
    EXPECT_EQ(weapon_attr->attribute.upgrade_level, 7);
    EXPECT_EQ(weapon_attr->attribute.main_type, enchantment_type::sharp);
    EXPECT_EQ(weapon_attr->attribute.sub_type, sub_enchantment_type::physical_resist);
    EXPECT_EQ(weapon_attr->attribute.sub_value, 3);

    ASSERT_NE(body_attr, nullptr);
    EXPECT_EQ(body_attr->attribute.upgrade_level, 5);
    EXPECT_EQ(body_attr->attribute.sub_type, sub_enchantment_type::hp_recovery);
    EXPECT_EQ(body_attr->attribute.sub_value, 4);
    EXPECT_TRUE(body_attr->attribute.custom_made);
    EXPECT_EQ(body_attr->attribute.custom_quality, 42);
}

TEST(item_persistence_test, backward_compat_equipment_no_attribute_key)
{
    // Old format: no "attribute" key
    std::string old_json = R"([{"slot":5,"item_id":100,"durability":80,"max_durability":100}])";
    auto result = deserialize_equipment_with_attributes(old_json);

    EXPECT_TRUE(result.equipment.has_equipped(equip_slot::weapon));
    EXPECT_EQ(result.equipment.weapon().id.value, 100u);
    EXPECT_TRUE(result.attributes.empty());
}

TEST(item_persistence_test, empty_attributes_not_serialized)
{
    equipment_state equip;
    equip.equip(equip_slot::weapon, item_id{100}, 80, 100);

    // Provide an empty attribute — should NOT produce "attribute" key
    std::vector<equipment_slot_attribute> attrs;
    attrs.push_back({
        .slot = static_cast<size_t>(equip_slot::weapon), .attribute = {} // empty/default
    });

    auto json = serialize_equipment_with_attributes(equip, attrs);

    // Parse and check no "attribute" key present
    auto j = nlohmann::json::parse(json);
    EXPECT_FALSE(j[0].contains("attribute"));
}

// --- Inventory serialization with attributes ---

TEST(item_persistence_test, serialize_inventory_with_attributes)
{
    inventory::inventory inv(50);
    auto* slot0 = inv.get_slot(0);
    slot0->item = item_id{300};
    slot0->count = 1;
    auto* slot1 = inv.get_slot(1);
    slot1->item = item_id{400};
    slot1->count = 5;

    std::vector<inventory_slot_attribute> attrs;
    attrs.push_back(
        {.slot = 0, .attribute = {.upgrade_level = 3, .main_type = enchantment_type::poison, .main_value = 5}});

    auto json = serialize_inventory_with_attributes(inv, attrs);

    inventory::inventory inv2(50);
    std::vector<inventory_slot_attribute> loaded_attrs;
    deserialize_inventory_with_attributes(json, inv2, loaded_attrs);

    EXPECT_EQ(inv2.get_slot(0)->item.value, 300u);
    EXPECT_EQ(inv2.get_slot(0)->count, 1);
    EXPECT_EQ(inv2.get_slot(1)->item.value, 400u);
    EXPECT_EQ(inv2.get_slot(1)->count, 5);

    ASSERT_EQ(loaded_attrs.size(), 1u);
    EXPECT_EQ(loaded_attrs[0].slot, 0);
    EXPECT_EQ(loaded_attrs[0].attribute.upgrade_level, 3);
    EXPECT_EQ(loaded_attrs[0].attribute.main_type, enchantment_type::poison);
    EXPECT_EQ(loaded_attrs[0].attribute.main_value, 5);
}

TEST(item_persistence_test, backward_compat_inventory_no_attribute)
{
    std::string old_json = R"([{"slot":0,"item_id":300,"count":1}])";

    inventory::inventory inv(50);
    std::vector<inventory_slot_attribute> attrs;
    deserialize_inventory_with_attributes(old_json, inv, attrs);

    EXPECT_EQ(inv.get_slot(0)->item.value, 300u);
    EXPECT_TRUE(attrs.empty());
}

TEST(item_persistence_test, empty_string_produces_empty_result)
{
    auto equip_result = deserialize_equipment_with_attributes("");
    EXPECT_TRUE(equip_result.attributes.empty());

    inventory::inventory inv(50);
    std::vector<inventory_slot_attribute> attrs;
    deserialize_inventory_with_attributes("", inv, attrs);
    EXPECT_TRUE(attrs.empty());
}

TEST(item_persistence_test, invalid_json_produces_empty_result)
{
    auto equip_result = deserialize_equipment_with_attributes("not json");
    EXPECT_TRUE(equip_result.attributes.empty());

    inventory::inventory inv(50);
    std::vector<inventory_slot_attribute> attrs;
    deserialize_inventory_with_attributes("not json", inv, attrs);
    EXPECT_TRUE(attrs.empty());
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
