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
        case network::json_message_type::player_move_request:
            handle_player_move(conn_id, msg);
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

    // Store old position for visibility calculations
    auto old_pos = player->pos;

    // Attempt to move
    world::position target_pos{data.target_x, data.target_y};
    auto dir = static_cast<world::direction>(data.direction);

    auto move_result = players_->try_move(pid, target_pos, dir);

    switch (move_result.result) {
        case player::player_system::move_result::success: {
            // Send success response to moving player
            auto response = network::make_player_move_response(
                msg.seq, true, data.target_x, data.target_y, data.direction);
            conn->send(response);

            // Broadcast position to nearby players
            broadcast_position_update(pid, data.target_x, data.target_y, data.direction);

            // Update entity visibility for all affected players
            update_entity_visibility(pid, old_pos, target_pos);

            LOG_DEBUG(bridge, "Player {} moved to ({}, {})",
                pid.value, data.target_x, data.target_y);
            break;
        }

        case player::player_system::move_result::teleport: {
            // Handle teleport - this is a map change, send world_init
            // For now, treat as success at teleport destination
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

void game_handlers::broadcast_position_update(player_id moved_player,
                                               int16_t x, int16_t y, int16_t direction)
{
    if (!players_ || !ws_server_) return;

    auto* player = players_->get_player(moved_player);
    if (!player) return;

    // Get players in range who can see this movement
    constexpr int visibility_radius = 20;
    auto nearby = players_->get_players_in_range(moved_player, visibility_radius);

    auto update_msg = network::make_player_position_update(
        moved_player.value, x, y, direction);

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
