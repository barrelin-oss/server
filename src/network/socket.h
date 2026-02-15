#pragma once

// socket.h
// Cross-platform socket abstraction

#include "core/types.h"
#include "core/result.h"
#include "platform/platform.h"

#include <string>
#include <string_view>
#include <cstdint>
#include <span>
#include <optional>

#ifdef HB_PLATFORM_WINDOWS
#include <WinSock2.h>
#include <WS2tcpip.h>
#pragma comment(lib, "ws2_32.lib")
using socket_handle = SOCKET;
constexpr socket_handle invalid_socket_handle = INVALID_SOCKET;
#else
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
using socket_handle = int;
constexpr socket_handle invalid_socket_handle = -1;
#endif

namespace hb::network
{

// Socket address wrapper
struct socket_address
{
    std::string ip;
    uint16_t port{0};

    [[nodiscard]] auto to_string() const -> std::string { return ip + ":" + std::to_string(port); }
};

// Socket error codes
enum class socket_error
{
    none = 0,
    would_block,
    connection_reset,
    connection_refused,
    not_connected,
    address_in_use,
    invalid_argument,
    unknown
};

// Get last socket error
auto get_last_error() -> socket_error;
auto error_string(socket_error err) -> std::string;

// Initialize socket library (Windows needs WSAStartup)
auto initialize_sockets() -> result<void, std::string>;
void cleanup_sockets();

// TCP Socket wrapper
class tcp_socket
{
public:
    tcp_socket();
    explicit tcp_socket(socket_handle handle);
    ~tcp_socket();

    // Non-copyable
    tcp_socket(const tcp_socket&) = delete;
    auto operator=(const tcp_socket&) -> tcp_socket& = delete;

    // Movable
    tcp_socket(tcp_socket&& other) noexcept;
    auto operator=(tcp_socket&& other) noexcept -> tcp_socket&;

    // Create socket
    auto create() -> result<void, socket_error>;

    // Server operations
    auto bind(uint16_t port) -> result<void, socket_error>;
    auto listen(int backlog = 128) -> result<void, socket_error>;
    auto accept() -> result<std::pair<tcp_socket, socket_address>, socket_error>;

    // Client operations
    auto connect(std::string_view host, uint16_t port) -> result<void, socket_error>;

    // Data transfer
    auto send(std::span<const uint8_t> data) -> result<size_t, socket_error>;
    auto receive(std::span<uint8_t> buffer) -> result<size_t, socket_error>;

    // Socket options
    auto set_non_blocking(bool non_blocking) -> result<void, socket_error>;
    auto set_reuse_address(bool reuse) -> result<void, socket_error>;
    auto set_no_delay(bool no_delay) -> result<void, socket_error>;
    auto set_keep_alive(bool keep_alive) -> result<void, socket_error>;

    // State
    [[nodiscard]] auto is_valid() const -> bool;
    [[nodiscard]] auto handle() const -> socket_handle;
    [[nodiscard]] auto local_address() const -> std::optional<socket_address>;
    [[nodiscard]] auto remote_address() const -> std::optional<socket_address>;

    // Close
    void close();

private:
    socket_handle handle_{invalid_socket_handle};
};

// TCP Listener for accepting connections
class tcp_listener
{
public:
    tcp_listener() = default;
    ~tcp_listener();

    // Non-copyable, non-movable
    tcp_listener(const tcp_listener&) = delete;
    auto operator=(const tcp_listener&) -> tcp_listener& = delete;

    // Start listening
    auto start(uint16_t port) -> result<void, std::string>;
    void stop();

    // Accept connection (non-blocking)
    auto accept() -> result<std::pair<tcp_socket, socket_address>, socket_error>;

    // State
    [[nodiscard]] auto is_listening() const -> bool;
    [[nodiscard]] auto port() const -> uint16_t;

private:
    tcp_socket socket_;
    uint16_t port_{0};
    bool listening_{false};
};

} // namespace hb::network
