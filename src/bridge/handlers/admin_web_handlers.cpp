// admin_web_handlers.cpp
// Admin web tool handler implementations

#include "bridge/handlers/admin_web_handlers.h"
#include "core/logger.h"
#include <algorithm>
#include <cctype>
#include <fstream>
#include <filesystem>
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
#include "registry/npc_template.h"
#include "registry/item_template.h"
#include "war/war_system.h"
#include "war/war_types.h"
#include "war/war_persistence.h"
#include "effect/effect_system.h"
#include "effect/active_effect.h"
#include "config/config_system.h"
#include "config/server_config.h"
#include "magic/magic_system.h"
#include "magic/spell.h"
#include "quest/quest_system.h"
#include "quest/quest.h"
#include "skill/skill_system.h"
#include "skill/skill.h"
#include "perf/perf_stats.h"
#include "application.h"

namespace hb::bridge {

namespace {

// Standard base64 encoding for binary data transfer
auto base64_encode_standard(const uint8_t* data, size_t len) -> std::string
{
    static constexpr char table[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string result;
    result.reserve((len + 2) / 3 * 4);

    for (size_t i = 0; i < len; i += 3)
    {
        uint32_t n = static_cast<uint32_t>(data[i]) << 16;
        if (i + 1 < len) n |= static_cast<uint32_t>(data[i + 1]) << 8;
        if (i + 2 < len) n |= static_cast<uint32_t>(data[i + 2]);

        result.push_back(table[(n >> 18) & 0x3F]);
        result.push_back(table[(n >> 12) & 0x3F]);
        result.push_back((i + 1 < len) ? table[(n >> 6) & 0x3F] : '=');
        result.push_back((i + 2 < len) ? table[n & 0x3F] : '=');
    }

    return result;
}

} // anonymous namespace

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
    item_registry* item_reg,
    war::war_system* war,
    effect::effect_system* effects,
    config_system* config,
    magic::magic_system* magic,
    quest::quest_system* quest,
    skill::skill_system* skill,
    perf::perf_stats_system* perf_stats)
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
    war_ = war;
    effects_ = effects;
    config_ = config;
    magic_ = magic;
    quest_ = quest;
    skill_ = skill;
    perf_stats_ = perf_stats;
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
        case mt::admin_get_map_data_request:    handle_get_map_data(conn_id, msg); break;
        case mt::admin_broadcast_request:       handle_broadcast(conn_id, msg); break;
        case mt::admin_mute_player_request:     handle_mute_player(conn_id, msg); break;
        case mt::admin_unmute_player_request:    handle_unmute_player(conn_id, msg); break;
        case mt::admin_list_item_templates_request: handle_list_item_templates(conn_id, msg); break;
        case mt::admin_get_item_template_request:   handle_get_item_template(conn_id, msg); break;
        case mt::admin_list_npc_templates_request:  handle_list_npc_templates(conn_id, msg); break;
        case mt::admin_get_npc_template_request:    handle_get_npc_template(conn_id, msg); break;
        case mt::admin_get_war_status_request:      handle_get_war_status(conn_id, msg); break;
        case mt::admin_list_parties_request:         handle_list_parties(conn_id, msg); break;
        case mt::admin_search_players_request:       handle_search_players(conn_id, msg); break;
        case mt::admin_get_audit_log_request:        handle_get_audit_log(conn_id, msg); break;
        case mt::admin_get_config_request:           handle_get_config(conn_id, msg); break;
        case mt::admin_set_config_request:           handle_set_config(conn_id, msg); break;
        case mt::admin_reload_config_request:        handle_reload_config(conn_id, msg); break;
        case mt::admin_list_scheduled_tasks_request: handle_list_scheduled_tasks(conn_id, msg); break;
        case mt::admin_cancel_scheduled_task_request: handle_cancel_scheduled_task(conn_id, msg); break;
        case mt::admin_start_task_request:           handle_start_task(conn_id, msg); break;
        case mt::admin_run_query_request:            handle_run_query(conn_id, msg); break;
        case mt::admin_list_map_npcs_request:        handle_list_map_npcs(conn_id, msg); break;
        case mt::admin_list_map_ground_items_request: handle_list_map_ground_items(conn_id, msg); break;
        case mt::admin_remove_ground_item_request:   handle_remove_ground_item(conn_id, msg); break;
        case mt::admin_guild_action_request:         handle_guild_action(conn_id, msg); break;
        case mt::admin_message_player_request:       handle_message_player(conn_id, msg); break;
        case mt::admin_set_environment_request:      handle_set_environment(conn_id, msg); break;
        case mt::admin_shutdown_server_request:       handle_shutdown_server(conn_id, msg); break;
        case mt::admin_modify_skills_request:        handle_modify_skills(conn_id, msg); break;
        case mt::admin_modify_spells_request:        handle_modify_spells(conn_id, msg); break;
        case mt::admin_get_player_quests_request:    handle_get_player_quests(conn_id, msg); break;
        case mt::admin_quest_action_request:         handle_quest_action(conn_id, msg); break;
        case mt::admin_remove_effects_request:       handle_remove_effects(conn_id, msg); break;
        case mt::admin_create_account_request:       handle_create_account(conn_id, msg); break;
        case mt::admin_change_password_request:      handle_change_password(conn_id, msg); break;
        case mt::admin_set_admin_level_request:      handle_set_admin_level(conn_id, msg); break;
        case mt::admin_list_spawn_points_request:    handle_list_spawn_points(conn_id, msg); break;
        case mt::admin_list_spell_templates_request: handle_list_spell_templates(conn_id, msg); break;
        case mt::admin_get_spell_template_request:   handle_get_spell_template(conn_id, msg); break;
        case mt::admin_set_maintenance_mode_request: handle_set_maintenance_mode(conn_id, msg); break;
        case mt::admin_create_character_request_admin: handle_create_character_admin(conn_id, msg); break;
        case mt::admin_delete_character_request_admin: handle_delete_character_admin(conn_id, msg); break;
        case mt::admin_manage_ip_bans_request:       handle_manage_ip_bans(conn_id, msg); break;
        case mt::admin_perf_stats_request:           handle_perf_stats(conn_id, msg); break;
        case mt::admin_start_war_request:            handle_start_war(conn_id, msg); break;
        case mt::admin_end_war_request:              handle_end_war(conn_id, msg); break;
        case mt::admin_war_history_request:           handle_war_history(conn_id, msg); break;
        case mt::admin_war_participants_request:      handle_war_participants(conn_id, msg); break;
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

void admin_web_handlers::audit_log(connection_id conn_id, std::string_view action,
                                    bool success, std::string_view result)
{
    if (!admin_) return;
    auto* conn = ws_server_->get_connection(conn_id);
    std::string name = conn ? conn->username() : "unknown";
    admin_->log_action(name, action, success, result);
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
    auto uptime = std::chrono::steady_clock::now() - start_time_;
    data["uptime_seconds"] = std::chrono::duration_cast<std::chrono::seconds>(uptime).count();
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
        data["scheduled_tasks"] = scheduler_->pending_count();
    }

    // Economy stats
    int64_t total_gold = 0;
    if (inventory_ && players_) {
        players_->for_each_player([&](player_id pid, const player::player& plr) {
            total_gold += inventory_->get_gold(entity_id(plr.id.value));
        });
    }
    data["total_gold"] = total_gold;

    if (admin_) {
        data["active_admin_count"] = admin_->active_admin_count();
    }

    ws_server_->send(conn_id, network::make_admin_response(
        network::json_message_type::admin_server_stats_response,
        msg.seq, true, data));
}

// === Player Management ===

void admin_web_handlers::handle_list_players(connection_id conn_id, const network::json_message& msg)
{
    if (!require_admin(conn_id, msg.seq)) return;

    // Collect connected accounts: account_id → {username, ip, active_player_id}
    struct account_entry {
        std::string username;
        std::string ip;
        bool in_game{false};
        player_id active_player{};
        int64_t online_seconds{0};
    };
    std::unordered_map<uint32_t, account_entry> accounts;

    auto now = std::chrono::steady_clock::now();
    ws_server_->for_each_connection([&](const network::ws_connection& conn) {
        if (conn.state() == network::ws_connection_state::authenticated) {
            auto aid = conn.account().value;
            if (accounts.find(aid) == accounts.end()) {
                auto secs = std::chrono::duration_cast<std::chrono::seconds>(
                    now - conn.connected_at()).count();
                accounts[aid] = {conn.username(), conn.remote_address(), false, {}, secs};
            }
        }
        else if (conn.state() == network::ws_connection_state::in_game) {
            auto aid = conn.account().value;
            auto secs = std::chrono::duration_cast<std::chrono::seconds>(
                now - conn.connected_at()).count();
            accounts[aid] = {conn.username(), conn.remote_address(), true, conn.player(), secs};
        }
    });

    // Build response: for each account, fetch characters and mark the active one
    nlohmann::json accounts_arr = nlohmann::json::array();
    for (auto& [aid, entry] : accounts) {
        nlohmann::json acct;
        acct["account"] = entry.username;
        acct["ip"] = entry.ip;
        acct["in_game"] = entry.in_game;
        acct["online_seconds"] = entry.online_seconds;

        // Fetch characters from DB
        nlohmann::json chars_arr = nlohmann::json::array();
        if (auth_) {
            auto chars_result = auth_->get_characters(account_id{aid});
            if (chars_result.is_ok()) {
                for (auto& ch : chars_result.value()) {
                    nlohmann::json cj;
                    cj["id"] = ch.id.value;
                    cj["name"] = ch.name;
                    cj["level"] = ch.level;
                    cj["faction"] = ch.nation;
                    cj["gender"] = ch.gender;
                    cj["map"] = ch.map_name;
                    cj["active"] = false;

                    // If this character is the one in-game, overlay live data
                    // Note: entry.active_player is runtime player_id, ch.id is DB character_id
                    // Compare via player->character_id to match correctly
                    if (entry.in_game) {
                        auto* plr = players_ ? players_->get_player(entry.active_player) : nullptr;
                        if (plr && ch.id == plr->character_id) {
                            cj["active"] = true;
                            cj["level"] = plr->experience.level;
                            cj["map"] = world_ ? [&]() -> std::string {
                                auto* m = world_->get_map(plr->current_map);
                                return m ? std::string(m->name()) : "unknown";
                            }() : "unknown";
                            cj["x"] = plr->pos.x;
                            cj["y"] = plr->pos.y;
                            cj["hp"] = plr->hp;
                            cj["max_hp"] = plr->computed.max_hp;
                            cj["mp"] = plr->mp;
                            cj["max_mp"] = plr->computed.max_mp;
                            cj["faction"] = static_cast<int>(plr->faction);
                            cj["guild"] = plr->guild_name;
                            cj["pk_count"] = plr->pk.count;
                        }
                    }

                    chars_arr.push_back(std::move(cj));
                }
            }
        }

        acct["characters"] = chars_arr;
        accounts_arr.push_back(std::move(acct));
    }

    nlohmann::json data;
    data["accounts"] = accounts_arr;
    data["count"] = accounts_arr.size();

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

    // Active effects
    nlohmann::json effects_arr = nlohmann::json::array();
    if (effects_) {
        auto* effects_list = effects_->get_effects(plr->ecs_entity);
        if (effects_list) {
            for (const auto& eff : *effects_list) {
                nlohmann::json ej;
                ej["type"] = static_cast<int>(eff.type);
                ej["group"] = static_cast<int>(eff.group);
                ej["magnitude"] = eff.magnitude;
                int64_t remaining = eff.expires_at_ms > 0
                    ? std::max(int64_t{0}, eff.expires_at_ms - eff.applied_at_ms)
                    : 0;
                ej["remaining_ms"] = remaining;
                if (eff.source_spell) {
                    ej["source_spell"] = eff.source_spell->value;
                }
                effects_arr.push_back(std::move(ej));
            }
        }
    }
    data["effects"] = effects_arr;

    // Appearance
    data["appearance"] = {
        {"hair_style", plr->hair_style},
        {"hair_color", plr->hair_color},
        {"skin_color", plr->skin_color},
        {"underwear_color", plr->underwear_color}
    };

    // Extended stats
    data["stat_points_available"] = plr->stats_pts.available;
    data["contribution"] = plr->experience.contribution;
    data["enemy_kill_count"] = plr->experience.enemy_kill_count;
    data["connection_id"] = plr->connection.value;
    data["is_in_combat"] = plr->target.is_valid();

    // Skills
    nlohmann::json skills_arr = nlohmann::json::array();
    if (skill_) {
        auto* ps = skill_->get_player_skills(plr->id);
        if (ps) {
            for (int i = 0; i < static_cast<int>(skill::skill_type::skill_count); ++i) {
                auto st = static_cast<skill::skill_type>(i);
                auto& s = ps->get(st);
                if (s.level > 0 || s.total_uses > 0) {
                    skills_arr.push_back({
                        {"type", i},
                        {"level", s.level},
                        {"total_uses", s.total_uses},
                        {"uses_this_level", s.uses_this_level}
                    });
                }
            }
        }
    }
    data["skills"] = skills_arr;

    // Spells
    nlohmann::json spells_arr = nlohmann::json::array();
    if (magic_) {
        auto* known = magic_->get_player_spells(plr->ecs_entity);
        if (known) {
            for (const auto& sk : *known) {
                nlohmann::json sj;
                sj["spell_id"] = sk.spell.value;
                sj["level"] = sk.level;
                sj["total_casts"] = sk.total_casts;
                auto* tmpl = magic_->get_spell(sk.spell);
                if (tmpl) sj["name"] = tmpl->name;
                spells_arr.push_back(std::move(sj));
            }
        }
    }
    data["spells"] = spells_arr;

    // Quests
    nlohmann::json active_quests_arr = nlohmann::json::array();
    int completed_quest_count = 0;
    if (quest_) {
        auto* journal = quest_->get_journal(plr->id);
        if (journal) {
            completed_quest_count = static_cast<int>(journal->completed_quests.size());
            for (const auto& qs : journal->active_quests) {
                nlohmann::json qj;
                qj["quest_id"] = qs.template_id.value;
                qj["status"] = static_cast<int>(qs.status);
                auto* qt = quest_->get_quest_template(qs.template_id);
                if (qt) qj["name"] = qt->name;
                nlohmann::json objs = nlohmann::json::array();
                for (const auto& obj : qs.objectives) {
                    objs.push_back({
                        {"id", obj.template_id},
                        {"current", obj.current_count},
                        {"required", obj.required_count},
                        {"complete", obj.is_complete()}
                    });
                }
                qj["objectives"] = objs;
                active_quests_arr.push_back(std::move(qj));
            }
        }
    }
    data["active_quests"] = active_quests_arr;
    data["completed_quest_count"] = completed_quest_count;

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
    audit_log(conn_id, "kick " + req.player_name + ": " + req.reason);

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
    audit_log(conn_id, "ban " + req.player_name + ": " + req.reason);

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
    audit_log(conn_id, "teleport " + req.player_name + " to " + req.dest_map);

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
    if (mods.contains("experience") && mods["experience"].is_number()) {
        plr->experience.experience = mods["experience"].get<int64_t>();
    }
    if (mods.contains("faction") && mods["faction"].is_number()) {
        plr->faction = static_cast<hb::faction>(mods["faction"].get<int>());
    }
    if (mods.contains("hunger") && mods["hunger"].is_number()) {
        plr->hunger.level = mods["hunger"].get<int8_t>();
    }
    if (mods.contains("stat_points") && mods["stat_points"].is_number()) {
        plr->stats_pts.available = mods["stat_points"].get<int16_t>();
    }
    if (mods.contains("contribution") && mods["contribution"].is_number()) {
        plr->experience.contribution = mods["contribution"].get<int32_t>();
    }
    if (mods.contains("enemy_kill_count") && mods["enemy_kill_count"].is_number()) {
        plr->experience.enemy_kill_count = mods["enemy_kill_count"].get<int32_t>();
    }
    if (mods.contains("hair_style") && mods["hair_style"].is_number()) {
        plr->hair_style = mods["hair_style"].get<int16_t>();
    }
    if (mods.contains("hair_color") && mods["hair_color"].is_number()) {
        plr->hair_color = mods["hair_color"].get<int16_t>();
    }
    if (mods.contains("skin_color") && mods["skin_color"].is_number()) {
        plr->skin_color = mods["skin_color"].get<int16_t>();
    }
    if (mods.contains("underwear_color") && mods["underwear_color"].is_number()) {
        plr->underwear_color = mods["underwear_color"].get<int16_t>();
    }

    LOG_INFO(admin, "Admin modified player '{}': {}", req.player_name, mods.dump());
    audit_log(conn_id, "modify_player " + req.player_name);

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
    audit_log(conn_id, "spawn_npc " + req.npc_name + " x" + std::to_string(spawned) + " on " + req.map_name);

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
    audit_log(conn_id, "kill_npc " + npc_name);

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

    // Bank slots
    nlohmann::json bank_arr = nlohmann::json::array();
    if (inventory_) {
        auto* bank = inventory_->get_bank(owner_id);
        if (bank) {
            for (int16_t i = 0; i < 200; ++i) {
                auto* slot = bank->get_slot(i);
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
                    bank_arr.push_back(entry);
                }
            }
        }
    }
    data["bank"] = bank_arr;

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
    audit_log(conn_id, "give_item " + item_name + " x" + std::to_string(req.count) + " to " + req.player_name);

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
    audit_log(conn_id, "remove_item " + item_name + " from " + req.player_name);

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
    audit_log(conn_id, "unban " + parse.value().player_name);

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

void admin_web_handlers::handle_get_map_data(connection_id conn_id, const network::json_message& msg)
{
    auto* conn = require_admin(conn_id, msg.seq);
    if (!conn) return;

    auto parse = network::admin_get_map_data_request_data::from_json(msg.data);
    if (parse.is_err())
    {
        send_error(conn_id, msg.seq, "parse_error", parse.error());
        return;
    }

    auto* m = world_->get_map_by_name(parse.value().map_name);
    if (!m)
    {
        ws_server_->send(conn_id, network::make_admin_response(
            network::json_message_type::admin_get_map_data_response,
            msg.seq, false, {}, "Map not found"));
        return;
    }

    auto& path = m->source_path();
    if (path.empty() || !std::filesystem::exists(path))
    {
        ws_server_->send(conn_id, network::make_admin_response(
            network::json_message_type::admin_get_map_data_response,
            msg.seq, false, {}, "Map file not available"));
        return;
    }

    std::ifstream file(path, std::ios::binary);
    if (!file)
    {
        ws_server_->send(conn_id, network::make_admin_response(
            network::json_message_type::admin_get_map_data_response,
            msg.seq, false, {}, "Failed to read map file"));
        return;
    }

    std::vector<uint8_t> data((std::istreambuf_iterator<char>(file)),
                               std::istreambuf_iterator<char>());

    auto encoded = base64_encode_standard(data.data(), data.size());

    nlohmann::json resp_data;
    resp_data["map_name"] = std::string(m->name());
    resp_data["width"] = m->width();
    resp_data["height"] = m->height();
    resp_data["data"] = std::move(encoded);

    ws_server_->send(conn_id, network::make_admin_response(
        network::json_message_type::admin_get_map_data_response,
        msg.seq, true, resp_data));
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

// === New Admin Endpoints ===

void admin_web_handlers::handle_broadcast(connection_id conn_id, const network::json_message& msg)
{
    if (!require_admin(conn_id, msg.seq)) return;

    auto parse = network::admin_broadcast_request_data::from_json(msg.data);
    if (parse.is_err()) {
        send_error(conn_id, msg.seq, "parse_error", parse.error());
        return;
    }
    auto& req = parse.value();

    if (req.message.empty()) {
        ws_server_->send(conn_id, network::make_admin_response(
            network::json_message_type::admin_broadcast_response,
            msg.seq, false, {}, "Message cannot be empty"));
        return;
    }

    // Broadcast as system message to all in-game connections
    auto broadcast_msg = network::make_chat_message_broadcast({
        .channel = "system",
        .sender_id = 0,
        .sender_name = "SYSTEM",
        .content = req.message,
        .flags = {"system"},
        .timestamp = ""
    });
    ws_server_->broadcast_to_authenticated(broadcast_msg);

    LOG_INFO(admin, "Admin broadcast: {}", req.message);
    audit_log(conn_id, "broadcast: " + req.message);

    ws_server_->send(conn_id, network::make_admin_response(
        network::json_message_type::admin_broadcast_response,
        msg.seq, true, {{"message", req.message}}));
}

void admin_web_handlers::handle_mute_player(connection_id conn_id, const network::json_message& msg)
{
    if (!require_admin(conn_id, msg.seq)) return;

    auto parse = network::admin_mute_player_request_data::from_json(msg.data);
    if (parse.is_err()) {
        send_error(conn_id, msg.seq, "parse_error", parse.error());
        return;
    }
    auto& req = parse.value();

    auto* plr = players_->get_player_by_name(req.player_name);
    if (!plr) {
        ws_server_->send(conn_id, network::make_admin_response(
            network::json_message_type::admin_mute_player_response,
            msg.seq, false, {}, "Player not found"));
        return;
    }

    if (admin_) {
        auto duration_secs = static_cast<int64_t>(req.duration_minutes) * 60;
        auto result = admin_->mute_player(plr->id, player_id{}, "Muted by admin panel", duration_secs);
        if (!result.success) {
            ws_server_->send(conn_id, network::make_admin_response(
                network::json_message_type::admin_mute_player_response,
                msg.seq, false, {}, result.message));
            return;
        }
    }

    LOG_INFO(admin, "Admin muted player '{}' for {} min", req.player_name, req.duration_minutes);
    audit_log(conn_id, "mute " + req.player_name + " " + std::to_string(req.duration_minutes) + "min");

    ws_server_->send(conn_id, network::make_admin_response(
        network::json_message_type::admin_mute_player_response,
        msg.seq, true, {
            {"player_name", req.player_name},
            {"duration_minutes", req.duration_minutes}
        }));
}

void admin_web_handlers::handle_unmute_player(connection_id conn_id, const network::json_message& msg)
{
    if (!require_admin(conn_id, msg.seq)) return;

    auto parse = network::admin_unmute_player_request_data::from_json(msg.data);
    if (parse.is_err()) {
        send_error(conn_id, msg.seq, "parse_error", parse.error());
        return;
    }
    auto& req = parse.value();

    auto* plr = players_->get_player_by_name(req.player_name);
    if (!plr) {
        ws_server_->send(conn_id, network::make_admin_response(
            network::json_message_type::admin_unmute_player_response,
            msg.seq, false, {}, "Player not found"));
        return;
    }

    if (admin_) {
        admin_->unmute_player(plr->id, player_id{});
    }

    LOG_INFO(admin, "Admin unmuted player '{}'", req.player_name);
    audit_log(conn_id, "unmute " + req.player_name);

    ws_server_->send(conn_id, network::make_admin_response(
        network::json_message_type::admin_unmute_player_response,
        msg.seq, true, {{"player_name", req.player_name}}));
}

void admin_web_handlers::handle_list_item_templates(connection_id conn_id, const network::json_message& msg)
{
    if (!require_admin(conn_id, msg.seq)) return;

    nlohmann::json items_arr = nlohmann::json::array();
    if (item_registry_) {
        for (const auto& tmpl : item_registry_->all()) {
            items_arr.push_back({
                {"id", tmpl.id.value},
                {"name", tmpl.name},
                {"type", static_cast<int>(tmpl.type)},
                {"equip_pos", static_cast<int>(tmpl.equip_pos)},
                {"level_limit", tmpl.level_limit},
                {"price", tmpl.price},
                {"weight", tmpl.weight}
            });
        }
    }

    nlohmann::json data;
    data["items"] = items_arr;
    data["count"] = items_arr.size();

    ws_server_->send(conn_id, network::make_admin_response(
        network::json_message_type::admin_list_item_templates_response,
        msg.seq, true, data));
}

void admin_web_handlers::handle_get_item_template(connection_id conn_id, const network::json_message& msg)
{
    if (!require_admin(conn_id, msg.seq)) return;

    auto parse = network::admin_get_item_template_request_data::from_json(msg.data);
    if (parse.is_err()) {
        send_error(conn_id, msg.seq, "parse_error", parse.error());
        return;
    }
    auto& req = parse.value();

    const item_template* tmpl = nullptr;
    if (item_registry_) {
        if (req.item_id > 0) {
            tmpl = item_registry_->get(item_id(req.item_id));
        } else if (!req.item_name.empty()) {
            tmpl = item_registry_->find_by_name(req.item_name);
        }
    }

    if (!tmpl) {
        ws_server_->send(conn_id, network::make_admin_response(
            network::json_message_type::admin_get_item_template_response,
            msg.seq, false, {}, "Item template not found"));
        return;
    }

    nlohmann::json data;
    data["id"] = tmpl->id.value;
    data["name"] = tmpl->name;
    data["type"] = static_cast<int>(tmpl->type);
    data["equip_pos"] = static_cast<int>(tmpl->equip_pos);
    data["category"] = static_cast<int>(tmpl->category);
    data["weight"] = tmpl->weight;
    data["price"] = tmpl->price;
    data["level_limit"] = tmpl->level_limit;
    data["attack_dice"] = tmpl->attack_dice;
    data["attack_sides"] = tmpl->attack_sides;
    data["attack_bonus"] = tmpl->attack_bonus;
    data["defense"] = tmpl->defense;
    data["hit_prob_bonus"] = tmpl->hit_prob_bonus;
    data["dodge_prob_bonus"] = tmpl->dodge_prob_bonus;
    data["magic_power"] = tmpl->magic_power;
    data["mana_cost"] = tmpl->mana_cost;
    data["max_durability"] = tmpl->max_durability;
    data["max_stack"] = tmpl->max_stack;
    data["hp_bonus"] = tmpl->hp_bonus;
    data["mp_bonus"] = tmpl->mp_bonus;
    data["sp_bonus"] = tmpl->sp_bonus;
    data["str_req"] = tmpl->str_req;
    data["dex_req"] = tmpl->dex_req;
    data["int_req"] = tmpl->int_req;
    data["mag_req"] = tmpl->mag_req;
    data["vit_req"] = tmpl->vit_req;
    data["cha_req"] = tmpl->cha_req;
    data["str_bonus"] = tmpl->str_bonus;
    data["dex_bonus"] = tmpl->dex_bonus;
    data["int_bonus"] = tmpl->int_bonus;
    data["mag_bonus"] = tmpl->mag_bonus;
    data["vit_bonus"] = tmpl->vit_bonus;
    data["cha_bonus"] = tmpl->cha_bonus;
    data["is_stackable"] = tmpl->is_stackable;
    data["is_tradeable"] = tmpl->is_tradeable;
    data["is_droppable"] = tmpl->is_droppable;
    data["is_consumable"] = tmpl->is_consumable;
    data["is_quest_item"] = tmpl->is_quest_item;
    data["sprite_id"] = tmpl->sprite_id;
    data["two_hand_modifier"] = tmpl->two_hand_modifier;

    ws_server_->send(conn_id, network::make_admin_response(
        network::json_message_type::admin_get_item_template_response,
        msg.seq, true, data));
}

void admin_web_handlers::handle_list_npc_templates(connection_id conn_id, const network::json_message& msg)
{
    if (!require_admin(conn_id, msg.seq)) return;

    nlohmann::json npcs_arr = nlohmann::json::array();
    if (npc_registry_) {
        for (const auto& tmpl : npc_registry_->all()) {
            npcs_arr.push_back({
                {"id", tmpl.id.value},
                {"name", tmpl.name},
                {"type", static_cast<int>(tmpl.type)},
                {"level", tmpl.level},
                {"hp", tmpl.hp},
                {"exp_reward", tmpl.exp_reward},
                {"is_boss", tmpl.is_boss},
                {"is_aggressive", tmpl.is_aggressive}
            });
        }
    }

    nlohmann::json data;
    data["npcs"] = npcs_arr;
    data["count"] = npcs_arr.size();

    ws_server_->send(conn_id, network::make_admin_response(
        network::json_message_type::admin_list_npc_templates_response,
        msg.seq, true, data));
}

void admin_web_handlers::handle_get_npc_template(connection_id conn_id, const network::json_message& msg)
{
    if (!require_admin(conn_id, msg.seq)) return;

    auto parse = network::admin_get_npc_template_request_data::from_json(msg.data);
    if (parse.is_err()) {
        send_error(conn_id, msg.seq, "parse_error", parse.error());
        return;
    }
    auto& req = parse.value();

    const npc_template* tmpl = nullptr;
    if (npc_registry_) {
        if (req.npc_id > 0) {
            tmpl = npc_registry_->get(npc_id(static_cast<uint16_t>(req.npc_id)));
        } else if (!req.npc_name.empty()) {
            tmpl = npc_registry_->find_by_name(req.npc_name);
        }
    }

    if (!tmpl) {
        ws_server_->send(conn_id, network::make_admin_response(
            network::json_message_type::admin_get_npc_template_response,
            msg.seq, false, {}, "NPC template not found"));
        return;
    }

    nlohmann::json data;
    data["id"] = tmpl->id.value;
    data["name"] = tmpl->name;
    data["type"] = static_cast<int>(tmpl->type);
    data["level"] = tmpl->level;
    data["hp"] = tmpl->hp;
    data["mp"] = tmpl->mp;
    data["attack_dice"] = tmpl->attack_dice;
    data["attack_sides"] = tmpl->attack_sides;
    data["attack_bonus"] = tmpl->attack_bonus;
    data["defense"] = tmpl->defense;
    data["hit_rate"] = tmpl->hit_rate;
    data["dodge_rate"] = tmpl->dodge_rate;
    data["magic_resist"] = tmpl->magic_resist;
    data["crit_chance"] = tmpl->crit_chance;
    data["move_speed"] = tmpl->move_speed;
    data["attack_speed"] = tmpl->attack_speed;
    data["attack_range"] = tmpl->attack_range;
    data["sight_range"] = tmpl->sight_range;
    data["action_time"] = tmpl->action_time;
    data["exp_reward"] = tmpl->exp_reward;
    data["gold_min"] = tmpl->gold_min;
    data["gold_max"] = tmpl->gold_max;
    data["is_aggressive"] = tmpl->is_aggressive;
    data["is_boss"] = tmpl->is_boss;
    data["is_undead"] = tmpl->is_undead;
    data["can_talk"] = tmpl->can_talk;
    data["is_merchant"] = tmpl->is_merchant;
    data["gives_quest"] = tmpl->gives_quest;
    data["respawns"] = tmpl->respawns;
    data["resist_physical"] = tmpl->resist_physical;
    data["resist_magic"] = tmpl->resist_magic;
    data["resist_fire"] = tmpl->resist_fire;
    data["resist_ice"] = tmpl->resist_ice;
    data["resist_lightning"] = tmpl->resist_lightning;

    ws_server_->send(conn_id, network::make_admin_response(
        network::json_message_type::admin_get_npc_template_response,
        msg.seq, true, data));
}

void admin_web_handlers::handle_get_war_status(connection_id conn_id, const network::json_message& msg)
{
    if (!require_admin(conn_id, msg.seq)) return;

    nlohmann::json wars_arr = nlohmann::json::array();
    if (war_) {
        auto active_ids = war_->get_all_active_wars();
        for (auto wid : active_ids) {
            auto* w = war_->get_war(wid);
            if (!w) continue;

            nlohmann::json wj;
            wj["id"] = wid.value;
            wj["type"] = static_cast<int>(w->type);
            wj["state"] = static_cast<int>(w->state);
            wj["phase"] = static_cast<int>(w->phase);
            wj["elapsed_seconds"] = w->elapsed_seconds;
            wj["participant_count"] = w->participants.size();

            // Scores
            wj["aresden_score"] = {
                {"kills", w->aresden_score.kills},
                {"deaths", w->aresden_score.deaths},
                {"objectives", w->aresden_score.objectives},
                {"total_score", w->aresden_score.total_score},
                {"participant_count", w->aresden_score.participant_count}
            };
            wj["elvine_score"] = {
                {"kills", w->elvine_score.kills},
                {"deaths", w->elvine_score.deaths},
                {"objectives", w->elvine_score.objectives},
                {"total_score", w->elvine_score.total_score},
                {"participant_count", w->elvine_score.participant_count}
            };

            wars_arr.push_back(std::move(wj));
        }
    }

    nlohmann::json data;
    data["wars"] = wars_arr;
    data["count"] = wars_arr.size();

    ws_server_->send(conn_id, network::make_admin_response(
        network::json_message_type::admin_get_war_status_response,
        msg.seq, true, data));
}

void admin_web_handlers::handle_list_parties(connection_id conn_id, const network::json_message& msg)
{
    if (!require_admin(conn_id, msg.seq)) return;

    nlohmann::json parties_arr = nlohmann::json::array();
    if (social_) {
        social_->for_each_party([&](social::party_id pid, const social::party& p) {
            nlohmann::json pj;
            pj["id"] = pid.value;
            pj["leader"] = p.leader.value;
            pj["loot_mode"] = static_cast<int>(p.loot);
            pj["exp_mode"] = static_cast<int>(p.experience);

            nlohmann::json members_arr = nlohmann::json::array();
            for (const auto& member : p.members) {
                std::string map_name = "unknown";
                if (world_) {
                    auto* m = world_->get_map(member.current_map);
                    if (m) map_name = std::string(m->name());
                }
                members_arr.push_back({
                    {"player_id", member.player.value},
                    {"name", member.name},
                    {"level", member.level},
                    {"map", map_name},
                    {"is_leader", member.player == p.leader}
                });
            }
            pj["members"] = members_arr;
            pj["member_count"] = p.member_count();

            parties_arr.push_back(std::move(pj));
        });
    }

    nlohmann::json data;
    data["parties"] = parties_arr;
    data["count"] = parties_arr.size();

    ws_server_->send(conn_id, network::make_admin_response(
        network::json_message_type::admin_list_parties_response,
        msg.seq, true, data));
}

void admin_web_handlers::handle_search_players(connection_id conn_id, const network::json_message& msg)
{
    if (!require_admin(conn_id, msg.seq)) return;

    auto parse = network::admin_search_players_request_data::from_json(msg.data);
    if (parse.is_err()) {
        send_error(conn_id, msg.seq, "parse_error", parse.error());
        return;
    }
    auto& req = parse.value();

    // Convert query to lowercase for case-insensitive search
    std::string query_lower = req.query;
    std::transform(query_lower.begin(), query_lower.end(), query_lower.begin(),
        [](unsigned char c) { return std::tolower(c); });

    // Prepare optional filter strings for case-insensitive comparison
    std::string map_filter_lower;
    if (!req.map_name.empty()) {
        map_filter_lower = req.map_name;
        std::transform(map_filter_lower.begin(), map_filter_lower.end(), map_filter_lower.begin(),
            [](unsigned char c) { return std::tolower(c); });
    }
    std::string guild_filter_lower;
    if (!req.guild_name.empty()) {
        guild_filter_lower = req.guild_name;
        std::transform(guild_filter_lower.begin(), guild_filter_lower.end(), guild_filter_lower.begin(),
            [](unsigned char c) { return std::tolower(c); });
    }

    nlohmann::json results_arr = nlohmann::json::array();
    if (players_) {
        players_->for_each_player([&](player_id pid, const player::player& plr) {
            // Name filter
            std::string name_lower = plr.name;
            std::transform(name_lower.begin(), name_lower.end(), name_lower.begin(),
                [](unsigned char c) { return std::tolower(c); });

            if (!query_lower.empty() && name_lower.find(query_lower) == std::string::npos) return;

            // Level range filter
            if (req.level_min && plr.experience.level < *req.level_min) return;
            if (req.level_max && plr.experience.level > *req.level_max) return;

            // Faction filter
            if (req.faction && static_cast<int>(plr.faction) != *req.faction) return;

            // Map filter
            std::string map_name = "unknown";
            if (world_) {
                auto* m = world_->get_map(plr.current_map);
                if (m) map_name = std::string(m->name());
            }
            if (!map_filter_lower.empty()) {
                std::string map_lower = map_name;
                std::transform(map_lower.begin(), map_lower.end(), map_lower.begin(),
                    [](unsigned char c) { return std::tolower(c); });
                if (map_lower.find(map_filter_lower) == std::string::npos) return;
            }

            // Guild filter
            if (!guild_filter_lower.empty()) {
                std::string guild_lower = plr.guild_name;
                std::transform(guild_lower.begin(), guild_lower.end(), guild_lower.begin(),
                    [](unsigned char c) { return std::tolower(c); });
                if (guild_lower.find(guild_filter_lower) == std::string::npos) return;
            }

            results_arr.push_back({
                {"id", pid.value},
                {"name", plr.name},
                {"level", plr.experience.level},
                {"map", map_name},
                {"faction", static_cast<int>(plr.faction)},
                {"guild", plr.guild_name}
            });
        });
    }

    nlohmann::json data;
    data["players"] = results_arr;
    data["count"] = results_arr.size();
    data["query"] = req.query;

    ws_server_->send(conn_id, network::make_admin_response(
        network::json_message_type::admin_search_players_response,
        msg.seq, true, data));
}

// === Phase 3: Audit Log, Config, Scheduler, Query, NPC, Ground Items, Guild, Message, Environment, Shutdown ===

void admin_web_handlers::handle_get_audit_log(connection_id conn_id, const network::json_message& msg)
{
    if (!require_admin(conn_id, msg.seq)) return;

    auto parse = network::admin_get_audit_log_request_data::from_json(msg.data);
    if (parse.is_err()) {
        send_error(conn_id, msg.seq, "parse_error", parse.error());
        return;
    }
    auto& req = parse.value();

    if (!admin_) {
        ws_server_->send(conn_id, network::make_admin_response(
            network::json_message_type::admin_get_audit_log_response,
            msg.seq, false, {}, "Admin system not available"));
        return;
    }

    auto entries = admin_->get_log_entries(req.count);

    nlohmann::json entries_arr = nlohmann::json::array();
    for (const auto& e : entries) {
        // Filter by executor_name if specified
        if (!req.executor_name.empty() && e.executor_name != req.executor_name) continue;

        entries_arr.push_back({
            {"timestamp", e.timestamp},
            {"executor", e.executor.value},
            {"executor_name", e.executor_name},
            {"executor_level", static_cast<int>(e.executor_level)},
            {"command", e.command_name},
            {"full_command", e.full_command},
            {"success", e.success},
            {"result", e.result_message}
        });
    }

    nlohmann::json data;
    data["entries"] = entries_arr;
    data["count"] = entries_arr.size();

    ws_server_->send(conn_id, network::make_admin_response(
        network::json_message_type::admin_get_audit_log_response,
        msg.seq, true, data));
}

void admin_web_handlers::handle_get_config(connection_id conn_id, const network::json_message& msg)
{
    if (!require_admin(conn_id, msg.seq)) return;

    if (!config_) {
        ws_server_->send(conn_id, network::make_admin_response(
            network::json_message_type::admin_get_config_response,
            msg.seq, false, {}, "Config system not available"));
        return;
    }

    auto data = config_->server().to_json_sanitized();

    ws_server_->send(conn_id, network::make_admin_response(
        network::json_message_type::admin_get_config_response,
        msg.seq, true, data));
}

void admin_web_handlers::handle_set_config(connection_id conn_id, const network::json_message& msg)
{
    if (!require_admin(conn_id, msg.seq, 20)) return;

    auto parse = network::admin_set_config_request_data::from_json(msg.data);
    if (parse.is_err()) {
        send_error(conn_id, msg.seq, "parse_error", parse.error());
        return;
    }
    auto& req = parse.value();

    if (!config_) {
        ws_server_->send(conn_id, network::make_admin_response(
            network::json_message_type::admin_set_config_response,
            msg.seq, false, {}, "Config system not available"));
        return;
    }

    auto config_path = config_->server_config_path();
    if (config_path.empty()) {
        ws_server_->send(conn_id, network::make_admin_response(
            network::json_message_type::admin_set_config_response,
            msg.seq, false, {}, "No config file path set"));
        return;
    }

    // Load current config from disk to avoid clobbering
    auto load_result = server_config::load_from_json(config_path);
    if (load_result.is_err()) {
        ws_server_->send(conn_id, network::make_admin_response(
            network::json_message_type::admin_set_config_response,
            msg.seq, false, {}, "Failed to load config: " + load_result.error()));
        return;
    }

    auto cfg = std::move(load_result.value());
    auto apply_result = cfg.apply_dot_values(req.values);
    if (apply_result.is_err()) {
        ws_server_->send(conn_id, network::make_admin_response(
            network::json_message_type::admin_set_config_response,
            msg.seq, false, {}, "Failed to apply values: " + apply_result.error()));
        return;
    }

    auto& applied = apply_result.value();

    // Determine which applied keys were skipped (sentinel "***") and which need restart
    nlohmann::json skipped_arr = nlohmann::json::array();
    nlohmann::json restart_arr = nlohmann::json::array();
    for (const auto& [key, val] : req.values.items()) {
        if (val.is_string() && val.get<std::string>() == "***") {
            skipped_arr.push_back(key);
        }
    }
    for (const auto& key : applied) {
        if (server_config::requires_restart(key)) {
            restart_arr.push_back(key);
        }
    }

    // Save back to disk
    auto save_result = cfg.save_to_json(config_path);
    if (save_result.is_err()) {
        ws_server_->send(conn_id, network::make_admin_response(
            network::json_message_type::admin_set_config_response,
            msg.seq, false, {}, "Failed to save config: " + save_result.error()));
        return;
    }

    LOG_INFO(admin, "Admin updated config: {} keys applied", applied.size());
    audit_log(conn_id, "set_config " + std::to_string(applied.size()) + " keys");

    nlohmann::json data;
    data["applied"] = applied;
    data["skipped"] = skipped_arr;
    data["restart_required_for"] = restart_arr;

    ws_server_->send(conn_id, network::make_admin_response(
        network::json_message_type::admin_set_config_response,
        msg.seq, true, data));
}

void admin_web_handlers::handle_reload_config(connection_id conn_id, const network::json_message& msg)
{
    if (!require_admin(conn_id, msg.seq, 20)) return;

    if (!config_) {
        ws_server_->send(conn_id, network::make_admin_response(
            network::json_message_type::admin_reload_config_response,
            msg.seq, false, {}, "Config system not available"));
        return;
    }

    auto config_path = config_->server_config_path();
    if (config_path.empty()) {
        ws_server_->send(conn_id, network::make_admin_response(
            network::json_message_type::admin_reload_config_response,
            msg.seq, false, {}, "No config file path set"));
        return;
    }

    auto load_result = config_->load_server_config(config_path);
    if (load_result.is_err()) {
        ws_server_->send(conn_id, network::make_admin_response(
            network::json_message_type::admin_reload_config_response,
            msg.seq, false, {}, "Failed to reload: " + load_result.error()));
        return;
    }

    // Determine what was hot-applied vs needs restart
    nlohmann::json hot_applied = nlohmann::json::array();
    nlohmann::json restart_required = nlohmann::json::array();

    // Hot-reload logging levels
    auto& server_cfg = config_->server();
    hot_applied.push_back("logging.console_level");
    hot_applied.push_back("logging.file_level");

    // Hot-reload auto-save interval
    hot_applied.push_back("auto_save.interval_seconds");
    hot_applied.push_back("auto_save.enabled");

    // These always require restart
    restart_required.push_back("database.*");
    restart_required.push_back("websocket.*");
    restart_required.push_back("enable_legacy_protocol");
    restart_required.push_back("legacy_port");

    LOG_INFO(admin, "Admin reloaded config from disk");
    audit_log(conn_id, "reload_config");

    nlohmann::json data;
    data["reloaded"] = true;
    data["hot_applied"] = hot_applied;
    data["restart_required_for"] = restart_required;

    ws_server_->send(conn_id, network::make_admin_response(
        network::json_message_type::admin_reload_config_response,
        msg.seq, true, data));
}

void admin_web_handlers::handle_list_scheduled_tasks(connection_id conn_id, const network::json_message& msg)
{
    if (!require_admin(conn_id, msg.seq)) return;

    nlohmann::json tasks_arr = nlohmann::json::array();
    if (scheduler_) {
        scheduler_->for_each_task([&](const scheduler::task_info& info) {
            tasks_arr.push_back({
                {"id", info.id},
                {"tag", info.tag},
                {"next_fire_ms", info.next_fire_ms},
                {"interval_ms", info.interval_ms},
                {"repeating", info.repeating}
            });
        });
    }

    nlohmann::json defs_arr = nlohmann::json::array();
    if (scheduler_) {
        scheduler_->for_each_definition([&](const scheduler::task_definition& def, bool running) {
            defs_arr.push_back({
                {"tag", def.tag},
                {"description", def.description},
                {"default_interval_ms", def.default_interval_ms},
                {"repeating", def.repeating},
                {"running", running}
            });
        });
    }

    nlohmann::json data;
    data["tasks"] = tasks_arr;
    data["count"] = tasks_arr.size();
    data["definitions"] = defs_arr;

    ws_server_->send(conn_id, network::make_admin_response(
        network::json_message_type::admin_list_scheduled_tasks_response,
        msg.seq, true, data));
}

void admin_web_handlers::handle_cancel_scheduled_task(connection_id conn_id, const network::json_message& msg)
{
    if (!require_admin(conn_id, msg.seq, 20)) return;

    auto parse = network::admin_cancel_scheduled_task_request_data::from_json(msg.data);
    if (parse.is_err()) {
        send_error(conn_id, msg.seq, "parse_error", parse.error());
        return;
    }
    auto& req = parse.value();

    if (!scheduler_) {
        ws_server_->send(conn_id, network::make_admin_response(
            network::json_message_type::admin_cancel_scheduled_task_response,
            msg.seq, false, {}, "Scheduler not available"));
        return;
    }

    scheduler_->cancel_tagged(req.tag);

    LOG_INFO(admin, "Admin cancelled scheduled tasks with tag '{}'", req.tag);
    audit_log(conn_id, "cancel_task " + req.tag);

    nlohmann::json data;
    data["tag"] = req.tag;
    data["cancelled"] = true;

    ws_server_->send(conn_id, network::make_admin_response(
        network::json_message_type::admin_cancel_scheduled_task_response,
        msg.seq, true, data));
}

void admin_web_handlers::handle_start_task(connection_id conn_id, const network::json_message& msg)
{
    if (!require_admin(conn_id, msg.seq, 20)) return;

    auto parse = network::admin_start_task_request_data::from_json(msg.data);
    if (parse.is_err()) {
        send_error(conn_id, msg.seq, "parse_error", parse.error());
        return;
    }
    auto& req = parse.value();

    if (!scheduler_) {
        ws_server_->send(conn_id, network::make_admin_response(
            network::json_message_type::admin_start_task_response,
            msg.seq, false, {}, "Scheduler not available"));
        return;
    }

    if (scheduler_->is_task_running(req.tag)) {
        ws_server_->send(conn_id, network::make_admin_response(
            network::json_message_type::admin_start_task_response,
            msg.seq, false, {}, "Task '" + req.tag + "' is already running"));
        return;
    }

    std::optional<duration_ms> interval;
    if (req.interval_ms.has_value()) {
        interval = duration_ms{req.interval_ms.value()};
    }

    auto id = scheduler_->start_task(req.tag, interval);
    bool started = id.is_valid();

    if (!started) {
        ws_server_->send(conn_id, network::make_admin_response(
            network::json_message_type::admin_start_task_response,
            msg.seq, false, {}, "Unknown task tag '" + req.tag + "'"));
        return;
    }

    // Determine actual interval used
    int64_t actual_interval = 0;
    if (req.interval_ms.has_value()) {
        actual_interval = req.interval_ms.value();
    } else {
        scheduler_->for_each_definition([&](const scheduler::task_definition& def, bool) {
            if (def.tag == req.tag) {
                actual_interval = def.default_interval_ms;
            }
        });
    }

    LOG_INFO(admin, "Admin started task '{}' (interval: {}ms)", req.tag, actual_interval);
    audit_log(conn_id, "start_task " + req.tag);

    nlohmann::json data;
    data["tag"] = req.tag;
    data["started"] = true;
    data["interval_ms"] = actual_interval;

    ws_server_->send(conn_id, network::make_admin_response(
        network::json_message_type::admin_start_task_response,
        msg.seq, true, data));
}

void admin_web_handlers::handle_run_query(connection_id conn_id, const network::json_message& msg)
{
    if (!require_admin(conn_id, msg.seq, 20)) return;

    auto parse = network::admin_run_query_request_data::from_json(msg.data);
    if (parse.is_err()) {
        send_error(conn_id, msg.seq, "parse_error", parse.error());
        return;
    }
    auto& req = parse.value();

    if (!db_) {
        ws_server_->send(conn_id, network::make_admin_response(
            network::json_message_type::admin_run_query_response,
            msg.seq, false, {}, "Database not available"));
        return;
    }

    int limit = 50;
    if (req.params.contains("limit") && req.params["limit"].is_number()) {
        limit = std::clamp(req.params["limit"].get<int>(), 1, 500);
    }

    // Canned queries — no raw SQL
    hb::result<database::query_result, std::string> query_res =
        hb::result<database::query_result, std::string>::err("Unknown query: " + req.query_name);

    if (req.query_name == "top_players_by_level") {
        query_res = db_->execute_params(
            "SELECT id, name, level, nation, map_name FROM characters ORDER BY level DESC LIMIT $1",
            limit);
    } else if (req.query_name == "top_players_by_gold") {
        query_res = db_->execute_params(
            "SELECT id, name, level, gold FROM characters ORDER BY gold DESC LIMIT $1",
            limit);
    } else if (req.query_name == "recent_logins") {
        query_res = db_->execute_params(
            "SELECT id, username, last_login FROM accounts ORDER BY last_login DESC NULLS LAST LIMIT $1",
            limit);
    } else if (req.query_name == "account_search") {
        std::string query_str = "%";
        if (req.params.contains("query") && req.params["query"].is_string()) {
            query_str = "%" + req.params["query"].get<std::string>() + "%";
        }
        query_res = db_->execute_params(
            "SELECT id, username, created_at, last_login, banned FROM accounts WHERE username ILIKE $1 LIMIT $2",
            query_str, limit);
    } else if (req.query_name == "character_search") {
        std::string query_str = "%";
        if (req.params.contains("query") && req.params["query"].is_string()) {
            query_str = "%" + req.params["query"].get<std::string>() + "%";
        }
        query_res = db_->execute_params(
            "SELECT id, name, level, nation, map_name FROM characters WHERE name ILIKE $1 LIMIT $2",
            query_str, limit);
    } else if (req.query_name == "ban_list") {
        query_res = db_->execute_params(
            "SELECT id, username, ban_reason FROM accounts WHERE banned = true LIMIT $1",
            limit);
    } else if (req.query_name == "guild_rankings") {
        query_res = db_->execute_params(
            "SELECT id, name, tag, member_count FROM guilds ORDER BY member_count DESC LIMIT $1",
            limit);
    } else if (req.query_name == "faction_distribution") {
        query_res = db_->execute_unsafe(
            "SELECT nation, COUNT(*) as count FROM characters GROUP BY nation ORDER BY count DESC");
    } else if (req.query_name == "recent_characters") {
        query_res = db_->execute_params(
            "SELECT id, name, level, nation, created_at FROM characters ORDER BY created_at DESC LIMIT $1",
            limit);
    }

    if (query_res.is_err()) {
        ws_server_->send(conn_id, network::make_admin_response(
            network::json_message_type::admin_run_query_response,
            msg.seq, false, {}, query_res.error()));
        return;
    }

    auto& result = query_res.value();

    // Build column names from first row
    nlohmann::json columns_arr = nlohmann::json::array();
    nlohmann::json rows_arr = nlohmann::json::array();

    if (!result.empty()) {
        auto first_row = result[0];
        for (size_t c = 0; c < first_row.size(); ++c) {
            columns_arr.push_back(first_row[c].name());
        }

        for (size_t r = 0; r < result.size(); ++r) {
            nlohmann::json row = nlohmann::json::array();
            auto pqxx_row = result[r];
            for (size_t c = 0; c < pqxx_row.size(); ++c) {
                if (pqxx_row[c].is_null()) {
                    row.push_back(nullptr);
                } else {
                    row.push_back(std::string(pqxx_row[c].c_str()));
                }
            }
            rows_arr.push_back(std::move(row));
        }
    }

    nlohmann::json data;
    data["query_name"] = req.query_name;
    data["columns"] = columns_arr;
    data["rows"] = rows_arr;
    data["row_count"] = rows_arr.size();

    ws_server_->send(conn_id, network::make_admin_response(
        network::json_message_type::admin_run_query_response,
        msg.seq, true, data));
}

void admin_web_handlers::handle_list_map_npcs(connection_id conn_id, const network::json_message& msg)
{
    if (!require_admin(conn_id, msg.seq)) return;

    auto parse = network::admin_list_map_npcs_request_data::from_json(msg.data);
    if (parse.is_err()) {
        send_error(conn_id, msg.seq, "parse_error", parse.error());
        return;
    }

    auto* m = world_ ? world_->get_map_by_name(parse.value().map_name) : nullptr;
    if (!m) {
        ws_server_->send(conn_id, network::make_admin_response(
            network::json_message_type::admin_list_map_npcs_response,
            msg.seq, false, {}, "Map not found"));
        return;
    }

    nlohmann::json npcs_arr = nlohmann::json::array();
    if (npc_) {
        npc_->for_each_npc_on_map(m->id(), [&](entity::entity eid, const npc::npc& n) {
            npcs_arr.push_back({
                {"entity_id", eid.id},
                {"template_id", n.template_id.value},
                {"name", n.name},
                {"level", n.level},
                {"hp", n.hp},
                {"max_hp", n.max_hp},
                {"x", n.pos.x},
                {"y", n.pos.y},
                {"category", static_cast<int>(n.category)},
                {"is_alive", n.is_alive()},
                {"ai_state", static_cast<int>(n.ai_state.state)},
                {"facing", static_cast<int>(n.facing)}
            });
        });
    }

    nlohmann::json data;
    data["npcs"] = npcs_arr;
    data["count"] = npcs_arr.size();
    data["map_name"] = parse.value().map_name;

    ws_server_->send(conn_id, network::make_admin_response(
        network::json_message_type::admin_list_map_npcs_response,
        msg.seq, true, data));
}

void admin_web_handlers::handle_list_map_ground_items(connection_id conn_id, const network::json_message& msg)
{
    if (!require_admin(conn_id, msg.seq)) return;

    auto parse = network::admin_list_map_ground_items_request_data::from_json(msg.data);
    if (parse.is_err()) {
        send_error(conn_id, msg.seq, "parse_error", parse.error());
        return;
    }

    auto* m = world_ ? world_->get_map_by_name(parse.value().map_name) : nullptr;
    if (!m) {
        ws_server_->send(conn_id, network::make_admin_response(
            network::json_message_type::admin_list_map_ground_items_response,
            msg.seq, false, {}, "Map not found"));
        return;
    }

    auto now = std::chrono::steady_clock::now();
    nlohmann::json items_arr = nlohmann::json::array();
    world_->for_each_ground_item_on_map(m->id(),
        [&](const world::position& pos, item_id iid, std::chrono::steady_clock::time_point drop_time) {
            std::string item_name = "Item#" + std::to_string(iid.value);
            uint32_t template_id = 0;
            if (item_registry_) {
                auto* tmpl = item_registry_->get(iid);
                if (tmpl) {
                    item_name = tmpl->name;
                    template_id = tmpl->id.value;
                }
            }

            auto age_seconds = std::chrono::duration_cast<std::chrono::seconds>(now - drop_time).count();
            items_arr.push_back({
                {"x", pos.x},
                {"y", pos.y},
                {"item_id", iid.value},
                {"template_id", template_id},
                {"name", item_name},
                {"age_seconds", age_seconds}
            });
        });

    nlohmann::json data;
    data["items"] = items_arr;
    data["count"] = items_arr.size();
    data["map_name"] = parse.value().map_name;

    ws_server_->send(conn_id, network::make_admin_response(
        network::json_message_type::admin_list_map_ground_items_response,
        msg.seq, true, data));
}

void admin_web_handlers::handle_remove_ground_item(connection_id conn_id, const network::json_message& msg)
{
    if (!require_admin(conn_id, msg.seq)) return;

    auto parse = network::admin_remove_ground_item_request_data::from_json(msg.data);
    if (parse.is_err()) {
        send_error(conn_id, msg.seq, "parse_error", parse.error());
        return;
    }
    auto& req = parse.value();

    auto* m = world_ ? world_->get_map_by_name(req.map_name) : nullptr;
    if (!m) {
        ws_server_->send(conn_id, network::make_admin_response(
            network::json_message_type::admin_remove_ground_item_response,
            msg.seq, false, {}, "Map not found"));
        return;
    }

    world::position pos{req.x, req.y};
    bool removed = world_->remove_ground_item(m->id(), pos, item_id{req.item_id});
    if (!removed) {
        ws_server_->send(conn_id, network::make_admin_response(
            network::json_message_type::admin_remove_ground_item_response,
            msg.seq, false, {}, "Item not found at position"));
        return;
    }

    // Broadcast ground_item_removed to nearby players
    if (players_) {
        auto nearby = players_->get_players_on_map_in_range(m->id(), pos, 20);
        auto remove_msg = network::json_message{
            .type = network::json_message_type::ground_item_removed,
            .seq = 0,
            .data = {{"x", pos.x}, {"y", pos.y}, {"item_id", req.item_id}}
        };
        for (auto pid : nearby) {
            auto* plr = players_->get_player(pid);
            if (plr && plr->connection.is_valid()) {
                ws_server_->send(plr->connection, remove_msg);
            }
        }
    }

    LOG_INFO(admin, "Admin removed ground item {} at ({},{}) on {}",
        req.item_id, req.x, req.y, req.map_name);
    audit_log(conn_id, "remove_ground_item " + std::to_string(req.item_id) + " on " + req.map_name);

    ws_server_->send(conn_id, network::make_admin_response(
        network::json_message_type::admin_remove_ground_item_response,
        msg.seq, true, {{"removed", true}}));
}

void admin_web_handlers::handle_guild_action(connection_id conn_id, const network::json_message& msg)
{
    if (!require_admin(conn_id, msg.seq, 20)) return;

    auto parse = network::admin_guild_action_request_data::from_json(msg.data);
    if (parse.is_err()) {
        send_error(conn_id, msg.seq, "parse_error", parse.error());
        return;
    }
    auto& req = parse.value();

    if (!social_) {
        ws_server_->send(conn_id, network::make_admin_response(
            network::json_message_type::admin_guild_action_response,
            msg.seq, false, {}, "Social system not available"));
        return;
    }

    auto gid = social_->find_guild_by_name(req.guild_name);
    if (!gid.is_valid()) {
        ws_server_->send(conn_id, network::make_admin_response(
            network::json_message_type::admin_guild_action_response,
            msg.seq, false, {}, "Guild not found"));
        return;
    }

    social::guild_result result;
    if (req.action == "disband") {
        result = social_->admin_disband_guild(gid);
    } else if (req.action == "kick") {
        if (req.target_player.empty()) {
            ws_server_->send(conn_id, network::make_admin_response(
                network::json_message_type::admin_guild_action_response,
                msg.seq, false, {}, "target_player required for kick"));
            return;
        }
        result = social_->admin_kick_from_guild(gid, req.target_player);
    } else if (req.action == "set_rank") {
        if (req.target_player.empty()) {
            ws_server_->send(conn_id, network::make_admin_response(
                network::json_message_type::admin_guild_action_response,
                msg.seq, false, {}, "target_player required for set_rank"));
            return;
        }
        // Map rank string to enum
        social::guild_rank rank = social::guild_rank::member;
        if (req.rank == "guild_master") rank = social::guild_rank::guild_master;
        else if (req.rank == "officer") rank = social::guild_rank::officer;
        else if (req.rank == "veteran") rank = social::guild_rank::veteran;
        else if (req.rank == "member") rank = social::guild_rank::member;
        else if (req.rank == "recruit") rank = social::guild_rank::recruit;

        result = social_->admin_set_member_rank(gid, req.target_player, rank);
    } else {
        ws_server_->send(conn_id, network::make_admin_response(
            network::json_message_type::admin_guild_action_response,
            msg.seq, false, {}, "Unknown action: " + req.action));
        return;
    }

    if (result != social::guild_result::success) {
        ws_server_->send(conn_id, network::make_admin_response(
            network::json_message_type::admin_guild_action_response,
            msg.seq, false, {}, "Guild action failed (code " + std::to_string(static_cast<int>(result)) + ")"));
        return;
    }

    LOG_INFO(admin, "Admin guild action '{}' on '{}' target '{}'",
        req.action, req.guild_name, req.target_player);
    audit_log(conn_id, "guild_action " + req.action + " " + req.guild_name + " " + req.target_player);

    nlohmann::json data;
    data["action"] = req.action;
    data["guild_name"] = req.guild_name;
    if (!req.target_player.empty()) {
        data["target_player"] = req.target_player;
    }

    ws_server_->send(conn_id, network::make_admin_response(
        network::json_message_type::admin_guild_action_response,
        msg.seq, true, data));
}

void admin_web_handlers::handle_message_player(connection_id conn_id, const network::json_message& msg)
{
    if (!require_admin(conn_id, msg.seq)) return;

    auto parse = network::admin_message_player_request_data::from_json(msg.data);
    if (parse.is_err()) {
        send_error(conn_id, msg.seq, "parse_error", parse.error());
        return;
    }
    auto& req = parse.value();

    auto* plr = players_ ? players_->get_player_by_name(req.player_name) : nullptr;
    if (!plr) {
        ws_server_->send(conn_id, network::make_admin_response(
            network::json_message_type::admin_message_player_response,
            msg.seq, false, {}, "Player not found"));
        return;
    }

    if (!plr->connection.is_valid()) {
        ws_server_->send(conn_id, network::make_admin_response(
            network::json_message_type::admin_message_player_response,
            msg.seq, false, {}, "Player has no active connection"));
        return;
    }

    auto chat_msg = network::make_chat_message_broadcast({
        .channel = "system",
        .sender_id = 0,
        .sender_name = "SYSTEM",
        .content = req.message,
        .flags = {"system"},
        .timestamp = ""
    });
    ws_server_->send(plr->connection, chat_msg);

    LOG_INFO(admin, "Admin sent message to '{}': {}", req.player_name, req.message);
    audit_log(conn_id, "message " + req.player_name);

    ws_server_->send(conn_id, network::make_admin_response(
        network::json_message_type::admin_message_player_response,
        msg.seq, true, {
            {"player_name", req.player_name},
            {"delivered", true}
        }));
}

void admin_web_handlers::handle_set_environment(connection_id conn_id, const network::json_message& msg)
{
    if (!require_admin(conn_id, msg.seq)) return;

    auto parse = network::admin_set_environment_request_data::from_json(msg.data);
    if (parse.is_err()) {
        send_error(conn_id, msg.seq, "parse_error", parse.error());
        return;
    }
    auto& req = parse.value();

    bool time_set = false;
    bool weather_set = false;
    int maps_affected = 0;

    // Set time globally
    if (req.hour.has_value() && scheduler_) {
        auto& clock = scheduler_->game_time();
        clock.set_time(*req.hour, req.minute.value_or(0));
        time_set = true;
    }

    // Set weather on specific map or all maps
    if (req.weather.has_value() && world_) {
        if (!req.map_name.empty()) {
            auto* m = world_->get_map_by_name(req.map_name);
            if (m) {
                if (*req.weather == 0) {
                    m->clear_weather();
                } else {
                    auto now = std::chrono::steady_clock::now();
                    m->start_weather(static_cast<world::weather_type>(*req.weather),
                        now + std::chrono::hours(1));
                }
                ++maps_affected;
                weather_set = true;
            }
        } else {
            world_->for_each_map([&](map_id, world::map& m) {
                if (*req.weather == 0) {
                    m.clear_weather();
                } else {
                    auto now = std::chrono::steady_clock::now();
                    m.start_weather(static_cast<world::weather_type>(*req.weather),
                        now + std::chrono::hours(1));
                }
                ++maps_affected;
            });
            weather_set = true;
        }
    }

    // Broadcast environment update to all players
    if ((time_set || weather_set) && scheduler_ && world_ && players_) {
        auto& clock = scheduler_->game_time();
        players_->for_each_player([&](player_id, const player::player& plr) {
            auto* m = world_->get_map(plr.current_map);
            if (!m) return;
            network::json_message env_msg{
                .type = network::json_message_type::environment_update,
                .seq = 0,
                .data = {
                    {"hour", clock.hour()},
                    {"minute", clock.minute()},
                    {"is_day", clock.is_day()},
                    {"weather", static_cast<int>(m->weather())}
                }
            };
            if (plr.connection.is_valid()) {
                ws_server_->send(plr.connection, env_msg);
            }
        });
    }

    LOG_INFO(admin, "Admin set environment: time_set={}, weather_set={}, maps={}",
        time_set, weather_set, maps_affected);
    audit_log(conn_id, "set_environment");

    ws_server_->send(conn_id, network::make_admin_response(
        network::json_message_type::admin_set_environment_response,
        msg.seq, true, {
            {"time_set", time_set},
            {"weather_set", weather_set},
            {"maps_affected", maps_affected}
        }));
}

void admin_web_handlers::handle_shutdown_server(connection_id conn_id, const network::json_message& msg)
{
    if (!require_admin(conn_id, msg.seq, 20)) return;

    auto parse = network::admin_shutdown_server_request_data::from_json(msg.data);
    if (parse.is_err()) {
        send_error(conn_id, msg.seq, "parse_error", parse.error());
        return;
    }
    auto& req = parse.value();

    // Handle cancel
    if (req.cancel) {
        if (scheduler_) {
            scheduler_->cancel_tagged("shutdown_countdown");
        }
        LOG_INFO(admin, "Admin cancelled shutdown countdown");
        audit_log(conn_id, "cancel_shutdown");
        ws_server_->send(conn_id, network::make_admin_response(
            network::json_message_type::admin_shutdown_server_response,
            msg.seq, true, {{"cancelled", true}}));
        return;
    }

    std::string reason = req.reason.empty() ? "Server shutdown by admin" : req.reason;

    if (req.countdown_seconds <= 0) {
        // Immediate shutdown
        LOG_INFO(admin, "Admin initiated immediate shutdown: {}", reason);
        audit_log(conn_id, "shutdown_immediate: " + reason);

        // Notify all players
        auto broadcast_msg = network::make_chat_message_broadcast({
            .channel = "system",
            .sender_id = 0,
            .sender_name = "SYSTEM",
            .content = "Server is shutting down: " + reason,
            .flags = {"system"},
            .timestamp = ""
        });
        ws_server_->broadcast_to_authenticated(broadcast_msg);

        ws_server_->send(conn_id, network::make_admin_response(
            network::json_message_type::admin_shutdown_server_response,
            msg.seq, true, {
                {"countdown_seconds", 0},
                {"reason", reason}
            }));

        application::instance().request_shutdown(reason);
    } else {
        // Countdown shutdown
        int countdown = req.countdown_seconds;

        // Cancel any existing countdown
        if (scheduler_) {
            scheduler_->cancel_tagged("shutdown_countdown");
        }

        LOG_INFO(admin, "Admin initiated shutdown countdown: {}s - {}", countdown, reason);
        audit_log(conn_id, "shutdown_countdown " + std::to_string(countdown) + "s: " + reason);

        // Schedule warning messages at various marks
        auto schedule_warning = [&](int seconds_remaining) {
            if (seconds_remaining > countdown) return;
            int delay = (countdown - seconds_remaining) * 1000;
            std::string warn_msg;
            if (seconds_remaining >= 60) {
                warn_msg = "Server shutting down in " + std::to_string(seconds_remaining / 60) +
                           " minute(s): " + reason;
            } else {
                warn_msg = "Server shutting down in " + std::to_string(seconds_remaining) +
                           " seconds: " + reason;
            }
            auto* ws = ws_server_;
            scheduler_->schedule_tagged(duration_ms{delay}, "shutdown_countdown",
                [ws, warn_msg]() {
                    auto broadcast = network::make_chat_message_broadcast({
                        .channel = "system",
                        .sender_id = 0,
                        .sender_name = "SYSTEM",
                        .content = warn_msg,
                        .flags = {"system"},
                        .timestamp = ""
                    });
                    ws->broadcast_to_authenticated(broadcast);
                });
        };

        schedule_warning(300);  // 5 min
        schedule_warning(180);  // 3 min
        schedule_warning(60);   // 1 min
        schedule_warning(30);   // 30s
        schedule_warning(10);   // 10s

        // Schedule actual shutdown
        auto shutdown_reason = reason;
        scheduler_->schedule_tagged(duration_ms{countdown * 1000}, "shutdown_countdown",
            [shutdown_reason]() {
                application::instance().request_shutdown(shutdown_reason);
            });

        ws_server_->send(conn_id, network::make_admin_response(
            network::json_message_type::admin_shutdown_server_response,
            msg.seq, true, {
                {"countdown_seconds", countdown},
                {"reason", reason}
            }));
    }
}

// === Phase 4: Skills, Spells, Quests, Effects, Account, Spawn Points, Spell Templates, Maintenance, Character, IP Bans ===

void admin_web_handlers::handle_modify_skills(connection_id conn_id, const network::json_message& msg)
{
    if (!require_admin(conn_id, msg.seq, 20)) return;

    auto parse = network::admin_modify_skills_request_data::from_json(msg.data);
    if (parse.is_err()) {
        send_error(conn_id, msg.seq, "parse_error", parse.error());
        return;
    }
    auto& req = parse.value();

    auto* plr = players_->get_player_by_name(req.player_name);
    if (!plr) {
        ws_server_->send(conn_id, network::make_admin_response(
            network::json_message_type::admin_modify_skills_response,
            msg.seq, false, {}, "Player not found"));
        return;
    }
    if (!skill_) {
        ws_server_->send(conn_id, network::make_admin_response(
            network::json_message_type::admin_modify_skills_response,
            msg.seq, false, {}, "Skill system unavailable"));
        return;
    }

    auto st = static_cast<skill::skill_type>(req.skill_type);
    int16_t new_level = 0;

    if (req.action == "set") {
        skill_->set_skill_level(plr->id, st, static_cast<int16_t>(req.value));
        new_level = skill_->get_skill_level(plr->id, st);
    } else if (req.action == "reset") {
        skill_->reset_skill(plr->id, st);
        new_level = 0;
    } else if (req.action == "reset_all") {
        skill_->reset_all_skills(plr->id);
        new_level = 0;
    } else if (req.action == "add_uses") {
        skill_->add_skill_uses(plr->id, st, req.value);
        new_level = skill_->get_skill_level(plr->id, st);
    } else {
        ws_server_->send(conn_id, network::make_admin_response(
            network::json_message_type::admin_modify_skills_response,
            msg.seq, false, {}, "Invalid action: " + req.action));
        return;
    }

    LOG_INFO(admin, "Admin {} skill for '{}': action={} skill={} value={}",
        req.action, req.player_name, req.action, req.skill_type, req.value);
    audit_log(conn_id, "modify_skill " + req.action + " " + req.player_name + " skill=" + std::to_string(req.skill_type));

    ws_server_->send(conn_id, network::make_admin_response(
        network::json_message_type::admin_modify_skills_response,
        msg.seq, true, {
            {"player_name", req.player_name},
            {"action", req.action},
            {"skill_type", req.skill_type},
            {"new_level", new_level}
        }));
}

void admin_web_handlers::handle_modify_spells(connection_id conn_id, const network::json_message& msg)
{
    if (!require_admin(conn_id, msg.seq, 20)) return;

    auto parse = network::admin_modify_spells_request_data::from_json(msg.data);
    if (parse.is_err()) {
        send_error(conn_id, msg.seq, "parse_error", parse.error());
        return;
    }
    auto& req = parse.value();

    auto* plr = players_->get_player_by_name(req.player_name);
    if (!plr) {
        ws_server_->send(conn_id, network::make_admin_response(
            network::json_message_type::admin_modify_spells_response,
            msg.seq, false, {}, "Player not found"));
        return;
    }
    if (!magic_) {
        ws_server_->send(conn_id, network::make_admin_response(
            network::json_message_type::admin_modify_spells_response,
            msg.seq, false, {}, "Magic system unavailable"));
        return;
    }

    auto sid = spell_id(static_cast<int>(req.spell_id));

    if (req.action == "learn") {
        magic_->learn_spell(plr->ecs_entity, sid);
    } else if (req.action == "forget") {
        magic_->forget_spell(plr->ecs_entity, sid);
    } else if (req.action == "level_up") {
        magic_->level_up_spell(plr->ecs_entity, sid);
    } else if (req.action == "reset_cooldowns") {
        magic_->reset_all_cooldowns(plr->ecs_entity);
    } else {
        ws_server_->send(conn_id, network::make_admin_response(
            network::json_message_type::admin_modify_spells_response,
            msg.seq, false, {}, "Invalid action: " + req.action));
        return;
    }

    LOG_INFO(admin, "Admin {} spell for '{}': spell_id={}",
        req.action, req.player_name, req.spell_id);
    audit_log(conn_id, "modify_spell " + req.action + " " + req.player_name + " spell=" + std::to_string(req.spell_id));

    ws_server_->send(conn_id, network::make_admin_response(
        network::json_message_type::admin_modify_spells_response,
        msg.seq, true, {
            {"player_name", req.player_name},
            {"action", req.action},
            {"spell_id", req.spell_id}
        }));
}

void admin_web_handlers::handle_get_player_quests(connection_id conn_id, const network::json_message& msg)
{
    if (!require_admin(conn_id, msg.seq)) return;

    auto parse = network::admin_get_player_quests_request_data::from_json(msg.data);
    if (parse.is_err()) {
        send_error(conn_id, msg.seq, "parse_error", parse.error());
        return;
    }
    auto& req = parse.value();

    auto* plr = players_->get_player_by_name(req.player_name);
    if (!plr) {
        ws_server_->send(conn_id, network::make_admin_response(
            network::json_message_type::admin_get_player_quests_response,
            msg.seq, false, {}, "Player not found"));
        return;
    }
    if (!quest_) {
        ws_server_->send(conn_id, network::make_admin_response(
            network::json_message_type::admin_get_player_quests_response,
            msg.seq, false, {}, "Quest system unavailable"));
        return;
    }

    nlohmann::json data;
    data["player_name"] = plr->name;

    auto* journal = quest_->get_journal(plr->id);
    nlohmann::json active_arr = nlohmann::json::array();
    nlohmann::json completed_arr = nlohmann::json::array();
    int completed_count = 0;

    if (journal) {
        completed_count = static_cast<int>(journal->completed_quests.size());
        for (auto qid : journal->completed_quests) {
            completed_arr.push_back(qid.value);
        }
        for (const auto& qs : journal->active_quests) {
            nlohmann::json qj;
            qj["quest_id"] = qs.template_id.value;
            qj["status"] = static_cast<int>(qs.status);
            qj["elapsed_seconds"] = qs.elapsed_seconds;
            qj["time_limit"] = qs.time_limit_seconds;
            auto* qt = quest_->get_quest_template(qs.template_id);
            if (qt) qj["name"] = qt->name;
            nlohmann::json objs = nlohmann::json::array();
            for (const auto& obj : qs.objectives) {
                nlohmann::json oj;
                oj["id"] = obj.template_id;
                oj["current"] = obj.current_count;
                oj["required"] = obj.required_count;
                oj["complete"] = obj.is_complete();
                // Find description from template
                if (qt) {
                    for (const auto& ot : qt->objectives) {
                        if (ot.id == obj.template_id) {
                            oj["description"] = ot.description;
                            break;
                        }
                    }
                }
                objs.push_back(std::move(oj));
            }
            qj["objectives"] = objs;
            active_arr.push_back(std::move(qj));
        }
    }

    data["active_quests"] = active_arr;
    data["completed_quests"] = completed_arr;
    data["completed_count"] = completed_count;

    ws_server_->send(conn_id, network::make_admin_response(
        network::json_message_type::admin_get_player_quests_response,
        msg.seq, true, data));
}

void admin_web_handlers::handle_quest_action(connection_id conn_id, const network::json_message& msg)
{
    if (!require_admin(conn_id, msg.seq, 20)) return;

    auto parse = network::admin_quest_action_request_data::from_json(msg.data);
    if (parse.is_err()) {
        send_error(conn_id, msg.seq, "parse_error", parse.error());
        return;
    }
    auto& req = parse.value();

    auto* plr = players_->get_player_by_name(req.player_name);
    if (!plr) {
        ws_server_->send(conn_id, network::make_admin_response(
            network::json_message_type::admin_quest_action_response,
            msg.seq, false, {}, "Player not found"));
        return;
    }
    if (!quest_) {
        ws_server_->send(conn_id, network::make_admin_response(
            network::json_message_type::admin_quest_action_response,
            msg.seq, false, {}, "Quest system unavailable"));
        return;
    }

    auto qid = quest_id{static_cast<uint16_t>(req.quest_id)};
    std::string result_str;

    if (req.action == "accept") {
        auto r = quest_->accept_quest(plr->id, qid);
        result_str = (r == quest::accept_result::success) ? "accepted" : "failed";
    } else if (req.action == "abandon") {
        bool ok = quest_->abandon_quest(plr->id, qid);
        result_str = ok ? "abandoned" : "failed";
    } else if (req.action == "complete") {
        auto r = quest_->complete_quest(plr->id, qid);
        result_str = (r == quest::complete_result::success) ? "completed" : "failed";
    } else {
        ws_server_->send(conn_id, network::make_admin_response(
            network::json_message_type::admin_quest_action_response,
            msg.seq, false, {}, "Invalid action: " + req.action));
        return;
    }

    LOG_INFO(admin, "Admin quest action for '{}': {} quest {}  -> {}",
        req.player_name, req.action, req.quest_id, result_str);
    audit_log(conn_id, "quest_action " + req.action + " " + req.player_name + " quest=" + std::to_string(req.quest_id));

    ws_server_->send(conn_id, network::make_admin_response(
        network::json_message_type::admin_quest_action_response,
        msg.seq, true, {
            {"player_name", req.player_name},
            {"action", req.action},
            {"quest_id", req.quest_id},
            {"result", result_str}
        }));
}

void admin_web_handlers::handle_remove_effects(connection_id conn_id, const network::json_message& msg)
{
    if (!require_admin(conn_id, msg.seq)) return;

    auto parse = network::admin_remove_effects_request_data::from_json(msg.data);
    if (parse.is_err()) {
        send_error(conn_id, msg.seq, "parse_error", parse.error());
        return;
    }
    auto& req = parse.value();

    auto* plr = players_->get_player_by_name(req.player_name);
    if (!plr) {
        ws_server_->send(conn_id, network::make_admin_response(
            network::json_message_type::admin_remove_effects_response,
            msg.seq, false, {}, "Player not found"));
        return;
    }
    if (!effects_) {
        ws_server_->send(conn_id, network::make_admin_response(
            network::json_message_type::admin_remove_effects_response,
            msg.seq, false, {}, "Effect system unavailable"));
        return;
    }

    int removed = 0;
    if (req.mode == "all") {
        // Count existing effects before removal
        auto* elist = effects_->get_effects(plr->ecs_entity);
        if (elist) removed = static_cast<int>(elist->size());
        effects_->remove_all_effects(plr->ecs_entity);
    } else if (req.mode == "group") {
        effects_->remove_effects_by_group(plr->ecs_entity, static_cast<magic_type>(req.group));
        removed = 1;  // Can't easily count, report 1 for success
    } else if (req.mode == "single") {
        effects_->remove_effect(plr->ecs_entity, effect::effect_id(static_cast<uint32_t>(req.effect_id)));
        removed = 1;
    } else {
        ws_server_->send(conn_id, network::make_admin_response(
            network::json_message_type::admin_remove_effects_response,
            msg.seq, false, {}, "Invalid mode: " + req.mode));
        return;
    }

    LOG_INFO(admin, "Admin removed effects from '{}': mode={}", req.player_name, req.mode);
    audit_log(conn_id, "remove_effects " + req.player_name + " mode=" + req.mode);

    ws_server_->send(conn_id, network::make_admin_response(
        network::json_message_type::admin_remove_effects_response,
        msg.seq, true, {
            {"player_name", req.player_name},
            {"mode", req.mode},
            {"removed_count", removed}
        }));
}

void admin_web_handlers::handle_create_account(connection_id conn_id, const network::json_message& msg)
{
    if (!require_admin(conn_id, msg.seq, 20)) return;

    auto parse = network::admin_create_account_request_data::from_json(msg.data);
    if (parse.is_err()) {
        send_error(conn_id, msg.seq, "parse_error", parse.error());
        return;
    }
    auto& req = parse.value();

    if (!auth_) {
        ws_server_->send(conn_id, network::make_admin_response(
            network::json_message_type::admin_create_account_response,
            msg.seq, false, {}, "Auth system unavailable"));
        return;
    }

    auto result = auth_->create_account(req.username, req.password);
    if (result.is_err()) {
        ws_server_->send(conn_id, network::make_admin_response(
            network::json_message_type::admin_create_account_response,
            msg.seq, false, {}, "Failed to create account"));
        return;
    }

    auto acc_id = result.value();

    // Set admin level if specified
    if (req.admin_level > 0) {
        auth_->set_admin_level(acc_id, static_cast<auth::admin_level>(req.admin_level));
    }

    LOG_INFO(admin, "Admin created account '{}' (id={}, admin_level={})",
        req.username, acc_id.value, req.admin_level);
    audit_log(conn_id, "create_account " + req.username);

    ws_server_->send(conn_id, network::make_admin_response(
        network::json_message_type::admin_create_account_response,
        msg.seq, true, {
            {"username", req.username},
            {"account_id", acc_id.value}
        }));
}

void admin_web_handlers::handle_change_password(connection_id conn_id, const network::json_message& msg)
{
    if (!require_admin(conn_id, msg.seq, 20)) return;

    auto parse = network::admin_change_password_request_data::from_json(msg.data);
    if (parse.is_err()) {
        send_error(conn_id, msg.seq, "parse_error", parse.error());
        return;
    }
    auto& req = parse.value();

    if (!auth_) {
        ws_server_->send(conn_id, network::make_admin_response(
            network::json_message_type::admin_change_password_response,
            msg.seq, false, {}, "Auth system unavailable"));
        return;
    }

    auto acc_result = auth_->get_account_by_username(req.username);
    if (acc_result.is_err()) {
        ws_server_->send(conn_id, network::make_admin_response(
            network::json_message_type::admin_change_password_response,
            msg.seq, false, {}, "Account not found"));
        return;
    }

    auto result = auth_->admin_change_password(acc_result.value().id, req.new_password);
    if (result.is_err()) {
        ws_server_->send(conn_id, network::make_admin_response(
            network::json_message_type::admin_change_password_response,
            msg.seq, false, {}, "Failed to change password"));
        return;
    }

    LOG_INFO(admin, "Admin reset password for '{}'", req.username);
    audit_log(conn_id, "change_password " + req.username);

    ws_server_->send(conn_id, network::make_admin_response(
        network::json_message_type::admin_change_password_response,
        msg.seq, true, {{"username", req.username}}));
}

void admin_web_handlers::handle_set_admin_level(connection_id conn_id, const network::json_message& msg)
{
    if (!require_admin(conn_id, msg.seq, 20)) return;

    auto parse = network::admin_set_admin_level_request_data::from_json(msg.data);
    if (parse.is_err()) {
        send_error(conn_id, msg.seq, "parse_error", parse.error());
        return;
    }
    auto& req = parse.value();

    if (!auth_) {
        ws_server_->send(conn_id, network::make_admin_response(
            network::json_message_type::admin_set_admin_level_response,
            msg.seq, false, {}, "Auth system unavailable"));
        return;
    }

    // Check that the admin isn't setting a level >= their own
    auto* conn = ws_server_->get_connection(conn_id);
    if (conn && req.admin_level >= conn->admin_level()) {
        ws_server_->send(conn_id, network::make_admin_response(
            network::json_message_type::admin_set_admin_level_response,
            msg.seq, false, {}, "Cannot set admin level equal to or above your own"));
        return;
    }

    auto acc_result = auth_->get_account_by_username(req.username);
    if (acc_result.is_err()) {
        ws_server_->send(conn_id, network::make_admin_response(
            network::json_message_type::admin_set_admin_level_response,
            msg.seq, false, {}, "Account not found"));
        return;
    }

    auto new_level = static_cast<auth::admin_level>(req.admin_level);
    auth_->set_admin_level(acc_result.value().id, new_level);

    // Note: runtime admin_system uses a different admin_level enum,
    // so we don't update it here. Player must re-login for changes to take effect.

    std::string level_name;
    switch (req.admin_level) {
        case 0: level_name = "player"; break;
        case 10: level_name = "gamemaster"; break;
        case 20: level_name = "administrator"; break;
        default: level_name = "level_" + std::to_string(req.admin_level); break;
    }

    LOG_INFO(admin, "Admin set admin level for '{}' to {} ({})",
        req.username, req.admin_level, level_name);
    audit_log(conn_id, "set_admin_level " + req.username + " " + level_name);

    ws_server_->send(conn_id, network::make_admin_response(
        network::json_message_type::admin_set_admin_level_response,
        msg.seq, true, {
            {"username", req.username},
            {"admin_level", req.admin_level},
            {"level_name", level_name}
        }));
}

void admin_web_handlers::handle_list_spawn_points(connection_id conn_id, const network::json_message& msg)
{
    if (!require_admin(conn_id, msg.seq)) return;

    auto parse = network::admin_list_spawn_points_request_data::from_json(msg.data);
    if (parse.is_err()) {
        send_error(conn_id, msg.seq, "parse_error", parse.error());
        return;
    }
    auto& req = parse.value();

    if (!npc_) {
        ws_server_->send(conn_id, network::make_admin_response(
            network::json_message_type::admin_list_spawn_points_response,
            msg.seq, false, {}, "NPC system unavailable"));
        return;
    }

    // Convert filter map name to map_id if provided
    std::optional<map_id> filter_map;
    if (!req.map_name.empty() && world_) {
        auto* m = world_->get_map_by_name(req.map_name);
        if (m) filter_map = m->id();
    }

    nlohmann::json spawn_arr = nlohmann::json::array();
    npc_->for_each_spawn_point([&](const npc::spawn_point& sp) {
        if (filter_map && sp.map != *filter_map) return;

        nlohmann::json sj;
        sj["npc_type"] = sp.npc_type.value;
        sj["center_x"] = sp.center.x;
        sj["center_y"] = sp.center.y;
        sj["radius"] = sp.radius;
        sj["max_count"] = sp.max_count;
        sj["current_count"] = sp.current_count;
        sj["respawn_time_ms"] = sp.respawn_time_ms;

        // Get NPC name from registry
        if (npc_registry_) {
            auto* tmpl = npc_registry_->get(sp.npc_type);
            if (tmpl) sj["npc_name"] = tmpl->name;
        }

        // Get map name
        if (world_) {
            auto* m = world_->get_map(sp.map);
            if (m) sj["map_name"] = std::string(m->name());
        }

        spawn_arr.push_back(std::move(sj));
    });

    ws_server_->send(conn_id, network::make_admin_response(
        network::json_message_type::admin_list_spawn_points_response,
        msg.seq, true, {
            {"map_name", req.map_name},
            {"spawn_points", spawn_arr},
            {"count", spawn_arr.size()}
        }));
}

void admin_web_handlers::handle_list_spell_templates(connection_id conn_id, const network::json_message& msg)
{
    if (!require_admin(conn_id, msg.seq)) return;

    if (!magic_) {
        ws_server_->send(conn_id, network::make_admin_response(
            network::json_message_type::admin_list_spell_templates_response,
            msg.seq, false, {}, "Magic system unavailable"));
        return;
    }

    nlohmann::json spells_arr = nlohmann::json::array();
    magic_->for_each_spell([&](spell_id id, const magic::spell_template& spell) {
        spells_arr.push_back({
            {"id", id.value},
            {"name", spell.name},
            {"category", static_cast<int>(spell.category)},
            {"target_type", static_cast<int>(spell.target_type)},
            {"element", static_cast<int>(spell.element)},
            {"mana_cost", spell.mana_cost},
            {"cast_time_ms", spell.cast_time_ms},
            {"cooldown_ms", spell.cooldown_ms},
            {"level_requirement", spell.level_requirement}
        });
    });

    ws_server_->send(conn_id, network::make_admin_response(
        network::json_message_type::admin_list_spell_templates_response,
        msg.seq, true, {
            {"spells", spells_arr},
            {"count", spells_arr.size()}
        }));
}

void admin_web_handlers::handle_get_spell_template(connection_id conn_id, const network::json_message& msg)
{
    if (!require_admin(conn_id, msg.seq)) return;

    auto parse = network::admin_get_spell_template_request_data::from_json(msg.data);
    if (parse.is_err()) {
        send_error(conn_id, msg.seq, "parse_error", parse.error());
        return;
    }
    auto& req = parse.value();

    if (!magic_) {
        ws_server_->send(conn_id, network::make_admin_response(
            network::json_message_type::admin_get_spell_template_response,
            msg.seq, false, {}, "Magic system unavailable"));
        return;
    }

    const magic::spell_template* spell = nullptr;

    if (req.spell_id > 0) {
        spell = magic_->get_spell(spell_id(static_cast<int>(req.spell_id)));
    } else if (!req.spell_name.empty()) {
        // Search by name
        std::string name_lower = req.spell_name;
        std::transform(name_lower.begin(), name_lower.end(), name_lower.begin(),
            [](unsigned char c) { return std::tolower(c); });
        magic_->for_each_spell([&](spell_id /*id*/, const magic::spell_template& s) {
            if (spell) return;  // Already found
            std::string s_lower = s.name;
            std::transform(s_lower.begin(), s_lower.end(), s_lower.begin(),
                [](unsigned char c) { return std::tolower(c); });
            if (s_lower == name_lower) spell = &s;
        });
    }

    if (!spell) {
        ws_server_->send(conn_id, network::make_admin_response(
            network::json_message_type::admin_get_spell_template_response,
            msg.seq, false, {}, "Spell not found"));
        return;
    }

    nlohmann::json data;
    data["id"] = spell->id.value;
    data["name"] = spell->name;
    data["category"] = static_cast<int>(spell->category);
    data["target_type"] = static_cast<int>(spell->target_type);
    data["element"] = static_cast<int>(spell->element);
    data["mana_cost"] = spell->mana_cost;
    data["hp_cost"] = spell->hp_cost;
    data["sp_cost"] = spell->sp_cost;
    data["cast_time_ms"] = spell->cast_time_ms;
    data["cooldown_ms"] = spell->cooldown_ms;
    data["duration_ms"] = spell->duration_ms;
    data["range"] = spell->range;
    data["aoe_radius"] = spell->aoe_radius;
    data["base_damage"] = spell->base_damage;
    data["base_heal"] = spell->base_heal;
    data["effect_value"] = spell->effect_value;
    data["int_scaling"] = spell->int_scaling;
    data["mag_scaling"] = spell->mag_scaling;
    data["level_requirement"] = spell->level_requirement;
    data["int_requirement"] = spell->int_requirement;
    data["mag_requirement"] = spell->mag_requirement;
    data["can_critical"] = spell->can_critical;
    data["ignores_defense"] = spell->ignores_defense;
    data["channeled"] = spell->channeled;

    ws_server_->send(conn_id, network::make_admin_response(
        network::json_message_type::admin_get_spell_template_response,
        msg.seq, true, data));
}

void admin_web_handlers::handle_set_maintenance_mode(connection_id conn_id, const network::json_message& msg)
{
    if (!require_admin(conn_id, msg.seq, 20)) return;

    auto parse = network::admin_set_maintenance_mode_request_data::from_json(msg.data);
    if (parse.is_err()) {
        send_error(conn_id, msg.seq, "parse_error", parse.error());
        return;
    }
    auto& req = parse.value();

    if (!auth_) {
        ws_server_->send(conn_id, network::make_admin_response(
            network::json_message_type::admin_set_maintenance_mode_response,
            msg.seq, false, {}, "Auth system unavailable"));
        return;
    }

    auth_->set_maintenance_mode(req.enabled, req.message);

    LOG_INFO(admin, "Admin set maintenance mode: enabled={} message='{}'",
        req.enabled, req.message);
    audit_log(conn_id, std::string("maintenance_mode ") + (req.enabled ? "on" : "off"));

    ws_server_->send(conn_id, network::make_admin_response(
        network::json_message_type::admin_set_maintenance_mode_response,
        msg.seq, true, {
            {"enabled", req.enabled},
            {"message", req.message}
        }));
}

void admin_web_handlers::handle_create_character_admin(connection_id conn_id, const network::json_message& msg)
{
    if (!require_admin(conn_id, msg.seq, 20)) return;

    auto parse = network::admin_create_character_request_admin_data::from_json(msg.data);
    if (parse.is_err()) {
        send_error(conn_id, msg.seq, "parse_error", parse.error());
        return;
    }
    auto& req = parse.value();

    if (!auth_) {
        ws_server_->send(conn_id, network::make_admin_response(
            network::json_message_type::admin_create_character_response_admin,
            msg.seq, false, {}, "Auth system unavailable"));
        return;
    }

    auto acc_result = auth_->get_account_by_username(req.username);
    if (acc_result.is_err()) {
        ws_server_->send(conn_id, network::make_admin_response(
            network::json_message_type::admin_create_character_response_admin,
            msg.seq, false, {}, "Account not found"));
        return;
    }

    auth::character_create_info info;
    info.name = req.name;
    info.gender = req.gender;
    info.hair_style = req.hair_style;
    info.hair_color = req.hair_color;
    info.skin_color = req.skin_color;
    info.underwear_color = req.underwear_color;

    auto create_result = auth_->create_character(acc_result.value().id, info);
    if (create_result.is_err()) {
        ws_server_->send(conn_id, network::make_admin_response(
            network::json_message_type::admin_create_character_response_admin,
            msg.seq, false, {}, "Failed to create character"));
        return;
    }

    LOG_INFO(admin, "Admin created character '{}' for account '{}'",
        req.name, req.username);
    audit_log(conn_id, "create_character " + req.name + " for " + req.username);

    ws_server_->send(conn_id, network::make_admin_response(
        network::json_message_type::admin_create_character_response_admin,
        msg.seq, true, {
            {"username", req.username},
            {"character_name", req.name},
            {"character_id", create_result.value().value}
        }));
}

void admin_web_handlers::handle_delete_character_admin(connection_id conn_id, const network::json_message& msg)
{
    if (!require_admin(conn_id, msg.seq, 20)) return;

    auto parse = network::admin_delete_character_request_admin_data::from_json(msg.data);
    if (parse.is_err()) {
        send_error(conn_id, msg.seq, "parse_error", parse.error());
        return;
    }
    auto& req = parse.value();

    if (!auth_) {
        ws_server_->send(conn_id, network::make_admin_response(
            network::json_message_type::admin_delete_character_response_admin,
            msg.seq, false, {}, "Auth system unavailable"));
        return;
    }

    auto acc_result = auth_->get_account_by_username(req.username);
    if (acc_result.is_err()) {
        ws_server_->send(conn_id, network::make_admin_response(
            network::json_message_type::admin_delete_character_response_admin,
            msg.seq, false, {}, "Account not found"));
        return;
    }

    // Check if character is online
    if (players_) {
        auto* plr = players_->get_player_by_name(req.character_name);
        if (plr) {
            ws_server_->send(conn_id, network::make_admin_response(
                network::json_message_type::admin_delete_character_response_admin,
                msg.seq, false, {}, "Cannot delete character while online"));
            return;
        }
    }

    // Find the character id by name
    auto chars_result = auth_->get_characters(acc_result.value().id);
    if (chars_result.is_err()) {
        ws_server_->send(conn_id, network::make_admin_response(
            network::json_message_type::admin_delete_character_response_admin,
            msg.seq, false, {}, "Failed to list characters"));
        return;
    }

    player_id char_id{};
    for (const auto& ch : chars_result.value()) {
        if (ch.name == req.character_name) {
            char_id = ch.id;
            break;
        }
    }

    if (char_id.value == 0) {
        ws_server_->send(conn_id, network::make_admin_response(
            network::json_message_type::admin_delete_character_response_admin,
            msg.seq, false, {}, "Character not found on this account"));
        return;
    }

    auto del_result = auth_->delete_character(acc_result.value().id, char_id);
    if (del_result.is_err()) {
        ws_server_->send(conn_id, network::make_admin_response(
            network::json_message_type::admin_delete_character_response_admin,
            msg.seq, false, {}, "Failed to delete character"));
        return;
    }

    LOG_INFO(admin, "Admin deleted character '{}' from account '{}'",
        req.character_name, req.username);
    audit_log(conn_id, "delete_character " + req.character_name + " from " + req.username);

    ws_server_->send(conn_id, network::make_admin_response(
        network::json_message_type::admin_delete_character_response_admin,
        msg.seq, true, {
            {"username", req.username},
            {"character_name", req.character_name},
            {"deleted", true}
        }));
}

void admin_web_handlers::handle_manage_ip_bans(connection_id conn_id, const network::json_message& msg)
{
    if (!require_admin(conn_id, msg.seq, 20)) return;

    auto parse = network::admin_manage_ip_bans_request_data::from_json(msg.data);
    if (parse.is_err()) {
        send_error(conn_id, msg.seq, "parse_error", parse.error());
        return;
    }
    auto& req = parse.value();

    if (!admin_) {
        ws_server_->send(conn_id, network::make_admin_response(
            network::json_message_type::admin_manage_ip_bans_response,
            msg.seq, false, {}, "Admin system unavailable"));
        return;
    }

    nlohmann::json data;
    data["action"] = req.action;

    if (req.action == "add") {
        admin_->add_ip_ban(req.ip, req.reason);
        data["ip"] = req.ip;
        LOG_INFO(admin, "Admin added IP ban: {} ({})", req.ip, req.reason);
        audit_log(conn_id, "add_ip_ban " + req.ip);
    } else if (req.action == "remove") {
        admin_->remove_ip_ban(req.ip);
        data["ip"] = req.ip;
        LOG_INFO(admin, "Admin removed IP ban: {}", req.ip);
        audit_log(conn_id, "remove_ip_ban " + req.ip);
    } else if (req.action == "list") {
        // Just list all
    } else {
        ws_server_->send(conn_id, network::make_admin_response(
            network::json_message_type::admin_manage_ip_bans_response,
            msg.seq, false, {}, "Invalid action: " + req.action));
        return;
    }

    // Always include current ban list
    nlohmann::json bans_arr = nlohmann::json::array();
    for (const auto& ip : admin_->get_ip_bans()) {
        bans_arr.push_back(ip);
    }
    data["banned_ips"] = bans_arr;

    ws_server_->send(conn_id, network::make_admin_response(
        network::json_message_type::admin_manage_ip_bans_response,
        msg.seq, true, data));
}

// === Performance Stats ===

void admin_web_handlers::handle_perf_stats(connection_id conn_id, const network::json_message& msg)
{
    if (!require_admin(conn_id, msg.seq)) return;

    auto req_result = network::admin_perf_stats_request_data::from_json(msg.data);
    auto req = req_result.is_ok() ? req_result.value() : network::admin_perf_stats_request_data{};

    nlohmann::json data;

    if (perf_stats_)
    {
        if (req.include_timing)
        {
            auto timing = perf_stats_->get_all_timing_snapshots();
            auto arr = nlohmann::json::array();
            for (const auto& t : timing)
            {
                arr.push_back({
                    {"name", std::string(t.name)},
                    {"importance", static_cast<int>(t.importance)},
                    {"status", std::string(perf::health_status_string(t.status))},
                    {"sample_count", t.sample_count},
                    {"avg_ms", t.avg_ms},
                    {"min_ms", t.min_ms},
                    {"max_ms", t.max_ms},
                    {"p99_ms", t.p99_ms}
                });
            }
            data["timing"] = std::move(arr);
        }

        if (req.include_counters)
        {
            auto counters = perf_stats_->get_all_counter_snapshots();
            auto arr = nlohmann::json::array();
            for (const auto& c : counters)
            {
                arr.push_back({
                    {"name", std::string(c.name)},
                    {"importance", static_cast<int>(c.importance)},
                    {"status", std::string(perf::health_status_string(c.status))},
                    {"total", c.total},
                    {"per_second", c.per_second}
                });
            }
            data["counters"] = std::move(arr);
        }

        if (req.include_gauges)
        {
            auto g = perf_stats_->get_gauge_snapshot();
            data["gauges"] = {
                {"players_online", g.players_online},
                {"npcs_alive", g.npcs_alive},
                {"ground_items", g.ground_items},
                {"active_effects", g.active_effects},
                {"scheduled_tasks", g.scheduled_tasks},
                {"active_connections", g.active_connections}
            };

            nlohmann::json gauge_statuses;
            for (const auto& [name, status] : g.statuses)
            {
                gauge_statuses[name] = std::string(perf::health_status_string(status));
            }
            data["gauge_statuses"] = std::move(gauge_statuses);
        }

        data["enabled"] = perf_stats_->is_enabled();
        data["server_health"] = std::string(
            perf::health_status_string(perf_stats_->compute_overall_health()));
    }
    else
    {
        data["enabled"] = false;
    }

    ws_server_->send(conn_id, network::make_admin_response(
        network::json_message_type::admin_perf_stats_response,
        msg.seq, true, data));
}

// ========== War Management (Phase 6) ==========

void admin_web_handlers::handle_start_war(connection_id conn_id, const network::json_message& msg)
{
    if (!require_admin(conn_id, msg.seq, 15)) return;

    if (!war_)
    {
        send_error(conn_id, msg.seq, "no_war_system", "War system not available");
        return;
    }

    auto war_type_int = msg.data.value("war_type", -1);
    if (war_type_int < 0 || war_type_int > 4)
    {
        send_error(conn_id, msg.seq, "invalid_war_type", "Invalid war type (0=crusade, 1=heldenian, 2=apocalypse)");
        return;
    }

    auto wtype = static_cast<war::war_type>(war_type_int);
    auto result = war_->start_war(wtype);

    nlohmann::json data;
    if (result.is_ok())
    {
        data["war_id"] = result.value().value;
        data["war_type"] = war_type_int;
        ws_server_->send(conn_id, network::make_admin_response(
            network::json_message_type::admin_start_war_response,
            msg.seq, true, data));
        audit_log(conn_id, "start_war", true, "type=" + std::to_string(war_type_int));
    }
    else
    {
        data["error_code"] = static_cast<int>(result.error());
        ws_server_->send(conn_id, network::make_admin_response(
            network::json_message_type::admin_start_war_response,
            msg.seq, false, data, "Failed to start war"));
    }
}

void admin_web_handlers::handle_end_war(connection_id conn_id, const network::json_message& msg)
{
    if (!require_admin(conn_id, msg.seq, 15)) return;

    if (!war_)
    {
        send_error(conn_id, msg.seq, "no_war_system", "War system not available");
        return;
    }

    auto war_id_val = msg.data.value("war_id", 0u);
    if (war_id_val == 0)
    {
        send_error(conn_id, msg.seq, "missing_war_id", "War ID required");
        return;
    }

    auto wid = war::war_id(static_cast<uint32_t>(war_id_val));
    auto result = war_->end_war(wid);

    nlohmann::json data;
    data["war_id"] = war_id_val;

    if (result == war::war_result_code::success)
    {
        ws_server_->send(conn_id, network::make_admin_response(
            network::json_message_type::admin_end_war_response,
            msg.seq, true, data));
        audit_log(conn_id, "end_war", true, "war_id=" + std::to_string(war_id_val));
    }
    else
    {
        data["error_code"] = static_cast<int>(result);
        ws_server_->send(conn_id, network::make_admin_response(
            network::json_message_type::admin_end_war_response,
            msg.seq, false, data, "Failed to end war"));
    }
}

void admin_web_handlers::handle_war_history(connection_id conn_id, const network::json_message& msg)
{
    if (!require_admin(conn_id, msg.seq)) return;

    if (!war_persistence_)
    {
        send_error(conn_id, msg.seq, "no_war_persistence", "War persistence not available");
        return;
    }

    auto limit = msg.data.value("limit", 20);
    auto offset = msg.data.value("offset", 0);
    auto type_filter = msg.data.value("war_type", -1);

    hb::result<std::vector<war::war_history_row>, std::string> history_result =
        (type_filter >= 0)
            ? war_persistence_->load_war_history_by_type(
                static_cast<war::war_type>(type_filter), limit, offset)
            : war_persistence_->load_war_history(limit, offset);

    if (history_result.is_err())
    {
        send_error(conn_id, msg.seq, "db_error", history_result.error());
        return;
    }

    nlohmann::json wars_arr = nlohmann::json::array();
    for (const auto& row : history_result.value())
    {
        nlohmann::json wj;
        wj["id"] = row.id;
        wj["war_type"] = static_cast<int>(row.type);
        wj["started_at"] = row.started_at;
        wj["ended_at"] = row.ended_at;
        wj["duration_seconds"] = row.duration_seconds;
        wj["winner_faction"] = static_cast<int>(row.winner);
        wj["aresden_score"] = row.aresden_score;
        wj["elvine_score"] = row.elvine_score;
        wars_arr.push_back(std::move(wj));
    }

    // Get total count
    auto count_result = (type_filter >= 0)
        ? war_persistence_->count_wars_by_type(static_cast<war::war_type>(type_filter))
        : war_persistence_->count_wars();

    nlohmann::json data;
    data["wars"] = wars_arr;
    data["count"] = wars_arr.size();
    data["total"] = count_result.is_ok() ? count_result.value() : 0;

    ws_server_->send(conn_id, network::make_admin_response(
        network::json_message_type::admin_war_history_response,
        msg.seq, true, data));
}

void admin_web_handlers::handle_war_participants(connection_id conn_id, const network::json_message& msg)
{
    if (!require_admin(conn_id, msg.seq)) return;

    if (!war_persistence_)
    {
        send_error(conn_id, msg.seq, "no_war_persistence", "War persistence not available");
        return;
    }

    auto war_db_id = msg.data.value("war_id", 0);
    if (war_db_id <= 0)
    {
        send_error(conn_id, msg.seq, "missing_war_id", "War ID required");
        return;
    }

    auto result = war_persistence_->load_war_participants(war_db_id);
    if (result.is_err())
    {
        send_error(conn_id, msg.seq, "db_error", result.error());
        return;
    }

    nlohmann::json participants_arr = nlohmann::json::array();
    for (const auto& row : result.value())
    {
        nlohmann::json pj;
        pj["character_id"] = row.character_id;
        pj["faction"] = static_cast<int>(row.faction);
        pj["duty"] = row.duty;
        pj["kills"] = row.kills;
        pj["deaths"] = row.deaths;
        pj["assists"] = row.assists;
        pj["damage_dealt"] = row.damage_dealt;
        pj["healing_done"] = row.healing_done;
        pj["contribution"] = row.contribution;
        pj["reward_exp"] = row.reward_exp;
        pj["reward_gold"] = row.reward_gold;
        pj["reward_contribution"] = row.reward_contribution;
        participants_arr.push_back(std::move(pj));
    }

    nlohmann::json data;
    data["war_id"] = war_db_id;
    data["participants"] = participants_arr;
    data["count"] = participants_arr.size();

    ws_server_->send(conn_id, network::make_admin_response(
        network::json_message_type::admin_war_participants_response,
        msg.seq, true, data));
}

}  // namespace hb::bridge
