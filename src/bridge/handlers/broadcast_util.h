#pragma once

// broadcast_util.h
// Shared utility for broadcasting messages to nearby visible players.
// Eliminates the repeated "get visible → iterate → null check → send" pattern.

#include "core/types.h"
#include "world/position.h"

#include <functional>
#include <optional>

namespace hb::network
{
class websocket_server;
class ws_connection;
struct json_message;
} // namespace hb::network

namespace hb::player
{
class player_system;
struct player;
} // namespace hb::player

namespace hb::bridge
{

// Send the same message to all players who can see (map, pos).
// Optionally exclude one player (e.g., the acting player).
void broadcast_to_visible(player::player_system* players,
                          network::websocket_server* ws,
                          map_id map,
                          const world::position& pos,
                          const network::json_message& msg,
                          std::optional<player_id> exclude = std::nullopt);

// Call fn(player_id, player&, ws_connection&) for each visible player with a valid open connection.
// Optionally exclude one player.
void for_each_visible_connection(
    player::player_system* players,
    network::websocket_server* ws,
    map_id map,
    const world::position& pos,
    const std::function<void(player_id, player::player&, network::ws_connection&)>& fn,
    std::optional<player_id> exclude = std::nullopt);

} // namespace hb::bridge
