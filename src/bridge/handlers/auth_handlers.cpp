// auth_handlers.cpp
// Message handlers for authentication protocol implementation

// Include platform header first to define NOMINMAX before Windows headers
#include "platform/platform.h"

#include "bridge/handlers/auth_handlers.h"
#include "network/websocket_server.h"
#include "auth/auth_system.h"
#include "player/player_system.h"
#include "world/world_subsystem.h"
#include "world/map.h"
#include "core/logger.h"

namespace hb::bridge {

auth_handlers::auth_handlers() = default;
auth_handlers::~auth_handlers() = default;

void auth_handlers::initialize(network::websocket_server* ws_server,
                                auth::auth_system* auth,
                                player::player_system* players,
                                world::world_subsystem* world) {
    ws_server_ = ws_server;
    auth_ = auth;
    players_ = players;
    world_ = world;
    LOG_INFO(bridge, "Auth handlers initialized (players: {}, world: {})",
        players_ != nullptr ? "yes" : "no",
        world_ != nullptr ? "yes" : "no");
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

    // Attempt authentication
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

        // Notify nearby players of despawn
        if (players_ && ws_server_) {
            auto* player = players_->get_player(pid);
            if (player) {
                constexpr int visibility_radius = 20;
                auto nearby = players_->get_players_in_range(pid, visibility_radius);

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

        auto response = network::make_create_character_response(msg.seq, false, std::nullopt, error_str);
        conn->send(response);
        return;
    }

    auto char_id = create_result.value();

    LOG_INFO(bridge, "Character '{}' created with id {} for account {}",
        data.name, char_id.value, conn->account().value);

    auto response = network::make_create_character_response(msg.seq, true, char_id.value);
    conn->send(response);
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

    // Create player instance if player_system is available
    player_id live_player_id = char_id;
    if (players_) {
        // Build player creation info from loaded character
        player::player_create_info create_info{
            .name = char_data.name,
            .account_name = "",  // Could look up account name if needed
            .sex = char_data.gender == 0 ? player::gender::male : player::gender::female,
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
            // Apply loaded stats
            player->experience.level = static_cast<uint8_t>(char_data.level);
            player->experience.experience = char_data.experience;
            player->hp = char_data.hp;
            player->mp = char_data.mp;
            player->sp = char_data.sp;
            player->hunger.level = static_cast<int8_t>(char_data.hunger_level);
            player->pk.count = char_data.pk_count;

            // Recalculate computed stats
            player->base.level_bonus = char_data.level;
            player->recalculate_stats();

            // Set position if world system is available
            if (world_) {
                auto* target_map = world_->get_map_by_name(char_data.map_name);
                if (!target_map) {
                    // Try to get a default map
                    world_->for_each_map([&target_map](map_id, world::map& m) {
                        if (!target_map) target_map = &m;
                    });
                }
                if (target_map) {
                    world::position spawn_pos{char_data.pos_x, char_data.pos_y};
                    players_->set_position(live_player_id, target_map->id(),
                        spawn_pos, world::direction::south);
                }
            }

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

    LOG_INFO(bridge, "Player {} (character '{}') entering game from account {}",
        live_player_id.value, char_data.name, conn->account().value);

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
        .inventory = {},  // TODO: Load from database
        .equipment = {},  // TODO: Load from database
        .skills = {},     // TODO: Load from database
        .entities = build_visible_entities(live_player_id),
        .gold = char_data.gold
    };

    // Send combined enter game response with full game state
    auto response = network::make_enter_game_response(msg.seq, true, &game_state);
    conn->send(response);

    LOG_DEBUG(bridge, "Sent game state to player {}: {} visible entities",
        live_player_id.value, game_state.entities.size());
}

auto auth_handlers::build_visible_entities(player_id player_id)
    -> std::vector<network::visible_entity_msg>
{
    std::vector<network::visible_entity_msg> entities;

    if (!players_) return entities;

    auto* player = players_->get_player(player_id);
    if (!player) return entities;

    // Get players in range (default visibility radius of 20 tiles)
    constexpr int visibility_radius = 20;
    auto nearby_players = players_->get_players_in_range(player_id, visibility_radius);

    for (auto other_id : nearby_players) {
        if (other_id == player_id) continue;  // Skip self

        auto* other = players_->get_player(other_id);
        if (!other) continue;

        entities.push_back(network::visible_entity_msg{
            .entity_id = other_id.value,
            .type = "player",
            .name = other->name,
            .x = other->pos.x,
            .y = other->pos.y,
            .hp_percent = static_cast<int16_t>(other->hp_percent() * 100),
            .direction = static_cast<int16_t>(other->facing)
        });
    }

    // TODO: Add nearby NPCs from npc_system

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

    // Build character data from current player state
    auth::character_full_data data{
        .id = pid,
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
        .gold = 0,  // TODO: Get from inventory system
        .strength = player->base.strength,
        .dexterity = player->base.dexterity,
        .vitality = player->base.vitality,
        .intelligence = player->base.intelligence,
        .magic = player->base.magic,
        .charisma = player->base.charisma,
        .hair_style = 0,  // Not tracked in player struct currently
        .hair_color = 0,
        .skin_color = 0,
        .underwear_color = 0,
        .pk_count = player->pk.count,
        .hunger_level = player->hunger.level
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

    // Only handle if player was in game
    if (conn->state() != network::ws_connection_state::in_game) {
        return;
    }

    auto pid = conn->player();
    if (pid.value == 0) return;

    LOG_INFO(bridge, "Player {} disconnecting, saving state...", pid.value);

    // Save player state
    save_player_state(pid);

    // Notify nearby players of despawn
    if (players_ && ws_server_) {
        auto* player = players_->get_player(pid);
        if (player) {
            constexpr int visibility_radius = 20;
            auto nearby = players_->get_players_in_range(pid, visibility_radius);

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

    LOG_INFO(bridge, "Player {} cleanup complete", pid.value);
}

}  // namespace hb::bridge
