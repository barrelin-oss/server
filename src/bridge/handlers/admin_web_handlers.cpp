// admin_web_handlers.cpp
// Admin web tool handler implementations

#include "bridge/handlers/admin_web_handlers.h"
#include "core/logger.h"
#include "network/websocket_server.h"
#include "auth/auth_system.h"
#include "player/player_system.h"
#include "player/player.h"
#include "world/world_subsystem.h"
#include "world/map.h"
#include "inventory/inventory_system.h"
#include "inventory/inventory.h"
#include "admin/admin_system.h"
#include "npc/npc_system.h"
#include "npc/npc.h"
#include "item/item_system.h"
#include "social/social_system.h"
#include "social/guild.h"
#include "combat/combat_system.h"
#include "database/database_system.h"
#include "scheduler/scheduler.h"
#include "registry/npc_registry.h"
#include "registry/item_registry.h"

namespace hb::bridge {

admin_web_handlers::admin_web_handlers() = default;
admin_web_handlers::~admin_web_handlers() = default;

void admin_web_handlers::initialize(
    network::websocket_server* ws_server,
    auth::auth_system* auth,
    player::player_system* players,
    world::world_subsystem* world,
    inventory::inventory_system* inventory,
    admin::admin_system* admin,
    npc::npc_system* npc,
    item::item_system* item,
    social::social_system* social,
    combat::combat_system* combat,
    database::database_system* db,
    scheduler* sched,
    npc_registry* npc_reg,
    item_registry* item_reg)
{
    ws_server_ = ws_server;
    auth_ = auth;
    players_ = players;
    world_ = world;
    inventory_ = inventory;
    admin_ = admin;
    npc_ = npc;
    item_ = item;
    social_ = social;
    combat_ = combat;
    db_ = db;
    scheduler_ = sched;
    npc_registry_ = npc_reg;
    item_registry_ = item_reg;
}

void admin_web_handlers::handle_message(connection_id conn_id, const network::json_message& msg)
{
    using mt = network::json_message_type;
    switch (msg.type) {
        case mt::admin_server_stats_request:    handle_server_stats(conn_id, msg); break;
        case mt::admin_list_players_request:    handle_list_players(conn_id, msg); break;
        case mt::admin_get_player_request:      handle_get_player(conn_id, msg); break;
        case mt::admin_kick_player_request:     handle_kick_player(conn_id, msg); break;
        case mt::admin_ban_player_request:      handle_ban_player(conn_id, msg); break;
        case mt::admin_teleport_player_request: handle_teleport_player(conn_id, msg); break;
        case mt::admin_modify_player_request:   handle_modify_player(conn_id, msg); break;
        case mt::admin_list_maps_request:       handle_list_maps(conn_id, msg); break;
        case mt::admin_get_map_request:         handle_get_map(conn_id, msg); break;
        case mt::admin_spawn_npc_request:       handle_spawn_npc(conn_id, msg); break;
        case mt::admin_kill_npc_request:        handle_kill_npc(conn_id, msg); break;
        case mt::admin_get_inventory_request:   handle_get_inventory(conn_id, msg); break;
        case mt::admin_give_item_request:       handle_give_item(conn_id, msg); break;
        case mt::admin_remove_item_request:     handle_remove_item(conn_id, msg); break;
        case mt::admin_list_guilds_request:     handle_list_guilds(conn_id, msg); break;
        case mt::admin_get_guild_request:       handle_get_guild(conn_id, msg); break;
        case mt::admin_get_account_request:     handle_get_account(conn_id, msg); break;
        case mt::admin_unban_player_request:    handle_unban_player(conn_id, msg); break;
        case mt::admin_subscribe_map_request:   handle_subscribe_map(conn_id, msg); break;
        case mt::admin_subscribe_player_request: handle_subscribe_player(conn_id, msg); break;
        case mt::admin_unsubscribe_request:     handle_unsubscribe(conn_id, msg); break;
        default:
            send_error(conn_id, msg.seq, "unknown_admin_message", "Unknown admin message type");
            break;
    }
}

void admin_web_handlers::send_error(connection_id conn_id, uint32_t seq,
    std::string_view error_code, std::string_view message)
{
    auto msg = network::make_error_response(seq, error_code, message);
    ws_server_->send(conn_id, msg);
}

auto admin_web_handlers::require_admin(connection_id conn_id, uint32_t seq, uint8_t min_level)
    -> network::ws_connection*
{
    auto* conn = ws_server_->get_connection(conn_id);
    if (!conn) return nullptr;

    if (conn->state() != network::ws_connection_state::admin_dashboard) {
        send_error(conn_id, seq, "not_admin", "Not in admin dashboard mode");
        return nullptr;
    }

    if (conn->admin_level() < min_level) {
        send_error(conn_id, seq, "insufficient_privileges", "Insufficient admin level");
        return nullptr;
    }

    return conn;
}

// === Push notifications ===

void admin_web_handlers::notify_player_connected(const std::string& name,
    int16_t level, const std::string& map_name)
{
    auto msg = network::make_admin_player_connected(name, level, map_name);
    for (auto cid : ws_server_->get_all_admin_connections()) {
        ws_server_->send(cid, msg);
    }
}

void admin_web_handlers::notify_player_disconnected(const std::string& name)
{
    auto msg = network::make_admin_player_disconnected(name);
    for (auto cid : ws_server_->get_all_admin_connections()) {
        ws_server_->send(cid, msg);
    }
}

void admin_web_handlers::notify_chat_message(const std::string& channel,
    const std::string& sender, const std::string& content)
{
    auto msg = network::make_admin_chat_log(channel, sender, content);
    for (auto cid : ws_server_->get_all_admin_connections()) {
        ws_server_->send(cid, msg);
    }
}

// === Server Stats ===

void admin_web_handlers::handle_server_stats(connection_id conn_id, const network::json_message& msg)
{
    if (!require_admin(conn_id, msg.seq)) return;

    nlohmann::json data;
    data["player_count"] = players_ ? players_->player_count() : 0;
    data["npc_count"] = npc_ ? npc_->npc_count() : 0;
    data["map_count"] = world_ ? world_->map_count() : 0;
    data["guild_count"] = social_ ? social_->guild_count() : 0;
    data["party_count"] = social_ ? social_->party_count() : 0;
    data["connection_count"] = ws_server_->connection_count();

    if (db_) {
        data["db_available_connections"] = db_->available_connections();
        data["db_total_connections"] = db_->total_connections();
        data["db_connected"] = db_->is_connected();
    }

    if (scheduler_) {
        auto& clock = scheduler_->game_time();
        data["game_hour"] = clock.hour();
        data["game_minute"] = clock.minute();
        data["game_is_day"] = clock.is_day();
        data["game_day"] = clock.day();
    }

    ws_server_->send(conn_id, network::make_admin_response(
        network::json_message_type::admin_server_stats_response,
        msg.seq, true, data));
}

// === Player Management ===

void admin_web_handlers::handle_list_players(connection_id conn_id, const network::json_message& msg)
{
    if (!require_admin(conn_id, msg.seq)) return;

    nlohmann::json players_arr = nlohmann::json::array();
    if (players_) {
        players_->for_each_player([&](player_id pid, const player::player& plr) {
            players_arr.push_back({
                {"id", pid.value},
                {"name", plr.name},
                {"level", plr.experience.level},
                {"map", world_ ? [&]() -> std::string {
                    auto* m = world_->get_map(plr.current_map);
                    return m ? std::string(m->name()) : "unknown";
                }() : "unknown"},
                {"x", plr.pos.x},
                {"y", plr.pos.y},
                {"hp", plr.hp},
                {"max_hp", plr.computed.max_hp},
                {"faction", static_cast<int>(plr.faction)},
                {"guild", plr.guild_name},
                {"pk_count", plr.pk.count}
            });
        });
    }

    nlohmann::json data;
    data["players"] = players_arr;
    data["count"] = players_arr.size();

    ws_server_->send(conn_id, network::make_admin_response(
        network::json_message_type::admin_list_players_response,
        msg.seq, true, data));
}

void admin_web_handlers::handle_get_player(connection_id conn_id, const network::json_message& msg)
{
    if (!require_admin(conn_id, msg.seq)) return;

    auto parse = network::admin_get_player_request_data::from_json(msg.data);
    if (parse.is_err()) {
        send_error(conn_id, msg.seq, "parse_error", parse.error());
        return;
    }
    auto& req = parse.value();

    player::player* plr = nullptr;
    if (!req.player_name.empty()) {
        plr = players_->get_player_by_name(req.player_name);
    } else {
        plr = players_->get_player(player_id{req.player_id});
    }

    if (!plr) {
        ws_server_->send(conn_id, network::make_admin_response(
            network::json_message_type::admin_get_player_response,
            msg.seq, false, {}, "Player not found"));
        return;
    }

    std::string map_name = "unknown";
    if (world_) {
        auto* m = world_->get_map(plr->current_map);
        if (m) map_name = std::string(m->name());
    }

    nlohmann::json data;
    data["id"] = plr->id.value;
    data["character_id"] = plr->character_id.value;
    data["name"] = plr->name;
    data["account_name"] = plr->account_name;
    data["level"] = plr->experience.level;
    data["experience"] = plr->experience.experience;
    data["class"] = static_cast<int>(plr->profession);
    data["gender"] = static_cast<int>(plr->sex);
    data["faction"] = static_cast<int>(plr->faction);
    data["map"] = map_name;
    data["x"] = plr->pos.x;
    data["y"] = plr->pos.y;
    data["hp"] = plr->hp;
    data["max_hp"] = plr->computed.max_hp;
    data["mp"] = plr->mp;
    data["max_mp"] = plr->computed.max_mp;
    data["sp"] = plr->sp;
    data["max_sp"] = plr->computed.max_sp;
    data["str"] = plr->base.strength;
    data["dex"] = plr->base.dexterity;
    data["vit"] = plr->base.vitality;
    data["int"] = plr->base.intelligence;
    data["mag"] = plr->base.magic;
    data["cha"] = plr->base.charisma;
    data["guild"] = plr->guild_name;
    data["guild_rank"] = plr->guild_rank;
    data["pk_count"] = plr->pk.count;
    data["pk_points"] = plr->pk.points;
    data["hunger"] = plr->hunger.level;
    data["admin_level"] = static_cast<int>(plr->admin);
    data["is_alive"] = plr->is_alive();

    auto owner_id = entity_id(plr->id.value);
    if (inventory_) {
        data["gold"] = inventory_->get_gold(owner_id);
    }

    ws_server_->send(conn_id, network::make_admin_response(
        network::json_message_type::admin_get_player_response,
        msg.seq, true, data));
}

void admin_web_handlers::handle_kick_player(connection_id conn_id, const network::json_message& msg)
{
    if (!require_admin(conn_id, msg.seq)) return;

    auto parse = network::admin_kick_player_request_data::from_json(msg.data);
    if (parse.is_err()) {
        send_error(conn_id, msg.seq, "parse_error", parse.error());
        return;
    }
    auto& req = parse.value();

    auto* plr = players_->get_player_by_name(req.player_name);
    if (!plr) {
        ws_server_->send(conn_id, network::make_admin_response(
            network::json_message_type::admin_kick_player_response,
            msg.seq, false, {}, "Player not found"));
        return;
    }

    if (plr->connection.is_valid()) {
        ws_server_->disconnect(plr->connection, req.reason.empty() ? "Kicked by admin" : req.reason);
    }

    LOG_INFO(admin, "Admin kicked player '{}': {}", req.player_name, req.reason);

    ws_server_->send(conn_id, network::make_admin_response(
        network::json_message_type::admin_kick_player_response,
        msg.seq, true, {{"player_name", req.player_name}}));
}

void admin_web_handlers::handle_ban_player(connection_id conn_id, const network::json_message& msg)
{
    if (!require_admin(conn_id, msg.seq)) return;

    auto parse = network::admin_ban_player_request_data::from_json(msg.data);
    if (parse.is_err()) {
        send_error(conn_id, msg.seq, "parse_error", parse.error());
        return;
    }
    auto& req = parse.value();

    // Look up account by player name
    auto* plr = players_->get_player_by_name(req.player_name);
    if (!plr) {
        ws_server_->send(conn_id, network::make_admin_response(
            network::json_message_type::admin_ban_player_response,
            msg.seq, false, {}, "Player not found (must be online)"));
        return;
    }

    // Ban the account
    if (auth_) {
        std::optional<std::chrono::system_clock::time_point> expires;
        if (req.duration_hours > 0) {
            expires = std::chrono::system_clock::now() + std::chrono::hours(req.duration_hours);
        }
        auth_->ban_account(plr->account, req.reason, expires);
    }

    // Kick from game
    if (plr->connection.is_valid()) {
        ws_server_->disconnect(plr->connection, "Banned: " + req.reason);
    }

    LOG_INFO(admin, "Admin banned player '{}' (account {}): {} ({}h)",
        req.player_name, plr->account.value, req.reason, req.duration_hours);

    ws_server_->send(conn_id, network::make_admin_response(
        network::json_message_type::admin_ban_player_response,
        msg.seq, true, {{"player_name", req.player_name}}));
}

void admin_web_handlers::handle_teleport_player(connection_id conn_id, const network::json_message& msg)
{
    if (!require_admin(conn_id, msg.seq)) return;

    auto parse = network::admin_teleport_player_request_data::from_json(msg.data);
    if (parse.is_err()) {
        send_error(conn_id, msg.seq, "parse_error", parse.error());
        return;
    }
    auto& req = parse.value();

    auto* plr = players_->get_player_by_name(req.player_name);
    if (!plr) {
        ws_server_->send(conn_id, network::make_admin_response(
            network::json_message_type::admin_teleport_player_response,
            msg.seq, false, {}, "Player not found"));
        return;
    }

    auto result = players_->execute_teleport(plr->id, req.dest_map,
        world::position{req.dest_x, req.dest_y}, world::direction::south);

    if (!result.success) {
        ws_server_->send(conn_id, network::make_admin_response(
            network::json_message_type::admin_teleport_player_response,
            msg.seq, false, {}, result.error));
        return;
    }

    LOG_INFO(admin, "Admin teleported '{}' to {} ({},{})",
        req.player_name, req.dest_map, req.dest_x, req.dest_y);

    ws_server_->send(conn_id, network::make_admin_response(
        network::json_message_type::admin_teleport_player_response,
        msg.seq, true, {{"player_name", req.player_name}}));
}

void admin_web_handlers::handle_modify_player(connection_id conn_id, const network::json_message& msg)
{
    if (!require_admin(conn_id, msg.seq, 20)) return;  // Administrator only

    auto parse = network::admin_modify_player_request_data::from_json(msg.data);
    if (parse.is_err()) {
        send_error(conn_id, msg.seq, "parse_error", parse.error());
        return;
    }
    auto& req = parse.value();

    auto* plr = players_->get_player_by_name(req.player_name);
    if (!plr) {
        ws_server_->send(conn_id, network::make_admin_response(
            network::json_message_type::admin_modify_player_response,
            msg.seq, false, {}, "Player not found"));
        return;
    }

    auto& mods = req.modifications;
    if (mods.contains("hp") && mods["hp"].is_number()) {
        plr->hp = std::clamp(mods["hp"].get<int32_t>(), 0, plr->computed.max_hp);
    }
    if (mods.contains("mp") && mods["mp"].is_number()) {
        plr->mp = std::clamp(mods["mp"].get<int32_t>(), 0, plr->computed.max_mp);
    }
    if (mods.contains("sp") && mods["sp"].is_number()) {
        plr->sp = std::clamp(mods["sp"].get<int32_t>(), 0, plr->computed.max_sp);
    }
    if (mods.contains("level") && mods["level"].is_number()) {
        plr->experience.level = std::clamp(mods["level"].get<int16_t>(), static_cast<int16_t>(1), static_cast<int16_t>(180));
        plr->recalculate_stats();
    }
    if (mods.contains("str") && mods["str"].is_number()) {
        plr->base.strength = mods["str"].get<int16_t>();
        plr->recalculate_stats();
    }
    if (mods.contains("dex") && mods["dex"].is_number()) {
        plr->base.dexterity = mods["dex"].get<int16_t>();
        plr->recalculate_stats();
    }
    if (mods.contains("vit") && mods["vit"].is_number()) {
        plr->base.vitality = mods["vit"].get<int16_t>();
        plr->recalculate_stats();
    }
    if (mods.contains("int") && mods["int"].is_number()) {
        plr->base.intelligence = mods["int"].get<int16_t>();
        plr->recalculate_stats();
    }
    if (mods.contains("mag") && mods["mag"].is_number()) {
        plr->base.magic = mods["mag"].get<int16_t>();
        plr->recalculate_stats();
    }
    if (mods.contains("cha") && mods["cha"].is_number()) {
        plr->base.charisma = mods["cha"].get<int16_t>();
        plr->recalculate_stats();
    }
    if (mods.contains("gold") && mods["gold"].is_number() && inventory_) {
        auto owner_id = entity_id(plr->id.value);
        auto current_gold = inventory_->get_gold(owner_id);
        auto target_gold = mods["gold"].get<int64_t>();
        if (target_gold > current_gold) {
            inventory_->add_gold(owner_id, target_gold - current_gold);
        } else if (target_gold < current_gold) {
            inventory_->remove_gold(owner_id, current_gold - target_gold);
        }
    }
    if (mods.contains("pk_points") && mods["pk_points"].is_number()) {
        plr->pk.points = mods["pk_points"].get<int32_t>();
    }

    LOG_INFO(admin, "Admin modified player '{}': {}", req.player_name, mods.dump());

    ws_server_->send(conn_id, network::make_admin_response(
        network::json_message_type::admin_modify_player_response,
        msg.seq, true, {{"player_name", req.player_name}}));
}

// === World/NPC Management ===

void admin_web_handlers::handle_list_maps(connection_id conn_id, const network::json_message& msg)
{
    if (!require_admin(conn_id, msg.seq)) return;

    nlohmann::json maps_arr = nlohmann::json::array();
    if (world_) {
        world_->for_each_map([&](map_id id, const world::map& m) {
            int player_count = 0;
            int npc_count = 0;
            if (players_) {
                auto pids = players_->get_players_on_map_in_range(id,
                    world::position{0, 0}, 10000);
                player_count = static_cast<int>(pids.size());
            }
            if (npc_) {
                npc_->for_each_npc_on_map(id, [&](entity::entity, const npc::npc&) {
                    ++npc_count;
                });
            }
            maps_arr.push_back({
                {"id", id.value},
                {"name", std::string(m.name())},
                {"width", m.width()},
                {"height", m.height()},
                {"player_count", player_count},
                {"npc_count", npc_count},
                {"weather", static_cast<int>(m.weather())},
                {"weather_active", m.weather_active()}
            });
        });
    }

    nlohmann::json data;
    data["maps"] = maps_arr;
    data["count"] = maps_arr.size();

    ws_server_->send(conn_id, network::make_admin_response(
        network::json_message_type::admin_list_maps_response,
        msg.seq, true, data));
}

void admin_web_handlers::handle_get_map(connection_id conn_id, const network::json_message& msg)
{
    if (!require_admin(conn_id, msg.seq)) return;

    auto parse = network::admin_get_map_request_data::from_json(msg.data);
    if (parse.is_err()) {
        send_error(conn_id, msg.seq, "parse_error", parse.error());
        return;
    }

    auto* m = world_->get_map_by_name(parse.value().map_name);
    if (!m) {
        ws_server_->send(conn_id, network::make_admin_response(
            network::json_message_type::admin_get_map_response,
            msg.seq, false, {}, "Map not found"));
        return;
    }

    auto mid = m->id();

    // Collect players on map
    nlohmann::json players_arr = nlohmann::json::array();
    if (players_) {
        players_->for_each_player([&](player_id pid, const player::player& plr) {
            if (plr.current_map == mid) {
                players_arr.push_back({
                    {"id", pid.value},
                    {"name", plr.name},
                    {"x", plr.pos.x},
                    {"y", plr.pos.y},
                    {"level", plr.experience.level}
                });
            }
        });
    }

    // Collect NPCs on map
    nlohmann::json npcs_arr = nlohmann::json::array();
    if (npc_) {
        npc_->for_each_npc_on_map(mid, [&](entity::entity eid, const npc::npc& n) {
            npcs_arr.push_back({
                {"entity_id", eid.id},
                {"template_id", n.template_id.value},
                {"name", n.name},
                {"x", n.pos.x},
                {"y", n.pos.y},
                {"hp", n.hp},
                {"max_hp", n.max_hp},
                {"level", n.level}
            });
        });
    }

    nlohmann::json data;
    data["name"] = std::string(m->name());
    data["width"] = m->width();
    data["height"] = m->height();
    data["weather"] = static_cast<int>(m->weather());
    data["weather_active"] = m->weather_active();
    data["spawner_count"] = m->mob_spawner_count();
    data["players"] = players_arr;
    data["npcs"] = npcs_arr;

    ws_server_->send(conn_id, network::make_admin_response(
        network::json_message_type::admin_get_map_response,
        msg.seq, true, data));
}

void admin_web_handlers::handle_spawn_npc(connection_id conn_id, const network::json_message& msg)
{
    if (!require_admin(conn_id, msg.seq, 20)) return;

    auto parse = network::admin_spawn_npc_request_data::from_json(msg.data);
    if (parse.is_err()) {
        send_error(conn_id, msg.seq, "parse_error", parse.error());
        return;
    }
    auto& req = parse.value();

    if (!npc_registry_) {
        ws_server_->send(conn_id, network::make_admin_response(
            network::json_message_type::admin_spawn_npc_response,
            msg.seq, false, {}, "NPC registry not available"));
        return;
    }

    auto* tmpl = npc_registry_->find_by_name(req.npc_name);
    if (!tmpl) {
        ws_server_->send(conn_id, network::make_admin_response(
            network::json_message_type::admin_spawn_npc_response,
            msg.seq, false, {}, "NPC template not found"));
        return;
    }

    auto* m = world_->get_map_by_name(req.map_name);
    if (!m) {
        ws_server_->send(conn_id, network::make_admin_response(
            network::json_message_type::admin_spawn_npc_response,
            msg.seq, false, {}, "Map not found"));
        return;
    }

    int spawned = 0;
    for (int i = 0; i < req.count; ++i) {
        auto result = npc_->spawn_npc(tmpl->id, m->id(),
            world::position{req.x, req.y});
        if (result.is_ok()) ++spawned;
    }

    LOG_INFO(admin, "Admin spawned {} x{} on {} at ({},{})",
        req.npc_name, spawned, req.map_name, req.x, req.y);

    ws_server_->send(conn_id, network::make_admin_response(
        network::json_message_type::admin_spawn_npc_response,
        msg.seq, true, {{"spawned", spawned}}));
}

void admin_web_handlers::handle_kill_npc(connection_id conn_id, const network::json_message& msg)
{
    if (!require_admin(conn_id, msg.seq)) return;

    auto parse = network::admin_kill_npc_request_data::from_json(msg.data);
    if (parse.is_err()) {
        send_error(conn_id, msg.seq, "parse_error", parse.error());
        return;
    }

    entity::entity eid;
    eid.id = parse.value().entity_id;

    auto* n = npc_->get_npc(eid);
    if (!n) {
        ws_server_->send(conn_id, network::make_admin_response(
            network::json_message_type::admin_kill_npc_response,
            msg.seq, false, {}, "NPC not found"));
        return;
    }

    std::string npc_name = n->name;
    npc_->kill_npc(eid, entity::entity{});

    LOG_INFO(admin, "Admin killed NPC '{}' (entity {})", npc_name, eid.id);

    ws_server_->send(conn_id, network::make_admin_response(
        network::json_message_type::admin_kill_npc_response,
        msg.seq, true, {{"npc_name", npc_name}}));
}

// === Inventory Management ===

void admin_web_handlers::handle_get_inventory(connection_id conn_id, const network::json_message& msg)
{
    if (!require_admin(conn_id, msg.seq)) return;

    auto parse = network::admin_get_inventory_request_data::from_json(msg.data);
    if (parse.is_err()) {
        send_error(conn_id, msg.seq, "parse_error", parse.error());
        return;
    }

    auto* plr = players_->get_player_by_name(parse.value().player_name);
    if (!plr) {
        ws_server_->send(conn_id, network::make_admin_response(
            network::json_message_type::admin_get_inventory_response,
            msg.seq, false, {}, "Player not found"));
        return;
    }

    auto owner_id = entity_id(plr->id.value);
    nlohmann::json data;
    data["player_name"] = plr->name;
    data["gold"] = inventory_ ? inventory_->get_gold(owner_id) : 0;

    // Inventory slots
    nlohmann::json inv_arr = nlohmann::json::array();
    if (inventory_) {
        auto* inv = inventory_->get_inventory(owner_id);
        if (inv) {
            for (int16_t i = 0; i < 50; ++i) {
                auto* slot = inv->get_slot(i);
                if (slot && !slot->is_empty()) {
                    nlohmann::json entry = {
                        {"slot", i},
                        {"item_id", slot->item.value},
                        {"count", slot->count}
                    };
                    if (item_registry_) {
                        auto* tmpl = item_registry_->get(slot->item);
                        if (tmpl) entry["name"] = tmpl->name;
                    }
                    inv_arr.push_back(entry);
                }
            }
        }
    }
    data["inventory"] = inv_arr;

    // Equipment
    nlohmann::json equip_arr = nlohmann::json::array();
    for (int s = 0; s < static_cast<int>(player::equip_slot::count); ++s) {
        auto slot = static_cast<player::equip_slot>(s);
        auto equipped = plr->equipment.get(slot);
        if (equipped.id.is_valid()) {
            nlohmann::json entry = {
                {"slot", s},
                {"item_id", equipped.id.value},
                {"durability", equipped.durability},
                {"max_durability", equipped.max_durability}
            };
            if (item_registry_) {
                auto* tmpl = item_registry_->get(equipped.id);
                if (tmpl) entry["name"] = tmpl->name;
            }
            equip_arr.push_back(entry);
        }
    }
    data["equipment"] = equip_arr;

    ws_server_->send(conn_id, network::make_admin_response(
        network::json_message_type::admin_get_inventory_response,
        msg.seq, true, data));
}

void admin_web_handlers::handle_give_item(connection_id conn_id, const network::json_message& msg)
{
    if (!require_admin(conn_id, msg.seq, 20)) return;

    auto parse = network::admin_give_item_request_data::from_json(msg.data);
    if (parse.is_err()) {
        send_error(conn_id, msg.seq, "parse_error", parse.error());
        return;
    }
    auto& req = parse.value();

    auto* plr = players_->get_player_by_name(req.player_name);
    if (!plr) {
        ws_server_->send(conn_id, network::make_admin_response(
            network::json_message_type::admin_give_item_response,
            msg.seq, false, {}, "Player not found"));
        return;
    }

    // Create item instance
    item::item_create_info create_info;
    create_info.template_id = item_id{req.item_template_id};
    create_info.count = req.count;
    create_info.owner = entity_id(plr->id.value);

    auto create_result = item_->create_item(create_info);
    if (create_result.is_err()) {
        ws_server_->send(conn_id, network::make_admin_response(
            network::json_message_type::admin_give_item_response,
            msg.seq, false, {}, create_result.error()));
        return;
    }

    auto new_item_id = create_result.value();
    auto add_result = inventory_->add_item(entity_id(plr->id.value), new_item_id, req.count);
    if (add_result != inventory::inventory_result::success) {
        ws_server_->send(conn_id, network::make_admin_response(
            network::json_message_type::admin_give_item_response,
            msg.seq, false, {}, "Failed to add item to inventory"));
        return;
    }

    std::string item_name = "Item#" + std::to_string(req.item_template_id);
    if (item_registry_) {
        auto* tmpl = item_registry_->get(item_id{req.item_template_id});
        if (tmpl) item_name = tmpl->name;
    }

    LOG_INFO(admin, "Admin gave '{}' x{} to '{}'", item_name, req.count, req.player_name);

    ws_server_->send(conn_id, network::make_admin_response(
        network::json_message_type::admin_give_item_response,
        msg.seq, true, {
            {"player_name", req.player_name},
            {"item_name", item_name},
            {"count", req.count}
        }));
}

void admin_web_handlers::handle_remove_item(connection_id conn_id, const network::json_message& msg)
{
    if (!require_admin(conn_id, msg.seq, 20)) return;

    auto parse = network::admin_remove_item_request_data::from_json(msg.data);
    if (parse.is_err()) {
        send_error(conn_id, msg.seq, "parse_error", parse.error());
        return;
    }
    auto& req = parse.value();

    auto* plr = players_->get_player_by_name(req.player_name);
    if (!plr) {
        ws_server_->send(conn_id, network::make_admin_response(
            network::json_message_type::admin_remove_item_response,
            msg.seq, false, {}, "Player not found"));
        return;
    }

    auto owner_id = entity_id(plr->id.value);
    auto* inv = inventory_->get_inventory(owner_id);
    if (!inv) {
        ws_server_->send(conn_id, network::make_admin_response(
            network::json_message_type::admin_remove_item_response,
            msg.seq, false, {}, "Inventory not found"));
        return;
    }

    auto* slot = inv->get_slot(req.inventory_slot);
    if (!slot || slot->is_empty()) {
        ws_server_->send(conn_id, network::make_admin_response(
            network::json_message_type::admin_remove_item_response,
            msg.seq, false, {}, "Slot is empty"));
        return;
    }

    std::string item_name = "Item#" + std::to_string(slot->item.value);
    if (item_registry_) {
        auto* tmpl = item_registry_->get(slot->item);
        if (tmpl) item_name = tmpl->name;
    }

    if (req.count <= 0 || req.count >= slot->count) {
        slot->clear();
    } else {
        slot->count -= req.count;
    }

    LOG_INFO(admin, "Admin removed '{}' from '{}' slot {}",
        item_name, req.player_name, req.inventory_slot);

    ws_server_->send(conn_id, network::make_admin_response(
        network::json_message_type::admin_remove_item_response,
        msg.seq, true, {
            {"player_name", req.player_name},
            {"item_name", item_name}
        }));
}

// === Social ===

void admin_web_handlers::handle_list_guilds(connection_id conn_id, const network::json_message& msg)
{
    if (!require_admin(conn_id, msg.seq)) return;

    nlohmann::json guilds_arr = nlohmann::json::array();
    if (social_) {
        social_->for_each_guild([&](guild_id gid, const social::guild& g) {
            guilds_arr.push_back({
                {"id", gid.value},
                {"name", g.name},
                {"tag", g.tag},
                {"member_count", g.member_count()},
                {"master_id", g.master.value}
            });
        });
    }

    nlohmann::json data;
    data["guilds"] = guilds_arr;
    data["count"] = guilds_arr.size();

    ws_server_->send(conn_id, network::make_admin_response(
        network::json_message_type::admin_list_guilds_response,
        msg.seq, true, data));
}

void admin_web_handlers::handle_get_guild(connection_id conn_id, const network::json_message& msg)
{
    if (!require_admin(conn_id, msg.seq)) return;

    auto parse = network::admin_get_guild_request_data::from_json(msg.data);
    if (parse.is_err()) {
        send_error(conn_id, msg.seq, "parse_error", parse.error());
        return;
    }

    auto gid = social_->find_guild_by_name(parse.value().guild_name);
    if (!gid.is_valid()) {
        ws_server_->send(conn_id, network::make_admin_response(
            network::json_message_type::admin_get_guild_response,
            msg.seq, false, {}, "Guild not found"));
        return;
    }

    auto* g = social_->get_guild(gid);
    if (!g) {
        ws_server_->send(conn_id, network::make_admin_response(
            network::json_message_type::admin_get_guild_response,
            msg.seq, false, {}, "Guild not found"));
        return;
    }

    nlohmann::json members_arr = nlohmann::json::array();
    for (const auto& member : g->members) {
        members_arr.push_back({
            {"name", member.name},
            {"rank", static_cast<int>(member.rank)},
            {"online", member.player.is_valid()},
            {"contribution", member.contribution}
        });
    }

    nlohmann::json data;
    data["id"] = gid.value;
    data["name"] = g->name;
    data["tag"] = g->tag;
    data["motd"] = g->motd;
    data["members"] = members_arr;
    data["member_count"] = g->member_count();

    ws_server_->send(conn_id, network::make_admin_response(
        network::json_message_type::admin_get_guild_response,
        msg.seq, true, data));
}

// === Account Management ===

void admin_web_handlers::handle_get_account(connection_id conn_id, const network::json_message& msg)
{
    if (!require_admin(conn_id, msg.seq)) return;

    auto parse = network::admin_get_account_request_data::from_json(msg.data);
    if (parse.is_err()) {
        send_error(conn_id, msg.seq, "parse_error", parse.error());
        return;
    }

    auto account_result = auth_->get_account_by_username(parse.value().username);
    if (account_result.is_err()) {
        ws_server_->send(conn_id, network::make_admin_response(
            network::json_message_type::admin_get_account_response,
            msg.seq, false, {}, "Account not found"));
        return;
    }

    auto& acct = account_result.value();

    nlohmann::json data;
    data["id"] = acct.id.value;
    data["username"] = acct.username;
    data["admin_level"] = static_cast<int>(acct.admin);
    data["is_banned"] = acct.is_banned;
    data["ban_reason"] = acct.ban_reason;

    // Get character list
    auto chars_result = auth_->get_characters(acct.id);
    if (chars_result.is_ok()) {
        nlohmann::json chars_arr = nlohmann::json::array();
        for (const auto& ch : chars_result.value()) {
            chars_arr.push_back({
                {"id", ch.id.value},
                {"name", ch.name},
                {"level", ch.level},
                {"class", ch.class_type},
                {"nation", ch.nation},
                {"map", ch.map_name}
            });
        }
        data["characters"] = chars_arr;
    }

    ws_server_->send(conn_id, network::make_admin_response(
        network::json_message_type::admin_get_account_response,
        msg.seq, true, data));
}

void admin_web_handlers::handle_unban_player(connection_id conn_id, const network::json_message& msg)
{
    if (!require_admin(conn_id, msg.seq)) return;

    auto parse = network::admin_unban_player_request_data::from_json(msg.data);
    if (parse.is_err()) {
        send_error(conn_id, msg.seq, "parse_error", parse.error());
        return;
    }

    // Look up account by username (player_name is the username here)
    auto account_result = auth_->get_account_by_username(parse.value().player_name);
    if (account_result.is_err()) {
        ws_server_->send(conn_id, network::make_admin_response(
            network::json_message_type::admin_unban_player_response,
            msg.seq, false, {}, "Account not found"));
        return;
    }

    auth_->unban_account(account_result.value().id);

    LOG_INFO(admin, "Admin unbanned account '{}'", parse.value().player_name);

    ws_server_->send(conn_id, network::make_admin_response(
        network::json_message_type::admin_unban_player_response,
        msg.seq, true, {{"username", parse.value().player_name}}));
}

// === Spectator ===

void admin_web_handlers::handle_subscribe_map(connection_id conn_id, const network::json_message& msg)
{
    auto* conn = require_admin(conn_id, msg.seq);
    if (!conn) return;

    auto parse = network::admin_subscribe_map_request_data::from_json(msg.data);
    if (parse.is_err()) {
        send_error(conn_id, msg.seq, "parse_error", parse.error());
        return;
    }

    auto* m = world_->get_map_by_name(parse.value().map_name);
    if (!m) {
        ws_server_->send(conn_id, network::make_admin_response(
            network::json_message_type::admin_subscribe_map_response,
            msg.seq, false, {}, "Map not found"));
        return;
    }

    auto& sub = conn->subscription();
    sub.sub_mode = network::admin_subscription::mode::map;
    sub.target_map = m->id();
    sub.target_player = player_id{};

    ws_server_->send(conn_id, network::make_admin_response(
        network::json_message_type::admin_subscribe_map_response,
        msg.seq, true, {{"map_name", std::string(m->name())}}));

    // Send initial map state
    send_spectator_init(conn_id, m->id());
}

void admin_web_handlers::handle_subscribe_player(connection_id conn_id, const network::json_message& msg)
{
    auto* conn = require_admin(conn_id, msg.seq);
    if (!conn) return;

    auto parse = network::admin_subscribe_player_request_data::from_json(msg.data);
    if (parse.is_err()) {
        send_error(conn_id, msg.seq, "parse_error", parse.error());
        return;
    }

    auto* plr = players_->get_player_by_name(parse.value().player_name);
    if (!plr) {
        ws_server_->send(conn_id, network::make_admin_response(
            network::json_message_type::admin_subscribe_player_response,
            msg.seq, false, {}, "Player not found"));
        return;
    }

    auto& sub = conn->subscription();
    sub.sub_mode = network::admin_subscription::mode::player;
    sub.target_player = plr->id;
    sub.target_map = plr->current_map;

    ws_server_->send(conn_id, network::make_admin_response(
        network::json_message_type::admin_subscribe_player_response,
        msg.seq, true, {{"player_name", plr->name}}));

    // Send initial map state for the player's current map
    send_spectator_init(conn_id, plr->current_map);
}

void admin_web_handlers::handle_unsubscribe(connection_id conn_id, const network::json_message& msg)
{
    auto* conn = require_admin(conn_id, msg.seq);
    if (!conn) return;

    auto& sub = conn->subscription();
    sub.sub_mode = network::admin_subscription::mode::none;
    sub.target_map = map_id{};
    sub.target_player = player_id{};

    ws_server_->send(conn_id, network::make_admin_response(
        network::json_message_type::admin_unsubscribe_response,
        msg.seq, true));
}

void admin_web_handlers::send_spectator_init(connection_id conn_id, map_id map)
{
    nlohmann::json data;

    // Map info
    if (auto* m = world_->get_map(map)) {
        data["map_name"] = std::string(m->name());
        data["width"] = m->width();
        data["height"] = m->height();
        data["weather"] = static_cast<int>(m->weather());
    }

    // All players on map
    nlohmann::json players_arr = nlohmann::json::array();
    if (players_) {
        players_->for_each_player([&](player_id pid, const player::player& plr) {
            if (plr.current_map == map) {
                players_arr.push_back({
                    {"entity_id", plr.ecs_entity.id},
                    {"type", "player"},
                    {"name", plr.name},
                    {"x", plr.pos.x},
                    {"y", plr.pos.y},
                    {"level", plr.experience.level},
                    {"hp", plr.hp},
                    {"max_hp", plr.computed.max_hp}
                });
            }
        });
    }
    data["players"] = players_arr;

    // All NPCs on map
    nlohmann::json npcs_arr = nlohmann::json::array();
    if (npc_) {
        npc_->for_each_npc_on_map(map, [&](entity::entity eid, const npc::npc& n) {
            npcs_arr.push_back({
                {"entity_id", eid.id},
                {"type", "npc"},
                {"name", n.name},
                {"template_id", n.template_id.value},
                {"x", n.pos.x},
                {"y", n.pos.y},
                {"level", n.level},
                {"hp", n.hp},
                {"max_hp", n.max_hp}
            });
        });
    }
    data["npcs"] = npcs_arr;

    // Environment
    if (scheduler_) {
        auto& clock = scheduler_->game_time();
        data["hour"] = clock.hour();
        data["minute"] = clock.minute();
        data["is_day"] = clock.is_day();
    }

    network::json_message init_msg{
        .type = network::json_message_type::admin_spectator_init,
        .seq = 0,
        .data = data
    };
    ws_server_->send(conn_id, init_msg);
}

}  // namespace hb::bridge
