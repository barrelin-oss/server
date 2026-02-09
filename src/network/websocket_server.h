#pragma once

// websocket_server.h
// WebSocket server using IXWebSocket library

#include "core/types.h"
#include "core/result.h"
#include "network/json_protocol.h"

#include <ixwebsocket/IXWebSocketServer.h>
#include <string>
#include <string_view>
#include <functional>
#include <unordered_map>
#include <vector>
#include <mutex>
#include <memory>
#include <atomic>

namespace hb::network {

// Forward declarations
class ws_connection;

// WebSocket server configuration
struct websocket_config {
    std::string bind_address = "0.0.0.0";
    uint16_t port = 2848;
    int max_connections = 2000;
    bool enable_ping = true;
    int ping_interval_seconds = 30;
    bool enable_per_message_deflate = false;  // Compression
};

// WebSocket event types
enum class ws_event_type {
    connected,
    disconnected,
    message,
    error
};

// WebSocket connection state
enum class ws_connection_state {
    connecting,
    connected,
    authenticated,
    in_game,
    admin_dashboard,
    disconnecting,
    disconnected
};

// Admin subscription mode for spectator functionality
struct admin_subscription {
    enum class mode : uint8_t { none, map, player };
    mode sub_mode{mode::none};
    map_id target_map{};
    player_id target_player{};
};

// Connection info for events
struct ws_connection_info {
    connection_id id;
    std::string remote_address;
    ws_connection_state state;
};

// Message handler callback type
using message_handler = std::function<void(connection_id, const json_message&)>;
using connect_handler = std::function<void(connection_id, const std::string& remote_address)>;
using disconnect_handler = std::function<void(connection_id, const std::string& reason)>;
using error_handler = std::function<void(connection_id, const std::string& error)>;

// WebSocket connection wrapper
class ws_connection {
public:
    ws_connection(connection_id id, ix::WebSocket* socket);
    ~ws_connection();

    // Non-copyable
    ws_connection(const ws_connection&) = delete;
    auto operator=(const ws_connection&) -> ws_connection& = delete;

    // Movable
    ws_connection(ws_connection&&) noexcept = default;
    auto operator=(ws_connection&&) noexcept -> ws_connection& = default;

    // Properties
    [[nodiscard]] auto id() const -> connection_id { return id_; }
    [[nodiscard]] auto remote_address() const -> const std::string& { return remote_address_; }
    [[nodiscard]] auto state() const -> ws_connection_state { return state_; }

    // State management
    void set_state(ws_connection_state state);
    void set_account(account_id account);
    void set_player(player_id player);
    [[nodiscard]] auto account() const -> account_id { return account_; }
    [[nodiscard]] auto player() const -> player_id { return player_; }

    // Session token (for authenticated connections)
    void set_session_token(std::string_view token);
    [[nodiscard]] auto session_token() const -> const std::string& { return session_token_; }

    // Mouse destination tracking (where client clicked to walk to)
    void set_destination(int16_t x, int16_t y) { dest_x_ = x; dest_y_ = y; has_destination_ = true; }
    void clear_destination() { dest_x_ = 0; dest_y_ = 0; has_destination_ = false; }
    [[nodiscard]] auto has_destination() const -> bool { return has_destination_; }
    [[nodiscard]] auto dest_x() const -> int16_t { return dest_x_; }
    [[nodiscard]] auto dest_y() const -> int16_t { return dest_y_; }

    // Admin dashboard
    void set_admin_level(uint8_t level) { admin_level_ = level; }
    [[nodiscard]] auto admin_level() const -> uint8_t { return admin_level_; }
    void set_subscription(const admin_subscription& sub) { subscription_ = sub; }
    [[nodiscard]] auto subscription() const -> const admin_subscription& { return subscription_; }
    [[nodiscard]] auto subscription() -> admin_subscription& { return subscription_; }

    // Send message
    void send(const json_message& msg);
    void send_raw(std::string_view data);

    // Close connection
    void close(uint16_t code = 1000, std::string_view reason = "");

    // Check if connection is open
    [[nodiscard]] auto is_open() const -> bool;

    // Invalidate socket pointer (called from disconnect callback)
    void invalidate_socket() { socket_ = nullptr; }

private:
    friend class websocket_server;

    void set_remote_address(std::string_view addr);

    connection_id id_;
    ix::WebSocket* socket_;  // Raw pointer - IXWebSocket owns this
    std::string remote_address_;
    ws_connection_state state_{ws_connection_state::connecting};
    account_id account_{};
    player_id player_{};
    std::string session_token_;

    // Mouse destination (where client clicked to walk to)
    int16_t dest_x_{0};
    int16_t dest_y_{0};
    bool has_destination_{false};

    // Admin dashboard
    uint8_t admin_level_{0};
    admin_subscription subscription_;
};

// WebSocket server
class websocket_server {
public:
    websocket_server();
    ~websocket_server();

    // Non-copyable, non-movable
    websocket_server(const websocket_server&) = delete;
    auto operator=(const websocket_server&) -> websocket_server& = delete;
    websocket_server(websocket_server&&) = delete;
    auto operator=(websocket_server&&) -> websocket_server& = delete;

    // Configuration
    void set_config(const websocket_config& config);

    // Start/stop
    [[nodiscard]] auto start() -> hb::result<void, std::string>;
    void stop();
    [[nodiscard]] auto is_running() const -> bool;

    // Event handlers
    void on_message(message_handler handler);
    void on_connect(connect_handler handler);
    void on_disconnect(disconnect_handler handler);
    void on_error(error_handler handler);

    // Connection access
    [[nodiscard]] auto get_connection(connection_id id) -> ws_connection*;
    [[nodiscard]] auto get_connection(connection_id id) const -> const ws_connection*;
    [[nodiscard]] auto get_connection_by_account(account_id id) -> ws_connection*;
    [[nodiscard]] auto get_connection_by_player(player_id id) -> ws_connection*;

    // Connection management
    void disconnect(connection_id id, std::string_view reason = "");
    void disconnect_all(std::string_view reason = "");

    // Account/player registration for lookup maps
    void register_account(connection_id conn, account_id account);
    void register_player(connection_id conn, player_id player);

    // Send messages
    void send(connection_id id, const json_message& msg);
    void broadcast(const json_message& msg);
    void broadcast_to_authenticated(const json_message& msg);

    // Admin subscriber queries
    [[nodiscard]] auto get_admin_subscribers(map_id map) -> std::vector<connection_id>;
    [[nodiscard]] auto get_all_admin_connections() -> std::vector<connection_id>;

    // Statistics
    [[nodiscard]] auto connection_count() const -> size_t;
    [[nodiscard]] auto authenticated_count() const -> size_t;

    // Process pending disconnects - call from main thread periodically
    void process_pending_disconnects();

    // Iteration
    template<typename Func>
    void for_each_connection(Func&& func) {
        std::lock_guard lock{mutex_};
        for (auto& [id, conn] : connections_) {
            func(*conn);
        }
    }

private:
    void handle_connection(std::weak_ptr<ix::WebSocket> socket);
    [[nodiscard]] auto next_connection_id() -> connection_id;
    void cleanup_connection(connection_id conn_id);

    websocket_config config_;
    std::unique_ptr<ix::WebSocketServer> server_;
    std::atomic<bool> running_{false};

    // Connections
    std::unordered_map<connection_id, std::unique_ptr<ws_connection>> connections_;
    std::unordered_map<account_id, connection_id> account_to_connection_;
    std::unordered_map<player_id, connection_id> player_to_connection_;
    std::unordered_map<ix::ConnectionState*, connection_id> state_to_connection_;
    mutable std::mutex mutex_;
    std::atomic<uint32_t> next_id_{1};

    // Pending disconnects (thread-safe queue for main thread processing)
    struct pending_disconnect {
        connection_id id;
        std::string reason;
        ix::ConnectionState* state_ptr;
    };
    std::vector<pending_disconnect> pending_disconnects_;
    std::mutex disconnect_mutex_;

    // Event handlers
    message_handler message_handler_;
    connect_handler connect_handler_;
    disconnect_handler disconnect_handler_;
    error_handler error_handler_;
};

}  // namespace hb::network
