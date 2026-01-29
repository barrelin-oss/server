// socket.cpp
// Cross-platform socket implementation

#include "network/socket.h"

#ifdef HB_PLATFORM_WINDOWS
    #include <WinSock2.h>
    #include <WS2tcpip.h>
#else
    #include <sys/socket.h>
    #include <netinet/in.h>
    #include <netinet/tcp.h>
    #include <arpa/inet.h>
    #include <unistd.h>
    #include <fcntl.h>
    #include <errno.h>
#endif

namespace hb::network {

namespace {

#ifdef HB_PLATFORM_WINDOWS
bool wsa_initialized = false;
#endif

}  // namespace

auto get_last_error() -> socket_error {
#ifdef HB_PLATFORM_WINDOWS
    int err = WSAGetLastError();
    switch (err) {
        case WSAEWOULDBLOCK: return socket_error::would_block;
        case WSAECONNRESET: return socket_error::connection_reset;
        case WSAECONNREFUSED: return socket_error::connection_refused;
        case WSAENOTCONN: return socket_error::not_connected;
        case WSAEADDRINUSE: return socket_error::address_in_use;
        case WSAEINVAL: return socket_error::invalid_argument;
        case 0: return socket_error::none;
        default: return socket_error::unknown;
    }
#else
    switch (errno) {
        case EAGAIN:
        case EWOULDBLOCK: return socket_error::would_block;
        case ECONNRESET: return socket_error::connection_reset;
        case ECONNREFUSED: return socket_error::connection_refused;
        case ENOTCONN: return socket_error::not_connected;
        case EADDRINUSE: return socket_error::address_in_use;
        case EINVAL: return socket_error::invalid_argument;
        case 0: return socket_error::none;
        default: return socket_error::unknown;
    }
#endif
}

auto error_string(socket_error err) -> std::string {
    switch (err) {
        case socket_error::none: return "No error";
        case socket_error::would_block: return "Operation would block";
        case socket_error::connection_reset: return "Connection reset by peer";
        case socket_error::connection_refused: return "Connection refused";
        case socket_error::not_connected: return "Socket not connected";
        case socket_error::address_in_use: return "Address already in use";
        case socket_error::invalid_argument: return "Invalid argument";
        case socket_error::unknown: return "Unknown socket error";
    }
    return "Unknown error";
}

auto initialize_sockets() -> result<void, std::string> {
#ifdef HB_PLATFORM_WINDOWS
    if (!wsa_initialized) {
        WSADATA wsa_data;
        int wsa_result = WSAStartup(MAKEWORD(2, 2), &wsa_data);
        if (wsa_result != 0) {
            return result<void, std::string>::err(
                "WSAStartup failed with error: " + std::to_string(wsa_result)
            );
        }
        wsa_initialized = true;
    }
#endif
    return result<void, std::string>::ok();
}

void cleanup_sockets() {
#ifdef HB_PLATFORM_WINDOWS
    if (wsa_initialized) {
        WSACleanup();
        wsa_initialized = false;
    }
#endif
}

// tcp_socket implementation

tcp_socket::tcp_socket() = default;

tcp_socket::tcp_socket(socket_handle handle)
    : handle_(handle)
{}

tcp_socket::~tcp_socket() {
    close();
}

tcp_socket::tcp_socket(tcp_socket&& other) noexcept
    : handle_(other.handle_)
{
    other.handle_ = invalid_socket_handle;
}

auto tcp_socket::operator=(tcp_socket&& other) noexcept -> tcp_socket& {
    if (this != &other) {
        close();
        handle_ = other.handle_;
        other.handle_ = invalid_socket_handle;
    }
    return *this;
}

auto tcp_socket::create() -> result<void, socket_error> {
    close();

    handle_ = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (handle_ == invalid_socket_handle) {
        return result<void, socket_error>::err(get_last_error());
    }

    return result<void, socket_error>::ok();
}

auto tcp_socket::bind(uint16_t port) -> result<void, socket_error> {
    if (!is_valid()) {
        return result<void, socket_error>::err(socket_error::invalid_argument);
    }

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(port);

    if (::bind(handle_, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0) {
        return result<void, socket_error>::err(get_last_error());
    }

    return result<void, socket_error>::ok();
}

auto tcp_socket::listen(int backlog) -> result<void, socket_error> {
    if (!is_valid()) {
        return result<void, socket_error>::err(socket_error::invalid_argument);
    }

    if (::listen(handle_, backlog) != 0) {
        return result<void, socket_error>::err(get_last_error());
    }

    return result<void, socket_error>::ok();
}

auto tcp_socket::accept() -> result<std::pair<tcp_socket, socket_address>, socket_error> {
    if (!is_valid()) {
        return result<std::pair<tcp_socket, socket_address>, socket_error>::err(
            socket_error::invalid_argument
        );
    }

    sockaddr_in client_addr{};
    socklen_t addr_len = sizeof(client_addr);

    socket_handle client_handle = ::accept(
        handle_,
        reinterpret_cast<sockaddr*>(&client_addr),
        &addr_len
    );

    if (client_handle == invalid_socket_handle) {
        return result<std::pair<tcp_socket, socket_address>, socket_error>::err(
            get_last_error()
        );
    }

    socket_address addr;
    char ip_str[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &client_addr.sin_addr, ip_str, sizeof(ip_str));
    addr.ip = ip_str;
    addr.port = ntohs(client_addr.sin_port);

    return result<std::pair<tcp_socket, socket_address>, socket_error>::ok(
        std::make_pair(tcp_socket(client_handle), addr)
    );
}

auto tcp_socket::connect(std::string_view host, uint16_t port) -> result<void, socket_error> {
    if (!is_valid()) {
        return result<void, socket_error>::err(socket_error::invalid_argument);
    }

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);

    if (inet_pton(AF_INET, std::string(host).c_str(), &addr.sin_addr) <= 0) {
        return result<void, socket_error>::err(socket_error::invalid_argument);
    }

    if (::connect(handle_, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0) {
        return result<void, socket_error>::err(get_last_error());
    }

    return result<void, socket_error>::ok();
}

auto tcp_socket::send(std::span<const uint8_t> data) -> result<size_t, socket_error> {
    if (!is_valid()) {
        return result<size_t, socket_error>::err(socket_error::not_connected);
    }

    auto sent = ::send(
        handle_,
        reinterpret_cast<const char*>(data.data()),
        static_cast<int>(data.size()),
        0
    );

    if (sent < 0) {
        return result<size_t, socket_error>::err(get_last_error());
    }

    return result<size_t, socket_error>::ok(static_cast<size_t>(sent));
}

auto tcp_socket::receive(std::span<uint8_t> buffer) -> result<size_t, socket_error> {
    if (!is_valid()) {
        return result<size_t, socket_error>::err(socket_error::not_connected);
    }

    auto received = ::recv(
        handle_,
        reinterpret_cast<char*>(buffer.data()),
        static_cast<int>(buffer.size()),
        0
    );

    if (received < 0) {
        return result<size_t, socket_error>::err(get_last_error());
    }

    if (received == 0) {
        return result<size_t, socket_error>::err(socket_error::connection_reset);
    }

    return result<size_t, socket_error>::ok(static_cast<size_t>(received));
}

auto tcp_socket::set_non_blocking(bool non_blocking) -> result<void, socket_error> {
    if (!is_valid()) {
        return result<void, socket_error>::err(socket_error::invalid_argument);
    }

#ifdef HB_PLATFORM_WINDOWS
    u_long mode = non_blocking ? 1 : 0;
    if (ioctlsocket(handle_, FIONBIO, &mode) != 0) {
        return result<void, socket_error>::err(get_last_error());
    }
#else
    int flags = fcntl(handle_, F_GETFL, 0);
    if (flags < 0) {
        return result<void, socket_error>::err(get_last_error());
    }

    if (non_blocking) {
        flags |= O_NONBLOCK;
    } else {
        flags &= ~O_NONBLOCK;
    }

    if (fcntl(handle_, F_SETFL, flags) < 0) {
        return result<void, socket_error>::err(get_last_error());
    }
#endif

    return result<void, socket_error>::ok();
}

auto tcp_socket::set_reuse_address(bool reuse) -> result<void, socket_error> {
    if (!is_valid()) {
        return result<void, socket_error>::err(socket_error::invalid_argument);
    }

    int opt = reuse ? 1 : 0;
    if (setsockopt(handle_, SOL_SOCKET, SO_REUSEADDR,
                   reinterpret_cast<const char*>(&opt), sizeof(opt)) != 0) {
        return result<void, socket_error>::err(get_last_error());
    }

    return result<void, socket_error>::ok();
}

auto tcp_socket::set_no_delay(bool no_delay) -> result<void, socket_error> {
    if (!is_valid()) {
        return result<void, socket_error>::err(socket_error::invalid_argument);
    }

    int opt = no_delay ? 1 : 0;
    if (setsockopt(handle_, IPPROTO_TCP, TCP_NODELAY,
                   reinterpret_cast<const char*>(&opt), sizeof(opt)) != 0) {
        return result<void, socket_error>::err(get_last_error());
    }

    return result<void, socket_error>::ok();
}

auto tcp_socket::set_keep_alive(bool keep_alive) -> result<void, socket_error> {
    if (!is_valid()) {
        return result<void, socket_error>::err(socket_error::invalid_argument);
    }

    int opt = keep_alive ? 1 : 0;
    if (setsockopt(handle_, SOL_SOCKET, SO_KEEPALIVE,
                   reinterpret_cast<const char*>(&opt), sizeof(opt)) != 0) {
        return result<void, socket_error>::err(get_last_error());
    }

    return result<void, socket_error>::ok();
}

auto tcp_socket::is_valid() const -> bool {
    return handle_ != invalid_socket_handle;
}

auto tcp_socket::handle() const -> socket_handle {
    return handle_;
}

auto tcp_socket::local_address() const -> std::optional<socket_address> {
    if (!is_valid()) return std::nullopt;

    sockaddr_in addr{};
    socklen_t len = sizeof(addr);
    if (getsockname(handle_, reinterpret_cast<sockaddr*>(&addr), &len) != 0) {
        return std::nullopt;
    }

    socket_address result;
    char ip_str[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &addr.sin_addr, ip_str, sizeof(ip_str));
    result.ip = ip_str;
    result.port = ntohs(addr.sin_port);
    return result;
}

auto tcp_socket::remote_address() const -> std::optional<socket_address> {
    if (!is_valid()) return std::nullopt;

    sockaddr_in addr{};
    socklen_t len = sizeof(addr);
    if (getpeername(handle_, reinterpret_cast<sockaddr*>(&addr), &len) != 0) {
        return std::nullopt;
    }

    socket_address result;
    char ip_str[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &addr.sin_addr, ip_str, sizeof(ip_str));
    result.ip = ip_str;
    result.port = ntohs(addr.sin_port);
    return result;
}

void tcp_socket::close() {
    if (is_valid()) {
#ifdef HB_PLATFORM_WINDOWS
        closesocket(handle_);
#else
        ::close(handle_);
#endif
        handle_ = invalid_socket_handle;
    }
}

// tcp_listener implementation

tcp_listener::~tcp_listener() {
    stop();
}

auto tcp_listener::start(uint16_t port) -> result<void, std::string> {
    if (listening_) {
        return result<void, std::string>::err("Already listening");
    }

    // Initialize sockets (Windows needs this)
    auto init_result = initialize_sockets();
    if (init_result.is_err()) {
        return init_result;
    }

    // Create socket
    auto create_result = socket_.create();
    if (create_result.is_err()) {
        return result<void, std::string>::err(
            "Failed to create socket: " + error_string(create_result.error())
        );
    }

    // Set options
    socket_.set_reuse_address(true);
    socket_.set_non_blocking(true);

    // Bind
    auto bind_result = socket_.bind(port);
    if (bind_result.is_err()) {
        return result<void, std::string>::err(
            "Failed to bind to port " + std::to_string(port) + ": " +
            error_string(bind_result.error())
        );
    }

    // Listen
    auto listen_result = socket_.listen();
    if (listen_result.is_err()) {
        return result<void, std::string>::err(
            "Failed to listen: " + error_string(listen_result.error())
        );
    }

    port_ = port;
    listening_ = true;

    return result<void, std::string>::ok();
}

void tcp_listener::stop() {
    if (listening_) {
        socket_.close();
        listening_ = false;
        port_ = 0;
    }
}

auto tcp_listener::accept() -> result<std::pair<tcp_socket, socket_address>, socket_error> {
    return socket_.accept();
}

auto tcp_listener::is_listening() const -> bool {
    return listening_;
}

auto tcp_listener::port() const -> uint16_t {
    return port_;
}

}  // namespace hb::network
