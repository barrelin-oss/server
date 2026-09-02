// auth_handlers.cpp
// Message handlers for authentication protocol implementation

// Include platform header first to define NOMINMAX before Windows headers
#include "platform/platform.h"

#include "bridge/handlers/auth_handlers.h"
#include "bridge/handlers/broadcast_util.h"
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
#include "skill/skill_system.h"
#include "scheduler/scheduler.h"
#include "core/logger.h"
#include "core/subsystem.h"
#include "perf/perf_stats.h"
#include "war/war_persistence.h"
#include "bridge/handlers/entity_builders.h"
#include "effect/effect_system.h"
#include "registry/item_registry.h"
#include "item/item_serialization.h"

namespace hb::bridge
{

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
                               scheduler* sched,
                               war::war_persistence* war_persistence,
                               effect::effect_system* effects,
                               item_registry* item_reg)
{
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
    war_persistence_ = war_persistence;
    effects_ = effects;
    item_registry_ = item_reg;
    LOG_INFO(
        bridge,
        "Auth handlers initialized (players: {}, world: {}, inventory: {}, admin: {}, npc: {}, item: {}, social: {})",
        players_ != nullptr ? "yes" : "no",
        world_ != nullptr ? "yes" : "no",
        inventory_ != nullptr ? "yes" : "no",
        admin_ != nullptr ? "yes" : "no",
        npc_ != nullptr ? "yes" : "no",
        item_ != nullptr ? "yes" : "no",
        social_ != nullptr ? "yes" : "no");
}

bool auth_handlers::handle_message(connection_id conn_id, const network::json_message& msg)
{
    switch (msg.type)
    {
    case network::json_message_type::login_request:
        handle_login(conn_id, msg);
        return true;
    case network::json_message_type::logout_request:
        handle_logout(conn_id, msg);
        return true;
    case network::json_message_type::create_account_request:
        handle_create_account(conn_id, msg);
        return true;
    case network::json_message_type::get_characters_request:
        handle_get_characters(conn_id, msg);
        return true;
    case network::json_message_type::create_character_request:
        handle_create_character(conn_id, msg);
        return true;
    case network::json_message_type::delete_character_request:
        handle_delete_character(conn_id, msg);
        return true;
    case network::json_message_type::enter_game_request:
        handle_enter_game(conn_id, msg);
        return true;
    case network::json_message_type::ping:
        handle_ping(conn_id, msg);
        return true;
    case network::json_message_type::enter_admin_mode_request:
        handle_enter_admin_mode(conn_id, msg);
        return true;
    default:
        return false;
    }
}

void auth_handlers::handle_login(connection_id conn_id, const network::json_message& msg)
{
    auto* conn = get_connection_or_error(conn_id, msg.seq);
    if (!conn)
        return;

    // Parse request data
    auto data_result = network::login_request_data::from_json(msg.data);
    if (data_result.is_err())
    {
        send_error(conn_id, msg.seq, "invalid_request", data_result.error());
        return;
    }

    auto& data = data_result.value();

    LOG_DEBUG(bridge, "Login request from {} for user '{}'", conn->remote_address(), data.username);

    // Check IP ban
    if (admin_ && admin_->is_ip_banned(conn->remote_address()))
    {
        LOG_INFO(bridge, "Login blocked for '{}': IP {} is banned", data.username, conn->remote_address());
        auto response = network::make_login_response(msg.seq, false, std::nullopt, "ip_banned");
        conn->send(response);
        return;
    }

    // Check maintenance mode (allow admins through)
    if (auth_->is_maintenance_mode())
    {
        // Try to look up account to check admin status
        auto acct_result = auth_->get_account_by_username(data.username);
        bool is_admin = acct_result.is_ok() && acct_result.value().admin >= auth::admin_level::gamemaster;
        if (!is_admin)
        {
            LOG_INFO(bridge, "Login blocked for '{}': server in maintenance mode", data.username);
            auto response = network::make_login_response(
                msg.seq, false, std::nullopt, std::string("maintenance:") + auth_->maintenance_message());
            conn->send(response);
            return;
        }
    }

    // Forum auth path
    if (auth_->forum_auth_enabled())
    {
        if (!data.forum_token.empty())
        {
            // Token-based auto-login
            auto forum_result = auth_->authenticate_forum_token(data.forum_token, conn->remote_address());

            if (forum_result.is_err())
            {
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
            conn->set_username(data.username);

            LOG_INFO(bridge, "User '{}' logged in via forum token from {}", data.username, conn->remote_address());

            auto response = network::make_login_response(msg.seq, true, result.session.token);
            conn->send(response);
            return;
        }

        // Password-based forum login
        auto forum_result = auth_->authenticate_forum(data.username, data.password, conn->remote_address());

        if (forum_result.is_err())
        {
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
        conn->set_username(data.username);

        LOG_INFO(bridge, "User '{}' logged in via forum from {}", data.username, conn->remote_address());

        auto response =
            network::make_login_response(msg.seq, true, result.session.token, std::nullopt, result.forum_token);
        conn->send(response);
        return;
    }

    // Local auth path (default)
    auto auth_result = auth_->authenticate(data.username, data.password, conn->remote_address());

    if (auth_result.is_err())
    {
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
    conn->set_username(data.username);

    LOG_INFO(bridge, "User '{}' logged in from {}", data.username, conn->remote_address());

    // Send success response
    auto response = network::make_login_response(msg.seq, true, session.token);
    conn->send(response);
}

void auth_handlers::handle_logout(connection_id conn_id, const network::json_message& msg)
{
    auto* conn = require_authenticated(conn_id, msg.seq);
    if (!conn)
        return;

    // If player was in game, save and clean up
    if (conn->state() == network::ws_connection_state::in_game && conn->player().value != 0)
    {
        auto pid = conn->player();
        LOG_INFO(bridge, "Player {} logging out, saving state...", pid.value);

        // Save player state
        save_player_state(pid);

        // Disconnect from guild and friends
        if (social_ && players_)
        {
            auto* player = players_->get_player(pid);
            if (player)
            {
                // Notify online friends before disconnecting
                auto* friends = social_->get_friends(player->character_id);
                if (friends && ws_server_)
                {
                    for (const auto& f : *friends)
                    {
                        if (f.is_online())
                        {
                            auto* friend_conn = ws_server_->get_connection_by_player(f.runtime_id);
                            if (friend_conn)
                            {
                                friend_conn->send(network::make_friend_offline_notification(player->name));
                            }
                        }
                    }
                }
                social_->disconnect_friend(pid, player->character_id);
                social_->disconnect_guild_member(pid, player->character_id);
            }
            social_->unregister_player(pid);
        }

        // Notify nearby players of despawn
        if (players_ && ws_server_)
        {
            auto* player = players_->get_player(pid);
            if (player)
            {
                auto despawn_msg = network::make_entity_despawn(0, player->ecs_entity.id);
                bridge::broadcast_to_visible(players_, ws_server_, player->current_map, player->pos, despawn_msg, pid);
            }

            // Remove player from system
            players_->remove_player(pid);
        }

        // Destroy item instances before inventory (items_ map must be cleaned up)
        destroy_player_items(entity_id{pid.value});

        // Clean up inventory
        if (inventory_)
        {
            inventory_->destroy_inventory(entity_id{pid.value});
        }
    }

    // Invalidate session
    if (!conn->session_token().empty())
    {
        auth_->invalidate_session(conn->session_token());
    }

    // Reset connection state
    conn->set_state(network::ws_connection_state::connected);
    conn->set_account(account_id{});
    conn->set_player(player_id{});
    conn->set_session_token("");
    conn->set_username("");

    LOG_INFO(bridge, "Connection {} logged out", conn_id.value);

    auto response = network::make_logout_response(msg.seq, true);
    conn->send(response);
}

void auth_handlers::handle_create_account(connection_id conn_id, const network::json_message& msg)
{
    auto* conn = get_connection_or_error(conn_id, msg.seq);
    if (!conn)
        return;

    // Parse request data
    auto data_result = network::create_account_request_data::from_json(msg.data);
    if (data_result.is_err())
    {
        send_error(conn_id, msg.seq, "invalid_request", data_result.error());
        return;
    }

    auto& data = data_result.value();

    LOG_DEBUG(bridge, "Account creation request for '{}'", data.username);

    // Check IP ban
    if (admin_ && admin_->is_ip_banned(conn->remote_address()))
    {
        LOG_INFO(bridge, "Account creation blocked for '{}': IP {} is banned", data.username, conn->remote_address());
        auto response = network::make_create_account_response(msg.seq, false, std::nullopt, "ip_banned");
        conn->send(response);
        return;
    }

    // Attempt to create account (with IP for rate limiting)
    auto create_result = auth_->create_account(data.username, data.password, conn->remote_address());

    if (create_result.is_err())
    {
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

void auth_handlers::handle_get_characters(connection_id conn_id, const network::json_message& msg)
{
    auto* conn = require_authenticated(conn_id, msg.seq);
    if (!conn)
        return;

    auto chars_result = auth_->get_characters(conn->account());

    if (chars_result.is_err())
    {
        auto error = chars_result.error();
        std::string error_str(auth::to_string(error));

        LOG_WARN(bridge, "Failed to get characters for account {}: {}", conn->account().value, error_str);

        send_error(conn_id, msg.seq, error_str, "Failed to retrieve characters");
        return;
    }

    auto& characters = chars_result.value();

    LOG_DEBUG(bridge, "Sending {} characters for account {}", characters.size(), conn->account().value);

    auto response = network::make_get_characters_response(msg.seq, characters);
    conn->send(response);
}

void auth_handlers::handle_create_character(connection_id conn_id, const network::json_message& msg)
{
    auto* conn = require_authenticated(conn_id, msg.seq);
    if (!conn)
        return;

    // Parse request data
    auto data_result = network::create_character_request_data::from_json(msg.data);
    if (data_result.is_err())
    {
        send_error(conn_id, msg.seq, "invalid_request", data_result.error());
        return;
    }

    auto& data = data_result.value();
    auto create_info = data.to_create_info();

    LOG_DEBUG(bridge, "Character creation request for '{}' by account {}", data.name, conn->account().value);

    // Attempt to create character
    auto create_result = auth_->create_character(conn->account(), create_info);

    if (create_result.is_err())
    {
        auto error = create_result.error();
        std::string error_str(auth::to_string(error));

        LOG_INFO(bridge, "Character creation failed for '{}': {}", data.name, error_str);

        auto response = network::make_create_character_response(msg.seq, false, std::nullopt, error_str);
        conn->send(response);
        return;
    }

    auto char_id = create_result.value();

    LOG_INFO(
        bridge, "Character '{}' created with id {} for account {}", data.name, char_id.value, conn->account().value);

    // Send success response
    auto response = network::make_create_character_response(msg.seq, true, char_id.value);
    conn->send(response);

    // Re-send the full character list so the client updates immediately
    auto chars_result = auth_->get_characters(conn->account());
    if (chars_result.is_ok())
    {
        auto list_response = network::make_get_characters_response(0, chars_result.value());
        conn->send(list_response);
    }
}

void auth_handlers::handle_delete_character(connection_id conn_id, const network::json_message& msg)
{
    auto* conn = require_authenticated(conn_id, msg.seq);
    if (!conn)
        return;

    // Parse request data
    auto data_result = network::delete_character_request_data::from_json(msg.data);
    if (data_result.is_err())
    {
        send_error(conn_id, msg.seq, "invalid_request", data_result.error());
        return;
    }

    auto& data = data_result.value();
    auto char_id = player_id{data.character_id};

    LOG_DEBUG(bridge, "Character deletion request for id {} by account {}", char_id.value, conn->account().value);

    // Attempt to delete character
    auto delete_result = auth_->delete_character(conn->account(), char_id);

    if (delete_result.is_err())
    {
        auto error = delete_result.error();
        std::string error_str(auth::to_string(error));

        LOG_INFO(bridge, "Character deletion failed for id {}: {}", char_id.value, error_str);

        auto response = network::make_delete_character_response(msg.seq, false, error_str);
        conn->send(response);
        return;
    }

    LOG_INFO(bridge, "Character id {} deleted for account {}", char_id.value, conn->account().value);

    auto response = network::make_delete_character_response(msg.seq, true);
    conn->send(response);

    // Re-send the full character list so the client updates immediately
    auto chars_result = auth_->get_characters(conn->account());
    if (chars_result.is_ok())
    {
        auto list_response = network::make_get_characters_response(0, chars_result.value());
        conn->send(list_response);
    }
}

void auth_handlers::handle_enter_game(connection_id conn_id, const network::json_message& msg)
{
    auto* perf = subsystems().get<perf::perf_stats_system>();
    PERF_TIMER(perf, perf::metric_category::player_save);

    auto* conn = require_authenticated(conn_id, msg.seq);
    if (!conn)
        return;

    // Parse request data
    auto data_result = network::enter_game_request_data::from_json(msg.data);
    if (data_result.is_err())
    {
        send_error(conn_id, msg.seq, "invalid_request", data_result.error());
        return;
    }

    auto& data = data_result.value();
    auto char_id = player_id{data.character_id};

    // Calculate initial visibility radii from client viewport
    auto vis_radii = network::calculate_visibility_radius(data.screen_width, data.screen_height);

    LOG_DEBUG(bridge, "Enter game request for character {} by account {}", char_id.value, conn->account().value);

    // Load full character data with ownership verification
    auto char_result = auth_->load_character_full(char_id, conn->account());
    if (char_result.is_err())
    {
        auto error = char_result.error();
        std::string error_str(auth::to_string(error));

        LOG_INFO(bridge, "Enter game failed for character {}: {}", char_id.value, error_str);

        auto response = network::make_enter_game_response(msg.seq, false, nullptr, error_str);
        conn->send(response);
        return;
    }

    auto& char_data = char_result.value();

    // Check if this account already has a character in game (stale session not cleaned up)
    if (players_)
    {
        player_id existing_player_id{0};
        connection_id old_connection{0};

        // Find any player belonging to this account
        players_->for_each_player(
            [&](player_id pid, const player::player& p)
            {
                if (p.account == conn->account())
                {
                    existing_player_id = pid;
                    old_connection = p.connection;
                }
            });

        if (existing_player_id.value != 0)
        {
            auto* existing_player = players_->get_player(existing_player_id);
            std::string existing_name = existing_player ? existing_player->name : "unknown";

            if (!data.force_disconnect)
            {
                // Account already has a character in game from a previous session
                LOG_INFO(bridge,
                         "Account {} already has character '{}' in game (stale session), rejecting new entry",
                         conn->account().value,
                         existing_name);

                auto response = network::make_enter_game_response(msg.seq, false, nullptr, "account_already_in_game");
                conn->send(response);
                return;
            }

            // Force disconnect - clean up the stale player
            LOG_INFO(bridge,
                     "Force disconnecting stale session for account {} (character '{}')",
                     conn->account().value,
                     existing_name);

            // Save state before cleanup
            save_player_state(existing_player_id);

            // Notify nearby players of despawn (re-fetch in case save_player_state invalidated pointer)
            existing_player = players_->get_player(existing_player_id);
            if (existing_player)
            {
                auto despawn_msg = network::make_entity_despawn(0, existing_player->ecs_entity.id);
                bridge::broadcast_to_visible(
                    players_, ws_server_, existing_player->current_map, existing_player->pos, despawn_msg, existing_player_id);
            }

            // Remove the stale player
            players_->remove_player(existing_player_id);

            // Destroy item instances before inventory
            destroy_player_items(entity_id{existing_player_id.value});

            // Clean up inventory
            if (inventory_)
            {
                inventory_->destroy_inventory(entity_id{existing_player_id.value});
            }

            // Disconnect old connection if it still exists
            if (old_connection.value != 0)
            {
                ws_server_->disconnect(old_connection, "Disconnected: Another session logged in");
            }
        }
    }

    // Create player instance if player_system is available
    player_id live_player_id = char_id;
    bool was_dead_on_login = false;
    if (players_)
    {
        // Build player creation info from loaded character
        player::player_create_info create_info{
            .name = char_data.name,
            .account_name = "", // Could look up account name if needed
            .sex = char_data.gender == 1 ? player::gender::male : player::gender::female,
            .profession = static_cast<player::player_class>(char_data.class_type),
            .faction = static_cast<hb::faction>(char_data.nation),
            .initial_stats = player::base_stats{.strength = char_data.strength,
                                                .dexterity = char_data.dexterity,
                                                .vitality = char_data.vitality,
                                                .intelligence = char_data.intelligence,
                                                .magic = char_data.magic,
                                                .charisma = char_data.charisma}};

        auto create_result = players_->create_player(create_info);
        if (create_result.is_err())
        {
            LOG_ERROR(bridge, "Failed to create player instance: {}", create_result.error());
            auto response = network::make_enter_game_response(msg.seq, false, nullptr, "internal_error");
            conn->send(response);
            return;
        }

        live_player_id = create_result.value();
        auto* player = players_->get_player(live_player_id);
        if (player)
        {
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
            if (!char_data.skills_data.empty())
            {
                player->skills = auth::deserialize_skills(char_data.skills_data);
                LOG_DEBUG(bridge, "Loaded skills for player {}", live_player_id.value);
            }

            // Register skills with skill_system so record_skill_use/set_skill_level work
            auto* skill_sys = subsystems().get<skill::skill_system>();
            if (skill_sys)
            {
                skill_sys->register_player(live_player_id);
                auto* ps = skill_sys->get_player_skills(live_player_id);
                if (ps)
                {
                    *ps = player->skills;
                }
            }

            // Create inventory and bank containers
            if (inventory_)
            {
                auto entity = entity_id{live_player_id.value};
                inventory_->create_inventory(entity);
                inventory_->create_bank(entity);

                // Load items from DB and restore into item_system + containers
                auto item_rows = auth_->load_items(char_data.id);
                auto* inv = inventory_->get_inventory(entity);
                auto* bank = inventory_->get_bank(entity);

                for (const auto& row : item_rows)
                {
                    // Restore item in item_system with its persistent DB ID
                    auto restore_result = item_->restore_item(
                        item_id{row.id},
                        {.template_id = item_id{row.template_id},
                         .count = row.count,
                         .owner = entity,
                         .full_durability = false,
                         .attribute = row.to_attribute()});

                    if (!restore_result.is_ok())
                    {
                        LOG_WARN(bridge, "Failed to restore item {} for player {}: {}",
                                 row.id, live_player_id.value, restore_result.error());
                        continue;
                    }
                    auto restored_id = restore_result.value();

                    // Override per-instance fields from DB
                    if (auto* itm = item_->get_item(restored_id))
                    {
                        itm->durability = row.durability;
                        itm->max_durability = row.max_durability;
                        itm->color = row.color;
                        itm->bound_to = row.bound_to
                            ? std::optional<player_id>(player_id{static_cast<uint32_t>(*row.bound_to)})
                            : std::nullopt;
                    }

                    // Place into correct container
                    switch (row.location)
                    {
                    case auth::item_location::inventory:
                    {
                        if (inv)
                        {
                            auto* entry = inv->add_item(restored_id, static_cast<int16_t>(row.count), row.pos_x, row.pos_y);
                            if (entry)
                            {
                                entry->z_order = row.z_order;
                            }
                        }
                        break;
                    }
                    case auth::item_location::bank:
                    {
                        if (bank)
                        {
                            // Use bank_page/bank_slot if available, otherwise fall back to flat slot
                            int16_t page = row.bank_page.value_or(
                                static_cast<int16_t>(row.slot / bank->slots_per_page()));
                            int16_t slot_in_page = row.bank_slot.value_or(
                                static_cast<int16_t>(row.slot % bank->slots_per_page()));
                            bank->set_slot(page, slot_in_page, restored_id);
                        }
                        break;
                    }
                    case auth::item_location::mail:
                        break; // Mail system handles separately
                    }
                }

                // Load equipment from character_equipment table
                auto equip_rows = auth_->load_equipment(char_data.id);
                for (const auto& er : equip_rows)
                {
                    auto eq_slot = static_cast<player::equip_slot>(er.slot);
                    if (static_cast<size_t>(eq_slot) < player::equip_slot_count)
                    {
                        player->equipment.equip(eq_slot, item_id{er.item_id});
                    }
                }

                // Set next_z_order from the maximum z_order loaded
                if (inv && inv->count() > 0)
                {
                    int32_t max_z = 0;
                    for (const auto& entry : inv->items())
                    {
                        if (entry.z_order > max_z)
                            max_z = entry.z_order;
                    }
                    inv->set_next_z_order(max_z + 1);
                }

                LOG_DEBUG(bridge, "Restored {} items for player {}", item_rows.size(), live_player_id.value);

                // Set gold from character data
                inventory_->add_gold(entity, char_data.gold);
                LOG_DEBUG(bridge, "Set gold for player {}: {}", live_player_id.value, char_data.gold);

                // Recalculate equipment modifiers and appearance from equipment_state
                players_->rebuild_equipment_cache(live_player_id);

                // Initialize weight tracking
                if (inv && item_registry_)
                {
                    int32_t total = 0;
                    for (const auto& entry : inv->items())
                    {
                        auto* tmpl = item_registry_->get(entry.item);
                        if (tmpl)
                            total += tmpl->weight * entry.count;
                    }
                    auto str = player->base.strength;
                    auto lvl = player->experience.level;
                    inventory_->set_weight(entity, total,
                                           inventory::inventory::max_weight(str, lvl));
                }
            }

            // Deserialize and apply magic (spell knowledge)
            std::vector<magic::spell_knowledge> known_spells;
            if (!char_data.magic_data.empty())
            {
                known_spells = auth::deserialize_magic(char_data.magic_data);
            }
            if (!known_spells.empty())
            {
                auto* magic_sys = subsystems().get<magic::magic_system>();
                if (magic_sys)
                {
                    magic_sys->set_player_spells(player->ecs_entity, std::move(known_spells));
                    LOG_DEBUG(bridge, "Loaded magic data for player {}", live_player_id.value);
                }
            }
            else
            {
                // First login (no persisted spells — magic_data defaults to '[]'): grant the
                // spells this character already qualifies for by INT/MAG. Persisted on the
                // next save like any learned spell. TODO: replace with a purchase/learning flow.
                auto* magic_sys = subsystems().get<magic::magic_system>();
                if (magic_sys)
                {
                    int granted = 0;
                    auto int_stat = player->base.intelligence;
                    auto mag_stat = player->base.magic;
                    magic_sys->for_each_spell(
                        [&](auto grant_sid, const auto& tmpl)
                        {
                            if (tmpl.int_requirement > 0 && tmpl.int_requirement <= int_stat &&
                                tmpl.mag_requirement <= mag_stat)
                            {
                                magic_sys->learn_spell(player->ecs_entity, grant_sid);
                                ++granted;
                            }
                        });
                    if (granted > 0)
                    {
                        LOG_INFO(bridge,
                                 "Granted {} starting spells to '{}' (INT {}, MAG {})",
                                 granted,
                                 player->name,
                                 int_stat,
                                 mag_stat);
                    }
                }
            }

            // Deserialize and apply quest data
            if (!char_data.quest_data.empty())
            {
                auto* quest_sys = subsystems().get<quest::quest_system>();
                if (quest_sys)
                {
                    quest_sys->register_player(live_player_id);
                    auto journal = auth::deserialize_quests(char_data.quest_data);
                    auto* player_journal = quest_sys->get_journal(live_player_id);
                    if (player_journal)
                    {
                        *player_journal = std::move(journal);
                        LOG_DEBUG(bridge, "Loaded quest data for player {}", live_player_id.value);
                    }
                }
            }

            // Recalculate computed stats
            player->base.level_bonus = char_data.level;
            player->recalculate_stats();

            // If player logged in dead (hp <= 0), respawn at faction town
            if (player->is_dead())
            {
                LOG_INFO(bridge,
                         "Player {} ('{}') logged in dead (hp={}), respawning at faction town",
                         live_player_id.value,
                         char_data.name,
                         player->hp);
                was_dead_on_login = true;

                // Determine respawn map from faction
                std::string respawn_map;
                switch (player->faction)
                {
                case faction::aresden:
                    respawn_map = "aresden";
                    break;
                case faction::elvine:
                    respawn_map = "elvine";
                    break;
                default:
                    respawn_map = "default";
                    break;
                }

                // Find spawn position on the respawn map
                world::position respawn_pos{18, 18};
                if (world_)
                {
                    auto* spawn_map = world_->get_map_by_name(respawn_map);
                    if (spawn_map)
                    {
                        auto pos = spawn_map->get_random_initial_point();
                        if (pos.has_value())
                            respawn_pos = *pos;
                    }
                }

                // Override char_data so game state message has correct values
                char_data.map_name = respawn_map;
                char_data.pos_x = respawn_pos.x;
                char_data.pos_y = respawn_pos.y;

                // Restore HP/MP to 50%
                player->hp = player->computed.max_hp / 2;
                player->mp = player->computed.max_mp / 2;
                char_data.hp = player->hp;
                char_data.mp = player->mp;
            }

            // Connect guild membership
            if (social_)
            {
                social_->register_player(live_player_id, char_data.name);
                social_->connect_guild_member(char_id, live_player_id, char_data.name);

                auto guild = social_->get_player_guild(live_player_id);
                if (guild.is_valid())
                {
                    auto* g = social_->get_guild(guild);
                    if (g)
                    {
                        player->guild_name = g->name;
                        player->guild_tag = g->tag;
                        auto* member = g->get_member(live_player_id);
                        if (member)
                            player->guild_rank = static_cast<uint8_t>(member->rank);
                    }
                }

                // Connect friend list
                social_->connect_friend(char_id, live_player_id, char_data.name);

                // Notify online friends that this player came online
                auto* friends = social_->get_friends(char_id);
                if (friends && ws_server_)
                {
                    for (const auto& f : *friends)
                    {
                        if (f.is_online())
                        {
                            auto* friend_conn = ws_server_->get_connection_by_player(f.runtime_id);
                            if (friend_conn)
                            {
                                friend_conn->send(network::make_friend_online_notification(char_data.name));
                            }
                        }
                    }
                }
            }

            // Set position if world system is available
            if (world_)
            {
                auto* target_map = world_->get_map_by_name(char_data.map_name);
                if (!target_map)
                {
                    // Try to get a default map (first loaded map)
                    world_->for_each_map(
                        [&target_map](map_id, world::map& m)
                        {
                            if (!target_map)
                                target_map = &m;
                        });
                    if (target_map)
                    {
                        // Update map name to the actual map we're using
                        char_data.map_name = std::string(target_map->name());
                    }
                }
                if (target_map)
                {
                    world::position spawn_pos{char_data.pos_x, char_data.pos_y};

                    // Check for invalid/default position marker (-1, -1)
                    // This indicates the player should spawn at the map's initial point
                    if (char_data.pos_x == -1 || char_data.pos_y == -1)
                    {
                        auto initial_pos = target_map->get_random_initial_point();
                        if (initial_pos)
                        {
                            spawn_pos = *initial_pos;
                            // Update char_data so the client receives correct position
                            char_data.pos_x = spawn_pos.x;
                            char_data.pos_y = spawn_pos.y;
                            LOG_DEBUG(bridge,
                                      "Player {} using map initial point: ({}, {})",
                                      live_player_id.value,
                                      spawn_pos.x,
                                      spawn_pos.y);
                        }
                        else
                        {
                            // Fallback to center of map if no initial points defined
                            spawn_pos = world::position{static_cast<int16_t>(target_map->width() / 2),
                                                        static_cast<int16_t>(target_map->height() / 2)};
                            char_data.pos_x = spawn_pos.x;
                            char_data.pos_y = spawn_pos.y;
                            LOG_WARN(bridge,
                                     "Map '{}' has no initial points, using center: ({}, {})",
                                     target_map->name(),
                                     spawn_pos.x,
                                     spawn_pos.y);
                        }
                    }

                    players_->set_position(live_player_id, target_map->id(), spawn_pos, world::direction::south);
                }
            }

            // Set visibility radii from client resolution
            player->visibility_radius_x = vis_radii.x;
            player->visibility_radius_y = vis_radii.y;

            // Bind connection to player
            players_->bind_connection(live_player_id, conn_id);

            LOG_DEBUG(bridge,
                      "Player instance created: id={}, hp={}/{}, mp={}/{}, level={}",
                      live_player_id.value,
                      player->hp,
                      player->computed.max_hp,
                      player->mp,
                      player->computed.max_mp,
                      player->experience.level);
        }
    }

    // Update connection state
    conn->set_state(network::ws_connection_state::in_game);
    conn->set_player(live_player_id);

    // Register player in lookup map
    ws_server_->register_player(conn_id, live_player_id);

    // Register with admin system for command access
    if (admin_ && auth_)
    {
        // Get admin level from account
        auto auth_admin_level = auth_->get_admin_level(conn->account());

        // Convert auth::admin_level to admin::admin_level
        admin::admin_level cmd_level = admin::admin_level::player;
        switch (auth_admin_level)
        {
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

        // Set player struct admin level so is_gm() works
        if (auto* plr = players_->get_player(live_player_id))
        {
            if (auth_admin_level >= auth::admin_level::senior_gm)
            {
                plr->admin = player::admin_level::admin;
            }
            else if (auth_admin_level >= auth::admin_level::helper)
            {
                plr->admin = player::admin_level::gamemaster;
            }
        }
    }

    LOG_INFO(bridge,
             "Player {} (character '{}') entering game from account {}",
             live_player_id.value,
             char_data.name,
             conn->account().value);

    // Notify admin web tool of player connection
    if (enter_game_callback_)
    {
        enter_game_callback_(char_data.name, static_cast<int16_t>(char_data.level), char_data.map_name);
    }

    // Build v2 inventory data for separate inventory_data message
    auto* enter_player = players_->get_player(live_player_id);
    int32_t player_gold = char_data.gold;
    std::vector<std::tuple<nlohmann::json, int16_t, int16_t, int32_t>> inv_items;
    std::map<std::string, uint32_t> equip_slots;
    int32_t inv_weight = 0;
    int32_t inv_max_weight = 0;

    if (inventory_)
    {
        auto player_entity = entity_id{live_player_id.value};
        auto* inv = inventory_->get_inventory(player_entity);
        if (inv && item_)
        {
            for (const auto& entry : inv->items())
            {
                auto* itm = item_->get_item(entry.item);
                if (itm)
                {
                    inv_items.emplace_back(
                        item::serialize_item(*itm),
                        entry.pos_x,
                        entry.pos_y,
                        entry.z_order);
                }
            }
        }
        player_gold = static_cast<int32_t>(inventory_->get_gold(player_entity));
        inv_weight = inventory_->get_current_weight(player_entity);
        inv_max_weight = inventory_->get_max_weight(player_entity);
    }

    // Build equipment slot map from player equipment_state
    if (enter_player)
    {
        // Map equip_slot enum to lowercase protocol string names
        static constexpr const char* slot_names[] = {
            "head", "body", "arms", "pants", "boots", "weapon", "shield",
            "twohand", "ring_left", "ring_right", "amulet", "cape", "angel", "fullbody"};

        for (const auto& [slot, id] : enter_player->equipment.all_equipped())
        {
            auto idx = static_cast<size_t>(slot);
            if (idx < std::size(slot_names))
            {
                equip_slots[slot_names[idx]] = id.value;
            }
        }
    }

    // Build skills data for network message
    std::vector<network::skill_entry_msg> skills_list;
    if (players_)
    {
        auto* player = players_->get_player(live_player_id);
        if (player)
        {
            auto* skill_sys = subsystems().get<skill::skill_system>();
            for (size_t i = 0; i < skill::max_skills; ++i)
            {
                const auto& sk = player->skills.skills[i];
                auto st = static_cast<skill::skill_type>(i);
                skills_list.push_back(
                    {.skill_id = static_cast<uint8_t>(i),
                     .level = sk.level,
                     .total_uses = sk.total_uses,
                     .uses_this_level = sk.uses_this_level,
                     .uses_to_next_level = skill_sys ? skill_sys->uses_to_next_level(st, sk.level) : 0});
            }
        }
    }

    // Build spell data for network message
    std::vector<network::known_spell_msg> spells_list;
    {
        auto* magic_sys = subsystems().get<magic::magic_system>();
        if (magic_sys && players_)
        {
            auto* player = players_->get_player(live_player_id);
            if (player)
            {
                const auto* known = magic_sys->get_player_spells(player->ecs_entity);
                if (known)
                {
                    for (const auto& sk : *known)
                    {
                        spells_list.push_back(
                            {.spell_id = sk.spell.value, .level = sk.level, .total_casts = sk.total_casts});
                    }
                }
            }
        }
    }

    // Build quest data for network message
    std::vector<network::active_quest_msg> quests_list;
    std::vector<uint16_t> completed_quest_ids;
    {
        auto* quest_sys = subsystems().get<quest::quest_system>();
        if (quest_sys)
        {
            const auto* journal = quest_sys->get_journal(live_player_id);
            if (journal)
            {
                for (const auto& q : journal->active_quests)
                {
                    network::active_quest_msg qm;
                    qm.quest_id = q.template_id.value;
                    qm.status = static_cast<uint8_t>(q.status);
                    for (const auto& obj : q.objectives)
                    {
                        qm.objectives.push_back({.id = obj.template_id,
                                                 .status = static_cast<uint8_t>(obj.status),
                                                 .current = obj.current_count,
                                                 .required = obj.required_count});
                    }
                    quests_list.push_back(std::move(qm));
                }
                for (const auto& qid : journal->completed_quests)
                {
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

    if (scheduler_)
    {
        auto& clock = scheduler_->game_time();
        env_hour = static_cast<uint8_t>(clock.hour());
        env_minute = static_cast<uint8_t>(clock.minute());
        env_is_day = clock.is_day();
    }

    if (world_ && players_)
    {
        auto* plr = players_->get_player(live_player_id);
        if (plr)
        {
            auto* current_map = world_->get_map(plr->current_map);
            if (current_map)
            {
                if (current_map->config().is_fixed_day_mode)
                {
                    env_is_day = true;
                    env_weather = 0;
                }
                else
                {
                    env_weather = static_cast<uint8_t>(current_map->weather());
                }
            }
        }
    }

    // Fetch guild info, ECS entity ID, and equipment visuals for enter_game_response
    // (player pointer may be out of scope after this block)
    std::string enter_guild_name;
    std::string enter_guild_tag;
    uint8_t enter_guild_rank = 0;
    uint32_t enter_entity_id = live_player_id.value;
    network::equip_visual_msg weapon_vis, shield_vis, body_vis, pants_vis, head_vis, arms_vis, boots_vis, cape_vis;
    if (players_)
    {
        auto* plr = players_->get_player(live_player_id);
        if (plr)
        {
            enter_guild_name = plr->guild_name;
            enter_guild_tag = plr->guild_tag;
            enter_guild_rank = plr->guild_rank;
            // Use ECS entity ID so client entity ID matches all subsequent messages
            // (entity_hp_update, combat broadcasts, entity_spawn, etc.)
            enter_entity_id = plr->ecs_entity.id;

            // Equipment visuals from player appearance state
            weapon_vis = {plr->appearance.weapon.appr, plr->appearance.weapon.color};
            shield_vis = {plr->appearance.shield.appr, plr->appearance.shield.color};
            body_vis = {plr->appearance.body.appr, plr->appearance.body.color};
            pants_vis = {plr->appearance.pants.appr, plr->appearance.pants.color};
            head_vis = {plr->appearance.head.appr, plr->appearance.head.color};
            arms_vis = {plr->appearance.arms.appr, plr->appearance.arms.color};
            boots_vis = {plr->appearance.boots.appr, plr->appearance.boots.color};
            cape_vis = {plr->appearance.cape.appr, plr->appearance.cape.color};
        }
    }

    // Build full game state message
    network::game_state_msg game_state{.character = network::character_data_msg{.id = enter_entity_id,
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
                                                                                .stat_points = char_data.stat_points_available,
                                                                                .hunger_level = char_data.hunger_level,
                                                                                .guild_name = enter_guild_name,
                                                                                .guild_tag = enter_guild_tag,
                                                                                .guild_rank = enter_guild_rank,
                                                                                .weapon_visual = weapon_vis,
                                                                                .shield_visual = shield_vis,
                                                                                .body_visual = body_vis,
                                                                                .pants_visual = pants_vis,
                                                                                .head_visual = head_vis,
                                                                                .arms_visual = arms_vis,
                                                                                .boots_visual = boots_vis,
                                                                                .cape_visual = cape_vis},
                                       .inventory = {},  // inventory sent separately via v2 inventory_data
                                       .skills = skills_list,
                                       .spells = spells_list,
                                       .quests = quests_list,
                                       .completed_quests = completed_quest_ids,
                                       .gold = player_gold,
                                       .time_hour = env_hour,
                                       .time_minute = env_minute,
                                       .is_day = env_is_day,
                                       .weather = env_weather};

    // Send combined enter game response with full game state
    auto response = network::make_enter_game_response(msg.seq, true, &game_state);
    conn->send(response);

    // Send v2 inventory data with items, equipment slots, gold, and weight
    {
        auto inv_msg = network::make_inventory_data_v2(
            inv_items, equip_slots, player_gold, inv_weight, inv_max_weight);
        conn->send_raw(inv_msg.dump());
    }

    // Send visible entities (players + alive/dead NPCs) at current position
    if (players_ && world_)
    {
        auto* player = players_->get_player(live_player_id);
        if (player)
        {
            bridge::send_visible_entity_spawns(
                conn, live_player_id, player->current_map, player->pos,
                players_, npc_, world_, item_, item_registry_, effects_);

            // Send map teleporters
            auto* current_map = world_->get_map(player->current_map);
            if (current_map)
            {
                bridge::send_map_teleporters(conn, *current_map);
            }

            // Send visible ground items
            if (item_)
            {
                int rx = player->visibility_radius_x > 0 ? player->visibility_radius_x : 20;
                int ry = player->visibility_radius_y > 0 ? player->visibility_radius_y : 15;
                bridge::send_visible_ground_items(
                    conn, player->current_map, player->pos, rx, ry, world_, item_, item_registry_);
            }

            // Send visible ground fields (spikes, ice storms, poison clouds)
            {
                int rx = player->visibility_radius_x > 0 ? player->visibility_radius_x : 20;
                int ry = player->visibility_radius_y > 0 ? player->visibility_radius_y : 15;
                bridge::send_visible_dynamic_objects(conn, player->current_map, player->pos, rx, ry);
            }
        }
    }

    LOG_DEBUG(bridge, "Sent game state to player {}", live_player_id.value);

    // Player was dead on login: already auto-respawned at faction town with 50% HP/MP
    // (see above). No death notification sent — client receives alive game state.
    if (was_dead_on_login)
    {
        LOG_DEBUG(bridge,
                  "Player {} was dead on login, auto-respawned at {}",
                  live_player_id.value,
                  game_state.character.map_name);
    }

    // Notify nearby players of the new spawn
    if (players_ && ws_server_)
    {
        auto* player = players_->get_player(live_player_id);
        if (player)
        {
            // Send per-player (hostility is viewer-relative)
            bridge::for_each_visible_connection(
                players_,
                ws_server_,
                player->current_map,
                player->pos,
                [&](player_id, player::player& other, network::ws_connection& peer_conn)
                {
                    auto spawn_entity = build_player_spawn(
                        *player, player_hostility(other.faction, player->faction), item_, item_registry_, effects_);
                    peer_conn.send(network::make_entity_spawn(0, spawn_entity));
                },
                live_player_id);

            LOG_DEBUG(bridge, "Notified nearby players of spawn for {}", player->name);

            // Forward to admin spectators (neutral perspective)
            auto admin_spawn = build_player_spawn(*player, "neutral", item_, item_registry_, effects_);
            auto admin_msg = network::make_entity_spawn(0, admin_spawn);
            for (auto admin_conn : ws_server_->get_admin_subscribers(player->current_map))
            {
                ws_server_->send(admin_conn, admin_msg);
            }
        }
    }

    // Check for unclaimed war rewards (deferred from crusade end while offline)
    if (war_persistence_ && players_)
    {
        auto* player = players_->get_player(live_player_id);
        if (player)
        {
            int32_t db_char_id = static_cast<int32_t>(player->character_id.value);
            auto unclaimed_result = war_persistence_->get_unclaimed_rewards(db_char_id);
            if (unclaimed_result.is_ok())
            {
                for (const auto& row : unclaimed_result.value())
                {
                    // Apply deferred EXP reward
                    if (row.reward_exp > 0)
                    {
                        players_->add_experience(live_player_id, row.reward_exp);
                    }

                    // Send reward summary to client
                    auto reward_msg = network::make_crusade_reward_summary(0,
                                                                           static_cast<uint8_t>(row.faction),
                                                                           row.contribution,
                                                                           row.reward_exp,
                                                                           row.reward_gold,
                                                                           row.reward_contribution);
                    conn->send(reward_msg);

                    // Mark as claimed
                    war_persistence_->mark_rewards_claimed(row.id);

                    LOG_INFO(bridge,
                             "Applied deferred war reward to player {} (char_id={}): {} exp",
                             live_player_id.value,
                             db_char_id,
                             row.reward_exp);
                }
            }
        }
    }

    // Notify game handlers of enter_game completion (for command list, etc.)
    if (post_enter_game_callback_)
    {
        post_enter_game_callback_(live_player_id, conn_id);
    }
}

void auth_handlers::handle_ping(connection_id conn_id, const network::json_message& msg)
{
    auto* conn = get_connection_or_error(conn_id, msg.seq);
    if (!conn)
        return;

    auto response = network::make_pong_response(msg.seq);
    conn->send(response);
}

void auth_handlers::handle_enter_admin_mode(connection_id conn_id, const network::json_message& msg)
{
    auto* conn = require_authenticated(conn_id, msg.seq);
    if (!conn)
        return;

    // Look up account admin level
    auto account_result = auth_->get_account(conn->account());
    if (account_result.is_err())
    {
        auto response = network::make_enter_admin_mode_response(msg.seq, false, 0, "account_not_found");
        conn->send(response);
        return;
    }

    auto& acct = account_result.value();
    auto level = static_cast<uint8_t>(acct.admin);

    // Minimum gamemaster (10) required
    if (level < static_cast<uint8_t>(auth::admin_level::gamemaster))
    {
        LOG_WARN(bridge, "Admin mode denied for account '{}' (level {})", acct.username, level);
        auto response = network::make_enter_admin_mode_response(msg.seq, false, 0, "insufficient_permissions");
        conn->send(response);
        return;
    }

    // Transition to admin_dashboard state
    conn->set_state(network::ws_connection_state::admin_dashboard);
    conn->set_admin_level(level);

    LOG_INFO(bridge, "Account '{}' entered admin dashboard mode (level {})", acct.username, level);

    auto response = network::make_enter_admin_mode_response(msg.seq, true, level);
    conn->send(response);
}

void auth_handlers::send_error(connection_id conn_id,
                               uint32_t seq,
                               std::string_view error_code,
                               std::string_view message)
{
    if (!ws_server_)
        return;

    auto* conn = ws_server_->get_connection(conn_id);
    if (!conn)
        return;

    auto response = network::make_error_response(seq, error_code, message);
    conn->send(response);
}

auto auth_handlers::get_connection_or_error(connection_id conn_id, [[maybe_unused]] uint32_t seq) -> network::ws_connection*
{
    if (!ws_server_)
    {
        return nullptr;
    }

    auto* conn = ws_server_->get_connection(conn_id);
    if (!conn)
    {
        LOG_WARN(bridge, "Message from unknown connection {}", conn_id.value);
        return nullptr;
    }

    return conn;
}

auto auth_handlers::require_authenticated(connection_id conn_id, uint32_t seq) -> network::ws_connection*
{
    auto* conn = get_connection_or_error(conn_id, seq);
    if (!conn)
        return nullptr;

    if (conn->state() != network::ws_connection_state::authenticated &&
        conn->state() != network::ws_connection_state::in_game)
    {
        LOG_WARN(bridge, "Unauthenticated request from connection {}", conn_id.value);
        send_error(conn_id, seq, "not_authenticated", "You must be logged in to perform this action");
        return nullptr;
    }

    return conn;
}

void auth_handlers::save_player_state(player_id pid)
{
    auto* perf = subsystems().get<perf::perf_stats_system>();
    PERF_TIMER(perf, perf::metric_category::player_save);

    if (!players_ || !auth_)
        return;

    auto* player = players_->get_player(pid);
    if (!player)
    {
        LOG_WARN(bridge, "Cannot save player {}: not found", pid.value);
        return;
    }

    // Sync skills from skill_system back to player struct before saving
    auto* skill_sys = subsystems().get<skill::skill_system>();
    if (skill_sys)
    {
        auto* ps = skill_sys->get_player_skills(pid);
        if (ps)
        {
            player->skills = *ps;
        }
    }

    // Serialize skills
    auto skills_json = auth::serialize_skills(player->skills);

    // Collect items from inventory, bank into item_rows
    std::vector<auth::item_row> item_rows;
    std::vector<auth::equipment_row> equip_rows;
    int32_t player_gold = 0;

    if (inventory_)
    {
        auto entity = entity_id{pid.value};

        // Gather inventory items
        auto* inv = inventory_->get_inventory(entity);
        if (inv)
        {
            for (const auto& entry : inv->items())
            {
                if (auto* itm = item_->get_item(entry.item))
                {
                    item_rows.push_back({
                        .id = itm->id.value,
                        .character_id = static_cast<int32_t>(player->character_id.value),
                        .template_id = itm->template_id.value,
                        .name = itm->name,
                        .location = auth::item_location::inventory,
                        .slot = 0, // Slot is meaningless in entry-based inventory
                        .count = entry.count,
                        .durability = itm->durability,
                        .max_durability = itm->max_durability,
                        .color = itm->color,
                        .bound_to = itm->bound_to
                            ? std::optional<int32_t>(static_cast<int32_t>(itm->bound_to->value))
                            : std::nullopt,
                        .upgrade_level = itm->attribute.upgrade_level,
                        .main_enchant_type = static_cast<uint8_t>(itm->attribute.main_type),
                        .main_enchant_value = itm->attribute.main_value,
                        .sub_enchant_type = static_cast<uint8_t>(itm->attribute.sub_type),
                        .sub_enchant_value = itm->attribute.sub_value,
                        .custom_made = itm->attribute.custom_made,
                        .custom_quality = itm->attribute.custom_quality,
                        .pos_x = entry.pos_x,
                        .pos_y = entry.pos_y,
                        .z_order = entry.z_order,
                    });
                }
            }
            LOG_DEBUG(bridge, "Saving inventory for player {} ({} items)", pid.value, inv->count());
        }

        // Build equipment rows from equipment_state
        for (const auto& [slot, iid] : player->equipment.all_equipped())
        {
            equip_rows.push_back({
                .character_id = static_cast<int32_t>(player->character_id.value),
                .slot = static_cast<int16_t>(slot),
                .item_id = iid.value,
            });
        }

        // Gather bank items
        auto* bank = inventory_->get_bank(entity);
        if (bank)
        {
            for (int16_t p = 0; p < bank->total_pages(); ++p)
            {
                for (int16_t s = 0; s < bank->slots_per_page(); ++s)
                {
                    auto slot_item = bank->get_slot(p, s);
                    if (slot_item)
                    {
                        if (auto* itm = item_->get_item(*slot_item))
                        {
                            // Store flat index for legacy compatibility + bank_page/bank_slot
                            auto flat_slot = static_cast<int16_t>(p * bank->slots_per_page() + s);
                            item_rows.push_back({
                                .id = itm->id.value,
                                .character_id = static_cast<int32_t>(player->character_id.value),
                                .template_id = itm->template_id.value,
                                .name = itm->name,
                                .location = auth::item_location::bank,
                                .slot = flat_slot,
                                .count = itm->count,
                                .durability = itm->durability,
                                .max_durability = itm->max_durability,
                                .color = itm->color,
                                .bound_to = itm->bound_to
                                    ? std::optional<int32_t>(static_cast<int32_t>(itm->bound_to->value))
                                    : std::nullopt,
                                .upgrade_level = itm->attribute.upgrade_level,
                                .main_enchant_type = static_cast<uint8_t>(itm->attribute.main_type),
                                .main_enchant_value = itm->attribute.main_value,
                                .sub_enchant_type = static_cast<uint8_t>(itm->attribute.sub_type),
                                .sub_enchant_value = itm->attribute.sub_value,
                                .custom_made = itm->attribute.custom_made,
                                .custom_quality = itm->attribute.custom_quality,
                                .bank_page = p,
                                .bank_slot = s,
                            });
                        }
                    }
                }
            }
            LOG_DEBUG(bridge, "Saving bank for player {} ({} items)", pid.value, bank->used_slots());
        }

        // Get gold
        player_gold = static_cast<int32_t>(inventory_->get_gold(entity));
    }

    // Save items and equipment to DB
    auth_->save_items(player->character_id, item_rows);
    auth_->save_equipment(player->character_id, equip_rows);

    // Serialize magic (spell knowledge)
    std::string magic_json = "[]";
    {
        auto* magic_sys = subsystems().get<magic::magic_system>();
        if (magic_sys)
        {
            const auto* spells = magic_sys->get_player_spells(player->ecs_entity);
            if (spells)
            {
                magic_json = auth::serialize_magic(*spells);
                LOG_DEBUG(bridge, "Saving magic data for player {} ({} spells)", pid.value, spells->size());
            }
        }
    }

    // Serialize quest data
    std::string quest_json = "[]";
    {
        auto* quest_sys = subsystems().get<quest::quest_system>();
        if (quest_sys)
        {
            const auto* journal = quest_sys->get_journal(pid);
            if (journal)
            {
                quest_json = auth::serialize_quests(*journal);
                LOG_DEBUG(bridge,
                          "Saving quest data for player {} ({} active, {} completed)",
                          pid.value,
                          journal->active_quests.size(),
                          journal->completed_quests.size());
            }
        }
    }

    // Build character data from current player state
    // Use the database character_id, not the runtime player_id
    auth::character_full_data data{.id = player->character_id,
                                   .account = {}, // Not needed for save
                                   .name = player->name,
                                   .level = static_cast<int16_t>(player->experience.level),
                                   .class_type = static_cast<int16_t>(player->profession),
                                   .nation = static_cast<int16_t>(player->faction),
                                   .gender = static_cast<int16_t>(player->sex),
                                   .map_name = "", // Will be filled from map
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
                                   .luck = 0, // TODO: add luck to player struct when luck system is implemented
                                   .reward_gold =
                                       0, // TODO: add reward_gold to player struct when bounty system is implemented
                                   .skills_data = skills_json,
                                   .magic_data = magic_json,
                                   .quest_data = quest_json};

    // Get map name
    if (world_ && player->current_map.value != 0)
    {
        auto* map = world_->get_map(player->current_map);
        if (map)
        {
            data.map_name = std::string(map->name());
        }
    }
    if (data.map_name.empty())
    {
        data.map_name = "default";
    }

    auto save_result = auth_->save_character(data);
    if (save_result.is_err())
    {
        LOG_ERROR(bridge, "Failed to save player {}: {}", pid.value, auth::to_string(save_result.error()));
    }
    else
    {
        LOG_DEBUG(bridge, "Saved player {} at ({}, {}) on {}", pid.value, data.pos_x, data.pos_y, data.map_name);
    }
}

void auth_handlers::destroy_player_items(entity_id owner)
{
    if (!inventory_ || !item_)
        return;

    // Collect item IDs from inventory
    auto* inv = inventory_->get_inventory(owner);
    if (inv)
    {
        for (const auto& entry : inv->items())
        {
            item_->destroy_item(entry.item);
        }
    }

    // Collect item IDs from bank
    auto* bank = inventory_->get_bank(owner);
    if (bank)
    {
        for (int16_t p = 0; p < bank->total_pages(); ++p)
        {
            for (int16_t s = 0; s < bank->slots_per_page(); ++s)
            {
                auto slot_item = bank->get_slot(p, s);
                if (slot_item)
                {
                    item_->destroy_item(*slot_item);
                }
            }
        }
    }
}

void auth_handlers::handle_player_disconnect(connection_id conn_id)
{
    if (!ws_server_)
        return;

    auto* conn = ws_server_->get_connection(conn_id);
    if (!conn)
        return;

    // Check if player was in game by looking at the player_id
    // Note: We can't check conn->state() == in_game because the websocket layer
    // already set the state to 'disconnected' before calling this handler
    auto pid = conn->player();
    if (!pid.is_valid())
        return;

    LOG_INFO(bridge, "Player {} disconnecting, saving state...", pid.value);

    // Save player state
    save_player_state(pid);

    // Disconnect from guild and friends (before removing player)
    if (social_)
    {
        auto* player = players_ ? players_->get_player(pid) : nullptr;
        if (player)
        {
            // Notify online friends before disconnecting
            auto* friends = social_->get_friends(player->character_id);
            if (friends && ws_server_)
            {
                for (const auto& f : *friends)
                {
                    if (f.is_online())
                    {
                        auto* friend_conn = ws_server_->get_connection_by_player(f.runtime_id);
                        if (friend_conn)
                        {
                            friend_conn->send(network::make_friend_offline_notification(player->name));
                        }
                    }
                }
            }
            social_->disconnect_friend(pid, player->character_id);
            social_->disconnect_guild_member(pid, player->character_id);
        }
        social_->unregister_player(pid);
    }

    // Notify nearby players of despawn
    if (players_ && ws_server_)
    {
        auto* player = players_->get_player(pid);
        if (player)
        {
            auto despawn_msg = network::make_entity_despawn(0, player->ecs_entity.id);
            bridge::broadcast_to_visible(players_, ws_server_, player->current_map, player->pos, despawn_msg, pid);

            // Forward to admin spectators
            for (auto admin_conn : ws_server_->get_admin_subscribers(player->current_map))
            {
                ws_server_->send(admin_conn, despawn_msg);
            }
        }

        // Remove player from system
        players_->remove_player(pid);
    }

    // Destroy item instances before inventory
    destroy_player_items(entity_id{pid.value});

    // Clean up inventory
    if (inventory_)
    {
        inventory_->destroy_inventory(entity_id{pid.value});
    }

    // Unregister from skill system
    {
        auto* skill_sys = subsystems().get<skill::skill_system>();
        if (skill_sys)
        {
            skill_sys->unregister_player(pid);
        }
    }

    // Unregister from admin system
    if (admin_)
    {
        admin_->unregister_admin(pid);
    }

    LOG_INFO(bridge, "Player {} cleanup complete", pid.value);
}

void auth_handlers::save_player(player_id pid)
{
    save_player_state(pid);
}

void auth_handlers::set_enter_game_callback(enter_game_callback cb)
{
    enter_game_callback_ = std::move(cb);
}

void auth_handlers::set_post_enter_game_callback(post_enter_game_callback cb)
{
    post_enter_game_callback_ = std::move(cb);
}

auto auth_handlers::save_all_players() -> size_t
{
    if (!players_)
    {
        LOG_WARN(bridge, "Cannot save all players: player_system not available");
        return 0;
    }

    size_t saved_count = 0;
    size_t total_count = 0;

    players_->for_each_player(
        [this, &saved_count, &total_count](player_id pid, const player::player&)
        {
            ++total_count;
            // Only save players that have a valid connection (are actually in-game)
            if (players_->get_player(pid)->connection.value != 0)
            {
                save_player_state(pid);
                ++saved_count;
            }
        });

    if (saved_count > 0)
    {
        LOG_INFO(bridge, "Periodic save completed: {}/{} players saved", saved_count, total_count);
    }

    return saved_count;
}

} // namespace hb::bridge
