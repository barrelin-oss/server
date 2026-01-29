// test_player.cpp
// Unit tests for player system

#include <gtest/gtest.h>
#include "core/types.h"
#include "player/stats.h"
#include "player/experience.h"
#include "player/equipment.h"
#include "player/player.h"
#include "player/player_system.h"

using hb::item_id;
using namespace hb::player;

// Stats tests

TEST(base_stats_test, default_values) {
    base_stats stats;
    EXPECT_EQ(stats.strength, 10);
    EXPECT_EQ(stats.dexterity, 10);
    EXPECT_EQ(stats.vitality, 10);
    EXPECT_EQ(stats.intelligence, 10);
    EXPECT_EQ(stats.magic, 10);
    EXPECT_EQ(stats.charisma, 10);
}

TEST(base_stats_test, derived_values) {
    base_stats stats;
    stats.strength = 20;
    stats.vitality = 30;
    stats.dexterity = 15;
    stats.level_bonus = 10;

    EXPECT_GT(stats.max_hp(), 0);
    EXPECT_GT(stats.physical_attack(), 0);
    EXPECT_GT(stats.hit_rate(), 0);
}

TEST(stat_modifiers_test, addition) {
    stat_modifiers a;
    a.strength = 5;
    a.attack_power = 10;

    stat_modifiers b;
    b.strength = 3;
    b.defense = 15;

    auto result = a + b;
    EXPECT_EQ(result.strength, 8);
    EXPECT_EQ(result.attack_power, 10);
    EXPECT_EQ(result.defense, 15);
}

TEST(computed_stats_test, compute) {
    base_stats base;
    base.strength = 20;
    base.vitality = 25;

    stat_modifiers mods;
    mods.attack_power = 50;
    mods.defense = 100;

    computed_stats computed;
    computed.compute(base, mods);

    EXPECT_EQ(computed.strength, 20);
    EXPECT_EQ(computed.vitality, 25);
    EXPECT_GT(computed.max_hp, 0);
    EXPECT_GT(computed.attack_power, 0);
    EXPECT_EQ(computed.defense, 100);
}

// Experience tests

TEST(experience_table_test, level_requirements) {
    EXPECT_EQ(exp_table.exp_for_level(1), 0);
    EXPECT_GT(exp_table.exp_for_level(2), 0);
    EXPECT_GT(exp_table.exp_for_level(10), exp_table.exp_for_level(5));
    EXPECT_GT(exp_table.exp_for_level(100), exp_table.exp_for_level(50));
}

TEST(experience_state_test, add_experience) {
    experience_state exp;
    exp.level = 1;
    exp.experience = 0;

    int levels_gained = exp.add_experience(exp_table.exp_for_level(3));
    EXPECT_GE(levels_gained, 1);
    EXPECT_GE(exp.level, 2);
}

TEST(experience_state_test, max_level_cap) {
    experience_state exp;
    exp.level = max_level;
    exp.experience = 999999999;

    int levels_gained = exp.add_experience(999999999);
    EXPECT_EQ(levels_gained, 0);
    EXPECT_EQ(exp.level, max_level);
}

TEST(stat_points_test, allocation) {
    stat_points points;
    points.available = 10;

    EXPECT_TRUE(points.allocate(5));
    EXPECT_EQ(points.available, 5);
    EXPECT_EQ(points.used, 5);

    EXPECT_FALSE(points.allocate(10));  // Not enough
    EXPECT_EQ(points.available, 5);
}

// Equipment tests

TEST(equipment_state_test, equip_unequip) {
    equipment_state equip;

    EXPECT_FALSE(equip.has_equipped(equip_slot::weapon));

    equip.equip(equip_slot::weapon, item_id{100}, 50, 100);
    EXPECT_TRUE(equip.has_equipped(equip_slot::weapon));
    EXPECT_EQ(equip.weapon().id.value, 100);
    EXPECT_EQ(equip.weapon().durability, 50);

    auto item = equip.unequip(equip_slot::weapon);
    EXPECT_FALSE(equip.has_equipped(equip_slot::weapon));
    EXPECT_EQ(item.id.value, 100);
}

TEST(equipped_item_test, durability) {
    equipped_item item;
    item.id = item_id{1};
    item.durability = 50;
    item.max_durability = 100;

    EXPECT_FLOAT_EQ(item.durability_percent(), 0.5f);
}

// Player component tests

TEST(player_test, status_flags) {
    player p;
    EXPECT_FALSE(p.has_status(player_status::poisoned));

    p.add_status(player_status::poisoned);
    EXPECT_TRUE(p.has_status(player_status::poisoned));

    p.remove_status(player_status::poisoned);
    EXPECT_FALSE(p.has_status(player_status::poisoned));
}

TEST(player_test, resources) {
    player p;
    p.computed.max_hp = 100;
    p.computed.max_mp = 50;
    p.hp = 100;
    p.mp = 50;

    p.damage_hp(30);
    EXPECT_EQ(p.hp, 70);
    EXPECT_TRUE(p.is_alive());

    p.heal_hp(50);
    EXPECT_EQ(p.hp, 100);  // Capped at max

    p.damage_hp(200);
    EXPECT_EQ(p.hp, 0);
    EXPECT_TRUE(p.is_dead());
}

TEST(player_test, mana_spending) {
    player p;
    p.computed.max_mp = 100;
    p.mp = 50;

    EXPECT_TRUE(p.spend_mp(30));
    EXPECT_EQ(p.mp, 20);

    EXPECT_FALSE(p.spend_mp(30));  // Not enough
    EXPECT_EQ(p.mp, 20);
}

TEST(hunger_state_test, decay_and_consume) {
    hunger_state hunger;
    hunger.level = 100;

    EXPECT_TRUE(hunger.is_full());

    hunger.decay(30);
    EXPECT_EQ(hunger.level, 70);
    EXPECT_FALSE(hunger.is_hungry());

    hunger.decay(50);
    EXPECT_TRUE(hunger.is_hungry());

    hunger.consume(100);
    EXPECT_EQ(hunger.level, 100);
}

TEST(pk_state_test, kill_tracking) {
    pk_state pk;
    EXPECT_TRUE(pk.is_innocent());

    pk.add_kill();
    EXPECT_EQ(pk.count, 1);
    EXPECT_EQ(pk.points, 50);
    EXPECT_TRUE(pk.is_criminal());

    pk.add_kill();
    EXPECT_TRUE(pk.is_murderer());

    pk.decay_points(80);
    // 100 - 80 = 20 points, which is innocent (< 30)
    EXPECT_TRUE(pk.is_innocent());
}

// Player system tests

class player_system_test : public ::testing::Test {
protected:
    void SetUp() override {
        system_.initialize();
    }

    void TearDown() override {
        system_.shutdown();
    }

    player_system system_;
};

TEST_F(player_system_test, lifecycle) {
    EXPECT_TRUE(system_.is_initialized());
    EXPECT_EQ(system_.name(), "player_system");
}

TEST_F(player_system_test, create_player) {
    player_create_info info;
    info.name = "TestPlayer";
    info.account_name = "testaccount";
    info.sex = gender::male;
    info.profession = player_class::warrior;

    auto result = system_.create_player(info);
    ASSERT_TRUE(result.is_ok());

    auto id = result.value();
    EXPECT_TRUE(id.is_valid());
    EXPECT_EQ(system_.player_count(), 1);

    auto* p = system_.get_player(id);
    ASSERT_NE(p, nullptr);
    EXPECT_EQ(p->name, "TestPlayer");
}

TEST_F(player_system_test, get_by_name) {
    player_create_info info;
    info.name = "UniquePlayer";

    auto result = system_.create_player(info);
    ASSERT_TRUE(result.is_ok());

    auto* p = system_.get_player_by_name("UniquePlayer");
    ASSERT_NE(p, nullptr);
    EXPECT_EQ(p->name, "UniquePlayer");

    EXPECT_EQ(system_.get_player_by_name("Nonexistent"), nullptr);
}

TEST_F(player_system_test, remove_player) {
    player_create_info info;
    info.name = "ToRemove";

    auto result = system_.create_player(info);
    auto id = result.value();

    EXPECT_EQ(system_.player_count(), 1);
    system_.remove_player(id);
    EXPECT_EQ(system_.player_count(), 0);
    EXPECT_EQ(system_.get_player(id), nullptr);
}

TEST_F(player_system_test, duplicate_name_fails) {
    player_create_info info;
    info.name = "DuplicateName";

    auto result1 = system_.create_player(info);
    EXPECT_TRUE(result1.is_ok());

    auto result2 = system_.create_player(info);
    EXPECT_TRUE(result2.is_err());
}

TEST_F(player_system_test, add_experience) {
    player_create_info info;
    info.name = "ExpPlayer";

    auto result = system_.create_player(info);
    auto id = result.value();

    auto* p = system_.get_player(id);
    EXPECT_EQ(p->experience.level, 1);

    system_.add_experience(id, exp_table.exp_for_level(5));
    EXPECT_GT(p->experience.level, 1);
}

TEST_F(player_system_test, status_effects) {
    player_create_info info;
    info.name = "StatusPlayer";

    auto result = system_.create_player(info);
    auto id = result.value();

    auto* p = system_.get_player(id);
    EXPECT_FALSE(p->has_status(player_status::poisoned));

    system_.add_status(id, player_status::poisoned);
    EXPECT_TRUE(p->has_status(player_status::poisoned));

    system_.remove_status(id, player_status::poisoned);
    EXPECT_FALSE(p->has_status(player_status::poisoned));
}
