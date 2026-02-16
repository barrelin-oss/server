// test_websocket_queue.cpp
// Unit tests for websocket_server pending event queue mechanism

#include <gtest/gtest.h>
#include "network/websocket_server.h"

#include <vector>
#include <string>

using namespace hb;
using namespace hb::network;

class websocket_queue_test : public ::testing::Test
{
protected:
    websocket_server server;

    // Track handler calls
    struct connect_call
    {
        connection_id id;
        std::string address;
    };
    struct message_call
    {
        connection_id id;
        json_message_type type;
    };

    std::vector<connect_call> connect_calls;
    std::vector<message_call> message_calls;

    void SetUp() override
    {
        server.on_connect([this](connection_id id, const std::string& addr)
        {
            connect_calls.push_back({id, addr});
        });
        server.on_message([this](connection_id id, const json_message& msg)
        {
            message_calls.push_back({id, msg.type});
        });
    }
};

TEST_F(websocket_queue_test, process_events_drains_empty_queue)
{
    // Should be a no-op, not crash
    server.process_pending_events();
    EXPECT_TRUE(connect_calls.empty());
    EXPECT_TRUE(message_calls.empty());
}

TEST_F(websocket_queue_test, process_events_drains_empty_queue_multiple_times)
{
    server.process_pending_events();
    server.process_pending_events();
    server.process_pending_events();
    EXPECT_TRUE(connect_calls.empty());
    EXPECT_TRUE(message_calls.empty());
}

TEST_F(websocket_queue_test, no_handlers_does_not_crash)
{
    // Create server with no handlers registered
    websocket_server bare_server;
    // Should not crash even with no handlers
    bare_server.process_pending_events();
}
