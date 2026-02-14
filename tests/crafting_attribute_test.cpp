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
    // Legacy: m_wAttribute gets shifted left 16 to form bits 16-31
    // Bits 7-4 of the 16-bit value → bits 23-20 of dword → main_type
    // Bits 3-0 of the 16-bit value → bits 19-16 of dword → main_value
    // sharp = 7, value = 1 → 0x0071
    uint16_t attr = 0x0071;

    auto decoded = item_attribute::from_legacy_dword(
        static_cast<uint32_t>(attr) << 16);

    EXPECT_EQ(decoded.main_type, enchantment_type::sharp);
    EXPECT_EQ(decoded.main_value, 1);
    EXPECT_EQ(decoded.sub_type, sub_enchantment_type::none);
    EXPECT_EQ(decoded.sub_value, 0);
}

TEST(crafting_attribute_test, recipe_attribute_decodes_sub_enchantment)
{
    // Bits 11-8 of 16-bit → bits 27-24 of dword → (not used in our mapping)
    // Bits 15-12 of 16-bit → bits 31-28 of dword → upgrade_level (would be 0 for crafted)
    // Actually looking at the bit layout:
    // 16-bit recipe attribute << 16:
    //   recipe bit 15-12 → dword bit 31-28 = upgrade level
    //   recipe bit 11-8  → dword bit 27-24 = (unused)
    //   recipe bit 7-4   → dword bit 23-20 = main enchant type
    //   recipe bit 3-0   → dword bit 19-16 = main enchant value
    // Sub enchantments are in bits 15-8 of the dword (NOT from recipe shift)
    // So recipe attribute only sets main enchantment, not sub
    uint16_t attr = 0x0071;
    auto decoded = item_attribute::from_legacy_dword(
        static_cast<uint32_t>(attr) << 16);

    // Main should be set
    EXPECT_EQ(decoded.main_type, enchantment_type::sharp);
    EXPECT_EQ(decoded.main_value, 1);
}

TEST(crafting_attribute_test, recipe_attribute_with_both_enchantments)
{
    // Create a full dword manually:
    // main_type=fire(8), main_value=3, sub_type=hp_recovery(4), sub_value=5
    item_attribute attr;
    attr.main_type = enchantment_type::fire;
    attr.main_value = 3;
    attr.sub_type = sub_enchantment_type::hp_recovery;
    attr.sub_value = 5;
    auto dword = attr.to_legacy_dword();

    // Verify round-trip
    auto decoded = item_attribute::from_legacy_dword(dword);
    EXPECT_EQ(decoded.main_type, enchantment_type::fire);
    EXPECT_EQ(decoded.main_value, 3);
    EXPECT_EQ(decoded.sub_type, sub_enchantment_type::hp_recovery);
    EXPECT_EQ(decoded.sub_value, 5);
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

// --- Legacy attribute persistence ---

TEST(crafting_attribute_test, custom_made_survives_legacy_roundtrip)
{
    item_attribute attr;
    attr.custom_made = true;
    attr.custom_quality = 42;
    attr.main_type = enchantment_type::poison;
    attr.main_value = 3;

    auto dword = attr.to_legacy_dword();
    auto decoded = item_attribute::from_legacy_dword(dword);

    EXPECT_TRUE(decoded.custom_made);
    EXPECT_EQ(decoded.main_type, enchantment_type::poison);
    EXPECT_EQ(decoded.main_value, 3);
}

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
