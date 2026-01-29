// npc_system.cpp
// NPC management subsystem implementation

#include "npc/npc_system.h"
#include "core/logger.h"

namespace hb::npc {

npc_system::npc_system() = default;

npc_system::~npc_system() {
    if (is_initialized()) {
        shutdown();
    }
}

void npc_system::initialize() {
    LOG_INFO(general, "NPC system initializing...");
    set_initialized(true);
    LOG_INFO(general, "NPC system initialized (max_npcs: {})", config_.max_npcs);
}

void npc_system::shutdown() {
    LOG_INFO(general, "NPC system shutting down...");

    npcs_.clear();
    spawn_points_.clear();

    set_initialized(false);
    LOG_INFO(general, "NPC system shutdown complete");
}

void npc_system::update(float delta_time) {
    update_spawns(delta_time);
    if (config_.enable_ai) {
        update_all_ai(delta_time);
    }
}

void npc_system::set_config(const npc_system_config& config) {
    config_ = config;
}

auto npc_system::spawn_npc(npc_id template_id, map_id map, hb::world::position pos)
    -> result<entity::entity, std::string>
{
    if (npcs_.size() >= config_.max_npcs) {
        return result<entity::entity, std::string>::err("Maximum NPC count reached");
    }

    auto entity_id = next_entity_id();
    auto new_npc = std::make_unique<npc>();

    new_npc->entity_id = entity_id;
    new_npc->template_id = template_id;
    new_npc->current_map = map;
    new_npc->pos = pos;
    new_npc->ai_state.spawn_point = pos;

    // Default stats (would be loaded from template)
    new_npc->max_hp = 100;
    new_npc->hp = new_npc->max_hp;
    new_npc->max_mp = 50;
    new_npc->mp = new_npc->max_mp;
    new_npc->level = 1;
    new_npc->exp_reward = 10;
    new_npc->attack_dice = 1;
    new_npc->attack_sides = 6;
    new_npc->attack_bonus = 2;
    new_npc->defense = 5;

    new_npc->ai_state.set_state(ai_state::idle);

    npcs_[entity_id] = std::move(new_npc);

    LOG_DEBUG(general, "Spawned NPC template {} at ({}, {}) on map {}",
        template_id.value, pos.x, pos.y, map.value);

    return result<entity::entity, std::string>::ok(entity_id);
}

auto npc_system::spawn_npc_at(spawn_point& spawn)
    -> result<entity::entity, std::string>
{
    if (!spawn.can_spawn()) {
        return result<entity::entity, std::string>::err("Spawn point not ready");
    }

    auto pos = spawn.get_spawn_position();
    auto result = spawn_npc(spawn.npc_type, spawn.map, pos);

    if (result.is_ok()) {
        spawn.on_spawn();
        auto* npc_ptr = get_npc(result.value());
        if (npc_ptr) {
            npc_ptr->spawn = &spawn;
            npc_ptr->ai_state.spawn_point = spawn.center;
        }
    }

    return result;
}

void npc_system::despawn_npc(entity::entity id) {
    auto it = npcs_.find(id);
    if (it == npcs_.end()) return;

    auto& npc_ref = *it->second;
    if (npc_ref.spawn) {
        npc_ref.spawn->on_death();
    }

    LOG_DEBUG(general, "Despawned NPC {}", id.id);
    npcs_.erase(it);
}

void npc_system::kill_npc(entity::entity id, entity::entity killer) {
    auto* npc_ptr = get_npc(id);
    if (!npc_ptr || npc_ptr->is_dead()) return;

    npc_ptr->hp = 0;
    npc_ptr->ai_state.set_state(ai_state::dead);
    npc_ptr->ai_state.death_time = std::chrono::steady_clock::now();

    LOG_DEBUG(general, "NPC {} killed by {}", id.id, killer.id);

    // Loot generation would happen here
    // Experience award would happen here
}

auto npc_system::get_npc(entity::entity id) -> npc* {
    auto it = npcs_.find(id);
    return it != npcs_.end() ? it->second.get() : nullptr;
}

auto npc_system::get_npc(entity::entity id) const -> const npc* {
    auto it = npcs_.find(id);
    return it != npcs_.end() ? it->second.get() : nullptr;
}

void npc_system::add_spawn_point(spawn_point point) {
    spawn_points_.push_back(std::move(point));
}

void npc_system::remove_spawn_points(map_id map) {
    std::erase_if(spawn_points_, [map](const spawn_point& sp) {
        return sp.map == map;
    });
}

void npc_system::activate_spawns(map_id map) {
    for (auto& sp : spawn_points_) {
        if (sp.map == map) {
            sp.next_spawn_time = std::chrono::steady_clock::time_point{};
        }
    }
}

void npc_system::deactivate_spawns(map_id map) {
    // Remove all NPCs from map
    std::vector<entity::entity> to_remove;
    for (auto& [id, npc_ptr] : npcs_) {
        if (npc_ptr->current_map == map) {
            to_remove.push_back(id);
        }
    }
    for (auto id : to_remove) {
        despawn_npc(id);
    }
}

void npc_system::apply_damage(entity::entity id, int32_t damage, entity::entity source) {
    auto* npc_ptr = get_npc(id);
    if (!npc_ptr || npc_ptr->is_dead()) return;

    npc_ptr->damage(damage);

    // Set attacker as target if not already targeting
    if (!npc_ptr->ai_state.target.is_valid()) {
        npc_ptr->ai_state.target = source;
        npc_ptr->ai_state.set_state(ai_state::chase);
    }

    // Increase aggro
    npc_ptr->ai_state.aggro_level += damage;

    if (npc_ptr->is_dead()) {
        kill_npc(id, source);
    }
}

void npc_system::set_target(entity::entity id, entity::entity target) {
    auto* npc_ptr = get_npc(id);
    if (!npc_ptr) return;

    npc_ptr->ai_state.target = target;
    if (target.is_valid()) {
        npc_ptr->ai_state.set_state(ai_state::chase);
    }
}

void npc_system::clear_target(entity::entity id) {
    auto* npc_ptr = get_npc(id);
    if (!npc_ptr) return;

    npc_ptr->ai_state.clear_target();
    npc_ptr->ai_state.set_state(ai_state::return_home);
}

void npc_system::update_ai(entity::entity id) {
    auto* npc_ptr = get_npc(id);
    if (!npc_ptr || npc_ptr->is_dead()) return;

    process_ai_state(*npc_ptr);
}

auto npc_system::get_npcs_in_range(map_id map, hb::world::position center, int range) const
    -> std::vector<entity::entity>
{
    std::vector<entity::entity> result;

    for (const auto& [id, npc_ptr] : npcs_) {
        if (npc_ptr->current_map != map) continue;
        if (npc_ptr->is_dead()) continue;

        int dist = center.chebyshev_distance(npc_ptr->pos);
        if (dist <= range) {
            result.push_back(id);
        }
    }

    return result;
}

void npc_system::update_spawns(float delta_time) {
    spawn_accumulator_ += delta_time * 1000.0f;

    if (spawn_accumulator_ < static_cast<float>(config_.spawn_check_interval_ms)) {
        return;
    }

    spawn_accumulator_ -= static_cast<float>(config_.spawn_check_interval_ms);

    for (auto& sp : spawn_points_) {
        if (sp.can_spawn()) {
            spawn_npc_at(sp);
        }
    }
}

void npc_system::update_all_ai(float delta_time) {
    ai_accumulator_ += delta_time * 1000.0f;

    if (ai_accumulator_ < static_cast<float>(config_.ai_update_interval_ms)) {
        return;
    }

    ai_accumulator_ -= static_cast<float>(config_.ai_update_interval_ms);

    for (auto& [id, npc_ptr] : npcs_) {
        if (npc_ptr->is_dead()) continue;
        process_ai_state(*npc_ptr);
    }
}

void npc_system::process_ai_state(npc& npc_ref) {
    auto now = std::chrono::steady_clock::now();
    auto& state = npc_ref.ai_state;

    // Check think interval
    auto time_since_think = std::chrono::duration_cast<std::chrono::milliseconds>(
        now - state.last_think_time).count();

    if (time_since_think < npc_ref.ai.think_interval_ms) {
        return;
    }

    state.last_think_time = now;

    switch (state.state) {
        case ai_state::idle:
            // Check for enemies in aggro range
            if (npc_ref.ai.has_flag(ai_flags::aggressive)) {
                // Would check for nearby players/enemies
                // If found, set target and switch to chase
            }

            // Random chance to wander
            if (!npc_ref.ai.has_flag(ai_flags::stationary)) {
                // Would randomly switch to wander state
            }
            break;

        case ai_state::wander:
            // Move randomly within wander range
            // After some time, return to idle
            if (state.time_in_state_ms() > 5000) {
                state.set_state(ai_state::idle);
            }
            break;

        case ai_state::chase:
            if (!state.target.is_valid()) {
                state.set_state(ai_state::return_home);
                break;
            }

            // Check distance to spawn point
            {
                int spawn_dist = npc_ref.pos.chebyshev_distance(state.spawn_point);
                if (spawn_dist > npc_ref.ai.chase_range) {
                    // Too far from spawn, give up
                    state.clear_target();
                    state.set_state(ai_state::return_home);
                    break;
                }
            }

            // Would check distance to target and move towards it
            // If in attack range, switch to attack
            break;

        case ai_state::attack:
            if (!state.target.is_valid()) {
                state.set_state(ai_state::return_home);
                break;
            }

            // Check flee condition
            if (npc_ref.ai.has_flag(ai_flags::cowardly) &&
                npc_ref.hp_percent() < npc_ref.ai.flee_hp_percent) {
                state.set_state(ai_state::flee);
                break;
            }

            // Perform attack if cooldown is ready
            // Would trigger attack event
            break;

        case ai_state::flee:
            // Move away from target
            if (npc_ref.hp_percent() > npc_ref.ai.flee_hp_percent + 10) {
                // HP recovered, stop fleeing
                state.set_state(ai_state::return_home);
            }
            break;

        case ai_state::return_home:
            // Move towards spawn point
            {
                int spawn_dist = npc_ref.pos.chebyshev_distance(state.spawn_point);
                if (spawn_dist <= 1) {
                    state.set_state(ai_state::idle);
                }
            }
            break;

        case ai_state::dead:
            // Handled by spawn system
            break;

        case ai_state::scripted:
            // Follow scripted behavior
            break;
    }
}

}  // namespace hb::npc
