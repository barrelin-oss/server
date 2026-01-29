// test_npc.cpp
// Unit tests for NPC system

#include <gtest/gtest.h>
#include "core/types.h"
#include "entity/entity.h"
#include "npc/ai_behavior.h"
#include "npc/spawn_point.h"
#include "npc/loot_table.h"
#include "npc/npc.h"
#include "npc/npc_system.h"

using hb::item_id;
using hb::npc_id;
using hb::map_id;
using namespace hb::entity;
using namespace hb::npc;
using namespace hb::world;

// AI behavior tests

TEST(ai_config_test, flags) {
    ai_config config;
    config.flags = ai_flags::aggressive | ai_flags::pursues_far;

    EXPECT_TRUE(config.has_flag(ai_flags::aggressive));
    EXPECT_TRUE(config.has_flag(ai_flags::pursues_far));
    EXPECT_FALSE(config.has_flag(ai_flags::cowardly));
}

TEST(ai_runtime_state_test, state_transitions) {
    ai_runtime_state state;
    state.set_state(ai_state::idle);

    EXPECT_EQ(state.state, ai_state::idle);
    EXPECT_GE(state.time_in_state_ms(), 0);

    state.set_state(ai_state::chase);
    EXPECT_EQ(state.state, ai_state::chase);
}

TEST(ai_runtime_state_test, target_management) {
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

TEST(spawn_point_test, can_spawn) {
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

TEST(spawn_point_test, on_death) {
    spawn_point spawn;
    spawn.max_count = 2;
    spawn.current_count = 2;
    spawn.respawn_time_ms = 1000;

    spawn.on_death();
    EXPECT_EQ(spawn.current_count, 1);
}

// Loot table tests

TEST(loot_entry_test, creation) {
    loot_entry entry;
    entry.item = item_id{100};
    entry.min_count = 1;
    entry.max_count = 5;
    entry.drop_chance = 500;  // 5%

    EXPECT_EQ(entry.item.value, 100);
}

TEST(loot_table_test, generate_drops) {
    loot_table table;

    loot_entry guaranteed;
    guaranteed.item = item_id{1};
    guaranteed.min_count = 1;
    guaranteed.max_count = 1;
    table.guaranteed.push_back(guaranteed);

    auto drops = table.generate_drops();
    EXPECT_GE(drops.size(), 1);
    EXPECT_EQ(drops[0].first.value, 1);
}

TEST(gold_drop_test, configuration) {
    gold_drop gold;
    gold.min_gold = 100;
    gold.max_gold = 500;
    gold.drop_chance = 10000;

    auto amount = gold.roll_gold();
    EXPECT_GE(amount, gold.min_gold);
}

// NPC component tests

TEST(npc_test, damage_and_death) {
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

TEST(npc_test, healing) {
    npc n;
    n.max_hp = 100;
    n.hp = 50;

    n.heal(30);
    EXPECT_EQ(n.hp, 80);

    n.heal(100);
    EXPECT_EQ(n.hp, 100);  // Capped at max
}

TEST(npc_test, hp_percent) {
    npc n;
    n.max_hp = 100;
    n.hp = 25;

    EXPECT_FLOAT_EQ(n.hp_percent(), 25.0f);
}

TEST(npc_test, categories) {
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

TEST(npc_test, ownership) {
    npc n;
    EXPECT_FALSE(n.has_owner());

    n.owner = entity{42};
    EXPECT_TRUE(n.has_owner());
}

// NPC system tests

class npc_system_test : public ::testing::Test {
protected:
    void SetUp() override {
        system_.initialize();
    }

    void TearDown() override {
        system_.shutdown();
    }

    npc_system system_;
};

TEST_F(npc_system_test, lifecycle) {
    EXPECT_TRUE(system_.is_initialized());
    EXPECT_EQ(system_.name(), "npc_system");
}

TEST_F(npc_system_test, spawn_npc) {
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

TEST_F(npc_system_test, despawn_npc) {
    auto result = system_.spawn_npc(npc_id{1}, map_id{1}, position{0, 0});
    auto entity_id = result.value();

    EXPECT_EQ(system_.npc_count(), 1);
    system_.despawn_npc(entity_id);
    EXPECT_EQ(system_.npc_count(), 0);
}

TEST_F(npc_system_test, apply_damage) {
    auto result = system_.spawn_npc(npc_id{1}, map_id{1}, position{0, 0});
    auto entity_id = result.value();

    auto* n = system_.get_npc(entity_id);
    int32_t initial_hp = n->hp;

    system_.apply_damage(entity_id, 20, entity{100});
    EXPECT_EQ(n->hp, initial_hp - 20);
    EXPECT_EQ(n->ai_state.target.id, 100);
}

TEST_F(npc_system_test, kill_npc) {
    auto result = system_.spawn_npc(npc_id{1}, map_id{1}, position{0, 0});
    auto entity_id = result.value();

    auto* n = system_.get_npc(entity_id);
    system_.kill_npc(entity_id, entity{100});

    EXPECT_TRUE(n->is_dead());
    EXPECT_EQ(n->ai_state.state, ai_state::dead);
}

TEST_F(npc_system_test, get_npcs_in_range) {
    system_.spawn_npc(npc_id{1}, map_id{1}, position{10, 10});
    system_.spawn_npc(npc_id{2}, map_id{1}, position{15, 15});
    system_.spawn_npc(npc_id{3}, map_id{1}, position{50, 50});

    auto nearby = system_.get_npcs_in_range(map_id{1}, position{10, 10}, 10);
    EXPECT_EQ(nearby.size(), 2);

    auto far = system_.get_npcs_in_range(map_id{1}, position{50, 50}, 5);
    EXPECT_EQ(far.size(), 1);
}

TEST_F(npc_system_test, spawn_point_integration) {
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
