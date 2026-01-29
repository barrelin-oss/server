// session_manager.cpp
// Session manager subsystem implementation

#include "session/session_manager.h"
#include "core/logger.h"
#include "core/event_bus.h"

namespace hb::session {

session_manager::session_manager() = default;

session_manager::~session_manager() {
    if (is_initialized()) {
        shutdown();
    }
}

void session_manager::initialize() {
    LOG_INFO(general, "Session manager initializing...");
    set_initialized(true);
    LOG_INFO(general, "Session manager initialized");
}

void session_manager::shutdown() {
    LOG_INFO(general, "Session manager shutting down...");

    {
        std::lock_guard lock{mutex_};
        sessions_.clear();
        connection_to_session_.clear();
        player_to_session_.clear();
    }

    set_initialized(false);
    LOG_INFO(general, "Session manager shutdown complete");
}

void session_manager::update(float delta_time) {
    timeout_check_timer_ += delta_time;
    if (timeout_check_timer_ >= timeout_check_interval) {
        timeout_check_timer_ = 0.0f;
        check_timeouts();
    }
}

void session_manager::set_config(const session_config& config) {
    config_ = config;
}

auto session_manager::create_session(connection_id connection) -> result<session_id, std::string> {
    std::lock_guard lock{mutex_};

    // Check if connection already has a session
    if (connection_to_session_.contains(connection)) {
        return result<session_id, std::string>::err("Connection already has a session");
    }

    // Check session limit
    if (sessions_.size() >= config_.max_sessions) {
        return result<session_id, std::string>::err("Session limit reached");
    }

    auto id = next_id();
    auto sess = std::make_unique<session>(id, connection);

    sessions_[id] = std::move(sess);
    connection_to_session_[connection] = id;

    LOG_INFO(general, "Session created: {} for connection {}",
        id.value, connection.value);

    // Publish event (outside lock would be safer but adds complexity)
    event_bus().publish(session_created_event{
        .id = id,
        .connection = connection,
        .timestamp = std::chrono::system_clock::now()
    });

    return result<session_id, std::string>::ok(id);
}

void session_manager::destroy_session(session_id id, std::string_view reason) {
    std::unique_ptr<session> sess;

    {
        std::lock_guard lock{mutex_};

        auto it = sessions_.find(id);
        if (it == sessions_.end()) {
            return;
        }

        sess = std::move(it->second);
        connection_to_session_.erase(sess->connection());
        if (sess->current_player()) {
            player_to_session_.erase(*sess->current_player());
        }
        sessions_.erase(it);
    }

    LOG_INFO(general, "Session destroyed: {} - {}",
        id.value, reason.empty() ? "no reason" : reason);

    event_bus().publish(session_destroyed_event{
        .id = id,
        .reason = std::string(reason),
        .timestamp = std::chrono::system_clock::now()
    });
}

void session_manager::destroy_session_by_connection(connection_id connection, std::string_view reason) {
    session_id id;
    {
        std::lock_guard lock{mutex_};
        auto it = connection_to_session_.find(connection);
        if (it == connection_to_session_.end()) {
            return;
        }
        id = it->second;
    }
    destroy_session(id, reason);
}

auto session_manager::get(session_id id) -> session* {
    std::lock_guard lock{mutex_};
    auto it = sessions_.find(id);
    return it != sessions_.end() ? it->second.get() : nullptr;
}

auto session_manager::get(session_id id) const -> const session* {
    std::lock_guard lock{mutex_};
    auto it = sessions_.find(id);
    return it != sessions_.end() ? it->second.get() : nullptr;
}

auto session_manager::get_by_connection(connection_id connection) -> session* {
    std::lock_guard lock{mutex_};
    auto it = connection_to_session_.find(connection);
    if (it == connection_to_session_.end()) {
        return nullptr;
    }
    auto sess_it = sessions_.find(it->second);
    return sess_it != sessions_.end() ? sess_it->second.get() : nullptr;
}

auto session_manager::get_by_player(player_id player) -> session* {
    std::lock_guard lock{mutex_};
    auto it = player_to_session_.find(player);
    if (it == player_to_session_.end()) {
        return nullptr;
    }
    auto sess_it = sessions_.find(it->second);
    return sess_it != sessions_.end() ? sess_it->second.get() : nullptr;
}

void session_manager::set_session_state(session_id id, session_state state) {
    session* sess = nullptr;
    session_state old_state;

    {
        std::lock_guard lock{mutex_};
        auto it = sessions_.find(id);
        if (it == sessions_.end()) {
            return;
        }
        sess = it->second.get();
        old_state = sess->state();
        sess->set_state(state);
    }

    LOG_DEBUG(general, "Session {} state changed: {} -> {}",
        id.value, state_string(old_state), state_string(state));

    event_bus().publish(session_state_changed_event{
        .id = id,
        .old_state = old_state,
        .new_state = state,
        .timestamp = std::chrono::system_clock::now()
    });
}

void session_manager::authenticate_session(session_id id, account_info account) {
    {
        std::lock_guard lock{mutex_};
        auto it = sessions_.find(id);
        if (it == sessions_.end()) {
            return;
        }
        it->second->set_account(account);
        it->second->set_state(session_state::character_select);
    }

    LOG_INFO(general, "Session {} authenticated as {} (admin level {})",
        id.value, account.account_name, account.admin_level);

    event_bus().publish(session_authenticated_event{
        .id = id,
        .account = account,
        .timestamp = std::chrono::system_clock::now()
    });
}

void session_manager::enter_game(session_id id, player_id player) {
    {
        std::lock_guard lock{mutex_};
        auto it = sessions_.find(id);
        if (it == sessions_.end()) {
            return;
        }
        it->second->set_current_player(player);
        it->second->set_state(session_state::in_game);
        player_to_session_[player] = id;
    }

    LOG_INFO(general, "Session {} entered game with player {}",
        id.value, player.value);
}

auto session_manager::count() const -> size_t {
    std::lock_guard lock{mutex_};
    return sessions_.size();
}

auto session_manager::count_authenticated() const -> size_t {
    std::lock_guard lock{mutex_};
    size_t count = 0;
    for (const auto& [id, sess] : sessions_) {
        if (sess->is_authenticated()) {
            ++count;
        }
    }
    return count;
}

auto session_manager::count_in_game() const -> size_t {
    std::lock_guard lock{mutex_};
    size_t count = 0;
    for (const auto& [id, sess] : sessions_) {
        if (sess->is_in_game()) {
            ++count;
        }
    }
    return count;
}

auto session_manager::next_id() -> session_id {
    return session_id{next_id_++};
}

void session_manager::check_timeouts() {
    auto now = std::chrono::steady_clock::now();
    std::vector<std::pair<session_id, std::string>> to_destroy;

    {
        std::lock_guard lock{mutex_};
        for (const auto& [id, sess] : sessions_) {
            auto idle_time = std::chrono::duration_cast<std::chrono::milliseconds>(
                now - sess->last_activity()
            );

            // Check auth timeout for unauthenticated sessions
            if (!sess->is_authenticated() && idle_time > config_.auth_timeout) {
                to_destroy.emplace_back(id, "Authentication timeout");
                continue;
            }

            // Check idle timeout for all sessions
            if (idle_time > config_.idle_timeout) {
                to_destroy.emplace_back(id, "Idle timeout");
            }
        }
    }

    for (const auto& [id, reason] : to_destroy) {
        destroy_session(id, reason);
    }
}

}  // namespace hb::session
