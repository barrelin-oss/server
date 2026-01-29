#pragma once

// session.h
// Client session state and lifecycle

#include "core/types.h"

#include <string>
#include <chrono>
#include <optional>

namespace hb::session {

// Session state machine
enum class session_state : uint8_t {
    connected = 0,      // Just connected, no auth yet
    authenticating,     // Sent auth request, waiting for response
    character_select,   // Authenticated, choosing character
    entering_world,     // Character selected, loading into world
    in_game,            // Playing in the world
    disconnecting       // Graceful disconnect in progress
};

// Convert session state to string
[[nodiscard]] inline auto state_string(session_state state) -> std::string_view {
    switch (state) {
        case session_state::connected: return "connected";
        case session_state::authenticating: return "authenticating";
        case session_state::character_select: return "character_select";
        case session_state::entering_world: return "entering_world";
        case session_state::in_game: return "in_game";
        case session_state::disconnecting: return "disconnecting";
    }
    return "unknown";
}

// Account information (from auth server)
struct account_info {
    uint32_t account_id{0};
    std::string account_name;
    uint8_t admin_level{0};
    bool is_banned{false};
};

// Character info for selection screen
struct character_info {
    uint32_t character_id{0};
    std::string name;
    uint8_t level{0};
    uint8_t class_type{0};
    uint32_t last_map_id{0};
};

// Client session - represents a connected client
class session {
public:
    session(session_id id, connection_id connection);
    ~session() = default;

    // Non-copyable, movable
    session(const session&) = delete;
    auto operator=(const session&) -> session& = delete;
    session(session&&) noexcept = default;
    auto operator=(session&&) noexcept -> session& = default;

    // Identity
    [[nodiscard]] auto id() const -> session_id { return id_; }
    [[nodiscard]] auto connection() const -> connection_id { return connection_; }

    // State management
    [[nodiscard]] auto state() const -> session_state { return state_; }
    void set_state(session_state state);

    // Account info (set after successful authentication)
    [[nodiscard]] auto account() const -> const std::optional<account_info>& { return account_; }
    void set_account(account_info info);

    // Current character (set when entering world)
    [[nodiscard]] auto current_player() const -> std::optional<hb::player_id> { return current_player_; }
    void set_current_player(hb::player_id id);

    // Character list (set after authentication)
    [[nodiscard]] auto characters() const -> const std::vector<character_info>& { return characters_; }
    void set_characters(std::vector<character_info> chars);

    // Timing
    [[nodiscard]] auto connected_at() const -> std::chrono::steady_clock::time_point { return connected_at_; }
    [[nodiscard]] auto last_activity() const -> std::chrono::steady_clock::time_point { return last_activity_; }
    void update_activity();

    // Utility
    [[nodiscard]] auto is_authenticated() const -> bool { return account_.has_value(); }
    [[nodiscard]] auto is_in_game() const -> bool { return state_ == session_state::in_game; }
    [[nodiscard]] auto is_admin() const -> bool { return account_ && account_->admin_level > 0; }

private:
    session_id id_;
    connection_id connection_;
    session_state state_{session_state::connected};

    std::optional<account_info> account_;
    std::optional<hb::player_id> current_player_;
    std::vector<character_info> characters_;

    std::chrono::steady_clock::time_point connected_at_;
    std::chrono::steady_clock::time_point last_activity_;
};

}  // namespace hb::session
