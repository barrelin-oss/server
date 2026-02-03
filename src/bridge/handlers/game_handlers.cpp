// game_handlers.cpp
// Message handlers for in-game protocol implementation

// Include platform header first to define NOMINMAX before Windows headers
#include "platform/platform.h"

#include "bridge/handlers/game_handlers.h"
#include "network/websocket_server.h"
#include "player/player_system.h"
#include "world/world_subsystem.h"
#include "social/social_system.h"
#include "admin/admin_system.h"
#include "combat/combat_system.h"
#include "combat/combat_events.h"
#include "npc/npc_system.h"
#include "npc/npc.h"
#include "core/logger.h"

#include <chrono>
#include <iomanip>
#include <sstream>

namespace hb::bridge {

game_handlers::game_handlers() = default;
game_handlers::~game_handlers() = default;

void game_handlers::initialize(network::websocket_server* ws_server,
                                player::player_system* players,
                                world::world_subsystem* world,
                                social::social_system* social,
                                admin::admin_system* admin,
                                combat::combat_system* combat,
                                npc::npc_system* npc) {
    ws_server_ = ws_server;
    players_ = players;
    world_ = world;
    social_ = social;
    admin_ = admin;
    combat_ = combat;
    npc_ = npc;

    // Register chat message callback to distribute messages
    if (social_) {
        social_->on_chat_message([this](const social::chat_message_event& event) {
            on_chat_message(event);
        });
    }

    // Register combat callbacks
    if (combat_) {
        combat_->on_damage([this](const combat::damage_event& event) {
            on_damage_dealt(event);
        });
        combat_->on_death([this](const combat::death_event& event) {
            on_entity_death(event);
        });
    }

    // Register NPC callbacks
    if (npc_) {
        npc_->set_on_spawn_callback([this](const npc::npc& n) {
            broadcast_npc_spawn(n);
        });
        npc_->set_on_move_callback([this](const npc::npc& n) {
            broadcast_npc_move(n);
        });
        npc_->set_on_death_callback([this](const npc::npc& n, entity::entity killer) {
            broadcast_npc_death(n, killer);
        });
        npc_->set_on_attack_callback([this](const npc::npc& n, entity::entity target, int32_t damage) {
            broadcast_npc_attack(n, target, damage);
        });
    }

    LOG_INFO(bridge, "Game handlers initialized (chat: {}, admin: {}, combat: {}, npc: {})",
        social_ != nullptr ? "yes" : "no",
        admin_ != nullptr ? "yes" : "no",
        combat_ != nullptr ? "yes" : "no",
        npc_ != nullptr ? "yes" : "no");
}

void game_handlers::handle_message(connection_id conn_id, const network::json_message& msg) {
    switch (msg.type) {
        // Movement
        case network::json_message_type::player_move_request:
            handle_player_move(conn_id, msg);
            break;
        case network::json_message_type::player_run_request:
            handle_player_run(conn_id, msg);
            break;
        case network::json_message_type::player_stop_request:
            handle_player_stop(conn_id, msg);
            break;

        // Combat
        case network::json_message_type::player_attack_request:
            handle_player_attack(conn_id, msg);
            break;

        // Actions
        case network::json_message_type::player_magic_request:
            handle_player_magic(conn_id, msg);
            break;
        case network::json_message_type::player_skill_request:
            handle_player_skill(conn_id, msg);
            break;
        case network::json_message_type::player_pickup_request:
            handle_player_pickup(conn_id, msg);
            break;
        case network::json_message_type::player_interact_request:
            handle_player_interact(conn_id, msg);
            break;

        // Chat
        case network::json_message_type::chat_message:
            handle_chat_message(conn_id, msg);
            break;

        // Commands
        case network::json_message_type::command_request:
            handle_command(conn_id, msg);
            break;

        // View range
        case network::json_message_type::set_view_range:
            handle_set_view_range(conn_id, msg);
            break;

        default:
            LOG_WARN(bridge, "Unhandled game message type: {}",
                network::to_string(msg.type));
            send_error(conn_id, msg.seq, "unknown_message_type",
                "Message type not recognized by game handler");
            break;
    }
}

void game_handlers::handle_player_move(connection_id conn_id, const network::json_message& msg) {
    auto* conn = require_in_game(conn_id, msg.seq);
    if (!conn) return;

    if (!players_) {
        send_error(conn_id, msg.seq, "internal_error", "Player system unavailable");
        return;
    }

    // Parse request
    auto data_result = network::player_move_request_data::from_json(msg.data);
    if (data_result.is_err()) {
        send_error(conn_id, msg.seq, "invalid_request", data_result.error());
        return;
    }

    auto& data = data_result.value();
    auto pid = conn->player();

    // Get current player
    auto* player = players_->get_player(pid);
    if (!player) {
        send_error(conn_id, msg.seq, "invalid_player", "Player not found");
        return;
    }

    // Validate client position matches server position (anti-cheat)
    if (std::abs(player->pos.x - data.x) > 1 || std::abs(player->pos.y - data.y) > 1) {
        LOG_WARN(bridge, "Player {} position mismatch: client ({},{}) server ({},{})",
            pid.value, data.x, data.y, player->pos.x, player->pos.y);
        // Send correction - client should resync
        conn->send(network::make_player_move_response(
            msg.seq, false, player->pos.x, player->pos.y,
            static_cast<int16_t>(player->facing), "position_desync"));
        return;
    }

    // Store old position for visibility calculations
    auto old_pos = player->pos;

    // Calculate target position from direction
    auto dir = static_cast<world::direction>(data.direction & 7);  // Clamp to 0-7
    world::position target_pos = old_pos.move(dir);

    auto move_result = players_->try_move(pid, target_pos, dir);

    switch (move_result.result) {
        case player::player_system::move_result::success: {
            // Send success response to moving player
            auto response = network::make_player_move_response(
                msg.seq, true, target_pos.x, target_pos.y, data.direction);
            conn->send(response);

            // Broadcast position to nearby players (not running)
            broadcast_position_update(pid, target_pos.x, target_pos.y, data.direction, false);

            // Update entity visibility for all affected players
            update_entity_visibility(pid, old_pos, target_pos);

            LOG_DEBUG(bridge, "Player {} walked to ({}, {})",
                pid.value, target_pos.x, target_pos.y);
            break;
        }

        case player::player_system::move_result::teleport: {
            // Execute the teleport with full handling
            execute_player_teleport(pid, conn_id, msg.seq,
                move_result.teleport_dest_map,
                move_result.teleport_dest_pos,
                move_result.teleport_dest_dir);
            break;
        }

        case player::player_system::move_result::blocked_terrain:
            conn->send(network::make_player_move_response(
                msg.seq, false, player->pos.x, player->pos.y,
                static_cast<int16_t>(player->facing), "blocked_terrain"));
            break;

        case player::player_system::move_result::blocked_occupied:
            conn->send(network::make_player_move_response(
                msg.seq, false, player->pos.x, player->pos.y,
                static_cast<int16_t>(player->facing), "blocked_occupied"));
            break;

        case player::player_system::move_result::blocked_out_of_bounds:
            conn->send(network::make_player_move_response(
                msg.seq, false, player->pos.x, player->pos.y,
                static_cast<int16_t>(player->facing), "out_of_bounds"));
            break;

        case player::player_system::move_result::blocked_status:
            conn->send(network::make_player_move_response(
                msg.seq, false, player->pos.x, player->pos.y,
                static_cast<int16_t>(player->facing), "cannot_move"));
            break;

        case player::player_system::move_result::blocked_dead:
            conn->send(network::make_player_move_response(
                msg.seq, false, player->pos.x, player->pos.y,
                static_cast<int16_t>(player->facing), "dead"));
            break;

        default:
            conn->send(network::make_player_move_response(
                msg.seq, false, player->pos.x, player->pos.y,
                static_cast<int16_t>(player->facing), "move_failed"));
            break;
    }
}

void game_handlers::handle_player_run(connection_id conn_id, const network::json_message& msg) {
    auto* conn = require_in_game(conn_id, msg.seq);
    if (!conn) return;

    if (!players_) {
        send_error(conn_id, msg.seq, "internal_error", "Player system unavailable");
        return;
    }

    // Parse request
    auto data_result = network::player_run_request_data::from_json(msg.data);
    if (data_result.is_err()) {
        send_error(conn_id, msg.seq, "invalid_request", data_result.error());
        return;
    }

    auto& data = data_result.value();
    auto pid = conn->player();

    auto* player = players_->get_player(pid);
    if (!player) {
        send_error(conn_id, msg.seq, "invalid_player", "Player not found");
        return;
    }

    // Validate client position
    if (std::abs(player->pos.x - data.x) > 1 || std::abs(player->pos.y - data.y) > 1) {
        LOG_WARN(bridge, "Player {} run position mismatch", pid.value);
        conn->send(network::make_player_run_response(
            msg.seq, false, player->pos.x, player->pos.y,
            static_cast<int16_t>(player->facing), "position_desync"));
        return;
    }

    // TODO: Check if player can run (has stamina, not encumbered, etc.)

    auto old_pos = player->pos;
    auto dir = static_cast<world::direction>(data.direction & 7);

    // Running moves 2 tiles in the direction
    world::position target_pos = old_pos.move(dir).move(dir);

    auto move_result = players_->try_move(pid, target_pos, dir);

    if (move_result.result == player::player_system::move_result::success) {
        conn->send(network::make_player_run_response(
            msg.seq, true, target_pos.x, target_pos.y, data.direction));

        broadcast_position_update(pid, target_pos.x, target_pos.y, data.direction, true);
        update_entity_visibility(pid, old_pos, target_pos);

        LOG_DEBUG(bridge, "Player {} ran to ({}, {})", pid.value, target_pos.x, target_pos.y);
    } else {
        conn->send(network::make_player_run_response(
            msg.seq, false, player->pos.x, player->pos.y,
            static_cast<int16_t>(player->facing), "blocked"));
    }
}

void game_handlers::handle_player_stop(connection_id conn_id, const network::json_message& msg) {
    auto* conn = require_in_game(conn_id, msg.seq);
    if (!conn) return;

    if (!players_) {
        send_error(conn_id, msg.seq, "internal_error", "Player system unavailable");
        return;
    }

    auto data_result = network::player_stop_request_data::from_json(msg.data);
    if (data_result.is_err()) {
        send_error(conn_id, msg.seq, "invalid_request", data_result.error());
        return;
    }

    auto& data = data_result.value();
    auto pid = conn->player();

    auto* player = players_->get_player(pid);
    if (!player) {
        send_error(conn_id, msg.seq, "invalid_player", "Player not found");
        return;
    }

    // Just acknowledge the stop - server confirms current position
    conn->send(network::make_player_stop_response(
        msg.seq, true, player->pos.x, player->pos.y));

    LOG_DEBUG(bridge, "Player {} stopped at ({}, {})", pid.value, player->pos.x, player->pos.y);
}

void game_handlers::handle_player_attack(connection_id conn_id, const network::json_message& msg) {
    auto* conn = require_in_game(conn_id, msg.seq);
    if (!conn) return;

    if (!players_) {
        send_error(conn_id, msg.seq, "internal_error", "Player system unavailable");
        return;
    }

    if (!combat_) {
        send_error(conn_id, msg.seq, "internal_error", "Combat system unavailable");
        return;
    }

    auto data_result = network::player_attack_request_data::from_json(msg.data);
    if (data_result.is_err()) {
        send_error(conn_id, msg.seq, "invalid_request", data_result.error());
        return;
    }

    auto& data = data_result.value();
    auto pid = conn->player();

    auto* attacker = players_->get_player(pid);
    if (!attacker) {
        send_error(conn_id, msg.seq, "invalid_player", "Player not found");
        return;
    }

    // Check if attacker is alive
    if (attacker->is_dead()) {
        network::attack_result_msg result{
            .hit = false,
            .target_id = data.target_id,
            .attacker_x = attacker->pos.x,
            .attacker_y = attacker->pos.y
        };
        conn->send(network::make_player_attack_response(msg.seq, false, &result, "attacker_dead"));
        return;
    }

    // Check if attacker has movement/action blocking status
    if (attacker->has_status(player::player_status::stunned) ||
        attacker->has_status(player::player_status::paralyzed) ||
        attacker->has_status(player::player_status::frozen)) {
        network::attack_result_msg result{
            .hit = false,
            .target_id = data.target_id,
            .attacker_x = attacker->pos.x,
            .attacker_y = attacker->pos.y
        };
        conn->send(network::make_player_attack_response(msg.seq, false, &result, "cannot_attack"));
        return;
    }

    // Only PvP for now (target must be a player)
    if (data.target_type != network::target_type::player) {
        network::attack_result_msg result{
            .hit = false,
            .target_id = data.target_id,
            .attacker_x = attacker->pos.x,
            .attacker_y = attacker->pos.y
        };
        conn->send(network::make_player_attack_response(msg.seq, false, &result, "invalid_target_type"));
        return;
    }

    // Get target player
    player_id target_pid{data.target_id};
    auto* target = players_->get_player(target_pid);
    if (!target) {
        network::attack_result_msg result{
            .hit = false,
            .target_id = data.target_id,
            .attacker_x = attacker->pos.x,
            .attacker_y = attacker->pos.y
        };
        conn->send(network::make_player_attack_response(msg.seq, false, &result, "target_not_found"));
        return;
    }

    // Check target is alive
    if (target->is_dead()) {
        network::attack_result_msg result{
            .hit = false,
            .target_id = data.target_id,
            .attacker_x = attacker->pos.x,
            .attacker_y = attacker->pos.y
        };
        conn->send(network::make_player_attack_response(msg.seq, false, &result, "target_dead"));
        return;
    }

    // Check same map
    if (attacker->current_map != target->current_map) {
        network::attack_result_msg result{
            .hit = false,
            .target_id = data.target_id,
            .attacker_x = attacker->pos.x,
            .attacker_y = attacker->pos.y
        };
        conn->send(network::make_player_attack_response(msg.seq, false, &result, "target_not_in_range"));
        return;
    }

    // Calculate distance
    int distance = attacker->pos.chebyshev_distance(target->pos);

    // Validate range based on attack type
    int max_range = 1;  // Melee
    if (data.type == network::attack_type::dash) {
        max_range = 2;  // Dash attack range
    }

    if (distance > max_range) {
        network::attack_result_msg result{
            .hit = false,
            .target_id = data.target_id,
            .attacker_x = attacker->pos.x,
            .attacker_y = attacker->pos.y
        };
        conn->send(network::make_player_attack_response(msg.seq, false, &result, "target_not_in_range"));
        return;
    }

    // Build attack event
    combat::attack_event attack;
    attack.attacker = entity::entity{pid.value};
    attack.defender = entity::entity{target_pid.value};
    attack.type = combat::damage_type::physical;
    attack.base_damage = 0;  // Let combat_system calculate from stats
    attack.is_skill = false;

    // Process the attack through combat system
    auto combat_result = combat_->process_attack(attack);

    // Build response
    network::attack_result_msg result{
        .hit = combat_result.hit.is_hit(),
        .critical = combat_result.hit.is_critical(),
        .damage = combat_result.hit.final_damage,
        .target_id = data.target_id,
        .target_hp = static_cast<int16_t>(target->hp),
        .target_hp_max = static_cast<int16_t>(target->computed.max_hp),
        .attacker_x = attacker->pos.x,
        .attacker_y = attacker->pos.y
    };

    // Send response to attacker
    conn->send(network::make_player_attack_response(msg.seq, true, &result));

    // Broadcast attack to nearby players
    constexpr int visibility_radius = 20;
    auto nearby = players_->get_players_in_range(pid, visibility_radius);

    // Create broadcast message
    auto broadcast_msg = network::make_combat_attack_broadcast(
        pid.value,
        target_pid.value,
        attacker->pos.x, attacker->pos.y,
        target->pos.x, target->pos.y,
        combat_result.hit.is_hit(),
        combat_result.hit.is_critical(),
        combat_result.hit.final_damage
    );

    for (auto other_id : nearby) {
        if (other_id == pid) continue;  // Attacker already got response

        auto* other = players_->get_player(other_id);
        if (!other || other->connection.value == 0) continue;

        auto* other_conn = ws_server_->get_connection(other->connection);
        if (other_conn && other_conn->is_open()) {
            other_conn->send(broadcast_msg);
        }
    }

    LOG_DEBUG(bridge, "Player {} attacked player {} (hit={}, crit={}, dmg={}, target_hp={})",
        pid.value, target_pid.value,
        combat_result.hit.is_hit(), combat_result.hit.is_critical(),
        combat_result.hit.final_damage, target->hp);
}

void game_handlers::handle_player_magic(connection_id conn_id, const network::json_message& msg) {
    auto* conn = require_in_game(conn_id, msg.seq);
    if (!conn) return;

    if (!players_) {
        send_error(conn_id, msg.seq, "internal_error", "Player system unavailable");
        return;
    }

    auto data_result = network::player_magic_request_data::from_json(msg.data);
    if (data_result.is_err()) {
        send_error(conn_id, msg.seq, "invalid_request", data_result.error());
        return;
    }

    auto& data = data_result.value();
    auto pid = conn->player();

    auto* player = players_->get_player(pid);
    if (!player) {
        send_error(conn_id, msg.seq, "invalid_player", "Player not found");
        return;
    }

    // TODO: Implement actual magic through magic_system
    // Placeholder response
    network::magic_result_msg result{
        .success = false,
        .spell_id = data.spell_id,
        .mana_cost = 0,
        .damage = 0,
        .heal = 0,
        .target_id = data.target_id,
        .caster_mp = static_cast<int16_t>(player->mp)
    };

    conn->send(network::make_player_magic_response(msg.seq, false, &result, "not_implemented"));
    LOG_DEBUG(bridge, "Player {} magic request (spell={}, target={})",
        pid.value, data.spell_id, data.target_id);
}

void game_handlers::handle_player_skill(connection_id conn_id, const network::json_message& msg) {
    auto* conn = require_in_game(conn_id, msg.seq);
    if (!conn) return;

    if (!players_) {
        send_error(conn_id, msg.seq, "internal_error", "Player system unavailable");
        return;
    }

    auto data_result = network::player_skill_request_data::from_json(msg.data);
    if (data_result.is_err()) {
        send_error(conn_id, msg.seq, "invalid_request", data_result.error());
        return;
    }

    auto& data = data_result.value();
    auto pid = conn->player();

    auto* player = players_->get_player(pid);
    if (!player) {
        send_error(conn_id, msg.seq, "invalid_player", "Player not found");
        return;
    }

    // TODO: Implement actual skill use through skill_system
    // Placeholder response
    network::skill_result_msg result{
        .success = false,
        .skill_id = data.skill_id,
        .effect_value = 0,
        .target_id = data.target_id
    };

    conn->send(network::make_player_skill_response(msg.seq, false, &result, "not_implemented"));
    LOG_DEBUG(bridge, "Player {} skill request (skill={}, target={})",
        pid.value, data.skill_id, data.target_id);
}

void game_handlers::handle_player_pickup(connection_id conn_id, const network::json_message& msg) {
    auto* conn = require_in_game(conn_id, msg.seq);
    if (!conn) return;

    if (!players_) {
        send_error(conn_id, msg.seq, "internal_error", "Player system unavailable");
        return;
    }

    auto data_result = network::player_pickup_request_data::from_json(msg.data);
    if (data_result.is_err()) {
        send_error(conn_id, msg.seq, "invalid_request", data_result.error());
        return;
    }

    auto& data = data_result.value();
    auto pid = conn->player();

    auto* player = players_->get_player(pid);
    if (!player) {
        send_error(conn_id, msg.seq, "invalid_player", "Player not found");
        return;
    }

    // TODO: Implement actual pickup through item_system/inventory_system
    // Placeholder response
    network::pickup_result_msg result{
        .success = false,
        .item_id = data.item_id,
        .item_name = "",
        .quantity = 0,
        .inventory_slot = 0
    };

    conn->send(network::make_player_pickup_response(msg.seq, false, &result, "not_implemented"));
    LOG_DEBUG(bridge, "Player {} pickup request (item={})", pid.value, data.item_id);
}

void game_handlers::handle_player_interact(connection_id conn_id, const network::json_message& msg) {
    auto* conn = require_in_game(conn_id, msg.seq);
    if (!conn) return;

    if (!players_) {
        send_error(conn_id, msg.seq, "internal_error", "Player system unavailable");
        return;
    }

    auto data_result = network::player_interact_request_data::from_json(msg.data);
    if (data_result.is_err()) {
        send_error(conn_id, msg.seq, "invalid_request", data_result.error());
        return;
    }

    auto& data = data_result.value();
    auto pid = conn->player();

    auto* player = players_->get_player(pid);
    if (!player) {
        send_error(conn_id, msg.seq, "invalid_player", "Player not found");
        return;
    }

    // TODO: Implement actual interaction through npc_system or world_subsystem
    // Placeholder response
    network::interact_result_msg result{
        .success = false,
        .target_id = data.target_id,
        .interaction_type = "unknown",
        .interaction_data = nlohmann::json::object()
    };

    conn->send(network::make_player_interact_response(msg.seq, false, &result, "not_implemented"));
    LOG_DEBUG(bridge, "Player {} interact request (target={}, type={})",
        pid.value, data.target_id, static_cast<int>(data.target_type));
}

void game_handlers::broadcast_position_update(player_id moved_player,
                                               int16_t x, int16_t y, int16_t direction,
                                               bool is_running)
{
    if (!players_ || !ws_server_) return;

    auto* player = players_->get_player(moved_player);
    if (!player) return;

    // Get players in range who can see this movement
    constexpr int visibility_radius = 20;
    auto nearby = players_->get_players_in_range(moved_player, visibility_radius);

    auto update_msg = network::make_player_position_update(
        moved_player.value, x, y, direction, is_running);

    for (auto other_id : nearby) {
        if (other_id == moved_player) continue;

        auto* other = players_->get_player(other_id);
        if (!other || other->connection.value == 0) continue;

        auto* other_conn = ws_server_->get_connection(other->connection);
        if (other_conn && other_conn->is_open()) {
            other_conn->send(update_msg);
        }
    }
}

void game_handlers::update_entity_visibility(player_id moved_player,
                                              const world::position& old_pos,
                                              const world::position& new_pos)
{
    if (!players_ || !ws_server_) return;

    auto* player = players_->get_player(moved_player);
    if (!player) return;

    constexpr int visibility_radius = 20;

    // Get all players who could possibly be affected
    // (those who were in range at old position or are in range at new position)
    auto old_nearby = players_->get_players_in_range(moved_player, visibility_radius + 5);

    // For each player in the expanded range, check visibility changes
    for (auto other_id : old_nearby) {
        if (other_id == moved_player) continue;

        auto* other = players_->get_player(other_id);
        if (!other || other->connection.value == 0) continue;

        // Check if this player was visible before and after
        bool was_visible = old_pos.chebyshev_distance(other->pos) <= visibility_radius;
        bool is_visible = new_pos.chebyshev_distance(other->pos) <= visibility_radius;

        auto* other_conn = ws_server_->get_connection(other->connection);
        if (!other_conn || !other_conn->is_open()) continue;

        if (!was_visible && is_visible) {
            // Player just came into view - send entity_spawn to other
            auto spawn_msg = network::make_entity_spawn(0, network::visible_entity_msg{
                .entity_id = moved_player.value,
                .type = "player",
                .name = player->name,
                .x = new_pos.x,
                .y = new_pos.y,
                .hp_percent = static_cast<int16_t>(player->hp_percent() * 100),
                .direction = static_cast<int16_t>(player->facing)
            });
            other_conn->send(spawn_msg);

            // Also send the other player info to the moving player
            auto* my_conn = ws_server_->get_connection(player->connection);
            if (my_conn && my_conn->is_open()) {
                auto other_spawn_msg = network::make_entity_spawn(0, network::visible_entity_msg{
                    .entity_id = other_id.value,
                    .type = "player",
                    .name = other->name,
                    .x = other->pos.x,
                    .y = other->pos.y,
                    .hp_percent = static_cast<int16_t>(other->hp_percent() * 100),
                    .direction = static_cast<int16_t>(other->facing)
                });
                my_conn->send(other_spawn_msg);
            }
        }
        else if (was_visible && !is_visible) {
            // Player just left view - send entity_despawn to other
            auto despawn_msg = network::make_entity_despawn(0, moved_player.value);
            other_conn->send(despawn_msg);

            // Also despawn the other player from the moving player's view
            auto* my_conn = ws_server_->get_connection(player->connection);
            if (my_conn && my_conn->is_open()) {
                auto other_despawn_msg = network::make_entity_despawn(0, other_id.value);
                my_conn->send(other_despawn_msg);
            }
        }
    }
}

void game_handlers::send_error(connection_id conn_id, uint32_t seq,
                                std::string_view error_code, std::string_view message)
{
    if (!ws_server_) return;

    auto* conn = ws_server_->get_connection(conn_id);
    if (!conn) return;

    auto response = network::make_error_response(seq, error_code, message);
    conn->send(response);
}

auto game_handlers::get_connection(connection_id conn_id) -> network::ws_connection* {
    if (!ws_server_) return nullptr;
    return ws_server_->get_connection(conn_id);
}

auto game_handlers::require_in_game(connection_id conn_id, uint32_t seq)
    -> network::ws_connection*
{
    auto* conn = get_connection(conn_id);
    if (!conn) {
        LOG_WARN(bridge, "Message from unknown connection {}", conn_id.value);
        return nullptr;
    }

    if (conn->state() != network::ws_connection_state::in_game) {
        LOG_WARN(bridge, "In-game request from non-in-game connection {}", conn_id.value);
        send_error(conn_id, seq, "not_in_game", "You must be in-game to perform this action");
        return nullptr;
    }

    return conn;
}

// ========== Chat Handling ==========

namespace {

// Parse chat channel from prefix or explicit channel name
auto parse_chat_channel(std::string_view content, const std::optional<std::string>& explicit_channel)
    -> std::pair<social::chat_channel, std::string>
{
    // If explicit channel provided, use that
    if (explicit_channel.has_value()) {
        const auto& ch = *explicit_channel;
        if (ch == "local") return {social::chat_channel::local, std::string(content)};
        if (ch == "shout") return {social::chat_channel::shout, std::string(content)};
        if (ch == "guild") return {social::chat_channel::guild, std::string(content)};
        if (ch == "party") return {social::chat_channel::party, std::string(content)};
        if (ch == "whisper") return {social::chat_channel::whisper, std::string(content)};
        if (ch == "global") return {social::chat_channel::global, std::string(content)};
        if (ch == "trade") return {social::chat_channel::trade, std::string(content)};
        if (ch == "faction") return {social::chat_channel::faction, std::string(content)};
        // Default to local for unknown channels
        return {social::chat_channel::local, std::string(content)};
    }

    // Check for prefix-based channel
    if (!content.empty()) {
        char prefix = content[0];
        std::string msg_content = std::string(content.substr(1));

        switch (prefix) {
            case '!':  // Shout - server-wide
                return {social::chat_channel::shout, msg_content};
            case '@':  // Guild
                return {social::chat_channel::guild, msg_content};
            case '$':  // Party
                return {social::chat_channel::party, msg_content};
            case '#':  // Whisper (legacy - normally use recipient field)
                return {social::chat_channel::whisper, msg_content};
            case '^':  // Global guild (treat as shout for single server)
                return {social::chat_channel::shout, msg_content};
            case '~':  // Trade channel
                return {social::chat_channel::trade, msg_content};
            default:
                break;
        }
    }

    // Default to local chat
    return {social::chat_channel::local, std::string(content)};
}

auto channel_to_string(social::chat_channel channel) -> std::string {
    switch (channel) {
        case social::chat_channel::local: return "local";
        case social::chat_channel::global: return "global";
        case social::chat_channel::guild: return "guild";
        case social::chat_channel::party: return "party";
        case social::chat_channel::whisper: return "whisper";
        case social::chat_channel::trade: return "trade";
        case social::chat_channel::shout: return "shout";
        case social::chat_channel::alliance: return "alliance";
        case social::chat_channel::faction: return "faction";
        case social::chat_channel::gm: return "gm";
        case social::chat_channel::system: return "system";
        default: return "unknown";
    }
}

auto format_timestamp(std::chrono::system_clock::time_point tp) -> std::string {
    auto time_t = std::chrono::system_clock::to_time_t(tp);
    std::tm tm_buf{};
#ifdef _WIN32
    gmtime_s(&tm_buf, &time_t);
#else
    gmtime_r(&time_t, &tm_buf);
#endif
    std::ostringstream oss;
    oss << std::put_time(&tm_buf, "%Y-%m-%dT%H:%M:%SZ");
    return oss.str();
}

auto filter_result_to_string(social::filter_result result) -> std::string_view {
    switch (result) {
        case social::filter_result::allowed: return "allowed";
        case social::filter_result::censored: return "censored";
        case social::filter_result::blocked: return "blocked";
        case social::filter_result::rate_limited: return "rate_limited";
        default: return "unknown";
    }
}

}  // namespace

void game_handlers::handle_chat_message(connection_id conn_id, const network::json_message& msg) {
    auto* conn = require_in_game(conn_id, msg.seq);
    if (!conn) return;

    if (!social_ || !players_) {
        send_error(conn_id, msg.seq, "internal_error", "Chat system unavailable");
        return;
    }

    // Parse request
    auto data_result = network::chat_message_request_data::from_json(msg.data);
    if (data_result.is_err()) {
        send_error(conn_id, msg.seq, "invalid_request", data_result.error());
        return;
    }

    auto& data = data_result.value();
    auto pid = conn->player();

    auto* player = players_->get_player(pid);
    if (!player) {
        send_error(conn_id, msg.seq, "invalid_player", "Player not found");
        return;
    }

    // Parse channel and extract message content
    auto [channel, content] = parse_chat_channel(data.content, data.channel);

    // Validate content is not empty after prefix removal
    if (content.empty()) {
        conn->send(network::make_chat_message_response(msg.seq, false, "empty_message"));
        return;
    }

    // Handle based on channel type
    social::filter_result result;

    switch (channel) {
        case social::chat_channel::local:
            result = social_->send_local_chat(pid, content,
                player->current_map, player->pos.x, player->pos.y);
            break;

        case social::chat_channel::shout:
            // Shout is server-wide
            result = social_->send_global_chat(pid, content);
            break;

        case social::chat_channel::guild:
            result = social_->send_guild_chat(pid, content);
            break;

        case social::chat_channel::party:
            result = social_->send_party_chat(pid, content);
            break;

        case social::chat_channel::whisper: {
            // Need recipient for whisper
            if (!data.recipient_name.has_value() || data.recipient_name->empty()) {
                conn->send(network::make_chat_message_response(msg.seq, false, "no_recipient"));
                return;
            }

            // Find recipient by name
            auto* recipient = players_->get_player_by_name(*data.recipient_name);
            if (!recipient) {
                conn->send(network::make_chat_message_response(msg.seq, false, "recipient_not_found"));
                return;
            }

            result = social_->send_whisper(pid, recipient->id, content);
            break;
        }

        case social::chat_channel::global:
            result = social_->send_global_chat(pid, content);
            break;

        case social::chat_channel::trade:
            // Trade channel is global for now
            result = social_->send_global_chat(pid, content);
            break;

        default:
            result = social_->send_local_chat(pid, content,
                player->current_map, player->pos.x, player->pos.y);
            break;
    }

    // Send response to sender
    if (result == social::filter_result::allowed || result == social::filter_result::censored) {
        conn->send(network::make_chat_message_response(msg.seq, true));
    } else {
        conn->send(network::make_chat_message_response(msg.seq, false,
            filter_result_to_string(result)));
    }

    LOG_DEBUG(bridge, "Player {} sent {} chat: {}",
        pid.value, channel_to_string(channel), content.substr(0, 50));
}

void game_handlers::handle_command(connection_id conn_id, const network::json_message& msg) {
    auto* conn = require_in_game(conn_id, msg.seq);
    if (!conn) return;

    if (!players_) {
        send_error(conn_id, msg.seq, "internal_error", "Player system unavailable");
        return;
    }

    // Parse request
    auto data_result = network::command_request_data::from_json(msg.data);
    if (data_result.is_err()) {
        send_error(conn_id, msg.seq, "invalid_request", data_result.error());
        return;
    }

    auto& data = data_result.value();
    auto pid = conn->player();

    auto* player = players_->get_player(pid);
    if (!player) {
        send_error(conn_id, msg.seq, "invalid_player", "Player not found");
        return;
    }

    LOG_DEBUG(bridge, "Player {} command: {} args={}", pid.value, data.command, data.args.size());

    // Simple built-in commands (available to all players)
    if (data.command == "online") {
        // Return online player count
        auto count = players_->active_player_count();
        conn->send(network::make_command_response(msg.seq, true, data.command,
            std::to_string(count) + " players online",
            nlohmann::json{{"count", count}}));
        return;
    }

    if (data.command == "time") {
        // Return server time
        auto now = std::chrono::system_clock::now();
        conn->send(network::make_command_response(msg.seq, true, data.command,
            format_timestamp(now),
            nlohmann::json{{"timestamp", format_timestamp(now)}}));
        return;
    }

    if (data.command == "pos" || data.command == "position") {
        // Return player position
        std::string map_name = "unknown";
        if (world_) {
            auto* current_map = world_->get_map(player->current_map);
            if (current_map) {
                map_name = std::string(current_map->name());
            }
        }
        conn->send(network::make_command_response(msg.seq, true, data.command,
            "Position: " + map_name + " (" + std::to_string(player->pos.x) + ", " + std::to_string(player->pos.y) + ")",
            nlohmann::json{
                {"x", player->pos.x},
                {"y", player->pos.y},
                {"map", player->current_map.value},
                {"map_name", map_name}
            }));
        return;
    }

    // Route to admin system for admin commands
    if (admin_) {
        // Build command string: /<command> [args...]
        std::string cmd_string = "/" + data.command;
        for (const auto& arg : data.args) {
            cmd_string += " " + arg;
        }

        auto result = admin_->execute(pid, cmd_string);

        // Send response
        conn->send(network::make_command_response(msg.seq, result.success, data.command,
            result.message));
        return;
    }

    // Unknown command (no admin system available)
    conn->send(network::make_command_response(msg.seq, false, data.command,
        "Unknown command: " + data.command));
}

void game_handlers::on_chat_message(const social::chat_message_event& event) {
    if (!ws_server_ || !players_) return;

    const auto& msg = event.message;

    // Build broadcast data
    network::chat_message_broadcast_data broadcast;
    broadcast.channel = channel_to_string(msg.channel);
    broadcast.sender_id = msg.sender.value;
    broadcast.sender_name = msg.sender_name;
    broadcast.content = msg.content;
    broadcast.timestamp = format_timestamp(msg.timestamp);

    // Add flags
    if (social::has_flag(msg.flags, social::chat_flags::emote)) {
        broadcast.flags.push_back("emote");
    }
    if (social::has_flag(msg.flags, social::chat_flags::censored)) {
        broadcast.flags.push_back("censored");
    }
    if (social::has_flag(msg.flags, social::chat_flags::system)) {
        broadcast.flags.push_back("system");
    }
    if (social::has_flag(msg.flags, social::chat_flags::gm)) {
        broadcast.flags.push_back("gm");
    }

    // Route based on channel
    switch (msg.channel) {
        case social::chat_channel::local:
            // Send to nearby players
            send_chat_to_nearby(msg.sender, 15, broadcast);  // 15 tile range
            break;

        case social::chat_channel::shout:
        case social::chat_channel::global: {
            // Send to all online players
            auto all_players = players_->get_all_players();
            for (auto pid : all_players) {
                send_chat_to_player(pid, broadcast);
            }
            break;
        }

        case social::chat_channel::guild: {
            // Send to guild members
            if (!social_) break;

            auto guild_id = social_->get_player_guild(msg.sender);
            if (!guild_id.is_valid()) break;

            auto* guild = social_->get_guild(guild_id);
            if (!guild) break;

            for (const auto& member : guild->members) {
                send_chat_to_player(member.player, broadcast);
            }
            break;
        }

        case social::chat_channel::party: {
            // Send to party members
            if (!social_) break;

            auto party_id = social_->get_player_party(msg.sender);
            if (!party_id.is_valid()) break;

            auto* party = social_->get_party(party_id);
            if (!party) break;

            for (const auto& member : party->members) {
                send_chat_to_player(member.player, broadcast);
            }
            break;
        }

        case social::chat_channel::whisper: {
            // Send to sender (confirmation) and recipient
            broadcast.recipient_name = msg.recipient_name;
            send_chat_to_player(msg.sender, broadcast);  // Echo to sender
            send_chat_to_player(msg.recipient, broadcast);  // Send to recipient
            break;
        }

        case social::chat_channel::system: {
            // System messages to specific recipient
            send_chat_to_player(msg.recipient, broadcast);
            break;
        }

        case social::chat_channel::trade:
        case social::chat_channel::faction:
        case social::chat_channel::alliance: {
            // Broadcast to all for now
            auto all_players = players_->get_all_players();
            for (auto pid : all_players) {
                send_chat_to_player(pid, broadcast);
            }
            break;
        }

        default:
            LOG_WARN(bridge, "Unknown chat channel: {}", static_cast<int>(msg.channel));
            break;
    }
}

void game_handlers::send_chat_to_player(player_id target,
                                         const network::chat_message_broadcast_data& data) {
    if (!ws_server_ || !players_) return;

    auto* player = players_->get_player(target);
    if (!player || player->connection.value == 0) return;

    auto* conn = ws_server_->get_connection(player->connection);
    if (!conn || !conn->is_open()) return;

    // Check if player has this channel enabled
    if (social_) {
        auto* settings = social_->get_chat_settings(target);
        if (settings) {
            // Convert string channel back to enum for settings check
            social::chat_channel ch = social::chat_channel::local;
            if (data.channel == "local") ch = social::chat_channel::local;
            else if (data.channel == "global") ch = social::chat_channel::global;
            else if (data.channel == "guild") ch = social::chat_channel::guild;
            else if (data.channel == "party") ch = social::chat_channel::party;
            else if (data.channel == "whisper") ch = social::chat_channel::whisper;
            else if (data.channel == "trade") ch = social::chat_channel::trade;
            else if (data.channel == "shout") ch = social::chat_channel::shout;
            else if (data.channel == "system") ch = social::chat_channel::system;

            if (!settings->is_channel_enabled(ch) && ch != social::chat_channel::system) {
                return;  // Player has this channel disabled
            }

            // Check if sender is blocked
            if (settings->is_player_blocked(player_id{data.sender_id})) {
                return;  // Player has blocked the sender
            }
        }
    }

    conn->send(network::make_chat_message_broadcast(data));
}

void game_handlers::send_chat_to_nearby(player_id sender, int16_t range,
                                         const network::chat_message_broadcast_data& data) {
    if (!players_) return;

    auto nearby = players_->get_players_in_range(sender, range);
    for (auto pid : nearby) {
        send_chat_to_player(pid, data);
    }
}

// ========== View Range Handling ==========

void game_handlers::handle_set_view_range(connection_id conn_id, const network::json_message& msg) {
    auto* conn = require_in_game(conn_id, msg.seq);
    if (!conn) return;

    if (!players_) {
        send_error(conn_id, msg.seq, "internal_error", "Player system unavailable");
        return;
    }

    auto data_result = network::set_view_range_request_data::from_json(msg.data);
    if (data_result.is_err()) {
        send_error(conn_id, msg.seq, "invalid_request", data_result.error());
        return;
    }

    auto& data = data_result.value();
    auto pid = conn->player();

    auto* player = players_->get_player(pid);
    if (!player) {
        send_error(conn_id, msg.seq, "invalid_player", "Player not found");
        return;
    }

    player->visibility_radius = network::calculate_visibility_radius(data.screen_width, data.screen_height);

    LOG_DEBUG(bridge, "Player {} updated visibility radius to {} ({}x{})",
        pid.value, player->visibility_radius, data.screen_width, data.screen_height);
}

// ========== Teleportation Handling ==========

void game_handlers::execute_player_teleport(player_id pid, connection_id conn_id, uint32_t seq,
                                             const std::string& dest_map,
                                             const world::position& dest_pos,
                                             world::direction dest_dir)
{
    if (!players_ || !ws_server_ || !world_) {
        send_error(conn_id, seq, "internal_error", "System unavailable");
        return;
    }

    auto* player = players_->get_player(pid);
    if (!player) {
        send_error(conn_id, seq, "invalid_player", "Player not found");
        return;
    }

    // Store old position for despawn notifications
    auto old_map_id = player->current_map;
    auto old_pos = player->pos;

    // Execute the teleport
    auto teleport_result = players_->execute_teleport(pid, dest_map, dest_pos, dest_dir);
    if (!teleport_result.success) {
        send_error(conn_id, seq, "teleport_failed", teleport_result.error);
        return;
    }

    // Check if this is a cross-map teleport
    bool is_cross_map = (old_map_id != teleport_result.new_map);

    // Despawn from players who could see OLD position
    auto old_viewers = players_->get_players_who_can_see(old_map_id, old_pos);
    auto despawn_msg = network::make_entity_despawn(0, pid.value);
    for (auto other_id : old_viewers) {
        if (other_id == pid) continue;

        auto* other = players_->get_player(other_id);
        if (!other || other->connection.value == 0) continue;

        auto* other_conn = ws_server_->get_connection(other->connection);
        if (other_conn && other_conn->is_open()) {
            other_conn->send(despawn_msg);
        }
    }

    // Build visible entities at destination using teleporting player's visibility
    auto visible_entities = build_visible_entities_at(teleport_result.new_map, dest_pos,
                                                       player->visibility_radius);

    // Build and send player_teleport message
    network::player_teleport_msg teleport_msg{
        .dest_map = dest_map,
        .dest_x = dest_pos.x,
        .dest_y = dest_pos.y,
        .dest_dir = static_cast<int16_t>(dest_dir),
        .entities = visible_entities
    };

    auto* conn = ws_server_->get_connection(conn_id);
    if (conn && conn->is_open()) {
        conn->send(network::make_player_teleport(seq, teleport_msg));

        // If cross-map, also send map_teleporters for the new map
        if (is_cross_map) {
            auto* new_map = world_->get_map(teleport_result.new_map);
            if (new_map) {
                send_map_teleporters(conn_id, *new_map);
            }
        }
    }

    // Spawn to players who can see NEW position
    auto new_viewers = players_->get_players_who_can_see(teleport_result.new_map, dest_pos);

    network::visible_entity_msg spawn_entity{
        .entity_id = pid.value,
        .type = "player",
        .name = player->name,
        .x = dest_pos.x,
        .y = dest_pos.y,
        .hp_percent = static_cast<int16_t>(player->hp_percent() * 100),
        .direction = static_cast<int16_t>(dest_dir)
    };
    auto spawn_msg = network::make_entity_spawn(0, spawn_entity);

    for (auto other_id : new_viewers) {
        if (other_id == pid) continue;

        auto* other = players_->get_player(other_id);
        if (!other || other->connection.value == 0) continue;

        auto* other_conn = ws_server_->get_connection(other->connection);
        if (other_conn && other_conn->is_open()) {
            other_conn->send(spawn_msg);
        }
    }

    LOG_INFO(bridge, "Player {} teleported to {} ({}, {}), {} entities visible",
        pid.value, dest_map, dest_pos.x, dest_pos.y, visible_entities.size());
}

void game_handlers::send_map_teleporters(connection_id conn_id, const world::map& map) {
    if (!ws_server_) return;

    auto* conn = ws_server_->get_connection(conn_id);
    if (!conn || !conn->is_open()) return;

    const auto& teleports = map.get_all_teleports();

    network::map_teleporters_msg teleporters_msg;
    teleporters_msg.map_name = std::string(map.name());

    for (const auto& [pos, dest] : teleports) {
        network::teleporter_info_msg tp_info{
            .id = (static_cast<uint32_t>(pos.x) << 16) | static_cast<uint32_t>(static_cast<uint16_t>(pos.y)),
            .x = pos.x,
            .y = pos.y,
            .dest_map = dest.dest_map,
            .dest_x = dest.dest_x,
            .dest_y = dest.dest_y,
            .dest_dir = static_cast<int16_t>(dest.dest_dir)
        };
        teleporters_msg.teleporters.push_back(tp_info);
    }

    conn->send(network::make_map_teleporters(teleporters_msg));

    LOG_DEBUG(bridge, "Sent {} teleporters for map {} to connection {}",
        teleporters_msg.teleporters.size(), map.name(), conn_id.value);
}

void game_handlers::broadcast_teleporter_update(map_id map, const std::string& action,
                                                 const world::position& pos,
                                                 const world::teleport_dest* dest)
{
    if (!players_ || !ws_server_ || !world_) return;

    auto* m = world_->get_map(map);
    if (!m) return;

    network::teleporter_update_msg update_msg;
    update_msg.action = action;
    update_msg.map_name = std::string(m->name());
    update_msg.teleporter.id = (static_cast<uint32_t>(pos.x) << 16) |
                                static_cast<uint32_t>(static_cast<uint16_t>(pos.y));
    update_msg.teleporter.x = pos.x;
    update_msg.teleporter.y = pos.y;

    if (dest) {
        update_msg.teleporter.dest_map = dest->dest_map;
        update_msg.teleporter.dest_x = dest->dest_x;
        update_msg.teleporter.dest_y = dest->dest_y;
        update_msg.teleporter.dest_dir = static_cast<int16_t>(dest->dest_dir);
    }

    auto msg = network::make_teleporter_update(update_msg);

    // Send to all players on this map
    players_->for_each_player([&](player_id pid, const player::player& p) {
        if (p.current_map != map) return;
        if (p.connection.value == 0) return;

        auto* conn = ws_server_->get_connection(p.connection);
        if (conn && conn->is_open()) {
            conn->send(msg);
        }
    });

    LOG_DEBUG(bridge, "Broadcast teleporter {} at ({},{}) on map {}",
        action, pos.x, pos.y, m->name());
}

auto game_handlers::build_visible_entities_at(map_id map, const world::position& pos,
                                               int visibility_radius)
    -> std::vector<network::visible_entity_msg>
{
    std::vector<network::visible_entity_msg> entities;

    if (!players_ || !world_) return entities;

    auto* m = world_->get_map(map);
    if (!m) return entities;

    // Get all entities in the visibility range from spatial index
    auto nearby_entities = m->get_entities_in_range(pos, visibility_radius);

    for (auto entity_id : nearby_entities) {
        // Check if this is a player
        player_id pid{entity_id.value};
        auto* p = players_->get_player(pid);
        if (p) {
            entities.push_back(network::visible_entity_msg{
                .entity_id = pid.value,
                .type = "player",
                .name = p->name,
                .x = p->pos.x,
                .y = p->pos.y,
                .hp_percent = static_cast<int16_t>(p->hp_percent() * 100),
                .direction = static_cast<int16_t>(p->facing)
            });
        }
        // TODO: Also include NPCs from npc_system when available
    }

    return entities;
}

// ========== Combat Event Callbacks ==========

void game_handlers::on_damage_dealt(const combat::damage_event& event) {
    if (!players_ || !ws_server_) return;

    // Only broadcast for player targets for now
    player_id target_pid{event.target.id};
    auto* target = players_->get_player(target_pid);
    if (!target) return;

    // Broadcast HP update to players who can see the target
    broadcast_hp_update(target_pid, target->hp, target->computed.max_hp);
}

void game_handlers::on_entity_death(const combat::death_event& event) {
    if (!players_ || !ws_server_) return;

    // Only handle player deaths for now
    player_id victim_pid{event.victim.id};
    auto* victim = players_->get_player(victim_pid);
    if (!victim) return;

    player_id killer_pid{event.killer.id};

    // Broadcast death to nearby players
    broadcast_entity_death(victim_pid, killer_pid);

    // Handle respawn
    handle_player_death(victim_pid);
}

void game_handlers::handle_player_death(player_id pid) {
    if (!players_ || !ws_server_ || !world_ || !combat_) return;

    auto* player = players_->get_player(pid);
    if (!player) return;

    // Determine spawn point based on faction
    std::string spawn_map;
    world::position spawn_pos{18, 18};  // Default center position

    switch (player->faction) {
        case faction::aresden:
            spawn_map = "aresden";
            spawn_pos = {18, 18};
            break;
        case faction::elvine:
            spawn_map = "elvine";
            spawn_pos = {18, 18};
            break;
        case faction::neutral:
        default:
            spawn_map = "default";
            spawn_pos = {18, 18};
            break;
    }

    // Check if map exists, fall back to current map spawn
    if (world_) {
        auto* spawn_map_ptr = world_->get_map_by_name(spawn_map);
        if (!spawn_map_ptr) {
            // Fall back to current map
            auto* current_map = world_->get_map(player->current_map);
            if (current_map) {
                spawn_map = std::string(current_map->name());
            }
        }
    }

    // Restore HP to 50%
    player->hp = player->computed.max_hp / 2;

    // Set 3-second invulnerability
    entity::entity player_entity{pid.value};
    combat_->set_invulnerable(player_entity, 3000);

    // Execute teleport to spawn
    auto* conn = ws_server_->get_connection(player->connection);
    if (!conn) return;

    execute_player_teleport(pid, player->connection, 0, spawn_map, spawn_pos,
                            world::direction::south);

    LOG_INFO(bridge, "Player {} died and respawned at {} ({}, {}) with {} HP",
        pid.value, spawn_map, spawn_pos.x, spawn_pos.y, player->hp);
}

void game_handlers::broadcast_hp_update(player_id target, int32_t hp, int32_t hp_max) {
    if (!players_ || !ws_server_) return;

    auto* player = players_->get_player(target);
    if (!player) return;

    auto hp_msg = network::make_entity_hp_update(target.value, hp, hp_max);

    // Broadcast to players who can see this target
    auto viewers = players_->get_players_who_can_see(player->current_map, player->pos);

    for (auto other_id : viewers) {
        auto* other = players_->get_player(other_id);
        if (!other || other->connection.value == 0) continue;

        auto* other_conn = ws_server_->get_connection(other->connection);
        if (other_conn && other_conn->is_open()) {
            other_conn->send(hp_msg);
        }
    }
}

void game_handlers::broadcast_entity_death(player_id victim, player_id killer) {
    if (!players_ || !ws_server_) return;

    auto* victim_player = players_->get_player(victim);
    if (!victim_player) return;

    auto death_msg = network::make_entity_death(
        victim.value, killer.value,
        victim_player->pos.x, victim_player->pos.y
    );

    // Broadcast to players who can see the victim's position
    auto viewers = players_->get_players_who_can_see(victim_player->current_map, victim_player->pos);

    for (auto other_id : viewers) {
        auto* other = players_->get_player(other_id);
        if (!other || other->connection.value == 0) continue;

        auto* other_conn = ws_server_->get_connection(other->connection);
        if (other_conn && other_conn->is_open()) {
            other_conn->send(death_msg);
        }
    }
}

// ========== NPC Broadcast Methods ==========

void game_handlers::broadcast_npc_spawn(const npc::npc& n) {
    if (!players_ || !ws_server_) return;

    network::npc_spawn_data data{
        .entity_id = n.entity_id.id,
        .template_id = n.template_id.value,
        .name = n.name,
        .x = n.pos.x,
        .y = n.pos.y,
        .direction = static_cast<uint8_t>(n.facing),
        .hp = n.hp,
        .max_hp = n.max_hp,
        .level = n.level
    };

    auto msg = network::make_npc_spawn_message(data);

    // Send to all players who can see this position
    auto players = players_->get_players_who_can_see(n.current_map, n.pos);
    for (auto pid : players) {
        auto* p = players_->get_player(pid);
        if (!p || p->connection.value == 0) continue;

        auto* conn = ws_server_->get_connection(p->connection);
        if (conn && conn->is_open()) {
            conn->send(msg);
        }
    }

    LOG_DEBUG(bridge, "Broadcast NPC spawn: {} '{}' at ({}, {})",
        n.entity_id.id, n.name, n.pos.x, n.pos.y);
}

void game_handlers::broadcast_npc_move(const npc::npc& n) {
    if (!players_ || !ws_server_) return;

    network::npc_move_data data{
        .entity_id = n.entity_id.id,
        .x = n.pos.x,
        .y = n.pos.y,
        .direction = static_cast<uint8_t>(n.facing)
    };

    auto msg = network::make_npc_move_message(data);

    auto players = players_->get_players_who_can_see(n.current_map, n.pos);
    for (auto pid : players) {
        auto* p = players_->get_player(pid);
        if (!p || p->connection.value == 0) continue;

        auto* conn = ws_server_->get_connection(p->connection);
        if (conn && conn->is_open()) {
            conn->send(msg);
        }
    }
}

void game_handlers::broadcast_npc_attack(const npc::npc& n, entity::entity target, int32_t damage) {
    if (!players_ || !ws_server_) return;

    network::npc_attack_data data{
        .attacker_id = n.entity_id.id,
        .target_id = target.id,
        .damage = damage,
        .is_critical = false  // NPCs don't crit for now
    };

    auto msg = network::make_npc_attack_message(data);

    auto players = players_->get_players_who_can_see(n.current_map, n.pos);
    for (auto pid : players) {
        auto* p = players_->get_player(pid);
        if (!p || p->connection.value == 0) continue;

        auto* conn = ws_server_->get_connection(p->connection);
        if (conn && conn->is_open()) {
            conn->send(msg);
        }
    }
}

void game_handlers::broadcast_npc_death(const npc::npc& n, entity::entity killer) {
    if (!players_ || !ws_server_) return;

    network::npc_death_data data{
        .entity_id = n.entity_id.id,
        .killer_id = killer.id,
        .x = n.pos.x,
        .y = n.pos.y
    };

    auto msg = network::make_npc_death_message(data);

    auto players = players_->get_players_who_can_see(n.current_map, n.pos);
    for (auto pid : players) {
        auto* p = players_->get_player(pid);
        if (!p || p->connection.value == 0) continue;

        auto* conn = ws_server_->get_connection(p->connection);
        if (conn && conn->is_open()) {
            conn->send(msg);
        }
    }

    LOG_DEBUG(bridge, "Broadcast NPC death: {} '{}' killed by {}",
        n.entity_id.id, n.name, killer.id);
}

void game_handlers::broadcast_npc_hp_update(const npc::npc& n) {
    if (!players_ || !ws_server_) return;

    // Use entity_hp_update message for NPCs too
    auto msg = network::make_entity_hp_update(n.entity_id.id, n.hp, n.max_hp);

    auto players = players_->get_players_who_can_see(n.current_map, n.pos);
    for (auto pid : players) {
        auto* p = players_->get_player(pid);
        if (!p || p->connection.value == 0) continue;

        auto* conn = ws_server_->get_connection(p->connection);
        if (conn && conn->is_open()) {
            conn->send(msg);
        }
    }
}

}  // namespace hb::bridge
