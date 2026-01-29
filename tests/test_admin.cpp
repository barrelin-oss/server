// test_admin.cpp
// Unit tests for admin system

#include <gtest/gtest.h>

#include "admin/command.h"
#include "admin/admin_system.h"

using namespace hb;
using namespace hb::admin;

// ========== Command Parser Tests ==========

class command_parser_test : public ::testing::Test {};

TEST_F(command_parser_test, parse_simple_command) {
    auto result = command_parser::parse("/help");
    EXPECT_TRUE(result.valid);
    EXPECT_EQ(result.command_name, "help");
    EXPECT_TRUE(result.raw_args.empty());
}

TEST_F(command_parser_test, parse_command_with_args) {
    auto result = command_parser::parse("/spawn slime 5");
    EXPECT_TRUE(result.valid);
    EXPECT_EQ(result.command_name, "spawn");
    ASSERT_EQ(result.raw_args.size(), 2);
    EXPECT_EQ(result.raw_args[0], "slime");
    EXPECT_EQ(result.raw_args[1], "5");
}

TEST_F(command_parser_test, parse_quoted_args) {
    auto result = command_parser::parse("/say \"hello world\" player");
    EXPECT_TRUE(result.valid);
    EXPECT_EQ(result.command_name, "say");
    ASSERT_EQ(result.raw_args.size(), 2);
    EXPECT_EQ(result.raw_args[0], "hello world");
    EXPECT_EQ(result.raw_args[1], "player");
}

TEST_F(command_parser_test, parse_empty_fails) {
    auto result = command_parser::parse("");
    EXPECT_FALSE(result.valid);
}

TEST_F(command_parser_test, parse_no_prefix_fails) {
    auto result = command_parser::parse("help");
    EXPECT_FALSE(result.valid);
}

TEST_F(command_parser_test, parse_only_prefix_fails) {
    auto result = command_parser::parse("/");
    EXPECT_FALSE(result.valid);
}

TEST_F(command_parser_test, parse_custom_prefix) {
    auto result = command_parser::parse("!help arg", '!');
    EXPECT_TRUE(result.valid);
    EXPECT_EQ(result.command_name, "help");
    ASSERT_EQ(result.raw_args.size(), 1);
    EXPECT_EQ(result.raw_args[0], "arg");
}

TEST_F(command_parser_test, parse_int) {
    EXPECT_TRUE(command_parser::parse_int("123").is_ok());
    EXPECT_EQ(command_parser::parse_int("123").value(), 123);
    EXPECT_TRUE(command_parser::parse_int("-456").is_ok());
    EXPECT_EQ(command_parser::parse_int("-456").value(), -456);
    EXPECT_FALSE(command_parser::parse_int("abc").is_ok());
    EXPECT_FALSE(command_parser::parse_int("12.5").is_ok());
}

TEST_F(command_parser_test, parse_float) {
    EXPECT_TRUE(command_parser::parse_float("3.14").is_ok());
    EXPECT_DOUBLE_EQ(command_parser::parse_float("3.14").value(), 3.14);
    EXPECT_TRUE(command_parser::parse_float("-2.5").is_ok());
    EXPECT_TRUE(command_parser::parse_float("100").is_ok());
    EXPECT_FALSE(command_parser::parse_float("abc").is_ok());
}

TEST_F(command_parser_test, parse_bool) {
    EXPECT_TRUE(command_parser::parse_bool("true").is_ok());
    EXPECT_TRUE(command_parser::parse_bool("true").value());
    EXPECT_TRUE(command_parser::parse_bool("false").is_ok());
    EXPECT_FALSE(command_parser::parse_bool("false").value());
    EXPECT_TRUE(command_parser::parse_bool("yes").is_ok());
    EXPECT_TRUE(command_parser::parse_bool("yes").value());
    EXPECT_TRUE(command_parser::parse_bool("no").is_ok());
    EXPECT_FALSE(command_parser::parse_bool("no").value());
    EXPECT_TRUE(command_parser::parse_bool("1").is_ok());
    EXPECT_TRUE(command_parser::parse_bool("0").is_ok());
    EXPECT_FALSE(command_parser::parse_bool("maybe").is_ok());
}

// ========== Command Arg Tests ==========

class command_arg_test : public ::testing::Test {};

TEST_F(command_arg_test, from_string) {
    auto arg = command_arg::from_string("hello");
    EXPECT_EQ(arg.type, arg_type::string);
    EXPECT_EQ(arg.string_value, "hello");
}

TEST_F(command_arg_test, from_int) {
    auto arg = command_arg::from_int(42);
    EXPECT_EQ(arg.type, arg_type::integer);
    EXPECT_EQ(arg.int_value, 42);
}

TEST_F(command_arg_test, from_float) {
    auto arg = command_arg::from_float(3.14);
    EXPECT_EQ(arg.type, arg_type::floating);
    EXPECT_DOUBLE_EQ(arg.float_value, 3.14);
}

TEST_F(command_arg_test, from_bool) {
    auto arg = command_arg::from_bool(true);
    EXPECT_EQ(arg.type, arg_type::boolean);
    EXPECT_TRUE(arg.bool_value);
}

// ========== Admin Level Tests ==========

class admin_level_test : public ::testing::Test {};

TEST_F(admin_level_test, to_string) {
    EXPECT_EQ(to_string(admin_level::player), "Player");
    EXPECT_EQ(to_string(admin_level::helper), "Helper");
    EXPECT_EQ(to_string(admin_level::moderator), "Moderator");
    EXPECT_EQ(to_string(admin_level::game_master), "Game Master");
    EXPECT_EQ(to_string(admin_level::senior_gm), "Senior GM");
    EXPECT_EQ(to_string(admin_level::admin), "Admin");
    EXPECT_EQ(to_string(admin_level::owner), "Owner");
}

TEST_F(admin_level_test, hierarchy) {
    EXPECT_LT(static_cast<int>(admin_level::player), static_cast<int>(admin_level::helper));
    EXPECT_LT(static_cast<int>(admin_level::helper), static_cast<int>(admin_level::moderator));
    EXPECT_LT(static_cast<int>(admin_level::moderator), static_cast<int>(admin_level::game_master));
    EXPECT_LT(static_cast<int>(admin_level::game_master), static_cast<int>(admin_level::senior_gm));
    EXPECT_LT(static_cast<int>(admin_level::senior_gm), static_cast<int>(admin_level::admin));
    EXPECT_LT(static_cast<int>(admin_level::admin), static_cast<int>(admin_level::owner));
}

// ========== Admin Info Tests ==========

class admin_info_test : public ::testing::Test {};

TEST_F(admin_info_test, can_execute_same_level) {
    admin_info info;
    info.level = admin_level::moderator;
    EXPECT_TRUE(info.can_execute(admin_level::moderator));
}

TEST_F(admin_info_test, can_execute_lower_level) {
    admin_info info;
    info.level = admin_level::game_master;
    EXPECT_TRUE(info.can_execute(admin_level::moderator));
    EXPECT_TRUE(info.can_execute(admin_level::helper));
}

TEST_F(admin_info_test, cannot_execute_higher_level) {
    admin_info info;
    info.level = admin_level::helper;
    EXPECT_FALSE(info.can_execute(admin_level::moderator));
    EXPECT_FALSE(info.can_execute(admin_level::game_master));
}

// ========== Simple Command Tests ==========

class simple_command_test : public ::testing::Test {};

TEST_F(simple_command_test, execute) {
    bool executed = false;
    command_info info;
    info.name = "test";
    info.description = "Test command";
    info.usage = "/test";
    info.required_level = admin_level::helper;

    simple_command cmd(info, [&executed](const command_context&) {
        executed = true;
        return command_result::ok("success");
    });

    EXPECT_EQ(cmd.name(), "test");
    EXPECT_EQ(cmd.required_level(), admin_level::helper);

    command_context ctx;
    auto result = cmd.execute(ctx);
    EXPECT_TRUE(executed);
    EXPECT_TRUE(result.success);
    EXPECT_EQ(result.message, "success");
}

// ========== Admin System Tests ==========

class admin_system_test : public ::testing::Test {
protected:
    admin_system system;

    void SetUp() override {
        system.initialize();
    }

    void TearDown() override {
        system.shutdown();
    }
};

TEST_F(admin_system_test, lifecycle) {
    // System was initialized in SetUp
    EXPECT_EQ(system.name(), "admin_system");
}

TEST_F(admin_system_test, register_custom_command) {
    bool executed = false;

    command_info info;
    info.name = "custom";
    info.description = "Custom command";
    info.usage = "/custom";
    info.required_level = admin_level::helper;

    system.register_command(
        info,
        [&executed](const command_context&) {
            executed = true;
            return command_result::ok("done");
        }
    );

    EXPECT_TRUE(system.has_command("custom"));

    // Register an admin to execute
    player_id admin{1};
    system.register_admin(admin, "TestAdmin", admin_level::game_master);

    auto result = system.execute(admin, "/custom");
    EXPECT_TRUE(result.success);
    EXPECT_TRUE(executed);
}

TEST_F(admin_system_test, builtin_help_command) {
    EXPECT_TRUE(system.has_command("help"));

    player_id admin{1};
    system.register_admin(admin, "TestAdmin", admin_level::game_master);

    auto result = system.execute(admin, "/help");
    EXPECT_TRUE(result.success);
    EXPECT_FALSE(result.message.empty());
}

TEST_F(admin_system_test, command_aliases) {
    EXPECT_TRUE(system.has_command("help"));
    EXPECT_TRUE(system.has_command("?"));
    EXPECT_TRUE(system.has_command("commands"));
}

TEST_F(admin_system_test, unknown_command) {
    player_id admin{1};
    system.register_admin(admin, "TestAdmin", admin_level::game_master);

    auto result = system.execute(admin, "/nonexistent");
    EXPECT_FALSE(result.success);
    EXPECT_TRUE(result.message.find("Unknown command") != std::string::npos);
}

TEST_F(admin_system_test, permission_check) {
    player_id helper{1};
    system.register_admin(helper, "Helper", admin_level::helper);

    // Helper cannot use ban (requires senior_gm)
    EXPECT_FALSE(system.can_execute(helper, "ban"));

    auto result = system.execute(helper, "/ban 999");
    EXPECT_FALSE(result.success);
    EXPECT_TRUE(result.message.find("permission") != std::string::npos);
}

TEST_F(admin_system_test, admin_level_management) {
    player_id player{1};
    system.register_admin(player, "Player", admin_level::player);

    EXPECT_EQ(system.get_admin_level(player), admin_level::player);
    EXPECT_FALSE(system.is_admin(player));

    system.set_admin_level(player, admin_level::moderator);
    EXPECT_EQ(system.get_admin_level(player), admin_level::moderator);
    EXPECT_TRUE(system.is_admin(player));
}

TEST_F(admin_system_test, mute_unmute) {
    player_id gm{1};
    player_id target{2};

    system.register_admin(gm, "GM", admin_level::moderator);
    system.register_admin(target, "Target", admin_level::player);

    EXPECT_FALSE(system.is_muted(target));

    // Mute the target
    auto result = system.mute_player(target, gm, "Being rude", 300);
    EXPECT_TRUE(result.success);
    EXPECT_TRUE(system.is_muted(target));
    EXPECT_GT(system.get_mute_remaining(target), 0);

    // Unmute
    result = system.unmute_player(target, gm);
    EXPECT_TRUE(result.success);
    EXPECT_FALSE(system.is_muted(target));
}

TEST_F(admin_system_test, mute_without_permission) {
    player_id helper{1};
    player_id target{2};

    system.register_admin(helper, "Helper", admin_level::helper);
    system.register_admin(target, "Target", admin_level::player);

    auto result = system.mute_player(target, helper, "Test", 0);
    EXPECT_FALSE(result.success);
    EXPECT_FALSE(system.is_muted(target));
}

TEST_F(admin_system_test, kick_player) {
    player_id gm{1};
    player_id target{2};

    system.register_admin(gm, "GM", admin_level::moderator);
    system.register_admin(target, "Target", admin_level::player);

    bool kicked = false;
    system.on_player_kicked([&kicked](const player_kicked_event& event) {
        kicked = true;
        EXPECT_EQ(event.player.value, 2);
        EXPECT_EQ(event.reason, "AFK");
    });

    auto result = system.kick_player(target, gm, "AFK");
    EXPECT_TRUE(result.success);
    EXPECT_TRUE(kicked);
}

TEST_F(admin_system_test, ban_player) {
    player_id senior{1};
    player_id target{2};

    system.register_admin(senior, "SeniorGM", admin_level::senior_gm);
    system.register_admin(target, "Target", admin_level::player);

    bool banned = false;
    system.on_player_banned([&banned](const player_banned_event& event) {
        banned = true;
        EXPECT_EQ(event.player.value, 2);
        EXPECT_EQ(event.duration_seconds, 86400);
    });

    auto result = system.ban_player(target, senior, "Cheating", 86400);
    EXPECT_TRUE(result.success);
    EXPECT_TRUE(banned);
}

TEST_F(admin_system_test, invisibility) {
    player_id gm{1};
    system.register_admin(gm, "GM", admin_level::game_master);

    EXPECT_FALSE(system.is_invisible(gm));

    system.set_invisible(gm, true);
    EXPECT_TRUE(system.is_invisible(gm));

    system.set_invisible(gm, false);
    EXPECT_FALSE(system.is_invisible(gm));
}

TEST_F(admin_system_test, invincibility) {
    player_id gm{1};
    system.register_admin(gm, "GM", admin_level::game_master);

    EXPECT_FALSE(system.is_invincible(gm));

    system.set_invincible(gm, true);
    EXPECT_TRUE(system.is_invincible(gm));

    system.set_invincible(gm, false);
    EXPECT_FALSE(system.is_invincible(gm));
}

TEST_F(admin_system_test, command_log) {
    player_id gm{1};
    system.register_admin(gm, "GM", admin_level::game_master);

    // Execute some commands
    system.execute(gm, "/help");
    system.execute(gm, "/who");

    auto log = system.get_log_entries(10);
    EXPECT_GE(log.size(), 2);

    // Most recent should be first
    EXPECT_EQ(log[0].command_name, "who");
    EXPECT_EQ(log[1].command_name, "help");
}

TEST_F(admin_system_test, command_callback) {
    player_id gm{1};
    system.register_admin(gm, "GM", admin_level::game_master);

    int callback_count = 0;
    system.on_command_executed([&callback_count](const admin_command_executed_event& event) {
        ++callback_count;
        EXPECT_TRUE(event.success);
    });

    system.execute(gm, "/help");
    system.execute(gm, "/who");

    EXPECT_EQ(callback_count, 2);
}

TEST_F(admin_system_test, get_commands_for_level) {
    auto helper_cmds = system.get_commands_for_level(admin_level::helper);
    auto gm_cmds = system.get_commands_for_level(admin_level::game_master);
    auto owner_cmds = system.get_commands_for_level(admin_level::owner);

    // Higher levels should have access to more commands
    EXPECT_LE(helper_cmds.size(), gm_cmds.size());
    EXPECT_LE(gm_cmds.size(), owner_cmds.size());
}

TEST_F(admin_system_test, get_help_specific_command) {
    auto help = system.get_help("help");
    EXPECT_FALSE(help.empty());
    EXPECT_TRUE(help.find("help") != std::string::npos);
    EXPECT_TRUE(help.find("Description") != std::string::npos);
}

TEST_F(admin_system_test, get_help_unknown_command) {
    auto help = system.get_help("nonexistent");
    EXPECT_TRUE(help.find("Unknown") != std::string::npos);
}

TEST_F(admin_system_test, unregister_admin) {
    player_id gm{1};
    system.register_admin(gm, "GM", admin_level::game_master);
    EXPECT_TRUE(system.is_admin(gm));

    system.unregister_admin(gm);
    EXPECT_FALSE(system.is_admin(gm));
    EXPECT_EQ(system.get_admin_level(gm), admin_level::player);
}

TEST_F(admin_system_test, active_admin_count) {
    EXPECT_EQ(system.active_admin_count(), 0);

    system.register_admin(player_id{1}, "GM1", admin_level::game_master);
    EXPECT_EQ(system.active_admin_count(), 1);

    system.register_admin(player_id{2}, "GM2", admin_level::moderator);
    EXPECT_EQ(system.active_admin_count(), 2);

    // Players don't count
    system.register_admin(player_id{3}, "Player", admin_level::player);
    EXPECT_EQ(system.active_admin_count(), 2);
}

TEST_F(admin_system_test, level_change_callback) {
    player_id gm{1};
    player_id target{2};

    system.register_admin(gm, "GM", admin_level::owner);
    system.register_admin(target, "Target", admin_level::player);

    bool callback_called = false;
    system.on_level_changed([&callback_called](const admin_level_changed_event& event) {
        callback_called = true;
        EXPECT_EQ(event.old_level, admin_level::player);
        EXPECT_EQ(event.new_level, admin_level::moderator);
    });

    system.set_admin_level(target, admin_level::moderator, gm);
    EXPECT_TRUE(callback_called);
}

TEST_F(admin_system_test, total_commands_executed) {
    player_id gm{1};
    system.register_admin(gm, "GM", admin_level::game_master);

    size_t initial = system.total_commands_executed();

    system.execute(gm, "/help");
    system.execute(gm, "/who");
    system.execute(gm, "/help");

    EXPECT_EQ(system.total_commands_executed(), initial + 3);
}

TEST_F(admin_system_test, clear_log) {
    player_id gm{1};
    system.register_admin(gm, "GM", admin_level::game_master);

    system.execute(gm, "/help");
    EXPECT_GT(system.get_log_entries().size(), 0);

    system.clear_log();
    EXPECT_EQ(system.get_log_entries().size(), 0);
}

TEST_F(admin_system_test, unregister_command) {
    command_info info;
    info.name = "temptest";
    info.description = "Temporary";
    info.usage = "/temptest";
    info.required_level = admin_level::helper;

    system.register_command(info, [](const command_context&) {
        return command_result::ok("ok");
    });

    EXPECT_TRUE(system.has_command("temptest"));

    system.unregister_command("temptest");
    EXPECT_FALSE(system.has_command("temptest"));
}

// ========== Command Result Tests ==========

class command_result_test : public ::testing::Test {};

TEST_F(command_result_test, ok) {
    auto result = command_result::ok("success");
    EXPECT_TRUE(result.success);
    EXPECT_EQ(result.message, "success");
    EXPECT_FALSE(result.broadcast);
}

TEST_F(command_result_test, ok_broadcast) {
    auto result = command_result::ok_broadcast("broadcast message");
    EXPECT_TRUE(result.success);
    EXPECT_EQ(result.message, "broadcast message");
    EXPECT_TRUE(result.broadcast);
}

TEST_F(command_result_test, error) {
    auto result = command_result::error("failed");
    EXPECT_FALSE(result.success);
    EXPECT_EQ(result.message, "failed");
    EXPECT_FALSE(result.broadcast);
}
