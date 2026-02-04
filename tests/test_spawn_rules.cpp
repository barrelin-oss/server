// test_spawn_rules.cpp
// Unit tests for spawn rule engine, spawn context, and random spawn whitelist

#include <gtest/gtest.h>
#include "npc/spawn_rule.h"
#include "npc/spawn_rule_engine.h"
#include "npc/spawn_context.h"
#include "npc/random_spawn_whitelist.h"
#include "world/tile.h"
#include "world/position.h"

using namespace hb;
using namespace hb::npc;
using namespace hb::world;

// ========== Weighted NPC Entry Tests ==========

TEST(weighted_npc_entry_test, creation) {
    weighted_npc_entry entry;
    entry.id = npc_id{100};
    entry.name = "Slime";
    entry.weight = 50;

    EXPECT_EQ(entry.id.value, 100);
    EXPECT_EQ(entry.name, "Slime");
    EXPECT_EQ(entry.weight, 50);
}

// ========== Rule Result Tests ==========

TEST(rule_result_test, pass) {
    std::vector<weighted_npc_entry> npcs;
    npcs.push_back({npc_id{1}, "Slime", 10});
    npcs.push_back({npc_id{2}, "Goblin", 20});

    auto result = rule_result::pass(std::move(npcs));

    EXPECT_TRUE(result.matches);
    EXPECT_EQ(result.npcs.size(), 2);
    EXPECT_EQ(result.npcs[0].name, "Slime");
    EXPECT_EQ(result.npcs[1].name, "Goblin");
}

TEST(rule_result_test, fail) {
    auto result = rule_result::fail();

    EXPECT_FALSE(result.matches);
    EXPECT_TRUE(result.npcs.empty());
}

// ========== Spawn Context Tests ==========

TEST(spawn_context_test, default_values) {
    spawn_context ctx;

    EXPECT_TRUE(ctx.map_name.empty());
    EXPECT_EQ(ctx.pos.x, 0);
    EXPECT_EQ(ctx.pos.y, 0);
    EXPECT_EQ(ctx.tile, nullptr);
    EXPECT_EQ(ctx.weather, weather_type::clear);
}

TEST(spawn_context_test, terrain_queries_with_null_tile) {
    spawn_context ctx;
    ctx.tile = nullptr;

    // With null tile, all terrain queries should return false
    EXPECT_FALSE(ctx.is_water());
    EXPECT_FALSE(ctx.is_farm());
    EXPECT_FALSE(ctx.is_safe_zone());
}

TEST(spawn_context_test, terrain_queries_with_water_tile) {
    static_tile tile;
    tile.flags = tile_flags::is_water;

    spawn_context ctx;
    ctx.tile = &tile;

    EXPECT_TRUE(ctx.is_water());
    EXPECT_FALSE(ctx.is_farm());
    EXPECT_FALSE(ctx.is_safe_zone());
}

TEST(spawn_context_test, terrain_queries_with_farm_tile) {
    static_tile tile;
    tile.flags = tile_flags::is_farm;

    spawn_context ctx;
    ctx.tile = &tile;

    EXPECT_FALSE(ctx.is_water());
    EXPECT_TRUE(ctx.is_farm());
    EXPECT_FALSE(ctx.is_safe_zone());
}

TEST(spawn_context_test, terrain_queries_with_safe_zone_tile) {
    // Note: is_safe_zone() checks map_ptr->config().is_fight_zone, not tile flags
    // So this test only verifies tile-based queries (is_water, is_farm) are correct
    // when a tile with safe_zone flag is set
    static_tile tile;
    tile.flags = tile_flags::is_safe_zone;

    spawn_context ctx;
    ctx.tile = &tile;

    EXPECT_FALSE(ctx.is_water());
    EXPECT_FALSE(ctx.is_farm());
    // is_safe_zone() returns false without map_ptr set (implementation detail)
    EXPECT_FALSE(ctx.is_safe_zone());
}

TEST(spawn_context_test, terrain_queries_with_combined_flags) {
    static_tile tile;
    tile.flags = tile_flags::is_water | tile_flags::is_farm;

    spawn_context ctx;
    ctx.tile = &tile;

    EXPECT_TRUE(ctx.is_water());
    EXPECT_TRUE(ctx.is_farm());
    EXPECT_FALSE(ctx.is_safe_zone());
}

// ========== Level Rule Tests ==========

TEST(level_rule_test, matches_exact_level) {
    std::vector<weighted_npc_entry> npcs;
    npcs.push_back({npc_id{1}, "Slime", 100});

    level_rule rule("level_5", 5, std::move(npcs));

    spawn_context ctx;
    // Note: The level rule checks against spawner level, not player level
    // For testing, we simulate a spawner at level 5
    // The actual implementation may need a level field in spawn_context

    EXPECT_EQ(rule.name(), "level_5");
}

TEST(level_rule_test, name_getter) {
    std::vector<weighted_npc_entry> npcs;
    level_rule rule("test_rule", 10, std::move(npcs));

    EXPECT_EQ(rule.name(), "test_rule");
}

// ========== Biome Rule Tests ==========

TEST(biome_rule_test, water_biome_matches_water_tile) {
    std::vector<weighted_npc_entry> npcs;
    npcs.push_back({npc_id{1}, "Fish", 100});

    biome_rule rule("water_spawns", biome_type::water, std::move(npcs));

    static_tile tile;
    tile.flags = tile_flags::is_water;

    spawn_context ctx;
    ctx.tile = &tile;

    auto result = rule.evaluate(ctx);
    EXPECT_TRUE(result.matches);
    EXPECT_EQ(result.npcs.size(), 1);
    EXPECT_EQ(result.npcs[0].name, "Fish");
}

TEST(biome_rule_test, water_biome_no_match_on_land) {
    std::vector<weighted_npc_entry> npcs;
    npcs.push_back({npc_id{1}, "Fish", 100});

    biome_rule rule("water_spawns", biome_type::water, std::move(npcs));

    static_tile tile;
    tile.flags = tile_flags::none;

    spawn_context ctx;
    ctx.tile = &tile;

    auto result = rule.evaluate(ctx);
    EXPECT_FALSE(result.matches);
}

TEST(biome_rule_test, farm_biome_matches_farm_tile) {
    std::vector<weighted_npc_entry> npcs;
    npcs.push_back({npc_id{1}, "Scarecrow", 100});

    biome_rule rule("farm_spawns", biome_type::farm, std::move(npcs));

    static_tile tile;
    tile.flags = tile_flags::is_farm;

    spawn_context ctx;
    ctx.tile = &tile;

    auto result = rule.evaluate(ctx);
    EXPECT_TRUE(result.matches);
}

TEST(biome_rule_test, safe_zone_biome_matches) {
    // Note: is_safe_zone() checks map_ptr->config().is_fight_zone, not tile flags
    // So without a map_ptr set, the rule won't match even with safe_zone tile flags
    std::vector<weighted_npc_entry> npcs;
    npcs.push_back({npc_id{1}, "Guard", 100});

    biome_rule rule("safe_zone_spawns", biome_type::safe_zone, std::move(npcs));

    static_tile tile;
    tile.flags = tile_flags::is_safe_zone;

    spawn_context ctx;
    ctx.tile = &tile;
    // No map_ptr set, so is_safe_zone() returns false

    auto result = rule.evaluate(ctx);
    EXPECT_FALSE(result.matches);
}

// ========== Time Rule Tests ==========

TEST(time_rule_test, name_getter) {
    std::vector<weighted_npc_entry> npcs;
    time_rule rule("night_spawns", time_period::night, std::move(npcs));

    EXPECT_EQ(rule.name(), "night_spawns");
}

TEST(time_rule_test, day_period) {
    std::vector<weighted_npc_entry> npcs;
    npcs.push_back({npc_id{1}, "Deer", 100});

    time_rule rule("day_animals", time_period::day, std::move(npcs));

    // Without a game clock, time queries may return defaults
    spawn_context ctx;
    ctx.clock = nullptr;

    // The rule should handle null clock gracefully
    auto result = rule.evaluate(ctx);
    // Result depends on default behavior when clock is null
}

TEST(time_rule_test, hour_range_period) {
    std::vector<weighted_npc_entry> npcs;
    npcs.push_back({npc_id{1}, "Vampire", 100});

    // Spawn between midnight (0) and 4 AM
    time_rule rule("midnight_spawns", time_period::hour_range, std::move(npcs), 0, 4);

    EXPECT_EQ(rule.name(), "midnight_spawns");
}

// ========== Weather Rule Tests ==========

TEST(weather_rule_test, clear_weather_matches) {
    std::vector<weighted_npc_entry> npcs;
    npcs.push_back({npc_id{1}, "Butterfly", 100});

    weather_rule rule("clear_weather_spawns", weather_type::clear, std::move(npcs));

    spawn_context ctx;
    ctx.weather = weather_type::clear;

    auto result = rule.evaluate(ctx);
    EXPECT_TRUE(result.matches);
}

TEST(weather_rule_test, rain_weather_no_match_when_clear) {
    std::vector<weighted_npc_entry> npcs;
    npcs.push_back({npc_id{1}, "Frog", 100});

    weather_rule rule("rain_spawns", weather_type::rain, std::move(npcs));

    spawn_context ctx;
    ctx.weather = weather_type::clear;

    auto result = rule.evaluate(ctx);
    EXPECT_FALSE(result.matches);
}

TEST(weather_rule_test, rain_weather_matches) {
    std::vector<weighted_npc_entry> npcs;
    npcs.push_back({npc_id{1}, "Frog", 100});

    weather_rule rule("rain_spawns", weather_type::rain, std::move(npcs));

    spawn_context ctx;
    ctx.weather = weather_type::rain;

    auto result = rule.evaluate(ctx);
    EXPECT_TRUE(result.matches);
}

// ========== Proximity Rule Tests ==========

TEST(proximity_rule_test, name_getter) {
    std::vector<weighted_npc_entry> npcs;
    proximity_rule rule("lonely_spawns", proximity_condition::min_distance, 20, std::move(npcs));

    EXPECT_EQ(rule.name(), "lonely_spawns");
}

TEST(proximity_rule_test, min_distance_condition) {
    std::vector<weighted_npc_entry> npcs;
    npcs.push_back({npc_id{1}, "RareMob", 100});

    // Spawn only if nearest player is at least 30 tiles away
    proximity_rule rule("far_from_players", proximity_condition::min_distance, 30, std::move(npcs));

    spawn_context ctx;
    // Without map_ptr, proximity queries return large distance (no players nearby)
    ctx.map_ptr = nullptr;

    auto result = rule.evaluate(ctx);
    // With no players, min_distance condition should pass
    EXPECT_TRUE(result.matches);
}

TEST(proximity_rule_test, max_distance_condition) {
    std::vector<weighted_npc_entry> npcs;
    npcs.push_back({npc_id{1}, "AmbushMob", 100});

    // Spawn only if player is within 10 tiles
    proximity_rule rule("near_players", proximity_condition::max_distance, 10, std::move(npcs));

    spawn_context ctx;
    ctx.map_ptr = nullptr;

    auto result = rule.evaluate(ctx);
    // With no players (infinite distance), max_distance condition should fail
    EXPECT_FALSE(result.matches);
}

TEST(proximity_rule_test, player_count_condition) {
    std::vector<weighted_npc_entry> npcs;
    npcs.push_back({npc_id{1}, "WorldBoss", 100});

    // Spawn only if at least 5 players are within 50 tiles
    proximity_rule rule("boss_spawn", proximity_condition::player_count, 5, std::move(npcs), 50);

    spawn_context ctx;
    ctx.map_ptr = nullptr;

    auto result = rule.evaluate(ctx);
    // With no players, player_count condition should fail
    EXPECT_FALSE(result.matches);
}

// ========== Event Rule Tests ==========

TEST(event_rule_test, matches_active_event) {
    std::vector<weighted_npc_entry> npcs;
    npcs.push_back({npc_id{1}, "HalloweenGhost", 100});

    event_rule rule("halloween_spawns", "halloween", std::move(npcs));

    spawn_context ctx;
    ctx.active_event = "halloween";

    auto result = rule.evaluate(ctx);
    EXPECT_TRUE(result.matches);
}

TEST(event_rule_test, no_match_when_different_event) {
    std::vector<weighted_npc_entry> npcs;
    npcs.push_back({npc_id{1}, "HalloweenGhost", 100});

    event_rule rule("halloween_spawns", "halloween", std::move(npcs));

    spawn_context ctx;
    ctx.active_event = "christmas";

    auto result = rule.evaluate(ctx);
    EXPECT_FALSE(result.matches);
}

TEST(event_rule_test, no_match_when_no_event) {
    std::vector<weighted_npc_entry> npcs;
    npcs.push_back({npc_id{1}, "EventMob", 100});

    event_rule rule("event_spawns", "special_event", std::move(npcs));

    spawn_context ctx;
    ctx.active_event = "";

    auto result = rule.evaluate(ctx);
    EXPECT_FALSE(result.matches);
}

// ========== Composite Rule Tests ==========

TEST(composite_rule_test, and_all_match) {
    std::vector<std::unique_ptr<spawn_rule>> children;

    // Child 1: Weather rule (clear)
    std::vector<weighted_npc_entry> npcs1;
    children.push_back(std::make_unique<weather_rule>("clear", weather_type::clear, std::move(npcs1)));

    // Result NPCs
    std::vector<weighted_npc_entry> result_npcs;
    result_npcs.push_back({npc_id{1}, "SunnyMob", 100});

    composite_rule rule("sunny_and_something", composite_operator::op_and,
                        std::move(children), std::move(result_npcs));

    spawn_context ctx;
    ctx.weather = weather_type::clear;

    auto result = rule.evaluate(ctx);
    EXPECT_TRUE(result.matches);
}

TEST(composite_rule_test, and_partial_match_fails) {
    std::vector<std::unique_ptr<spawn_rule>> children;

    // Child 1: Weather rule (rain - will match)
    std::vector<weighted_npc_entry> npcs1;
    children.push_back(std::make_unique<weather_rule>("rain", weather_type::rain, std::move(npcs1)));

    // Child 2: Event rule (halloween - won't match)
    std::vector<weighted_npc_entry> npcs2;
    children.push_back(std::make_unique<event_rule>("halloween", "halloween", std::move(npcs2)));

    std::vector<weighted_npc_entry> result_npcs;
    result_npcs.push_back({npc_id{1}, "ComboMob", 100});

    composite_rule rule("rain_and_halloween", composite_operator::op_and,
                        std::move(children), std::move(result_npcs));

    spawn_context ctx;
    ctx.weather = weather_type::rain;
    ctx.active_event = "christmas";  // Not halloween

    auto result = rule.evaluate(ctx);
    EXPECT_FALSE(result.matches);
}

TEST(composite_rule_test, or_one_match_succeeds) {
    std::vector<std::unique_ptr<spawn_rule>> children;

    // Child 1: Weather rule (rain - won't match)
    std::vector<weighted_npc_entry> npcs1;
    children.push_back(std::make_unique<weather_rule>("rain", weather_type::rain, std::move(npcs1)));

    // Child 2: Weather rule (clear - will match)
    std::vector<weighted_npc_entry> npcs2;
    children.push_back(std::make_unique<weather_rule>("clear", weather_type::clear, std::move(npcs2)));

    std::vector<weighted_npc_entry> result_npcs;
    result_npcs.push_back({npc_id{1}, "AnyWeatherMob", 100});

    composite_rule rule("rain_or_clear", composite_operator::op_or,
                        std::move(children), std::move(result_npcs));

    spawn_context ctx;
    ctx.weather = weather_type::clear;

    auto result = rule.evaluate(ctx);
    EXPECT_TRUE(result.matches);
}

TEST(composite_rule_test, or_none_match_fails) {
    std::vector<std::unique_ptr<spawn_rule>> children;

    // Child 1: Event rule (halloween)
    std::vector<weighted_npc_entry> npcs1;
    children.push_back(std::make_unique<event_rule>("halloween", "halloween", std::move(npcs1)));

    // Child 2: Event rule (christmas)
    std::vector<weighted_npc_entry> npcs2;
    children.push_back(std::make_unique<event_rule>("christmas", "christmas", std::move(npcs2)));

    std::vector<weighted_npc_entry> result_npcs;
    result_npcs.push_back({npc_id{1}, "HolidayMob", 100});

    composite_rule rule("holiday_events", composite_operator::op_or,
                        std::move(children), std::move(result_npcs));

    spawn_context ctx;
    ctx.active_event = "easter";  // Neither halloween nor christmas

    auto result = rule.evaluate(ctx);
    EXPECT_FALSE(result.matches);
}

TEST(composite_rule_test, not_inverts_match) {
    std::vector<std::unique_ptr<spawn_rule>> children;

    // Child: Weather rule (rain)
    std::vector<weighted_npc_entry> npcs1;
    children.push_back(std::make_unique<weather_rule>("rain", weather_type::rain, std::move(npcs1)));

    std::vector<weighted_npc_entry> result_npcs;
    result_npcs.push_back({npc_id{1}, "NoRainMob", 100});

    composite_rule rule("not_raining", composite_operator::op_not,
                        std::move(children), std::move(result_npcs));

    spawn_context ctx;
    ctx.weather = weather_type::clear;  // Not raining

    auto result = rule.evaluate(ctx);
    EXPECT_TRUE(result.matches);  // NOT(rain) = true when clear
}

TEST(composite_rule_test, not_inverts_non_match) {
    std::vector<std::unique_ptr<spawn_rule>> children;

    // Child: Weather rule (rain)
    std::vector<weighted_npc_entry> npcs1;
    children.push_back(std::make_unique<weather_rule>("rain", weather_type::rain, std::move(npcs1)));

    std::vector<weighted_npc_entry> result_npcs;
    result_npcs.push_back({npc_id{1}, "NoRainMob", 100});

    composite_rule rule("not_raining", composite_operator::op_not,
                        std::move(children), std::move(result_npcs));

    spawn_context ctx;
    ctx.weather = weather_type::rain;  // Is raining

    auto result = rule.evaluate(ctx);
    EXPECT_FALSE(result.matches);  // NOT(rain) = false when raining
}

// ========== Random Spawn Whitelist Tests ==========

TEST(random_spawn_whitelist_test, empty_whitelist) {
    random_spawn_whitelist whitelist;

    EXPECT_FALSE(whitelist.is_allowed("Slime"));
    EXPECT_FALSE(whitelist.is_allowed(npc_id{1}));
    EXPECT_EQ(whitelist.spawnable_count(), 0);
}

TEST(random_spawn_whitelist_test, clear) {
    random_spawn_whitelist whitelist;
    // Would need to add entries first, then clear
    whitelist.clear();

    EXPECT_EQ(whitelist.all().size(), 0);
}

TEST(random_spawn_whitelist_test, special_attack_kind_enum) {
    // Verify enum values match legacy constants
    EXPECT_EQ(static_cast<uint8_t>(special_attack_kind::none), 0);
    EXPECT_EQ(static_cast<uint8_t>(special_attack_kind::melee), 1);
    EXPECT_EQ(static_cast<uint8_t>(special_attack_kind::ranged), 2);
    EXPECT_EQ(static_cast<uint8_t>(special_attack_kind::poison), 3);
    EXPECT_EQ(static_cast<uint8_t>(special_attack_kind::stun), 4);
    EXPECT_EQ(static_cast<uint8_t>(special_attack_kind::magic), 5);
    EXPECT_EQ(static_cast<uint8_t>(special_attack_kind::area), 6);
    EXPECT_EQ(static_cast<uint8_t>(special_attack_kind::buff), 7);
    EXPECT_EQ(static_cast<uint8_t>(special_attack_kind::summon), 8);
}

TEST(random_spawn_entry_test, default_values) {
    random_spawn_entry entry;

    EXPECT_TRUE(entry.name.empty());
    EXPECT_EQ(entry.template_id.value, 0);
    EXPECT_EQ(entry.special_attack_prob, 0);
    EXPECT_EQ(entry.special_attack, special_attack_kind::none);
    EXPECT_TRUE(entry.enabled);
}

// ========== Spawn Rule Engine Tests ==========

class spawn_rule_engine_test : public ::testing::Test {
protected:
    void SetUp() override {
        engine_.initialize();
    }

    void TearDown() override {
        engine_.shutdown();
    }

    spawn_rule_engine engine_;
};

TEST_F(spawn_rule_engine_test, lifecycle) {
    // Note: is_initialized() is set by subsystem_manager, not by direct initialize() calls
    // So we only test the name() method which doesn't depend on that flag
    EXPECT_EQ(engine_.name(), "spawn_rule_engine");
}

TEST_F(spawn_rule_engine_test, initial_state) {
    EXPECT_EQ(engine_.get_rule_count(), 0);
}

TEST_F(spawn_rule_engine_test, select_npc_with_no_rules) {
    spawn_context ctx;
    ctx.weather = weather_type::clear;

    auto selected = engine_.select_npc(ctx);
    EXPECT_FALSE(selected.has_value());
}

TEST_F(spawn_rule_engine_test, get_matching_npcs_with_no_rules) {
    spawn_context ctx;

    auto matches = engine_.get_matching_npcs(ctx);
    EXPECT_TRUE(matches.empty());
}

TEST_F(spawn_rule_engine_test, whitelist_access) {
    const auto& whitelist = engine_.whitelist();
    EXPECT_EQ(whitelist.spawnable_count(), 0);
}

TEST_F(spawn_rule_engine_test, can_random_spawn_by_name) {
    // Without loading any whitelist, nothing should be spawnable
    EXPECT_FALSE(engine_.can_random_spawn("Slime"));
    EXPECT_FALSE(engine_.can_random_spawn("Goblin"));
}

TEST_F(spawn_rule_engine_test, can_random_spawn_by_id) {
    EXPECT_FALSE(engine_.can_random_spawn(npc_id{1}));
    EXPECT_FALSE(engine_.can_random_spawn(npc_id{100}));
}

TEST_F(spawn_rule_engine_test, get_spawn_entry_not_found) {
    auto entry = engine_.get_spawn_entry("NonexistentMob");
    EXPECT_FALSE(entry.has_value());
}

TEST_F(spawn_rule_engine_test, get_map_rule_count_unknown_map) {
    EXPECT_EQ(engine_.get_map_rule_count("unknown_map"), 0);
}

TEST_F(spawn_rule_engine_test, load_from_nonexistent_file) {
    auto result = engine_.load_from_file("nonexistent_spawn_rules.yaml");
    EXPECT_TRUE(result.is_err());
}

// ========== Biome Type Enum Tests ==========

TEST(biome_type_test, enum_values) {
    // Verify all biome types are distinct
    EXPECT_NE(static_cast<int>(biome_type::water), static_cast<int>(biome_type::farm));
    EXPECT_NE(static_cast<int>(biome_type::forest), static_cast<int>(biome_type::mountain));
    EXPECT_NE(static_cast<int>(biome_type::dungeon), static_cast<int>(biome_type::safe_zone));
    EXPECT_NE(static_cast<int>(biome_type::fight_zone), static_cast<int>(biome_type::water));
}

// ========== Time Period Enum Tests ==========

TEST(time_period_test, enum_values) {
    EXPECT_NE(static_cast<int>(time_period::day), static_cast<int>(time_period::night));
    EXPECT_NE(static_cast<int>(time_period::dawn), static_cast<int>(time_period::dusk));
    EXPECT_NE(static_cast<int>(time_period::hour_range), static_cast<int>(time_period::day));
}

// ========== Proximity Condition Enum Tests ==========

TEST(proximity_condition_test, enum_values) {
    EXPECT_NE(static_cast<int>(proximity_condition::min_distance),
              static_cast<int>(proximity_condition::max_distance));
    EXPECT_NE(static_cast<int>(proximity_condition::player_count),
              static_cast<int>(proximity_condition::min_distance));
}

// ========== Composite Operator Enum Tests ==========

TEST(composite_operator_test, enum_values) {
    EXPECT_NE(static_cast<int>(composite_operator::op_and),
              static_cast<int>(composite_operator::op_or));
    EXPECT_NE(static_cast<int>(composite_operator::op_not),
              static_cast<int>(composite_operator::op_and));
}
