// test_admin_web.cpp
// Tests for admin web tool protocol messages and handler logic

#include <gtest/gtest.h>

#include "network/json_protocol.h"
#include "network/websocket_server.h"

using namespace hb;

// ============================================================================
// Protocol Message Tests
// ============================================================================

TEST(admin_web_protocol, enter_admin_mode_response_success) {
    auto msg = network::make_enter_admin_mode_response(42, true, 10);
    EXPECT_EQ(msg.type, network::json_message_type::enter_admin_mode_response);
    EXPECT_EQ(msg.seq, 42u);
    EXPECT_TRUE(msg.data["success"].get<bool>());
    EXPECT_EQ(msg.data["admin_level"].get<int>(), 10);
    EXPECT_FALSE(msg.data.contains("error"));
}

TEST(admin_web_protocol, enter_admin_mode_response_failure) {
    auto msg = network::make_enter_admin_mode_response(42, false, 0, "insufficient_permissions");
    EXPECT_FALSE(msg.data["success"].get<bool>());
    EXPECT_EQ(msg.data["error"].get<std::string>(), "insufficient_permissions");
}

TEST(admin_web_protocol, admin_response_generic_success) {
    nlohmann::json data = {{"player_count", 5}, {"npc_count", 100}};
    auto msg = network::make_admin_response(
        network::json_message_type::admin_server_stats_response,
        1, true, data);
    EXPECT_EQ(msg.type, network::json_message_type::admin_server_stats_response);
    EXPECT_EQ(msg.seq, 1u);
    EXPECT_TRUE(msg.data["success"].get<bool>());
    EXPECT_EQ(msg.data["player_count"].get<int>(), 5);
}

TEST(admin_web_protocol, admin_response_generic_failure) {
    auto msg = network::make_admin_response(
        network::json_message_type::admin_get_player_response,
        2, false, {}, "Player not found");
    EXPECT_FALSE(msg.data["success"].get<bool>());
    EXPECT_EQ(msg.data["error"].get<std::string>(), "Player not found");
}

TEST(admin_web_protocol, admin_player_connected) {
    auto msg = network::make_admin_player_connected("TestPlayer", 100, "aresden");
    EXPECT_EQ(msg.type, network::json_message_type::admin_player_connected);
    EXPECT_EQ(msg.data["name"].get<std::string>(), "TestPlayer");
    EXPECT_EQ(msg.data["level"].get<int>(), 100);
    EXPECT_EQ(msg.data["map"].get<std::string>(), "aresden");
}

TEST(admin_web_protocol, admin_player_disconnected) {
    auto msg = network::make_admin_player_disconnected("TestPlayer");
    EXPECT_EQ(msg.type, network::json_message_type::admin_player_disconnected);
    EXPECT_EQ(msg.data["name"].get<std::string>(), "TestPlayer");
}

TEST(admin_web_protocol, admin_chat_log) {
    auto msg = network::make_admin_chat_log("global", "Sender", "Hello world");
    EXPECT_EQ(msg.type, network::json_message_type::admin_chat_log);
    EXPECT_EQ(msg.data["channel"].get<std::string>(), "global");
    EXPECT_EQ(msg.data["sender"].get<std::string>(), "Sender");
    EXPECT_EQ(msg.data["content"].get<std::string>(), "Hello world");
}

// ============================================================================
// Request Data Parsing Tests
// ============================================================================

TEST(admin_web_protocol, parse_get_player_request_by_name) {
    nlohmann::json j = {{"player_name", "TestPlayer"}};
    auto result = network::admin_get_player_request_data::from_json(j);
    ASSERT_TRUE(result.is_ok());
    EXPECT_EQ(result.value().player_name, "TestPlayer");
    EXPECT_EQ(result.value().player_id, 0u);
}

TEST(admin_web_protocol, parse_get_player_request_by_id) {
    nlohmann::json j = {{"player_id", 42}};
    auto result = network::admin_get_player_request_data::from_json(j);
    ASSERT_TRUE(result.is_ok());
    EXPECT_EQ(result.value().player_id, 42u);
}

TEST(admin_web_protocol, parse_kick_player_request) {
    nlohmann::json j = {{"player_name", "BadPlayer"}, {"reason", "Cheating"}};
    auto result = network::admin_kick_player_request_data::from_json(j);
    ASSERT_TRUE(result.is_ok());
    EXPECT_EQ(result.value().player_name, "BadPlayer");
    EXPECT_EQ(result.value().reason, "Cheating");
}

TEST(admin_web_protocol, parse_ban_player_request) {
    nlohmann::json j = {
        {"player_name", "BadPlayer"},
        {"reason", "Hacking"},
        {"duration_hours", 24}
    };
    auto result = network::admin_ban_player_request_data::from_json(j);
    ASSERT_TRUE(result.is_ok());
    EXPECT_EQ(result.value().player_name, "BadPlayer");
    EXPECT_EQ(result.value().reason, "Hacking");
    EXPECT_EQ(result.value().duration_hours, 24);
}

TEST(admin_web_protocol, parse_teleport_player_request) {
    nlohmann::json j = {
        {"player_name", "Player1"},
        {"dest_map", "elvine"},
        {"dest_x", 50},
        {"dest_y", 60}
    };
    auto result = network::admin_teleport_player_request_data::from_json(j);
    ASSERT_TRUE(result.is_ok());
    auto& req = result.value();
    EXPECT_EQ(req.player_name, "Player1");
    EXPECT_EQ(req.dest_map, "elvine");
    EXPECT_EQ(req.dest_x, 50);
    EXPECT_EQ(req.dest_y, 60);
}

TEST(admin_web_protocol, parse_modify_player_request) {
    nlohmann::json j = {
        {"player_name", "Player1"},
        {"modifications", {{"hp", 500}, {"level", 50}}}
    };
    auto result = network::admin_modify_player_request_data::from_json(j);
    ASSERT_TRUE(result.is_ok());
    EXPECT_EQ(result.value().player_name, "Player1");
    EXPECT_TRUE(result.value().modifications.contains("hp"));
    EXPECT_EQ(result.value().modifications["hp"].get<int>(), 500);
}

TEST(admin_web_protocol, parse_get_map_request) {
    nlohmann::json j = {{"map_name", "aresden"}};
    auto result = network::admin_get_map_request_data::from_json(j);
    ASSERT_TRUE(result.is_ok());
    EXPECT_EQ(result.value().map_name, "aresden");
}

TEST(admin_web_protocol, parse_spawn_npc_request) {
    nlohmann::json j = {
        {"npc_name", "Slime"},
        {"map_name", "aresden"},
        {"x", 30},
        {"y", 40},
        {"count", 5}
    };
    auto result = network::admin_spawn_npc_request_data::from_json(j);
    ASSERT_TRUE(result.is_ok());
    auto& req = result.value();
    EXPECT_EQ(req.npc_name, "Slime");
    EXPECT_EQ(req.map_name, "aresden");
    EXPECT_EQ(req.x, 30);
    EXPECT_EQ(req.y, 40);
    EXPECT_EQ(req.count, 5);
}

TEST(admin_web_protocol, parse_kill_npc_request) {
    nlohmann::json j = {{"entity_id", 12345}};
    auto result = network::admin_kill_npc_request_data::from_json(j);
    ASSERT_TRUE(result.is_ok());
    EXPECT_EQ(result.value().entity_id, 12345u);
}

TEST(admin_web_protocol, parse_give_item_request) {
    nlohmann::json j = {
        {"player_name", "Player1"},
        {"item_template_id", 77},
        {"count", 10}
    };
    auto result = network::admin_give_item_request_data::from_json(j);
    ASSERT_TRUE(result.is_ok());
    EXPECT_EQ(result.value().player_name, "Player1");
    EXPECT_EQ(result.value().item_template_id, 77u);
    EXPECT_EQ(result.value().count, 10);
}

TEST(admin_web_protocol, parse_remove_item_request) {
    nlohmann::json j = {
        {"player_name", "Player1"},
        {"inventory_slot", 5},
        {"count", 3}
    };
    auto result = network::admin_remove_item_request_data::from_json(j);
    ASSERT_TRUE(result.is_ok());
    EXPECT_EQ(result.value().player_name, "Player1");
    EXPECT_EQ(result.value().inventory_slot, 5);
    EXPECT_EQ(result.value().count, 3);
}

TEST(admin_web_protocol, parse_get_guild_request) {
    nlohmann::json j = {{"guild_name", "MyGuild"}};
    auto result = network::admin_get_guild_request_data::from_json(j);
    ASSERT_TRUE(result.is_ok());
    EXPECT_EQ(result.value().guild_name, "MyGuild");
}

TEST(admin_web_protocol, parse_get_account_request) {
    nlohmann::json j = {{"username", "admin_user"}};
    auto result = network::admin_get_account_request_data::from_json(j);
    ASSERT_TRUE(result.is_ok());
    EXPECT_EQ(result.value().username, "admin_user");
}

TEST(admin_web_protocol, parse_unban_player_request) {
    nlohmann::json j = {{"player_name", "banned_user"}};
    auto result = network::admin_unban_player_request_data::from_json(j);
    ASSERT_TRUE(result.is_ok());
    EXPECT_EQ(result.value().player_name, "banned_user");
}

TEST(admin_web_protocol, parse_subscribe_map_request) {
    nlohmann::json j = {{"map_name", "aresden"}};
    auto result = network::admin_subscribe_map_request_data::from_json(j);
    ASSERT_TRUE(result.is_ok());
    EXPECT_EQ(result.value().map_name, "aresden");
}

TEST(admin_web_protocol, parse_subscribe_player_request) {
    nlohmann::json j = {{"player_name", "Target"}};
    auto result = network::admin_subscribe_player_request_data::from_json(j);
    ASSERT_TRUE(result.is_ok());
    EXPECT_EQ(result.value().player_name, "Target");
}

// ============================================================================
// Message Type String Roundtrip Tests
// ============================================================================

TEST(admin_web_protocol, message_type_to_string_coverage) {
    // Verify all admin message types have to_string mappings
    using mt = network::json_message_type;

    auto check = [](mt type) {
        auto str = network::to_string(type);
        EXPECT_FALSE(str.empty()) << "Missing to_string for type " << static_cast<int>(type);
        EXPECT_NE(str, "unknown") << "Unknown to_string for type " << static_cast<int>(type);
    };

    check(mt::enter_admin_mode_request);
    check(mt::enter_admin_mode_response);
    check(mt::admin_server_stats_request);
    check(mt::admin_server_stats_response);
    check(mt::admin_list_players_request);
    check(mt::admin_list_players_response);
    check(mt::admin_get_player_request);
    check(mt::admin_get_player_response);
    check(mt::admin_kick_player_request);
    check(mt::admin_kick_player_response);
    check(mt::admin_ban_player_request);
    check(mt::admin_ban_player_response);
    check(mt::admin_teleport_player_request);
    check(mt::admin_teleport_player_response);
    check(mt::admin_modify_player_request);
    check(mt::admin_modify_player_response);
    check(mt::admin_list_maps_request);
    check(mt::admin_list_maps_response);
    check(mt::admin_get_map_request);
    check(mt::admin_get_map_response);
    check(mt::admin_spawn_npc_request);
    check(mt::admin_spawn_npc_response);
    check(mt::admin_kill_npc_request);
    check(mt::admin_kill_npc_response);
    check(mt::admin_get_inventory_request);
    check(mt::admin_get_inventory_response);
    check(mt::admin_give_item_request);
    check(mt::admin_give_item_response);
    check(mt::admin_remove_item_request);
    check(mt::admin_remove_item_response);
    check(mt::admin_list_guilds_request);
    check(mt::admin_list_guilds_response);
    check(mt::admin_get_guild_request);
    check(mt::admin_get_guild_response);
    check(mt::admin_get_account_request);
    check(mt::admin_get_account_response);
    check(mt::admin_unban_player_request);
    check(mt::admin_unban_player_response);
    check(mt::admin_subscribe_map_request);
    check(mt::admin_subscribe_map_response);
    check(mt::admin_subscribe_player_request);
    check(mt::admin_subscribe_player_response);
    check(mt::admin_unsubscribe_request);
    check(mt::admin_unsubscribe_response);
    check(mt::admin_spectator_init);
    check(mt::admin_player_connected);
    check(mt::admin_player_disconnected);
    check(mt::admin_chat_log);
}

// ============================================================================
// WebSocket Connection State Tests
// ============================================================================

TEST(admin_web_connection, admin_subscription_defaults) {
    network::admin_subscription sub;
    EXPECT_EQ(sub.sub_mode, network::admin_subscription::mode::none);
    EXPECT_FALSE(sub.target_map.is_valid());
    EXPECT_FALSE(sub.target_player.is_valid());
}

TEST(admin_web_connection, admin_dashboard_state_exists) {
    // Verify the state enum value exists
    auto state = network::ws_connection_state::admin_dashboard;
    EXPECT_NE(static_cast<int>(state), 0);  // Not default
}

// ============================================================================
// JSON Serialization Roundtrip Tests
// ============================================================================

TEST(admin_web_protocol, response_json_roundtrip) {
    auto msg = network::make_admin_response(
        network::json_message_type::admin_list_players_response,
        99, true, {{"count", 3}, {"players", nlohmann::json::array()}});

    auto json = msg.to_json();
    EXPECT_TRUE(json.contains("type"));
    EXPECT_TRUE(json.contains("seq"));
    EXPECT_TRUE(json.contains("data"));

    auto json_str = json.dump();
    auto parse_result = network::json_message::parse(json_str);
    ASSERT_TRUE(parse_result.is_ok());
    EXPECT_EQ(parse_result.value().type, network::json_message_type::admin_list_players_response);
    EXPECT_EQ(parse_result.value().seq, 99u);
}

TEST(admin_web_protocol, push_notification_json_roundtrip) {
    auto msg = network::make_admin_player_connected("Hero", 50, "elvine");
    auto json_str = msg.to_json().dump();
    auto parse_result = network::json_message::parse(json_str);
    ASSERT_TRUE(parse_result.is_ok());
    EXPECT_EQ(parse_result.value().type, network::json_message_type::admin_player_connected);
}

TEST(admin_web_protocol, chat_log_json_roundtrip) {
    auto msg = network::make_admin_chat_log("guild", "Tester", "Hello guild");
    auto json_str = msg.to_json().dump();
    auto parse_result = network::json_message::parse(json_str);
    ASSERT_TRUE(parse_result.is_ok());
    EXPECT_EQ(parse_result.value().type, network::json_message_type::admin_chat_log);
}
