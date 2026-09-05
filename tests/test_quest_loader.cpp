// Tests for quest/quest_loader.h - legacy Quest.cfg rows into quest_templates.
#include "quest/quest_loader.h"
#include "quest/quest_system.h"
#include "registry/npc_registry.h"

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>

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
                               "  - {name: William, sprite_id: 34, hit_dice: 100, defense_ratio: 100, hit_ratio: 100, exp: 0}\n");
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
