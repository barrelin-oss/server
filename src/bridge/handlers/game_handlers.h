#pragma once

// game_handlers.h
// Message handlers for in-game protocol (movement, combat, etc.)

#include "core/types.h"
#include "network/json_protocol.h"
#include "world/position.h"

namespace hb::network {
    class websocket_server;
    class ws_connection;
}

namespace hb::player {
    class player_system;
}

namespace hb::world {
    class world_subsystem;
}

namespace hb::bridge {

// Game message handler
// Handles movement, actions, and other in-game messages
class game_handlers {
public:
    game_handlers();
    ~game_handlers();

    // Initialize with required systems
    void initialize(network::websocket_server* ws_server,
                    player::player_system* players,
                    world::world_subsystem* world);

    // Main message handler - routes to specific handlers
    void handle_message(connection_id conn_id, const network::json_message& msg);

private:
    // Individual message handlers - Movement
    void handle_player_move(connection_id conn_id, const network::json_message& msg);
    void handle_player_run(connection_id conn_id, const network::json_message& msg);
    void handle_player_stop(connection_id conn_id, const network::json_message& msg);

    // Combat
    void handle_player_attack(connection_id conn_id, const network::json_message& msg);

    // Actions
    void handle_player_magic(connection_id conn_id, const network::json_message& msg);
    void handle_player_skill(connection_id conn_id, const network::json_message& msg);
    void handle_player_pickup(connection_id conn_id, const network::json_message& msg);
    void handle_player_interact(connection_id conn_id, const network::json_message& msg);

    // Helper to send error response
    void send_error(connection_id conn_id, uint32_t seq,
                    std::string_view error_code, std::string_view message);

    // Get connection or return nullptr
    [[nodiscard]] auto get_connection(connection_id conn_id) -> network::ws_connection*;

    // Require in-game state
    [[nodiscard]] auto require_in_game(connection_id conn_id, uint32_t seq)
        -> network::ws_connection*;

    // Broadcast position update to nearby players
    void broadcast_position_update(player_id moved_player, int16_t x, int16_t y,
                                    int16_t direction, bool is_running = false);

    // Handle entity visibility changes after movement
    void update_entity_visibility(player_id moved_player,
                                   const world::position& old_pos,
                                   const world::position& new_pos);

    network::websocket_server* ws_server_{nullptr};
    player::player_system* players_{nullptr};
    world::world_subsystem* world_{nullptr};
};

}  // namespace hb::bridge
