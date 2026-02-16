// test_fishing.cpp
// Unit tests for fishing system

#include "crafting/fishing_system.h"
#include "crafting/fishing_config.h"
#include "core/subsystem.h"
#include "core/types.h"
#include "player/player_system.h"
#include "player/player.h"
#include "skill/skill_system.h"
#include "inventory/inventory_system.h"
#include "item/item_system.h"
#include "world/world_subsystem.h"
#include "entity/entity_manager.h"

#include <gtest/gtest.h>

namespace hb::crafting
{

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
    EXPECT_EQ(sys.get_node(0), nullptr);   // Index 0 reserved
    EXPECT_EQ(sys.get_node(999), nullptr); // Beyond max
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

// ============================================================================
// Fishing flow tests — requires friend access to fish_nodes_
// ============================================================================

class fishing_flow_test : public ::testing::Test
{
protected:
    void SetUp() override
    {
        hb::subsystems().create_subsystem<hb::player::player_system>();
        hb::subsystems().create_subsystem<hb::skill::skill_system>();
        hb::subsystems().create_subsystem<hb::inventory::inventory_system>();
        hb::subsystems().create_subsystem<hb::item::item_system>();
        hb::subsystems().create_subsystem<hb::world::world_subsystem>();
        hb::subsystems().create_subsystem<hb::entity::entity_manager>();
        hb::subsystems().initialize_all();

        player_sys_ = hb::subsystems().get<hb::player::player_system>();
        skill_sys_ = hb::subsystems().get<hb::skill::skill_system>();
        inventory_sys_ = hb::subsystems().get<hb::inventory::inventory_system>();
        item_sys_ = hb::subsystems().get<hb::item::item_system>();
        world_sys_ = hb::subsystems().get<hb::world::world_subsystem>();

        fishing_.initialize();
        fishing_.set_dependencies(player_sys_, skill_sys_, inventory_sys_, item_sys_,
                                  nullptr, nullptr, world_sys_);

        // Create a test map
        hb::world::map_config mcfg;
        mcfg.name = "test_map";
        mcfg.width = 100;
        mcfg.height = 100;
        auto map_result = world_sys_->create_map(mcfg);
        ASSERT_TRUE(map_result.is_ok());
        map_id_ = map_result.value();
    }

    void TearDown() override
    {
        fishing_.shutdown();
        hb::subsystems().clear_all();
    }

    auto create_player_at(int16_t x, int16_t y) -> hb::player_id
    {
        hb::player::player_create_info info;
        info.name = "fisher_" + std::to_string(next_player_++);
        auto result = player_sys_->create_player(info);
        auto pid = result.value();
        player_sys_->set_position(pid, map_id_, hb::world::position{x, y},
                                  hb::world::direction::south);
        return pid;
    }

    void inject_fish(uint32_t index, const std::string& map_name, int16_t x, int16_t y,
                     const fish_type_config* cfg = nullptr)
    {
        fishing_.fish_nodes_[index] = fish_node{
            .index = index,
            .type_id = cfg ? cfg->type_id : 1,
            .map_name = map_name,
            .x = x,
            .y = y,
            .config = cfg,
            .engaging_count = 0,
            .spawn_time = std::chrono::steady_clock::now(),
            .lifespan = duration_ms{600000},
        };
    }

    void set_fish_engaging_count(uint32_t index, int32_t count)
    {
        if (fishing_.fish_nodes_[index])
        {
            fishing_.fish_nodes_[index]->engaging_count = count;
        }
    }

    fishing_system fishing_;
    hb::player::player_system* player_sys_{};
    hb::skill::skill_system* skill_sys_{};
    hb::inventory::inventory_system* inventory_sys_{};
    hb::item::item_system* item_sys_{};
    hb::world::world_subsystem* world_sys_{};
    hb::map_id map_id_{};
    fish_type_config test_config_{.type_id = 1, .name = "Test Fish", .item_name = "Raw Fish",
                                  .template_id = 200, .difficulty = 10};
    int next_player_{1};
};

// --- check_fish_nearby ---

TEST_F(fishing_flow_test, finds_adjacent_fish)
{
    auto pid = create_player_at(11, 10);
    inject_fish(1, "test_map", 10, 10, &test_config_);

    auto result = fishing_.check_fish_nearby(entity_id(pid.value), "test_map", 11, 10);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(*result, 1u);
}

TEST_F(fishing_flow_test, no_fish_returns_nullopt)
{
    auto pid = create_player_at(10, 10);
    // No fish injected
    auto result = fishing_.check_fish_nearby(entity_id(pid.value), "test_map", 10, 10);
    EXPECT_FALSE(result.has_value());
}

TEST_F(fishing_flow_test, fish_too_far_returns_nullopt)
{
    auto pid = create_player_at(15, 15);
    inject_fish(1, "test_map", 10, 10, &test_config_);

    auto result = fishing_.check_fish_nearby(entity_id(pid.value), "test_map", 15, 15);
    EXPECT_FALSE(result.has_value());
}

TEST_F(fishing_flow_test, wrong_map_returns_nullopt)
{
    auto pid = create_player_at(10, 10);
    inject_fish(1, "other_map", 10, 10, &test_config_);

    auto result = fishing_.check_fish_nearby(entity_id(pid.value), "test_map", 10, 10);
    EXPECT_FALSE(result.has_value());
}

TEST_F(fishing_flow_test, already_fishing_returns_nullopt)
{
    auto pid = create_player_at(10, 10);
    inject_fish(1, "test_map", 10, 10, &test_config_);

    // Manually set the player as already engaged
    auto* plr = player_sys_->get_player(pid);
    ASSERT_NE(plr, nullptr);
    plr->fishing.fish_node_index = 5;

    auto result = fishing_.check_fish_nearby(entity_id(pid.value), "test_map", 10, 10);
    EXPECT_FALSE(result.has_value());
}

TEST_F(fishing_flow_test, sets_engagement_state)
{
    auto pid = create_player_at(11, 10);
    inject_fish(1, "test_map", 10, 10, &test_config_);

    auto result = fishing_.check_fish_nearby(entity_id(pid.value), "test_map", 11, 10);
    ASSERT_TRUE(result.has_value());

    auto* plr = player_sys_->get_player(pid);
    EXPECT_EQ(plr->fishing.fish_node_index, 1u);
    EXPECT_EQ(plr->fishing.catch_chance, 1);

    auto* node = fishing_.get_node(1);
    ASSERT_NE(node, nullptr);
    EXPECT_EQ(node->engaging_count, 1);
}

TEST_F(fishing_flow_test, max_engagement_returns_nullopt)
{
    auto pid = create_player_at(10, 10);
    inject_fish(1, "test_map", 10, 10, &test_config_);

    // Saturate engagement count
    set_fish_engaging_count(1, max_engaging_fish);

    auto result = fishing_.check_fish_nearby(entity_id(pid.value), "test_map", 10, 10);
    EXPECT_FALSE(result.has_value());
}

// --- attempt_catch ---

TEST_F(fishing_flow_test, attempt_catch_not_fishing_returns_no_fish)
{
    auto pid = create_player_at(10, 10);
    // Player not engaged — fish_node_index == 0
    auto result = fishing_.attempt_catch(entity_id(pid.value));
    EXPECT_EQ(result.result, catch_result::no_fish);
}

TEST_F(fishing_flow_test, attempt_catch_node_gone_returns_stolen)
{
    auto pid = create_player_at(10, 10);
    auto* plr = player_sys_->get_player(pid);
    // Point player at an empty slot
    plr->fishing.fish_node_index = 5;

    auto result = fishing_.attempt_catch(entity_id(pid.value));
    EXPECT_EQ(result.result, catch_result::canceled_stolen);
    EXPECT_EQ(plr->fishing.fish_node_index, 0u);
}

TEST_F(fishing_flow_test, attempt_catch_without_rod_returns_no_rod)
{
    auto pid = create_player_at(11, 10);
    inject_fish(1, "test_map", 10, 10, &test_config_);

    // Engage the player
    auto check = fishing_.check_fish_nearby(entity_id(pid.value), "test_map", 11, 10);
    ASSERT_TRUE(check.has_value());

    // Player has no fishing rod in inventory → no_rod
    auto result = fishing_.attempt_catch(entity_id(pid.value));
    EXPECT_EQ(result.result, catch_result::no_rod);

    // Fish should still exist but engagement decremented
    auto* node = fishing_.get_node(1);
    ASSERT_NE(node, nullptr);
    EXPECT_EQ(node->engaging_count, 0);
}

// --- cancel_fishing ---

TEST_F(fishing_flow_test, cancel_disengages_player)
{
    auto pid = create_player_at(10, 10);
    inject_fish(1, "test_map", 10, 10, &test_config_);

    auto check = fishing_.check_fish_nearby(entity_id(pid.value), "test_map", 10, 10);
    ASSERT_TRUE(check.has_value());

    auto* plr = player_sys_->get_player(pid);
    EXPECT_EQ(plr->fishing.fish_node_index, 1u);
    EXPECT_EQ(fishing_.get_node(1)->engaging_count, 1);

    fishing_.cancel_fishing(entity_id(pid.value), catch_result::canceled_moved);

    EXPECT_EQ(plr->fishing.fish_node_index, 0u);
    EXPECT_EQ(fishing_.get_node(1)->engaging_count, 0);
}

TEST_F(fishing_flow_test, cancel_not_fishing_is_noop)
{
    auto pid = create_player_at(10, 10);
    // Not fishing — should not crash
    fishing_.cancel_fishing(entity_id(pid.value), catch_result::canceled_moved);

    auto* plr = player_sys_->get_player(pid);
    EXPECT_EQ(plr->fishing.fish_node_index, 0u);
}

TEST_F(fishing_flow_test, cancel_fires_callback_with_reason)
{
    auto pid = create_player_at(10, 10);
    inject_fish(1, "test_map", 10, 10, &test_config_);

    auto check = fishing_.check_fish_nearby(entity_id(pid.value), "test_map", 10, 10);
    ASSERT_TRUE(check.has_value());

    bool callback_fired = false;
    entity_id callback_eid{};
    catch_result callback_reason{};

    fishing_.set_catch_complete_callback(
        [&](entity_id eid, const fish_catch_result& r)
        {
            callback_fired = true;
            callback_eid = eid;
            callback_reason = r.result;
        });

    fishing_.cancel_fishing(entity_id(pid.value), catch_result::canceled_moved);

    EXPECT_TRUE(callback_fired);
    EXPECT_EQ(callback_eid.value, pid.value);
    EXPECT_EQ(callback_reason, catch_result::canceled_moved);
}

} // namespace hb::crafting
