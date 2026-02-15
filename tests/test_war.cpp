// test_war.cpp
// Unit tests for war system

#include <gtest/gtest.h>
#include "core/types.h"
#include "war/war_types.h"
#include "war/territory.h"
#include "war/war_system.h"

using hb::item_id;
using hb::map_id;
using hb::player_id;
using namespace hb::war;

// War types tests

TEST(war_participant_test, default_state)
{
    war_participant participant;
    EXPECT_EQ(participant.kills, 0);
    EXPECT_EQ(participant.deaths, 0);
    EXPECT_EQ(participant.contribution_score, 0);
    EXPECT_FLOAT_EQ(participant.kd_ratio(), 0.0f);
}

TEST(war_participant_test, record_kill)
{
    war_participant participant;
    participant.record_kill();

    EXPECT_EQ(participant.kills, 1);
    EXPECT_EQ(participant.contribution_score, 10);
    EXPECT_FLOAT_EQ(participant.kd_ratio(), 1.0f);
}

TEST(war_participant_test, record_death)
{
    war_participant participant;
    participant.record_kill();
    participant.record_kill();
    participant.record_death();

    EXPECT_EQ(participant.deaths, 1);
    EXPECT_FLOAT_EQ(participant.kd_ratio(), 2.0f);
}

TEST(war_participant_test, record_assist)
{
    war_participant participant;
    participant.record_assist();

    EXPECT_EQ(participant.assists, 1);
    EXPECT_EQ(participant.contribution_score, 5);
}

TEST(faction_score_test, add_operations)
{
    faction_score score;
    score.faction = war_faction::aresden;

    score.add_kill();
    EXPECT_EQ(score.kills, 1);
    EXPECT_EQ(score.total_score, 1);

    score.add_death();
    EXPECT_EQ(score.deaths, 1);

    score.add_objective();
    EXPECT_EQ(score.objectives, 1);
    EXPECT_EQ(score.total_score, 11); // 1 + 10
}

TEST(war_objective_test, capture_state)
{
    war_objective obj;
    obj.capture_progress = 0;
    obj.controlling_faction = war_faction::aresden;
    obj.capturing_faction = war_faction::neutral;

    EXPECT_FALSE(obj.is_captured());
    EXPECT_FALSE(obj.is_contested());

    obj.capturing_faction = war_faction::elvine;
    obj.capture_progress = 50;
    EXPECT_TRUE(obj.is_contested());
    EXPECT_FALSE(obj.is_captured());

    obj.capture_progress = 100;
    EXPECT_TRUE(obj.is_captured());
}

// Territory tests

TEST(territory_test, contains)
{
    territory t;
    t.min_x = 100;
    t.min_y = 100;
    t.max_x = 200;
    t.max_y = 200;

    EXPECT_TRUE(t.contains(150, 150));
    EXPECT_TRUE(t.contains(100, 100));
    EXPECT_TRUE(t.contains(200, 200));
    EXPECT_FALSE(t.contains(99, 150));
    EXPECT_FALSE(t.contains(201, 150));
}

TEST(territory_test, area)
{
    territory t;
    t.min_x = 0;
    t.min_y = 0;
    t.max_x = 100;
    t.max_y = 50;

    EXPECT_EQ(t.area(), 5000);
}

TEST(faction_territory_summary_test, add_territory)
{
    faction_territory_summary summary;
    summary.faction = war_faction::aresden;

    territory t1;
    t1.type = territory_type::town;
    t1.strategic_value = 100;

    territory t2;
    t2.type = territory_type::outpost;
    t2.strategic_value = 50;

    summary.add_territory(t1);
    summary.add_territory(t2);

    EXPECT_EQ(summary.total_territories, 2);
    EXPECT_EQ(summary.towns, 1);
    EXPECT_EQ(summary.outposts, 1);
    EXPECT_EQ(summary.total_strategic_value, 150);
}

// War state tests

TEST(war_state_test, faction_scores)
{
    war_state war;

    auto& aresden = war.get_faction_score(war_faction::aresden);
    auto& elvine = war.get_faction_score(war_faction::elvine);

    aresden.add_kill();
    aresden.add_kill();
    elvine.add_kill();

    EXPECT_EQ(war.aresden_score.kills, 2);
    EXPECT_EQ(war.elvine_score.kills, 1);
}

TEST(war_state_test, leading_faction)
{
    war_state war;

    EXPECT_EQ(war.get_leading_faction(), war_faction::neutral); // Tie at 0

    war.aresden_score.total_score = 10;
    EXPECT_EQ(war.get_leading_faction(), war_faction::aresden);

    war.elvine_score.total_score = 20;
    EXPECT_EQ(war.get_leading_faction(), war_faction::elvine);
}

TEST(war_state_test, is_active)
{
    war_state war;

    war.state = war_status::inactive;
    EXPECT_FALSE(war.is_active());
    EXPECT_FALSE(war.is_running());

    war.state = war_status::preparing;
    EXPECT_FALSE(war.is_active());
    EXPECT_TRUE(war.is_running());

    war.state = war_status::active;
    EXPECT_TRUE(war.is_active());
    EXPECT_TRUE(war.is_running());

    war.state = war_status::ending;
    EXPECT_FALSE(war.is_active());
    EXPECT_TRUE(war.is_running());

    war.state = war_status::completed;
    EXPECT_FALSE(war.is_active());
    EXPECT_FALSE(war.is_running());
}

// War system tests

class war_system_test : public ::testing::Test
{
protected:
    void SetUp() override
    {
        system_.initialize();

        war_system_config config;
        config.max_concurrent_wars = 3;
        system_.set_config(config);
    }

    void TearDown() override { system_.shutdown(); }

    war_system system_;
};

TEST_F(war_system_test, lifecycle)
{
    EXPECT_TRUE(system_.is_initialized());
    EXPECT_EQ(system_.name(), "war_system");
}

TEST_F(war_system_test, start_war)
{
    auto result = system_.start_war(war_type::crusade);
    ASSERT_TRUE(result.is_ok());

    war_id wid = result.value();
    EXPECT_TRUE(wid.is_valid());
    EXPECT_TRUE(system_.is_war_active(war_type::crusade));
    EXPECT_EQ(system_.active_war_count(), 1);
}

TEST_F(war_system_test, start_duplicate_war_type)
{
    system_.start_war(war_type::crusade);

    auto result = system_.start_war(war_type::crusade);
    EXPECT_FALSE(result.is_ok());
    EXPECT_EQ(result.error(), war_result_code::war_already_running);
}

TEST_F(war_system_test, start_different_war_types)
{
    system_.start_war(war_type::crusade);
    system_.start_war(war_type::heldenian);

    EXPECT_EQ(system_.active_war_count(), 2);
    EXPECT_TRUE(system_.is_war_active(war_type::crusade));
    EXPECT_TRUE(system_.is_war_active(war_type::heldenian));
}

TEST_F(war_system_test, get_active_war)
{
    auto result = system_.start_war(war_type::crusade);
    war_id wid = result.value();

    auto active = system_.get_active_war(war_type::crusade);
    ASSERT_TRUE(active.has_value());
    EXPECT_EQ(active.value(), wid);

    auto no_war = system_.get_active_war(war_type::heldenian);
    EXPECT_FALSE(no_war.has_value());
}

TEST_F(war_system_test, join_war)
{
    auto result = system_.start_war(war_type::crusade);
    war_id wid = result.value();

    auto join_result = system_.join_war(wid, player_id{1}, "Player1", war_faction::aresden);
    EXPECT_EQ(join_result, war_result_code::success);

    const auto* war = system_.get_war(wid);
    EXPECT_TRUE(war->is_participant(player_id{1}));
    EXPECT_EQ(war->participant_count(), 1);
}

TEST_F(war_system_test, join_war_duplicate)
{
    auto result = system_.start_war(war_type::crusade);
    war_id wid = result.value();

    system_.join_war(wid, player_id{1}, "Player1", war_faction::aresden);
    auto join_result = system_.join_war(wid, player_id{1}, "Player1", war_faction::aresden);
    EXPECT_EQ(join_result, war_result_code::already_participant);
}

TEST_F(war_system_test, leave_war)
{
    auto result = system_.start_war(war_type::crusade);
    war_id wid = result.value();

    system_.join_war(wid, player_id{1}, "Player1", war_faction::aresden);
    auto leave_result = system_.leave_war(wid, player_id{1});
    EXPECT_EQ(leave_result, war_result_code::success);

    const auto* war = system_.get_war(wid);
    EXPECT_FALSE(war->is_participant(player_id{1}));
}

TEST_F(war_system_test, record_kill)
{
    auto result = system_.start_war(war_type::crusade);
    war_id wid = result.value();

    // Transition to active state
    auto* war = system_.get_war(wid);
    war->state = war_status::active;

    system_.join_war(wid, player_id{1}, "Killer", war_faction::aresden);
    system_.join_war(wid, player_id{2}, "Victim", war_faction::elvine);

    system_.record_kill(wid, player_id{1}, player_id{2});

    const auto* killer = war->get_participant(player_id{1});
    const auto* victim = war->get_participant(player_id{2});

    EXPECT_EQ(killer->kills, 1);
    EXPECT_EQ(victim->deaths, 1);
    EXPECT_EQ(victim->status, participant_status::dead);

    EXPECT_EQ(war->aresden_score.kills, 1);
    EXPECT_EQ(war->elvine_score.deaths, 1);
}

TEST_F(war_system_test, cancel_war)
{
    auto result = system_.start_war(war_type::crusade);
    war_id wid = result.value();

    auto cancel_result = system_.cancel_war(wid);
    EXPECT_EQ(cancel_result, war_result_code::success);
    EXPECT_FALSE(system_.is_war_active(war_type::crusade));
}

TEST_F(war_system_test, war_started_callback)
{
    bool callback_fired = false;
    system_.on_war_started(
        [&](const war_started_event& event)
        {
            callback_fired = true;
            EXPECT_EQ(event.type, war_type::crusade);
        });

    system_.start_war(war_type::crusade);
    EXPECT_TRUE(callback_fired);
}

TEST_F(war_system_test, player_faction)
{
    system_.set_player_faction(player_id{1}, war_faction::aresden);
    system_.set_player_faction(player_id{2}, war_faction::elvine);

    EXPECT_EQ(system_.get_player_faction(player_id{1}), war_faction::aresden);
    EXPECT_EQ(system_.get_player_faction(player_id{2}), war_faction::elvine);
    EXPECT_EQ(system_.get_player_faction(player_id{3}), war_faction::neutral);
}

TEST_F(war_system_test, player_in_war)
{
    auto result = system_.start_war(war_type::crusade);
    war_id wid = result.value();

    EXPECT_FALSE(system_.is_player_in_war(player_id{1}));

    system_.join_war(wid, player_id{1}, "Player1", war_faction::aresden);
    EXPECT_TRUE(system_.is_player_in_war(player_id{1}));
    EXPECT_EQ(system_.get_player_war(player_id{1}), wid);
}

// Territory tests

TEST_F(war_system_test, register_territory)
{
    territory t;
    t.id = territory_id{1};
    t.name = "Test Territory";
    t.map = map_id{1};
    t.min_x = 0;
    t.min_y = 0;
    t.max_x = 100;
    t.max_y = 100;

    system_.register_territory(t);
    EXPECT_EQ(system_.territory_count(), 1);

    const auto* retrieved = system_.get_territory(territory_id{1});
    ASSERT_NE(retrieved, nullptr);
    EXPECT_EQ(retrieved->name, "Test Territory");
}

TEST_F(war_system_test, get_territory_at)
{
    territory t;
    t.id = territory_id{1};
    t.map = map_id{1};
    t.min_x = 100;
    t.min_y = 100;
    t.max_x = 200;
    t.max_y = 200;

    system_.register_territory(t);

    EXPECT_EQ(system_.get_territory_at(map_id{1}, 150, 150), territory_id{1});
    EXPECT_FALSE(system_.get_territory_at(map_id{1}, 50, 50).is_valid());
    EXPECT_FALSE(system_.get_territory_at(map_id{2}, 150, 150).is_valid());
}

TEST_F(war_system_test, capture_territory)
{
    territory t;
    t.id = territory_id{1};
    t.controlling_faction = war_faction::neutral;

    system_.register_territory(t);

    bool callback_fired = false;
    system_.on_territory_captured(
        [&](const territory_captured_event& event)
        {
            callback_fired = true;
            EXPECT_EQ(event.territory, territory_id{1});
            EXPECT_EQ(event.old_faction, war_faction::neutral);
            EXPECT_EQ(event.new_faction, war_faction::aresden);
        });

    EXPECT_TRUE(system_.capture_territory(territory_id{1}, war_faction::aresden));
    EXPECT_TRUE(callback_fired);

    const auto* captured = system_.get_territory(territory_id{1});
    EXPECT_EQ(captured->controlling_faction, war_faction::aresden);
}

TEST_F(war_system_test, get_faction_territories)
{
    territory t1;
    t1.id = territory_id{1};
    t1.controlling_faction = war_faction::aresden;

    territory t2;
    t2.id = territory_id{2};
    t2.controlling_faction = war_faction::elvine;

    territory t3;
    t3.id = territory_id{3};
    t3.controlling_faction = war_faction::aresden;

    system_.register_territory(t1);
    system_.register_territory(t2);
    system_.register_territory(t3);

    auto aresden_territories = system_.get_faction_territories(war_faction::aresden);
    EXPECT_EQ(aresden_territories.size(), 2);

    auto elvine_territories = system_.get_faction_territories(war_faction::elvine);
    EXPECT_EQ(elvine_territories.size(), 1);
}

TEST_F(war_system_test, calculate_rewards)
{
    auto result = system_.start_war(war_type::crusade);
    war_id wid = result.value();

    auto* war = system_.get_war(wid);
    war->state = war_status::active;

    system_.join_war(wid, player_id{1}, "Player1", war_faction::aresden);

    // Record some contribution
    auto* participant = war->get_participant(player_id{1});
    participant->contribution_score = 100;

    auto rewards = system_.calculate_rewards(wid, player_id{1});
    EXPECT_EQ(rewards.contribution_points, 100);
    EXPECT_GT(rewards.experience, 0);
    EXPECT_GT(rewards.gold, 0);
}
