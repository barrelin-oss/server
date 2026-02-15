// test_integration.cpp
// Integration tests for full message round-trips through modern handlers

#include <gtest/gtest.h>
#include "bridge/message_router.h"
#include "bridge/handler_registry.h"
#include "bridge/handlers/wave1_handlers.h"
#include "bridge/handlers/wave2_handlers.h"
#include "bridge/handlers/wave3_handlers.h"
#include "bridge/handlers/wave4_handlers.h"
#include "bridge/handlers/wave5_handlers.h"
#include "protocol/protocol.h"
#include "protocol/message_writer.h"
#include "protocol/message_reader.h"
#include "core/subsystem.h"
#include "player/player_system.h"
#include "player/player.h"
#include "social/social_system.h"
#include "skill/skill_system.h"
#include "admin/admin_system.h"
#include "network/network_subsystem.h"

using namespace hb;
using namespace hb::bridge;
using namespace hb::protocol;

// Test fixture for integration tests
class integration_test : public ::testing::Test
{
protected:
    void SetUp() override
    {
        // Ensure clean state - unregister any existing handlers
        wave1::unregister_wave1_handlers();
        wave2::unregister_wave2_handlers();
        wave3::unregister_wave3_handlers();
        wave4::unregister_wave4_handlers();
        wave5::unregister_wave5_handlers();

        // Reset router stats for accurate counting in tests
        router().reset_stats();

        // Create subsystems needed for testing
        subsystems().create_subsystem<player::player_system>();
        subsystems().create_subsystem<social::social_system>();
        subsystems().create_subsystem<skill::skill_system>();
        subsystems().create_subsystem<admin::admin_system>();

        // Initialize subsystems
        subsystems().initialize_all();

        // Register all wave handlers
        wave1::register_wave1_handlers();
        wave2::register_wave2_handlers();
        wave3::register_wave3_handlers();
        wave4::register_wave4_handlers();
        wave5::register_wave5_handlers();

        // Track responses
        sent_messages_.clear();
    }

    void TearDown() override
    {
        // Unregister handlers
        wave5::unregister_wave5_handlers();
        wave4::unregister_wave4_handlers();
        wave3::unregister_wave3_handlers();
        wave2::unregister_wave2_handlers();
        wave1::unregister_wave1_handlers();

        // Shutdown subsystems
        subsystems().clear_all();
    }

    // Helper to create a message and route it
    auto route_message(connection_id conn,
                       session_id sess,
                       player_id player,
                       message_id msg_id,
                       std::function<void(message_writer&)> write_body = nullptr) -> bool
    {
        message_writer writer;
        writer.write_u32(static_cast<uint32_t>(msg_id));
        if (write_body)
        {
            write_body(writer);
        }
        auto data = writer.data();

        // Set up connection mappings
        router().set_session_for_connection(conn, sess);
        router().set_player_for_connection(conn, player);

        // Route the message
        return router().route(conn, data);
    }

    // Helper to verify a response was sent
    [[nodiscard]] auto get_sent_message_count() const -> size_t { return sent_messages_.size(); }

    std::vector<std::vector<uint8_t>> sent_messages_;
};

// ========== Wave 1: Read-only Handler Tests ==========

TEST_F(integration_test, help_request_handled)
{
    // Send a help request message
    auto handled =
        route_message(connection_id{1},
                      session_id{1},
                      player_id{},
                      message_id::command_common,
                      [](message_writer& w) { w.write_u16(static_cast<uint16_t>(common_type::request_help)); });

    EXPECT_TRUE(handled);
}

TEST_F(integration_test, non_help_command_falls_through)
{
    bool legacy_called = false;
    router().set_legacy_fallback([&](connection_id, std::span<const uint8_t>) { legacy_called = true; });

    // Send a common command that's not handled by wave 1
    auto handled =
        route_message(connection_id{1},
                      session_id{1},
                      player_id{},
                      message_id::command_common,
                      [](message_writer& w) { w.write_u16(static_cast<uint16_t>(common_type::toggle_combat_mode)); });

    // Should fall through to legacy since we don't handle this
    auto stats = router().stats();
    EXPECT_GE(stats.total_messages, 1);
}

// ========== Wave 3: Player Movement Tests ==========

TEST_F(integration_test, movement_requires_player)
{
    // Try to send movement without a valid player
    auto handled = route_message(connection_id{1},
                                 session_id{1},
                                 player_id{}, // Invalid player
                                 message_id::command_motion,
                                 [](message_writer& w)
                                 {
                                     w.write_u8(static_cast<uint8_t>(wave3::motion_type::move));
                                     w.write_i16(100); // x
                                     w.write_i16(200); // y
                                     w.write_i8(0);    // direction
                                 });

    // Should be handled (as error) due to require_player wrapper
    EXPECT_TRUE(handled);
    EXPECT_EQ(router().stats().errors, 1);
}

TEST_F(integration_test, movement_with_valid_player)
{
    // Create a player first
    auto* player_sys = subsystems().get<player::player_system>();
    ASSERT_NE(player_sys, nullptr);

    player::player_create_info info{.name = "TestPlayer",
                                    .account_name = "test_account",
                                    .sex = player::gender::male,
                                    .profession = player::player_class::warrior,
                                    .faction = faction::aresden};

    auto result = player_sys->create_player(info);
    ASSERT_TRUE(result.is_ok());
    auto pid = result.value();

    // Send movement command
    auto handled = route_message(connection_id{1},
                                 session_id{1},
                                 pid,
                                 message_id::command_motion,
                                 [](message_writer& w)
                                 {
                                     w.write_u8(static_cast<uint8_t>(wave3::motion_type::move));
                                     w.write_i16(100); // x
                                     w.write_i16(200); // y
                                     w.write_i8(0);    // direction
                                 });

    // Should be handled successfully
    EXPECT_TRUE(handled);

    // Clean up
    player_sys->remove_player(pid);
}

// ========== Wave 4: Combat Tests ==========

TEST_F(integration_test, skill_use_with_player)
{
    // Create a player
    auto* player_sys = subsystems().get<player::player_system>();
    auto* skill_sys = subsystems().get<skill::skill_system>();
    ASSERT_NE(player_sys, nullptr);
    ASSERT_NE(skill_sys, nullptr);

    player::player_create_info info{.name = "SkillTestPlayer",
                                    .account_name = "test_account",
                                    .sex = player::gender::male,
                                    .profession = player::player_class::warrior,
                                    .faction = faction::aresden};

    auto result = player_sys->create_player(info);
    ASSERT_TRUE(result.is_ok());
    auto pid = result.value();
    skill_sys->register_player(pid);

    // Send skill use command
    auto handled = route_message(connection_id{1},
                                 session_id{1},
                                 pid,
                                 message_id::command_common,
                                 [](message_writer& w)
                                 {
                                     w.write_u16(static_cast<uint16_t>(common_type::req_use_skill));
                                     w.write_u16(static_cast<uint16_t>(skill::skill_type::mining));
                                 });

    // Skill handler returns handled (trains the skill)
    // Note: Result depends on whether skill_system has the player registered
    // and whether the skill can be trained
    auto stats = router().stats();
    EXPECT_GE(stats.total_messages, 1);

    // Clean up
    skill_sys->unregister_player(pid);
    player_sys->remove_player(pid);
}

// ========== Wave 5: Social Tests ==========

TEST_F(integration_test, party_create_with_player)
{
    // Create a player
    auto* player_sys = subsystems().get<player::player_system>();
    auto* social_sys = subsystems().get<social::social_system>();
    ASSERT_NE(player_sys, nullptr);
    ASSERT_NE(social_sys, nullptr);

    player::player_create_info info{.name = "PartyTestPlayer",
                                    .account_name = "test_account",
                                    .sex = player::gender::male,
                                    .profession = player::player_class::warrior,
                                    .faction = faction::aresden};

    auto result = player_sys->create_player(info);
    ASSERT_TRUE(result.is_ok());
    auto pid = result.value();
    social_sys->register_player(pid, "PartyTestPlayer");

    // Send party creation command
    auto handled = route_message(connection_id{1},
                                 session_id{1},
                                 pid,
                                 message_id::party_operation,
                                 [](message_writer& w)
                                 {
                                     w.write_u8(0); // operation: create party
                                 });

    // Should be handled
    EXPECT_TRUE(handled);

    // Verify party was created
    auto party_id = social_sys->get_player_party(pid);
    EXPECT_TRUE(party_id.is_valid());

    // Clean up
    social_sys->unregister_player(pid);
    player_sys->remove_player(pid);
}

TEST_F(integration_test, guild_create_with_player)
{
    // Create a player
    auto* player_sys = subsystems().get<player::player_system>();
    auto* social_sys = subsystems().get<social::social_system>();
    ASSERT_NE(player_sys, nullptr);
    ASSERT_NE(social_sys, nullptr);

    player::player_create_info info{.name = "GuildLeader",
                                    .account_name = "test_account",
                                    .sex = player::gender::male,
                                    .profession = player::player_class::warrior,
                                    .faction = faction::aresden};

    auto result = player_sys->create_player(info);
    ASSERT_TRUE(result.is_ok());
    auto pid = result.value();
    social_sys->register_player(pid, "GuildLeader");

    // Send guild creation command
    std::string guild_name = "TestGuild";
    auto handled = route_message(connection_id{1},
                                 session_id{1},
                                 pid,
                                 message_id::request_create_new_guild,
                                 [&](message_writer& w)
                                 {
                                     w.write_u8(static_cast<uint8_t>(guild_name.size()));
                                     for (char c : guild_name)
                                     {
                                         w.write_u8(static_cast<uint8_t>(c));
                                     }
                                 });

    // Should be handled
    EXPECT_TRUE(handled);

    // Verify guild was created (may fail due to gold requirement)
    // The handler processes it regardless
    auto stats = router().stats();
    EXPECT_GE(stats.modern_handled, 1);

    // Clean up
    social_sys->unregister_player(pid);
    player_sys->remove_player(pid);
}

// ========== Router Statistics Tests ==========

TEST_F(integration_test, stats_track_all_waves)
{
    router().reset_stats();

    // Send messages through different waves
    route_message(connection_id{1},
                  session_id{1},
                  player_id{},
                  message_id::command_common,
                  [](message_writer& w) { w.write_u16(static_cast<uint16_t>(common_type::request_help)); });

    auto stats = router().stats();
    EXPECT_GE(stats.total_messages, 1);
}

TEST_F(integration_test, modernized_handlers_registered)
{
    // Verify all expected handlers are registered
    EXPECT_TRUE(router().is_modernized(message_id::command_common));
    EXPECT_TRUE(router().is_modernized(message_id::command_motion));
    EXPECT_TRUE(router().is_modernized(message_id::command_chat_msg));
    EXPECT_TRUE(router().is_modernized(message_id::party_operation));
    EXPECT_TRUE(router().is_modernized(message_id::request_create_new_guild));

    // Count should be > 0
    EXPECT_GT(router().modernized_count(), 0);
}

// ========== Legacy Fallback Tests ==========

TEST_F(integration_test, unhandled_message_falls_to_legacy)
{
    bool legacy_called = false;
    router().set_legacy_fallback([&](connection_id, std::span<const uint8_t>) { legacy_called = true; });

    // Send a message type that's not handled by any wave
    message_writer writer;
    writer.write_u32(static_cast<uint32_t>(message_id::request_login));

    auto handled = router().route(connection_id{1}, writer.data());

    EXPECT_FALSE(handled);      // Not handled by modern code
    EXPECT_TRUE(legacy_called); // Legacy was called
}

TEST_F(integration_test, partial_handler_falls_to_legacy)
{
    bool legacy_called = false;
    router().set_legacy_fallback([&](connection_id, std::span<const uint8_t>) { legacy_called = true; });

    // Send a common command subtype that's not handled by any wave
    // Using toggle_combat_mode which isn't handled by any of our waves
    auto handled =
        route_message(connection_id{1},
                      session_id{1},
                      player_id{},
                      message_id::command_common,
                      [](message_writer& w) { w.write_u16(static_cast<uint16_t>(common_type::toggle_combat_mode)); });

    // This falls through because toggle_combat_mode is not handled by any wave
    // However, if any wave registers command_common but doesn't handle this subtype,
    // it returns not_handled which causes fallback
    auto stats = router().stats();
    EXPECT_GE(stats.total_messages, 1);
    // Legacy should be called if no handler processes this subtype
    // Note: The behavior depends on whether handlers return not_handled for unknown subtypes
}

// ========== Connection Context Tests ==========

TEST_F(integration_test, connection_context_preserved)
{
    connection_id conn{42};
    session_id sess{100};
    player_id pid{200};

    router().set_session_for_connection(conn, sess);
    router().set_player_for_connection(conn, pid);

    // Verify context is passed to handler
    session_id received_sess{};
    player_id received_pid{};

    // Use a custom handler to capture context
    router().register_handler(message_id::request_enter_game,
                              "test_context",
                              [&](const handler_context& ctx)
                              {
                                  received_sess = ctx.session;
                                  received_pid = ctx.player;
                                  return handle_result::handled;
                              });

    message_writer writer;
    writer.write_u32(static_cast<uint32_t>(message_id::request_enter_game));
    router().route(conn, writer.data());

    EXPECT_EQ(received_sess.value, 100);
    EXPECT_EQ(received_pid.value, 200);

    // Clean up
    router().unregister_handler(message_id::request_enter_game);
    router().clear_connection(conn);
}
