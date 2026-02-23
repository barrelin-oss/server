// player_system.cpp
// Player management subsystem implementation

#include "player/player_system.h"
#include "core/logger.h"
#include "core/subsystem.h"
#include "item/item_system.h"
#include "item/item_effect.h"
#include "registry/item_registry.h"
#include "effect/effect_system.h"
#include "world/world_subsystem.h"
#include "entity/entity_manager.h"
#include "entity/components/transform.h"
#include "perf/perf_stats.h"

#include <random>

namespace hb::player
{

player_system::player_system() = default;

player_system::~player_system()
{
    if (is_initialized())
    {
        shutdown();
    }
}

void player_system::initialize()
{
    LOG_INFO(general, "Player system initializing...");
    set_initialized(true);
    LOG_INFO(general, "Player system initialized (max_players: {})", config_.max_players);
}

void player_system::shutdown()
{
    LOG_INFO(general, "Player system shutting down...");

    players_.clear();
    name_to_id_.clear();
    connection_to_id_.clear();
    session_to_id_.clear();
    ecs_index_to_id_.clear();

    set_initialized(false);
    LOG_INFO(general, "Player system shutdown complete");
}

void player_system::update(float delta_time)
{
    update_regeneration(delta_time);
    update_hunger(delta_time);
    update_pk_decay(delta_time);
}

void player_system::set_config(const player_system_config& config)
{
    config_ = config;
}

auto player_system::create_player(const player_create_info& info) -> result<player_id, std::string>
{
    // Check player limit
    if (players_.size() >= config_.max_players)
    {
        return result<player_id, std::string>::err("Maximum player count reached");
    }

    // Check for duplicate name
    if (name_to_id_.contains(info.name))
    {
        return result<player_id, std::string>::err("Player name already exists");
    }

    auto id = next_player_id();
    auto new_player = std::make_unique<player>();

    new_player->id = id;
    new_player->name = info.name;
    new_player->account_name = info.account_name;
    new_player->sex = info.sex;
    new_player->profession = info.profession;
    new_player->faction = info.faction;
    new_player->base = info.initial_stats;

    // Set initial level
    new_player->experience.level = 1;
    new_player->stats_pts.available = 0;

    // Calculate stats and restore to full
    new_player->recalculate_stats();
    new_player->restore_to_full();

    // Create entity in entity_manager for unified spatial queries
    auto* entity_mgr = subsystems().get<entity::entity_manager>();
    if (entity_mgr)
    {
        new_player->ecs_entity = entity_mgr->create(entity::entity_type::player);
        // Add transform component (position will be set when player enters world)
        entity_mgr->add_component<entity::transform>(new_player->ecs_entity);
        // Add to ecs index lookup
        ecs_index_to_id_[new_player->ecs_entity.index()] = id;
    }

    name_to_id_[info.name] = id;
    players_[id] = std::move(new_player);

    LOG_INFO(general, "Created player '{}' (id={})", info.name, id.value);

    return result<player_id, std::string>::ok(id);
}

void player_system::remove_player(player_id id)
{
    auto it = players_.find(id);
    if (it == players_.end())
        return;

    auto& p = *it->second;

    // Remove active spell effects
    auto* effect_sys = subsystems().get<effect::effect_system>();
    if (effect_sys && p.ecs_entity.is_valid())
    {
        effect_sys->remove_all_effects(entity::entity{p.id.value});
    }

    // Remove from spatial index
    auto* world_sys = subsystems().get<world::world_subsystem>();
    if (world_sys && p.current_map.is_valid())
    {
        auto* map = world_sys->get_map(p.current_map);
        if (map)
        {
            map->clear_occupant(p.pos);
            map->spatial().remove(entity_id{p.ecs_entity.index()});
        }
    }

    // Destroy entity in entity_manager
    auto* entity_mgr = subsystems().get<entity::entity_manager>();
    if (entity_mgr && p.ecs_entity.is_valid())
    {
        ecs_index_to_id_.erase(p.ecs_entity.index());
        entity_mgr->destroy(p.ecs_entity);
    }

    // Remove from lookup maps
    name_to_id_.erase(p.name);
    if (p.connection.is_valid())
    {
        connection_to_id_.erase(p.connection);
    }
    if (p.session.is_valid())
    {
        session_to_id_.erase(p.session);
    }

    LOG_INFO(general, "Removed player '{}' (id={})", p.name, id.value);

    players_.erase(it);
}

auto player_system::get_player(player_id id) -> player*
{
    auto it = players_.find(id);
    return it != players_.end() ? it->second.get() : nullptr;
}

auto player_system::get_player(player_id id) const -> const player*
{
    auto it = players_.find(id);
    return it != players_.end() ? it->second.get() : nullptr;
}

auto player_system::get_player_by_name(std::string_view name) -> player*
{
    auto it = name_to_id_.find(std::string(name));
    return it != name_to_id_.end() ? get_player(it->second) : nullptr;
}

auto player_system::get_player_by_connection(connection_id conn) -> player*
{
    auto it = connection_to_id_.find(conn);
    return it != connection_to_id_.end() ? get_player(it->second) : nullptr;
}

auto player_system::get_player_by_session(session_id sess) -> player*
{
    auto it = session_to_id_.find(sess);
    return it != session_to_id_.end() ? get_player(it->second) : nullptr;
}

auto player_system::get_player_by_entity(entity::entity eid) -> player*
{
    auto it = ecs_index_to_id_.find(eid.index());
    if (it == ecs_index_to_id_.end())
        return nullptr;
    return get_player(it->second);
}

auto player_system::get_player_by_entity(entity::entity eid) const -> const player*
{
    auto it = ecs_index_to_id_.find(eid.index());
    if (it == ecs_index_to_id_.end())
        return nullptr;
    return get_player(it->second);
}

auto player_system::get_player_id_by_entity(entity::entity eid) const -> std::optional<player_id>
{
    auto it = ecs_index_to_id_.find(eid.index());
    if (it == ecs_index_to_id_.end())
        return std::nullopt;
    return it->second;
}

void player_system::bind_connection(player_id id, connection_id conn)
{
    auto* p = get_player(id);
    if (!p)
        return;

    if (p->connection.is_valid())
    {
        connection_to_id_.erase(p->connection);
    }
    p->connection = conn;
    connection_to_id_[conn] = id;
}

void player_system::bind_session(player_id id, session_id sess)
{
    auto* p = get_player(id);
    if (!p)
        return;

    if (p->session.is_valid())
    {
        session_to_id_.erase(p->session);
    }
    p->session = sess;
    session_to_id_[sess] = id;
}

void player_system::unbind_connection(player_id id)
{
    auto* p = get_player(id);
    if (!p)
        return;

    if (p->connection.is_valid())
    {
        connection_to_id_.erase(p->connection);
        p->connection = connection_id{};
    }
}

void player_system::unbind_session(player_id id)
{
    auto* p = get_player(id);
    if (!p)
        return;

    if (p->session.is_valid())
    {
        session_to_id_.erase(p->session);
        p->session = session_id{};
    }
}

void player_system::update_stats(player_id id)
{
    auto* p = get_player(id);
    if (!p)
        return;

    p->recalculate_stats();
}

void player_system::apply_damage(player_id id, int32_t damage)
{
    auto* p = get_player(id);
    if (!p || p->is_dead())
        return;

    p->damage_hp(damage);

    if (p->is_dead())
    {
        LOG_DEBUG(general, "Player '{}' died", p->name);
        // Death handling would trigger events here
    }
}

void player_system::apply_heal(player_id id, int32_t amount)
{
    auto* p = get_player(id);
    if (!p || p->is_dead())
        return;

    p->heal_hp(amount);
}

void player_system::add_experience(player_id id, int64_t amount)
{
    auto* p = get_player(id);
    if (!p)
        return;

    int levels_gained = p->experience.add_experience(amount);
    if (levels_gained > 0)
    {
        // Award stat points (3 per level typically)
        p->stats_pts.award(static_cast<int16_t>(levels_gained * 3));

        // Recalculate stats with new level
        p->recalculate_stats();

        LOG_INFO(general, "Player '{}' leveled up to {}", p->name, p->experience.level);
    }
}

void player_system::add_stat_point(player_id id, int16_t stat_index)
{
    auto* p = get_player(id);
    if (!p || p->stats_pts.available <= 0)
        return;
    if (stat_index < 0 || stat_index > 5)
        return;

    if (!p->stats_pts.allocate(1))
        return;

    switch (stat_index)
    {
    case 0:
        ++p->base.strength;
        break;
    case 1:
        ++p->base.dexterity;
        break;
    case 2:
        ++p->base.vitality;
        break;
    case 3:
        ++p->base.intelligence;
        break;
    case 4:
        ++p->base.magic;
        break;
    case 5:
        ++p->base.charisma;
        break;
    }

    p->recalculate_stats();
}

void player_system::add_status(player_id id, player_status status)
{
    auto* p = get_player(id);
    if (!p)
        return;
    p->add_status(status);
}

void player_system::remove_status(player_id id, player_status status)
{
    auto* p = get_player(id);
    if (!p)
        return;
    p->remove_status(status);
}

void player_system::clear_all_status(player_id id)
{
    auto* p = get_player(id);
    if (!p)
        return;
    p->status = player_status::none;
}

void player_system::equip_item(player_id id, item_id item, equip_slot slot)
{
    auto* p = get_player(id);
    if (!p)
        return;

    p->equipment.equip(slot, item);
    recalculate_equipment_modifiers(id);
    recalculate_appearance(id);
}

auto player_system::unequip_item(player_id id, equip_slot slot) -> item_id
{
    auto* p = get_player(id);
    if (!p)
        return item_id{};

    auto unequipped = p->equipment.unequip(slot);
    if (!unequipped.has_value())
        return item_id{};

    recalculate_equipment_modifiers(id);
    recalculate_appearance(id);
    return *unequipped;
}

void player_system::rebuild_equipment_cache(player_id id)
{
    // Equipment state is now maintained directly by equip_item/unequip_item
    // and populated from DB during load. This function just recalculates
    // derived stats and appearance from the current equipment_state.
    recalculate_equipment_modifiers(id);
    recalculate_appearance(id);
}

void player_system::recalculate_equipment_modifiers(player_id id)
{
    auto* p = get_player(id);
    if (!p)
        return;

    // Clear equipment modifiers only - preserve effect modifiers
    p->equipment_modifiers = stat_modifiers{};

    auto* item_sys = subsystems().get<item::item_system>();
    if (!item_sys)
    {
        // No item system available, just recalculate with zeroed equipment modifiers
        p->recalculate_stats();
        return;
    }

    // Also scan for special ability items
    auto* item_reg = subsystems().get<item_registry>();
    item::special_ability_type found_ability = item::special_ability_type::none;

    // Iterate through all equipment slots and apply their effects
    for (auto [slot, eq_item_id] : p->equipment.all_equipped())
    {
        // Get the item from the item system
        auto* eq_item = item_sys->get_item(eq_item_id);
        if (!eq_item)
            continue;

        // Skip broken items
        if (eq_item->is_broken())
            continue;

        // Apply item base stats and effects to equipment modifiers only
        item::apply_item_base_stats(*eq_item, p->equipment_modifiers);

        // Apply per-instance attribute bonuses (upgrade level, enchantments)
        item::apply_item_attribute(*eq_item, p->equipment_modifiers);

        // Check for special ability (first one found wins)
        if (found_ability == item::special_ability_type::none && item_reg)
        {
            auto* tmpl = item_reg->get(eq_item->template_id);
            if (tmpl && tmpl->special_effect != 0)
            {
                found_ability = static_cast<item::special_ability_type>(tmpl->special_effect);
            }
        }
    }

    // Update special ability state
    if (found_ability != p->special_ability.type)
    {
        p->special_ability.set_ability(found_ability);
    }

    LOG_DEBUG(general, "Recalculated equipment modifiers for player '{}'", p->name);

    // Now recalculate computed stats (combines equipment + effect modifiers)
    p->recalculate_stats();
}

void player_system::recalculate_appearance(player_id id)
{
    auto* p = get_player(id);
    if (!p)
        return;

    // Reset appearance state
    p->appearance = appearance_state{};

    auto* item_sys = subsystems().get<item::item_system>();
    auto* item_reg = subsystems().get<item_registry>();
    if (!item_sys || !item_reg)
        return;

    // Helper: extract visual from an equipped slot
    auto extract_visual = [&](equip_slot slot) -> equipment_visual
    {
        auto equipped = p->equipment.get_equipped(slot);
        if (!equipped.has_value())
            return {};

        auto* inst = item_sys->get_item(*equipped);
        if (!inst)
            return {};

        auto* tmpl = item_reg->get(inst->template_id);
        if (!tmpl)
            return {};

        return equipment_visual{tmpl->appr_value, tmpl->item_color};
    };

    p->appearance.weapon = extract_visual(equip_slot::weapon);
    p->appearance.shield = extract_visual(equip_slot::shield);
    p->appearance.body = extract_visual(equip_slot::body);
    p->appearance.pants = extract_visual(equip_slot::pants);
    p->appearance.head = extract_visual(equip_slot::head);
    p->appearance.arms = extract_visual(equip_slot::arms);
    p->appearance.boots = extract_visual(equip_slot::boots);
    p->appearance.cape = extract_visual(equip_slot::cape);

    // Weapon speed from template
    auto weapon_equipped = p->equipment.get_equipped(equip_slot::weapon);
    if (weapon_equipped.has_value())
    {
        if (auto* inst = item_sys->get_item(*weapon_equipped))
        {
            if (auto* tmpl = item_reg->get(inst->template_id))
            {
                p->appearance.weapon_speed = tmpl->speed;
            }
        }
    }
}

void player_system::set_effect_modifiers(player_id id, const stat_modifiers& mods)
{
    auto* p = get_player(id);
    if (!p)
        return;

    p->effect_modifiers = mods;
    p->recalculate_stats();
}

void player_system::set_effect_status(player_id id, player_status effect_flags)
{
    auto* p = get_player(id);
    if (!p)
        return;

    // Mask of status flags managed by the effect system.
    // These get cleared and re-set each update. Non-effect flags (like gm_invisible) are preserved.
    constexpr auto effect_managed_mask =
        player_status::poisoned | player_status::paralyzed | player_status::invisible | player_status::frozen |
        player_status::berserk | player_status::protection | player_status::defense_up | player_status::attack_up |
        player_status::magic_up | player_status::haste | player_status::slow | player_status::stunned |
        player_status::silenced | player_status::invincible | player_status::cursed;

    p->status = (p->status & ~effect_managed_mask) | effect_flags;
}

void player_system::set_position(player_id id, map_id map, hb::world::position pos, hb::world::direction facing)
{
    auto* p = get_player(id);
    if (!p)
        return;

    auto* world_sys = subsystems().get<world::world_subsystem>();

    // Compute spatial ID from ECS entity
    entity_id spatial_id = p->ecs_entity.is_valid() ? entity_id{p->ecs_entity.index()} : entity_id{id.value};

    // If changing maps, remove from old map's spatial index and occupant grid
    if (world_sys && p->current_map != map && p->current_map.value != 0)
    {
        auto* old_map = world_sys->get_map(p->current_map);
        if (old_map)
        {
            old_map->clear_occupant(p->pos);
            old_map->spatial().remove(spatial_id);
        }
    }

    p->current_map = map;
    p->pos = pos;
    p->facing = facing;

    // Add to new map's spatial index and set occupant
    if (world_sys)
    {
        auto* new_map = world_sys->get_map(map);
        if (new_map)
        {
            new_map->set_occupant(pos, spatial_id, world::owner_type::player);
            new_map->spatial().add(spatial_id, pos);
        }
    }

    // Update transform component
    auto* entity_mgr = subsystems().get<entity::entity_manager>();
    if (entity_mgr && p->ecs_entity.is_valid())
    {
        if (auto* t = entity_mgr->get_component<entity::transform>(p->ecs_entity))
        {
            t->map = map;
            t->pos = pos;
            t->facing = facing;
        }
    }
}

void player_system::set_facing(player_id id, hb::world::direction facing)
{
    auto* p = get_player(id);
    if (!p)
        return;

    p->facing = facing;
}

void player_system::set_target(player_id id, entity::entity target)
{
    auto* p = get_player(id);
    if (!p)
        return;

    p->target = target;
}

void player_system::clear_target(player_id id)
{
    auto* p = get_player(id);
    if (!p)
        return;

    p->target = entity::entity::null();
}

void player_system::update_regeneration(float delta_time)
{
    regen_accumulator_ += delta_time * 1000.0f;

    if (regen_accumulator_ < static_cast<float>(config_.regen_tick_ms))
    {
        return;
    }

    regen_accumulator_ -= static_cast<float>(config_.regen_tick_ms);

    // Scaling factor: our_tick / legacy_interval (e.g. 5000/15000 = 0.333)
    const float tick_ms = static_cast<float>(config_.regen_tick_ms);
    const float hp_scale = tick_ms / static_cast<float>(config_.legacy_hp_interval_ms);
    const float mp_scale = tick_ms / static_cast<float>(config_.legacy_mp_interval_ms);
    const float sp_scale = tick_ms / static_cast<float>(config_.legacy_sp_interval_ms);

    thread_local std::mt19937 rng{std::random_device{}()};

    for (auto& [id, p] : players_)
    {
        if (p->is_dead())
            continue;
        if (p->has_status(player_status::poisoned))
            continue;

        // HUNGER: Block regeneration entirely if starving (hunger <= 0)
        if (p->hunger.is_starving())
            continue;

        // HUNGER: Delay regen when hungry (hunger < 30)
        // Legacy adds (30 - hunger) * 1000 ms to each resource's interval.
        // We approximate by skipping ticks proportionally.
        if (p->hunger.is_hungry())
        {
            float extra_delay_ms = static_cast<float>(30 - p->hunger.level) * 1000.0f;
            // Probability of skipping this tick based on delay ratio
            // e.g. hunger=20: extra 10s delay on a 15s base HP timer = skip ~40% of ticks
            // We use HP timer (15s) as baseline since all resources share this tick
            float skip_chance = extra_delay_ms / (15000.0f + extra_delay_ms);
            std::uniform_real_distribution<float> skip_dist(0.0f, 1.0f);
            if (skip_dist(rng) < skip_chance)
            {
                continue;
            }
        }

        auto old_hp = p->hp;
        auto old_mp = p->mp;
        auto old_sp = p->sp;

        // === HP Regen ===
        // Legacy: 1d(VIT), min VIT/2, + hp_stock (food), + m_iAddHP % bonus, every 15s
        // hp_stock is spread across ticks proportionally (legacy consumed it all at once on a 15s tick)
        if (p->hp < p->computed.max_hp || p->hp_stock > 0)
        {
            int32_t vit = std::max(1, static_cast<int>(p->computed.vitality));
            std::uniform_int_distribution<int32_t> hp_dist(1, vit);
            int32_t hp_roll = hp_dist(rng);
            if (hp_roll < vit / 2)
            {
                hp_roll = vit / 2;
            }

            // Consume hp_stock at a fixed per-tick rate (set when food is eaten)
            int32_t stock_portion = 0;
            if (p->hp_stock > 0)
            {
                stock_portion = std::min(p->hp_stock, p->hp_stock_per_tick);
                p->hp_stock -= stock_portion;
                if (p->hp_stock <= 0)
                {
                    p->hp_stock_per_tick = 0;
                }
            }

            int32_t hp_total = hp_roll + stock_portion;

            // Equipment percentage bonus (legacy: m_iAddHP is a %)
            if (p->computed.hp_regen != 0)
            {
                hp_total += static_cast<int32_t>(
                    static_cast<double>(hp_total) * static_cast<double>(p->computed.hp_regen) / 100.0);
            }

            float hp_amount = static_cast<float>(hp_total) * hp_scale;
            p->hp_regen_fraction += hp_amount;
            int32_t hp_heal = static_cast<int32_t>(p->hp_regen_fraction);
            if (hp_heal > 0)
            {
                p->hp_regen_fraction -= static_cast<float>(hp_heal);
                p->heal_hp(hp_heal);
            }
        }

        // === MP Regen ===
        // Legacy: 1d(MAG) + m_iAddMP % bonus, every 20s
        if (p->mp < p->computed.max_mp)
        {
            int32_t mag = std::max(1, static_cast<int>(p->computed.magic));
            std::uniform_int_distribution<int32_t> mp_dist(1, mag);
            int32_t mp_total = mp_dist(rng);

            // Equipment percentage bonus
            if (p->computed.mp_regen != 0)
            {
                mp_total += static_cast<int32_t>(
                    static_cast<double>(mp_total) * static_cast<double>(p->computed.mp_regen) / 100.0);
            }

            float mp_amount = static_cast<float>(mp_total) * mp_scale;
            p->mp_regen_fraction += mp_amount;
            int32_t mp_heal = static_cast<int32_t>(p->mp_regen_fraction);
            if (mp_heal > 0)
            {
                p->mp_regen_fraction -= static_cast<float>(mp_heal);
                p->heal_mp(mp_heal);
            }
        }

        // === SP Regen ===
        // Legacy: 1d(VIT/3) + m_iAddSP % bonus + level bonus, every 10s
        if (p->sp < p->computed.max_sp)
        {
            int32_t vit_third = std::max(1, static_cast<int>(p->computed.vitality / 3));
            std::uniform_int_distribution<int32_t> sp_dist(1, vit_third);
            int32_t sp_total = sp_dist(rng);

            // Equipment percentage bonus
            if (p->computed.sp_regen != 0)
            {
                sp_total += static_cast<int32_t>(
                    static_cast<double>(sp_total) * static_cast<double>(p->computed.sp_regen) / 100.0);
            }

            // Level-based bonus (legacy: low-level players regen SP faster)
            uint8_t level = p->experience.level;
            if (level <= 20)
                sp_total += 15;
            else if (level <= 40)
                sp_total += 10;
            else if (level <= 60)
                sp_total += 5;

            float sp_amount = static_cast<float>(sp_total) * sp_scale;
            p->sp_regen_fraction += sp_amount;
            int32_t sp_heal = static_cast<int32_t>(p->sp_regen_fraction);
            if (sp_heal > 0)
            {
                p->sp_regen_fraction -= static_cast<float>(sp_heal);
                p->heal_sp(sp_heal);
            }
        }

        if (regen_callback_ && (p->hp != old_hp || p->mp != old_mp || p->sp != old_sp))
        {
            regen_callback_(id, p->hp, p->mp, p->sp);
        }
    }
}

void player_system::update_hunger(float delta_time)
{
    hunger_accumulator_ += delta_time * 1000.0f;

    if (hunger_accumulator_ < static_cast<float>(config_.hunger_decay_interval_ms))
    {
        return;
    }

    hunger_accumulator_ -= static_cast<float>(config_.hunger_decay_interval_ms);

    for (auto& [id, p] : players_)
    {
        int8_t old_level = p->hunger.level;
        p->hunger.decay(1);

        if (hunger_callback_ && old_level != p->hunger.level)
        {
            hunger_callback_(id, old_level, p->hunger.level);
        }
    }
}

void player_system::restore_hunger(player_id id, int8_t amount)
{
    auto* p = get_player(id);
    if (!p)
        return;

    int8_t old_level = p->hunger.level;
    p->hunger.consume(amount);

    if (hunger_callback_ && old_level != p->hunger.level)
    {
        hunger_callback_(id, old_level, p->hunger.level);
    }
}

void player_system::add_hp_stock(player_id id, int32_t amount)
{
    auto* p = get_player(id);
    if (!p || amount <= 0)
        return;

    p->hp_stock = std::clamp(p->hp_stock + amount, 0, player::max_hp_stock);
    int32_t ticks = std::max(1, config_.legacy_hp_interval_ms / config_.regen_tick_ms);
    p->hp_stock_per_tick = (p->hp_stock + ticks - 1) / ticks;
}

void player_system::update_pk_decay(float delta_time)
{
    pk_decay_accumulator_ += delta_time * 1000.0f;

    if (pk_decay_accumulator_ < static_cast<float>(config_.pk_decay_interval_ms))
    {
        return;
    }

    pk_decay_accumulator_ -= static_cast<float>(config_.pk_decay_interval_ms);

    for (auto& [id, p] : players_)
    {
        if (p->pk.points > 0)
        {
            p->pk.decay_points(1);
        }
    }
}

auto player_system::try_move(player_id id, hb::world::position target_pos, hb::world::direction facing) -> move_info
{
    move_info info;

    auto* p = get_player(id);
    if (!p)
    {
        info.result = move_result::invalid_player;
        return info;
    }

    // Check if player is dead
    if (p->is_dead())
    {
        info.result = move_result::blocked_dead;
        return info;
    }

    // Check for movement-blocking status effects
    if (p->has_status(player_status::paralyzed) || p->has_status(player_status::frozen) ||
        p->has_status(player_status::stunned))
    {
        info.result = move_result::blocked_status;
        return info;
    }

    auto* world_sys = subsystems().get<world::world_subsystem>();
    if (!world_sys)
    {
        // No world system, just update position
        p->pos = target_pos;
        p->facing = facing;
        info.result = move_result::success;
        return info;
    }

    auto* map = world_sys->get_map(p->current_map);
    if (!map)
    {
        info.result = move_result::invalid_map;
        return info;
    }

    // Check bounds
    if (!map->is_valid_position(target_pos))
    {
        info.result = move_result::blocked_out_of_bounds;
        return info;
    }

    // Check if tile is walkable
    if (!map->is_walkable(target_pos))
    {
        info.result = move_result::blocked_terrain;
        return info;
    }

    // Check if staying in place (no movement needed)
    if (target_pos == p->pos)
    {
        info.result = move_result::success;
        return info;
    }

    // Check if tile is occupied
    auto occupant = map->get_occupant(target_pos);
    if (occupant.has_value() && occupant.value().value != 0)
    {
        info.result = move_result::blocked_occupied;
        return info;
    }

    // Clear old occupant
    map->clear_occupant(p->pos);

    // Update player position
    hb::world::position old_pos = p->pos;
    p->pos = target_pos;
    p->facing = facing;

    // Use ecs_entity index for spatial tracking (unified with NPCs)
    entity_id spatial_id = p->ecs_entity.is_valid() ? entity_id{p->ecs_entity.index()} : entity_id{id.value};

    // Set new occupant
    map->set_occupant(target_pos, spatial_id, world::owner_type::player);

    // Update spatial index
    map->spatial().update(spatial_id, target_pos);

    // Update transform component
    auto* entity_mgr = subsystems().get<entity::entity_manager>();
    if (entity_mgr && p->ecs_entity.is_valid())
    {
        if (auto* t = entity_mgr->get_component<entity::transform>(p->ecs_entity))
        {
            t->pos = target_pos;
            t->facing = facing;
        }
    }

    // Check for teleport
    if (map->is_teleport(target_pos))
    {
        auto teleport = map->get_teleport_dest(target_pos);
        if (teleport.has_value())
        {
            info.result = move_result::teleport;
            info.teleport_dest_map = teleport->dest_map;
            info.teleport_dest_pos = {teleport->dest_x, teleport->dest_y};
            info.teleport_dest_dir = teleport->dest_dir;

            LOG_DEBUG(general,
                      "Player '{}' triggered teleport to {} at ({},{})",
                      p->name,
                      info.teleport_dest_map,
                      info.teleport_dest_pos.x,
                      info.teleport_dest_pos.y);
        }
    }

    if (info.result != move_result::teleport)
    {
        info.result = move_result::success;
    }

    LOG_TRACE(general,
              "Player '{}' moved from ({},{}) to ({},{})",
              p->name,
              old_pos.x,
              old_pos.y,
              target_pos.x,
              target_pos.y);

    return info;
}

auto player_system::can_move_to(player_id id, hb::world::position target_pos) const -> move_result
{
    auto* p = get_player(id);
    if (!p)
    {
        return move_result::invalid_player;
    }

    if (p->is_dead())
    {
        return move_result::blocked_dead;
    }

    if (p->has_status(player_status::paralyzed) || p->has_status(player_status::frozen) ||
        p->has_status(player_status::stunned))
    {
        return move_result::blocked_status;
    }

    auto* world_sys = subsystems().get<world::world_subsystem>();
    if (!world_sys)
    {
        return move_result::success; // No world system, allow move
    }

    auto* map = world_sys->get_map(p->current_map);
    if (!map)
    {
        return move_result::invalid_map;
    }

    if (!map->is_valid_position(target_pos))
    {
        return move_result::blocked_out_of_bounds;
    }

    if (!map->is_walkable(target_pos))
    {
        return move_result::blocked_terrain;
    }

    // Staying in place is always allowed
    if (target_pos == p->pos)
    {
        return move_result::success;
    }

    auto occupant = map->get_occupant(target_pos);
    if (occupant.has_value() && occupant.value().value != 0)
    {
        return move_result::blocked_occupied;
    }

    return move_result::success;
}

auto player_system::execute_teleport(player_id id,
                                     const std::string& dest_map_name,
                                     hb::world::position dest_pos,
                                     hb::world::direction dest_dir) -> teleport_result
{
    teleport_result result;

    auto* p = get_player(id);
    if (!p)
    {
        result.error = "Player not found";
        return result;
    }

    auto* world_sys = subsystems().get<world::world_subsystem>();
    if (!world_sys)
    {
        result.error = "World system unavailable";
        return result;
    }

    // Get destination map by name
    auto* dest_map = world_sys->get_map_by_name(dest_map_name);
    if (!dest_map)
    {
        result.error = "Destination map not found: " + dest_map_name;
        return result;
    }

    // Resolve (-1,-1) to a random initial point on the map
    if (dest_pos.x == -1 || dest_pos.y == -1)
    {
        auto initial = dest_map->get_random_initial_point();
        if (!initial)
        {
            result.error = "No initial spawn point on map: " + dest_map_name;
            return result;
        }
        dest_pos = *initial;
    }

    // Validate destination position is walkable
    if (!dest_map->is_walkable(dest_pos))
    {
        result.error = "Destination position is not walkable";
        return result;
    }

    // Store old position for return value and spatial index cleanup
    result.old_map = p->current_map;
    result.old_pos = p->pos;

    // Use ecs_entity index for spatial tracking (unified with NPCs)
    entity_id spatial_id = p->ecs_entity.is_valid() ? entity_id{p->ecs_entity.index()} : entity_id{id.value};

    // Get old map for cleanup
    auto* old_map = world_sys->get_map(p->current_map);

    // Clear occupant at old position
    if (old_map)
    {
        old_map->clear_occupant(p->pos);
        old_map->spatial().remove(spatial_id);
    }

    // Update player's position
    p->current_map = dest_map->id();
    p->pos = dest_pos;
    p->facing = dest_dir;

    // Set occupant at new position
    dest_map->set_occupant(dest_pos, spatial_id, world::owner_type::player);

    // Add to new map's spatial index
    dest_map->spatial().add(spatial_id, dest_pos);

    // Update transform component
    auto* entity_mgr = subsystems().get<entity::entity_manager>();
    if (entity_mgr && p->ecs_entity.is_valid())
    {
        if (auto* t = entity_mgr->get_component<entity::transform>(p->ecs_entity))
        {
            t->map = dest_map->id();
            t->pos = dest_pos;
            t->facing = dest_dir;
        }
    }

    result.success = true;
    result.new_map = dest_map->id();
    result.new_pos = dest_pos;

    LOG_DEBUG(general,
              "Player '{}' teleported from map {} ({},{}) to {} ({},{})",
              p->name,
              result.old_map.value,
              result.old_pos.x,
              result.old_pos.y,
              dest_map_name,
              dest_pos.x,
              dest_pos.y);

    return result;
}

auto player_system::get_players_who_can_see(map_id map, const hb::world::position& pos) const -> std::vector<player_id>
{
    auto* perf = subsystems().get<perf::perf_stats_system>();
    PERF_TIMER(perf, perf::metric_category::spatial_query_visibility);

    std::vector<player_id> result;

    // Use max possible visibility range to get candidates from spatial index
    constexpr int max_visibility = 100; // Support high resolution displays
    auto candidates = get_players_on_map_in_range(map, pos, max_visibility);

    for (auto pid : candidates)
    {
        auto* p = get_player(pid);
        if (!p)
            continue;

        // Admin sees_all bypasses distance check; normal players use rectangular visibility
        if (p->sees_all || (std::abs(pos.x - p->pos.x) <= p->visibility_radius_x &&
                            std::abs(pos.y - p->pos.y) <= p->visibility_radius_y))
        {
            result.push_back(pid);
        }
    }

    // Find sees_all admins on this map who are beyond the spatial query range
    auto far_admins = find_players_if(
        [&](const player& p)
        { return p.sees_all && p.current_map == map && pos.chebyshev_distance(p.pos) > max_visibility; });
    result.insert(result.end(), far_admins.begin(), far_admins.end());

    return result;
}

auto player_system::get_players_on_map_in_range(map_id map,
                                                const hb::world::position& center,
                                                int radius) const -> std::vector<player_id>
{
    auto* perf = subsystems().get<perf::perf_stats_system>();
    PERF_TIMER(perf, perf::metric_category::spatial_query_range);

    std::vector<player_id> result;

    auto* world_sys = subsystems().get<world::world_subsystem>();
    if (!world_sys)
    {
        return result;
    }

    auto* m = world_sys->get_map(map);
    if (!m)
    {
        return result;
    }

    // Get entities in range from spatial index
    auto entities = m->get_entities_in_range(center, radius);

    // Filter for players using ecs_index_to_id_ lookup (O(1) per entity)
    for (auto eid : entities)
    {
        auto it = ecs_index_to_id_.find(eid.value);
        if (it != ecs_index_to_id_.end())
        {
            result.push_back(it->second);
        }
    }

    return result;
}

auto player_system::get_players_in_range(player_id id, int radius) const -> std::vector<player_id>
{
    std::vector<player_id> result;

    auto* p = get_player(id);
    if (!p)
    {
        return result;
    }

    auto* world_sys = subsystems().get<world::world_subsystem>();
    if (!world_sys)
    {
        return result;
    }

    auto* map = world_sys->get_map(p->current_map);
    if (!map)
    {
        return result;
    }

    // Get entities in range from spatial index
    auto entities = map->get_entities_in_range(p->pos, radius);

    // Filter for players using ecs_index_to_id_ lookup (O(1) per entity)
    for (auto eid : entities)
    {
        auto it = ecs_index_to_id_.find(eid.value);
        if (it != ecs_index_to_id_.end() && it->second != id)
        {
            result.push_back(it->second);
        }
    }

    return result;
}

auto player_system::get_player_at(map_id map, hb::world::position pos) const -> std::optional<player_id>
{
    auto* world_sys = subsystems().get<world::world_subsystem>();
    if (!world_sys)
    {
        return std::nullopt;
    }

    auto* m = world_sys->get_map(map);
    if (!m)
    {
        return std::nullopt;
    }

    auto occupant = m->get_occupant(pos);
    if (!occupant.has_value())
    {
        return std::nullopt;
    }

    auto occupant_type = m->get_occupant_type(pos);
    if (occupant_type != world::owner_type::player)
    {
        return std::nullopt;
    }

    // Occupant value is the ecs_entity index, not the player_id
    auto it = ecs_index_to_id_.find(occupant.value().value);
    if (it != ecs_index_to_id_.end())
    {
        return it->second;
    }

    return std::nullopt;
}

} // namespace hb::player
