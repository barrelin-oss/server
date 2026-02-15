// test_environment.cpp
// Unit tests for environment (day/night + weather) system

#include "network/json_protocol.h"
#include "world/map.h"

#include <gtest/gtest.h>
#include <chrono>

namespace hb::network
{

// === environment_update_data tests ===

TEST(environment_update_test, to_json_basic)
{
    environment_update_data data{.hour = 14, .minute = 30, .is_day = true, .weather = 0};

    auto j = data.to_json();
    EXPECT_EQ(j["hour"], 14);
    EXPECT_EQ(j["minute"], 30);
    EXPECT_EQ(j["is_day"], true);
    EXPECT_EQ(j["weather"], 0);
}

TEST(environment_update_test, to_json_night_with_weather)
{
    environment_update_data data{.hour = 22, .minute = 45, .is_day = false, .weather = 2};

    auto j = data.to_json();
    EXPECT_EQ(j["hour"], 22);
    EXPECT_EQ(j["minute"], 45);
    EXPECT_EQ(j["is_day"], false);
    EXPECT_EQ(j["weather"], 2);
}

TEST(environment_update_test, make_environment_update_builds_valid_message)
{
    environment_update_data data{.hour = 6, .minute = 0, .is_day = true, .weather = 1};

    auto msg = make_environment_update(data);
    EXPECT_EQ(msg.type, json_message_type::environment_update);
    EXPECT_EQ(msg.seq, 0u);
    EXPECT_EQ(msg.data["hour"], 6);
    EXPECT_EQ(msg.data["minute"], 0);
    EXPECT_EQ(msg.data["is_day"], true);
    EXPECT_EQ(msg.data["weather"], 1);
}

// === game_state_msg environment tests ===

TEST(environment_update_test, game_state_msg_includes_environment)
{
    game_state_msg state{};
    state.time_hour = 20;
    state.time_minute = 15;
    state.is_day = false;
    state.weather = 3;

    auto j = state.to_json();
    ASSERT_TRUE(j.contains("world"));
    ASSERT_TRUE(j["world"].contains("environment"));

    auto& env = j["world"]["environment"];
    EXPECT_EQ(env["hour"], 20);
    EXPECT_EQ(env["minute"], 15);
    EXPECT_EQ(env["is_day"], false);
    EXPECT_EQ(env["weather"], 3);
}

TEST(environment_update_test, game_state_msg_default_environment)
{
    game_state_msg state{};

    auto j = state.to_json();
    auto& env = j["world"]["environment"];
    EXPECT_EQ(env["hour"], 12);
    EXPECT_EQ(env["minute"], 0);
    EXPECT_EQ(env["is_day"], true);
    EXPECT_EQ(env["weather"], 0);
}

// === Protocol message type tests ===

TEST(environment_update_test, parse_message_type)
{
    auto type = parse_message_type("environment_update");
    EXPECT_EQ(type, json_message_type::environment_update);
}

TEST(environment_update_test, to_string_round_trip)
{
    auto str = to_string(json_message_type::environment_update);
    EXPECT_EQ(str, "environment_update");
}

} // namespace hb::network

namespace hb::world
{

// === Map weather timing tests ===

TEST(map_weather_test, initial_state_is_clear_and_inactive)
{
    map m;
    map_config cfg;
    cfg.name = "test";
    cfg.width = 10;
    cfg.height = 10;
    m.initialize(map_id{1}, cfg);

    EXPECT_EQ(m.weather(), weather_type::clear);
    EXPECT_FALSE(m.weather_active());
}

TEST(map_weather_test, start_weather_activates)
{
    map m;
    map_config cfg;
    cfg.name = "test";
    cfg.width = 10;
    cfg.height = 10;
    m.initialize(map_id{1}, cfg);

    auto end = std::chrono::steady_clock::now() + std::chrono::minutes(5);
    m.start_weather(weather_type::rain, end);

    EXPECT_EQ(m.weather(), weather_type::rain);
    EXPECT_TRUE(m.weather_active());
    EXPECT_EQ(m.weather_end_time(), end);
}

TEST(map_weather_test, clear_weather_resets)
{
    map m;
    map_config cfg;
    cfg.name = "test";
    cfg.width = 10;
    cfg.height = 10;
    m.initialize(map_id{1}, cfg);

    auto end = std::chrono::steady_clock::now() + std::chrono::minutes(5);
    m.start_weather(weather_type::heavy_snow, end);
    EXPECT_TRUE(m.weather_active());

    m.clear_weather();
    EXPECT_EQ(m.weather(), weather_type::clear);
    EXPECT_FALSE(m.weather_active());
}

TEST(map_weather_test, start_weather_overwrites_previous)
{
    map m;
    map_config cfg;
    cfg.name = "test";
    cfg.width = 10;
    cfg.height = 10;
    m.initialize(map_id{1}, cfg);

    auto end1 = std::chrono::steady_clock::now() + std::chrono::minutes(5);
    m.start_weather(weather_type::rain, end1);

    auto end2 = std::chrono::steady_clock::now() + std::chrono::minutes(10);
    m.start_weather(weather_type::stormy, end2);

    EXPECT_EQ(m.weather(), weather_type::stormy);
    EXPECT_TRUE(m.weather_active());
    EXPECT_EQ(m.weather_end_time(), end2);
}

TEST(map_weather_test, fixed_day_mode_config)
{
    map m;
    map_config cfg;
    cfg.name = "safe_town";
    cfg.width = 10;
    cfg.height = 10;
    cfg.is_fixed_day_mode = true;
    m.initialize(map_id{1}, cfg);

    EXPECT_TRUE(m.config().is_fixed_day_mode);
}

TEST(map_weather_test, snow_enabled_config)
{
    map m;
    map_config cfg;
    cfg.name = "ice_map";
    cfg.width = 10;
    cfg.height = 10;
    cfg.is_snow_enabled = true;
    m.initialize(map_id{1}, cfg);

    EXPECT_TRUE(m.config().is_snow_enabled);
}

} // namespace hb::world
