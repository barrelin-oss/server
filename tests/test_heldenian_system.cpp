// test_heldenian_system.cpp
// Unit tests for heldenian warfare system

#include <gtest/gtest.h>
#include "core/types.h"
#include "war/war_types.h"
#include "war/war_system.h"
#include "war/heldenian/heldenian_types.h"
#include "war/heldenian/heldenian_system.h"
#include "network/json_protocol.h"
#include "npc/npc_system.h"

using hb::player_id;
using namespace hb::war;

// ========== Type Tests ==========

TEST(heldenian_types_test, objective_default)
{
    heldenian_objective obj;
    EXPECT_EQ(obj.id, 0);
    EXPECT_EQ(obj.hp, 0);
    EXPECT_EQ(obj.max_hp, 500);
    EXPECT_TRUE(obj.is_destroyed());
}

TEST(heldenian_types_test, objective_alive)
{
    heldenian_objective obj;
    obj.hp = 250;
    EXPECT_FALSE(obj.is_destroyed());
}

TEST(heldenian_types_test, player_data_default)
{
    heldenian_player_data data;
    EXPECT_EQ(data.kills, 0);
    EXPECT_EQ(data.deaths, 0);
    EXPECT_EQ(data.construction_points, 0);
}

// ========== Tower Defense Mode Tests ==========

class heldenian_tower_test : public ::testing::Test
{
protected:
    void SetUp() override
    {
        war_sys_.initialize();
        heldenian_.initialize();
        heldenian_.set_dependencies(&war_sys_, nullptr, nullptr, nullptr, nullptr);

        heldenian_config cfg;
        cfg.enabled = true;
        cfg.timing.duration_seconds = 2700;
        cfg.timing.status_broadcast_seconds = 15;

        heldenian_objective t1{.id = 1, .max_hp = 200};
        heldenian_objective t2{.id = 2, .max_hp = 200};
        cfg.aresden_towers = {t1, t2};

        heldenian_objective et1{.id = 1, .max_hp = 200};
        heldenian_objective et2{.id = 2, .max_hp = 200};
        cfg.elvine_towers = {et1, et2};

        heldenian_.set_config(cfg);
    }

    void TearDown() override
    {
        heldenian_.shutdown();
        war_sys_.shutdown();
    }

    war_system war_sys_;
    heldenian_system heldenian_;
};

TEST_F(heldenian_tower_test, starts_inactive)
{
    EXPECT_FALSE(heldenian_.is_active());
}

TEST_F(heldenian_tower_test, start_tower_mode)
{
    auto result = heldenian_.start_heldenian(heldenian_mode::tower_defense);
    EXPECT_TRUE(result.is_ok());
    EXPECT_TRUE(heldenian_.is_active());
    EXPECT_EQ(heldenian_.current_mode(), heldenian_mode::tower_defense);
}

TEST_F(heldenian_tower_test, start_twice_fails)
{
    heldenian_.start_heldenian(heldenian_mode::tower_defense);
    auto r2 = heldenian_.start_heldenian(heldenian_mode::tower_defense);
    EXPECT_TRUE(r2.is_err());
    EXPECT_EQ(r2.error(), heldenian_result::already_active);
}

TEST_F(heldenian_tower_test, end_succeeds)
{
    heldenian_.start_heldenian(heldenian_mode::tower_defense);
    auto result = heldenian_.end_heldenian(war_faction::aresden);
    EXPECT_EQ(result, heldenian_result::success);
    EXPECT_FALSE(heldenian_.is_active());
}

TEST_F(heldenian_tower_test, end_when_inactive_fails)
{
    auto result = heldenian_.end_heldenian(war_faction::aresden);
    EXPECT_EQ(result, heldenian_result::not_active);
}

TEST_F(heldenian_tower_test, cancel_works)
{
    heldenian_.start_heldenian(heldenian_mode::tower_defense);
    heldenian_.cancel_heldenian();
    EXPECT_FALSE(heldenian_.is_active());
}

TEST_F(heldenian_tower_test, towers_restored_on_start)
{
    heldenian_.start_heldenian(heldenian_mode::tower_defense);

    const auto& ares = heldenian_.get_objectives(war_faction::aresden);
    ASSERT_EQ(ares.size(), 2);
    EXPECT_EQ(ares[0].hp, 200);
    EXPECT_EQ(ares[0].faction, war_faction::aresden);

    const auto& elv = heldenian_.get_objectives(war_faction::elvine);
    ASSERT_EQ(elv.size(), 2);
    EXPECT_EQ(elv[0].hp, 200);
}

TEST_F(heldenian_tower_test, damage_objective)
{
    heldenian_.start_heldenian(heldenian_mode::tower_defense);

    bool hit = heldenian_.damage_objective(war_faction::aresden, 1, 50);
    EXPECT_TRUE(hit);

    const auto& ares = heldenian_.get_objectives(war_faction::aresden);
    EXPECT_EQ(ares[0].hp, 150);
}

TEST_F(heldenian_tower_test, damage_invalid_objective)
{
    heldenian_.start_heldenian(heldenian_mode::tower_defense);
    bool hit = heldenian_.damage_objective(war_faction::aresden, 99, 50);
    EXPECT_FALSE(hit);
}

TEST_F(heldenian_tower_test, count_surviving)
{
    heldenian_.start_heldenian(heldenian_mode::tower_defense);

    EXPECT_EQ(heldenian_.count_surviving(war_faction::aresden), 2);

    heldenian_.damage_objective(war_faction::aresden, 1, 999);
    EXPECT_EQ(heldenian_.count_surviving(war_faction::aresden), 1);
    EXPECT_TRUE(heldenian_.is_active()); // Still 1 left
}

TEST_F(heldenian_tower_test, all_destroyed_triggers_victory)
{
    heldenian_.start_heldenian(heldenian_mode::tower_defense);

    heldenian_.damage_objective(war_faction::aresden, 1, 999);
    EXPECT_TRUE(heldenian_.is_active());

    heldenian_.damage_objective(war_faction::aresden, 2, 999);
    EXPECT_FALSE(heldenian_.is_active()); // Elvine wins
}

TEST_F(heldenian_tower_test, time_limit_winner_by_tower_count)
{
    heldenian_.start_heldenian(heldenian_mode::tower_defense);

    // Destroy 1 aresden tower (aresden has 1 left, elvine has 2)
    heldenian_.damage_objective(war_faction::aresden, 1, 999);
    EXPECT_TRUE(heldenian_.is_active());

    // Fast-forward past time limit
    heldenian_.update(2701.0f);
    EXPECT_FALSE(heldenian_.is_active()); // Elvine should win (2 > 1)
}

// ========== Player State Tests ==========

TEST_F(heldenian_tower_test, join_heldenian)
{
    heldenian_.start_heldenian(heldenian_mode::tower_defense);

    player_id pid(1);
    auto result = heldenian_.join_heldenian(pid, war_faction::aresden);
    EXPECT_EQ(result, heldenian_result::success);
    EXPECT_TRUE(heldenian_.is_in_heldenian(pid));
    EXPECT_EQ(heldenian_.participant_count(), 1);
}

TEST_F(heldenian_tower_test, join_requires_active)
{
    auto result = heldenian_.join_heldenian(player_id(1), war_faction::aresden);
    EXPECT_EQ(result, heldenian_result::not_active);
}

TEST_F(heldenian_tower_test, join_requires_faction)
{
    heldenian_.start_heldenian(heldenian_mode::tower_defense);
    auto result = heldenian_.join_heldenian(player_id(1), war_faction::neutral);
    EXPECT_EQ(result, heldenian_result::not_in_faction);
}

TEST_F(heldenian_tower_test, leave_heldenian)
{
    heldenian_.start_heldenian(heldenian_mode::tower_defense);

    player_id pid(1);
    heldenian_.join_heldenian(pid, war_faction::aresden);
    heldenian_.leave_heldenian(pid);
    EXPECT_FALSE(heldenian_.is_in_heldenian(pid));
}

TEST_F(heldenian_tower_test, cleanup_on_end)
{
    heldenian_.start_heldenian(heldenian_mode::tower_defense);
    heldenian_.join_heldenian(player_id(1), war_faction::aresden);

    heldenian_.end_heldenian(war_faction::neutral);
    EXPECT_EQ(heldenian_.participant_count(), 0);
    EXPECT_TRUE(heldenian_.get_objectives(war_faction::aresden).empty());
}

// ========== Door Defense Mode Tests ==========

class heldenian_door_test : public ::testing::Test
{
protected:
    void SetUp() override
    {
        war_sys_.initialize();
        heldenian_.initialize();
        heldenian_.set_dependencies(&war_sys_, nullptr, nullptr, nullptr, nullptr);

        heldenian_config cfg;
        cfg.enabled = true;
        cfg.timing.duration_seconds = 2700;

        heldenian_objective d1{.id = 1, .max_hp = 300};
        heldenian_objective d2{.id = 2, .max_hp = 300};
        cfg.defender_doors = {d1, d2};

        heldenian_.set_config(cfg);
    }

    void TearDown() override
    {
        heldenian_.shutdown();
        war_sys_.shutdown();
    }

    war_system war_sys_;
    heldenian_system heldenian_;
};

TEST_F(heldenian_door_test, start_door_mode)
{
    auto result = heldenian_.start_heldenian(heldenian_mode::door_defense);
    EXPECT_TRUE(result.is_ok());
    EXPECT_EQ(heldenian_.current_mode(), heldenian_mode::door_defense);
}

TEST_F(heldenian_door_test, doors_belong_to_aresden)
{
    heldenian_.start_heldenian(heldenian_mode::door_defense);

    const auto& doors = heldenian_.get_objectives(war_faction::aresden);
    ASSERT_EQ(doors.size(), 2);
    EXPECT_EQ(doors[0].hp, 300);
    EXPECT_EQ(doors[0].faction, war_faction::aresden);

    // Elvine (attacker) has no objectives
    const auto& elv = heldenian_.get_objectives(war_faction::elvine);
    EXPECT_TRUE(elv.empty());
}

TEST_F(heldenian_door_test, all_doors_destroyed_attacker_wins)
{
    heldenian_.start_heldenian(heldenian_mode::door_defense);

    heldenian_.damage_objective(war_faction::aresden, 1, 999);
    EXPECT_TRUE(heldenian_.is_active());

    heldenian_.damage_objective(war_faction::aresden, 2, 999);
    EXPECT_FALSE(heldenian_.is_active()); // Elvine (attacker) wins
}

TEST_F(heldenian_door_test, time_limit_defender_wins)
{
    heldenian_.start_heldenian(heldenian_mode::door_defense);

    // Damage but don't destroy
    heldenian_.damage_objective(war_faction::aresden, 1, 100);

    // Time runs out — defender (Aresden) wins
    heldenian_.update(2701.0f);
    EXPECT_FALSE(heldenian_.is_active());
}

// ========== Protocol Message Tests ==========

TEST(heldenian_protocol_test, message_types)
{
    EXPECT_EQ(hb::network::to_string(hb::network::json_message_type::heldenian_started), "heldenian_started");
    EXPECT_EQ(hb::network::to_string(hb::network::json_message_type::heldenian_ended), "heldenian_ended");
    EXPECT_EQ(hb::network::to_string(hb::network::json_message_type::heldenian_status_update),
              "heldenian_status_update");
}

TEST(heldenian_protocol_test, message_type_roundtrip)
{
    auto parsed = hb::network::parse_message_type("heldenian_started");
    EXPECT_EQ(parsed, hb::network::json_message_type::heldenian_started);

    parsed = hb::network::parse_message_type("heldenian_ended");
    EXPECT_EQ(parsed, hb::network::json_message_type::heldenian_ended);

    parsed = hb::network::parse_message_type("heldenian_status_update");
    EXPECT_EQ(parsed, hb::network::json_message_type::heldenian_status_update);
}

// ========== A2: Empty Objectives Validation ==========

TEST(heldenian_empty_config_test, start_tower_mode_with_no_towers_fails)
{
    war_system war_sys;
    heldenian_system heldenian;
    war_sys.initialize();
    heldenian.initialize();
    heldenian.set_dependencies(&war_sys, nullptr, nullptr, nullptr, nullptr);

    heldenian_config cfg;
    cfg.enabled = true;
    // No towers configured
    cfg.aresden_towers.clear();
    cfg.elvine_towers.clear();
    heldenian.set_config(cfg);

    auto result = heldenian.start_heldenian(heldenian_mode::tower_defense);
    EXPECT_TRUE(result.is_err());
    EXPECT_EQ(result.error(), heldenian_result::config_error);
    EXPECT_FALSE(heldenian.is_active());

    heldenian.shutdown();
    war_sys.shutdown();
}

TEST(heldenian_empty_config_test, start_door_mode_with_no_doors_fails)
{
    war_system war_sys;
    heldenian_system heldenian;
    war_sys.initialize();
    heldenian.initialize();
    heldenian.set_dependencies(&war_sys, nullptr, nullptr, nullptr, nullptr);

    heldenian_config cfg;
    cfg.enabled = true;
    cfg.defender_doors.clear();
    heldenian.set_config(cfg);

    auto result = heldenian.start_heldenian(heldenian_mode::door_defense);
    EXPECT_TRUE(result.is_err());
    EXPECT_EQ(result.error(), heldenian_result::config_error);
    EXPECT_FALSE(heldenian.is_active());

    heldenian.shutdown();
    war_sys.shutdown();
}

// ========== A4: Kill/Death Tracking ==========

TEST_F(heldenian_tower_test, record_kill_updates_both)
{
    heldenian_.start_heldenian(heldenian_mode::tower_defense);

    player_id killer(1);
    player_id victim(2);
    heldenian_.join_heldenian(killer, war_faction::aresden);
    heldenian_.join_heldenian(victim, war_faction::elvine);

    heldenian_.record_kill(killer, victim, war_faction::elvine);

    auto* killer_data = heldenian_.get_player_data(killer);
    auto* victim_data = heldenian_.get_player_data(victim);
    EXPECT_EQ(killer_data->kills, 1);
    EXPECT_EQ(killer_data->deaths, 0);
    EXPECT_EQ(victim_data->kills, 0);
    EXPECT_EQ(victim_data->deaths, 1);
}

TEST_F(heldenian_tower_test, record_kill_nonparticipant_ignored)
{
    heldenian_.start_heldenian(heldenian_mode::tower_defense);

    player_id killer(1);
    heldenian_.join_heldenian(killer, war_faction::aresden);

    // victim(2) is not in the war
    heldenian_.record_kill(killer, player_id(2), war_faction::elvine);

    auto* killer_data = heldenian_.get_player_data(killer);
    EXPECT_EQ(killer_data->kills, 1);
    EXPECT_EQ(heldenian_.get_player_data(player_id(2)), nullptr);
}

TEST_F(heldenian_tower_test, record_kill_accumulates)
{
    heldenian_.start_heldenian(heldenian_mode::tower_defense);

    player_id killer(1);
    player_id victim(2);
    heldenian_.join_heldenian(killer, war_faction::aresden);
    heldenian_.join_heldenian(victim, war_faction::elvine);

    heldenian_.record_kill(killer, victim, war_faction::elvine);
    heldenian_.record_kill(killer, victim, war_faction::elvine);
    heldenian_.record_kill(killer, victim, war_faction::elvine);

    auto* killer_data = heldenian_.get_player_data(killer);
    EXPECT_EQ(killer_data->kills, 3);
    auto* victim_data = heldenian_.get_player_data(victim);
    EXPECT_EQ(victim_data->deaths, 3);
}

// ========== A5: Configurable Defending Faction ==========

TEST_F(heldenian_door_test, elvine_defends_doors)
{
    auto result = heldenian_.start_heldenian(heldenian_mode::door_defense, war_faction::elvine);
    EXPECT_TRUE(result.is_ok());
    EXPECT_EQ(heldenian_.defending_faction(), war_faction::elvine);

    // Doors should be in elvine's objectives
    const auto& elv = heldenian_.get_objectives(war_faction::elvine);
    EXPECT_EQ(elv.size(), 2);
    EXPECT_EQ(elv[0].faction, war_faction::elvine);

    // Aresden should have no objectives
    const auto& ares = heldenian_.get_objectives(war_faction::aresden);
    EXPECT_TRUE(ares.empty());
}

TEST_F(heldenian_door_test, aresden_defends_doors)
{
    auto result = heldenian_.start_heldenian(heldenian_mode::door_defense, war_faction::aresden);
    EXPECT_TRUE(result.is_ok());
    EXPECT_EQ(heldenian_.defending_faction(), war_faction::aresden);

    const auto& ares = heldenian_.get_objectives(war_faction::aresden);
    EXPECT_EQ(ares.size(), 2);
}

TEST_F(heldenian_door_test, default_defender_from_config)
{
    // Config default is aresden
    auto result = heldenian_.start_heldenian(heldenian_mode::door_defense);
    EXPECT_TRUE(result.is_ok());
    EXPECT_EQ(heldenian_.defending_faction(), war_faction::aresden);
}

TEST_F(heldenian_door_test, elvine_defending_time_limit_victory)
{
    heldenian_.start_heldenian(heldenian_mode::door_defense, war_faction::elvine);

    // Damage but don't destroy
    heldenian_.damage_objective(war_faction::elvine, 1, 100);

    // Time runs out — defender (Elvine) wins
    heldenian_.update(2701.0f);
    EXPECT_FALSE(heldenian_.is_active());
}

TEST_F(heldenian_door_test, elvine_defending_all_destroyed_aresden_wins)
{
    heldenian_.start_heldenian(heldenian_mode::door_defense, war_faction::elvine);

    // Destroy all elvine doors
    heldenian_.damage_objective(war_faction::elvine, 1, 999);
    EXPECT_TRUE(heldenian_.is_active());

    heldenian_.damage_objective(war_faction::elvine, 2, 999);
    EXPECT_FALSE(heldenian_.is_active()); // Aresden (attacker) wins
}

// ========== NPC Spawning: Type Tests ==========

TEST(heldenian_types_test, objective_has_npc_fields)
{
    heldenian_objective obj;
    EXPECT_EQ(obj.npc_type, 0);
    EXPECT_EQ(obj.direction, 0);
    EXPECT_FALSE(obj.eid.is_valid());
}

TEST(heldenian_types_test, teleport_coords_default)
{
    heldenian_teleport_coords tc;
    EXPECT_TRUE(tc.map_name.empty());
    EXPECT_EQ(tc.x, 0);
    EXPECT_EQ(tc.y, 0);
}

// ========== NPC Spawning: Teleport Tests ==========

TEST_F(heldenian_tower_test, teleport_tower_mode_aresden)
{
    heldenian_.start_heldenian(heldenian_mode::tower_defense);

    auto dest = heldenian_.get_teleport_destination(war_faction::aresden);
    ASSERT_TRUE(dest.has_value());
    EXPECT_EQ(dest->map_name, "BtField");
    EXPECT_EQ(dest->x, 68);
    EXPECT_EQ(dest->y, 225);
}

TEST_F(heldenian_tower_test, teleport_tower_mode_elvine)
{
    heldenian_.start_heldenian(heldenian_mode::tower_defense);

    auto dest = heldenian_.get_teleport_destination(war_faction::elvine);
    ASSERT_TRUE(dest.has_value());
    EXPECT_EQ(dest->map_name, "BtField");
    EXPECT_EQ(dest->x, 202);
    EXPECT_EQ(dest->y, 70);
}

TEST_F(heldenian_door_test, teleport_door_mode_defender)
{
    heldenian_.start_heldenian(heldenian_mode::door_defense);

    auto dest = heldenian_.get_teleport_destination(war_faction::aresden); // default defender
    ASSERT_TRUE(dest.has_value());
    EXPECT_EQ(dest->map_name, "HRampart");
    EXPECT_EQ(dest->x, 81);
    EXPECT_EQ(dest->y, 42);
}

TEST_F(heldenian_door_test, teleport_door_mode_attacker)
{
    heldenian_.start_heldenian(heldenian_mode::door_defense);

    auto dest = heldenian_.get_teleport_destination(war_faction::elvine); // attacker
    ASSERT_TRUE(dest.has_value());
    EXPECT_EQ(dest->map_name, "HRampart");
    EXPECT_EQ(dest->x, 156);
    EXPECT_EQ(dest->y, 153);
}

TEST_F(heldenian_tower_test, teleport_inactive_returns_nullopt)
{
    auto dest = heldenian_.get_teleport_destination(war_faction::aresden);
    EXPECT_FALSE(dest.has_value());
}

TEST_F(heldenian_tower_test, teleport_neutral_returns_nullopt)
{
    heldenian_.start_heldenian(heldenian_mode::tower_defense);
    auto dest = heldenian_.get_teleport_destination(war_faction::neutral);
    EXPECT_FALSE(dest.has_value());
}

// ========== NPC Spawning: Spawn/Despawn Tests ==========

class heldenian_npc_test : public ::testing::Test
{
protected:
    void SetUp() override
    {
        npc_sys_.initialize();
        war_sys_.initialize();
        heldenian_.initialize();
        heldenian_.set_dependencies(&war_sys_, nullptr, nullptr, &npc_sys_, nullptr);
    }

    void TearDown() override
    {
        heldenian_.shutdown();
        npc_sys_.shutdown();
        war_sys_.shutdown();
    }

    hb::npc::npc_system npc_sys_;
    war_system war_sys_;
    heldenian_system heldenian_;
};

TEST_F(heldenian_npc_test, tower_mode_spawns_npcs)
{
    heldenian_config cfg;
    cfg.enabled = true;

    heldenian_objective t1{.id = 1, .x = 10, .y = 20, .max_hp = 500, .npc_type = 87};
    heldenian_objective t2{.id = 2, .x = 30, .y = 40, .max_hp = 500, .npc_type = 89};
    cfg.aresden_towers = {t1};
    cfg.elvine_towers = {t2};

    heldenian_.set_config(cfg);
    auto result = heldenian_.start_heldenian(heldenian_mode::tower_defense);
    EXPECT_TRUE(result.is_ok());

    const auto& ares = heldenian_.get_objectives(war_faction::aresden);
    ASSERT_EQ(ares.size(), 1);
    EXPECT_TRUE(ares[0].eid.is_valid());
    EXPECT_TRUE(npc_sys_.npc_exists(ares[0].eid));

    const auto& elv = heldenian_.get_objectives(war_faction::elvine);
    ASSERT_EQ(elv.size(), 1);
    EXPECT_TRUE(elv[0].eid.is_valid());
    EXPECT_TRUE(npc_sys_.npc_exists(elv[0].eid));
}

TEST_F(heldenian_npc_test, door_mode_spawns_defender_npcs)
{
    heldenian_config cfg;
    cfg.enabled = true;

    heldenian_objective d1{.id = 1, .x = 50, .y = 60, .max_hp = 300, .npc_type = 91, .direction = 3};
    heldenian_objective d2{.id = 2, .x = 70, .y = 80, .max_hp = 300, .npc_type = 91, .direction = 1};
    cfg.defender_doors = {d1, d2};

    heldenian_.set_config(cfg);
    auto result = heldenian_.start_heldenian(heldenian_mode::door_defense);
    EXPECT_TRUE(result.is_ok());

    const auto& doors = heldenian_.get_objectives(war_faction::aresden);
    ASSERT_EQ(doors.size(), 2);
    EXPECT_TRUE(doors[0].eid.is_valid());
    EXPECT_TRUE(doors[1].eid.is_valid());
    EXPECT_TRUE(npc_sys_.npc_exists(doors[0].eid));
    EXPECT_TRUE(npc_sys_.npc_exists(doors[1].eid));
}

TEST_F(heldenian_npc_test, cleanup_despawns_surviving_npcs)
{
    heldenian_config cfg;
    cfg.enabled = true;

    heldenian_objective t1{.id = 1, .x = 10, .y = 20, .max_hp = 500, .npc_type = 87};
    cfg.aresden_towers = {t1};

    heldenian_objective t2{.id = 1, .x = 30, .y = 40, .max_hp = 500, .npc_type = 89};
    cfg.elvine_towers = {t2};

    heldenian_.set_config(cfg);
    heldenian_.start_heldenian(heldenian_mode::tower_defense);

    const auto& ares = heldenian_.get_objectives(war_faction::aresden);
    auto eid = ares[0].eid;
    EXPECT_TRUE(npc_sys_.npc_exists(eid));

    heldenian_.end_heldenian(war_faction::neutral);
    EXPECT_FALSE(npc_sys_.npc_exists(eid));
}

// ========== NPC Spawning: NPC Death → Objective Destruction ==========

TEST_F(heldenian_npc_test, npc_death_destroys_objective)
{
    heldenian_config cfg;
    cfg.enabled = true;

    heldenian_objective t1{.id = 1, .x = 10, .y = 20, .max_hp = 500, .npc_type = 87};
    heldenian_objective t2{.id = 2, .x = 30, .y = 40, .max_hp = 500, .npc_type = 89};
    cfg.aresden_towers = {t1, t2};

    heldenian_objective e1{.id = 1, .x = 50, .y = 60, .max_hp = 500, .npc_type = 87};
    cfg.elvine_towers = {e1};

    heldenian_.set_config(cfg);
    heldenian_.start_heldenian(heldenian_mode::tower_defense);

    const auto& ares = heldenian_.get_objectives(war_faction::aresden);
    auto eid1 = ares[0].eid;

    heldenian_.on_npc_killed(eid1);

    EXPECT_EQ(heldenian_.count_surviving(war_faction::aresden), 1);
    EXPECT_TRUE(heldenian_.is_active());
}

TEST_F(heldenian_npc_test, all_towers_killed_triggers_victory)
{
    heldenian_config cfg;
    cfg.enabled = true;

    heldenian_objective t1{.id = 1, .x = 10, .y = 20, .max_hp = 500, .npc_type = 87};
    cfg.aresden_towers = {t1};

    heldenian_objective e1{.id = 1, .x = 50, .y = 60, .max_hp = 500, .npc_type = 87};
    cfg.elvine_towers = {e1};

    heldenian_.set_config(cfg);
    heldenian_.start_heldenian(heldenian_mode::tower_defense);

    const auto& ares = heldenian_.get_objectives(war_faction::aresden);
    heldenian_.on_npc_killed(ares[0].eid);

    EXPECT_FALSE(heldenian_.is_active());
}

TEST_F(heldenian_npc_test, npc_killed_with_invalid_eid_ignored)
{
    heldenian_config cfg;
    cfg.enabled = true;

    heldenian_objective t1{.id = 1, .x = 10, .y = 20, .max_hp = 500, .npc_type = 87};
    cfg.aresden_towers = {t1};
    heldenian_objective e1{.id = 1, .x = 50, .y = 60, .max_hp = 500, .npc_type = 87};
    cfg.elvine_towers = {e1};

    heldenian_.set_config(cfg);
    heldenian_.start_heldenian(heldenian_mode::tower_defense);

    heldenian_.on_npc_killed(hb::entity::entity(99999));
    EXPECT_TRUE(heldenian_.is_active());
    EXPECT_EQ(heldenian_.count_surviving(war_faction::aresden), 1);
}

TEST_F(heldenian_npc_test, door_npc_killed_triggers_victory)
{
    heldenian_config cfg;
    cfg.enabled = true;

    heldenian_objective d1{.id = 1, .x = 50, .y = 60, .max_hp = 300, .npc_type = 91};
    cfg.defender_doors = {d1};

    heldenian_.set_config(cfg);
    heldenian_.start_heldenian(heldenian_mode::door_defense);

    const auto& doors = heldenian_.get_objectives(war_faction::aresden);
    heldenian_.on_npc_killed(doors[0].eid);

    EXPECT_FALSE(heldenian_.is_active());
}

TEST_F(heldenian_npc_test, start_without_npc_system_still_works)
{
    heldenian_system h2;
    h2.initialize();
    h2.set_dependencies(&war_sys_, nullptr, nullptr, nullptr, nullptr);

    heldenian_config cfg;
    cfg.enabled = true;
    heldenian_objective t1{.id = 1, .max_hp = 200, .npc_type = 87};
    cfg.aresden_towers = {t1};
    heldenian_objective t2{.id = 1, .max_hp = 200, .npc_type = 89};
    cfg.elvine_towers = {t2};
    h2.set_config(cfg);

    auto result = h2.start_heldenian(heldenian_mode::tower_defense);
    EXPECT_TRUE(result.is_ok());
    EXPECT_TRUE(h2.is_active());

    const auto& ares = h2.get_objectives(war_faction::aresden);
    EXPECT_FALSE(ares[0].eid.is_valid());

    h2.shutdown();
}

// ========== NPC Spawning: Evacuation Tests ==========

TEST(heldenian_evacuate_test, evacuate_calls_callback_for_map)
{
    war_system war_sys;
    heldenian_system heldenian;
    war_sys.initialize();
    heldenian.initialize();
    heldenian.set_dependencies(&war_sys, nullptr, nullptr, nullptr, nullptr);

    std::vector<player_id> evacuated;
    heldenian.set_evacuate_fn([&](player_id pid, [[maybe_unused]] const std::string& map) { evacuated.push_back(pid); });

    heldenian_config cfg;
    cfg.enabled = true;
    heldenian_objective t1{.id = 1, .max_hp = 200, .npc_type = 87};
    cfg.aresden_towers = {t1};
    heldenian_objective t2{.id = 1, .max_hp = 200, .npc_type = 89};
    cfg.elvine_towers = {t2};
    heldenian.set_config(cfg);

    // Evacuate a map — since no player_system, callback should not fire
    heldenian.evacuate_map("BtField");
    EXPECT_TRUE(evacuated.empty());

    heldenian.shutdown();
    war_sys.shutdown();
}

// ========== Faction Death Counters & Tiebreaker ==========

TEST_F(heldenian_tower_test, faction_deaths_increment_on_kill)
{
    heldenian_.start_heldenian(heldenian_mode::tower_defense);

    player_id killer(1);
    player_id victim(2);
    heldenian_.join_heldenian(killer, war_faction::aresden);
    heldenian_.join_heldenian(victim, war_faction::elvine);

    EXPECT_EQ(heldenian_.aresden_deaths(), 0);
    EXPECT_EQ(heldenian_.elvine_deaths(), 0);

    heldenian_.record_kill(killer, victim, war_faction::elvine);
    EXPECT_EQ(heldenian_.elvine_deaths(), 1);
    EXPECT_EQ(heldenian_.aresden_deaths(), 0);

    // Kill an aresden player
    player_id killer2(3);
    player_id victim2(4);
    heldenian_.join_heldenian(killer2, war_faction::elvine);
    heldenian_.join_heldenian(victim2, war_faction::aresden);
    heldenian_.record_kill(killer2, victim2, war_faction::aresden);
    EXPECT_EQ(heldenian_.aresden_deaths(), 1);
    EXPECT_EQ(heldenian_.elvine_deaths(), 1);
}

TEST_F(heldenian_tower_test, death_counters_reset_on_start)
{
    heldenian_.start_heldenian(heldenian_mode::tower_defense);

    player_id p1(1), p2(2);
    heldenian_.join_heldenian(p1, war_faction::aresden);
    heldenian_.join_heldenian(p2, war_faction::elvine);
    heldenian_.record_kill(p1, p2, war_faction::elvine);
    EXPECT_EQ(heldenian_.elvine_deaths(), 1);

    heldenian_.end_heldenian(war_faction::aresden);

    // Start new war — counters should be zero
    heldenian_.start_heldenian(heldenian_mode::tower_defense);
    EXPECT_EQ(heldenian_.aresden_deaths(), 0);
    EXPECT_EQ(heldenian_.elvine_deaths(), 0);
}

TEST_F(heldenian_tower_test, tiebreaker_fewer_deaths_wins)
{
    heldenian_.start_heldenian(heldenian_mode::tower_defense);

    player_id a1(1), a2(2), e1(3), e2(4);
    heldenian_.join_heldenian(a1, war_faction::aresden);
    heldenian_.join_heldenian(a2, war_faction::aresden);
    heldenian_.join_heldenian(e1, war_faction::elvine);
    heldenian_.join_heldenian(e2, war_faction::elvine);

    // Aresden has 2 deaths, elvine has 5
    heldenian_.record_kill(e1, a1, war_faction::aresden);
    heldenian_.record_kill(e1, a2, war_faction::aresden);
    heldenian_.record_kill(a1, e1, war_faction::elvine);
    heldenian_.record_kill(a1, e2, war_faction::elvine);
    heldenian_.record_kill(a2, e1, war_faction::elvine);
    heldenian_.record_kill(a2, e2, war_faction::elvine);
    heldenian_.record_kill(a1, e1, war_faction::elvine);

    EXPECT_EQ(heldenian_.aresden_deaths(), 2);
    EXPECT_EQ(heldenian_.elvine_deaths(), 5);

    // Both factions still have 2 towers each (equal)
    EXPECT_EQ(heldenian_.count_surviving(war_faction::aresden), 2);
    EXPECT_EQ(heldenian_.count_surviving(war_faction::elvine), 2);

    // Time runs out — aresden should win (fewer deaths)
    heldenian_.update(2701.0f);
    EXPECT_FALSE(heldenian_.is_active());
    EXPECT_EQ(heldenian_.last_winner(), war_faction::aresden);
}

TEST_F(heldenian_tower_test, tiebreaker_complete_tie_previous_winner)
{
    // Set previous winner to elvine
    heldenian_.set_last_winner(war_faction::elvine);

    heldenian_.start_heldenian(heldenian_mode::tower_defense);

    // Equal towers, equal deaths (0 each) — previous winner stays
    heldenian_.update(2701.0f);
    EXPECT_FALSE(heldenian_.is_active());
    EXPECT_EQ(heldenian_.last_winner(), war_faction::elvine);
}

TEST_F(heldenian_tower_test, tiebreaker_complete_tie_no_previous_winner)
{
    // No previous winner (neutral)
    EXPECT_EQ(heldenian_.last_winner(), war_faction::neutral);

    heldenian_.start_heldenian(heldenian_mode::tower_defense);

    // Equal towers, equal deaths — neutral result
    heldenian_.update(2701.0f);
    EXPECT_FALSE(heldenian_.is_active());
    // last_winner_ stays neutral since end_heldenian only sets for non-neutral
    EXPECT_EQ(heldenian_.last_winner(), war_faction::neutral);
}

TEST_F(heldenian_tower_test, last_winner_persists_across_wars)
{
    // First war: aresden wins
    heldenian_.start_heldenian(heldenian_mode::tower_defense);
    heldenian_.damage_objective(war_faction::elvine, 1, 999);
    heldenian_.damage_objective(war_faction::elvine, 2, 999);
    EXPECT_FALSE(heldenian_.is_active());
    EXPECT_EQ(heldenian_.last_winner(), war_faction::aresden);

    // Second war: tiebreaker should use aresden as previous winner
    heldenian_.start_heldenian(heldenian_mode::tower_defense);
    // Equal towers, equal deaths — previous winner (aresden) stays
    heldenian_.update(2701.0f);
    EXPECT_FALSE(heldenian_.is_active());
    EXPECT_EQ(heldenian_.last_winner(), war_faction::aresden);
}

TEST_F(heldenian_tower_test, winner_sets_last_winner)
{
    heldenian_.start_heldenian(heldenian_mode::tower_defense);
    heldenian_.end_heldenian(war_faction::elvine);
    EXPECT_EQ(heldenian_.last_winner(), war_faction::elvine);

    heldenian_.start_heldenian(heldenian_mode::tower_defense);
    heldenian_.end_heldenian(war_faction::aresden);
    EXPECT_EQ(heldenian_.last_winner(), war_faction::aresden);
}

TEST_F(heldenian_tower_test, neutral_winner_does_not_overwrite_last_winner)
{
    heldenian_.set_last_winner(war_faction::aresden);
    heldenian_.start_heldenian(heldenian_mode::tower_defense);
    heldenian_.end_heldenian(war_faction::neutral);
    // Neutral should not overwrite
    EXPECT_EQ(heldenian_.last_winner(), war_faction::aresden);
}
