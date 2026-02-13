// test_manufacturing.cpp
// Unit tests for manufacturing system

#include "crafting/manufacturing_system.h"
#include "crafting/recipe_config.h"

#include <gtest/gtest.h>

namespace hb::crafting {

// === Success chance calculation tests ===

class manufacturing_success_test : public ::testing::Test {
protected:
    build_recipe make_recipe(int16_t skill_req, int32_t success_rate, int16_t skill_limit = 0)
    {
        build_recipe r;
        r.skill_req = skill_req;
        r.success_rate = success_rate;
        r.skill_limit = skill_limit;
        return r;
    }
};

TEST_F(manufacturing_success_test, base_rate_no_bonus)
{
    auto recipe = make_recipe(10, 60);
    // skill == skill_req: skill_bonus = 0, dex = 0
    auto chance = manufacturing_system::calculate_success_chance(10, 0, recipe);
    // 60 + 0 + 0 = 60, clamped to [10, 95]
    EXPECT_EQ(chance, 60);
}

TEST_F(manufacturing_success_test, skill_bonus)
{
    auto recipe = make_recipe(10, 50);
    // skill = 20, skill_req = 10: bonus = min(40, (20-10)*2) = 20
    auto chance = manufacturing_system::calculate_success_chance(20, 0, recipe);
    EXPECT_EQ(chance, 70);  // 50 + 20 + 0
}

TEST_F(manufacturing_success_test, skill_bonus_capped_at_40)
{
    auto recipe = make_recipe(10, 50);
    // skill = 50, skill_req = 10: bonus = min(40, (50-10)*2) = min(40, 80) = 40
    auto chance = manufacturing_system::calculate_success_chance(50, 0, recipe);
    EXPECT_EQ(chance, 90);  // 50 + 40 + 0
}

TEST_F(manufacturing_success_test, dex_bonus)
{
    auto recipe = make_recipe(10, 50);
    // dex = 20: bonus = 20/2 = 10
    auto chance = manufacturing_system::calculate_success_chance(10, 20, recipe);
    EXPECT_EQ(chance, 60);  // 50 + 0 + 10
}

TEST_F(manufacturing_success_test, combined_bonuses)
{
    auto recipe = make_recipe(10, 40);
    // skill = 15, dex = 30
    // skill_bonus = min(40, (15-10)*2) = 10
    // dex_bonus = 30/2 = 15
    auto chance = manufacturing_system::calculate_success_chance(15, 30, recipe);
    EXPECT_EQ(chance, 65);  // 40 + 10 + 15
}

TEST_F(manufacturing_success_test, clamp_minimum_10)
{
    auto recipe = make_recipe(10, 5);
    // Very low base rate
    auto chance = manufacturing_system::calculate_success_chance(10, 0, recipe);
    EXPECT_EQ(chance, 10);  // clamped up from 5
}

TEST_F(manufacturing_success_test, clamp_maximum_95)
{
    auto recipe = make_recipe(0, 90);
    // High skill + high dex
    auto chance = manufacturing_system::calculate_success_chance(50, 60, recipe);
    // 90 + min(40, 100) + 30 = 90 + 40 + 30 = 160, clamped to 95
    EXPECT_EQ(chance, 95);
}

TEST_F(manufacturing_success_test, negative_skill_difference)
{
    // skill below req still works for formula (would be blocked at call site)
    auto recipe = make_recipe(20, 50);
    // skill = 15: bonus = min(40, (15-20)*2) = min(40, -10) = -10
    auto chance = manufacturing_system::calculate_success_chance(15, 0, recipe);
    EXPECT_EQ(chance, 40);  // 50 + (-10) + 0 = 40
}

// === Recipe config tests ===

TEST(build_recipe_test, default_values)
{
    build_recipe r;
    EXPECT_EQ(r.id, 0);
    EXPECT_TRUE(r.result.empty());
    EXPECT_EQ(r.result_template_id, 0);
    EXPECT_EQ(r.skill_req, 0);
    EXPECT_EQ(r.skill_limit, 0);
    EXPECT_EQ(r.success_rate, 0);
    EXPECT_TRUE(r.ingredients.empty());
}

TEST(recipe_ingredient_test, default_values)
{
    recipe_ingredient ing;
    EXPECT_EQ(ing.item_id, 0);
    EXPECT_EQ(ing.count, 1);
}

TEST(craft_result_test, default_values)
{
    craft_result r;
    EXPECT_FALSE(r.success);
}

// === Edge case tests ===

TEST_F(manufacturing_success_test, zero_base_rate)
{
    auto recipe = make_recipe(0, 0);
    auto chance = manufacturing_system::calculate_success_chance(0, 0, recipe);
    EXPECT_EQ(chance, 10);  // clamped to minimum
}

TEST_F(manufacturing_success_test, max_skill_no_extra_bonus)
{
    auto recipe = make_recipe(0, 50);
    // skill = 100: bonus = min(40, 200) = 40
    auto chance = manufacturing_system::calculate_success_chance(100, 0, recipe);
    EXPECT_EQ(chance, 90);  // 50 + 40 = 90
}

}  // namespace hb::crafting
