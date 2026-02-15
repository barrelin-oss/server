// command_list_test.cpp
// Tests for command list feature (available_commands + command_availability_update)

#include <gtest/gtest.h>
#include "core/types.h"
#include "network/json_protocol.h"
#include "bridge/handlers/game_handlers.h"
#include "player/player.h"
#include "player/player_system.h"
#include "social/social_system.h"
#include "social/guild.h"

namespace net = hb::network;

// ========== Protocol Serialization Tests ==========

TEST(command_list_test, command_entry_to_json)
{
    net::command_entry_msg entry{.name = "online",
                                 .description = "Show online player count",
                                 .usage = "/online",
                                 .category = "general",
                                 .enabled = true};
    auto j = entry.to_json();
    EXPECT_EQ(j["name"], "online");
    EXPECT_EQ(j["description"], "Show online player count");
    EXPECT_EQ(j["usage"], "/online");
    EXPECT_EQ(j["category"], "general");
    EXPECT_TRUE(j["enabled"].get<bool>());
}

TEST(command_list_test, command_entry_disabled)
{
    net::command_entry_msg entry{.name = "gcreate",
                                 .description = "Create a new guild",
                                 .usage = "/gcreate <name> [tag]",
                                 .category = "guild",
                                 .enabled = false};
    auto j = entry.to_json();
    EXPECT_EQ(j["name"], "gcreate");
    EXPECT_FALSE(j["enabled"].get<bool>());
}

TEST(command_list_test, make_available_commands_empty)
{
    auto msg = net::make_available_commands({});
    EXPECT_EQ(msg.type, net::json_message_type::available_commands);
    EXPECT_EQ(msg.seq, 0u);
    EXPECT_TRUE(msg.data.contains("commands"));
    EXPECT_TRUE(msg.data["commands"].is_array());
    EXPECT_EQ(msg.data["commands"].size(), 0u);
}

TEST(command_list_test, make_available_commands_with_entries)
{
    std::vector<net::command_entry_msg> entries = {
        {"online", "Show online player count", "/online", "general", true},
        {"gcreate", "Create a new guild", "/gcreate <name> [tag]", "guild", true},
        {"gdisband", "Disband your guild", "/gdisband", "guild", false}};

    auto msg = net::make_available_commands(entries);
    EXPECT_EQ(msg.type, net::json_message_type::available_commands);
    EXPECT_EQ(msg.data["commands"].size(), 3u);

    EXPECT_EQ(msg.data["commands"][0]["name"], "online");
    EXPECT_TRUE(msg.data["commands"][0]["enabled"].get<bool>());

    EXPECT_EQ(msg.data["commands"][1]["name"], "gcreate");
    EXPECT_TRUE(msg.data["commands"][1]["enabled"].get<bool>());

    EXPECT_EQ(msg.data["commands"][2]["name"], "gdisband");
    EXPECT_FALSE(msg.data["commands"][2]["enabled"].get<bool>());
}

TEST(command_list_test, make_command_availability_update_empty)
{
    auto msg = net::make_command_availability_update({});
    EXPECT_EQ(msg.type, net::json_message_type::command_availability_update);
    EXPECT_EQ(msg.seq, 0u);
    EXPECT_TRUE(msg.data["commands"].is_array());
    EXPECT_EQ(msg.data["commands"].size(), 0u);
}

TEST(command_list_test, make_command_availability_update_with_changes)
{
    std::vector<std::pair<std::string, bool>> changes = {{"gcreate", false}, {"gquit", true}, {"ginvite", true}};

    auto msg = net::make_command_availability_update(changes);
    EXPECT_EQ(msg.type, net::json_message_type::command_availability_update);
    EXPECT_EQ(msg.data["commands"].size(), 3u);

    EXPECT_EQ(msg.data["commands"][0]["name"], "gcreate");
    EXPECT_FALSE(msg.data["commands"][0]["enabled"].get<bool>());

    EXPECT_EQ(msg.data["commands"][1]["name"], "gquit");
    EXPECT_TRUE(msg.data["commands"][1]["enabled"].get<bool>());

    EXPECT_EQ(msg.data["commands"][2]["name"], "ginvite");
    EXPECT_TRUE(msg.data["commands"][2]["enabled"].get<bool>());
}

// ========== Message Type Tests ==========

TEST(command_list_test, to_string_available_commands)
{
    EXPECT_EQ(net::to_string(net::json_message_type::available_commands), "available_commands");
}

TEST(command_list_test, to_string_command_availability_update)
{
    EXPECT_EQ(net::to_string(net::json_message_type::command_availability_update), "command_availability_update");
}

TEST(command_list_test, parse_message_type_available_commands)
{
    EXPECT_EQ(net::parse_message_type("available_commands"), net::json_message_type::available_commands);
}

TEST(command_list_test, parse_message_type_command_availability_update)
{
    EXPECT_EQ(net::parse_message_type("command_availability_update"),
              net::json_message_type::command_availability_update);
}

// ========== Command Registry Tests ==========

TEST(command_list_test, player_commands_registry_not_empty)
{
    using gh = hb::bridge::game_handlers;
    const auto& commands = gh::get_player_commands();
    EXPECT_GT(commands.size(), 0u);
}

TEST(command_list_test, player_commands_has_general)
{
    using gh = hb::bridge::game_handlers;
    const auto& commands = gh::get_player_commands();

    int general_count = 0;
    for (const auto& cmd : commands)
    {
        if (cmd.category == gh::command_category::general)
        {
            general_count++;
        }
    }
    EXPECT_GE(general_count, 3); // online, time, pos
}

TEST(command_list_test, player_commands_has_guild)
{
    using gh = hb::bridge::game_handlers;
    const auto& commands = gh::get_player_commands();

    int guild_count = 0;
    for (const auto& cmd : commands)
    {
        if (cmd.category == gh::command_category::guild)
        {
            guild_count++;
        }
    }
    EXPECT_GE(guild_count, 7); // gcreate, gdisband, ginvite, gkick, gaccept, gdecline, gquit
}

TEST(command_list_test, general_commands_always_enabled)
{
    using gh = hb::bridge::game_handlers;
    const auto& commands = gh::get_player_commands();

    for (const auto& cmd : commands)
    {
        if (cmd.category == gh::command_category::general)
        {
            EXPECT_EQ(cmd.enabled_check, nullptr)
                << "General command '" << cmd.name << "' should have no visibility check";
        }
    }
}

TEST(command_list_test, guild_management_commands_have_visibility_check)
{
    using gh = hb::bridge::game_handlers;
    const auto& commands = gh::get_player_commands();

    // Guild management commands (not gaccept/gdecline) should have visibility checks
    for (const auto& cmd : commands)
    {
        if (cmd.category == gh::command_category::guild && cmd.name != "gaccept" && cmd.name != "gdecline")
        {
            EXPECT_NE(cmd.enabled_check, nullptr)
                << "Guild command '" << cmd.name << "' should have a visibility check";
        }
    }
}

// ========== Visibility Predicate Tests ==========

class command_visibility_test : public ::testing::Test
{
protected:
    void SetUp() override
    {
        social_.initialize();

        plr_.id = hb::player_id(1);
        plr_.name = "TestPlayer";
    }

    auto find_command(const std::string& name) -> const hb::bridge::game_handlers::command_descriptor*
    {
        using gh = hb::bridge::game_handlers;
        for (const auto& cmd : gh::get_player_commands())
        {
            if (cmd.name == name)
                return &cmd;
        }
        return nullptr;
    }

    auto check_enabled(const std::string& name) -> bool
    {
        auto* cmd = find_command(name);
        if (!cmd || !cmd->enabled_check)
            return true;
        return cmd->enabled_check(plr_, &social_);
    }

    hb::social::social_system social_;
    hb::player::player plr_;
};

TEST_F(command_visibility_test, no_guild_gcreate_enabled)
{
    EXPECT_TRUE(check_enabled("gcreate"));
}

TEST_F(command_visibility_test, no_guild_gquit_disabled)
{
    EXPECT_FALSE(check_enabled("gquit"));
}

TEST_F(command_visibility_test, no_guild_gdisband_disabled)
{
    EXPECT_FALSE(check_enabled("gdisband"));
}

TEST_F(command_visibility_test, no_guild_ginvite_disabled)
{
    EXPECT_FALSE(check_enabled("ginvite"));
}

TEST_F(command_visibility_test, no_guild_gkick_disabled)
{
    EXPECT_FALSE(check_enabled("gkick"));
}

TEST_F(command_visibility_test, gaccept_always_enabled)
{
    // gaccept is always shown enabled - client handles contextual graying
    EXPECT_TRUE(check_enabled("gaccept"));
}

TEST_F(command_visibility_test, gdecline_always_enabled)
{
    EXPECT_TRUE(check_enabled("gdecline"));
}

TEST_F(command_visibility_test, guild_master_gcreate_disabled)
{
    auto result = social_.create_guild(plr_.id, "TestGuild", "TG");
    ASSERT_TRUE(result.is_ok());

    EXPECT_FALSE(check_enabled("gcreate"));
}

TEST_F(command_visibility_test, guild_master_gdisband_enabled)
{
    auto result = social_.create_guild(plr_.id, "TestGuild", "TG");
    ASSERT_TRUE(result.is_ok());

    EXPECT_TRUE(check_enabled("gdisband"));
}

TEST_F(command_visibility_test, guild_master_gquit_disabled)
{
    auto result = social_.create_guild(plr_.id, "TestGuild", "TG");
    ASSERT_TRUE(result.is_ok());

    // Guild master cannot /gquit
    EXPECT_FALSE(check_enabled("gquit"));
}

TEST_F(command_visibility_test, guild_master_ginvite_enabled)
{
    auto result = social_.create_guild(plr_.id, "TestGuild", "TG");
    ASSERT_TRUE(result.is_ok());

    EXPECT_TRUE(check_enabled("ginvite"));
}

TEST_F(command_visibility_test, guild_master_gkick_enabled)
{
    auto result = social_.create_guild(plr_.id, "TestGuild", "TG");
    ASSERT_TRUE(result.is_ok());

    EXPECT_TRUE(check_enabled("gkick"));
}

TEST_F(command_visibility_test, guild_recruit_gquit_enabled)
{
    // Create guild with another player, then add our player as recruit
    hb::player::player master;
    master.id = hb::player_id(2);
    master.name = "Master";

    auto result = social_.create_guild(master.id, "TestGuild", "TG");
    ASSERT_TRUE(result.is_ok());

    auto gid = result.value();
    social_.join_guild(plr_.id, gid);

    EXPECT_TRUE(check_enabled("gquit"));
}

TEST_F(command_visibility_test, guild_recruit_gdisband_disabled)
{
    hb::player::player master;
    master.id = hb::player_id(2);
    master.name = "Master";

    auto result = social_.create_guild(master.id, "TestGuild", "TG");
    ASSERT_TRUE(result.is_ok());

    auto gid = result.value();
    social_.join_guild(plr_.id, gid);

    EXPECT_FALSE(check_enabled("gdisband"));
}

TEST_F(command_visibility_test, guild_recruit_ginvite_disabled)
{
    hb::player::player master;
    master.id = hb::player_id(2);
    master.name = "Master";

    auto result = social_.create_guild(master.id, "TestGuild", "TG");
    ASSERT_TRUE(result.is_ok());

    auto gid = result.value();
    social_.join_guild(plr_.id, gid);

    // Recruit rank doesn't have invite permission by default
    EXPECT_FALSE(check_enabled("ginvite"));
}

TEST_F(command_visibility_test, guild_recruit_gkick_disabled)
{
    hb::player::player master;
    master.id = hb::player_id(2);
    master.name = "Master";

    auto result = social_.create_guild(master.id, "TestGuild", "TG");
    ASSERT_TRUE(result.is_ok());

    auto gid = result.value();
    social_.join_guild(plr_.id, gid);

    EXPECT_FALSE(check_enabled("gkick"));
}

TEST_F(command_visibility_test, gaccept_gdecline_always_enabled_regardless_of_invite)
{
    // Always enabled even without pending invite - client grays out contextually
    EXPECT_TRUE(check_enabled("gaccept"));
    EXPECT_TRUE(check_enabled("gdecline"));

    // Create a guild with another player and invite us
    hb::player::player master;
    master.id = hb::player_id(2);
    master.name = "Master";

    auto result = social_.create_guild(master.id, "TestGuild", "TG");
    ASSERT_TRUE(result.is_ok());

    auto gid = result.value();
    social_.invite_to_guild(master.id, gid, plr_.id);

    // Still enabled
    EXPECT_TRUE(check_enabled("gaccept"));
    EXPECT_TRUE(check_enabled("gdecline"));
}

// ========== Evaluate Guild Commands Tests ==========

class evaluate_guild_test : public ::testing::Test
{
protected:
    void SetUp() override
    {
        social_.initialize();
        players_.initialize();

        // Create a player
        hb::player::player_create_info info;
        info.name = "TestPlayer";
        auto result = players_.create_player(info);
        ASSERT_TRUE(result.is_ok());
        pid_ = result.value();

        handlers_.initialize(nullptr, &players_, nullptr, &social_);
    }

    auto find_change(const std::vector<std::pair<std::string, bool>>& changes,
                     const std::string& name) -> std::optional<bool>
    {
        for (const auto& [n, e] : changes)
        {
            if (n == name)
                return e;
        }
        return std::nullopt;
    }

    hb::social::social_system social_;
    hb::player::player_system players_;
    hb::bridge::game_handlers handlers_;
    hb::player_id pid_;
};

TEST_F(evaluate_guild_test, no_guild_returns_guild_commands)
{
    auto changes = handlers_.evaluate_guild_commands(pid_);
    EXPECT_GT(changes.size(), 0u);

    EXPECT_EQ(find_change(changes, "gcreate"), true);
    EXPECT_EQ(find_change(changes, "gquit"), false);
    EXPECT_EQ(find_change(changes, "gdisband"), false);
    EXPECT_EQ(find_change(changes, "ginvite"), false);
    EXPECT_EQ(find_change(changes, "gkick"), false);
}

TEST_F(evaluate_guild_test, in_guild_as_master)
{
    auto result = social_.create_guild(pid_, "TestGuild", "TG");
    ASSERT_TRUE(result.is_ok());

    auto changes = handlers_.evaluate_guild_commands(pid_);

    EXPECT_EQ(find_change(changes, "gcreate"), false);
    EXPECT_EQ(find_change(changes, "gquit"), false); // master can't gquit
    EXPECT_EQ(find_change(changes, "gdisband"), true);
    EXPECT_EQ(find_change(changes, "ginvite"), true);
    EXPECT_EQ(find_change(changes, "gkick"), true);
}

TEST_F(evaluate_guild_test, invalid_player_returns_empty)
{
    auto changes = handlers_.evaluate_guild_commands(hb::player_id(9999));
    EXPECT_TRUE(changes.empty());
}
