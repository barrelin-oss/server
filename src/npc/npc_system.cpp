// npc_system.cpp
// NPC management subsystem implementation

#include "npc/npc_system.h"
#include "npc/special_ability.h"
#include "core/logger.h"
#include "core/subsystem.h"
#include "player/player_system.h"
#include "combat/combat_system.h"
#include "effect/effect_system.h"
#include "world/world_subsystem.h"
#include "registry/npc_registry.h"
#include "npc/random_mob_generator.h"
#include "npc/spawn_rule_engine.h"
#include "npc/spawn_context.h"
#include "scheduler/scheduler.h"
#include "entity/entity_manager.h"
#include "entity/components/transform.h"
#include "entity/components/combat_stats.h"
#include "perf/perf_stats.h"

#include <array>
#include <random>

namespace hb::npc
{

namespace
{
// Random movement helper
thread_local std::mt19937 rng{std::random_device{}()};

auto random_direction() -> hb::world::direction
{
    std::uniform_int_distribution<int> dist(0, 7);
    return static_cast<hb::world::direction>(dist(rng));
}

auto random_int(int min, int max) -> int
{
    std::uniform_int_distribution<int> dist(min, max);
    return dist(rng);
}
} // namespace

npc_system::npc_system() = default;

npc_system::~npc_system()
{
    if (is_initialized())
    {
        shutdown();
    }
}

void npc_system::initialize()
{
    LOG_INFO(general, "NPC system initializing...");

    // Cache entity_manager pointer
    entity_manager_ = subsystems().get<entity::entity_manager>();
    if (!entity_manager_)
    {
        LOG_WARN(general, "NPC system: entity_manager not available, NPC components will be disabled");
    }

    // Initialize script executor
    script_executor_.set_npc_system(this);

    // Load scripts from scripts/ directory
    auto script_result = script_loader_.load_directory("scripts");
    if (script_result.is_ok() && script_result.value() > 0)
    {
        LOG_INFO(general, "NPC system loaded {} scripts", script_result.value());
    }

    // Load behavior trees from behaviors/ directory
    auto bt_result = bt_loader_.load_directory("behaviors");
    if (bt_result.is_ok() && bt_result.value() > 0)
    {
        LOG_INFO(general, "NPC system loaded {} behavior trees", bt_result.value());
    }

    // Initialize BT context
    bt_context_.npc_sys = this;

    // Initialize boss controller
    boss_controller_.set_npc_system(this);

    // Load boss configs from bosses/ directory
    auto boss_result = boss_loader_.load_directory("bosses");
    if (boss_result.is_ok() && boss_result.value() > 0)
    {
        LOG_INFO(general, "NPC system loaded {} boss configurations", boss_result.value());
    }

    set_initialized(true);
    LOG_INFO(general, "NPC system initialized (max_npcs: {})", config_.max_npcs);
}

void npc_system::shutdown()
{
    LOG_INFO(general, "NPC system shutting down...");

    npcs_.clear();
    spawn_points_.clear();

    set_initialized(false);
    LOG_INFO(general, "NPC system shutdown complete");
}

void npc_system::update(float delta_time)
{
    // Safety: flush any deferred deaths that weren't flushed by their caller
    flush_pending_deaths();

    update_spawns(delta_time);
    update_corpses(delta_time);
    if (config_.enable_ai)
    {
        auto* perf = subsystems().get<perf::perf_stats_system>();
        PERF_TIMER(perf, perf::metric_category::npc_ai_update);
        update_all_ai(delta_time);
    }
    script_executor_.update(delta_time);
    packs_.update(delta_time);
    boss_controller_.update(delta_time);
}

void npc_system::set_config(const npc_system_config& config)
{
    config_ = config;
}

auto npc_system::spawn_npc(npc_id template_id,
                           map_id map,
                           hb::world::position pos) -> result<entity::entity, std::string>
{
    auto* perf = subsystems().get<perf::perf_stats_system>();
    PERF_TIMER(perf, perf::metric_category::entity_lifecycle);

    if (npcs_.size() >= config_.max_npcs)
    {
        return result<entity::entity, std::string>::err("Maximum NPC count reached");
    }

    // Create entity via entity_manager (handles ID allocation)
    entity::entity eid;
    if (entity_manager_)
    {
        eid = entity_manager_->create(entity::entity_type::npc);
        if (!eid.is_valid())
        {
            return result<entity::entity, std::string>::err("Failed to create entity");
        }
    }
    else
    {
        // Fallback: generate our own ID (should not happen in normal operation)
        static uint32_t fallback_id = 1;
        eid = entity::entity{fallback_id++, 0};
    }

    // Look up template from registry
    auto* registry = subsystems().get<npc_registry>();
    const npc_template* tmpl = registry ? registry->get(template_id) : nullptr;

    auto new_npc = std::make_unique<npc>();

    new_npc->entity_id = eid;
    new_npc->template_id = template_id;
    new_npc->current_map = map;
    new_npc->pos = pos;
    new_npc->ai_state.spawn_point = pos;

    if (tmpl)
    {
        // Apply template stats
        new_npc->name = tmpl->name;
        new_npc->sprite_id = tmpl->sprite_id;

        // HP from legacy dice formula
        int32_t rolled_hp = 0;
        if (tmpl->hit_dice > 0)
        {
            if (tmpl->hit_dice <= 5)
            {
                // iDice(hit_dice, 4) + hit_dice
                int32_t dice_total = 0;
                for (int i = 0; i < tmpl->hit_dice; ++i)
                {
                    dice_total += random_int(1, 4);
                }
                rolled_hp = dice_total + tmpl->hit_dice;
            }
            else
            {
                // hit_dice * 5 + iDice(1, hit_dice)
                rolled_hp = tmpl->hit_dice * 5 + random_int(1, tmpl->hit_dice);
            }
        }
        new_npc->max_hp = rolled_hp;
        new_npc->hp = rolled_hp;
        new_npc->max_mp = tmpl->mp;
        new_npc->mp = tmpl->mp;
        new_npc->level = tmpl->level;
        new_npc->exp_reward = tmpl->exp_reward;
        new_npc->gold_min = tmpl->gold_min;
        new_npc->gold_max = tmpl->gold_max;
        new_npc->attack_dice = tmpl->attack_dice;
        new_npc->attack_sides = tmpl->attack_sides;
        new_npc->attack_bonus = tmpl->attack_bonus;
        new_npc->defense = tmpl->defense;
        new_npc->hit_rate = tmpl->hit_rate;
        new_npc->magic_defense = tmpl->magic_resist;

        // Extended combat stats from cfg
        new_npc->magic_level = tmpl->magic_level;
        new_npc->abs_damage = tmpl->abs_damage;
        new_npc->body_size = tmpl->body_size;
        new_npc->attribute = tmpl->attribute;
        new_npc->magic_hit_ratio = tmpl->magic_hit_ratio;
        new_npc->area = tmpl->area;

        // Speed defaults (not in cfg)
        new_npc->attack_speed = 100;
        new_npc->move_speed = 100;

        // Roll special ability from pool
        if (tmpl->sa_prob > 0 && tmpl->sa_pool > 0)
        {
            std::uniform_int_distribution<int> prob_dist(1, 100);
            if (prob_dist(rng) <= tmpl->sa_prob)
            {
                auto sa = roll_from_pool(registry->sa_cfg(), tmpl->sa_pool);
                if (sa > 0)
                    apply_special_ability(*new_npc, sa);
            }
        }
        // Beholder always gets Clairvoyant even without sa_prob
        if (new_npc->special_ability == 0 && new_npc->sprite_id == 53)
        {
            apply_special_ability(*new_npc, 1);
        }

        // Determine category from template type
        switch (tmpl->type)
        {
        case npc_type::monster:
            new_npc->category = npc_category::monster;
            break;
        case npc_type::boss:
            new_npc->category = npc_category::boss;
            break;
        case npc_type::guard:
            new_npc->category = npc_category::guard;
            break;
        case npc_type::npc:
            new_npc->category = npc_category::merchant; // Default NPCs to merchant
            break;
        default:
            new_npc->category = npc_category::monster;
            break;
        }

        // AI config from template
        new_npc->ai.aggro_range = tmpl->sight_range > 0 ? tmpl->sight_range : 10;
        new_npc->ai.attack_range = tmpl->attack_range > 0 ? tmpl->attack_range : 1;

        if (tmpl->is_aggressive)
        {
            new_npc->ai.flags = new_npc->ai.flags | ai_flags::aggressive;
        }

        // Wire min_bravery to flee behavior
        if (tmpl->min_bravery > 0 && tmpl->min_bravery < 100)
        {
            new_npc->ai.flee_hp_percent = tmpl->min_bravery;
            new_npc->ai.flags = new_npc->ai.flags | ai_flags::cowardly;
        }

        // Guard-specific AI overrides
        if (new_npc->category == npc_category::guard)
        {
            new_npc->ai.flags = new_npc->ai.flags | ai_flags::guard | ai_flags::aggressive | ai_flags::detect_invisible;
            new_npc->ai.aggro_range = std::max(new_npc->ai.aggro_range, static_cast<int16_t>(10));
            if (tmpl->behavior_tree.empty())
            {
                new_npc->ai.behavior_tree = "dungeon_guardian";
            }
            // Derive guard faction from name
            if (new_npc->name.find("Aresden") != std::string::npos)
            {
                new_npc->faction = faction::aresden;
            }
            else if (new_npc->name.find("Elvine") != std::string::npos)
            {
                new_npc->faction = faction::elvine;
            }
            // else stays neutral (Guard-Neutral)
        }

        // Apply behavior tree from template
        if (!tmpl->behavior_tree.empty())
        {
            new_npc->ai.behavior_tree = tmpl->behavior_tree;
        }

        // Set action interval from template (with 0-299ms spawn jitter to desynchronize NPCs)
        new_npc->ai.think_interval_ms = tmpl->action_time + random_int(0, 299);

        // Set movement type from template
        new_npc->ai.move_type = tmpl->move_type;

        // Loot is generated at death/despawn time by config-driven loot generator
        // (see loot_generator.h) using sprite_id and gold_min/gold_max from template

        // LOG_DEBUG(general, "Spawned NPC '{}' (template {}) at ({}, {}) on map {} - HP: {}, Level: {}",
        //     new_npc->name, template_id.value, pos.x, pos.y, map.value, new_npc->hp, new_npc->level);
    }
    else
    {
        // Fallback defaults for unknown templates
        new_npc->name = "Unknown";
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

        LOG_WARN(general,
                 "Spawned NPC with unknown template {} at ({}, {}) on map {}",
                 template_id.value,
                 pos.x,
                 pos.y,
                 map.value);
    }

    new_npc->ai_state.set_state(ai_state::idle);

    // Add components to entity_manager
    if (entity_manager_)
    {
        // Transform component
        entity_manager_->add_component<entity::transform>(eid, map, pos, hb::world::direction::south);

        // Health component
        entity_manager_->add_component<entity::health>(eid, new_npc->hp, new_npc->max_hp);

        // Mana component
        entity_manager_->add_component<entity::mana>(eid, new_npc->mp, new_npc->max_mp);

        // Combat stats component
        auto& stats = entity_manager_->add_component<entity::combat_stats>(eid);
        stats.defense = new_npc->defense;
        stats.magic_defense = new_npc->magic_defense;
        stats.hit_rate = new_npc->hit_rate;
        stats.dodge_rate = new_npc->dodge_rate;
        stats.attack_speed = new_npc->attack_speed;
        stats.move_speed = new_npc->move_speed;
    }

    // Add to spatial index and mark as occupant
    auto* world = subsystems().get<world::world_subsystem>();
    if (world)
    {
        auto* m = world->get_map(map);
        if (m)
        {
            m->spatial().add(hb::entity_id{eid.index()}, pos);
            m->set_occupant(pos, hb::entity_id{eid.index()}, world::owner_type::npc);
        }
    }

    // Invoke spawn callback if set
    npc* npc_ptr = new_npc.get();
    npcs_[eid] = std::move(new_npc);

    if (on_spawn_callback_)
    {
        on_spawn_callback_(*npc_ptr);
    }

    return result<entity::entity, std::string>::ok(eid);
}

auto npc_system::spawn_npc_at(spawn_point& spawn) -> result<entity::entity, std::string>
{
    if (!spawn.can_spawn())
    {
        return result<entity::entity, std::string>::err("Spawn point not ready");
    }

    auto pos = spawn.get_spawn_position();
    auto result = spawn_npc(spawn.npc_type, spawn.map, pos);

    if (result.is_ok())
    {
        spawn.on_spawn();
        auto* npc_ptr = get_npc(result.value());
        if (npc_ptr)
        {
            npc_ptr->spawn = &spawn;
            npc_ptr->ai_state.spawn_point = spawn.center;
        }
    }

    return result;
}

auto npc_system::spawn_random_mob(map_id map, hb::world::position pos) -> result<entity::entity, std::string>
{
    // Get subsystems
    auto* world = subsystems().get<world::world_subsystem>();
    if (!world)
    {
        return result<entity::entity, std::string>::err("World subsystem not available");
    }

    auto* map_ptr = world->get_map(map);
    if (!map_ptr)
    {
        return result<entity::entity, std::string>::err("Map not found");
    }

    auto* rule_engine = subsystems().get<spawn_rule_engine>();
    auto* sched = subsystems().get<scheduler>();

    // Try new rule engine first if available
    if (rule_engine && rule_engine->get_rule_count() > 0)
    {
        // Build spawn context
        spawn_context ctx;
        ctx.map_name = map_ptr->name();
        ctx.pos = pos;
        ctx.tile = map_ptr->get_static_tile(pos);
        ctx.dyn_tile = map_ptr->get_dynamic_tile(pos);
        ctx.map_ptr = map_ptr;
        ctx.clock = sched ? &sched->game_time() : nullptr;
        ctx.weather = map_ptr->weather();
        ctx.active_event = ""; // TODO: Add event system integration

        // Select NPC using rule engine
        auto npc_id = rule_engine->select_npc(ctx);
        if (npc_id.has_value())
        {
            LOG_DEBUG(general,
                      "Spawn rule engine selected NPC template {} for map {} at ({}, {})",
                      npc_id->value,
                      map_ptr->name(),
                      pos.x,
                      pos.y);
            return spawn_npc(*npc_id, map, pos);
        }

        // Fall through to legacy system if no rules matched
        LOG_DEBUG(general,
                  "No spawn rules matched for map {} at ({}, {}), falling back to legacy system",
                  map_ptr->name(),
                  pos.x,
                  pos.y);
    }

    // Fall back to legacy random mob generator
    if (!map_ptr->random_mob_generator_enabled())
    {
        return result<entity::entity, std::string>::err("Random mob generator not enabled on this map");
    }

    // Get random NPC for this map's level
    auto choice = random_mob_generator::get_random_npc(map_ptr->random_mob_generator_level());
    if (!choice.has_value())
    {
        return result<entity::entity, std::string>::err("Invalid random mob generator level");
    }

    // Spawn the selected NPC
    LOG_DEBUG(general,
              "Random mob spawn on map {} (level {}): {} (template {})",
              map.value,
              map_ptr->random_mob_generator_level(),
              choice->npc_name,
              choice->template_id.value);

    return spawn_npc(choice->template_id, map, pos);
}

void npc_system::despawn_npc(entity::entity id)
{
    auto* perf = subsystems().get<perf::perf_stats_system>();
    PERF_TIMER(perf, perf::metric_category::entity_lifecycle);

    auto it = npcs_.find(id);
    if (it == npcs_.end())
        return;

    auto& npc_ref = *it->second;

    // Remove active spell effects
    auto* effect_sys = subsystems().get<effect::effect_system>();
    if (effect_sys)
    {
        effect_sys->remove_all_effects(id);
    }

    // Remove from pack
    if (npc_ref.pack != 0)
    {
        packs_.remove_member(id);
        npc_ref.pack = 0;
    }

    // Unregister boss
    if (boss_controller_.is_boss(id))
    {
        boss_controller_.unregister_boss(id);
    }

    // Stop scripts
    script_executor_.stop(id);

    // Remove from spatial index and clear tile slots
    auto* world = subsystems().get<world::world_subsystem>();
    if (world)
    {
        auto* m = world->get_map(npc_ref.current_map);
        if (m)
        {
            m->spatial().remove(hb::entity_id{id.index()});
            m->clear_occupant(npc_ref.pos);

            // Clear dead slot only if it still belongs to this NPC
            auto dead_eid = m->get_dead_entity(npc_ref.pos);
            if (dead_eid && dead_eid->value == id.index())
            {
                m->clear_dead_entity(npc_ref.pos);
            }
        }
    }

    // Destroy entity in entity_manager (removes all components)
    if (entity_manager_)
    {
        entity_manager_->destroy(id);
    }

    if (npc_ref.spawn)
    {
        npc_ref.spawn->on_death();
    }

    // LOG_DEBUG(general, "Despawned NPC {}", id.id);
    npcs_.erase(it);
}

void npc_system::kill_npc(entity::entity id, entity::entity killer)
{
    auto* npc_ptr = get_npc(id);
    if (!npc_ptr || npc_ptr->is_dead())
        return;

    npc_ptr->hp = 0;
    npc_ptr->ai_state.set_state(ai_state::dead);
    npc_ptr->ai_state.death_time = std::chrono::steady_clock::now();

    // Move from live occupant slot to dead slot on tile
    auto* world = subsystems().get<world::world_subsystem>();
    if (world)
    {
        auto* m = world->get_map(npc_ptr->current_map);
        if (m)
        {
            m->clear_occupant(npc_ptr->pos);
            m->set_dead_entity(npc_ptr->pos, hb::entity_id{id.index()}, world::owner_type::npc);
        }
    }

    // Unregister boss (runs on_death_script)
    if (boss_controller_.is_boss(id))
    {
        boss_controller_.unregister_boss(id);
    }

    // Stop any running scripts
    script_executor_.stop(id);

    // Queue death for deferred processing (so caller can send broadcasts first)
    pending_npc_deaths_.push_back({*npc_ptr, killer, npc_ptr->max_hp}); // kill_npc = instant kill, use max_hp as damage
}

auto npc_system::get_npc(entity::entity id) -> npc*
{
    auto it = npcs_.find(id);
    return it != npcs_.end() ? it->second.get() : nullptr;
}

auto npc_system::get_npc(entity::entity id) const -> const npc*
{
    auto it = npcs_.find(id);
    return it != npcs_.end() ? it->second.get() : nullptr;
}

void npc_system::add_spawn_point(spawn_point point)
{
    spawn_points_.push_back(std::move(point));
}

void npc_system::remove_spawn_points(map_id map)
{
    std::erase_if(spawn_points_, [map](const spawn_point& sp) { return sp.map == map; });
}

void npc_system::activate_spawns(map_id map)
{
    for (auto& sp : spawn_points_)
    {
        if (sp.map == map)
        {
            sp.next_spawn_time = std::chrono::steady_clock::time_point{};
        }
    }
}

void npc_system::deactivate_spawns(map_id map)
{
    // Remove all NPCs from map
    std::vector<entity::entity> to_remove;
    for (auto& [id, npc_ptr] : npcs_)
    {
        if (npc_ptr->current_map == map)
        {
            to_remove.push_back(id);
        }
    }
    for (auto id : to_remove)
    {
        despawn_npc(id);
    }
}

void npc_system::apply_damage(entity::entity id, int32_t damage, entity::entity source)
{
    auto* npc_ptr = get_npc(id);
    if (!npc_ptr || npc_ptr->is_dead())
        return;

    // Track whether NPC was alive before damage
    bool was_alive = npc_ptr->is_alive();

    // Apply damage through health component if available
    if (entity_manager_)
    {
        if (auto* health = entity_manager_->get_component<entity::health>(id))
        {
            health->damage(damage);
            // Sync NPC struct hp with component
            npc_ptr->hp = health->current;

            if (!health->is_alive())
            {
                npc_ptr->ai_state.set_state(ai_state::dead);
                npc_ptr->ai_state.death_time = std::chrono::steady_clock::now();
            }
        }
        else
        {
            // Fallback: use NPC struct directly
            npc_ptr->damage(damage);
        }
    }
    else
    {
        npc_ptr->damage(damage);
    }

    // Set attacker as target if not already targeting
    if (!npc_ptr->ai_state.target.is_valid())
    {
        npc_ptr->ai_state.target = source;
        if (!npc_ptr->is_dead())
        {
            npc_ptr->ai_state.set_state(ai_state::chase);
        }
    }

    // Increase aggro
    npc_ptr->ai_state.aggro_level += damage;

    // Notify damage callback
    if (on_damage_callback_)
    {
        on_damage_callback_(*npc_ptr, damage, source);
    }

    // Call for help if this NPC has the calls_help flag
    if (npc_ptr->ai.has_flag(ai_flags::calls_help))
    {
        call_for_help(*npc_ptr, source);
    }

    // If NPC just died from this damage, process death
    // Note: kill_npc guards against is_dead(), so we need to handle the death
    // callback and cleanup directly here since damage() already set the dead state
    if (was_alive && npc_ptr->is_dead())
    {
        // Move from live occupant slot to dead slot on tile
        auto* world = subsystems().get<world::world_subsystem>();
        if (world)
        {
            auto* m = world->get_map(npc_ptr->current_map);
            if (m)
            {
                m->clear_occupant(npc_ptr->pos);
                m->set_dead_entity(npc_ptr->pos, hb::entity_id{id.index()}, world::owner_type::npc);
            }
        }

        // Unregister boss (runs on_death_script)
        if (boss_controller_.is_boss(id))
        {
            boss_controller_.unregister_boss(id);
        }

        // Stop any running scripts
        script_executor_.stop(id);

        // Queue death for deferred processing (so caller can send broadcasts first)
        pending_npc_deaths_.push_back({*npc_ptr, source, damage});
    }
}

void npc_system::flush_pending_deaths()
{
    if (pending_npc_deaths_.empty())
        return;

    // Move to local to allow re-entrancy
    auto deaths = std::move(pending_npc_deaths_);
    pending_npc_deaths_.clear();

    for (auto& death : deaths)
    {
        if (on_death_callback_)
        {
            on_death_callback_(death.snapshot, death.killer, death.damage);
        }
    }
}

void npc_system::set_target(entity::entity id, entity::entity target)
{
    auto* npc_ptr = get_npc(id);
    if (!npc_ptr)
        return;

    npc_ptr->ai_state.target = target;
    if (target.is_valid())
    {
        npc_ptr->ai_state.set_state(ai_state::chase);
    }
}

void npc_system::clear_target(entity::entity id)
{
    auto* npc_ptr = get_npc(id);
    if (!npc_ptr)
        return;

    npc_ptr->ai_state.clear_target();
    npc_ptr->ai_state.set_state(ai_state::return_home);
}

void npc_system::disengage_all_from(entity::entity target)
{
    for (auto& [id, npc_ptr] : npcs_)
    {
        if (npc_ptr->ai_state.target == target)
        {
            npc_ptr->ai_state.clear_target();
            npc_ptr->ai_state.set_state(ai_state::return_home);
        }
    }
}

void npc_system::update_ai(entity::entity id)
{
    auto* npc_ptr = get_npc(id);
    if (!npc_ptr || npc_ptr->is_dead())
        return;

    process_ai_state(*npc_ptr);
}

auto npc_system::get_npcs_in_range(map_id map,
                                   hb::world::position center,
                                   int range) const -> std::vector<entity::entity>
{
    std::vector<entity::entity> result;

    // Use spatial index for O(log n) query instead of O(n)
    auto* world = subsystems().get<world::world_subsystem>();
    if (!world)
    {
        // Fallback to O(n) scan when world subsystem isn't available (e.g., in tests)
        for (const auto& [id, npc_ptr] : npcs_)
        {
            if (npc_ptr->current_map != map)
                continue;
            if (npc_ptr->is_dead())
                continue;

            int dist = center.chebyshev_distance(npc_ptr->pos);
            if (dist <= range)
            {
                result.push_back(id);
            }
        }
        return result;
    }

    auto* m = world->get_map(map);
    if (!m)
    {
        // Map not found - also fallback to O(n) scan
        for (const auto& [id, npc_ptr] : npcs_)
        {
            if (npc_ptr->current_map != map)
                continue;
            if (npc_ptr->is_dead())
                continue;

            int dist = center.chebyshev_distance(npc_ptr->pos);
            if (dist <= range)
            {
                result.push_back(id);
            }
        }
        return result;
    }

    // Get entities in range from spatial index
    auto entities = m->get_entities_in_range(center, range);

    // Filter for NPCs using entity_manager type check
    for (auto eid : entities)
    {
        // Look up as entity with generation 0 for type check
        entity::entity e{eid.value, 0};

        // Check if this is an NPC (entity_manager validates)
        if (entity_manager_)
        {
            if (entity_manager_->get_type(e) != entity::entity_type::npc)
            {
                continue;
            }
        }

        // Find the NPC in our map
        auto it = npcs_.find(e);
        if (it == npcs_.end())
        {
            // Try with proper entity lookup (generation might differ)
            for (const auto& [npc_id, npc_ptr] : npcs_)
            {
                if (npc_id.index() == eid.value && !npc_ptr->is_dead())
                {
                    result.push_back(npc_id);
                    break;
                }
            }
        }
        else if (!it->second->is_dead())
        {
            result.push_back(it->first);
        }
    }

    return result;
}

void npc_system::update_spawns(float delta_time)
{
    spawn_accumulator_ += delta_time * 1000.0f;

    if (spawn_accumulator_ < static_cast<float>(config_.spawn_check_interval_ms))
    {
        return;
    }

    spawn_accumulator_ -= static_cast<float>(config_.spawn_check_interval_ms);

    for (auto& sp : spawn_points_)
    {
        if (sp.can_spawn())
        {
            spawn_npc_at(sp);
        }
    }
}

void npc_system::update_corpses(float delta_time)
{
    corpse_accumulator_ += delta_time * 1000.0f;

    if (corpse_accumulator_ < static_cast<float>(config_.corpse_check_interval_ms))
    {
        return;
    }

    corpse_accumulator_ -= static_cast<float>(config_.corpse_check_interval_ms);

    // Collect expired corpse IDs first to avoid iterator invalidation
    std::vector<entity::entity> expired;
    for (const auto& [id, npc_ptr] : npcs_)
    {
        if (!npc_ptr->is_dead())
            continue;
        if (npc_ptr->ai_state.time_since_death_ms() >= config_.corpse_linger_ms)
        {
            expired.push_back(id);
        }
    }

    for (auto id : expired)
    {
        auto* npc_ptr = get_npc(id);
        if (!npc_ptr)
            continue;

        // Fire despawn callback before removing the NPC
        if (on_despawn_callback_)
        {
            on_despawn_callback_(*npc_ptr);
        }

        despawn_npc(id);
    }

    if (!expired.empty())
    {
        LOG_DEBUG(general, "Cleaned up {} corpses", expired.size());
    }
}

void npc_system::update_all_ai(float delta_time)
{
    ai_accumulator_ += delta_time * 1000.0f;

    if (ai_accumulator_ < static_cast<float>(config_.ai_update_interval_ms))
    {
        return;
    }

    ai_accumulator_ -= static_cast<float>(config_.ai_update_interval_ms);

    for (auto& [id, npc_ptr] : npcs_)
    {
        if (npc_ptr->is_dead())
            continue;
        process_ai_state(*npc_ptr);
    }
}

void npc_system::process_ai_state(npc& npc_ref)
{
    // Skip AI if NPC is stunned, frozen, or paralyzed by spell effects
    auto* effect_sys = subsystems().get<effect::effect_system>();
    if (effect_sys)
    {
        if (effect_sys->has_status(npc_ref.entity_id, player::player_status::stunned) ||
            effect_sys->has_status(npc_ref.entity_id, player::player_status::frozen) ||
            effect_sys->has_status(npc_ref.entity_id, player::player_status::paralyzed))
        {
            return;
        }
    }

    auto now = std::chrono::steady_clock::now();
    auto& state = npc_ref.ai_state;

    // Calculate effective action interval
    auto time_since_think = std::chrono::duration_cast<std::chrono::milliseconds>(now - state.last_think_time).count();

    int32_t effective_interval = npc_ref.ai.think_interval_ms;

    // Attack mode: randomly reduce interval (legacy d7 roll)
    if (state.state == ai_state::attack)
    {
        static constexpr std::array<int32_t, 7> reductions = {0, 100, 200, 300, 400, 600, 700};
        effective_interval -= reductions[static_cast<size_t>(random_int(0, 6))];
        if (effective_interval < 600)
            effective_interval = 600;
    }

    // TODO: Ice magic effect adds +50% to action time
    // if (npc_ref has ice effect) effective_interval += effective_interval / 2;

    if (time_since_think < effective_interval)
    {
        return;
    }

    state.last_think_time = now;

    // If NPC has a behavior tree, tick it instead of using the state machine
    if (!npc_ref.ai.behavior_tree.empty())
    {
        auto* tree = bt_loader_.get_tree(npc_ref.ai.behavior_tree);
        if (tree)
        {
            bt_context_.delta_time = static_cast<float>(npc_ref.ai.think_interval_ms) / 1000.0f;
            const_cast<bt::bt_node*>(tree)->tick(npc_ref, bt_context_);
            return;
        }
    }

    switch (state.state)
    {
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
        // Scripted NPCs are driven by the script_executor
        // Return early to skip state machine
        break;
    }
}

void npc_system::process_idle_state(npc& npc_ref)
{
    auto& state = npc_ref.ai_state;

    // Check for enemies in aggro range
    if (npc_ref.ai.has_flag(ai_flags::aggressive) && npc_ref.is_monster())
    {
        auto target = find_aggro_target(npc_ref);
        if (target.is_valid())
        {
            state.target = target;
            state.set_state(ai_state::chase);
            LOG_DEBUG(general, "NPC {} acquired target {}", npc_ref.entity_id.id, target.id);
            return;
        }
    }

    // Social NPCs try to form packs
    if (npc_ref.ai.has_flag(ai_flags::social) && npc_ref.pack == 0)
    {
        try_form_pack(npc_ref);
    }

    // Decide whether to wander based on movement type
    if (!npc_ref.ai.has_flag(ai_flags::stationary))
    {
        bool should_wander = false;

        switch (npc_ref.ai.move_type)
        {
        case hb::npc_move_type::stop:
            // Never wander
            should_wander = false;
            break;

        case hb::npc_move_type::random_area:
        case hb::npc_move_type::random:
            // Always wander for free roaming types
            should_wander = true;
            break;

        case hb::npc_move_type::seq_waypoint:
        case hb::npc_move_type::random_waypoint:
        case hb::npc_move_type::guard:
            // Waypoint and guard types follow paths (not implemented here,
            // would need separate handling in process_wander_state)
            should_wander = true;
            break;

        case hb::npc_move_type::follow:
            // Follow types would be handled separately
            break;
        }

        if (should_wander)
        {
            state.set_state(ai_state::wander);
        }
    }
}

void npc_system::process_wander_state(npc& npc_ref)
{
    auto& state = npc_ref.ai_state;

    // Check for aggro while wandering
    if (npc_ref.ai.has_flag(ai_flags::aggressive) && npc_ref.is_monster())
    {
        auto target = find_aggro_target(npc_ref);
        if (target.is_valid())
        {
            state.target = target;
            state.set_state(ai_state::chase);
            return;
        }
    }

    // After some time, return to idle
    if (state.time_in_state_ms() > 5000)
    {
        state.set_state(ai_state::idle);
        return;
    }

    // Pack members follow their leader instead of random wander
    if (state.pack_leader.is_valid())
    {
        auto leader_pos = get_entity_position(state.pack_leader);
        if (leader_pos.has_value())
        {
            int dist_to_leader = npc_ref.pos.chebyshev_distance(leader_pos.value());
            if (dist_to_leader > 3)
            {
                move_towards(npc_ref, leader_pos.value());
                return;
            }
        }
        else
        {
            // Leader gone, clear pack reference
            state.pack_leader = entity::entity::null();
            packs_.remove_member(npc_ref.entity_id);
            npc_ref.pack = 0;
        }
    }

    // Move randomly within wander range
    auto dir = random_direction();
    auto new_pos = hb::world::move_in_direction(npc_ref.pos, dir);

    // Check wander range from spawn
    int spawn_dist = new_pos.chebyshev_distance(state.spawn_point);
    if (spawn_dist <= npc_ref.ai.wander_range)
    {
        try_move_npc(npc_ref, new_pos);
    }
}

void npc_system::process_chase_state(npc& npc_ref)
{
    auto& state = npc_ref.ai_state;

    if (!state.target.is_valid())
    {
        state.set_state(ai_state::return_home);
        return;
    }

    // Check distance to spawn point
    int spawn_dist = npc_ref.pos.chebyshev_distance(state.spawn_point);
    if (spawn_dist > npc_ref.ai.chase_range)
    {
        // Too far from spawn, give up
        state.clear_target();
        state.set_state(ai_state::return_home);
        return;
    }

    // Get target position
    auto target_pos = get_entity_position(state.target);
    if (!target_pos.has_value())
    {
        // Target no longer exists
        state.clear_target();
        state.set_state(ai_state::return_home);
        return;
    }

    // Check if target is on same map
    auto target_map = get_entity_map(state.target);
    if (!target_map.has_value() || target_map.value() != npc_ref.current_map)
    {
        state.clear_target();
        state.set_state(ai_state::return_home);
        return;
    }

    // Calculate distance to target
    int target_dist = npc_ref.pos.chebyshev_distance(target_pos.value());

    // If in attack range, switch to attack
    if (target_dist <= npc_ref.ai.attack_range)
    {
        state.set_state(ai_state::attack);
        return;
    }

    // Move towards target
    move_towards(npc_ref, target_pos.value());
}

void npc_system::process_attack_state(npc& npc_ref)
{
    auto& state = npc_ref.ai_state;

    if (!state.target.is_valid())
    {
        state.set_state(ai_state::return_home);
        return;
    }

    // Check flee condition
    if (npc_ref.ai.has_flag(ai_flags::cowardly) && npc_ref.hp_percent() < npc_ref.ai.flee_hp_percent)
    {
        state.set_state(ai_state::flee);
        return;
    }

    // Get target position
    auto target_pos = get_entity_position(state.target);
    if (!target_pos.has_value())
    {
        state.clear_target();
        state.set_state(ai_state::return_home);
        return;
    }

    // Check if still in range
    int target_dist = npc_ref.pos.chebyshev_distance(target_pos.value());
    if (target_dist > npc_ref.ai.attack_range)
    {
        state.set_state(ai_state::chase);
        return;
    }

    // Attack once per AI tick (action interval already gates frequency)
    perform_npc_attack(npc_ref, state.target);
    state.last_attack_time = std::chrono::steady_clock::now();
}

void npc_system::process_flee_state(npc& npc_ref)
{
    auto& state = npc_ref.ai_state;

    // If HP recovered, stop fleeing
    if (npc_ref.hp_percent() > npc_ref.ai.flee_hp_percent + 20)
    {
        state.set_state(ai_state::return_home);
        return;
    }

    // Move away from target or towards spawn
    if (state.target.is_valid())
    {
        auto target_pos = get_entity_position(state.target);
        if (target_pos.has_value())
        {
            // Move away from target
            auto dir = hb::world::direction_to(target_pos.value(), npc_ref.pos);
            if (dir.has_value())
            {
                auto new_pos = hb::world::move_in_direction(npc_ref.pos, *dir);
                try_move_npc(npc_ref, new_pos);
                return;
            }
        }
    }

    // Otherwise move towards spawn
    move_towards(npc_ref, state.spawn_point);
}

void npc_system::process_return_home_state(npc& npc_ref)
{
    auto& state = npc_ref.ai_state;

    int spawn_dist = npc_ref.pos.chebyshev_distance(state.spawn_point);
    if (spawn_dist <= 1)
    {
        state.set_state(ai_state::idle);
        // Heal when returning home
        npc_ref.heal(npc_ref.max_hp / 10);
        return;
    }

    // Move towards spawn point
    move_towards(npc_ref, state.spawn_point);
}

auto npc_system::find_aggro_target(const npc& npc_ref) -> entity::entity
{
    // Use unified spatial query for efficient target finding
    auto* world = subsystems().get<world::world_subsystem>();
    if (!world)
        return entity::entity::null();

    auto* player_sys = subsystems().get<player::player_system>();

    // Get all entities in aggro range using spatial index
    auto entities = world->get_all_entities_in_range(npc_ref.current_map, npc_ref.pos, npc_ref.ai.aggro_range);

    entity::entity closest_target = entity::entity::null();
    int closest_dist = npc_ref.ai.aggro_range + 1;

    for (const auto& entry : entities)
    {
        // Only target players for now (could extend to hostile NPCs later)
        if (entry.type != entity::entity_type::player)
        {
            continue;
        }

        // Need to look up the actual player to check status
        if (!player_sys)
            continue;

        // Find player by ecs_entity index
        player::player* p = nullptr;
        player_sys->for_each_player(
            [&](player_id, player::player& player)
            {
                if (player.ecs_entity.index() == entry.entity.index())
                {
                    p = &player;
                }
            });

        if (!p)
            continue;

        // Skip dead players
        if (p->is_dead())
            continue;

        // Skip invisible players (unless NPC can detect)
        if (p->has_status(player::player_status::invisible) && !npc_ref.ai.has_flag(ai_flags::detect_invisible))
        {
            continue;
        }

        // Guards only target criminals and murderers
        if (npc_ref.ai.has_flag(ai_flags::guard))
        {
            if (!p->pk.is_criminal() && !p->pk.is_murderer())
            {
                continue;
            }
        }

        // Check distance (spatial query already filtered by range, but find closest)
        int dist = npc_ref.pos.chebyshev_distance(entry.pos);
        if (dist < closest_dist)
        {
            closest_dist = dist;
            closest_target = p->ecs_entity;
        }
    }

    return closest_target;
}

auto npc_system::get_entity_position(entity::entity e) const -> std::optional<hb::world::position>
{
    // Check if it's a player
    auto* player_sys = subsystems().get<player::player_system>();
    if (player_sys)
    {
        if (auto* p = player_sys->get_player_by_entity(e))
        {
            if (p->is_dead())
                return std::nullopt;
            return p->pos;
        }
    }

    // Check if it's an NPC
    if (auto* n = get_npc(e))
    {
        if (n->is_dead())
            return std::nullopt;
        return n->pos;
    }

    return std::nullopt;
}

auto npc_system::get_entity_map(entity::entity e) const -> std::optional<map_id>
{
    auto* player_sys = subsystems().get<player::player_system>();
    if (player_sys)
    {
        if (auto* p = player_sys->get_player_by_entity(e))
        {
            return p->current_map;
        }
    }

    if (auto* n = get_npc(e))
    {
        return n->current_map;
    }

    return std::nullopt;
}

void npc_system::move_towards(npc& npc_ref, hb::world::position target_pos)
{
    auto* perf = subsystems().get<perf::perf_stats_system>();
    PERF_TIMER(perf, perf::metric_category::npc_pathfinding);

    auto dir = hb::world::direction_to(npc_ref.pos, target_pos);
    if (!dir.has_value())
        return;

    auto new_pos = hb::world::move_in_direction(npc_ref.pos, *dir);
    try_move_npc(npc_ref, new_pos);
}

void npc_system::try_move_npc(npc& npc_ref, hb::world::position new_pos)
{
    // Check if position is walkable
    auto* world = subsystems().get<world::world_subsystem>();
    if (world && !world->can_move_to(npc_ref.current_map, new_pos))
    {
        return;
    }

    // Get the map
    auto* m = world ? world->get_map(npc_ref.current_map) : nullptr;
    if (!m)
        return;

    // Check if new position is occupied by another alive entity
    auto occupant = m->get_occupant(new_pos);
    if (occupant.has_value() && occupant.value().value != 0)
    {
        return; // Blocked by another entity
    }

    // Clear old position's occupant
    m->clear_occupant(npc_ref.pos);

    // Update position
    auto old_pos = npc_ref.pos;
    npc_ref.pos = new_pos;
    if (auto dir = hb::world::direction_to(old_pos, new_pos))
    {
        npc_ref.facing = *dir;
    }

    // Set new position's occupant
    m->set_occupant(new_pos, hb::entity_id{npc_ref.entity_id.index()}, world::owner_type::npc);

    // Update transform component
    if (entity_manager_)
    {
        if (auto* t = entity_manager_->get_component<entity::transform>(npc_ref.entity_id))
        {
            t->pos = new_pos;
            t->facing = npc_ref.facing;
        }
    }

    // Update spatial index
    m->spatial().update(hb::entity_id{npc_ref.entity_id.index()}, new_pos);

    // Invoke move callback
    if (on_move_callback_)
    {
        on_move_callback_(npc_ref, old_pos);
    }
}

void npc_system::perform_npc_attack(npc& npc_ref, entity::entity target)
{
    auto* combat_sys = subsystems().get<combat::combat_system>();
    if (!combat_sys)
        return;

    // Create attack event
    combat::attack_event attack;
    attack.attacker = npc_ref.entity_id;
    attack.defender = target;
    attack.type = combat::damage_type::physical;
    attack.base_damage = npc_ref.roll_damage();

    // Process attack through combat system
    auto result = combat_sys->process_attack(attack);

    LOG_DEBUG(general,
              "NPC {} '{}' attacked {} for {} damage",
              npc_ref.entity_id.id,
              npc_ref.name,
              target.id,
              result.hit.final_damage);

    // Invoke attack callback
    if (on_attack_callback_)
    {
        on_attack_callback_(npc_ref, target, result.hit.final_damage);
    }

    // Flush deferred deaths (NPC-vs-NPC or NPC-vs-player kills)
    if (result.target_killed)
    {
        flush_pending_deaths();
    }
}

auto npc_system::reload_boss_configs() -> result<size_t, std::string>
{
    return boss_loader_.reload();
}

void npc_system::register_boss(entity::entity id, std::string_view boss_config_name)
{
    auto* config = boss_loader_.get_config(boss_config_name);
    if (!config)
    {
        LOG_WARN(general, "Boss config '{}' not found", boss_config_name);
        return;
    }

    boss_controller_.register_boss(id, *config);
}

auto npc_system::find_aggro_target_for(const npc& npc_ref) -> entity::entity
{
    return find_aggro_target(npc_ref);
}

void npc_system::move_npc_towards_target(npc& npc_ref)
{
    if (!npc_ref.ai_state.target.is_valid())
        return;

    auto target_pos = get_entity_position(npc_ref.ai_state.target);
    if (target_pos.has_value())
    {
        move_towards(npc_ref, target_pos.value());
    }
}

void npc_system::move_npc_away_from_target(npc& npc_ref)
{
    if (!npc_ref.ai_state.target.is_valid())
        return;

    auto target_pos = get_entity_position(npc_ref.ai_state.target);
    if (target_pos.has_value())
    {
        auto dir = hb::world::direction_to(target_pos.value(), npc_ref.pos);
        if (dir.has_value())
        {
            auto new_pos = hb::world::move_in_direction(npc_ref.pos, *dir);
            try_move_npc(npc_ref, new_pos);
        }
    }
}

void npc_system::move_npc_towards_position(npc& npc_ref, hb::world::position target_pos)
{
    move_towards(npc_ref, target_pos);
}

void npc_system::perform_attack(npc& npc_ref, entity::entity target)
{
    perform_npc_attack(npc_ref, target);
}

void npc_system::call_for_help_from(const npc& caller, entity::entity attacker)
{
    call_for_help(caller, attacker);
}

void npc_system::try_move(npc& npc_ref, hb::world::position new_pos)
{
    try_move_npc(npc_ref, new_pos);
}

auto npc_system::reload_behavior_trees() -> result<size_t, std::string>
{
    return bt_loader_.reload();
}

void npc_system::start_script(entity::entity npc_entity, std::string_view script_name)
{
    auto* script = script_loader_.get_script(script_name);
    if (!script)
    {
        LOG_WARN(general, "Script '{}' not found", script_name);
        return;
    }

    auto* npc_ptr = get_npc(npc_entity);
    if (!npc_ptr)
        return;

    npc_ptr->ai_state.set_state(ai_state::scripted);
    script_executor_.start(npc_entity, *script);
}

void npc_system::stop_script(entity::entity npc_entity)
{
    script_executor_.stop(npc_entity);

    auto* npc_ptr = get_npc(npc_entity);
    if (npc_ptr && npc_ptr->ai_state.state == ai_state::scripted)
    {
        npc_ptr->ai_state.set_state(ai_state::idle);
    }
}

auto npc_system::reload_scripts() -> result<size_t, std::string>
{
    return script_loader_.reload();
}

void npc_system::call_for_help(const npc& caller, entity::entity attacker)
{
    // Alert nearby NPCs of the same template to help
    int help_range = caller.ai.aggro_range * 2;
    auto nearby = get_npcs_in_range(caller.current_map, caller.pos, help_range);

    for (auto ally_id : nearby)
    {
        if (ally_id == caller.entity_id)
            continue;

        auto* ally = get_npc(ally_id);
        if (!ally || ally->is_dead())
            continue;

        // Only same template or same category responds to help calls
        if (ally->template_id != caller.template_id)
            continue;

        // Only idle/wandering NPCs respond
        if (ally->ai_state.state != ai_state::idle && ally->ai_state.state != ai_state::wander)
        {
            continue;
        }

        // Skip if already has a target
        if (ally->ai_state.target.is_valid())
            continue;

        // Set attacker as target and switch to chase
        ally->ai_state.target = attacker;
        ally->ai_state.set_state(ai_state::chase);
        LOG_DEBUG(
            general, "NPC {} '{}' responding to help call from '{}'", ally->entity_id.id, ally->name, caller.name);
    }
}

void npc_system::try_form_pack(npc& npc_ref)
{
    // Look for nearby same-template NPCs to form a pack
    auto nearby = get_npcs_in_range(npc_ref.current_map, npc_ref.pos, npc_ref.ai.wander_range);

    for (auto other_id : nearby)
    {
        if (other_id == npc_ref.entity_id)
            continue;

        auto* other = get_npc(other_id);
        if (!other || other->is_dead())
            continue;
        if (other->template_id != npc_ref.template_id)
            continue;
        if (!other->ai.has_flag(ai_flags::social))
            continue;

        // Found a social ally - join or form a pack
        if (other->pack != 0)
        {
            // Join existing pack
            auto* existing_pack = packs_.get_pack(other->pack);
            if (existing_pack && existing_pack->members.size() < 6)
            {
                packs_.add_member(other->pack, npc_ref.entity_id);
                npc_ref.pack = other->pack;
                npc_ref.ai_state.pack_leader = existing_pack->leader;
                return;
            }
        }
        else
        {
            // Form new pack with other as leader
            auto pid = packs_.create_pack(other->entity_id, npc_ref.current_map);
            other->pack = pid;

            packs_.add_member(pid, npc_ref.entity_id);
            npc_ref.pack = pid;
            npc_ref.ai_state.pack_leader = other->entity_id;
            return;
        }
    }
}

} // namespace hb::npc
