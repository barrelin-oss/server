#pragma once

// wave4_handlers.h
// Wave 4 Migration: Combat message handlers
// These handlers process attacks, magic, and skill usage.

#include "bridge/message_router.h"
#include "protocol/protocol.h"

namespace hb::bridge::wave4
{

// ========== Wave 4: Combat Handlers ==========

// Initialize and register all Wave 4 handlers
// Returns the number of handlers registered
auto register_wave4_handlers() -> size_t;

// Unregister all Wave 4 handlers (for testing)
void unregister_wave4_handlers();

// ========== Attack Handlers ==========

// Handle physical attack command
auto handle_attack(const handler_context& ctx) -> handle_result;

// Handle attack with target ID
auto handle_attack_target(const handler_context& ctx, entity_id target_id) -> handle_result;

// ========== Magic Handlers ==========

// Handle magic/spell cast command
auto handle_magic(const handler_context& ctx) -> handle_result;

// Cast a specific spell
auto handle_cast_spell(const handler_context& ctx, spell_id spell, entity_id target) -> handle_result;

// ========== Skill Handlers ==========

// Handle skill use command
auto handle_use_skill(const handler_context& ctx) -> handle_result;

// Use a specific skill
auto handle_skill_use(const handler_context& ctx, skill_id skill, entity_id target) -> handle_result;

// ========== Combat Event Responses ==========

// Send damage notification to client
void send_damage_event(const handler_context& ctx, entity_id target, int32_t damage, bool critical, bool killed);

// Send spell effect notification
void send_spell_effect(const handler_context& ctx, spell_id spell, entity_id target, int32_t damage, bool resisted);

// Send skill result notification
void send_skill_result(const handler_context& ctx, skill_id skill, bool success);

} // namespace hb::bridge::wave4
