// combat_system.cpp
// Combat management implementation

#include "combat/combat_system.h"
#include "core/logger.h"
#include "core/subsystem.h"
#include "player/player_system.h"
#include "npc/npc_system.h"

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

    // Build combat context from entity stats
    combat_context ctx = build_combat_context(attack.attacker, attack.defender, attack.type);

    // Override with base damage if provided
    if (attack.base_damage > 0) {
        if (attack.type == damage_type::physical) {
            ctx.attack_power = attack.base_damage;
        } else {
            ctx.magic_power = attack.base_damage;
        }
    }

    // Resolve the hit
    result.hit = resolve_hit(ctx);

    if (result.hit.is_hit()) {
        apply_damage(attack.defender, result.hit, attack.attacker);

        // Check if target died
        bool target_killed = check_entity_dead(attack.defender);
        if (target_killed) {
            result.hit.flags = result.hit.flags | hit_flags::killed;
            result.target_killed = true;

            // Queue death event
            death_event death;
            death.victim = attack.defender;
            death.killer = attack.attacker;
            death.is_pvp = is_player_entity(attack.attacker) && is_player_entity(attack.defender);
            pending_deaths_.push_back(death);

            // Calculate rewards from NPC kills
            if (!is_player_entity(attack.defender)) {
                auto rewards = calculate_kill_rewards(attack.defender);
                result.exp_reward = rewards.first;
                result.gold_reward = rewards.second;
            }
        }
    }

    return result;
}

auto combat_system::build_combat_context(hb::entity::entity attacker, hb::entity::entity defender,
                                          damage_type type) -> combat_context {
    combat_context ctx;
    ctx.attacker = attacker;
    ctx.defender = defender;
    ctx.type = type;

    // Get attacker stats
    auto* player_sys = subsystems().get<player::player_system>();
    auto* npc_sys = subsystems().get<npc::npc_system>();

    bool attacker_is_player = false;
    bool defender_is_player = false;

    // Get attacker stats
    if (player_sys) {
        if (auto* p = player_sys->get_player(player_id{attacker.id})) {
            attacker_is_player = true;
            ctx.attack_power = p->computed.attack_power;
            ctx.magic_power = p->computed.magic_power;
            ctx.hit_rate = p->computed.hit_rate;
            ctx.critical_rate = p->computed.critical_rate;
            ctx.critical_damage = p->computed.critical_damage;
        }
    }

    if (!attacker_is_player && npc_sys) {
        if (auto* n = npc_sys->get_npc(attacker)) {
            // Calculate attack power from dice (average damage)
            ctx.attack_power = n->attack_dice * (n->attack_sides / 2 + 1) + n->attack_bonus;
            ctx.magic_power = ctx.attack_power;  // NPCs use same for magic
            ctx.hit_rate = n->hit_rate;
            ctx.critical_rate = 5;  // NPCs have lower base crit
            ctx.critical_damage = 150;
        }
    }

    // Get defender stats
    if (player_sys) {
        if (auto* p = player_sys->get_player(player_id{defender.id})) {
            defender_is_player = true;
            ctx.defense = p->computed.defense;
            ctx.magic_defense = p->computed.magic_defense;
            ctx.dodge_rate = p->computed.dodge_rate;
            ctx.block_rate = 0;  // TODO: Calculate from shield
            ctx.damage_reduction = p->computed.physical_resist;
        }
    }

    if (!defender_is_player && npc_sys) {
        if (auto* n = npc_sys->get_npc(defender)) {
            ctx.defense = n->defense;
            ctx.magic_defense = n->magic_defense;
            ctx.dodge_rate = n->dodge_rate;
            ctx.block_rate = 0;
            ctx.damage_reduction = 0;
        }
    }

    // Apply PvP modifier
    bool is_pvp = attacker_is_player && defender_is_player;
    ctx.damage_multiplier = is_pvp ? config_.pvp_damage_modifier : config_.pve_damage_modifier;

    return ctx;
}

auto combat_system::is_player_entity(hb::entity::entity e) const -> bool {
    auto* player_sys = subsystems().get<player::player_system>();
    return player_sys && player_sys->player_exists(player_id{e.id});
}

auto combat_system::check_entity_dead(hb::entity::entity e) const -> bool {
    auto* player_sys = subsystems().get<player::player_system>();
    auto* npc_sys = subsystems().get<npc::npc_system>();

    if (player_sys) {
        if (auto* p = player_sys->get_player(player_id{e.id})) {
            return p->is_dead();
        }
    }

    if (npc_sys) {
        if (auto* n = npc_sys->get_npc(e)) {
            return n->is_dead();
        }
    }

    return false;
}

auto combat_system::calculate_kill_rewards(hb::entity::entity target) const -> std::pair<int32_t, int32_t> {
    auto* npc_sys = subsystems().get<npc::npc_system>();
    if (!npc_sys) return {0, 0};

    auto* n = npc_sys->get_npc(target);
    if (!n) return {0, 0};

    int32_t exp = n->exp_reward;

    // Gold based on NPC level (would normally come from loot table)
    int32_t gold_base = n->level * 5;
    int32_t gold = random_int(gold_base / 2, gold_base);

    return {exp, gold};
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
