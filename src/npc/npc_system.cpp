// npc_system.cpp
// NPC management subsystem implementation

#include "npc/npc_system.h"
#include "core/logger.h"
#include "core/subsystem.h"
#include "player/player_system.h"
#include "combat/combat_system.h"
#include "world/world_subsystem.h"
#include "registry/npc_registry.h"

#include <random>

namespace hb::npc {

namespace {
    // Random movement helper
    thread_local std::mt19937 rng{std::random_device{}()};

    auto random_direction() -> hb::world::direction {
        std::uniform_int_distribution<int> dist(0, 7);
        return static_cast<hb::world::direction>(dist(rng));
    }

    auto random_int(int min, int max) -> int {
        std::uniform_int_distribution<int> dist(min, max);
        return dist(rng);
    }
}

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
            process_idle_state(npc_ref);
            break;

        case ai_state::wander:
            process_wander_state(npc_ref);
            break;

        case ai_state::chase:
            process_chase_state(npc_ref);
            break;

        case ai_state::attack:
            process_attack_state(npc_ref);
            break;

        case ai_state::flee:
            process_flee_state(npc_ref);
            break;

        case ai_state::return_home:
            process_return_home_state(npc_ref);
            break;

        case ai_state::dead:
            // Handled by spawn system
            break;

        case ai_state::scripted:
            // Follow scripted behavior
            break;
    }
}

void npc_system::process_idle_state(npc& npc_ref) {
    auto& state = npc_ref.ai_state;

    // Check for enemies in aggro range
    if (npc_ref.ai.has_flag(ai_flags::aggressive) && npc_ref.is_monster()) {
        auto target = find_aggro_target(npc_ref);
        if (target.is_valid()) {
            state.target = target;
            state.set_state(ai_state::chase);
            LOG_DEBUG(general, "NPC {} acquired target {}", npc_ref.entity_id.id, target.id);
            return;
        }
    }

    // Random chance to wander
    if (!npc_ref.ai.has_flag(ai_flags::stationary)) {
        if (random_int(1, 100) <= 10) {  // 10% chance per think tick
            state.set_state(ai_state::wander);
        }
    }
}

void npc_system::process_wander_state(npc& npc_ref) {
    auto& state = npc_ref.ai_state;

    // Check for aggro while wandering
    if (npc_ref.ai.has_flag(ai_flags::aggressive) && npc_ref.is_monster()) {
        auto target = find_aggro_target(npc_ref);
        if (target.is_valid()) {
            state.target = target;
            state.set_state(ai_state::chase);
            return;
        }
    }

    // After some time, return to idle
    if (state.time_in_state_ms() > 5000) {
        state.set_state(ai_state::idle);
        return;
    }

    // Move randomly within wander range
    auto dir = random_direction();
    auto new_pos = hb::world::move_in_direction(npc_ref.pos, dir);

    // Check wander range from spawn
    int spawn_dist = new_pos.chebyshev_distance(state.spawn_point);
    if (spawn_dist <= npc_ref.ai.wander_range) {
        try_move_npc(npc_ref, new_pos);
    }
}

void npc_system::process_chase_state(npc& npc_ref) {
    auto& state = npc_ref.ai_state;

    if (!state.target.is_valid()) {
        state.set_state(ai_state::return_home);
        return;
    }

    // Check distance to spawn point
    int spawn_dist = npc_ref.pos.chebyshev_distance(state.spawn_point);
    if (spawn_dist > npc_ref.ai.chase_range) {
        // Too far from spawn, give up
        state.clear_target();
        state.set_state(ai_state::return_home);
        return;
    }

    // Get target position
    auto target_pos = get_entity_position(state.target);
    if (!target_pos.has_value()) {
        // Target no longer exists
        state.clear_target();
        state.set_state(ai_state::return_home);
        return;
    }

    // Check if target is on same map
    auto target_map = get_entity_map(state.target);
    if (!target_map.has_value() || target_map.value() != npc_ref.current_map) {
        state.clear_target();
        state.set_state(ai_state::return_home);
        return;
    }

    // Calculate distance to target
    int target_dist = npc_ref.pos.chebyshev_distance(target_pos.value());

    // If in attack range, switch to attack
    if (target_dist <= npc_ref.ai.attack_range) {
        state.set_state(ai_state::attack);
        return;
    }

    // Move towards target
    move_towards(npc_ref, target_pos.value());
}

void npc_system::process_attack_state(npc& npc_ref) {
    auto& state = npc_ref.ai_state;

    if (!state.target.is_valid()) {
        state.set_state(ai_state::return_home);
        return;
    }

    // Check flee condition
    if (npc_ref.ai.has_flag(ai_flags::cowardly) &&
        npc_ref.hp_percent() < npc_ref.ai.flee_hp_percent) {
        state.set_state(ai_state::flee);
        return;
    }

    // Get target position
    auto target_pos = get_entity_position(state.target);
    if (!target_pos.has_value()) {
        state.clear_target();
        state.set_state(ai_state::return_home);
        return;
    }

    // Check if still in range
    int target_dist = npc_ref.pos.chebyshev_distance(target_pos.value());
    if (target_dist > npc_ref.ai.attack_range) {
        state.set_state(ai_state::chase);
        return;
    }

    // Perform attack if cooldown is ready
    auto now = std::chrono::steady_clock::now();
    auto time_since_attack = std::chrono::duration_cast<std::chrono::milliseconds>(
        now - state.last_attack_time).count();

    int attack_cooldown = 1000 * 100 / npc_ref.attack_speed;  // Attack speed affects cooldown

    if (time_since_attack >= attack_cooldown) {
        perform_npc_attack(npc_ref, state.target);
        state.last_attack_time = now;
    }
}

void npc_system::process_flee_state(npc& npc_ref) {
    auto& state = npc_ref.ai_state;

    // If HP recovered, stop fleeing
    if (npc_ref.hp_percent() > npc_ref.ai.flee_hp_percent + 20) {
        state.set_state(ai_state::return_home);
        return;
    }

    // Move away from target or towards spawn
    if (state.target.is_valid()) {
        auto target_pos = get_entity_position(state.target);
        if (target_pos.has_value()) {
            // Move away from target
            auto dir = hb::world::direction_to(target_pos.value(), npc_ref.pos);
            auto new_pos = hb::world::move_in_direction(npc_ref.pos, dir);
            try_move_npc(npc_ref, new_pos);
            return;
        }
    }

    // Otherwise move towards spawn
    move_towards(npc_ref, state.spawn_point);
}

void npc_system::process_return_home_state(npc& npc_ref) {
    auto& state = npc_ref.ai_state;

    int spawn_dist = npc_ref.pos.chebyshev_distance(state.spawn_point);
    if (spawn_dist <= 1) {
        state.set_state(ai_state::idle);
        // Heal when returning home
        npc_ref.heal(npc_ref.max_hp / 10);
        return;
    }

    // Move towards spawn point
    move_towards(npc_ref, state.spawn_point);
}

auto npc_system::find_aggro_target(const npc& npc_ref) -> entity::entity {
    auto* player_sys = subsystems().get<player::player_system>();
    if (!player_sys) return entity::entity::null();

    entity::entity closest_target = entity::entity::null();
    int closest_dist = npc_ref.ai.aggro_range + 1;

    player_sys->for_each_player([&](player_id id, const player::player& p) {
        // Skip dead players
        if (p.is_dead()) return;

        // Skip players on different maps
        if (p.current_map != npc_ref.current_map) return;

        // Skip invisible players (unless NPC can detect)
        if (p.has_status(player::player_status::invisible) &&
            !npc_ref.ai.has_flag(ai_flags::detect_invisible)) {
            return;
        }

        // Check distance
        int dist = npc_ref.pos.chebyshev_distance(p.pos);
        if (dist <= npc_ref.ai.aggro_range && dist < closest_dist) {
            closest_dist = dist;
            closest_target = entity::entity{id.value, 0};
        }
    });

    return closest_target;
}

auto npc_system::get_entity_position(entity::entity e) const -> std::optional<hb::world::position> {
    // Check if it's a player
    auto* player_sys = subsystems().get<player::player_system>();
    if (player_sys) {
        if (auto* p = player_sys->get_player(player_id{e.id})) {
            return p->pos;
        }
    }

    // Check if it's an NPC
    if (auto* n = get_npc(e)) {
        return n->pos;
    }

    return std::nullopt;
}

auto npc_system::get_entity_map(entity::entity e) const -> std::optional<map_id> {
    auto* player_sys = subsystems().get<player::player_system>();
    if (player_sys) {
        if (auto* p = player_sys->get_player(player_id{e.id})) {
            return p->current_map;
        }
    }

    if (auto* n = get_npc(e)) {
        return n->current_map;
    }

    return std::nullopt;
}

void npc_system::move_towards(npc& npc_ref, hb::world::position target_pos) {
    auto dir = hb::world::direction_to(npc_ref.pos, target_pos);
    if (dir == hb::world::direction::none) return;

    auto new_pos = hb::world::move_in_direction(npc_ref.pos, dir);
    try_move_npc(npc_ref, new_pos);
}

void npc_system::try_move_npc(npc& npc_ref, hb::world::position new_pos) {
    // Check if position is walkable
    auto* world = subsystems().get<world::world_subsystem>();
    if (world && !world->can_move_to(npc_ref.current_map, new_pos)) {
        return;
    }

    // Update position
    npc_ref.pos = new_pos;
    npc_ref.facing = hb::world::direction_to(npc_ref.pos, new_pos);
}

void npc_system::perform_npc_attack(npc& npc_ref, entity::entity target) {
    auto* combat_sys = subsystems().get<combat::combat_system>();
    if (!combat_sys) return;

    // Create attack event
    combat::attack_event attack;
    attack.attacker = npc_ref.entity_id;
    attack.defender = target;
    attack.type = combat::damage_type::physical;
    attack.base_damage = npc_ref.roll_damage();

    // Process attack through combat system
    auto result = combat_sys->process_attack(attack);

    LOG_DEBUG(general, "NPC {} attacked {} for {} damage",
        npc_ref.entity_id.id, target.id, result.hit.final_damage);
}

}  // namespace hb::npc
