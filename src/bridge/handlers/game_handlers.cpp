// game_handlers.cpp
// Message handlers for in-game protocol implementation

// Include platform header first to define NOMINMAX before Windows headers
#include "platform/platform.h"

#include "bridge/handlers/game_handlers.h"
#include "network/websocket_server.h"
#include "player/player_system.h"
#include "world/world_subsystem.h"
#include "core/logger.h"

namespace hb::bridge {

game_handlers::game_handlers() = default;
game_handlers::~game_handlers() = default;

void game_handlers::initialize(network::websocket_server* ws_server,
                                player::player_system* players,
                                world::world_subsystem* world) {
    ws_server_ = ws_server;
    players_ = players;
    world_ = world;
    LOG_INFO(bridge, "Game handlers initialized");
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
            // Handle teleport - this is a map change, send world_init
            auto response = network::make_player_move_response(
                msg.seq, true,
                move_result.teleport_dest_pos.x,
                move_result.teleport_dest_pos.y,
                static_cast<int16_t>(move_result.teleport_dest_dir));
            conn->send(response);

            // TODO: Send world_init with new map entities
            LOG_INFO(bridge, "Player {} teleported to {} ({}, {})",
                pid.value, move_result.teleport_dest_map,
                move_result.teleport_dest_pos.x, move_result.teleport_dest_pos.y);
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

    auto data_result = network::player_attack_request_data::from_json(msg.data);
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

    // TODO: Implement actual combat through combat_system
    // For now, just validate the request and return a placeholder response

    // Validate attack type requirements
    if (data.type == network::attack_type::dash) {
        // Dash requires 100% skill and 1 tile gap
        // TODO: Check skill level
        // TODO: Check distance to target is exactly 2 tiles
    } else if (data.type == network::attack_type::super) {
        // Super requires 100% skill and charges
        // TODO: Check skill level
        // TODO: Check super attack charges
    }

    // Placeholder response - actual implementation needs combat_system integration
    network::attack_result_msg result{
        .hit = false,
        .critical = false,
        .damage = 0,
        .target_id = data.target_id,
        .target_hp = 0,
        .target_hp_max = 0,
        .attacker_x = player->pos.x,
        .attacker_y = player->pos.y
    };

    conn->send(network::make_player_attack_response(msg.seq, false, &result, "not_implemented"));
    LOG_DEBUG(bridge, "Player {} attack request (type={}, target={})",
        pid.value, static_cast<int>(data.type), data.target_id);
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

}  // namespace hb::bridge
