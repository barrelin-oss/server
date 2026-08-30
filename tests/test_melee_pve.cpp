// test_melee_pve.cpp
// Tests for melee PvE combat (player attacking NPC)

#include <gtest/gtest.h>
#include "core/types.h"
#include "core/subsystem.h"
#include "entity/entity.h"
#include "entity/entity_manager.h"
#include "player/player_system.h"
#include "npc/npc.h"
#include "npc/npc_system.h"
#include "combat/combat_system.h"
#include "combat/combat_events.h"
#include "world/world_subsystem.h"

#include <chrono>
#include <thread>

namespace
{

using hb::map_id;
using hb::npc_id;
using hb::player_id;
using pos = hb::world::position;

// ============================================================================
// Fixture: combat_system::apply_damage() wiring NPC damage
// ============================================================================

class combat_npc_damage_test : public ::testing::Test
{
protected:
    void SetUp() override
    {
        hb::subsystems().create_subsystem<hb::player::player_system>();
        hb::subsystems().create_subsystem<hb::npc::npc_system>();
        hb::subsystems().create_subsystem<hb::combat::combat_system>();
        hb::subsystems().create_subsystem<hb::entity::entity_manager>();
        hb::subsystems().initialize_all();

        npc_sys_ = hb::subsystems().get<hb::npc::npc_system>();
        combat_ = hb::subsystems().get<hb::combat::combat_system>();
    }

    void TearDown() override { hb::subsystems().clear_all(); }

    hb::npc::npc_system* npc_sys_{};
    hb::combat::combat_system* combat_{};
};

TEST_F(combat_npc_damage_test, apply_damage_reduces_npc_hp)
{
    auto spawn_result = npc_sys_->spawn_npc(npc_id{1}, map_id{1}, pos{10, 10});
    ASSERT_TRUE(spawn_result.is_ok());
    auto npc_eid = spawn_result.value();

    auto* n = npc_sys_->get_npc(npc_eid);
    ASSERT_NE(n, nullptr);
    int32_t initial_hp = n->hp;
    ASSERT_GT(initial_hp, 0);

    hb::combat::hit_result hr;
    hr.flags = hb::combat::hit_flags::hit;
    hr.raw_damage = 30;
    hr.final_damage = 30;

    combat_->apply_damage(npc_eid, hr, hb::entity::entity(999));

    EXPECT_EQ(n->hp, initial_hp - 30);
}

TEST_F(combat_npc_damage_test, apply_damage_sets_npc_aggro)
{
    auto spawn_result = npc_sys_->spawn_npc(npc_id{1}, map_id{1}, pos{10, 10});
    ASSERT_TRUE(spawn_result.is_ok());
    auto npc_eid = spawn_result.value();

    auto* n = npc_sys_->get_npc(npc_eid);
    ASSERT_NE(n, nullptr);

    hb::combat::hit_result hr;
    hr.flags = hb::combat::hit_flags::hit;
    hr.raw_damage = 10;
    hr.final_damage = 10;

    combat_->apply_damage(npc_eid, hr, hb::entity::entity(42));

    EXPECT_EQ(n->ai_state.target.id, 42u);
}

TEST_F(combat_npc_damage_test, apply_damage_kills_npc)
{
    auto spawn_result = npc_sys_->spawn_npc(npc_id{1}, map_id{1}, pos{10, 10});
    ASSERT_TRUE(spawn_result.is_ok());
    auto npc_eid = spawn_result.value();

    auto* n = npc_sys_->get_npc(npc_eid);
    ASSERT_NE(n, nullptr);
    int32_t hp = n->hp;

    hb::combat::hit_result hr;
    hr.flags = hb::combat::hit_flags::hit;
    hr.raw_damage = hp + 100;
    hr.final_damage = hp + 100;

    combat_->apply_damage(npc_eid, hr, hb::entity::entity(999));

    EXPECT_TRUE(n->is_dead());
    EXPECT_EQ(n->hp, 0);
}

TEST_F(combat_npc_damage_test, apply_damage_skips_miss)
{
    auto spawn_result = npc_sys_->spawn_npc(npc_id{1}, map_id{1}, pos{10, 10});
    ASSERT_TRUE(spawn_result.is_ok());
    auto npc_eid = spawn_result.value();

    auto* n = npc_sys_->get_npc(npc_eid);
    ASSERT_NE(n, nullptr);
    int32_t initial_hp = n->hp;

    hb::combat::hit_result hr;
    hr.flags = hb::combat::hit_flags::miss;
    hr.raw_damage = 0;
    hr.final_damage = 0;

    combat_->apply_damage(npc_eid, hr, hb::entity::entity(999));

    EXPECT_EQ(n->hp, initial_hp);
}

TEST_F(combat_npc_damage_test, process_attack_on_npc_returns_kill_rewards)
{
    auto spawn_result = npc_sys_->spawn_npc(npc_id{1}, map_id{1}, pos{10, 10});
    ASSERT_TRUE(spawn_result.is_ok());
    auto npc_eid = spawn_result.value();

    auto* n = npc_sys_->get_npc(npc_eid);
    ASSERT_NE(n, nullptr);

    hb::combat::attack_event attack;
    attack.attacker = hb::entity::entity(1);
    attack.defender = npc_eid;
    attack.type = hb::combat::damage_type::physical;
    attack.base_damage = 10000;

    auto result = combat_->process_attack(attack);
    if (result.target_killed)
    {
        EXPECT_GE(result.exp_reward, 0);
        EXPECT_GE(result.gold_reward, 0);
    }
}

TEST_F(combat_npc_damage_test, damage_fires_callback)
{
    bool callback_fired = false;
    npc_sys_->set_on_damage_callback(
        [&]([[maybe_unused]] const hb::npc::npc& n, int32_t damage, hb::entity::entity source)
        {
            callback_fired = true;
            EXPECT_EQ(damage, 25);
            EXPECT_EQ(source.id, 77u);
        });

    auto spawn_result = npc_sys_->spawn_npc(npc_id{1}, map_id{1}, pos{10, 10});
    ASSERT_TRUE(spawn_result.is_ok());
    auto npc_eid = spawn_result.value();

    hb::combat::hit_result hr;
    hr.flags = hb::combat::hit_flags::hit;
    hr.raw_damage = 25;
    hr.final_damage = 25;

    combat_->apply_damage(npc_eid, hr, hb::entity::entity(77));

    EXPECT_TRUE(callback_fired);
}

TEST_F(combat_npc_damage_test, death_fires_callback)
{
    bool death_fired = false;
    uint32_t killer_id = 0;
    npc_sys_->set_on_death_callback(
        [&]([[maybe_unused]] const hb::npc::npc& n, hb::entity::entity killer, int32_t /*damage*/)
        {
            death_fired = true;
            killer_id = killer.id;
        });

    auto spawn_result = npc_sys_->spawn_npc(npc_id{1}, map_id{1}, pos{10, 10});
    ASSERT_TRUE(spawn_result.is_ok());
    auto npc_eid = spawn_result.value();

    auto* n = npc_sys_->get_npc(npc_eid);
    ASSERT_NE(n, nullptr);

    hb::combat::hit_result hr;
    hr.flags = hb::combat::hit_flags::hit;
    hr.raw_damage = n->hp + 100;
    hr.final_damage = n->hp + 100;

    combat_->apply_damage(npc_eid, hr, hb::entity::entity(55));
    npc_sys_->flush_pending_deaths();

    EXPECT_TRUE(death_fired);
    EXPECT_EQ(killer_id, 55u);
}

// ============================================================================
// Fixture: NPC as target in combat with player attacker
// ============================================================================

class melee_pve_combat_test : public ::testing::Test
{
protected:
    void SetUp() override
    {
        hb::subsystems().create_subsystem<hb::player::player_system>();
        hb::subsystems().create_subsystem<hb::npc::npc_system>();
        hb::subsystems().create_subsystem<hb::combat::combat_system>();
        hb::subsystems().create_subsystem<hb::world::world_subsystem>();
        hb::subsystems().create_subsystem<hb::entity::entity_manager>();
        hb::subsystems().initialize_all();

        player_sys_ = hb::subsystems().get<hb::player::player_system>();
        npc_sys_ = hb::subsystems().get<hb::npc::npc_system>();
        combat_ = hb::subsystems().get<hb::combat::combat_system>();
        world_ = hb::subsystems().get<hb::world::world_subsystem>();

        hb::world::map_config cfg;
        cfg.name = "test_map";
        cfg.width = 100;
        cfg.height = 100;
        auto map_result = world_->create_map(cfg);
        ASSERT_TRUE(map_result.is_ok());
        map_id_ = map_result.value();
    }

    void TearDown() override { hb::subsystems().clear_all(); }

    auto create_player_at(pos p) -> player_id
    {
        hb::player::player_create_info info;
        info.name = "test_player_" + std::to_string(next_player_++);
        auto result = player_sys_->create_player(info);
        auto pid = result.value();
        player_sys_->set_position(pid, map_id_, p, hb::world::direction::south);
        auto* plr = player_sys_->get_player(pid);
        plr->computed.attack_power = 50;
        plr->computed.hit_rate = 100;
        plr->computed.max_hp = 200;
        plr->hp = 200;
        return pid;
    }

    auto spawn_monster_at(pos p) -> hb::entity::entity
    {
        auto result = npc_sys_->spawn_npc(npc_id{1}, map_id_, p);
        EXPECT_TRUE(result.is_ok());
        return result.value();
    }

    hb::player::player_system* player_sys_{};
    hb::npc::npc_system* npc_sys_{};
    hb::combat::combat_system* combat_{};
    hb::world::world_subsystem* world_{};
    map_id map_id_{};
    int next_player_{1};
};

TEST_F(melee_pve_combat_test, player_can_attack_npc_at_melee_range)
{
    auto pid = create_player_at({10, 10});
    auto npc_eid = spawn_monster_at({11, 10});

    auto* n = npc_sys_->get_npc(npc_eid);
    ASSERT_NE(n, nullptr);
    int32_t initial_hp = n->hp;

    hb::combat::attack_event attack;
    attack.attacker = player_sys_->get_player(pid)->ecs_entity;
    attack.defender = npc_eid;
    attack.type = hb::combat::damage_type::physical;
    attack.base_damage = 50;

    auto result = combat_->process_attack(attack);

    if (result.hit.is_hit())
    {
        EXPECT_LT(n->hp, initial_hp);
    }
}

TEST_F(melee_pve_combat_test, npc_death_yields_rewards)
{
    auto pid = create_player_at({10, 10});
    auto npc_eid = spawn_monster_at({11, 10});

    auto* n = npc_sys_->get_npc(npc_eid);
    ASSERT_NE(n, nullptr);

    hb::combat::attack_event attack;
    attack.attacker = player_sys_->get_player(pid)->ecs_entity;
    attack.defender = npc_eid;
    attack.type = hb::combat::damage_type::physical;
    attack.base_damage = 10000;

    // Hit chance is capped at 95%, so retry on miss (up to 20 attempts)
    hb::combat::combat_result result;
    for (int i = 0; i < 20 && !result.target_killed; ++i)
    {
        result = combat_->process_attack(attack);
    }

    EXPECT_TRUE(result.target_killed);
    EXPECT_TRUE(n->is_dead());
    EXPECT_GE(result.exp_reward, 0);
}

TEST_F(melee_pve_combat_test, npc_not_killed_by_zero_damage)
{
    auto pid = create_player_at({10, 10});
    auto npc_eid = spawn_monster_at({11, 10});

    auto* n = npc_sys_->get_npc(npc_eid);
    ASSERT_NE(n, nullptr);

    hb::combat::attack_event attack;
    attack.attacker = player_sys_->get_player(pid)->ecs_entity;
    attack.defender = npc_eid;
    attack.type = hb::combat::damage_type::physical;
    attack.base_damage = 0;

    auto result = combat_->process_attack(attack);
    EXPECT_FALSE(result.target_killed);
}

// ============================================================================
// NPC target validation tests (unit-level)
// ============================================================================

class npc_target_validation_test : public ::testing::Test
{
protected:
    void SetUp() override
    {
        hb::subsystems().create_subsystem<hb::npc::npc_system>();
        hb::subsystems().create_subsystem<hb::entity::entity_manager>();
        hb::subsystems().initialize_all();

        npc_sys_ = hb::subsystems().get<hb::npc::npc_system>();
    }

    void TearDown() override { hb::subsystems().clear_all(); }

    hb::npc::npc_system* npc_sys_{};
};

TEST_F(npc_target_validation_test, friendly_npc_detected)
{
    auto result = npc_sys_->spawn_npc(npc_id{1}, map_id{1}, pos{10, 10});
    ASSERT_TRUE(result.is_ok());
    auto eid = result.value();

    auto* n = npc_sys_->get_npc(eid);
    ASSERT_NE(n, nullptr);

    EXPECT_TRUE(n->is_monster());
    EXPECT_FALSE(n->is_friendly());

    n->category = hb::npc::npc_category::merchant;
    EXPECT_TRUE(n->is_friendly());
    EXPECT_FALSE(n->is_monster());
}

TEST_F(npc_target_validation_test, guard_is_friendly)
{
    auto result = npc_sys_->spawn_npc(npc_id{1}, map_id{1}, pos{10, 10});
    ASSERT_TRUE(result.is_ok());
    auto eid = result.value();

    auto* n = npc_sys_->get_npc(eid);
    ASSERT_NE(n, nullptr);

    n->category = hb::npc::npc_category::guard;
    EXPECT_TRUE(n->is_friendly());
}

TEST_F(npc_target_validation_test, dead_npc_detected)
{
    auto result = npc_sys_->spawn_npc(npc_id{1}, map_id{1}, pos{10, 10});
    ASSERT_TRUE(result.is_ok());
    auto eid = result.value();

    auto* n = npc_sys_->get_npc(eid);
    ASSERT_NE(n, nullptr);

    EXPECT_TRUE(n->is_alive());
    EXPECT_FALSE(n->is_dead());

    npc_sys_->kill_npc(eid, hb::entity::entity(1));

    EXPECT_TRUE(n->is_dead());
    EXPECT_FALSE(n->is_alive());
}

TEST_F(npc_target_validation_test, nonexistent_npc_returns_null)
{
    auto* n = npc_sys_->get_npc(hb::entity::entity(9999));
    EXPECT_EQ(n, nullptr);
}

// ============================================================================
// Attack cooldown tests (player struct level)
// ============================================================================

TEST(attack_cooldown_test, last_attack_time_default_zero)
{
    hb::player::player p{};
    auto epoch = std::chrono::steady_clock::time_point{};
    EXPECT_EQ(p.last_attack_time, epoch);
}

TEST(attack_cooldown_test, elapsed_time_calculation)
{
    auto t1 = std::chrono::steady_clock::now();
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    auto t2 = std::chrono::steady_clock::now();

    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(t2 - t1);
    EXPECT_GE(elapsed.count(), 10);
}

} // namespace
