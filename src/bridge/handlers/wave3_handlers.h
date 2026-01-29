#pragma once

// wave3_handlers.h
// Wave 3 Migration: Player state message handlers
// These handlers manage player movement, items, and basic state changes.

#include "bridge/message_router.h"
#include "protocol/protocol.h"

namespace hb::bridge::wave3 {

// ========== Wave 3: Player State Handlers ==========

// Initialize and register all Wave 3 handlers
// Returns the number of handlers registered
auto register_wave3_handlers() -> size_t;

// Unregister all Wave 3 handlers (for testing)
void unregister_wave3_handlers();

// ========== Motion Handlers ==========

// Handle player movement commands
auto handle_motion_command(const handler_context& ctx) -> handle_result;

// Process move action
auto handle_motion_move(const handler_context& ctx, int16_t x, int16_t y,
                        int8_t dir, uint8_t run_mode) -> handle_result;

// Process stop action
auto handle_motion_stop(const handler_context& ctx, int16_t x, int16_t y,
                        int8_t dir) -> handle_result;

// ========== Item Handlers ==========

// Handle use item command
auto handle_use_item(const handler_context& ctx) -> handle_result;

// Handle drop item command
auto handle_drop_item(const handler_context& ctx) -> handle_result;

// Handle pickup item command
auto handle_pickup_item(const handler_context& ctx) -> handle_result;

// ========== Motion Types ==========

// Motion command subtypes
enum class motion_type : uint8_t {
    none = 0,
    move = 1,
    stop = 2,
    attack = 3,
    attack_move = 4,
    damage_move = 5,
    run = 6,
    get_item = 7,
    magic = 8,
    dead = 9,
    dying = 10,
};

}  // namespace hb::bridge::wave3
