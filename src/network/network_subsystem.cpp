// network_subsystem.cpp
// Main network orchestrator implementation

#include "network/network_subsystem.h"
#include "bridge/message_router.h"
#include "core/logger.h"
#include "core/event_bus.h"

#include <algorithm>

namespace hb::network {

network_subsystem::network_subsystem() = default;

network_subsystem::~network_subsystem() {
    if (is_initialized()) {
        shutdown();
    }
}

void network_subsystem::initialize() {
    LOG_INFO(network, "Network subsystem initializing...");

    // Initialize platform sockets (Windows needs WSAStartup)
    auto result = initialize_sockets();
    if (result.is_err()) {
        LOG_ERROR(network, "Failed to initialize sockets: {}", result.error());
        return;
    }

    set_initialized(true);
    LOG_INFO(network, "Network subsystem initialized");
}

void network_subsystem::shutdown() {
    LOG_INFO(network, "Network subsystem shutting down...");

    stop();
    cleanup_sockets();

    set_initialized(false);
    LOG_INFO(network, "Network subsystem shutdown complete");
}

void network_subsystem::update(float /*delta_time*/) {
    // Main update is handled in network thread
    // This could be used for periodic stats logging or other main-thread tasks
}

auto network_subsystem::start(uint16_t port) -> result<void, std::string> {
    network_config config;
    config.port = port;
    return start(config);
}

auto network_subsystem::start(const network_config& config) -> result<void, std::string> {
    if (running_.load()) {
        return result<void, std::string>::err("Network already running");
    }

    config_ = config;

    // Start listener
    auto listen_result = listener_.start(config_.port);
    if (listen_result.is_err()) {
        return listen_result;
    }

    LOG_INFO(network, "Network listening on port {}", config_.port);

    // Start network thread
    running_ = true;
    network_thread_ = std::jthread([this](std::stop_token st) {
        while (!st.stop_requested() && running_.load()) {
            network_thread_func();
        }
    });

    return result<void, std::string>::ok();
}

void network_subsystem::stop() {
    if (!running_.load()) {
        return;
    }

    LOG_INFO(network, "Stopping network...");

    running_ = false;

    // Stop the network thread
    if (network_thread_.joinable()) {
        network_thread_.request_stop();
        network_thread_.join();
    }

    // Disconnect all clients
    connections_.remove_all();

    // Stop listener
    listener_.stop();

    LOG_INFO(network, "Network stopped");
}

void network_subsystem::send(connection_id id, std::span<const uint8_t> data) {
    if (auto* conn = connections_.get(id)) {
        conn->send(data);
    }
}

void network_subsystem::send_message(connection_id id, const message_writer& writer) {
    send(id, writer.data());
}

void network_subsystem::broadcast(std::span<const uint8_t> data) {
    connections_.for_each([&data](connection& conn) {
        if (conn.is_connected()) {
            conn.send(data);
        }
    });
}

void network_subsystem::broadcast_message(const message_writer& writer) {
    broadcast(writer.data());
}

void network_subsystem::disconnect(connection_id id, std::string_view reason) {
    handle_disconnect(id, reason);
}

auto network_subsystem::is_running() const -> bool {
    return running_.load();
}

auto network_subsystem::connection_count() const -> size_t {
    return connections_.count();
}

auto network_subsystem::get_connection(connection_id id) -> connection* {
    return connections_.get(id);
}

void network_subsystem::set_message_callback(message_callback callback) {
    std::lock_guard lock{callback_mutex_};
    message_callback_ = std::move(callback);
}

void network_subsystem::set_message_router(bridge::message_router* router) {
    std::lock_guard lock{callback_mutex_};
    message_router_ = router;
    if (router) {
        LOG_INFO(network, "Message router attached to network subsystem");
    }
}

auto network_subsystem::message_router() -> bridge::message_router* {
    return message_router_;
}

auto network_subsystem::uses_message_router() const -> bool {
    return config_.use_message_router && message_router_ != nullptr;
}

void network_subsystem::network_thread_func() {
    accept_connections();
    process_connections();
    check_timeouts();

    // Small sleep to prevent busy-looping
    std::this_thread::sleep_for(config_.poll_interval);
}

void network_subsystem::accept_connections() {
    // Accept new connections (non-blocking)
    while (true) {
        auto accept_result = listener_.accept();

        if (accept_result.is_err()) {
            if (accept_result.error() == socket_error::would_block) {
                // No pending connections
                break;
            }
            // Accept error, but keep trying
            LOG_WARN(network, "Accept error: {}", error_string(accept_result.error()));
            break;
        }

        auto [socket, address] = std::move(accept_result.value());

        // Check connection limit
        if (connections_.count() >= config_.max_connections) {
            LOG_WARN(network, "Connection limit reached, rejecting {}:{}",
                address.ip, address.port);
            socket.close();
            continue;
        }

        // Add connection
        auto id = connections_.add(std::move(socket), address);
        ++total_connections_;

        LOG_INFO(network, "Client connected: {} ({}:{})",
            id.value, address.ip, address.port);

        // Publish event
        event_bus().publish(client_connected_event{
            .id = id,
            .address = address,
            .timestamp = std::chrono::system_clock::now()
        });
    }
}

void network_subsystem::process_connections() {
    std::vector<connection_id> to_remove;

    connections_.for_each([this, &to_remove](connection& conn) {
        if (!conn.is_connected()) {
            to_remove.push_back(conn.id());
            return;
        }

        // Process sends
        if (!conn.process_send()) {
            to_remove.push_back(conn.id());
            return;
        }

        // Process receives
        if (!conn.process_receive()) {
            to_remove.push_back(conn.id());
            return;
        }

        // Process complete messages
        while (conn.has_pending_message()) {
            auto message = conn.extract_message();
            if (message.empty()) {
                break;
            }

            // Dispatch via message router, callback, or event (in priority order)
            std::lock_guard lock{callback_mutex_};

            if (config_.use_message_router && message_router_) {
                // Use modern message router for dispatch
                message_router_->route(conn.id(), message);
            } else if (message_callback_) {
                // Use callback for dispatch
                message_callback_(conn.id(), message);
            } else {
                // Fall back to event bus
                event_bus().publish(data_received_event{
                    .id = conn.id(),
                    .data = std::move(message)
                });
            }
        }
    });

    // Remove disconnected connections
    for (auto id : to_remove) {
        handle_disconnect(id, "Connection lost");
    }
}

void network_subsystem::check_timeouts() {
    std::vector<connection_id> timed_out;

    connections_.for_each([this, &timed_out](connection& conn) {
        if (conn.idle_duration() > config_.idle_timeout) {
            timed_out.push_back(conn.id());
        }
    });

    for (auto id : timed_out) {
        handle_disconnect(id, "Idle timeout");
    }
}

void network_subsystem::handle_disconnect(connection_id id, std::string_view reason) {
    auto* conn = connections_.get(id);
    if (!conn) {
        return;
    }

    auto address = conn->address();

    LOG_INFO(network, "Client disconnected: {} ({}:{}) - {}",
        id.value, address.ip, address.port, reason);

    // Notify message router to clean up session/player mappings
    if (message_router_) {
        message_router_->clear_connection(id);
    }

    // Publish event before removal
    event_bus().publish(client_disconnected_event{
        .id = id,
        .reason = std::string(reason),
        .timestamp = std::chrono::system_clock::now()
    });

    connections_.remove(id);
}

}  // namespace hb::network
