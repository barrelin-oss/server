// test_skill.cpp
// Unit tests for skill system

#include <gtest/gtest.h>
#include "core/types.h"
#include "skill/skill.h"
#include "skill/skill_system.h"

#include <filesystem>
#include <fstream>

using hb::player_id;
using namespace hb::skill;

// Skill state tests

TEST(skill_state_test, default_values)
{
    skill_state state;
    EXPECT_EQ(state.level, 0);
    EXPECT_EQ(state.total_uses, 0);
    EXPECT_EQ(state.uses_this_level, 0);
}

TEST(skill_state_test, mastery_levels)
{
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

TEST(player_skills_test, initialization)
{
    player_skills skills;

    // All skills should default to level 0
    EXPECT_EQ(skills.get(skill_type::short_sword).level, 0);
    EXPECT_EQ(skills.get(skill_type::mining).level, 0);
}

TEST(player_skills_test, get_set_level)
{
    player_skills skills;

    skills.set_level(skill_type::short_sword, 50);
    EXPECT_EQ(skills.level(skill_type::short_sword), 50);

    skills.set_level(skill_type::mining, 75);
    EXPECT_EQ(skills.level(skill_type::mining), 75);
}

TEST(player_skills_test, total_combat_skill)
{
    player_skills skills;

    skills.set_level(skill_type::short_sword, 50);
    skills.set_level(skill_type::axe, 30);
    skills.set_level(skill_type::archery, 20);

    int32_t total = skills.total_combat_skill();
    EXPECT_EQ(total, 100); // 50 + 30 + 20
}

// Skill category tests

TEST(skill_category_test, combat_skills)
{
    EXPECT_EQ(get_skill_category(skill_type::short_sword), skill_category::combat);
    EXPECT_EQ(get_skill_category(skill_type::axe), skill_category::combat);
    EXPECT_EQ(get_skill_category(skill_type::archery), skill_category::combat);
}

TEST(skill_category_test, gathering_skills)
{
    EXPECT_EQ(get_skill_category(skill_type::mining), skill_category::gathering);
    EXPECT_EQ(get_skill_category(skill_type::fishing), skill_category::gathering);
    EXPECT_EQ(get_skill_category(skill_type::farming), skill_category::gathering);
}

TEST(skill_category_test, crafting_skills)
{
    EXPECT_EQ(get_skill_category(skill_type::manufacturing), skill_category::crafting);
    EXPECT_EQ(get_skill_category(skill_type::alchemy), skill_category::crafting);
}

TEST(skill_category_test, defense_skills)
{
    EXPECT_EQ(get_skill_category(skill_type::shield), skill_category::defense);
    EXPECT_EQ(get_skill_category(skill_type::magic_resistance), skill_category::defense);
}

// Skill system tests

class skill_system_test : public ::testing::Test
{
protected:
    void SetUp() override { system_.initialize(); }

    void TearDown() override { system_.shutdown(); }

    skill_system system_;
};

TEST_F(skill_system_test, lifecycle)
{
    EXPECT_TRUE(system_.is_initialized());
    EXPECT_EQ(system_.name(), "skill_system");
}

TEST_F(skill_system_test, register_unregister_player)
{
    player_id player{1};

    system_.register_player(player);
    EXPECT_EQ(system_.get_skill_level(player, skill_type::short_sword), 0);

    system_.unregister_player(player);
    EXPECT_EQ(system_.get_skill_level(player, skill_type::short_sword), 0); // Returns 0 for unknown player
}

TEST_F(skill_system_test, set_skill_level)
{
    player_id player{1};
    system_.register_player(player);

    system_.set_skill_level(player, skill_type::short_sword, 50);
    EXPECT_EQ(system_.get_skill_level(player, skill_type::short_sword), 50);
    // set_skill_level resets uses_this_level
    EXPECT_EQ(system_.get_uses_this_level(player, skill_type::short_sword), 0);
}

TEST_F(skill_system_test, uses_to_next_level_default_tiers)
{
    // Default tiers: 0-19 → N=10, 20-39 → N=25, 40-59 → N=50,
    //                60-79 → N=75, 80-89 → N=100, 90+ → N=125

    // Level 0: (0+1)*10 = 10
    EXPECT_EQ(system_.uses_to_next_level(skill_type::short_sword, 0), 10);

    // Level 19: (19+1)*10 = 200
    EXPECT_EQ(system_.uses_to_next_level(skill_type::short_sword, 19), 200);

    // Level 20: (20+1)*25 = 525
    EXPECT_EQ(system_.uses_to_next_level(skill_type::short_sword, 20), 525);

    // Level 39: (39+1)*25 = 1000
    EXPECT_EQ(system_.uses_to_next_level(skill_type::short_sword, 39), 1000);

    // Level 40: (40+1)*50 = 2050
    EXPECT_EQ(system_.uses_to_next_level(skill_type::short_sword, 40), 2050);

    // Level 59: (59+1)*50 = 3000
    EXPECT_EQ(system_.uses_to_next_level(skill_type::short_sword, 59), 3000);

    // Level 60: (60+1)*75 = 4575
    EXPECT_EQ(system_.uses_to_next_level(skill_type::short_sword, 60), 4575);

    // Level 79: (79+1)*75 = 6000
    EXPECT_EQ(system_.uses_to_next_level(skill_type::short_sword, 79), 6000);

    // Level 80: (80+1)*100 = 8100
    EXPECT_EQ(system_.uses_to_next_level(skill_type::short_sword, 80), 8100);

    // Level 89: (89+1)*100 = 9000
    EXPECT_EQ(system_.uses_to_next_level(skill_type::short_sword, 89), 9000);

    // Level 90: (90+1)*125 = 11375
    EXPECT_EQ(system_.uses_to_next_level(skill_type::short_sword, 90), 11375);

    // Level 99: (99+1)*125 = 12500
    EXPECT_EQ(system_.uses_to_next_level(skill_type::short_sword, 99), 12500);
}

TEST_F(skill_system_test, record_skill_use_single)
{
    player_id player{1};
    system_.register_player(player);

    auto levels = system_.record_skill_use(player, skill_type::short_sword);
    EXPECT_EQ(levels, 0);
    EXPECT_EQ(system_.get_uses_this_level(player, skill_type::short_sword), 1);
    EXPECT_EQ(system_.get_total_uses(player, skill_type::short_sword), 1);
    EXPECT_EQ(system_.get_skill_level(player, skill_type::short_sword), 0);
}

TEST_F(skill_system_test, record_skill_use_level_up)
{
    player_id player{1};
    system_.register_player(player);

    // Level 0→1 needs 10 uses (default tier: (0+1)*10 = 10)
    for (int i = 0; i < 9; ++i)
    {
        system_.record_skill_use(player, skill_type::mining);
    }
    EXPECT_EQ(system_.get_skill_level(player, skill_type::mining), 0);
    EXPECT_EQ(system_.get_uses_this_level(player, skill_type::mining), 9);

    // 10th use triggers level up
    auto levels = system_.record_skill_use(player, skill_type::mining);
    EXPECT_EQ(levels, 1);
    EXPECT_EQ(system_.get_skill_level(player, skill_type::mining), 1);
    EXPECT_EQ(system_.get_uses_this_level(player, skill_type::mining), 0);
    EXPECT_EQ(system_.get_total_uses(player, skill_type::mining), 10);
}

TEST_F(skill_system_test, add_skill_uses_bulk_level_up)
{
    player_id player{1};
    system_.register_player(player);

    // Level 0→1 = 10, level 1→2 = 20: total 30 for 2 levels
    system_.add_skill_uses(player, skill_type::axe, 30);
    EXPECT_EQ(system_.get_skill_level(player, skill_type::axe), 2);
    EXPECT_EQ(system_.get_total_uses(player, skill_type::axe), 30);
    EXPECT_EQ(system_.get_uses_this_level(player, skill_type::axe), 0);
}

TEST_F(skill_system_test, add_skill_uses_with_remainder)
{
    player_id player{1};
    system_.register_player(player);

    // Level 0→1 = 10, +5 leftover
    system_.add_skill_uses(player, skill_type::fencing, 15);
    EXPECT_EQ(system_.get_skill_level(player, skill_type::fencing), 1);
    EXPECT_EQ(system_.get_uses_this_level(player, skill_type::fencing), 5);
    EXPECT_EQ(system_.get_total_uses(player, skill_type::fencing), 15);
}

TEST_F(skill_system_test, get_mastery)
{
    player_id player{1};
    system_.register_player(player);

    system_.set_skill_level(player, skill_type::alchemy, 100);
    EXPECT_EQ(system_.get_mastery(player, skill_type::alchemy), mastery_level::master);
}

TEST_F(skill_system_test, reset_skill)
{
    player_id player{1};
    system_.register_player(player);

    system_.set_skill_level(player, skill_type::short_sword, 50);
    system_.add_skill_uses(player, skill_type::short_sword, 100);
    system_.reset_skill(player, skill_type::short_sword);

    EXPECT_EQ(system_.get_skill_level(player, skill_type::short_sword), 0);
    EXPECT_EQ(system_.get_total_uses(player, skill_type::short_sword), 0);
    EXPECT_EQ(system_.get_uses_this_level(player, skill_type::short_sword), 0);
}

TEST_F(skill_system_test, reset_all_skills)
{
    player_id player{1};
    system_.register_player(player);

    system_.set_skill_level(player, skill_type::short_sword, 50);
    system_.set_skill_level(player, skill_type::mining, 75);
    system_.reset_all_skills(player);

    EXPECT_EQ(system_.get_skill_level(player, skill_type::short_sword), 0);
    EXPECT_EQ(system_.get_skill_level(player, skill_type::mining), 0);
}

TEST_F(skill_system_test, level_up_callback)
{
    player_id player{1};
    system_.register_player(player);

    bool callback_fired = false;
    system_.on_level_up(
        [&](const skill_level_event& event)
        {
            callback_fired = true;
            EXPECT_EQ(event.player, player);
            EXPECT_EQ(event.skill, skill_type::fishing);
            EXPECT_EQ(event.old_level, 0);
            EXPECT_EQ(event.new_level, 1);
        });

    // Level 0→1 needs 10 uses
    system_.add_skill_uses(player, skill_type::fishing, 10);
    EXPECT_TRUE(callback_fired);
}

TEST_F(skill_system_test, damage_bonus)
{
    player_id player{1};
    system_.register_player(player);

    system_.set_skill_level(player, skill_type::short_sword, 50);
    int16_t bonus = system_.calculate_damage_bonus(player, skill_type::short_sword);
    EXPECT_EQ(bonus, 5); // 50 / 10
}

TEST_F(skill_system_test, hit_bonus)
{
    player_id player{1};
    system_.register_player(player);

    system_.set_skill_level(player, skill_type::axe, 30);
    int16_t bonus = system_.calculate_hit_bonus(player, skill_type::axe);
    EXPECT_EQ(bonus, 6); // 30 / 5
}

TEST_F(skill_system_test, max_level_stops_leveling)
{
    player_id player{1};
    system_.register_player(player);

    // Default max_level is 100
    system_.set_skill_level(player, skill_type::archery, 100);

    auto levels = system_.record_skill_use(player, skill_type::archery);
    EXPECT_EQ(levels, 0);
    EXPECT_EQ(system_.get_skill_level(player, skill_type::archery), 100);
    // total_uses should not increment at max level
    EXPECT_EQ(system_.get_total_uses(player, skill_type::archery), 0);
}

TEST_F(skill_system_test, train_skill_calls_record_skill_use)
{
    player_id player{1};
    system_.register_player(player);

    auto result = system_.train_skill(player, skill_type::staff);
    EXPECT_EQ(result, skill_use_result::success);
    EXPECT_EQ(system_.get_total_uses(player, skill_type::staff), 1);
    EXPECT_EQ(system_.get_uses_this_level(player, skill_type::staff), 1);
}

// YAML loading tests

TEST_F(skill_system_test, load_config_file_not_found_uses_defaults)
{
    system_.load_config("/nonexistent/path.yaml");

    // Should still use defaults
    EXPECT_EQ(system_.uses_to_next_level(skill_type::short_sword, 0), 10);
}

TEST_F(skill_system_test, load_config_custom_tiers)
{
    auto tmp_path = std::filesystem::temp_directory_path() / "test_skills.yaml";
    {
        std::ofstream f(tmp_path);
        f << "defaults:\n"
             "  max_level: 50\n"
             "  tiers:\n"
             "    - { max_level: 10, multiplier: 5 }\n"
             "    - { max_level: 200, multiplier: 20 }\n"
             "skill_tiers:\n"
             "  mining:\n"
             "    max_level: 80\n"
             "    tiers:\n"
             "      - { max_level: 20, multiplier: 8 }\n"
             "      - { max_level: 200, multiplier: 30 }\n";
    }

    system_.load_config(tmp_path.string());

    // Default config updated: level 0 → (0+1)*5 = 5
    EXPECT_EQ(system_.uses_to_next_level(skill_type::short_sword, 0), 5);
    // Default config: level 15 → (15+1)*20 = 320 (>= max_level 10)
    EXPECT_EQ(system_.uses_to_next_level(skill_type::short_sword, 15), 320);

    // Mining has custom tiers: level 0 → (0+1)*8 = 8
    EXPECT_EQ(system_.uses_to_next_level(skill_type::mining, 0), 8);
    // Mining: level 25 → (25+1)*30 = 780 (>= max_level 20)
    EXPECT_EQ(system_.uses_to_next_level(skill_type::mining, 25), 780);

    // Mining respects its custom max_level=80 (test via record_skill_use)
    player_id player{1};
    system_.register_player(player);
    system_.set_skill_level(player, skill_type::mining, 80);
    auto levels = system_.record_skill_use(player, skill_type::mining);
    EXPECT_EQ(levels, 0);

    std::filesystem::remove(tmp_path);
}

TEST_F(skill_system_test, unconfigured_skill_falls_back_to_defaults)
{
    auto tmp_path = std::filesystem::temp_directory_path() / "test_skills2.yaml";
    {
        std::ofstream f(tmp_path);
        f << "defaults:\n"
             "  max_level: 100\n"
             "  tiers:\n"
             "    - { max_level: 200, multiplier: 7 }\n"
             "skill_tiers:\n"
             "  magic:\n"
             "    tiers:\n"
             "      - { max_level: 200, multiplier: 15 }\n";
    }

    system_.load_config(tmp_path.string());

    // Magic has custom multiplier 15
    EXPECT_EQ(system_.uses_to_next_level(skill_type::magic, 0), 15);

    // Short_sword falls back to default multiplier 7
    EXPECT_EQ(system_.uses_to_next_level(skill_type::short_sword, 0), 7);

    std::filesystem::remove(tmp_path);
}
