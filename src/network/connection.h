#pragma once

// connection.h
// Individual client connection wrapper

#include "network/socket.h"
#include "network/message_buffer.h"
#include "core/types.h"
#include "core/result.h"

#include <array>
#include <memory>
#include <queue>
#include <mutex>
#include <chrono>
#include <span>
#include <atomic>
#include <unordered_map>

namespace hb::network {

// Connection state
enum class connection_state : uint8_t {
    connecting = 0,
    connected,
    disconnecting,
    disconnected
};

// Connection statistics
struct connection_stats {
    uint64_t bytes_sent{0};
    uint64_t bytes_received{0};
    uint64_t messages_sent{0};
    uint64_t messages_received{0};
    std::chrono::steady_clock::time_point connected_at;
    std::chrono::steady_clock::time_point last_activity;
};

// Individual client connection
class connection {
public:
    explicit connection(connection_id id, tcp_socket socket, socket_address address);
    ~connection();

    // Non-copyable, movable
    connection(const connection&) = delete;
    auto operator=(const connection&) -> connection& = delete;
    connection(connection&&) noexcept;
    auto operator=(connection&&) noexcept -> connection&;

    // Send data (queues for sending)
    void send(std::span<const uint8_t> data);
    void send_message(const message_writer& writer);

    // Process pending I/O (called by network subsystem)
    // Returns false if connection should be closed
    auto process_send() -> bool;
    auto process_receive() -> bool;

    // Check if we have complete messages to process
    [[nodiscard]] auto has_pending_message() const -> bool;
    [[nodiscard]] auto extract_message() -> std::vector<uint8_t>;

    // State management
    void disconnect();
    [[nodiscard]] auto state() const -> connection_state;
    [[nodiscard]] auto is_connected() const -> bool;

    // Accessors
    [[nodiscard]] auto id() const -> connection_id { return id_; }
    [[nodiscard]] auto address() const -> const socket_address& { return address_; }
    [[nodiscard]] auto stats() const -> const connection_stats& { return stats_; }
    [[nodiscard]] auto socket_handle() const -> ::socket_handle { return socket_.handle(); }

    // Activity tracking
    void update_activity();
    [[nodiscard]] auto idle_duration() const -> std::chrono::milliseconds;

private:
    connection_id id_;
    tcp_socket socket_;
    socket_address address_;
    std::atomic<connection_state> state_{connection_state::connecting};
    connection_stats stats_;

    // Receive buffer
    receive_buffer recv_buffer_;

    // Send queue
    std::queue<std::vector<uint8_t>> send_queue_;
    std::vector<uint8_t> current_send_;
    size_t send_offset_{0};
    mutable std::mutex send_mutex_;

    // Receive buffer for raw socket reads
    static constexpr size_t recv_chunk_size = 4096;
    std::array<uint8_t, recv_chunk_size> recv_chunk_;
};

// Connection manager (internal to network subsystem)
class connection_manager {
public:
    connection_manager() = default;
    ~connection_manager() = default;

    // Non-copyable
    connection_manager(const connection_manager&) = delete;
    auto operator=(const connection_manager&) -> connection_manager& = delete;

    // Connection lifecycle
    auto add(tcp_socket socket, socket_address address) -> connection_id;
    void remove(connection_id id);
    void remove_all();

    // Access
    [[nodiscard]] auto get(connection_id id) -> connection*;
    [[nodiscard]] auto get(connection_id id) const -> const connection*;
    [[nodiscard]] auto count() const -> size_t;

    // Iteration
    template<typename F>
    void for_each(F&& func) {
        std::lock_guard lock{mutex_};
        for (auto& [id, conn] : connections_) {
            func(*conn);
        }
    }

    template<typename F>
    void for_each(F&& func) const {
        std::lock_guard lock{mutex_};
        for (const auto& [id, conn] : connections_) {
            func(*conn);
        }
    }

private:
    [[nodiscard]] auto next_id() -> connection_id;

    std::unordered_map<connection_id, std::unique_ptr<connection>> connections_;
    mutable std::mutex mutex_;
    uint32_t next_id_{1};
};

}  // namespace hb::network
