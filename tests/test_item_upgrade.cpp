#include <gtest/gtest.h>

#include "item/item.h"
#include "item/item_upgrade.h"
#include "network/json_protocol.h"

using namespace hb::item;
using namespace hb::network;
using hb::entity_id;
using hb::item_id;

// Helper to create a test weapon
static auto make_weapon(uint8_t upgrade_level = 0) -> item
{
    item itm;
    itm.id = item_id{100};
    itm.template_id = item_id{1};
    itm.name = "Test Sword";
    itm.type = item_type::weapon;
    itm.equip_position = equip_pos::weapon;
    itm.attack_power = 20;
    itm.attribute.upgrade_level = upgrade_level;
    return itm;
}

// Helper to create a test armor
static auto make_armor(uint8_t upgrade_level = 0) -> item
{
    item itm;
    itm.id = item_id{200};
    itm.template_id = item_id{50};
    itm.name = "Test Armor";
    itm.type = item_type::armor;
    itm.equip_position = equip_pos::body;
    itm.defense = 30;
    itm.attribute.upgrade_level = upgrade_level;
    return itm;
}

// Helper to create a test accessory
static auto make_accessory(uint8_t upgrade_level = 0) -> item
{
    item itm;
    itm.id = item_id{300};
    itm.template_id = item_id{80};
    itm.name = "Test Ring";
    itm.type = item_type::accessory;
    itm.equip_position = equip_pos::ring_left;
    itm.attribute.upgrade_level = upgrade_level;
    return itm;
}

// Helper to create a consumable (non-equipment)
static auto make_consumable() -> item
{
    item itm;
    itm.id = item_id{400};
    itm.template_id = item_id{91};
    itm.name = "Red Potion";
    itm.type = item_type::consumable;
    return itm;
}

// --- Stone validation tests ---

TEST(item_upgrade_test, xelima_valid_for_weapon)
{
    auto weapon = make_weapon();
    EXPECT_TRUE(is_valid_upgrade_stone(weapon, xelima_stone_id));
}

TEST(item_upgrade_test, xelima_invalid_for_armor)
{
    auto armor = make_armor();
    EXPECT_FALSE(is_valid_upgrade_stone(armor, xelima_stone_id));
}

TEST(item_upgrade_test, xelima_invalid_for_accessory)
{
    auto acc = make_accessory();
    EXPECT_FALSE(is_valid_upgrade_stone(acc, xelima_stone_id));
}

TEST(item_upgrade_test, merien_valid_for_armor)
{
    auto armor = make_armor();
    EXPECT_TRUE(is_valid_upgrade_stone(armor, merien_stone_id));
}

TEST(item_upgrade_test, merien_valid_for_accessory)
{
    auto acc = make_accessory();
    EXPECT_TRUE(is_valid_upgrade_stone(acc, merien_stone_id));
}

TEST(item_upgrade_test, merien_invalid_for_weapon)
{
    auto weapon = make_weapon();
    EXPECT_FALSE(is_valid_upgrade_stone(weapon, merien_stone_id));
}

TEST(item_upgrade_test, invalid_stone_id_rejected)
{
    auto weapon = make_weapon();
    EXPECT_FALSE(is_valid_upgrade_stone(weapon, 999));
}

TEST(item_upgrade_test, non_equipment_rejected_by_both_stones)
{
    auto potion = make_consumable();
    EXPECT_FALSE(is_valid_upgrade_stone(potion, xelima_stone_id));
    EXPECT_FALSE(is_valid_upgrade_stone(potion, merien_stone_id));
}

// --- Upgrade result tests ---

TEST(item_upgrade_test, max_level_item_cannot_upgrade)
{
    auto weapon = make_weapon(max_upgrade_level);
    auto result = attempt_upgrade(weapon);

    EXPECT_FALSE(result.success);
    EXPECT_EQ(result.new_level, max_upgrade_level);
    EXPECT_TRUE(result.stone_consumed);
}

TEST(item_upgrade_test, stone_always_consumed)
{
    // Run several trials - stone_consumed should always be true
    for (int i = 0; i < 20; ++i)
    {
        auto weapon = make_weapon(0);
        auto result = attempt_upgrade(weapon);
        EXPECT_TRUE(result.stone_consumed);
    }
}

TEST(item_upgrade_test, successful_upgrade_increments_level)
{
    // +0 has 30% chance, run enough trials to get at least one success
    bool got_success = false;
    for (int i = 0; i < 100; ++i)
    {
        auto weapon = make_weapon(0);
        auto result = attempt_upgrade(weapon);
        if (result.success)
        {
            EXPECT_EQ(result.new_level, 1);
            EXPECT_EQ(weapon.attribute.upgrade_level, 1);
            got_success = true;
            break;
        }
    }
    EXPECT_TRUE(got_success) << "Expected at least one success in 100 trials at 30% probability";
}

TEST(item_upgrade_test, failed_upgrade_keeps_level)
{
    // +9 has 3% chance, run a few trials to get a failure
    bool got_failure = false;
    for (int i = 0; i < 100; ++i)
    {
        auto weapon = make_weapon(9);
        uint8_t before = weapon.attribute.upgrade_level;
        auto result = attempt_upgrade(weapon);
        if (!result.success)
        {
            EXPECT_EQ(result.new_level, before);
            EXPECT_EQ(weapon.attribute.upgrade_level, before);
            got_failure = true;
            break;
        }
    }
    EXPECT_TRUE(got_failure) << "Expected at least one failure in 100 trials at 3% probability";
}

TEST(item_upgrade_test, upgrade_modifies_item_attribute_directly)
{
    // Ensure the upgrade modifies the item's attribute in-place
    auto weapon = make_weapon(0);
    weapon.attribute.main_type = enchantment_type::sharp;
    weapon.attribute.main_value = 1;

    // Run until we get a success
    for (int i = 0; i < 200; ++i)
    {
        weapon.attribute.upgrade_level = 0;
        auto result = attempt_upgrade(weapon);
        if (result.success)
        {
            // Verify only upgrade_level changed, other attributes preserved
            EXPECT_EQ(weapon.attribute.upgrade_level, 1);
            EXPECT_EQ(weapon.attribute.main_type, enchantment_type::sharp);
            EXPECT_EQ(weapon.attribute.main_value, 1);
            return;
        }
    }
    FAIL() << "No success in 200 trials";
}

TEST(item_upgrade_test, level_never_exceeds_max)
{
    auto weapon = make_weapon(14); // +14, one away from max
    // Run until success
    for (int i = 0; i < 2000; ++i)
    {
        weapon.attribute.upgrade_level = 14;
        auto result = attempt_upgrade(weapon);
        if (result.success)
        {
            EXPECT_EQ(result.new_level, 15);
            EXPECT_LE(weapon.attribute.upgrade_level, max_upgrade_level);
            return;
        }
    }
    // +14 has 1% chance, so 2000 trials should almost certainly get one
    FAIL() << "No success in 2000 trials at 1% probability";
}

// --- Probability distribution test ---

TEST(item_upgrade_test, upgrade_probability_roughly_matches_table)
{
    // Test +0 upgrade (30% expected)
    int successes = 0;
    constexpr int trials = 10000;

    for (int i = 0; i < trials; ++i)
    {
        auto weapon = make_weapon(0);
        auto result = attempt_upgrade(weapon);
        if (result.success)
            ++successes;
    }

    // 30% = 3000 successes expected, allow ±5% margin
    double rate = static_cast<double>(successes) / trials;
    EXPECT_GT(rate, 0.20) << "Success rate " << rate << " too low for 30% expected";
    EXPECT_LT(rate, 0.40) << "Success rate " << rate << " too high for 30% expected";
}

// --- Custom-made bonus test ---

TEST(item_upgrade_test, custom_made_high_quality_increases_success_rate)
{
    // Compare success rates: normal vs custom-made with quality > 100
    constexpr int trials = 10000;
    int normal_successes = 0;
    int custom_successes = 0;

    for (int i = 0; i < trials; ++i)
    {
        auto weapon = make_weapon(4); // +4 = 10% base
        auto result = attempt_upgrade(weapon);
        if (result.success)
            ++normal_successes;
    }

    for (int i = 0; i < trials; ++i)
    {
        auto weapon = make_weapon(4); // +4 = 10% base
        weapon.attribute.custom_made = true;
        weapon.attribute.custom_quality = 120;
        auto result = attempt_upgrade(weapon);
        if (result.success)
            ++custom_successes;
    }

    // Custom-made should have higher success rate
    EXPECT_GT(custom_successes, normal_successes)
        << "Custom-made (" << custom_successes << ") should beat normal (" << normal_successes << ")";
}

TEST(item_upgrade_test, custom_made_low_quality_no_bonus)
{
    // custom_made but quality <= 0 should NOT get bonus
    constexpr int trials = 10000;
    int normal_successes = 0;
    int custom_successes = 0;

    for (int i = 0; i < trials; ++i)
    {
        auto weapon = make_weapon(0);
        auto result = attempt_upgrade(weapon);
        if (result.success)
            ++normal_successes;
    }

    for (int i = 0; i < trials; ++i)
    {
        auto weapon = make_weapon(0);
        weapon.attribute.custom_made = true;
        weapon.attribute.custom_quality = 0; // zero quality — no bonus
        auto result = attempt_upgrade(weapon);
        if (result.success)
            ++custom_successes;
    }

    // Should be roughly the same rate (within ±5%)
    double normal_rate = static_cast<double>(normal_successes) / trials;
    double custom_rate = static_cast<double>(custom_successes) / trials;
    EXPECT_NEAR(normal_rate, custom_rate, 0.05);
}

// --- Upgrade constants test ---

TEST(item_upgrade_test, constants_are_correct)
{
    EXPECT_EQ(xelima_stone_id, 656u);
    EXPECT_EQ(merien_stone_id, 657u);
    EXPECT_EQ(max_upgrade_level, 15);
    EXPECT_EQ(upgrade_base_prob.size(), 16u);
    EXPECT_EQ(upgrade_base_prob[0], 3000);
    EXPECT_EQ(upgrade_base_prob[15], 0);
}

TEST(item_upgrade_test, level_15_has_zero_probability)
{
    // Level 15 probability is 0 - should never succeed even if we could try
    // (but attempt_upgrade rejects level >= 15 before rolling)
    EXPECT_EQ(upgrade_base_prob[15], 0);
}

// --- Protocol message tests ---

TEST(item_upgrade_test, request_from_json_valid)
{
    nlohmann::json j = {{"item_id", 5}};
    auto result = item_upgrade_request_data::from_json(j);
    ASSERT_TRUE(result.is_ok());
    EXPECT_EQ(result.value().item_id, 5u);
}

TEST(item_upgrade_test, request_from_json_missing_item_id)
{
    nlohmann::json j = {};
    auto result = item_upgrade_request_data::from_json(j);
    EXPECT_TRUE(result.is_err());
}

TEST(item_upgrade_test, response_success)
{
    auto msg = make_item_upgrade_response(42, true, 5, 3);
    EXPECT_EQ(msg.type, json_message_type::item_upgrade_response);
    EXPECT_EQ(msg.seq, 42u);
    bool success = msg.data["success"];
    EXPECT_TRUE(success);
    uint32_t id = msg.data["item_id"];
    EXPECT_EQ(id, 5u);
    int level = msg.data["new_level"];
    EXPECT_EQ(level, 3);
    EXPECT_FALSE(msg.data.contains("error"));
}

TEST(item_upgrade_test, response_failure_with_error)
{
    auto msg = make_item_upgrade_response(42, false, 5, 0, "no_stone");
    bool success = msg.data["success"];
    EXPECT_FALSE(success);
    std::string err = msg.data["error"];
    EXPECT_EQ(err, "no_stone");
}
