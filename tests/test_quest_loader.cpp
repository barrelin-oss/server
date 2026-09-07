// Tests for quest/quest_loader.h - legacy Quest.cfg rows into quest_templates.
#include "quest/quest_loader.h"
#include "quest/quest_system.h"
#include "registry/npc_registry.h"

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <chrono>

using namespace hb;
using namespace hb::quest;

namespace
{

class quest_loader_test : public ::testing::Test
{
protected:
    void SetUp() override
    {
        dir_ = std::filesystem::temp_directory_path() / "hgserver_quest_loader_test";
        std::filesystem::create_directories(dir_);
        auto npcs_yaml = write("npcs.yaml",
                               "npcs:\n"
                               "  - {name: Giant-Ant, sprite_id: 16, hit_dice: 3, defense_ratio: 30, hit_ratio: 40, exp: 74}\n"
                               "  - {name: Kennedy, sprite_id: 34, hit_dice: 100, defense_ratio: 100, hit_ratio: 100, exp: 0}\n"
                               "  - {name: William, sprite_id: 34, hit_dice: 100, defense_ratio: 100, hit_ratio: 100, exp: 0}\n"
                               "  - {name: Enzu, sprite_id: 106, hit_dice: 10, defense_ratio: 10, hit_ratio: 20, exp: 1, side: 0, action_limit: 2}\n");
        npcs_.initialize();
        ASSERT_TRUE(npcs_.load_from_file(npcs_yaml).is_ok());
        quests_.initialize();
    }
    void TearDown() override { std::filesystem::remove_all(dir_); }

    auto write(const std::string& name, const std::string& content) -> std::filesystem::path
    {
        auto path = dir_ / name;
        std::ofstream f(path);
        f << content;
        return path;
    }

    static auto maps(std::string_view name) -> map_id
    {
        if (name == "aresden")
            return map_id{1};
        if (name == "elvine")
            return map_id{2};
        return map_id{0};
    }

    std::filesystem::path dir_;
    npc_registry npcs_;
    quest_system quests_;
};

constexpr const char* hunt_row =
    "  - {id: 1, side: 1, type: 1, target_type: 16, max_count: 22, from_id: 4, min_level: 11, max_level: 20, "
    "req_skill: -1, req_skill_pct: -1, time_limit: -1, assign_type: -1, reward_type1: -1, reward_amount1: 100, "
    "reward_type2: 90, reward_amount2: 150, reward_type3: 90, reward_amount3: 100, contribution: 1, "
    "contribution_limit: 10, resp_mode: 1, map: aresden, x: 0, y: 0, range: 0, quest_id: 417, req_contribution: 0}\n";

constexpr const char* goplace_row =
    "  - {id: 29, side: 2, type: 7, target_type: 0, max_count: 0, from_id: 4, min_level: 50, max_level: 300, "
    "reward_type1: -2, reward_amount1: 1, reward_type2: -2, reward_amount2: 1, reward_type3: -2, reward_amount3: 1, "
    "contribution: 1, contribution_limit: 500, resp_mode: 1, map: elvine, x: 218, y: 90, range: 3, quest_id: 208}\n";

} // namespace

TEST_F(quest_loader_test, hunt_row_becomes_kill_quest_from_city_hall)
{
    auto path = write("quests.yaml", std::string("quests:\n") + hunt_row);
    auto loaded = load_legacy_quests(quests_, path, npcs_, &quest_loader_test::maps);
    ASSERT_TRUE(loaded.is_ok()) << loaded.error();
    EXPECT_EQ(loaded.value(), 1u);

    const auto* t = quests_.get_quest_template(quest_id{1});
    ASSERT_NE(t, nullptr);
    EXPECT_EQ(t->name, "Hunt Giant-Ant x22");
    EXPECT_EQ(t->min_level, 11);
    EXPECT_EQ(t->max_level, 20);
    EXPECT_EQ(t->required_faction, 1);
    EXPECT_TRUE(t->repeatable);
    EXPECT_EQ(t->quest_giver, npcs_.find_by_name("Kennedy")->id);
    EXPECT_EQ(t->quest_giver_map, map_id{1});

    ASSERT_EQ(t->objectives.size(), 1u);
    EXPECT_EQ(t->objectives[0].type, objective_type::kill_monster);
    const auto* kill = std::get_if<kill_objective_data>(&t->objectives[0].data);
    ASSERT_NE(kill, nullptr);
    EXPECT_EQ(kill->target_type, npcs_.find_by_name("Giant-Ant")->id);
    EXPECT_EQ(kill->required_count, 22);

    EXPECT_EQ(t->rewards.experience, 100);
    EXPECT_EQ(t->rewards.gold, 250); // two Gold (item 90) rewards: 150 + 100
    EXPECT_TRUE(t->rewards.items.empty());
    ASSERT_EQ(t->rewards.reputation.size(), 1u);
    EXPECT_EQ(t->rewards.reputation[0].faction_id, 1);
}

TEST_F(quest_loader_test, goplace_row_becomes_visit_location_for_elvine)
{
    auto path = write("quests.yaml", std::string("quests:\n") + goplace_row);
    auto loaded = load_legacy_quests(quests_, path, npcs_, &quest_loader_test::maps);
    ASSERT_TRUE(loaded.is_ok()) << loaded.error();

    const auto* t = quests_.get_quest_template(quest_id{29});
    ASSERT_NE(t, nullptr);
    EXPECT_EQ(t->quest_giver, npcs_.find_by_name("William")->id);
    EXPECT_EQ(t->required_faction, 2);
    ASSERT_EQ(t->objectives.size(), 1u);
    EXPECT_EQ(t->objectives[0].type, objective_type::visit_location);
    const auto* loc = std::get_if<location_objective_data>(&t->objectives[0].data);
    ASSERT_NE(loc, nullptr);
    EXPECT_EQ(loc->target_map, map_id{2});
    EXPECT_EQ(loc->target_x, 218);
    EXPECT_EQ(loc->target_y, 90);
    EXPECT_EQ(loc->radius, 3);
    // scaled exp (-2) three times with amount 1, scaled by min_level 50
    EXPECT_EQ(t->rewards.experience, 150);
}

TEST_F(quest_loader_test, named_giver_texts_second_target_and_gather)
{
    auto path = write("quests.yaml",
                      std::string("quests:\n") +
                          "  - {id: 200, side: 0, type: 1, giver: Enzu, giver_map: elvine, name: \"Humble Beginning\", "
                          "description: \"Slay a few Slimes south of here.\", target_type: 16, max_count: 50, "
                          "target_type2: 16, max_count2: 5, gather_item: 190, gather_count: 3, gather_item_2: 189, "
                          "min_level: 1, max_level: 20, reward_type1: -1, reward_amount1: 400, reward_type2: 90, "
                          "reward_amount2: 100, contribution: 10}\n"
                          "  - {id: 201, side: 0, type: 2, giver: Enzu, name: Epidemy, gather_item: 191, gather_count: 3, "
                          "min_level: 10, max_level: 300, reward_type1: -1, reward_amount1: 100}\n");
    auto loaded = load_legacy_quests(quests_, path, npcs_, &quest_loader_test::maps);
    ASSERT_TRUE(loaded.is_ok()) << loaded.error();
    EXPECT_EQ(loaded.value(), 2u);

    const auto* q = quests_.get_quest_template(quest_id{200});
    ASSERT_NE(q, nullptr);
    EXPECT_EQ(q->name, "Humble Beginning");
    EXPECT_EQ(q->description, "Slay a few Slimes south of here.");
    EXPECT_EQ(q->required_faction, 0);
    EXPECT_EQ(q->quest_giver, npcs_.find_by_name("Enzu")->id);
    EXPECT_EQ(q->quest_giver_map, map_id{2});
    ASSERT_EQ(q->objectives.size(), 4u);
    EXPECT_EQ(q->objectives[0].type, objective_type::kill_monster);
    EXPECT_EQ(q->objectives[1].type, objective_type::kill_monster);
    EXPECT_EQ(std::get<kill_objective_data>(q->objectives[1].data).required_count, 5);
    EXPECT_EQ(q->objectives[2].type, objective_type::collect_item);
    EXPECT_EQ(std::get<collect_objective_data>(q->objectives[2].data).item_type, item_id{190});
    EXPECT_EQ(std::get<collect_objective_data>(q->objectives[3].data).required_count, 1); // default count
    EXPECT_EQ(q->rewards.experience, 400);
    EXPECT_EQ(q->rewards.gold, 100);

    const auto* g = quests_.get_quest_template(quest_id{201});
    ASSERT_NE(g, nullptr);
    EXPECT_EQ(g->name, "Epidemy");
    ASSERT_EQ(g->objectives.size(), 1u);
    EXPECT_EQ(g->objectives[0].type, objective_type::collect_item);
}

TEST_F(quest_loader_test, elite_flags_make_elite_only_kill_objectives)
{
    auto path = write("quests.yaml",
                      std::string("quests:\n") +
                          "  - {id: 300, side: 0, type: 1, giver: Enzu, target_type: 16, max_count: 10, elite: true, "
                          "target_type2: 16, max_count2: 2, elite2: true, min_level: 1, max_level: 300}\n");
    auto loaded = load_legacy_quests(quests_, path, npcs_, &quest_loader_test::maps);
    ASSERT_TRUE(loaded.is_ok()) << loaded.error();
    const auto* q = quests_.get_quest_template(quest_id{300});
    ASSERT_NE(q, nullptr);
    ASSERT_EQ(q->objectives.size(), 2u);
    EXPECT_TRUE(std::get<kill_objective_data>(q->objectives[0].data).elite_only);
    EXPECT_EQ(q->objectives[0].description, "Kill 10 Elite Giant-Ant");
    EXPECT_TRUE(std::get<kill_objective_data>(q->objectives[1].data).elite_only);
}

TEST_F(quest_loader_test, period_hours_makes_a_daily_with_a_cooldown)
{
    auto path = write("quests.yaml",
                      std::string("quests:\n") +
                          "  - {id: 301, side: 0, type: 1, giver: Enzu, period_hours: 20, target_type: 16, max_count: 10, "
                          "min_level: 1, max_level: 300}\n");
    ASSERT_TRUE(load_legacy_quests(quests_, path, npcs_, &quest_loader_test::maps).is_ok());
    const auto* q = quests_.get_quest_template(quest_id{301});
    ASSERT_NE(q, nullptr);
    EXPECT_EQ(q->repeat_after_seconds, 20 * 3600);
    EXPECT_EQ(q->type, hb::quest::quest_type::daily);
    EXPECT_TRUE(q->repeatable);

    const player_id me{9};
    quests_.register_player(me);
    EXPECT_EQ(quests_.cooldown_remaining(me, quest_id{301}), 0);
    auto* journal = quests_.get_journal(me);
    ASSERT_NE(journal, nullptr);
    journal->last_completed[301] =
        std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count();
    EXPECT_GT(quests_.cooldown_remaining(me, quest_id{301}), 19 * 3600);
    EXPECT_EQ(quests_.accept_quest(me, quest_id{301}), accept_result::on_cooldown);
    journal->last_completed[301] -= 21 * 3600;
    EXPECT_EQ(quests_.cooldown_remaining(me, quest_id{301}), 0);
}

TEST_F(quest_loader_test, unknown_target_is_skipped_not_fatal)
{
    auto path = write("quests.yaml",
                      std::string("quests:\n") + hunt_row +
                          "  - {id: 2, side: 1, type: 1, target_type: 99, max_count: 5, min_level: 1, max_level: 10}\n");
    auto loaded = load_legacy_quests(quests_, path, npcs_, &quest_loader_test::maps);
    ASSERT_TRUE(loaded.is_ok());
    EXPECT_EQ(loaded.value(), 1u);
    EXPECT_EQ(quests_.get_quest_template(quest_id{2}), nullptr);
}

TEST_F(quest_loader_test, missing_file_is_an_error)
{
    auto loaded = load_legacy_quests(quests_, dir_ / "nope.yaml", npcs_, &quest_loader_test::maps);
    EXPECT_TRUE(loaded.is_err());
}
