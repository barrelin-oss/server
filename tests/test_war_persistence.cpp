// test_war_persistence.cpp
// Unit tests for Phase 6: war rewards, persistence types, admin protocol

#include <gtest/gtest.h>
#include "war/war_types.h"
#include "war/war_system.h"
#include "war/war_persistence.h"
#include "war/crusade/crusade_system.h"
#include "network/json_protocol.h"

#include <nlohmann/json.hpp>

using namespace hb::war;
using hb::player_id;

// ========== War Reward Calculation Tests ==========

class war_reward_test : public ::testing::Test
{
protected:
    void SetUp() override
    {
        sys_.initialize();

        war_system_config cfg;
        cfg.default_war_config.winner_reward_mult = 1.5f;
        cfg.default_war_config.loser_reward_mult = 0.5f;
        cfg.default_war_config.participation_reward_mult = 1.0f;
        sys_.set_config(cfg);
    }

    void TearDown() override { sys_.shutdown(); }

    war_system sys_;
};

TEST_F(war_reward_test, no_war_returns_empty)
{
    auto rewards = sys_.calculate_rewards(war_id(999), player_id(1));
    EXPECT_EQ(rewards.experience, 0);
    EXPECT_EQ(rewards.gold, 0);
    EXPECT_EQ(rewards.contribution_points, 0);
    EXPECT_FALSE(rewards.has_rewards());
}

TEST_F(war_reward_test, no_participant_returns_empty)
{
    auto result = sys_.start_war(war_type::crusade);
    ASSERT_TRUE(result.is_ok());
    auto wid = result.value();

    auto rewards = sys_.calculate_rewards(wid, player_id(999));
    EXPECT_FALSE(rewards.has_rewards());
}

TEST_F(war_reward_test, participant_gets_contribution_based_rewards)
{
    auto result = sys_.start_war(war_type::crusade);
    ASSERT_TRUE(result.is_ok());
    auto wid = result.value();

    // Transition to active (start_war puts it in preparing)
    auto* war = sys_.get_war(wid);
    war->state = war_status::active;

    sys_.join_war(wid, player_id(1), "TestPlayer", war_faction::aresden);

    // Record some kills to earn contribution
    sys_.record_kill(wid, player_id(1), player_id(99));
    sys_.record_kill(wid, player_id(1), player_id(99));
    sys_.record_damage(wid, player_id(1), 1000);

    auto* participant = war->get_participant(player_id(1));
    ASSERT_NE(participant, nullptr);
    EXPECT_GT(participant->contribution_score, 0);

    auto rewards = sys_.calculate_rewards(wid, player_id(1));
    EXPECT_TRUE(rewards.has_rewards());
    EXPECT_GT(rewards.experience, 0);
    EXPECT_GT(rewards.gold, 0);
    EXPECT_EQ(rewards.contribution_points, participant->contribution_score);
}

TEST_F(war_reward_test, winner_gets_bonus_multiplier)
{
    auto result = sys_.start_war(war_type::crusade);
    ASSERT_TRUE(result.is_ok());
    auto wid = result.value();

    // Transition to active
    auto* war = sys_.get_war(wid);
    war->state = war_status::active;

    // Add aresden player with contribution
    sys_.join_war(wid, player_id(1), "AresdenPlayer", war_faction::aresden);
    sys_.record_damage(wid, player_id(1), 5000);

    // Add elvine player with same contribution
    sys_.join_war(wid, player_id(2), "ElvinePlayer", war_faction::elvine);
    sys_.record_damage(wid, player_id(2), 5000);

    // Make aresden win by adding kill score
    war->aresden_score.total_score = 100;
    war->elvine_score.total_score = 50;

    auto winner_rewards = sys_.calculate_rewards(wid, player_id(1));
    auto loser_rewards = sys_.calculate_rewards(wid, player_id(2));

    // Winner gets 1.5x, loser gets 0.5x
    EXPECT_GT(winner_rewards.experience, loser_rewards.experience);
    // With same base contribution, winner_exp / loser_exp should be ~3x (1.5 / 0.5)
    if (loser_rewards.experience > 0)
    {
        float ratio = static_cast<float>(winner_rewards.experience) / static_cast<float>(loser_rewards.experience);
        EXPECT_NEAR(ratio, 3.0f, 0.5f);
    }
}

TEST_F(war_reward_test, draw_uses_participation_mult)
{
    auto result = sys_.start_war(war_type::crusade);
    ASSERT_TRUE(result.is_ok());
    auto wid = result.value();

    auto* war = sys_.get_war(wid);
    war->state = war_status::active;

    sys_.join_war(wid, player_id(1), "Player", war_faction::aresden);
    sys_.record_damage(wid, player_id(1), 1000);

    // Tied score → neutral winner → participation multiplier (1.0x)
    war->aresden_score.total_score = 50;
    war->elvine_score.total_score = 50;

    auto rewards = sys_.calculate_rewards(wid, player_id(1));
    auto* participant = war->get_participant(player_id(1));

    // participation_reward_mult = 1.0, so exp = contribution * 10
    EXPECT_EQ(rewards.experience, participant->contribution_score * 10);
}

// ========== War History Row Tests ==========

TEST(war_history_row_test, default_values)
{
    war_history_row row;
    EXPECT_EQ(row.id, 0);
    EXPECT_EQ(row.type, war_type::crusade);
    EXPECT_EQ(row.winner, war_faction::neutral);
    EXPECT_EQ(row.duration_seconds, 0);
    EXPECT_EQ(row.aresden_score, 0);
    EXPECT_EQ(row.elvine_score, 0);
}

TEST(war_participant_row_test, default_values)
{
    war_participant_row row;
    EXPECT_EQ(row.war_id, 0);
    EXPECT_EQ(row.character_id, 0);
    EXPECT_EQ(row.faction, war_faction::neutral);
    EXPECT_EQ(row.duty, 0);
    EXPECT_EQ(row.kills, 0);
    EXPECT_EQ(row.reward_exp, 0);
}

// ========== War Persistence Without DB ==========

TEST(war_persistence_test, null_db_returns_error)
{
    war_persistence persistence(nullptr);

    war_result wr;
    wr.type = war_type::crusade;
    wr.started_at = std::chrono::system_clock::now();
    wr.ended_at = std::chrono::system_clock::now();

    auto result = persistence.save_war_result(wr);
    EXPECT_TRUE(result.is_err());

    auto history = persistence.load_war_history();
    EXPECT_TRUE(history.is_err());

    auto participants = persistence.load_war_participants(1);
    EXPECT_TRUE(participants.is_err());

    auto count = persistence.count_wars();
    EXPECT_TRUE(count.is_err());

    auto count_type = persistence.count_wars_by_type(war_type::crusade);
    EXPECT_TRUE(count_type.is_err());
}

// ========== War Result Type Tests ==========

TEST(war_result_test, winning_score)
{
    war_result wr;
    wr.winner = war_faction::aresden;
    wr.aresden_score = faction_score{war_faction::aresden};
    wr.aresden_score.total_score = 100;
    wr.elvine_score = faction_score{war_faction::elvine};
    wr.elvine_score.total_score = 50;

    EXPECT_EQ(wr.winning_score().total_score, 100);
    EXPECT_EQ(wr.losing_score().total_score, 50);
}

TEST(war_result_test, draw_result)
{
    war_result wr;
    wr.winner = war_faction::neutral;
    wr.draw = true;
    EXPECT_TRUE(wr.draw);
}

// ========== Crusade Reward Summary Builder ==========

TEST(crusade_reward_summary_test, builds_correct_message)
{
    auto msg = hb::network::make_crusade_reward_summary(42, 1, 500, 10000, 5000, 200);

    EXPECT_EQ(msg.type, hb::network::json_message_type::crusade_reward_summary);
    EXPECT_EQ(msg.seq, 42u);
    EXPECT_EQ(msg.data["winner_faction"], 1);
    EXPECT_EQ(msg.data["contribution"], 500);
    EXPECT_EQ(msg.data["reward_exp"], 10000);
    EXPECT_EQ(msg.data["reward_gold"], 5000);
    EXPECT_EQ(msg.data["reward_contribution"], 200);
}

// ========== Phase 6 Protocol Message Tests ==========

TEST(phase6_protocol_test, reward_message_type)
{
    EXPECT_EQ(hb::network::to_string(hb::network::json_message_type::crusade_reward_summary), "crusade_reward_summary");
}

TEST(phase6_protocol_test, admin_war_message_types)
{
    EXPECT_EQ(hb::network::to_string(hb::network::json_message_type::admin_start_war_request),
              "admin_start_war_request");
    EXPECT_EQ(hb::network::to_string(hb::network::json_message_type::admin_start_war_response),
              "admin_start_war_response");
    EXPECT_EQ(hb::network::to_string(hb::network::json_message_type::admin_end_war_request), "admin_end_war_request");
    EXPECT_EQ(hb::network::to_string(hb::network::json_message_type::admin_end_war_response), "admin_end_war_response");
    EXPECT_EQ(hb::network::to_string(hb::network::json_message_type::admin_war_history_request),
              "admin_war_history_request");
    EXPECT_EQ(hb::network::to_string(hb::network::json_message_type::admin_war_history_response),
              "admin_war_history_response");
    EXPECT_EQ(hb::network::to_string(hb::network::json_message_type::admin_war_participants_request),
              "admin_war_participants_request");
    EXPECT_EQ(hb::network::to_string(hb::network::json_message_type::admin_war_participants_response),
              "admin_war_participants_response");
}

TEST(phase6_protocol_test, message_type_roundtrip)
{
    auto parsed = hb::network::parse_message_type("crusade_reward_summary");
    EXPECT_EQ(parsed, hb::network::json_message_type::crusade_reward_summary);

    parsed = hb::network::parse_message_type("admin_start_war_request");
    EXPECT_EQ(parsed, hb::network::json_message_type::admin_start_war_request);

    parsed = hb::network::parse_message_type("admin_war_history_request");
    EXPECT_EQ(parsed, hb::network::json_message_type::admin_war_history_request);

    parsed = hb::network::parse_message_type("admin_war_participants_request");
    EXPECT_EQ(parsed, hb::network::json_message_type::admin_war_participants_request);
}

// ========== Crusade System Persistence Integration ==========

TEST(crusade_persistence_integration, persistence_setter)
{
    // Verify the setter compiles and can be called with nullptr
    crusade_system sys;
    sys.initialize();
    sys.set_persistence(nullptr);
    sys.shutdown();
}

// ========== War Rewards Struct Tests ==========

TEST(war_rewards_test, has_rewards_checks_all_fields)
{
    war_rewards r;
    EXPECT_FALSE(r.has_rewards());

    r.experience = 1;
    EXPECT_TRUE(r.has_rewards());

    r = war_rewards{};
    r.gold = 1;
    EXPECT_TRUE(r.has_rewards());

    r = war_rewards{};
    r.contribution_points = 1;
    EXPECT_TRUE(r.has_rewards());

    r = war_rewards{};
    r.honor_points = 1;
    EXPECT_TRUE(r.has_rewards());
}

// ========== War Participant Tests ==========

TEST(war_participant_test, record_operations)
{
    war_participant p;
    p.record_kill();
    EXPECT_EQ(p.kills, 1);
    EXPECT_EQ(p.contribution_score, 10);

    p.record_assist();
    EXPECT_EQ(p.assists, 1);
    EXPECT_EQ(p.contribution_score, 15);

    p.record_death();
    EXPECT_EQ(p.deaths, 1);

    EXPECT_FLOAT_EQ(p.kd_ratio(), 1.0f);
}

TEST(war_participant_test, kd_ratio_no_deaths)
{
    war_participant p;
    p.record_kill();
    p.record_kill();
    EXPECT_FLOAT_EQ(p.kd_ratio(), 2.0f);
}
