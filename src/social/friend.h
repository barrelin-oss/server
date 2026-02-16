#pragma once

// friend.h
// Friend list definitions and state

#include "core/types.h"

#include <string>
#include <chrono>
#include <cstdint>

namespace hb::social
{

// Friend request - pending friend request
struct friend_request
{
    player_id requester_char_id{};
    player_id requestee_char_id{};
    std::string requester_name;
    std::string requestee_name;
    std::chrono::system_clock::time_point created_at{};
};

// Friend entry - represents an accepted friendship
struct friend_entry
{
    player_id runtime_id{};   // Runtime ID (0 when offline)
    player_id character_id{}; // Persistent DB character ID
    std::string name;
    std::chrono::system_clock::time_point created_at{};

    [[nodiscard]] auto is_online() const -> bool { return runtime_id.is_valid(); }
};

// Friend list configuration
struct friend_list_config
{
    size_t max_friends{100};
};

// Friend operation results
enum class friend_result : uint8_t
{
    success = 0,
    friend_not_found = 1,
    already_friends = 2,
    friend_limit_reached = 3,
    cannot_add_self = 4,
    is_blocked = 5,
    player_not_found = 6,
    not_friends = 7,
    request_already_exists = 8,
    no_pending_request = 9,
    target_limit_reached = 10,
    internal_error = 11,
};

[[nodiscard]] constexpr auto to_string_view(friend_result r) -> std::string_view
{
    switch (r)
    {
    case friend_result::success:
        return "success";
    case friend_result::friend_not_found:
        return "friend_not_found";
    case friend_result::already_friends:
        return "already_friends";
    case friend_result::friend_limit_reached:
        return "friend_limit_reached";
    case friend_result::cannot_add_self:
        return "cannot_add_self";
    case friend_result::is_blocked:
        return "is_blocked";
    case friend_result::player_not_found:
        return "player_not_found";
    case friend_result::not_friends:
        return "not_friends";
    case friend_result::request_already_exists:
        return "request_already_exists";
    case friend_result::no_pending_request:
        return "no_pending_request";
    case friend_result::target_limit_reached:
        return "target_limit_reached";
    case friend_result::internal_error:
        return "internal_error";
    }
    return "unknown";
}

} // namespace hb::social
