#pragma once

// wave1_handlers.h
// Wave 1 Migration: Read-only / Stateless message handlers
// These handlers don't modify game state, making them safe to migrate first.

#include "bridge/message_router.h"
#include "protocol/protocol.h"

namespace hb::bridge::wave1
{

// ========== Wave 1: Read-Only Handlers ==========

// Initialize and register all Wave 1 handlers
// Returns the number of handlers registered
auto register_wave1_handlers() -> size_t;

// Unregister all Wave 1 handlers (for testing)
void unregister_wave1_handlers();

// ========== Individual Handler Functions ==========

// Handle help request (common_type::request_help)
// Responds with help text for the player's current context
auto handle_help_request(const handler_context& ctx) -> handle_result;

// Handle item info lookup by ID
// Responds with item template data from item_registry
auto handle_item_lookup(const handler_context& ctx) -> handle_result;

// Handle NPC info lookup by ID
// Responds with NPC template data from npc_registry
auto handle_npc_lookup(const handler_context& ctx) -> handle_result;

// Handle magic/spell info lookup by ID
// Responds with spell template data from magic_registry
auto handle_magic_lookup(const handler_context& ctx) -> handle_result;

// ========== Response Builders ==========

// Build a notify message with help text
void send_help_response(const handler_context& ctx, std::string_view help_text);

// Build a notify message with item info
void send_item_info(const handler_context& ctx, uint16_t item_id);

// Build a notify message with NPC info
void send_npc_info(const handler_context& ctx, uint16_t npc_id);

// Build a notify message with magic info
void send_magic_info(const handler_context& ctx, uint16_t spell_id);

} // namespace hb::bridge::wave1
