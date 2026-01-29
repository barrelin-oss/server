#pragma once

// session_manager.h
// Session lifecycle management subsystem

#include "core/subsystem.h"
#include "core/types.h"
#include "core/result.h"
#include "session/session.h"

#include <unordered_map>
#include <mutex>
#include <chrono>

namespace hb::session {

// Session events (published to event bus)
struct session_created_event {
    session_id id;
    connection_id connection;
    std::chrono::system_clock::time_point timestamp;
};

struct session_authenticated_event {
    session_id id;
    account_info account;
    std::chrono::system_clock::time_point timestamp;
};

struct session_state_changed_event {
    session_id id;
    session_state old_state;
    session_state new_state;
    std::chrono::system_clock::time_point timestamp;
};

struct session_destroyed_event {
    session_id id;
    std::string reason;
    std::chrono::system_clock::time_point timestamp;
};

// Session manager configuration
struct session_config {
    std::chrono::milliseconds auth_timeout{std::chrono::seconds{30}};
    std::chrono::milliseconds idle_timeout{std::chrono::minutes{15}};
    size_t max_sessions{2000};
};

// Session manager subsystem
class session_manager : public subsystem {
public:
    session_manager();
    ~session_manager() override;

    // Subsystem interface
    [[nodiscard]] auto name() const -> std::string_view override { return "session_manager"; }
    void initialize() override;
    void shutdown() override;
    void update(float delta_time) override;

    // Configuration
    void set_config(const session_config& config);
    [[nodiscard]] auto config() const -> const session_config& { return config_; }

    // Session lifecycle
    auto create_session(connection_id connection) -> result<session_id, std::string>;
    void destroy_session(session_id id, std::string_view reason = "");
    void destroy_session_by_connection(connection_id connection, std::string_view reason = "");

    // Session access
    [[nodiscard]] auto get(session_id id) -> session*;
    [[nodiscard]] auto get(session_id id) const -> const session*;
    [[nodiscard]] auto get_by_connection(connection_id connection) -> session*;
    [[nodiscard]] auto get_by_player(player_id player) -> session*;

    // State management
    void set_session_state(session_id id, session_state state);
    void authenticate_session(session_id id, account_info account);
    void enter_game(session_id id, player_id player);

    // Queries
    [[nodiscard]] auto count() const -> size_t;
    [[nodiscard]] auto count_authenticated() const -> size_t;
    [[nodiscard]] auto count_in_game() const -> size_t;

    // Iteration
    template<typename F>
    void for_each(F&& func) {
        std::lock_guard lock{mutex_};
        for (auto& [id, sess] : sessions_) {
            func(*sess);
        }
    }

    template<typename F>
    void for_each(F&& func) const {
        std::lock_guard lock{mutex_};
        for (const auto& [id, sess] : sessions_) {
            func(*sess);
        }
    }

private:
    [[nodiscard]] auto next_id() -> session_id;
    void check_timeouts();

    session_config config_;
    std::unordered_map<session_id, std::unique_ptr<session>> sessions_;
    std::unordered_map<connection_id, session_id> connection_to_session_;
    std::unordered_map<player_id, session_id> player_to_session_;
    mutable std::mutex mutex_;
    uint32_t next_id_{1};

    // Timeout checking
    float timeout_check_timer_{0.0f};
    static constexpr float timeout_check_interval = 5.0f;  // Check every 5 seconds
};

}  // namespace hb::session
