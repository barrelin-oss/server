// test_quest.cpp
// Unit tests for quest system

#include <gtest/gtest.h>
#include "core/types.h"
#include "quest/objective.h"
#include "quest/reward.h"
#include "quest/quest.h"
#include "quest/quest_system.h"

using hb::item_id;
using hb::map_id;
using hb::npc_id;
using hb::player_id;
using hb::quest_id;
using namespace hb::quest;

// Objective tests

TEST(objective_state_test, default_values)
{
    objective_state state;
    EXPECT_EQ(state.status, objective_status::incomplete);
    EXPECT_EQ(state.current_count, 0);
    EXPECT_EQ(state.required_count, 1);
    EXPECT_FALSE(state.is_complete());
}

TEST(objective_state_test, add_progress)
{
    objective_state state;
    state.required_count = 10;

    EXPECT_FALSE(state.add_progress(5)); // Not complete yet
    EXPECT_EQ(state.current_count, 5);
    EXPECT_FALSE(state.is_complete());

    EXPECT_TRUE(state.add_progress(5)); // Now complete
    EXPECT_EQ(state.current_count, 10);
    EXPECT_TRUE(state.is_complete());
}

TEST(objective_state_test, progress_percent)
{
    objective_state state;
    state.required_count = 100;

    state.current_count = 0;
    EXPECT_FLOAT_EQ(state.progress_percent(), 0.0f);

    state.current_count = 50;
    EXPECT_FLOAT_EQ(state.progress_percent(), 50.0f);

    state.current_count = 100;
    EXPECT_FLOAT_EQ(state.progress_percent(), 100.0f);
}

TEST(objective_state_test, fail)
{
    objective_state state;
    state.fail();

    EXPECT_TRUE(state.is_failed());
    EXPECT_FALSE(state.is_complete());
}

TEST(objective_state_test, reset)
{
    objective_state state;
    state.current_count = 50;
    state.status = objective_status::complete;

    state.reset();

    EXPECT_EQ(state.current_count, 0);
    EXPECT_EQ(state.status, objective_status::incomplete);
}

// Reward tests

TEST(quest_rewards_test, has_rewards)
{
    quest_rewards empty;
    EXPECT_FALSE(empty.has_rewards());

    quest_rewards with_exp;
    with_exp.experience = 100;
    EXPECT_TRUE(with_exp.has_rewards());

    quest_rewards with_gold;
    with_gold.gold = 50;
    EXPECT_TRUE(with_gold.has_rewards());

    quest_rewards with_items;
    with_items.items.push_back({item_id{1}, 1, 0});
    EXPECT_TRUE(with_items.has_rewards());
}

TEST(quest_rewards_test, apply_multiplier)
{
    quest_rewards base;
    base.experience = 100;
    base.gold = 50;
    base.items.push_back({item_id{1}, 10, 0});

    reward_multiplier mult;
    mult.experience_mult = 2.0f;
    mult.gold_mult = 1.5f;
    mult.item_quantity_mult = 3.0f;

    auto result = apply_multiplier(base, mult);

    EXPECT_EQ(result.experience, 200);
    EXPECT_EQ(result.gold, 75);
    EXPECT_EQ(result.items[0].count, 30);
}

// Quest state tests

TEST(quest_state_test, status_checks)
{
    quest_state state;

    state.status = quest_status::active;
    EXPECT_TRUE(state.is_active());
    EXPECT_FALSE(state.is_complete());

    state.status = quest_status::complete;
    EXPECT_FALSE(state.is_active());
    EXPECT_TRUE(state.is_complete());

    state.status = quest_status::failed;
    EXPECT_TRUE(state.is_failed());
}

TEST(quest_state_test, time_tracking)
{
    quest_state state;
    state.time_limit_seconds = 60;
    state.elapsed_seconds = 0;

    EXPECT_FALSE(state.has_timed_out());
    EXPECT_EQ(state.remaining_time(), 60);

    state.elapsed_seconds = 30;
    EXPECT_FALSE(state.has_timed_out());
    EXPECT_EQ(state.remaining_time(), 30);

    state.elapsed_seconds = 60;
    EXPECT_TRUE(state.has_timed_out());
    EXPECT_EQ(state.remaining_time(), 0);
}

TEST(quest_state_test, initialize_objectives)
{
    std::vector<objective_template> templates;

    objective_template kill_obj;
    kill_obj.id = 1;
    kill_obj.type = objective_type::kill_monster;
    kill_obj.data = kill_objective_data{npc_id{100}, 10, false};
    templates.push_back(kill_obj);

    objective_template collect_obj;
    collect_obj.id = 2;
    collect_obj.type = objective_type::collect_item;
    collect_obj.data = collect_objective_data{item_id{50}, 5, npc_id{}};
    templates.push_back(collect_obj);

    quest_state state;
    state.initialize_objectives(templates);

    EXPECT_EQ(state.objectives.size(), 2);
    EXPECT_EQ(state.objectives[0].required_count, 10);
    EXPECT_EQ(state.objectives[1].required_count, 5);
}

// Quest journal tests

TEST(quest_journal_test, can_accept_quest)
{
    quest_journal journal;
    EXPECT_TRUE(journal.can_accept_quest());
    EXPECT_EQ(journal.active_count(), 0);
}

TEST(quest_journal_test, has_active_quest)
{
    quest_journal journal;

    quest_state state;
    state.template_id = quest_id{1};
    state.status = quest_status::active;
    journal.active_quests.push_back(state);

    EXPECT_TRUE(journal.has_active_quest(quest_id{1}));
    EXPECT_FALSE(journal.has_active_quest(quest_id{2}));
}

TEST(quest_journal_test, has_completed_quest)
{
    quest_journal journal;
    journal.completed_quests.push_back(quest_id{1});

    EXPECT_TRUE(journal.has_completed_quest(quest_id{1}));
    EXPECT_FALSE(journal.has_completed_quest(quest_id{2}));
}

// Quest system tests

class quest_system_test : public ::testing::Test
{
protected:
    void SetUp() override
    {
        system_.initialize();

        // Create a test quest template
        quest_template tmpl;
        tmpl.id = quest_id{1};
        tmpl.name = "Test Quest";
        tmpl.description = "A test quest";
        tmpl.min_level = 1;
        tmpl.max_level = 200;

        objective_template obj;
        obj.id = 1;
        obj.type = objective_type::kill_monster;
        obj.data = kill_objective_data{npc_id{100}, 5, false};
        tmpl.objectives.push_back(obj);

        tmpl.rewards.experience = 100;
        tmpl.rewards.gold = 50;

        system_.register_quest(std::move(tmpl));
    }

    void TearDown() override { system_.shutdown(); }

    quest_system system_;
};

TEST_F(quest_system_test, lifecycle)
{
    EXPECT_TRUE(system_.is_initialized());
    EXPECT_EQ(system_.name(), "quest_system");
}

TEST_F(quest_system_test, register_quest)
{
    EXPECT_EQ(system_.quest_count(), 1);

    const auto* tmpl = system_.get_quest_template(quest_id{1});
    ASSERT_NE(tmpl, nullptr);
    EXPECT_EQ(tmpl->name, "Test Quest");
}

TEST_F(quest_system_test, register_unregister_player)
{
    player_id player{1};

    system_.register_player(player);
    EXPECT_NE(system_.get_journal(player), nullptr);

    system_.unregister_player(player);
    EXPECT_EQ(system_.get_journal(player), nullptr);
}

TEST_F(quest_system_test, accept_quest)
{
    player_id player{1};
    system_.register_player(player);

    auto result = system_.accept_quest(player, quest_id{1});
    EXPECT_EQ(result, accept_result::success);
    EXPECT_TRUE(system_.has_quest(player, quest_id{1}));
}

TEST_F(quest_system_test, accept_nonexistent_quest)
{
    player_id player{1};
    system_.register_player(player);

    auto result = system_.accept_quest(player, quest_id{999});
    EXPECT_EQ(result, accept_result::quest_not_found);
}

TEST_F(quest_system_test, accept_duplicate_quest)
{
    player_id player{1};
    system_.register_player(player);

    system_.accept_quest(player, quest_id{1});
    auto result = system_.accept_quest(player, quest_id{1});
    EXPECT_EQ(result, accept_result::already_active);
}

TEST_F(quest_system_test, abandon_quest)
{
    player_id player{1};
    system_.register_player(player);
    system_.accept_quest(player, quest_id{1});

    EXPECT_TRUE(system_.abandon_quest(player, quest_id{1}));
    EXPECT_FALSE(system_.has_quest(player, quest_id{1}));
}

TEST_F(quest_system_test, get_active_quests)
{
    player_id player{1};
    system_.register_player(player);
    system_.accept_quest(player, quest_id{1});

    auto active = system_.get_active_quests(player);
    EXPECT_EQ(active.size(), 1);
    EXPECT_EQ(active[0], quest_id{1});
}

TEST_F(quest_system_test, kill_progress)
{
    player_id player{1};
    system_.register_player(player);
    system_.accept_quest(player, quest_id{1});

    // Kill 3 of the required NPCs
    system_.on_kill({player, npc_id{100}, false});
    system_.on_kill({player, npc_id{100}, false});
    system_.on_kill({player, npc_id{100}, false});

    const auto* state = system_.get_quest_state(player, quest_id{1});
    ASSERT_NE(state, nullptr);
    EXPECT_EQ(state->objectives[0].current_count, 3);
    EXPECT_FALSE(state->objectives[0].is_complete());
}

TEST_F(quest_system_test, quest_completion)
{
    player_id player{1};
    system_.register_player(player);
    system_.accept_quest(player, quest_id{1});

    // Kill all 5 required NPCs
    for (int i = 0; i < 5; ++i)
    {
        system_.on_kill({player, npc_id{100}, false});
    }

    // Quest should be marked complete (ready to turn in)
    const auto* state = system_.get_quest_state(player, quest_id{1});
    ASSERT_NE(state, nullptr);
    EXPECT_TRUE(state->objectives[0].is_complete());
}

TEST_F(quest_system_test, turn_in_quest)
{
    player_id player{1};
    system_.register_player(player);
    system_.accept_quest(player, quest_id{1});

    // Complete the objective
    for (int i = 0; i < 5; ++i)
    {
        system_.on_kill({player, npc_id{100}, false});
    }

    // Turn in the quest
    auto result = system_.complete_quest(player, quest_id{1});
    EXPECT_EQ(result, complete_result::success);
    EXPECT_TRUE(system_.has_completed_quest(player, quest_id{1}));
}

TEST_F(quest_system_test, incomplete_turn_in)
{
    player_id player{1};
    system_.register_player(player);
    system_.accept_quest(player, quest_id{1});

    // Try to turn in without completing objectives
    auto result = system_.complete_quest(player, quest_id{1});
    EXPECT_EQ(result, complete_result::objectives_incomplete);
}

TEST_F(quest_system_test, completion_callback)
{
    player_id player{1};
    system_.register_player(player);

    bool callback_fired = false;
    system_.on_quest_completed(
        [&](const quest_completed_event& event)
        {
            callback_fired = true;
            EXPECT_EQ(event.player, player);
            EXPECT_EQ(event.quest, quest_id{1});
            EXPECT_EQ(event.rewards.experience, 100);
            EXPECT_EQ(event.rewards.gold, 50);
        });

    system_.accept_quest(player, quest_id{1});

    // Complete and turn in
    for (int i = 0; i < 5; ++i)
    {
        system_.on_kill({player, npc_id{100}, false});
    }
    system_.complete_quest(player, quest_id{1});

    EXPECT_TRUE(callback_fired);
}

TEST_F(quest_system_test, get_available_quests)
{
    player_id player{1};
    system_.register_player(player);

    auto available = system_.get_available_quests(player, 50, 0);
    EXPECT_EQ(available.size(), 1);
}

TEST_F(quest_system_test, get_quests_from_npc)
{
    // Register a quest with a specific quest giver
    quest_template tmpl;
    tmpl.id = quest_id{2};
    tmpl.name = "NPC Quest";
    tmpl.quest_giver = npc_id{500};
    system_.register_quest(std::move(tmpl));

    auto quests = system_.get_quests_from_npc(npc_id{500});
    EXPECT_EQ(quests.size(), 1);
    EXPECT_EQ(quests[0], quest_id{2});
}

TEST_F(quest_system_test, prerequisite_check)
{
    // Register a quest with prerequisites
    quest_template tmpl;
    tmpl.id = quest_id{3};
    tmpl.name = "Sequel Quest";
    tmpl.prerequisite_quests.push_back(quest_id{1});
    system_.register_quest(std::move(tmpl));

    player_id player{1};
    system_.register_player(player);

    // Can't accept quest 3 without completing quest 1
    auto result = system_.can_accept_quest(player, quest_id{3});
    EXPECT_EQ(result, accept_result::missing_prerequisite);
}

// Collection objective test
TEST_F(quest_system_test, collect_item_progress)
{
    // Add a collection quest
    quest_template tmpl;
    tmpl.id = quest_id{10};
    tmpl.name = "Collect Items";

    objective_template obj;
    obj.id = 1;
    obj.type = objective_type::collect_item;
    obj.data = collect_objective_data{item_id{50}, 10, npc_id{}};
    tmpl.objectives.push_back(obj);

    system_.register_quest(std::move(tmpl));

    player_id player{1};
    system_.register_player(player);
    system_.accept_quest(player, quest_id{10});

    // Collect some items
    system_.on_item_collected({player, item_id{50}, 5});

    const auto* state = system_.get_quest_state(player, quest_id{10});
    ASSERT_NE(state, nullptr);
    EXPECT_EQ(state->objectives[0].current_count, 5);
}

// Location objective test
TEST_F(quest_system_test, visit_location_progress)
{
    // Add a location quest
    quest_template tmpl;
    tmpl.id = quest_id{11};
    tmpl.name = "Visit Location";

    objective_template obj;
    obj.id = 1;
    obj.type = objective_type::visit_location;
    obj.data = location_objective_data{map_id{1}, 100, 100, 10};
    tmpl.objectives.push_back(obj);

    system_.register_quest(std::move(tmpl));

    player_id player{1};
    system_.register_player(player);
    system_.accept_quest(player, quest_id{11});

    // Visit the location
    system_.on_location_reached({player, map_id{1}, 105, 95});

    const auto* state = system_.get_quest_state(player, quest_id{11});
    ASSERT_NE(state, nullptr);
    EXPECT_TRUE(state->objectives[0].is_complete());
}
