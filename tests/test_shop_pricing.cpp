// test_shop_pricing.cpp
// Unit tests for shop pricing functions

#include "npc/shop_pricing.h"

#include <gtest/gtest.h>

namespace hb::npc {

// === Buy price tests ===

TEST(shop_pricing_test, buy_price_no_charisma_bonus) {
    // Charisma 10 -> (10-10)/4 = 0% discount
    EXPECT_EQ(calculate_buy_price(100, 1, 10), 100);
}

TEST(shop_pricing_test, buy_price_with_charisma_discount) {
    // Charisma 14 -> (14-10)/4 = 1% discount -> 99
    EXPECT_EQ(calculate_buy_price(100, 1, 14), 99);
}

TEST(shop_pricing_test, buy_price_high_charisma) {
    // Charisma 30 -> (30-10)/4 = 5% discount -> 95
    EXPECT_EQ(calculate_buy_price(100, 1, 30), 95);
}

TEST(shop_pricing_test, buy_price_max_discount_cap) {
    // Charisma 250 -> (250-10)/4 = 60, capped at 50% -> 50
    EXPECT_EQ(calculate_buy_price(100, 1, 250), 50);
}

TEST(shop_pricing_test, buy_price_low_charisma) {
    // Charisma 5 -> (5-10)/4 = -1, clamped to 0% -> 100
    EXPECT_EQ(calculate_buy_price(100, 1, 5), 100);
}

TEST(shop_pricing_test, buy_price_multiple_count) {
    EXPECT_EQ(calculate_buy_price(100, 3, 10), 300);
}

TEST(shop_pricing_test, buy_price_zero_base) {
    EXPECT_EQ(calculate_buy_price(0, 1, 10), 0);
}

TEST(shop_pricing_test, buy_price_zero_count) {
    EXPECT_EQ(calculate_buy_price(100, 0, 10), 0);
}

// === Sell price equipment tests ===

TEST(shop_pricing_test, sell_price_equipment_full_durability) {
    // Full durability: (100/100) * 0.5 * 200 = 100
    EXPECT_EQ(calculate_sell_price_equipment(200, 100, 100, false), 100);
}

TEST(shop_pricing_test, sell_price_equipment_half_durability) {
    // Half durability: (50/100) * 0.5 * 200 = 50
    EXPECT_EQ(calculate_sell_price_equipment(200, 50, 100, false), 50);
}

TEST(shop_pricing_test, sell_price_equipment_zero_durability) {
    // Zero durability = unsellable
    EXPECT_EQ(calculate_sell_price_equipment(200, 0, 100, false), 0);
}

TEST(shop_pricing_test, sell_price_equipment_neutral_penalty) {
    // Full durability, neutral: (100/100) * 0.5 * 200 / 2 = 50
    EXPECT_EQ(calculate_sell_price_equipment(200, 100, 100, true), 50);
}

TEST(shop_pricing_test, sell_price_equipment_min_one) {
    // Very low durability but not zero -> minimum 1
    EXPECT_GE(calculate_sell_price_equipment(10, 1, 100, false), 1);
}

// === Sell price consumable tests ===

TEST(shop_pricing_test, sell_price_consumable_single) {
    // base/2 * count = 100/2 * 1 = 50
    EXPECT_EQ(calculate_sell_price_consumable(100, 1, false), 50);
}

TEST(shop_pricing_test, sell_price_consumable_multiple) {
    // base/2 * count = 100/2 * 5 = 250
    EXPECT_EQ(calculate_sell_price_consumable(100, 5, false), 250);
}

TEST(shop_pricing_test, sell_price_consumable_neutral) {
    // base/2 * count / 2 = 100/2 * 1 / 2 = 25
    EXPECT_EQ(calculate_sell_price_consumable(100, 1, true), 25);
}

TEST(shop_pricing_test, sell_price_consumable_zero_count) {
    EXPECT_EQ(calculate_sell_price_consumable(100, 0, false), 0);
}

// === Repair cost tests ===

TEST(shop_pricing_test, repair_cost_fully_damaged) {
    // Full repair needed: base/2 - (0/100 * 0.5 * base) = 50 - 0 = 50
    EXPECT_EQ(calculate_repair_cost(100, 0, 100), 50);
}

TEST(shop_pricing_test, repair_cost_half_damaged) {
    // Half repair: base/2 - (50/100 * 0.5 * 100) = 50 - 25 = 25
    EXPECT_EQ(calculate_repair_cost(100, 50, 100), 25);
}

TEST(shop_pricing_test, repair_cost_no_damage) {
    // Already repaired
    EXPECT_EQ(calculate_repair_cost(100, 100, 100), 0);
}

TEST(shop_pricing_test, repair_cost_slight_damage) {
    // Slight damage: base/2 - (90/100 * 0.5 * 100) = 50 - 45 = 5
    EXPECT_EQ(calculate_repair_cost(100, 90, 100), 5);
}

TEST(shop_pricing_test, repair_cost_minimum_one) {
    // Very slight damage should still cost at least 1
    auto cost = calculate_repair_cost(100, 99, 100);
    EXPECT_GE(cost, 1);
}

// === Category checks ===

TEST(shop_pricing_test, category_accepted) {
    shop_config shop;
    shop.buy_categories = {1, 2, 3, 10, 11};
    EXPECT_TRUE(is_category_accepted(shop, 1));
    EXPECT_TRUE(is_category_accepted(shop, 10));
    EXPECT_FALSE(is_category_accepted(shop, 99));
    EXPECT_FALSE(is_category_accepted(shop, 0));
}

TEST(shop_pricing_test, category_repairable) {
    shop_config shop;
    shop.repair_categories = {1, 2, 3};
    EXPECT_TRUE(is_category_repairable(shop, 1));
    EXPECT_FALSE(is_category_repairable(shop, 10));
}

// === Territory checks ===

TEST(shop_pricing_test, territory_neutral_player_can_buy_everywhere) {
    EXPECT_TRUE(can_buy_in_territory(faction::neutral, "Aresden"));
    EXPECT_TRUE(can_buy_in_territory(faction::neutral, "Elvine"));
    EXPECT_TRUE(can_buy_in_territory(faction::neutral, "SomeField"));
}

TEST(shop_pricing_test, territory_aresden_player_blocked_in_elvine) {
    EXPECT_FALSE(can_buy_in_territory(faction::aresden, "Elvine"));
    EXPECT_FALSE(can_buy_in_territory(faction::aresden, "elvine2"));
}

TEST(shop_pricing_test, territory_aresden_player_allowed_in_aresden) {
    EXPECT_TRUE(can_buy_in_territory(faction::aresden, "Aresden"));
    EXPECT_TRUE(can_buy_in_territory(faction::aresden, "aresden2"));
}

TEST(shop_pricing_test, territory_aresden_player_allowed_in_neutral) {
    EXPECT_TRUE(can_buy_in_territory(faction::aresden, "MiddleLand"));
    EXPECT_TRUE(can_buy_in_territory(faction::aresden, ""));
}

TEST(shop_pricing_test, territory_elvine_player_blocked_in_aresden) {
    EXPECT_FALSE(can_buy_in_territory(faction::elvine, "Aresden"));
    EXPECT_FALSE(can_buy_in_territory(faction::elvine, "aresden2"));
}

TEST(shop_pricing_test, territory_elvine_player_allowed_in_elvine) {
    EXPECT_TRUE(can_buy_in_territory(faction::elvine, "Elvine"));
}

TEST(shop_pricing_test, neutral_territory_check) {
    EXPECT_TRUE(is_neutral_territory("MiddleLand"));
    EXPECT_TRUE(is_neutral_territory(""));
    EXPECT_FALSE(is_neutral_territory("Aresden"));
    EXPECT_FALSE(is_neutral_territory("elvine2"));
}

}  // namespace hb::npc
