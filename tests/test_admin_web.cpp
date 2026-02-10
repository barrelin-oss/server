// test_admin_web.cpp
// Tests for admin web tool protocol messages and handler logic

#include <gtest/gtest.h>
#include <map>

#include "network/json_protocol.h"
#include "network/websocket_server.h"
#include "config/server_config.h"
#include "scheduler/scheduler.h"

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

    // New expanded admin message types
    check(mt::admin_broadcast_request);
    check(mt::admin_broadcast_response);
    check(mt::admin_mute_player_request);
    check(mt::admin_mute_player_response);
    check(mt::admin_unmute_player_request);
    check(mt::admin_unmute_player_response);
    check(mt::admin_list_item_templates_request);
    check(mt::admin_list_item_templates_response);
    check(mt::admin_get_item_template_request);
    check(mt::admin_get_item_template_response);
    check(mt::admin_list_npc_templates_request);
    check(mt::admin_list_npc_templates_response);
    check(mt::admin_get_npc_template_request);
    check(mt::admin_get_npc_template_response);
    check(mt::admin_get_war_status_request);
    check(mt::admin_get_war_status_response);
    check(mt::admin_list_parties_request);
    check(mt::admin_list_parties_response);
    check(mt::admin_search_players_request);
    check(mt::admin_search_players_response);
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

// ============================================================================
// New Admin Expanded - Request Data Parsing Tests
// ============================================================================

TEST(admin_web_protocol, parse_broadcast_request) {
    nlohmann::json j = {{"message", "Server restart in 5 minutes"}};
    auto result = network::admin_broadcast_request_data::from_json(j);
    ASSERT_TRUE(result.is_ok());
    EXPECT_EQ(result.value().message, "Server restart in 5 minutes");
}

TEST(admin_web_protocol, parse_broadcast_request_missing_message) {
    nlohmann::json j = {};
    auto result = network::admin_broadcast_request_data::from_json(j);
    EXPECT_TRUE(result.is_err());
}

TEST(admin_web_protocol, parse_mute_player_request) {
    nlohmann::json j = {{"player_name", "BadPlayer"}, {"duration_minutes", 30}};
    auto result = network::admin_mute_player_request_data::from_json(j);
    ASSERT_TRUE(result.is_ok());
    EXPECT_EQ(result.value().player_name, "BadPlayer");
    EXPECT_EQ(result.value().duration_minutes, 30);
}

TEST(admin_web_protocol, parse_mute_player_request_permanent) {
    nlohmann::json j = {{"player_name", "BadPlayer"}};
    auto result = network::admin_mute_player_request_data::from_json(j);
    ASSERT_TRUE(result.is_ok());
    EXPECT_EQ(result.value().duration_minutes, 0);  // Default = permanent
}

TEST(admin_web_protocol, parse_unmute_player_request) {
    nlohmann::json j = {{"player_name", "GoodPlayer"}};
    auto result = network::admin_unmute_player_request_data::from_json(j);
    ASSERT_TRUE(result.is_ok());
    EXPECT_EQ(result.value().player_name, "GoodPlayer");
}

TEST(admin_web_protocol, parse_unmute_player_request_missing_name) {
    nlohmann::json j = {};
    auto result = network::admin_unmute_player_request_data::from_json(j);
    EXPECT_TRUE(result.is_err());
}

TEST(admin_web_protocol, parse_get_item_template_request_by_id) {
    nlohmann::json j = {{"item_id", 42}};
    auto result = network::admin_get_item_template_request_data::from_json(j);
    ASSERT_TRUE(result.is_ok());
    EXPECT_EQ(result.value().item_id, 42u);
    EXPECT_TRUE(result.value().item_name.empty());
}

TEST(admin_web_protocol, parse_get_item_template_request_by_name) {
    nlohmann::json j = {{"item_name", "Iron Sword"}};
    auto result = network::admin_get_item_template_request_data::from_json(j);
    ASSERT_TRUE(result.is_ok());
    EXPECT_EQ(result.value().item_name, "Iron Sword");
    EXPECT_EQ(result.value().item_id, 0u);
}

TEST(admin_web_protocol, parse_get_item_template_request_missing_both) {
    nlohmann::json j = {};
    auto result = network::admin_get_item_template_request_data::from_json(j);
    EXPECT_TRUE(result.is_err());
}

TEST(admin_web_protocol, parse_get_npc_template_request_by_id) {
    nlohmann::json j = {{"npc_id", 7}};
    auto result = network::admin_get_npc_template_request_data::from_json(j);
    ASSERT_TRUE(result.is_ok());
    EXPECT_EQ(result.value().npc_id, 7u);
    EXPECT_TRUE(result.value().npc_name.empty());
}

TEST(admin_web_protocol, parse_get_npc_template_request_by_name) {
    nlohmann::json j = {{"npc_name", "Goblin"}};
    auto result = network::admin_get_npc_template_request_data::from_json(j);
    ASSERT_TRUE(result.is_ok());
    EXPECT_EQ(result.value().npc_name, "Goblin");
    EXPECT_EQ(result.value().npc_id, 0u);
}

TEST(admin_web_protocol, parse_get_npc_template_request_missing_both) {
    nlohmann::json j = {};
    auto result = network::admin_get_npc_template_request_data::from_json(j);
    EXPECT_TRUE(result.is_err());
}

TEST(admin_web_protocol, parse_search_players_request) {
    nlohmann::json j = {{"query", "zer"}};
    auto result = network::admin_search_players_request_data::from_json(j);
    ASSERT_TRUE(result.is_ok());
    EXPECT_EQ(result.value().query, "zer");
}

TEST(admin_web_protocol, parse_search_players_request_missing_query) {
    nlohmann::json j = {};
    auto result = network::admin_search_players_request_data::from_json(j);
    EXPECT_TRUE(result.is_err());
}

// ============================================================================
// New Admin Expanded - Message Type String Roundtrip Tests
// ============================================================================

TEST(admin_web_protocol, new_type_string_roundtrip) {
    using mt = network::json_message_type;
    auto check = [](mt type) {
        auto str = network::to_string(type);
        auto parsed = network::parse_message_type(str);
        EXPECT_EQ(parsed, type) << "Roundtrip failed for " << std::string(str);
    };

    check(mt::admin_broadcast_request);
    check(mt::admin_broadcast_response);
    check(mt::admin_mute_player_request);
    check(mt::admin_mute_player_response);
    check(mt::admin_unmute_player_request);
    check(mt::admin_unmute_player_response);
    check(mt::admin_list_item_templates_request);
    check(mt::admin_list_item_templates_response);
    check(mt::admin_get_item_template_request);
    check(mt::admin_get_item_template_response);
    check(mt::admin_list_npc_templates_request);
    check(mt::admin_list_npc_templates_response);
    check(mt::admin_get_npc_template_request);
    check(mt::admin_get_npc_template_response);
    check(mt::admin_get_war_status_request);
    check(mt::admin_get_war_status_response);
    check(mt::admin_list_parties_request);
    check(mt::admin_list_parties_response);
    check(mt::admin_search_players_request);
    check(mt::admin_search_players_response);
}

// ============================================================================
// New Admin Expanded - JSON Serialization Roundtrip Tests
// ============================================================================

TEST(admin_web_protocol, broadcast_response_roundtrip) {
    auto msg = network::make_admin_response(
        network::json_message_type::admin_broadcast_response,
        50, true, {{"message", "Hello"}});
    auto json_str = msg.to_json().dump();
    auto parse_result = network::json_message::parse(json_str);
    ASSERT_TRUE(parse_result.is_ok());
    EXPECT_EQ(parse_result.value().type, network::json_message_type::admin_broadcast_response);
    EXPECT_EQ(parse_result.value().seq, 50u);
}

TEST(admin_web_protocol, mute_response_roundtrip) {
    auto msg = network::make_admin_response(
        network::json_message_type::admin_mute_player_response,
        51, true, {{"player_name", "Test"}, {"duration_minutes", 30}});
    auto json_str = msg.to_json().dump();
    auto parse_result = network::json_message::parse(json_str);
    ASSERT_TRUE(parse_result.is_ok());
    EXPECT_EQ(parse_result.value().type, network::json_message_type::admin_mute_player_response);
}

TEST(admin_web_protocol, search_players_response_roundtrip) {
    nlohmann::json data = {
        {"success", true},
        {"query", "zer"},
        {"count", 1},
        {"players", nlohmann::json::array({{
            {"id", 1}, {"name", "zero"}, {"level", 100}
        }})}
    };
    auto msg = network::make_admin_response(
        network::json_message_type::admin_search_players_response,
        52, true, data);
    auto json_str = msg.to_json().dump();
    auto parse_result = network::json_message::parse(json_str);
    ASSERT_TRUE(parse_result.is_ok());
    EXPECT_EQ(parse_result.value().type, network::json_message_type::admin_search_players_response);
}

// ============================================================================
// Phase 3: Request Data Parsing Tests
// ============================================================================

TEST(admin_web_protocol, parse_audit_log_request_defaults) {
    nlohmann::json j = {};
    auto result = network::admin_get_audit_log_request_data::from_json(j);
    ASSERT_TRUE(result.is_ok());
    EXPECT_EQ(result.value().count, 100u);
    EXPECT_TRUE(result.value().executor_name.empty());
}

TEST(admin_web_protocol, parse_audit_log_request_with_filter) {
    nlohmann::json j = {{"count", 50}, {"executor_name", "admin1"}};
    auto result = network::admin_get_audit_log_request_data::from_json(j);
    ASSERT_TRUE(result.is_ok());
    EXPECT_EQ(result.value().count, 50u);
    EXPECT_EQ(result.value().executor_name, "admin1");
}

TEST(admin_web_protocol, parse_set_config_request) {
    nlohmann::json j = {{"values", {{"auto_save.interval_seconds", 600}, {"auth.allow_registration", false}}}};
    auto result = network::admin_set_config_request_data::from_json(j);
    ASSERT_TRUE(result.is_ok());
    EXPECT_TRUE(result.value().values.contains("auto_save.interval_seconds"));
    EXPECT_EQ(result.value().values["auto_save.interval_seconds"].get<int>(), 600);
}

TEST(admin_web_protocol, parse_set_config_request_missing_values) {
    nlohmann::json j = {};
    auto result = network::admin_set_config_request_data::from_json(j);
    EXPECT_TRUE(result.is_err());
}

TEST(admin_web_protocol, parse_cancel_scheduled_task_request) {
    nlohmann::json j = {{"tag", "environment_tick"}};
    auto result = network::admin_cancel_scheduled_task_request_data::from_json(j);
    ASSERT_TRUE(result.is_ok());
    EXPECT_EQ(result.value().tag, "environment_tick");
}

TEST(admin_web_protocol, parse_cancel_scheduled_task_request_missing_tag) {
    nlohmann::json j = {};
    auto result = network::admin_cancel_scheduled_task_request_data::from_json(j);
    EXPECT_TRUE(result.is_err());
}

TEST(admin_web_protocol, parse_run_query_request) {
    nlohmann::json j = {{"query_name", "top_players_by_level"}, {"params", {{"limit", 20}}}};
    auto result = network::admin_run_query_request_data::from_json(j);
    ASSERT_TRUE(result.is_ok());
    EXPECT_EQ(result.value().query_name, "top_players_by_level");
    EXPECT_EQ(result.value().params["limit"].get<int>(), 20);
}

TEST(admin_web_protocol, parse_run_query_request_no_params) {
    nlohmann::json j = {{"query_name", "faction_distribution"}};
    auto result = network::admin_run_query_request_data::from_json(j);
    ASSERT_TRUE(result.is_ok());
    EXPECT_EQ(result.value().query_name, "faction_distribution");
    EXPECT_TRUE(result.value().params.empty());
}

TEST(admin_web_protocol, parse_run_query_request_missing_name) {
    nlohmann::json j = {};
    auto result = network::admin_run_query_request_data::from_json(j);
    EXPECT_TRUE(result.is_err());
}

TEST(admin_web_protocol, parse_list_map_npcs_request) {
    nlohmann::json j = {{"map_name", "aresden"}};
    auto result = network::admin_list_map_npcs_request_data::from_json(j);
    ASSERT_TRUE(result.is_ok());
    EXPECT_EQ(result.value().map_name, "aresden");
}

TEST(admin_web_protocol, parse_list_map_ground_items_request) {
    nlohmann::json j = {{"map_name", "elvine"}};
    auto result = network::admin_list_map_ground_items_request_data::from_json(j);
    ASSERT_TRUE(result.is_ok());
    EXPECT_EQ(result.value().map_name, "elvine");
}

TEST(admin_web_protocol, parse_remove_ground_item_request) {
    nlohmann::json j = {{"map_name", "aresden"}, {"x", 50}, {"y", 60}, {"item_id", 12345}};
    auto result = network::admin_remove_ground_item_request_data::from_json(j);
    ASSERT_TRUE(result.is_ok());
    auto& req = result.value();
    EXPECT_EQ(req.map_name, "aresden");
    EXPECT_EQ(req.x, 50);
    EXPECT_EQ(req.y, 60);
    EXPECT_EQ(req.item_id, 12345u);
}

TEST(admin_web_protocol, parse_remove_ground_item_request_missing_fields) {
    nlohmann::json j = {{"map_name", "aresden"}};
    auto result = network::admin_remove_ground_item_request_data::from_json(j);
    EXPECT_TRUE(result.is_err());
}

TEST(admin_web_protocol, parse_guild_action_request_disband) {
    nlohmann::json j = {{"guild_name", "TestGuild"}, {"action", "disband"}};
    auto result = network::admin_guild_action_request_data::from_json(j);
    ASSERT_TRUE(result.is_ok());
    EXPECT_EQ(result.value().guild_name, "TestGuild");
    EXPECT_EQ(result.value().action, "disband");
}

TEST(admin_web_protocol, parse_guild_action_request_kick) {
    nlohmann::json j = {{"guild_name", "TestGuild"}, {"action", "kick"}, {"target_player", "Player1"}};
    auto result = network::admin_guild_action_request_data::from_json(j);
    ASSERT_TRUE(result.is_ok());
    EXPECT_EQ(result.value().action, "kick");
    EXPECT_EQ(result.value().target_player, "Player1");
}

TEST(admin_web_protocol, parse_guild_action_request_set_rank) {
    nlohmann::json j = {
        {"guild_name", "TestGuild"},
        {"action", "set_rank"},
        {"target_player", "Player1"},
        {"rank", "officer"}
    };
    auto result = network::admin_guild_action_request_data::from_json(j);
    ASSERT_TRUE(result.is_ok());
    EXPECT_EQ(result.value().rank, "officer");
}

TEST(admin_web_protocol, parse_guild_action_request_missing_fields) {
    nlohmann::json j = {{"guild_name", "TestGuild"}};
    auto result = network::admin_guild_action_request_data::from_json(j);
    EXPECT_TRUE(result.is_err());
}

TEST(admin_web_protocol, parse_message_player_request) {
    nlohmann::json j = {{"player_name", "zero"}, {"message", "Your account has been reviewed"}};
    auto result = network::admin_message_player_request_data::from_json(j);
    ASSERT_TRUE(result.is_ok());
    EXPECT_EQ(result.value().player_name, "zero");
    EXPECT_EQ(result.value().message, "Your account has been reviewed");
}

TEST(admin_web_protocol, parse_message_player_request_missing_fields) {
    nlohmann::json j = {{"player_name", "zero"}};
    auto result = network::admin_message_player_request_data::from_json(j);
    EXPECT_TRUE(result.is_err());
}

TEST(admin_web_protocol, parse_set_environment_request_full) {
    nlohmann::json j = {{"map_name", "aresden"}, {"weather", 3}, {"hour", 14}, {"minute", 30}};
    auto result = network::admin_set_environment_request_data::from_json(j);
    ASSERT_TRUE(result.is_ok());
    auto& req = result.value();
    EXPECT_EQ(req.map_name, "aresden");
    EXPECT_EQ(*req.weather, 3);
    EXPECT_EQ(*req.hour, 14);
    EXPECT_EQ(*req.minute, 30);
}

TEST(admin_web_protocol, parse_set_environment_request_time_only) {
    nlohmann::json j = {{"hour", 6}};
    auto result = network::admin_set_environment_request_data::from_json(j);
    ASSERT_TRUE(result.is_ok());
    auto& req = result.value();
    EXPECT_TRUE(req.map_name.empty());
    EXPECT_FALSE(req.weather.has_value());
    EXPECT_EQ(*req.hour, 6);
    EXPECT_FALSE(req.minute.has_value());
}

TEST(admin_web_protocol, parse_set_environment_request_empty) {
    nlohmann::json j = {};
    auto result = network::admin_set_environment_request_data::from_json(j);
    ASSERT_TRUE(result.is_ok());  // All fields optional
}

TEST(admin_web_protocol, parse_shutdown_server_request_immediate) {
    nlohmann::json j = {{"countdown_seconds", 0}, {"reason", "Maintenance"}};
    auto result = network::admin_shutdown_server_request_data::from_json(j);
    ASSERT_TRUE(result.is_ok());
    EXPECT_EQ(result.value().countdown_seconds, 0);
    EXPECT_EQ(result.value().reason, "Maintenance");
    EXPECT_FALSE(result.value().cancel);
}

TEST(admin_web_protocol, parse_shutdown_server_request_countdown) {
    nlohmann::json j = {{"countdown_seconds", 300}, {"reason", "Update"}};
    auto result = network::admin_shutdown_server_request_data::from_json(j);
    ASSERT_TRUE(result.is_ok());
    EXPECT_EQ(result.value().countdown_seconds, 300);
}

TEST(admin_web_protocol, parse_shutdown_server_request_cancel) {
    nlohmann::json j = {{"cancel", true}};
    auto result = network::admin_shutdown_server_request_data::from_json(j);
    ASSERT_TRUE(result.is_ok());
    EXPECT_TRUE(result.value().cancel);
}

TEST(admin_web_protocol, parse_shutdown_server_request_defaults) {
    nlohmann::json j = {};
    auto result = network::admin_shutdown_server_request_data::from_json(j);
    ASSERT_TRUE(result.is_ok());
    EXPECT_EQ(result.value().countdown_seconds, 0);
    EXPECT_TRUE(result.value().reason.empty());
    EXPECT_FALSE(result.value().cancel);
}

// ============================================================================
// Phase 3: Message Type String Roundtrip Tests
// ============================================================================

TEST(admin_web_protocol, phase3_type_string_roundtrip) {
    using mt = network::json_message_type;
    auto check = [](mt type) {
        auto str = network::to_string(type);
        auto parsed = network::parse_message_type(str);
        EXPECT_EQ(parsed, type) << "Roundtrip failed for " << std::string(str);
    };

    check(mt::admin_get_audit_log_request);
    check(mt::admin_get_audit_log_response);
    check(mt::admin_get_config_request);
    check(mt::admin_get_config_response);
    check(mt::admin_set_config_request);
    check(mt::admin_set_config_response);
    check(mt::admin_reload_config_request);
    check(mt::admin_reload_config_response);
    check(mt::admin_list_scheduled_tasks_request);
    check(mt::admin_list_scheduled_tasks_response);
    check(mt::admin_cancel_scheduled_task_request);
    check(mt::admin_cancel_scheduled_task_response);
    check(mt::admin_start_task_request);
    check(mt::admin_start_task_response);
    check(mt::admin_run_query_request);
    check(mt::admin_run_query_response);
    check(mt::admin_list_map_npcs_request);
    check(mt::admin_list_map_npcs_response);
    check(mt::admin_list_map_ground_items_request);
    check(mt::admin_list_map_ground_items_response);
    check(mt::admin_remove_ground_item_request);
    check(mt::admin_remove_ground_item_response);
    check(mt::admin_guild_action_request);
    check(mt::admin_guild_action_response);
    check(mt::admin_message_player_request);
    check(mt::admin_message_player_response);
    check(mt::admin_set_environment_request);
    check(mt::admin_set_environment_response);
    check(mt::admin_shutdown_server_request);
    check(mt::admin_shutdown_server_response);
}

// ============================================================================
// Phase 3: to_string coverage
// ============================================================================

TEST(admin_web_protocol, phase3_message_type_to_string_coverage) {
    using mt = network::json_message_type;
    auto check = [](mt type) {
        auto str = network::to_string(type);
        EXPECT_FALSE(str.empty()) << "Missing to_string for type " << static_cast<int>(type);
        EXPECT_NE(str, "unknown") << "Unknown to_string for type " << static_cast<int>(type);
    };

    check(mt::admin_get_audit_log_request);
    check(mt::admin_get_audit_log_response);
    check(mt::admin_get_config_request);
    check(mt::admin_get_config_response);
    check(mt::admin_set_config_request);
    check(mt::admin_set_config_response);
    check(mt::admin_reload_config_request);
    check(mt::admin_reload_config_response);
    check(mt::admin_list_scheduled_tasks_request);
    check(mt::admin_list_scheduled_tasks_response);
    check(mt::admin_cancel_scheduled_task_request);
    check(mt::admin_cancel_scheduled_task_response);
    check(mt::admin_start_task_request);
    check(mt::admin_start_task_response);
    check(mt::admin_run_query_request);
    check(mt::admin_run_query_response);
    check(mt::admin_list_map_npcs_request);
    check(mt::admin_list_map_npcs_response);
    check(mt::admin_list_map_ground_items_request);
    check(mt::admin_list_map_ground_items_response);
    check(mt::admin_remove_ground_item_request);
    check(mt::admin_remove_ground_item_response);
    check(mt::admin_guild_action_request);
    check(mt::admin_guild_action_response);
    check(mt::admin_message_player_request);
    check(mt::admin_message_player_response);
    check(mt::admin_set_environment_request);
    check(mt::admin_set_environment_response);
    check(mt::admin_shutdown_server_request);
    check(mt::admin_shutdown_server_response);
}

// ============================================================================
// Config Serialization Tests
// ============================================================================

TEST(admin_web_config, to_json_contains_all_sections) {
    server_config cfg;
    auto j = cfg.to_json();
    EXPECT_TRUE(j.contains("server_name"));
    EXPECT_TRUE(j.contains("database"));
    EXPECT_TRUE(j.contains("websocket"));
    EXPECT_TRUE(j.contains("auth"));
    EXPECT_TRUE(j.contains("forum_auth"));
    EXPECT_TRUE(j.contains("auto_save"));
    EXPECT_TRUE(j.contains("logging"));
    EXPECT_TRUE(j.contains("self_contained"));
}

TEST(admin_web_config, to_json_sanitized_hides_secrets) {
    server_config cfg;
    cfg.database.password = "super_secret";
    cfg.forum_auth.api_key = "my_api_key";
    auto j = cfg.to_json_sanitized();
    EXPECT_EQ(j["database"]["password"].get<std::string>(), "***");
    EXPECT_EQ(j["forum_auth"]["api_key"].get<std::string>(), "***");
}

TEST(admin_web_config, to_json_sanitized_preserves_non_secrets) {
    server_config cfg;
    cfg.database.host = "myhost";
    cfg.websocket.port = 9999;
    auto j = cfg.to_json_sanitized();
    EXPECT_EQ(j["database"]["host"].get<std::string>(), "myhost");
    EXPECT_EQ(j["websocket"]["port"].get<int>(), 9999);
}

TEST(admin_web_config, apply_dot_values_basic) {
    server_config cfg;
    nlohmann::json values = {
        {"database.host", "newhost"},
        {"websocket.port", 3000},
        {"auth.allow_registration", false}
    };
    auto result = cfg.apply_dot_values(values);
    ASSERT_TRUE(result.is_ok());
    auto& applied = result.value();
    EXPECT_EQ(applied.size(), 3u);
    EXPECT_EQ(cfg.database.host, "newhost");
    EXPECT_EQ(cfg.websocket.port, 3000);
    EXPECT_FALSE(cfg.auth.allow_registration);
}

TEST(admin_web_config, apply_dot_values_skips_sentinel) {
    server_config cfg;
    cfg.database.password = "original";
    nlohmann::json values = {
        {"database.password", "***"},
        {"database.host", "updated"}
    };
    auto result = cfg.apply_dot_values(values);
    ASSERT_TRUE(result.is_ok());
    EXPECT_EQ(result.value().size(), 1u);  // Only host applied
    EXPECT_EQ(cfg.database.password, "original");  // Not overwritten
    EXPECT_EQ(cfg.database.host, "updated");
}

TEST(admin_web_config, apply_dot_values_unknown_key) {
    server_config cfg;
    nlohmann::json values = {{"nonexistent.field", 42}};
    auto result = cfg.apply_dot_values(values);
    ASSERT_TRUE(result.is_ok());
    EXPECT_TRUE(result.value().empty());
}

TEST(admin_web_config, requires_restart_database) {
    EXPECT_TRUE(server_config::requires_restart("database.host"));
    EXPECT_TRUE(server_config::requires_restart("database.port"));
    EXPECT_TRUE(server_config::requires_restart("database.password"));
}

TEST(admin_web_config, requires_restart_websocket) {
    EXPECT_TRUE(server_config::requires_restart("websocket.port"));
    EXPECT_TRUE(server_config::requires_restart("websocket.bind_address"));
}

TEST(admin_web_config, requires_restart_legacy) {
    EXPECT_TRUE(server_config::requires_restart("enable_legacy_protocol"));
    EXPECT_TRUE(server_config::requires_restart("legacy_port"));
}

TEST(admin_web_config, does_not_require_restart) {
    EXPECT_FALSE(server_config::requires_restart("auto_save.interval_seconds"));
    EXPECT_FALSE(server_config::requires_restart("logging.console_level"));
    EXPECT_FALSE(server_config::requires_restart("auth.allow_registration"));
}

// ============================================================================
// Phase 4 Protocol Tests
// ============================================================================

TEST(admin_web_phase4, modify_skills_type_map) {
    auto type = network::parse_message_type("admin_modify_skills_request");
    EXPECT_EQ(type, network::json_message_type::admin_modify_skills_request);
    type = network::parse_message_type("admin_modify_skills_response");
    EXPECT_EQ(type, network::json_message_type::admin_modify_skills_response);
}

TEST(admin_web_phase4, modify_spells_type_map) {
    auto type = network::parse_message_type("admin_modify_spells_request");
    EXPECT_EQ(type, network::json_message_type::admin_modify_spells_request);
    type = network::parse_message_type("admin_modify_spells_response");
    EXPECT_EQ(type, network::json_message_type::admin_modify_spells_response);
}

TEST(admin_web_phase4, quest_types_map) {
    EXPECT_EQ(network::parse_message_type("admin_get_player_quests_request"),
        network::json_message_type::admin_get_player_quests_request);
    EXPECT_EQ(network::parse_message_type("admin_quest_action_request"),
        network::json_message_type::admin_quest_action_request);
}

TEST(admin_web_phase4, effect_and_account_types_map) {
    EXPECT_EQ(network::parse_message_type("admin_remove_effects_request"),
        network::json_message_type::admin_remove_effects_request);
    EXPECT_EQ(network::parse_message_type("admin_create_account_request"),
        network::json_message_type::admin_create_account_request);
    EXPECT_EQ(network::parse_message_type("admin_change_password_request"),
        network::json_message_type::admin_change_password_request);
    EXPECT_EQ(network::parse_message_type("admin_set_admin_level_request"),
        network::json_message_type::admin_set_admin_level_request);
}

TEST(admin_web_phase4, spawn_and_spell_template_types_map) {
    EXPECT_EQ(network::parse_message_type("admin_list_spawn_points_request"),
        network::json_message_type::admin_list_spawn_points_request);
    EXPECT_EQ(network::parse_message_type("admin_list_spell_templates_request"),
        network::json_message_type::admin_list_spell_templates_request);
    EXPECT_EQ(network::parse_message_type("admin_get_spell_template_request"),
        network::json_message_type::admin_get_spell_template_request);
}

TEST(admin_web_phase4, maintenance_and_char_types_map) {
    EXPECT_EQ(network::parse_message_type("admin_set_maintenance_mode_request"),
        network::json_message_type::admin_set_maintenance_mode_request);
    EXPECT_EQ(network::parse_message_type("admin_create_character_request_admin"),
        network::json_message_type::admin_create_character_request_admin);
    EXPECT_EQ(network::parse_message_type("admin_delete_character_request_admin"),
        network::json_message_type::admin_delete_character_request_admin);
}

TEST(admin_web_phase4, ip_bans_type_map) {
    EXPECT_EQ(network::parse_message_type("admin_manage_ip_bans_request"),
        network::json_message_type::admin_manage_ip_bans_request);
    EXPECT_EQ(network::parse_message_type("admin_manage_ip_bans_response"),
        network::json_message_type::admin_manage_ip_bans_response);
}

TEST(admin_web_phase4, to_string_roundtrip) {
    // Verify to_string works for all phase 4 types
    auto check = [](network::json_message_type t) {
        auto str = network::to_string(t);
        EXPECT_NE(str, "unknown") << "to_string failed for enum value " << static_cast<int>(t);
        auto parsed = network::parse_message_type(str);
        EXPECT_EQ(parsed, t) << "Roundtrip failed for " << str;
    };

    check(network::json_message_type::admin_modify_skills_request);
    check(network::json_message_type::admin_modify_skills_response);
    check(network::json_message_type::admin_modify_spells_request);
    check(network::json_message_type::admin_modify_spells_response);
    check(network::json_message_type::admin_get_player_quests_request);
    check(network::json_message_type::admin_get_player_quests_response);
    check(network::json_message_type::admin_quest_action_request);
    check(network::json_message_type::admin_quest_action_response);
    check(network::json_message_type::admin_remove_effects_request);
    check(network::json_message_type::admin_remove_effects_response);
    check(network::json_message_type::admin_create_account_request);
    check(network::json_message_type::admin_create_account_response);
    check(network::json_message_type::admin_change_password_request);
    check(network::json_message_type::admin_change_password_response);
    check(network::json_message_type::admin_set_admin_level_request);
    check(network::json_message_type::admin_set_admin_level_response);
    check(network::json_message_type::admin_list_spawn_points_request);
    check(network::json_message_type::admin_list_spawn_points_response);
    check(network::json_message_type::admin_list_spell_templates_request);
    check(network::json_message_type::admin_list_spell_templates_response);
    check(network::json_message_type::admin_get_spell_template_request);
    check(network::json_message_type::admin_get_spell_template_response);
    check(network::json_message_type::admin_set_maintenance_mode_request);
    check(network::json_message_type::admin_set_maintenance_mode_response);
    check(network::json_message_type::admin_create_character_request_admin);
    check(network::json_message_type::admin_create_character_response_admin);
    check(network::json_message_type::admin_delete_character_request_admin);
    check(network::json_message_type::admin_delete_character_response_admin);
    check(network::json_message_type::admin_manage_ip_bans_request);
    check(network::json_message_type::admin_manage_ip_bans_response);
}

// ============================================================================
// Phase 4 from_json Parsing Tests
// ============================================================================

TEST(admin_web_phase4, modify_skills_from_json) {
    nlohmann::json j = {
        {"player_name", "zero"},
        {"action", "set"},
        {"skill_type", 5},
        {"value", 100}
    };
    auto r = network::admin_modify_skills_request_data::from_json(j);
    ASSERT_TRUE(r.is_ok());
    auto& d = r.value();
    EXPECT_EQ(d.player_name, "zero");
    EXPECT_EQ(d.action, "set");
    EXPECT_EQ(d.skill_type, 5);
    EXPECT_EQ(d.value, 100);
}

TEST(admin_web_phase4, modify_spells_from_json) {
    nlohmann::json j = {
        {"player_name", "zero"},
        {"action", "learn"},
        {"spell_id", 10}
    };
    auto r = network::admin_modify_spells_request_data::from_json(j);
    ASSERT_TRUE(r.is_ok());
    EXPECT_EQ(r.value().action, "learn");
    EXPECT_EQ(r.value().spell_id, 10u);
}

TEST(admin_web_phase4, quest_action_from_json) {
    nlohmann::json j = {
        {"player_name", "zero"},
        {"action", "complete"},
        {"quest_id", 5}
    };
    auto r = network::admin_quest_action_request_data::from_json(j);
    ASSERT_TRUE(r.is_ok());
    EXPECT_EQ(r.value().action, "complete");
    EXPECT_EQ(r.value().quest_id, 5u);
}

TEST(admin_web_phase4, remove_effects_from_json) {
    nlohmann::json j = {
        {"player_name", "zero"},
        {"mode", "all"}
    };
    auto r = network::admin_remove_effects_request_data::from_json(j);
    ASSERT_TRUE(r.is_ok());
    EXPECT_EQ(r.value().mode, "all");
    EXPECT_EQ(r.value().group, 0u);
    EXPECT_EQ(r.value().effect_id, 0u);
}

TEST(admin_web_phase4, remove_effects_from_json_group) {
    nlohmann::json j = {
        {"player_name", "zero"},
        {"mode", "group"},
        {"group", 12}
    };
    auto r = network::admin_remove_effects_request_data::from_json(j);
    ASSERT_TRUE(r.is_ok());
    EXPECT_EQ(r.value().group, 12u);
}

TEST(admin_web_phase4, create_account_from_json) {
    nlohmann::json j = {
        {"username", "newplayer"},
        {"password", "pass123"}
    };
    auto r = network::admin_create_account_request_data::from_json(j);
    ASSERT_TRUE(r.is_ok());
    EXPECT_EQ(r.value().username, "newplayer");
    EXPECT_EQ(r.value().password, "pass123");
    EXPECT_EQ(r.value().admin_level, 0);
}

TEST(admin_web_phase4, create_account_with_admin_level) {
    nlohmann::json j = {
        {"username", "admin"},
        {"password", "pass"},
        {"admin_level", 20}
    };
    auto r = network::admin_create_account_request_data::from_json(j);
    ASSERT_TRUE(r.is_ok());
    EXPECT_EQ(r.value().admin_level, 20);
}

TEST(admin_web_phase4, change_password_from_json) {
    nlohmann::json j = {
        {"username", "player1"},
        {"new_password", "newpass"}
    };
    auto r = network::admin_change_password_request_data::from_json(j);
    ASSERT_TRUE(r.is_ok());
    EXPECT_EQ(r.value().username, "player1");
    EXPECT_EQ(r.value().new_password, "newpass");
}

TEST(admin_web_phase4, set_admin_level_from_json) {
    nlohmann::json j = {
        {"username", "player1"},
        {"admin_level", 10}
    };
    auto r = network::admin_set_admin_level_request_data::from_json(j);
    ASSERT_TRUE(r.is_ok());
    EXPECT_EQ(r.value().admin_level, 10);
}

TEST(admin_web_phase4, list_spawn_points_from_json) {
    nlohmann::json j = {{"map_name", "aresden"}};
    auto r = network::admin_list_spawn_points_request_data::from_json(j);
    ASSERT_TRUE(r.is_ok());
    EXPECT_EQ(r.value().map_name, "aresden");
}

TEST(admin_web_phase4, list_spawn_points_empty_map) {
    nlohmann::json j = nlohmann::json::object();
    auto r = network::admin_list_spawn_points_request_data::from_json(j);
    ASSERT_TRUE(r.is_ok());
    EXPECT_TRUE(r.value().map_name.empty());
}

TEST(admin_web_phase4, get_spell_template_by_id) {
    nlohmann::json j = {{"spell_id", 10}};
    auto r = network::admin_get_spell_template_request_data::from_json(j);
    ASSERT_TRUE(r.is_ok());
    EXPECT_EQ(r.value().spell_id, 10u);
    EXPECT_TRUE(r.value().spell_name.empty());
}

TEST(admin_web_phase4, get_spell_template_by_name) {
    nlohmann::json j = {{"spell_name", "Energy Bolt"}};
    auto r = network::admin_get_spell_template_request_data::from_json(j);
    ASSERT_TRUE(r.is_ok());
    EXPECT_EQ(r.value().spell_name, "Energy Bolt");
    EXPECT_EQ(r.value().spell_id, 0u);
}

TEST(admin_web_phase4, set_maintenance_mode_from_json) {
    nlohmann::json j = {
        {"enabled", true},
        {"message", "Server maintenance"}
    };
    auto r = network::admin_set_maintenance_mode_request_data::from_json(j);
    ASSERT_TRUE(r.is_ok());
    EXPECT_TRUE(r.value().enabled);
    EXPECT_EQ(r.value().message, "Server maintenance");
}

TEST(admin_web_phase4, create_character_admin_from_json) {
    nlohmann::json j = {
        {"username", "player1"},
        {"name", "NewChar"},
        {"gender", 1},
        {"hair_style", 2},
        {"hair_color", 3},
        {"skin_color", 1},
        {"underwear_color", 0}
    };
    auto r = network::admin_create_character_request_admin_data::from_json(j);
    ASSERT_TRUE(r.is_ok());
    auto& d = r.value();
    EXPECT_EQ(d.username, "player1");
    EXPECT_EQ(d.name, "NewChar");
    EXPECT_EQ(d.gender, 1);
    EXPECT_EQ(d.hair_style, 2);
    EXPECT_EQ(d.hair_color, 3);
}

TEST(admin_web_phase4, delete_character_admin_from_json) {
    nlohmann::json j = {
        {"username", "player1"},
        {"character_name", "OldChar"}
    };
    auto r = network::admin_delete_character_request_admin_data::from_json(j);
    ASSERT_TRUE(r.is_ok());
    EXPECT_EQ(r.value().username, "player1");
    EXPECT_EQ(r.value().character_name, "OldChar");
}

TEST(admin_web_phase4, manage_ip_bans_list) {
    nlohmann::json j = {{"action", "list"}};
    auto r = network::admin_manage_ip_bans_request_data::from_json(j);
    ASSERT_TRUE(r.is_ok());
    EXPECT_EQ(r.value().action, "list");
    EXPECT_TRUE(r.value().ip.empty());
}

TEST(admin_web_phase4, manage_ip_bans_add) {
    nlohmann::json j = {
        {"action", "add"},
        {"ip", "1.2.3.4"},
        {"reason", "spam"}
    };
    auto r = network::admin_manage_ip_bans_request_data::from_json(j);
    ASSERT_TRUE(r.is_ok());
    EXPECT_EQ(r.value().action, "add");
    EXPECT_EQ(r.value().ip, "1.2.3.4");
    EXPECT_EQ(r.value().reason, "spam");
}

TEST(admin_web_phase4, search_players_enhanced_filters) {
    nlohmann::json j = {
        {"query", ""},
        {"level_min", 10},
        {"level_max", 50},
        {"map_name", "aresden"},
        {"faction", 1},
        {"guild_name", "TestGuild"}
    };
    auto r = network::admin_search_players_request_data::from_json(j);
    ASSERT_TRUE(r.is_ok());
    auto& d = r.value();
    ASSERT_TRUE(d.level_min.has_value());
    EXPECT_EQ(*d.level_min, 10);
    ASSERT_TRUE(d.level_max.has_value());
    EXPECT_EQ(*d.level_max, 50);
    EXPECT_EQ(d.map_name, "aresden");
    ASSERT_TRUE(d.faction.has_value());
    EXPECT_EQ(*d.faction, 1);
    EXPECT_EQ(d.guild_name, "TestGuild");
}

TEST(admin_web_phase4, search_players_filters_defaults) {
    nlohmann::json j = {{"query", "test"}};
    auto r = network::admin_search_players_request_data::from_json(j);
    ASSERT_TRUE(r.is_ok());
    auto& d = r.value();
    EXPECT_FALSE(d.level_min.has_value());
    EXPECT_FALSE(d.level_max.has_value());
    EXPECT_TRUE(d.map_name.empty());
    EXPECT_FALSE(d.faction.has_value());
    EXPECT_TRUE(d.guild_name.empty());
}

// ============================================================================
// Task Registry Tests
// ============================================================================

TEST(admin_web_task_registry, start_task_type_map) {
    EXPECT_EQ(network::parse_message_type("admin_start_task_request"),
        network::json_message_type::admin_start_task_request);
    EXPECT_EQ(network::parse_message_type("admin_start_task_response"),
        network::json_message_type::admin_start_task_response);
}

TEST(admin_web_task_registry, start_task_to_string_roundtrip) {
    auto check = [](network::json_message_type t) {
        auto s = std::string(network::to_string(t));
        EXPECT_EQ(network::parse_message_type(s), t) << "Failed for " << s;
    };
    check(network::json_message_type::admin_start_task_request);
    check(network::json_message_type::admin_start_task_response);
}

TEST(admin_web_task_registry, parse_start_task_request) {
    nlohmann::json j = {{"tag", "auto_save"}};
    auto r = network::admin_start_task_request_data::from_json(j);
    ASSERT_TRUE(r.is_ok());
    EXPECT_EQ(r.value().tag, "auto_save");
    EXPECT_FALSE(r.value().interval_ms.has_value());
}

TEST(admin_web_task_registry, parse_start_task_request_with_interval) {
    nlohmann::json j = {{"tag", "environment_tick"}, {"interval_ms", 5000}};
    auto r = network::admin_start_task_request_data::from_json(j);
    ASSERT_TRUE(r.is_ok());
    EXPECT_EQ(r.value().tag, "environment_tick");
    ASSERT_TRUE(r.value().interval_ms.has_value());
    EXPECT_EQ(r.value().interval_ms.value(), 5000);
}

TEST(admin_web_task_registry, parse_start_task_request_missing_tag) {
    nlohmann::json j = {};
    auto r = network::admin_start_task_request_data::from_json(j);
    EXPECT_TRUE(r.is_err());
}

TEST(admin_web_task_registry, scheduler_register_and_query) {
    hb::scheduler sched;
    sched.initialize();

    bool factory_called = false;
    sched.register_task("test_task", "A test task",
        hb::duration_ms{1000}, true,
        [&]() -> hb::task_callback {
            factory_called = true;
            return []() {};
        });

    EXPECT_FALSE(sched.is_task_running("test_task"));
    EXPECT_FALSE(sched.is_task_running("nonexistent"));

    int def_count = 0;
    sched.for_each_definition([&](const hb::scheduler::task_definition& def, bool running) {
        EXPECT_EQ(def.tag, "test_task");
        EXPECT_EQ(def.description, "A test task");
        EXPECT_EQ(def.default_interval_ms, 1000);
        EXPECT_TRUE(def.repeating);
        EXPECT_FALSE(running);
        ++def_count;
    });
    EXPECT_EQ(def_count, 1);

    sched.shutdown();
}

TEST(admin_web_task_registry, scheduler_start_task) {
    hb::scheduler sched;
    sched.initialize();

    sched.register_task("my_task", "desc",
        hb::duration_ms{500}, true,
        []() -> hb::task_callback {
            return []() {};
        });

    auto id = sched.start_task("my_task");
    EXPECT_TRUE(id.is_valid());
    EXPECT_TRUE(sched.is_task_running("my_task"));

    sched.shutdown();
}

TEST(admin_web_task_registry, scheduler_start_unknown_tag) {
    hb::scheduler sched;
    sched.initialize();

    auto id = sched.start_task("no_such_task");
    EXPECT_FALSE(id.is_valid());

    sched.shutdown();
}

TEST(admin_web_task_registry, scheduler_cancel_and_restart) {
    hb::scheduler sched;
    sched.initialize();

    int call_count = 0;
    sched.register_task("restartable", "A restartable task",
        hb::duration_ms{100}, true,
        [&]() -> hb::task_callback {
            return [&]() { ++call_count; };
        });

    sched.start_task("restartable");
    EXPECT_TRUE(sched.is_task_running("restartable"));

    sched.cancel_tagged("restartable");
    EXPECT_FALSE(sched.is_task_running("restartable"));

    // Restart it
    auto id2 = sched.start_task("restartable");
    EXPECT_TRUE(id2.is_valid());
    EXPECT_TRUE(sched.is_task_running("restartable"));

    sched.shutdown();
}

TEST(admin_web_task_registry, scheduler_start_with_override_interval) {
    hb::scheduler sched;
    sched.initialize();

    sched.register_task("custom_interval", "desc",
        hb::duration_ms{1000}, true,
        []() -> hb::task_callback {
            return []() {};
        });

    auto id = sched.start_task("custom_interval", hb::duration_ms{5000});
    EXPECT_TRUE(id.is_valid());
    EXPECT_TRUE(sched.is_task_running("custom_interval"));

    // Verify the task has the overridden interval
    bool found = false;
    sched.for_each_task([&](const hb::scheduler::task_info& info) {
        if (info.tag == "custom_interval") {
            EXPECT_EQ(info.interval_ms, 5000);
            found = true;
        }
    });
    EXPECT_TRUE(found);

    sched.shutdown();
}

TEST(admin_web_task_registry, for_each_definition_shows_running_status) {
    hb::scheduler sched;
    sched.initialize();

    sched.register_task("task_a", "desc a", hb::duration_ms{100}, true,
        []() -> hb::task_callback { return []() {}; });
    sched.register_task("task_b", "desc b", hb::duration_ms{200}, true,
        []() -> hb::task_callback { return []() {}; });

    sched.start_task("task_a");
    // task_b not started

    std::map<std::string, bool> status;
    sched.for_each_definition([&](const hb::scheduler::task_definition& def, bool running) {
        status[def.tag] = running;
    });

    EXPECT_TRUE(status["task_a"]);
    EXPECT_FALSE(status["task_b"]);

    sched.shutdown();
}
