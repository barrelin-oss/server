// test_mining.cpp
// Unit tests for mining system

#include "crafting/mining_system.h"
#include "crafting/mining_config.h"

#include <gtest/gtest.h>

namespace hb::crafting
{

// === Success chance calculation tests ===

TEST(mining_test, success_chance_basic)
{
    // skill 50, difficulty 10 => effective 40
    EXPECT_EQ(mining_system::calculate_success_chance(50, 10), 40);
}

TEST(mining_test, success_chance_high_skill)
{
    // skill 100, difficulty 10 => effective 90
    EXPECT_EQ(mining_system::calculate_success_chance(100, 10), 90);
}

TEST(mining_test, success_chance_equal)
{
    // skill == difficulty => effective 0 => floor to 1
    EXPECT_EQ(mining_system::calculate_success_chance(10, 10), 1);
}

TEST(mining_test, success_chance_low_skill)
{
    // skill < difficulty => negative => floor to 1
    EXPECT_EQ(mining_system::calculate_success_chance(5, 50), 1);
}

TEST(mining_test, success_chance_zero_skill)
{
    // skill 0, difficulty 10 => floor to 1
    EXPECT_EQ(mining_system::calculate_success_chance(0, 10), 1);
}

TEST(mining_test, success_chance_zero_difficulty)
{
    // skill 50, difficulty 0 => effective 50
    EXPECT_EQ(mining_system::calculate_success_chance(50, 0), 50);
}

// === Mine result defaults ===

TEST(mining_test, mine_result_defaults)
{
    mine_result result;
    EXPECT_FALSE(result.success);
    EXPECT_EQ(result.reason, skill::skill_use_result::success);
    EXPECT_TRUE(result.item_name.empty());
    EXPECT_EQ(result.template_id, 0);
    EXPECT_EQ(result.count, 1);
    EXPECT_FALSE(result.node_depleted);
}

// === Mineral type config defaults ===

TEST(mining_test, mineral_type_config_defaults)
{
    mineral_type_config config;
    EXPECT_EQ(config.type_id, 0);
    EXPECT_TRUE(config.name.empty());
    EXPECT_EQ(config.difficulty, 0);
    EXPECT_EQ(config.max_hits, 0);
    EXPECT_EQ(config.visual_type, 0);
    EXPECT_TRUE(config.drops.empty());
}

// === Mineral drop defaults ===

TEST(mining_test, mineral_drop_defaults)
{
    mineral_drop drop;
    EXPECT_TRUE(drop.item_name.empty());
    EXPECT_EQ(drop.template_id, 0);
    EXPECT_EQ(drop.min_skill, 0);
    EXPECT_EQ(drop.weight, 100);
}

// === Mineral node defaults ===

TEST(mining_test, mineral_node_defaults)
{
    mineral_node node;
    EXPECT_EQ(node.node_id, 0);
    EXPECT_EQ(node.type_id, 0);
    EXPECT_TRUE(node.map_name.empty());
    EXPECT_EQ(node.x, 0);
    EXPECT_EQ(node.y, 0);
    EXPECT_EQ(node.hits_remaining, 0);
}

// === Pickaxe template ID constant ===

TEST(mining_test, pickaxe_template_id)
{
    EXPECT_EQ(pickaxe_template_id, 231);
}

// === Node query on empty system ===

TEST(mining_test, get_node_at_empty_system)
{
    mining_system sys;
    sys.initialize();
    EXPECT_EQ(sys.get_node_at("test_map", 10, 20), nullptr);
    EXPECT_EQ(sys.node_count(), 0u);
    sys.shutdown();
}

} // namespace hb::crafting
