// dynamic_object_system.cpp
// Dynamic object lifecycle and field effect processing

#include "world/dynamic_object_system.h"
#include "world/world_subsystem.h"
#include "world/map.h"
#include "player/player_system.h"
#include "npc/npc_system.h"
#include "npc/npc.h"
#include "combat/combat_system.h"
#include "effect/effect_system.h"
#include "core/subsystem.h"
#include "core/logger.h"

#include <algorithm>
#include <chrono>
#include <random>

namespace hb::world
{

namespace
{

constexpr float effect_tick_seconds = 1.0f;   // Legacy game tick (~1s)
constexpr int64_t freeze_duration_ms = 20000; // Ice storm freeze (legacy: 20s)
constexpr int64_t poison_duration_ms = 30000; // Poison cloud poison duration
constexpr int64_t poison_tick_ms = 3000;      // Poison DoT tick interval

auto roll(int dice, int sides) -> int32_t
{
    thread_local std::mt19937 rng{std::random_device{}()};
    if (dice <= 0 || sides <= 0)
        return 0;
    std::uniform_int_distribution<int> dist(1, sides);
    int32_t total = 0;
    for (int i = 0; i < dice; ++i)
        total += dist(rng);
    return total;
}

} // anonymous namespace

dynamic_object_system::dynamic_object_system() = default;

dynamic_object_system::~dynamic_object_system()
{
    if (is_initialized())
    {
        shutdown();
    }
}

void dynamic_object_system::initialize()
{
    set_initialized(true);
    LOG_INFO(general, "Dynamic object system initialized");
}

void dynamic_object_system::shutdown()
{
    objects_.clear();
    set_initialized(false);
}

void dynamic_object_system::update(float delta_time)
{
    tick_accum_ += delta_time;
    if (tick_accum_ < effect_tick_seconds)
        return;
    tick_accum_ = 0.0f;

    process_expiry(now_ms());
    process_effects();
}

auto dynamic_object_system::spawn(entity::entity owner,
                                  dynamic_object_type type,
                                  map_id map,
                                  position pos,
                                  int64_t duration_ms,
                                  int32_t power) -> uint16_t
{
    auto* world = subsystems().get<world_subsystem>();
    if (!world)
        return 0;

    auto* m = world->get_map(map);
    if (!m)
        return 0;

    if (!m->is_walkable(pos) || m->is_safe_zone(pos))
        return 0;

    auto* tile = m->get_dynamic_tile(pos);
    if (!tile || tile->has_dynamic_object())
        return 0;

    auto id = allocate_id();
    if (id == 0)
        return 0;

    dynamic_object obj{
        .id = id,
        .type = type,
        .owner = owner,
        .map = map,
        .pos = pos,
        .created_ms = now_ms(),
        .duration_ms = duration_ms,
        .power = power,
    };

    tile->dynamic_object_id = id;
    tile->dynamic_object_type = static_cast<uint16_t>(type);

    auto [it, inserted] = objects_.emplace(id, obj);
    if (on_spawn_)
        on_spawn_(it->second);

    return id;
}

auto dynamic_object_system::spawn_field(entity::entity owner,
                                        dynamic_object_type type,
                                        map_id map,
                                        position center,
                                        int16_t radius_x,
                                        int16_t radius_y,
                                        int64_t duration_ms,
                                        int32_t power) -> int
{
    int spawned = 0;

    if (type == dynamic_object_type::spike)
    {
        // Spike fields fill the area with one trap per tile
        for (int16_t dx = static_cast<int16_t>(-radius_x); dx <= radius_x; ++dx)
        {
            for (int16_t dy = static_cast<int16_t>(-radius_y); dy <= radius_y; ++dy)
            {
                position pos{static_cast<int16_t>(center.x + dx), static_cast<int16_t>(center.y + dy)};
                if (spawn(owner, type, map, pos, duration_ms, power) != 0)
                    ++spawned;
            }
        }
    }
    else
    {
        // Area-processing types (fire, icestorm, pcloud) are a single object whose
        // tick covers the surrounding tiles
        if (spawn(owner, type, map, center, duration_ms, power) != 0)
            ++spawned;
    }

    return spawned;
}

void dynamic_object_system::remove(uint16_t id)
{
    auto it = objects_.find(id);
    if (it == objects_.end())
        return;

    // Clear the tile registration (only if this object still owns it)
    if (auto* world = subsystems().get<world_subsystem>())
    {
        if (auto* m = world->get_map(it->second.map))
        {
            if (auto* tile = m->get_dynamic_tile(it->second.pos); tile && tile->dynamic_object_id == id)
            {
                tile->dynamic_object_id = 0;
                tile->dynamic_object_type = 0;
            }
        }
    }

    if (on_remove_)
        on_remove_(it->second);

    objects_.erase(it);
}

auto dynamic_object_system::get(uint16_t id) const -> const dynamic_object*
{
    auto it = objects_.find(id);
    return it != objects_.end() ? &it->second : nullptr;
}

void dynamic_object_system::on_entity_step(entity::entity ent, map_id map, position pos)
{
    auto* world = subsystems().get<world_subsystem>();
    if (!world)
        return;

    auto* m = world->get_map(map);
    if (!m)
        return;

    const auto* tile = m->get_dynamic_tile(pos);
    if (!tile || tile->dynamic_object_type != static_cast<uint16_t>(dynamic_object_type::spike))
        return;

    const auto* obj = get(tile->dynamic_object_id);
    if (!obj)
        return;

    // Spikes never hurt their owner
    if (obj->owner.id != 0 && obj->owner.id == ent.id)
        return;

    if (auto* combat = subsystems().get<combat::combat_system>())
    {
        int32_t damage = roll(2, 4); // Legacy: 2d4 on step
        combat->deal_damage(ent, damage, combat::damage_type::physical, obj->owner);
        LOG_DEBUG(general, "Spike {} dealt {} damage to entity {} at ({},{})", obj->id, damage, ent.id, pos.x, pos.y);
    }
}

auto dynamic_object_system::objects_in_area(map_id map, position center, int radius_x, int radius_y) const
    -> std::vector<const dynamic_object*>
{
    std::vector<const dynamic_object*> result;
    for (const auto& [id, obj] : objects_)
    {
        if (obj.map == map && std::abs(obj.pos.x - center.x) <= radius_x && std::abs(obj.pos.y - center.y) <= radius_y)
        {
            result.push_back(&obj);
        }
    }
    return result;
}

void dynamic_object_system::process_expiry(int64_t now)
{
    std::vector<uint16_t> expired;
    for (const auto& [id, obj] : objects_)
    {
        if (obj.duration_ms > 0 && now - obj.created_ms >= obj.duration_ms)
        {
            expired.push_back(id);
        }
    }
    for (auto id : expired)
    {
        remove(id);
    }
}

void dynamic_object_system::process_effects()
{
    // Collect ids first: damage can trigger callbacks that could mutate the map
    std::vector<uint16_t> active;
    active.reserve(objects_.size());
    for (const auto& [id, obj] : objects_)
    {
        if (is_damaging_area_type(obj.type))
            active.push_back(id);
    }

    for (auto id : active)
    {
        auto it = objects_.find(id);
        if (it != objects_.end())
        {
            apply_area_damage(it->second);
        }
    }
}

auto dynamic_object_system::is_damaging_area_type(dynamic_object_type type) -> bool
{
    switch (type)
    {
    case dynamic_object_type::fire:
    case dynamic_object_type::fire3:
    case dynamic_object_type::icestorm:
    case dynamic_object_type::pcloud_begin:
    case dynamic_object_type::pcloud_loop:
    case dynamic_object_type::pcloud_end:
        return true;
    default:
        return false;
    }
}

void dynamic_object_system::apply_area_damage(const dynamic_object& obj)
{
    // Legacy areas: fire/pcloud 3x3, icestorm 5x5
    int radius = obj.type == dynamic_object_type::icestorm ? 2 : 1;

    for (int16_t dx = static_cast<int16_t>(-radius); dx <= radius; ++dx)
    {
        for (int16_t dy = static_cast<int16_t>(-radius); dy <= radius; ++dy)
        {
            position tile{static_cast<int16_t>(obj.pos.x + dx), static_cast<int16_t>(obj.pos.y + dy)};
            damage_entity_at(obj, tile);
        }
    }
}

void dynamic_object_system::damage_entity_at(const dynamic_object& obj, position tile)
{
    auto* player_sys = subsystems().get<player::player_system>();
    auto* npc_sys = subsystems().get<npc::npc_system>();
    auto* combat = subsystems().get<combat::combat_system>();
    auto* effects = subsystems().get<effect::effect_system>();
    if (!combat)
        return;

    // Resolve the entity standing on this tile
    entity::entity target{};
    bool target_is_player = false;
    if (player_sys)
    {
        if (auto pid = player_sys->get_player_at(obj.map, tile))
        {
            auto* p = player_sys->get_player(*pid);
            if (p && !p->is_dead())
            {
                // Safe-zone tiles protect players (fields cannot exist in safe zones,
                // but their area can reach into one)
                if (auto* world = subsystems().get<world_subsystem>())
                {
                    if (auto* m = world->get_map(obj.map); m && m->is_safe_zone(tile))
                        return;
                }
                // ECS entity ids are the canonical target handles (never player ids)
                target = p->ecs_entity;
                target_is_player = true;
            }
        }
    }
    if (target.id == 0 && npc_sys)
    {
        for (auto npc_entity : npc_sys->get_npcs_in_range(obj.map, tile, 0))
        {
            if (const auto* n = npc_sys->get_npc(npc_entity); n && n->pos.x == tile.x && n->pos.y == tile.y)
            {
                target = npc_entity;
                break;
            }
        }
    }
    if (target.id == 0)
        return;

    int32_t damage = 0;
    auto damage_type = combat::damage_type::magic;

    switch (obj.type)
    {
    case dynamic_object_type::fire:
    case dynamic_object_type::fire3:
        damage = roll(1, 6); // Legacy: 1d6
        damage_type = combat::damage_type::fire;
        break;

    case dynamic_object_type::icestorm:
        damage = roll(3, 3) + 5; // Legacy: 3d3+5
        damage_type = combat::damage_type::ice;
        break;

    case dynamic_object_type::pcloud_begin:
    case dynamic_object_type::pcloud_loop:
    case dynamic_object_type::pcloud_end:
        damage = obj.power < 20 ? roll(1, 6) : roll(1, 8); // Legacy: power-scaled
        damage_type = combat::damage_type::poison;
        break;

    default:
        return;
    }

    combat->deal_damage(target, damage, damage_type, obj.owner);

    // Secondary status effects (players only; apply_effect refuses occupied group slots)
    if (effects && target_is_player)
    {
        if (obj.type == dynamic_object_type::icestorm)
        {
            effect::apply_effect_params params{};
            params.source = obj.owner;
            params.target = target;
            params.group = magic_type::ice;
            params.type = spell_effect_type::freeze;
            params.magnitude = 1;
            params.duration_ms = freeze_duration_ms;
            effects->apply_effect(params);
        }
        else if (obj.type == dynamic_object_type::pcloud_begin || obj.type == dynamic_object_type::pcloud_loop ||
                 obj.type == dynamic_object_type::pcloud_end)
        {
            effect::apply_effect_params params{};
            params.source = obj.owner;
            params.target = target;
            params.group = magic_type::poison;
            params.type = spell_effect_type::poison;
            params.magnitude = std::max(1, obj.power / 10);
            params.duration_ms = poison_duration_ms;
            params.tick_interval_ms = poison_tick_ms;
            effects->apply_effect(params);
        }
    }
}

auto dynamic_object_system::now_ms() const -> int64_t
{
    return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch())
        .count();
}

auto dynamic_object_system::allocate_id() -> uint16_t
{
    // Find a free non-zero id (wraps; 65535 concurrent objects is far above practical use)
    for (int attempts = 0; attempts < 65535; ++attempts)
    {
        uint16_t candidate = next_id_++;
        if (next_id_ == 0)
            next_id_ = 1;
        if (candidate != 0 && !objects_.contains(candidate))
            return candidate;
    }
    return 0;
}

} // namespace hb::world
