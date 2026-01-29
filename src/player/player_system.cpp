// player_system.cpp
// Player management subsystem implementation

#include "player/player_system.h"
#include "core/logger.h"

namespace hb::player {

player_system::player_system() = default;

player_system::~player_system() {
    if (is_initialized()) {
        shutdown();
    }
}

void player_system::initialize() {
    LOG_INFO(general, "Player system initializing...");
    set_initialized(true);
    LOG_INFO(general, "Player system initialized (max_players: {})", config_.max_players);
}

void player_system::shutdown() {
    LOG_INFO(general, "Player system shutting down...");

    players_.clear();
    name_to_id_.clear();
    connection_to_id_.clear();
    session_to_id_.clear();

    set_initialized(false);
    LOG_INFO(general, "Player system shutdown complete");
}

void player_system::update(float delta_time) {
    update_regeneration(delta_time);
    update_hunger(delta_time);
    update_pk_decay(delta_time);
}

void player_system::set_config(const player_system_config& config) {
    config_ = config;
}

auto player_system::create_player(const player_create_info& info) -> result<player_id, std::string> {
    // Check player limit
    if (players_.size() >= config_.max_players) {
        return result<player_id, std::string>::err("Maximum player count reached");
    }

    // Check for duplicate name
    if (name_to_id_.contains(info.name)) {
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
    new_player->stat_points.available = 0;

    // Calculate stats and restore to full
    new_player->recalculate_stats();
    new_player->restore_to_full();

    name_to_id_[info.name] = id;
    players_[id] = std::move(new_player);

    LOG_INFO(general, "Created player '{}' (id={})", info.name, id.value);

    return result<player_id, std::string>::ok(id);
}

void player_system::remove_player(player_id id) {
    auto it = players_.find(id);
    if (it == players_.end()) return;

    auto& p = *it->second;

    // Remove from lookup maps
    name_to_id_.erase(p.name);
    if (p.connection.is_valid()) {
        connection_to_id_.erase(p.connection);
    }
    if (p.session.is_valid()) {
        session_to_id_.erase(p.session);
    }

    LOG_INFO(general, "Removed player '{}' (id={})", p.name, id.value);

    players_.erase(it);
}

auto player_system::get_player(player_id id) -> player* {
    auto it = players_.find(id);
    return it != players_.end() ? it->second.get() : nullptr;
}

auto player_system::get_player(player_id id) const -> const player* {
    auto it = players_.find(id);
    return it != players_.end() ? it->second.get() : nullptr;
}

auto player_system::get_player_by_name(std::string_view name) -> player* {
    auto it = name_to_id_.find(std::string(name));
    return it != name_to_id_.end() ? get_player(it->second) : nullptr;
}

auto player_system::get_player_by_connection(connection_id conn) -> player* {
    auto it = connection_to_id_.find(conn);
    return it != connection_to_id_.end() ? get_player(it->second) : nullptr;
}

auto player_system::get_player_by_session(session_id sess) -> player* {
    auto it = session_to_id_.find(sess);
    return it != session_to_id_.end() ? get_player(it->second) : nullptr;
}

void player_system::bind_connection(player_id id, connection_id conn) {
    auto* p = get_player(id);
    if (!p) return;

    if (p->connection.is_valid()) {
        connection_to_id_.erase(p->connection);
    }
    p->connection = conn;
    connection_to_id_[conn] = id;
}

void player_system::bind_session(player_id id, session_id sess) {
    auto* p = get_player(id);
    if (!p) return;

    if (p->session.is_valid()) {
        session_to_id_.erase(p->session);
    }
    p->session = sess;
    session_to_id_[sess] = id;
}

void player_system::unbind_connection(player_id id) {
    auto* p = get_player(id);
    if (!p) return;

    if (p->connection.is_valid()) {
        connection_to_id_.erase(p->connection);
        p->connection = connection_id{};
    }
}

void player_system::unbind_session(player_id id) {
    auto* p = get_player(id);
    if (!p) return;

    if (p->session.is_valid()) {
        session_to_id_.erase(p->session);
        p->session = session_id{};
    }
}

void player_system::update_stats(player_id id) {
    auto* p = get_player(id);
    if (!p) return;

    p->recalculate_stats();
}

void player_system::apply_damage(player_id id, int32_t damage) {
    auto* p = get_player(id);
    if (!p || p->is_dead()) return;

    p->damage_hp(damage);

    if (p->is_dead()) {
        LOG_DEBUG(general, "Player '{}' died", p->name);
        // Death handling would trigger events here
    }
}

void player_system::apply_heal(player_id id, int32_t amount) {
    auto* p = get_player(id);
    if (!p || p->is_dead()) return;

    p->heal_hp(amount);
}

void player_system::add_experience(player_id id, int64_t amount) {
    auto* p = get_player(id);
    if (!p) return;

    int levels_gained = p->experience.add_experience(amount);
    if (levels_gained > 0) {
        // Award stat points (3 per level typically)
        p->stat_points.award(static_cast<int16_t>(levels_gained * 3));

        // Recalculate stats with new level
        p->recalculate_stats();

        LOG_INFO(general, "Player '{}' leveled up to {}", p->name, p->experience.level);
    }
}

void player_system::add_stat_point(player_id id, int16_t stat_index) {
    auto* p = get_player(id);
    if (!p || p->stat_points.available <= 0) return;
    if (stat_index < 0 || stat_index > 5) return;

    if (!p->stat_points.allocate(1)) return;

    switch (stat_index) {
        case 0: ++p->base.strength; break;
        case 1: ++p->base.dexterity; break;
        case 2: ++p->base.vitality; break;
        case 3: ++p->base.intelligence; break;
        case 4: ++p->base.magic; break;
        case 5: ++p->base.charisma; break;
    }

    p->recalculate_stats();
}

void player_system::add_status(player_id id, player_status status) {
    auto* p = get_player(id);
    if (!p) return;
    p->add_status(status);
}

void player_system::remove_status(player_id id, player_status status) {
    auto* p = get_player(id);
    if (!p) return;
    p->remove_status(status);
}

void player_system::clear_all_status(player_id id) {
    auto* p = get_player(id);
    if (!p) return;
    p->status = player_status::none;
}

void player_system::equip_item(player_id id, equip_slot slot, item_id item, uint16_t dur, uint16_t max_dur) {
    auto* p = get_player(id);
    if (!p) return;

    p->equipment.equip(slot, item, dur, max_dur);
    recalculate_equipment_modifiers(id);
}

auto player_system::unequip_item(player_id id, equip_slot slot) -> equipped_item {
    auto* p = get_player(id);
    if (!p) return equipped_item{};

    auto item = p->equipment.unequip(slot);
    recalculate_equipment_modifiers(id);
    return item;
}

void player_system::recalculate_equipment_modifiers(player_id id) {
    auto* p = get_player(id);
    if (!p) return;

    // Clear equipment modifiers and recalculate from items
    // This would query the item system for each equipped item's stats
    // For now, just recalculate the computed stats
    p->recalculate_stats();
}

void player_system::set_position(player_id id, map_id map, hb::world::position pos, hb::world::direction facing) {
    auto* p = get_player(id);
    if (!p) return;

    p->current_map = map;
    p->pos = pos;
    p->facing = facing;
}

void player_system::set_facing(player_id id, hb::world::direction facing) {
    auto* p = get_player(id);
    if (!p) return;

    p->facing = facing;
}

void player_system::set_target(player_id id, entity::entity target) {
    auto* p = get_player(id);
    if (!p) return;

    p->target = target;
}

void player_system::clear_target(player_id id) {
    auto* p = get_player(id);
    if (!p) return;

    p->target = entity::entity::null();
}

void player_system::update_regeneration(float delta_time) {
    regen_accumulator_ += delta_time * 1000.0f;

    if (regen_accumulator_ < static_cast<float>(config_.regen_tick_ms)) {
        return;
    }

    regen_accumulator_ -= static_cast<float>(config_.regen_tick_ms);

    for (auto& [id, p] : players_) {
        if (p->is_dead()) continue;
        if (p->has_status(player_status::poisoned)) continue;

        p->heal_hp(p->computed.hp_regen);
        p->heal_mp(p->computed.mp_regen);
        p->heal_sp(p->computed.sp_regen);
    }
}

void player_system::update_hunger(float delta_time) {
    hunger_accumulator_ += delta_time * 1000.0f;

    if (hunger_accumulator_ < static_cast<float>(config_.hunger_decay_interval_ms)) {
        return;
    }

    hunger_accumulator_ -= static_cast<float>(config_.hunger_decay_interval_ms);

    for (auto& [id, p] : players_) {
        p->hunger.decay(1);
    }
}

void player_system::update_pk_decay(float delta_time) {
    pk_decay_accumulator_ += delta_time * 1000.0f;

    if (pk_decay_accumulator_ < static_cast<float>(config_.pk_decay_interval_ms)) {
        return;
    }

    pk_decay_accumulator_ -= static_cast<float>(config_.pk_decay_interval_ms);

    for (auto& [id, p] : players_) {
        if (p->pk.points > 0) {
            p->pk.decay_points(1);
        }
    }
}

}  // namespace hb::player
