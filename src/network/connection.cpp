// connection.cpp
// Individual client connection implementation

#include "network/connection.h"
#include "core/logger.h"

namespace hb::network
{

// connection implementation

connection::connection(connection_id id, tcp_socket socket, socket_address address)
    : id_(id)
    , socket_(std::move(socket))
    , address_(std::move(address))
{
    stats_.connected_at = std::chrono::steady_clock::now();
    stats_.last_activity = stats_.connected_at;

    // Set socket options for game server
    socket_.set_non_blocking(true);
    socket_.set_no_delay(true); // Disable Nagle's algorithm for low latency
    socket_.set_keep_alive(true);

    state_ = connection_state::connected;
}

connection::~connection()
{
    disconnect();
}

connection::connection(connection&& other) noexcept
    : id_(other.id_)
    , socket_(std::move(other.socket_))
    , address_(std::move(other.address_))
    , state_(other.state_.load())
    , stats_(other.stats_)
    , recv_buffer_(std::move(other.recv_buffer_))
{
    std::lock_guard lock{other.send_mutex_};
    send_queue_ = std::move(other.send_queue_);
    current_send_ = std::move(other.current_send_);
    send_offset_ = other.send_offset_;

    other.state_ = connection_state::disconnected;
}

auto connection::operator=(connection&& other) noexcept -> connection&
{
    if (this != &other)
    {
        disconnect();

        id_ = other.id_;
        socket_ = std::move(other.socket_);
        address_ = std::move(other.address_);
        state_ = other.state_.load();
        stats_ = other.stats_;
        recv_buffer_ = std::move(other.recv_buffer_);

        std::scoped_lock lock{send_mutex_, other.send_mutex_};
        send_queue_ = std::move(other.send_queue_);
        current_send_ = std::move(other.current_send_);
        send_offset_ = other.send_offset_;

        other.state_ = connection_state::disconnected;
    }
    return *this;
}

void connection::send(std::span<const uint8_t> data)
{
    if (!is_connected() || data.empty())
    {
        return;
    }

    // Create message with length prefix
    std::vector<uint8_t> message;
    message.reserve(2 + data.size());

    // Write length prefix (little-endian)
    uint16_t length = static_cast<uint16_t>(data.size());
    message.push_back(static_cast<uint8_t>(length & 0xFF));
    message.push_back(static_cast<uint8_t>((length >> 8) & 0xFF));

    // Append data
    message.insert(message.end(), data.begin(), data.end());

    std::lock_guard lock{send_mutex_};
    send_queue_.push(std::move(message));
}

void connection::send_message(const message_writer& writer)
{
    send(writer.data());
}

auto connection::process_send() -> bool
{
    if (!is_connected())
    {
        return false;
    }

    std::lock_guard lock{send_mutex_};

    // Get next message to send if we don't have one
    if (current_send_.empty() && !send_queue_.empty())
    {
        current_send_ = std::move(send_queue_.front());
        send_queue_.pop();
        send_offset_ = 0;
    }

    // Nothing to send
    if (current_send_.empty())
    {
        return true;
    }

    // Try to send remaining data
    std::span<const uint8_t> remaining{current_send_.data() + send_offset_, current_send_.size() - send_offset_};

    auto result = socket_.send(remaining);
    if (result.is_err())
    {
        if (result.error() == socket_error::would_block)
        {
            // Socket buffer is full, try again later
            return true;
        }
        // Connection error
        LOG_DEBUG(network, "Connection {} send error: {}", id_.value, error_string(result.error()));
        return false;
    }

    size_t sent = result.value();
    send_offset_ += sent;
    stats_.bytes_sent += sent;

    // Check if message is complete
    if (send_offset_ >= current_send_.size())
    {
        current_send_.clear();
        send_offset_ = 0;
        ++stats_.messages_sent;
    }

    update_activity();
    return true;
}

auto connection::process_receive() -> bool
{
    if (!is_connected())
    {
        return false;
    }

    std::span<uint8_t> recv_span{recv_chunk_.data(), recv_chunk_.size()};
    auto result = socket_.receive(recv_span);
    if (result.is_err())
    {
        if (result.error() == socket_error::would_block)
        {
            // No data available
            return true;
        }
        // Connection error or closed
        LOG_DEBUG(network, "Connection {} receive error: {}", id_.value, error_string(result.error()));
        return false;
    }

    size_t received = result.value();
    if (received == 0)
    {
        // Connection closed by peer
        return false;
    }

    // Append to receive buffer
    std::span<const uint8_t> data_span{recv_chunk_.data(), received};
    recv_buffer_.append(data_span);
    stats_.bytes_received += received;

    update_activity();
    return true;
}

auto connection::has_pending_message() const -> bool
{
    return recv_buffer_.has_complete_message();
}

auto connection::extract_message() -> std::vector<uint8_t>
{
    auto message = recv_buffer_.extract_message();
    if (!message.empty())
    {
        ++stats_.messages_received;
    }
    return message;
}

void connection::disconnect()
{
    auto expected = connection_state::connected;
    if (state_.compare_exchange_strong(expected, connection_state::disconnecting))
    {
        socket_.close();
        state_ = connection_state::disconnected;
    }
}

auto connection::state() const -> connection_state
{
    return state_.load();
}

auto connection::is_connected() const -> bool
{
    return state_.load() == connection_state::connected;
}

void connection::update_activity()
{
    stats_.last_activity = std::chrono::steady_clock::now();
}

auto connection::idle_duration() const -> std::chrono::milliseconds
{
    auto now = std::chrono::steady_clock::now();
    return std::chrono::duration_cast<std::chrono::milliseconds>(now - stats_.last_activity);
}

// connection_manager implementation

auto connection_manager::add(tcp_socket socket, socket_address address) -> connection_id
{
    auto id = next_id();

    auto conn = std::make_unique<connection>(id, std::move(socket), std::move(address));

    std::lock_guard lock{mutex_};
    connections_[id] = std::move(conn);

    return id;
}

void connection_manager::remove(connection_id id)
{
    std::lock_guard lock{mutex_};
    auto it = connections_.find(id);
    if (it != connections_.end())
    {
        it->second->disconnect();
        connections_.erase(it);
    }
}

void connection_manager::remove_all()
{
    std::lock_guard lock{mutex_};
    for (auto& [id, conn] : connections_)
    {
        conn->disconnect();
    }
    connections_.clear();
}

auto connection_manager::get(connection_id id) -> connection*
{
    std::lock_guard lock{mutex_};
    auto it = connections_.find(id);
    return it != connections_.end() ? it->second.get() : nullptr;
}

auto connection_manager::get(connection_id id) const -> const connection*
{
    std::lock_guard lock{mutex_};
    auto it = connections_.find(id);
    return it != connections_.end() ? it->second.get() : nullptr;
}

auto connection_manager::count() const -> size_t
{
    std::lock_guard lock{mutex_};
    return connections_.size();
}

auto connection_manager::next_id() -> connection_id
{
    return connection_id{next_id_++};
}

} // namespace hb::network
