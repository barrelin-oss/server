#pragma once

// message_router.h
// Routes incoming network messages to modern subsystem handlers
// Falls back to legacy code for unimplemented message types

#include "core/types.h"
#include "core/result.h"
#include "protocol/protocol.h"
#include "protocol/message_reader.h"

#include <cstdint>
#include <span>
#include <functional>
#include <unordered_map>
#include <string>
#include <string_view>
#include <vector>
#include <mutex>
#include <optional>
#include <chrono>

namespace hb::bridge {

// Forward declarations
class message_router;

// Handler context passed to message handlers
struct handler_context {
    connection_id connection;
    session_id session;           // May be invalid if not authenticated
    player_id player;             // May be invalid if not in game
    protocol::message_id msg_id;
    std::span<const uint8_t> raw_data;
    std::chrono::steady_clock::time_point received_at;

    // Convenience: create a reader positioned after the message ID
    [[nodiscard]] auto make_reader() const -> protocol::message_reader {
        return protocol::message_reader{raw_data};
    }
};

// Result of handling a message
enum class handle_result : uint8_t {
    handled,           // Message was handled successfully
    handled_async,     // Handler started async operation, will complete later
    not_handled,       // Handler chose not to handle (fallback to legacy)
    error,             // Handler encountered an error
    disconnect,        // Client should be disconnected
};

// Message handler function signature
// Returns handle_result to indicate if message was handled
using message_handler = std::function<handle_result(const handler_context&)>;

// Handler metadata for registration
struct handler_info {
    std::string name;                    // Handler name for logging
    std::string subsystem;               // Which modern subsystem owns this
    protocol::message_id msg_id;         // Message ID this handler processes
    message_handler handler;             // The actual handler function
    bool enabled{true};                  // Can be disabled for testing
};

// Legacy fallback handler signature
// Called when no modern handler is registered for a message
using legacy_fallback = std::function<void(connection_id, std::span<const uint8_t>)>;

// Statistics for a single message type
struct message_stats {
    protocol::message_id msg_id{};
    uint64_t total_count{0};
    uint64_t success_count{0};
    uint64_t error_count{0};
    uint64_t fallback_count{0};
    std::chrono::nanoseconds total_time{0};
    std::chrono::nanoseconds max_time{0};
};

// Router statistics
struct router_stats {
    uint64_t total_messages{0};
    uint64_t modern_handled{0};
    uint64_t legacy_fallback{0};
    uint64_t errors{0};
    uint64_t disconnects{0};
    std::chrono::nanoseconds total_processing_time{0};
};

// Message router - central dispatcher for all incoming messages
class message_router {
public:
    message_router();
    ~message_router();

    // Non-copyable, movable
    message_router(const message_router&) = delete;
    auto operator=(const message_router&) -> message_router& = delete;
    message_router(message_router&&) noexcept;
    auto operator=(message_router&&) noexcept -> message_router&;

    // ========== Handler Registration ==========

    // Register a handler for a specific message type
    void register_handler(handler_info info);

    // Register a simple handler (name auto-generated)
    void register_handler(protocol::message_id msg_id,
                         std::string_view subsystem,
                         message_handler handler);

    // Unregister a handler
    void unregister_handler(protocol::message_id msg_id);

    // Enable/disable a handler (for testing without unregistering)
    void set_handler_enabled(protocol::message_id msg_id, bool enabled);

    // Check if a message type has a modern handler
    [[nodiscard]] auto is_modernized(protocol::message_id msg_id) const -> bool;

    // Check if a handler is enabled
    [[nodiscard]] auto is_handler_enabled(protocol::message_id msg_id) const -> bool;

    // Get handler info
    [[nodiscard]] auto get_handler_info(protocol::message_id msg_id) const
        -> const handler_info*;

    // Get all registered handlers
    [[nodiscard]] auto get_all_handlers() const -> std::vector<const handler_info*>;

    // ========== Message Routing ==========

    // Route a message to the appropriate handler
    // Returns true if handled by modern code, false if fell back to legacy
    auto route(const handler_context& ctx) -> bool;

    // Route with raw data (creates context internally)
    auto route(connection_id conn, std::span<const uint8_t> data) -> bool;

    // ========== Legacy Fallback ==========

    // Set the legacy fallback handler
    void set_legacy_fallback(legacy_fallback fallback);

    // Check if legacy fallback is set
    [[nodiscard]] auto has_legacy_fallback() const -> bool;

    // ========== Session/Player Context ==========

    // Update session mapping (called by session manager)
    void set_session_for_connection(connection_id conn, session_id session);

    // Update player mapping (called when player enters game)
    void set_player_for_connection(connection_id conn, player_id player);

    // Clear mappings when connection closes
    void clear_connection(connection_id conn);

    // ========== Statistics ==========

    // Get overall statistics
    [[nodiscard]] auto stats() const -> router_stats;

    // Get statistics for a specific message type
    [[nodiscard]] auto stats_for(protocol::message_id msg_id) const -> message_stats;

    // Get statistics for all message types
    [[nodiscard]] auto all_message_stats() const -> std::vector<message_stats>;

    // Reset all statistics
    void reset_stats();

    // ========== Debugging ==========

    // Enable/disable verbose logging of all messages
    void set_verbose_logging(bool enabled);

    // Get count of modernized message types
    [[nodiscard]] auto modernized_count() const -> size_t;

private:
    struct impl;
    std::unique_ptr<impl> impl_;
};

// Global message router singleton
[[nodiscard]] auto router() -> message_router&;

}  // namespace hb::bridge
