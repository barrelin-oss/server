// test_handlers.cpp
// Additional unit tests for bridge handlers - data structures and edge cases
// Note: Core message_router tests are in test_bridge.cpp

#include <gtest/gtest.h>
#include "bridge/message_router.h"
#include "protocol/protocol.h"
#include "protocol/message_writer.h"

#include <chrono>
#include <atomic>

using namespace hb;
using namespace hb::bridge;
using namespace hb::protocol;

// ========== handler_context Tests ==========

TEST(handler_context_test, default_construction)
{
    handler_context ctx{};

    EXPECT_EQ(ctx.connection.value, 0);
    EXPECT_EQ(ctx.session.value, 0);
    EXPECT_EQ(ctx.player.value, 0);
    EXPECT_TRUE(ctx.raw_data.empty());
}

TEST(handler_context_test, construction_with_values)
{
    std::vector<uint8_t> data = {0x01, 0x02, 0x03, 0x04};

    handler_context ctx{.connection = connection_id{42},
                        .session = session_id{100},
                        .player = player_id{200},
                        .msg_id = message_id::command_motion,
                        .raw_data = data,
                        .received_at = std::chrono::steady_clock::now()};

    EXPECT_EQ(ctx.connection.value, 42);
    EXPECT_EQ(ctx.session.value, 100);
    EXPECT_EQ(ctx.player.value, 200);
    EXPECT_EQ(ctx.msg_id, message_id::command_motion);
    EXPECT_EQ(ctx.raw_data.size(), 4);
}

TEST(handler_context_test, make_reader)
{
    std::vector<uint8_t> data = {0x12, 0x34, 0x56, 0x78};

    handler_context ctx{.connection = connection_id{1}, .raw_data = data};

    auto reader = ctx.make_reader();
    EXPECT_EQ(reader.read_u8(), 0x12);
    EXPECT_EQ(reader.read_u8(), 0x34);
    EXPECT_EQ(reader.remaining(), 2);
}

TEST(handler_context_test, make_reader_empty_data)
{
    handler_context ctx{.connection = connection_id{1}};

    auto reader = ctx.make_reader();
    EXPECT_TRUE(reader.at_end());
    EXPECT_EQ(reader.remaining(), 0);
}

// ========== handle_result Tests ==========

TEST(handle_result_test, enum_values)
{
    // Verify distinct values
    EXPECT_NE(static_cast<int>(handle_result::handled), static_cast<int>(handle_result::handled_async));
    EXPECT_NE(static_cast<int>(handle_result::handled), static_cast<int>(handle_result::not_handled));
    EXPECT_NE(static_cast<int>(handle_result::handled), static_cast<int>(handle_result::error));
    EXPECT_NE(static_cast<int>(handle_result::handled), static_cast<int>(handle_result::disconnect));
}

TEST(handle_result_test, can_use_in_switch)
{
    auto test_result = [](handle_result r) -> std::string
    {
        switch (r)
        {
        case handle_result::handled:
            return "handled";
        case handle_result::handled_async:
            return "async";
        case handle_result::not_handled:
            return "not_handled";
        case handle_result::error:
            return "error";
        case handle_result::disconnect:
            return "disconnect";
        default:
            return "unknown";
        }
    };

    EXPECT_EQ(test_result(handle_result::handled), "handled");
    EXPECT_EQ(test_result(handle_result::handled_async), "async");
    EXPECT_EQ(test_result(handle_result::not_handled), "not_handled");
    EXPECT_EQ(test_result(handle_result::error), "error");
    EXPECT_EQ(test_result(handle_result::disconnect), "disconnect");
}

// ========== handler_info Tests ==========

TEST(handler_info_test, default_construction)
{
    handler_info info{};

    EXPECT_TRUE(info.name.empty());
    EXPECT_TRUE(info.subsystem.empty());
    EXPECT_TRUE(info.enabled); // Default enabled
    EXPECT_EQ(info.handler, nullptr);
}

TEST(handler_info_test, construction_with_values)
{
    handler_info info{.name = "test_handler",
                      .subsystem = "test_system",
                      .msg_id = message_id::event_common,
                      .handler = [](const handler_context&) { return handle_result::handled; },
                      .enabled = true};

    EXPECT_EQ(info.name, "test_handler");
    EXPECT_EQ(info.subsystem, "test_system");
    EXPECT_EQ(info.msg_id, message_id::event_common);
    EXPECT_NE(info.handler, nullptr);
    EXPECT_TRUE(info.enabled);
}

TEST(handler_info_test, handler_callable)
{
    std::atomic<int> call_count{0};

    handler_info info{.name = "counter",
                      .subsystem = "test",
                      .msg_id = message_id::command_motion,
                      .handler = [&call_count](const handler_context&)
                      {
                          ++call_count;
                          return handle_result::handled;
                      }};

    handler_context ctx{};
    auto result = info.handler(ctx);

    EXPECT_EQ(result, handle_result::handled);
    EXPECT_EQ(call_count.load(), 1);
}

// ========== message_stats Tests ==========

TEST(message_stats_test, default_construction)
{
    message_stats stats{};

    EXPECT_EQ(stats.total_count, 0);
    EXPECT_EQ(stats.success_count, 0);
    EXPECT_EQ(stats.error_count, 0);
    EXPECT_EQ(stats.fallback_count, 0);
    EXPECT_EQ(stats.total_time.count(), 0);
    EXPECT_EQ(stats.max_time.count(), 0);
}

TEST(message_stats_test, construction_with_values)
{
    message_stats stats{.msg_id = message_id::request_login,
                        .total_count = 100,
                        .success_count = 95,
                        .error_count = 5,
                        .fallback_count = 0,
                        .total_time = std::chrono::nanoseconds{1000000},
                        .max_time = std::chrono::nanoseconds{50000}};

    EXPECT_EQ(stats.msg_id, message_id::request_login);
    EXPECT_EQ(stats.total_count, 100);
    EXPECT_EQ(stats.success_count, 95);
    EXPECT_EQ(stats.error_count, 5);
}

// ========== router_stats Tests ==========

TEST(router_stats_test, default_construction)
{
    router_stats stats{};

    EXPECT_EQ(stats.total_messages, 0);
    EXPECT_EQ(stats.modern_handled, 0);
    EXPECT_EQ(stats.legacy_fallback, 0);
    EXPECT_EQ(stats.errors, 0);
    EXPECT_EQ(stats.disconnects, 0);
    EXPECT_EQ(stats.total_processing_time.count(), 0);
}

TEST(router_stats_test, construction_with_values)
{
    router_stats stats{.total_messages = 1000,
                       .modern_handled = 800,
                       .legacy_fallback = 150,
                       .errors = 40,
                       .disconnects = 10,
                       .total_processing_time = std::chrono::nanoseconds{5000000}};

    EXPECT_EQ(stats.total_messages, 1000);
    EXPECT_EQ(stats.modern_handled, 800);
    EXPECT_EQ(stats.legacy_fallback, 150);
    EXPECT_EQ(stats.errors, 40);
    EXPECT_EQ(stats.disconnects, 10);
}

// ========== Handler Context Data Tests ==========

TEST(handler_context_data_test, reader_reads_correct_data)
{
    message_writer writer;
    writer.write_u8(0x42);
    writer.write_u16(0x1234);
    writer.write_u32(0xDEADBEEF);

    auto data = writer.data();
    std::vector<uint8_t> vec_data(data.begin(), data.end());

    handler_context ctx{.connection = connection_id{1}, .raw_data = vec_data};

    auto reader = ctx.make_reader();
    EXPECT_EQ(reader.read_u8(), 0x42);
    EXPECT_EQ(reader.read_u16(), 0x1234);
    EXPECT_EQ(reader.read_u32(), 0xDEADBEEF);
    EXPECT_TRUE(reader.at_end());
}

TEST(handler_context_data_test, received_at_timestamp)
{
    auto before = std::chrono::steady_clock::now();

    handler_context ctx{.connection = connection_id{1}, .received_at = std::chrono::steady_clock::now()};

    auto after = std::chrono::steady_clock::now();

    EXPECT_GE(ctx.received_at, before);
    EXPECT_LE(ctx.received_at, after);
}

// ========== Handler info with different result types ==========

TEST(handler_info_result_test, async_handler)
{
    handler_info info{.name = "async_test",
                      .subsystem = "test",
                      .msg_id = message_id::request_login,
                      .handler = [](const handler_context&)
                      {
                          return handle_result::handled_async;
                      }};

    handler_context ctx{};
    EXPECT_EQ(info.handler(ctx), handle_result::handled_async);
}

TEST(handler_info_result_test, not_handled_handler)
{
    handler_info info{.name = "passthrough_test",
                      .subsystem = "test",
                      .msg_id = message_id::request_login,
                      .handler = [](const handler_context&)
                      {
                          return handle_result::not_handled;
                      }};

    handler_context ctx{};
    EXPECT_EQ(info.handler(ctx), handle_result::not_handled);
}

TEST(handler_info_result_test, error_handler)
{
    handler_info info{.name = "error_test",
                      .subsystem = "test",
                      .msg_id = message_id::request_login,
                      .handler = [](const handler_context&)
                      {
                          return handle_result::error;
                      }};

    handler_context ctx{};
    EXPECT_EQ(info.handler(ctx), handle_result::error);
}

TEST(handler_info_result_test, disconnect_handler)
{
    handler_info info{.name = "disconnect_test",
                      .subsystem = "test",
                      .msg_id = message_id::request_login,
                      .handler = [](const handler_context&)
                      {
                          return handle_result::disconnect;
                      }};

    handler_context ctx{};
    EXPECT_EQ(info.handler(ctx), handle_result::disconnect);
}

// ========== Handler context with various data sizes ==========

TEST(handler_context_data_test, large_data)
{
    std::vector<uint8_t> large_data(10000, 0xAB);

    handler_context ctx{.connection = connection_id{1}, .raw_data = large_data};

    EXPECT_EQ(ctx.raw_data.size(), 10000);

    auto reader = ctx.make_reader();
    EXPECT_EQ(reader.remaining(), 10000);
    EXPECT_EQ(reader.read_u8(), 0xAB);
}

TEST(handler_context_data_test, single_byte_data)
{
    std::vector<uint8_t> data = {0xFF};

    handler_context ctx{.connection = connection_id{1}, .raw_data = data};

    auto reader = ctx.make_reader();
    EXPECT_EQ(reader.remaining(), 1);
    EXPECT_EQ(reader.read_u8(), 0xFF);
    EXPECT_TRUE(reader.at_end());
}

// ========== Stats time tracking ==========

TEST(message_stats_test, time_tracking)
{
    message_stats stats{.total_time = std::chrono::nanoseconds{5000000}, .max_time = std::chrono::nanoseconds{100000}};

    // Verify time values are stored correctly
    EXPECT_EQ(stats.total_time.count(), 5000000);
    EXPECT_EQ(stats.max_time.count(), 100000);

    // Calculate average (if total_count were set)
    message_stats stats2{.total_count = 100, .total_time = std::chrono::nanoseconds{1000000}};

    auto avg_ns = stats2.total_time.count() / stats2.total_count;
    EXPECT_EQ(avg_ns, 10000);
}

TEST(router_stats_test, time_tracking)
{
    router_stats stats{.total_messages = 1000, .total_processing_time = std::chrono::nanoseconds{10000000}};

    auto avg_ns = stats.total_processing_time.count() / stats.total_messages;
    EXPECT_EQ(avg_ns, 10000);
}
