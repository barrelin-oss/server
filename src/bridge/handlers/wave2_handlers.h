#pragma once

// wave2_handlers.h
// Wave 2 Migration: Admin command message handlers
// These handlers process GM commands through the modern admin_system.

#include "bridge/message_router.h"
#include "protocol/protocol.h"

namespace hb::bridge::wave2 {

// ========== Wave 2: Admin Command Handlers ==========

// Initialize and register all Wave 2 handlers
// Returns the number of handlers registered
auto register_wave2_handlers() -> size_t;

// Unregister all Wave 2 handlers (for testing)
void unregister_wave2_handlers();

// ========== Individual Handler Functions ==========

// Handle chat messages that may contain admin commands
// Routes to admin_system if message starts with command prefix
auto handle_chat_command(const handler_context& ctx) -> handle_result;

// ========== Admin Action Handlers ==========
// These are called by the admin_system command execution
// They bridge between admin commands and the game systems

// Kill target player/NPC (GM command)
auto admin_kill(const handler_context& ctx, player_id target) -> handle_result;

// Summon player to GM's location
auto admin_summon(const handler_context& ctx, player_id target) -> handle_result;

// Teleport GM to location or player
auto admin_teleport(const handler_context& ctx, int16_t x, int16_t y, map_id map) -> handle_result;

// Toggle GM invisibility
auto admin_invisible(const handler_context& ctx, bool invisible) -> handle_result;

// Ban a player
auto admin_ban(const handler_context& ctx, player_id target, std::string_view reason,
               int64_t duration_seconds) -> handle_result;

}  // namespace hb::bridge::wave2
