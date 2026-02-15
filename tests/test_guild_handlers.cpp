// test_guild_handlers.cpp
// Tests for guild JSON protocol handlers

#include <gtest/gtest.h>
#include "core/types.h"
#include "social/guild.h"
#include "social/social_system.h"
#include "network/json_protocol.h"

using hb::guild_id;
using hb::player_id;
using namespace hb::social;
namespace net = hb::network;

// ========== Protocol Data Struct Tests ==========

TEST(guild_protocol_test, create_request_from_json)
{
    auto j = nlohmann::json{{"name", "TestGuild"}, {"tag", "TG"}};
    auto result = net::guild_create_request_data::from_json(j);
    ASSERT_TRUE(result.is_ok());
    EXPECT_EQ(result.value().name, "TestGuild");
    EXPECT_EQ(result.value().tag, "TG");
}

TEST(guild_protocol_test, create_request_missing_name)
{
    auto j = nlohmann::json{{"tag", "TG"}};
    auto result = net::guild_create_request_data::from_json(j);
    EXPECT_TRUE(result.is_err());
}

TEST(guild_protocol_test, create_request_empty_name)
{
    auto j = nlohmann::json{{"name", ""}, {"tag", "TG"}};
    auto result = net::guild_create_request_data::from_json(j);
    EXPECT_TRUE(result.is_err());
}

TEST(guild_protocol_test, create_request_missing_tag)
{
    auto j = nlohmann::json{{"name", "TestGuild"}};
    auto result = net::guild_create_request_data::from_json(j);
    EXPECT_TRUE(result.is_err());
}

TEST(guild_protocol_test, target_request_from_json)
{
    auto j = nlohmann::json{{"target_name", "Player1"}};
    auto result = net::guild_target_request_data::from_json(j);
    ASSERT_TRUE(result.is_ok());
    EXPECT_EQ(result.value().target_name, "Player1");
}

TEST(guild_protocol_test, target_request_missing_name)
{
    auto j = nlohmann::json{};
    auto result = net::guild_target_request_data::from_json(j);
    EXPECT_TRUE(result.is_err());
}

TEST(guild_protocol_test, target_request_empty_name)
{
    auto j = nlohmann::json{{"target_name", ""}};
    auto result = net::guild_target_request_data::from_json(j);
    EXPECT_TRUE(result.is_err());
}

TEST(guild_protocol_test, set_motd_request_from_json)
{
    auto j = nlohmann::json{{"motd", "Welcome to the guild!"}};
    auto result = net::guild_set_motd_request_data::from_json(j);
    ASSERT_TRUE(result.is_ok());
    EXPECT_EQ(result.value().motd, "Welcome to the guild!");
}

TEST(guild_protocol_test, set_motd_request_allows_empty)
{
    auto j = nlohmann::json{{"motd", ""}};
    auto result = net::guild_set_motd_request_data::from_json(j);
    ASSERT_TRUE(result.is_ok());
    EXPECT_EQ(result.value().motd, "");
}

TEST(guild_protocol_test, set_motd_request_missing_field)
{
    auto j = nlohmann::json{};
    auto result = net::guild_set_motd_request_data::from_json(j);
    EXPECT_TRUE(result.is_err());
}

// ========== Builder Tests ==========

TEST(guild_protocol_test, make_guild_response_success)
{
    auto msg = net::make_guild_response(42, net::json_message_type::guild_create_response, true);
    EXPECT_EQ(msg.type, net::json_message_type::guild_create_response);
    EXPECT_EQ(msg.seq, 42u);
    EXPECT_TRUE(msg.data["success"].get<bool>());
    EXPECT_FALSE(msg.data.contains("error"));
}

TEST(guild_protocol_test, make_guild_response_failure)
{
    auto msg = net::make_guild_response(42, net::json_message_type::guild_create_response, false, "name_taken");
    EXPECT_FALSE(msg.data["success"].get<bool>());
    EXPECT_EQ(msg.data["error"], "name_taken");
}

TEST(guild_protocol_test, make_guild_response_with_extra)
{
    auto extra = nlohmann::json{{"guild_name", "TestGuild"}, {"tag", "TG"}};
    auto msg = net::make_guild_response(1, net::json_message_type::guild_create_response, true, {}, extra);
    EXPECT_TRUE(msg.data["success"].get<bool>());
    EXPECT_EQ(msg.data["guild_name"], "TestGuild");
    EXPECT_EQ(msg.data["tag"], "TG");
}

TEST(guild_protocol_test, make_guild_info_response_success)
{
    std::vector<net::guild_member_info_msg> members = {
        {.name = "Alice", .rank = 0, .rank_name = "Guild Master", .is_online = true},
        {.name = "Bob", .rank = 3, .rank_name = "Member", .is_online = false},
    };

    auto msg = net::make_guild_info_response(5, true, "TestGuild", "TG", "Hello!", 2, "Alice", members);
    EXPECT_EQ(msg.type, net::json_message_type::guild_info_response);
    EXPECT_EQ(msg.seq, 5u);
    EXPECT_TRUE(msg.data["success"].get<bool>());
    EXPECT_EQ(msg.data["guild_name"], "TestGuild");
    EXPECT_EQ(msg.data["tag"], "TG");
    EXPECT_EQ(msg.data["motd"], "Hello!");
    EXPECT_EQ(msg.data["member_count"], 2);
    EXPECT_EQ(msg.data["master_name"], "Alice");
    ASSERT_EQ(msg.data["members"].size(), 2u);
    EXPECT_EQ(msg.data["members"][0]["name"], "Alice");
    EXPECT_TRUE(msg.data["members"][0]["is_online"].get<bool>());
    EXPECT_EQ(msg.data["members"][1]["name"], "Bob");
    EXPECT_FALSE(msg.data["members"][1]["is_online"].get<bool>());
}

TEST(guild_protocol_test, make_guild_info_response_failure)
{
    auto msg = net::make_guild_info_response(5, false, {}, {}, {}, 0, {}, {}, "not_in_guild");
    EXPECT_FALSE(msg.data["success"].get<bool>());
    EXPECT_EQ(msg.data["error"], "not_in_guild");
    EXPECT_FALSE(msg.data.contains("guild_name"));
}

TEST(guild_protocol_test, make_guild_update)
{
    auto msg = net::make_guild_update("member_joined", "TestGuild", "Alice");
    EXPECT_EQ(msg.type, net::json_message_type::guild_update);
    EXPECT_EQ(msg.seq, 0u);
    EXPECT_EQ(msg.data["action"], "member_joined");
    EXPECT_EQ(msg.data["guild_name"], "TestGuild");
    EXPECT_EQ(msg.data["player_name"], "Alice");
}

TEST(guild_protocol_test, make_guild_update_no_player)
{
    auto msg = net::make_guild_update("guild_disbanded", "TestGuild");
    EXPECT_EQ(msg.data["action"], "guild_disbanded");
    EXPECT_FALSE(msg.data.contains("player_name"));
}

TEST(guild_protocol_test, make_guild_update_with_extra)
{
    auto msg = net::make_guild_update("motd_changed", "TestGuild", {}, {{"motd", "New MOTD"}});
    EXPECT_EQ(msg.data["motd"], "New MOTD");
}

TEST(guild_protocol_test, guild_member_info_to_json)
{
    net::guild_member_info_msg info{.name = "Alice", .rank = 0, .rank_name = "Guild Master", .is_online = true};
    auto j = info.to_json();
    EXPECT_EQ(j["name"], "Alice");
    EXPECT_EQ(j["rank"], 0);
    EXPECT_EQ(j["rank_name"], "Guild Master");
    EXPECT_TRUE(j["is_online"].get<bool>());
}

// ========== Message Type Parsing ==========

TEST(guild_protocol_test, message_type_roundtrip)
{
    auto types = {
        net::json_message_type::guild_create_request,   net::json_message_type::guild_create_response,
        net::json_message_type::guild_disband_request,  net::json_message_type::guild_disband_response,
        net::json_message_type::guild_leave_request,    net::json_message_type::guild_leave_response,
        net::json_message_type::guild_kick_request,     net::json_message_type::guild_kick_response,
        net::json_message_type::guild_invite_request,   net::json_message_type::guild_invite_response,
        net::json_message_type::guild_promote_request,  net::json_message_type::guild_promote_response,
        net::json_message_type::guild_demote_request,   net::json_message_type::guild_demote_response,
        net::json_message_type::guild_set_motd_request, net::json_message_type::guild_set_motd_response,
        net::json_message_type::guild_info_request,     net::json_message_type::guild_info_response,
        net::json_message_type::guild_update,
    };

    for (auto t : types)
    {
        auto str = net::to_string(t);
        EXPECT_NE(str, "unknown") << "to_string failed for enum value " << static_cast<int>(t);
        auto parsed = net::parse_message_type(str);
        EXPECT_EQ(parsed, t) << "parse_message_type roundtrip failed for '" << str << "'";
    }
}

// ========== Visible Entity Guild Data ==========

TEST(guild_protocol_test, entity_spawn_includes_guild_data)
{
    net::visible_entity_msg entity{.entity_id = 42,
                                   .type = "player",
                                   .name = "Alice",
                                   .x = 100,
                                   .y = 200,
                                   .hp_percent = 80,
                                   .direction = 2,
                                   .faction = "aresden",
                                   .hostility = "friendly",
                                   .pk_status = "innocent",
                                   .guild_name = "TestGuild",
                                   .guild_tag = "TG",
                                   .template_id = 0,
                                   .sprite_id = 0,
                                   .level = 0,
                                   .category = {}};

    auto j = entity.to_json();
    EXPECT_EQ(j["guild_name"], "TestGuild");
    EXPECT_EQ(j["guild_tag"], "TG");
}

TEST(guild_protocol_test, entity_spawn_no_guild_data_when_empty)
{
    net::visible_entity_msg entity{.entity_id = 42,
                                   .type = "player",
                                   .name = "Alice",
                                   .x = 100,
                                   .y = 200,
                                   .hp_percent = 80,
                                   .direction = 2,
                                   .faction = "aresden",
                                   .hostility = "friendly",
                                   .pk_status = "innocent",
                                   .guild_name = {},
                                   .guild_tag = {},
                                   .template_id = 0,
                                   .sprite_id = 0,
                                   .level = 0,
                                   .category = {}};

    auto j = entity.to_json();
    EXPECT_FALSE(j.contains("guild_name"));
    EXPECT_FALSE(j.contains("guild_tag"));
}

TEST(guild_protocol_test, npc_entity_spawn_no_guild_data)
{
    net::visible_entity_msg entity{.entity_id = 99,
                                   .type = "npc",
                                   .name = "Slime",
                                   .x = 50,
                                   .y = 60,
                                   .hp_percent = 100,
                                   .direction = 0,
                                   .faction = {},
                                   .hostility = "hostile",
                                   .pk_status = {},
                                   .guild_name = {},
                                   .guild_tag = {},
                                   .template_id = 10,
                                   .sprite_id = 10,
                                   .level = 5,
                                   .category = "monster"};

    auto j = entity.to_json();
    EXPECT_FALSE(j.contains("guild_name"));
    EXPECT_FALSE(j.contains("guild_tag"));
}

// ========== Social System Guild Operations ==========

class guild_handler_test : public ::testing::Test
{
protected:
    void SetUp() override
    {
        social.initialize();
        social.register_player(player_id{1}, "Alice");
        social.register_player(player_id{2}, "Bob");
        social.register_player(player_id{3}, "Charlie");
    }

    void TearDown() override { social.shutdown(); }

    auto create_test_guild(player_id founder,
                           const std::string& name = "TestGuild",
                           const std::string& tag = "TG") -> guild_id
    {
        auto result = social.create_guild(founder, name, tag);
        EXPECT_TRUE(result.is_ok()) << "Failed to create guild";
        return result.value();
    }

    social_system social;
};

TEST_F(guild_handler_test, create_guild_success)
{
    auto result = social.create_guild(player_id{1}, "TestGuild", "TG");
    ASSERT_TRUE(result.is_ok());
    auto gid = result.value();

    auto* g = social.get_guild(gid);
    ASSERT_NE(g, nullptr);
    EXPECT_EQ(g->name, "TestGuild");
    EXPECT_EQ(g->tag, "TG");
    EXPECT_EQ(g->member_count(), 1u);
    EXPECT_TRUE(g->is_member(player_id{1}));

    auto* member = g->get_member(player_id{1});
    ASSERT_NE(member, nullptr);
    EXPECT_EQ(member->rank, guild_rank::guild_master);
}

TEST_F(guild_handler_test, create_guild_duplicate_name)
{
    create_test_guild(player_id{1}, "TestGuild", "TG");

    auto result = social.create_guild(player_id{2}, "TestGuild", "XX");
    EXPECT_TRUE(result.is_err());
    EXPECT_EQ(result.error(), guild_result::name_taken);
}

TEST_F(guild_handler_test, create_guild_already_in_guild)
{
    create_test_guild(player_id{1});

    auto result = social.create_guild(player_id{1}, "Another", "AN");
    EXPECT_TRUE(result.is_err());
    EXPECT_EQ(result.error(), guild_result::already_in_guild);
}

TEST_F(guild_handler_test, disband_guild_as_master)
{
    auto gid = create_test_guild(player_id{1});
    social.join_guild(player_id{2}, gid);

    auto result = social.disband_guild(player_id{1}, gid);
    EXPECT_EQ(result, guild_result::success);
    EXPECT_EQ(social.get_guild(gid), nullptr);
    EXPECT_FALSE(social.get_player_guild(player_id{1}).is_valid());
    EXPECT_FALSE(social.get_player_guild(player_id{2}).is_valid());
}

TEST_F(guild_handler_test, disband_guild_non_master_fails)
{
    auto gid = create_test_guild(player_id{1});
    social.join_guild(player_id{2}, gid);

    auto result = social.disband_guild(player_id{2}, gid);
    EXPECT_EQ(result, guild_result::insufficient_permissions);
    EXPECT_NE(social.get_guild(gid), nullptr);
}

TEST_F(guild_handler_test, leave_guild_normal_member)
{
    auto gid = create_test_guild(player_id{1});
    social.join_guild(player_id{2}, gid);

    auto result = social.leave_guild(player_id{2});
    EXPECT_EQ(result, guild_result::success);
    EXPECT_FALSE(social.get_player_guild(player_id{2}).is_valid());

    auto* g = social.get_guild(gid);
    ASSERT_NE(g, nullptr);
    EXPECT_EQ(g->member_count(), 1u);
}

TEST_F(guild_handler_test, kick_member)
{
    auto gid = create_test_guild(player_id{1});
    social.join_guild(player_id{2}, gid);

    auto result = social.kick_from_guild(player_id{1}, player_id{2});
    EXPECT_EQ(result, guild_result::success);
    EXPECT_FALSE(social.get_player_guild(player_id{2}).is_valid());
}

TEST_F(guild_handler_test, kick_cannot_kick_self)
{
    create_test_guild(player_id{1});

    auto result = social.kick_from_guild(player_id{1}, player_id{1});
    EXPECT_EQ(result, guild_result::cannot_kick_self);
}

TEST_F(guild_handler_test, kick_non_member_fails)
{
    create_test_guild(player_id{1});

    auto result = social.kick_from_guild(player_id{1}, player_id{3});
    EXPECT_EQ(result, guild_result::player_not_member);
}

TEST_F(guild_handler_test, promote_member)
{
    auto gid = create_test_guild(player_id{1});
    social.join_guild(player_id{2}, gid);

    auto result = social.promote_member(player_id{1}, player_id{2});
    EXPECT_EQ(result, guild_result::success);

    auto* g = social.get_guild(gid);
    ASSERT_NE(g, nullptr);
    auto* member = g->get_member(player_id{2});
    ASSERT_NE(member, nullptr);
    // Promote from recruit (4) goes to member (3)
    EXPECT_EQ(member->rank, guild_rank::member);
}

TEST_F(guild_handler_test, demote_member)
{
    auto gid = create_test_guild(player_id{1});
    social.join_guild(player_id{2}, gid);
    social.promote_member(player_id{1}, player_id{2}); // recruit → member

    auto result = social.demote_member(player_id{1}, player_id{2});
    EXPECT_EQ(result, guild_result::success);

    auto* g = social.get_guild(gid);
    auto* member = g->get_member(player_id{2});
    ASSERT_NE(member, nullptr);
    EXPECT_EQ(member->rank, guild_rank::recruit);
}

TEST_F(guild_handler_test, set_motd)
{
    auto gid = create_test_guild(player_id{1});

    auto result = social.set_guild_motd(player_id{1}, "Welcome everyone!");
    EXPECT_EQ(result, guild_result::success);

    auto* g = social.get_guild(gid);
    ASSERT_NE(g, nullptr);
    EXPECT_EQ(g->motd, "Welcome everyone!");
}

TEST_F(guild_handler_test, set_motd_non_member_fails)
{
    create_test_guild(player_id{1});

    auto result = social.set_guild_motd(player_id{2}, "Hacked MOTD");
    EXPECT_NE(result, guild_result::success);
}

TEST_F(guild_handler_test, join_guild)
{
    auto gid = create_test_guild(player_id{1});

    auto result = social.join_guild(player_id{2}, gid);
    EXPECT_EQ(result, guild_result::success);

    auto* g = social.get_guild(gid);
    ASSERT_NE(g, nullptr);
    EXPECT_EQ(g->member_count(), 2u);
    EXPECT_TRUE(g->is_member(player_id{2}));
}

TEST_F(guild_handler_test, join_guild_already_in_guild)
{
    auto gid = create_test_guild(player_id{1});
    social.join_guild(player_id{2}, gid);

    auto result = social.join_guild(player_id{2}, gid);
    EXPECT_EQ(result, guild_result::already_in_guild);
}

TEST_F(guild_handler_test, guild_info_query)
{
    auto gid = create_test_guild(player_id{1}, "InfoGuild", "IG");
    social.join_guild(player_id{2}, gid);
    social.set_guild_motd(player_id{1}, "Test MOTD");

    auto* g = social.get_guild(gid);
    ASSERT_NE(g, nullptr);
    EXPECT_EQ(g->name, "InfoGuild");
    EXPECT_EQ(g->tag, "IG");
    EXPECT_EQ(g->motd, "Test MOTD");
    EXPECT_EQ(g->member_count(), 2u);

    // Find master
    bool found_master = false;
    for (const auto& member : g->members)
    {
        if (member.rank == guild_rank::guild_master)
        {
            EXPECT_EQ(member.name, "Alice");
            found_master = true;
        }
    }
    EXPECT_TRUE(found_master);
}

TEST_F(guild_handler_test, guild_player_lookup)
{
    auto gid = create_test_guild(player_id{1});

    EXPECT_EQ(social.get_player_guild(player_id{1}), gid);
    EXPECT_FALSE(social.get_player_guild(player_id{2}).is_valid());
}

TEST_F(guild_handler_test, guild_find_by_name)
{
    auto gid = create_test_guild(player_id{1}, "FindMe", "FM");
    EXPECT_EQ(social.find_guild_by_name("FindMe"), gid);
    EXPECT_FALSE(social.find_guild_by_name("NotExists").is_valid());
}

// ========== Guild Invite Flow Tests ==========

TEST_F(guild_handler_test, invite_creates_pending_invite)
{
    auto gid = create_test_guild(player_id{1});

    auto result = social.invite_to_guild(player_id{1}, gid, player_id{2});
    EXPECT_EQ(result, guild_result::success);

    // Player should NOT be in guild yet
    EXPECT_FALSE(social.get_player_guild(player_id{2}).is_valid());

    // Should have pending invite
    EXPECT_TRUE(social.has_guild_invite(player_id{2}));
    auto* invite = social.get_guild_invite(player_id{2});
    ASSERT_NE(invite, nullptr);
    EXPECT_EQ(invite->guild, gid);
    EXPECT_EQ(invite->inviter, player_id{1});
    EXPECT_EQ(invite->guild_name, "TestGuild");
    EXPECT_EQ(invite->guild_tag, "TG");
    EXPECT_EQ(invite->inviter_name, "Alice");
}

TEST_F(guild_handler_test, accept_guild_invite)
{
    auto gid = create_test_guild(player_id{1});
    social.invite_to_guild(player_id{1}, gid, player_id{2});

    auto result = social.accept_guild_invite(player_id{2});
    ASSERT_TRUE(result.is_ok());
    EXPECT_EQ(result.value(), gid);

    // Player should now be in guild
    EXPECT_EQ(social.get_player_guild(player_id{2}), gid);
    auto* g = social.get_guild(gid);
    ASSERT_NE(g, nullptr);
    EXPECT_EQ(g->member_count(), 2u);
    EXPECT_TRUE(g->is_member(player_id{2}));

    // Invite should be cleared
    EXPECT_FALSE(social.has_guild_invite(player_id{2}));
}

TEST_F(guild_handler_test, decline_guild_invite)
{
    auto gid = create_test_guild(player_id{1});
    social.invite_to_guild(player_id{1}, gid, player_id{2});

    auto result = social.decline_guild_invite(player_id{2});
    EXPECT_EQ(result, guild_result::success);

    // Player should NOT be in guild
    EXPECT_FALSE(social.get_player_guild(player_id{2}).is_valid());

    // Invite should be cleared
    EXPECT_FALSE(social.has_guild_invite(player_id{2}));
}

TEST_F(guild_handler_test, accept_without_invite_fails)
{
    auto result = social.accept_guild_invite(player_id{2});
    EXPECT_TRUE(result.is_err());
}

TEST_F(guild_handler_test, decline_without_invite_fails)
{
    auto result = social.decline_guild_invite(player_id{2});
    EXPECT_NE(result, guild_result::success);
}

TEST_F(guild_handler_test, invite_replaces_existing)
{
    auto gid1 = create_test_guild(player_id{1}, "Guild1", "G1");

    // Create second guild with player 3
    auto gid2 = social.create_guild(player_id{3}, "Guild2", "G2");
    ASSERT_TRUE(gid2.is_ok());

    // First invite
    social.invite_to_guild(player_id{1}, gid1, player_id{2});
    EXPECT_EQ(social.get_guild_invite(player_id{2})->guild, gid1);

    // Second invite replaces first
    social.invite_to_guild(player_id{3}, gid2.value(), player_id{2});
    EXPECT_EQ(social.get_guild_invite(player_id{2})->guild, gid2.value());
}

TEST_F(guild_handler_test, invite_already_in_guild_fails)
{
    auto gid = create_test_guild(player_id{1});
    social.join_guild(player_id{2}, gid);

    auto result = social.invite_to_guild(player_id{1}, gid, player_id{2});
    EXPECT_EQ(result, guild_result::already_in_guild);
}

TEST_F(guild_handler_test, invite_insufficient_permissions)
{
    auto gid = create_test_guild(player_id{1});
    social.join_guild(player_id{3}, gid);

    // Recruit (player 3) shouldn't be able to invite
    auto result = social.invite_to_guild(player_id{3}, gid, player_id{2});
    EXPECT_EQ(result, guild_result::insufficient_permissions);
}

// ========== Invite Protocol Tests ==========

TEST(guild_protocol_test, invite_respond_request_from_json_accept)
{
    auto j = nlohmann::json{{"accept", true}};
    auto result = net::guild_invite_respond_request_data::from_json(j);
    ASSERT_TRUE(result.is_ok());
    EXPECT_TRUE(result.value().accept);
}

TEST(guild_protocol_test, invite_respond_request_from_json_decline)
{
    auto j = nlohmann::json{{"accept", false}};
    auto result = net::guild_invite_respond_request_data::from_json(j);
    ASSERT_TRUE(result.is_ok());
    EXPECT_FALSE(result.value().accept);
}

TEST(guild_protocol_test, invite_respond_request_defaults_to_false)
{
    auto j = nlohmann::json{};
    auto result = net::guild_invite_respond_request_data::from_json(j);
    ASSERT_TRUE(result.is_ok());
    EXPECT_FALSE(result.value().accept);
}

TEST(guild_protocol_test, make_guild_invite_received)
{
    auto msg = net::make_guild_invite_received("Knights", "KNT", "Alice");
    EXPECT_EQ(msg.type, net::json_message_type::guild_invite_received);
    EXPECT_EQ(msg.seq, 0u);
    EXPECT_EQ(msg.data["guild_name"], "Knights");
    EXPECT_EQ(msg.data["guild_tag"], "KNT");
    EXPECT_EQ(msg.data["inviter_name"], "Alice");
}

TEST(guild_protocol_test, invite_message_types_roundtrip)
{
    auto types = {
        net::json_message_type::guild_invite_received,
        net::json_message_type::guild_invite_respond_request,
        net::json_message_type::guild_invite_respond_response,
    };

    for (auto t : types)
    {
        auto str = net::to_string(t);
        EXPECT_NE(str, "unknown") << "to_string failed for enum value " << static_cast<int>(t);
        auto parsed = net::parse_message_type(str);
        EXPECT_EQ(parsed, t) << "parse_message_type roundtrip failed for '" << str << "'";
    }
}

// ========== Character Data Guild Fields ==========

TEST(guild_protocol_test, character_data_includes_guild_fields)
{
    net::character_data_msg char_data{.id = 1,
                                      .name = "Alice",
                                      .level = 50,
                                      .class_type = 1,
                                      .nation = 1,
                                      .gender = 1,
                                      .map_name = "aresden",
                                      .pos_x = 100,
                                      .pos_y = 200,
                                      .hp = 500,
                                      .hp_max = 500,
                                      .mp = 100,
                                      .mp_max = 100,
                                      .sp = 50,
                                      .sp_max = 50,
                                      .gold = 1000,
                                      .str = 30,
                                      .dex = 20,
                                      .vit = 25,
                                      .int_ = 15,
                                      .mag = 10,
                                      .cha = 10,
                                      .hair_style = 1,
                                      .hair_color = 0,
                                      .skin_color = 0,
                                      .experience = 50000,
                                      .pk_count = 0,
                                      .hunger_level = 100,
                                      .guild_name = "Knights",
                                      .guild_tag = "KNT",
                                      .guild_rank = 0};

    auto j = char_data.to_json();
    EXPECT_EQ(j["guild_name"], "Knights");
    EXPECT_EQ(j["guild_tag"], "KNT");
    EXPECT_EQ(j["guild_rank"], 0);
}

TEST(guild_protocol_test, character_data_empty_guild)
{
    net::character_data_msg char_data{.id = 1,
                                      .name = "Alice",
                                      .level = 1,
                                      .class_type = 0,
                                      .nation = 0,
                                      .gender = 1,
                                      .map_name = "default",
                                      .pos_x = 0,
                                      .pos_y = 0,
                                      .hp = 100,
                                      .hp_max = 100,
                                      .mp = 50,
                                      .mp_max = 50,
                                      .sp = 50,
                                      .sp_max = 50,
                                      .gold = 0,
                                      .str = 10,
                                      .dex = 10,
                                      .vit = 10,
                                      .int_ = 10,
                                      .mag = 10,
                                      .cha = 10,
                                      .hair_style = 0,
                                      .hair_color = 0,
                                      .skin_color = 0,
                                      .experience = 0,
                                      .pk_count = 0,
                                      .hunger_level = 100,
                                      .guild_name = {},
                                      .guild_tag = {},
                                      .guild_rank = 0};

    auto j = char_data.to_json();
    // Fields are always present (empty strings for no guild)
    EXPECT_EQ(j["guild_name"], "");
    EXPECT_EQ(j["guild_tag"], "");
    EXPECT_EQ(j["guild_rank"], 0);
}
