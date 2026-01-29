// test_skill.cpp
// Unit tests for skill system

#include <gtest/gtest.h>
#include "core/types.h"
#include "skill/skill.h"
#include "skill/skill_system.h"

using hb::player_id;
using namespace hb::skill;

// Skill state tests

TEST(skill_state_test, default_values) {
    skill_state state;
    EXPECT_EQ(state.type, skill_type::none);
    EXPECT_EQ(state.level, 0);
    EXPECT_EQ(state.experience, 0);
}

TEST(skill_state_test, exp_for_next_level) {
    skill_state state;
    state.level = 0;
    EXPECT_EQ(state.exp_for_next_level(), 100);

    state.level = 10;
    EXPECT_EQ(state.exp_for_next_level(), 1100);
}

TEST(skill_state_test, add_experience) {
    skill_state state;
    state.type = skill_type::sword;
    state.level = 0;
    state.experience = 0;

    // Level 0->1 needs 100 exp, Level 1->2 needs 200 exp
    // With 350 exp: 100 + 200 = 300, leaves 50 extra
    int16_t levels = state.add_experience(350);
    EXPECT_EQ(levels, 2);
    EXPECT_EQ(state.level, 2);
    EXPECT_EQ(state.experience, 50);  // 350 - 100 - 200 = 50
}

TEST(skill_state_test, mastery_levels) {
    skill_state state;
    state.level = 10;
    EXPECT_EQ(state.mastery(), mastery_level::novice);

    state.level = 50;
    EXPECT_EQ(state.mastery(), mastery_level::apprentice);

    state.level = 100;
    EXPECT_EQ(state.mastery(), mastery_level::master);

    state.level = 120;
    EXPECT_EQ(state.mastery(), mastery_level::grand_master);
}

// Player skills tests

TEST(player_skills_test, initialization) {
    player_skills skills;

    // All skills should be initialized with correct type
    EXPECT_EQ(skills.get(skill_type::sword).type, skill_type::sword);
    EXPECT_EQ(skills.get(skill_type::mining).type, skill_type::mining);
}

TEST(player_skills_test, get_set_level) {
    player_skills skills;

    skills.set_level(skill_type::sword, 50);
    EXPECT_EQ(skills.level(skill_type::sword), 50);

    skills.set_level(skill_type::mining, 75);
    EXPECT_EQ(skills.level(skill_type::mining), 75);
}

TEST(player_skills_test, add_experience) {
    player_skills skills;

    int16_t levels = skills.add_experience(skill_type::sword, 150);
    EXPECT_EQ(levels, 1);
    EXPECT_EQ(skills.level(skill_type::sword), 1);
}

TEST(player_skills_test, total_combat_skill) {
    player_skills skills;

    skills.set_level(skill_type::sword, 50);
    skills.set_level(skill_type::axe, 30);
    skills.set_level(skill_type::bow, 20);

    int32_t total = skills.total_combat_skill();
    EXPECT_EQ(total, 100);  // 50 + 30 + 20
}

// Skill category tests

TEST(skill_category_test, combat_skills) {
    EXPECT_EQ(get_skill_category(skill_type::sword), skill_category::combat);
    EXPECT_EQ(get_skill_category(skill_type::axe), skill_category::combat);
    EXPECT_EQ(get_skill_category(skill_type::bow), skill_category::combat);
}

TEST(skill_category_test, gathering_skills) {
    EXPECT_EQ(get_skill_category(skill_type::mining), skill_category::gathering);
    EXPECT_EQ(get_skill_category(skill_type::fishing), skill_category::gathering);
    EXPECT_EQ(get_skill_category(skill_type::farming), skill_category::gathering);
}

TEST(skill_category_test, crafting_skills) {
    EXPECT_EQ(get_skill_category(skill_type::manufacturing), skill_category::crafting);
    EXPECT_EQ(get_skill_category(skill_type::alchemy), skill_category::crafting);
}

TEST(skill_category_test, defense_skills) {
    EXPECT_EQ(get_skill_category(skill_type::shield), skill_category::defense);
    EXPECT_EQ(get_skill_category(skill_type::magic_resistance), skill_category::defense);
}

// Skill system tests

class skill_system_test : public ::testing::Test {
protected:
    void SetUp() override {
        system_.initialize();
    }

    void TearDown() override {
        system_.shutdown();
    }

    skill_system system_;
};

TEST_F(skill_system_test, lifecycle) {
    EXPECT_TRUE(system_.is_initialized());
    EXPECT_EQ(system_.name(), "skill_system");
}

TEST_F(skill_system_test, register_unregister_player) {
    player_id player{1};

    system_.register_player(player);
    EXPECT_EQ(system_.get_skill_level(player, skill_type::sword), 0);

    system_.unregister_player(player);
    EXPECT_EQ(system_.get_skill_level(player, skill_type::sword), 0);  // Returns 0 for unknown player
}

TEST_F(skill_system_test, set_skill_level) {
    player_id player{1};
    system_.register_player(player);

    system_.set_skill_level(player, skill_type::sword, 50);
    EXPECT_EQ(system_.get_skill_level(player, skill_type::sword), 50);
}

TEST_F(skill_system_test, add_skill_exp) {
    player_id player{1};
    system_.register_player(player);

    int16_t levels = system_.add_skill_exp(player, skill_type::mining, 500);
    EXPECT_GT(levels, 0);
    EXPECT_GT(system_.get_skill_level(player, skill_type::mining), 0);
}

TEST_F(skill_system_test, get_mastery) {
    player_id player{1};
    system_.register_player(player);

    system_.set_skill_level(player, skill_type::alchemy, 100);
    EXPECT_EQ(system_.get_mastery(player, skill_type::alchemy), mastery_level::master);
}

TEST_F(skill_system_test, reset_skill) {
    player_id player{1};
    system_.register_player(player);

    system_.set_skill_level(player, skill_type::sword, 50);
    system_.reset_skill(player, skill_type::sword);

    EXPECT_EQ(system_.get_skill_level(player, skill_type::sword), 0);
}

TEST_F(skill_system_test, reset_all_skills) {
    player_id player{1};
    system_.register_player(player);

    system_.set_skill_level(player, skill_type::sword, 50);
    system_.set_skill_level(player, skill_type::mining, 75);
    system_.reset_all_skills(player);

    EXPECT_EQ(system_.get_skill_level(player, skill_type::sword), 0);
    EXPECT_EQ(system_.get_skill_level(player, skill_type::mining), 0);
}

TEST_F(skill_system_test, level_up_callback) {
    player_id player{1};
    system_.register_player(player);

    bool callback_fired = false;
    system_.on_level_up([&](const skill_level_event& event) {
        callback_fired = true;
        EXPECT_EQ(event.player, player);
        EXPECT_EQ(event.skill, skill_type::fishing);
    });

    system_.add_skill_exp(player, skill_type::fishing, 500);
    EXPECT_TRUE(callback_fired);
}

TEST_F(skill_system_test, damage_bonus) {
    player_id player{1};
    system_.register_player(player);

    system_.set_skill_level(player, skill_type::sword, 50);
    int16_t bonus = system_.calculate_damage_bonus(player, skill_type::sword);
    EXPECT_EQ(bonus, 5);  // 50 / 10
}

TEST_F(skill_system_test, hit_bonus) {
    player_id player{1};
    system_.register_player(player);

    system_.set_skill_level(player, skill_type::axe, 30);
    int16_t bonus = system_.calculate_hit_bonus(player, skill_type::axe);
    EXPECT_EQ(bonus, 6);  // 30 / 5
}
