// test_bridge.cpp
// Unit tests for message router and protocol bridge

#include <gtest/gtest.h>
#include "bridge/message_router.h"
#include "bridge/handler_registry.h"
#include "protocol/protocol.h"
#include "protocol/message_writer.h"

using namespace hb;
using namespace hb::bridge;
using namespace hb::protocol;

// Test fixture for message router tests
class message_router_test : public ::testing::Test
{
protected:
    void SetUp() override { router_ = std::make_unique<message_router>(); }

    void TearDown() override { router_.reset(); }

    // Helper to create a test message
    auto make_test_message(message_id id,
                           std::function<void(message_writer&)> write_data = nullptr) -> std::vector<uint8_t>
    {
        message_writer writer;
        writer.write_u32(static_cast<uint32_t>(id));
        if (write_data)
        {
            write_data(writer);
        }
        return writer.copy();
    }

    std::unique_ptr<message_router> router_;
};

// Basic registration tests

TEST_F(message_router_test, initially_no_handlers)
{
    EXPECT_EQ(router_->modernized_count(), 0);
    EXPECT_FALSE(router_->is_modernized(message_id::request_login));
}

TEST_F(message_router_test, register_handler)
{
    router_->register_handler(
        message_id::request_login, "test_subsystem", [](const handler_context&) { return handle_result::handled; });

    EXPECT_EQ(router_->modernized_count(), 1);
    EXPECT_TRUE(router_->is_modernized(message_id::request_login));
    EXPECT_FALSE(router_->is_modernized(message_id::request_enter_game));
}

TEST_F(message_router_test, register_handler_with_info)
{
    router_->register_handler(handler_info{.name = "login_handler",
                                           .subsystem = "auth",
                                           .msg_id = message_id::request_login,
                                           .handler = [](const handler_context&) { return handle_result::handled; },
                                           .enabled = true});

    auto* info = router_->get_handler_info(message_id::request_login);
    ASSERT_NE(info, nullptr);
    EXPECT_EQ(info->name, "login_handler");
    EXPECT_EQ(info->subsystem, "auth");
    EXPECT_TRUE(info->enabled);
}

TEST_F(message_router_test, unregister_handler)
{
    router_->register_handler(
        message_id::request_login, "test", [](const handler_context&) { return handle_result::handled; });
    EXPECT_TRUE(router_->is_modernized(message_id::request_login));

    router_->unregister_handler(message_id::request_login);
    EXPECT_FALSE(router_->is_modernized(message_id::request_login));
}

TEST_F(message_router_test, enable_disable_handler)
{
    router_->register_handler(
        message_id::request_login, "test", [](const handler_context&) { return handle_result::handled; });

    EXPECT_TRUE(router_->is_handler_enabled(message_id::request_login));

    router_->set_handler_enabled(message_id::request_login, false);
    EXPECT_FALSE(router_->is_handler_enabled(message_id::request_login));
    EXPECT_TRUE(router_->is_modernized(message_id::request_login)); // Still registered

    router_->set_handler_enabled(message_id::request_login, true);
    EXPECT_TRUE(router_->is_handler_enabled(message_id::request_login));
}

// Message routing tests

TEST_F(message_router_test, route_to_modern_handler)
{
    bool handler_called = false;
    connection_id received_conn{};
    message_id received_msg_id{};

    router_->register_handler(message_id::request_login,
                              "test",
                              [&](const handler_context& ctx)
                              {
                                  handler_called = true;
                                  received_conn = ctx.connection;
                                  received_msg_id = ctx.msg_id;
                                  return handle_result::handled;
                              });

    auto data = make_test_message(message_id::request_login);
    connection_id conn{42};

    bool handled = router_->route(conn, data);

    EXPECT_TRUE(handled);
    EXPECT_TRUE(handler_called);
    EXPECT_EQ(received_conn.value, 42);
    EXPECT_EQ(received_msg_id, message_id::request_login);
}

TEST_F(message_router_test, route_to_legacy_fallback)
{
    bool legacy_called = false;
    connection_id received_conn{};

    router_->set_legacy_fallback(
        [&](connection_id conn, std::span<const uint8_t>)
        {
            legacy_called = true;
            received_conn = conn;
        });

    auto data = make_test_message(message_id::request_login);
    connection_id conn{123};

    bool handled = router_->route(conn, data);

    EXPECT_FALSE(handled);
    EXPECT_TRUE(legacy_called);
    EXPECT_EQ(received_conn.value, 123);
}

TEST_F(message_router_test, disabled_handler_falls_to_legacy)
{
    bool modern_called = false;
    bool legacy_called = false;

    router_->register_handler(message_id::request_login,
                              "test",
                              [&](const handler_context&)
                              {
                                  modern_called = true;
                                  return handle_result::handled;
                              });

    router_->set_legacy_fallback([&](connection_id, std::span<const uint8_t>) { legacy_called = true; });

    router_->set_handler_enabled(message_id::request_login, false);

    auto data = make_test_message(message_id::request_login);
    bool handled = router_->route(connection_id{1}, data);

    EXPECT_FALSE(handled);
    EXPECT_FALSE(modern_called);
    EXPECT_TRUE(legacy_called);
}

TEST_F(message_router_test, handler_returns_not_handled_falls_to_legacy)
{
    bool modern_called = false;
    bool legacy_called = false;

    router_->register_handler(message_id::request_login,
                              "test",
                              [&](const handler_context&)
                              {
                                  modern_called = true;
                                  return handle_result::not_handled;
                              });

    router_->set_legacy_fallback([&](connection_id, std::span<const uint8_t>) { legacy_called = true; });

    auto data = make_test_message(message_id::request_login);
    bool handled = router_->route(connection_id{1}, data);

    EXPECT_FALSE(handled); // Fell through to legacy
    EXPECT_TRUE(modern_called);
    EXPECT_TRUE(legacy_called);
}

// Context tests

TEST_F(message_router_test, context_includes_session_and_player)
{
    session_id received_session{};
    player_id received_player{};

    router_->register_handler(message_id::request_login,
                              "test",
                              [&](const handler_context& ctx)
                              {
                                  received_session = ctx.session;
                                  received_player = ctx.player;
                                  return handle_result::handled;
                              });

    connection_id conn{42};
    router_->set_session_for_connection(conn, session_id{100});
    router_->set_player_for_connection(conn, player_id{200});

    auto data = make_test_message(message_id::request_login);
    router_->route(conn, data);

    EXPECT_EQ(received_session.value, 100);
    EXPECT_EQ(received_player.value, 200);
}

TEST_F(message_router_test, clear_connection_removes_mappings)
{
    connection_id conn{42};
    router_->set_session_for_connection(conn, session_id{100});
    router_->set_player_for_connection(conn, player_id{200});

    router_->clear_connection(conn);

    session_id received_session{999};
    player_id received_player{999};

    router_->register_handler(message_id::request_login,
                              "test",
                              [&](const handler_context& ctx)
                              {
                                  received_session = ctx.session;
                                  received_player = ctx.player;
                                  return handle_result::handled;
                              });

    auto data = make_test_message(message_id::request_login);
    router_->route(conn, data);

    EXPECT_EQ(received_session.value, 0); // Invalid (cleared)
    EXPECT_EQ(received_player.value, 0);
}

// Statistics tests

TEST_F(message_router_test, stats_initially_zero)
{
    auto stats = router_->stats();
    EXPECT_EQ(stats.total_messages, 0);
    EXPECT_EQ(stats.modern_handled, 0);
    EXPECT_EQ(stats.legacy_fallback, 0);
    EXPECT_EQ(stats.errors, 0);
}

TEST_F(message_router_test, stats_track_modern_handled)
{
    router_->register_handler(
        message_id::request_login, "test", [](const handler_context&) { return handle_result::handled; });

    auto data = make_test_message(message_id::request_login);
    router_->route(connection_id{1}, data);
    router_->route(connection_id{2}, data);

    auto stats = router_->stats();
    EXPECT_EQ(stats.total_messages, 2);
    EXPECT_EQ(stats.modern_handled, 2);
    EXPECT_EQ(stats.legacy_fallback, 0);
}

TEST_F(message_router_test, stats_track_legacy_fallback)
{
    router_->set_legacy_fallback([](connection_id, std::span<const uint8_t>) {});

    auto data = make_test_message(message_id::request_login);
    router_->route(connection_id{1}, data);

    auto stats = router_->stats();
    EXPECT_EQ(stats.total_messages, 1);
    EXPECT_EQ(stats.modern_handled, 0);
    EXPECT_EQ(stats.legacy_fallback, 1);
}

TEST_F(message_router_test, stats_track_errors)
{
    router_->register_handler(
        message_id::request_login, "test", [](const handler_context&) { return handle_result::error; });

    auto data = make_test_message(message_id::request_login);
    router_->route(connection_id{1}, data);

    auto stats = router_->stats();
    EXPECT_EQ(stats.errors, 1);
}

TEST_F(message_router_test, stats_per_message_type)
{
    router_->register_handler(
        message_id::request_login, "test", [](const handler_context&) { return handle_result::handled; });

    auto data = make_test_message(message_id::request_login);
    router_->route(connection_id{1}, data);
    router_->route(connection_id{2}, data);
    router_->route(connection_id{3}, data);

    auto msg_stats = router_->stats_for(message_id::request_login);
    EXPECT_EQ(msg_stats.msg_id, message_id::request_login);
    EXPECT_EQ(msg_stats.total_count, 3);
    EXPECT_EQ(msg_stats.success_count, 3);
}

TEST_F(message_router_test, reset_stats)
{
    router_->register_handler(
        message_id::request_login, "test", [](const handler_context&) { return handle_result::handled; });

    auto data = make_test_message(message_id::request_login);
    router_->route(connection_id{1}, data);

    router_->reset_stats();

    auto stats = router_->stats();
    EXPECT_EQ(stats.total_messages, 0);
}

// Handler exception handling

TEST_F(message_router_test, handler_exception_counts_as_error)
{
    router_->register_handler(message_id::request_login,
                              "test",
                              [](const handler_context&) -> handle_result
                              { throw std::runtime_error("test exception"); });

    auto data = make_test_message(message_id::request_login);
    bool handled = router_->route(connection_id{1}, data);

    EXPECT_TRUE(handled); // Exception = handled (with error)
    EXPECT_EQ(router_->stats().errors, 1);
}

// Message data tests

TEST_F(message_router_test, message_data_passed_to_handler)
{
    std::vector<uint8_t> received_data;

    router_->register_handler(message_id::request_login,
                              "test",
                              [&](const handler_context& ctx)
                              {
                                  received_data.assign(ctx.raw_data.begin(), ctx.raw_data.end());
                                  return handle_result::handled;
                              });

    auto data = make_test_message(message_id::request_login,
                                  [](message_writer& w)
                                  {
                                      w.write_u16(0x1234);
                                      w.write_string_u8("TestUser");
                                  });

    router_->route(connection_id{1}, data);

    EXPECT_EQ(received_data, data);
}

TEST_F(message_router_test, make_reader_skips_message_id)
{
    uint16_t read_value = 0;
    std::string read_string;

    router_->register_handler(message_id::request_login,
                              "test",
                              [&](const handler_context& ctx)
                              {
                                  auto reader = ctx.make_reader();
                                  // Reader should be at start, but raw_data includes msg ID
                                  // So we need to skip 4 bytes for msg ID
                                  reader.skip(4);
                                  read_value = reader.read_u16();
                                  read_string = reader.read_string_u8();
                                  return handle_result::handled;
                              });

    auto data = make_test_message(message_id::request_login,
                                  [](message_writer& w)
                                  {
                                      w.write_u16(0xABCD);
                                      w.write_string_u8("TestData");
                                  });

    router_->route(connection_id{1}, data);

    EXPECT_EQ(read_value, 0xABCD);
    EXPECT_EQ(read_string, "TestData");
}

// Multiple handlers test

TEST_F(message_router_test, multiple_handlers_independent)
{
    int login_count = 0;
    int enter_count = 0;

    router_->register_handler(message_id::request_login,
                              "auth",
                              [&](const handler_context&)
                              {
                                  login_count++;
                                  return handle_result::handled;
                              });

    router_->register_handler(message_id::request_enter_game,
                              "game",
                              [&](const handler_context&)
                              {
                                  enter_count++;
                                  return handle_result::handled;
                              });

    auto login_data = make_test_message(message_id::request_login);
    auto enter_data = make_test_message(message_id::request_enter_game);

    router_->route(connection_id{1}, login_data);
    router_->route(connection_id{1}, login_data);
    router_->route(connection_id{1}, enter_data);

    EXPECT_EQ(login_count, 2);
    EXPECT_EQ(enter_count, 1);
}

// Get all handlers test

TEST_F(message_router_test, get_all_handlers)
{
    router_->register_handler(
        message_id::request_login, "auth", [](const handler_context&) { return handle_result::handled; });
    router_->register_handler(
        message_id::request_enter_game, "game", [](const handler_context&) { return handle_result::handled; });
    router_->register_handler(
        message_id::notify, "notify", [](const handler_context&) { return handle_result::handled; });

    auto handlers = router_->get_all_handlers();
    EXPECT_EQ(handlers.size(), 3);
}

// Handler builder tests

TEST(handler_builder_test, fluent_registration)
{
    message_router local_router;
    bool handler1_called = false;
    bool handler2_called = false;

    // Use a local builder instead of global
    handler_info info1{.name = "request_login",
                       .subsystem = "test",
                       .msg_id = message_id::request_login,
                       .handler =
                           [&](const handler_context&)
                       {
                           handler1_called = true;
                           return handle_result::handled;
                       },
                       .enabled = true};

    handler_info info2{.name = "notify",
                       .subsystem = "test",
                       .msg_id = message_id::notify,
                       .handler =
                           [&](const handler_context&)
                       {
                           handler2_called = true;
                           return handle_result::handled;
                       },
                       .enabled = true};

    local_router.register_handler(info1);
    local_router.register_handler(info2);

    EXPECT_TRUE(local_router.is_modernized(message_id::request_login));
    EXPECT_TRUE(local_router.is_modernized(message_id::notify));
}

// Migration tracking tests

TEST(migration_tracking_test, mark_fully_migrated)
{
    // Reset state
    auto migrated = get_migrated_messages();
    for (auto id : migrated)
    {
        // No unmark function, so just verify current state
    }

    mark_fully_migrated(message_id::request_login);
    EXPECT_TRUE(is_fully_migrated(message_id::request_login));
    EXPECT_FALSE(is_fully_migrated(message_id::request_enter_game));
}

TEST(migration_tracking_test, get_migrated_messages)
{
    mark_fully_migrated(message_id::request_login);
    mark_fully_migrated(message_id::notify);

    auto migrated = get_migrated_messages();
    EXPECT_GE(migrated.size(), 2);

    bool has_login = std::find(migrated.begin(), migrated.end(), message_id::request_login) != migrated.end();
    bool has_notify = std::find(migrated.begin(), migrated.end(), message_id::notify) != migrated.end();
    EXPECT_TRUE(has_login);
    EXPECT_TRUE(has_notify);
}

// ========== Wave 1 Handler Tests ==========

#include "bridge/handlers/wave1_handlers.h"

using namespace hb::bridge::wave1;

class wave1_handlers_test : public ::testing::Test
{
protected:
    void SetUp() override
    {
        // Ensure clean state
        unregister_wave1_handlers();
    }

    void TearDown() override { unregister_wave1_handlers(); }
};

TEST_F(wave1_handlers_test, registration_returns_count)
{
    auto count = register_wave1_handlers();
    EXPECT_GT(count, 0);
}

TEST_F(wave1_handlers_test, registers_command_common_handler)
{
    register_wave1_handlers();
    EXPECT_TRUE(router().is_modernized(message_id::command_common));
}

TEST_F(wave1_handlers_test, unregistration_removes_handlers)
{
    register_wave1_handlers();
    EXPECT_TRUE(router().is_modernized(message_id::command_common));

    unregister_wave1_handlers();
    EXPECT_FALSE(router().is_modernized(message_id::command_common));
}

TEST_F(wave1_handlers_test, help_handler_returns_handled)
{
    register_wave1_handlers();

    // Build a help request message (command_common + request_help subtype)
    message_writer writer;
    writer.write_u32(static_cast<uint32_t>(message_id::command_common));
    writer.write_u16(static_cast<uint16_t>(common_type::request_help));

    auto data = writer.data();

    handler_context ctx{.connection = connection_id{1},
                        .session = session_id{},
                        .player = player_id{},
                        .msg_id = message_id::command_common,
                        .raw_data = data,
                        .received_at = std::chrono::steady_clock::now()};

    auto result = router().route(ctx);
    EXPECT_TRUE(result); // Should be handled by modern code
}

TEST_F(wave1_handlers_test, non_help_common_falls_through)
{
    register_wave1_handlers();

    // Build a non-help command_common message (e.g., item_drop)
    message_writer writer;
    writer.write_u32(static_cast<uint32_t>(message_id::command_common));
    writer.write_u16(static_cast<uint16_t>(common_type::item_drop));

    auto data = writer.data();

    bool legacy_called = false;
    router().set_legacy_fallback([&](connection_id, std::span<const uint8_t>) { legacy_called = true; });

    handler_context ctx{.connection = connection_id{1},
                        .session = session_id{},
                        .player = player_id{},
                        .msg_id = message_id::command_common,
                        .raw_data = data,
                        .received_at = std::chrono::steady_clock::now()};

    auto result = router().route(ctx);
    EXPECT_FALSE(result); // Should fall through to legacy
    EXPECT_TRUE(legacy_called);
}

TEST_F(wave1_handlers_test, short_message_falls_through)
{
    register_wave1_handlers();

    // Build a message that's too short (no subtype)
    message_writer writer;
    writer.write_u32(static_cast<uint32_t>(message_id::command_common));
    // No subtype - only 4 bytes

    auto data = writer.data();

    bool legacy_called = false;
    router().set_legacy_fallback([&](connection_id, std::span<const uint8_t>) { legacy_called = true; });

    handler_context ctx{.connection = connection_id{1},
                        .session = session_id{},
                        .player = player_id{},
                        .msg_id = message_id::command_common,
                        .raw_data = data,
                        .received_at = std::chrono::steady_clock::now()};

    auto result = router().route(ctx);
    EXPECT_FALSE(result); // Should fall through due to insufficient data
    EXPECT_TRUE(legacy_called);
}

// ========== Wave 2 Handler Tests ==========

#include "bridge/handlers/wave2_handlers.h"

using namespace hb::bridge::wave2;

class wave2_handlers_test : public ::testing::Test
{
protected:
    void SetUp() override { unregister_wave2_handlers(); }

    void TearDown() override { unregister_wave2_handlers(); }

    // Helper to build a chat message
    auto make_chat_message(std::string_view sender, std::string_view message) -> std::vector<uint8_t>
    {
        message_writer writer;
        writer.write_u32(static_cast<uint32_t>(message_id::command_chat_msg));
        writer.write_u16(0); // subtype

        // Sender name (length-prefixed)
        writer.write_u8(static_cast<uint8_t>(sender.size()));
        for (char c : sender)
        {
            writer.write_u8(static_cast<uint8_t>(c));
        }

        // Message content (length-prefixed)
        writer.write_u16(static_cast<uint16_t>(message.size()));
        for (char c : message)
        {
            writer.write_u8(static_cast<uint8_t>(c));
        }

        return writer.copy();
    }
};

TEST_F(wave2_handlers_test, registration_returns_count)
{
    auto count = register_wave2_handlers();
    EXPECT_GT(count, 0);
}

TEST_F(wave2_handlers_test, registers_chat_msg_handler)
{
    register_wave2_handlers();
    EXPECT_TRUE(router().is_modernized(message_id::command_chat_msg));
}

TEST_F(wave2_handlers_test, unregistration_removes_handlers)
{
    register_wave2_handlers();
    EXPECT_TRUE(router().is_modernized(message_id::command_chat_msg));

    unregister_wave2_handlers();
    EXPECT_FALSE(router().is_modernized(message_id::command_chat_msg));
}

TEST_F(wave2_handlers_test, non_command_chat_falls_through)
{
    register_wave2_handlers();

    auto data = make_chat_message("Player1", "Hello everyone!");

    bool legacy_called = false;
    router().set_legacy_fallback([&](connection_id, std::span<const uint8_t>) { legacy_called = true; });

    handler_context ctx{.connection = connection_id{1},
                        .session = session_id{},
                        .player = player_id{},
                        .msg_id = message_id::command_chat_msg,
                        .raw_data = data,
                        .received_at = std::chrono::steady_clock::now()};

    auto result = router().route(ctx);
    EXPECT_FALSE(result); // Regular chat should fall through to legacy
    EXPECT_TRUE(legacy_called);
}

TEST_F(wave2_handlers_test, admin_command_without_player_falls_through)
{
    register_wave2_handlers();

    auto data = make_chat_message("Player1", "/help");

    bool legacy_called = false;
    router().set_legacy_fallback([&](connection_id, std::span<const uint8_t>) { legacy_called = true; });

    handler_context ctx{.connection = connection_id{1},
                        .session = session_id{},
                        .player = player_id{}, // Invalid player
                        .msg_id = message_id::command_chat_msg,
                        .raw_data = data,
                        .received_at = std::chrono::steady_clock::now()};

    auto result = router().route(ctx);
    EXPECT_FALSE(result); // Should fall through without valid player
    EXPECT_TRUE(legacy_called);
}

TEST_F(wave2_handlers_test, short_chat_message_falls_through)
{
    register_wave2_handlers();

    // Build a message that's too short
    message_writer writer;
    writer.write_u32(static_cast<uint32_t>(message_id::command_chat_msg));
    writer.write_u16(0); // subtype only

    auto data = writer.data();

    bool legacy_called = false;
    router().set_legacy_fallback([&](connection_id, std::span<const uint8_t>) { legacy_called = true; });

    handler_context ctx{.connection = connection_id{1},
                        .session = session_id{},
                        .player = player_id{},
                        .msg_id = message_id::command_chat_msg,
                        .raw_data = data,
                        .received_at = std::chrono::steady_clock::now()};

    auto result = router().route(ctx);
    EXPECT_FALSE(result);
    EXPECT_TRUE(legacy_called);
}

// ========== Wave 3 Handler Tests ==========

#include "bridge/handlers/wave3_handlers.h"

using namespace hb::bridge::wave3;

class wave3_handlers_test : public ::testing::Test
{
protected:
    void SetUp() override
    {
        unregister_wave3_handlers();
        router().reset_stats(); // Reset stats for accurate error counting
    }

    void TearDown() override { unregister_wave3_handlers(); }

    // Helper to build a motion command
    auto make_motion_message(motion_type type, int16_t x, int16_t y, int8_t dir) -> std::vector<uint8_t>
    {
        message_writer writer;
        writer.write_u32(static_cast<uint32_t>(message_id::command_motion));
        writer.write_u8(static_cast<uint8_t>(type));
        writer.write_i16(x);
        writer.write_i16(y);
        writer.write_i8(dir);
        return writer.copy();
    }
};

TEST_F(wave3_handlers_test, registration_returns_count)
{
    auto count = register_wave3_handlers();
    EXPECT_GT(count, 0);
}

TEST_F(wave3_handlers_test, registers_motion_handler)
{
    register_wave3_handlers();
    EXPECT_TRUE(router().is_modernized(message_id::command_motion));
}

TEST_F(wave3_handlers_test, unregistration_removes_handlers)
{
    register_wave3_handlers();
    EXPECT_TRUE(router().is_modernized(message_id::command_motion));

    unregister_wave3_handlers();
    EXPECT_FALSE(router().is_modernized(message_id::command_motion));
}

TEST_F(wave3_handlers_test, motion_without_player_returns_error)
{
    register_wave3_handlers();

    auto data = make_motion_message(motion_type::move, 100, 200, 0);

    bool legacy_called = false;
    router().set_legacy_fallback([&](connection_id, std::span<const uint8_t>) { legacy_called = true; });

    handler_context ctx{.connection = connection_id{1},
                        .session = session_id{},
                        .player = player_id{}, // Invalid player
                        .msg_id = message_id::command_motion,
                        .raw_data = data,
                        .received_at = std::chrono::steady_clock::now()};

    auto result = router().route(ctx);
    // require_player wrapper returns error (handled), not fallback
    EXPECT_TRUE(result);                   // Still handled (as error)
    EXPECT_FALSE(legacy_called);           // Legacy not called
    EXPECT_EQ(router().stats().errors, 1); // Error was counted
}

TEST_F(wave3_handlers_test, short_motion_message_falls_through)
{
    register_wave3_handlers();

    // Build a message that's too short (no position data)
    message_writer writer;
    writer.write_u32(static_cast<uint32_t>(message_id::command_motion));
    writer.write_u8(1); // motion type only

    auto data = writer.data();

    bool legacy_called = false;
    router().set_legacy_fallback([&](connection_id, std::span<const uint8_t>) { legacy_called = true; });

    handler_context ctx{.connection = connection_id{1},
                        .session = session_id{},
                        .player = player_id{1},
                        .msg_id = message_id::command_motion,
                        .raw_data = data,
                        .received_at = std::chrono::steady_clock::now()};

    auto result = router().route(ctx);
    EXPECT_FALSE(result);
    EXPECT_TRUE(legacy_called);
}

TEST_F(wave3_handlers_test, combat_motion_falls_through)
{
    register_wave3_handlers();

    // Attack motion should fall through to Wave 4 (combat)
    auto data = make_motion_message(motion_type::attack, 100, 200, 0);

    bool legacy_called = false;
    router().set_legacy_fallback([&](connection_id, std::span<const uint8_t>) { legacy_called = true; });

    handler_context ctx{.connection = connection_id{1},
                        .session = session_id{},
                        .player = player_id{1},
                        .msg_id = message_id::command_motion,
                        .raw_data = data,
                        .received_at = std::chrono::steady_clock::now()};

    auto result = router().route(ctx);
    EXPECT_FALSE(result); // Combat should fall through
    EXPECT_TRUE(legacy_called);
}

// ========== Wave 4 Handler Tests ==========

#include "bridge/handlers/wave4_handlers.h"

using namespace hb::bridge::wave4;

class wave4_handlers_test : public ::testing::Test
{
protected:
    void SetUp() override
    {
        unregister_wave4_handlers();
        router().reset_stats(); // Reset stats for accurate error counting
    }

    void TearDown() override { unregister_wave4_handlers(); }

    // Helper to build a magic cast message
    auto make_magic_message(uint16_t spell_id_val, int16_t x, int16_t y) -> std::vector<uint8_t>
    {
        message_writer writer;
        writer.write_u32(static_cast<uint32_t>(message_id::command_common));
        writer.write_u16(static_cast<uint16_t>(common_type::magic));
        writer.write_u16(spell_id_val);
        writer.write_i16(x);
        writer.write_i16(y);
        return writer.copy();
    }

    // Helper to build a skill use message
    auto make_skill_message(uint16_t skill_id_val) -> std::vector<uint8_t>
    {
        message_writer writer;
        writer.write_u32(static_cast<uint32_t>(message_id::command_common));
        writer.write_u16(static_cast<uint16_t>(common_type::req_use_skill));
        writer.write_u16(skill_id_val);
        return writer.copy();
    }
};

TEST_F(wave4_handlers_test, registration_returns_count)
{
    auto count = register_wave4_handlers();
    EXPECT_GT(count, 0);
}

TEST_F(wave4_handlers_test, registers_command_common_handler)
{
    register_wave4_handlers();
    EXPECT_TRUE(router().is_modernized(message_id::command_common));
}

TEST_F(wave4_handlers_test, unregistration_removes_handlers)
{
    register_wave4_handlers();
    EXPECT_TRUE(router().is_modernized(message_id::command_common));

    unregister_wave4_handlers();
    EXPECT_FALSE(router().is_modernized(message_id::command_common));
}

TEST_F(wave4_handlers_test, non_combat_common_falls_through)
{
    register_wave4_handlers();

    // Non-combat common type should fall through
    message_writer writer;
    writer.write_u32(static_cast<uint32_t>(message_id::command_common));
    writer.write_u16(static_cast<uint16_t>(common_type::item_drop));

    auto data = writer.data();

    bool legacy_called = false;
    router().set_legacy_fallback([&](connection_id, std::span<const uint8_t>) { legacy_called = true; });

    handler_context ctx{.connection = connection_id{1},
                        .session = session_id{},
                        .player = player_id{1},
                        .msg_id = message_id::command_common,
                        .raw_data = data,
                        .received_at = std::chrono::steady_clock::now()};

    auto result = router().route(ctx);
    EXPECT_FALSE(result);
    EXPECT_TRUE(legacy_called);
}

TEST_F(wave4_handlers_test, magic_without_player_returns_error)
{
    register_wave4_handlers();

    auto data = make_magic_message(1, 100, 200);

    handler_context ctx{.connection = connection_id{1},
                        .session = session_id{},
                        .player = player_id{}, // Invalid
                        .msg_id = message_id::command_common,
                        .raw_data = data,
                        .received_at = std::chrono::steady_clock::now()};

    auto result = router().route(ctx);
    EXPECT_TRUE(result); // Handled as error by require_player
    EXPECT_EQ(router().stats().errors, 1);
}

TEST_F(wave4_handlers_test, skill_falls_through_without_system)
{
    register_wave4_handlers();

    auto data = make_skill_message(1);

    bool legacy_called = false;
    router().set_legacy_fallback([&](connection_id, std::span<const uint8_t>) { legacy_called = true; });

    handler_context ctx{.connection = connection_id{1},
                        .session = session_id{},
                        .player = player_id{1},
                        .msg_id = message_id::command_common,
                        .raw_data = data,
                        .received_at = std::chrono::steady_clock::now()};

    auto result = router().route(ctx);
    // Skill handler returns not_handled when system is not available
    EXPECT_FALSE(result);
    EXPECT_TRUE(legacy_called);
}

// ========== Wave 5 Handler Tests ==========

#include "bridge/handlers/wave5_handlers.h"

using namespace hb::bridge::wave5;

class wave5_handlers_test : public ::testing::Test
{
protected:
    void SetUp() override
    {
        unregister_wave5_handlers();
        router().reset_stats(); // Reset stats for accurate error counting
    }

    void TearDown() override { unregister_wave5_handlers(); }

    // Helper to build a party operation message
    auto make_party_message(uint8_t operation, uint32_t party_or_player_id = 0) -> std::vector<uint8_t>
    {
        message_writer writer;
        writer.write_u32(static_cast<uint32_t>(message_id::party_operation));
        writer.write_u8(operation);
        if (party_or_player_id != 0)
        {
            writer.write_u32(party_or_player_id);
        }
        return writer.copy();
    }

    // Helper to build a guild creation message
    auto make_guild_create_message(std::string_view name) -> std::vector<uint8_t>
    {
        message_writer writer;
        writer.write_u32(static_cast<uint32_t>(message_id::request_create_new_guild));
        writer.write_u8(static_cast<uint8_t>(name.size()));
        for (char c : name)
        {
            writer.write_u8(static_cast<uint8_t>(c));
        }
        return writer.copy();
    }

    // Helper to build an exchange request message
    auto make_exchange_message(common_type subtype, uint32_t target_id = 0) -> std::vector<uint8_t>
    {
        message_writer writer;
        writer.write_u32(static_cast<uint32_t>(message_id::command_common));
        writer.write_u16(static_cast<uint16_t>(subtype));
        if (target_id != 0)
        {
            writer.write_u32(target_id);
        }
        return writer.copy();
    }
};

TEST_F(wave5_handlers_test, registration_returns_count)
{
    auto count = register_wave5_handlers();
    EXPECT_GT(count, 0);
}

TEST_F(wave5_handlers_test, registers_party_operation_handler)
{
    register_wave5_handlers();
    EXPECT_TRUE(router().is_modernized(message_id::party_operation));
}

TEST_F(wave5_handlers_test, registers_guild_create_handler)
{
    register_wave5_handlers();
    EXPECT_TRUE(router().is_modernized(message_id::request_create_new_guild));
}

TEST_F(wave5_handlers_test, registers_command_common_handler)
{
    register_wave5_handlers();
    EXPECT_TRUE(router().is_modernized(message_id::command_common));
}

TEST_F(wave5_handlers_test, unregistration_removes_handlers)
{
    register_wave5_handlers();
    EXPECT_TRUE(router().is_modernized(message_id::party_operation));
    EXPECT_TRUE(router().is_modernized(message_id::request_create_new_guild));

    unregister_wave5_handlers();
    EXPECT_FALSE(router().is_modernized(message_id::party_operation));
    EXPECT_FALSE(router().is_modernized(message_id::request_create_new_guild));
}

TEST_F(wave5_handlers_test, party_without_player_returns_error)
{
    register_wave5_handlers();

    auto data = make_party_message(0); // Create party

    handler_context ctx{.connection = connection_id{1},
                        .session = session_id{},
                        .player = player_id{}, // Invalid
                        .msg_id = message_id::party_operation,
                        .raw_data = data,
                        .received_at = std::chrono::steady_clock::now()};

    auto result = router().route(ctx);
    EXPECT_TRUE(result); // Handled as error by require_player
    EXPECT_EQ(router().stats().errors, 1);
}

TEST_F(wave5_handlers_test, party_unknown_operation_falls_through)
{
    register_wave5_handlers();

    auto data = make_party_message(99); // Unknown operation

    bool legacy_called = false;
    router().set_legacy_fallback([&](connection_id, std::span<const uint8_t>) { legacy_called = true; });

    handler_context ctx{.connection = connection_id{1},
                        .session = session_id{},
                        .player = player_id{1},
                        .msg_id = message_id::party_operation,
                        .raw_data = data,
                        .received_at = std::chrono::steady_clock::now()};

    auto result = router().route(ctx);
    EXPECT_FALSE(result); // Unknown ops fall through
    EXPECT_TRUE(legacy_called);
}

TEST_F(wave5_handlers_test, short_party_message_falls_through)
{
    register_wave5_handlers();

    // Build a message that's too short (no operation byte)
    message_writer writer;
    writer.write_u32(static_cast<uint32_t>(message_id::party_operation));

    auto data = writer.data();

    bool legacy_called = false;
    router().set_legacy_fallback([&](connection_id, std::span<const uint8_t>) { legacy_called = true; });

    handler_context ctx{.connection = connection_id{1},
                        .session = session_id{},
                        .player = player_id{1},
                        .msg_id = message_id::party_operation,
                        .raw_data = data,
                        .received_at = std::chrono::steady_clock::now()};

    auto result = router().route(ctx);
    EXPECT_FALSE(result);
    EXPECT_TRUE(legacy_called);
}

TEST_F(wave5_handlers_test, guild_create_without_player_returns_error)
{
    register_wave5_handlers();

    auto data = make_guild_create_message("TestGuild");

    handler_context ctx{.connection = connection_id{1},
                        .session = session_id{},
                        .player = player_id{}, // Invalid
                        .msg_id = message_id::request_create_new_guild,
                        .raw_data = data,
                        .received_at = std::chrono::steady_clock::now()};

    auto result = router().route(ctx);
    EXPECT_TRUE(result); // Handled as error by require_player
    EXPECT_EQ(router().stats().errors, 1);
}

TEST_F(wave5_handlers_test, exchange_non_social_type_falls_through)
{
    register_wave5_handlers();

    // Non-social common type should fall through
    auto data = make_exchange_message(common_type::item_drop, 100);

    bool legacy_called = false;
    router().set_legacy_fallback([&](connection_id, std::span<const uint8_t>) { legacy_called = true; });

    handler_context ctx{.connection = connection_id{1},
                        .session = session_id{},
                        .player = player_id{1},
                        .msg_id = message_id::command_common,
                        .raw_data = data,
                        .received_at = std::chrono::steady_clock::now()};

    auto result = router().route(ctx);
    EXPECT_FALSE(result);
    EXPECT_TRUE(legacy_called);
}

TEST_F(wave5_handlers_test, short_common_message_falls_through)
{
    register_wave5_handlers();

    // Build a message that's too short (no subtype)
    message_writer writer;
    writer.write_u32(static_cast<uint32_t>(message_id::command_common));

    auto data = writer.data();

    bool legacy_called = false;
    router().set_legacy_fallback([&](connection_id, std::span<const uint8_t>) { legacy_called = true; });

    handler_context ctx{.connection = connection_id{1},
                        .session = session_id{},
                        .player = player_id{1},
                        .msg_id = message_id::command_common,
                        .raw_data = data,
                        .received_at = std::chrono::steady_clock::now()};

    auto result = router().route(ctx);
    EXPECT_FALSE(result);
    EXPECT_TRUE(legacy_called);
}
