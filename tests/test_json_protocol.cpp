// test_json_protocol.cpp
// Unit tests for JSON protocol message serialization/deserialization

#include <gtest/gtest.h>
#include "network/json_protocol.h"

using namespace hb::network;

// ========== Message Type Tests ==========

TEST(json_message_type_test, to_string_conversions)
{
    EXPECT_EQ(to_string(json_message_type::error), "error");
    EXPECT_EQ(to_string(json_message_type::ping), "ping");
    EXPECT_EQ(to_string(json_message_type::pong), "pong");
    EXPECT_EQ(to_string(json_message_type::login_request), "login_request");
    EXPECT_EQ(to_string(json_message_type::login_response), "login_response");
    EXPECT_EQ(to_string(json_message_type::create_account_request), "create_account_request");
    EXPECT_EQ(to_string(json_message_type::enter_game_request), "enter_game_request");
    EXPECT_EQ(to_string(json_message_type::player_move_request), "player_move_request");
    EXPECT_EQ(to_string(json_message_type::player_attack_request), "player_attack_request");
    EXPECT_EQ(to_string(json_message_type::chat_message), "chat_message");
    EXPECT_EQ(to_string(json_message_type::command_request), "command_request");
    EXPECT_EQ(to_string(json_message_type::npc_spawn), "npc_spawn");
    EXPECT_EQ(to_string(json_message_type::entity_death), "entity_death");
    EXPECT_EQ(to_string(json_message_type::unknown), "unknown");
}

TEST(json_message_type_test, parse_message_type)
{
    EXPECT_EQ(parse_message_type("login_request"), json_message_type::login_request);
    EXPECT_EQ(parse_message_type("login_response"), json_message_type::login_response);
    EXPECT_EQ(parse_message_type("ping"), json_message_type::ping);
    EXPECT_EQ(parse_message_type("pong"), json_message_type::pong);
    EXPECT_EQ(parse_message_type("chat_message"), json_message_type::chat_message);
    EXPECT_EQ(parse_message_type("invalid_type"), json_message_type::unknown);
    EXPECT_EQ(parse_message_type(""), json_message_type::unknown);
}

// ========== Attack Type Tests ==========

TEST(attack_type_test, enum_values)
{
    EXPECT_EQ(static_cast<uint8_t>(attack_type::regular), 0);
    EXPECT_EQ(static_cast<uint8_t>(attack_type::dash), 1);
    EXPECT_EQ(static_cast<uint8_t>(attack_type::ranged), 2);
}

// ========== Target Type Tests ==========

TEST(target_type_test, enum_values)
{
    EXPECT_EQ(static_cast<uint8_t>(target_type::none), 0);
    EXPECT_EQ(static_cast<uint8_t>(target_type::player), 1);
    EXPECT_EQ(static_cast<uint8_t>(target_type::npc), 2);
    EXPECT_EQ(static_cast<uint8_t>(target_type::ground), 3);
    EXPECT_EQ(static_cast<uint8_t>(target_type::item), 4);
}

// ========== Chat Channel Type Tests ==========

TEST(chat_channel_type_test, enum_values)
{
    EXPECT_EQ(static_cast<uint8_t>(chat_channel_type::local), 0);
    EXPECT_EQ(static_cast<uint8_t>(chat_channel_type::shout), 1);
    EXPECT_EQ(static_cast<uint8_t>(chat_channel_type::guild), 2);
    EXPECT_EQ(static_cast<uint8_t>(chat_channel_type::party), 3);
    EXPECT_EQ(static_cast<uint8_t>(chat_channel_type::whisper), 4);
    EXPECT_EQ(static_cast<uint8_t>(chat_channel_type::system), 8);
}

// ========== JSON Message Tests ==========

TEST(json_message_test, to_json)
{
    json_message msg;
    msg.type = json_message_type::ping;
    msg.seq = 42;
    msg.data = nlohmann::json::object();

    auto j = msg.to_json();

    EXPECT_EQ(j["type"], "ping");
    EXPECT_EQ(j["seq"], 42);
    EXPECT_TRUE(j["data"].is_object());
}

TEST(json_message_test, from_json_valid)
{
    nlohmann::json j = {
        {"type", "login_request"}, {"seq", 1}, {"data", {{"username", "test"}, {"password", "pass123"}}}};

    auto result = json_message::from_json(j);
    ASSERT_TRUE(result.is_ok());

    auto msg = result.value();
    EXPECT_EQ(msg.type, json_message_type::login_request);
    EXPECT_EQ(msg.seq, 1);
    EXPECT_EQ(msg.data["username"], "test");
}

TEST(json_message_test, from_json_missing_type)
{
    nlohmann::json j = {{"seq", 1}, {"data", {}}};

    auto result = json_message::from_json(j);
    EXPECT_TRUE(result.is_err());
}

TEST(json_message_test, parse_valid_json)
{
    std::string json_str = R"({"type":"ping","seq":5,"data":{}})";

    auto result = json_message::parse(json_str);
    ASSERT_TRUE(result.is_ok());

    auto msg = result.value();
    EXPECT_EQ(msg.type, json_message_type::ping);
    EXPECT_EQ(msg.seq, 5);
}

TEST(json_message_test, parse_invalid_json)
{
    std::string json_str = "not valid json{";

    auto result = json_message::parse(json_str);
    EXPECT_TRUE(result.is_err());
}

// ========== Login Request Data Tests ==========

TEST(login_request_data_test, from_json_valid)
{
    nlohmann::json j = {{"username", "testuser"}, {"password", "testpass123"}};

    auto result = login_request_data::from_json(j);
    ASSERT_TRUE(result.is_ok());

    auto data = result.value();
    EXPECT_EQ(data.username, "testuser");
    EXPECT_EQ(data.password, "testpass123");
}

TEST(login_request_data_test, from_json_missing_username)
{
    nlohmann::json j = {{"password", "testpass123"}};

    auto result = login_request_data::from_json(j);
    EXPECT_TRUE(result.is_err());
}

TEST(login_request_data_test, from_json_missing_password)
{
    nlohmann::json j = {{"username", "testuser"}};

    auto result = login_request_data::from_json(j);
    EXPECT_TRUE(result.is_err());
}

// ========== Create Account Request Data Tests ==========

TEST(create_account_request_data_test, from_json_valid)
{
    nlohmann::json j = {{"username", "newuser"}, {"password", "newpass123"}};

    auto result = create_account_request_data::from_json(j);
    ASSERT_TRUE(result.is_ok());

    auto data = result.value();
    EXPECT_EQ(data.username, "newuser");
    EXPECT_EQ(data.password, "newpass123");
}

// ========== Create Character Request Data Tests ==========

TEST(create_character_request_data_test, from_json_minimal)
{
    nlohmann::json j = {{"name", "TestChar"}, {"class_type", 1}, {"nation", 1}, {"gender", 0}};

    auto result = create_character_request_data::from_json(j);
    ASSERT_TRUE(result.is_ok());

    auto data = result.value();
    EXPECT_EQ(data.name, "TestChar");
    EXPECT_EQ(data.class_type, 1);
    EXPECT_EQ(data.nation, 1);
    EXPECT_EQ(data.gender, 0);
}

TEST(create_character_request_data_test, from_json_with_stats)
{
    nlohmann::json j = {{"name", "StatChar"},
                        {"class_type", 2},
                        {"nation", 2},
                        {"gender", 1},
                        {"hair_style", 3},
                        {"hair_color", 5},
                        {"skin_color", 2},
                        {"strength", 14},
                        {"dexterity", 12},
                        {"vitality", 10},
                        {"intelligence", 8},
                        {"magic", 10},
                        {"charisma", 6}};

    auto result = create_character_request_data::from_json(j);
    ASSERT_TRUE(result.is_ok());

    auto data = result.value();
    EXPECT_EQ(data.name, "StatChar");
    EXPECT_TRUE(data.strength.has_value());
    EXPECT_EQ(*data.strength, 14);
    EXPECT_TRUE(data.dexterity.has_value());
    EXPECT_EQ(*data.dexterity, 12);
}

// ========== Enter Game Request Data Tests ==========

TEST(enter_game_request_data_test, from_json_minimal)
{
    nlohmann::json j = {{"character_id", 12345}};

    auto result = enter_game_request_data::from_json(j);
    ASSERT_TRUE(result.is_ok());

    auto data = result.value();
    EXPECT_EQ(data.character_id, 12345);
    EXPECT_FALSE(data.force_disconnect);
    EXPECT_EQ(data.screen_width, 800);  // Default from from_json
    EXPECT_EQ(data.screen_height, 600); // Default from from_json
}

TEST(enter_game_request_data_test, from_json_with_resolution)
{
    nlohmann::json j = {
        {"character_id", 99}, {"force_disconnect", true}, {"screen_width", 1920}, {"screen_height", 1080}};

    auto result = enter_game_request_data::from_json(j);
    ASSERT_TRUE(result.is_ok());

    auto data = result.value();
    EXPECT_EQ(data.character_id, 99);
    EXPECT_TRUE(data.force_disconnect);
    EXPECT_EQ(data.screen_width, 1920);
    EXPECT_EQ(data.screen_height, 1080);
}

// ========== Player Move Request Data Tests ==========

TEST(player_move_request_data_test, from_json_walk)
{
    nlohmann::json j = {{"x", 100}, {"y", 200}, {"direction", 2}, {"is_running", false}, {"timestamp", 1234567890}};

    auto result = player_move_request_data::from_json(j);
    ASSERT_TRUE(result.is_ok());

    auto data = result.value();
    EXPECT_EQ(data.x, 100);
    EXPECT_EQ(data.y, 200);
    EXPECT_EQ(data.direction, 2);
    EXPECT_FALSE(data.is_running);
    EXPECT_EQ(data.timestamp, 1234567890);
}

TEST(player_move_request_data_test, from_json_run)
{
    nlohmann::json j = {{"x", 50}, {"y", 75}, {"direction", 6}, {"is_running", true}};

    auto result = player_move_request_data::from_json(j);
    ASSERT_TRUE(result.is_ok());

    auto data = result.value();
    EXPECT_TRUE(data.is_running);
}

// ========== Player Attack Request Data Tests ==========

TEST(player_attack_request_data_test, from_json_regular_attack)
{
    nlohmann::json j = {{"x", 100}, {"y", 100}, {"direction", 4}, {"type", 0}, {"target_type", 2}, {"target_id", 500}};

    auto result = player_attack_request_data::from_json(j);
    ASSERT_TRUE(result.is_ok());

    auto data = result.value();
    EXPECT_EQ(data.x, 100);
    EXPECT_EQ(data.y, 100);
    EXPECT_EQ(data.type, attack_type::regular);
    EXPECT_EQ(data.tgt_type, target_type::npc);
    EXPECT_EQ(data.target_id, 500);
}

TEST(player_attack_request_data_test, from_json_dash_attack)
{
    nlohmann::json j = {
        {"x", 50}, {"y", 50}, {"direction", 0}, {"attack_type", "dash"}, {"target_type", 1}, {"target_id", 42}};

    auto result = player_attack_request_data::from_json(j);
    ASSERT_TRUE(result.is_ok());

    auto data = result.value();
    EXPECT_EQ(data.type, attack_type::dash);
    EXPECT_EQ(data.tgt_type, target_type::player);
}

// ========== Player Magic Request Data Tests ==========

TEST(player_magic_request_data_test, from_json_targeted)
{
    nlohmann::json j = {
        {"x", 100}, {"y", 100}, {"direction", 2}, {"spell_id", 10}, {"target_type", 2}, {"target_id", 300}};

    auto result = player_magic_request_data::from_json(j);
    ASSERT_TRUE(result.is_ok());

    auto data = result.value();
    EXPECT_EQ(data.spell_id, 10);
    EXPECT_EQ(data.tgt_type, target_type::npc);
    EXPECT_EQ(data.target_id, 300);
}

TEST(player_magic_request_data_test, from_json_ground_target)
{
    nlohmann::json j = {{"x", 100},
                        {"y", 100},
                        {"direction", 2},
                        {"spell_id", 20},
                        {"target_type", 3},
                        {"target_x", 150},
                        {"target_y", 175}};

    auto result = player_magic_request_data::from_json(j);
    ASSERT_TRUE(result.is_ok());

    auto data = result.value();
    EXPECT_EQ(data.tgt_type, target_type::ground);
    EXPECT_EQ(data.target_x, 150);
    EXPECT_EQ(data.target_y, 175);
}

// ========== Chat Message Request Data Tests ==========

TEST(chat_message_request_data_test, from_json_simple)
{
    nlohmann::json j = {{"content", "Hello world!"}};

    auto result = chat_message_request_data::from_json(j);
    ASSERT_TRUE(result.is_ok());

    auto data = result.value();
    EXPECT_EQ(data.content, "Hello world!");
    EXPECT_FALSE(data.channel.has_value());
    EXPECT_FALSE(data.recipient_name.has_value());
}

TEST(chat_message_request_data_test, from_json_with_channel)
{
    nlohmann::json j = {{"content", "Guild message"}, {"channel", "guild"}};

    auto result = chat_message_request_data::from_json(j);
    ASSERT_TRUE(result.is_ok());

    auto data = result.value();
    EXPECT_TRUE(data.channel.has_value());
    EXPECT_EQ(*data.channel, "guild");
}

TEST(chat_message_request_data_test, from_json_whisper)
{
    nlohmann::json j = {{"content", "Secret message"}, {"channel", "whisper"}, {"recipient", "OtherPlayer"}};

    auto result = chat_message_request_data::from_json(j);
    ASSERT_TRUE(result.is_ok());

    auto data = result.value();
    EXPECT_TRUE(data.recipient_name.has_value());
    EXPECT_EQ(*data.recipient_name, "OtherPlayer");
}

// ========== Command Request Data Tests ==========

TEST(command_request_data_test, from_json_no_args)
{
    nlohmann::json j = {{"command", "help"}};

    auto result = command_request_data::from_json(j);
    ASSERT_TRUE(result.is_ok());

    auto data = result.value();
    EXPECT_EQ(data.command, "help");
    EXPECT_TRUE(data.args.empty());
}

TEST(command_request_data_test, from_json_with_args)
{
    nlohmann::json j = {{"command", "teleport"}, {"args", {"aresden", "100", "200"}}};

    auto result = command_request_data::from_json(j);
    ASSERT_TRUE(result.is_ok());

    auto data = result.value();
    EXPECT_EQ(data.command, "teleport");
    ASSERT_EQ(data.args.size(), 3);
    EXPECT_EQ(data.args[0], "aresden");
    EXPECT_EQ(data.args[1], "100");
    EXPECT_EQ(data.args[2], "200");
}

// ========== Response Builder Tests ==========

TEST(response_builders_test, make_error_response)
{
    auto msg = make_error_response(42, "invalid_request", "Missing required field");

    EXPECT_EQ(msg.type, json_message_type::error);
    EXPECT_EQ(msg.seq, 42);
    EXPECT_EQ(msg.data["error_code"], "invalid_request");
    EXPECT_EQ(msg.data["message"], "Missing required field");
}

TEST(response_builders_test, make_login_response_success)
{
    auto msg = make_login_response(1, true, "abc123token");

    EXPECT_EQ(msg.type, json_message_type::login_response);
    EXPECT_EQ(msg.seq, 1);
    EXPECT_TRUE(msg.data["success"]);
    EXPECT_EQ(msg.data["session_token"], "abc123token");
}

TEST(response_builders_test, make_login_response_failure)
{
    auto msg = make_login_response(2, false, std::nullopt, "Invalid credentials");

    EXPECT_EQ(msg.type, json_message_type::login_response);
    EXPECT_FALSE(msg.data["success"]);
    EXPECT_EQ(msg.data["error"], "Invalid credentials");
}

TEST(response_builders_test, make_pong_response)
{
    auto msg = make_pong_response(99);

    EXPECT_EQ(msg.type, json_message_type::pong);
    EXPECT_EQ(msg.seq, 99);
}

TEST(response_builders_test, make_logout_response)
{
    auto msg = make_logout_response(5, true);

    EXPECT_EQ(msg.type, json_message_type::logout_response);
    EXPECT_TRUE(msg.data["success"]);
}

TEST(response_builders_test, make_player_move_response_success)
{
    auto msg = make_player_move_response(10, true, 100, 200, 4);

    EXPECT_EQ(msg.type, json_message_type::player_move_response);
    EXPECT_TRUE(msg.data["success"]);
    EXPECT_EQ(msg.data["x"], 100);
    EXPECT_EQ(msg.data["y"], 200);
    EXPECT_EQ(msg.data["direction"], 4);
}

TEST(response_builders_test, make_player_position_update)
{
    auto msg = make_player_position_update(42, 150, 175, 6, true);

    EXPECT_EQ(msg.type, json_message_type::player_position_update);
    EXPECT_EQ(msg.data["entity_id"], 42);
    EXPECT_EQ(msg.data["x"], 150);
    EXPECT_EQ(msg.data["y"], 175);
    EXPECT_EQ(msg.data["direction"], 6);
    EXPECT_TRUE(msg.data["is_running"]);
}

TEST(response_builders_test, make_entity_hp_update)
{
    auto msg = make_entity_hp_update(100, 75, 100);

    EXPECT_EQ(msg.type, json_message_type::entity_hp_update);
    EXPECT_EQ(msg.data["entity_id"], 100);
    EXPECT_EQ(msg.data["hp"], 75);
    EXPECT_EQ(msg.data["hp_max"], 100);
}

TEST(response_builders_test, make_entity_death)
{
    auto msg = make_entity_death(50, 42, 100, 200);

    EXPECT_EQ(msg.type, json_message_type::entity_death);
    EXPECT_EQ(msg.data["victim_id"], 50);
    EXPECT_EQ(msg.data["killer_id"], 42);
    EXPECT_EQ(msg.data["x"], 100);
    EXPECT_EQ(msg.data["y"], 200);
}

TEST(response_builders_test, make_entity_spawn)
{
    visible_entity_msg entity;
    entity.entity_id = 99;
    entity.type = "player";
    entity.name = "TestPlayer";
    entity.x = 100;
    entity.y = 200;
    entity.hp_percent = 100;
    entity.direction = 2;

    auto msg = make_entity_spawn(0, entity);

    EXPECT_EQ(msg.type, json_message_type::entity_spawn);
    EXPECT_EQ(msg.data["entity_id"], 99);
    EXPECT_EQ(msg.data["type"], "player");
    EXPECT_EQ(msg.data["name"], "TestPlayer");
}

TEST(response_builders_test, make_entity_despawn)
{
    auto msg = make_entity_despawn(0, 42);

    EXPECT_EQ(msg.type, json_message_type::entity_despawn);
    EXPECT_EQ(msg.data["entity_id"], 42);
}

TEST(response_builders_test, make_command_response)
{
    nlohmann::json result_data = {{"online_count", 42}};
    auto msg = make_command_response(5, true, "who", "42 players online", result_data);

    EXPECT_EQ(msg.type, json_message_type::command_response);
    EXPECT_TRUE(msg.data["success"]);
    EXPECT_EQ(msg.data["command"], "who");
    EXPECT_EQ(msg.data["message"], "42 players online");
    EXPECT_EQ(msg.data["result"]["online_count"], 42);
}

// ========== NPC Message Tests ==========

TEST(npc_spawn_data_test, to_json)
{
    npc_spawn_data data;
    data.entity_id = 500;
    data.template_id = 10;
    data.name = "Slime";
    data.x = 100;
    data.y = 200;
    data.direction = 4;
    data.hp = 50;
    data.max_hp = 100;
    data.level = 5;

    auto j = data.to_json();

    EXPECT_EQ(j["entity_id"], 500);
    EXPECT_EQ(j["template_id"], 10);
    EXPECT_EQ(j["name"], "Slime");
    EXPECT_EQ(j["x"], 100);
    EXPECT_EQ(j["y"], 200);
}

TEST(npc_move_data_test, to_json)
{
    npc_move_data data;
    data.entity_id = 500;
    data.x = 105;
    data.y = 205;
    data.direction = 2;

    auto j = data.to_json();

    EXPECT_EQ(j["entity_id"], 500);
    EXPECT_EQ(j["x"], 105);
    EXPECT_EQ(j["y"], 205);
    EXPECT_EQ(j["direction"], 2);
}

TEST(npc_attack_data_test, to_json)
{
    npc_attack_data data;
    data.attacker_id = 500;
    data.target_id = 42;
    data.damage = 25;
    data.is_critical = true;

    auto j = data.to_json();

    EXPECT_EQ(j["attacker_id"], 500);
    EXPECT_EQ(j["target_id"], 42);
    EXPECT_EQ(j["damage"], 25);
    EXPECT_TRUE(j["is_critical"]);
}

TEST(entity_death_test, npc_death_uses_entity_death)
{
    auto msg = make_entity_death(500, 42, 100, 200);

    EXPECT_EQ(msg.type, json_message_type::entity_death);
    EXPECT_EQ(msg.data["victim_id"], 500);
    EXPECT_EQ(msg.data["killer_id"], 42);
    EXPECT_EQ(msg.data["x"], 100);
    EXPECT_EQ(msg.data["y"], 200);
}

TEST(npc_message_builders_test, make_npc_spawn_message)
{
    npc_spawn_data data;
    data.entity_id = 100;
    data.name = "Goblin";

    auto msg = make_npc_spawn_message(data);

    EXPECT_EQ(msg.type, json_message_type::npc_spawn);
}

TEST(npc_message_builders_test, make_npc_despawn_message)
{
    auto msg = make_npc_despawn_message(100);

    EXPECT_EQ(msg.type, json_message_type::npc_despawn);
    EXPECT_EQ(msg.data["entity_id"], 100);
}

// ========== Chat Broadcast Data Tests ==========

TEST(chat_message_broadcast_data_test, to_json)
{
    chat_message_broadcast_data data;
    data.channel = "local";
    data.sender_id = 42;
    data.sender_name = "TestPlayer";
    data.content = "Hello everyone!";
    data.timestamp = "2024-01-01T12:00:00Z";

    auto j = data.to_json();

    EXPECT_EQ(j["channel"], "local");
    EXPECT_EQ(j["sender_id"], 42);
    EXPECT_EQ(j["sender_name"], "TestPlayer");
    EXPECT_EQ(j["content"], "Hello everyone!");
}

TEST(chat_message_broadcast_data_test, to_json_with_flags)
{
    chat_message_broadcast_data data;
    data.channel = "shout";
    data.sender_id = 1;
    data.sender_name = "GM";
    data.content = "Announcement";
    data.flags = {"system", "gm"};
    data.timestamp = "2024-01-01T12:00:00Z";

    auto j = data.to_json();

    EXPECT_EQ(j["flags"].size(), 2);
}

// ========== Visibility Calculation Tests ==========

TEST(visibility_test, calculate_visibility_radius_640x480)
{
    auto [rx, ry] = calculate_visibility_radius(640, 480);
    // x: tiles=20, base=10, buf=max(5,2)=5 → 15; y: tiles=15, base=7.5, buf=5 → 15 (clamped)
    EXPECT_EQ(rx, 15);
    EXPECT_EQ(ry, 15);
}

TEST(visibility_test, calculate_visibility_radius_1920x1080)
{
    auto [rx, ry] = calculate_visibility_radius(1920, 1080);
    // x: tiles=60, base=30, buf=max(5,6)=6 → 36; y: tiles=33.75, base=16.875, buf=5 → 21
    EXPECT_EQ(rx, 36);
    EXPECT_EQ(ry, 21);
}

TEST(visibility_test, calculate_visibility_radius_1280x720)
{
    auto [rx, ry] = calculate_visibility_radius(1280, 720);
    // x: tiles=40, base=20, buf=max(5,4)=5 → 25; y: tiles=22.5, base=11.25, buf=5 → 16
    EXPECT_EQ(rx, 25);
    EXPECT_EQ(ry, 16);
}

TEST(visibility_test, calculate_visibility_radius_800x600)
{
    auto [rx, ry] = calculate_visibility_radius(800, 600);
    // x: tiles=25, base=12.5, buf=max(5,2.5)=5 → 17; y: tiles=18.75, base=9.375, buf=5 → 15 (clamped)
    EXPECT_EQ(rx, 17);
    EXPECT_EQ(ry, 15);
}

TEST(visibility_test, widescreen_x_greater_than_y)
{
    // For widescreen resolutions, X radius should exceed Y radius
    auto [rx, ry] = calculate_visibility_radius(1920, 1080);
    EXPECT_GT(rx, ry);
}

TEST(visibility_test, square_input_equal_x_y)
{
    // Square viewport produces equal X and Y radii
    auto [rx, ry] = calculate_visibility_radius(800, 800);
    EXPECT_EQ(rx, ry);
}

TEST(visibility_test, larger_viewport_increases_radius)
{
    // Commander mode sends larger effective viewport (e.g., screen / min_zoom)
    auto [nx, ny] = calculate_visibility_radius(800, 600);
    auto [cx, cy] = calculate_visibility_radius(1600, 1200); // 2x effective viewport
    EXPECT_GT(cx, nx);
    EXPECT_GT(cy, ny);
}

TEST(visibility_test, radius_never_below_minimum)
{
    auto [rx, ry] = calculate_visibility_radius(320, 240);
    EXPECT_GE(rx, min_visibility_radius);
    EXPECT_GE(ry, min_visibility_radius);
}

TEST(visibility_test, radius_never_above_maximum)
{
    // Even with huge effective viewport, radii are capped
    auto [rx, ry] = calculate_visibility_radius(8000, 6000);
    EXPECT_LE(rx, max_visibility_radius);
    EXPECT_LE(ry, max_visibility_radius);
}

TEST(visibility_test, proportional_buffer_kicks_in_at_large_viewports)
{
    // At 1920x1080 X axis: base=30, 20% buffer=6 > min buffer of 5
    auto [rx, ry] = calculate_visibility_radius(1920, 1080);
    // Without proportional buffer (flat +5): would be 35
    // With proportional buffer (+6): should be 36
    EXPECT_EQ(rx, 36);
}

// ========== Teleporter Message Tests ==========

TEST(teleporter_info_msg_test, to_json)
{
    teleporter_info_msg info;
    info.id = 123456;
    info.x = 100;
    info.y = 200;
    info.dest_map = "elvine";
    info.dest_x = 50;
    info.dest_y = 75;
    info.dest_dir = 4;

    auto j = info.to_json();

    EXPECT_EQ(j["id"], 123456);
    EXPECT_EQ(j["x"], 100);
    EXPECT_EQ(j["y"], 200);
    EXPECT_EQ(j["dest_map"], "elvine");
}

TEST(map_teleporters_msg_test, to_json)
{
    map_teleporters_msg msg;
    msg.map_name = "aresden";

    teleporter_info_msg tp1;
    tp1.id = 1;
    tp1.x = 10;
    tp1.y = 20;
    tp1.dest_map = "elvine";
    msg.teleporters.push_back(tp1);

    auto j = msg.to_json();

    EXPECT_EQ(j["map_name"], "aresden");
    EXPECT_EQ(j["teleporters"].size(), 1);
}

// ========== Character Data Message Tests ==========

TEST(character_data_msg_test, to_json)
{
    character_data_msg data;
    data.id = 12345;
    data.name = "TestHero";
    data.level = 50;
    data.class_type = 1;
    data.nation = 1;
    data.gender = 0;
    data.map_name = "aresden";
    data.pos_x = 100;
    data.pos_y = 200;
    data.hp = 500;
    data.hp_max = 600;
    data.mp = 200;
    data.mp_max = 250;
    data.sp = 100;
    data.sp_max = 100;
    data.gold = 10000;
    data.str = 30;
    data.dex = 25;
    data.vit = 20;
    data.int_ = 15;
    data.mag = 10;
    data.cha = 10;
    data.experience = 1000000;

    auto j = data.to_json();

    EXPECT_EQ(j["id"], 12345);
    EXPECT_EQ(j["name"], "TestHero");
    EXPECT_EQ(j["level"], 50);
    EXPECT_EQ(j["hp"], 500);
    EXPECT_EQ(j["hp_max"], 600);
    EXPECT_EQ(j["gold"], 10000);
}

// ========== Inventory/Equipment Message Tests ==========

TEST(inventory_item_msg_test, to_json)
{
    inventory_item_msg item;
    item.slot = 5;
    item.item_id = 100;
    item.name = "Health Potion";
    item.count = 10;
    item.durability = 0;
    item.max_durability = 0;

    auto j = item.to_json();

    EXPECT_EQ(j["slot"], 5);
    EXPECT_EQ(j["item_id"], 100);
    EXPECT_EQ(j["name"], "Health Potion");
    EXPECT_EQ(j["count"], 10);
}

TEST(equipment_item_msg_test, to_json)
{
    equipment_item_msg item;
    item.slot = 1; // Weapon slot
    item.item_id = 500;
    item.name = "Iron Sword";
    item.durability = 80;
    item.max_durability = 100;

    auto j = item.to_json();

    EXPECT_EQ(j["slot"], 1);
    EXPECT_EQ(j["item_id"], 500);
    EXPECT_EQ(j["name"], "Iron Sword");
    EXPECT_EQ(j["durability"], 80);
}

// ========== Attack/Magic/Skill Result Tests ==========

TEST(attack_result_msg_test, to_json_hit)
{
    attack_result_msg result;
    result.hit = true;
    result.critical = false;
    result.damage = 50;
    result.target_id = 100;
    result.target_hp = 150;
    result.target_hp_max = 200;

    auto j = result.to_json();

    EXPECT_TRUE(j["hit"]);
    EXPECT_FALSE(j["critical"]);
    EXPECT_EQ(j["damage"], 50);
}

TEST(attack_result_msg_test, to_json_critical)
{
    attack_result_msg result;
    result.hit = true;
    result.critical = true;
    result.damage = 100;

    auto j = result.to_json();

    EXPECT_TRUE(j["critical"]);
    EXPECT_EQ(j["damage"], 100);
}

TEST(magic_result_msg_test, to_json)
{
    magic_result_msg result;
    result.success = true;
    result.spell_id = 10;
    result.mana_cost = 25;
    result.damage = 75;
    result.caster_mp = 175;

    auto j = result.to_json();

    EXPECT_TRUE(j["success"]);
    EXPECT_EQ(j["spell_id"], 10);
    EXPECT_EQ(j["mana_cost"], 25);
    EXPECT_EQ(j["damage"], 75);
}

TEST(pickup_result_msg_test, to_json)
{
    pickup_result_msg result;
    result.success = true;
    result.item_id = 100;
    result.item_name = "Gold Coin";
    result.quantity = 50;
    result.inventory_slot = 10;

    auto j = result.to_json();

    EXPECT_TRUE(j["success"]);
    EXPECT_EQ(j["item_name"], "Gold Coin");
    EXPECT_EQ(j["quantity"], 50);
}
