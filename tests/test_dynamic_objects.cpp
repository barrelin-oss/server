// test_dynamic_objects.cpp
// Unit tests for the dynamic object (ground field) system

#include <gtest/gtest.h>

#include <chrono>
#include <thread>

#include "core/subsystem.h"
#include "core/types.h"
#include "core/enums.h"
#include "world/world_subsystem.h"
#include "world/dynamic_object_system.h"
#include "world/map.h"
#include "player/player_system.h"
#include "entity/entity_manager.h"
#include "combat/combat_system.h"
#include "effect/effect_system.h"

using hb::dynamic_object_type;
using hb::map_id;
using hb::player_id;

class dynamic_object_test : public ::testing::Test
{
protected:
    void SetUp() override
    {
        hb::subsystems().create_subsystem<hb::player::player_system>();
        hb::subsystems().create_subsystem<hb::world::world_subsystem>();
        hb::subsystems().create_subsystem<hb::world::dynamic_object_system>();
        hb::subsystems().create_subsystem<hb::entity::entity_manager>();
        hb::subsystems().create_subsystem<hb::combat::combat_system>();
        hb::subsystems().create_subsystem<hb::effect::effect_system>();
        hb::subsystems().initialize_all();

        player_sys_ = hb::subsystems().get<hb::player::player_system>();
        world_ = hb::subsystems().get<hb::world::world_subsystem>();
        dos_ = hb::subsystems().get<hb::world::dynamic_object_system>();

        hb::world::map_config cfg;
        cfg.name = "test_map";
        cfg.width = 100;
        cfg.height = 100;
        auto result = world_->create_map(cfg);
        ASSERT_TRUE(result.is_ok());
        map_id_ = result.value();
    }

    void TearDown() override { hb::subsystems().clear_all(); }

    auto create_player_at(hb::world::position pos) -> player_id
    {
        hb::player::player_create_info info;
        info.name = "player_" + std::to_string(next_name_++);
        auto result = player_sys_->create_player(info);
        auto pid = result.value();
        player_sys_->set_position(pid, map_id_, pos, hb::world::direction::south);
        return pid;
    }

    hb::player::player_system* player_sys_{};
    hb::world::world_subsystem* world_{};
    hb::world::dynamic_object_system* dos_{};
    map_id map_id_{};
    int next_name_{1};
};

TEST_F(dynamic_object_test, spawn_registers_object_and_tile)
{
    auto id = dos_->spawn(hb::entity::entity{}, dynamic_object_type::fire, map_id_, {50, 50}, 30000, 0);
    ASSERT_NE(id, 0);

    const auto* obj = dos_->get(id);
    ASSERT_NE(obj, nullptr);
    EXPECT_EQ(obj->type, dynamic_object_type::fire);
    EXPECT_EQ(obj->pos.x, 50);
    EXPECT_EQ(obj->pos.y, 50);

    auto* m = world_->get_map(map_id_);
    ASSERT_NE(m, nullptr);
    const auto* tile = m->get_dynamic_tile(hb::world::position{50, 50});
    ASSERT_NE(tile, nullptr);
    EXPECT_EQ(tile->dynamic_object_id, id);
    EXPECT_EQ(tile->dynamic_object_type, static_cast<uint16_t>(dynamic_object_type::fire));
}

TEST_F(dynamic_object_test, spawn_fails_on_occupied_tile)
{
    auto first = dos_->spawn(hb::entity::entity{}, dynamic_object_type::fire, map_id_, {40, 40}, 30000, 0);
    ASSERT_NE(first, 0);

    auto second = dos_->spawn(hb::entity::entity{}, dynamic_object_type::icestorm, map_id_, {40, 40}, 30000, 0);
    EXPECT_EQ(second, 0);
}

TEST_F(dynamic_object_test, remove_clears_tile)
{
    auto id = dos_->spawn(hb::entity::entity{}, dynamic_object_type::spike, map_id_, {30, 30}, 30000, 0);
    ASSERT_NE(id, 0);

    dos_->remove(id);
    EXPECT_EQ(dos_->get(id), nullptr);

    auto* m = world_->get_map(map_id_);
    const auto* tile = m->get_dynamic_tile(hb::world::position{30, 30});
    ASSERT_NE(tile, nullptr);
    EXPECT_EQ(tile->dynamic_object_id, 0);
    EXPECT_EQ(tile->dynamic_object_type, 0);
}

TEST_F(dynamic_object_test, spike_field_fills_area_others_single)
{
    // Spike-Field data: type 9, rx 2, ry 2 -> 5x5 = 25 spikes
    auto spikes =
        dos_->spawn_field(hb::entity::entity{}, dynamic_object_type::spike, map_id_, {50, 50}, 2, 2, 30000, 0);
    EXPECT_EQ(spikes, 25);
    EXPECT_EQ(dos_->object_count(), 25u);

    // Ice-Storm data: single object regardless of radius params
    auto storms =
        dos_->spawn_field(hb::entity::entity{}, dynamic_object_type::icestorm, map_id_, {70, 70}, 1, 0, 30000, 0);
    EXPECT_EQ(storms, 1);
}

TEST_F(dynamic_object_test, expired_objects_are_removed)
{
    auto id = dos_->spawn(hb::entity::entity{}, dynamic_object_type::fire, map_id_, {20, 20}, 50, 0);
    ASSERT_NE(id, 0);

    std::this_thread::sleep_for(std::chrono::milliseconds(80));
    dos_->update(1.5f); // One full tick: expiry runs

    EXPECT_EQ(dos_->get(id), nullptr);
    EXPECT_EQ(dos_->object_count(), 0u);
}

TEST_F(dynamic_object_test, field_tick_damages_player_in_area)
{
    auto pid = create_player_at({51, 50});
    auto* p = player_sys_->get_player(pid);
    ASSERT_NE(p, nullptr);
    p->hp = 100;
    p->computed.max_hp = 100;

    // Fire at (50,50): 3x3 area covers the player at (51,50)
    auto id = dos_->spawn(hb::entity::entity{}, dynamic_object_type::fire, map_id_, {50, 50}, 30000, 0);
    ASSERT_NE(id, 0);

    dos_->update(1.0f); // One effect tick

    EXPECT_LT(p->hp, 100); // 1d6 fire damage applied
}

TEST_F(dynamic_object_test, spike_triggers_on_step)
{
    auto pid = create_player_at({60, 60});
    auto* p = player_sys_->get_player(pid);
    ASSERT_NE(p, nullptr);
    p->hp = 100;
    p->computed.max_hp = 100;

    auto id = dos_->spawn(hb::entity::entity{}, dynamic_object_type::spike, map_id_, {61, 60}, 30000, 0);
    ASSERT_NE(id, 0);

    // Simulate the step onto the spike tile (targets use the ECS entity, not the player id)
    player_sys_->set_position(pid, map_id_, {61, 60}, hb::world::direction::east);
    dos_->on_entity_step(p->ecs_entity, map_id_, {61, 60});

    EXPECT_LT(p->hp, 100); // 2d4 damage applied
    EXPECT_GE(p->hp, 92);
}

TEST_F(dynamic_object_test, spawn_and_remove_callbacks_fire)
{
    int spawns = 0;
    int removes = 0;
    dos_->set_on_spawn_callback([&](const hb::world::dynamic_object&) { ++spawns; });
    dos_->set_on_remove_callback([&](const hb::world::dynamic_object&) { ++removes; });

    auto id = dos_->spawn(hb::entity::entity{}, dynamic_object_type::fire, map_id_, {10, 10}, 30000, 0);
    ASSERT_NE(id, 0);
    EXPECT_EQ(spawns, 1);

    dos_->remove(id);
    EXPECT_EQ(removes, 1);
}
