#pragma once

// wave5_handlers.h
// Wave 5 Migration: Social system message handlers
// These handlers process party, guild, and trade operations.

#include "bridge/message_router.h"
#include "protocol/protocol.h"

namespace hb::bridge::wave5
{

// ========== Wave 5: Social System Handlers ==========

// Initialize and register all Wave 5 handlers
// Returns the number of handlers registered
auto register_wave5_handlers() -> size_t;

// Unregister all Wave 5 handlers (for testing)
void unregister_wave5_handlers();

// ========== Party Handlers ==========

// Handle create party request
auto handle_create_party(const handler_context& ctx) -> handle_result;

// Handle join party request
auto handle_join_party(const handler_context& ctx) -> handle_result;

// Handle party invitation response
auto handle_party_response(const handler_context& ctx, bool accept) -> handle_result;

// ========== Guild Handlers ==========

// Handle create guild request
auto handle_create_guild(const handler_context& ctx) -> handle_result;

// Handle join guild approval/rejection
auto handle_guild_join_response(const handler_context& ctx, bool approve) -> handle_result;

// Handle dismiss guild member
auto handle_guild_dismiss(const handler_context& ctx) -> handle_result;

// ========== Exchange/Trade Handlers ==========

// Handle item exchange request
auto handle_exchange_request(const handler_context& ctx) -> handle_result;

// Handle set item in exchange window
auto handle_set_exchange_item(const handler_context& ctx) -> handle_result;

// Handle exchange confirmation
auto handle_confirm_exchange(const handler_context& ctx) -> handle_result;

// Handle exchange cancellation
auto handle_cancel_exchange(const handler_context& ctx) -> handle_result;

// ========== Response Helpers ==========

// Send party creation result
void send_party_result(const handler_context& ctx, bool success, std::string_view message);

// Send guild creation result
void send_guild_result(const handler_context& ctx, bool success, std::string_view message);

// Send exchange window update
void send_exchange_update(const handler_context& ctx);

} // namespace hb::bridge::wave5
