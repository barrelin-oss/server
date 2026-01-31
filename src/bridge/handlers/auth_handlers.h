#pragma once

// auth_handlers.h
// Message handlers for authentication protocol (Wave 0)

#include "core/types.h"
#include "network/json_protocol.h"

namespace hb::network {
    class websocket_server;
    class ws_connection;
}

namespace hb::auth {
    class auth_system;
}

namespace hb::player {
    class player_system;
}

namespace hb::world {
    class world_subsystem;
}

namespace hb::inventory {
    class inventory_system;
}

namespace hb::bridge {

// Authentication message handler
// Handles login, account creation, character management, and game entry
class auth_handlers {
public:
    auth_handlers();
    ~auth_handlers();

    // Initialize with required systems
    void initialize(network::websocket_server* ws_server,
                    auth::auth_system* auth,
                    player::player_system* players = nullptr,
                    world::world_subsystem* world = nullptr,
                    inventory::inventory_system* inventory = nullptr);

    // Main message handler - routes to specific handlers
    void handle_message(connection_id conn_id, const network::json_message& msg);

    // Handle player disconnect (save and cleanup) - called by application on disconnect
    void handle_player_disconnect(connection_id conn_id);

private:
    // Individual message handlers
    void handle_login(connection_id conn_id, const network::json_message& msg);
    void handle_logout(connection_id conn_id, const network::json_message& msg);
    void handle_create_account(connection_id conn_id, const network::json_message& msg);
    void handle_get_characters(connection_id conn_id, const network::json_message& msg);
    void handle_create_character(connection_id conn_id, const network::json_message& msg);
    void handle_delete_character(connection_id conn_id, const network::json_message& msg);
    void handle_enter_game(connection_id conn_id, const network::json_message& msg);
    void handle_ping(connection_id conn_id, const network::json_message& msg);

    // Helper to send error response
    void send_error(connection_id conn_id, uint32_t seq,
                    std::string_view error_code, std::string_view message);

    // Get connection or send error
    [[nodiscard]] auto get_connection_or_error(connection_id conn_id, uint32_t seq)
        -> network::ws_connection*;

    // Require authenticated state
    [[nodiscard]] auto require_authenticated(connection_id conn_id, uint32_t seq)
        -> network::ws_connection*;

    // Build visible entity list for world_init
    [[nodiscard]] auto build_visible_entities(player_id player_id)
        -> std::vector<network::visible_entity_msg>;

    // Save player state to database
    void save_player_state(player_id player_id);

    network::websocket_server* ws_server_{nullptr};
    auth::auth_system* auth_{nullptr};
    player::player_system* players_{nullptr};
    world::world_subsystem* world_{nullptr};
    inventory::inventory_system* inventory_{nullptr};
};

}  // namespace hb::bridge
