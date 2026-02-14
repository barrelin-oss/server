// loot_attribute_test.cpp
// Tests for loot attribute generation system

#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

#include "npc/loot_config.h"
#include "npc/loot_generator.h"
#include "item/item_attribute.h"
#include "network/json_protocol.h"

using namespace hb::npc;
using hb::item::item_attribute;
using hb::item::enchantment_type;
using hb::item::sub_enchantment_type;
using hb::item_id;

// ============================================================================
// Loot attribute config
// ============================================================================

TEST(loot_attribute_config_test, default_config_produces_empty) {
    loot_attribute_config cfg;
    // All zeros — should produce an empty attribute
    auto attr = detail::generate_attribute(cfg);
    EXPECT_TRUE(attr.is_empty());
}

TEST(loot_attribute_config_test, upgrade_only_config) {
    loot_attribute_config cfg;
    cfg.max_upgrade_level = 5;

    // Generate many attributes — all should have upgrade 0-5, no enchantments
    for (int i = 0; i < 50; ++i) {
        auto attr = detail::generate_attribute(cfg);
        EXPECT_LE(attr.upgrade_level, 5);
        EXPECT_EQ(attr.main_type, enchantment_type::none);
        EXPECT_EQ(attr.sub_type, sub_enchantment_type::none);
    }
}

TEST(loot_attribute_config_test, enchantment_100_percent) {
    loot_attribute_config cfg;
    cfg.enchantment_chance = 100;
    cfg.max_enchantment_value = 10;

    // 100% chance should always produce an enchantment
    for (int i = 0; i < 20; ++i) {
        auto attr = detail::generate_attribute(cfg);
        EXPECT_NE(attr.main_type, enchantment_type::none);
        EXPECT_GE(attr.main_value, 1);
        EXPECT_LE(attr.main_value, 10);
    }
}

TEST(loot_attribute_config_test, sub_enchantment_100_percent) {
    loot_attribute_config cfg;
    cfg.sub_enchantment_chance = 100;
    cfg.max_enchantment_value = 8;

    for (int i = 0; i < 20; ++i) {
        auto attr = detail::generate_attribute(cfg);
        EXPECT_NE(attr.sub_type, sub_enchantment_type::none);
        EXPECT_GE(attr.sub_value, 1);
        EXPECT_LE(attr.sub_value, 8);
    }
}

TEST(loot_attribute_config_test, full_config_generates_all_fields) {
    loot_attribute_config cfg;
    cfg.max_upgrade_level = 15;
    cfg.enchantment_chance = 100;
    cfg.sub_enchantment_chance = 100;
    cfg.max_enchantment_value = 15;

    auto attr = detail::generate_attribute(cfg);
    EXPECT_LE(attr.upgrade_level, 15);
    EXPECT_NE(attr.main_type, enchantment_type::none);
    EXPECT_NE(attr.sub_type, sub_enchantment_type::none);
}

// ============================================================================
// Loot drop entry with attribute
// ============================================================================

TEST(loot_drop_entry_test, default_has_no_attribute) {
    loot_drop_entry entry;
    entry.pool_name = "test";
    entry.chance = 5000;
    EXPECT_FALSE(entry.attribute.has_value());
}

TEST(loot_drop_entry_test, with_attribute_config) {
    loot_drop_entry entry;
    entry.pool_name = "boss_weapons";
    entry.chance = 3000;
    entry.attribute = loot_attribute_config{
        .max_upgrade_level = 3,
        .enchantment_chance = 50,
        .sub_enchantment_chance = 30,
        .max_enchantment_value = 5
    };

    EXPECT_TRUE(entry.attribute.has_value());
    EXPECT_EQ(entry.attribute->max_upgrade_level, 3);
}

// ============================================================================
// Loot item result
// ============================================================================

TEST(loot_item_result_test, default_has_empty_attribute) {
    loot_item_result result;
    EXPECT_EQ(result.template_id.value, 0);
    EXPECT_TRUE(result.attribute.is_empty());
}

TEST(loot_item_result_test, with_attribute) {
    loot_item_result result;
    result.template_id = item_id{500};
    result.attribute.upgrade_level = 3;
    result.attribute.main_type = enchantment_type::sharp;
    result.attribute.main_value = 1;

    EXPECT_EQ(result.template_id.value, 500);
    EXPECT_FALSE(result.attribute.is_empty());
    EXPECT_EQ(result.attribute.upgrade_level, 3);
}

// ============================================================================
// Admin give item with attribute
// ============================================================================

TEST(admin_give_item_attribute_test, parse_without_attribute) {
    nlohmann::json j = {
        {"player_name", "TestPlayer"},
        {"item_template_id", 100},
        {"count", 1}
    };

    auto result = hb::network::admin_give_item_request_data::from_json(j);
    ASSERT_TRUE(result.is_ok());
    EXPECT_FALSE(result.value().attribute.has_value());
}

TEST(admin_give_item_attribute_test, parse_with_attribute) {
    nlohmann::json j = {
        {"player_name", "TestPlayer"},
        {"item_template_id", 100},
        {"count", 1},
        {"attribute", {
            {"upgrade", 7},
            {"mt", 7},
            {"mv", 1},
            {"st", 2},
            {"sv", 5},
            {"cm", true}
        }}
    };

    auto result = hb::network::admin_give_item_request_data::from_json(j);
    ASSERT_TRUE(result.is_ok());
    const auto& req = result.value();
    ASSERT_TRUE(req.attribute.has_value());
    EXPECT_EQ(req.attribute->upgrade_level, 7);
    EXPECT_EQ(req.attribute->main_type, enchantment_type::sharp);
    EXPECT_EQ(req.attribute->main_value, 1);
    EXPECT_EQ(req.attribute->sub_type, sub_enchantment_type::attack_rating);
    EXPECT_EQ(req.attribute->sub_value, 5);
    EXPECT_TRUE(req.attribute->custom_made);
}

TEST(admin_give_item_attribute_test, parse_with_empty_attribute_object) {
    nlohmann::json j = {
        {"player_name", "TestPlayer"},
        {"item_template_id", 100},
        {"count", 1},
        {"attribute", nlohmann::json::object()}
    };

    auto result = hb::network::admin_give_item_request_data::from_json(j);
    ASSERT_TRUE(result.is_ok());
    ASSERT_TRUE(result.value().attribute.has_value());
    // Empty JSON object → empty attribute (all defaults)
    EXPECT_TRUE(result.value().attribute->is_empty());
}
