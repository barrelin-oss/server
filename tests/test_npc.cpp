// test_npc.cpp
// Unit tests for NPC system

#include <gtest/gtest.h>
#include "core/types.h"
#include "entity/entity.h"
#include "network/json_protocol.h"
#include "npc/ai_behavior.h"
#include "npc/spawn_point.h"
#include "npc/loot_config.h"
#include "npc/loot_generator.h"
#include "npc/npc.h"
#include "npc/npc_system.h"
#include "world/tile.h"

using hb::item_id;
using hb::map_id;
using hb::npc_id;
using namespace hb::entity;
using namespace hb::npc;
using namespace hb::world;

// AI behavior tests

TEST(ai_config_test, flags)
{
    ai_config config;
    config.flags = ai_flags::aggressive | ai_flags::pursues_far;

    EXPECT_TRUE(config.has_flag(ai_flags::aggressive));
    EXPECT_TRUE(config.has_flag(ai_flags::pursues_far));
    EXPECT_FALSE(config.has_flag(ai_flags::cowardly));
}

TEST(ai_runtime_state_test, state_transitions)
{
    ai_runtime_state state;
    state.set_state(ai_state::idle);

    EXPECT_EQ(state.state, ai_state::idle);
    EXPECT_GE(state.time_in_state_ms(), 0);

    state.set_state(ai_state::chase);
    EXPECT_EQ(state.state, ai_state::chase);
}

TEST(ai_runtime_state_test, target_management)
{
    ai_runtime_state state;
    state.target = entity{42};
    state.aggro_level = 100;

    EXPECT_TRUE(state.target.is_valid());
    EXPECT_EQ(state.aggro_level, 100);

    state.clear_target();
    EXPECT_FALSE(state.target.is_valid());
    EXPECT_EQ(state.aggro_level, 0);
}

// Spawn point tests

TEST(spawn_point_test, can_spawn)
{
    spawn_point spawn;
    spawn.npc_type = npc_id{1};
    spawn.max_count = 3;
    spawn.current_count = 0;
    spawn.next_spawn_time = std::chrono::steady_clock::time_point{};

    EXPECT_TRUE(spawn.can_spawn());

    spawn.on_spawn();
    EXPECT_EQ(spawn.current_count, 1);
    EXPECT_TRUE(spawn.can_spawn());

    spawn.current_count = 3;
    EXPECT_FALSE(spawn.can_spawn());
}

TEST(spawn_point_test, on_death)
{
    spawn_point spawn;
    spawn.max_count = 2;
    spawn.current_count = 2;
    spawn.respawn_time_ms = 1000;

    spawn.on_death();
    EXPECT_EQ(spawn.current_count, 1);
}

// Loot config tests

TEST(loot_config_test, weighted_item)
{
    weighted_item wi;
    wi.item = item_id{100};
    wi.weight = 50;

    EXPECT_EQ(wi.item.value, 100);
    EXPECT_EQ(wi.weight, 50);
}

TEST(loot_config_test, item_pool)
{
    item_pool pool;
    pool.name = "test_pool";
    pool.items.push_back({item_id{1}, 10});
    pool.items.push_back({item_id{2}, 20});
    pool.total_weight = 30;

    EXPECT_EQ(pool.items.size(), 2);
    EXPECT_EQ(pool.total_weight, 30);
}

TEST(loot_config_test, pick_from_pool)
{
    item_pool pool;
    pool.name = "single";
    pool.items.push_back({item_id{42}, 1});
    pool.total_weight = 1;

    // Single item pool always returns that item
    auto picked = detail::pick_from_pool(pool);
    EXPECT_EQ(picked.value, 42);
}

TEST(loot_config_test, loot_phase_config)
{
    loot_phase_config phase;
    phase.gold_chance = 2100;
    phase.drops.push_back({"potions", 1260});
    phase.drops.push_back({"melee_tier1", 67});

    EXPECT_EQ(phase.gold_chance, 2100);
    EXPECT_EQ(phase.drops.size(), 2);
    EXPECT_EQ(phase.drops[0].pool_name, "potions");
}

TEST(loot_config_test, multi_drop_config)
{
    loot_phase_config phase;
    phase.multi_drop = multi_drop_config{5, 15};

    EXPECT_TRUE(phase.multi_drop.has_value());
    EXPECT_EQ(phase.multi_drop->min_count, 5);
    EXPECT_EQ(phase.multi_drop->max_count, 15);
}

// NPC component tests

TEST(npc_test, damage_and_death)
{
    npc n;
    n.max_hp = 100;
    n.hp = 100;

    EXPECT_TRUE(n.is_alive());

    n.damage(30);
    EXPECT_EQ(n.hp, 70);
    EXPECT_TRUE(n.is_alive());

    n.damage(100);
    EXPECT_EQ(n.hp, 0);
    EXPECT_TRUE(n.is_dead());
    EXPECT_EQ(n.ai_state.state, ai_state::dead);
}

TEST(npc_test, healing)
{
    npc n;
    n.max_hp = 100;
    n.hp = 50;

    n.heal(30);
    EXPECT_EQ(n.hp, 80);

    n.heal(100);
    EXPECT_EQ(n.hp, 100); // Capped at max
}

TEST(npc_test, hp_percent)
{
    npc n;
    n.max_hp = 100;
    n.hp = 25;

    EXPECT_FLOAT_EQ(n.hp_percent(), 25.0f);
}

TEST(npc_test, categories)
{
    npc n;
    n.category = npc_category::monster;
    EXPECT_TRUE(n.is_monster());
    EXPECT_FALSE(n.is_friendly());

    n.category = npc_category::merchant;
    EXPECT_FALSE(n.is_monster());
    EXPECT_TRUE(n.is_friendly());

    n.category = npc_category::pet;
    EXPECT_TRUE(n.is_pet());
}

TEST(npc_test, ownership)
{
    npc n;
    EXPECT_FALSE(n.has_owner());

    n.owner = entity{42};
    EXPECT_TRUE(n.has_owner());
}

// NPC system tests

class npc_system_test : public ::testing::Test
{
protected:
    void SetUp() override { system_.initialize(); }

    void TearDown() override { system_.shutdown(); }

    npc_system system_;
};

TEST_F(npc_system_test, lifecycle)
{
    EXPECT_TRUE(system_.is_initialized());
    EXPECT_EQ(system_.name(), "npc_system");
}

TEST_F(npc_system_test, spawn_npc)
{
    auto result = system_.spawn_npc(npc_id{1}, map_id{1}, position{10, 10});
    ASSERT_TRUE(result.is_ok());

    auto entity_id = result.value();
    EXPECT_TRUE(entity_id.is_valid());
    EXPECT_EQ(system_.npc_count(), 1);

    auto* n = system_.get_npc(entity_id);
    ASSERT_NE(n, nullptr);
    EXPECT_EQ(n->pos.x, 10);
    EXPECT_EQ(n->pos.y, 10);
}

TEST_F(npc_system_test, despawn_npc)
{
    auto result = system_.spawn_npc(npc_id{1}, map_id{1}, position{0, 0});
    auto entity_id = result.value();

    EXPECT_EQ(system_.npc_count(), 1);
    system_.despawn_npc(entity_id);
    EXPECT_EQ(system_.npc_count(), 0);
}

TEST_F(npc_system_test, apply_damage)
{
    auto result = system_.spawn_npc(npc_id{1}, map_id{1}, position{0, 0});
    auto entity_id = result.value();

    auto* n = system_.get_npc(entity_id);
    int32_t initial_hp = n->hp;

    system_.apply_damage(entity_id, 20, entity{100});
    EXPECT_EQ(n->hp, initial_hp - 20);
    EXPECT_EQ(n->ai_state.target.id, 100);
}

TEST_F(npc_system_test, kill_npc)
{
    auto result = system_.spawn_npc(npc_id{1}, map_id{1}, position{0, 0});
    auto entity_id = result.value();

    auto* n = system_.get_npc(entity_id);
    system_.kill_npc(entity_id, entity{100});

    EXPECT_TRUE(n->is_dead());
    EXPECT_EQ(n->ai_state.state, ai_state::dead);
}

TEST_F(npc_system_test, get_npcs_in_range)
{
    system_.spawn_npc(npc_id{1}, map_id{1}, position{10, 10});
    system_.spawn_npc(npc_id{2}, map_id{1}, position{15, 15});
    system_.spawn_npc(npc_id{3}, map_id{1}, position{50, 50});

    auto nearby = system_.get_npcs_in_range(map_id{1}, position{10, 10}, 10);
    EXPECT_EQ(nearby.size(), 2);

    auto far = system_.get_npcs_in_range(map_id{1}, position{50, 50}, 5);
    EXPECT_EQ(far.size(), 1);
}

TEST_F(npc_system_test, spawn_point_integration)
{
    spawn_point spawn;
    spawn.npc_type = npc_id{1};
    spawn.map = map_id{1};
    spawn.center = position{20, 20};
    spawn.max_count = 2;
    spawn.respawn_time_ms = 100;

    system_.add_spawn_point(spawn);

    // Initial spawn should work
    // (The actual spawning would happen in update())
}

// NPC max limit test

TEST_F(npc_system_test, max_npc_limit)
{
    npc_system_config config;
    config.max_npcs = 2;
    system_.set_config(config);

    auto r1 = system_.spawn_npc(npc_id{1}, map_id{1}, position{0, 0});
    EXPECT_TRUE(r1.is_ok());

    auto r2 = system_.spawn_npc(npc_id{1}, map_id{1}, position{1, 0});
    EXPECT_TRUE(r2.is_ok());

    auto r3 = system_.spawn_npc(npc_id{1}, map_id{1}, position{2, 0});
    EXPECT_TRUE(r3.is_err());
    EXPECT_EQ(system_.npc_count(), 2);
}

// Callback tests

TEST_F(npc_system_test, spawn_callback)
{
    bool callback_fired = false;
    std::string spawned_name;

    system_.set_on_spawn_callback(
        [&](const npc& n)
        {
            callback_fired = true;
            spawned_name = n.name;
        });

    system_.spawn_npc(npc_id{1}, map_id{1}, position{10, 10});
    EXPECT_TRUE(callback_fired);
}

TEST_F(npc_system_test, death_callback)
{
    bool callback_fired = false;
    uint32_t killer_id = 0;

    system_.set_on_death_callback(
        [&](const npc&, hb::entity::entity killer, int32_t /*damage*/)
        {
            callback_fired = true;
            killer_id = killer.id;
        });

    auto result = system_.spawn_npc(npc_id{1}, map_id{1}, position{5, 5});
    auto eid = result.value();

    system_.kill_npc(eid, entity{999});
    system_.flush_pending_deaths();
    EXPECT_TRUE(callback_fired);
    EXPECT_EQ(killer_id, 999);
}

TEST_F(npc_system_test, kill_already_dead_npc)
{
    auto result = system_.spawn_npc(npc_id{1}, map_id{1}, position{5, 5});
    auto eid = result.value();

    system_.kill_npc(eid, entity{1});
    system_.flush_pending_deaths();
    EXPECT_TRUE(system_.get_npc(eid)->is_dead());

    // Killing again should be a no-op
    int death_count = 0;
    system_.set_on_death_callback([&](const npc&, hb::entity::entity, int32_t) { ++death_count; });

    system_.kill_npc(eid, entity{2});
    system_.flush_pending_deaths();
    EXPECT_EQ(death_count, 0); // Already dead, callback not fired
}

// Damage leading to death

TEST_F(npc_system_test, damage_kills_npc)
{
    auto result = system_.spawn_npc(npc_id{1}, map_id{1}, position{10, 10});
    auto eid = result.value();

    auto* n = system_.get_npc(eid);
    int32_t hp = n->hp;

    system_.apply_damage(eid, hp + 100, entity{50});
    EXPECT_TRUE(n->is_dead());
    EXPECT_EQ(n->hp, 0);
}

// BUG: apply_damage calls npc::damage() which sets hp=0 (is_dead()=true),
// then calls kill_npc() which early-returns because is_dead() is already true.
// Death callback only fires via kill_npc() on a living NPC.
TEST_F(npc_system_test, kill_npc_fires_death_callback)
{
    auto result = system_.spawn_npc(npc_id{1}, map_id{1}, position{10, 10});
    auto eid = result.value();

    bool death_fired = false;
    system_.set_on_death_callback([&](const npc&, hb::entity::entity, int32_t) { death_fired = true; });

    system_.kill_npc(eid, entity{50});
    system_.flush_pending_deaths();
    EXPECT_TRUE(death_fired);
}

TEST_F(npc_system_test, damage_sets_target)
{
    auto result = system_.spawn_npc(npc_id{1}, map_id{1}, position{10, 10});
    auto eid = result.value();

    auto* n = system_.get_npc(eid);
    EXPECT_FALSE(n->ai_state.target.is_valid());

    system_.apply_damage(eid, 5, entity{42});
    EXPECT_EQ(n->ai_state.target.id, 42);
    EXPECT_EQ(n->ai_state.state, ai_state::chase);
}

TEST_F(npc_system_test, damage_increases_aggro)
{
    auto result = system_.spawn_npc(npc_id{1}, map_id{1}, position{10, 10});
    auto eid = result.value();

    auto* n = system_.get_npc(eid);
    EXPECT_EQ(n->ai_state.aggro_level, 0);

    system_.apply_damage(eid, 30, entity{1});
    EXPECT_EQ(n->ai_state.aggro_level, 30);

    system_.apply_damage(eid, 20, entity{1});
    EXPECT_EQ(n->ai_state.aggro_level, 50);
}

// Set/clear target

TEST_F(npc_system_test, set_target)
{
    auto result = system_.spawn_npc(npc_id{1}, map_id{1}, position{10, 10});
    auto eid = result.value();

    system_.set_target(eid, entity{77});

    auto* n = system_.get_npc(eid);
    EXPECT_EQ(n->ai_state.target.id, 77);
    EXPECT_EQ(n->ai_state.state, ai_state::chase);
}

TEST_F(npc_system_test, clear_target)
{
    auto result = system_.spawn_npc(npc_id{1}, map_id{1}, position{10, 10});
    auto eid = result.value();

    system_.set_target(eid, entity{77});
    system_.clear_target(eid);

    auto* n = system_.get_npc(eid);
    EXPECT_FALSE(n->ai_state.target.is_valid());
    EXPECT_EQ(n->ai_state.state, ai_state::return_home);
}

// Update AI directly

TEST_F(npc_system_test, update_ai_dead_npc_noop)
{
    auto result = system_.spawn_npc(npc_id{1}, map_id{1}, position{10, 10});
    auto eid = result.value();

    system_.kill_npc(eid, entity{1});

    // Should not crash
    system_.update_ai(eid);
}

TEST_F(npc_system_test, update_ai_nonexistent_npc_noop)
{
    // Should not crash for non-existent entity
    system_.update_ai(entity{99999});
}

// NPC exists and access tests

TEST_F(npc_system_test, npc_exists)
{
    auto result = system_.spawn_npc(npc_id{1}, map_id{1}, position{0, 0});
    auto eid = result.value();

    EXPECT_TRUE(system_.npc_exists(eid));
    EXPECT_FALSE(system_.npc_exists(entity{99999}));
}

TEST_F(npc_system_test, get_npc_nonexistent)
{
    EXPECT_EQ(system_.get_npc(entity{99999}), nullptr);
}

// Remove spawn points by map

TEST_F(npc_system_test, remove_spawn_points)
{
    spawn_point sp1;
    sp1.npc_type = npc_id{1};
    sp1.map = map_id{1};
    sp1.center = position{10, 10};
    sp1.max_count = 2;

    spawn_point sp2;
    sp2.npc_type = npc_id{2};
    sp2.map = map_id{2};
    sp2.center = position{20, 20};
    sp2.max_count = 3;

    system_.add_spawn_point(sp1);
    system_.add_spawn_point(sp2);

    system_.remove_spawn_points(map_id{1});

    // Only map 2 spawn points should remain
    // (No direct API to count spawn points, but this shouldn't crash)
}

// For each NPC

TEST_F(npc_system_test, for_each_npc)
{
    system_.spawn_npc(npc_id{1}, map_id{1}, position{0, 0});
    system_.spawn_npc(npc_id{2}, map_id{1}, position{5, 5});
    system_.spawn_npc(npc_id{3}, map_id{2}, position{10, 10});

    int total = 0;
    system_.for_each_npc([&](hb::entity::entity, npc&) { ++total; });
    EXPECT_EQ(total, 3);

    int map1_count = 0;
    system_.for_each_npc_on_map(map_id{1}, [&](hb::entity::entity, npc&) { ++map1_count; });
    EXPECT_EQ(map1_count, 2);
}

// Enable/disable AI

TEST_F(npc_system_test, enable_disable_ai)
{
    system_.enable_ai(false);

    auto result = system_.spawn_npc(npc_id{1}, map_id{1}, position{10, 10});
    auto eid = result.value();

    auto* n = system_.get_npc(eid);
    n->ai_state.set_state(ai_state::idle);

    // With AI disabled, update should not change state
    // (update checks config_.enable_ai before calling update_all_ai)
    system_.update(1.0f);

    // State should still be idle (no AI processing happened)
    EXPECT_EQ(n->ai_state.state, ai_state::idle);
}

// Deactivate spawns removes NPCs

TEST_F(npc_system_test, deactivate_spawns_removes_npcs)
{
    system_.spawn_npc(npc_id{1}, map_id{1}, position{0, 0});
    system_.spawn_npc(npc_id{2}, map_id{1}, position{5, 5});
    system_.spawn_npc(npc_id{3}, map_id{2}, position{10, 10});

    EXPECT_EQ(system_.npc_count(), 3);

    system_.deactivate_spawns(map_id{1});
    EXPECT_EQ(system_.npc_count(), 1); // Only map 2 NPC remains
}

// Apply damage to dead NPC is no-op

TEST_F(npc_system_test, apply_damage_dead_npc_noop)
{
    auto result = system_.spawn_npc(npc_id{1}, map_id{1}, position{10, 10});
    auto eid = result.value();

    system_.kill_npc(eid, entity{1});

    // Applying damage to dead NPC should not crash
    system_.apply_damage(eid, 100, entity{2});
    EXPECT_EQ(system_.get_npc(eid)->hp, 0);
}

// Damage to non-existent NPC

TEST_F(npc_system_test, apply_damage_nonexistent_npc)
{
    // Should not crash
    system_.apply_damage(entity{99999}, 50, entity{1});
}

// NPC fallback defaults (no registry)

TEST_F(npc_system_test, spawn_applies_defaults_without_registry)
{
    auto result = system_.spawn_npc(npc_id{999}, map_id{1}, position{10, 10});
    ASSERT_TRUE(result.is_ok());

    auto* n = system_.get_npc(result.value());
    // Without registry, should get fallback defaults
    EXPECT_EQ(n->name, "Unknown");
    EXPECT_EQ(n->max_hp, 100);
    EXPECT_EQ(n->level, 1);
}

TEST_F(npc_system_test, disengage_all_from)
{
    // Spawn 3 NPCs targeting the same entity
    auto r1 = system_.spawn_npc(npc_id{1}, map_id{1}, position{0, 0});
    auto r2 = system_.spawn_npc(npc_id{2}, map_id{1}, position{5, 5});
    auto r3 = system_.spawn_npc(npc_id{3}, map_id{1}, position{10, 10});
    ASSERT_TRUE(r1.is_ok());
    ASSERT_TRUE(r2.is_ok());
    ASSERT_TRUE(r3.is_ok());

    auto player_entity = entity{999};

    // Set NPCs 1 and 2 to target the player, NPC 3 targets something else
    system_.set_target(r1.value(), player_entity);
    system_.set_target(r2.value(), player_entity);
    system_.set_target(r3.value(), entity{888});

    EXPECT_EQ(system_.get_npc(r1.value())->ai_state.target, player_entity);
    EXPECT_EQ(system_.get_npc(r2.value())->ai_state.target, player_entity);
    EXPECT_EQ(system_.get_npc(r3.value())->ai_state.target, entity{888});

    // Player dies — all NPCs targeting them should disengage
    system_.disengage_all_from(player_entity);

    EXPECT_FALSE(system_.get_npc(r1.value())->ai_state.target.is_valid());
    EXPECT_FALSE(system_.get_npc(r2.value())->ai_state.target.is_valid());
    EXPECT_EQ(system_.get_npc(r1.value())->ai_state.state, ai_state::return_home);
    EXPECT_EQ(system_.get_npc(r2.value())->ai_state.state, ai_state::return_home);

    // NPC 3 should be unaffected
    EXPECT_EQ(system_.get_npc(r3.value())->ai_state.target, entity{888});
}

// ========== Tile Dead Occupant Tests ==========

TEST(tile_dead_occupant_test, set_and_clear_dead_entity)
{
    hb::world::dynamic_tile tile;

    EXPECT_FALSE(tile.has_dead_entity());
    EXPECT_FALSE(tile.has_occupant());

    // Simulate entity alive on tile
    tile.set_occupant(hb::entity_id{42}, hb::world::owner_type::npc);
    EXPECT_TRUE(tile.has_occupant());

    // Simulate death: clear live, set dead
    tile.clear_occupant();
    tile.set_dead_entity(hb::entity_id{42}, hb::world::owner_type::npc);

    EXPECT_FALSE(tile.has_occupant());
    EXPECT_TRUE(tile.has_dead_entity());
    EXPECT_EQ(tile.dead_entity.value, 42u);
    EXPECT_EQ(tile.dead_type, hb::world::owner_type::npc);
}

TEST(tile_dead_occupant_test, dead_slot_is_last_write_wins)
{
    hb::world::dynamic_tile tile;

    tile.set_dead_entity(hb::entity_id{1}, hb::world::owner_type::npc);
    EXPECT_EQ(tile.dead_entity.value, 1u);

    // Second death overwrites
    tile.set_dead_entity(hb::entity_id{2}, hb::world::owner_type::npc);
    EXPECT_EQ(tile.dead_entity.value, 2u);
}

TEST(tile_dead_occupant_test, dead_entity_does_not_block_movement)
{
    hb::world::dynamic_tile tile;
    tile.set_dead_entity(hb::entity_id{42}, hb::world::owner_type::npc);

    // Dead entity should not count as occupant
    EXPECT_FALSE(tile.has_occupant());
    EXPECT_FALSE(tile.is_temp_blocked());
}

TEST(tile_dead_occupant_test, clear_dead_entity_removes_corpse_flag)
{
    hb::world::dynamic_tile tile;
    tile.set_dead_entity(hb::entity_id{42}, hb::world::owner_type::npc);
    EXPECT_TRUE(hb::world::has_flag(tile.flags, hb::world::dynamic_tile_flags::has_corpse));

    tile.clear_dead_entity();
    EXPECT_FALSE(tile.has_dead_entity());
    EXPECT_FALSE(hb::world::has_flag(tile.flags, hb::world::dynamic_tile_flags::has_corpse));
}

// ========== Visible Entity Msg is_dead Tests ==========

TEST(visible_entity_msg_test, is_dead_serialized_when_true)
{
    hb::network::visible_entity_msg msg;
    msg.entity_id = 42;
    msg.type = "npc";
    msg.name = "Slime";
    msg.x = 10;
    msg.y = 20;
    msg.hp_percent = 0;
    msg.direction = 0;
    msg.template_id = 1;
    msg.sprite_id = 10;
    msg.level = 1;
    msg.category = "monster";
    msg.hostility = "neutral";
    msg.faction = {};
    msg.pk_status = {};
    msg.guild_name = {};
    msg.guild_tag = {};
    msg.is_dead = true;

    auto j = msg.to_json();
    EXPECT_TRUE(j.contains("is_dead"));
    EXPECT_TRUE(j["is_dead"].get<bool>());
}

TEST(visible_entity_msg_test, is_dead_omitted_when_false)
{
    hb::network::visible_entity_msg msg;
    msg.entity_id = 42;
    msg.type = "npc";
    msg.name = "Slime";
    msg.x = 10;
    msg.y = 20;
    msg.hp_percent = 100;
    msg.direction = 0;
    msg.template_id = 1;
    msg.sprite_id = 10;
    msg.level = 1;
    msg.category = "monster";
    msg.hostility = "neutral";
    msg.faction = {};
    msg.pk_status = {};
    msg.guild_name = {};
    msg.guild_tag = {};
    msg.is_dead = false;

    auto j = msg.to_json();
    EXPECT_FALSE(j.contains("is_dead"));
}
