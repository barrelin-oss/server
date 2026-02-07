// test_party_exp_share.cpp
// Unit tests for party XP sharing functions

#include "social/party.h"

#include <gtest/gtest.h>

namespace hb::social {

// === calculate_party_exp_share (equal split) ===

TEST(party_exp_share_test, solo_kill_returns_full_exp) {
    EXPECT_EQ(calculate_party_exp_share(1000, 1), 1000);
}

TEST(party_exp_share_test, party_of_2_applies_2_percent_bonus) {
    // 1000 * 1.02 / 2 = 510
    EXPECT_EQ(calculate_party_exp_share(1000, 2), 510);
}

TEST(party_exp_share_test, party_of_3_applies_5_percent_bonus) {
    // 1000 * 1.05 / 3 = 350
    EXPECT_EQ(calculate_party_exp_share(1000, 3), 350);
}

TEST(party_exp_share_test, party_of_4_applies_7_percent_bonus) {
    // 1000 * 1.07 / 4 = 267.5 -> 268
    EXPECT_EQ(calculate_party_exp_share(1000, 4), 268);
}

TEST(party_exp_share_test, party_of_5_applies_10_percent_bonus) {
    // 1000 * 1.10 / 5 = 220
    EXPECT_EQ(calculate_party_exp_share(1000, 5), 220);
}

TEST(party_exp_share_test, party_of_6_applies_14_percent_bonus) {
    // 1000 * 1.14 / 6 = 190
    EXPECT_EQ(calculate_party_exp_share(1000, 6), 190);
}

TEST(party_exp_share_test, party_of_7_applies_17_percent_bonus) {
    // 1000 * 1.17 / 7 = 167.14 -> 167
    EXPECT_EQ(calculate_party_exp_share(1000, 7), 167);
}

TEST(party_exp_share_test, party_of_8_applies_20_percent_bonus) {
    // 1000 * 1.20 / 8 = 150
    EXPECT_EQ(calculate_party_exp_share(1000, 8), 150);
}

TEST(party_exp_share_test, clamps_above_8_to_8_bonus) {
    // 10 members should use index 8 bonus (20%)
    // 1000 * 1.20 / 10 = 120
    EXPECT_EQ(calculate_party_exp_share(1000, 10), 120);
}

TEST(party_exp_share_test, minimum_1_exp_per_member) {
    // 1 XP split among 8: 1 * 1.20 / 8 = 0.15 -> rounds to 0, clamped to 1
    EXPECT_EQ(calculate_party_exp_share(1, 8), 1);
}

TEST(party_exp_share_test, zero_exp_returns_zero) {
    EXPECT_EQ(calculate_party_exp_share(0, 4), 0);
}

TEST(party_exp_share_test, negative_exp_returns_zero) {
    EXPECT_EQ(calculate_party_exp_share(-100, 4), 0);
}

TEST(party_exp_share_test, zero_members_returns_zero) {
    EXPECT_EQ(calculate_party_exp_share(1000, 0), 0);
}

// === calculate_level_weighted_exp ===

TEST(party_exp_share_test, level_weighted_solo_returns_full_exp) {
    EXPECT_EQ(calculate_level_weighted_exp(1000, 1, 50, 50), 1000);
}

TEST(party_exp_share_test, level_weighted_equal_levels) {
    // Two members, both level 50, total = 100
    // 1000 * 1.02 * (50/100) = 510
    EXPECT_EQ(calculate_level_weighted_exp(1000, 2, 50, 100), 510);
}

TEST(party_exp_share_test, level_weighted_higher_level_gets_more) {
    // Party of 2: level 80 and level 20, total = 100
    // 1000 * 1.02 = 1020 pool
    // Level 80: 1020 * 80/100 = 816
    // Level 20: 1020 * 20/100 = 204
    EXPECT_EQ(calculate_level_weighted_exp(1000, 2, 80, 100), 816);
    EXPECT_EQ(calculate_level_weighted_exp(1000, 2, 20, 100), 204);
}

TEST(party_exp_share_test, level_weighted_party_of_4) {
    // Levels: 100, 80, 60, 40 = total 280
    // 1000 * 1.07 = 1070 pool
    // Level 100: 1070 * 100/280 = 382.14 -> 382
    // Level 40:  1070 * 40/280  = 152.86 -> 153
    EXPECT_EQ(calculate_level_weighted_exp(1000, 4, 100, 280), 382);
    EXPECT_EQ(calculate_level_weighted_exp(1000, 4, 40, 280), 153);
}

TEST(party_exp_share_test, level_weighted_minimum_1_exp) {
    // Very low level in big group
    // 10 XP * 1.20 = 12 pool, level 1 / total 700 = 0.0014 -> rounds to 0, clamped to 1
    EXPECT_EQ(calculate_level_weighted_exp(10, 8, 1, 700), 1);
}

TEST(party_exp_share_test, level_weighted_zero_total_levels_returns_zero) {
    EXPECT_EQ(calculate_level_weighted_exp(1000, 4, 50, 0), 0);
}

}  // namespace hb::social
