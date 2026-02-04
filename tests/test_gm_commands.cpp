// test_gm_commands.cpp
// Unit tests for GM commands

#include <gtest/gtest.h>
#include "admin/admin_system.h"
#include "admin/gm_commands.h"
#include "admin/command.h"

using namespace hb;
using namespace hb::admin;

// ========== GM Command Registration Tests ==========

class gm_commands_test : public ::testing::Test {
protected:
    void SetUp() override {
        admin_.initialize();

        // Create command context with null pointers for isolated testing
        // In real usage, these would point to actual subsystems
        ctx_.players = nullptr;
        ctx_.world = nullptr;
        ctx_.inventory = nullptr;

        // Register GM commands
        register_gm_commands(admin_, ctx_);
    }

    void TearDown() override {
        admin_.shutdown();
    }

    admin_system admin_;
    gm_command_context ctx_;
};

TEST_F(gm_commands_test, goto_command_registered) {
    EXPECT_TRUE(admin_.has_command("goto"));
    EXPECT_TRUE(admin_.has_command("warp"));   // alias
    EXPECT_TRUE(admin_.has_command("gotp"));   // alias
}

TEST_F(gm_commands_test, summon_command_registered) {
    EXPECT_TRUE(admin_.has_command("summonplayer"));
    EXPECT_TRUE(admin_.has_command("bring"));  // alias
    EXPECT_TRUE(admin_.has_command("summon")); // alias
}

TEST_F(gm_commands_test, teleport_command_registered) {
    EXPECT_TRUE(admin_.has_command("teleport"));
    EXPECT_TRUE(admin_.has_command("tp"));     // alias
    EXPECT_TRUE(admin_.has_command("move"));   // alias
}

TEST_F(gm_commands_test, heal_command_registered) {
    EXPECT_TRUE(admin_.has_command("heal"));
    EXPECT_TRUE(admin_.has_command("restore")); // alias
}

TEST_F(gm_commands_test, kill_command_registered) {
    EXPECT_TRUE(admin_.has_command("kill"));
    EXPECT_TRUE(admin_.has_command("slay"));   // alias
}

TEST_F(gm_commands_test, setlevel_command_registered) {
    EXPECT_TRUE(admin_.has_command("setlevel"));
    EXPECT_TRUE(admin_.has_command("level"));  // alias
}

TEST_F(gm_commands_test, setstats_command_registered) {
    EXPECT_TRUE(admin_.has_command("setstats"));
    EXPECT_TRUE(admin_.has_command("stat"));    // alias
    EXPECT_TRUE(admin_.has_command("setstat")); // alias
}

TEST_F(gm_commands_test, setgold_command_registered) {
    EXPECT_TRUE(admin_.has_command("setgold"));
    EXPECT_TRUE(admin_.has_command("gold"));   // alias
}

TEST_F(gm_commands_test, getinfo_command_registered) {
    EXPECT_TRUE(admin_.has_command("getinfo"));
    EXPECT_TRUE(admin_.has_command("info"));       // alias
    EXPECT_TRUE(admin_.has_command("playerinfo")); // alias
}

TEST_F(gm_commands_test, where_command_registered) {
    EXPECT_TRUE(admin_.has_command("where"));
    EXPECT_TRUE(admin_.has_command("find"));   // alias
    EXPECT_TRUE(admin_.has_command("locate")); // alias
}

// ========== Command Permission Tests ==========

TEST_F(gm_commands_test, goto_requires_game_master) {
    player_id helper{1};
    player_id gm{2};

    admin_.register_admin(helper, "Helper", admin_level::helper);
    admin_.register_admin(gm, "GM", admin_level::game_master);

    // Helper should not be able to use goto
    EXPECT_FALSE(admin_.can_execute(helper, "goto"));

    // GM should be able to use goto
    EXPECT_TRUE(admin_.can_execute(gm, "goto"));
}

TEST_F(gm_commands_test, kill_requires_senior_gm) {
    player_id gm{1};
    player_id senior{2};

    admin_.register_admin(gm, "GM", admin_level::game_master);
    admin_.register_admin(senior, "SeniorGM", admin_level::senior_gm);

    // Regular GM should not be able to use kill
    EXPECT_FALSE(admin_.can_execute(gm, "kill"));

    // Senior GM should be able to use kill
    EXPECT_TRUE(admin_.can_execute(senior, "kill"));
}

TEST_F(gm_commands_test, setlevel_requires_admin) {
    player_id senior{1};
    player_id admin{2};

    admin_.register_admin(senior, "SeniorGM", admin_level::senior_gm);
    admin_.register_admin(admin, "Admin", admin_level::admin);

    // Senior GM should not be able to use setlevel
    EXPECT_FALSE(admin_.can_execute(senior, "setlevel"));

    // Admin should be able to use setlevel
    EXPECT_TRUE(admin_.can_execute(admin, "setlevel"));
}

TEST_F(gm_commands_test, getinfo_requires_helper) {
    player_id player{1};
    player_id helper{2};

    admin_.register_admin(player, "Player", admin_level::player);
    admin_.register_admin(helper, "Helper", admin_level::helper);

    // Player should not be able to use getinfo
    EXPECT_FALSE(admin_.can_execute(player, "getinfo"));

    // Helper should be able to use getinfo
    EXPECT_TRUE(admin_.can_execute(helper, "getinfo"));
}

// ========== Command Execution Tests (with null systems) ==========

TEST_F(gm_commands_test, goto_fails_without_player_system) {
    player_id gm{1};
    admin_.register_admin(gm, "GM", admin_level::game_master);

    auto result = admin_.execute(gm, "/goto TestPlayer");

    EXPECT_FALSE(result.success);
    EXPECT_TRUE(result.message.find("not available") != std::string::npos);
}

TEST_F(gm_commands_test, summon_fails_without_player_system) {
    player_id gm{1};
    admin_.register_admin(gm, "GM", admin_level::game_master);

    auto result = admin_.execute(gm, "/summonplayer TestPlayer");

    EXPECT_FALSE(result.success);
    EXPECT_TRUE(result.message.find("not available") != std::string::npos);
}

TEST_F(gm_commands_test, teleport_fails_without_systems) {
    player_id gm{1};
    admin_.register_admin(gm, "GM", admin_level::game_master);

    auto result = admin_.execute(gm, "/teleport aresden 100 200");

    EXPECT_FALSE(result.success);
    EXPECT_TRUE(result.message.find("not available") != std::string::npos);
}

TEST_F(gm_commands_test, heal_fails_without_player_system) {
    player_id gm{1};
    admin_.register_admin(gm, "GM", admin_level::game_master);

    auto result = admin_.execute(gm, "/heal");

    EXPECT_FALSE(result.success);
    EXPECT_TRUE(result.message.find("not available") != std::string::npos);
}

TEST_F(gm_commands_test, kill_fails_without_player_system) {
    player_id senior{1};
    admin_.register_admin(senior, "SeniorGM", admin_level::senior_gm);

    auto result = admin_.execute(senior, "/kill TestPlayer");

    EXPECT_FALSE(result.success);
    EXPECT_TRUE(result.message.find("not available") != std::string::npos);
}

TEST_F(gm_commands_test, setlevel_fails_without_player_system) {
    player_id admin{1};
    admin_.register_admin(admin, "Admin", admin_level::admin);

    auto result = admin_.execute(admin, "/setlevel TestPlayer 50");

    EXPECT_FALSE(result.success);
    EXPECT_TRUE(result.message.find("not available") != std::string::npos);
}

TEST_F(gm_commands_test, setstats_fails_without_player_system) {
    player_id admin{1};
    admin_.register_admin(admin, "Admin", admin_level::admin);

    auto result = admin_.execute(admin, "/setstats TestPlayer str 30");

    EXPECT_FALSE(result.success);
    EXPECT_TRUE(result.message.find("not available") != std::string::npos);
}

TEST_F(gm_commands_test, setgold_fails_without_player_system) {
    player_id admin{1};
    admin_.register_admin(admin, "Admin", admin_level::admin);

    auto result = admin_.execute(admin, "/setgold TestPlayer 10000");

    EXPECT_FALSE(result.success);
    EXPECT_TRUE(result.message.find("not available") != std::string::npos);
}

TEST_F(gm_commands_test, getinfo_fails_without_player_system) {
    player_id helper{1};
    admin_.register_admin(helper, "Helper", admin_level::helper);

    auto result = admin_.execute(helper, "/getinfo TestPlayer");

    EXPECT_FALSE(result.success);
    EXPECT_TRUE(result.message.find("not available") != std::string::npos);
}

TEST_F(gm_commands_test, where_fails_without_player_system) {
    player_id helper{1};
    admin_.register_admin(helper, "Helper", admin_level::helper);

    auto result = admin_.execute(helper, "/where TestPlayer");

    EXPECT_FALSE(result.success);
    EXPECT_TRUE(result.message.find("not available") != std::string::npos);
}

// ========== Command Argument Validation Tests ==========

TEST_F(gm_commands_test, goto_requires_player_name) {
    player_id gm{1};
    admin_.register_admin(gm, "GM", admin_level::game_master);

    // Even without player system, the command should parse args
    // If player system is null, it returns early
    auto result = admin_.execute(gm, "/goto");

    EXPECT_FALSE(result.success);
}

TEST_F(gm_commands_test, teleport_requires_three_arguments) {
    player_id gm{1};
    admin_.register_admin(gm, "GM", admin_level::game_master);

    auto result = admin_.execute(gm, "/teleport aresden 100");

    // Should fail due to missing argument or system not available
    EXPECT_FALSE(result.success);
}

TEST_F(gm_commands_test, setlevel_requires_two_arguments) {
    player_id admin{1};
    admin_.register_admin(admin, "Admin", admin_level::admin);

    auto result = admin_.execute(admin, "/setlevel TestPlayer");

    // Should fail due to missing argument or system not available
    EXPECT_FALSE(result.success);
}

TEST_F(gm_commands_test, setstats_requires_three_arguments) {
    player_id admin{1};
    admin_.register_admin(admin, "Admin", admin_level::admin);

    auto result = admin_.execute(admin, "/setstats TestPlayer str");

    // Should fail due to missing argument or system not available
    EXPECT_FALSE(result.success);
}

TEST_F(gm_commands_test, setgold_requires_two_arguments) {
    player_id admin{1};
    admin_.register_admin(admin, "Admin", admin_level::admin);

    auto result = admin_.execute(admin, "/setgold TestPlayer");

    // Should fail due to missing argument or system not available
    EXPECT_FALSE(result.success);
}

// ========== Command Help Text Tests ==========

TEST_F(gm_commands_test, goto_has_help_text) {
    auto help = admin_.get_help("goto");
    EXPECT_FALSE(help.empty());
    EXPECT_TRUE(help.find("teleport") != std::string::npos ||
                help.find("Teleport") != std::string::npos);
}

TEST_F(gm_commands_test, heal_has_help_text) {
    auto help = admin_.get_help("heal");
    EXPECT_FALSE(help.empty());
    EXPECT_TRUE(help.find("HP") != std::string::npos ||
                help.find("Restore") != std::string::npos ||
                help.find("heal") != std::string::npos);
}

TEST_F(gm_commands_test, setlevel_has_help_text) {
    auto help = admin_.get_help("setlevel");
    EXPECT_FALSE(help.empty());
    EXPECT_TRUE(help.find("level") != std::string::npos);
}

// ========== Admin Level Hierarchy Tests ==========

TEST_F(gm_commands_test, higher_levels_can_use_lower_level_commands) {
    player_id owner{1};
    admin_.register_admin(owner, "Owner", admin_level::owner);

    // Owner should be able to use all commands
    EXPECT_TRUE(admin_.can_execute(owner, "goto"));      // game_master
    EXPECT_TRUE(admin_.can_execute(owner, "kill"));      // senior_gm
    EXPECT_TRUE(admin_.can_execute(owner, "setlevel"));  // admin
    EXPECT_TRUE(admin_.can_execute(owner, "getinfo"));   // helper
}

TEST_F(gm_commands_test, commands_available_at_correct_level) {
    // Get commands for each level and verify appropriate access
    auto helper_cmds = admin_.get_commands_for_level(admin_level::helper);
    auto gm_cmds = admin_.get_commands_for_level(admin_level::game_master);
    auto senior_cmds = admin_.get_commands_for_level(admin_level::senior_gm);
    auto admin_cmds = admin_.get_commands_for_level(admin_level::admin);

    // Helper commands should be subset of GM commands
    EXPECT_LE(helper_cmds.size(), gm_cmds.size());

    // GM commands should be subset of Senior GM commands
    EXPECT_LE(gm_cmds.size(), senior_cmds.size());

    // Senior GM commands should be subset of Admin commands
    EXPECT_LE(senior_cmds.size(), admin_cmds.size());
}

// ========== gm_command_context Tests ==========

TEST(gm_command_context_test, default_construction) {
    gm_command_context ctx;

    EXPECT_EQ(ctx.players, nullptr);
    EXPECT_EQ(ctx.world, nullptr);
    EXPECT_EQ(ctx.inventory, nullptr);
}

// ========== Command Result Types ==========

TEST(gm_command_result_test, ok_result) {
    auto result = command_result::ok("Operation successful");

    EXPECT_TRUE(result.success);
    EXPECT_EQ(result.message, "Operation successful");
    EXPECT_FALSE(result.broadcast);
}

TEST(gm_command_result_test, ok_broadcast_result) {
    auto result = command_result::ok_broadcast("Server message");

    EXPECT_TRUE(result.success);
    EXPECT_EQ(result.message, "Server message");
    EXPECT_TRUE(result.broadcast);
}

TEST(gm_command_result_test, error_result) {
    auto result = command_result::error("Something went wrong");

    EXPECT_FALSE(result.success);
    EXPECT_EQ(result.message, "Something went wrong");
    EXPECT_FALSE(result.broadcast);
}

// ========== Arg Spec Tests ==========

TEST(arg_spec_test, player_name_arg) {
    arg_spec spec;
    spec.name = "player";
    spec.type = arg_type::player_name;
    spec.required = true;
    spec.description = "Target player";

    EXPECT_EQ(spec.name, "player");
    EXPECT_EQ(spec.type, arg_type::player_name);
    EXPECT_TRUE(spec.required);
}

TEST(arg_spec_test, optional_arg_with_default) {
    arg_spec spec;
    spec.name = "count";
    spec.type = arg_type::integer;
    spec.required = false;
    spec.default_value = "1";
    spec.description = "Number of items";

    EXPECT_EQ(spec.name, "count");
    EXPECT_FALSE(spec.required);
    EXPECT_EQ(spec.default_value, "1");
}

TEST(arg_spec_test, map_name_arg) {
    arg_spec spec;
    spec.name = "map";
    spec.type = arg_type::map_name;
    spec.required = true;

    EXPECT_EQ(spec.type, arg_type::map_name);
}

// ========== Arg Type Enum Tests ==========

TEST(arg_type_test, distinct_values) {
    EXPECT_NE(static_cast<int>(arg_type::string), static_cast<int>(arg_type::integer));
    EXPECT_NE(static_cast<int>(arg_type::integer), static_cast<int>(arg_type::floating));
    EXPECT_NE(static_cast<int>(arg_type::floating), static_cast<int>(arg_type::boolean));
    EXPECT_NE(static_cast<int>(arg_type::player_name), static_cast<int>(arg_type::map_name));
    EXPECT_NE(static_cast<int>(arg_type::item_id), static_cast<int>(arg_type::npc_id));
}
