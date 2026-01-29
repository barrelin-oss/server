// combat_system.cpp
// Combat management implementation

#include "combat/combat_system.h"
#include "core/logger.h"

#include <chrono>

namespace hb::combat {

combat_system::combat_system() = default;

combat_system::~combat_system() {
    if (is_initialized()) {
        shutdown();
    }
}

void combat_system::initialize() {
    LOG_INFO(general, "Combat system initializing...");
    set_initialized(true);
    LOG_INFO(general, "Combat system initialized");
}

void combat_system::shutdown() {
    LOG_INFO(general, "Combat system shutting down...");

    damage_callbacks_.clear();
    death_callbacks_.clear();
    combat_states_.clear();
    pending_deaths_.clear();

    set_initialized(false);
    LOG_INFO(general, "Combat system shutdown complete");
}

void combat_system::update(float delta_time) {
    process_pending_deaths();
    update_combat_states(delta_time);
}

void combat_system::set_config(const combat_system_config& config) {
    config_ = config;
}

auto combat_system::process_attack(const attack_event& attack) -> combat_result {
    combat_result result;

    if (!can_attack(attack.attacker, attack.defender)) {
        result.hit.flags = hit_flags::miss;
        return result;
    }

    // Build combat context
    // In a real implementation, this would query player/NPC systems for stats
    combat_context ctx;
    ctx.attacker = attack.attacker;
    ctx.defender = attack.defender;
    ctx.type = attack.type;
    ctx.attack_power = attack.base_damage;
    ctx.critical_rate = 10;
    ctx.critical_damage = 150;
    ctx.defense = 0;
    ctx.dodge_rate = 0;

    // Apply PvP modifier if applicable
    // Would check if both are players
    bool is_pvp = false;  // Would be determined by entity types
    if (is_pvp) {
        ctx.damage_multiplier = config_.pvp_damage_modifier;
    } else {
        ctx.damage_multiplier = config_.pve_damage_modifier;
    }

    // Resolve the hit
    result.hit = resolve_hit(ctx);

    if (result.hit.is_hit()) {
        apply_damage(attack.defender, result.hit, attack.attacker);

        // Check for kill (would query entity health)
        if (result.hit.caused_death()) {
            result.target_killed = true;
            // Calculate rewards based on target
        }
    }

    return result;
}

auto combat_system::resolve_hit(const combat_context& ctx) -> hit_result {
    return calculate_final_damage(ctx);
}

void combat_system::apply_damage(hb::entity::entity target, const hit_result& result, hb::entity::entity source) {
    if (is_invulnerable(target)) {
        return;
    }

    if (!result.is_hit() || result.final_damage <= 0) {
        return;
    }

    // Mark both as in combat
    enter_combat(source);
    enter_combat(target);

    // Create damage event
    damage_event event;
    event.target = target;
    event.source = source;
    event.result = result;
    // event.location would be set from entity position

    // Notify listeners
    notify_damage(event);

    // The actual HP reduction would be done by the player/NPC system
    // based on the damage event

    LOG_DEBUG(general, "Entity {} dealt {} damage to entity {} (crit: {})",
        source.id, result.final_damage, target.id, result.is_critical());
}

void combat_system::deal_damage(hb::entity::entity target, int32_t damage, damage_type type, hb::entity::entity source) {
    hit_result result;
    result.flags = hit_flags::hit;
    result.type = type;
    result.raw_damage = damage;
    result.final_damage = damage;

    apply_damage(target, result, source);
}

void combat_system::deal_pure_damage(hb::entity::entity target, int32_t damage, hb::entity::entity source) {
    deal_damage(target, damage, damage_type::pure, source);
}

auto combat_system::can_attack(hb::entity::entity attacker, hb::entity::entity defender) const -> bool {
    if (!attacker.is_valid() || !defender.is_valid()) {
        return false;
    }

    if (attacker == defender) {
        return false;
    }

    if (is_invulnerable(defender)) {
        return false;
    }

    // PvP check
    // In real implementation, would check entity types and faction

    return true;
}

auto combat_system::is_hostile(hb::entity::entity a, hb::entity::entity b) const -> bool {
    // Would check factions, PK status, war state, etc.
    return true;
}

auto combat_system::is_in_combat(hb::entity::entity e) const -> bool {
    auto it = combat_states_.find(e);
    return it != combat_states_.end() && it->second.in_combat;
}

void combat_system::on_damage(damage_callback callback) {
    damage_callbacks_.push_back(std::move(callback));
}

void combat_system::on_death(death_callback callback) {
    death_callbacks_.push_back(std::move(callback));
}

void combat_system::enter_combat(hb::entity::entity e) {
    combat_states_[e].in_combat = true;
}

void combat_system::leave_combat(hb::entity::entity e) {
    auto it = combat_states_.find(e);
    if (it != combat_states_.end()) {
        it->second.in_combat = false;
    }
}

void combat_system::set_invulnerable(hb::entity::entity e, int32_t duration_ms) {
    auto now = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
    combat_states_[e].invulnerable_until_ms = now + duration_ms;
}

auto combat_system::is_invulnerable(hb::entity::entity e) const -> bool {
    auto it = combat_states_.find(e);
    if (it == combat_states_.end()) return false;

    auto now = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
    return now < it->second.invulnerable_until_ms;
}

auto combat_system::get_kill_count(hb::entity::entity e) const -> int32_t {
    auto it = combat_states_.find(e);
    return it != combat_states_.end() ? it->second.kill_count : 0;
}

auto combat_system::get_death_count(hb::entity::entity e) const -> int32_t {
    auto it = combat_states_.find(e);
    return it != combat_states_.end() ? it->second.death_count : 0;
}

void combat_system::process_pending_deaths() {
    for (const auto& death : pending_deaths_) {
        notify_death(death);

        // Update kill/death counts
        if (death.killer.is_valid()) {
            combat_states_[death.killer].kill_count++;
        }
        combat_states_[death.victim].death_count++;

        LOG_DEBUG(general, "Entity {} killed by entity {}", death.victim.id, death.killer.id);
    }
    pending_deaths_.clear();
}

void combat_system::update_combat_states(float /*delta_time*/) {
    // Update combat states, check for combat timeout, etc.
}

void combat_system::notify_damage(const damage_event& event) {
    for (const auto& callback : damage_callbacks_) {
        callback(event);
    }
}

void combat_system::notify_death(const death_event& event) {
    for (const auto& callback : death_callbacks_) {
        callback(event);
    }
}

}  // namespace hb::combat
