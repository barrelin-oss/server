// auth_handlers.cpp
// Message handlers for authentication protocol implementation

// Include platform header first to define NOMINMAX before Windows headers
#include "platform/platform.h"

#include "bridge/handlers/auth_handlers.h"
#include "network/websocket_server.h"
#include "auth/auth_system.h"
#include "auth/character_serialization.h"
#include "player/player_system.h"
#include "inventory/inventory_system.h"
#include "world/world_subsystem.h"
#include "world/map.h"
#include "admin/admin_system.h"
#include "npc/npc_system.h"
#include "npc/npc.h"
#include "item/item_system.h"
#include "item/item.h"
#include "social/social_system.h"
#include "magic/magic_system.h"
#include "quest/quest_system.h"
#include "scheduler/scheduler.h"
#include "core/logger.h"
#include "core/subsystem.h"

namespace hb::bridge {

auth_handlers::auth_handlers() = default;
auth_handlers::~auth_handlers() = default;

void auth_handlers::initialize(network::websocket_server* ws_server,
                                auth::auth_system* auth,
                                player::player_system* players,
                                world::world_subsystem* world,
                                inventory::inventory_system* inventory,
                                admin::admin_system* admin,
                                npc::npc_system* npc,
                                item::item_system* item,
                                social::social_system* social,
                                scheduler* sched) {
    ws_server_ = ws_server;
    auth_ = auth;
    players_ = players;
    world_ = world;
    inventory_ = inventory;
    admin_ = admin;
    npc_ = npc;
    item_ = item;
    social_ = social;
    scheduler_ = sched;
    LOG_INFO(bridge, "Auth handlers initialized (players: {}, world: {}, inventory: {}, admin: {}, npc: {}, item: {}, social: {})",
        players_ != nullptr ? "yes" : "no",
        world_ != nullptr ? "yes" : "no",
        inventory_ != nullptr ? "yes" : "no",
        admin_ != nullptr ? "yes" : "no",
        npc_ != nullptr ? "yes" : "no",
        item_ != nullptr ? "yes" : "no",
        social_ != nullptr ? "yes" : "no");
}

void auth_handlers::handle_message(connection_id conn_id, const network::json_message& msg) {
    switch (msg.type) {
        case network::json_message_type::login_request:
            handle_login(conn_id, msg);
            break;
        case network::json_message_type::logout_request:
            handle_logout(conn_id, msg);
            break;
        case network::json_message_type::create_account_request:
            handle_create_account(conn_id, msg);
            break;
        case network::json_message_type::get_characters_request:
            handle_get_characters(conn_id, msg);
            break;
        case network::json_message_type::create_character_request:
            handle_create_character(conn_id, msg);
            break;
        case network::json_message_type::delete_character_request:
            handle_delete_character(conn_id, msg);
            break;
        case network::json_message_type::enter_game_request:
            handle_enter_game(conn_id, msg);
            break;
        case network::json_message_type::ping:
            handle_ping(conn_id, msg);
            break;
        default:
            LOG_WARN(bridge, "Unhandled auth message type: {}",
                network::to_string(msg.type));
            send_error(conn_id, msg.seq, "unknown_message_type",
                "Message type not recognized by auth handler");
            break;
    }
}

void auth_handlers::handle_login(connection_id conn_id, const network::json_message& msg) {
    auto* conn = get_connection_or_error(conn_id, msg.seq);
    if (!conn) return;

    // Parse request data
    auto data_result = network::login_request_data::from_json(msg.data);
    if (data_result.is_err()) {
        send_error(conn_id, msg.seq, "invalid_request", data_result.error());
        return;
    }

    auto& data = data_result.value();

    LOG_DEBUG(bridge, "Login request from {} for user '{}'",
        conn->remote_address(), data.username);

    // Forum auth path
    if (auth_->forum_auth_enabled()) {
        if (!data.forum_token.empty()) {
            // Token-based auto-login
            auto forum_result = auth_->authenticate_forum_token(
                data.forum_token, conn->remote_address());

            if (forum_result.is_err()) {
                std::string error_str(auth::to_string(forum_result.error()));
                LOG_INFO(bridge, "Forum token login failed for '{}': {}", data.username, error_str);
                auto response = network::make_login_response(msg.seq, false, std::nullopt, error_str);
                conn->send(response);
                return;
            }

            auto& result = forum_result.value();
            conn->set_state(network::ws_connection_state::authenticated);
            conn->set_account(result.session.account);
            conn->set_session_token(result.session.token);

            LOG_INFO(bridge, "User '{}' logged in via forum token from {}",
                data.username, conn->remote_address());

            auto response = network::make_login_response(msg.seq, true, result.session.token);
            conn->send(response);
            return;
        }

        // Password-based forum login
        auto forum_result = auth_->authenticate_forum(
            data.username, data.password, conn->remote_address());

        if (forum_result.is_err()) {
            std::string error_str(auth::to_string(forum_result.error()));
            LOG_INFO(bridge, "Forum login failed for '{}': {}", data.username, error_str);
            auto response = network::make_login_response(msg.seq, false, std::nullopt, error_str);
            conn->send(response);
            return;
        }

        auto& result = forum_result.value();
        conn->set_state(network::ws_connection_state::authenticated);
        conn->set_account(result.session.account);
        conn->set_session_token(result.session.token);

        LOG_INFO(bridge, "User '{}' logged in via forum from {}", data.username, conn->remote_address());

        auto response = network::make_login_response(
            msg.seq, true, result.session.token, std::nullopt, result.forum_token);
        conn->send(response);
        return;
    }

    // Local auth path (default)
    auto auth_result = auth_->authenticate(data.username, data.password, conn->remote_address());

    if (auth_result.is_err()) {
        auto error = auth_result.error();
        std::string error_str(auth::to_string(error));

        LOG_INFO(bridge, "Login failed for '{}': {}", data.username, error_str);

        auto response = network::make_login_response(msg.seq, false, std::nullopt, error_str);
        conn->send(response);
        return;
    }

    auto& session = auth_result.value();

    // Update connection state
    conn->set_state(network::ws_connection_state::authenticated);
    conn->set_account(session.account);
    conn->set_session_token(session.token);

    LOG_INFO(bridge, "User '{}' logged in from {}", data.username, conn->remote_address());

    // Send success response
    auto response = network::make_login_response(msg.seq, true, session.token);
    conn->send(response);
}

void auth_handlers::handle_logout(connection_id conn_id, const network::json_message& msg) {
    auto* conn = require_authenticated(conn_id, msg.seq);
    if (!conn) return;

    // If player was in game, save and clean up
    if (conn->state() == network::ws_connection_state::in_game && conn->player().value != 0) {
        auto pid = conn->player();
        LOG_INFO(bridge, "Player {} logging out, saving state...", pid.value);

        // Save player state
        save_player_state(pid);

        // Disconnect from guild
        if (social_ && players_) {
            auto* player = players_->get_player(pid);
            if (player) {
                social_->disconnect_guild_member(pid, player->character_id);
            }
            social_->unregister_player(pid);
        }

        // Notify nearby players of despawn
        if (players_ && ws_server_) {
            auto* player = players_->get_player(pid);
            if (player) {
                auto nearby = players_->get_players_who_can_see(player->current_map, player->pos);

                auto despawn_msg = network::make_entity_despawn(0, player->ecs_entity.id);

                for (auto other_id : nearby) {
                    if (other_id == pid) continue;

                    auto* other = players_->get_player(other_id);
                    if (!other || other->connection.value == 0) continue;

                    auto* other_conn = ws_server_->get_connection(other->connection);
                    if (other_conn && other_conn->is_open()) {
                        other_conn->send(despawn_msg);
                    }
                }
            }

            // Remove player from system
            players_->remove_player(pid);
        }

        // Clean up inventory
        if (inventory_) {
            inventory_->destroy_inventory(entity_id{pid.value});
        }
    }

    // Invalidate session
    if (!conn->session_token().empty()) {
        auth_->invalidate_session(conn->session_token());
    }

    // Reset connection state
    conn->set_state(network::ws_connection_state::connected);
    conn->set_account(account_id{});
    conn->set_player(player_id{});
    conn->set_session_token("");

    LOG_INFO(bridge, "Connection {} logged out", conn_id.value);

    auto response = network::make_logout_response(msg.seq, true);
    conn->send(response);
}

void auth_handlers::handle_create_account(connection_id conn_id, const network::json_message& msg) {
    auto* conn = get_connection_or_error(conn_id, msg.seq);
    if (!conn) return;

    // Parse request data
    auto data_result = network::create_account_request_data::from_json(msg.data);
    if (data_result.is_err()) {
        send_error(conn_id, msg.seq, "invalid_request", data_result.error());
        return;
    }

    auto& data = data_result.value();

    LOG_DEBUG(bridge, "Account creation request for '{}'", data.username);

    // Attempt to create account
    auto create_result = auth_->create_account(data.username, data.password);

    if (create_result.is_err()) {
        auto error = create_result.error();
        std::string error_str(auth::to_string(error));

        LOG_INFO(bridge, "Account creation failed for '{}': {}", data.username, error_str);

        auto response = network::make_create_account_response(msg.seq, false, std::nullopt, error_str);
        conn->send(response);
        return;
    }

    auto account_id = create_result.value();

    LOG_INFO(bridge, "Account '{}' created with id {}", data.username, account_id.value);

    auto response = network::make_create_account_response(msg.seq, true, account_id.value);
    conn->send(response);
}

void auth_handlers::handle_get_characters(connection_id conn_id, const network::json_message& msg) {
    auto* conn = require_authenticated(conn_id, msg.seq);
    if (!conn) return;

    auto chars_result = auth_->get_characters(conn->account());

    if (chars_result.is_err()) {
        auto error = chars_result.error();
        std::string error_str(auth::to_string(error));

        LOG_WARN(bridge, "Failed to get characters for account {}: {}",
            conn->account().value, error_str);

        send_error(conn_id, msg.seq, error_str, "Failed to retrieve characters");
        return;
    }

    auto& characters = chars_result.value();

    LOG_DEBUG(bridge, "Sending {} characters for account {}",
        characters.size(), conn->account().value);

    auto response = network::make_get_characters_response(msg.seq, characters);
    conn->send(response);
}

void auth_handlers::handle_create_character(connection_id conn_id, const network::json_message& msg) {
    auto* conn = require_authenticated(conn_id, msg.seq);
    if (!conn) return;

    // Parse request data
    auto data_result = network::create_character_request_data::from_json(msg.data);
    if (data_result.is_err()) {
        send_error(conn_id, msg.seq, "invalid_request", data_result.error());
        return;
    }

    auto& data = data_result.value();
    auto create_info = data.to_create_info();

    LOG_DEBUG(bridge, "Character creation request for '{}' by account {}",
        data.name, conn->account().value);

    // Attempt to create character
    auto create_result = auth_->create_character(conn->account(), create_info);

    if (create_result.is_err()) {
        auto error = create_result.error();
        std::string error_str(auth::to_string(error));

        LOG_INFO(bridge, "Character creation failed for '{}': {}", data.name, error_str);

        auto response = network::make_create_character_response(
            msg.seq, false, std::nullopt, error_str);
        conn->send(response);
        return;
    }

    auto char_id = create_result.value();

    LOG_INFO(bridge, "Character '{}' created with id {} for account {}",
        data.name, char_id.value, conn->account().value);

    // Send success response
    auto response = network::make_create_character_response(msg.seq, true, char_id.value);
    conn->send(response);

    // Re-send the full character list so the client updates immediately
    auto chars_result = auth_->get_characters(conn->account());
    if (chars_result.is_ok()) {
        auto list_response = network::make_get_characters_response(0, chars_result.value());
        conn->send(list_response);
    }
}

void auth_handlers::handle_delete_character(connection_id conn_id, const network::json_message& msg) {
    auto* conn = require_authenticated(conn_id, msg.seq);
    if (!conn) return;

    // Parse request data
    auto data_result = network::delete_character_request_data::from_json(msg.data);
    if (data_result.is_err()) {
        send_error(conn_id, msg.seq, "invalid_request", data_result.error());
        return;
    }

    auto& data = data_result.value();
    auto char_id = player_id{data.character_id};

    LOG_DEBUG(bridge, "Character deletion request for id {} by account {}",
        char_id.value, conn->account().value);

    // Attempt to delete character
    auto delete_result = auth_->delete_character(conn->account(), char_id);

    if (delete_result.is_err()) {
        auto error = delete_result.error();
        std::string error_str(auth::to_string(error));

        LOG_INFO(bridge, "Character deletion failed for id {}: {}", char_id.value, error_str);

        auto response = network::make_delete_character_response(msg.seq, false, error_str);
        conn->send(response);
        return;
    }

    LOG_INFO(bridge, "Character id {} deleted for account {}",
        char_id.value, conn->account().value);

    auto response = network::make_delete_character_response(msg.seq, true);
    conn->send(response);

    // Re-send the full character list so the client updates immediately
    auto chars_result = auth_->get_characters(conn->account());
    if (chars_result.is_ok()) {
        auto list_response = network::make_get_characters_response(0, chars_result.value());
        conn->send(list_response);
    }
}

void auth_handlers::handle_enter_game(connection_id conn_id, const network::json_message& msg) {
    auto* conn = require_authenticated(conn_id, msg.seq);
    if (!conn) return;

    // Parse request data
    auto data_result = network::enter_game_request_data::from_json(msg.data);
    if (data_result.is_err()) {
        send_error(conn_id, msg.seq, "invalid_request", data_result.error());
        return;
    }

    auto& data = data_result.value();
    auto char_id = player_id{data.character_id};

    // Calculate initial visibility radii from client viewport
    auto vis_radii = network::calculate_visibility_radius(data.screen_width, data.screen_height);

    LOG_DEBUG(bridge, "Enter game request for character {} by account {}",
        char_id.value, conn->account().value);

    // Load full character data with ownership verification
    auto char_result = auth_->load_character_full(char_id, conn->account());
    if (char_result.is_err()) {
        auto error = char_result.error();
        std::string error_str(auth::to_string(error));

        LOG_INFO(bridge, "Enter game failed for character {}: {}", char_id.value, error_str);

        auto response = network::make_enter_game_response(msg.seq, false, nullptr, error_str);
        conn->send(response);
        return;
    }

    auto& char_data = char_result.value();

    // Check if this account already has a character in game (stale session not cleaned up)
    if (players_) {
        player_id existing_player_id{0};
        connection_id old_connection{0};

        // Find any player belonging to this account
        players_->for_each_player([&](player_id pid, const player::player& p) {
            if (p.account == conn->account()) {
                existing_player_id = pid;
                old_connection = p.connection;
            }
        });

        if (existing_player_id.value != 0) {
            auto* existing_player = players_->get_player(existing_player_id);
            std::string existing_name = existing_player ? existing_player->name : "unknown";

            if (!data.force_disconnect) {
                // Account already has a character in game from a previous session
                LOG_INFO(bridge, "Account {} already has character '{}' in game (stale session), rejecting new entry",
                    conn->account().value, existing_name);

                auto response = network::make_enter_game_response(
                    msg.seq, false, nullptr, "account_already_in_game");
                conn->send(response);
                return;
            }

            // Force disconnect - clean up the stale player
            LOG_INFO(bridge, "Force disconnecting stale session for account {} (character '{}')",
                conn->account().value, existing_name);

            // Save state before cleanup
            save_player_state(existing_player_id);

            // Notify nearby players of despawn (re-fetch in case save_player_state invalidated pointer)
            existing_player = players_->get_player(existing_player_id);
            auto nearby = existing_player
                ? players_->get_players_who_can_see(existing_player->current_map, existing_player->pos)
                : std::vector<player_id>{};
            auto despawn_msg = network::make_entity_despawn(0, existing_player_id.value);

            for (auto other_id : nearby) {
                if (other_id == existing_player_id) continue;
                auto* other = players_->get_player(other_id);
                if (!other || other->connection.value == 0) continue;
                auto* other_conn = ws_server_->get_connection(other->connection);
                if (other_conn && other_conn->is_open()) {
                    other_conn->send(despawn_msg);
                }
            }

            // Remove the stale player
            players_->remove_player(existing_player_id);

            // Clean up inventory
            if (inventory_) {
                inventory_->destroy_inventory(entity_id{existing_player_id.value});
            }

            // Disconnect old connection if it still exists
            if (old_connection.value != 0) {
                ws_server_->disconnect(old_connection,
                    "Disconnected: Another session logged in");
            }
        }
    }

    // Create player instance if player_system is available
    player_id live_player_id = char_id;
    if (players_) {
        // Build player creation info from loaded character
        player::player_create_info create_info{
            .name = char_data.name,
            .account_name = "",  // Could look up account name if needed
            .sex = char_data.gender == 1 ? player::gender::male : player::gender::female,
            .profession = static_cast<player::player_class>(char_data.class_type),
            .faction = static_cast<hb::faction>(char_data.nation),
            .initial_stats = player::base_stats{
                .strength = char_data.strength,
                .dexterity = char_data.dexterity,
                .vitality = char_data.vitality,
                .intelligence = char_data.intelligence,
                .magic = char_data.magic,
                .charisma = char_data.charisma
            }
        };

        auto create_result = players_->create_player(create_info);
        if (create_result.is_err()) {
            LOG_ERROR(bridge, "Failed to create player instance: {}", create_result.error());
            auto response = network::make_enter_game_response(msg.seq, false, nullptr, "internal_error");
            conn->send(response);
            return;
        }

        live_player_id = create_result.value();
        auto* player = players_->get_player(live_player_id);
        if (player) {
            // Set database character ID for persistence
            player->character_id = char_id;
            // Set account for duplicate session detection
            player->account = conn->account();

            // Apply loaded stats
            player->experience.level = static_cast<uint8_t>(char_data.level);
            player->experience.experience = char_data.experience;
            player->experience.enemy_kill_count = char_data.enemy_kill_count;
            player->experience.contribution = char_data.contribution;
            player->hp = char_data.hp;
            player->mp = char_data.mp;
            player->sp = char_data.sp;
            player->hunger.level = static_cast<int8_t>(char_data.hunger_level);
            player->pk.count = char_data.pk_count;
            player->pk.points = char_data.pk_points;
            player->stats_pts.available = char_data.stat_points_available;
            player->hair_style = char_data.hair_style;
            player->hair_color = char_data.hair_color;
            player->skin_color = char_data.skin_color;
            player->underwear_color = char_data.underwear_color;

            // Deserialize and apply skills
            if (!char_data.skills_data.empty()) {
                player->skills = auth::deserialize_skills(char_data.skills_data);
                LOG_DEBUG(bridge, "Loaded skills for player {}", live_player_id.value);
            }

            // Deserialize and apply equipment
            if (!char_data.equipment_data.empty()) {
                player->equipment = auth::deserialize_equipment(char_data.equipment_data);
                LOG_DEBUG(bridge, "Loaded equipment for player {}", live_player_id.value);
            }

            // Create and populate inventory
            if (inventory_) {
                auto entity = entity_id{live_player_id.value};

                // Create inventory for this player
                inventory_->create_inventory(entity);

                // Deserialize inventory from DB
                if (!char_data.inventory_data.empty()) {
                    auto* inv = inventory_->get_inventory(entity);
                    if (inv) {
                        auth::deserialize_inventory(char_data.inventory_data, *inv);
                        LOG_DEBUG(bridge, "Loaded inventory for player {} ({} items)",
                            live_player_id.value, inv->used_slots());
                    }
                }

                // Create and populate bank
                inventory_->create_bank(entity);
                if (!char_data.bank_data.empty()) {
                    auto* bank = inventory_->get_bank(entity);
                    if (bank) {
                        auth::deserialize_inventory(char_data.bank_data, *bank);
                        LOG_DEBUG(bridge, "Loaded bank for player {} ({} items)",
                            live_player_id.value, bank->used_slots());
                    }
                }

                // Set gold from character data
                inventory_->add_gold(entity, char_data.gold);
                LOG_DEBUG(bridge, "Set gold for player {}: {}", live_player_id.value, char_data.gold);
            }

            // Deserialize and apply magic (spell knowledge)
            if (!char_data.magic_data.empty()) {
                auto spells = auth::deserialize_magic(char_data.magic_data);
                if (!spells.empty()) {
                    auto* magic_sys = subsystems().get<magic::magic_system>();
                    if (magic_sys) {
                        auto entity = hb::entity::entity{live_player_id.value, 0};
                        magic_sys->set_player_spells(entity, std::move(spells));
                        LOG_DEBUG(bridge, "Loaded magic data for player {}", live_player_id.value);
                    }
                }
            }

            // Deserialize and apply quest data
            if (!char_data.quest_data.empty()) {
                auto* quest_sys = subsystems().get<quest::quest_system>();
                if (quest_sys) {
                    quest_sys->register_player(live_player_id);
                    auto journal = auth::deserialize_quests(char_data.quest_data);
                    auto* player_journal = quest_sys->get_journal(live_player_id);
                    if (player_journal) {
                        *player_journal = std::move(journal);
                        LOG_DEBUG(bridge, "Loaded quest data for player {}", live_player_id.value);
                    }
                }
            }

            // Recalculate computed stats
            player->base.level_bonus = char_data.level;
            player->recalculate_stats();

            // Connect guild membership
            if (social_) {
                social_->register_player(live_player_id, char_data.name);
                social_->connect_guild_member(char_id, live_player_id, char_data.name);

                auto guild = social_->get_player_guild(live_player_id);
                if (guild.is_valid()) {
                    auto* g = social_->get_guild(guild);
                    if (g) {
                        player->guild_name = g->name;
                        auto* member = g->get_member(live_player_id);
                        if (member)
                            player->guild_rank = static_cast<uint8_t>(member->rank);
                    }
                }
            }

            // Set position if world system is available
            if (world_) {
                auto* target_map = world_->get_map_by_name(char_data.map_name);
                if (!target_map) {
                    // Try to get a default map (first loaded map)
                    world_->for_each_map([&target_map](map_id, world::map& m) {
                        if (!target_map) target_map = &m;
                    });
                    if (target_map) {
                        // Update map name to the actual map we're using
                        char_data.map_name = std::string(target_map->name());
                    }
                }
                if (target_map) {
                    world::position spawn_pos{char_data.pos_x, char_data.pos_y};

                    // Check for invalid/default position marker (-1, -1)
                    // This indicates the player should spawn at the map's initial point
                    if (char_data.pos_x == -1 || char_data.pos_y == -1) {
                        auto initial_pos = target_map->get_random_initial_point();
                        if (initial_pos) {
                            spawn_pos = *initial_pos;
                            // Update char_data so the client receives correct position
                            char_data.pos_x = spawn_pos.x;
                            char_data.pos_y = spawn_pos.y;
                            LOG_DEBUG(bridge, "Player {} using map initial point: ({}, {})",
                                live_player_id.value, spawn_pos.x, spawn_pos.y);
                        } else {
                            // Fallback to center of map if no initial points defined
                            spawn_pos = world::position{
                                static_cast<int16_t>(target_map->width() / 2),
                                static_cast<int16_t>(target_map->height() / 2)
                            };
                            char_data.pos_x = spawn_pos.x;
                            char_data.pos_y = spawn_pos.y;
                            LOG_WARN(bridge, "Map '{}' has no initial points, using center: ({}, {})",
                                target_map->name(), spawn_pos.x, spawn_pos.y);
                        }
                    }

                    players_->set_position(live_player_id, target_map->id(),
                        spawn_pos, world::direction::south);
                }
            }

            // Set visibility radii from client resolution
            player->visibility_radius_x = vis_radii.x;
            player->visibility_radius_y = vis_radii.y;

            // Bind connection to player
            players_->bind_connection(live_player_id, conn_id);

            LOG_DEBUG(bridge, "Player instance created: id={}, hp={}/{}, mp={}/{}, level={}",
                live_player_id.value, player->hp, player->computed.max_hp,
                player->mp, player->computed.max_mp, player->experience.level);
        }
    }

    // Update connection state
    conn->set_state(network::ws_connection_state::in_game);
    conn->set_player(live_player_id);

    // Register player in lookup map
    ws_server_->register_player(conn_id, live_player_id);

    // Register with admin system for command access
    if (admin_ && auth_) {
        // Get admin level from account
        auto auth_admin_level = auth_->get_admin_level(conn->account());

        // Convert auth::admin_level to admin::admin_level
        admin::admin_level cmd_level = admin::admin_level::player;
        switch (auth_admin_level) {
            case auth::admin_level::helper:
                cmd_level = admin::admin_level::helper;
                break;
            case auth::admin_level::gamemaster:
                cmd_level = admin::admin_level::game_master;
                break;
            case auth::admin_level::senior_gm:
                cmd_level = admin::admin_level::senior_gm;
                break;
            case auth::admin_level::administrator:
                cmd_level = admin::admin_level::admin;
                break;
            default:
                cmd_level = admin::admin_level::player;
                break;
        }

        admin_->register_admin(live_player_id, char_data.name, cmd_level);
    }

    LOG_INFO(bridge, "Player {} (character '{}') entering game from account {}",
        live_player_id.value, char_data.name, conn->account().value);

    // Build inventory data for network message
    std::vector<network::inventory_item_msg> inventory_list;
    int32_t player_gold = char_data.gold;
    if (inventory_) {
        auto entity = entity_id{live_player_id.value};
        auto* inv = inventory_->get_inventory(entity);
        if (inv) {
            for (int16_t i = 0; i < inv->capacity(); ++i) {
                const auto* slot = inv->get_slot(i);
                if (slot && !slot->is_empty()) {
                    inventory_list.push_back({
                        .slot = static_cast<uint8_t>(i),
                        .item_id = slot->item.value,
                        .name = "",  // TODO: Get from item registry
                        .count = slot->count
                    });
                }
            }
        }
        player_gold = static_cast<int32_t>(inventory_->get_gold(entity));
    }

    // Build equipment data for network message
    std::vector<network::equipment_item_msg> equipment_list;
    if (players_) {
        auto* player = players_->get_player(live_player_id);
        if (player) {
            for (size_t i = 0; i < player::equip_slot_count; ++i) {
                const auto& item = player->equipment.slots[i];
                if (!item.is_empty()) {
                    equipment_list.push_back({
                        .slot = static_cast<uint8_t>(i),
                        .item_id = item.id.value,
                        .name = "",  // TODO: Get from item registry
                        .durability = static_cast<int16_t>(item.durability),
                        .max_durability = static_cast<int16_t>(item.max_durability)
                    });
                }
            }
        }
    }

    // Build skills data for network message
    std::vector<std::pair<uint8_t, int16_t>> skills_list;
    if (players_) {
        auto* player = players_->get_player(live_player_id);
        if (player) {
            for (size_t i = 0; i < skill::max_skills; ++i) {
                const auto& sk = player->skills.skills[i];
                if (sk.level > 0) {
                    skills_list.emplace_back(static_cast<uint8_t>(i), sk.level);
                }
            }
        }
    }

    // Build spell data for network message
    std::vector<network::known_spell_msg> spells_list;
    {
        auto* magic_sys = subsystems().get<magic::magic_system>();
        if (magic_sys) {
            auto entity = hb::entity::entity{live_player_id.value, 0};
            const auto* known = magic_sys->get_player_spells(entity);
            if (known) {
                for (const auto& sk : *known) {
                    spells_list.push_back({
                        .spell_id = sk.spell.value,
                        .level = sk.level,
                        .total_casts = sk.total_casts
                    });
                }
            }
        }
    }

    // Build quest data for network message
    std::vector<network::active_quest_msg> quests_list;
    std::vector<uint16_t> completed_quest_ids;
    {
        auto* quest_sys = subsystems().get<quest::quest_system>();
        if (quest_sys) {
            const auto* journal = quest_sys->get_journal(live_player_id);
            if (journal) {
                for (const auto& q : journal->active_quests) {
                    network::active_quest_msg qm;
                    qm.quest_id = q.template_id.value;
                    qm.status = static_cast<uint8_t>(q.status);
                    for (const auto& obj : q.objectives) {
                        qm.objectives.push_back({
                            .id = obj.template_id,
                            .status = static_cast<uint8_t>(obj.status),
                            .current = obj.current_count,
                            .required = obj.required_count
                        });
                    }
                    quests_list.push_back(std::move(qm));
                }
                for (const auto& qid : journal->completed_quests) {
                    completed_quest_ids.push_back(qid.value);
                }
            }
        }
    }

    // Determine environment state for initial sync
    uint8_t env_hour = 12;
    uint8_t env_minute = 0;
    bool env_is_day = true;
    uint8_t env_weather = 0;

    if (scheduler_) {
        auto& clock = scheduler_->game_time();
        env_hour = static_cast<uint8_t>(clock.hour());
        env_minute = static_cast<uint8_t>(clock.minute());
        env_is_day = clock.is_day();
    }

    if (world_ && players_) {
        auto* plr = players_->get_player(live_player_id);
        if (plr) {
            auto* current_map = world_->get_map(plr->current_map);
            if (current_map) {
                if (current_map->config().is_fixed_day_mode) {
                    env_is_day = true;
                    env_weather = 0;
                } else {
                    env_weather = static_cast<uint8_t>(current_map->weather());
                }
            }
        }
    }

    // Build full game state message
    network::game_state_msg game_state{
        .character = network::character_data_msg{
            .id = live_player_id.value,
            .name = char_data.name,
            .level = char_data.level,
            .class_type = char_data.class_type,
            .nation = char_data.nation,
            .gender = char_data.gender,
            .map_name = char_data.map_name,
            .pos_x = char_data.pos_x,
            .pos_y = char_data.pos_y,
            .hp = char_data.hp,
            .hp_max = char_data.max_hp,
            .mp = char_data.mp,
            .mp_max = char_data.max_mp,
            .sp = char_data.sp,
            .sp_max = char_data.max_sp,
            .gold = char_data.gold,
            .str = char_data.strength,
            .dex = char_data.dexterity,
            .vit = char_data.vitality,
            .int_ = char_data.intelligence,
            .mag = char_data.magic,
            .cha = char_data.charisma,
            .hair_style = char_data.hair_style,
            .hair_color = char_data.hair_color,
            .skin_color = char_data.skin_color,
            .experience = char_data.experience,
            .pk_count = char_data.pk_count,
            .hunger_level = char_data.hunger_level
        },
        .inventory = inventory_list,
        .equipment = equipment_list,
        .skills = skills_list,
        .spells = spells_list,
        .quests = quests_list,
        .completed_quests = completed_quest_ids,
        .entities = build_visible_entities(live_player_id),
        .gold = player_gold,
        .time_hour = env_hour,
        .time_minute = env_minute,
        .is_day = env_is_day,
        .weather = env_weather
    };

    // Send combined enter game response with full game state
    auto response = network::make_enter_game_response(msg.seq, true, &game_state);
    conn->send(response);

    LOG_DEBUG(bridge, "Sent game state to player {}: {} visible entities",
        live_player_id.value, game_state.entities.size());

    // Send map teleporters
    if (world_ && players_) {
        auto* player = players_->get_player(live_player_id);
        if (player) {
            auto* current_map = world_->get_map(player->current_map);
            if (current_map) {
                const auto& teleports = current_map->get_all_teleports();

                network::map_teleporters_msg teleporters_msg;
                teleporters_msg.map_name = std::string(current_map->name());

                for (const auto& [pos, dest] : teleports) {
                    network::teleporter_info_msg tp_info{
                        .id = (static_cast<uint32_t>(pos.x) << 16) |
                              static_cast<uint32_t>(static_cast<uint16_t>(pos.y)),
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

                LOG_DEBUG(bridge, "Sent {} teleporters for map {} to player {}",
                    teleporters_msg.teleporters.size(), current_map->name(), live_player_id.value);
            }

            // Send visible ground items at player's position
            if (item_) {
                int rx = player->visibility_radius_x > 0 ? player->visibility_radius_x : 20;
                int ry = player->visibility_radius_y > 0 ? player->visibility_radius_y : 15;
                for (int16_t dx = static_cast<int16_t>(-rx); dx <= rx; ++dx) {
                    for (int16_t dy = static_cast<int16_t>(-ry); dy <= ry; ++dy) {
                        world::position tile_pos{
                            static_cast<int16_t>(player->pos.x + dx),
                            static_cast<int16_t>(player->pos.y + dy)
                        };

                        auto items = world_->get_ground_items(player->current_map, tile_pos);
                        for (auto ground_item : items) {
                            auto* itm = item_->get_item(ground_item);
                            if (!itm) continue;

                            network::ground_item_spawn_data spawn_data{
                                .item_id = ground_item.value,
                                .template_id = itm->template_id.value,
                                .item_name = itm->name,
                                .count = itm->count,
                                .x = tile_pos.x,
                                .y = tile_pos.y
                            };

                            conn->send(network::make_ground_item_spawn(spawn_data));
                        }
                    }
                }
            }
        }
    }

    // Notify nearby players of the new spawn
    if (players_ && ws_server_) {
        auto* player = players_->get_player(live_player_id);
        if (player) {
            auto nearby = players_->get_players_who_can_see(player->current_map, player->pos);

            // Build spawn message for this player
            auto spawn_entity = network::visible_entity_msg{
                .entity_id = live_player_id.value,
                .type = "player",
                .name = player->name,
                .x = player->pos.x,
                .y = player->pos.y,
                .hp_percent = static_cast<int16_t>(player->hp_percent() * 100),
                .direction = static_cast<int16_t>(player->facing)
            };
            auto spawn_msg = network::make_entity_spawn(0, spawn_entity);

            for (auto other_id : nearby) {
                if (other_id == live_player_id) continue;

                auto* other = players_->get_player(other_id);
                if (!other || other->connection.value == 0) continue;

                auto* other_conn = ws_server_->get_connection(other->connection);
                if (other_conn && other_conn->is_open()) {
                    other_conn->send(spawn_msg);
                }
            }

            LOG_DEBUG(bridge, "Notified {} nearby players of spawn for {}",
                nearby.size() > 0 ? nearby.size() - 1 : 0, player->name);
        }
    }
}

auto auth_handlers::build_visible_entities(player_id player_id)
    -> std::vector<network::visible_entity_msg>
{
    std::vector<network::visible_entity_msg> entities;

    if (!players_) return entities;

    auto* player = players_->get_player(player_id);
    if (!player) return entities;

    // Get players within this player's visibility radius
    auto nearby_players = players_->get_players_in_range(player_id,
        std::max(player->visibility_radius_x, player->visibility_radius_y));

    for (auto other_id : nearby_players) {
        if (other_id == player_id) continue;  // Skip self

        auto* other = players_->get_player(other_id);
        if (!other) continue;

        entities.push_back(network::visible_entity_msg{
            .entity_id = other->ecs_entity.id,
            .type = "player",
            .name = other->name,
            .x = other->pos.x,
            .y = other->pos.y,
            .hp_percent = static_cast<int16_t>(other->hp_percent() * 100),
            .direction = static_cast<int16_t>(other->facing)
        });
    }

    // Add nearby NPCs from npc_system
    if (npc_) {
        npc_->for_each_npc_on_map(player->current_map, [&](entity::entity id, const npc::npc& n) {
            // Skip dead NPCs
            if (n.ai_state.state == npc::ai_state::dead) return;

            // Check if NPC is within rectangular visibility
            if (std::abs(player->pos.x - n.pos.x) > player->visibility_radius_x
                || std::abs(player->pos.y - n.pos.y) > player->visibility_radius_y) return;

            entities.push_back(network::visible_entity_msg{
                .entity_id = id.id,
                .type = "npc",
                .name = n.name,
                .x = n.pos.x,
                .y = n.pos.y,
                .hp_percent = n.max_hp > 0 ? static_cast<int16_t>((n.hp * 100) / n.max_hp) : static_cast<int16_t>(100),
                .direction = static_cast<int16_t>(n.facing),
                .template_id = n.template_id.value,
                .level = n.level
            });
        });
    }

    return entities;
}

void auth_handlers::handle_ping(connection_id conn_id, const network::json_message& msg) {
    auto* conn = get_connection_or_error(conn_id, msg.seq);
    if (!conn) return;

    auto response = network::make_pong_response(msg.seq);
    conn->send(response);
}

void auth_handlers::send_error(connection_id conn_id, uint32_t seq,
                                std::string_view error_code, std::string_view message)
{
    if (!ws_server_) return;

    auto* conn = ws_server_->get_connection(conn_id);
    if (!conn) return;

    auto response = network::make_error_response(seq, error_code, message);
    conn->send(response);
}

auto auth_handlers::get_connection_or_error(connection_id conn_id, uint32_t seq)
    -> network::ws_connection*
{
    if (!ws_server_) {
        return nullptr;
    }

    auto* conn = ws_server_->get_connection(conn_id);
    if (!conn) {
        LOG_WARN(bridge, "Message from unknown connection {}", conn_id.value);
        return nullptr;
    }

    return conn;
}

auto auth_handlers::require_authenticated(connection_id conn_id, uint32_t seq)
    -> network::ws_connection*
{
    auto* conn = get_connection_or_error(conn_id, seq);
    if (!conn) return nullptr;

    if (conn->state() != network::ws_connection_state::authenticated &&
        conn->state() != network::ws_connection_state::in_game) {
        LOG_WARN(bridge, "Unauthenticated request from connection {}", conn_id.value);
        send_error(conn_id, seq, "not_authenticated", "You must be logged in to perform this action");
        return nullptr;
    }

    return conn;
}

void auth_handlers::save_player_state(player_id pid) {
    if (!players_ || !auth_) return;

    auto* player = players_->get_player(pid);
    if (!player) {
        LOG_WARN(bridge, "Cannot save player {}: not found", pid.value);
        return;
    }

    // Serialize skills and equipment
    auto skills_json = auth::serialize_skills(player->skills);
    auto equipment_json = auth::serialize_equipment(player->equipment);

    // Serialize inventory, bank, and get gold
    // Initialize with valid empty JSON arrays (PostgreSQL JSONB requires valid JSON, not empty strings)
    std::string inventory_json = "[]";
    std::string bank_json = "[]";
    int32_t player_gold = 0;

    if (inventory_) {
        auto entity = entity_id{pid.value};

        // Serialize inventory
        auto* inv = inventory_->get_inventory(entity);
        if (inv) {
            inventory_json = auth::serialize_inventory(*inv);
            LOG_DEBUG(bridge, "Saving inventory for player {} ({} items)",
                pid.value, inv->used_slots());
        }

        // Serialize bank
        auto* bank = inventory_->get_bank(entity);
        if (bank) {
            bank_json = auth::serialize_inventory(*bank);
            LOG_DEBUG(bridge, "Saving bank for player {} ({} items)",
                pid.value, bank->used_slots());
        }

        // Get gold
        player_gold = static_cast<int32_t>(inventory_->get_gold(entity));
    }

    // Serialize magic (spell knowledge)
    std::string magic_json = "[]";
    {
        auto* magic_sys = subsystems().get<magic::magic_system>();
        if (magic_sys) {
            const auto* spells = magic_sys->get_player_spells(player->ecs_entity);
            if (spells) {
                magic_json = auth::serialize_magic(*spells);
                LOG_DEBUG(bridge, "Saving magic data for player {} ({} spells)",
                    pid.value, spells->size());
            }
        }
    }

    // Serialize quest data
    std::string quest_json = "[]";
    {
        auto* quest_sys = subsystems().get<quest::quest_system>();
        if (quest_sys) {
            const auto* journal = quest_sys->get_journal(pid);
            if (journal) {
                quest_json = auth::serialize_quests(*journal);
                LOG_DEBUG(bridge, "Saving quest data for player {} ({} active, {} completed)",
                    pid.value, journal->active_quests.size(), journal->completed_quests.size());
            }
        }
    }

    // Build character data from current player state
    // Use the database character_id, not the runtime player_id
    auth::character_full_data data{
        .id = player->character_id,
        .account = {},  // Not needed for save
        .name = player->name,
        .level = static_cast<int16_t>(player->experience.level),
        .class_type = static_cast<int16_t>(player->profession),
        .nation = static_cast<int16_t>(player->faction),
        .gender = static_cast<int16_t>(player->sex),
        .map_name = "",  // Will be filled from map
        .pos_x = player->pos.x,
        .pos_y = player->pos.y,
        .experience = player->experience.experience,
        .hp = player->hp,
        .max_hp = player->computed.max_hp,
        .mp = player->mp,
        .max_mp = player->computed.max_mp,
        .sp = player->sp,
        .max_sp = player->computed.max_sp,
        .gold = player_gold,
        .strength = player->base.strength,
        .dexterity = player->base.dexterity,
        .vitality = player->base.vitality,
        .intelligence = player->base.intelligence,
        .magic = player->base.magic,
        .charisma = player->base.charisma,
        .hair_style = player->hair_style,
        .hair_color = player->hair_color,
        .skin_color = player->skin_color,
        .underwear_color = player->underwear_color,
        .pk_count = player->pk.count,
        .pk_points = player->pk.points,
        .hunger_level = player->hunger.level,
        .enemy_kill_count = player->experience.enemy_kill_count,
        .contribution = player->experience.contribution,
        .stat_points_available = player->stats_pts.available,
        .luck = 0,  // TODO: add luck to player struct when luck system is implemented
        .reward_gold = 0,  // TODO: add reward_gold to player struct when bounty system is implemented
        .skills_data = skills_json,
        .inventory_data = inventory_json,
        .equipment_data = equipment_json,
        .bank_data = bank_json,
        .magic_data = magic_json,
        .quest_data = quest_json
    };

    // Get map name
    if (world_ && player->current_map.value != 0) {
        auto* map = world_->get_map(player->current_map);
        if (map) {
            data.map_name = std::string(map->name());
        }
    }
    if (data.map_name.empty()) {
        data.map_name = "default";
    }

    auto save_result = auth_->save_character(data);
    if (save_result.is_err()) {
        LOG_ERROR(bridge, "Failed to save player {}: {}", pid.value,
            auth::to_string(save_result.error()));
    } else {
        LOG_DEBUG(bridge, "Saved player {} at ({}, {}) on {}",
            pid.value, data.pos_x, data.pos_y, data.map_name);
    }
}

void auth_handlers::handle_player_disconnect(connection_id conn_id) {
    if (!ws_server_) return;

    auto* conn = ws_server_->get_connection(conn_id);
    if (!conn) return;

    // Check if player was in game by looking at the player_id
    // Note: We can't check conn->state() == in_game because the websocket layer
    // already set the state to 'disconnected' before calling this handler
    auto pid = conn->player();
    if (!pid.is_valid()) return;

    LOG_INFO(bridge, "Player {} disconnecting, saving state...", pid.value);

    // Save player state
    save_player_state(pid);

    // Disconnect from guild (before removing player)
    if (social_) {
        auto* player = players_ ? players_->get_player(pid) : nullptr;
        if (player) {
            social_->disconnect_guild_member(pid, player->character_id);
        }
        social_->unregister_player(pid);
    }

    // Notify nearby players of despawn
    if (players_ && ws_server_) {
        auto* player = players_->get_player(pid);
        if (player) {
            auto nearby = players_->get_players_who_can_see(player->current_map, player->pos);

            auto despawn_msg = network::make_entity_despawn(0, pid.value);

            for (auto other_id : nearby) {
                if (other_id == pid) continue;

                auto* other = players_->get_player(other_id);
                if (!other || other->connection.value == 0) continue;

                auto* other_conn = ws_server_->get_connection(other->connection);
                if (other_conn && other_conn->is_open()) {
                    other_conn->send(despawn_msg);
                }
            }
        }

        // Remove player from system
        players_->remove_player(pid);
    }

    // Clean up inventory
    if (inventory_) {
        inventory_->destroy_inventory(entity_id{pid.value});
    }

    // Unregister from admin system
    if (admin_) {
        admin_->unregister_admin(pid);
    }

    LOG_INFO(bridge, "Player {} cleanup complete", pid.value);
}

void auth_handlers::save_player(player_id pid) {
    save_player_state(pid);
}

auto auth_handlers::save_all_players() -> size_t {
    if (!players_) {
        LOG_WARN(bridge, "Cannot save all players: player_system not available");
        return 0;
    }

    size_t saved_count = 0;
    size_t total_count = 0;

    players_->for_each_player([this, &saved_count, &total_count](player_id pid, const player::player&) {
        ++total_count;
        // Only save players that have a valid connection (are actually in-game)
        if (players_->get_player(pid)->connection.value != 0) {
            save_player_state(pid);
            ++saved_count;
        }
    });

    if (saved_count > 0) {
        LOG_INFO(bridge, "Periodic save completed: {}/{} players saved", saved_count, total_count);
    }

    return saved_count;
}

}  // namespace hb::bridge
