// websocket_server.cpp
// WebSocket server implementation using IXWebSocket

#include "network/websocket_server.h"
#include "core/logger.h"

namespace hb::network {

// ws_connection implementation

ws_connection::ws_connection(connection_id id, ix::WebSocket* socket)
    : id_(id)
    , socket_(socket)
{
}

ws_connection::~ws_connection() {
    // Don't call socket_->close() here - it causes deadlock when the destructor
    // is called from within the IXWebSocket Close callback. The socket is already
    // closing in that case, and calling close() re-entrantly triggers a deadlock.
    //
    // We don't own the socket anyway (note the no-op deleter on line 124), so
    // just let it go. The close() method exists for explicit disconnection.
}

void ws_connection::set_state(ws_connection_state state) {
    state_ = state;
}

void ws_connection::set_account(account_id account) {
    account_ = account;
}

void ws_connection::set_player(player_id player) {
    player_ = player;
}

void ws_connection::set_session_token(std::string_view token) {
    session_token_ = std::string(token);
}

void ws_connection::send(const json_message& msg) {
    if (!socket_ || state_ == ws_connection_state::disconnected ||
        state_ == ws_connection_state::disconnecting) {
        return;
    }

    if (socket_->getReadyState() != ix::ReadyState::Open) {
        return;
    }

    try {
        auto json_str = msg.to_json().dump();
        // LOG_DEBUG(network, "[SEND conn={}] {}", id_.value, json_str);
        socket_->send(json_str);
    } catch (const std::exception& e) {
        LOG_ERROR(network, "Failed to send message to connection {}: {}", id_.value, e.what());
    }
}

void ws_connection::send_raw(std::string_view data) {
    if (!socket_ || state_ == ws_connection_state::disconnected ||
        state_ == ws_connection_state::disconnecting) {
        return;
    }

    if (socket_->getReadyState() != ix::ReadyState::Open) {
        return;
    }

    socket_->send(std::string(data));
}

void ws_connection::close(uint16_t code, std::string_view reason) {
    if (socket_ && state_ != ws_connection_state::disconnecting &&
        state_ != ws_connection_state::disconnected) {
        state_ = ws_connection_state::disconnecting;
        socket_->close(code, std::string(reason));
    }
}

auto ws_connection::is_open() const -> bool {
    return socket_ && state_ != ws_connection_state::disconnected &&
           state_ != ws_connection_state::disconnecting &&
           socket_->getReadyState() == ix::ReadyState::Open;
}

void ws_connection::set_remote_address(std::string_view addr) {
    remote_address_ = std::string(addr);
}

// websocket_server implementation

websocket_server::websocket_server() = default;

websocket_server::~websocket_server() {
    stop();
}

void websocket_server::set_config(const websocket_config& config) {
    config_ = config;
}

auto websocket_server::start() -> hb::result<void, std::string> {
    if (running_) {
        return hb::result<void, std::string>::err("Server already running");
    }

    LOG_INFO(network, "Starting WebSocket server on {}:{}", config_.bind_address, config_.port);

    try {
        server_ = std::make_unique<ix::WebSocketServer>(
            config_.port,
            config_.bind_address
        );

        // Configure server
        server_->setOnClientMessageCallback(
            [this](std::shared_ptr<ix::ConnectionState> connection_state,
                   ix::WebSocket& ws,
                   const ix::WebSocketMessagePtr& msg) {

                // Get the raw pointer for map lookup
                ix::ConnectionState* state_ptr = connection_state.get();

                // Find our connection wrapper using the state pointer
                connection_id conn_id{0};
                {
                    std::lock_guard lock{mutex_};
                    auto it = state_to_connection_.find(state_ptr);
                    if (it != state_to_connection_.end()) {
                        conn_id = it->second;
                    }
                }

                switch (msg->type) {
                    case ix::WebSocketMessageType::Open: {
                        // New connection
                        auto new_id = next_connection_id();
                        if (!new_id.is_valid()) {
                            LOG_ERROR(network, "Connection ID overflow - rejecting connection");
                            ws.close(1013, "Server overloaded");
                            break;
                        }

                        auto conn = std::make_unique<ws_connection>(new_id, &ws);
                        conn->set_remote_address(connection_state->getRemoteIp());
                        conn->set_state(ws_connection_state::connected);

                        {
                            std::lock_guard lock{mutex_};
                            connections_[new_id] = std::move(conn);
                            state_to_connection_[state_ptr] = new_id;
                        }

                        LOG_INFO(network, "WebSocket connection {} from {}",
                            new_id.value, connection_state->getRemoteIp());

                        if (connect_handler_) {
                            connect_handler_(new_id, connection_state->getRemoteIp());
                        }
                        break;
                    }

                    case ix::WebSocketMessageType::Close: {
                        if (conn_id.is_valid()) {
                            LOG_INFO(network, "WebSocket connection {} closed: {} ({})",
                                conn_id.value, msg->closeInfo.code, msg->closeInfo.reason);

                            // Invalidate socket pointer and mark as disconnected immediately
                            // to prevent further sends from this thread
                            {
                                std::lock_guard lock{mutex_};
                                auto it = connections_.find(conn_id);
                                if (it != connections_.end()) {
                                    it->second->invalidate_socket();
                                    it->second->set_state(ws_connection_state::disconnected);
                                }
                            }

                            // Queue disconnect for main thread processing
                            // This avoids modifying lookup maps from the callback thread
                            {
                                std::lock_guard lock{disconnect_mutex_};
                                pending_disconnects_.push_back({
                                    conn_id,
                                    msg->closeInfo.reason,
                                    state_ptr
                                });
                            }
                        }
                        break;
                    }

                    case ix::WebSocketMessageType::Message: {
                        if (!conn_id.is_valid()) {
                            LOG_WARN(network, "Message from unknown connection");
                            break;
                        }

                        // LOG_DEBUG(network, "[RECV conn={}] {}", conn_id.value, msg->str);

                        // Parse JSON message
                        auto parse_result = json_message::parse(msg->str);
                        if (parse_result.is_err()) {
                            LOG_WARN(network, "Failed to parse message from {}: {}",
                                conn_id.value, parse_result.error());

                            // Send error response
                            auto error_msg = make_error_response(0, "parse_error", parse_result.error());
                            ws.send(error_msg.to_json().dump());
                            break;
                        }

                        if (message_handler_) {
                            message_handler_(conn_id, parse_result.value());
                        }
                        break;
                    }

                    case ix::WebSocketMessageType::Error: {
                        LOG_ERROR(network, "WebSocket error for connection {}: {}",
                            conn_id.value, msg->errorInfo.reason);

                        if (error_handler_ && conn_id.is_valid()) {
                            error_handler_(conn_id, msg->errorInfo.reason);
                        }
                        break;
                    }

                    case ix::WebSocketMessageType::Ping:
                    case ix::WebSocketMessageType::Pong:
                    case ix::WebSocketMessageType::Fragment:
                        // Handled automatically by IXWebSocket
                        break;
                }
            }
        );

        // Start listening
        auto listen_result = server_->listen();
        if (!listen_result.first) {
            return hb::result<void, std::string>::err("Failed to start listening: " + listen_result.second);
        }

        // Start the server (non-blocking)
        server_->start();
        running_ = true;

        LOG_INFO(network, "WebSocket server started on {}:{}", config_.bind_address, config_.port);

        return hb::result<void, std::string>::ok();

    } catch (const std::exception& ex) {
        return hb::result<void, std::string>::err(std::string("Failed to start server: ") + ex.what());
    }
}

void websocket_server::stop() {
    if (!running_) {
        return;
    }

    LOG_INFO(network, "Stopping WebSocket server");

    running_ = false;

    // Close all connections
    disconnect_all("Server shutting down");

    if (server_) {
        server_->stop();
        server_.reset();
    }

    // Process any remaining pending disconnects
    process_pending_disconnects();

    {
        std::lock_guard lock{mutex_};
        connections_.clear();
        account_to_connection_.clear();
        player_to_connection_.clear();
        state_to_connection_.clear();
    }

    {
        std::lock_guard lock{disconnect_mutex_};
        pending_disconnects_.clear();
    }

    LOG_INFO(network, "WebSocket server stopped");
}

auto websocket_server::is_running() const -> bool {
    return running_;
}

void websocket_server::on_message(message_handler handler) {
    message_handler_ = std::move(handler);
}

void websocket_server::on_connect(connect_handler handler) {
    connect_handler_ = std::move(handler);
}

void websocket_server::on_disconnect(disconnect_handler handler) {
    disconnect_handler_ = std::move(handler);
}

void websocket_server::on_error(error_handler handler) {
    error_handler_ = std::move(handler);
}

auto websocket_server::get_connection(connection_id id) -> ws_connection* {
    std::lock_guard lock{mutex_};
    auto it = connections_.find(id);
    return it != connections_.end() ? it->second.get() : nullptr;
}

auto websocket_server::get_connection(connection_id id) const -> const ws_connection* {
    std::lock_guard lock{mutex_};
    auto it = connections_.find(id);
    return it != connections_.end() ? it->second.get() : nullptr;
}

auto websocket_server::get_connection_by_account(account_id id) -> ws_connection* {
    std::lock_guard lock{mutex_};
    auto it = account_to_connection_.find(id);
    if (it == account_to_connection_.end()) {
        return nullptr;
    }
    auto conn_it = connections_.find(it->second);
    return conn_it != connections_.end() ? conn_it->second.get() : nullptr;
}

auto websocket_server::get_connection_by_player(player_id id) -> ws_connection* {
    std::lock_guard lock{mutex_};
    auto it = player_to_connection_.find(id);
    if (it == player_to_connection_.end()) {
        return nullptr;
    }
    auto conn_it = connections_.find(it->second);
    return conn_it != connections_.end() ? conn_it->second.get() : nullptr;
}

void websocket_server::disconnect(connection_id id, std::string_view reason) {
    std::lock_guard lock{mutex_};
    auto it = connections_.find(id);
    if (it != connections_.end()) {
        it->second->close(1000, reason);
    }
}

void websocket_server::disconnect_all(std::string_view reason) {
    // Collect IDs first to avoid holding lock while calling close()
    // (close() may trigger callbacks that try to acquire the lock)
    std::vector<connection_id> ids;
    {
        std::lock_guard lock{mutex_};
        ids.reserve(connections_.size());
        for (const auto& [id, conn] : connections_) {
            ids.push_back(id);
        }
    }

    // Now disconnect without holding the lock
    for (auto id : ids) {
        disconnect(id, reason);
    }
}

void websocket_server::register_account(connection_id conn, account_id account) {
    std::lock_guard lock{mutex_};
    // Remove any existing mapping for this account (handles reconnect case)
    account_to_connection_.erase(account);
    account_to_connection_[account] = conn;
}

void websocket_server::register_player(connection_id conn, player_id player) {
    std::lock_guard lock{mutex_};
    // Remove any existing mapping for this player
    player_to_connection_.erase(player);
    player_to_connection_[player] = conn;
}

void websocket_server::send(connection_id id, const json_message& msg) {
    std::lock_guard lock{mutex_};
    auto it = connections_.find(id);
    if (it != connections_.end()) {
        it->second->send(msg);
    }
}

void websocket_server::broadcast(const json_message& msg) {
    std::lock_guard lock{mutex_};
    auto json_str = msg.to_json().dump();
    for (auto& [id, conn] : connections_) {
        if (conn->is_open()) {
            conn->send_raw(json_str);
        }
    }
}

void websocket_server::broadcast_to_authenticated(const json_message& msg) {
    std::lock_guard lock{mutex_};
    auto json_str = msg.to_json().dump();
    for (auto& [id, conn] : connections_) {
        if (conn->is_open() &&
            (conn->state() == ws_connection_state::authenticated ||
             conn->state() == ws_connection_state::in_game)) {
            conn->send_raw(json_str);
        }
    }
}

auto websocket_server::get_admin_subscribers(map_id map) -> std::vector<connection_id> {
    std::vector<connection_id> result;
    std::lock_guard lock{mutex_};
    for (const auto& [id, conn] : connections_) {
        if (conn->state() != ws_connection_state::admin_dashboard) continue;
        const auto& sub = conn->subscription();
        if (sub.sub_mode == admin_subscription::mode::map && sub.target_map == map) {
            result.push_back(id);
        } else if (sub.sub_mode == admin_subscription::mode::player) {
            // Player-follow mode: the target_map is updated when the followed player moves maps
            if (sub.target_map == map) {
                result.push_back(id);
            }
        }
    }
    return result;
}

auto websocket_server::get_all_admin_connections() -> std::vector<connection_id> {
    std::vector<connection_id> result;
    std::lock_guard lock{mutex_};
    for (const auto& [id, conn] : connections_) {
        if (conn->state() == ws_connection_state::admin_dashboard && conn->is_open()) {
            result.push_back(id);
        }
    }
    return result;
}

auto websocket_server::connection_count() const -> size_t {
    std::lock_guard lock{mutex_};
    return connections_.size();
}

auto websocket_server::authenticated_count() const -> size_t {
    std::lock_guard lock{mutex_};
    size_t count = 0;
    for (const auto& [id, conn] : connections_) {
        if (conn->state() == ws_connection_state::authenticated ||
            conn->state() == ws_connection_state::in_game) {
            ++count;
        }
    }
    return count;
}

auto websocket_server::next_connection_id() -> connection_id {
    // Check for overflow - reserve 0 as invalid
    // Note: Extra parentheses around max() prevent Windows macro expansion
    uint32_t current = next_id_.load();
    if (current == 0 || current == (std::numeric_limits<uint32_t>::max)()) {
        // Try to find a free ID by scanning existing connections
        std::lock_guard lock{mutex_};
        for (uint32_t candidate = 1; candidate < (std::numeric_limits<uint32_t>::max)(); ++candidate) {
            if (!connections_.contains(connection_id{candidate})) {
                next_id_.store(candidate + 1);
                return connection_id{candidate};
            }
        }
        // No free IDs available
        return connection_id{0};
    }
    return connection_id{next_id_++};
}

void websocket_server::process_pending_disconnects() {
    // Swap out the pending list to minimize lock hold time
    std::vector<pending_disconnect> to_process;
    {
        std::lock_guard lock{disconnect_mutex_};
        to_process.swap(pending_disconnects_);
    }

    for (const auto& pd : to_process) {
        // Call disconnect handler on main thread
        if (disconnect_handler_) {
            disconnect_handler_(pd.id, pd.reason);
        }

        // Now safe to cleanup on main thread
        cleanup_connection(pd.id);

        // Remove state mapping
        {
            std::lock_guard lock{mutex_};
            state_to_connection_.erase(pd.state_ptr);
        }
    }
}

void websocket_server::cleanup_connection(connection_id conn_id) {
    std::lock_guard lock{mutex_};
    auto it = connections_.find(conn_id);
    if (it != connections_.end()) {
        // Remove from lookup maps
        if (it->second->account().is_valid()) {
            account_to_connection_.erase(it->second->account());
        }
        if (it->second->player().is_valid()) {
            player_to_connection_.erase(it->second->player());
        }
        connections_.erase(it);
    }
}

}  // namespace hb::network
