// test_alchemy.cpp
// Unit tests for alchemy system

#include "crafting/alchemy_system.h"
#include "crafting/recipe_config.h"

#include <gtest/gtest.h>

namespace hb::crafting {

// === Success chance calculation tests ===

class alchemy_success_test : public ::testing::Test {
protected:
    craft_recipe make_recipe(int16_t skill_limit, int16_t difficulty)
    {
        craft_recipe r;
        r.skill_limit = skill_limit;
        r.difficulty = difficulty;
        return r;
    }
};

TEST_F(alchemy_success_test, base_difficulty_only)
{
    auto recipe = make_recipe(0, 30);
    // Base: 100 - 30 = 70, skill = 0, int = 0
    auto chance = alchemy_system::calculate_success_chance(0, 0, recipe);
    EXPECT_EQ(chance, 70);
}

TEST_F(alchemy_success_test, skill_bonus)
{
    auto recipe = make_recipe(0, 30);
    // skill = 40: bonus = 40/2 = 20
    auto chance = alchemy_system::calculate_success_chance(40, 0, recipe);
    EXPECT_EQ(chance, 90);  // 70 + 20 + 0
}

TEST_F(alchemy_success_test, int_bonus)
{
    auto recipe = make_recipe(0, 30);
    // int = 30: bonus = 30/3 = 10
    auto chance = alchemy_system::calculate_success_chance(0, 30, recipe);
    EXPECT_EQ(chance, 80);  // 70 + 0 + 10
}

TEST_F(alchemy_success_test, combined_bonuses)
{
    auto recipe = make_recipe(0, 40);
    // skill = 20, int = 30
    // Base: 100 - 40 = 60
    // skill: 20/2 = 10
    // int: 30/3 = 10
    auto chance = alchemy_system::calculate_success_chance(20, 30, recipe);
    EXPECT_EQ(chance, 80);  // 60 + 10 + 10
}

TEST_F(alchemy_success_test, clamp_minimum_5)
{
    auto recipe = make_recipe(0, 99);
    // Base: 100 - 99 = 1
    auto chance = alchemy_system::calculate_success_chance(0, 0, recipe);
    EXPECT_EQ(chance, 5);  // clamped up from 1
}

TEST_F(alchemy_success_test, clamp_maximum_98)
{
    auto recipe = make_recipe(0, 0);
    // Base: 100, high skill + int
    auto chance = alchemy_system::calculate_success_chance(100, 90, recipe);
    // 100 + 50 + 30 = 180, clamped to 98
    EXPECT_EQ(chance, 98);
}

TEST_F(alchemy_success_test, high_difficulty)
{
    auto recipe = make_recipe(0, 80);
    // Base: 100 - 80 = 20, skill = 10, int = 15
    // 20 + 5 + 5 = 30
    auto chance = alchemy_system::calculate_success_chance(10, 15, recipe);
    EXPECT_EQ(chance, 30);
}

TEST_F(alchemy_success_test, difficulty_exceeds_100)
{
    auto recipe = make_recipe(0, 120);
    // Base: 100 - 120 = -20
    auto chance = alchemy_system::calculate_success_chance(0, 0, recipe);
    EXPECT_EQ(chance, 5);  // clamped to minimum
}

TEST_F(alchemy_success_test, high_skill_overcomes_difficulty)
{
    auto recipe = make_recipe(0, 90);
    // Base: 100 - 90 = 10
    // skill = 80: 80/2 = 40
    // int = 60: 60/3 = 20
    auto chance = alchemy_system::calculate_success_chance(80, 60, recipe);
    EXPECT_EQ(chance, 70);  // 10 + 40 + 20
}

TEST_F(alchemy_success_test, zero_difficulty)
{
    auto recipe = make_recipe(0, 0);
    // Base: 100 - 0 = 100, no bonuses, clamped to 98
    auto chance = alchemy_system::calculate_success_chance(0, 0, recipe);
    EXPECT_EQ(chance, 98);
}

// === Craft recipe config tests ===

TEST(craft_recipe_test, default_values)
{
    craft_recipe r;
    EXPECT_EQ(r.id, 0);
    EXPECT_TRUE(r.result.empty());
    EXPECT_EQ(r.result_template_id, 0);
    EXPECT_EQ(r.skill_limit, 0);
    EXPECT_EQ(r.difficulty, 0);
    EXPECT_TRUE(r.ingredients.empty());
}

// === Integer truncation edge cases ===

TEST_F(alchemy_success_test, odd_skill_truncates)
{
    auto recipe = make_recipe(0, 50);
    // skill = 11: 11/2 = 5 (integer division)
    auto chance = alchemy_system::calculate_success_chance(11, 0, recipe);
    EXPECT_EQ(chance, 55);  // 50 + 5 + 0
}

TEST_F(alchemy_success_test, odd_int_truncates)
{
    auto recipe = make_recipe(0, 50);
    // int = 10: 10/3 = 3 (integer division)
    auto chance = alchemy_system::calculate_success_chance(0, 10, recipe);
    EXPECT_EQ(chance, 53);  // 50 + 0 + 3
}

}  // namespace hb::crafting
