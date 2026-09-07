// Tests for specialty/specialty_system.h - monster mastery ladders.
#include "specialty/specialty_system.h"
#include <gtest/gtest.h>

using namespace hb;
using namespace hb::specialty;

namespace
{
constexpr const char* yaml =
    "specialties:\n"
    "  - {npc_type: 10, npc_name: Slime, base_kills: 150, ladder: [damage, drop_rate, drop_rate]}\n"
    "  - {npc_type: 14, npc_name: Orc, base_kills: 100, ladder: [damage, damage_reduction, hit_ratio_pct]}\n";

class specialty_test : public ::testing::Test
{
protected:
    void SetUp() override
    {
        sys_.initialize();
        auto loaded = sys_.load_from_yaml(yaml);
        ASSERT_TRUE(loaded.is_ok()) << loaded.error();
        ASSERT_EQ(loaded.value(), 2u);
        sys_.register_player(me_);
    }
    specialty_system sys_;
    player_id me_{7};
};
} // namespace

TEST(specialty_levels, kills_grow_triangularly)
{
    EXPECT_EQ(specialty_system::kills_for_level(150, 1), 150);
    EXPECT_EQ(specialty_system::kills_for_level(150, 2), 450);
    EXPECT_EQ(specialty_system::kills_for_level(150, 3), 900);
    EXPECT_EQ(specialty_system::level_for(149, 150, 6), 0);
    EXPECT_EQ(specialty_system::level_for(150, 150, 6), 1);
    EXPECT_EQ(specialty_system::level_for(449, 150, 6), 1);
    EXPECT_EQ(specialty_system::level_for(450, 150, 6), 2);
    EXPECT_EQ(specialty_system::level_for(1000000, 150, 3), 3); // capped at the ladder
}

TEST_F(specialty_test, a_level_is_gained_at_the_threshold_and_reported_once)
{
    for (int i = 1; i < 150; ++i)
        EXPECT_FALSE(sys_.record_kill(me_, 10).has_value()) << "kill " << i;
    auto gained = sys_.record_kill(me_, 10);
    ASSERT_TRUE(gained.has_value());
    EXPECT_EQ(gained->level, 1);
    EXPECT_EQ(gained->kills, 150);
    EXPECT_EQ(gained->npc_name, "Slime");
    EXPECT_EQ(gained->max_level, 3);
    EXPECT_EQ(gained->next_level_kills, 450);
    ASSERT_EQ(gained->unlocked.size(), 1u);
    EXPECT_EQ(gained->unlocked[0], bonus_kind::damage);
    EXPECT_FALSE(sys_.record_kill(me_, 10).has_value());
    EXPECT_FALSE(sys_.record_kill(me_, 99).has_value()); // no specialty for that monster
}

TEST_F(specialty_test, bonuses_follow_the_ladder_and_percentages_multiply)
{
    const auto* orc = sys_.find(14);
    ASSERT_NE(orc, nullptr);
    auto none = specialty_system::bonuses_for(*orc, 0);
    EXPECT_FALSE(none.any());
    auto two = specialty_system::bonuses_for(*orc, 2);
    EXPECT_EQ(two.damage, damage_per_step);
    EXPECT_EQ(two.reduction_pct, reduction_per_step);
    EXPECT_FLOAT_EQ(two.hit_mult, 1.0f);
    auto three = specialty_system::bonuses_for(*orc, 3);
    EXPECT_FLOAT_EQ(three.hit_mult, 1.0f + hit_ratio_pct_per_step / 100.0f);

    const auto* slime = sys_.find(10);
    auto slime3 = specialty_system::bonuses_for(*slime, 3);
    const float step = 1.0f + drop_rate_pct_per_step / 100.0f;
    EXPECT_FLOAT_EQ(slime3.drop_mult, step * step);

    // Live bonuses follow the recorded kills
    EXPECT_FALSE(sys_.bonuses(me_, 14).any());
    for (int i = 0; i < 100; ++i)
        sys_.record_kill(me_, 14);
    EXPECT_EQ(sys_.bonuses(me_, 14).damage, damage_per_step);
}

TEST_F(specialty_test, serialize_round_trip_and_progress_listing)
{
    for (int i = 0; i < 160; ++i)
        sys_.record_kill(me_, 10);
    for (int i = 0; i < 3; ++i)
        sys_.record_kill(me_, 14);
    auto json = sys_.serialize(me_);
    EXPECT_NE(json.find("\"kills\":160"), std::string::npos);

    specialty_system other;
    other.initialize();
    ASSERT_TRUE(other.load_from_yaml(yaml).is_ok());
    other.register_player(me_);
    other.deserialize(me_, json);
    EXPECT_EQ(other.kills(me_, 10), 160);
    EXPECT_EQ(other.kills(me_, 14), 3);

    auto list = other.progress(me_);
    ASSERT_EQ(list.size(), 2u);
    EXPECT_EQ(list[0].npc_type, 10); // most kills first
    EXPECT_EQ(list[0].level, 1);
    EXPECT_EQ(list[1].level, 0);
    EXPECT_EQ(list[1].next_level_kills, 100);

    other.deserialize(me_, "not json");
    EXPECT_EQ(other.kills(me_, 10), 0);
}
