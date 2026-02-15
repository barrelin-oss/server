// test_death_respawn.cpp
// Unit tests for death/respawn system (XP penalty, PK tracking, bounty)

#include <gtest/gtest.h>
#include "player/experience.h"
#include "player/player.h"

using namespace hb::player;

// ========== remove_experience tests ==========

TEST(death_xp_penalty, level_1_immune)
{
    experience_state exp;
    exp.level = 1;
    exp.experience = 500;

    auto removed = exp.remove_experience(100);

    EXPECT_EQ(removed, 0);
    EXPECT_EQ(exp.experience, 500);
}

TEST(death_xp_penalty, zero_amount_removes_nothing)
{
    experience_state exp;
    exp.level = 10;
    exp.experience = exp_table.exp_for_level(10) + 5000;

    auto removed = exp.remove_experience(0);

    EXPECT_EQ(removed, 0);
}

TEST(death_xp_penalty, negative_amount_removes_nothing)
{
    experience_state exp;
    exp.level = 10;
    exp.experience = exp_table.exp_for_level(10) + 5000;

    auto removed = exp.remove_experience(-100);

    EXPECT_EQ(removed, 0);
}

TEST(death_xp_penalty, clamps_to_level_floor)
{
    experience_state exp;
    exp.level = 10;
    int64_t floor = exp_table.exp_for_level(10);
    exp.experience = floor + 100; // 100 above floor

    // Try to remove 500 - should only remove 100
    auto removed = exp.remove_experience(500);

    EXPECT_EQ(removed, 100);
    EXPECT_EQ(exp.experience, floor);
}

TEST(death_xp_penalty, at_floor_removes_nothing)
{
    experience_state exp;
    exp.level = 10;
    exp.experience = exp_table.exp_for_level(10); // Exactly at floor

    auto removed = exp.remove_experience(100);

    EXPECT_EQ(removed, 0);
    EXPECT_EQ(exp.experience, exp_table.exp_for_level(10));
}

TEST(death_xp_penalty, partial_removal)
{
    experience_state exp;
    exp.level = 10;
    int64_t floor = exp_table.exp_for_level(10);
    exp.experience = floor + 5000;

    auto removed = exp.remove_experience(3000);

    EXPECT_EQ(removed, 3000);
    EXPECT_EQ(exp.experience, floor + 2000);
}

TEST(death_xp_penalty, exact_removal)
{
    experience_state exp;
    exp.level = 5;
    int64_t floor = exp_table.exp_for_level(5);
    exp.experience = floor + 250;

    auto removed = exp.remove_experience(250);

    EXPECT_EQ(removed, 250);
    EXPECT_EQ(exp.experience, floor);
}

TEST(death_xp_penalty, level_not_reduced)
{
    experience_state exp;
    exp.level = 10;
    int64_t floor = exp_table.exp_for_level(10);
    exp.experience = floor + 50;

    exp.remove_experience(999999);

    // Level should remain 10
    EXPECT_EQ(exp.level, 10);
    EXPECT_GE(exp.experience, floor);
}

// ========== pk_state tests ==========

TEST(pk_state_test, starts_innocent)
{
    pk_state pk;

    EXPECT_TRUE(pk.is_innocent());
    EXPECT_FALSE(pk.is_criminal());
    EXPECT_FALSE(pk.is_murderer());
    EXPECT_EQ(pk.count, 0);
    EXPECT_EQ(pk.points, 0);
}

TEST(pk_state_test, add_kill_gains_points)
{
    pk_state pk;

    pk.add_kill();

    EXPECT_EQ(pk.count, 1);
    EXPECT_EQ(pk.points, 50);
    EXPECT_TRUE(pk.is_criminal());  // 50 >= 30
    EXPECT_FALSE(pk.is_murderer()); // 50 < 100
}

TEST(pk_state_test, two_kills_becomes_murderer)
{
    pk_state pk;

    pk.add_kill();
    pk.add_kill();

    EXPECT_EQ(pk.count, 2);
    EXPECT_EQ(pk.points, 100);
    EXPECT_TRUE(pk.is_murderer());
}

TEST(pk_state_test, decay_reduces_points)
{
    pk_state pk;
    pk.add_kill(); // 50 points
    pk.add_kill(); // 100 points

    pk.decay_points(80);

    EXPECT_EQ(pk.points, 20);
    EXPECT_TRUE(pk.is_innocent()); // Back to innocent
    EXPECT_EQ(pk.count, 2);        // Kill count stays
}

TEST(pk_state_test, decay_clamps_to_zero)
{
    pk_state pk;
    pk.add_kill();

    pk.decay_points(999);

    EXPECT_EQ(pk.points, 0);
    EXPECT_TRUE(pk.is_innocent());
}

TEST(pk_state_test, criminal_threshold)
{
    pk_state pk;
    pk.points = 29;
    EXPECT_TRUE(pk.is_innocent());

    pk.points = 30;
    EXPECT_TRUE(pk.is_criminal());
    EXPECT_FALSE(pk.is_murderer());
}

TEST(pk_state_test, murderer_threshold)
{
    pk_state pk;
    pk.points = 99;
    EXPECT_TRUE(pk.is_criminal());
    EXPECT_FALSE(pk.is_murderer());

    pk.points = 100;
    EXPECT_TRUE(pk.is_murderer());
    EXPECT_FALSE(pk.is_criminal()); // is_criminal checks < 100
}

// ========== experience_table tests ==========

TEST(experience_table_test, level_1_requires_no_exp)
{
    EXPECT_EQ(exp_table.exp_for_level(1), 0);
}

TEST(experience_table_test, levels_are_monotonically_increasing)
{
    for (int i = 2; i <= max_level; ++i)
    {
        EXPECT_GT(exp_table.exp_for_level(static_cast<uint8_t>(i)),
                  exp_table.exp_for_level(static_cast<uint8_t>(i - 1)))
            << "Level " << i << " should require more exp than level " << (i - 1);
    }
}
