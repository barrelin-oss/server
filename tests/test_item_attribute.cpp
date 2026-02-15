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

// --- Legacy DWORD round-trip ---

TEST(item_attribute_test, legacy_dword_empty_roundtrip)
{
    item_attribute attr;
    uint32_t dw = attr.to_legacy_dword();
    EXPECT_EQ(dw, 0u);
    auto parsed = item_attribute::from_legacy_dword(dw);
    EXPECT_TRUE(parsed.is_empty());
}

TEST(item_attribute_test, legacy_dword_upgrade_only)
{
    item_attribute attr;
    attr.upgrade_level = 7;
    uint32_t dw = attr.to_legacy_dword();
    auto parsed = item_attribute::from_legacy_dword(dw);
    EXPECT_EQ(parsed.upgrade_level, 7);
    EXPECT_EQ(parsed.main_type, enchantment_type::none);
    EXPECT_EQ(parsed.sub_type, sub_enchantment_type::none);
}

TEST(item_attribute_test, legacy_dword_full_roundtrip)
{
    item_attribute attr;
    attr.upgrade_level = 10;
    attr.main_type = enchantment_type::sharp;
    attr.main_value = 5;
    attr.sub_type = sub_enchantment_type::physical_resist;
    attr.sub_value = 3;
    attr.custom_made = true;

    uint32_t dw = attr.to_legacy_dword();
    auto parsed = item_attribute::from_legacy_dword(dw);

    EXPECT_EQ(parsed.upgrade_level, 10);
    EXPECT_EQ(parsed.main_type, enchantment_type::sharp);
    EXPECT_EQ(parsed.main_value, 5);
    EXPECT_EQ(parsed.sub_type, sub_enchantment_type::physical_resist);
    EXPECT_EQ(parsed.sub_value, 3);
    EXPECT_TRUE(parsed.custom_made);
}

TEST(item_attribute_test, legacy_dword_max_values)
{
    item_attribute attr;
    attr.upgrade_level = 15;
    attr.main_type = enchantment_type::charge_critical; // 12
    attr.main_value = 15;
    attr.sub_type = sub_enchantment_type::gold_bonus; // 12
    attr.sub_value = 15;
    attr.custom_made = true;

    uint32_t dw = attr.to_legacy_dword();
    auto parsed = item_attribute::from_legacy_dword(dw);

    EXPECT_EQ(parsed.upgrade_level, 15);
    EXPECT_EQ(parsed.main_type, enchantment_type::charge_critical);
    EXPECT_EQ(parsed.main_value, 15);
    EXPECT_EQ(parsed.sub_type, sub_enchantment_type::gold_bonus);
    EXPECT_EQ(parsed.sub_value, 15);
    EXPECT_TRUE(parsed.custom_made);
}

TEST(item_attribute_test, legacy_dword_all_enchantment_types)
{
    for (uint8_t i = 0; i <= 12; ++i)
    {
        item_attribute attr;
        attr.main_type = static_cast<enchantment_type>(i);
        attr.main_value = i;
        uint32_t dw = attr.to_legacy_dword();
        auto parsed = item_attribute::from_legacy_dword(dw);
        EXPECT_EQ(parsed.main_type, static_cast<enchantment_type>(i));
        EXPECT_EQ(parsed.main_value, i);
    }
}

TEST(item_attribute_test, legacy_dword_all_sub_enchantment_types)
{
    for (uint8_t i = 0; i <= 12; ++i)
    {
        item_attribute attr;
        attr.sub_type = static_cast<sub_enchantment_type>(i);
        attr.sub_value = i;
        uint32_t dw = attr.to_legacy_dword();
        auto parsed = item_attribute::from_legacy_dword(dw);
        EXPECT_EQ(parsed.sub_type, static_cast<sub_enchantment_type>(i));
        EXPECT_EQ(parsed.sub_value, i);
    }
}

// --- Clamping ---

TEST(item_attribute_test, legacy_dword_clamps_upgrade_level)
{
    item_attribute attr;
    attr.upgrade_level = 20; // > 15
    uint32_t dw = attr.to_legacy_dword();
    auto parsed = item_attribute::from_legacy_dword(dw);
    EXPECT_EQ(parsed.upgrade_level, 15);
}

TEST(item_attribute_test, legacy_dword_clamps_values)
{
    item_attribute attr;
    attr.main_value = 20; // > 15
    attr.sub_value = 20;  // > 15
    uint32_t dw = attr.to_legacy_dword();
    auto parsed = item_attribute::from_legacy_dword(dw);
    EXPECT_EQ(parsed.main_value, 15);
    EXPECT_EQ(parsed.sub_value, 15);
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
    EXPECT_FALSE(j.contains("mt"));
    EXPECT_FALSE(j.contains("st"));
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
    EXPECT_TRUE(j["cm"].get<bool>());
    EXPECT_FALSE(j.contains("cq")); // quality 0 not serialized
}

TEST(item_attribute_test, json_clamps_upgrade_level)
{
    nlohmann::json j = {{"upgrade", 20}};
    auto parsed = item_attribute::from_json(j);
    EXPECT_EQ(parsed.upgrade_level, 15);
}

TEST(item_attribute_test, json_clamps_values)
{
    nlohmann::json j = {{"mt", 7}, {"mv", 20}, {"st", 1}, {"sv", 20}};
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

// --- Custom quality not stored in legacy dword ---

TEST(item_attribute_test, custom_quality_not_in_legacy_dword)
{
    item_attribute attr;
    attr.custom_made = true;
    attr.custom_quality = 75;
    uint32_t dw = attr.to_legacy_dword();
    auto parsed = item_attribute::from_legacy_dword(dw);
    // custom_quality is NOT part of the legacy dword
    EXPECT_TRUE(parsed.custom_made);
    EXPECT_EQ(parsed.custom_quality, 0);
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
