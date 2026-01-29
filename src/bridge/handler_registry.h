#pragma once

// handler_registry.h
// Helper functions and utilities for registering message handlers
// Provides type-safe wrappers for common handler patterns

#include "bridge/message_router.h"
#include "protocol/protocol.h"
#include "protocol/message_reader.h"
#include "protocol/message_writer.h"
#include "core/logger.h"

#include <functional>
#include <string_view>
#include <type_traits>

namespace hb::bridge {

// ========== Handler Registration Helpers ==========

// Register multiple handlers at once from a subsystem
template<typename... Handlers>
void register_handlers(std::string_view subsystem, Handlers&&... handlers) {
    (router().register_handler(
        std::forward<Handlers>(handlers).msg_id,
        subsystem,
        std::forward<Handlers>(handlers).handler
    ), ...);
}

// ========== Typed Handler Wrappers ==========

// Handler that expects specific message data after the message ID
// Automatically parses and validates the header
template<typename ParseFunc, typename HandleFunc>
auto make_typed_handler(ParseFunc parse, HandleFunc handle) -> message_handler {
    return [parse, handle](const handler_context& ctx) -> handle_result {
        try {
            protocol::message_reader reader{ctx.raw_data};
            reader.skip(4);  // Skip message ID (already parsed)

            auto parsed_data = parse(reader);
            return handle(ctx, parsed_data);
        } catch (const protocol::read_error& e) {
            LOG_WARN(proto_bridge, "Parse error for {}: {}",
                     protocol::message_id_string(ctx.msg_id), e.what());
            return handle_result::error;
        }
    };
}

// Simple handler that just processes raw remaining data
template<typename HandleFunc>
auto make_simple_handler(HandleFunc handle) -> message_handler {
    return [handle](const handler_context& ctx) -> handle_result {
        protocol::message_reader reader{ctx.raw_data};
        reader.skip(4);  // Skip message ID
        return handle(ctx, reader);
    };
}

// Handler that validates session exists
template<typename HandleFunc>
auto require_session(HandleFunc handle) -> message_handler {
    return [handle](const handler_context& ctx) -> handle_result {
        if (!ctx.session.is_valid()) {
            LOG_WARN(proto_bridge, "Message {} requires session (conn={})",
                     protocol::message_id_string(ctx.msg_id),
                     ctx.connection.value);
            return handle_result::disconnect;
        }
        return handle(ctx);
    };
}

// Handler that validates player exists (in-game)
template<typename HandleFunc>
auto require_player(HandleFunc handle) -> message_handler {
    return [handle](const handler_context& ctx) -> handle_result {
        if (!ctx.player.is_valid()) {
            LOG_WARN(proto_bridge, "Message {} requires player (conn={})",
                     protocol::message_id_string(ctx.msg_id),
                     ctx.connection.value);
            return handle_result::error;
        }
        return handle(ctx);
    };
}

// ========== Common Message Patterns ==========

// Handler binding with automatic logging
struct handler_binding {
    protocol::message_id msg_id;
    message_handler handler;
    std::string description;

    handler_binding(protocol::message_id id, message_handler h, std::string_view desc = "")
        : msg_id(id)
        , handler(std::move(h))
        , description(desc)
    {}
};

// ========== Handler Registration Builder ==========

// Fluent interface for registering handlers
class handler_builder {
public:
    explicit handler_builder(std::string_view subsystem)
        : subsystem_(subsystem)
    {}

    // Register a handler
    auto on(protocol::message_id msg_id, message_handler handler) -> handler_builder& {
        router().register_handler(msg_id, subsystem_, std::move(handler));
        return *this;
    }

    // Register with session requirement
    auto on_session(protocol::message_id msg_id, message_handler handler) -> handler_builder& {
        return on(msg_id, require_session(std::move(handler)));
    }

    // Register with player requirement
    auto on_player(protocol::message_id msg_id, message_handler handler) -> handler_builder& {
        return on(msg_id, require_player(std::move(handler)));
    }

private:
    std::string subsystem_;
};

// Create a handler builder for a subsystem
[[nodiscard]] inline auto handlers(std::string_view subsystem) -> handler_builder {
    return handler_builder{subsystem};
}

// ========== Response Helpers ==========

// Send a response to the connection that sent the message
void send_response(const handler_context& ctx, const protocol::message_writer& response);

// Send a response with a specific message ID
void send_response(const handler_context& ctx,
                   protocol::message_id response_id,
                   std::function<void(protocol::message_writer&)> write_data);

// Build a standard response message
[[nodiscard]] auto build_response(protocol::message_id msg_id,
                                   std::function<void(protocol::message_writer&)> write_data)
    -> protocol::message_writer;

// ========== Migration Helpers ==========

// Check if we should use modern handler for a message type
// (Can be controlled via configuration)
[[nodiscard]] auto should_use_modern(protocol::message_id msg_id) -> bool;

// Mark a message type as "fully migrated" (no legacy fallback)
void mark_fully_migrated(protocol::message_id msg_id);

// Check if a message type is fully migrated
[[nodiscard]] auto is_fully_migrated(protocol::message_id msg_id) -> bool;

// Get list of fully migrated message types
[[nodiscard]] auto get_migrated_messages() -> std::vector<protocol::message_id>;

// Get migration progress (percentage of messages with modern handlers)
[[nodiscard]] auto migration_progress() -> float;

// ========== Debug Utilities ==========

// Log all registered handlers
void log_registered_handlers();

// Dump handler statistics to log
void log_handler_stats();

}  // namespace hb::bridge
