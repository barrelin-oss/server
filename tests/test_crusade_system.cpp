// test_crusade_system.cpp
// Unit tests for crusade warfare system

#include <gtest/gtest.h>
#include "core/types.h"
#include "war/war_types.h"
#include "war/war_system.h"
#include "war/crusade/crusade_types.h"
#include "war/crusade/crusade_system.h"
#include "war/crusade/mana_system.h"
#include "war/crusade/meteor_handler.h"
#include "effect/effect_system.h"
#include "player/player_system.h"
#include "player/player.h"
#include "social/social_system.h"
#include "social/guild.h"
#include "network/json_protocol.h"
#include "world/world_subsystem.h"
#include "world/map.h"

using hb::player_id;
using namespace hb::war;

// ========== Crusade Types Tests ==========

TEST(crusade_types_test, strike_point_default) {
    strike_point sp;
    EXPECT_EQ(sp.id, 0);
    EXPECT_EQ(sp.hp, 0);
    EXPECT_EQ(sp.max_hp, 100);
    EXPECT_TRUE(sp.is_destroyed());
}

TEST(crusade_types_test, strike_point_alive) {
    strike_point sp;
    sp.hp = 50;
    sp.max_hp = 100;
    EXPECT_FALSE(sp.is_destroyed());
}

TEST(crusade_types_test, strike_point_destroyed_at_zero) {
    strike_point sp;
    sp.hp = 0;
    EXPECT_TRUE(sp.is_destroyed());
}

TEST(crusade_types_test, player_data_default) {
    crusade_player_data data;
    EXPECT_EQ(data.duty, crusade_duty::none);
    EXPECT_EQ(data.construction_points, 0);
    EXPECT_EQ(data.war_contribution, 0);
    EXPECT_EQ(data.crusade_guid, 0);
}

// ========== Crusade System Lifecycle Tests ==========

class crusade_system_test : public ::testing::Test {
protected:
    void SetUp() override {
        war_sys_.initialize();
        crusade_.initialize();
        crusade_.set_dependencies(&war_sys_, nullptr, nullptr, nullptr, nullptr);

        // Set up a default config with strike points
        crusade_config cfg;
        cfg.enabled = true;
        cfg.timing.duration_seconds = 3600;
        cfg.timing.status_broadcast_seconds = 30;

        strike_point sp1;
        sp1.id = 1;
        sp1.max_hp = 100;
        cfg.aresden_strike_points.push_back(sp1);

        strike_point sp1b;
        sp1b.id = 2;
        sp1b.max_hp = 100;
        cfg.aresden_strike_points.push_back(sp1b);

        strike_point sp2;
        sp2.id = 1;
        sp2.max_hp = 100;
        cfg.elvine_strike_points.push_back(sp2);

        strike_point sp2b;
        sp2b.id = 2;
        sp2b.max_hp = 100;
        cfg.elvine_strike_points.push_back(sp2b);

        crusade_.set_config(cfg);
    }

    void TearDown() override {
        crusade_.shutdown();
        war_sys_.shutdown();
    }

    war_system war_sys_;
    crusade_system crusade_;
};

TEST_F(crusade_system_test, starts_inactive) {
    EXPECT_FALSE(crusade_.is_active());
    EXPECT_EQ(crusade_.participant_count(), 0);
}

TEST_F(crusade_system_test, start_crusade_succeeds) {
    auto result = crusade_.start_crusade();
    EXPECT_TRUE(result.is_ok());
    EXPECT_TRUE(crusade_.is_active());
    EXPECT_TRUE(crusade_.current_war_id().is_valid());
    EXPECT_NE(crusade_.crusade_guid(), 0u);
}

TEST_F(crusade_system_test, start_twice_fails) {
    auto r1 = crusade_.start_crusade();
    EXPECT_TRUE(r1.is_ok());

    auto r2 = crusade_.start_crusade();
    EXPECT_TRUE(r2.is_err());
    EXPECT_EQ(r2.error(), crusade_result::already_active);
}

TEST_F(crusade_system_test, end_crusade_succeeds) {
    crusade_.start_crusade();
    auto result = crusade_.end_crusade(war_faction::aresden);
    EXPECT_EQ(result, crusade_result::success);
    EXPECT_FALSE(crusade_.is_active());
}

TEST_F(crusade_system_test, end_when_not_active_fails) {
    auto result = crusade_.end_crusade(war_faction::aresden);
    EXPECT_EQ(result, crusade_result::not_active);
}

TEST_F(crusade_system_test, cancel_cleans_up) {
    crusade_.start_crusade();
    crusade_.cancel_crusade();
    EXPECT_FALSE(crusade_.is_active());
}

TEST_F(crusade_system_test, cancel_when_not_active_is_noop) {
    crusade_.cancel_crusade();  // Should not crash
    EXPECT_FALSE(crusade_.is_active());
}

// ========== Strike Point Tests ==========

TEST_F(crusade_system_test, strike_points_restored_on_start) {
    crusade_.start_crusade();

    const auto& aresden = crusade_.get_strike_points(war_faction::aresden);
    ASSERT_EQ(aresden.size(), 2);
    EXPECT_EQ(aresden[0].hp, 100);
    EXPECT_EQ(aresden[0].max_hp, 100);
    EXPECT_EQ(aresden[0].faction, war_faction::aresden);

    const auto& elvine = crusade_.get_strike_points(war_faction::elvine);
    ASSERT_EQ(elvine.size(), 2);
    EXPECT_EQ(elvine[0].hp, 100);
    EXPECT_EQ(elvine[0].faction, war_faction::elvine);
}

TEST_F(crusade_system_test, damage_strike_point) {
    crusade_.start_crusade();

    bool hit = crusade_.damage_strike_point(war_faction::aresden, 1, 30);
    EXPECT_TRUE(hit);

    const auto& aresden = crusade_.get_strike_points(war_faction::aresden);
    EXPECT_EQ(aresden[0].hp, 70);
}

TEST_F(crusade_system_test, damage_strike_point_clamps_to_zero) {
    crusade_.start_crusade();

    crusade_.damage_strike_point(war_faction::aresden, 1, 999);

    const auto& aresden = crusade_.get_strike_points(war_faction::aresden);
    EXPECT_EQ(aresden[0].hp, 0);
    EXPECT_TRUE(aresden[0].is_destroyed());
}

TEST_F(crusade_system_test, damage_invalid_point_returns_false) {
    crusade_.start_crusade();

    bool hit = crusade_.damage_strike_point(war_faction::aresden, 99, 10);
    EXPECT_FALSE(hit);
}

TEST_F(crusade_system_test, damage_already_destroyed_returns_false) {
    crusade_.start_crusade();

    crusade_.damage_strike_point(war_faction::aresden, 1, 999);
    bool hit = crusade_.damage_strike_point(war_faction::aresden, 1, 10);
    EXPECT_FALSE(hit);
}

TEST_F(crusade_system_test, all_strike_points_destroyed) {
    crusade_.start_crusade();

    EXPECT_FALSE(crusade_.all_strike_points_destroyed(war_faction::aresden));

    // Destroy first point — not all destroyed yet
    crusade_.damage_strike_point(war_faction::aresden, 1, 999);
    EXPECT_FALSE(crusade_.all_strike_points_destroyed(war_faction::aresden));
    EXPECT_TRUE(crusade_.is_active());
}

TEST_F(crusade_system_test, destroying_all_strike_points_does_not_end_immediately) {
    // Victory is deferred to meteor result phase (legacy: CalcMeteorStrikeEffectHandler)
    crusade_.start_crusade();

    crusade_.damage_strike_point(war_faction::aresden, 1, 999);
    EXPECT_TRUE(crusade_.is_active());

    crusade_.damage_strike_point(war_faction::aresden, 2, 999);
    // Direct damage no longer triggers victory — deferred to meteor result callback
    EXPECT_TRUE(crusade_.is_active());
    EXPECT_TRUE(crusade_.all_strike_points_destroyed(war_faction::aresden));
}

TEST_F(crusade_system_test, neutral_faction_returns_empty_points) {
    crusade_.start_crusade();
    const auto& points = crusade_.get_strike_points(war_faction::neutral);
    EXPECT_TRUE(points.empty());
}

// ========== Player War State Tests ==========

TEST_F(crusade_system_test, join_crusade) {
    crusade_.start_crusade();

    player_id pid(1);
    auto result = crusade_.join_crusade(pid, war_faction::aresden);
    EXPECT_EQ(result, crusade_result::success);
    EXPECT_TRUE(crusade_.is_in_crusade(pid));
    EXPECT_EQ(crusade_.participant_count(), 1);
}

TEST_F(crusade_system_test, join_requires_active) {
    player_id pid(1);
    auto result = crusade_.join_crusade(pid, war_faction::aresden);
    EXPECT_EQ(result, crusade_result::not_active);
}

TEST_F(crusade_system_test, join_requires_faction) {
    crusade_.start_crusade();

    player_id pid(1);
    auto result = crusade_.join_crusade(pid, war_faction::neutral);
    EXPECT_EQ(result, crusade_result::not_in_faction);
}

TEST_F(crusade_system_test, join_twice_is_noop) {
    crusade_.start_crusade();

    player_id pid(1);
    crusade_.join_crusade(pid, war_faction::aresden);
    auto result = crusade_.join_crusade(pid, war_faction::aresden);
    EXPECT_EQ(result, crusade_result::success);
    EXPECT_EQ(crusade_.participant_count(), 1);  // Still just 1
}

TEST_F(crusade_system_test, leave_crusade) {
    crusade_.start_crusade();

    player_id pid(1);
    crusade_.join_crusade(pid, war_faction::aresden);
    crusade_.leave_crusade(pid);

    EXPECT_FALSE(crusade_.is_in_crusade(pid));
    EXPECT_EQ(crusade_.participant_count(), 0);
}

TEST_F(crusade_system_test, leave_nonexistent_is_noop) {
    crusade_.start_crusade();
    crusade_.leave_crusade(player_id(99));  // Should not crash
}

// ========== Duty Selection Tests ==========

TEST_F(crusade_system_test, select_duty_fighter) {
    crusade_.start_crusade();

    player_id pid(1);
    crusade_.join_crusade(pid, war_faction::aresden);
    auto result = crusade_.select_duty(pid, crusade_duty::fighter);

    EXPECT_EQ(result, crusade_result::success);

    auto* data = crusade_.get_player_data(pid);
    ASSERT_NE(data, nullptr);
    EXPECT_EQ(data->duty, crusade_duty::fighter);
}

TEST_F(crusade_system_test, select_duty_constructor) {
    crusade_.start_crusade();

    player_id pid(1);
    crusade_.join_crusade(pid, war_faction::elvine);
    auto result = crusade_.select_duty(pid, crusade_duty::constructor);

    EXPECT_EQ(result, crusade_result::success);
}

TEST_F(crusade_system_test, select_duty_commander) {
    crusade_.start_crusade();

    player_id pid(1);
    crusade_.join_crusade(pid, war_faction::aresden);
    auto result = crusade_.select_duty(pid, crusade_duty::commander);

    EXPECT_EQ(result, crusade_result::success);
}

TEST_F(crusade_system_test, select_duty_twice_fails) {
    crusade_.start_crusade();

    player_id pid(1);
    crusade_.join_crusade(pid, war_faction::aresden);
    crusade_.select_duty(pid, crusade_duty::fighter);

    auto result = crusade_.select_duty(pid, crusade_duty::constructor);
    EXPECT_EQ(result, crusade_result::already_has_duty);
}

TEST_F(crusade_system_test, select_duty_none_fails) {
    crusade_.start_crusade();

    player_id pid(1);
    crusade_.join_crusade(pid, war_faction::aresden);
    auto result = crusade_.select_duty(pid, crusade_duty::none);

    EXPECT_EQ(result, crusade_result::invalid_duty);
}

TEST_F(crusade_system_test, select_duty_requires_active) {
    auto result = crusade_.select_duty(player_id(1), crusade_duty::fighter);
    EXPECT_EQ(result, crusade_result::not_active);
}

TEST_F(crusade_system_test, select_duty_requires_participation) {
    crusade_.start_crusade();

    auto result = crusade_.select_duty(player_id(99), crusade_duty::fighter);
    EXPECT_EQ(result, crusade_result::not_in_crusade);
}

// ========== Construction Points Tests ==========

TEST_F(crusade_system_test, award_construction_points) {
    crusade_.start_crusade();

    player_id pid(1);
    crusade_.join_crusade(pid, war_faction::aresden);
    crusade_.select_duty(pid, crusade_duty::fighter);

    crusade_.award_construction_points(pid, 100);

    auto* data = crusade_.get_player_data(pid);
    ASSERT_NE(data, nullptr);
    EXPECT_EQ(data->construction_points, 100);
}

TEST_F(crusade_system_test, award_construction_points_accumulates) {
    crusade_.start_crusade();

    player_id pid(1);
    crusade_.join_crusade(pid, war_faction::aresden);

    crusade_.award_construction_points(pid, 50);
    crusade_.award_construction_points(pid, 75);

    auto* data = crusade_.get_player_data(pid);
    EXPECT_EQ(data->construction_points, 125);
}

TEST_F(crusade_system_test, award_contribution) {
    crusade_.start_crusade();

    player_id pid(1);
    crusade_.join_crusade(pid, war_faction::aresden);

    crusade_.award_contribution(pid, 42);

    auto* data = crusade_.get_player_data(pid);
    EXPECT_EQ(data->war_contribution, 42);
}

TEST_F(crusade_system_test, award_to_nonparticipant_is_noop) {
    crusade_.start_crusade();
    crusade_.award_construction_points(player_id(99), 100);
    crusade_.award_contribution(player_id(99), 100);
    // No crash, no effect
}

// ========== Player Data Access Tests ==========

TEST_F(crusade_system_test, get_player_data_returns_null_for_nonparticipant) {
    crusade_.start_crusade();
    EXPECT_EQ(crusade_.get_player_data(player_id(99)), nullptr);
}

TEST_F(crusade_system_test, get_player_data_const_returns_null_for_nonparticipant) {
    crusade_.start_crusade();
    const auto& csys = crusade_;
    EXPECT_EQ(csys.get_player_data(player_id(99)), nullptr);
}

TEST_F(crusade_system_test, player_data_has_correct_faction) {
    crusade_.start_crusade();

    player_id pid(1);
    crusade_.join_crusade(pid, war_faction::elvine);

    auto* data = crusade_.get_player_data(pid);
    ASSERT_NE(data, nullptr);
    EXPECT_EQ(data->faction, war_faction::elvine);
}

TEST_F(crusade_system_test, player_data_has_crusade_guid) {
    crusade_.start_crusade();

    player_id pid(1);
    crusade_.join_crusade(pid, war_faction::aresden);

    auto* data = crusade_.get_player_data(pid);
    EXPECT_EQ(data->crusade_guid, crusade_.crusade_guid());
}

// ========== Update / Time Limit Tests ==========

TEST_F(crusade_system_test, update_when_inactive_does_nothing) {
    crusade_.update(10.0f);  // Should not crash
    EXPECT_FALSE(crusade_.is_active());
}

TEST_F(crusade_system_test, elapsed_seconds_increments) {
    crusade_.start_crusade();

    crusade_.update(5.0f);
    EXPECT_EQ(crusade_.elapsed_seconds(), 5);

    crusade_.update(3.0f);
    EXPECT_EQ(crusade_.elapsed_seconds(), 8);
}

TEST_F(crusade_system_test, crusade_ends_on_time_limit) {
    // Config has 3600s limit
    crusade_.start_crusade();

    // Fast-forward past the limit
    crusade_.update(3601.0f);

    EXPECT_FALSE(crusade_.is_active());
}

// ========== Multiple Strike Points Tests ==========

class crusade_multi_sp_test : public ::testing::Test {
protected:
    void SetUp() override {
        war_sys_.initialize();
        crusade_.initialize();
        crusade_.set_dependencies(&war_sys_, nullptr, nullptr, nullptr, nullptr);

        crusade_config cfg;
        cfg.enabled = true;
        cfg.timing.duration_seconds = 7200;

        strike_point sp1{.id = 1, .max_hp = 50};
        strike_point sp2{.id = 2, .max_hp = 80};
        strike_point sp3{.id = 3, .max_hp = 120};
        cfg.aresden_strike_points = {sp1, sp2, sp3};

        strike_point esp1{.id = 1, .max_hp = 100};
        cfg.elvine_strike_points = {esp1};

        crusade_.set_config(cfg);
    }

    void TearDown() override {
        crusade_.shutdown();
        war_sys_.shutdown();
    }

    war_system war_sys_;
    crusade_system crusade_;
};

TEST_F(crusade_multi_sp_test, multiple_strike_points_loaded) {
    crusade_.start_crusade();

    const auto& ares = crusade_.get_strike_points(war_faction::aresden);
    EXPECT_EQ(ares.size(), 3);
    EXPECT_EQ(ares[0].max_hp, 50);
    EXPECT_EQ(ares[1].max_hp, 80);
    EXPECT_EQ(ares[2].max_hp, 120);
}

TEST_F(crusade_multi_sp_test, partial_destruction_not_victory) {
    crusade_.start_crusade();

    crusade_.damage_strike_point(war_faction::aresden, 1, 999);
    EXPECT_TRUE(crusade_.is_active());  // Still 2 more points

    crusade_.damage_strike_point(war_faction::aresden, 2, 999);
    EXPECT_TRUE(crusade_.is_active());  // Still 1 more
}

TEST_F(crusade_multi_sp_test, all_destroyed_does_not_trigger_immediate_victory) {
    // Victory deferred to meteor result phase
    crusade_.start_crusade();

    crusade_.damage_strike_point(war_faction::aresden, 1, 999);
    crusade_.damage_strike_point(war_faction::aresden, 2, 999);
    crusade_.damage_strike_point(war_faction::aresden, 3, 999);

    // All destroyed but crusade still active — victory checked in meteor result callback
    EXPECT_TRUE(crusade_.is_active());
    EXPECT_TRUE(crusade_.all_strike_points_destroyed(war_faction::aresden));
}

// ========== Protocol Message Tests ==========

TEST(crusade_protocol_test, select_duty_response_success) {
    auto msg = hb::network::make_select_duty_response(42, true, 1, 500);
    EXPECT_EQ(msg.type, hb::network::json_message_type::select_duty_response);
    EXPECT_EQ(msg.seq, 42u);
    EXPECT_TRUE(msg.data["success"].get<bool>());
    EXPECT_EQ(msg.data["duty"].get<int>(), 1);
    EXPECT_EQ(msg.data["construction_points"].get<int>(), 500);
}

TEST(crusade_protocol_test, select_duty_response_failure) {
    auto msg = hb::network::make_select_duty_response(10, false, 0, 0, "already_has_duty");
    EXPECT_EQ(msg.type, hb::network::json_message_type::select_duty_response);
    EXPECT_FALSE(msg.data["success"].get<bool>());
    EXPECT_EQ(msg.data["error"].get<std::string>(), "already_has_duty");
}

TEST(crusade_protocol_test, message_type_to_string) {
    EXPECT_EQ(hb::network::to_string(hb::network::json_message_type::crusade_started), "crusade_started");
    EXPECT_EQ(hb::network::to_string(hb::network::json_message_type::crusade_ended), "crusade_ended");
    EXPECT_EQ(hb::network::to_string(hb::network::json_message_type::crusade_status_update), "crusade_status_update");
    EXPECT_EQ(hb::network::to_string(hb::network::json_message_type::select_duty_request), "select_duty_request");
    EXPECT_EQ(hb::network::to_string(hb::network::json_message_type::select_duty_response), "select_duty_response");
    EXPECT_EQ(hb::network::to_string(hb::network::json_message_type::crusade_strike_point_update), "crusade_strike_point_update");
}

TEST(crusade_protocol_test, message_type_roundtrip) {
    auto parsed = hb::network::parse_message_type("select_duty_request");
    EXPECT_EQ(parsed, hb::network::json_message_type::select_duty_request);

    parsed = hb::network::parse_message_type("crusade_started");
    EXPECT_EQ(parsed, hb::network::json_message_type::crusade_started);

    parsed = hb::network::parse_message_type("crusade_strike_point_update");
    EXPECT_EQ(parsed, hb::network::json_message_type::crusade_strike_point_update);
}

// ========== Guid Uniqueness Tests ==========

TEST_F(crusade_system_test, each_crusade_gets_unique_guid) {
    crusade_.start_crusade();
    auto guid1 = crusade_.crusade_guid();
    crusade_.end_crusade(war_faction::neutral);

    crusade_.set_last_crusade_day(-1);
    crusade_.start_crusade();
    auto guid2 = crusade_.crusade_guid();

    EXPECT_NE(guid1, guid2);
}

// ========== Cleanup Tests ==========

TEST_F(crusade_system_test, end_crusade_clears_players) {
    crusade_.start_crusade();
    crusade_.join_crusade(player_id(1), war_faction::aresden);
    crusade_.join_crusade(player_id(2), war_faction::elvine);
    EXPECT_EQ(crusade_.participant_count(), 2);

    crusade_.end_crusade(war_faction::neutral);
    EXPECT_EQ(crusade_.participant_count(), 0);
}

TEST_F(crusade_system_test, end_crusade_clears_state) {
    crusade_.start_crusade();
    EXPECT_FALSE(crusade_.get_strike_points(war_faction::aresden).empty());

    crusade_.end_crusade(war_faction::neutral);
    EXPECT_TRUE(crusade_.get_strike_points(war_faction::aresden).empty());
    EXPECT_EQ(crusade_.elapsed_seconds(), 0);
}

// ========== Mana System Tests ==========

class mana_system_test : public ::testing::Test {
protected:
    void SetUp() override {
        mana_config cfg;
        cfg.collector_scan_radius = 5;
        cfg.collector_harvest_rate = 3;
        cfg.collector_mp_restore = 5;
        cfg.gmg_mana_threshold = 15;
        cfg.gmg_charges_for_meteor = 1;
        cfg.tick_interval_seconds = 5.0f;
        mana_.set_config(cfg);
    }

    mana_system mana_;
};

TEST_F(mana_system_test, initial_state_is_zero) {
    EXPECT_EQ(mana_.aresden_mana(), 0);
    EXPECT_EQ(mana_.elvine_mana(), 0);
    EXPECT_EQ(mana_.get_state(war_faction::aresden).total_mana_collected, 0);
    EXPECT_EQ(mana_.get_state(war_faction::aresden).gmg_charge, 0);
    EXPECT_EQ(mana_.get_state(war_faction::aresden).meteors_fired, 0);
}

TEST_F(mana_system_test, tick_with_zero_collectors_does_nothing) {
    mana_.tick_faction_mana(war_faction::aresden, 0, 5);
    EXPECT_EQ(mana_.aresden_mana(), 0);
}

TEST_F(mana_system_test, tick_collects_mana_from_stones) {
    // 1 collector, 3 stones in range → 3 * harvest_rate(3) = 9 mana
    mana_.tick_faction_mana(war_faction::aresden, 1, 3);
    EXPECT_EQ(mana_.aresden_mana(), 9);
    EXPECT_EQ(mana_.get_state(war_faction::aresden).total_mana_collected, 9);
}

TEST_F(mana_system_test, tick_accumulates_mana) {
    mana_.tick_faction_mana(war_faction::elvine, 1, 2);  // 2 * 3 = 6
    mana_.tick_faction_mana(war_faction::elvine, 1, 2);  // 2 * 3 = 6
    EXPECT_EQ(mana_.elvine_mana(), 12);
    EXPECT_EQ(mana_.get_state(war_faction::elvine).total_mana_collected, 12);
}

TEST_F(mana_system_test, gmg_charges_at_threshold) {
    // Threshold is 15. Collect 15 mana → 1 charge, pool becomes 0
    mana_.tick_faction_mana(war_faction::aresden, 1, 5);  // 5 * 3 = 15
    EXPECT_EQ(mana_.aresden_mana(), 0);  // Consumed by GMG
    // charge was consumed to fire meteor (charges_for_meteor=1)
    EXPECT_EQ(mana_.get_state(war_faction::aresden).gmg_charge, 0);
}

TEST_F(mana_system_test, meteor_fires_when_charges_accumulated) {
    int meteor_count = 0;
    war_faction meteor_faction = war_faction::neutral;
    mana_.set_meteor_trigger([&](war_faction f) {
        meteor_count++;
        meteor_faction = f;
    });

    // Collect enough for 1 meteor (15 mana = 1 charge = 1 meteor)
    mana_.tick_faction_mana(war_faction::aresden, 1, 5);  // 15 mana
    EXPECT_EQ(meteor_count, 1);
    EXPECT_EQ(meteor_faction, war_faction::aresden);
    EXPECT_EQ(mana_.get_state(war_faction::aresden).meteors_fired, 1);
}

TEST_F(mana_system_test, single_charge_per_threshold_crossing) {
    int meteor_count = 0;
    mana_.set_meteor_trigger([&](war_faction) { meteor_count++; });

    // Collect 45 mana in one tick → only 1 charge (pool resets to 0)
    mana_.tick_faction_mana(war_faction::elvine, 1, 15);  // 15 * 3 = 45
    EXPECT_EQ(meteor_count, 1);  // charges_for_meteor=1, so 1 meteor
    EXPECT_EQ(mana_.get_state(war_faction::elvine).meteors_fired, 1);
    EXPECT_EQ(mana_.elvine_mana(), 0);
}

TEST_F(mana_system_test, leftover_mana_after_gmg) {
    mana_.set_meteor_trigger([](war_faction) {});

    // Collect 21 mana → 1 charge used, pool resets to 0 (remainder discarded)
    mana_.tick_faction_mana(war_faction::aresden, 1, 7);  // 7 * 3 = 21
    EXPECT_EQ(mana_.aresden_mana(), 0);  // Legacy: remainder discarded
}

TEST_F(mana_system_test, add_mana_directly) {
    mana_.set_meteor_trigger([](war_faction) {});

    mana_.add_mana(war_faction::elvine, 10);
    EXPECT_EQ(mana_.elvine_mana(), 10);
    EXPECT_EQ(mana_.get_state(war_faction::elvine).total_mana_collected, 10);
}

TEST_F(mana_system_test, add_mana_zero_or_negative_ignored) {
    mana_.add_mana(war_faction::aresden, 0);
    mana_.add_mana(war_faction::aresden, -5);
    EXPECT_EQ(mana_.aresden_mana(), 0);
}

TEST_F(mana_system_test, add_mana_triggers_gmg) {
    int meteor_count = 0;
    mana_.set_meteor_trigger([&](war_faction) { meteor_count++; });

    mana_.add_mana(war_faction::aresden, 15);
    EXPECT_EQ(meteor_count, 1);
}

TEST_F(mana_system_test, reset_clears_all) {
    mana_.set_meteor_trigger([](war_faction) {});

    mana_.add_mana(war_faction::aresden, 10);
    mana_.add_mana(war_faction::elvine, 20);

    mana_.reset();

    EXPECT_EQ(mana_.aresden_mana(), 0);
    EXPECT_EQ(mana_.elvine_mana(), 0);
    EXPECT_EQ(mana_.get_state(war_faction::aresden).total_mana_collected, 0);
    EXPECT_EQ(mana_.get_state(war_faction::elvine).total_mana_collected, 0);
}

TEST_F(mana_system_test, factions_are_independent) {
    mana_.set_meteor_trigger([](war_faction) {});

    mana_.add_mana(war_faction::aresden, 10);
    mana_.add_mana(war_faction::elvine, 5);

    EXPECT_EQ(mana_.aresden_mana(), 10);
    EXPECT_EQ(mana_.elvine_mana(), 5);
}

TEST_F(mana_system_test, no_trigger_callback_is_safe) {
    // No trigger set — collecting 15 mana should not crash
    mana_.tick_faction_mana(war_faction::aresden, 1, 5);  // 15 mana
    EXPECT_EQ(mana_.get_state(war_faction::aresden).meteors_fired, 1);
}

TEST_F(mana_system_test, multi_charge_threshold) {
    // Reconfigure: need 3 charges for 1 meteor
    mana_config cfg = mana_.get_config();
    cfg.gmg_charges_for_meteor = 3;
    mana_.set_config(cfg);

    int meteor_count = 0;
    mana_.set_meteor_trigger([&](war_faction) { meteor_count++; });

    // Collect 15 mana = 1 charge
    mana_.add_mana(war_faction::aresden, 15);
    EXPECT_EQ(meteor_count, 0);  // Need 3 charges
    EXPECT_EQ(mana_.get_state(war_faction::aresden).gmg_charge, 1);

    // Collect 30 mana → pool goes to 30, check_gmg: 30>=15 → pool=0, charge=2
    // Only 1 charge per check_gmg call now (not 2)
    mana_.add_mana(war_faction::aresden, 30);
    EXPECT_EQ(mana_.get_state(war_faction::aresden).gmg_charge, 2);
    EXPECT_EQ(meteor_count, 0);  // Still need 3 charges

    // One more charge
    mana_.add_mana(war_faction::aresden, 15);
    EXPECT_EQ(meteor_count, 1);
    EXPECT_EQ(mana_.get_state(war_faction::aresden).gmg_charge, 0);
}

TEST_F(mana_system_test, mana_pool_reset_discards_remainder) {
    // Override to 1 charge for simplicity
    mana_config cfg = mana_.get_config();
    cfg.gmg_charges_for_meteor = 1;
    mana_.set_config(cfg);
    mana_.set_meteor_trigger([](war_faction) {});

    // Collect 21 mana (threshold=15). Legacy discards remainder.
    mana_.add_mana(war_faction::aresden, 21);
    EXPECT_EQ(mana_.aresden_mana(), 0);  // NOT 6 — remainder discarded
    EXPECT_EQ(mana_.get_state(war_faction::aresden).gmg_charge, 0);
    EXPECT_EQ(mana_.get_state(war_faction::aresden).meteors_fired, 1);
}

TEST_F(mana_system_test, gmg_requires_multiple_charges_default) {
    // Use default config (charges_for_meteor=10)
    mana_system fresh;
    int meteor_count = 0;
    fresh.set_meteor_trigger([&](war_faction) { meteor_count++; });

    // Add 15 mana = 1 charge. Need 10 charges for meteor.
    fresh.add_mana(war_faction::aresden, 15);
    EXPECT_EQ(meteor_count, 0);
    EXPECT_EQ(fresh.get_state(war_faction::aresden).gmg_charge, 1);

    // Add 15 mana 8 more times = 9 charges total
    for (int i = 0; i < 8; i++)
    {
        fresh.add_mana(war_faction::aresden, 15);
    }
    EXPECT_EQ(meteor_count, 0);
    EXPECT_EQ(fresh.get_state(war_faction::aresden).gmg_charge, 9);

    // 10th charge triggers meteor
    fresh.add_mana(war_faction::aresden, 15);
    EXPECT_EQ(meteor_count, 1);
    EXPECT_EQ(fresh.get_state(war_faction::aresden).gmg_charge, 0);
}

// ========== GMG Damage Tests ==========

TEST_F(mana_system_test, gmg_damage_reduces_charges) {
    mana_.set_meteor_trigger([](war_faction) {});

    // Increase charges_for_meteor so charges accumulate without firing
    mana_config cfg = mana_.get_config();
    cfg.gmg_charges_for_meteor = 10;
    mana_.set_config(cfg);

    // Give aresden 2 charges
    mana_.add_mana(war_faction::aresden, 15);
    mana_.add_mana(war_faction::aresden, 15);
    EXPECT_EQ(mana_.get_state(war_faction::aresden).gmg_charge, 2);

    // Accumulate 500 damage -> lose 1 charge
    mana_.apply_gmg_damage(war_faction::aresden, 500);
    EXPECT_EQ(mana_.get_state(war_faction::aresden).gmg_charge, 1);
}

TEST_F(mana_system_test, gmg_damage_below_threshold_no_effect) {
    mana_.set_meteor_trigger([](war_faction) {});

    mana_config cfg = mana_.get_config();
    cfg.gmg_charges_for_meteor = 10;
    mana_.set_config(cfg);

    mana_.add_mana(war_faction::aresden, 15);
    EXPECT_EQ(mana_.get_state(war_faction::aresden).gmg_charge, 1);

    // 499 damage -- not enough
    mana_.apply_gmg_damage(war_faction::aresden, 499);
    EXPECT_EQ(mana_.get_state(war_faction::aresden).gmg_charge, 1);
}

TEST_F(mana_system_test, gmg_damage_resets_accumulator) {
    mana_.set_meteor_trigger([](war_faction) {});

    mana_config cfg = mana_.get_config();
    cfg.gmg_charges_for_meteor = 10;
    mana_.set_config(cfg);

    mana_.add_mana(war_faction::aresden, 15);
    mana_.add_mana(war_faction::aresden, 15);
    EXPECT_EQ(mana_.get_state(war_faction::aresden).gmg_charge, 2);

    // 300 damage, then 250 more = 550 total -> triggers at 500, accumulator resets
    mana_.apply_gmg_damage(war_faction::aresden, 300);
    EXPECT_EQ(mana_.get_state(war_faction::aresden).gmg_charge, 2);

    mana_.apply_gmg_damage(war_faction::aresden, 250);
    EXPECT_EQ(mana_.get_state(war_faction::aresden).gmg_charge, 1);

    // 450 more won't trigger (accumulator was reset to 0, now at 450)
    mana_.apply_gmg_damage(war_faction::aresden, 450);
    EXPECT_EQ(mana_.get_state(war_faction::aresden).gmg_charge, 1);

    // 50 more (accumulator = 500) -> triggers again
    mana_.apply_gmg_damage(war_faction::aresden, 50);
    EXPECT_EQ(mana_.get_state(war_faction::aresden).gmg_charge, 0);
}

TEST_F(mana_system_test, gmg_damage_at_zero_charges_no_underflow) {
    mana_.set_meteor_trigger([](war_faction) {});

    // No charges, take 500 damage -- should not underflow
    mana_.apply_gmg_damage(war_faction::aresden, 500);
    EXPECT_EQ(mana_.get_state(war_faction::aresden).gmg_charge, 0);
}

// ========== Mana Stone Depletion Tests ==========

TEST_F(mana_system_test, stone_depletion_limits_harvest) {
    // 1 stone, 2 collectors. Stone has max 5 mana.
    // First collector drains 3, second drains 2 (remaining). Total = 5, not 6.
    mana_.initialize_stones(1);
    mana_.set_meteor_trigger([](war_faction) {});

    mana_.tick(2, 0);
    EXPECT_EQ(mana_.aresden_mana(), 5);
}

TEST_F(mana_system_test, stones_regenerate_each_tick) {
    mana_.initialize_stones(1);
    mana_.set_meteor_trigger([](war_faction) {});

    // First tick: 1 collector takes 3 from stone
    mana_.tick(1, 0);
    EXPECT_EQ(mana_.aresden_mana(), 3);

    // Second tick: stone regens to 5, collector takes 3 again
    mana_.tick(1, 0);
    EXPECT_EQ(mana_.aresden_mana(), 6);
}

TEST_F(mana_system_test, both_factions_compete_for_stones) {
    mana_.initialize_stones(1);
    mana_.set_meteor_trigger([](war_faction) {});

    // 1 stone, 1 collector each faction
    // Aresden goes first: takes 3 (stone has 2 left)
    // Elvine goes second: takes 2 (stone has 0)
    mana_.tick(1, 1);
    EXPECT_EQ(mana_.aresden_mana(), 3);
    EXPECT_EQ(mana_.elvine_mana(), 2);
}

TEST_F(mana_system_test, multiple_stones_provide_more_mana) {
    mana_.initialize_stones(5);
    mana_.set_meteor_trigger([](war_faction) {});

    // 5 stones, 1 collector: takes 3 from each = 15 total
    // Pool is reset to 0 by GMG (threshold=15), so check total_mana_collected
    mana_.tick(1, 0);
    EXPECT_EQ(mana_.get_state(war_faction::aresden).total_mana_collected, 15);
}

TEST_F(mana_system_test, zero_collectors_yields_zero_with_stones) {
    mana_.initialize_stones(5);
    mana_.set_meteor_trigger([](war_faction) {});

    mana_.tick(0, 0);
    EXPECT_EQ(mana_.aresden_mana(), 0);
    EXPECT_EQ(mana_.elvine_mana(), 0);
}

// ========== Meteor Handler Tests ==========

class meteor_handler_test : public ::testing::Test {
protected:
    void SetUp() override {
        meteor_config cfg;
        cfg.warning_time_ms = 5000;
        cfg.player_wave1_delay_ms = 6000;
        cfg.player_wave2_delay_ms = 9000;
        cfg.result_delay_ms = 11000;
        cfg.esg_protection_radius = 10;
        cfg.base_strike_damage = 2;
        handler_.set_config(cfg);

        // No scheduler — executes immediately (for testing)
        handler_.set_scheduler(nullptr);

        // Default strike points for elvine (target when aresden attacks)
        elvine_points_ = {
            strike_point{.id = 1, .hp = 100, .max_hp = 100, .faction = war_faction::elvine},
            strike_point{.id = 2, .hp = 100, .max_hp = 100, .faction = war_faction::elvine},
        };
        aresden_points_ = {
            strike_point{.id = 1, .hp = 100, .max_hp = 100, .faction = war_faction::aresden},
        };

        wire_callbacks();
    }

    void wire_callbacks() {
        meteor_callbacks cbs;

        cbs.get_esg_count = [this](war_faction, uint16_t) -> int32_t {
            return esg_count_;
        };

        cbs.damage_strike_point = [this](war_faction faction, uint16_t point_id, int32_t damage) -> bool {
            auto& pts = (faction == war_faction::elvine) ? elvine_points_ : aresden_points_;
            for (auto& sp : pts) {
                if (sp.id == point_id && !sp.is_destroyed()) {
                    sp.hp = std::max(0, sp.hp - damage);
                    damages_applied_.push_back({faction, point_id, damage});
                    return true;
                }
            }
            return false;
        };

        cbs.get_strike_points = [this](war_faction faction) -> std::vector<strike_point> {
            return (faction == war_faction::elvine) ? elvine_points_ : aresden_points_;
        };

        cbs.broadcast_warning = [this](war_faction target, int32_t time_ms) {
            warnings_.push_back({target, time_ms});
        };

        cbs.broadcast_result = [this](const meteor_event_result& result) {
            results_.push_back(result);
        };

        cbs.damage_players = [this](war_faction, int32_t) -> int32_t {
            return player_casualties_;
        };

        handler_.set_callbacks(std::move(cbs));
    }

    meteor_handler handler_;

    // Test state
    int32_t esg_count_{0};
    int32_t player_casualties_{0};
    std::vector<strike_point> elvine_points_;
    std::vector<strike_point> aresden_points_;

    struct damage_record {
        war_faction faction;
        uint16_t point_id;
        int32_t damage;
    };
    std::vector<damage_record> damages_applied_;

    struct warning_record {
        war_faction target;
        int32_t time_ms;
    };
    std::vector<warning_record> warnings_;

    std::vector<meteor_event_result> results_;
};

TEST_F(meteor_handler_test, fire_meteor_broadcasts_warning) {
    handler_.fire_meteor(war_faction::aresden);

    ASSERT_EQ(warnings_.size(), 1);
    EXPECT_EQ(warnings_[0].target, war_faction::elvine);
    EXPECT_EQ(warnings_[0].time_ms, 5000);
}

TEST_F(meteor_handler_test, fire_meteor_damages_strike_points) {
    handler_.fire_meteor(war_faction::aresden);

    // Should damage each active elvine strike point
    ASSERT_EQ(damages_applied_.size(), 2);
    EXPECT_EQ(damages_applied_[0].faction, war_faction::elvine);
    EXPECT_EQ(damages_applied_[0].damage, 2);  // base_strike_damage
    EXPECT_EQ(damages_applied_[1].faction, war_faction::elvine);
    EXPECT_EQ(damages_applied_[1].damage, 2);
}

TEST_F(meteor_handler_test, esg_reduces_damage) {
    esg_count_ = 1;
    handler_.fire_meteor(war_faction::aresden);

    // base_damage(2) - esg(1) = 1
    ASSERT_EQ(damages_applied_.size(), 2);
    EXPECT_EQ(damages_applied_[0].damage, 1);
}

TEST_F(meteor_handler_test, esg_can_fully_block) {
    esg_count_ = 2;
    handler_.fire_meteor(war_faction::aresden);

    // base_damage(2) - esg(2) = 0 → no damage applied
    EXPECT_EQ(damages_applied_.size(), 0);
}

TEST_F(meteor_handler_test, esg_more_than_damage_still_zero) {
    esg_count_ = 5;
    handler_.fire_meteor(war_faction::aresden);

    // max(0, 2 - 5) = 0
    EXPECT_EQ(damages_applied_.size(), 0);
}

TEST_F(meteor_handler_test, broadcast_result_sent) {
    handler_.fire_meteor(war_faction::aresden);

    ASSERT_EQ(results_.size(), 1);
    EXPECT_EQ(results_[0].attacking_faction, war_faction::aresden);
    EXPECT_EQ(results_[0].target_faction, war_faction::elvine);
    EXPECT_EQ(results_[0].strike_results.size(), 2);
}

TEST_F(meteor_handler_test, result_contains_strike_details) {
    handler_.fire_meteor(war_faction::aresden);

    ASSERT_EQ(results_.size(), 1);
    const auto& sr = results_[0].strike_results;
    ASSERT_EQ(sr.size(), 2);
    EXPECT_EQ(sr[0].strike_point_id, 1);
    EXPECT_EQ(sr[0].damage_applied, 2);
    EXPECT_FALSE(sr[0].point_destroyed);
    EXPECT_EQ(sr[1].strike_point_id, 2);
}

TEST_F(meteor_handler_test, destroyed_points_not_targeted) {
    // Destroy point 1
    elvine_points_[0].hp = 0;

    handler_.fire_meteor(war_faction::aresden);

    // Only point 2 should be hit
    ASSERT_EQ(damages_applied_.size(), 1);
    EXPECT_EQ(damages_applied_[0].point_id, 2);
}

TEST_F(meteor_handler_test, elvine_attacks_aresden_points) {
    handler_.fire_meteor(war_faction::elvine);

    ASSERT_EQ(warnings_.size(), 1);
    EXPECT_EQ(warnings_[0].target, war_faction::aresden);

    ASSERT_EQ(damages_applied_.size(), 1);
    EXPECT_EQ(damages_applied_[0].faction, war_faction::aresden);
}

TEST_F(meteor_handler_test, neutral_faction_does_nothing) {
    handler_.fire_meteor(war_faction::neutral);

    EXPECT_EQ(warnings_.size(), 0);
    EXPECT_EQ(damages_applied_.size(), 0);
    EXPECT_EQ(results_.size(), 0);
}

TEST_F(meteor_handler_test, pending_count_tracks_active) {
    EXPECT_EQ(handler_.pending_count(), 0);

    handler_.fire_meteor(war_faction::aresden);
    // Without scheduler, sequence is immediate, so pending goes to 1 then back to 0
    EXPECT_EQ(handler_.pending_count(), 0);
}

TEST_F(meteor_handler_test, cancel_resets_pending) {
    handler_.cancel_all();
    EXPECT_EQ(handler_.pending_count(), 0);
}

TEST_F(meteor_handler_test, multiple_meteors_accumulate_damage) {
    handler_.fire_meteor(war_faction::aresden);
    handler_.fire_meteor(war_faction::aresden);

    // 2 meteors × 2 points each = 4 damage applications
    EXPECT_EQ(damages_applied_.size(), 4);
    EXPECT_EQ(results_.size(), 2);
}

// ========== Mana → Meteor Integration Tests ==========

class mana_meteor_integration_test : public ::testing::Test {
protected:
    void SetUp() override {
        war_sys_.initialize();
        crusade_.initialize();
        crusade_.set_dependencies(&war_sys_, nullptr, nullptr, nullptr, nullptr);

        crusade_config cfg;
        cfg.enabled = true;
        cfg.timing.duration_seconds = 3600;

        strike_point sp1{.id = 1, .hp = 100, .max_hp = 100};
        strike_point sp2{.id = 2, .hp = 100, .max_hp = 100};
        cfg.aresden_strike_points = {sp1, sp2};
        cfg.elvine_strike_points = {sp1, sp2};

        crusade_.set_config(cfg);

        // Override mana config for integration tests: 1 charge per meteor
        mana_config mcfg;
        mcfg.gmg_charges_for_meteor = 1;
        crusade_.mana().set_config(mcfg);

        // Track broadcasts
        crusade_.set_broadcast_all_fn([this](const hb::network::json_message& msg) {
            all_broadcasts_.push_back(msg);
        });
        crusade_.set_broadcast_fn([this](hb::player_id pid, const hb::network::json_message& msg) {
            player_broadcasts_.push_back({pid, msg});
        });
    }

    void TearDown() override {
        crusade_.shutdown();
        war_sys_.shutdown();
    }

    war_system war_sys_;
    crusade_system crusade_;

    std::vector<hb::network::json_message> all_broadcasts_;
    struct player_msg {
        hb::player_id pid;
        hb::network::json_message msg;
    };
    std::vector<player_msg> player_broadcasts_;
};

TEST_F(mana_meteor_integration_test, mana_system_available_after_start) {
    crusade_.start_crusade();

    auto& mana = crusade_.mana();
    EXPECT_EQ(mana.aresden_mana(), 0);
    EXPECT_EQ(mana.elvine_mana(), 0);
}

TEST_F(mana_meteor_integration_test, mana_reset_on_crusade_start) {
    crusade_.start_crusade();
    crusade_.mana().add_mana(war_faction::aresden, 10);
    EXPECT_EQ(crusade_.mana().aresden_mana(), 10);

    crusade_.end_crusade(war_faction::neutral);

    // Start a new crusade — mana should be reset
    crusade_.set_last_crusade_day(-1);
    crusade_.start_crusade();
    EXPECT_EQ(crusade_.mana().aresden_mana(), 0);
}

TEST_F(mana_meteor_integration_test, direct_mana_injection_triggers_meteor) {
    crusade_.start_crusade();

    // Mana config overridden in SetUp: threshold=15, charges_for_meteor=1
    crusade_.mana().add_mana(war_faction::aresden, 15);

    // Should have triggered a meteor on elvine
    EXPECT_EQ(crusade_.mana().get_state(war_faction::aresden).meteors_fired, 1);
}

TEST_F(mana_meteor_integration_test, meteor_damages_strike_points_through_crusade) {
    crusade_.start_crusade();

    // Clear existing broadcasts from start
    all_broadcasts_.clear();

    // Inject enough mana for a meteor (no scheduler = immediate)
    crusade_.mana().add_mana(war_faction::aresden, 15);

    // Check that elvine strike points took damage (base_damage=2)
    const auto& elvine = crusade_.get_strike_points(war_faction::elvine);
    ASSERT_EQ(elvine.size(), 2);
    EXPECT_EQ(elvine[0].hp, 98);  // 100 - 2
    EXPECT_EQ(elvine[1].hp, 98);  // 100 - 2
}

TEST_F(mana_meteor_integration_test, meteor_warning_broadcast) {
    crusade_.start_crusade();
    all_broadcasts_.clear();

    crusade_.mana().add_mana(war_faction::aresden, 15);

    // Should have: meteor_warning, strike_point_update(s), meteor_result
    bool has_warning = false;
    bool has_result = false;
    for (const auto& msg : all_broadcasts_) {
        if (msg.type == hb::network::json_message_type::crusade_meteor_warning) {
            has_warning = true;
            EXPECT_EQ(msg.data["target_faction"].get<int>(), static_cast<int>(war_faction::elvine));
        }
        if (msg.type == hb::network::json_message_type::crusade_meteor_result) {
            has_result = true;
            EXPECT_EQ(msg.data["attacking_faction"].get<int>(), static_cast<int>(war_faction::aresden));
        }
    }
    EXPECT_TRUE(has_warning);
    EXPECT_TRUE(has_result);
}

TEST_F(mana_meteor_integration_test, cancel_clears_meteor_state) {
    crusade_.start_crusade();
    EXPECT_EQ(crusade_.meteor().pending_count(), 0);

    crusade_.cancel_crusade();
    EXPECT_EQ(crusade_.meteor().pending_count(), 0);
}

TEST_F(mana_meteor_integration_test, mana_update_sent_to_commanders) {
    crusade_.start_crusade();

    hb::player_id commander_pid(1);
    hb::player_id fighter_pid(2);
    crusade_.join_crusade(commander_pid, war_faction::aresden);
    crusade_.join_crusade(fighter_pid, war_faction::aresden);
    crusade_.select_duty(commander_pid, crusade_duty::commander);
    crusade_.select_duty(fighter_pid, crusade_duty::fighter);

    player_broadcasts_.clear();

    // Force a mana broadcast by updating long enough
    // The mana broadcast interval is 10.0f seconds in crusade_system
    // The tick interval is 5.0f seconds in mana_config
    // update() accumulates delta_time in mana_tick_accumulator_
    // We need mana_broadcast_accumulator_ >= 10.0f
    // Each tick adds tick_interval(5.0f) to broadcast accumulator
    // So we need 2+ ticks → accumulate 10+ seconds of real time past mana tick
    crusade_.update(5.1f);  // triggers 1 tick, broadcast accum = 5.0
    crusade_.update(5.1f);  // triggers 2nd tick, broadcast accum = 10.0 → broadcasts

    bool commander_got_mana = false;
    bool fighter_got_mana = false;
    for (const auto& pm : player_broadcasts_) {
        if (pm.msg.type == hb::network::json_message_type::crusade_mana_update) {
            if (pm.pid == commander_pid) commander_got_mana = true;
            if (pm.pid == fighter_pid) fighter_got_mana = true;
        }
    }
    EXPECT_TRUE(commander_got_mana);
    EXPECT_FALSE(fighter_got_mana);  // Only commanders get mana updates
}

TEST_F(mana_meteor_integration_test, mp_restoration_no_crash_without_players) {
    crusade_.start_crusade();

    // Place a mana collector for aresden
    war_structure_instance collector;
    collector.type = war_unit_type::mana_collector;
    collector.faction = war_faction::aresden;
    collector.map_name = "middleland";
    collector.x = 100;
    collector.y = 100;
    crusade_.add_war_structure(collector);

    // Without player_system wired (nullptr in fixture), MP restoration
    // should not crash — the null guard skips the loop
    crusade_.update(5.1f);  // Trigger mana tick
    EXPECT_TRUE(true);  // No crash = success
}

TEST_F(mana_meteor_integration_test, gmg_damage_wiring_through_crusade) {
    // Use high charges_for_meteor so charges accumulate without firing
    mana_config mcfg;
    mcfg.gmg_charges_for_meteor = 10;
    crusade_.set_mana_config(mcfg);

    crusade_.start_crusade();

    // Place a GMG structure for aresden
    war_structure_instance gmg;
    gmg.eid = hb::entity::entity{999};
    gmg.type = war_unit_type::esg;  // Placeholder type (GMG has no dedicated war_unit_type)
    gmg.faction = war_faction::aresden;
    gmg.map_name = "middleland";
    gmg.x = 100;
    gmg.y = 100;
    crusade_.add_war_structure(gmg);

    crusade_.mana().add_mana(war_faction::aresden, 15);
    auto& state = crusade_.mana().get_state(war_faction::aresden);
    EXPECT_EQ(state.gmg_charge, 1);

    // Damage GMG via entity
    crusade_.on_gmg_damage(hb::entity::entity{999}, 500);
    EXPECT_EQ(state.gmg_charge, 0);
}

TEST_F(mana_meteor_integration_test, gmg_damage_unknown_entity_ignored) {
    mana_config mcfg;
    mcfg.gmg_charges_for_meteor = 10;
    crusade_.set_mana_config(mcfg);

    crusade_.start_crusade();

    crusade_.mana().add_mana(war_faction::aresden, 15);
    auto& state = crusade_.mana().get_state(war_faction::aresden);
    EXPECT_EQ(state.gmg_charge, 1);

    // Damage unknown entity -- should be ignored
    crusade_.on_gmg_damage(hb::entity::entity{12345}, 500);
    EXPECT_EQ(state.gmg_charge, 1);  // Unchanged
}

// ========== Phase 2 Protocol Message Tests ==========

TEST(crusade_phase2_protocol_test, meteor_message_types) {
    EXPECT_EQ(hb::network::to_string(hb::network::json_message_type::crusade_meteor_warning),
              "crusade_meteor_warning");
    EXPECT_EQ(hb::network::to_string(hb::network::json_message_type::crusade_meteor_hit),
              "crusade_meteor_hit");
    EXPECT_EQ(hb::network::to_string(hb::network::json_message_type::crusade_meteor_result),
              "crusade_meteor_result");
    EXPECT_EQ(hb::network::to_string(hb::network::json_message_type::crusade_mana_update),
              "crusade_mana_update");
}

TEST(crusade_phase2_protocol_test, meteor_message_type_roundtrip) {
    auto parsed = hb::network::parse_message_type("crusade_meteor_warning");
    EXPECT_EQ(parsed, hb::network::json_message_type::crusade_meteor_warning);

    parsed = hb::network::parse_message_type("crusade_meteor_result");
    EXPECT_EQ(parsed, hb::network::json_message_type::crusade_meteor_result);

    parsed = hb::network::parse_message_type("crusade_mana_update");
    EXPECT_EQ(parsed, hb::network::json_message_type::crusade_mana_update);
}

TEST(crusade_phase2_protocol_test, crusade_mp_restore_protocol_message) {
    EXPECT_EQ(hb::network::to_string(hb::network::json_message_type::crusade_mp_restore),
              "crusade_mp_restore");

    auto parsed = hb::network::parse_message_type("crusade_mp_restore");
    EXPECT_EQ(parsed, hb::network::json_message_type::crusade_mp_restore);

    auto msg = hb::network::make_crusade_mp_restore(128, 130, 5, 12);
    EXPECT_EQ(msg.type, hb::network::json_message_type::crusade_mp_restore);
    EXPECT_EQ(msg.data["source_x"], 128);
    EXPECT_EQ(msg.data["source_y"], 130);
    EXPECT_EQ(msg.data["radius"], 5);
    EXPECT_EQ(msg.data["your_restore"], 12);
}

// ========== Phase 3: Construction & Duty Tests ==========

TEST(construction_cost_test, costs_are_correct) {
    // Structures: zero cost (location-restricted)
    EXPECT_EQ(get_construction_cost(war_unit_type::agt), 0);
    EXPECT_EQ(get_construction_cost(war_unit_type::cgt), 0);
    EXPECT_EQ(get_construction_cost(war_unit_type::mana_collector), 0);
    EXPECT_EQ(get_construction_cost(war_unit_type::detector), 0);

    // Mobile units: legacy costs
    EXPECT_EQ(get_construction_cost(war_unit_type::lwb), 1000);
    EXPECT_EQ(get_construction_cost(war_unit_type::ghk), 2000);
    EXPECT_EQ(get_construction_cost(war_unit_type::ghkabs), 3000);
    EXPECT_EQ(get_construction_cost(war_unit_type::tk), 2000);
    EXPECT_EQ(get_construction_cost(war_unit_type::bg), 3000);
    EXPECT_EQ(get_construction_cost(war_unit_type::catapult), 1500);

    // ESG: not player-summonable
    EXPECT_LT(get_construction_cost(war_unit_type::esg), 0);
}

// ========== Kill-Based Construction Points ==========

TEST_F(crusade_system_test, on_kill_awards_construction_points) {
    crusade_.start_crusade();

    player_id killer(1);
    player_id victim(2);
    crusade_.join_crusade(killer, war_faction::aresden);
    crusade_.select_duty(killer, crusade_duty::fighter);

    // Legacy formula: victim_level / 2
    crusade_.on_player_kill(killer, victim, 100, 500);

    auto* data = crusade_.get_player_data(killer);
    ASSERT_NE(data, nullptr);
    EXPECT_EQ(data->construction_points, 50);  // 100 / 2
}

TEST_F(crusade_system_test, on_kill_all_duties_earn) {
    crusade_.start_crusade();

    player_id fighter(1);
    player_id constructor(2);
    player_id commander(3);
    crusade_.join_crusade(fighter, war_faction::aresden);
    crusade_.join_crusade(constructor, war_faction::aresden);
    crusade_.join_crusade(commander, war_faction::aresden);
    crusade_.select_duty(fighter, crusade_duty::fighter);
    crusade_.select_duty(constructor, crusade_duty::constructor);
    crusade_.select_duty(commander, crusade_duty::commander);

    crusade_.on_player_kill(fighter, player_id(10), 60, 100);
    crusade_.on_player_kill(constructor, player_id(11), 60, 100);
    crusade_.on_player_kill(commander, player_id(12), 60, 100);

    // All duties earn construction points (legacy behavior)
    EXPECT_EQ(crusade_.get_player_data(fighter)->construction_points, 30);
    EXPECT_EQ(crusade_.get_player_data(constructor)->construction_points, 30);
    EXPECT_EQ(crusade_.get_player_data(commander)->construction_points, 30);
}

TEST_F(crusade_system_test, on_kill_ignores_non_participant) {
    crusade_.start_crusade();
    crusade_.on_player_kill(player_id(99), player_id(100), 50, 100);  // Should not crash
}

TEST_F(crusade_system_test, on_kill_ignores_when_inactive) {
    crusade_.on_player_kill(player_id(1), player_id(2), 50, 100);  // Should not crash
}

TEST_F(crusade_system_test, on_kill_scales_by_level) {
    crusade_.start_crusade();

    player_id killer1(1);
    player_id killer2(2);
    crusade_.join_crusade(killer1, war_faction::aresden);
    crusade_.join_crusade(killer2, war_faction::aresden);
    crusade_.select_duty(killer1, crusade_duty::fighter);
    crusade_.select_duty(killer2, crusade_duty::fighter);

    crusade_.on_player_kill(killer1, player_id(10), 100, 500);  // High level victim
    crusade_.on_player_kill(killer2, player_id(11), 20, 100);   // Low level victim

    auto* data1 = crusade_.get_player_data(killer1);
    auto* data2 = crusade_.get_player_data(killer2);
    EXPECT_EQ(data1->construction_points, 50);  // 100 / 2
    EXPECT_EQ(data2->construction_points, 10);  // 20 / 2
    EXPECT_GT(data1->construction_points, data2->construction_points);
}

TEST_F(crusade_system_test, on_kill_minimum_points) {
    crusade_.start_crusade();

    player_id killer(1);
    crusade_.join_crusade(killer, war_faction::aresden);
    crusade_.select_duty(killer, crusade_duty::fighter);

    crusade_.on_player_kill(killer, player_id(2), 1, 10);  // Very low level

    auto* data = crusade_.get_player_data(killer);
    EXPECT_GE(data->construction_points, 1);  // Minimum 1 per kill (level 1 / 2 = 0, clamped to 1)
}

TEST_F(crusade_system_test, on_kill_awards_contribution) {
    crusade_.start_crusade();

    player_id killer(1);
    crusade_.join_crusade(killer, war_faction::aresden);
    crusade_.select_duty(killer, crusade_duty::fighter);

    // Legacy formula: (exp_reward - exp_reward/3) * 12
    // With exp_reward = 300: (300 - 100) * 12 = 2400
    crusade_.on_player_kill(killer, player_id(2), 50, 300);

    auto* data = crusade_.get_player_data(killer);
    EXPECT_EQ(data->war_contribution, 2400);
}

// ========== NPC Kill Rewards ==========

TEST_F(crusade_system_test, npc_kill_agt_rewards) {
    crusade_.start_crusade();

    player_id killer(1);
    crusade_.join_crusade(killer, war_faction::aresden);
    crusade_.select_duty(killer, crusade_duty::fighter);

    crusade_.on_npc_kill(killer, 36);  // AGT

    auto* data = crusade_.get_player_data(killer);
    EXPECT_EQ(data->construction_points, 700);
    EXPECT_EQ(data->war_contribution, 4000);
}

TEST_F(crusade_system_test, npc_kill_gmg_rewards) {
    crusade_.start_crusade();

    player_id killer(1);
    crusade_.join_crusade(killer, war_faction::aresden);

    crusade_.on_npc_kill(killer, 41);  // GMG

    auto* data = crusade_.get_player_data(killer);
    EXPECT_EQ(data->construction_points, 5000);
    EXPECT_EQ(data->war_contribution, 10000);
}

TEST_F(crusade_system_test, npc_kill_basic_mob_rewards) {
    crusade_.start_crusade();

    player_id killer(1);
    crusade_.join_crusade(killer, war_faction::aresden);

    crusade_.on_npc_kill(killer, 3);  // Basic mob (types 1-6)

    auto* data = crusade_.get_player_data(killer);
    EXPECT_EQ(data->construction_points, 50);
    EXPECT_EQ(data->war_contribution, 100);
}

TEST_F(crusade_system_test, npc_kill_unknown_type_no_reward) {
    crusade_.start_crusade();

    player_id killer(1);
    crusade_.join_crusade(killer, war_faction::aresden);

    crusade_.on_npc_kill(killer, 99);  // Unknown type

    auto* data = crusade_.get_player_data(killer);
    EXPECT_EQ(data->construction_points, 0);
    EXPECT_EQ(data->war_contribution, 0);
}

TEST_F(crusade_system_test, npc_kill_rewards_all_war_types) {
    // Verify all legacy war NPC reward values
    auto check = [](int16_t sprite, int32_t exp_construction, int32_t exp_contribution) {
        auto [c, w] = crusade_system::get_npc_kill_rewards(sprite);
        EXPECT_EQ(c, exp_construction) << "sprite " << sprite << " construction";
        EXPECT_EQ(w, exp_contribution) << "sprite " << sprite << " contribution";
    };

    check(36, 700, 4000);    // AGT
    check(37, 700, 4000);    // CGT
    check(38, 500, 2000);    // Mana Collector
    check(39, 500, 2000);    // Detector
    check(40, 1500, 5000);   // ESG
    check(41, 5000, 10000);  // GMG
    check(43, 500, 1000);    // LWB
    check(44, 1000, 2000);   // GHK
    check(45, 1500, 3000);   // GHKABS
    check(46, 1000, 2000);   // TK
    check(47, 1500, 3000);   // BG
    check(51, 500, 1500);    // Catapult
}

TEST_F(crusade_system_test, npc_kill_ignores_non_participant) {
    crusade_.start_crusade();
    crusade_.on_npc_kill(player_id(99), 36);  // Should not crash
}

TEST_F(crusade_system_test, npc_kill_ignores_when_inactive) {
    crusade_.on_npc_kill(player_id(1), 36);  // Should not crash
}

// ========== Friendly NPC Kill Penalty ==========

TEST_F(crusade_system_test, friendly_npc_kill_resets_contribution) {
    crusade_.start_crusade();

    player_id killer(1);
    crusade_.join_crusade(killer, war_faction::aresden);
    crusade_.award_contribution(killer, 5000);

    crusade_.on_friendly_npc_kill(killer);

    auto* data = crusade_.get_player_data(killer);
    EXPECT_EQ(data->war_contribution, 0);  // Reset to 0
}

TEST_F(crusade_system_test, friendly_npc_kill_ignores_non_participant) {
    crusade_.start_crusade();
    crusade_.on_friendly_npc_kill(player_id(99));  // Should not crash
}

TEST_F(crusade_system_test, friendly_npc_kill_ignores_when_inactive) {
    crusade_.on_friendly_npc_kill(player_id(1));  // Should not crash
}

// ========== War Unit Summoning ==========

TEST_F(crusade_system_test, summon_requires_active) {
    auto result = crusade_.summon_war_unit(player_id(1), war_unit_type::agt);
    EXPECT_EQ(result, crusade_result::not_active);
}

TEST_F(crusade_system_test, summon_requires_participant) {
    crusade_.start_crusade();
    auto result = crusade_.summon_war_unit(player_id(99), war_unit_type::agt);
    EXPECT_EQ(result, crusade_result::not_in_crusade);
}

TEST_F(crusade_system_test, summon_requires_constructor_duty) {
    crusade_.start_crusade();

    player_id pid(1);
    crusade_.join_crusade(pid, war_faction::aresden);
    crusade_.select_duty(pid, crusade_duty::fighter);
    crusade_.award_construction_points(pid, 5000);

    auto result = crusade_.summon_war_unit(pid, war_unit_type::agt);
    EXPECT_EQ(result, crusade_result::not_constructor);
}

TEST_F(crusade_system_test, summon_requires_enough_points) {
    crusade_.start_crusade();

    player_id pid(1);
    crusade_.join_crusade(pid, war_faction::aresden);
    crusade_.select_duty(pid, crusade_duty::constructor);
    crusade_.award_construction_points(pid, 500);  // Not enough for LWB (1000)

    auto result = crusade_.summon_war_unit(pid, war_unit_type::lwb);
    EXPECT_EQ(result, crusade_result::insufficient_points);
}

TEST_F(crusade_system_test, summon_deducts_cost) {
    crusade_.start_crusade();

    player_id pid(1);
    crusade_.join_crusade(pid, war_faction::aresden);
    crusade_.select_duty(pid, crusade_duty::constructor);
    crusade_.award_construction_points(pid, 5000);

    auto result = crusade_.summon_war_unit(pid, war_unit_type::ghk);  // Cost 2000
    EXPECT_EQ(result, crusade_result::success);

    auto* data = crusade_.get_player_data(pid);
    EXPECT_EQ(data->construction_points, 3000);  // 5000 - 2000
}

TEST_F(crusade_system_test, summon_creates_structure_record) {
    crusade_.start_crusade();

    player_id pid(1);
    crusade_.join_crusade(pid, war_faction::aresden);
    crusade_.select_duty(pid, crusade_duty::constructor);
    crusade_.award_construction_points(pid, 5000);

    crusade_.summon_war_unit(pid, war_unit_type::lwb);

    const auto& structures = crusade_.get_war_structures();
    ASSERT_EQ(structures.size(), 1);
    EXPECT_EQ(structures[0].type, war_unit_type::lwb);
    EXPECT_EQ(structures[0].faction, war_faction::aresden);
    EXPECT_EQ(structures[0].summoner, pid);
}

TEST_F(crusade_system_test, summon_multiple_units) {
    crusade_.start_crusade();

    player_id pid(1);
    crusade_.join_crusade(pid, war_faction::elvine);
    crusade_.select_duty(pid, crusade_duty::constructor);
    crusade_.award_construction_points(pid, 10000);

    crusade_.summon_war_unit(pid, war_unit_type::lwb);    // 1000
    crusade_.summon_war_unit(pid, war_unit_type::ghk);    // 2000
    crusade_.summon_war_unit(pid, war_unit_type::catapult); // 1500

    EXPECT_EQ(crusade_.count_structures_by_type(war_faction::elvine, war_unit_type::lwb), 1);
    EXPECT_EQ(crusade_.count_structures_by_type(war_faction::elvine, war_unit_type::ghk), 1);
    EXPECT_EQ(crusade_.count_structures_by_type(war_faction::elvine, war_unit_type::catapult), 1);
    EXPECT_EQ(crusade_.count_structures_by_type(war_faction::aresden, war_unit_type::lwb), 0);
}

TEST_F(crusade_system_test, structures_per_faction) {
    crusade_.start_crusade();

    player_id pid1(1);
    player_id pid2(2);
    crusade_.join_crusade(pid1, war_faction::aresden);
    crusade_.join_crusade(pid2, war_faction::elvine);
    crusade_.select_duty(pid1, crusade_duty::constructor);
    crusade_.select_duty(pid2, crusade_duty::constructor);
    crusade_.award_construction_points(pid1, 5000);
    crusade_.award_construction_points(pid2, 5000);

    crusade_.summon_war_unit(pid1, war_unit_type::lwb);
    crusade_.summon_war_unit(pid2, war_unit_type::ghk);

    auto aresden = crusade_.get_war_structures(war_faction::aresden);
    auto elvine = crusade_.get_war_structures(war_faction::elvine);
    EXPECT_EQ(aresden.size(), 1);
    EXPECT_EQ(elvine.size(), 1);
    EXPECT_EQ(aresden[0].type, war_unit_type::lwb);
    EXPECT_EQ(elvine[0].type, war_unit_type::ghk);
}

TEST_F(crusade_system_test, structures_cleared_on_end) {
    crusade_.start_crusade();

    player_id pid(1);
    crusade_.join_crusade(pid, war_faction::aresden);
    crusade_.select_duty(pid, crusade_duty::constructor);
    crusade_.award_construction_points(pid, 5000);
    crusade_.summon_war_unit(pid, war_unit_type::lwb);
    EXPECT_EQ(crusade_.get_war_structures().size(), 1);

    crusade_.end_crusade(war_faction::neutral);
    EXPECT_EQ(crusade_.get_war_structures().size(), 0);
}

// ========== Construction Point Transfer ==========

TEST_F(crusade_system_test, transfer_no_social_is_noop) {
    crusade_.start_crusade();

    player_id fighter(1);
    crusade_.join_crusade(fighter, war_faction::aresden);
    crusade_.select_duty(fighter, crusade_duty::fighter);
    crusade_.award_construction_points(fighter, 500);

    // social_ is null, so no commander can be found
    crusade_.transfer_construction_points();

    auto* data = crusade_.get_player_data(fighter);
    EXPECT_EQ(data->construction_points, 500);  // Unchanged
}

TEST_F(crusade_system_test, commander_keeps_own_points) {
    crusade_.start_crusade();

    player_id commander(1);
    crusade_.join_crusade(commander, war_faction::aresden);
    crusade_.select_duty(commander, crusade_duty::commander);
    crusade_.award_construction_points(commander, 500);

    crusade_.transfer_construction_points();

    // Commanders don't transfer their own points
    auto* data = crusade_.get_player_data(commander);
    EXPECT_EQ(data->construction_points, 500);
}

TEST_F(crusade_system_test, transfer_awards_contribution_to_commander) {
    // Set up social system with a guild
    hb::social::social_system social;
    social.initialize();
    crusade_.set_social(&social);

    player_id commander_pid(1);
    player_id fighter_pid(2);

    // Create guild with commander as guild master, fighter as member
    auto guild_result = social.create_guild(commander_pid, "TestGuild", "TG");
    ASSERT_TRUE(guild_result.is_ok());
    auto gid = guild_result.value();
    social.join_guild(fighter_pid, gid);

    crusade_.start_crusade();

    crusade_.join_crusade(commander_pid, war_faction::aresden);
    crusade_.join_crusade(fighter_pid, war_faction::aresden);
    crusade_.select_duty(commander_pid, crusade_duty::commander);
    crusade_.select_duty(fighter_pid, crusade_duty::fighter);

    crusade_.award_construction_points(fighter_pid, 1000);

    crusade_.transfer_construction_points();

    // Commander should receive construction points AND contribution
    auto* cmd_data = crusade_.get_player_data(commander_pid);
    EXPECT_EQ(cmd_data->construction_points, 1000);  // Received from fighter
    EXPECT_EQ(cmd_data->war_contribution, 100);       // 1000 * 0.1

    // Fighter should have 0 points and 0 contribution (contribution goes to commander)
    auto* ftr_data = crusade_.get_player_data(fighter_pid);
    EXPECT_EQ(ftr_data->construction_points, 0);
    EXPECT_EQ(ftr_data->war_contribution, 0);

    social.shutdown();
    crusade_.set_social(nullptr);
}

// ========== Phase 3 Protocol Message Tests ==========

TEST(crusade_phase3_protocol_test, construction_message_types) {
    EXPECT_EQ(hb::network::to_string(hb::network::json_message_type::crusade_construction_point_update),
              "crusade_construction_point_update");
    EXPECT_EQ(hb::network::to_string(hb::network::json_message_type::summon_war_unit_request),
              "summon_war_unit_request");
    EXPECT_EQ(hb::network::to_string(hb::network::json_message_type::summon_war_unit_response),
              "summon_war_unit_response");
    EXPECT_EQ(hb::network::to_string(hb::network::json_message_type::crusade_map_status),
              "crusade_map_status");
}

TEST(crusade_phase3_protocol_test, construction_message_roundtrip) {
    auto parsed = hb::network::parse_message_type("summon_war_unit_request");
    EXPECT_EQ(parsed, hb::network::json_message_type::summon_war_unit_request);

    parsed = hb::network::parse_message_type("crusade_construction_point_update");
    EXPECT_EQ(parsed, hb::network::json_message_type::crusade_construction_point_update);

    parsed = hb::network::parse_message_type("crusade_map_status");
    EXPECT_EQ(parsed, hb::network::json_message_type::crusade_map_status);
}

TEST(crusade_phase3_protocol_test, summon_response_success) {
    auto msg = hb::network::make_summon_war_unit_response(42, true, 1, 3000);
    EXPECT_EQ(msg.type, hb::network::json_message_type::summon_war_unit_response);
    EXPECT_EQ(msg.seq, 42u);
    EXPECT_TRUE(msg.data["success"].get<bool>());
    EXPECT_EQ(msg.data["unit_type"].get<int>(), 1);
    EXPECT_EQ(msg.data["remaining_points"].get<int>(), 3000);
}

TEST(crusade_phase3_protocol_test, summon_response_failure) {
    auto msg = hb::network::make_summon_war_unit_response(10, false, 0, 0, "insufficient_points");
    EXPECT_FALSE(msg.data["success"].get<bool>());
    EXPECT_EQ(msg.data["error"].get<std::string>(), "insufficient_points");
}

// ========== A1: Construction & Contribution Caps ==========

TEST_F(crusade_system_test, construction_points_capped) {
    crusade_.start_crusade();

    player_id pid(1);
    crusade_.join_crusade(pid, war_faction::aresden);
    crusade_.select_duty(pid, crusade_duty::fighter);

    crusade_.award_construction_points(pid, 35000);
    auto* data = crusade_.get_player_data(pid);
    EXPECT_EQ(data->construction_points, max_construction_points);  // 30000
}

TEST_F(crusade_system_test, contribution_capped) {
    crusade_.start_crusade();

    player_id pid(1);
    crusade_.join_crusade(pid, war_faction::aresden);

    crusade_.award_contribution(pid, 250000);
    auto* data = crusade_.get_player_data(pid);
    EXPECT_EQ(data->war_contribution, max_war_contribution);  // 200000
}

TEST_F(crusade_system_test, on_kill_caps_construction_points) {
    crusade_.start_crusade();

    player_id killer(1);
    crusade_.join_crusade(killer, war_faction::aresden);
    crusade_.select_duty(killer, crusade_duty::fighter);

    // Pre-load near the cap
    crusade_.award_construction_points(killer, max_construction_points - 5);

    // Kill should not exceed cap (victim_level 100 = 50 points)
    crusade_.on_player_kill(killer, player_id(2), 100, 0);
    auto* data = crusade_.get_player_data(killer);
    EXPECT_EQ(data->construction_points, max_construction_points);
}

// ========== A6: Guild Master Restriction ==========

TEST_F(crusade_system_test, commander_requires_guild_master_with_social) {
    // Set up a social system with a guild
    hb::social::social_system social;
    social.initialize();
    crusade_.set_social(&social);

    crusade_.start_crusade();

    player_id pid(1);
    crusade_.join_crusade(pid, war_faction::aresden);

    // Player is not in any guild — should be rejected
    auto result = crusade_.select_duty(pid, crusade_duty::commander);
    EXPECT_EQ(result, crusade_result::not_guild_master);

    social.shutdown();
    crusade_.set_social(nullptr);
}

TEST_F(crusade_system_test, commander_allowed_without_social) {
    // social_ is null — should allow (backward compat / testing)
    crusade_.start_crusade();

    player_id pid(1);
    crusade_.join_crusade(pid, war_faction::aresden);

    auto result = crusade_.select_duty(pid, crusade_duty::commander);
    EXPECT_EQ(result, crusade_result::success);
}

TEST_F(crusade_system_test, fighter_allowed_even_without_guild) {
    // Fighter duty should not require guild master
    hb::social::social_system social;
    social.initialize();
    crusade_.set_social(&social);

    crusade_.start_crusade();

    player_id pid(1);
    crusade_.join_crusade(pid, war_faction::aresden);

    auto result = crusade_.select_duty(pid, crusade_duty::fighter);
    EXPECT_EQ(result, crusade_result::success);

    social.shutdown();
    crusade_.set_social(nullptr);
}

// ========== A7: Winner Bonus ==========

TEST_F(crusade_system_test, winner_bonus_awarded_to_commander) {
    // First crusade: aresden wins
    crusade_.start_crusade();
    crusade_.end_crusade(war_faction::aresden);
    EXPECT_EQ(crusade_.last_winner(), war_faction::aresden);

    // Second crusade: aresden commander should get bonus
    crusade_.set_last_crusade_day(-1);  // Allow same-day restart for testing
    crusade_.start_crusade();

    player_id pid(1);
    crusade_.join_crusade(pid, war_faction::aresden);
    // No social system → commander selection allowed
    auto result = crusade_.select_duty(pid, crusade_duty::commander);
    EXPECT_EQ(result, crusade_result::success);

    auto* data = crusade_.get_player_data(pid);
    EXPECT_EQ(data->construction_points, crusade_.config().construction.commander_bonus_points);
}

TEST_F(crusade_system_test, winner_bonus_not_awarded_different_faction) {
    crusade_.start_crusade();
    crusade_.end_crusade(war_faction::aresden);

    crusade_.set_last_crusade_day(-1);
    crusade_.start_crusade();

    player_id pid(1);
    crusade_.join_crusade(pid, war_faction::elvine);
    crusade_.select_duty(pid, crusade_duty::commander);

    auto* data = crusade_.get_player_data(pid);
    EXPECT_EQ(data->construction_points, 0);  // No bonus for elvine
}

TEST_F(crusade_system_test, winner_bonus_not_awarded_neutral_winner) {
    crusade_.start_crusade();
    crusade_.end_crusade(war_faction::neutral);  // Draw
    EXPECT_EQ(crusade_.last_winner(), war_faction::neutral);

    crusade_.set_last_crusade_day(-1);
    crusade_.start_crusade();

    player_id pid(1);
    crusade_.join_crusade(pid, war_faction::aresden);
    crusade_.select_duty(pid, crusade_duty::commander);

    auto* data = crusade_.get_player_data(pid);
    EXPECT_EQ(data->construction_points, 0);
}

// ========== A8: Mana Collection Wiring ==========

TEST_F(crusade_system_test, mana_tick_with_zero_collectors_yields_zero) {
    crusade_.start_crusade();

    // No mana collectors spawned
    EXPECT_EQ(crusade_.count_structures_by_type(war_faction::aresden, war_unit_type::mana_collector), 0);

    // Tick mana — should produce nothing
    crusade_.update(5.1f);

    EXPECT_EQ(crusade_.mana().aresden_mana(), 0);
}

TEST_F(crusade_system_test, mana_tick_with_collector_yields_mana) {
    crusade_.start_crusade();

    // Place a mana collector directly (pre-placed structure)
    war_structure_instance collector;
    collector.type = war_unit_type::mana_collector;
    collector.faction = war_faction::aresden;
    collector.map_name = "war_map";
    collector.x = 50;
    collector.y = 50;
    crusade_.add_war_structure(collector);

    EXPECT_EQ(crusade_.count_structures_by_type(war_faction::aresden, war_unit_type::mana_collector), 1);

    // Tick mana — collector + stones should produce mana
    crusade_.update(5.1f);

    // Mana pool may be 0 if GMG consumed it all, but total_mana_collected proves it worked
    EXPECT_GT(crusade_.mana().get_state(war_faction::aresden).total_mana_collected, 0);
}

// ========== A9: ESG Count ==========

TEST_F(crusade_system_test, esg_count_zero_without_structures) {
    // Set gmg_charges_for_meteor=1 so 15 mana → 1 charge → instant meteor
    mana_config mc;
    mc.gmg_charges_for_meteor = 1;
    crusade_.set_mana_config(mc);

    crusade_.start_crusade();

    // Fire meteor — should see 0 ESG count on results
    crusade_.mana().add_mana(war_faction::aresden, 15);

    // Check that strike points took full damage (no ESG protection)
    const auto& elvine = crusade_.get_strike_points(war_faction::elvine);
    for (const auto& sp : elvine) {
        EXPECT_EQ(sp.hp, 98);  // 100 - 2 (base damage)
    }
}

TEST_F(crusade_system_test, esg_within_radius_reduces_damage) {
    // Set config with strike points that have map_name
    crusade_config cfg = crusade_.config();
    cfg.aresden_strike_points[0].map_name = "war_map";
    cfg.aresden_strike_points[1].map_name = "war_map";
    cfg.elvine_strike_points[0].map_name = "war_map";
    cfg.elvine_strike_points[0].x = 50;
    cfg.elvine_strike_points[0].y = 50;
    cfg.elvine_strike_points[1].map_name = "war_map";
    cfg.elvine_strike_points[1].x = 100;
    cfg.elvine_strike_points[1].y = 100;
    crusade_.set_config(cfg);

    mana_config mc;
    mc.gmg_charges_for_meteor = 1;
    crusade_.set_mana_config(mc);

    crusade_.start_crusade();

    // Place ESG near elvine strike point 1 (within radius 10) — ESG is pre-placed only
    war_structure_instance esg;
    esg.type = war_unit_type::esg;
    esg.faction = war_faction::elvine;
    esg.map_name = "war_map";
    esg.x = 52;
    esg.y = 52;
    crusade_.add_war_structure(esg);

    // Fire meteor from aresden targeting elvine
    crusade_.mana().add_mana(war_faction::aresden, 15);

    // Strike point 1 (at 50,50) should be protected by ESG at (52,52) — damage = max(0, 2-1) = 1
    // Strike point 2 (at 100,100) should NOT be protected — distance too far — damage = 2
    const auto& elvine = crusade_.get_strike_points(war_faction::elvine);
    EXPECT_EQ(elvine[0].hp, 99);   // 100 - 1 (ESG blocked 1)
    EXPECT_EQ(elvine[1].hp, 98);   // 100 - 2 (no ESG)
}

TEST_F(crusade_system_test, esg_outside_radius_not_counted) {
    crusade_config cfg = crusade_.config();
    cfg.elvine_strike_points[0].map_name = "war_map";
    cfg.elvine_strike_points[0].x = 50;
    cfg.elvine_strike_points[0].y = 50;
    cfg.elvine_strike_points[1].map_name = "war_map";
    cfg.elvine_strike_points[1].x = 100;
    cfg.elvine_strike_points[1].y = 100;
    crusade_.set_config(cfg);

    mana_config mc;
    mc.gmg_charges_for_meteor = 1;
    crusade_.set_mana_config(mc);

    crusade_.start_crusade();

    // Place ESG far from both strike points
    war_structure_instance esg;
    esg.type = war_unit_type::esg;
    esg.faction = war_faction::elvine;
    esg.map_name = "war_map";
    esg.x = 200;
    esg.y = 200;
    crusade_.add_war_structure(esg);

    crusade_.mana().add_mana(war_faction::aresden, 15);

    // Both points should take full damage since ESG is too far
    const auto& elvine = crusade_.get_strike_points(war_faction::elvine);
    EXPECT_EQ(elvine[0].hp, 98);
    EXPECT_EQ(elvine[1].hp, 98);
}

TEST_F(crusade_system_test, two_esgs_fully_protect) {
    crusade_config cfg = crusade_.config();
    cfg.elvine_strike_points[0].map_name = "war_map";
    cfg.elvine_strike_points[0].x = 50;
    cfg.elvine_strike_points[0].y = 50;
    cfg.elvine_strike_points[1].map_name = "war_map";
    cfg.elvine_strike_points[1].x = 100;
    cfg.elvine_strike_points[1].y = 100;
    crusade_.set_config(cfg);

    mana_config mc;
    mc.gmg_charges_for_meteor = 1;
    crusade_.set_mana_config(mc);

    crusade_.start_crusade();

    // Place 2 ESGs near strike point 1 — ESGs are pre-placed only
    war_structure_instance esg1;
    esg1.type = war_unit_type::esg;
    esg1.faction = war_faction::elvine;
    esg1.map_name = "war_map";
    esg1.x = 51;
    esg1.y = 51;
    crusade_.add_war_structure(esg1);

    war_structure_instance esg2;
    esg2.type = war_unit_type::esg;
    esg2.faction = war_faction::elvine;
    esg2.map_name = "war_map";
    esg2.x = 52;
    esg2.y = 52;
    crusade_.add_war_structure(esg2);

    crusade_.mana().add_mana(war_faction::aresden, 15);

    // Strike point 1 fully protected (2 ESG >= base damage 2)
    // Strike point 2 unprotected
    const auto& elvine = crusade_.get_strike_points(war_faction::elvine);
    EXPECT_EQ(elvine[0].hp, 100);  // No damage
    EXPECT_EQ(elvine[1].hp, 98);   // Full damage
}

// ========== A10: Player Meteor Damage ==========

TEST(meteor_damage_test, formula_matches_legacy) {
    meteor_handler handler;

    // Legacy formula: iDice(1, level) + level → range [level+1, level*2]
    for (int i = 0; i < 50; ++i)
    {
        int32_t damage = handler.calculate_player_damage(30);
        EXPECT_GE(damage, 31);
        EXPECT_LE(damage, 60);
    }

    for (int i = 0; i < 50; ++i)
    {
        int32_t damage = handler.calculate_player_damage(100);
        EXPECT_GE(damage, 101);
        EXPECT_LE(damage, 200);
    }
}

TEST(meteor_damage_test, capped_at_255) {
    meteor_handler handler;

    // Level 200: range would be [201, 400] but capped at 255
    for (int i = 0; i < 50; ++i)
    {
        int32_t damage = handler.calculate_player_damage(200);
        EXPECT_GE(damage, 201);
        EXPECT_LE(damage, 255);
    }
}

TEST(meteor_damage_test, zero_level_returns_zero) {
    meteor_handler handler;
    EXPECT_EQ(handler.calculate_player_damage(0), 0);
    EXPECT_EQ(handler.calculate_player_damage(-5), 0);
}

// ========== B1: Summon with Position ==========

TEST_F(crusade_system_test, summon_stores_position) {
    crusade_.start_crusade();

    player_id pid(1);
    crusade_.join_crusade(pid, war_faction::aresden);
    crusade_.select_duty(pid, crusade_duty::constructor);
    crusade_.award_construction_points(pid, 5000);

    crusade_.summon_war_unit(pid, war_unit_type::ghk, "war_map", 10, 20);

    const auto& structures = crusade_.get_war_structures();
    ASSERT_EQ(structures.size(), 1);
    EXPECT_EQ(structures[0].map_name, "war_map");
    EXPECT_EQ(structures[0].x, 10);
    EXPECT_EQ(structures[0].y, 20);
}

TEST_F(crusade_system_test, summon_without_npc_system_still_records) {
    // npcs_ is null (from test setup)
    crusade_.start_crusade();

    player_id pid(1);
    crusade_.join_crusade(pid, war_faction::aresden);
    crusade_.select_duty(pid, crusade_duty::constructor);
    crusade_.award_construction_points(pid, 5000);

    auto result = crusade_.summon_war_unit(pid, war_unit_type::tk, "map1", 5, 5);
    EXPECT_EQ(result, crusade_result::success);

    const auto& structures = crusade_.get_war_structures();
    ASSERT_EQ(structures.size(), 1);
    EXPECT_FALSE(structures[0].eid.is_valid());  // No NPC spawned
}

// ========== B2: Structure Cleanup ==========

TEST_F(crusade_system_test, cleanup_clears_structures) {
    crusade_.start_crusade();

    player_id pid(1);
    crusade_.join_crusade(pid, war_faction::aresden);
    crusade_.select_duty(pid, crusade_duty::constructor);
    crusade_.award_construction_points(pid, 5000);
    crusade_.summon_war_unit(pid, war_unit_type::lwb);
    EXPECT_EQ(crusade_.get_war_structures().size(), 1);

    crusade_.end_crusade(war_faction::neutral);
    EXPECT_EQ(crusade_.get_war_structures().size(), 0);
}

// ========== NPC Type Mapping ==========

TEST(crusade_npc_type_test, unit_types_map_correctly_aresden) {
    EXPECT_EQ(get_npc_id_for_unit(war_unit_type::agt, war_faction::aresden), 64);
    EXPECT_EQ(get_npc_id_for_unit(war_unit_type::cgt, war_faction::aresden), 66);
    EXPECT_EQ(get_npc_id_for_unit(war_unit_type::mana_collector, war_faction::aresden), 68);
    EXPECT_EQ(get_npc_id_for_unit(war_unit_type::detector, war_faction::aresden), 70);
    EXPECT_EQ(get_npc_id_for_unit(war_unit_type::lwb, war_faction::aresden), 77);
    EXPECT_EQ(get_npc_id_for_unit(war_unit_type::catapult, war_faction::aresden), 85);
    EXPECT_EQ(get_npc_id_for_unit(war_unit_type::esg, war_faction::aresden), 72);
}

TEST(crusade_npc_type_test, unit_types_map_correctly_elvine) {
    EXPECT_EQ(get_npc_id_for_unit(war_unit_type::agt, war_faction::elvine), 65);
    EXPECT_EQ(get_npc_id_for_unit(war_unit_type::cgt, war_faction::elvine), 67);
    EXPECT_EQ(get_npc_id_for_unit(war_unit_type::mana_collector, war_faction::elvine), 69);
    EXPECT_EQ(get_npc_id_for_unit(war_unit_type::detector, war_faction::elvine), 71);
    EXPECT_EQ(get_npc_id_for_unit(war_unit_type::lwb, war_faction::elvine), 78);
    EXPECT_EQ(get_npc_id_for_unit(war_unit_type::catapult, war_faction::elvine), 86);
    EXPECT_EQ(get_npc_id_for_unit(war_unit_type::esg, war_faction::elvine), 73);
}

// ========== Magic Protection vs Meteor Damage ==========

class meteor_protection_test : public ::testing::Test {
protected:
    void SetUp() override {
        war_sys_.initialize();
        player_sys_.initialize();
        effect_sys_.initialize();
        crusade_.initialize();
        crusade_.set_dependencies(&war_sys_, &player_sys_, nullptr, nullptr, nullptr);
        crusade_.set_effects(&effect_sys_);

        crusade_config cfg;
        cfg.enabled = true;
        cfg.timing.duration_seconds = 3600;

        strike_point sp1;
        sp1.id = 1;
        sp1.max_hp = 100;
        sp1.map_name = "aresden";
        cfg.aresden_strike_points.push_back(sp1);

        strike_point sp2;
        sp2.id = 1;
        sp2.max_hp = 100;
        sp2.map_name = "elvine";
        cfg.elvine_strike_points.push_back(sp2);

        crusade_.set_config(cfg);
    }

    void TearDown() override {
        crusade_.shutdown();
        effect_sys_.shutdown();
        player_sys_.shutdown();
        war_sys_.shutdown();
    }

    auto create_test_player(const std::string& name, int32_t level, int32_t hp_val)
        -> hb::player_id
    {
        hb::player::player_create_info info;
        info.name = name;
        info.account_name = name + "_acct";
        auto result = player_sys_.create_player(info);
        auto pid = result.value();
        auto* plr = player_sys_.get_player(pid);
        plr->experience.level = level;
        plr->hp = hp_val;
        plr->admin = hb::player::admin_level::player;
        // Assign an ECS entity for effect lookup
        plr->ecs_entity = hb::entity::entity(pid.value);
        return pid;
    }

    war_system war_sys_;
    hb::player::player_system player_sys_;
    hb::effect::effect_system effect_sys_;
    crusade_system crusade_;
};

TEST_F(meteor_protection_test, no_protection_takes_full_damage) {
    auto pid = create_test_player("warrior", 50, 500);

    crusade_.start_crusade();

    // Fire meteor at aresden (where the player would be)
    // The damage_players callback iterates all players
    // Without world system, all players take damage
    auto& meteor = crusade_.meteor();
    meteor.fire_meteor(war_faction::elvine);  // Attacks aresden

    auto* plr = player_sys_.get_player(pid);
    // Level 50: damage = iDice(1,50) + 50 = 51-100 per wave, 2 waves = 102-200 total
    EXPECT_LT(plr->hp, 500);
    EXPECT_GE(plr->hp, 300);
    EXPECT_LE(plr->hp, 398);
}

TEST_F(meteor_protection_test, protection_from_magic_reduces_damage) {
    auto pid = create_test_player("mage", 50, 500);
    auto* plr = player_sys_.get_player(pid);

    // Apply Protection From Magic (magnitude 2) via effect system
    hb::effect::apply_effect_params params{};
    params.source = plr->ecs_entity;
    params.target = plr->ecs_entity;
    params.group = hb::magic_type::protection;
    params.magnitude = 2;  // PFM level
    params.duration_ms = 60000;
    effect_sys_.apply_effect(params);

    crusade_.start_crusade();
    crusade_.meteor().fire_meteor(war_faction::elvine);

    // Level 50: base damage = iDice(1,50) + 50 = 51-100 per wave
    // PFM reduction: damage/2 - 2 = (51-100)/2 - 2 = 23-48 per wave
    // 2 waves: 46-96 total, HP = 500 - (46..96) = 404-454
    EXPECT_LT(plr->hp, 500);
    EXPECT_GE(plr->hp, 404);
    EXPECT_LE(plr->hp, 454);
}

TEST_F(meteor_protection_test, absolute_magic_protection_negates_damage) {
    auto pid = create_test_player("archmage", 100, 500);
    auto* plr = player_sys_.get_player(pid);

    // Apply Absolute Magic Protection (magnitude 5) via effect system
    hb::effect::apply_effect_params params{};
    params.source = plr->ecs_entity;
    params.target = plr->ecs_entity;
    params.group = hb::magic_type::protection;
    params.magnitude = 5;  // AMP level
    params.duration_ms = 60000;
    effect_sys_.apply_effect(params);

    crusade_.start_crusade();
    crusade_.meteor().fire_meteor(war_faction::elvine);

    // AMP: damage = 0
    EXPECT_EQ(plr->hp, 500);
}

TEST_F(meteor_protection_test, protection_without_effect_system_takes_full_damage) {
    // Wire crusade without effect system
    crusade_.set_effects(nullptr);

    auto pid = create_test_player("warrior", 50, 500);

    crusade_.start_crusade();
    crusade_.meteor().fire_meteor(war_faction::elvine);

    auto* plr = player_sys_.get_player(pid);
    // Should still take full damage
    EXPECT_LT(plr->hp, 500);
}

TEST_F(meteor_protection_test, meteor_damage_breaks_hold_effects) {
    auto pid = create_test_player("warrior", 50, 5000);  // High HP to survive
    auto* plr = player_sys_.get_player(pid);

    // Apply hold/paralyze effect
    hb::effect::apply_effect_params params{};
    params.source = plr->ecs_entity;
    params.target = plr->ecs_entity;
    params.group = hb::magic_type::hold_paralyze;
    params.magnitude = 1;
    params.duration_ms = 60000;
    effect_sys_.apply_effect(params);

    EXPECT_TRUE(effect_sys_.has_effect_in_group(plr->ecs_entity, hb::magic_type::hold_paralyze));

    crusade_.start_crusade();
    crusade_.meteor().fire_meteor(war_faction::elvine);

    // Player takes damage and survives
    EXPECT_LT(plr->hp, 5000);
    EXPECT_GT(plr->hp, 0);

    // Hold/paralyze should be removed
    EXPECT_FALSE(effect_sys_.has_effect_in_group(plr->ecs_entity, hb::magic_type::hold_paralyze));
}

// ========== End Crusade Reward Integration Tests ==========

TEST_F(crusade_system_test, end_crusade_sends_legacy_reward) {
    hb::player::player_system player_sys;
    player_sys.initialize();
    crusade_.set_dependencies(&war_sys_, &player_sys, nullptr, nullptr, nullptr);

    auto result = crusade_.start_crusade();
    ASSERT_TRUE(result.is_ok());

    hb::player::player_create_info info;
    info.name = "TestPlayer";
    info.account_name = "test_acct";
    auto pid = player_sys.create_player(info).value();
    auto* plr = player_sys.get_player(pid);
    ASSERT_NE(plr, nullptr);
    plr->experience.level = 50;

    crusade_.join_crusade(pid, war_faction::aresden);
    crusade_.award_contribution(pid, 1000);

    hb::network::json_message captured_msg{};
    bool got_reward = false;
    crusade_.set_broadcast_fn([&](player_id, const hb::network::json_message& msg) {
        if (msg.type == hb::network::json_message_type::crusade_reward_summary) {
            captured_msg = msg;
            got_reward = true;
        }
    });

    crusade_.end_crusade(war_faction::aresden);

    ASSERT_TRUE(got_reward);
    EXPECT_EQ(captured_msg.data["reward_exp"].get<int64_t>(), 6000);
    EXPECT_EQ(captured_msg.data["reward_gold"].get<int64_t>(), 0);
    EXPECT_EQ(captured_msg.data["contribution"].get<int32_t>(), 1000);

    player_sys.shutdown();
}

TEST_F(crusade_system_test, end_crusade_applies_exp_to_online_player) {
    hb::player::player_system player_sys;
    player_sys.initialize();
    crusade_.set_dependencies(&war_sys_, &player_sys, nullptr, nullptr, nullptr);

    auto result = crusade_.start_crusade();
    ASSERT_TRUE(result.is_ok());

    hb::player::player_create_info info;
    info.name = "TestPlayer";
    info.account_name = "test_acct";
    auto pid = player_sys.create_player(info).value();
    auto* plr = player_sys.get_player(pid);
    ASSERT_NE(plr, nullptr);
    plr->experience.level = 50;

    int64_t exp_before = plr->experience.experience;

    crusade_.join_crusade(pid, war_faction::aresden);
    crusade_.award_contribution(pid, 1000);

    crusade_.set_broadcast_fn([](player_id, const hb::network::json_message&) {});

    crusade_.end_crusade(war_faction::aresden);

    // Level 50 winner with 1000 contribution: exp = 1000 + 5000 = 6000
    EXPECT_EQ(plr->experience.experience, exp_before + 6000);

    player_sys.shutdown();
}

// ========== Crusade Reward Formula Tests ==========

TEST(crusade_reward_test, winner_level_50_full_contribution) {
    auto reward = calculate_crusade_reward(1000, 50, war_faction::aresden, war_faction::aresden);
    EXPECT_EQ(reward.experience, 6000);
    EXPECT_TRUE(reward.is_winner);
    EXPECT_FALSE(reward.is_draw);
    EXPECT_EQ(reward.war_contribution_used, 1000);
}

TEST(crusade_reward_test, loser_level_50_tenth_contribution) {
    auto reward = calculate_crusade_reward(1000, 50, war_faction::elvine, war_faction::aresden);
    EXPECT_EQ(reward.experience, 600);
    EXPECT_FALSE(reward.is_winner);
    EXPECT_FALSE(reward.is_draw);
}

TEST(crusade_reward_test, draw_level_50_sixth_contribution) {
    auto reward = calculate_crusade_reward(1000, 50, war_faction::aresden, war_faction::neutral);
    EXPECT_EQ(reward.experience, 1000);
    EXPECT_FALSE(reward.is_winner);
    EXPECT_TRUE(reward.is_draw);
}

TEST(crusade_reward_test, level_90_mid_bracket) {
    auto reward = calculate_crusade_reward(1000, 90, war_faction::aresden, war_faction::aresden);
    EXPECT_EQ(reward.experience, 4600);
    EXPECT_TRUE(reward.is_winner);
}

TEST(crusade_reward_test, level_120_high_bracket) {
    auto reward = calculate_crusade_reward(1000, 120, war_faction::aresden, war_faction::aresden);
    EXPECT_EQ(reward.experience, 1120);
    EXPECT_TRUE(reward.is_winner);
}

TEST(crusade_reward_test, level_80_boundary) {
    auto reward = calculate_crusade_reward(500, 80, war_faction::aresden, war_faction::aresden);
    EXPECT_EQ(reward.experience, 8500);
}

TEST(crusade_reward_test, level_81_boundary) {
    auto reward = calculate_crusade_reward(500, 81, war_faction::aresden, war_faction::aresden);
    EXPECT_EQ(reward.experience, 3740);
}

TEST(crusade_reward_test, level_100_boundary) {
    auto reward = calculate_crusade_reward(500, 100, war_faction::aresden, war_faction::aresden);
    EXPECT_EQ(reward.experience, 4500);
}

TEST(crusade_reward_test, level_101_boundary) {
    auto reward = calculate_crusade_reward(500, 101, war_faction::aresden, war_faction::aresden);
    EXPECT_EQ(reward.experience, 601);
}

TEST(crusade_reward_test, zero_contribution) {
    auto reward = calculate_crusade_reward(0, 50, war_faction::aresden, war_faction::aresden);
    EXPECT_EQ(reward.experience, 5000);
}

TEST(crusade_reward_test, zero_level) {
    auto reward = calculate_crusade_reward(1000, 0, war_faction::aresden, war_faction::aresden);
    EXPECT_EQ(reward.experience, 1000);
}

TEST(crusade_reward_test, no_gold_field) {
    auto reward = calculate_crusade_reward(10000, 80, war_faction::aresden, war_faction::aresden);
    EXPECT_GT(reward.experience, 0);
}

// ========== Build System Tests ==========

class crusade_build_test : public ::testing::Test {
protected:
    void SetUp() override {
        war_sys_.initialize();
        social_.initialize();
        crusade_.initialize();
        crusade_.set_dependencies(&war_sys_, nullptr, nullptr, nullptr, nullptr);
        crusade_.set_social(&social_);

        crusade_config cfg;
        cfg.enabled = true;
        cfg.timing.duration_seconds = 3600;

        strike_point sp1;
        sp1.id = 1;
        sp1.max_hp = 100;
        cfg.aresden_strike_points.push_back(sp1);

        strike_point sp2;
        sp2.id = 1;
        sp2.max_hp = 100;
        cfg.elvine_strike_points.push_back(sp2);

        crusade_.set_config(cfg);

        // Create guild with commander as guild master, constructor as member
        auto guild_result = social_.create_guild(commander_, "TestGuild", "TG");
        gid_ = guild_result.value();
        social_.join_guild(constructor_, gid_);
    }

    void TearDown() override {
        crusade_.shutdown();
        social_.shutdown();
        war_sys_.shutdown();
    }

    void start_and_join() {
        crusade_.start_crusade();
        crusade_.join_crusade(commander_, war_faction::aresden);
        crusade_.join_crusade(constructor_, war_faction::aresden);
        crusade_.select_duty(commander_, crusade_duty::commander);
        crusade_.select_duty(constructor_, crusade_duty::constructor);
    }

    war_system war_sys_;
    hb::social::social_system social_;
    crusade_system crusade_;

    player_id commander_{1};
    player_id constructor_{2};
    hb::guild_id gid_{};
};

// -- Initial structure spawning --

TEST(crusade_build_types_test, initial_structure_defaults) {
    initial_structure is;
    EXPECT_EQ(is.npc_type, 0);
    EXPECT_EQ(is.x, 0);
    EXPECT_EQ(is.y, 0);
    EXPECT_EQ(is.faction, war_faction::neutral);
}

TEST_F(crusade_system_test, initial_structures_in_config) {
    crusade_config cfg = crusade_.config();

    initial_structure is;
    is.map_name = "middleland";
    is.npc_type = 36;  // AGT
    is.x = 100;
    is.y = 200;
    is.faction = war_faction::aresden;
    cfg.initial_structures.push_back(is);

    crusade_.set_config(cfg);

    // Without world/npc systems, structures won't actually spawn
    crusade_.start_crusade();
    // But the config should be stored
    EXPECT_EQ(crusade_.config().initial_structures.size(), 1);
    EXPECT_EQ(crusade_.config().initial_structures[0].npc_type, 36);
}

// -- Guild construct location --

TEST_F(crusade_build_test, set_construct_location_requires_active) {
    auto result = crusade_.set_guild_construct_location(commander_, "map1", 50, 50);
    EXPECT_EQ(result, crusade_result::not_active);
}

TEST_F(crusade_build_test, set_construct_location_requires_commander) {
    start_and_join();

    // Constructor can't set location
    auto result = crusade_.set_guild_construct_location(constructor_, "map1", 50, 50);
    EXPECT_EQ(result, crusade_result::not_guild_master);
}

TEST_F(crusade_build_test, set_construct_location_success) {
    start_and_join();

    auto result = crusade_.set_guild_construct_location(commander_, "war_map", 100, 200);
    EXPECT_EQ(result, crusade_result::success);

    auto* loc = crusade_.get_guild_construct_location(gid_.value);
    ASSERT_NE(loc, nullptr);
    EXPECT_EQ(loc->map_name, "war_map");
    EXPECT_EQ(loc->x, 100);
    EXPECT_EQ(loc->y, 200);
    EXPECT_EQ(loc->structure_count, 0);
}

TEST_F(crusade_build_test, construct_location_can_be_updated) {
    start_and_join();

    crusade_.set_guild_construct_location(commander_, "map1", 10, 20);
    crusade_.set_guild_construct_location(commander_, "map2", 30, 40);

    auto* loc = crusade_.get_guild_construct_location(gid_.value);
    ASSERT_NE(loc, nullptr);
    EXPECT_EQ(loc->map_name, "map2");
    EXPECT_EQ(loc->x, 30);
    EXPECT_EQ(loc->y, 40);
    EXPECT_EQ(loc->structure_count, 0);  // Reset on update
}

// -- Structure placement: construct location required --

TEST_F(crusade_build_test, structure_requires_construct_location) {
    start_and_join();
    crusade_.award_construction_points(constructor_, 5000);

    auto result = crusade_.summon_war_unit(constructor_, war_unit_type::agt, "map1", 50, 50);
    EXPECT_EQ(result, crusade_result::no_construct_location);
}

TEST_F(crusade_build_test, mobile_unit_does_not_require_construct_location) {
    start_and_join();
    crusade_.award_construction_points(constructor_, 5000);

    auto result = crusade_.summon_war_unit(constructor_, war_unit_type::lwb, "map1", 50, 50);
    EXPECT_EQ(result, crusade_result::success);
}

// -- Structure placement: within 10 tiles --

TEST_F(crusade_build_test, structure_within_10_tiles_succeeds) {
    start_and_join();

    crusade_.set_guild_construct_location(commander_, "map1", 100, 100);

    auto result = crusade_.summon_war_unit(constructor_, war_unit_type::agt, "map1", 105, 105);
    EXPECT_EQ(result, crusade_result::success);
}

TEST_F(crusade_build_test, structure_beyond_10_tiles_rejected) {
    start_and_join();

    crusade_.set_guild_construct_location(commander_, "map1", 100, 100);

    auto result = crusade_.summon_war_unit(constructor_, war_unit_type::agt, "map1", 111, 100);
    EXPECT_EQ(result, crusade_result::too_far_from_construct_location);
}

TEST_F(crusade_build_test, structure_at_exactly_10_tiles_succeeds) {
    start_and_join();

    crusade_.set_guild_construct_location(commander_, "map1", 100, 100);

    auto result = crusade_.summon_war_unit(constructor_, war_unit_type::detector, "map1", 110, 110);
    EXPECT_EQ(result, crusade_result::success);
}

// -- Per-guild build limit --

TEST_F(crusade_build_test, guild_build_limit_10) {
    start_and_join();

    crusade_.set_guild_construct_location(commander_, "map1", 100, 400);

    // Build 10 structures (all allowed)
    for (int i = 0; i < 10; ++i)
    {
        auto result = crusade_.summon_war_unit(constructor_, war_unit_type::detector,
            "map1", static_cast<int16_t>(100 + i), 400);
        EXPECT_EQ(result, crusade_result::success) << "Build " << i << " should succeed";
    }

    // 11th should fail
    auto result = crusade_.summon_war_unit(constructor_, war_unit_type::detector, "map1", 100, 400);
    EXPECT_EQ(result, crusade_result::guild_build_limit);
}

// -- Guard tower proximity --

TEST_F(crusade_build_test, guard_tower_proximity_2_tiles) {
    start_and_join();

    crusade_.set_guild_construct_location(commander_, "map1", 100, 400);

    // Place first tower
    auto r1 = crusade_.summon_war_unit(constructor_, war_unit_type::agt, "map1", 100, 400);
    EXPECT_EQ(r1, crusade_result::success);

    // Place second tower within 2 tiles — rejected
    auto r2 = crusade_.summon_war_unit(constructor_, war_unit_type::cgt, "map1", 101, 401);
    EXPECT_EQ(r2, crusade_result::too_close_to_tower);
}

TEST_F(crusade_build_test, guard_tower_at_3_tiles_succeeds) {
    start_and_join();

    crusade_.set_guild_construct_location(commander_, "map1", 100, 400);

    auto r1 = crusade_.summon_war_unit(constructor_, war_unit_type::agt, "map1", 97, 400);
    EXPECT_EQ(r1, crusade_result::success);

    // 3 tiles away — should succeed
    auto r2 = crusade_.summon_war_unit(constructor_, war_unit_type::cgt, "map1", 100, 400);
    EXPECT_EQ(r2, crusade_result::success);
}

TEST_F(crusade_build_test, non_tower_structure_ignores_proximity) {
    start_and_join();

    crusade_.set_guild_construct_location(commander_, "map1", 100, 400);

    auto r1 = crusade_.summon_war_unit(constructor_, war_unit_type::agt, "map1", 100, 400);
    EXPECT_EQ(r1, crusade_result::success);

    // Detector right next to tower — allowed (not a tower)
    auto r2 = crusade_.summon_war_unit(constructor_, war_unit_type::detector, "map1", 101, 400);
    EXPECT_EQ(r2, crusade_result::success);
}

// -- Guard tower Y-bounds --

TEST_F(crusade_build_test, guard_tower_y_too_low) {
    start_and_join();

    crusade_.set_guild_construct_location(commander_, "map1", 100, 30);

    auto result = crusade_.summon_war_unit(constructor_, war_unit_type::agt, "map1", 100, 32);
    EXPECT_EQ(result, crusade_result::invalid_position);
}

TEST_F(crusade_build_test, guard_tower_y_too_high) {
    start_and_join();

    crusade_.set_guild_construct_location(commander_, "map1", 100, 785);

    auto result = crusade_.summon_war_unit(constructor_, war_unit_type::cgt, "map1", 100, 783);
    EXPECT_EQ(result, crusade_result::invalid_position);
}

TEST_F(crusade_build_test, guard_tower_y_at_boundary_ok) {
    start_and_join();

    crusade_.set_guild_construct_location(commander_, "map1", 100, 40);

    auto result = crusade_.summon_war_unit(constructor_, war_unit_type::agt, "map1", 100, 33);
    EXPECT_EQ(result, crusade_result::success);
}

// -- Construction costs --

TEST(crusade_cost_test, structures_zero_cost) {
    EXPECT_EQ(get_construction_cost(war_unit_type::agt), 0);
    EXPECT_EQ(get_construction_cost(war_unit_type::cgt), 0);
    EXPECT_EQ(get_construction_cost(war_unit_type::mana_collector), 0);
    EXPECT_EQ(get_construction_cost(war_unit_type::detector), 0);
}

TEST(crusade_cost_test, mobile_units_legacy_costs) {
    EXPECT_EQ(get_construction_cost(war_unit_type::lwb), 1000);
    EXPECT_EQ(get_construction_cost(war_unit_type::ghk), 2000);
    EXPECT_EQ(get_construction_cost(war_unit_type::ghkabs), 3000);
    EXPECT_EQ(get_construction_cost(war_unit_type::tk), 2000);
    EXPECT_EQ(get_construction_cost(war_unit_type::bg), 3000);
    EXPECT_EQ(get_construction_cost(war_unit_type::catapult), 1500);
}

TEST(crusade_cost_test, esg_not_summonable) {
    EXPECT_LT(get_construction_cost(war_unit_type::esg), 0);
}

TEST_F(crusade_build_test, structure_zero_cost_no_deduction) {
    start_and_join();
    crusade_.award_construction_points(constructor_, 5000);

    crusade_.set_guild_construct_location(commander_, "map1", 100, 400);

    crusade_.summon_war_unit(constructor_, war_unit_type::detector, "map1", 100, 400);

    auto* data = crusade_.get_player_data(constructor_);
    EXPECT_EQ(data->construction_points, 5000);  // No deduction for structures
}

// -- Map restrictions --

TEST_F(crusade_build_test, toh3_restricted) {
    start_and_join();
    crusade_.award_construction_points(constructor_, 5000);

    auto result = crusade_.summon_war_unit(constructor_, war_unit_type::lwb, "toh3", 50, 50);
    EXPECT_EQ(result, crusade_result::restricted_map);
}

TEST_F(crusade_build_test, icebound_restricted) {
    start_and_join();
    crusade_.award_construction_points(constructor_, 5000);

    auto result = crusade_.summon_war_unit(constructor_, war_unit_type::lwb, "icebound", 50, 50);
    EXPECT_EQ(result, crusade_result::restricted_map);
}

// -- ESG rectangular query --

TEST(crusade_esg_query_test, rectangular_includes_corners) {
    // Verify the is_structure_type / is_mobile_unit_type / is_guard_tower_type helpers
    EXPECT_TRUE(is_structure_type(war_unit_type::agt));
    EXPECT_TRUE(is_structure_type(war_unit_type::cgt));
    EXPECT_TRUE(is_structure_type(war_unit_type::mana_collector));
    EXPECT_TRUE(is_structure_type(war_unit_type::detector));
    EXPECT_FALSE(is_structure_type(war_unit_type::lwb));
    EXPECT_FALSE(is_structure_type(war_unit_type::esg));

    EXPECT_TRUE(is_mobile_unit_type(war_unit_type::lwb));
    EXPECT_TRUE(is_mobile_unit_type(war_unit_type::ghk));
    EXPECT_TRUE(is_mobile_unit_type(war_unit_type::ghkabs));
    EXPECT_TRUE(is_mobile_unit_type(war_unit_type::tk));
    EXPECT_TRUE(is_mobile_unit_type(war_unit_type::bg));
    EXPECT_TRUE(is_mobile_unit_type(war_unit_type::catapult));
    EXPECT_FALSE(is_mobile_unit_type(war_unit_type::agt));

    EXPECT_TRUE(is_guard_tower_type(war_unit_type::agt));
    EXPECT_TRUE(is_guard_tower_type(war_unit_type::cgt));
    EXPECT_FALSE(is_guard_tower_type(war_unit_type::detector));
}

TEST_F(crusade_system_test, esg_at_corner_counts_with_rectangular) {
    crusade_config cfg = crusade_.config();
    cfg.elvine_strike_points[0].map_name = "war_map";
    cfg.elvine_strike_points[0].x = 50;
    cfg.elvine_strike_points[0].y = 50;
    crusade_.set_config(cfg);

    mana_config mc;
    mc.gmg_charges_for_meteor = 1;
    crusade_.set_mana_config(mc);

    crusade_.start_crusade();

    // Place ESG at exactly (60, 60) — at corner, 10 tiles away in both axes
    // Circular: distance = sqrt(200) ≈ 14.14 > 10 → NOT counted
    // Rectangular: abs(10) <= 10 && abs(10) <= 10 → counted
    war_structure_instance esg;
    esg.type = war_unit_type::esg;
    esg.faction = war_faction::elvine;
    esg.map_name = "war_map";
    esg.x = 60;
    esg.y = 60;
    crusade_.add_war_structure(esg);

    // Fire meteor and check protection
    crusade_.mana().add_mana(war_faction::aresden, 15);

    const auto& elvine = crusade_.get_strike_points(war_faction::elvine);
    // ESG at corner should be counted (rectangular), reducing damage by 1
    EXPECT_EQ(elvine[0].hp, 99);  // 100 - max(0, 2-1) = 99
}

// -- NPC type mapping for new types --

TEST(crusade_npc_type_test, shared_unit_types_same_for_both_factions) {
    // GHK, GHKABS, TK, BG are shared — same NPC ID regardless of faction
    EXPECT_EQ(get_npc_id_for_unit(war_unit_type::ghk, war_faction::aresden), 79);
    EXPECT_EQ(get_npc_id_for_unit(war_unit_type::ghk, war_faction::elvine), 79);
    EXPECT_EQ(get_npc_id_for_unit(war_unit_type::ghkabs, war_faction::aresden), 80);
    EXPECT_EQ(get_npc_id_for_unit(war_unit_type::ghkabs, war_faction::elvine), 80);
    EXPECT_EQ(get_npc_id_for_unit(war_unit_type::tk, war_faction::aresden), 81);
    EXPECT_EQ(get_npc_id_for_unit(war_unit_type::tk, war_faction::elvine), 81);
    EXPECT_EQ(get_npc_id_for_unit(war_unit_type::bg, war_faction::aresden), 82);
    EXPECT_EQ(get_npc_id_for_unit(war_unit_type::bg, war_faction::elvine), 82);
}

// -- Construct location cleared on crusade end --

TEST_F(crusade_build_test, construct_locations_cleared_on_end) {
    start_and_join();

    crusade_.set_guild_construct_location(commander_, "map1", 100, 100);
    EXPECT_NE(crusade_.get_guild_construct_location(gid_.value), nullptr);

    crusade_.end_crusade(war_faction::neutral);

    EXPECT_EQ(crusade_.get_guild_construct_location(gid_.value), nullptr);
}

// -- ESG not summonable --

TEST_F(crusade_build_test, esg_summon_rejected) {
    start_and_join();
    crusade_.award_construction_points(constructor_, 5000);

    auto result = crusade_.summon_war_unit(constructor_, war_unit_type::esg, "map1", 50, 50);
    EXPECT_EQ(result, crusade_result::invalid_unit);
}

// ========== Lifecycle Fix Tests ==========

// -- Duplicate-in-day prevention --

TEST_F(crusade_system_test, duplicate_in_day_prevented) {
    // First crusade starts successfully
    auto r1 = crusade_.start_crusade();
    ASSERT_TRUE(r1.is_ok());
    EXPECT_TRUE(crusade_.is_active());
    EXPECT_GE(crusade_.last_crusade_day(), 0);

    // End it
    crusade_.end_crusade(war_faction::aresden);
    EXPECT_FALSE(crusade_.is_active());

    // Second start on same day should fail
    auto r2 = crusade_.start_crusade();
    ASSERT_TRUE(r2.is_err());
    EXPECT_EQ(r2.error(), crusade_result::duplicate_today);
    EXPECT_FALSE(crusade_.is_active());
}

TEST_F(crusade_system_test, different_day_allows_start) {
    // Simulate previous crusade was on a different day
    crusade_.set_last_crusade_day(6);  // Saturday

    // As long as today isn't Saturday, start should succeed
    // We verify by starting — if today IS Saturday, the test is still valid
    // because last_crusade_day was set before any start
    auto r = crusade_.start_crusade();
    auto now = std::chrono::system_clock::now();
    auto time_t_now = std::chrono::system_clock::to_time_t(now);
    std::tm local_tm{};
    localtime_r(&time_t_now, &local_tm);

    if (local_tm.tm_wday == 6) {
        // Today IS Saturday, so duplicate prevention kicks in
        EXPECT_TRUE(r.is_err());
    } else {
        EXPECT_TRUE(r.is_ok());
    }
}

TEST_F(crusade_system_test, first_start_always_succeeds) {
    // With last_crusade_day_ == -1 (default), any day should work
    EXPECT_EQ(crusade_.last_crusade_day(), -1);
    auto r = crusade_.start_crusade();
    EXPECT_TRUE(r.is_ok());
}

// -- Victory deferred to meteor result phase --

TEST_F(crusade_system_test, victory_deferred_to_meteor_result) {
    // Set up crusade with meteor handler (no scheduler = synchronous execution)
    crusade_.start_crusade();

    // Destroy all aresden strike points directly — should NOT end crusade
    crusade_.damage_strike_point(war_faction::aresden, 1, 999);
    crusade_.damage_strike_point(war_faction::aresden, 2, 999);
    EXPECT_TRUE(crusade_.is_active());
    EXPECT_TRUE(crusade_.all_strike_points_destroyed(war_faction::aresden));

    // Now fire meteor — the result callback should trigger victory check
    // Without scheduler, meteor executes synchronously (impact → waves → result)
    crusade_.meteor().fire_meteor(war_faction::elvine);  // Elvine attacks aresden

    // Meteor result callback runs check_victory_condition → ends crusade
    EXPECT_FALSE(crusade_.is_active());
    EXPECT_EQ(crusade_.last_winner(), war_faction::elvine);
}

TEST_F(crusade_system_test, partial_destruction_no_victory_even_after_meteor) {
    crusade_.start_crusade();

    // Only destroy ONE of two aresden strike points
    crusade_.damage_strike_point(war_faction::aresden, 1, 999);
    EXPECT_TRUE(crusade_.is_active());

    // Fire meteor — result callback checks victory, but not all points destroyed
    crusade_.meteor().fire_meteor(war_faction::elvine);
    EXPECT_TRUE(crusade_.is_active());
}

// -- Last winner persistence --

TEST_F(crusade_system_test, last_winner_set_on_end) {
    crusade_.start_crusade();
    EXPECT_EQ(crusade_.last_winner(), war_faction::neutral);

    crusade_.end_crusade(war_faction::aresden);
    EXPECT_EQ(crusade_.last_winner(), war_faction::aresden);
}

// -- Map disabling on strike point destruction --

class crusade_map_disable_test : public ::testing::Test {
protected:
    void SetUp() override {
        war_sys_.initialize();
        world_.initialize();

        // Create maps
        hb::world::map_config mc;
        mc.name = "aresden_city";
        mc.width = 100;
        mc.height = 100;
        world_.create_map(mc);

        mc.name = "elvine_city";
        world_.create_map(mc);

        crusade_.initialize();
        crusade_.set_dependencies(&war_sys_, nullptr, &world_, nullptr, nullptr);

        // Config with linked_map on strike points
        crusade_config cfg;
        cfg.enabled = true;
        cfg.timing.duration_seconds = 3600;

        strike_point sp1;
        sp1.id = 1;
        sp1.max_hp = 100;
        sp1.linked_map = "aresden_city";
        cfg.aresden_strike_points.push_back(sp1);

        strike_point sp2;
        sp2.id = 1;
        sp2.max_hp = 100;
        sp2.linked_map = "elvine_city";
        cfg.elvine_strike_points.push_back(sp2);

        crusade_.set_config(cfg);
    }

    void TearDown() override {
        crusade_.shutdown();
        world_.shutdown();
        war_sys_.shutdown();
    }

    war_system war_sys_;
    hb::world::world_subsystem world_;
    crusade_system crusade_;
};

TEST_F(crusade_map_disable_test, map_disabled_on_strike_point_destruction) {
    crusade_.start_crusade();

    EXPECT_FALSE(world_.is_map_disabled("aresden_city"));

    crusade_.damage_strike_point(war_faction::aresden, 1, 999);

    EXPECT_TRUE(world_.is_map_disabled("aresden_city"));
    EXPECT_FALSE(world_.is_map_disabled("elvine_city"));
}

TEST_F(crusade_map_disable_test, map_not_disabled_on_partial_damage) {
    crusade_.start_crusade();

    crusade_.damage_strike_point(war_faction::aresden, 1, 10);

    EXPECT_FALSE(world_.is_map_disabled("aresden_city"));
}

TEST_F(crusade_map_disable_test, maps_reenabled_on_crusade_end) {
    crusade_.start_crusade();

    crusade_.damage_strike_point(war_faction::aresden, 1, 999);
    EXPECT_TRUE(world_.is_map_disabled("aresden_city"));

    crusade_.end_crusade(war_faction::elvine);

    EXPECT_FALSE(world_.is_map_disabled("aresden_city"));
    EXPECT_FALSE(world_.is_map_disabled("elvine_city"));
}

TEST_F(crusade_map_disable_test, maps_reenabled_on_cancel) {
    crusade_.start_crusade();

    crusade_.damage_strike_point(war_faction::aresden, 1, 999);
    EXPECT_TRUE(world_.is_map_disabled("aresden_city"));

    crusade_.cancel_crusade();

    EXPECT_FALSE(world_.is_map_disabled("aresden_city"));
}

TEST_F(crusade_map_disable_test, strike_point_without_linked_map_does_not_crash) {
    // Create config with no linked_map
    crusade_config cfg;
    cfg.enabled = true;
    cfg.timing.duration_seconds = 3600;

    strike_point sp;
    sp.id = 1;
    sp.max_hp = 50;
    // linked_map is empty
    cfg.aresden_strike_points.push_back(sp);

    strike_point sp2;
    sp2.id = 1;
    sp2.max_hp = 50;
    cfg.elvine_strike_points.push_back(sp2);

    crusade_.set_config(cfg);
    crusade_.start_crusade();
    crusade_.damage_strike_point(war_faction::aresden, 1, 999);
    // Should not crash — no map to disable
    crusade_.end_crusade(war_faction::elvine);
}

// -- Offline player reward persistence (character_id stored at join) --

TEST_F(crusade_system_test, character_id_stored_at_join) {
    hb::player::player_system player_sys;
    player_sys.initialize();
    crusade_.set_dependencies(&war_sys_, &player_sys, nullptr, nullptr, nullptr);

    hb::player::player_create_info info;
    info.name = "test_char";
    info.account_name = "test_acct";
    auto pid = player_sys.create_player(info).value();
    auto* plr = player_sys.get_player(pid);
    plr->character_id = player_id{42};
    plr->experience.level = 75;
    plr->faction = hb::faction::aresden;

    crusade_.start_crusade();
    crusade_.join_crusade(pid, war_faction::aresden);

    auto* pdata = crusade_.get_player_data(pid);
    ASSERT_NE(pdata, nullptr);
    EXPECT_EQ(pdata->character_id, 42);
    EXPECT_EQ(pdata->level, 75);

    crusade_.end_crusade(war_faction::neutral);
    player_sys.shutdown();
}

TEST_F(crusade_system_test, character_id_zero_without_player_system) {
    // No player system wired
    crusade_.set_dependencies(&war_sys_, nullptr, nullptr, nullptr, nullptr);
    crusade_.start_crusade();
    crusade_.join_crusade(player_id{1}, war_faction::aresden);

    auto* pdata = crusade_.get_player_data(player_id{1});
    ASSERT_NE(pdata, nullptr);
    EXPECT_EQ(pdata->character_id, 0);
    EXPECT_EQ(pdata->level, 1);

    crusade_.end_crusade(war_faction::neutral);
}

// ========== Bug Fix Tests ==========

// Fix 1: Summon types 8-11 should succeed (GHK=8, GHKABS=9, TK=10, BG=11)
TEST_F(crusade_system_test, summon_mobile_units_types_8_to_11) {
    crusade_.start_crusade();

    player_id pid(1);
    crusade_.join_crusade(pid, war_faction::aresden);
    crusade_.select_duty(pid, crusade_duty::constructor);
    crusade_.award_construction_points(pid, 30000);

    // GHK (type 8, cost 2000)
    EXPECT_EQ(crusade_.summon_war_unit(pid, war_unit_type::ghk), crusade_result::success);
    // GHKABS (type 9, cost 3000)
    EXPECT_EQ(crusade_.summon_war_unit(pid, war_unit_type::ghkabs), crusade_result::success);
    // TK (type 10, cost 2000)
    EXPECT_EQ(crusade_.summon_war_unit(pid, war_unit_type::tk), crusade_result::success);
    // BG (type 11, cost 3000)
    EXPECT_EQ(crusade_.summon_war_unit(pid, war_unit_type::bg), crusade_result::success);

    EXPECT_EQ(crusade_.get_war_structures().size(), 4);
}

// Fix 2: Cross-map structure build rejected
TEST_F(crusade_build_test, cross_map_structure_rejected) {
    start_and_join();

    // Commander sets construct location on "middleland"
    crusade_.set_guild_construct_location(commander_, "middleland", 100, 100);

    // Constructor tries to build on a different map
    crusade_.award_construction_points(constructor_, 5000);
    auto result = crusade_.summon_war_unit(constructor_, war_unit_type::agt,
        "aresden", 100, 100);
    EXPECT_EQ(result, crusade_result::wrong_map);
}

TEST_F(crusade_build_test, same_map_structure_accepted) {
    start_and_join();

    crusade_.set_guild_construct_location(commander_, "middleland", 100, 100);
    crusade_.award_construction_points(constructor_, 5000);

    auto result = crusade_.summon_war_unit(constructor_, war_unit_type::agt,
        "middleland", 100, 100);
    EXPECT_EQ(result, crusade_result::success);
}

// Fix 3: MP restore deducts mana from pool; no restore when pool empty
TEST_F(crusade_system_test, mp_restore_requires_mana_pool) {
    // The mana system's try_consume should work correctly
    mana_system mana;
    mana_config cfg;
    cfg.gmg_mana_threshold = 15;
    mana.set_config(cfg);
    mana.initialize_stones(0);

    // No mana in pool — try_consume should fail
    EXPECT_FALSE(mana.try_consume(war_faction::aresden, 1));

    // Add some mana
    mana.add_mana(war_faction::aresden, 5);
    EXPECT_TRUE(mana.try_consume(war_faction::aresden, 1));
    EXPECT_EQ(mana.aresden_mana(), 4);

    // Consume remaining
    EXPECT_TRUE(mana.try_consume(war_faction::aresden, 4));
    EXPECT_EQ(mana.aresden_mana(), 0);

    // Pool empty again
    EXPECT_FALSE(mana.try_consume(war_faction::aresden, 1));
}

// Fix 5: NPC ID mapping returns correct sequential IDs (not sprite IDs)
TEST(crusade_npc_id_test, faction_specific_ids_differ) {
    // AGT: Aresden=64, Elvine=65
    EXPECT_NE(get_npc_id_for_unit(war_unit_type::agt, war_faction::aresden),
              get_npc_id_for_unit(war_unit_type::agt, war_faction::elvine));

    // Catapult: Aresden=85, Elvine=86
    EXPECT_NE(get_npc_id_for_unit(war_unit_type::catapult, war_faction::aresden),
              get_npc_id_for_unit(war_unit_type::catapult, war_faction::elvine));
}

TEST(crusade_npc_id_test, ids_are_not_sprite_ids) {
    // The old buggy function returned sprite IDs like 36, 37, etc.
    // New function returns sequential npc_ids ≥ 64
    EXPECT_GE(get_npc_id_for_unit(war_unit_type::agt, war_faction::aresden), 64);
    EXPECT_GE(get_npc_id_for_unit(war_unit_type::cgt, war_faction::aresden), 64);
    EXPECT_GE(get_npc_id_for_unit(war_unit_type::mana_collector, war_faction::aresden), 64);
}

// ========== Crusade Advantage Tests ==========

TEST_F(crusade_system_test, advantage_increments_toward_aresden_winner) {
    EXPECT_EQ(crusade_.crusade_advantage(), 0);

    crusade_.start_crusade();
    crusade_.end_crusade(war_faction::aresden);
    EXPECT_EQ(crusade_.crusade_advantage(), 1);

    crusade_.set_last_crusade_day(-1);  // Bypass duplicate-day check
    crusade_.start_crusade();
    crusade_.end_crusade(war_faction::aresden);
    EXPECT_EQ(crusade_.crusade_advantage(), 2);
}

TEST_F(crusade_system_test, advantage_increments_toward_elvine_winner) {
    EXPECT_EQ(crusade_.crusade_advantage(), 0);

    crusade_.start_crusade();
    crusade_.end_crusade(war_faction::elvine);
    EXPECT_EQ(crusade_.crusade_advantage(), -1);

    crusade_.set_last_crusade_day(-1);
    crusade_.start_crusade();
    crusade_.end_crusade(war_faction::elvine);
    EXPECT_EQ(crusade_.crusade_advantage(), -2);
}

TEST_F(crusade_system_test, advantage_caps_at_plus_minus_5) {
    crusade_.set_crusade_advantage(4);

    crusade_.start_crusade();
    crusade_.end_crusade(war_faction::aresden);
    EXPECT_EQ(crusade_.crusade_advantage(), 5);

    // Beyond 5 should stay at 5
    crusade_.set_last_crusade_day(-1);
    crusade_.start_crusade();
    crusade_.end_crusade(war_faction::aresden);
    EXPECT_EQ(crusade_.crusade_advantage(), 5);
}

TEST_F(crusade_system_test, advantage_caps_at_minus_5) {
    crusade_.set_crusade_advantage(-4);

    crusade_.start_crusade();
    crusade_.end_crusade(war_faction::elvine);
    EXPECT_EQ(crusade_.crusade_advantage(), -5);

    crusade_.set_last_crusade_day(-1);
    crusade_.start_crusade();
    crusade_.end_crusade(war_faction::elvine);
    EXPECT_EQ(crusade_.crusade_advantage(), -5);
}

TEST_F(crusade_system_test, advantage_no_change_on_draw) {
    crusade_.set_crusade_advantage(3);

    crusade_.start_crusade();
    crusade_.end_crusade(war_faction::neutral);  // Draw
    EXPECT_EQ(crusade_.crusade_advantage(), 3);  // Unchanged
}

TEST(mana_advantage_test, advantage_adjusts_gmg_threshold) {
    mana_system mana;
    mana_config cfg;
    cfg.gmg_mana_threshold = 15;
    cfg.gmg_charges_for_meteor = 100;  // High so we don't trigger meteor
    mana.set_config(cfg);
    mana.initialize_stones(0);

    // Aresden dominant: needs 15 + 16 = 31 mana per charge
    mana.set_threshold_adjustments(16, 0);

    // Add 15 mana to Aresden — should NOT convert to charge (threshold is 31)
    mana.add_mana(war_faction::aresden, 15);
    EXPECT_EQ(mana.get_state(war_faction::aresden).gmg_charge, 0);
    EXPECT_EQ(mana.aresden_mana(), 15);

    // Add 16 more — now at 31, should convert
    mana.add_mana(war_faction::aresden, 16);
    EXPECT_EQ(mana.get_state(war_faction::aresden).gmg_charge, 1);
    EXPECT_EQ(mana.aresden_mana(), 0);

    // Elvine has no adjustment — 15 mana converts to charge
    mana.add_mana(war_faction::elvine, 15);
    EXPECT_EQ(mana.get_state(war_faction::elvine).gmg_charge, 1);
}

TEST(mana_advantage_test, threshold_adjustments_reset_on_reset) {
    mana_system mana;
    mana_config cfg;
    cfg.gmg_mana_threshold = 15;
    cfg.gmg_charges_for_meteor = 100;
    mana.set_config(cfg);
    mana.initialize_stones(0);

    mana.set_threshold_adjustments(16, 0);
    mana.reset();
    mana.initialize_stones(0);

    // After reset, threshold should be back to base 15
    mana.add_mana(war_faction::aresden, 15);
    EXPECT_EQ(mana.get_state(war_faction::aresden).gmg_charge, 1);
}

// ========== Guild Teleport Tests ==========

TEST_F(crusade_build_test, commander_can_set_teleport_location) {
    start_and_join();

    auto result = crusade_.set_guild_teleport_location(commander_, "middleland", 50, 60);
    EXPECT_EQ(result, crusade_result::success);

    auto* loc = crusade_.get_guild_teleport_location(gid_.value);
    ASSERT_NE(loc, nullptr);
    EXPECT_EQ(loc->map_name, "middleland");
    EXPECT_EQ(loc->x, 50);
    EXPECT_EQ(loc->y, 60);
}

TEST_F(crusade_build_test, non_commander_cannot_set_teleport) {
    start_and_join();

    // constructor_ has constructor duty, not commander
    auto result = crusade_.set_guild_teleport_location(constructor_, "middleland", 50, 60);
    EXPECT_EQ(result, crusade_result::not_guild_master);
}

TEST_F(crusade_build_test, guild_member_can_use_teleport) {
    start_and_join();

    crusade_.set_guild_teleport_location(commander_, "middleland", 50, 60);

    auto result = crusade_.use_guild_teleport(constructor_);
    EXPECT_EQ(result, crusade_result::success);

    auto* dest = crusade_.get_guild_teleport_dest(constructor_);
    ASSERT_NE(dest, nullptr);
    EXPECT_EQ(dest->map_name, "middleland");
    EXPECT_EQ(dest->x, 50);
    EXPECT_EQ(dest->y, 60);
}

TEST_F(crusade_build_test, use_teleport_fails_without_set) {
    start_and_join();

    auto result = crusade_.use_guild_teleport(constructor_);
    EXPECT_EQ(result, crusade_result::no_construct_location);
}

TEST_F(crusade_build_test, teleport_locations_cleared_at_crusade_end) {
    start_and_join();

    crusade_.set_guild_teleport_location(commander_, "middleland", 50, 60);
    EXPECT_NE(crusade_.get_guild_teleport_location(gid_.value), nullptr);

    crusade_.end_crusade(war_faction::neutral);

    // After crusade end, teleport locations should be cleared
    // (crusade is no longer active, so we can't query — but the internal map was cleared)
    // Start a new one to verify
    crusade_.start_crusade();
    crusade_.join_crusade(commander_, war_faction::aresden);
    crusade_.select_duty(commander_, crusade_duty::commander);
    EXPECT_EQ(crusade_.get_guild_teleport_location(gid_.value), nullptr);
}

TEST_F(crusade_build_test, teleport_requires_active_crusade) {
    // Don't start crusade
    auto result = crusade_.set_guild_teleport_location(commander_, "middleland", 50, 60);
    EXPECT_EQ(result, crusade_result::not_active);
}
