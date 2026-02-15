// combat_mode_test.cpp
// Tests for combat mode toggle and broadcast

#include <gtest/gtest.h>
#include "core/types.h"
#include "network/json_protocol.h"
#include "player/player.h"

namespace net = hb::network;

// ========== Protocol Serialization Tests ==========

TEST(combat_mode_test, response_contains_combat_mode_true)
{
    auto msg = net::make_combat_mode_change_response(42, true);
    EXPECT_EQ(msg.type, net::json_message_type::combat_mode_change_response);
    EXPECT_EQ(msg.seq, 42u);
    EXPECT_TRUE(msg.data["combat_mode"].get<bool>());
}

TEST(combat_mode_test, response_contains_combat_mode_false)
{
    auto msg = net::make_combat_mode_change_response(7, false);
    EXPECT_EQ(msg.type, net::json_message_type::combat_mode_change_response);
    EXPECT_EQ(msg.seq, 7u);
    EXPECT_FALSE(msg.data["combat_mode"].get<bool>());
}

TEST(combat_mode_test, broadcast_data_to_json)
{
    net::combat_mode_change_broadcast_data data{.entity_id = 1001, .combat_mode = true};
    auto j = data.to_json();
    EXPECT_EQ(j["entity_id"], 1001u);
    EXPECT_TRUE(j["combat_mode"].get<bool>());
}

TEST(combat_mode_test, broadcast_data_to_json_peace)
{
    net::combat_mode_change_broadcast_data data{.entity_id = 2002, .combat_mode = false};
    auto j = data.to_json();
    EXPECT_EQ(j["entity_id"], 2002u);
    EXPECT_FALSE(j["combat_mode"].get<bool>());
}

TEST(combat_mode_test, make_broadcast_message)
{
    net::combat_mode_change_broadcast_data data{.entity_id = 500, .combat_mode = true};
    auto msg = net::make_combat_mode_change_broadcast(data);
    EXPECT_EQ(msg.type, net::json_message_type::combat_mode_change_broadcast);
    EXPECT_EQ(msg.seq, 0u);
    EXPECT_EQ(msg.data["entity_id"], 500u);
    EXPECT_TRUE(msg.data["combat_mode"].get<bool>());
}

// ========== Message Type String Tests ==========

TEST(combat_mode_test, message_type_to_string)
{
    EXPECT_EQ(net::to_string(net::json_message_type::combat_mode_change_request), "combat_mode_change_request");
    EXPECT_EQ(net::to_string(net::json_message_type::combat_mode_change_response), "combat_mode_change_response");
    EXPECT_EQ(net::to_string(net::json_message_type::combat_mode_change_broadcast), "combat_mode_change_broadcast");
}

TEST(combat_mode_test, parse_message_type)
{
    EXPECT_EQ(net::parse_message_type("combat_mode_change_request"),
              net::json_message_type::combat_mode_change_request);
    EXPECT_EQ(net::parse_message_type("combat_mode_change_response"),
              net::json_message_type::combat_mode_change_response);
    EXPECT_EQ(net::parse_message_type("combat_mode_change_broadcast"),
              net::json_message_type::combat_mode_change_broadcast);
}

// ========== Player State Tests ==========

TEST(combat_mode_test, player_defaults_to_peace_mode)
{
    hb::player::player p{};
    EXPECT_FALSE(p.combat_mode);
}

TEST(combat_mode_test, player_combat_mode_toggle)
{
    hb::player::player p{};
    EXPECT_FALSE(p.combat_mode);

    p.combat_mode = !p.combat_mode;
    EXPECT_TRUE(p.combat_mode);

    p.combat_mode = !p.combat_mode;
    EXPECT_FALSE(p.combat_mode);
}

// ========== Visible Entity Msg Tests ==========

TEST(combat_mode_test, visible_entity_includes_combat_mode_true)
{
    net::visible_entity_msg entity{.entity_id = 100,
                                   .type = "player",
                                   .name = "TestPlayer",
                                   .x = 10,
                                   .y = 20,
                                   .hp_percent = 100,
                                   .direction = 0,
                                   .faction = "aresden",
                                   .hostility = "friendly",
                                   .pk_status = "innocent",
                                   .guild_name = {},
                                   .guild_tag = {},
                                   .combat_mode = true};

    auto j = entity.to_json();
    EXPECT_TRUE(j.contains("combat_mode"));
    EXPECT_TRUE(j["combat_mode"].get<bool>());
}

TEST(combat_mode_test, visible_entity_includes_combat_mode_false)
{
    net::visible_entity_msg entity{.entity_id = 101,
                                   .type = "player",
                                   .name = "PeacePlayer",
                                   .x = 10,
                                   .y = 20,
                                   .hp_percent = 100,
                                   .direction = 0,
                                   .faction = "elvine",
                                   .hostility = "neutral",
                                   .pk_status = "innocent",
                                   .guild_name = {},
                                   .guild_tag = {},
                                   .combat_mode = false};

    auto j = entity.to_json();
    EXPECT_TRUE(j.contains("combat_mode"));
    EXPECT_FALSE(j["combat_mode"].get<bool>());
}

TEST(combat_mode_test, visible_entity_npc_has_no_combat_mode)
{
    net::visible_entity_msg entity{.entity_id = 5000,
                                   .type = "npc",
                                   .name = "Slime",
                                   .x = 50,
                                   .y = 60,
                                   .hp_percent = 100,
                                   .direction = 2,
                                   .faction = {},
                                   .hostility = "hostile",
                                   .pk_status = {},
                                   .guild_name = {},
                                   .guild_tag = {},
                                   .combat_mode = false,
                                   .template_id = 10,
                                   .sprite_id = 10,
                                   .level = 5,
                                   .category = "monster"};

    auto j = entity.to_json();
    EXPECT_FALSE(j.contains("combat_mode"));
}

// ========== Player Action Broadcast Tests ==========

TEST(player_action_broadcast_test, attack_action_to_json)
{
    net::player_action_broadcast_data data{.entity_id = 100, .action = "attack", .direction = 3, .target_id = 200};
    auto j = data.to_json();
    EXPECT_EQ(j["entity_id"], 100u);
    EXPECT_EQ(j["action"], "attack");
    EXPECT_EQ(j["direction"], 3);
    EXPECT_EQ(j["target_id"], 200u);
    EXPECT_FALSE(j.contains("spell_id"));
}

TEST(player_action_broadcast_test, dash_attack_action_to_json)
{
    net::player_action_broadcast_data data{.entity_id = 101, .action = "dash_attack", .direction = 5, .target_id = 300};
    auto j = data.to_json();
    EXPECT_EQ(j["entity_id"], 101u);
    EXPECT_EQ(j["action"], "dash_attack");
    EXPECT_EQ(j["direction"], 5);
    EXPECT_EQ(j["target_id"], 300u);
    EXPECT_FALSE(j.contains("spell_id"));
}

TEST(player_action_broadcast_test, magic_action_includes_spell_id)
{
    net::player_action_broadcast_data data{
        .entity_id = 102, .action = "magic", .direction = 1, .target_id = 400, .spell_id = 15};
    auto j = data.to_json();
    EXPECT_EQ(j["entity_id"], 102u);
    EXPECT_EQ(j["action"], "magic");
    EXPECT_EQ(j["direction"], 1);
    EXPECT_EQ(j["target_id"], 400u);
    EXPECT_EQ(j["spell_id"], 15u);
}

TEST(player_action_broadcast_test, pickup_action_omits_optional_fields)
{
    net::player_action_broadcast_data data{.entity_id = 103, .action = "pickup", .direction = 2};
    auto j = data.to_json();
    EXPECT_EQ(j["entity_id"], 103u);
    EXPECT_EQ(j["action"], "pickup");
    EXPECT_EQ(j["direction"], 2);
    EXPECT_FALSE(j.contains("target_id"));
    EXPECT_FALSE(j.contains("spell_id"));
}

TEST(player_action_broadcast_test, make_broadcast_message)
{
    net::player_action_broadcast_data data{.entity_id = 500, .action = "attack", .direction = 4, .target_id = 600};
    auto msg = net::make_player_action_broadcast(data);
    EXPECT_EQ(msg.type, net::json_message_type::player_action_broadcast);
    EXPECT_EQ(msg.seq, 0u);
    EXPECT_EQ(msg.data["entity_id"], 500u);
    EXPECT_EQ(msg.data["action"], "attack");
    EXPECT_EQ(msg.data["target_id"], 600u);
}

TEST(player_action_broadcast_test, message_type_to_string)
{
    EXPECT_EQ(net::to_string(net::json_message_type::player_action_broadcast), "player_action_broadcast");
}

TEST(player_action_broadcast_test, parse_message_type)
{
    EXPECT_EQ(net::parse_message_type("player_action_broadcast"), net::json_message_type::player_action_broadcast);
}
