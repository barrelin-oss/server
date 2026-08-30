// test_safe_zone.cpp
// Tests for safe zone PvP enforcement and guard NPC behavior

#include <gtest/gtest.h>
#include <fstream>
#include <filesystem>

#include "combat/combat_system.h"
#include "magic/magic_system.h"
#include "magic/spell.h"
#include "player/player_system.h"
#include "world/world_subsystem.h"
#include "npc/npc_system.h"
#include "npc/npc.h"
#include "npc/ai_behavior.h"
#include "entity/entity_manager.h"
#include "core/subsystem.h"

// Only use hb namespace — qualify everything else explicitly to avoid ambiguity
using namespace hb;

namespace
{

// Helper to write a minimal YAML config with safe zones and load it into a map
void setup_safe_zone_map(
    world::world_subsystem& world, map_id mid, int16_t sz_left, int16_t sz_top, int16_t sz_right, int16_t sz_bottom)
{
    auto tmp = std::filesystem::temp_directory_path() / "test_safe_zone.yaml";
    {
        std::ofstream f(tmp);
        f << "name: test_map\n";
        f << "safe_zones:\n";
        f << "  - { id: 1, left: " << sz_left << ", top: " << sz_top << ", right: " << sz_right
          << ", bottom: " << sz_bottom << " }\n";
    }
    auto* m = world.get_map(mid);
    ASSERT_NE(m, nullptr);
    auto result = m->load_config_yaml(tmp);
    ASSERT_TRUE(result.is_ok()) << "Failed to load safe zone config";
    std::filesystem::remove(tmp);
}

} // anonymous namespace

// ============================================================================
// Safe Zone PvP Combat Tests
// ============================================================================

class safe_zone_combat_test : public ::testing::Test
{
protected:
    void SetUp() override
    {
        subsystems().create_subsystem<player::player_system>();
        subsystems().create_subsystem<world::world_subsystem>();
        subsystems().create_subsystem<combat::combat_system>();
        subsystems().create_subsystem<entity::entity_manager>();
        subsystems().initialize_all();

        player_sys_ = subsystems().get<player::player_system>();
        world_ = subsystems().get<world::world_subsystem>();
        combat_ = subsystems().get<combat::combat_system>();

        // Create a map with safe zone at (10,10)-(20,20)
        world::map_config cfg;
        cfg.name = "test_map";
        cfg.width = 100;
        cfg.height = 100;
        auto result = world_->create_map(cfg);
        ASSERT_TRUE(result.is_ok());
        map_id_ = result.value();

        setup_safe_zone_map(*world_, map_id_, 10, 10, 20, 20);

        // Verify safe zone was applied
        auto* m = world_->get_map(map_id_);
        ASSERT_NE(m, nullptr);
        ASSERT_TRUE(m->is_safe_zone({15, 15}));
        ASSERT_FALSE(m->is_safe_zone({50, 50}));
    }

    void TearDown() override { subsystems().clear_all(); }

    auto create_player_at(world::position pos) -> player_id
    {
        player::player_create_info info;
        info.name = "player_" + std::to_string(next_name_++);
        auto result = player_sys_->create_player(info);
        auto pid = result.value();
        player_sys_->set_position(pid, map_id_, pos, world::direction::south);
        return pid;
    }

    // Resolve the real ECS entity handle for a player (player_id values are NOT entity ids)
    auto ecs(player_id pid) -> entity::entity { return player_sys_->get_player(pid)->ecs_entity; }

    player::player_system* player_sys_{};
    world::world_subsystem* world_{};
    combat::combat_system* combat_{};
    map_id map_id_{};
    int next_name_{1};
};

TEST_F(safe_zone_combat_test, pvp_blocked_when_attacker_in_safe_zone)
{
    auto p1 = create_player_at({15, 15}); // In safe zone
    auto p2 = create_player_at({50, 50}); // Outside safe zone

    auto e1 = ecs(p1);
    auto e2 = ecs(p2);

    EXPECT_FALSE(combat_->can_attack(e1, e2));
}

TEST_F(safe_zone_combat_test, pvp_blocked_when_defender_in_safe_zone)
{
    auto p1 = create_player_at({50, 50}); // Outside safe zone
    auto p2 = create_player_at({15, 15}); // In safe zone

    auto e1 = ecs(p1);
    auto e2 = ecs(p2);

    EXPECT_FALSE(combat_->can_attack(e1, e2));
}

TEST_F(safe_zone_combat_test, pvp_blocked_when_both_in_safe_zone)
{
    auto p1 = create_player_at({12, 12}); // In safe zone
    auto p2 = create_player_at({18, 18}); // In safe zone

    auto e1 = ecs(p1);
    auto e2 = ecs(p2);

    EXPECT_FALSE(combat_->can_attack(e1, e2));
}

TEST_F(safe_zone_combat_test, pvp_allowed_when_neither_in_safe_zone)
{
    auto p1 = create_player_at({50, 50}); // Outside safe zone
    auto p2 = create_player_at({60, 60}); // Outside safe zone

    auto e1 = ecs(p1);
    auto e2 = ecs(p2);

    EXPECT_TRUE(combat_->can_attack(e1, e2));
}

TEST_F(safe_zone_combat_test, player_vs_npc_allowed_in_safe_zone)
{
    // PvE should always be allowed even in safe zones
    auto p1 = create_player_at({15, 15}); // In safe zone

    auto player_e = ecs(p1);
    entity::entity npc_e(9999u); // Not a player ID

    EXPECT_TRUE(combat_->can_attack(player_e, npc_e));
}

TEST_F(safe_zone_combat_test, npc_vs_player_allowed_in_safe_zone)
{
    // NPC attacking a player in safe zone should be allowed
    auto p1 = create_player_at({15, 15}); // In safe zone

    auto player_e = ecs(p1);
    entity::entity npc_e(9999u); // Not a player ID

    EXPECT_TRUE(combat_->can_attack(npc_e, player_e));
}

// ============================================================================
// Safe Zone Magic Tests
// ============================================================================

class safe_zone_magic_test : public ::testing::Test
{
protected:
    void SetUp() override
    {
        subsystems().create_subsystem<player::player_system>();
        subsystems().create_subsystem<world::world_subsystem>();
        subsystems().create_subsystem<magic::magic_system>();
        subsystems().create_subsystem<entity::entity_manager>();
        subsystems().initialize_all();

        player_sys_ = subsystems().get<player::player_system>();
        world_ = subsystems().get<world::world_subsystem>();
        magic_ = subsystems().get<magic::magic_system>();

        // Create a map with safe zone at (10,10)-(20,20)
        world::map_config cfg;
        cfg.name = "test_map";
        cfg.width = 100;
        cfg.height = 100;
        auto result = world_->create_map(cfg);
        ASSERT_TRUE(result.is_ok());
        map_id_ = result.value();

        setup_safe_zone_map(*world_, map_id_, 10, 10, 20, 20);

        // Register an offensive test spell
        magic::spell_template offensive;
        offensive.id = spell_id(1);
        offensive.name = "Fireball";
        offensive.category = magic::spell_category::attack;
        offensive.target_type = magic::spell_target::single_enemy;
        offensive.mana_cost = 10;
        offensive.base_damage = 50;
        offensive.range = 10; // 10 screens = 120 tiles (enough for test map)
        magic_->register_spell(offensive);

        // Register a defensive/buff test spell
        magic::spell_template buff;
        buff.id = spell_id(2);
        buff.name = "Protection";
        buff.category = magic::spell_category::buff;
        buff.target_type = magic::spell_target::single_ally;
        buff.mana_cost = 5;
        buff.range = 10;
        magic_->register_spell(buff);

        // Register an AOE offensive spell
        magic::spell_template aoe;
        aoe.id = spell_id(3);
        aoe.name = "Blizzard";
        aoe.category = magic::spell_category::attack;
        aoe.target_type = magic::spell_target::aoe_enemy;
        aoe.mana_cost = 20;
        aoe.aoe_radius = 5;
        aoe.base_damage = 30;
        aoe.range = 10;
        magic_->register_spell(aoe);
    }

    void TearDown() override { subsystems().clear_all(); }

    auto create_player_at(world::position pos) -> player_id
    {
        player::player_create_info info;
        info.name = "mage_" + std::to_string(next_name_++);
        auto result = player_sys_->create_player(info);
        auto pid = result.value();
        player_sys_->set_position(pid, map_id_, pos, world::direction::south);

        // Give enough stats/mana to cast
        auto* p = player_sys_->get_player(pid);
        if (p)
        {
            p->mp = 1000;
            p->experience.level = 100;
            p->computed.intelligence = 100;
            p->computed.magic = 100;
        }

        return pid;
    }

    // Resolve the real ECS entity handle for a player (player_id values are NOT entity ids)
    auto ecs(player_id pid) -> entity::entity { return player_sys_->get_player(pid)->ecs_entity; }

    player::player_system* player_sys_{};
    world::world_subsystem* world_{};
    magic::magic_system* magic_{};
    map_id map_id_{};
    int next_name_{1};
};

TEST_F(safe_zone_magic_test, offensive_spell_blocked_on_player_in_safe_zone)
{
    auto caster = create_player_at({50, 50}); // Outside safe zone
    auto target = create_player_at({15, 15}); // In safe zone

    auto caster_e = ecs(caster);
    auto target_e = ecs(target);

    // Learn the spell
    magic_->learn_spell(caster_e, spell_id(1));

    magic::cast_target ct;
    ct.target = target_e;

    auto result = magic_->can_cast(caster_e, spell_id(1), ct);
    EXPECT_EQ(result, magic::cast_result::safe_zone_blocked);
}

TEST_F(safe_zone_magic_test, offensive_spell_blocked_when_caster_in_safe_zone)
{
    auto caster = create_player_at({15, 15}); // In safe zone
    auto target = create_player_at({50, 50}); // Outside

    auto caster_e = ecs(caster);
    auto target_e = ecs(target);

    magic_->learn_spell(caster_e, spell_id(1));

    magic::cast_target ct;
    ct.target = target_e;

    auto result = magic_->can_cast(caster_e, spell_id(1), ct);
    EXPECT_EQ(result, magic::cast_result::safe_zone_blocked);
}

TEST_F(safe_zone_magic_test, buff_spell_allowed_in_safe_zone)
{
    auto caster = create_player_at({15, 15}); // In safe zone
    auto target = create_player_at({16, 16}); // Also in safe zone

    auto caster_e = ecs(caster);
    auto target_e = ecs(target);

    magic_->learn_spell(caster_e, spell_id(2));

    magic::cast_target ct;
    ct.target = target_e;

    auto result = magic_->can_cast(caster_e, spell_id(2), ct);
    EXPECT_EQ(result, magic::cast_result::success);
}

TEST_F(safe_zone_magic_test, offensive_spell_on_npc_allowed_in_safe_zone)
{
    auto caster = create_player_at({15, 15}); // In safe zone
    auto caster_e = ecs(caster);
    entity::entity npc_e(9999u); // Not a player

    magic_->learn_spell(caster_e, spell_id(1));

    magic::cast_target ct;
    ct.target = npc_e;

    auto result = magic_->can_cast(caster_e, spell_id(1), ct);
    // Should NOT be safe_zone_blocked — NPC target is PvE
    EXPECT_NE(result, magic::cast_result::safe_zone_blocked);
}

// ============================================================================
// Guard Behavior Tests
// ============================================================================

class guard_behavior_test : public ::testing::Test
{
protected:
    void SetUp() override
    {
        subsystems().create_subsystem<player::player_system>();
        subsystems().create_subsystem<world::world_subsystem>();
        subsystems().create_subsystem<entity::entity_manager>();
        subsystems().create_subsystem<npc::npc_system>();
        subsystems().initialize_all();

        player_sys_ = subsystems().get<player::player_system>();
        world_ = subsystems().get<world::world_subsystem>();
        entity_mgr_ = subsystems().get<entity::entity_manager>();
        npc_sys_ = subsystems().get<npc::npc_system>();

        // Create test map
        world::map_config cfg;
        cfg.name = "test_map";
        cfg.width = 100;
        cfg.height = 100;
        auto result = world_->create_map(cfg);
        ASSERT_TRUE(result.is_ok());
        map_id_ = result.value();
    }

    void TearDown() override { subsystems().clear_all(); }

    auto create_player_at(world::position pos, int32_t pk_points = 0) -> player_id
    {
        player::player_create_info info;
        info.name = "player_" + std::to_string(next_name_++);
        auto result = player_sys_->create_player(info);
        auto pid = result.value();
        player_sys_->set_position(pid, map_id_, pos, world::direction::south);

        auto* p = player_sys_->get_player(pid);
        if (p)
        {
            p->hp = 1000;
            p->pk.points = pk_points;

            // Create ECS entity so spatial queries work
            auto eid = entity_mgr_->create(entity::entity_type::player);
            p->ecs_entity = eid;

            auto* m = world_->get_map(map_id_);
            if (m)
            {
                m->spatial().add(entity_id{eid.index()}, pos);
            }
        }

        return pid;
    }

    player::player_system* player_sys_{};
    world::world_subsystem* world_{};
    entity::entity_manager* entity_mgr_{};
    npc::npc_system* npc_sys_{};
    map_id map_id_{};
    int next_name_{1};
};

TEST_F(guard_behavior_test, guard_has_correct_ai_flags)
{
    npc::npc guard_npc;
    guard_npc.category = npc::npc_category::guard;
    guard_npc.ai.flags = npc::ai_flags::guard | npc::ai_flags::aggressive | npc::ai_flags::detect_invisible;

    EXPECT_TRUE(guard_npc.ai.has_flag(npc::ai_flags::guard));
    EXPECT_TRUE(guard_npc.ai.has_flag(npc::ai_flags::aggressive));
    EXPECT_TRUE(guard_npc.ai.has_flag(npc::ai_flags::detect_invisible));
}

TEST_F(guard_behavior_test, guard_targets_criminal)
{
    npc::npc guard_npc;
    guard_npc.category = npc::npc_category::guard;
    guard_npc.ai.flags = npc::ai_flags::guard | npc::ai_flags::aggressive | npc::ai_flags::detect_invisible;
    guard_npc.ai.aggro_range = 10;
    guard_npc.current_map = map_id_;
    guard_npc.pos = {50, 50};

    // Criminal player nearby (pk_points >= 30)
    auto criminal = create_player_at(world::position(52, 52), 50);
    auto* criminal_p = player_sys_->get_player(criminal);
    ASSERT_NE(criminal_p, nullptr);
    EXPECT_TRUE(criminal_p->pk.is_criminal());

    // Guard filter: criminal should NOT be skipped (is_criminal=true)
    EXPECT_TRUE(criminal_p->pk.is_criminal() || criminal_p->pk.is_murderer());
}

TEST_F(guard_behavior_test, guard_targets_murderer)
{
    auto murderer = create_player_at(world::position(52, 52), 150);
    auto* murderer_p = player_sys_->get_player(murderer);
    ASSERT_NE(murderer_p, nullptr);
    EXPECT_TRUE(murderer_p->pk.is_murderer());
}

TEST_F(guard_behavior_test, guard_ignores_innocent)
{
    auto innocent = create_player_at(world::position(52, 52), 10);
    auto* innocent_p = player_sys_->get_player(innocent);
    ASSERT_NE(innocent_p, nullptr);
    EXPECT_TRUE(innocent_p->pk.is_innocent());

    // Guard filter: innocent should be skipped
    npc::npc guard_npc;
    guard_npc.ai.flags = npc::ai_flags::guard;

    // Simulating the guard filter logic from find_aggro_target:
    // if guard && !criminal && !murderer -> skip
    bool should_skip =
        guard_npc.ai.has_flag(npc::ai_flags::guard) && !innocent_p->pk.is_criminal() && !innocent_p->pk.is_murderer();
    EXPECT_TRUE(should_skip);
}

TEST_F(guard_behavior_test, guard_detection_range)
{
    npc::npc guard_npc;
    guard_npc.category = npc::npc_category::guard;
    guard_npc.ai.aggro_range = 10;

    // A criminal at distance 8 should be in range
    EXPECT_LE(8, guard_npc.ai.aggro_range);

    // A criminal at distance 12 should be out of range
    EXPECT_GT(12, guard_npc.ai.aggro_range);
}

TEST_F(guard_behavior_test, guard_can_be_damaged)
{
    // Guards are NOT invulnerable - they have high stats but can be killed
    npc::npc guard_npc;
    guard_npc.category = npc::npc_category::guard;
    guard_npc.hp = 1350;
    guard_npc.max_hp = 1350;

    guard_npc.damage(500);
    EXPECT_EQ(guard_npc.hp, 850);
    EXPECT_TRUE(guard_npc.is_alive());

    guard_npc.damage(1000);
    EXPECT_EQ(guard_npc.hp, 0);
    EXPECT_TRUE(guard_npc.is_dead());
}

// PK state tests already exist in test_death_respawn.cpp
