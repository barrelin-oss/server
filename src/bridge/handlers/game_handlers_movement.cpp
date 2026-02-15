#include "platform/platform.h"
#include "bridge/handlers/game_handlers.h"
#include "bridge/handlers/broadcast_util.h"
#include "bridge/handlers/entity_builders.h"
#include "network/websocket_server.h"
#include "player/player_system.h"
#include "world/world_subsystem.h"
#include "npc/npc_system.h"
#include "npc/npc.h"
#include "item/item_system.h"
#include "registry/item_registry.h"
#include "effect/effect_system.h"
#include "skill/skill_system.h"
#include "scheduler/scheduler.h"
#include "core/subsystem.h"
#include "core/logger.h"
#include "perf/perf_stats.h"

namespace hb::bridge
{

void game_handlers::handle_player_move(connection_id conn_id, const network::json_message& msg)
{
    auto* conn = require_in_game(conn_id, msg.seq);
    if (!conn)
        return;

    if (!players_)
    {
        send_error(conn_id, msg.seq, "internal_error", "Player system unavailable");
        return;
    }

    // Parse request
    auto data_result = network::player_move_request_data::from_json(msg.data);
    if (data_result.is_err())
    {
        send_error(conn_id, msg.seq, "invalid_request", data_result.error());
        return;
    }

    auto& data = data_result.value();
    auto pid = conn->player();

    // Get current player
    auto* player = players_->get_player(pid);
    if (!player)
    {
        send_error(conn_id, msg.seq, "invalid_player", "Player not found");
        return;
    }

    // Dead players cannot move
    if (player->is_dead())
    {
        send_error(conn_id, msg.seq, "dead", "Cannot move while dead");
        return;
    }

    // Validate client position matches server position (anti-cheat)
    if (std::abs(player->pos.x - data.x) > 1 || std::abs(player->pos.y - data.y) > 1)
    {
        LOG_WARN(bridge,
                 "Player {} position mismatch: client ({},{}) server ({},{})",
                 pid.value,
                 data.x,
                 data.y,
                 player->pos.x,
                 player->pos.y);
        // Clear destination - desync interrupts movement
        conn->clear_destination();
        // Send correction - client should resync
        conn->send(network::make_player_move_response(
            msg.seq, false, player->pos.x, player->pos.y, static_cast<int16_t>(player->facing), "position_desync"));
        return;
    }

    // Store old position for visibility calculations
    auto old_pos = player->pos;

    // Calculate target position from client's reported position
    // This ensures exactly 1-tile movement from where the client thinks they are
    // The desync check above ensures client position is within tolerance
    auto dir = static_cast<world::direction>(data.direction & 7); // Clamp to 0-7
    world::position client_pos{data.x, data.y};
    world::position target_pos = client_pos.move(dir);

    auto move_result = players_->try_move(pid, target_pos, dir);

    switch (move_result.result)
    {
    case player::player_system::move_result::success:
    {
        // Update destination on connection if provided
        if (data.dest_x.has_value() && data.dest_y.has_value())
        {
            conn->set_destination(*data.dest_x, *data.dest_y);
        }

        // Check if player has reached destination
        std::optional<int16_t> broadcast_dest_x;
        std::optional<int16_t> broadcast_dest_y;

        if (conn->has_destination())
        {
            if (target_pos.x == conn->dest_x() && target_pos.y == conn->dest_y())
            {
                // Reached destination - clear it
                conn->clear_destination();
            }
            else
            {
                // Still moving toward destination - include in broadcast
                broadcast_dest_x = conn->dest_x();
                broadcast_dest_y = conn->dest_y();
            }
        }

        // Send success response to moving player
        auto response = network::make_player_move_response(msg.seq, true, target_pos.x, target_pos.y, data.direction);
        conn->send(response);

        // Broadcast position to nearby players (with destination if still moving)
        broadcast_position_update(
            pid, target_pos.x, target_pos.y, data.direction, data.is_running, broadcast_dest_x, broadcast_dest_y);

        // Update entity visibility for all affected players
        update_entity_visibility(pid, old_pos, target_pos);

        LOG_DEBUG(bridge,
                  "Player {} {} to ({}, {})",
                  pid.value,
                  data.is_running ? "ran" : "walked",
                  target_pos.x,
                  target_pos.y);
        break;
    }

    case player::player_system::move_result::teleport:
    {
        // Clear destination - teleport interrupts movement
        conn->clear_destination();

        // Execute the teleport with full handling
        execute_player_teleport(pid,
                                conn_id,
                                msg.seq,
                                move_result.teleport_dest_map,
                                move_result.teleport_dest_pos,
                                move_result.teleport_dest_dir);
        break;
    }

    case player::player_system::move_result::blocked_terrain:
        // Clear destination - movement was interrupted
        conn->clear_destination();
        conn->send(network::make_player_move_response(
            msg.seq, false, player->pos.x, player->pos.y, static_cast<int16_t>(player->facing), "blocked_terrain"));
        break;

    case player::player_system::move_result::blocked_occupied:
        // Clear destination - movement was interrupted (bumped)
        conn->clear_destination();
        conn->send(network::make_player_move_response(
            msg.seq, false, player->pos.x, player->pos.y, static_cast<int16_t>(player->facing), "blocked_occupied"));
        break;

    case player::player_system::move_result::blocked_out_of_bounds:
        // Clear destination - movement was interrupted
        conn->clear_destination();
        conn->send(network::make_player_move_response(
            msg.seq, false, player->pos.x, player->pos.y, static_cast<int16_t>(player->facing), "out_of_bounds"));
        break;

    case player::player_system::move_result::blocked_status:
        // Clear destination - movement was interrupted
        conn->clear_destination();
        conn->send(network::make_player_move_response(
            msg.seq, false, player->pos.x, player->pos.y, static_cast<int16_t>(player->facing), "cannot_move"));
        break;

    case player::player_system::move_result::blocked_dead:
        // Clear destination - movement was interrupted
        conn->clear_destination();
        conn->send(network::make_player_move_response(
            msg.seq, false, player->pos.x, player->pos.y, static_cast<int16_t>(player->facing), "dead"));
        break;

    default:
        // Clear destination - movement was interrupted
        conn->clear_destination();
        conn->send(network::make_player_move_response(
            msg.seq, false, player->pos.x, player->pos.y, static_cast<int16_t>(player->facing), "move_failed"));
        break;
    }
}

void game_handlers::handle_player_stop(connection_id conn_id, const network::json_message& msg)
{
    auto* conn = require_in_game(conn_id, msg.seq);
    if (!conn)
        return;

    if (!players_)
    {
        send_error(conn_id, msg.seq, "internal_error", "Player system unavailable");
        return;
    }

    auto data_result = network::player_stop_request_data::from_json(msg.data);
    if (data_result.is_err())
    {
        send_error(conn_id, msg.seq, "invalid_request", data_result.error());
        return;
    }

    auto& data = data_result.value();
    auto pid = conn->player();

    auto* player = players_->get_player(pid);
    if (!player)
    {
        send_error(conn_id, msg.seq, "invalid_player", "Player not found");
        return;
    }

    // Clear destination - player explicitly stopped
    conn->clear_destination();

    // Update facing direction if provided
    if (data.direction.has_value())
    {
        player->facing = static_cast<world::direction>(data.direction.value() & 7);
    }

    auto direction = static_cast<int16_t>(player->facing);

    // Acknowledge the stop to the sender
    conn->send(network::make_player_stop_response(msg.seq, true, player->pos.x, player->pos.y, direction));

    // Broadcast position update to nearby players (not running = stopped, no destination)
    broadcast_position_update(pid, player->pos.x, player->pos.y, direction, false);

    LOG_DEBUG(bridge, "Player {} stopped at ({}, {}) facing {}", pid.value, player->pos.x, player->pos.y, direction);
}

void game_handlers::broadcast_position_update(player_id moved_player,
                                              int16_t x,
                                              int16_t y,
                                              int16_t direction,
                                              bool is_running,
                                              std::optional<int16_t> dest_x,
                                              std::optional<int16_t> dest_y)
{
    auto* perf = subsystems().get<perf::perf_stats_system>();
    PERF_TIMER(perf, perf::metric_category::broadcast);

    if (!players_ || !ws_server_)
        return;

    auto* player = players_->get_player(moved_player);
    if (!player)
        return;

    auto update_msg =
        network::make_player_position_update(player->ecs_entity.id, x, y, direction, is_running, dest_x, dest_y);
    broadcast_to_visible(players_, ws_server_, player->current_map, player->pos, update_msg, moved_player);

    // Forward to admin spectators
    for (auto admin_conn : ws_server_->get_admin_subscribers(player->current_map))
    {
        ws_server_->send(admin_conn, update_msg);
    }
}

void game_handlers::update_entity_visibility(player_id moved_player,
                                             const world::position& old_pos,
                                             const world::position& new_pos)
{
    auto* perf = subsystems().get<perf::perf_stats_system>();
    PERF_TIMER(perf, perf::metric_category::visibility_update);

    if (!players_ || !ws_server_)
        return;

    auto* player = players_->get_player(moved_player);
    if (!player)
        return;

    // Get all players who could possibly be affected (use max radius as coarse filter)
    constexpr int coarse_radius = network::max_visibility_radius + 5;
    auto old_nearby = players_->get_players_in_range(moved_player, coarse_radius);

    // For each player in the expanded range, check visibility changes
    for (auto other_id : old_nearby)
    {
        if (other_id == moved_player)
            continue;

        auto* other = players_->get_player(other_id);
        if (!other || other->connection.value == 0)
            continue;

        // Check if other player could see the moved player at old/new positions
        // Uses other's rectangular visibility (sees_all always sees everything)
        auto orx = other->visibility_radius_x;
        auto ory = other->visibility_radius_y;
        bool was_visible =
            other->sees_all || (std::abs(old_pos.x - other->pos.x) <= orx && std::abs(old_pos.y - other->pos.y) <= ory);
        bool is_visible =
            other->sees_all || (std::abs(new_pos.x - other->pos.x) <= orx && std::abs(new_pos.y - other->pos.y) <= ory);

        auto* other_conn = ws_server_->get_connection(other->connection);
        if (!other_conn || !other_conn->is_open())
            continue;

        if (!was_visible && is_visible)
        {
            // Player just came into view - send entity_spawn to other
            other_conn->send(network::make_entity_spawn(
                0,
                build_player_spawn(
                    *player, player_hostility(other->faction, player->faction), item_, item_registry_, effects_)));

            // Also send the other player info to the moving player
            auto* my_conn = ws_server_->get_connection(player->connection);
            if (my_conn && my_conn->is_open())
            {
                my_conn->send(network::make_entity_spawn(
                    0,
                    build_player_spawn(
                        *other, player_hostility(player->faction, other->faction), item_, item_registry_, effects_)));
            }
        }
        else if (was_visible && !is_visible)
        {
            // Player just left view - send entity_despawn to other
            auto despawn_msg = network::make_entity_despawn(0, player->ecs_entity.id);
            other_conn->send(despawn_msg);

            // Also despawn the other player from the moving player's view
            auto* my_conn = ws_server_->get_connection(player->connection);
            if (my_conn && my_conn->is_open())
            {
                auto other_despawn_msg = network::make_entity_despawn(0, other->ecs_entity.id);
                my_conn->send(other_despawn_msg);
            }
        }
    }
}

void game_handlers::handle_set_view_range(connection_id conn_id, const network::json_message& msg)
{
    auto* conn = require_in_game(conn_id, msg.seq);
    if (!conn)
        return;

    if (!players_)
    {
        send_error(conn_id, msg.seq, "internal_error", "Player system unavailable");
        return;
    }

    auto data_result = network::set_view_range_request_data::from_json(msg.data);
    if (data_result.is_err())
    {
        send_error(conn_id, msg.seq, "invalid_request", data_result.error());
        return;
    }

    auto& data = data_result.value();
    auto pid = conn->player();

    auto* player = players_->get_player(pid);
    if (!player)
    {
        send_error(conn_id, msg.seq, "invalid_player", "Player not found");
        return;
    }

    // GM override prevents client from recalculating radii (e.g. after /setviewrange)
    if (player->gm_view_override)
    {
        LOG_DEBUG(bridge, "Player {} set_view_range ignored (GM override active)", pid.value);
        return;
    }

    auto radii = network::calculate_visibility_radius(data.screen_width, data.screen_height);
    player->visibility_radius_x = radii.x;
    player->visibility_radius_y = radii.y;

    LOG_DEBUG(bridge,
              "Player {} updated visibility radii to {}x{} (screen {}x{})",
              pid.value,
              radii.x,
              radii.y,
              data.screen_width,
              data.screen_height);
}

// ========== Teleportation Handling ==========

void game_handlers::execute_player_teleport(player_id pid,
                                            connection_id conn_id,
                                            uint32_t seq,
                                            const std::string& dest_map,
                                            const world::position& dest_pos,
                                            world::direction dest_dir)
{
    auto* perf = subsystems().get<perf::perf_stats_system>();
    PERF_TIMER(perf, perf::metric_category::visibility_update);

    if (!players_ || !ws_server_ || !world_)
    {
        send_error(conn_id, seq, "internal_error", "System unavailable");
        return;
    }

    auto* player = players_->get_player(pid);
    if (!player)
    {
        send_error(conn_id, seq, "invalid_player", "Player not found");
        return;
    }

    // Store old position for despawn notifications
    auto old_map_id = player->current_map;
    auto old_pos = player->pos;

    // Execute the teleport
    auto teleport_result = players_->execute_teleport(pid, dest_map, dest_pos, dest_dir);
    if (!teleport_result.success)
    {
        send_error(conn_id, seq, "teleport_failed", teleport_result.error);
        return;
    }

    // Use the resolved position from the teleport result (handles -1,-1 -> initial point)
    auto resolved_pos = teleport_result.new_pos;

    // Check if this is a cross-map teleport
    bool is_cross_map = (old_map_id != teleport_result.new_map);

    // Despawn from players who could see OLD position
    auto despawn_msg = network::make_entity_despawn(0, player->ecs_entity.id);
    broadcast_to_visible(players_, ws_server_, old_map_id, old_pos, despawn_msg, pid);

    // Forward despawn to admin spectators on old map
    for (auto admin_conn : ws_server_->get_admin_subscribers(old_map_id))
    {
        ws_server_->send(admin_conn, despawn_msg);
    }

    // Build and send player_teleport message
    network::player_teleport_msg teleport_msg{.dest_map = dest_map,
                                              .dest_x = resolved_pos.x,
                                              .dest_y = resolved_pos.y,
                                              .dest_dir = static_cast<int16_t>(dest_dir)};

    auto* conn = ws_server_->get_connection(conn_id);
    if (conn && conn->is_open())
    {
        conn->send(network::make_player_teleport(seq, teleport_msg));

        // Send full stat update so client resyncs HP/MP/SP/XP/gold after teleport
        send_full_stat_update(conn_id, *player);

        // Send visible entities (players + alive/dead NPCs) at destination
        bridge::send_visible_entity_spawns(
            conn, pid, teleport_result.new_map, resolved_pos,
            players_, npc_, world_, item_, item_registry_, effects_);

        // If cross-map, also send map_teleporters for the new map
        if (is_cross_map)
        {
            auto* new_map = world_->get_map(teleport_result.new_map);
            if (new_map)
            {
                bridge::send_map_teleporters(conn, *new_map);
            }
        }

        // Send visible ground items at destination
        bridge::send_visible_ground_items(
            conn, teleport_result.new_map, resolved_pos,
            player->visibility_radius_x, player->visibility_radius_y,
            world_, item_, item_registry_);

        // Send environment update for destination map
        if (scheduler_)
        {
            auto* dest_map_ptr = world_->get_map(teleport_result.new_map);
            if (dest_map_ptr)
            {
                auto& clock = scheduler_->game_time();
                network::environment_update_data env{.hour = static_cast<uint8_t>(clock.hour()),
                                                     .minute = static_cast<uint8_t>(clock.minute()),
                                                     .is_day = clock.is_day(),
                                                     .weather = static_cast<uint8_t>(dest_map_ptr->weather())};
                if (dest_map_ptr->config().is_fixed_day_mode)
                {
                    env.is_day = true;
                    env.weather = 0;
                }
                conn->send(network::make_environment_update(env));
            }
        }
    }

    // Spawn to players who can see NEW position (per-viewer hostility)
    for_each_visible_connection(
        players_,
        ws_server_,
        teleport_result.new_map,
        resolved_pos,
        [&](player_id, player::player& other, network::ws_connection& peer_conn)
        {
            peer_conn.send(network::make_entity_spawn(
                0,
                build_player_spawn(
                    *player, player_hostility(other.faction, player->faction), item_, item_registry_, effects_)));
        },
        pid);

    // Forward spawn to admin spectators on new map (neutral perspective)
    auto admin_spawn_msg =
        network::make_entity_spawn(0, build_player_spawn(*player, "neutral", item_, item_registry_, effects_));
    for (auto admin_conn : ws_server_->get_admin_subscribers(teleport_result.new_map))
    {
        ws_server_->send(admin_conn, admin_spawn_msg);
    }

    LOG_INFO(bridge, "Player {} teleported to {} ({}, {})", pid.value, dest_map, resolved_pos.x, resolved_pos.y);
}

void game_handlers::broadcast_teleporter_update(map_id map,
                                                const std::string& action,
                                                const world::position& pos,
                                                const world::teleport_dest* dest)
{
    if (!players_ || !ws_server_ || !world_)
        return;

    auto* m = world_->get_map(map);
    if (!m)
        return;

    network::teleporter_update_msg update_msg;
    update_msg.action = action;
    update_msg.map_name = std::string(m->name());
    update_msg.teleporter.id =
        (static_cast<uint32_t>(pos.x) << 16) | static_cast<uint32_t>(static_cast<uint16_t>(pos.y));
    update_msg.teleporter.x = pos.x;
    update_msg.teleporter.y = pos.y;

    if (dest)
    {
        update_msg.teleporter.dest_map = dest->dest_map;
        update_msg.teleporter.dest_x = dest->dest_x;
        update_msg.teleporter.dest_y = dest->dest_y;
        update_msg.teleporter.dest_dir = static_cast<int16_t>(dest->dest_dir);
    }

    auto msg = network::make_teleporter_update(update_msg);

    // Send to all players on this map
    players_->for_each_player(
        [&](player_id, const player::player& p)
        {
            if (p.current_map != map)
                return;
            if (p.connection.value == 0)
                return;

            auto* conn = ws_server_->get_connection(p.connection);
            if (conn && conn->is_open())
            {
                conn->send(msg);
            }
        });

    LOG_DEBUG(bridge, "Broadcast teleporter {} at ({},{}) on map {}", action, pos.x, pos.y, m->name());
}

} // namespace hb::bridge
