// Tests for achievement/achievement_system.h
#include "achievement/achievement_system.h"
#include <gtest/gtest.h>

using namespace hb;
using namespace hb::achievement;

namespace
{
constexpr const char* yaml =
    "achievements:\n"
    "  - {id: 1, category: general, kind: level, target: 20, points: 5, name: Apprentice, description: L20}\n"
    "  - {id: 2, category: pvm, kind: kills_total, target: 3, points: 10, name: Hunter, description: kills}\n"
    "  - {id: 3, category: pvm, kind: kills_type, param: 14, target: 2, points: 15, name: Orcs, title: Orc Slayer}\n"
    "  - {id: 4, category: general, kind: gold_looted, target: 100, points: 5, name: Gold}\n";

class achievement_test : public ::testing::Test
{
protected:
    void SetUp() override
    {
        sys_.initialize();
        auto loaded = sys_.load_from_yaml(yaml);
        ASSERT_TRUE(loaded.is_ok()) << loaded.error();
        ASSERT_EQ(loaded.value(), 4u);
        sys_.register_player(me_);
    }
    achievement_system sys_;
    player_id me_{5};
};
} // namespace

TEST_F(achievement_test, counters_unlock_once_at_the_target)
{
    EXPECT_TRUE(sys_.add(me_, counter_kind::kills_total, 0, 1).empty());
    EXPECT_TRUE(sys_.add(me_, counter_kind::kills_total, 0, 1).empty());
    auto got = sys_.add(me_, counter_kind::kills_total, 0, 1);
    ASSERT_EQ(got.size(), 1u);
    EXPECT_EQ(got[0]->id, 2);
    EXPECT_TRUE(sys_.is_unlocked(me_, 2));
    EXPECT_TRUE(sys_.add(me_, counter_kind::kills_total, 0, 5).empty()); // never twice
    EXPECT_EQ(sys_.counter(me_, counter_kind::kills_total), 8);
    EXPECT_EQ(sys_.points(me_), 10);
}

TEST_F(achievement_test, per_type_kills_only_count_their_type)
{
    sys_.add(me_, counter_kind::kills_type, 10, 5);
    EXPECT_FALSE(sys_.is_unlocked(me_, 3));
    sys_.add(me_, counter_kind::kills_type, 14, 1);
    auto got = sys_.add(me_, counter_kind::kills_type, 14, 1);
    ASSERT_EQ(got.size(), 1u);
    EXPECT_EQ(got[0]->title, "Orc Slayer");
}

TEST_F(achievement_test, level_keeps_the_maximum_and_gold_sums)
{
    EXPECT_TRUE(sys_.set_level(me_, 10).empty());
    EXPECT_TRUE(sys_.set_level(me_, 5).empty()); // lower: ignored
    EXPECT_EQ(sys_.counter(me_, counter_kind::level), 10);
    ASSERT_EQ(sys_.set_level(me_, 20).size(), 1u);
    sys_.add(me_, counter_kind::gold_looted, 0, 60);
    ASSERT_EQ(sys_.add(me_, counter_kind::gold_looted, 0, 40).size(), 1u);
    EXPECT_EQ(sys_.points(me_), 10);
}

TEST_F(achievement_test, serialize_round_trip_and_progress)
{
    sys_.add(me_, counter_kind::kills_total, 0, 3);
    sys_.set_level(me_, 12);
    auto json = sys_.serialize(me_);

    achievement_system other;
    other.initialize();
    ASSERT_TRUE(other.load_from_yaml(yaml).is_ok());
    other.register_player(me_);
    other.deserialize(me_, json);
    EXPECT_TRUE(other.is_unlocked(me_, 2));
    EXPECT_EQ(other.counter(me_, counter_kind::level), 12);

    auto list = other.progress(me_);
    ASSERT_EQ(list.size(), 4u);
    EXPECT_EQ(list[0].def->id, 1);
    EXPECT_EQ(list[0].current, 12);
    EXPECT_EQ(list[0].unlocked_at, 0);
    EXPECT_GT(list[1].unlocked_at, 0);
    EXPECT_EQ(list[1].current, 3); // capped at the target

    other.deserialize(me_, "[]");
    EXPECT_FALSE(other.is_unlocked(me_, 2));
}
