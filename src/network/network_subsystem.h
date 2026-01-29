#pragma once

// network_subsystem.h
// Main network orchestrator subsystem

#include "core/subsystem.h"
#include "core/result.h"
#include "core/types.h"
#include "network/connection.h"
#include "network/socket.h"

#include <cstdint>
#include <span>
#include <functional>
#include <vector>
#include <thread>
#include <atomic>
#include <chrono>

namespace hb::bridge {
    class message_router;
}

namespace hb::network {

// Network events (published to event bus)
struct client_connected_event {
    connection_id id;
    socket_address address;
    std::chrono::system_clock::time_point timestamp;
};

struct client_disconnected_event {
    connection_id id;
    std::string reason;
    std::chrono::system_clock::time_point timestamp;
};

struct data_received_event {
    connection_id id;
    std::vector<uint8_t> data;
};

// Network subsystem configuration
struct network_config {
    uint16_t port{2848};
    size_t max_connections{2000};
    std::chrono::milliseconds idle_timeout{std::chrono::minutes{5}};
    std::chrono::milliseconds poll_interval{std::chrono::milliseconds{10}};
    bool use_message_router{true};  // Use bridge::message_router for message dispatch
};

// Main network subsystem
class network_subsystem : public subsystem {
public:
    network_subsystem();
    ~network_subsystem() override;

    // Subsystem interface
    [[nodiscard]] auto name() const -> std::string_view override { return "network"; }
    void initialize() override;
    void shutdown() override;
    void update(float delta_time) override;

    // Network operations
    auto start(uint16_t port) -> result<void, std::string>;
    auto start(const network_config& config) -> result<void, std::string>;
    void stop();

    // Send data to a specific connection
    void send(connection_id id, std::span<const uint8_t> data);
    void send_message(connection_id id, const message_writer& writer);

    // Broadcast to all connections
    void broadcast(std::span<const uint8_t> data);
    void broadcast_message(const message_writer& writer);

    // Disconnect a client
    void disconnect(connection_id id, std::string_view reason = "");

    // State queries
    [[nodiscard]] auto is_running() const -> bool;
    [[nodiscard]] auto connection_count() const -> size_t;
    [[nodiscard]] auto get_connection(connection_id id) -> connection*;
    [[nodiscard]] auto config() const -> const network_config& { return config_; }

    // Message callback (alternative to event bus for high-frequency messages)
    using message_callback = std::function<void(connection_id, std::span<const uint8_t>)>;
    void set_message_callback(message_callback callback);

    // Message router integration (preferred over callback for modern code)
    void set_message_router(bridge::message_router* router);
    [[nodiscard]] auto message_router() -> bridge::message_router*;
    [[nodiscard]] auto uses_message_router() const -> bool;

private:
    void network_thread_func();
    void accept_connections();
    void process_connections();
    void check_timeouts();
    void handle_disconnect(connection_id id, std::string_view reason);

    network_config config_;
    tcp_listener listener_;
    connection_manager connections_;

    std::jthread network_thread_;
    std::atomic<bool> running_{false};

    message_callback message_callback_;
    bridge::message_router* message_router_{nullptr};
    std::mutex callback_mutex_;

    // Stats
    std::atomic<uint64_t> total_connections_{0};
    std::atomic<uint64_t> total_bytes_sent_{0};
    std::atomic<uint64_t> total_bytes_received_{0};
};

}  // namespace hb::network
