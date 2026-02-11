// test_friend_system.cpp
// Unit tests for friend list system

#include <gtest/gtest.h>
#include "core/types.h"
#include "social/friend.h"
#include "social/social_system.h"
#include "network/json_protocol.h"

using hb::player_id;
using namespace hb::social;

// Helper fixture that sets up a social system for friend testing
class friend_system_test : public ::testing::Test
{
protected:
    void SetUp() override
    {
        social.initialize();
        social.register_player(player_id{1}, "Alice");
        social.register_player(player_id{2}, "Bob");
        social.register_player(player_id{3}, "Charlie");

        // Simulate entering game: connect friends with character IDs
        // player_id{1} (runtime) → char_id 100
        // player_id{2} (runtime) → char_id 200
        // player_id{3} (runtime) → char_id 300
        social.connect_friend(player_id{100}, player_id{1}, "Alice");
        social.connect_friend(player_id{200}, player_id{2}, "Bob");
        social.connect_friend(player_id{300}, player_id{3}, "Charlie");
    }

    void TearDown() override
    {
        social.shutdown();
    }

    social_system social;

    // Character IDs
    static constexpr auto alice_char = player_id{100};
    static constexpr auto bob_char = player_id{200};
    static constexpr auto charlie_char = player_id{300};
};

// ========== Friend Result Enum ==========

TEST(friend_result_test, to_string_view_all_values)
{
    EXPECT_EQ(to_string_view(friend_result::success), "success");
    EXPECT_EQ(to_string_view(friend_result::friend_not_found), "friend_not_found");
    EXPECT_EQ(to_string_view(friend_result::already_friends), "already_friends");
    EXPECT_EQ(to_string_view(friend_result::friend_limit_reached), "friend_limit_reached");
    EXPECT_EQ(to_string_view(friend_result::cannot_add_self), "cannot_add_self");
    EXPECT_EQ(to_string_view(friend_result::is_blocked), "is_blocked");
    EXPECT_EQ(to_string_view(friend_result::player_not_found), "player_not_found");
    EXPECT_EQ(to_string_view(friend_result::not_friends), "not_friends");
    EXPECT_EQ(to_string_view(friend_result::request_already_exists), "request_already_exists");
    EXPECT_EQ(to_string_view(friend_result::no_pending_request), "no_pending_request");
    EXPECT_EQ(to_string_view(friend_result::target_limit_reached), "target_limit_reached");
}

// ========== Send Friend Request ==========

TEST_F(friend_system_test, send_request_success)
{
    auto result = social.send_friend_request(alice_char, bob_char);
    EXPECT_EQ(result, friend_result::success);

    EXPECT_TRUE(social.has_pending_request(alice_char, bob_char));
    EXPECT_FALSE(social.has_pending_request(bob_char, alice_char));
}

TEST_F(friend_system_test, send_request_cannot_add_self)
{
    auto result = social.send_friend_request(alice_char, alice_char);
    EXPECT_EQ(result, friend_result::cannot_add_self);
}

TEST_F(friend_system_test, send_request_duplicate)
{
    social.send_friend_request(alice_char, bob_char);
    auto result = social.send_friend_request(alice_char, bob_char);
    EXPECT_EQ(result, friend_result::request_already_exists);
}

TEST_F(friend_system_test, send_request_already_friends)
{
    // Create friendship first
    social.send_friend_request(alice_char, bob_char);
    social.accept_friend_request(bob_char, alice_char);

    // Try sending request when already friends
    auto result = social.send_friend_request(alice_char, bob_char);
    EXPECT_EQ(result, friend_result::already_friends);
}

TEST_F(friend_system_test, send_request_blocked_by_target)
{
    social.block_player_friend(bob_char, alice_char);

    auto result = social.send_friend_request(alice_char, bob_char);
    EXPECT_EQ(result, friend_result::is_blocked);
}

TEST_F(friend_system_test, send_request_auto_accepts_reverse)
{
    // Bob sends request to Alice
    social.send_friend_request(bob_char, alice_char);
    EXPECT_TRUE(social.has_pending_request(bob_char, alice_char));

    // Alice sends request to Bob → should auto-accept
    auto result = social.send_friend_request(alice_char, bob_char);
    EXPECT_EQ(result, friend_result::success);

    // Should now be friends
    EXPECT_TRUE(social.are_friends(alice_char, bob_char));

    // Pending request should be gone
    EXPECT_FALSE(social.has_pending_request(bob_char, alice_char));
    EXPECT_FALSE(social.has_pending_request(alice_char, bob_char));
}

// ========== Accept Friend Request ==========

TEST_F(friend_system_test, accept_request_success)
{
    social.send_friend_request(alice_char, bob_char);

    auto result = social.accept_friend_request(bob_char, alice_char);
    EXPECT_EQ(result, friend_result::success);

    EXPECT_TRUE(social.are_friends(alice_char, bob_char));
    EXPECT_TRUE(social.are_friends(bob_char, alice_char));
    EXPECT_FALSE(social.has_pending_request(alice_char, bob_char));
}

TEST_F(friend_system_test, accept_request_no_pending)
{
    auto result = social.accept_friend_request(bob_char, alice_char);
    EXPECT_EQ(result, friend_result::no_pending_request);
}

TEST_F(friend_system_test, accept_creates_bidirectional_friendship)
{
    social.send_friend_request(alice_char, bob_char);
    social.accept_friend_request(bob_char, alice_char);

    // Both should see each other as friends
    auto* alice_friends = social.get_friends(alice_char);
    auto* bob_friends = social.get_friends(bob_char);

    ASSERT_NE(alice_friends, nullptr);
    ASSERT_NE(bob_friends, nullptr);
    EXPECT_EQ(alice_friends->size(), 1);
    EXPECT_EQ(bob_friends->size(), 1);
    EXPECT_EQ((*alice_friends)[0].character_id, bob_char);
    EXPECT_EQ((*bob_friends)[0].character_id, alice_char);
}

// ========== Decline Friend Request ==========

TEST_F(friend_system_test, decline_request_success)
{
    social.send_friend_request(alice_char, bob_char);

    auto result = social.decline_friend_request(bob_char, alice_char);
    EXPECT_EQ(result, friend_result::success);

    EXPECT_FALSE(social.has_pending_request(alice_char, bob_char));
    EXPECT_FALSE(social.are_friends(alice_char, bob_char));
}

TEST_F(friend_system_test, decline_request_no_pending)
{
    auto result = social.decline_friend_request(bob_char, alice_char);
    EXPECT_EQ(result, friend_result::no_pending_request);
}

// ========== Cancel Friend Request ==========

TEST_F(friend_system_test, cancel_request_success)
{
    social.send_friend_request(alice_char, bob_char);

    auto result = social.cancel_friend_request(alice_char, bob_char);
    EXPECT_EQ(result, friend_result::success);

    EXPECT_FALSE(social.has_pending_request(alice_char, bob_char));
}

TEST_F(friend_system_test, cancel_request_no_pending)
{
    auto result = social.cancel_friend_request(alice_char, bob_char);
    EXPECT_EQ(result, friend_result::no_pending_request);
}

// ========== Remove Friend ==========

TEST_F(friend_system_test, remove_friend_success)
{
    social.send_friend_request(alice_char, bob_char);
    social.accept_friend_request(bob_char, alice_char);
    EXPECT_TRUE(social.are_friends(alice_char, bob_char));

    auto result = social.remove_friend(alice_char, bob_char);
    EXPECT_EQ(result, friend_result::success);

    EXPECT_FALSE(social.are_friends(alice_char, bob_char));
    EXPECT_FALSE(social.are_friends(bob_char, alice_char));
    EXPECT_EQ(social.friend_count(alice_char), 0);
    EXPECT_EQ(social.friend_count(bob_char), 0);
}

TEST_F(friend_system_test, remove_friend_not_friends)
{
    auto result = social.remove_friend(alice_char, bob_char);
    EXPECT_EQ(result, friend_result::not_friends);
}

TEST_F(friend_system_test, remove_friend_bidirectional)
{
    // Either side can remove the friendship
    social.send_friend_request(alice_char, bob_char);
    social.accept_friend_request(bob_char, alice_char);

    auto result = social.remove_friend(bob_char, alice_char);
    EXPECT_EQ(result, friend_result::success);

    EXPECT_FALSE(social.are_friends(alice_char, bob_char));
}

// ========== Block/Unblock ==========

TEST_F(friend_system_test, block_player_success)
{
    auto result = social.block_player_friend(alice_char, bob_char);
    EXPECT_EQ(result, friend_result::success);

    EXPECT_TRUE(social.is_blocked_friend(alice_char, bob_char));
    EXPECT_FALSE(social.is_blocked_friend(bob_char, alice_char));  // Unidirectional
}

TEST_F(friend_system_test, block_removes_existing_friendship)
{
    social.send_friend_request(alice_char, bob_char);
    social.accept_friend_request(bob_char, alice_char);
    EXPECT_TRUE(social.are_friends(alice_char, bob_char));

    social.block_player_friend(alice_char, bob_char);

    EXPECT_FALSE(social.are_friends(alice_char, bob_char));
    EXPECT_TRUE(social.is_blocked_friend(alice_char, bob_char));
}

TEST_F(friend_system_test, block_removes_pending_request)
{
    social.send_friend_request(alice_char, bob_char);
    EXPECT_TRUE(social.has_pending_request(alice_char, bob_char));

    social.block_player_friend(bob_char, alice_char);

    EXPECT_FALSE(social.has_pending_request(alice_char, bob_char));
}

TEST_F(friend_system_test, block_self_fails)
{
    auto result = social.block_player_friend(alice_char, alice_char);
    EXPECT_EQ(result, friend_result::cannot_add_self);
}

TEST_F(friend_system_test, unblock_player_success)
{
    social.block_player_friend(alice_char, bob_char);
    EXPECT_TRUE(social.is_blocked_friend(alice_char, bob_char));

    auto result = social.unblock_player_friend(alice_char, bob_char);
    EXPECT_EQ(result, friend_result::success);

    EXPECT_FALSE(social.is_blocked_friend(alice_char, bob_char));
}

TEST_F(friend_system_test, unblock_not_blocked)
{
    auto result = social.unblock_player_friend(alice_char, bob_char);
    EXPECT_EQ(result, friend_result::not_friends);
}

TEST_F(friend_system_test, blocked_player_cannot_send_request)
{
    social.block_player_friend(bob_char, alice_char);

    auto result = social.send_friend_request(alice_char, bob_char);
    EXPECT_EQ(result, friend_result::is_blocked);
}

TEST_F(friend_system_test, after_unblock_can_send_request)
{
    social.block_player_friend(bob_char, alice_char);
    social.unblock_player_friend(bob_char, alice_char);

    auto result = social.send_friend_request(alice_char, bob_char);
    EXPECT_EQ(result, friend_result::success);
}

// ========== Friend Queries ==========

TEST_F(friend_system_test, friend_count)
{
    EXPECT_EQ(social.friend_count(alice_char), 0);

    social.send_friend_request(alice_char, bob_char);
    social.accept_friend_request(bob_char, alice_char);
    EXPECT_EQ(social.friend_count(alice_char), 1);
    EXPECT_EQ(social.friend_count(bob_char), 1);

    social.send_friend_request(alice_char, charlie_char);
    social.accept_friend_request(charlie_char, alice_char);
    EXPECT_EQ(social.friend_count(alice_char), 2);
    EXPECT_EQ(social.friend_count(charlie_char), 1);
}

TEST_F(friend_system_test, get_incoming_requests)
{
    social.send_friend_request(alice_char, bob_char);
    social.send_friend_request(charlie_char, bob_char);

    auto* incoming = social.get_incoming_requests(bob_char);
    ASSERT_NE(incoming, nullptr);
    EXPECT_EQ(incoming->size(), 2);
}

TEST_F(friend_system_test, get_outgoing_requests)
{
    social.send_friend_request(alice_char, bob_char);
    social.send_friend_request(alice_char, charlie_char);

    auto* outgoing = social.get_outgoing_requests(alice_char);
    ASSERT_NE(outgoing, nullptr);
    EXPECT_EQ(outgoing->size(), 2);
}

TEST_F(friend_system_test, get_blocked_players)
{
    social.block_player_friend(alice_char, bob_char);
    social.block_player_friend(alice_char, charlie_char);

    auto* blocks = social.get_blocked_players(alice_char);
    ASSERT_NE(blocks, nullptr);
    EXPECT_EQ(blocks->size(), 2);
    EXPECT_TRUE(blocks->contains(bob_char));
    EXPECT_TRUE(blocks->contains(charlie_char));
}

TEST_F(friend_system_test, null_returns_for_unknown_player)
{
    auto unknown = player_id{999};
    EXPECT_EQ(social.get_friends(unknown), nullptr);
    EXPECT_EQ(social.get_incoming_requests(unknown), nullptr);
    EXPECT_EQ(social.get_outgoing_requests(unknown), nullptr);
    EXPECT_EQ(social.get_blocked_players(unknown), nullptr);
    EXPECT_FALSE(social.are_friends(unknown, alice_char));
    EXPECT_EQ(social.friend_count(unknown), 0);
}

// ========== Connect/Disconnect ==========

TEST_F(friend_system_test, connect_updates_runtime_ids)
{
    // Make Alice and Bob friends
    social.send_friend_request(alice_char, bob_char);
    social.accept_friend_request(bob_char, alice_char);

    // Both are already connected from SetUp
    auto* alice_friends = social.get_friends(alice_char);
    ASSERT_NE(alice_friends, nullptr);
    ASSERT_EQ(alice_friends->size(), 1);
    EXPECT_TRUE((*alice_friends)[0].is_online());
}

TEST_F(friend_system_test, disconnect_clears_runtime_ids)
{
    social.send_friend_request(alice_char, bob_char);
    social.accept_friend_request(bob_char, alice_char);

    // Disconnect Bob
    social.disconnect_friend(player_id{2}, bob_char);

    // Alice should see Bob as offline
    auto* alice_friends = social.get_friends(alice_char);
    ASSERT_NE(alice_friends, nullptr);
    ASSERT_EQ(alice_friends->size(), 1);
    EXPECT_FALSE((*alice_friends)[0].is_online());
}

TEST_F(friend_system_test, get_friend_runtime_id)
{
    EXPECT_EQ(social.get_friend_runtime_id(alice_char), player_id{1});
    EXPECT_EQ(social.get_friend_runtime_id(bob_char), player_id{2});
    EXPECT_EQ(social.get_friend_runtime_id(player_id{999}), player_id{});

    social.disconnect_friend(player_id{2}, bob_char);
    EXPECT_EQ(social.get_friend_runtime_id(bob_char), player_id{});
}

// ========== Protocol Messages ==========

TEST(friend_protocol_test, parse_friend_target_request)
{
    nlohmann::json j = {{"target_name", "Bob"}};
    auto result = hb::network::friend_target_request_data::from_json(j);
    ASSERT_TRUE(result.is_ok());
    EXPECT_EQ(result.value().target_name, "Bob");
}

TEST(friend_protocol_test, parse_friend_target_request_missing_name)
{
    nlohmann::json j = {};
    auto result = hb::network::friend_target_request_data::from_json(j);
    EXPECT_TRUE(result.is_err());
}

TEST(friend_protocol_test, parse_friend_target_request_empty_name)
{
    nlohmann::json j = {{"target_name", ""}};
    auto result = hb::network::friend_target_request_data::from_json(j);
    EXPECT_TRUE(result.is_err());
}

TEST(friend_protocol_test, make_friend_response_success)
{
    auto msg = hb::network::make_friend_response(42,
        hb::network::json_message_type::friend_request_send_response, true);
    EXPECT_EQ(msg.type, hb::network::json_message_type::friend_request_send_response);
    EXPECT_EQ(msg.seq, 42u);
    EXPECT_TRUE(msg.data["success"].get<bool>());
    EXPECT_FALSE(msg.data.contains("error"));
}

TEST(friend_protocol_test, make_friend_response_failure)
{
    auto msg = hb::network::make_friend_response(42,
        hb::network::json_message_type::friend_request_send_response, false, "is_blocked");
    EXPECT_FALSE(msg.data["success"].get<bool>());
    EXPECT_EQ(msg.data["error"].get<std::string>(), "is_blocked");
}

TEST(friend_protocol_test, make_friend_list_response)
{
    std::vector<hb::network::friend_list_entry_msg> friends = {
        {"Alice", true},
        {"Bob", false}
    };
    std::vector<hb::network::friend_request_msg> incoming = {{"Charlie", false}};
    std::vector<hb::network::friend_request_msg> outgoing = {{"Dave", true}};
    std::vector<std::string> blocked = {"Eve"};

    auto msg = hb::network::make_friend_list_response(1, friends, incoming, outgoing, blocked);
    EXPECT_EQ(msg.type, hb::network::json_message_type::friend_list_response);
    EXPECT_EQ(msg.data["friends"].size(), 2);
    EXPECT_EQ(msg.data["incoming_requests"].size(), 1);
    EXPECT_EQ(msg.data["outgoing_requests"].size(), 1);
    EXPECT_EQ(msg.data["blocked"].size(), 1);
    EXPECT_EQ(msg.data["friends"][0]["name"], "Alice");
    EXPECT_TRUE(msg.data["friends"][0]["is_online"].get<bool>());
    EXPECT_FALSE(msg.data["friends"][1]["is_online"].get<bool>());
}

TEST(friend_protocol_test, make_friend_request_notification)
{
    auto msg = hb::network::make_friend_request_notification("Alice");
    EXPECT_EQ(msg.type, hb::network::json_message_type::friend_request_notification);
    EXPECT_EQ(msg.data["requester_name"], "Alice");
}

TEST(friend_protocol_test, make_friend_accepted_notification)
{
    auto msg = hb::network::make_friend_accepted_notification("Bob");
    EXPECT_EQ(msg.type, hb::network::json_message_type::friend_accepted_notification);
    EXPECT_EQ(msg.data["friend_name"], "Bob");
}

TEST(friend_protocol_test, make_friend_online_notification)
{
    auto msg = hb::network::make_friend_online_notification("Charlie");
    EXPECT_EQ(msg.type, hb::network::json_message_type::friend_online_notification);
    EXPECT_EQ(msg.data["friend_name"], "Charlie");
}

TEST(friend_protocol_test, make_friend_offline_notification)
{
    auto msg = hb::network::make_friend_offline_notification("Dave");
    EXPECT_EQ(msg.type, hb::network::json_message_type::friend_offline_notification);
    EXPECT_EQ(msg.data["friend_name"], "Dave");
}

TEST(friend_protocol_test, message_type_roundtrip)
{
    // Verify all friend message types can be serialized and parsed back
    auto types = {
        hb::network::json_message_type::friend_request_send_request,
        hb::network::json_message_type::friend_request_send_response,
        hb::network::json_message_type::friend_request_accept_request,
        hb::network::json_message_type::friend_request_accept_response,
        hb::network::json_message_type::friend_request_decline_request,
        hb::network::json_message_type::friend_request_decline_response,
        hb::network::json_message_type::friend_request_cancel_request,
        hb::network::json_message_type::friend_request_cancel_response,
        hb::network::json_message_type::friend_remove_request,
        hb::network::json_message_type::friend_remove_response,
        hb::network::json_message_type::friend_block_request,
        hb::network::json_message_type::friend_block_response,
        hb::network::json_message_type::friend_unblock_request,
        hb::network::json_message_type::friend_unblock_response,
        hb::network::json_message_type::friend_list_request,
        hb::network::json_message_type::friend_list_response,
        hb::network::json_message_type::friend_request_notification,
        hb::network::json_message_type::friend_accepted_notification,
        hb::network::json_message_type::friend_online_notification,
        hb::network::json_message_type::friend_offline_notification,
    };

    for (auto type : types) {
        auto str = hb::network::to_string(type);
        EXPECT_NE(str, "unknown") << "Type " << static_cast<int>(type) << " has no string mapping";

        auto parsed = hb::network::parse_message_type(str);
        EXPECT_EQ(parsed, type) << "Roundtrip failed for " << str;
    }
}
