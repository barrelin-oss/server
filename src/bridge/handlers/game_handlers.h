#pragma once

// game_handlers.h
// Message handlers for in-game protocol (movement, combat, chat, etc.)

#include "core/types.h"
#include "core/enums.h"
#include "network/json_protocol.h"
#include "world/position.h"
#include "world/map.h"
#include "entity/entity.h"
#include "player/equipment.h"

#include <optional>
#include <functional>

namespace hb {
    class scheduler;
}

namespace hb::network {
    class websocket_server;
    class ws_connection;
}

namespace hb::player {
    class player_system;
    struct player;
}

namespace hb::world {
    class world_subsystem;
}

namespace hb::social {
    class social_system;
    struct chat_message_event;
}

namespace hb::admin {
    class admin_system;
}

namespace hb::combat {
    class combat_system;
    struct damage_event;
    struct death_event;
}

namespace hb::magic {
    class magic_system;
    struct spell_template;
    struct spell_effect_result;
}

namespace hb::npc {
    class npc_system;
    struct npc;
}

namespace hb::inventory {
    class inventory_system;
}

namespace hb::item {
    class item_system;
}

namespace hb {
    class loot_registry;
    class shop_registry;
    class dialog_registry;
}

namespace hb::bridge {

// Game message handler
// Handles movement, actions, chat, and other in-game messages
class game_handlers {
public:
    game_handlers();
    ~game_handlers();

    // Save player callback type
    using save_player_callback = std::function<void(player_id)>;

    // Initialize with required systems
    void initialize(network::websocket_server* ws_server,
                    player::player_system* players,
                    world::world_subsystem* world,
                    social::social_system* social,
                    admin::admin_system* admin = nullptr,
                    combat::combat_system* combat = nullptr,
                    npc::npc_system* npc = nullptr,
                    inventory::inventory_system* inventory = nullptr,
                    item::item_system* item = nullptr,
                    scheduler* sched = nullptr,
                    loot_registry* loot = nullptr,
                    shop_registry* shops = nullptr,
                    dialog_registry* dialogs = nullptr,
                    magic::magic_system* magic = nullptr);

    // Set callback for saving player state (used after death penalties)
    void set_save_callback(save_player_callback cb);

    // Main message handler - routes to specific handlers
    void handle_message(connection_id conn_id, const network::json_message& msg);

private:
    // Individual message handlers - Movement
    void handle_player_move(connection_id conn_id, const network::json_message& msg);
    void handle_player_stop(connection_id conn_id, const network::json_message& msg);

    // Combat
    void handle_player_attack(connection_id conn_id, const network::json_message& msg);

    // Actions
    void handle_player_magic(connection_id conn_id, const network::json_message& msg);
    void handle_player_skill(connection_id conn_id, const network::json_message& msg);
    void handle_player_pickup(connection_id conn_id, const network::json_message& msg);
    void handle_player_interact(connection_id conn_id, const network::json_message& msg);

    // NPC interaction - shops
    void handle_shop_buy(connection_id conn_id, const network::json_message& msg);
    void handle_shop_sell(connection_id conn_id, const network::json_message& msg);
    void handle_shop_sell_confirm(connection_id conn_id, const network::json_message& msg);
    void handle_shop_repair(connection_id conn_id, const network::json_message& msg);
    void handle_shop_repair_confirm(connection_id conn_id, const network::json_message& msg);

    // NPC interaction - banking
    void handle_bank_deposit(connection_id conn_id, const network::json_message& msg);
    void handle_bank_withdraw(connection_id conn_id, const network::json_message& msg);

    // NPC interaction - dialog
    void handle_dialog_choice(connection_id conn_id, const network::json_message& msg);

    // NPC interaction helper - validates NPC exists, is in range, and is friendly
    struct npc_interaction_check {
        player::player* plr{nullptr};
        npc::npc* target_npc{nullptr};
        bool valid{false};
        std::string error;
    };
    auto validate_npc_interaction(connection_id conn_id, uint32_t seq, uint32_t npc_entity_id)
        -> npc_interaction_check;

    // Equipment
    void handle_player_equip(connection_id conn_id, const network::json_message& msg);
    void handle_player_unequip(connection_id conn_id, const network::json_message& msg);
    void broadcast_equipment_change(player_id pid, player::equip_slot slot, item_id itm);
    void send_stat_update(connection_id conn_id, const player::player& plr);

    // Chat
    void handle_chat_message(connection_id conn_id, const network::json_message& msg);
    void handle_command(connection_id conn_id, const network::json_message& msg);

    // View range
    void handle_set_view_range(connection_id conn_id, const network::json_message& msg);

    // Entity info
    void handle_entity_info_request(connection_id conn_id, const network::json_message& msg);

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
                                    int16_t direction, bool is_running = false,
                                    std::optional<int16_t> dest_x = std::nullopt,
                                    std::optional<int16_t> dest_y = std::nullopt);

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
                                                  int visibility_radius_x,
                                                  int visibility_radius_y)
        -> std::vector<network::visible_entity_msg>;

    // Combat event callbacks
    void on_damage_dealt(const combat::damage_event& event);
    void on_entity_death(const combat::death_event& event);
    void handle_player_death(player_id pid, const combat::death_event& event);

    // Death/respawn helpers
    auto calculate_death_xp_penalty(uint8_t level) -> int64_t;
    auto calculate_pk_bounty_reward(uint8_t level) -> int32_t;
    auto get_respawn_map_name(hb::faction f) -> std::string;
    auto get_respawn_position(const std::string& map_name) -> world::position;
    void execute_respawn(player_id pid, const std::string& map_name,
                         const world::position& pos);

    // Combat broadcast helpers
    void broadcast_hp_update(player_id target, int32_t hp, int32_t hp_max);
    void broadcast_entity_death(player_id victim, player_id killer);
    void broadcast_combat_effect(map_id map, const world::position& pos,
                                 const network::combat_effect_data& data);
    void broadcast_combat_effect_to_faction(map_id map, const world::position& pos,
                                            hb::faction faction,
                                            const network::combat_effect_data& data);

    // Spell cast callback
    void on_spell_cast(entity::entity caster, const magic::spell_template& spell,
                       const magic::spell_effect_result& result);

    // NPC broadcast helpers
    void broadcast_npc_spawn(const npc::npc& n);
    void broadcast_npc_move(const npc::npc& n);
    void broadcast_npc_attack(const npc::npc& n, entity::entity target, int32_t damage);
    void broadcast_npc_death(const npc::npc& n, entity::entity killer);
    void broadcast_npc_hp_update(const npc::npc& n);

    // Item broadcast helpers
    void broadcast_ground_item_spawn(map_id map, const world::position& pos, item_id item);
    void broadcast_ground_item_removed(player_id picker, map_id map,
                                       const world::position& pos, item_id item);

    // XP distribution
    void distribute_npc_kill_exp(entity::entity killer, int32_t base_exp);

    // Loot drop handlers
    void handle_npc_loot_drop(const npc::npc& n, entity::entity killer);
    void handle_npc_despawn_drop(const npc::npc& n);

    // Send visible ground items to a player
    void send_visible_ground_items(connection_id conn_id, map_id map,
                                    const world::position& pos,
                                    int radius_x, int radius_y);

    // Hunger update helper
    void send_hunger_update(player_id pid, int8_t level);

    network::websocket_server* ws_server_{nullptr};
    player::player_system* players_{nullptr};
    world::world_subsystem* world_{nullptr};
    social::social_system* social_{nullptr};
    admin::admin_system* admin_{nullptr};
    combat::combat_system* combat_{nullptr};
    npc::npc_system* npc_{nullptr};
    inventory::inventory_system* inventory_{nullptr};
    item::item_system* item_{nullptr};
    scheduler* scheduler_{nullptr};
    loot_registry* loot_registry_{nullptr};
    shop_registry* shop_registry_{nullptr};
    dialog_registry* dialog_registry_{nullptr};
    magic::magic_system* magic_{nullptr};
    save_player_callback save_callback_;
};

}  // namespace hb::bridge
