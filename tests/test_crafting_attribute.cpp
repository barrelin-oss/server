#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

#include "item/item.h"
#include "item/item_attribute.h"
#include "crafting/recipe_config.h"

using namespace hb::item;
using namespace hb::crafting;

// --- Recipe attribute field ---

TEST(crafting_attribute_test, build_recipe_default_attribute_is_zero)
{
    build_recipe recipe;
    EXPECT_EQ(recipe.result_attribute, 0);
}

TEST(crafting_attribute_test, recipe_attribute_decodes_main_enchantment)
{
    // Recipe result_attribute is a packed byte: upper nibble = main enchant type, lower = value
    // sharp = 7, value = 1 → 0x71
    uint8_t attr = 0x71;

    auto main_type = static_cast<enchantment_type>((attr >> 4) & 0xF);
    auto main_value = static_cast<uint8_t>(attr & 0xF);

    EXPECT_EQ(main_type, enchantment_type::sharp);
    EXPECT_EQ(main_value, 1);
}

TEST(crafting_attribute_test, recipe_attribute_decodes_fire_enchantment)
{
    // fire = 8, value = 3 → 0x83
    uint8_t attr = 0x83;

    auto main_type = static_cast<enchantment_type>((attr >> 4) & 0xF);
    auto main_value = static_cast<uint8_t>(attr & 0xF);

    EXPECT_EQ(main_type, enchantment_type::fire);
    EXPECT_EQ(main_value, 3);
}

// --- Custom-made item properties ---

TEST(crafting_attribute_test, custom_made_flag)
{
    item_attribute attr;
    attr.custom_made = true;
    attr.custom_quality = 50;

    EXPECT_TRUE(attr.custom_made);
    EXPECT_EQ(attr.custom_quality, 50);
    EXPECT_FALSE(attr.is_empty());
}

TEST(crafting_attribute_test, custom_quality_range)
{
    item_attribute attr;

    // Positive quality (above average)
    attr.custom_quality = 100;
    EXPECT_EQ(attr.custom_quality, 100);

    // Negative quality (below average)
    attr.custom_quality = -100;
    EXPECT_EQ(attr.custom_quality, -100);

    // Neutral
    attr.custom_quality = 0;
    EXPECT_EQ(attr.custom_quality, 0);
}

// --- craft_result has created_item ---

TEST(crafting_attribute_test, craft_result_default)
{
    craft_result result;
    EXPECT_FALSE(result.success);
    EXPECT_FALSE(result.created_item.is_valid());
}

// --- JSON persistence ---

TEST(crafting_attribute_test, custom_made_survives_json_roundtrip)
{
    item_attribute attr;
    attr.custom_made = true;
    attr.custom_quality = -25;
    attr.main_type = enchantment_type::sharp;
    attr.main_value = 1;
    attr.sub_type = sub_enchantment_type::attack_rating;
    attr.sub_value = 3;

    auto j = attr.to_json();
    auto decoded = item_attribute::from_json(j);

    EXPECT_TRUE(decoded.custom_made);
    EXPECT_EQ(decoded.custom_quality, -25);
    EXPECT_EQ(decoded.main_type, enchantment_type::sharp);
    EXPECT_EQ(decoded.main_value, 1);
    EXPECT_EQ(decoded.sub_type, sub_enchantment_type::attack_rating);
    EXPECT_EQ(decoded.sub_value, 3);
}
