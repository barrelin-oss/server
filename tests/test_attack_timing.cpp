// Tests for combat/attack_timing.h - per-weapon attack pace with STR penalty.
#include "combat/attack_timing.h"

#include <gtest/gtest.h>

using namespace hb;
using namespace hb::combat;

namespace
{
auto defaults() -> attack_speed_config
{
    return attack_speed_config{}; // base 1000, step 60, penalty 25, min 300, max 3000, str_per_speed 6
}
} // namespace

TEST(attack_timing_test, unarmed_is_base_pace)
{
    EXPECT_EQ(attack_interval_ms(0, 0, 10, defaults()), 1000);
    EXPECT_EQ(attack_interval_ms(0, 0, 200, defaults()), 1000);
}

TEST(attack_timing_test, weapon_speed_adds_step_per_point)
{
    // Speed 6 (LongSword) derives 36 STR; with enough STR only the speed term applies.
    EXPECT_EQ(attack_interval_ms(6, 0, 36, defaults()), 1000 + 6 * 60);
    EXPECT_EQ(attack_interval_ms(15, 0, 90, defaults()), 1000 + 15 * 60);
}

TEST(attack_timing_test, low_str_pays_penalty_per_missing_point)
{
    // 10 STR on a speed-6 weapon: 26 points short -> +650 ms.
    EXPECT_EQ(attack_interval_ms(6, 0, 10, defaults()), 1360 + 26 * 25);
}

TEST(attack_timing_test, explicit_str_speed_req_wins_over_derived)
{
    EXPECT_EQ(attack_interval_ms(6, 20, 20, defaults()), 1360);
    EXPECT_EQ(str_for_full_speed(6, 20, defaults()), 20);
    EXPECT_EQ(str_for_full_speed(6, 0, defaults()), 36);
    EXPECT_EQ(str_for_full_speed(0, 0, defaults()), 0);
}

TEST(attack_timing_test, clamped_to_max_and_min)
{
    EXPECT_EQ(attack_interval_ms(15, 0, 0, defaults()), 3000);
    auto cfg = defaults();
    cfg.base_ms = 100;
    EXPECT_EQ(attack_interval_ms(0, 0, 10, cfg), cfg.min_ms);
}

TEST(attack_timing_test, disabled_falls_back_to_min)
{
    auto cfg = defaults();
    cfg.enabled = false;
    EXPECT_EQ(attack_interval_ms(15, 0, 0, cfg), cfg.min_ms);
}
