// game_handlers.cpp
// Message handlers for in-game protocol implementation

// Include platform header first to define NOMINMAX before Windows headers
#include "platform/platform.h"

#include "bridge/handlers/game_handlers.h"
#include "network/websocket_server.h"
#include "player/player_system.h"
#include "world/world_subsystem.h"
#include "social/social_system.h"
#include "admin/admin_system.h"
#include "admin/command.h"
#include "combat/combat_system.h"
#include "combat/combat_events.h"
#include "npc/npc_system.h"
#include "npc/npc.h"
#include "npc/loot_generator.h"
#include "npc/shop_pricing.h"
#include "registry/loot_registry.h"
#include "registry/shop_registry.h"
#include "registry/dialog_registry.h"
#include "registry/item_registry.h"
#include "inventory/inventory_system.h"
#include "item/item_system.h"
#include "item/item_effect.h"
#include "player/equip_mapping.h"
#include "magic/magic_system.h"
#include "magic/spell.h"
#include "crafting/manufacturing_system.h"
#include "crafting/alchemy_system.h"
#include "crafting/mining_system.h"
#include "registry/mining_registry.h"
#include "skill/skill_system.h"
#include "quest/quest_system.h"
#include "registry/build_recipe_registry.h"
#include "registry/craft_recipe_registry.h"
#include "social/party.h"
#include "scheduler/scheduler.h"
#include "config/config_system.h"
#include "core/subsystem.h"
#include "core/logger.h"

#include <chrono>
#include <random>
#include <iomanip>
#include <sstream>

namespace hb::bridge {

game_handlers::game_handlers() = default;
game_handlers::~game_handlers() = default;

void game_handlers::set_save_callback(save_player_callback cb) {
    save_callback_ = std::move(cb);
}

void game_handlers::initialize(network::websocket_server* ws_server,
                                player::player_system* players,
                                world::world_subsystem* world,
                                social::social_system* social,
                                admin::admin_system* admin,
                                combat::combat_system* combat,
                                npc::npc_system* npc,
                                inventory::inventory_system* inventory,
                                item::item_system* item,
                                scheduler* sched,
                                loot_registry* loot,
                                shop_registry* shops,
                                dialog_registry* dialogs,
                                magic::magic_system* magic,
                                crafting::manufacturing_system* manufacturing,
                                crafting::alchemy_system* alchemy,
                                skill::skill_system* skills,
                                quest::quest_system* quests,
                                crafting::mining_system* mining) {
    ws_server_ = ws_server;
    players_ = players;
    world_ = world;
    social_ = social;
    admin_ = admin;
    combat_ = combat;
    npc_ = npc;
    inventory_ = inventory;
    item_ = item;
    scheduler_ = sched;
    loot_registry_ = loot;
    shop_registry_ = shops;
    dialog_registry_ = dialogs;
    magic_ = magic;
    manufacturing_ = manufacturing;
    alchemy_ = alchemy;
    skills_ = skills;
    quests_ = quests;
    mining_ = mining;

    // Register chat message callback to distribute messages
    if (social_) {
        social_->on_chat_message([this](const social::chat_message_event& event) {
            on_chat_message(event);
        });
    }

    // Register combat callbacks
    if (combat_) {
        combat_->on_damage([this](const combat::damage_event& event) {
            on_damage_dealt(event);
        });
        combat_->on_death([this](const combat::death_event& event) {
            on_entity_death(event);
        });
    }

    // Register hunger change callback
    if (players_) {
        players_->on_hunger_change([this](player_id pid, int8_t, int8_t new_level) {
            send_hunger_update(pid, new_level);
        });
    }

    // Register NPC callbacks
    // NOTE: These callbacks are invoked synchronously and must immediately copy
    // any data from the npc& reference. The reference is only valid during the
    // callback invocation - do not store it or pass it to async operations.
    if (npc_) {
        npc_->set_on_spawn_callback([this](const npc::npc& n) {
            broadcast_npc_spawn(n);  // Copies data immediately
        });
        npc_->set_on_move_callback([this](const npc::npc& n) {
            broadcast_npc_move(n);  // Copies data immediately
        });
        npc_->set_on_death_callback([this](const npc::npc& n, entity::entity killer) {
            broadcast_npc_death(n, killer);  // Copies data immediately
            handle_npc_loot_drop(n, killer); // Generate and drop loot
            distribute_npc_kill_exp(killer, n.exp_reward); // Award XP
        });
        npc_->set_on_attack_callback([this](const npc::npc& n, entity::entity target, int32_t damage) {
            broadcast_npc_attack(n, target, damage);  // Copies data immediately
        });
        npc_->set_on_despawn_callback([this](const npc::npc& n) {
            handle_npc_despawn_drop(n);  // Generate despawn loot (body parts, rares, boss multi-drops)
        });
    }

    // Register magic spell cast callback for visual broadcasts
    if (magic_) {
        magic_->on_spell_cast([this](entity::entity caster, const magic::spell_template& spell,
                                      const magic::spell_effect_result& result) {
            on_spell_cast(caster, spell, result);
        });
    }

    // Register mining node spawn/despawn callbacks
    if (mining_) {
        mining_->set_spawn_callback([this](const crafting::mineral_node& node) {
            if (!players_ || !ws_server_ || !world_) return;

            auto* map = world_->get_map_by_name(node.map_name);
            if (!map) return;

            auto* mining_reg = subsystems().get<mining_registry>();
            auto* type_config = mining_reg ? mining_reg->get_type(node.type_id) : nullptr;
            uint8_t visual = type_config ? type_config->visual_type : 1;

            auto msg = network::make_mineral_spawn(node.node_id, visual, node.x, node.y);
            auto pos = world::position{node.x, node.y};
            auto players = players_->get_players_who_can_see(map->id(), pos);
            for (auto pid : players) {
                auto* p = players_->get_player(pid);
                if (!p || p->connection.value == 0) continue;
                auto* conn = ws_server_->get_connection(p->connection);
                if (conn && conn->is_open()) {
                    conn->send(msg);
                }
            }
        });

        mining_->set_despawn_callback([this](const crafting::mineral_node& node) {
            if (!players_ || !ws_server_ || !world_) return;

            auto* map = world_->get_map_by_name(node.map_name);
            if (!map) return;

            auto msg = network::make_mineral_despawn(node.node_id, node.x, node.y);
            auto pos = world::position{node.x, node.y};
            auto players = players_->get_players_who_can_see(map->id(), pos);
            for (auto pid : players) {
                auto* p = players_->get_player(pid);
                if (!p || p->connection.value == 0) continue;
                auto* conn = ws_server_->get_connection(p->connection);
                if (conn && conn->is_open()) {
                    conn->send(msg);
                }
            }
        });
    }

    // Schedule ground item despawn timer (every 30 seconds, expire after 3 minutes)
    if (scheduler_ && world_) {
        scheduler_->schedule_repeating_tagged(
            duration_ms{30000},
            "ground_item_cleanup",
            [this]() {
                auto expired = world_->remove_expired_ground_items(std::chrono::seconds(180));
                for (const auto& [map, pos, item] : expired) {
                    // Broadcast removal with picker_id 0 (despawn, not picked up)
                    if (players_ && ws_server_) {
                        network::ground_item_removed_data data{
                            .picker_id = 0,
                            .picker_name = "",
                            .item_id = item.value,
                            .item_name = "",
                            .x = pos.x,
                            .y = pos.y
                        };
                        auto msg = network::make_ground_item_removed(data);

                        auto players = players_->get_players_who_can_see(map, pos);
                        for (auto pid : players) {
                            auto* p = players_->get_player(pid);
                            if (!p || p->connection.value == 0) continue;
                            auto* conn = ws_server_->get_connection(p->connection);
                            if (conn && conn->is_open()) {
                                conn->send(msg);
                            }
                        }
                    }

                    // Destroy the expired item
                    if (item_) {
                        item_->destroy_item(item);
                    }
                }

                if (!expired.empty()) {
                    LOG_DEBUG(bridge, "Despawned {} expired ground items", expired.size());
                }
            });
    }

    // Schedule environment tick (weather cycling + broadcast to all players)
    if (scheduler_ && world_ && players_) {
        scheduler_->schedule_repeating_tagged(
            duration_ms{10000},
            "environment_tick",
            [this]() {
                tick_weather();
                broadcast_environment_update();
            });
    }

    LOG_INFO(bridge, "Game handlers initialized (chat: {}, admin: {}, combat: {}, npc: {}, inventory: {}, item: {})",
        social_ != nullptr ? "yes" : "no",
        admin_ != nullptr ? "yes" : "no",
        combat_ != nullptr ? "yes" : "no",
        npc_ != nullptr ? "yes" : "no",
        inventory_ != nullptr ? "yes" : "no",
        item_ != nullptr ? "yes" : "no");
}

void game_handlers::handle_message(connection_id conn_id, const network::json_message& msg) {
    switch (msg.type) {
        // Movement
        case network::json_message_type::player_move_request:
            handle_player_move(conn_id, msg);
            break;
        case network::json_message_type::player_stop_request:
            handle_player_stop(conn_id, msg);
            break;

        // Combat
        case network::json_message_type::player_attack_request:
            handle_player_attack(conn_id, msg);
            break;

        // Actions
        case network::json_message_type::player_magic_request:
            handle_player_magic(conn_id, msg);
            break;
        case network::json_message_type::player_skill_request:
            handle_player_skill(conn_id, msg);
            break;
        case network::json_message_type::player_pickup_request:
            handle_player_pickup(conn_id, msg);
            break;
        case network::json_message_type::player_interact_request:
            handle_player_interact(conn_id, msg);
            break;

        // Equipment
        case network::json_message_type::player_equip_request:
            handle_player_equip(conn_id, msg);
            break;
        case network::json_message_type::player_unequip_request:
            handle_player_unequip(conn_id, msg);
            break;

        // NPC interaction - shops
        case network::json_message_type::shop_buy_request:
            handle_shop_buy(conn_id, msg);
            break;
        case network::json_message_type::shop_sell_request:
            handle_shop_sell(conn_id, msg);
            break;
        case network::json_message_type::shop_sell_confirm_request:
            handle_shop_sell_confirm(conn_id, msg);
            break;
        case network::json_message_type::shop_repair_request:
            handle_shop_repair(conn_id, msg);
            break;
        case network::json_message_type::shop_repair_confirm_request:
            handle_shop_repair_confirm(conn_id, msg);
            break;

        // NPC interaction - banking
        case network::json_message_type::bank_deposit_request:
            handle_bank_deposit(conn_id, msg);
            break;
        case network::json_message_type::bank_withdraw_request:
            handle_bank_withdraw(conn_id, msg);
            break;

        // NPC interaction - dialog
        case network::json_message_type::dialog_choice_request:
            handle_dialog_choice(conn_id, msg);
            break;

        // Chat
        case network::json_message_type::chat_message:
            handle_chat_message(conn_id, msg);
            break;

        // Commands
        case network::json_message_type::command_request:
            handle_command(conn_id, msg);
            break;

        // View range
        case network::json_message_type::set_view_range:
            handle_set_view_range(conn_id, msg);
            break;

        // Entity info
        case network::json_message_type::entity_info_request:
            handle_entity_info_request(conn_id, msg);
            break;

        // Crafting - manufacturing
        case network::json_message_type::manufacture_list_request:
            handle_manufacture_list_request(conn_id, msg);
            break;
        case network::json_message_type::manufacture_request:
            handle_manufacture_request(conn_id, msg);
            break;

        // Crafting - alchemy
        case network::json_message_type::alchemy_list_request:
            handle_alchemy_list_request(conn_id, msg);
            break;
        case network::json_message_type::alchemy_request:
            handle_alchemy_request(conn_id, msg);
            break;

        // Mining
        case network::json_message_type::mine_request:
            handle_mine_request(conn_id, msg);
            break;

        default:
            LOG_WARN(bridge, "Unhandled game message type: {}",
                network::to_string(msg.type));
            send_error(conn_id, msg.seq, "unknown_message_type",
                "Message type not recognized by game handler");
            break;
    }
}

void game_handlers::handle_player_move(connection_id conn_id, const network::json_message& msg) {
    auto* conn = require_in_game(conn_id, msg.seq);
    if (!conn) return;

    if (!players_) {
        send_error(conn_id, msg.seq, "internal_error", "Player system unavailable");
        return;
    }

    // Parse request
    auto data_result = network::player_move_request_data::from_json(msg.data);
    if (data_result.is_err()) {
        send_error(conn_id, msg.seq, "invalid_request", data_result.error());
        return;
    }

    auto& data = data_result.value();
    auto pid = conn->player();

    // Get current player
    auto* player = players_->get_player(pid);
    if (!player) {
        send_error(conn_id, msg.seq, "invalid_player", "Player not found");
        return;
    }

    // Validate client position matches server position (anti-cheat)
    if (std::abs(player->pos.x - data.x) > 1 || std::abs(player->pos.y - data.y) > 1) {
        LOG_WARN(bridge, "Player {} position mismatch: client ({},{}) server ({},{})",
            pid.value, data.x, data.y, player->pos.x, player->pos.y);
        // Clear destination - desync interrupts movement
        conn->clear_destination();
        // Send correction - client should resync
        conn->send(network::make_player_move_response(
            msg.seq, false, player->pos.x, player->pos.y,
            static_cast<int16_t>(player->facing), "position_desync"));
        return;
    }

    // Store old position for visibility calculations
    auto old_pos = player->pos;

    // Calculate target position from client's reported position
    // This ensures exactly 1-tile movement from where the client thinks they are
    // The desync check above ensures client position is within tolerance
    auto dir = static_cast<world::direction>(data.direction & 7);  // Clamp to 0-7
    world::position client_pos{data.x, data.y};
    world::position target_pos = client_pos.move(dir);

    auto move_result = players_->try_move(pid, target_pos, dir);

    switch (move_result.result) {
        case player::player_system::move_result::success: {
            // Update destination on connection if provided
            if (data.dest_x.has_value() && data.dest_y.has_value()) {
                conn->set_destination(*data.dest_x, *data.dest_y);
            }

            // Check if player has reached destination
            std::optional<int16_t> broadcast_dest_x;
            std::optional<int16_t> broadcast_dest_y;

            if (conn->has_destination()) {
                if (target_pos.x == conn->dest_x() && target_pos.y == conn->dest_y()) {
                    // Reached destination - clear it
                    conn->clear_destination();
                } else {
                    // Still moving toward destination - include in broadcast
                    broadcast_dest_x = conn->dest_x();
                    broadcast_dest_y = conn->dest_y();
                }
            }

            // Send success response to moving player
            auto response = network::make_player_move_response(
                msg.seq, true, target_pos.x, target_pos.y, data.direction);
            conn->send(response);

            // Broadcast position to nearby players (with destination if still moving)
            broadcast_position_update(pid, target_pos.x, target_pos.y, data.direction,
                                      data.is_running, broadcast_dest_x, broadcast_dest_y);

            // Update entity visibility for all affected players
            update_entity_visibility(pid, old_pos, target_pos);

            LOG_DEBUG(bridge, "Player {} {} to ({}, {})",
                pid.value, data.is_running ? "ran" : "walked", target_pos.x, target_pos.y);
            break;
        }

        case player::player_system::move_result::teleport: {
            // Clear destination - teleport interrupts movement
            conn->clear_destination();

            // Execute the teleport with full handling
            execute_player_teleport(pid, conn_id, msg.seq,
                move_result.teleport_dest_map,
                move_result.teleport_dest_pos,
                move_result.teleport_dest_dir);
            break;
        }

        case player::player_system::move_result::blocked_terrain:
            // Clear destination - movement was interrupted
            conn->clear_destination();
            conn->send(network::make_player_move_response(
                msg.seq, false, player->pos.x, player->pos.y,
                static_cast<int16_t>(player->facing), "blocked_terrain"));
            break;

        case player::player_system::move_result::blocked_occupied:
            // Clear destination - movement was interrupted (bumped)
            conn->clear_destination();
            conn->send(network::make_player_move_response(
                msg.seq, false, player->pos.x, player->pos.y,
                static_cast<int16_t>(player->facing), "blocked_occupied"));
            break;

        case player::player_system::move_result::blocked_out_of_bounds:
            // Clear destination - movement was interrupted
            conn->clear_destination();
            conn->send(network::make_player_move_response(
                msg.seq, false, player->pos.x, player->pos.y,
                static_cast<int16_t>(player->facing), "out_of_bounds"));
            break;

        case player::player_system::move_result::blocked_status:
            // Clear destination - movement was interrupted
            conn->clear_destination();
            conn->send(network::make_player_move_response(
                msg.seq, false, player->pos.x, player->pos.y,
                static_cast<int16_t>(player->facing), "cannot_move"));
            break;

        case player::player_system::move_result::blocked_dead:
            // Clear destination - movement was interrupted
            conn->clear_destination();
            conn->send(network::make_player_move_response(
                msg.seq, false, player->pos.x, player->pos.y,
                static_cast<int16_t>(player->facing), "dead"));
            break;

        default:
            // Clear destination - movement was interrupted
            conn->clear_destination();
            conn->send(network::make_player_move_response(
                msg.seq, false, player->pos.x, player->pos.y,
                static_cast<int16_t>(player->facing), "move_failed"));
            break;
    }
}

void game_handlers::handle_player_stop(connection_id conn_id, const network::json_message& msg) {
    auto* conn = require_in_game(conn_id, msg.seq);
    if (!conn) return;

    if (!players_) {
        send_error(conn_id, msg.seq, "internal_error", "Player system unavailable");
        return;
    }

    auto data_result = network::player_stop_request_data::from_json(msg.data);
    if (data_result.is_err()) {
        send_error(conn_id, msg.seq, "invalid_request", data_result.error());
        return;
    }

    auto& data = data_result.value();
    auto pid = conn->player();

    auto* player = players_->get_player(pid);
    if (!player) {
        send_error(conn_id, msg.seq, "invalid_player", "Player not found");
        return;
    }

    // Clear destination - player explicitly stopped
    conn->clear_destination();

    // Update facing direction if provided
    if (data.direction.has_value()) {
        player->facing = static_cast<world::direction>(data.direction.value() & 7);
    }

    auto direction = static_cast<int16_t>(player->facing);

    // Acknowledge the stop to the sender
    conn->send(network::make_player_stop_response(
        msg.seq, true, player->pos.x, player->pos.y, direction));

    // Broadcast position update to nearby players (not running = stopped, no destination)
    broadcast_position_update(pid, player->pos.x, player->pos.y, direction, false);

    LOG_DEBUG(bridge, "Player {} stopped at ({}, {}) facing {}",
        pid.value, player->pos.x, player->pos.y, direction);
}

void game_handlers::handle_player_attack(connection_id conn_id, const network::json_message& msg) {
    auto* conn = require_in_game(conn_id, msg.seq);
    if (!conn) return;

    if (!players_) {
        send_error(conn_id, msg.seq, "internal_error", "Player system unavailable");
        return;
    }

    if (!combat_) {
        send_error(conn_id, msg.seq, "internal_error", "Combat system unavailable");
        return;
    }

    auto data_result = network::player_attack_request_data::from_json(msg.data);
    if (data_result.is_err()) {
        send_error(conn_id, msg.seq, "invalid_request", data_result.error());
        return;
    }

    auto& data = data_result.value();
    auto pid = conn->player();

    auto* attacker = players_->get_player(pid);
    if (!attacker) {
        send_error(conn_id, msg.seq, "invalid_player", "Player not found");
        return;
    }

    // Check if attacker is alive
    if (attacker->is_dead()) {
        network::attack_result_msg result{
            .hit = false,
            .target_id = data.target_id,
            .attacker_x = attacker->pos.x,
            .attacker_y = attacker->pos.y
        };
        conn->send(network::make_player_attack_response(msg.seq, false, &result, "attacker_dead"));
        return;
    }

    // Check if attacker has movement/action blocking status
    if (attacker->has_status(player::player_status::stunned) ||
        attacker->has_status(player::player_status::paralyzed) ||
        attacker->has_status(player::player_status::frozen)) {
        network::attack_result_msg result{
            .hit = false,
            .target_id = data.target_id,
            .attacker_x = attacker->pos.x,
            .attacker_y = attacker->pos.y
        };
        conn->send(network::make_player_attack_response(msg.seq, false, &result, "cannot_attack"));
        return;
    }

    // Only PvP for now (target must be a player)
    if (data.tgt_type != network::target_type::player) {
        network::attack_result_msg result{
            .hit = false,
            .target_id = data.target_id,
            .attacker_x = attacker->pos.x,
            .attacker_y = attacker->pos.y
        };
        conn->send(network::make_player_attack_response(msg.seq, false, &result, "invalid_target_type"));
        return;
    }

    // Get target player - resolve entity_id from client to player_id
    auto target_pid_opt = players_->get_player_id_by_entity(entity::entity{data.target_id});
    auto* target = target_pid_opt ? players_->get_player(*target_pid_opt) : nullptr;
    if (!target) {
        network::attack_result_msg result{
            .hit = false,
            .target_id = data.target_id,
            .attacker_x = attacker->pos.x,
            .attacker_y = attacker->pos.y
        };
        conn->send(network::make_player_attack_response(msg.seq, false, &result, "target_not_found"));
        return;
    }

    // Check target is alive
    if (target->is_dead()) {
        network::attack_result_msg result{
            .hit = false,
            .target_id = data.target_id,
            .attacker_x = attacker->pos.x,
            .attacker_y = attacker->pos.y
        };
        conn->send(network::make_player_attack_response(msg.seq, false, &result, "target_dead"));
        return;
    }

    // Check same map
    if (attacker->current_map != target->current_map) {
        network::attack_result_msg result{
            .hit = false,
            .target_id = data.target_id,
            .attacker_x = attacker->pos.x,
            .attacker_y = attacker->pos.y
        };
        conn->send(network::make_player_attack_response(msg.seq, false, &result, "target_not_in_range"));
        return;
    }

    // Determine if this is a ranged attack (bow equipped)
    bool is_ranged = false;
    network::projectile_type projectile = network::projectile_type::none;
    const item_template* weapon_tmpl = nullptr;

    auto* item_reg = subsystems().get<item_registry>();
    if (item_reg && attacker->equipment.has_equipped(player::equip_slot::weapon)) {
        weapon_tmpl = item_reg->get(attacker->equipment.weapon().id);
        if (weapon_tmpl && weapon_tmpl->is_bow()) {
            is_ranged = true;
        }
    }

    // For ranged attacks: check arrows and determine projectile type
    uint32_t arrow_template_id = 0;
    int32_t ammo_remaining = -1;

    if (is_ranged) {
        if (!inventory_) {
            send_error(conn_id, msg.seq, "internal_error", "Inventory system unavailable");
            return;
        }

        // Find arrows in inventory - prefer poison arrows (78), then normal (77)
        auto attacker_entity = entity_id{pid.value};
        constexpr uint32_t poison_arrow_id = 78;
        constexpr uint32_t normal_arrow_id = 77;

        if (inventory_->has_item(attacker_entity, item_id{poison_arrow_id})) {
            arrow_template_id = poison_arrow_id;
            projectile = network::projectile_type::poison_arrow;
        } else if (inventory_->has_item(attacker_entity, item_id{normal_arrow_id})) {
            arrow_template_id = normal_arrow_id;
            projectile = network::projectile_type::arrow;
        } else {
            network::attack_result_msg result{
                .hit = false,
                .target_id = data.target_id,
                .attacker_x = attacker->pos.x,
                .attacker_y = attacker->pos.y,
                .is_ranged = true
            };
            conn->send(network::make_player_attack_response(msg.seq, false, &result, "no_ammo"));
            return;
        }
    }

    // Calculate distance
    int distance = attacker->pos.chebyshev_distance(target->pos);

    // Validate range based on attack type and weapon
    int max_range = 1;  // Melee default
    if (is_ranged) {
        max_range = 10;  // Bow range (standard Helbreath bow range)
    } else if (data.type == network::attack_type::dash) {
        max_range = 2;   // Dash attack range
    }

    if (distance > max_range) {
        network::attack_result_msg result{
            .hit = false,
            .target_id = data.target_id,
            .attacker_x = attacker->pos.x,
            .attacker_y = attacker->pos.y,
            .is_ranged = is_ranged
        };
        conn->send(network::make_player_attack_response(msg.seq, false, &result, "target_not_in_range"));
        return;
    }

    // Ranged attacks require minimum distance (can't fire at melee range)
    if (is_ranged && distance < 2) {
        network::attack_result_msg result{
            .hit = false,
            .target_id = data.target_id,
            .attacker_x = attacker->pos.x,
            .attacker_y = attacker->pos.y,
            .is_ranged = true
        };
        conn->send(network::make_player_attack_response(msg.seq, false, &result, "target_too_close"));
        return;
    }

    // Consume arrow before processing attack
    if (is_ranged && arrow_template_id > 0) {
        inventory_->remove_item(entity_id{pid.value}, item_id{arrow_template_id}, 1);
        ammo_remaining = inventory_->count_item(entity_id{pid.value}, item_id{arrow_template_id});
    }

    // Build attack event
    combat::attack_event attack;
    attack.attacker = entity::entity{pid.value};
    attack.defender = entity::entity{target_pid_opt->value};
    attack.type = combat::damage_type::physical;
    attack.base_damage = 0;  // Let combat_system calculate from stats
    attack.is_skill = false;
    attack.is_ranged = is_ranged;
    attack.distance = distance;

    // Process the attack through combat system
    auto combat_result = combat_->process_attack(attack);

    // Build response
    network::attack_result_msg result{
        .hit = combat_result.hit.is_hit(),
        .critical = combat_result.hit.is_critical(),
        .damage = combat_result.hit.final_damage,
        .target_id = data.target_id,
        .target_hp = static_cast<int16_t>(target->hp),
        .target_hp_max = static_cast<int16_t>(target->computed.max_hp),
        .attacker_x = attacker->pos.x,
        .attacker_y = attacker->pos.y,
        .is_ranged = is_ranged,
        .ammo_count = ammo_remaining,
        .ammo_template_id = arrow_template_id
    };

    // Send response to attacker
    conn->send(network::make_player_attack_response(msg.seq, true, &result));

    // Broadcast attack to players who can see the attacker
    auto nearby = players_->get_players_who_can_see(attacker->current_map, attacker->pos);

    // Create broadcast message (includes projectile info for ranged)
    auto broadcast_msg = network::make_combat_attack_broadcast(
        attacker->ecs_entity.id,
        target->ecs_entity.id,
        attacker->pos.x, attacker->pos.y,
        target->pos.x, target->pos.y,
        combat_result.hit.is_hit(),
        combat_result.hit.is_critical(),
        combat_result.hit.final_damage,
        projectile
    );

    for (auto other_id : nearby) {
        if (other_id == pid) continue;  // Attacker already got response

        auto* other = players_->get_player(other_id);
        if (!other || other->connection.value == 0) continue;

        auto* other_conn = ws_server_->get_connection(other->connection);
        if (other_conn && other_conn->is_open()) {
            other_conn->send(broadcast_msg);
        }
    }

    LOG_DEBUG(bridge, "Player {} {} player {} (hit={}, crit={}, dmg={}, target_hp={}, ranged={})",
        pid.value, is_ranged ? "shot" : "attacked", target_pid_opt->value,
        combat_result.hit.is_hit(), combat_result.hit.is_critical(),
        combat_result.hit.final_damage, target->hp, is_ranged);
}

void game_handlers::handle_player_magic(connection_id conn_id, const network::json_message& msg) {
    auto* conn = require_in_game(conn_id, msg.seq);
    if (!conn) return;

    if (!players_) {
        send_error(conn_id, msg.seq, "internal_error", "Player system unavailable");
        return;
    }

    auto data_result = network::player_magic_request_data::from_json(msg.data);
    if (data_result.is_err()) {
        send_error(conn_id, msg.seq, "invalid_request", data_result.error());
        return;
    }

    auto& data = data_result.value();
    auto pid = conn->player();

    auto* player = players_->get_player(pid);
    if (!player) {
        send_error(conn_id, msg.seq, "invalid_player", "Player not found");
        return;
    }

    if (!magic_) {
        send_error(conn_id, msg.seq, "internal_error", "Magic system unavailable");
        return;
    }

    // Build cast target from request data
    magic::cast_target target{};
    if (data.tgt_type == network::target_type::player || data.tgt_type == network::target_type::npc) {
        target.target = entity::entity{data.target_id};
    }
    if (data.target_x != 0 || data.target_y != 0) {
        target.target_pos = world::position{data.target_x, data.target_y};
    }

    // Look up spell to determine cast type
    auto sid = spell_id(static_cast<int>(data.spell_id));
    auto* spell_tmpl = magic_->get_spell(sid);
    if (!spell_tmpl) {
        conn->send(network::make_player_magic_response(msg.seq, false, nullptr, "unknown_spell"));
        return;
    }

    if (spell_tmpl->cast_time_ms > 0) {
        // Channeled/cast time spell
        auto cast_result = magic_->begin_cast(player->ecs_entity, sid, target);
        if (cast_result.is_err()) {
            conn->send(network::make_player_magic_response(msg.seq, false, nullptr,
                                                            cast_result.error()));
            return;
        }

        // Cast started - result will come via callback when cast completes
        network::magic_result_msg result{
            .success = true,
            .spell_id = data.spell_id,
            .mana_cost = spell_tmpl->mana_cost,
            .damage = 0,
            .heal = 0,
            .target_id = data.target_id,
            .caster_mp = static_cast<int16_t>(player->mp)
        };
        conn->send(network::make_player_magic_response(msg.seq, true, &result));
    } else {
        // Instant cast
        auto cast_result = magic_->instant_cast(player->ecs_entity, sid, target);
        if (cast_result.is_err()) {
            conn->send(network::make_player_magic_response(msg.seq, false, nullptr,
                                                            cast_result.error()));
            return;
        }

        auto& effect = cast_result.value();
        network::magic_result_msg result{
            .success = effect.success,
            .spell_id = data.spell_id,
            .mana_cost = spell_tmpl->mana_cost,
            .damage = effect.damage_dealt,
            .heal = effect.heal_applied,
            .target_id = data.target_id,
            .caster_mp = static_cast<int16_t>(player->mp)
        };

        conn->send(network::make_player_magic_response(msg.seq, effect.success, &result,
            effect.success ? std::optional<std::string_view>{} : std::optional<std::string_view>{"cast_failed"}));
    }

    LOG_DEBUG(bridge, "Player {} magic request (spell={}, target={})",
        pid.value, data.spell_id, data.target_id);
}

void game_handlers::handle_player_skill(connection_id conn_id, const network::json_message& msg) {
    auto* conn = require_in_game(conn_id, msg.seq);
    if (!conn) return;

    if (!players_) {
        send_error(conn_id, msg.seq, "internal_error", "Player system unavailable");
        return;
    }

    auto data_result = network::player_skill_request_data::from_json(msg.data);
    if (data_result.is_err()) {
        send_error(conn_id, msg.seq, "invalid_request", data_result.error());
        return;
    }

    auto& data = data_result.value();
    auto pid = conn->player();

    auto* player = players_->get_player(pid);
    if (!player) {
        send_error(conn_id, msg.seq, "invalid_player", "Player not found");
        return;
    }

    // TODO: Implement actual skill use through skill_system
    // Placeholder response
    network::skill_result_msg result{
        .success = false,
        .skill_id = data.skill_id,
        .effect_value = 0,
        .target_id = data.target_id
    };

    conn->send(network::make_player_skill_response(msg.seq, false, &result, "not_implemented"));
    LOG_DEBUG(bridge, "Player {} skill request (skill={}, target={})",
        pid.value, data.skill_id, data.target_id);
}

void game_handlers::handle_player_pickup(connection_id conn_id, const network::json_message& msg) {
    auto* conn = require_in_game(conn_id, msg.seq);
    if (!conn) return;

    if (!players_ || !world_ || !inventory_) {
        send_error(conn_id, msg.seq, "internal_error", "Required subsystems unavailable");
        return;
    }

    auto data_result = network::player_pickup_request_data::from_json(msg.data);
    if (data_result.is_err()) {
        send_error(conn_id, msg.seq, "invalid_request", data_result.error());
        return;
    }

    auto& data = data_result.value();
    auto pid = conn->player();

    auto* player = players_->get_player(pid);
    if (!player) {
        send_error(conn_id, msg.seq, "invalid_player", "Player not found");
        return;
    }

    // No items on ground - silently ignore (no-op)
    if (!world_->has_ground_items(player->current_map, player->pos)) {
        return;
    }

    // Check if inventory has space
    auto player_entity = entity_id{pid.value};
    if (inventory_->is_full(player_entity)) {
        send_error(conn_id, msg.seq, "inventory_full", "Cannot carry more items");
        return;
    }

    // Remove top-most item from ground
    auto item_id_opt = world_->remove_top_ground_item(player->current_map, player->pos);
    if (!item_id_opt.has_value()) {
        send_error(conn_id, msg.seq, "item_not_found", "Item no longer available");
        return;
    }

    auto picked_item_id = item_id_opt.value();

    // Add item to inventory
    auto add_result = inventory_->add_item(player_entity, picked_item_id);
    if (add_result != inventory::inventory_result::success) {
        // Failed to add - put item back on ground
        world_->add_ground_item(player->current_map, player->pos, picked_item_id);
        send_error(conn_id, msg.seq, "inventory_full", "Failed to add item to inventory");
        return;
    }

    // Get item details for response
    std::string item_name = "Unknown";
    int16_t quantity = 1;
    if (item_) {
        auto* itm = item_->get_item(picked_item_id);
        if (itm) {
            item_name = itm->name;
            quantity = itm->count;
        }
    }

    // Success! Send response to player
    network::pickup_result_msg result{
        .success = true,
        .item_id = picked_item_id.value,
        .item_name = item_name,
        .quantity = quantity,
        .inventory_slot = 0  // TODO: Get actual slot from inventory system
    };

    conn->send(network::make_player_pickup_response(msg.seq, true, &result, std::nullopt));

    // Broadcast item removal to nearby players
    broadcast_ground_item_removed(pid, player->current_map, player->pos, picked_item_id);

    LOG_INFO(bridge, "Player {} picked up item {} ({}) at ({}, {})",
        pid.value, picked_item_id.value, item_name, player->pos.x, player->pos.y);
}

void game_handlers::handle_player_interact(connection_id conn_id, const network::json_message& msg) {
    auto* conn = require_in_game(conn_id, msg.seq);
    if (!conn) return;

    auto data_result = network::player_interact_request_data::from_json(msg.data);
    if (data_result.is_err()) {
        send_error(conn_id, msg.seq, "invalid_request", data_result.error());
        return;
    }

    auto& data = data_result.value();
    auto check = validate_npc_interaction(conn_id, msg.seq, data.target_id);
    if (!check.valid) return;

    auto* player = check.plr;
    auto* target = check.target_npc;

    // Route based on NPC type and available registries

    // 1. Check if NPC has a shop
    if (shop_registry_) {
        auto* shop = shop_registry_->get_shop(target->name);
        if (shop) {
            // Check territory restriction
            if (world_) {
                auto* map = world_->get_map(player->current_map);
                if (map && !npc::can_buy_in_territory(player->faction, map->location_name())) {
                    send_error(conn_id, msg.seq, "hostile_territory",
                        "You cannot trade in hostile territory");
                    return;
                }
            }

            // Build shop item list for client
            auto* item_reg = subsystems().get<item_registry>();
            nlohmann::json shop_data;
            shop_data["shop_type"] = target->name;
            auto items_array = nlohmann::json::array();
            for (const auto& entry : shop->items) {
                nlohmann::json item_json;
                item_json["item_id"] = entry.item.value;
                item_json["count"] = entry.default_count;
                if (item_reg) {
                    if (auto* tmpl = item_reg->get(entry.item)) {
                        item_json["name"] = tmpl->name;
                        item_json["price"] = npc::calculate_buy_price(
                            tmpl->price, 1, player->base.charisma);
                        item_json["base_price"] = tmpl->price;
                    }
                }
                items_array.push_back(std::move(item_json));
            }
            shop_data["items"] = std::move(items_array);

            network::interact_result_msg result{
                .success = true,
                .target_id = data.target_id,
                .interaction_type = "shop",
                .interaction_data = std::move(shop_data)
            };
            conn->send(network::make_player_interact_response(msg.seq, true, &result));
            LOG_DEBUG(bridge, "Player {} opened shop at NPC '{}'",
                player->id.value, target->name);
            return;
        }
    }

    // 2. Check if NPC is a banker/warehouse
    if (target->category == npc::npc_category::banker ||
        target->category == npc::npc_category::warehouse) {
        // Send bank contents
        nlohmann::json bank_data;
        bank_data["npc_name"] = target->name;

        auto items_array = nlohmann::json::array();
        if (inventory_) {
            auto* bank = inventory_->get_bank(entity_id(player->id.value));
            if (bank) {
                for (int16_t i = 0; i < bank->capacity(); ++i) {
                    auto* slot = bank->get_slot(i);
                    if (slot && !slot->is_empty()) {
                        nlohmann::json slot_json;
                        slot_json["slot"] = i;
                        slot_json["item_id"] = slot->item.value;
                        slot_json["count"] = slot->count;
                        if (item_) {
                            auto* itm = item_->get_item(slot->item);
                            if (itm) {
                                slot_json["name"] = itm->name;
                                slot_json["durability"] = itm->durability;
                                slot_json["max_durability"] = itm->max_durability;
                            }
                        }
                        items_array.push_back(std::move(slot_json));
                    }
                }
            }
        }
        bank_data["items"] = std::move(items_array);

        network::interact_result_msg result{
            .success = true,
            .target_id = data.target_id,
            .interaction_type = "bank",
            .interaction_data = std::move(bank_data)
        };
        conn->send(network::make_player_interact_response(msg.seq, true, &result));
        LOG_DEBUG(bridge, "Player {} opened bank at NPC '{}'",
            player->id.value, target->name);
        return;
    }

    // 3. Check if NPC has a dialog tree
    if (dialog_registry_) {
        auto* dialog = dialog_registry_->get_dialog(target->name);
        if (dialog) {
            auto* start = dialog_registry_->get_node(target->name, dialog->start_node);
            nlohmann::json dialog_data;
            dialog_data["npc_name"] = target->name;
            dialog_data["greeting"] = dialog->greeting;
            if (start) {
                dialog_data["node_id"] = start->id;
                dialog_data["text"] = start->text;
                auto opts = nlohmann::json::array();
                for (const auto& opt : start->options) {
                    nlohmann::json opt_json;
                    opt_json["label"] = opt.label;
                    opt_json["action"] = static_cast<int>(opt.action);
                    if (!opt.next_node.empty()) {
                        opt_json["next_node"] = opt.next_node;
                    }
                    opts.push_back(std::move(opt_json));
                }
                dialog_data["options"] = std::move(opts);
            }

            network::interact_result_msg result{
                .success = true,
                .target_id = data.target_id,
                .interaction_type = "dialog",
                .interaction_data = std::move(dialog_data)
            };
            conn->send(network::make_player_interact_response(msg.seq, true, &result));
            LOG_DEBUG(bridge, "Player {} opened dialog with NPC '{}'",
                player->id.value, target->name);
            return;
        }
    }

    // No interaction available
    network::interact_result_msg result{
        .success = false,
        .target_id = data.target_id,
        .interaction_type = "none",
        .interaction_data = nlohmann::json::object()
    };
    conn->send(network::make_player_interact_response(msg.seq, false, &result, "npc_no_interaction"));
    LOG_DEBUG(bridge, "Player {} interact request with NPC '{}' - no interaction available",
        player->id.value, target->name);
}

auto game_handlers::validate_npc_interaction(connection_id conn_id, uint32_t seq, uint32_t npc_entity_id)
    -> npc_interaction_check
{
    auto* conn = require_in_game(conn_id, seq);
    if (!conn) return {.valid = false, .error = "not_in_game"};

    if (!players_ || !npc_) {
        send_error(conn_id, seq, "internal_error", "Required systems unavailable");
        return {.valid = false, .error = "internal_error"};
    }

    auto pid = conn->player();
    auto* player = players_->get_player(pid);
    if (!player) {
        send_error(conn_id, seq, "invalid_player", "Player not found");
        return {.valid = false, .error = "invalid_player"};
    }

    auto* target = npc_->get_npc(entity::entity{npc_entity_id});
    if (!target) {
        send_error(conn_id, seq, "npc_not_found", "NPC not found");
        return {.valid = false, .error = "npc_not_found"};
    }

    if (!target->is_alive()) {
        send_error(conn_id, seq, "npc_dead", "NPC is dead");
        return {.valid = false, .error = "npc_dead"};
    }

    if (!target->is_friendly()) {
        send_error(conn_id, seq, "npc_hostile", "Cannot interact with hostile NPC");
        return {.valid = false, .error = "npc_hostile"};
    }

    // Check range (must be within 3 tiles)
    if (player->current_map != target->current_map ||
        player->pos.distance(target->pos) > 3) {
        send_error(conn_id, seq, "too_far", "NPC is too far away");
        return {.valid = false, .error = "too_far"};
    }

    return {.plr = player, .target_npc = target, .valid = true};
}

void game_handlers::handle_shop_buy(connection_id conn_id, const network::json_message& msg) {
    auto data_result = network::shop_buy_request_data::from_json(msg.data);
    if (data_result.is_err()) {
        send_error(conn_id, msg.seq, "invalid_request", data_result.error());
        return;
    }
    auto& data = data_result.value();

    auto check = validate_npc_interaction(conn_id, msg.seq, data.npc_entity_id);
    if (!check.valid) return;

    if (!shop_registry_ || !inventory_ || !item_) {
        send_error(conn_id, msg.seq, "internal_error", "Shop system unavailable");
        return;
    }

    auto* shop = shop_registry_->get_shop(check.target_npc->name);
    if (!shop) {
        send_error(conn_id, msg.seq, "not_a_shop", "This NPC does not have a shop");
        return;
    }

    // Territory check
    if (world_) {
        auto* map = world_->get_map(check.plr->current_map);
        if (map && !npc::can_buy_in_territory(check.plr->faction, map->location_name())) {
            send_error(conn_id, msg.seq, "hostile_territory", "Cannot buy in hostile territory");
            return;
        }
    }

    // Verify the item is in the shop
    bool item_in_shop = false;
    for (const auto& entry : shop->items) {
        if (entry.item.value == data.item_template_id) {
            item_in_shop = true;
            break;
        }
    }
    if (!item_in_shop) {
        send_error(conn_id, msg.seq, "item_not_in_shop", "Item not available in this shop");
        return;
    }

    // Look up item template for price
    auto* item_reg = subsystems().get<item_registry>();
    if (!item_reg) {
        send_error(conn_id, msg.seq, "internal_error", "Item registry unavailable");
        return;
    }

    auto* tmpl = item_reg->get(item_id{data.item_template_id});
    if (!tmpl) {
        send_error(conn_id, msg.seq, "item_not_found", "Item template not found");
        return;
    }

    int16_t count = std::max<int16_t>(data.count, 1);
    int32_t total_price = npc::calculate_buy_price(tmpl->price, count, check.plr->base.charisma);

    auto owner_id = entity_id(check.plr->id.value);

    // Check gold
    if (!inventory_->has_gold(owner_id, total_price)) {
        send_error(conn_id, msg.seq, "insufficient_gold", "Not enough gold");
        return;
    }

    // Check inventory space
    if (inventory_->is_full(owner_id)) {
        send_error(conn_id, msg.seq, "inventory_full", "Inventory is full");
        return;
    }

    // Create the item
    auto create_result = item_->create_from_template(item_id{data.item_template_id}, count);
    if (create_result.is_err()) {
        send_error(conn_id, msg.seq, "create_failed", "Failed to create item");
        return;
    }

    auto new_item_id = create_result.value();

    // Add to inventory
    auto add_result = inventory_->add_item(owner_id, new_item_id, count);
    if (add_result != inventory::inventory_result::success) {
        item_->destroy_item(new_item_id);
        send_error(conn_id, msg.seq, "add_failed", "Failed to add item to inventory");
        return;
    }

    // Deduct gold
    inventory_->remove_gold(owner_id, total_price);

    auto* conn = ws_server_->get_connection(conn_id);
    if (conn) {
        conn->send(network::make_shop_buy_response(msg.seq, true,
            tmpl->name, count, total_price, inventory_->get_gold(owner_id)));
    }

    LOG_DEBUG(bridge, "Player {} bought {}x '{}' for {} gold",
        check.plr->id.value, count, tmpl->name, total_price);
}

void game_handlers::handle_shop_sell(connection_id conn_id, const network::json_message& msg) {
    auto data_result = network::shop_sell_request_data::from_json(msg.data);
    if (data_result.is_err()) {
        send_error(conn_id, msg.seq, "invalid_request", data_result.error());
        return;
    }
    auto& data = data_result.value();

    auto check = validate_npc_interaction(conn_id, msg.seq, data.npc_entity_id);
    if (!check.valid) return;

    if (!shop_registry_ || !inventory_ || !item_) {
        send_error(conn_id, msg.seq, "internal_error", "Shop system unavailable");
        return;
    }

    auto* shop = shop_registry_->get_shop(check.target_npc->name);
    if (!shop) {
        send_error(conn_id, msg.seq, "not_a_shop", "This NPC does not have a shop");
        return;
    }

    auto owner_id = entity_id(check.plr->id.value);
    auto* inv = inventory_->get_inventory(owner_id);
    if (!inv) {
        send_error(conn_id, msg.seq, "no_inventory", "No inventory found");
        return;
    }

    auto* slot = inv->get_slot(data.inventory_slot);
    if (!slot || slot->is_empty()) {
        send_error(conn_id, msg.seq, "empty_slot", "No item in that slot");
        return;
    }

    auto* itm = item_->get_item(slot->item);
    if (!itm) {
        send_error(conn_id, msg.seq, "item_not_found", "Item not found");
        return;
    }

    // Check category
    auto* item_reg = subsystems().get<item_registry>();
    if (item_reg) {
        auto* tmpl = item_reg->get(itm->template_id);
        if (tmpl && !npc::is_category_accepted(*shop, static_cast<uint8_t>(tmpl->category))) {
            send_error(conn_id, msg.seq, "category_rejected", "Shop does not buy this type of item");
            return;
        }
    }

    // Calculate sell price (quote)
    bool is_neutral = false;
    if (world_) {
        auto* map = world_->get_map(check.plr->current_map);
        if (map) {
            is_neutral = npc::is_neutral_territory(map->location_name());
        }
    }

    int32_t offered_price = 0;
    if (itm->is_equipment()) {
        offered_price = npc::calculate_sell_price_equipment(
            itm->price, itm->durability, itm->max_durability, is_neutral);
    } else {
        offered_price = npc::calculate_sell_price_consumable(
            itm->price, itm->count, is_neutral);
    }

    auto* conn = ws_server_->get_connection(conn_id);
    if (conn) {
        conn->send(network::make_shop_sell_response(msg.seq, true,
            itm->name, offered_price, itm->durability));
    }
}

void game_handlers::handle_shop_sell_confirm(connection_id conn_id, const network::json_message& msg) {
    auto data_result = network::shop_sell_confirm_request_data::from_json(msg.data);
    if (data_result.is_err()) {
        send_error(conn_id, msg.seq, "invalid_request", data_result.error());
        return;
    }
    auto& data = data_result.value();

    auto check = validate_npc_interaction(conn_id, msg.seq, data.npc_entity_id);
    if (!check.valid) return;

    if (!shop_registry_ || !inventory_ || !item_) {
        send_error(conn_id, msg.seq, "internal_error", "Shop system unavailable");
        return;
    }

    auto* shop = shop_registry_->get_shop(check.target_npc->name);
    if (!shop) {
        send_error(conn_id, msg.seq, "not_a_shop", "This NPC does not have a shop");
        return;
    }

    auto owner_id = entity_id(check.plr->id.value);
    auto* inv = inventory_->get_inventory(owner_id);
    if (!inv) {
        send_error(conn_id, msg.seq, "no_inventory", "No inventory found");
        return;
    }

    auto* slot = inv->get_slot(data.inventory_slot);
    if (!slot || slot->is_empty()) {
        send_error(conn_id, msg.seq, "empty_slot", "No item in that slot");
        return;
    }

    auto sell_item_id = slot->item;
    auto* itm = item_->get_item(sell_item_id);
    if (!itm) {
        send_error(conn_id, msg.seq, "item_not_found", "Item not found");
        return;
    }

    // Recalculate price (don't trust client)
    bool is_neutral = false;
    if (world_) {
        auto* map = world_->get_map(check.plr->current_map);
        if (map) {
            is_neutral = npc::is_neutral_territory(map->location_name());
        }
    }

    int32_t sell_price = 0;
    if (itm->is_equipment()) {
        sell_price = npc::calculate_sell_price_equipment(
            itm->price, itm->durability, itm->max_durability, is_neutral);
    } else {
        sell_price = npc::calculate_sell_price_consumable(
            itm->price, itm->count, is_neutral);
    }

    if (sell_price <= 0) {
        send_error(conn_id, msg.seq, "worthless", "This item has no value");
        return;
    }

    // Remove item from inventory
    inv->clear_slot(data.inventory_slot);
    item_->destroy_item(sell_item_id);

    // Add gold
    inventory_->add_gold(owner_id, sell_price);

    auto* conn = ws_server_->get_connection(conn_id);
    if (conn) {
        conn->send(network::make_shop_sell_confirm_response(msg.seq, true,
            sell_price, inventory_->get_gold(owner_id)));
    }

    LOG_DEBUG(bridge, "Player {} sold item for {} gold", check.plr->id.value, sell_price);
}

void game_handlers::handle_shop_repair(connection_id conn_id, const network::json_message& msg) {
    auto data_result = network::shop_repair_request_data::from_json(msg.data);
    if (data_result.is_err()) {
        send_error(conn_id, msg.seq, "invalid_request", data_result.error());
        return;
    }
    auto& data = data_result.value();

    auto check = validate_npc_interaction(conn_id, msg.seq, data.npc_entity_id);
    if (!check.valid) return;

    if (!shop_registry_ || !inventory_ || !item_) {
        send_error(conn_id, msg.seq, "internal_error", "Shop system unavailable");
        return;
    }

    auto* shop = shop_registry_->get_shop(check.target_npc->name);
    if (!shop) {
        send_error(conn_id, msg.seq, "not_a_shop", "This NPC does not have a shop");
        return;
    }

    auto owner_id = entity_id(check.plr->id.value);
    auto* inv = inventory_->get_inventory(owner_id);
    if (!inv) {
        send_error(conn_id, msg.seq, "no_inventory", "No inventory found");
        return;
    }

    auto* slot = inv->get_slot(data.inventory_slot);
    if (!slot || slot->is_empty()) {
        send_error(conn_id, msg.seq, "empty_slot", "No item in that slot");
        return;
    }

    auto* itm = item_->get_item(slot->item);
    if (!itm) {
        send_error(conn_id, msg.seq, "item_not_found", "Item not found");
        return;
    }

    if (!itm->is_equipment()) {
        send_error(conn_id, msg.seq, "not_repairable", "This item cannot be repaired");
        return;
    }

    // Check if this shop can repair this category
    auto* item_reg = subsystems().get<item_registry>();
    if (item_reg) {
        auto* tmpl = item_reg->get(itm->template_id);
        if (tmpl && !npc::is_category_repairable(*shop, static_cast<uint8_t>(tmpl->category))) {
            send_error(conn_id, msg.seq, "cant_repair_type", "Shop cannot repair this type of item");
            return;
        }
    }

    if (itm->durability >= itm->max_durability) {
        send_error(conn_id, msg.seq, "already_repaired", "Item is already at full durability");
        return;
    }

    int32_t repair_cost = npc::calculate_repair_cost(itm->price, itm->durability, itm->max_durability);

    auto* conn = ws_server_->get_connection(conn_id);
    if (conn) {
        conn->send(network::make_shop_repair_response(msg.seq, true,
            itm->name, repair_cost, itm->durability));
    }
}

void game_handlers::handle_shop_repair_confirm(connection_id conn_id, const network::json_message& msg) {
    auto data_result = network::shop_repair_confirm_request_data::from_json(msg.data);
    if (data_result.is_err()) {
        send_error(conn_id, msg.seq, "invalid_request", data_result.error());
        return;
    }
    auto& data = data_result.value();

    auto check = validate_npc_interaction(conn_id, msg.seq, data.npc_entity_id);
    if (!check.valid) return;

    if (!shop_registry_ || !inventory_ || !item_) {
        send_error(conn_id, msg.seq, "internal_error", "Shop system unavailable");
        return;
    }

    auto owner_id = entity_id(check.plr->id.value);
    auto* inv = inventory_->get_inventory(owner_id);
    if (!inv) {
        send_error(conn_id, msg.seq, "no_inventory", "No inventory found");
        return;
    }

    auto* slot = inv->get_slot(data.inventory_slot);
    if (!slot || slot->is_empty()) {
        send_error(conn_id, msg.seq, "empty_slot", "No item in that slot");
        return;
    }

    auto* itm = item_->get_item(slot->item);
    if (!itm) {
        send_error(conn_id, msg.seq, "item_not_found", "Item not found");
        return;
    }

    if (itm->durability >= itm->max_durability) {
        send_error(conn_id, msg.seq, "already_repaired", "Item is already at full durability");
        return;
    }

    int32_t repair_cost = npc::calculate_repair_cost(itm->price, itm->durability, itm->max_durability);

    if (!inventory_->has_gold(owner_id, repair_cost)) {
        send_error(conn_id, msg.seq, "insufficient_gold", "Not enough gold");
        return;
    }

    // Repair the item
    item_->repair_item_full(slot->item);
    inventory_->remove_gold(owner_id, repair_cost);

    // Re-fetch for updated durability
    itm = item_->get_item(slot->item);
    int16_t new_dur = itm ? itm->durability : 0;

    auto* conn = ws_server_->get_connection(conn_id);
    if (conn) {
        conn->send(network::make_shop_repair_confirm_response(msg.seq, true,
            new_dur, repair_cost, inventory_->get_gold(owner_id)));
    }

    LOG_DEBUG(bridge, "Player {} repaired item for {} gold", check.plr->id.value, repair_cost);
}

void game_handlers::handle_bank_deposit(connection_id conn_id, const network::json_message& msg) {
    auto data_result = network::bank_deposit_request_data::from_json(msg.data);
    if (data_result.is_err()) {
        send_error(conn_id, msg.seq, "invalid_request", data_result.error());
        return;
    }
    auto& data = data_result.value();

    auto check = validate_npc_interaction(conn_id, msg.seq, data.npc_entity_id);
    if (!check.valid) return;

    if (check.target_npc->category != npc::npc_category::banker &&
        check.target_npc->category != npc::npc_category::warehouse) {
        send_error(conn_id, msg.seq, "not_a_bank", "This NPC is not a banker");
        return;
    }

    if (!inventory_ || !item_) {
        send_error(conn_id, msg.seq, "internal_error", "Inventory system unavailable");
        return;
    }

    auto owner_id = entity_id(check.plr->id.value);

    // Get item name before deposit (for response)
    auto* inv = inventory_->get_inventory(owner_id);
    if (!inv) {
        send_error(conn_id, msg.seq, "no_inventory", "No inventory found");
        return;
    }

    auto* slot = inv->get_slot(data.inventory_slot);
    if (!slot || slot->is_empty()) {
        send_error(conn_id, msg.seq, "empty_slot", "No item in that slot");
        return;
    }

    std::string item_name;
    if (auto* itm = item_->get_item(slot->item)) {
        item_name = itm->name;
    }

    auto result = inventory_->deposit_item(owner_id, data.inventory_slot);
    if (result != inventory::inventory_result::success) {
        send_error(conn_id, msg.seq, "deposit_failed", "Failed to deposit item");
        return;
    }

    auto* conn = ws_server_->get_connection(conn_id);
    if (conn) {
        conn->send(network::make_bank_deposit_response(msg.seq, true, item_name));
    }

    LOG_DEBUG(bridge, "Player {} deposited '{}'", check.plr->id.value, item_name);
}

void game_handlers::handle_bank_withdraw(connection_id conn_id, const network::json_message& msg) {
    auto data_result = network::bank_withdraw_request_data::from_json(msg.data);
    if (data_result.is_err()) {
        send_error(conn_id, msg.seq, "invalid_request", data_result.error());
        return;
    }
    auto& data = data_result.value();

    auto check = validate_npc_interaction(conn_id, msg.seq, data.npc_entity_id);
    if (!check.valid) return;

    if (check.target_npc->category != npc::npc_category::banker &&
        check.target_npc->category != npc::npc_category::warehouse) {
        send_error(conn_id, msg.seq, "not_a_bank", "This NPC is not a banker");
        return;
    }

    if (!inventory_ || !item_) {
        send_error(conn_id, msg.seq, "internal_error", "Inventory system unavailable");
        return;
    }

    auto owner_id = entity_id(check.plr->id.value);

    // Get item name before withdraw (for response)
    auto* bank = inventory_->get_bank(owner_id);
    if (!bank) {
        send_error(conn_id, msg.seq, "no_bank", "No bank storage found");
        return;
    }

    auto* slot = bank->get_slot(data.bank_slot);
    if (!slot || slot->is_empty()) {
        send_error(conn_id, msg.seq, "empty_slot", "No item in that bank slot");
        return;
    }

    std::string item_name;
    if (auto* itm = item_->get_item(slot->item)) {
        item_name = itm->name;
    }

    auto result = inventory_->withdraw_item(owner_id, data.bank_slot);
    if (result != inventory::inventory_result::success) {
        std::string error_msg = "Failed to withdraw item";
        if (result == inventory::inventory_result::inventory_full) {
            error_msg = "Inventory is full";
        }
        send_error(conn_id, msg.seq, "withdraw_failed", error_msg);
        return;
    }

    auto* conn = ws_server_->get_connection(conn_id);
    if (conn) {
        conn->send(network::make_bank_withdraw_response(msg.seq, true, item_name));
    }

    LOG_DEBUG(bridge, "Player {} withdrew '{}'", check.plr->id.value, item_name);
}

void game_handlers::handle_dialog_choice(connection_id conn_id, const network::json_message& msg) {
    auto data_result = network::dialog_choice_request_data::from_json(msg.data);
    if (data_result.is_err()) {
        send_error(conn_id, msg.seq, "invalid_request", data_result.error());
        return;
    }
    auto& data = data_result.value();

    auto check = validate_npc_interaction(conn_id, msg.seq, data.npc_entity_id);
    if (!check.valid) return;

    if (!dialog_registry_) {
        send_error(conn_id, msg.seq, "internal_error", "Dialog system unavailable");
        return;
    }

    auto* dialog = dialog_registry_->get_dialog(check.target_npc->name);
    if (!dialog) {
        send_error(conn_id, msg.seq, "no_dialog", "NPC has no dialog");
        return;
    }

    auto* node = dialog_registry_->get_node(check.target_npc->name, data.node_id);
    if (!node) {
        send_error(conn_id, msg.seq, "invalid_node", "Dialog node not found");
        return;
    }

    if (data.choice_index < 0 || data.choice_index >= static_cast<int16_t>(node->options.size())) {
        send_error(conn_id, msg.seq, "invalid_choice", "Choice index out of range");
        return;
    }

    auto& chosen = node->options[static_cast<size_t>(data.choice_index)];
    auto* conn = ws_server_->get_connection(conn_id);
    if (!conn) return;

    switch (chosen.action) {
        case npc::dialog_action::goto_node: {
            auto* next = dialog_registry_->get_node(check.target_npc->name, chosen.next_node);
            if (!next) {
                send_error(conn_id, msg.seq, "invalid_node", "Next dialog node not found");
                return;
            }
            std::vector<network::dialog_option_msg> opts;
            for (const auto& opt : next->options) {
                std::string action_str;
                switch (opt.action) {
                    case npc::dialog_action::goto_node: action_str = "goto_node"; break;
                    case npc::dialog_action::close: action_str = "close"; break;
                    case npc::dialog_action::open_shop: action_str = "open_shop"; break;
                    case npc::dialog_action::open_bank: action_str = "open_bank"; break;
                    case npc::dialog_action::open_quests: action_str = "open_quests"; break;
                    case npc::dialog_action::offer_citizenship: action_str = "offer_citizenship"; break;
                    case npc::dialog_action::select_crusade_job: action_str = "select_crusade_job"; break;
                    case npc::dialog_action::claim_rewards: action_str = "claim_rewards"; break;
                    case npc::dialog_action::open_manufacturing: action_str = "open_manufacturing"; break;
                    case npc::dialog_action::open_alchemy: action_str = "open_alchemy"; break;
                }
                opts.push_back({opt.label, action_str, opt.next_node});
            }
            conn->send(network::make_dialog_choice_response(
                msg.seq, true, "goto_node", next->id, next->text, opts));
            break;
        }

        case npc::dialog_action::close:
            conn->send(network::make_dialog_choice_response(msg.seq, true, "close"));
            break;

        case npc::dialog_action::open_shop:
            // Re-trigger interact to open shop
            conn->send(network::make_dialog_choice_response(msg.seq, true, "open_shop"));
            break;

        case npc::dialog_action::open_bank:
            conn->send(network::make_dialog_choice_response(msg.seq, true, "open_bank"));
            break;

        case npc::dialog_action::open_manufacturing: {
            // Send manufacturing recipe list directly
            conn->send(network::make_dialog_choice_response(msg.seq, true, "open_manufacturing"));
            if (manufacturing_) {
                auto recipes = manufacturing_->get_available_recipes(entity_id{check.plr->id.value});
                nlohmann::json recipe_list = nlohmann::json::array();
                for (const auto* recipe : recipes) {
                    nlohmann::json r;
                    r["id"] = recipe->id;
                    r["name"] = recipe->result;
                    r["skill_req"] = recipe->skill_req;
                    r["success_rate"] = recipe->success_rate;
                    nlohmann::json ings = nlohmann::json::array();
                    for (const auto& ing : recipe->ingredients) {
                        ings.push_back({{"item_id", ing.item_id}, {"count", ing.count}});
                    }
                    r["ingredients"] = ings;
                    recipe_list.push_back(r);
                }
                conn->send(network::make_manufacture_list_response(0, recipe_list));
            }
            break;
        }

        case npc::dialog_action::open_alchemy: {
            conn->send(network::make_dialog_choice_response(msg.seq, true, "open_alchemy"));
            if (alchemy_) {
                auto recipes = alchemy_->get_available_recipes(entity_id{check.plr->id.value});
                nlohmann::json recipe_list = nlohmann::json::array();
                for (const auto* recipe : recipes) {
                    nlohmann::json r;
                    r["id"] = recipe->id;
                    r["name"] = recipe->result;
                    r["skill_limit"] = recipe->skill_limit;
                    r["difficulty"] = recipe->difficulty;
                    nlohmann::json ings = nlohmann::json::array();
                    for (const auto& ing : recipe->ingredients) {
                        ings.push_back({{"item_id", ing.item_id}, {"count", ing.count}});
                    }
                    r["ingredients"] = ings;
                    recipe_list.push_back(r);
                }
                conn->send(network::make_alchemy_list_response(0, recipe_list));
            }
            break;
        }

        case npc::dialog_action::open_quests:
        case npc::dialog_action::offer_citizenship:
        case npc::dialog_action::select_crusade_job:
        case npc::dialog_action::claim_rewards: {
            // Stub actions - infrastructure is in place for when backend systems are ready
            std::string action_name;
            switch (chosen.action) {
                case npc::dialog_action::open_quests: action_name = "open_quests"; break;
                case npc::dialog_action::offer_citizenship: action_name = "offer_citizenship"; break;
                case npc::dialog_action::select_crusade_job: action_name = "select_crusade_job"; break;
                case npc::dialog_action::claim_rewards: action_name = "claim_rewards"; break;
                default: action_name = "unknown"; break;
            }
            conn->send(network::make_dialog_choice_response(
                msg.seq, true, "not_implemented", "", "This feature is not yet available."));
            LOG_DEBUG(bridge, "Player {} triggered stub dialog action '{}'",
                check.plr->id.value, action_name);
            break;
        }
    }
}

void game_handlers::broadcast_position_update(player_id moved_player,
                                               int16_t x, int16_t y, int16_t direction,
                                               bool is_running,
                                               std::optional<int16_t> dest_x,
                                               std::optional<int16_t> dest_y)
{
    if (!players_ || !ws_server_) return;

    auto* player = players_->get_player(moved_player);
    if (!player) return;

    // Get players who can see this movement
    auto nearby = players_->get_players_who_can_see(player->current_map, player->pos);

    auto update_msg = network::make_player_position_update(
        player->ecs_entity.id, x, y, direction, is_running, dest_x, dest_y);

    for (auto other_id : nearby) {
        if (other_id == moved_player) continue;

        auto* other = players_->get_player(other_id);
        if (!other || other->connection.value == 0) continue;

        auto* other_conn = ws_server_->get_connection(other->connection);
        if (other_conn && other_conn->is_open()) {
            other_conn->send(update_msg);
        }
    }
}

void game_handlers::update_entity_visibility(player_id moved_player,
                                              const world::position& old_pos,
                                              const world::position& new_pos)
{
    if (!players_ || !ws_server_) return;

    auto* player = players_->get_player(moved_player);
    if (!player) return;

    // Get all players who could possibly be affected (use max radius as coarse filter)
    constexpr int coarse_radius = network::max_visibility_radius + 5;
    auto old_nearby = players_->get_players_in_range(moved_player, coarse_radius);

    // For each player in the expanded range, check visibility changes
    for (auto other_id : old_nearby) {
        if (other_id == moved_player) continue;

        auto* other = players_->get_player(other_id);
        if (!other || other->connection.value == 0) continue;

        // Check if other player could see the moved player at old/new positions
        // Uses other's rectangular visibility (sees_all always sees everything)
        auto orx = other->visibility_radius_x;
        auto ory = other->visibility_radius_y;
        bool was_visible = other->sees_all
            || (std::abs(old_pos.x - other->pos.x) <= orx && std::abs(old_pos.y - other->pos.y) <= ory);
        bool is_visible = other->sees_all
            || (std::abs(new_pos.x - other->pos.x) <= orx && std::abs(new_pos.y - other->pos.y) <= ory);

        auto* other_conn = ws_server_->get_connection(other->connection);
        if (!other_conn || !other_conn->is_open()) continue;

        if (!was_visible && is_visible) {
            // Player just came into view - send entity_spawn to other
            auto spawn_msg = network::make_entity_spawn(0, network::visible_entity_msg{
                .entity_id = player->ecs_entity.id,
                .type = "player",
                .name = player->name,
                .x = new_pos.x,
                .y = new_pos.y,
                .hp_percent = static_cast<int16_t>(player->hp_percent() * 100),
                .direction = static_cast<int16_t>(player->facing)
            });
            other_conn->send(spawn_msg);

            // Also send the other player info to the moving player
            auto* my_conn = ws_server_->get_connection(player->connection);
            if (my_conn && my_conn->is_open()) {
                auto other_spawn_msg = network::make_entity_spawn(0, network::visible_entity_msg{
                    .entity_id = other->ecs_entity.id,
                    .type = "player",
                    .name = other->name,
                    .x = other->pos.x,
                    .y = other->pos.y,
                    .hp_percent = static_cast<int16_t>(other->hp_percent() * 100),
                    .direction = static_cast<int16_t>(other->facing)
                });
                my_conn->send(other_spawn_msg);
            }
        }
        else if (was_visible && !is_visible) {
            // Player just left view - send entity_despawn to other
            auto despawn_msg = network::make_entity_despawn(0, player->ecs_entity.id);
            other_conn->send(despawn_msg);

            // Also despawn the other player from the moving player's view
            auto* my_conn = ws_server_->get_connection(player->connection);
            if (my_conn && my_conn->is_open()) {
                auto other_despawn_msg = network::make_entity_despawn(0, other->ecs_entity.id);
                my_conn->send(other_despawn_msg);
            }
        }
    }
}

void game_handlers::send_error(connection_id conn_id, uint32_t seq,
                                std::string_view error_code, std::string_view message)
{
    if (!ws_server_) return;

    auto* conn = ws_server_->get_connection(conn_id);
    if (!conn) return;

    auto response = network::make_error_response(seq, error_code, message);
    conn->send(response);
}

auto game_handlers::get_connection(connection_id conn_id) -> network::ws_connection* {
    if (!ws_server_) return nullptr;
    return ws_server_->get_connection(conn_id);
}

auto game_handlers::require_in_game(connection_id conn_id, uint32_t seq)
    -> network::ws_connection*
{
    auto* conn = get_connection(conn_id);
    if (!conn) {
        LOG_WARN(bridge, "Message from unknown connection {}", conn_id.value);
        return nullptr;
    }

    if (conn->state() != network::ws_connection_state::in_game) {
        LOG_WARN(bridge, "In-game request from non-in-game connection {}", conn_id.value);
        send_error(conn_id, seq, "not_in_game", "You must be in-game to perform this action");
        return nullptr;
    }

    return conn;
}

// ========== Chat Handling ==========

namespace {

// Parse chat channel from prefix or explicit channel name
auto parse_chat_channel(std::string_view content, const std::optional<std::string>& explicit_channel)
    -> std::pair<social::chat_channel, std::string>
{
    // If explicit channel provided, use that
    if (explicit_channel.has_value()) {
        const auto& ch = *explicit_channel;
        if (ch == "local") return {social::chat_channel::local, std::string(content)};
        if (ch == "shout") return {social::chat_channel::shout, std::string(content)};
        if (ch == "guild") return {social::chat_channel::guild, std::string(content)};
        if (ch == "party") return {social::chat_channel::party, std::string(content)};
        if (ch == "whisper") return {social::chat_channel::whisper, std::string(content)};
        if (ch == "global") return {social::chat_channel::global, std::string(content)};
        if (ch == "trade") return {social::chat_channel::trade, std::string(content)};
        if (ch == "faction") return {social::chat_channel::faction, std::string(content)};
        // Default to local for unknown channels
        return {social::chat_channel::local, std::string(content)};
    }

    // Check for prefix-based channel
    if (!content.empty()) {
        char prefix = content[0];
        std::string msg_content = std::string(content.substr(1));

        switch (prefix) {
            case '!':  // Shout - server-wide
                return {social::chat_channel::shout, msg_content};
            case '@':  // Guild
                return {social::chat_channel::guild, msg_content};
            case '$':  // Party
                return {social::chat_channel::party, msg_content};
            case '#':  // Say override (local) during whisper mode
                return {social::chat_channel::local, msg_content};
            case '%':  // Trade channel
                return {social::chat_channel::trade, msg_content};
            case '~':  // Faction chat
                return {social::chat_channel::faction, msg_content};
            case '^':  // GM chat
                return {social::chat_channel::gm, msg_content};
            default:
                break;
        }
    }

    // Default to local chat
    return {social::chat_channel::local, std::string(content)};
}

auto channel_to_string(social::chat_channel channel) -> std::string {
    switch (channel) {
        case social::chat_channel::local: return "local";
        case social::chat_channel::global: return "global";
        case social::chat_channel::guild: return "guild";
        case social::chat_channel::party: return "party";
        case social::chat_channel::whisper: return "whisper";
        case social::chat_channel::trade: return "trade";
        case social::chat_channel::shout: return "shout";
        case social::chat_channel::alliance: return "alliance";
        case social::chat_channel::faction: return "faction";
        case social::chat_channel::gm: return "gm";
        case social::chat_channel::system: return "system";
        default: return "unknown";
    }
}

auto format_timestamp(std::chrono::system_clock::time_point tp) -> std::string {
    auto time_t = std::chrono::system_clock::to_time_t(tp);
    std::tm tm_buf{};
#ifdef _WIN32
    gmtime_s(&tm_buf, &time_t);
#else
    gmtime_r(&time_t, &tm_buf);
#endif
    std::ostringstream oss;
    oss << std::put_time(&tm_buf, "%Y-%m-%dT%H:%M:%SZ");
    return oss.str();
}

auto filter_result_to_string(social::filter_result result) -> std::string_view {
    switch (result) {
        case social::filter_result::allowed: return "allowed";
        case social::filter_result::censored: return "censored";
        case social::filter_result::blocked: return "blocked";
        case social::filter_result::rate_limited: return "rate_limited";
        default: return "unknown";
    }
}

}  // namespace

void game_handlers::handle_chat_message(connection_id conn_id, const network::json_message& msg) {
    auto* conn = require_in_game(conn_id, msg.seq);
    if (!conn) return;

    if (!social_ || !players_) {
        send_error(conn_id, msg.seq, "internal_error", "Chat system unavailable");
        return;
    }

    // Parse request
    auto data_result = network::chat_message_request_data::from_json(msg.data);
    if (data_result.is_err()) {
        send_error(conn_id, msg.seq, "invalid_request", data_result.error());
        return;
    }

    auto& data = data_result.value();
    auto pid = conn->player();

    auto* player = players_->get_player(pid);
    if (!player) {
        send_error(conn_id, msg.seq, "invalid_player", "Player not found");
        return;
    }

    // Parse channel and extract message content
    auto [channel, content] = parse_chat_channel(data.content, data.channel);

    // Validate content is not empty after prefix removal
    if (content.empty()) {
        conn->send(network::make_chat_message_response(msg.seq, false, "empty_message"));
        return;
    }

    // Intercept /commands from chat and route to command handler
    if (!content.empty() && content[0] == '/') {
        auto parsed = admin::command_parser::parse(content, '/');
        if (parsed.valid) {
            // Build a synthetic command_request message and route it
            network::json_message cmd_msg;
            cmd_msg.type = network::json_message_type::command_request;
            cmd_msg.seq = msg.seq;
            cmd_msg.data = nlohmann::json{
                {"command", parsed.command_name},
                {"args", parsed.raw_args}
            };
            handle_command(conn_id, cmd_msg);
            return;
        }
    }

    // Handle based on channel type
    social::filter_result result;

    switch (channel) {
        case social::chat_channel::local:
            result = social_->send_local_chat(pid, content,
                player->current_map, player->pos.x, player->pos.y);
            break;

        case social::chat_channel::shout:
            // Shout is server-wide
            result = social_->send_global_chat(pid, content);
            break;

        case social::chat_channel::guild:
            result = social_->send_guild_chat(pid, content);
            break;

        case social::chat_channel::party:
            result = social_->send_party_chat(pid, content);
            break;

        case social::chat_channel::whisper: {
            // Need recipient for whisper
            if (!data.recipient_name.has_value() || data.recipient_name->empty()) {
                conn->send(network::make_chat_message_response(msg.seq, false, "no_recipient"));
                return;
            }

            // Find recipient by name
            auto* recipient = players_->get_player_by_name(*data.recipient_name);
            if (!recipient) {
                conn->send(network::make_chat_message_response(msg.seq, false, "recipient_not_found"));
                return;
            }

            result = social_->send_whisper(pid, recipient->id, content);
            break;
        }

        case social::chat_channel::global:
            result = social_->send_global_chat(pid, content);
            break;

        case social::chat_channel::trade:
            // Trade channel is global for now
            result = social_->send_global_chat(pid, content);
            break;

        case social::chat_channel::gm:
            if (!player->is_gm()) {
                conn->send(network::make_chat_message_response(msg.seq, false, "not_authorized"));
                return;
            }
            result = social_->send_global_chat(pid, content);
            break;

        default:
            result = social_->send_local_chat(pid, content,
                player->current_map, player->pos.x, player->pos.y);
            break;
    }

    // Send response to sender
    if (result == social::filter_result::allowed || result == social::filter_result::censored) {
        conn->send(network::make_chat_message_response(msg.seq, true));
    } else {
        conn->send(network::make_chat_message_response(msg.seq, false,
            filter_result_to_string(result)));
    }

    LOG_DEBUG(bridge, "Player {} sent {} chat: {}",
        pid.value, channel_to_string(channel), content.substr(0, 50));
}

void game_handlers::handle_command(connection_id conn_id, const network::json_message& msg) {
    auto* conn = require_in_game(conn_id, msg.seq);
    if (!conn) return;

    if (!players_) {
        send_error(conn_id, msg.seq, "internal_error", "Player system unavailable");
        return;
    }

    // Parse request
    auto data_result = network::command_request_data::from_json(msg.data);
    if (data_result.is_err()) {
        send_error(conn_id, msg.seq, "invalid_request", data_result.error());
        return;
    }

    auto& data = data_result.value();
    auto pid = conn->player();

    auto* player = players_->get_player(pid);
    if (!player) {
        send_error(conn_id, msg.seq, "invalid_player", "Player not found");
        return;
    }

    LOG_DEBUG(bridge, "Player {} command: {} args={}", pid.value, data.command, data.args.size());

    // Simple built-in commands (available to all players)
    if (data.command == "online") {
        // Return online player count
        auto count = players_->active_player_count();
        conn->send(network::make_command_response(msg.seq, true, data.command,
            std::to_string(count) + " players online",
            nlohmann::json{{"count", count}}));
        return;
    }

    if (data.command == "time") {
        // Return server time
        auto now = std::chrono::system_clock::now();
        conn->send(network::make_command_response(msg.seq, true, data.command,
            format_timestamp(now),
            nlohmann::json{{"timestamp", format_timestamp(now)}}));
        return;
    }

    if (data.command == "pos" || data.command == "position") {
        // Return player position
        std::string map_name = "unknown";
        if (world_) {
            auto* current_map = world_->get_map(player->current_map);
            if (current_map) {
                map_name = std::string(current_map->name());
            }
        }
        conn->send(network::make_command_response(msg.seq, true, data.command,
            "Position: " + map_name + " (" + std::to_string(player->pos.x) + ", " + std::to_string(player->pos.y) + ")",
            nlohmann::json{
                {"x", player->pos.x},
                {"y", player->pos.y},
                {"map", player->current_map.value},
                {"map_name", map_name}
            }));
        return;
    }

    // Route to admin system for admin commands
    if (admin_) {
        // Build command string: /<command> [args...]
        std::string cmd_string = "/" + data.command;
        for (const auto& arg : data.args) {
            cmd_string += " " + arg;
        }

        auto result = admin_->execute(pid, cmd_string);

        // Send response
        conn->send(network::make_command_response(msg.seq, result.success, data.command,
            result.message));
        return;
    }

    // Unknown command (no admin system available)
    conn->send(network::make_command_response(msg.seq, false, data.command,
        "Unknown command: " + data.command));
}

void game_handlers::on_chat_message(const social::chat_message_event& event) {
    if (!ws_server_ || !players_) return;

    const auto& msg = event.message;

    // Build broadcast data
    network::chat_message_broadcast_data broadcast;
    broadcast.channel = channel_to_string(msg.channel);
    broadcast.sender_id = msg.sender.value;
    broadcast.sender_name = msg.sender_name;
    broadcast.content = msg.content;
    broadcast.timestamp = format_timestamp(msg.timestamp);

    // Add flags
    if (social::has_flag(msg.flags, social::chat_flags::emote)) {
        broadcast.flags.push_back("emote");
    }
    if (social::has_flag(msg.flags, social::chat_flags::censored)) {
        broadcast.flags.push_back("censored");
    }
    if (social::has_flag(msg.flags, social::chat_flags::system)) {
        broadcast.flags.push_back("system");
    }
    if (social::has_flag(msg.flags, social::chat_flags::gm)) {
        broadcast.flags.push_back("gm");
    }

    // Route based on channel
    switch (msg.channel) {
        case social::chat_channel::local:
            // Send to nearby players
            send_chat_to_nearby(msg.sender, 15, broadcast);  // 15 tile range
            break;

        case social::chat_channel::shout:
        case social::chat_channel::global: {
            // Send to all online players
            auto all_players = players_->get_all_players();
            for (auto pid : all_players) {
                send_chat_to_player(pid, broadcast);
            }
            break;
        }

        case social::chat_channel::guild: {
            // Send to guild members
            if (!social_) break;

            auto guild_id = social_->get_player_guild(msg.sender);
            if (!guild_id.is_valid()) break;

            auto* guild = social_->get_guild(guild_id);
            if (!guild) break;

            for (const auto& member : guild->members) {
                send_chat_to_player(member.player, broadcast);
            }
            break;
        }

        case social::chat_channel::party: {
            // Send to party members
            if (!social_) break;

            auto party_id = social_->get_player_party(msg.sender);
            if (!party_id.is_valid()) break;

            auto* party = social_->get_party(party_id);
            if (!party) break;

            for (const auto& member : party->members) {
                send_chat_to_player(member.player, broadcast);
            }
            break;
        }

        case social::chat_channel::whisper: {
            // Send to sender (confirmation) and recipient
            broadcast.recipient_name = msg.recipient_name;
            send_chat_to_player(msg.sender, broadcast);  // Echo to sender
            send_chat_to_player(msg.recipient, broadcast);  // Send to recipient
            break;
        }

        case social::chat_channel::system: {
            // System messages to specific recipient
            send_chat_to_player(msg.recipient, broadcast);
            break;
        }

        case social::chat_channel::trade:
        case social::chat_channel::faction:
        case social::chat_channel::alliance: {
            // Broadcast to all for now
            auto all_players = players_->get_all_players();
            for (auto pid : all_players) {
                send_chat_to_player(pid, broadcast);
            }
            break;
        }

        case social::chat_channel::gm: {
            // Only deliver to players with GM permissions
            auto all_players = players_->get_all_players();
            for (auto pid : all_players) {
                auto* p = players_->get_player(pid);
                if (p && p->is_gm()) {
                    send_chat_to_player(pid, broadcast);
                }
            }
            break;
        }

        default:
            LOG_WARN(bridge, "Unknown chat channel: {}", static_cast<int>(msg.channel));
            break;
    }
}

void game_handlers::send_chat_to_player(player_id target,
                                         const network::chat_message_broadcast_data& data) {
    if (!ws_server_ || !players_) return;

    auto* player = players_->get_player(target);
    if (!player || player->connection.value == 0) return;

    auto* conn = ws_server_->get_connection(player->connection);
    if (!conn || !conn->is_open()) return;

    // Check if player has this channel enabled
    if (social_) {
        auto* settings = social_->get_chat_settings(target);
        if (settings) {
            // Convert string channel back to enum for settings check
            social::chat_channel ch = social::chat_channel::local;
            if (data.channel == "local") ch = social::chat_channel::local;
            else if (data.channel == "global") ch = social::chat_channel::global;
            else if (data.channel == "guild") ch = social::chat_channel::guild;
            else if (data.channel == "party") ch = social::chat_channel::party;
            else if (data.channel == "whisper") ch = social::chat_channel::whisper;
            else if (data.channel == "trade") ch = social::chat_channel::trade;
            else if (data.channel == "shout") ch = social::chat_channel::shout;
            else if (data.channel == "system") ch = social::chat_channel::system;

            if (!settings->is_channel_enabled(ch) && ch != social::chat_channel::system) {
                return;  // Player has this channel disabled
            }

            // Check if sender is blocked
            if (settings->is_player_blocked(player_id{data.sender_id})) {
                return;  // Player has blocked the sender
            }
        }
    }

    conn->send(network::make_chat_message_broadcast(data));
}

void game_handlers::send_chat_to_nearby(player_id sender, int16_t range,
                                         const network::chat_message_broadcast_data& data) {
    if (!players_) return;

    auto nearby = players_->get_players_in_range(sender, range);
    for (auto pid : nearby) {
        send_chat_to_player(pid, data);
    }
}

// ========== View Range Handling ==========

void game_handlers::handle_set_view_range(connection_id conn_id, const network::json_message& msg) {
    auto* conn = require_in_game(conn_id, msg.seq);
    if (!conn) return;

    if (!players_) {
        send_error(conn_id, msg.seq, "internal_error", "Player system unavailable");
        return;
    }

    auto data_result = network::set_view_range_request_data::from_json(msg.data);
    if (data_result.is_err()) {
        send_error(conn_id, msg.seq, "invalid_request", data_result.error());
        return;
    }

    auto& data = data_result.value();
    auto pid = conn->player();

    auto* player = players_->get_player(pid);
    if (!player) {
        send_error(conn_id, msg.seq, "invalid_player", "Player not found");
        return;
    }

    // GM override prevents client from recalculating radii (e.g. after /setviewrange)
    if (player->gm_view_override) {
        LOG_DEBUG(bridge, "Player {} set_view_range ignored (GM override active)", pid.value);
        return;
    }

    auto radii = network::calculate_visibility_radius(data.screen_width, data.screen_height);
    player->visibility_radius_x = radii.x;
    player->visibility_radius_y = radii.y;

    LOG_DEBUG(bridge, "Player {} updated visibility radii to {}x{} (screen {}x{})",
        pid.value, radii.x, radii.y, data.screen_width, data.screen_height);
}

// ========== Teleportation Handling ==========

void game_handlers::execute_player_teleport(player_id pid, connection_id conn_id, uint32_t seq,
                                             const std::string& dest_map,
                                             const world::position& dest_pos,
                                             world::direction dest_dir)
{
    if (!players_ || !ws_server_ || !world_) {
        send_error(conn_id, seq, "internal_error", "System unavailable");
        return;
    }

    auto* player = players_->get_player(pid);
    if (!player) {
        send_error(conn_id, seq, "invalid_player", "Player not found");
        return;
    }

    // Store old position for despawn notifications
    auto old_map_id = player->current_map;
    auto old_pos = player->pos;

    // Execute the teleport
    auto teleport_result = players_->execute_teleport(pid, dest_map, dest_pos, dest_dir);
    if (!teleport_result.success) {
        send_error(conn_id, seq, "teleport_failed", teleport_result.error);
        return;
    }

    // Check if this is a cross-map teleport
    bool is_cross_map = (old_map_id != teleport_result.new_map);

    // Despawn from players who could see OLD position
    auto old_viewers = players_->get_players_who_can_see(old_map_id, old_pos);
    auto despawn_msg = network::make_entity_despawn(0, pid.value);
    for (auto other_id : old_viewers) {
        if (other_id == pid) continue;

        auto* other = players_->get_player(other_id);
        if (!other || other->connection.value == 0) continue;

        auto* other_conn = ws_server_->get_connection(other->connection);
        if (other_conn && other_conn->is_open()) {
            other_conn->send(despawn_msg);
        }
    }

    // Build visible entities at destination using teleporting player's visibility
    auto visible_entities = build_visible_entities_at(teleport_result.new_map, dest_pos,
                                                       player->visibility_radius_x,
                                                       player->visibility_radius_y);

    // Build and send player_teleport message
    network::player_teleport_msg teleport_msg{
        .dest_map = dest_map,
        .dest_x = dest_pos.x,
        .dest_y = dest_pos.y,
        .dest_dir = static_cast<int16_t>(dest_dir),
        .entities = visible_entities
    };

    auto* conn = ws_server_->get_connection(conn_id);
    if (conn && conn->is_open()) {
        conn->send(network::make_player_teleport(seq, teleport_msg));

        // If cross-map, also send map_teleporters for the new map
        if (is_cross_map) {
            auto* new_map = world_->get_map(teleport_result.new_map);
            if (new_map) {
                send_map_teleporters(conn_id, *new_map);
            }
        }

        // Send visible ground items at destination
        send_visible_ground_items(conn_id, teleport_result.new_map, dest_pos,
                                   player->visibility_radius_x,
                                   player->visibility_radius_y);

        // Send environment update for destination map
        if (scheduler_) {
            auto* dest_map_ptr = world_->get_map(teleport_result.new_map);
            if (dest_map_ptr) {
                auto& clock = scheduler_->game_time();
                network::environment_update_data env{
                    .hour = static_cast<uint8_t>(clock.hour()),
                    .minute = static_cast<uint8_t>(clock.minute()),
                    .is_day = clock.is_day(),
                    .weather = static_cast<uint8_t>(dest_map_ptr->weather())
                };
                if (dest_map_ptr->config().is_fixed_day_mode) {
                    env.is_day = true;
                    env.weather = 0;
                }
                conn->send(network::make_environment_update(env));
            }
        }
    }

    // Spawn to players who can see NEW position
    auto new_viewers = players_->get_players_who_can_see(teleport_result.new_map, dest_pos);

    network::visible_entity_msg spawn_entity{
        .entity_id = player->ecs_entity.id,
        .type = "player",
        .name = player->name,
        .x = dest_pos.x,
        .y = dest_pos.y,
        .hp_percent = static_cast<int16_t>(player->hp_percent() * 100),
        .direction = static_cast<int16_t>(dest_dir)
    };
    auto spawn_msg = network::make_entity_spawn(0, spawn_entity);

    for (auto other_id : new_viewers) {
        if (other_id == pid) continue;

        auto* other = players_->get_player(other_id);
        if (!other || other->connection.value == 0) continue;

        auto* other_conn = ws_server_->get_connection(other->connection);
        if (other_conn && other_conn->is_open()) {
            other_conn->send(spawn_msg);
        }
    }

    LOG_INFO(bridge, "Player {} teleported to {} ({}, {}), {} entities visible",
        pid.value, dest_map, dest_pos.x, dest_pos.y, visible_entities.size());
}

void game_handlers::send_map_teleporters(connection_id conn_id, const world::map& map) {
    if (!ws_server_) return;

    auto* conn = ws_server_->get_connection(conn_id);
    if (!conn || !conn->is_open()) return;

    const auto& teleports = map.get_all_teleports();

    network::map_teleporters_msg teleporters_msg;
    teleporters_msg.map_name = std::string(map.name());

    for (const auto& [pos, dest] : teleports) {
        network::teleporter_info_msg tp_info{
            .id = (static_cast<uint32_t>(pos.x) << 16) | static_cast<uint32_t>(static_cast<uint16_t>(pos.y)),
            .x = pos.x,
            .y = pos.y,
            .dest_map = dest.dest_map,
            .dest_x = dest.dest_x,
            .dest_y = dest.dest_y,
            .dest_dir = static_cast<int16_t>(dest.dest_dir)
        };
        teleporters_msg.teleporters.push_back(tp_info);
    }

    conn->send(network::make_map_teleporters(teleporters_msg));

    LOG_DEBUG(bridge, "Sent {} teleporters for map {} to connection {}",
        teleporters_msg.teleporters.size(), map.name(), conn_id.value);
}

void game_handlers::broadcast_teleporter_update(map_id map, const std::string& action,
                                                 const world::position& pos,
                                                 const world::teleport_dest* dest)
{
    if (!players_ || !ws_server_ || !world_) return;

    auto* m = world_->get_map(map);
    if (!m) return;

    network::teleporter_update_msg update_msg;
    update_msg.action = action;
    update_msg.map_name = std::string(m->name());
    update_msg.teleporter.id = (static_cast<uint32_t>(pos.x) << 16) |
                                static_cast<uint32_t>(static_cast<uint16_t>(pos.y));
    update_msg.teleporter.x = pos.x;
    update_msg.teleporter.y = pos.y;

    if (dest) {
        update_msg.teleporter.dest_map = dest->dest_map;
        update_msg.teleporter.dest_x = dest->dest_x;
        update_msg.teleporter.dest_y = dest->dest_y;
        update_msg.teleporter.dest_dir = static_cast<int16_t>(dest->dest_dir);
    }

    auto msg = network::make_teleporter_update(update_msg);

    // Send to all players on this map
    players_->for_each_player([&](player_id pid, const player::player& p) {
        if (p.current_map != map) return;
        if (p.connection.value == 0) return;

        auto* conn = ws_server_->get_connection(p.connection);
        if (conn && conn->is_open()) {
            conn->send(msg);
        }
    });

    LOG_DEBUG(bridge, "Broadcast teleporter {} at ({},{}) on map {}",
        action, pos.x, pos.y, m->name());
}

auto game_handlers::build_visible_entities_at(map_id map, const world::position& pos,
                                               int visibility_radius_x,
                                               int visibility_radius_y)
    -> std::vector<network::visible_entity_msg>
{
    std::vector<network::visible_entity_msg> entities;

    if (!players_ || !world_) return entities;

    auto* m = world_->get_map(map);
    if (!m) return entities;

    // Use max of X/Y as coarse filter for spatial index, then rect-filter below
    auto coarse_radius = std::max(visibility_radius_x, visibility_radius_y);
    auto nearby_entities = m->get_entities_in_range(pos, coarse_radius);

    for (auto eid : nearby_entities) {
        // Spatial index stores ecs_entity.index() - resolve via entity lookup
        auto* p = players_->get_player_by_entity(entity::entity{eid.value});
        if (p) {
            // Rect-filter: skip entities outside the rectangular viewport
            if (std::abs(p->pos.x - pos.x) > visibility_radius_x
                || std::abs(p->pos.y - pos.y) > visibility_radius_y) continue;

            entities.push_back(network::visible_entity_msg{
                .entity_id = p->ecs_entity.id,
                .type = "player",
                .name = p->name,
                .x = p->pos.x,
                .y = p->pos.y,
                .hp_percent = static_cast<int16_t>(p->hp_percent() * 100),
                .direction = static_cast<int16_t>(p->facing)
            });
        }
        // TODO: Also include NPCs from npc_system when available
    }

    return entities;
}

// ========== Combat Event Callbacks ==========

namespace {

auto damage_type_to_string(combat::damage_type dt) -> std::string_view
{
    switch (dt) {
        case combat::damage_type::physical:  return "physical";
        case combat::damage_type::magic:     return "magic";
        case combat::damage_type::fire:      return "fire";
        case combat::damage_type::ice:       return "ice";
        case combat::damage_type::lightning: return "lightning";
        case combat::damage_type::poison:    return "poison";
        case combat::damage_type::holy:      return "holy";
        case combat::damage_type::dark:      return "dark";
        case combat::damage_type::pure:      return "pure";
        default: return "physical";
    }
}

auto spell_element_to_damage_type_string(magic::spell_element elem) -> std::string_view
{
    switch (elem) {
        case magic::spell_element::fire:      return "fire";
        case magic::spell_element::ice:       return "ice";
        case magic::spell_element::lightning: return "lightning";
        case magic::spell_element::holy:      return "holy";
        case magic::spell_element::dark:      return "dark";
        default: return "magic";
    }
}

}  // namespace

void game_handlers::on_damage_dealt(const combat::damage_event& event) {
    if (!players_ || !ws_server_) return;

    // Only broadcast for player targets for now
    player_id target_pid{event.target.id};
    auto* target = players_->get_player(target_pid);
    if (!target) return;

    // Clear destination - damage interrupts movement (to be fleshed out later)
    // For now, always interrupt on any damage
    if (target->connection.value != 0) {
        auto* conn = ws_server_->get_connection(target->connection);
        if (conn) {
            conn->clear_destination();
        }
    }

    // Broadcast HP update to players who can see the target
    broadcast_hp_update(target_pid, target->hp, target->computed.max_hp);

    // Broadcast combat_effect for visual feedback (floating damage numbers, etc.)
    auto& hr = event.result;
    std::string effect_type;
    if (hr.is_miss()) {
        effect_type = "miss";
    } else if (hr.is_dodged()) {
        effect_type = "dodge";
    } else if (hr.is_blocked()) {
        effect_type = "block";
    } else if (hr.is_hit()) {
        effect_type = "damage";
    } else {
        return;  // No visual effect for this result
    }

    // Resolve source entity_id for client
    uint32_t source_eid = event.source.id;
    if (auto* src = players_->get_player(player_id{event.source.id})) {
        source_eid = src->ecs_entity.id;
    }

    network::combat_effect_data effect{
        .source_id = source_eid,
        .target_id = target->ecs_entity.id,
        .effect_type = std::move(effect_type),
        .value = hr.final_damage,
        .damage_type = std::string(damage_type_to_string(hr.type)),
        .spell_id = 0,
        .is_critical = hr.is_critical(),
        .target_x = target->pos.x,
        .target_y = target->pos.y
    };

    broadcast_combat_effect(target->current_map, target->pos, effect);
}

void game_handlers::on_entity_death(const combat::death_event& event) {
    if (!players_ || !ws_server_) return;

    // Only handle player deaths for now
    player_id victim_pid{event.victim.id};
    auto* victim = players_->get_player(victim_pid);
    if (!victim) return;

    player_id killer_pid{event.killer.id};

    // Broadcast death to nearby players
    broadcast_entity_death(victim_pid, killer_pid);

    // Handle death penalties and respawn
    handle_player_death(victim_pid, event);
}

void game_handlers::handle_player_death(player_id pid, const combat::death_event& event) {
    if (!players_ || !ws_server_ || !world_) return;

    auto* player = players_->get_player(pid);
    if (!player) return;

    // 1. Clear status effects and combat target
    player->status = player::player_status::none;
    player->target = {};

    int64_t xp_lost = 0;
    int32_t pk_points_change = 0;
    int32_t gold_reward = 0;
    std::string killer_name;

    player_id killer_pid{event.killer.id};
    auto* killer = players_->get_player(killer_pid);
    if (killer) {
        killer_name = killer->name;
    }

    // 2. PvP-specific penalties
    if (event.is_pvp && killer) {
        // XP penalty for victim
        int64_t penalty = calculate_death_xp_penalty(player->experience.level);
        xp_lost = player->experience.remove_experience(penalty);

        // If victim is innocent, killer gains PK points
        if (player->pk.is_innocent()) {
            killer->pk.add_kill();
            pk_points_change = 50;
            LOG_INFO(bridge, "Player {} gained PK point for killing innocent {}",
                killer_pid.value, pid.value);
        }

        // If killer is innocent and victim is a PKer, award bounty
        if (killer->pk.is_innocent() && (player->pk.is_criminal() || player->pk.is_murderer())) {
            gold_reward = calculate_pk_bounty_reward(player->experience.level);
            // Cap at max reward gold (from game config default)
            gold_reward = std::min(gold_reward, static_cast<int32_t>(99999999));
            // TODO: Actually add gold to killer's inventory when economy wiring is complete
            LOG_INFO(bridge, "Player {} earned {} gold bounty for killing PKer {}",
                killer_pid.value, gold_reward, pid.value);
        }
    }

    // 3. Determine respawn location
    std::string spawn_map = get_respawn_map_name(player->faction);
    world::position spawn_pos = get_respawn_position(spawn_map);

    // 4. Save player state after applying penalties
    if (save_callback_) {
        save_callback_(pid);
    }

    // 5. Send death info to the dead player
    uint32_t respawn_delay = 5000;  // Default 5s
    if (auto* cfg_sys = subsystems().get<config_system>()) {
        respawn_delay = cfg_sys->game().respawn_delay_ms;
    }

    // Use ecs_entity.id for client-visible killer ID
    uint32_t killer_eid = 0;
    if (killer) {
        killer_eid = killer->ecs_entity.id;
    }

    network::player_death_info_data death_info{
        .killer_id = killer_eid,
        .killer_name = killer_name,
        .is_pvp = event.is_pvp,
        .xp_lost = xp_lost,
        .pk_points_change = pk_points_change,
        .gold_reward = gold_reward,
        .respawn_delay_ms = respawn_delay,
        .respawn_map = spawn_map,
        .respawn_x = spawn_pos.x,
        .respawn_y = spawn_pos.y
    };

    auto* conn = ws_server_->get_connection(player->connection);
    if (conn && conn->is_open()) {
        conn->send(network::make_player_death_info(death_info));
    }

    // 6. Schedule delayed respawn
    if (scheduler_) {
        scheduler_->schedule(duration_ms{respawn_delay},
            [this, pid, spawn_map, spawn_pos]() {
                execute_respawn(pid, spawn_map, spawn_pos);
            });
    } else {
        // Fallback: immediate respawn if scheduler unavailable
        execute_respawn(pid, spawn_map, spawn_pos);
    }

    LOG_INFO(bridge, "Player {} died (pvp={}, xp_lost={}, pk_change={}, bounty={}), respawning at {} ({}, {}) in {}ms",
        pid.value, event.is_pvp, xp_lost, pk_points_change, gold_reward,
        spawn_map, spawn_pos.x, spawn_pos.y, respawn_delay);
}

void game_handlers::execute_respawn(player_id pid, const std::string& map_name,
                                     const world::position& pos)
{
    if (!players_ || !ws_server_ || !combat_) return;

    auto* player = players_->get_player(pid);
    if (!player) return;  // Player disconnected during respawn delay

    // Restore HP/MP to 50%
    player->hp = player->computed.max_hp / 2;
    player->mp = player->computed.max_mp / 2;

    // Set 3-second invulnerability
    entity::entity player_entity{pid.value};
    combat_->set_invulnerable(player_entity, 3000);

    // Execute teleport to spawn
    execute_player_teleport(pid, player->connection, 0, map_name, pos,
                            world::direction::south);
}

auto game_handlers::calculate_death_xp_penalty(uint8_t level) -> int64_t {
    if (level <= 1) return 0;

    // Legacy Helbreath formula: random(1, level/2+1) * 50
    static thread_local std::mt19937 rng{std::random_device{}()};
    int max_roll = level / 2 + 1;
    std::uniform_int_distribution<int> dist(1, max_roll);
    return static_cast<int64_t>(dist(rng)) * 50;
}

auto game_handlers::calculate_pk_bounty_reward(uint8_t level) -> int32_t {
    return static_cast<int32_t>(level) * 3;
}

auto game_handlers::get_respawn_map_name(hb::faction f) -> std::string {
    switch (f) {
        case faction::aresden: return "aresden";
        case faction::elvine: return "elvine";
        default: return "default";
    }
}

auto game_handlers::get_respawn_position(const std::string& map_name) -> world::position {
    if (!world_) return {18, 18};

    auto* m = world_->get_map_by_name(map_name);
    if (!m) {
        // Map not found - try to fall back
        return {18, 18};
    }

    auto pos = m->get_random_initial_point();
    if (pos.has_value()) {
        return *pos;
    }

    // No initial points defined - fallback
    return {18, 18};
}

void game_handlers::broadcast_hp_update(player_id target, int32_t hp, int32_t hp_max) {
    if (!players_ || !ws_server_) return;

    auto* player = players_->get_player(target);
    if (!player) return;

    auto hp_msg = network::make_entity_hp_update(player->ecs_entity.id, hp, hp_max);

    // Broadcast to players who can see this target
    auto viewers = players_->get_players_who_can_see(player->current_map, player->pos);

    for (auto other_id : viewers) {
        auto* other = players_->get_player(other_id);
        if (!other || other->connection.value == 0) continue;

        auto* other_conn = ws_server_->get_connection(other->connection);
        if (other_conn && other_conn->is_open()) {
            other_conn->send(hp_msg);
        }
    }
}

void game_handlers::broadcast_entity_death(player_id victim, player_id killer) {
    if (!players_ || !ws_server_) return;

    auto* victim_player = players_->get_player(victim);
    if (!victim_player) return;

    // Resolve killer's entity_id (may be offline/disconnected)
    uint32_t killer_entity_id = 0;
    auto* killer_player = players_->get_player(killer);
    if (killer_player) {
        killer_entity_id = killer_player->ecs_entity.id;
    }

    auto death_msg = network::make_entity_death(
        victim_player->ecs_entity.id, killer_entity_id,
        victim_player->pos.x, victim_player->pos.y
    );

    // Broadcast to players who can see the victim's position
    auto viewers = players_->get_players_who_can_see(victim_player->current_map, victim_player->pos);

    for (auto other_id : viewers) {
        auto* other = players_->get_player(other_id);
        if (!other || other->connection.value == 0) continue;

        auto* other_conn = ws_server_->get_connection(other->connection);
        if (other_conn && other_conn->is_open()) {
            other_conn->send(death_msg);
        }
    }
}

// ========== Combat Effect Broadcast Methods ==========

void game_handlers::broadcast_combat_effect(map_id map, const world::position& pos,
                                             const network::combat_effect_data& data)
{
    if (!players_ || !ws_server_) return;

    auto msg = network::make_combat_effect(data);
    auto viewers = players_->get_players_who_can_see(map, pos);

    for (auto pid : viewers) {
        auto* p = players_->get_player(pid);
        if (!p || p->connection.value == 0) continue;

        auto* conn = ws_server_->get_connection(p->connection);
        if (conn && conn->is_open()) {
            conn->send(msg);
        }
    }
}

void game_handlers::broadcast_combat_effect_to_faction(map_id map, const world::position& pos,
                                                        hb::faction fac,
                                                        const network::combat_effect_data& data)
{
    if (!players_ || !ws_server_) return;

    auto msg = network::make_combat_effect(data);
    auto viewers = players_->get_players_who_can_see(map, pos);

    for (auto pid : viewers) {
        auto* p = players_->get_player(pid);
        if (!p || p->connection.value == 0) continue;
        if (p->faction != fac) continue;

        auto* conn = ws_server_->get_connection(p->connection);
        if (conn && conn->is_open()) {
            conn->send(msg);
        }
    }
}

void game_handlers::on_spell_cast(entity::entity caster, const magic::spell_template& spell,
                                   const magic::spell_effect_result& result)
{
    if (!players_ || !ws_server_) return;
    if (!result.success) return;  // Failed casts aren't visible

    // Find caster position - could be player or NPC
    auto* caster_player = players_->get_player(player_id{caster.id});
    if (!caster_player) return;  // Only handle player casters for now

    auto caster_map = caster_player->current_map;
    auto caster_pos = caster_player->pos;
    auto caster_eid = caster_player->ecs_entity.id;
    auto dmg_type = std::string(spell_element_to_damage_type_string(spell.element));

    // Determine broadcast based on spell category
    switch (spell.category) {
        case magic::spell_category::attack:
        case magic::spell_category::debuff: {
            for (auto target_ent : result.affected_targets) {
                // Determine target position for the broadcast
                int16_t tx = caster_pos.x;
                int16_t ty = caster_pos.y;
                uint32_t target_eid = target_ent.id;

                // Try to get target position from player or NPC
                if (auto* tp = players_->get_player(player_id{target_ent.id})) {
                    tx = tp->pos.x;
                    ty = tp->pos.y;
                    target_eid = tp->ecs_entity.id;
                } else if (npc_) {
                    if (auto* tn = npc_->get_npc(entity::entity{target_ent.id})) {
                        tx = tn->pos.x;
                        ty = tn->pos.y;
                        target_eid = tn->entity_id.id;
                    }
                }

                auto effect_type = spell.category == magic::spell_category::attack ? "damage" : "debuff";
                auto value = spell.category == magic::spell_category::attack
                    ? result.damage_dealt : spell.effect_value;

                network::combat_effect_data effect{
                    .source_id = caster_eid,
                    .target_id = target_eid,
                    .effect_type = effect_type,
                    .value = value,
                    .damage_type = dmg_type,
                    .spell_id = spell.id.value,
                    .is_critical = false,
                    .target_x = tx,
                    .target_y = ty
                };

                if (spell.category == magic::spell_category::debuff) {
                    broadcast_combat_effect_to_faction(caster_map, world::position{tx, ty},
                                                       caster_player->faction, effect);
                } else {
                    broadcast_combat_effect(caster_map, world::position{tx, ty}, effect);
                }
            }
            break;
        }

        case magic::spell_category::healing: {
            for (auto target_ent : result.affected_targets) {
                int16_t tx = caster_pos.x;
                int16_t ty = caster_pos.y;
                uint32_t target_eid = target_ent.id;

                if (auto* tp = players_->get_player(player_id{target_ent.id})) {
                    tx = tp->pos.x;
                    ty = tp->pos.y;
                    target_eid = tp->ecs_entity.id;
                }

                network::combat_effect_data effect{
                    .source_id = caster_eid,
                    .target_id = target_eid,
                    .effect_type = "heal",
                    .value = result.heal_applied,
                    .damage_type = dmg_type,
                    .spell_id = spell.id.value,
                    .is_critical = false,
                    .target_x = tx,
                    .target_y = ty
                };

                broadcast_combat_effect(caster_map, world::position{tx, ty}, effect);
            }
            break;
        }

        case magic::spell_category::buff: {
            for (auto target_ent : result.affected_targets) {
                int16_t tx = caster_pos.x;
                int16_t ty = caster_pos.y;
                uint32_t target_eid = target_ent.id;

                if (auto* tp = players_->get_player(player_id{target_ent.id})) {
                    tx = tp->pos.x;
                    ty = tp->pos.y;
                    target_eid = tp->ecs_entity.id;
                }

                network::combat_effect_data effect{
                    .source_id = caster_eid,
                    .target_id = target_eid,
                    .effect_type = "buff",
                    .value = spell.effect_value,
                    .damage_type = {},
                    .spell_id = spell.id.value,
                    .is_critical = false,
                    .target_x = tx,
                    .target_y = ty
                };

                broadcast_combat_effect_to_faction(caster_map, world::position{tx, ty},
                                                    caster_player->faction, effect);
            }
            break;
        }

        default:
            break;
    }
}

// ========== NPC Broadcast Methods ==========

void game_handlers::broadcast_npc_spawn(const npc::npc& n) {
    if (!players_ || !ws_server_) return;

    network::npc_spawn_data data{
        .entity_id = n.entity_id.id,
        .template_id = n.template_id.value,
        .name = n.name,
        .x = n.pos.x,
        .y = n.pos.y,
        .direction = static_cast<uint8_t>(n.facing),
        .hp = n.hp,
        .max_hp = n.max_hp,
        .level = n.level
    };

    auto msg = network::make_npc_spawn_message(data);

    // Send to all players who can see this position
    auto players = players_->get_players_who_can_see(n.current_map, n.pos);
    for (auto pid : players) {
        auto* p = players_->get_player(pid);
        if (!p || p->connection.value == 0) continue;

        auto* conn = ws_server_->get_connection(p->connection);
        if (conn && conn->is_open()) {
            conn->send(msg);
        }
    }

    // LOG_DEBUG(bridge, "Broadcast NPC spawn: {} '{}' at ({}, {})",
    //     n.entity_id.id, n.name, n.pos.x, n.pos.y);
}

void game_handlers::broadcast_npc_move(const npc::npc& n) {
    if (!players_ || !ws_server_) return;

    network::npc_move_data data{
        .entity_id = n.entity_id.id,
        .x = n.pos.x,
        .y = n.pos.y,
        .direction = static_cast<uint8_t>(n.facing)
    };

    auto msg = network::make_npc_move_message(data);

    auto players = players_->get_players_who_can_see(n.current_map, n.pos);
    for (auto pid : players) {
        auto* p = players_->get_player(pid);
        if (!p || p->connection.value == 0) continue;

        auto* conn = ws_server_->get_connection(p->connection);
        if (conn && conn->is_open()) {
            conn->send(msg);
        }
    }
}

void game_handlers::broadcast_npc_attack(const npc::npc& n, entity::entity target, int32_t damage) {
    if (!players_ || !ws_server_) return;

    // Resolve target position for projectile visuals
    int16_t tgt_x = 0, tgt_y = 0;
    if (auto target_pid = players_->get_player_id_by_entity(target)) {
        if (auto* tgt = players_->get_player(*target_pid)) {
            tgt_x = tgt->pos.x;
            tgt_y = tgt->pos.y;
        }
    }

    network::npc_attack_data data{
        .attacker_id = n.entity_id.id,
        .target_id = target.id,
        .damage = damage,
        .is_critical = false,  // NPCs don't crit for now
        .is_ranged = n.ai.attack_range > 1,
        .attacker_x = n.pos.x,
        .attacker_y = n.pos.y,
        .target_x = tgt_x,
        .target_y = tgt_y
    };

    auto msg = network::make_npc_attack_message(data);

    auto players = players_->get_players_who_can_see(n.current_map, n.pos);
    for (auto pid : players) {
        auto* p = players_->get_player(pid);
        if (!p || p->connection.value == 0) continue;

        auto* conn = ws_server_->get_connection(p->connection);
        if (conn && conn->is_open()) {
            conn->send(msg);
        }
    }
}

void game_handlers::broadcast_npc_death(const npc::npc& n, entity::entity killer) {
    if (!players_ || !ws_server_) return;

    network::npc_death_data data{
        .entity_id = n.entity_id.id,
        .killer_id = killer.id,
        .x = n.pos.x,
        .y = n.pos.y
    };

    auto msg = network::make_npc_death_message(data);

    auto players = players_->get_players_who_can_see(n.current_map, n.pos);
    for (auto pid : players) {
        auto* p = players_->get_player(pid);
        if (!p || p->connection.value == 0) continue;

        auto* conn = ws_server_->get_connection(p->connection);
        if (conn && conn->is_open()) {
            conn->send(msg);
        }
    }

    // LOG_DEBUG(bridge, "Broadcast NPC death: {} '{}' killed by {}",
    //     n.entity_id.id, n.name, killer.id);
}

void game_handlers::broadcast_npc_hp_update(const npc::npc& n) {
    if (!players_ || !ws_server_) return;

    // Use entity_hp_update message for NPCs too
    auto msg = network::make_entity_hp_update(n.entity_id.id, n.hp, n.max_hp);

    auto players = players_->get_players_who_can_see(n.current_map, n.pos);
    for (auto pid : players) {
        auto* p = players_->get_player(pid);
        if (!p || p->connection.value == 0) continue;

        auto* conn = ws_server_->get_connection(p->connection);
        if (conn && conn->is_open()) {
            conn->send(msg);
        }
    }
}

// ========== Ground Item Broadcast ==========

void game_handlers::broadcast_ground_item_removed(player_id picker, map_id map,
                                                   const world::position& pos, item_id item) {
    if (!players_ || !ws_server_) return;

    auto* picker_player = players_->get_player(picker);
    if (!picker_player) return;

    // Get item name for display
    std::string item_name = "Unknown";
    if (item_) {
        auto* itm = item_->get_item(item);
        if (itm) {
            item_name = itm->name;
        }
    }

    // Build the broadcast data
    network::ground_item_removed_data data{
        .picker_id = picker.value,
        .picker_name = picker_player->name,
        .item_id = item.value,
        .item_name = item_name,
        .x = pos.x,
        .y = pos.y
    };

    auto msg = network::make_ground_item_removed(data);

    // Broadcast to all players who can see this position, EXCEPT the picker
    auto nearby = players_->get_players_who_can_see(picker_player->current_map, pos);

    for (auto other_id : nearby) {
        if (other_id == picker) continue;  // Don't send to the picker (they got the response)

        auto* other = players_->get_player(other_id);
        if (!other || other->connection.value == 0) continue;

        auto* other_conn = ws_server_->get_connection(other->connection);
        if (other_conn && other_conn->is_open()) {
            other_conn->send(msg);
        }
    }

    LOG_DEBUG(bridge, "Broadcast ground item {} removed at ({}, {}) by player {}",
        item.value, pos.x, pos.y, picker.value);
}

void game_handlers::broadcast_ground_item_spawn(map_id map, const world::position& pos, item_id item) {
    if (!players_ || !ws_server_ || !item_) return;

    auto* itm = item_->get_item(item);
    if (!itm) return;

    network::ground_item_spawn_data data{
        .item_id = item.value,
        .template_id = itm->template_id.value,
        .item_name = itm->name,
        .count = itm->count,
        .x = pos.x,
        .y = pos.y
    };

    auto msg = network::make_ground_item_spawn(data);

    auto players = players_->get_players_who_can_see(map, pos);
    for (auto pid : players) {
        auto* p = players_->get_player(pid);
        if (!p || p->connection.value == 0) continue;

        auto* conn = ws_server_->get_connection(p->connection);
        if (conn && conn->is_open()) {
            conn->send(msg);
        }
    }
}

void game_handlers::handle_npc_loot_drop(const npc::npc& n, entity::entity killer) {
    if (!item_ || !world_ || !loot_registry_) return;

    // Copy needed data from the npc reference (only valid during callback)
    auto npc_map = n.current_map;
    auto npc_pos = n.pos;
    auto npc_name = n.name;

    // Generate on_kill loot using config-driven system
    auto drop = npc::generate_kill_loot(*loot_registry_,
        n.sprite_id, n.gold_min, n.gold_max, n.has_owner());

    // Award gold directly to killer
    if (drop.gold > 0 && inventory_) {
        auto killer_entity = entity_id{killer.id};
        inventory_->add_gold(killer_entity, drop.gold);
        LOG_DEBUG(bridge, "NPC '{}' dropped {} gold to killer {}", npc_name, drop.gold, killer.id);
    }

    // Place item drops on ground
    for (auto tmpl_id : drop.items) {
        auto create_result = item_->create_from_template(tmpl_id, 1);
        if (create_result.is_err()) {
            LOG_WARN(bridge, "Failed to create drop item from template {}: {}",
                tmpl_id.value, create_result.error());
            continue;
        }

        auto dropped_item = create_result.value();
        world_->add_ground_item(npc_map, npc_pos, dropped_item);
        broadcast_ground_item_spawn(npc_map, npc_pos, dropped_item);

        LOG_DEBUG(bridge, "NPC '{}' dropped item {} (template {}) at ({}, {})",
            npc_name, dropped_item.value, tmpl_id.value, npc_pos.x, npc_pos.y);
    }
}

void game_handlers::handle_npc_despawn_drop(const npc::npc& n) {
    if (!item_ || !world_ || !loot_registry_) return;

    // Copy needed data from the npc reference (only valid during callback)
    auto npc_map = n.current_map;
    auto npc_pos = n.pos;
    auto npc_name = n.name;

    // Generate on_despawn loot (body parts, rares, boss multi-drops)
    auto drop = npc::generate_despawn_loot(*loot_registry_, n.sprite_id);

    // Place item drops on ground
    for (auto tmpl_id : drop.items) {
        auto create_result = item_->create_from_template(tmpl_id, 1);
        if (create_result.is_err()) {
            LOG_WARN(bridge, "Failed to create despawn drop item from template {}: {}",
                tmpl_id.value, create_result.error());
            continue;
        }

        auto dropped_item = create_result.value();
        world_->add_ground_item(npc_map, npc_pos, dropped_item);
        broadcast_ground_item_spawn(npc_map, npc_pos, dropped_item);

        LOG_DEBUG(bridge, "NPC '{}' despawn dropped item {} (template {}) at ({}, {})",
            npc_name, dropped_item.value, tmpl_id.value, npc_pos.x, npc_pos.y);
    }

    if (!drop.items.empty()) {
        LOG_DEBUG(bridge, "NPC '{}' corpse despawned with {} item drops",
            npc_name, drop.items.size());
    }
}

void game_handlers::send_visible_ground_items(connection_id conn_id, map_id map,
                                               const world::position& pos,
                                               int radius_x, int radius_y) {
    if (!world_ || !ws_server_ || !item_) return;

    auto* conn = ws_server_->get_connection(conn_id);
    if (!conn || !conn->is_open()) return;

    // Scan tiles in rectangular visibility for ground items
    for (int16_t dx = static_cast<int16_t>(-radius_x); dx <= radius_x; ++dx) {
        for (int16_t dy = static_cast<int16_t>(-radius_y); dy <= radius_y; ++dy) {
            world::position tile_pos{
                static_cast<int16_t>(pos.x + dx),
                static_cast<int16_t>(pos.y + dy)
            };

            auto items = world_->get_ground_items(map, tile_pos);
            for (auto item : items) {
                auto* itm = item_->get_item(item);
                if (!itm) continue;

                network::ground_item_spawn_data data{
                    .item_id = item.value,
                    .template_id = itm->template_id.value,
                    .item_name = itm->name,
                    .count = itm->count,
                    .x = tile_pos.x,
                    .y = tile_pos.y
                };

                conn->send(network::make_ground_item_spawn(data));
            }
        }
    }
}

// ========== Entity Info ==========

void game_handlers::handle_entity_info_request(connection_id conn_id, const network::json_message& msg) {
    auto* conn = require_in_game(conn_id, msg.seq);
    if (!conn) return;

    if (!players_) {
        send_error(conn_id, msg.seq, "internal_error", "Player system unavailable");
        return;
    }

    auto data_result = network::entity_info_request_data::from_json(msg.data);
    if (data_result.is_err()) {
        send_error(conn_id, msg.seq, "invalid_request", data_result.error());
        return;
    }

    auto& data = data_result.value();
    auto requester_pid = conn->player();

    // Try to find the entity - could be a player or an NPC
    // First check if it's a player (resolve ecs entity_id → player_id)
    entity::entity target_entity{data.entity_id};
    auto target_pid_opt = players_->get_player_id_by_entity(target_entity);
    auto* target_player = target_pid_opt ? players_->get_player(*target_pid_opt) : nullptr;

    if (target_player) {
        // It's a player
        network::entity_info_response_data response;
        response.entity_id = data.entity_id;
        response.entity_type = "player";
        response.name = target_player->name;
        response.level = target_player->experience.level;
        response.hp = target_player->hp;
        response.hp_max = target_player->computed.max_hp;
        response.x = target_player->pos.x;
        response.y = target_player->pos.y;
        response.direction = static_cast<int16_t>(target_player->facing);

        // Player-specific fields
        switch (target_player->faction) {
            case faction::aresden:
                response.faction = "aresden";
                break;
            case faction::elvine:
                response.faction = "elvine";
                break;
            default:
                response.faction = "neutral";
                break;
        }

        response.class_type = static_cast<int16_t>(target_player->profession);
        response.pk_count = target_player->pk.count;

        // Guild name if player has one
        if (social_) {
            auto guild_id = social_->get_player_guild(*target_pid_opt);
            if (guild_id.is_valid()) {
                auto* guild = social_->get_guild(guild_id);
                if (guild) {
                    response.guild_name = guild->name;
                }
            }
        }

        conn->send(network::make_entity_info_response(msg.seq, true, &response));
        LOG_DEBUG(bridge, "Player {} requested info about player {} ({})",
            requester_pid.value, data.entity_id, target_player->name);
        return;
    }

    // Not a player - check if it's an NPC
    if (npc_) {
        entity::entity entity_id{data.entity_id};
        auto* target_npc = npc_->get_npc(entity_id);

        if (target_npc) {
            network::entity_info_response_data response;
            response.entity_id = data.entity_id;
            response.entity_type = "npc";
            response.name = target_npc->name;
            response.level = target_npc->level;
            response.hp = target_npc->hp;
            response.hp_max = target_npc->max_hp;
            response.x = target_npc->pos.x;
            response.y = target_npc->pos.y;
            response.direction = static_cast<int16_t>(target_npc->facing);

            // NPC-specific fields
            response.template_id = target_npc->template_id.value;
            // TODO: Add npc_type based on template flags when available

            conn->send(network::make_entity_info_response(msg.seq, true, &response));
            LOG_DEBUG(bridge, "Player {} requested info about NPC {} ({})",
                requester_pid.value, data.entity_id, target_npc->name);
            return;
        }
    }

    // Entity not found
    conn->send(network::make_entity_info_response(msg.seq, false, nullptr, "entity_not_found"));
    LOG_DEBUG(bridge, "Player {} requested info about unknown entity {}",
        requester_pid.value, data.entity_id);
}

// ========== Hunger Update ==========

void game_handlers::send_hunger_update(player_id pid, int8_t level) {
    if (!players_ || !ws_server_) return;

    auto* player = players_->get_player(pid);
    if (!player || player->connection.value == 0) return;

    auto* conn = ws_server_->get_connection(player->connection);
    if (!conn || !conn->is_open()) return;

    conn->send(network::make_hunger_update(level));

    LOG_DEBUG(bridge, "Sent hunger update to player {}: level={}, starving={}",
        pid.value, level, level <= 0);
}

// ========== Environment (Day/Night + Weather) ==========

void game_handlers::tick_weather() {
    if (!world_) return;

    thread_local std::mt19937 rng{std::random_device{}()};
    auto now = std::chrono::steady_clock::now();

    world_->for_each_map([&](map_id, world::map& m) {
        if (m.config().is_fixed_day_mode) return;

        // Expire active weather
        if (m.weather_active() && now >= m.weather_end_time()) {
            m.clear_weather();
        }

        // Chance to start new weather (1-in-30 per 10s tick ≈ legacy 1-in-300 per 1s tick)
        if (!m.weather_active()) {
            std::uniform_int_distribution<int> chance_dist(1, 30);
            if (chance_dist(rng) != 1) return;

            // Pick weather type based on map type
            world::weather_type weather;
            if (m.config().is_snow_enabled) {
                std::uniform_int_distribution<int> type_dist(4, 6);
                weather = static_cast<world::weather_type>(type_dist(rng));
            } else {
                std::uniform_int_distribution<int> type_dist(1, 3);
                weather = static_cast<world::weather_type>(type_dist(rng));
            }

            // Duration: 3-10 minutes
            std::uniform_int_distribution<int> dur_dist(180, 600);
            auto duration = std::chrono::seconds(dur_dist(rng));
            m.start_weather(weather, now + duration);
        }
    });
}

void game_handlers::broadcast_environment_update() {
    if (!players_ || !ws_server_ || !world_ || !scheduler_) return;

    auto& clock = scheduler_->game_time();
    auto hour = static_cast<uint8_t>(clock.hour());
    auto minute = static_cast<uint8_t>(clock.minute());
    bool is_day = clock.is_day();

    players_->for_each_player([&](player_id, player::player& plr) {
        if (plr.connection.value == 0) return;

        auto* map = world_->get_map(plr.current_map);
        if (!map) return;

        network::environment_update_data env{
            .hour = hour,
            .minute = minute,
            .is_day = is_day,
            .weather = static_cast<uint8_t>(map->weather())
        };

        // Fixed-day maps always show daytime and clear weather
        if (map->config().is_fixed_day_mode) {
            env.is_day = true;
            env.weather = 0;
        }

        auto* conn = ws_server_->get_connection(plr.connection);
        if (conn && conn->is_open()) {
            conn->send(network::make_environment_update(env));
        }
    });
}

// ========== Equipment Handling ==========

void game_handlers::handle_player_equip(connection_id conn_id, const network::json_message& msg) {
    auto* conn = require_in_game(conn_id, msg.seq);
    if (!conn) return;

    if (!players_ || !inventory_ || !item_) {
        send_error(conn_id, msg.seq, "internal_error", "Required systems unavailable");
        return;
    }

    auto data_result = network::player_equip_request_data::from_json(msg.data);
    if (data_result.is_err()) {
        send_error(conn_id, msg.seq, "invalid_request", data_result.error());
        return;
    }

    auto& data = data_result.value();
    auto pid = conn->player();
    auto* plr = players_->get_player(pid);
    if (!plr) {
        send_error(conn_id, msg.seq, "invalid_player", "Player not found");
        return;
    }

    // Check alive
    if (plr->is_dead()) {
        send_error(conn_id, msg.seq, "player_dead", "Cannot equip while dead");
        return;
    }

    // Check not trading
    auto trade_partner = inventory_->get_trade_partner(plr->ecs_entity.id);
    if (trade_partner.is_valid()) {
        send_error(conn_id, msg.seq, "player_busy", "Cannot equip while trading");
        return;
    }

    // Get inventory and validate slot
    auto* inv = inventory_->get_inventory(plr->ecs_entity.id);
    if (!inv) {
        send_error(conn_id, msg.seq, "internal_error", "Inventory not found");
        return;
    }

    auto* inv_slot = inv->get_slot(data.inventory_slot);
    if (!inv_slot || inv_slot->is_empty()) {
        send_error(conn_id, msg.seq, "invalid_slot", "Inventory slot is empty");
        return;
    }

    // Get the item
    auto* itm = item_->get_item(inv_slot->item);
    if (!itm) {
        send_error(conn_id, msg.seq, "item_not_found", "Item not found");
        return;
    }

    // Validate item is equipment
    if (!itm->is_equipment() || itm->equip_position == item::equip_pos::none) {
        send_error(conn_id, msg.seq, "not_equippable", "Item cannot be equipped");
        return;
    }

    // Validate durability
    if (itm->is_broken()) {
        send_error(conn_id, msg.seq, "item_broken", "Item is broken and cannot be equipped");
        return;
    }

    // Validate target slot
    auto target_slot = static_cast<player::equip_slot>(data.target_slot);
    if (!player::is_valid_slot_for_item(itm->equip_position, target_slot)) {
        send_error(conn_id, msg.seq, "invalid_slot", "Item cannot go in that slot");
        return;
    }

    // Check stat requirements
    auto req = item::check_requirements(*itm,
        plr->experience.level,
        plr->computed.strength,
        plr->computed.dexterity,
        plr->computed.intelligence,
        plr->computed.magic);
    if (!req.can_use()) {
        send_error(conn_id, msg.seq, "requirements_not_met", "You do not meet the requirements");
        return;
    }

    network::equip_result_msg result;
    result.slot = data.target_slot;

    // Two-handed weapon logic: if equipping a 2H weapon and shield is occupied
    if (itm->two_handed && plr->equipment.has_equipped(player::equip_slot::shield)) {
        // Need an extra free inventory slot for the shield (beyond the one being freed)
        if (inv->free_slots() < 1) {
            send_error(conn_id, msg.seq, "inventory_full",
                "Need a free slot to unequip shield for two-handed weapon");
            return;
        }
        // Unequip shield to inventory
        auto shield_equipped = players_->unequip_item(pid, player::equip_slot::shield);
        auto shield_inv_slot = inv->find_empty_slot();
        if (shield_inv_slot.has_value()) {
            inv->get_slot(*shield_inv_slot)->set(shield_equipped.id, 1);
            result.unequipped_shield_id = shield_equipped.id.value;
            result.shield_to_inv_slot = static_cast<uint8_t>(*shield_inv_slot);
            broadcast_equipment_change(pid, player::equip_slot::shield, item_id{});
        }
    }

    // Shield equip + 2H weapon currently equipped: check the weapon in weapon slot
    if (target_slot == player::equip_slot::shield &&
        plr->equipment.has_equipped(player::equip_slot::weapon))
    {
        auto* weapon_itm = item_->get_item(plr->equipment.weapon().id);
        if (weapon_itm && weapon_itm->two_handed) {
            send_error(conn_id, msg.seq, "two_handed_weapon_equipped",
                "Cannot equip shield while using a two-handed weapon");
            return;
        }
    }

    // Swap logic: if target equipment slot is occupied
    if (plr->equipment.has_equipped(target_slot)) {
        auto old_equipped = players_->unequip_item(pid, target_slot);
        // Place old item in the inventory slot being freed by the new equip
        inv_slot->set(old_equipped.id, 1);
        result.swapped_item_id = old_equipped.id.value;
        result.swapped_to_inv_slot = static_cast<uint8_t>(data.inventory_slot);
    } else {
        // Just clear the inventory slot
        inv_slot->clear();
    }

    // Equip new item
    players_->equip_item(pid, target_slot, itm->id,
        static_cast<uint16_t>(itm->durability),
        static_cast<uint16_t>(itm->max_durability));

    // Recalculate stats
    players_->recalculate_equipment_modifiers(pid);

    // Build success response
    result.success = true;
    result.item_id = itm->id.value;
    result.item_name = itm->name;
    result.durability = itm->durability;
    result.max_durability = itm->max_durability;

    conn->send(network::make_player_equip_response(msg.seq, result));

    // Send stat update
    // Re-fetch player since recalculate may have changed computed stats
    plr = players_->get_player(pid);
    if (plr) {
        send_stat_update(conn_id, *plr);
    }

    // Broadcast to nearby players
    broadcast_equipment_change(pid, target_slot, itm->id);

    LOG_DEBUG(bridge, "Player {} equipped item {} ('{}') to slot {}",
        pid.value, itm->id.value, itm->name, data.target_slot);
}

void game_handlers::handle_player_unequip(connection_id conn_id, const network::json_message& msg) {
    auto* conn = require_in_game(conn_id, msg.seq);
    if (!conn) return;

    if (!players_ || !inventory_ || !item_) {
        send_error(conn_id, msg.seq, "internal_error", "Required systems unavailable");
        return;
    }

    auto data_result = network::player_unequip_request_data::from_json(msg.data);
    if (data_result.is_err()) {
        send_error(conn_id, msg.seq, "invalid_request", data_result.error());
        return;
    }

    auto& data = data_result.value();
    auto pid = conn->player();
    auto* plr = players_->get_player(pid);
    if (!plr) {
        send_error(conn_id, msg.seq, "invalid_player", "Player not found");
        return;
    }

    // Check alive
    if (plr->is_dead()) {
        send_error(conn_id, msg.seq, "player_dead", "Cannot unequip while dead");
        return;
    }

    // Check not trading
    auto trade_partner = inventory_->get_trade_partner(plr->ecs_entity.id);
    if (trade_partner.is_valid()) {
        send_error(conn_id, msg.seq, "player_busy", "Cannot unequip while trading");
        return;
    }

    // Validate slot
    if (data.equip_slot >= static_cast<uint8_t>(player::equip_slot::count)) {
        send_error(conn_id, msg.seq, "invalid_slot", "Invalid equipment slot");
        return;
    }

    auto slot = static_cast<player::equip_slot>(data.equip_slot);
    if (!plr->equipment.has_equipped(slot)) {
        send_error(conn_id, msg.seq, "slot_empty", "Nothing equipped in that slot");
        return;
    }

    // Check inventory space
    auto* inv = inventory_->get_inventory(plr->ecs_entity.id);
    if (!inv) {
        send_error(conn_id, msg.seq, "internal_error", "Inventory not found");
        return;
    }

    if (inv->is_full()) {
        send_error(conn_id, msg.seq, "inventory_full", "Inventory is full");
        return;
    }

    // Unequip
    auto equipped = players_->unequip_item(pid, slot);

    // Add to inventory
    auto inv_slot = inv->find_empty_slot();
    if (!inv_slot.has_value()) {
        // Rollback - re-equip
        players_->equip_item(pid, slot, equipped.id, equipped.durability, equipped.max_durability);
        send_error(conn_id, msg.seq, "inventory_full", "Failed to find inventory slot");
        return;
    }

    inv->get_slot(*inv_slot)->set(equipped.id, 1);

    // Recalculate stats
    players_->recalculate_equipment_modifiers(pid);

    // Get item name for response
    auto* itm = item_->get_item(equipped.id);
    std::string item_name = itm ? itm->name : "Unknown";

    // Build response
    network::unequip_result_msg result;
    result.success = true;
    result.slot = data.equip_slot;
    result.item_id = equipped.id.value;
    result.item_name = item_name;
    result.inventory_slot = static_cast<uint8_t>(*inv_slot);

    conn->send(network::make_player_unequip_response(msg.seq, result));

    // Send stat update
    plr = players_->get_player(pid);
    if (plr) {
        send_stat_update(conn_id, *plr);
    }

    // Broadcast to nearby (slot now empty)
    broadcast_equipment_change(pid, slot, item_id{});

    LOG_DEBUG(bridge, "Player {} unequipped item {} ('{}') from slot {}",
        pid.value, equipped.id.value, item_name, data.equip_slot);
}

void game_handlers::broadcast_equipment_change(player_id pid, player::equip_slot slot, item_id itm) {
    if (!players_ || !ws_server_) return;

    auto* plr = players_->get_player(pid);
    if (!plr) return;

    uint32_t template_id = 0;
    if (itm.is_valid() && item_) {
        auto* item_inst = item_->get_item(itm);
        if (item_inst) {
            template_id = item_inst->template_id.value;
        }
    }

    network::equipment_change_broadcast_data data{
        .entity_id = plr->ecs_entity.id,
        .slot = static_cast<uint8_t>(slot),
        .item_id = itm.value,
        .template_id = template_id
    };
    auto msg = network::make_equipment_change_broadcast(data);

    auto nearby = players_->get_players_who_can_see(plr->current_map, plr->pos);
    for (auto nearby_pid : nearby) {
        if (nearby_pid == pid) continue;  // Don't send to self
        auto* np = players_->get_player(nearby_pid);
        if (!np || np->connection.value == 0) continue;
        auto* conn = ws_server_->get_connection(np->connection);
        if (conn && conn->is_open()) {
            conn->send(msg);
        }
    }
}

void game_handlers::send_stat_update(connection_id conn_id, const player::player& plr) {
    if (!ws_server_) return;

    auto* conn = ws_server_->get_connection(conn_id);
    if (!conn || !conn->is_open()) return;

    network::stat_update_data data{
        .max_hp = plr.computed.max_hp,
        .max_mp = plr.computed.max_mp,
        .max_sp = plr.computed.max_sp,
        .attack_power = plr.computed.attack_power,
        .magic_power = plr.computed.magic_power,
        .defense = plr.computed.defense,
        .magic_defense = plr.computed.magic_defense,
        .hit_rate = plr.computed.hit_rate,
        .dodge_rate = plr.computed.dodge_rate,
        .critical_rate = plr.computed.critical_rate
    };
    conn->send(network::make_stat_update(data));
}

void game_handlers::distribute_npc_kill_exp(entity::entity killer, int32_t base_exp) {
    if (!players_ || base_exp <= 0) return;

    // Resolve killer to player_id
    auto killer_pid_opt = players_->get_player_id_by_entity(killer);
    if (!killer_pid_opt) return;
    auto killer_pid = *killer_pid_opt;

    auto* killer_player = players_->get_player(killer_pid);
    if (!killer_player) return;

    // Check party membership
    social::party* pt = nullptr;
    if (social_) {
        auto party_id = social_->get_player_party(killer_pid);
        if (party_id.is_valid()) {
            pt = social_->get_party(party_id);
        }
    }

    // No party, or individual mode, or low XP — award all to killer
    if (!pt || pt->experience == social::exp_mode::individual || base_exp < 10) {
        players_->add_experience(killer_pid, base_exp);
        LOG_DEBUG(bridge, "Awarded {} XP to player '{}' (solo kill)", base_exp, killer_player->name);
        return;
    }

    // Get eligible party members: same map, alive (hp > 0)
    auto same_map_ids = pt->members_in_map(killer_player->current_map);
    std::vector<std::pair<player_id, int16_t>> eligible; // pid, level
    int32_t total_levels = 0;
    for (auto pid : same_map_ids) {
        auto* p = players_->get_player(pid);
        if (p && !p->is_dead()) {
            eligible.emplace_back(pid, p->experience.level);
            total_levels += p->experience.level;
        }
    }

    if (eligible.empty()) return;

    // Single eligible member gets full XP (no bonus)
    if (eligible.size() == 1) {
        players_->add_experience(eligible[0].first, base_exp);
        LOG_DEBUG(bridge, "Awarded {} XP to player (party of 1 eligible)", base_exp);
        return;
    }

    auto eligible_count = static_cast<int>(eligible.size());

    if (pt->experience == social::exp_mode::equal_split) {
        auto per_member = social::calculate_party_exp_share(base_exp, eligible_count);
        for (auto& [pid, level] : eligible) {
            players_->add_experience(pid, per_member);
        }
        LOG_DEBUG(bridge, "Party equal split: {} base XP -> {} each for {} members",
                  base_exp, per_member, eligible_count);
    }
    else if (pt->experience == social::exp_mode::level_weighted) {
        for (auto& [pid, level] : eligible) {
            auto share = social::calculate_level_weighted_exp(
                base_exp, eligible_count, level, total_levels);
            players_->add_experience(pid, share);
        }
        LOG_DEBUG(bridge, "Party level-weighted: {} base XP for {} members (total levels {})",
                  base_exp, eligible_count, total_levels);
    }
}

// === Crafting: Manufacturing handlers ===

void game_handlers::handle_manufacture_list_request(connection_id conn_id,
                                                     const network::json_message& msg) {
    auto* conn = require_in_game(conn_id, msg.seq);
    if (!conn) return;

    if (!manufacturing_) {
        send_error(conn_id, msg.seq, "not_available", "Manufacturing is not available");
        return;
    }

    auto pid = conn->player();
    auto recipes = manufacturing_->get_available_recipes(entity_id{pid.value});

    nlohmann::json recipe_list = nlohmann::json::array();
    for (const auto* recipe : recipes) {
        nlohmann::json r;
        r["id"] = recipe->id;
        r["name"] = recipe->result;
        r["skill_req"] = recipe->skill_req;
        r["success_rate"] = recipe->success_rate;

        nlohmann::json ings = nlohmann::json::array();
        for (const auto& ing : recipe->ingredients) {
            ings.push_back({{"item_id", ing.item_id}, {"count", ing.count}});
        }
        r["ingredients"] = ings;

        recipe_list.push_back(r);
    }

    conn->send(network::make_manufacture_list_response(msg.seq, recipe_list));
}

void game_handlers::handle_manufacture_request(connection_id conn_id,
                                                const network::json_message& msg) {
    auto* conn = require_in_game(conn_id, msg.seq);
    if (!conn) return;

    if (!manufacturing_) {
        send_error(conn_id, msg.seq, "not_available", "Manufacturing is not available");
        return;
    }

    auto parse = network::manufacture_request_data::from_json(msg.data);
    if (parse.is_err()) {
        send_error(conn_id, msg.seq, "invalid_data", parse.error());
        return;
    }

    auto& data = parse.value();
    auto pid = conn->player();
    auto result = manufacturing_->attempt_craft(entity_id{pid.value}, data.recipe_index);

    if (result.reason == skill::skill_use_result::insufficient_skill) {
        conn->send(network::make_manufacture_response(msg.seq, false, "", 0, "insufficient_skill"));
        return;
    }
    if (result.reason == skill::skill_use_result::insufficient_materials) {
        conn->send(network::make_manufacture_response(msg.seq, false, "", 0, "insufficient_materials"));
        return;
    }
    if (!result.success && result.reason == skill::skill_use_result::failure) {
        conn->send(network::make_manufacture_response(msg.seq, false, "", 0, "inventory_full"));
        return;
    }

    // Look up recipe name for response
    std::string item_name;
    auto* registry = subsystems().get<build_recipe_registry>();
    if (registry) {
        auto* recipe = registry->get(data.recipe_index);
        if (recipe) item_name = recipe->result;
    }

    conn->send(network::make_manufacture_response(
        msg.seq, result.success, item_name, result.exp_gained));

    // Fire quest event on success
    if (result.success && quests_ && result.created_item.is_valid()) {
        quests_->on_item_crafted({
            .player = pid,
            .item = result.created_item,
            .count = 1
        });
    }
}

// === Crafting: Alchemy handlers ===

void game_handlers::handle_alchemy_list_request(connection_id conn_id,
                                                  const network::json_message& msg) {
    auto* conn = require_in_game(conn_id, msg.seq);
    if (!conn) return;

    if (!alchemy_) {
        send_error(conn_id, msg.seq, "not_available", "Alchemy is not available");
        return;
    }

    auto pid = conn->player();
    auto recipes = alchemy_->get_available_recipes(entity_id{pid.value});

    nlohmann::json recipe_list = nlohmann::json::array();
    for (const auto* recipe : recipes) {
        nlohmann::json r;
        r["id"] = recipe->id;
        r["name"] = recipe->result;
        r["skill_limit"] = recipe->skill_limit;
        r["difficulty"] = recipe->difficulty;

        nlohmann::json ings = nlohmann::json::array();
        for (const auto& ing : recipe->ingredients) {
            ings.push_back({{"item_id", ing.item_id}, {"count", ing.count}});
        }
        r["ingredients"] = ings;

        recipe_list.push_back(r);
    }

    conn->send(network::make_alchemy_list_response(msg.seq, recipe_list));
}

void game_handlers::handle_alchemy_request(connection_id conn_id,
                                            const network::json_message& msg) {
    auto* conn = require_in_game(conn_id, msg.seq);
    if (!conn) return;

    if (!alchemy_) {
        send_error(conn_id, msg.seq, "not_available", "Alchemy is not available");
        return;
    }

    auto parse = network::alchemy_request_data::from_json(msg.data);
    if (parse.is_err()) {
        send_error(conn_id, msg.seq, "invalid_data", parse.error());
        return;
    }

    auto& data = parse.value();
    auto pid = conn->player();
    auto result = alchemy_->attempt_craft(entity_id{pid.value}, data.recipe_id);

    if (result.reason == skill::skill_use_result::insufficient_skill) {
        conn->send(network::make_alchemy_response(msg.seq, false, "", 0, "insufficient_skill"));
        return;
    }
    if (result.reason == skill::skill_use_result::insufficient_materials) {
        conn->send(network::make_alchemy_response(msg.seq, false, "", 0, "insufficient_materials"));
        return;
    }
    if (!result.success && result.reason == skill::skill_use_result::failure) {
        conn->send(network::make_alchemy_response(msg.seq, false, "", 0, "inventory_full"));
        return;
    }

    // Look up recipe name for response
    std::string item_name;
    auto* registry = subsystems().get<craft_recipe_registry>();
    if (registry) {
        auto* recipe = registry->get(data.recipe_id);
        if (recipe) item_name = recipe->result;
    }

    conn->send(network::make_alchemy_response(
        msg.seq, result.success, item_name, result.exp_gained));

    // Fire quest event on success
    if (result.success && quests_ && result.created_item.is_valid()) {
        quests_->on_item_crafted({
            .player = pid,
            .item = result.created_item,
            .count = 1
        });
    }
}

// === Mining ===

void game_handlers::handle_mine_request(connection_id conn_id,
                                         const network::json_message& msg) {
    auto* conn = require_in_game(conn_id, msg.seq);
    if (!conn) return;

    if (!mining_) {
        send_error(conn_id, msg.seq, "not_available", "Mining is not available");
        return;
    }

    auto parse = network::mine_request_data::from_json(msg.data);
    if (parse.is_err()) {
        send_error(conn_id, msg.seq, "invalid_data", parse.error());
        return;
    }

    auto& data = parse.value();
    auto pid = conn->player();
    auto* plr = players_->get_player(pid);
    if (!plr) {
        send_error(conn_id, msg.seq, "internal_error", "Player not found");
        return;
    }

    auto* map = world_->get_map(plr->current_map);
    if (!map) {
        send_error(conn_id, msg.seq, "internal_error", "Map not found");
        return;
    }
    auto map_name = std::string(map->name());
    auto result = mining_->attempt_mine(entity_id(pid.value), data.target_x, data.target_y, map_name);

    if (result.reason == skill::skill_use_result::invalid_target) {
        conn->send(network::make_mine_response(msg.seq, false, "", 0, 0, false, "invalid_target"));
        return;
    }
    if (result.reason == skill::skill_use_result::insufficient_materials) {
        conn->send(network::make_mine_response(msg.seq, false, "", 0, 0, false, "no_pickaxe"));
        return;
    }
    if (result.reason == skill::skill_use_result::insufficient_skill) {
        conn->send(network::make_mine_response(msg.seq, false, "", 0, 0, false, "insufficient_skill"));
        return;
    }

    if (!result.success) {
        conn->send(network::make_mine_response(msg.seq, false, "", 0, 0, result.node_depleted, "miss"));
        return;
    }

    conn->send(network::make_mine_response(msg.seq, true,
        result.item_name, result.template_id, result.exp_gained, result.node_depleted));
}

}  // namespace hb::bridge
