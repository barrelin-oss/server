#pragma once

// admin_web_handlers.h
// Message handlers for admin web tool dashboard and spectator modes

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

namespace hb::combat {
    class combat_system;
}

namespace hb::database {
    class database_system;
}

namespace hb {
    class scheduler;
    class npc_registry;
    class item_registry;
}

namespace hb::bridge {

class admin_web_handlers {
public:
    admin_web_handlers();
    ~admin_web_handlers();

    void initialize(network::websocket_server* ws_server,
                    auth::auth_system* auth,
                    player::player_system* players,
                    world::world_subsystem* world,
                    inventory::inventory_system* inventory,
                    admin::admin_system* admin,
                    npc::npc_system* npc,
                    item::item_system* item,
                    social::social_system* social,
                    combat::combat_system* combat,
                    database::database_system* db,
                    scheduler* sched,
                    npc_registry* npc_reg,
                    item_registry* item_reg);

    void handle_message(connection_id conn_id, const network::json_message& msg);

    // Push notifications (called from auth_handlers/game_handlers)
    void notify_player_connected(const std::string& name, int16_t level, const std::string& map_name);
    void notify_player_disconnected(const std::string& name);
    void notify_chat_message(const std::string& channel, const std::string& sender, const std::string& content);

private:
    // Server stats
    void handle_server_stats(connection_id conn_id, const network::json_message& msg);

    // Player management
    void handle_list_players(connection_id conn_id, const network::json_message& msg);
    void handle_get_player(connection_id conn_id, const network::json_message& msg);
    void handle_kick_player(connection_id conn_id, const network::json_message& msg);
    void handle_ban_player(connection_id conn_id, const network::json_message& msg);
    void handle_teleport_player(connection_id conn_id, const network::json_message& msg);
    void handle_modify_player(connection_id conn_id, const network::json_message& msg);

    // World/NPC management
    void handle_list_maps(connection_id conn_id, const network::json_message& msg);
    void handle_get_map(connection_id conn_id, const network::json_message& msg);
    void handle_spawn_npc(connection_id conn_id, const network::json_message& msg);
    void handle_kill_npc(connection_id conn_id, const network::json_message& msg);

    // Inventory management
    void handle_get_inventory(connection_id conn_id, const network::json_message& msg);
    void handle_give_item(connection_id conn_id, const network::json_message& msg);
    void handle_remove_item(connection_id conn_id, const network::json_message& msg);

    // Social
    void handle_list_guilds(connection_id conn_id, const network::json_message& msg);
    void handle_get_guild(connection_id conn_id, const network::json_message& msg);

    // Account management
    void handle_get_account(connection_id conn_id, const network::json_message& msg);
    void handle_unban_player(connection_id conn_id, const network::json_message& msg);

    // Spectator
    void handle_subscribe_map(connection_id conn_id, const network::json_message& msg);
    void handle_subscribe_player(connection_id conn_id, const network::json_message& msg);
    void handle_unsubscribe(connection_id conn_id, const network::json_message& msg);
    void send_spectator_init(connection_id conn_id, map_id map);

    // Helpers
    void send_error(connection_id conn_id, uint32_t seq,
                    std::string_view error_code, std::string_view message);
    [[nodiscard]] auto require_admin(connection_id conn_id, uint32_t seq, uint8_t min_level = 10)
        -> network::ws_connection*;

    network::websocket_server* ws_server_{nullptr};
    auth::auth_system* auth_{nullptr};
    player::player_system* players_{nullptr};
    world::world_subsystem* world_{nullptr};
    inventory::inventory_system* inventory_{nullptr};
    admin::admin_system* admin_{nullptr};
    npc::npc_system* npc_{nullptr};
    item::item_system* item_{nullptr};
    social::social_system* social_{nullptr};
    combat::combat_system* combat_{nullptr};
    database::database_system* db_{nullptr};
    scheduler* scheduler_{nullptr};
    npc_registry* npc_registry_{nullptr};
    item_registry* item_registry_{nullptr};
};

}  // namespace hb::bridge
