// test_fishing.cpp
// Unit tests for fishing system

#include "crafting/fishing_system.h"
#include "crafting/fishing_config.h"

#include <gtest/gtest.h>

namespace hb::crafting {

// === Config defaults ===

TEST(fishing_test, fish_type_config_defaults)
{
    fish_type_config config;
    EXPECT_EQ(config.type_id, 0);
    EXPECT_TRUE(config.name.empty());
    EXPECT_TRUE(config.item_name.empty());
    EXPECT_EQ(config.template_id, 0);
    EXPECT_EQ(config.difficulty, 0);
    EXPECT_EQ(config.lifespan_min, 20);
    EXPECT_EQ(config.visual_type, 2);
    EXPECT_EQ(config.weight, 10);
}

TEST(fishing_test, fishing_state_defaults)
{
    fishing_state state;
    EXPECT_EQ(state.fish_node_index, 0u);
    EXPECT_EQ(state.catch_chance, 1);
}

TEST(fishing_test, fish_catch_result_defaults)
{
    fish_catch_result result;
    EXPECT_EQ(result.result, catch_result::success);
    EXPECT_TRUE(result.item_name.empty());
    EXPECT_EQ(result.template_id, 0);
    EXPECT_EQ(result.exp_gained, 0);
    EXPECT_EQ(result.levels_gained, 0);
}

TEST(fishing_test, fishing_rod_template_id)
{
    EXPECT_EQ(fishing_rod_template_id, 105);
}

TEST(fishing_test, max_engaging_fish_constant)
{
    EXPECT_EQ(max_engaging_fish, 30);
}

TEST(fishing_test, max_fish_nodes_constant)
{
    EXPECT_EQ(max_fish_nodes, 200u);
}

// === Fish node defaults ===

TEST(fishing_test, fish_node_defaults)
{
    fish_node node;
    EXPECT_EQ(node.index, 0u);
    EXPECT_EQ(node.type_id, 0);
    EXPECT_TRUE(node.map_name.empty());
    EXPECT_EQ(node.x, 0);
    EXPECT_EQ(node.y, 0);
    EXPECT_EQ(node.config, nullptr);
    EXPECT_EQ(node.engaging_count, 0);
}

// === System initialization ===

TEST(fishing_test, system_initialize_and_shutdown)
{
    fishing_system sys;
    sys.initialize();
    EXPECT_EQ(sys.active_node_count(), 0u);
    EXPECT_EQ(sys.get_node(1), nullptr);
    sys.shutdown();
}

TEST(fishing_test, get_node_out_of_range)
{
    fishing_system sys;
    sys.initialize();
    EXPECT_EQ(sys.get_node(0), nullptr);    // Index 0 reserved
    EXPECT_EQ(sys.get_node(999), nullptr);  // Beyond max
    sys.shutdown();
}

// === Chance change calculation ===

TEST(fishing_test, chance_change_basic)
{
    // skill 50, difficulty 10 => effective 40, max_change 4
    auto [effective, max_change] = fishing_system::calculate_chance_change(50, 10);
    EXPECT_EQ(effective, 40);
    EXPECT_EQ(max_change, 4);
}

TEST(fishing_test, chance_change_high_skill)
{
    // skill 100, difficulty 10 => effective 90, max_change 9
    auto [effective, max_change] = fishing_system::calculate_chance_change(100, 10);
    EXPECT_EQ(effective, 90);
    EXPECT_EQ(max_change, 9);
}

TEST(fishing_test, chance_change_equal)
{
    // skill == difficulty => effective 0 => floor to 1, max_change 1
    auto [effective, max_change] = fishing_system::calculate_chance_change(10, 10);
    EXPECT_EQ(effective, 1);
    EXPECT_EQ(max_change, 1);
}

TEST(fishing_test, chance_change_low_skill)
{
    // skill < difficulty => negative => floor to 1, max_change 1
    auto [effective, max_change] = fishing_system::calculate_chance_change(5, 50);
    EXPECT_EQ(effective, 1);
    EXPECT_EQ(max_change, 1);
}

TEST(fishing_test, chance_change_zero_skill)
{
    // skill 0, difficulty 10 => floor to 1, max_change 1
    auto [effective, max_change] = fishing_system::calculate_chance_change(0, 10);
    EXPECT_EQ(effective, 1);
    EXPECT_EQ(max_change, 1);
}

TEST(fishing_test, chance_change_zero_difficulty)
{
    // skill 50, difficulty 0 => effective 50, max_change 5
    auto [effective, max_change] = fishing_system::calculate_chance_change(50, 0);
    EXPECT_EQ(effective, 50);
    EXPECT_EQ(max_change, 5);
}

TEST(fishing_test, chance_change_very_high_skill)
{
    // skill 200, difficulty 10 => effective 190, max_change 19
    auto [effective, max_change] = fishing_system::calculate_chance_change(200, 10);
    EXPECT_EQ(effective, 190);
    EXPECT_EQ(max_change, 19);
}

// === Catch result enum coverage ===

TEST(fishing_test, catch_result_values)
{
    EXPECT_EQ(static_cast<uint8_t>(catch_result::success), 0);
    EXPECT_EQ(static_cast<uint8_t>(catch_result::failure), 1);
    EXPECT_EQ(static_cast<uint8_t>(catch_result::canceled_moved), 2);
    EXPECT_EQ(static_cast<uint8_t>(catch_result::canceled_stolen), 3);
    EXPECT_EQ(static_cast<uint8_t>(catch_result::canceled_timeout), 4);
    EXPECT_EQ(static_cast<uint8_t>(catch_result::no_fish), 5);
    EXPECT_EQ(static_cast<uint8_t>(catch_result::no_rod), 6);
    EXPECT_EQ(static_cast<uint8_t>(catch_result::rod_broken), 7);
}

}  // namespace hb::crafting
