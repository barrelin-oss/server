#pragma once

// admin_web_handlers.h
// Message handlers for admin web tool dashboard and spectator modes

#include "core/types.h"
#include "network/json_protocol.h"

namespace hb::network
{
class websocket_server;
class ws_connection;
} // namespace hb::network

namespace hb::auth
{
class auth_system;
}

namespace hb::player
{
class player_system;
}

namespace hb::world
{
class world_subsystem;
}

namespace hb::inventory
{
class inventory_system;
}

namespace hb::admin
{
class admin_system;
}

namespace hb::npc
{
class npc_system;
}

namespace hb::item
{
class item_system;
}

namespace hb::social
{
class social_system;
}

namespace hb::combat
{
class combat_system;
}

namespace hb::war
{
class war_system;
class war_persistence;
} // namespace hb::war

namespace hb::effect
{
class effect_system;
}

namespace hb::database
{
class database_system;
}

namespace hb::magic
{
class magic_system;
}

namespace hb::quest
{
class quest_system;
}

namespace hb::skill
{
class skill_system;
}

namespace hb
{
class scheduler;
class npc_registry;
class item_registry;
class config_system;
} // namespace hb

namespace hb::perf
{
class perf_stats_system;
}

namespace hb::audit
{
class item_audit_system;
}

namespace hb::bridge
{

class admin_web_handlers
{
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
                    item_registry* item_reg,
                    war::war_system* war,
                    effect::effect_system* effects,
                    config_system* config = nullptr,
                    magic::magic_system* magic = nullptr,
                    quest::quest_system* quest = nullptr,
                    skill::skill_system* skill = nullptr,
                    perf::perf_stats_system* perf_stats = nullptr,
                    audit::item_audit_system* audit = nullptr);

    // Returns true if this handler recognized and processed the message type.
    bool handle_message(connection_id conn_id, const network::json_message& msg);

    void set_war_persistence(war::war_persistence* wp) { war_persistence_ = wp; }

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
    void handle_get_map_data(connection_id conn_id, const network::json_message& msg);
    void send_spectator_init(connection_id conn_id, map_id map);

    // New admin endpoints
    void handle_broadcast(connection_id conn_id, const network::json_message& msg);
    void handle_mute_player(connection_id conn_id, const network::json_message& msg);
    void handle_unmute_player(connection_id conn_id, const network::json_message& msg);
    void handle_list_item_templates(connection_id conn_id, const network::json_message& msg);
    void handle_get_item_template(connection_id conn_id, const network::json_message& msg);
    void handle_list_npc_templates(connection_id conn_id, const network::json_message& msg);
    void handle_get_npc_template(connection_id conn_id, const network::json_message& msg);
    void handle_get_war_status(connection_id conn_id, const network::json_message& msg);
    void handle_list_parties(connection_id conn_id, const network::json_message& msg);
    void handle_search_players(connection_id conn_id, const network::json_message& msg);

    // Phase 3 admin endpoints
    void handle_get_audit_log(connection_id conn_id, const network::json_message& msg);
    void handle_get_config(connection_id conn_id, const network::json_message& msg);
    void handle_set_config(connection_id conn_id, const network::json_message& msg);
    void handle_reload_config(connection_id conn_id, const network::json_message& msg);
    void handle_list_scheduled_tasks(connection_id conn_id, const network::json_message& msg);
    void handle_cancel_scheduled_task(connection_id conn_id, const network::json_message& msg);
    void handle_start_task(connection_id conn_id, const network::json_message& msg);
    void handle_run_query(connection_id conn_id, const network::json_message& msg);
    void handle_list_map_npcs(connection_id conn_id, const network::json_message& msg);
    void handle_list_map_ground_items(connection_id conn_id, const network::json_message& msg);
    void handle_remove_ground_item(connection_id conn_id, const network::json_message& msg);
    void handle_guild_action(connection_id conn_id, const network::json_message& msg);
    void handle_message_player(connection_id conn_id, const network::json_message& msg);
    void handle_set_environment(connection_id conn_id, const network::json_message& msg);
    void handle_shutdown_server(connection_id conn_id, const network::json_message& msg);

    // Phase 4 admin endpoints
    void handle_modify_skills(connection_id conn_id, const network::json_message& msg);
    void handle_modify_spells(connection_id conn_id, const network::json_message& msg);
    void handle_get_player_quests(connection_id conn_id, const network::json_message& msg);
    void handle_quest_action(connection_id conn_id, const network::json_message& msg);
    void handle_remove_effects(connection_id conn_id, const network::json_message& msg);
    void handle_create_account(connection_id conn_id, const network::json_message& msg);
    void handle_change_password(connection_id conn_id, const network::json_message& msg);
    void handle_set_admin_level(connection_id conn_id, const network::json_message& msg);
    void handle_list_spawn_points(connection_id conn_id, const network::json_message& msg);
    void handle_list_spell_templates(connection_id conn_id, const network::json_message& msg);
    void handle_get_spell_template(connection_id conn_id, const network::json_message& msg);
    void handle_set_maintenance_mode(connection_id conn_id, const network::json_message& msg);
    void handle_create_character_admin(connection_id conn_id, const network::json_message& msg);
    void handle_delete_character_admin(connection_id conn_id, const network::json_message& msg);
    void handle_manage_ip_bans(connection_id conn_id, const network::json_message& msg);

    // Item audit log
    void handle_item_log(connection_id conn_id, const network::json_message& msg);

    // Performance stats
    void handle_perf_stats(connection_id conn_id, const network::json_message& msg);

    // War management (Phase 6)
    void handle_start_war(connection_id conn_id, const network::json_message& msg);
    void handle_end_war(connection_id conn_id, const network::json_message& msg);
    void handle_war_history(connection_id conn_id, const network::json_message& msg);
    void handle_war_participants(connection_id conn_id, const network::json_message& msg);

    // Helpers
    void send_error(connection_id conn_id, uint32_t seq, std::string_view error_code, std::string_view message);
    [[nodiscard]] auto
    require_admin(connection_id conn_id, uint32_t seq, uint8_t min_level = 10) -> network::ws_connection*;
    void audit_log(connection_id conn_id, std::string_view action, bool success = true, std::string_view result = "");

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
    war::war_system* war_{nullptr};
    effect::effect_system* effects_{nullptr};
    config_system* config_{nullptr};
    magic::magic_system* magic_{nullptr};
    quest::quest_system* quest_{nullptr};
    skill::skill_system* skill_{nullptr};
    perf::perf_stats_system* perf_stats_{nullptr};
    audit::item_audit_system* audit_{nullptr};
    war::war_persistence* war_persistence_{nullptr};
    std::chrono::steady_clock::time_point start_time_{std::chrono::steady_clock::now()};
};

} // namespace hb::bridge
