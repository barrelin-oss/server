#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

#include "item/item_attribute.h"
#include "item/item.h"

using namespace hb::item;

// --- Default construction ---

TEST(item_attribute_test, default_is_empty)
{
    item_attribute attr;
    EXPECT_TRUE(attr.is_empty());
    EXPECT_EQ(attr.upgrade_level, 0);
    EXPECT_EQ(attr.main_type, enchantment_type::none);
    EXPECT_EQ(attr.main_value, 0);
    EXPECT_EQ(attr.sub_type, sub_enchantment_type::none);
    EXPECT_EQ(attr.sub_value, 0);
    EXPECT_FALSE(attr.custom_made);
    EXPECT_EQ(attr.custom_quality, 0);
}

TEST(item_attribute_test, not_empty_with_upgrade)
{
    item_attribute attr;
    attr.upgrade_level = 3;
    EXPECT_FALSE(attr.is_empty());
}

TEST(item_attribute_test, not_empty_with_main_enchantment)
{
    item_attribute attr;
    attr.main_type = enchantment_type::poison;
    EXPECT_FALSE(attr.is_empty());
}

TEST(item_attribute_test, not_empty_with_sub_enchantment)
{
    item_attribute attr;
    attr.sub_type = sub_enchantment_type::hp_recovery;
    EXPECT_FALSE(attr.is_empty());
}

TEST(item_attribute_test, not_empty_with_custom_made)
{
    item_attribute attr;
    attr.custom_made = true;
    EXPECT_FALSE(attr.is_empty());
}

// --- JSON round-trip ---

TEST(item_attribute_test, json_empty_roundtrip)
{
    item_attribute attr;
    auto j = attr.to_json();
    EXPECT_TRUE(j.empty()); // Empty attributes produce empty object
    auto parsed = item_attribute::from_json(j);
    EXPECT_TRUE(parsed.is_empty());
}

TEST(item_attribute_test, json_null_produces_empty)
{
    auto parsed = item_attribute::from_json(nlohmann::json{});
    EXPECT_TRUE(parsed.is_empty());
}

TEST(item_attribute_test, json_upgrade_only)
{
    item_attribute attr;
    attr.upgrade_level = 5;
    auto j = attr.to_json();
    EXPECT_EQ(j["upgrade"], 5);
    EXPECT_FALSE(j.contains("main_type"));
    EXPECT_FALSE(j.contains("sub_type"));
    auto parsed = item_attribute::from_json(j);
    EXPECT_EQ(parsed.upgrade_level, 5);
}

TEST(item_attribute_test, json_full_roundtrip)
{
    item_attribute attr;
    attr.upgrade_level = 7;
    attr.main_type = enchantment_type::poison;
    attr.main_value = 3;
    attr.sub_type = sub_enchantment_type::exp_bonus;
    attr.sub_value = 2;
    attr.custom_made = true;
    attr.custom_quality = 42;

    auto j = attr.to_json();
    auto parsed = item_attribute::from_json(j);

    EXPECT_EQ(parsed.upgrade_level, 7);
    EXPECT_EQ(parsed.main_type, enchantment_type::poison);
    EXPECT_EQ(parsed.main_value, 3);
    EXPECT_EQ(parsed.sub_type, sub_enchantment_type::exp_bonus);
    EXPECT_EQ(parsed.sub_value, 2);
    EXPECT_TRUE(parsed.custom_made);
    EXPECT_EQ(parsed.custom_quality, 42);
}

TEST(item_attribute_test, json_custom_made_without_quality)
{
    item_attribute attr;
    attr.custom_made = true;
    auto j = attr.to_json();
    EXPECT_TRUE(j["custom_made"].get<bool>());
    EXPECT_FALSE(j.contains("custom_quality")); // quality 0 not serialized
}

TEST(item_attribute_test, json_clamps_upgrade_level)
{
    nlohmann::json j = {{"upgrade", 20}};
    auto parsed = item_attribute::from_json(j);
    EXPECT_EQ(parsed.upgrade_level, 15);
}

TEST(item_attribute_test, json_clamps_values)
{
    nlohmann::json j = {{"main_type", 7}, {"main_value", 20}, {"sub_type", 1}, {"sub_value", 20}};
    auto parsed = item_attribute::from_json(j);
    EXPECT_EQ(parsed.main_value, 15);
    EXPECT_EQ(parsed.sub_value, 15);
}

// --- to_string ---

TEST(item_attribute_test, enchantment_type_to_string)
{
    EXPECT_STREQ(to_string(enchantment_type::none), "none");
    EXPECT_STREQ(to_string(enchantment_type::poison), "poison");
    EXPECT_STREQ(to_string(enchantment_type::sharp), "sharp");
    EXPECT_STREQ(to_string(enchantment_type::ancient), "ancient");
    EXPECT_STREQ(to_string(enchantment_type::charge_critical), "charge_critical");
}

TEST(item_attribute_test, sub_enchantment_type_to_string)
{
    EXPECT_STREQ(to_string(sub_enchantment_type::none), "none");
    EXPECT_STREQ(to_string(sub_enchantment_type::physical_resist), "physical_resist");
    EXPECT_STREQ(to_string(sub_enchantment_type::exp_bonus), "exp_bonus");
    EXPECT_STREQ(to_string(sub_enchantment_type::gold_bonus), "gold_bonus");
}

// --- Item struct integration ---

TEST(item_attribute_test, item_has_default_empty_attribute)
{
    item itm;
    EXPECT_TRUE(itm.attribute.is_empty());
}

TEST(item_attribute_test, item_preserves_attribute)
{
    item itm;
    itm.attribute.upgrade_level = 5;
    itm.attribute.main_type = enchantment_type::fire;
    EXPECT_EQ(itm.attribute.upgrade_level, 5);
    EXPECT_EQ(itm.attribute.main_type, enchantment_type::fire);
}

TEST(item_attribute_test, custom_quality_preserved_in_json)
{
    item_attribute attr;
    attr.custom_made = true;
    attr.custom_quality = -50;
    auto j = attr.to_json();
    auto parsed = item_attribute::from_json(j);
    EXPECT_EQ(parsed.custom_quality, -50);
}
