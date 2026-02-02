#pragma once

// game_handlers.h
// Message handlers for in-game protocol (movement, combat, chat, etc.)

#include "core/types.h"
#include "network/json_protocol.h"
#include "world/position.h"
#include "world/map.h"

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

namespace hb::social {
    class social_system;
    struct chat_message_event;
}

namespace hb::bridge {

// Game message handler
// Handles movement, actions, chat, and other in-game messages
class game_handlers {
public:
    game_handlers();
    ~game_handlers();

    // Initialize with required systems
    void initialize(network::websocket_server* ws_server,
                    player::player_system* players,
                    world::world_subsystem* world,
                    social::social_system* social);

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

    // Chat
    void handle_chat_message(connection_id conn_id, const network::json_message& msg);
    void handle_command(connection_id conn_id, const network::json_message& msg);

    // View range
    void handle_set_view_range(connection_id conn_id, const network::json_message& msg);

    // Chat distribution callback - called when social_system processes a chat message
    void on_chat_message(const social::chat_message_event& event);

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

    // Send chat message to a specific player
    void send_chat_to_player(player_id target, const network::chat_message_broadcast_data& data);

    // Send chat to players in range
    void send_chat_to_nearby(player_id sender, int16_t range,
                              const network::chat_message_broadcast_data& data);

    // Teleportation helpers
    void execute_player_teleport(player_id pid, connection_id conn_id, uint32_t seq,
                                  const std::string& dest_map,
                                  const world::position& dest_pos,
                                  world::direction dest_dir);

    void send_map_teleporters(connection_id conn_id, const world::map& map);

    void broadcast_teleporter_update(map_id map, const std::string& action,
                                      const world::position& pos,
                                      const world::teleport_dest* dest);

    [[nodiscard]] auto build_visible_entities_at(map_id map, const world::position& pos,
                                                  int visibility_radius)
        -> std::vector<network::visible_entity_msg>;

    network::websocket_server* ws_server_{nullptr};
    player::player_system* players_{nullptr};
    world::world_subsystem* world_{nullptr};
    social::social_system* social_{nullptr};
};

}  // namespace hb::bridge
