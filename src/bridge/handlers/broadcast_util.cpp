#include "bridge/handlers/broadcast_util.h"

#include "network/websocket_server.h"
#include "player/player_system.h"

namespace hb::bridge
{

void for_each_visible_connection(
    player::player_system* players,
    network::websocket_server* ws,
    map_id map,
    const world::position& pos,
    const std::function<void(player_id, player::player&, network::ws_connection&)>& fn,
    std::optional<player_id> exclude)
{
    if (!players || !ws)
        return;

    auto viewers = players->get_players_who_can_see(map, pos);
    for (auto pid : viewers)
    {
        if (exclude && pid == *exclude)
            continue;

        auto* p = players->get_player(pid);
        if (!p || p->connection.value == 0)
            continue;

        auto* conn = ws->get_connection(p->connection);
        if (conn && conn->is_open())
        {
            fn(pid, *p, *conn);
        }
    }
}

void broadcast_to_visible(player::player_system* players,
                          network::websocket_server* ws,
                          map_id map,
                          const world::position& pos,
                          const network::json_message& msg,
                          std::optional<player_id> exclude)
{
    for_each_visible_connection(
        players,
        ws,
        map,
        pos,
        [&msg](player_id, player::player&, network::ws_connection& conn) { conn.send(msg); },
        exclude);
}

} // namespace hb::bridge
