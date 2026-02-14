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

namespace hb::admin {
    class admin_system;
}

namespace hb::npc {
    class npc_system;
}

namespace hb::item {
    class item_system;
}

namespace hb::social {
    class social_system;
}

namespace hb::effect {
    class effect_system;
}

namespace hb {
    class scheduler;
    class item_registry;
}

namespace hb::war {
    class war_persistence;
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
                    inventory::inventory_system* inventory = nullptr,
                    admin::admin_system* admin = nullptr,
                    npc::npc_system* npc = nullptr,
                    item::item_system* item = nullptr,
                    social::social_system* social = nullptr,
                    scheduler* sched = nullptr,
                    war::war_persistence* war_persistence = nullptr,
                    effect::effect_system* effects = nullptr,
                    item_registry* item_reg = nullptr);

    // Main message handler - routes to specific handlers
    void handle_message(connection_id conn_id, const network::json_message& msg);

    // Handle player disconnect (save and cleanup) - called by application on disconnect
    void handle_player_disconnect(connection_id conn_id);

    // Save a single player's state to database (for on-demand saves during important interactions)
    void save_player(player_id pid);

    // Save all online players' states to database (for periodic auto-save)
    // Returns the number of players successfully saved
    auto save_all_players() -> size_t;

    // Callback for when a player enters the game (name, level, map_name)
    using enter_game_callback = std::function<void(const std::string&, int16_t, const std::string&)>;
    void set_enter_game_callback(enter_game_callback cb);

    // Callback for post-enter-game actions (player_id, connection_id)
    using post_enter_game_callback = std::function<void(player_id, connection_id)>;
    void set_post_enter_game_callback(post_enter_game_callback cb);

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
    void handle_enter_admin_mode(connection_id conn_id, const network::json_message& msg);

    // Helper to send error response
    void send_error(connection_id conn_id, uint32_t seq,
                    std::string_view error_code, std::string_view message);

    // Get connection or send error
    [[nodiscard]] auto get_connection_or_error(connection_id conn_id, uint32_t seq)
        -> network::ws_connection*;

    // Require authenticated state
    [[nodiscard]] auto require_authenticated(connection_id conn_id, uint32_t seq)
        -> network::ws_connection*;

    // Send individual entity_spawn and npc_spawn messages for visible entities
    void send_visible_entity_spawns(network::ws_connection* conn, player_id player_id);

    // Save player state to database
    void save_player_state(player_id player_id);

    network::websocket_server* ws_server_{nullptr};
    auth::auth_system* auth_{nullptr};
    player::player_system* players_{nullptr};
    world::world_subsystem* world_{nullptr};
    inventory::inventory_system* inventory_{nullptr};
    admin::admin_system* admin_{nullptr};
    npc::npc_system* npc_{nullptr};
    item::item_system* item_{nullptr};
    social::social_system* social_{nullptr};
    scheduler* scheduler_{nullptr};
    war::war_persistence* war_persistence_{nullptr};
    effect::effect_system* effects_{nullptr};
    item_registry* item_registry_{nullptr};
    enter_game_callback enter_game_callback_;
    post_enter_game_callback post_enter_game_callback_;
};

}  // namespace hb::bridge
