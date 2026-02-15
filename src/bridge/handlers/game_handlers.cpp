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
#include "item/item_upgrade.h"
#include "item/special_ability.h"
#include "player/equip_mapping.h"
#include "magic/magic_system.h"
#include "magic/spell.h"
#include "crafting/manufacturing_system.h"
#include "crafting/alchemy_system.h"
#include "crafting/mining_system.h"
#include "crafting/fishing_system.h"
#include "registry/mining_registry.h"
#include "registry/fishing_registry.h"
#include "skill/skill_system.h"
#include "quest/quest_system.h"
#include "registry/build_recipe_registry.h"
#include "registry/craft_recipe_registry.h"
#include "social/party.h"
#include "scheduler/scheduler.h"
#include "config/config_system.h"
#include "core/subsystem.h"
#include "core/logger.h"
#include "perf/perf_stats.h"
#include "war/crusade/crusade_system.h"
#include "bridge/handlers/entity_builders.h"
#include "effect/effect_system.h"
#include "audit/item_audit_system.h"

#include <chrono>
#include <random>
#include <iomanip>
#include <sstream>

namespace hb::bridge
{

game_handlers::game_handlers() = default;
game_handlers::~game_handlers() = default;

void game_handlers::set_save_callback(save_player_callback cb)
{
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
                               crafting::mining_system* mining,
                               crafting::fishing_system* fishing,
                               war::crusade_system* crusade,
                               effect::effect_system* effects,
                               item_registry* item_reg,
                               audit::item_audit_system* audit)
{
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
    fishing_ = fishing;
    crusade_ = crusade;
    effects_ = effects;
    item_registry_ = item_reg;
    audit_ = audit;

    // Register chat message callback to distribute messages
    if (social_)
    {
        social_->on_chat_message([this](const social::chat_message_event& event) { on_chat_message(event); });
    }

    // Register combat callbacks
    if (combat_)
    {
        combat_->on_damage([this](const combat::damage_event& event) { on_damage_dealt(event); });
        combat_->on_death([this](const combat::death_event& event) { on_entity_death(event); });
    }

    // Register hunger change callback
    if (players_)
    {
        players_->on_hunger_change([this](player_id pid, int8_t, int8_t new_level)
                                   { send_hunger_update(pid, new_level); });
    }

    // Register NPC callbacks
    // NOTE: These callbacks are invoked synchronously and must immediately copy
    // any data from the npc& reference. The reference is only valid during the
    // callback invocation - do not store it or pass it to async operations.
    if (npc_)
    {
        npc_->set_on_spawn_callback(
            [this](const npc::npc& n)
            {
                broadcast_npc_spawn(n); // Copies data immediately
            });
        npc_->set_on_move_callback(
            [this](const npc::npc& n)
            {
                broadcast_npc_move(n); // Copies data immediately
            });
        npc_->set_on_death_callback(
            [this](const npc::npc& n, entity::entity killer, int32_t killing_damage)
            {
                broadcast_npc_death(n, killer, killing_damage); // Copies data immediately
                handle_npc_loot_drop(n, killer);                // Generate and drop loot
                distribute_npc_kill_exp(killer, n.exp_reward);  // Award XP

                // Crusade NPC kill rewards
                if (crusade_ && crusade_->is_active())
                {
                    player_id killer_pid(killer.id);
                    auto* killer_player = players_ ? players_->get_player(killer_pid) : nullptr;
                    if (killer_player)
                    {
                        // Check if NPC is friendly to the killer's faction
                        // War unit NPCs are enemy if they belong to the opposing faction
                        // For simplicity: if the NPC is a monster/boss, it's an enemy NPC kill
                        // Friendly NPCs (guards, merchants, etc.) trigger penalty
                        if (n.is_monster())
                        {
                            crusade_->on_npc_kill(killer_pid, n.sprite_id);
                        }
                        else if (n.is_friendly())
                        {
                            crusade_->on_friendly_npc_kill(killer_pid);
                        }
                    }
                }
            });
        npc_->set_on_attack_callback(
            [this](const npc::npc& n, entity::entity target, int32_t damage)
            {
                broadcast_npc_attack(n, target, damage); // Copies data immediately
            });
        npc_->set_on_damage_callback(
            [this](const npc::npc& n, int32_t damage, entity::entity /*source*/)
            {
                // Skip HP update for killing blows — entity_death subsumes it
                if (n.is_dead())
                    return;

                // Broadcast NPC HP update to nearby players
                broadcast_npc_hp_update(n);

                // GMG damage tracking for crusade mana system
                if (n.sprite_id == 41 && crusade_ && crusade_->is_active())
                {
                    crusade_->on_gmg_damage(n.entity_id, damage);
                }
            });
        npc_->set_on_despawn_callback(
            [this](const npc::npc& n)
            {
                handle_npc_despawn_drop(n); // Generate despawn loot (body parts, rares, boss multi-drops)

                // Broadcast entity_despawn so clients remove the corpse
                if (players_ && ws_server_)
                {
                    auto msg = network::make_entity_despawn(0, n.entity_id.id);
                    auto players = players_->get_players_who_can_see(n.current_map, n.pos);
                    for (auto pid : players)
                    {
                        auto* p = players_->get_player(pid);
                        if (!p || p->connection.value == 0)
                            continue;
                        auto* conn = ws_server_->get_connection(p->connection);
                        if (conn && conn->is_open())
                            conn->send(msg);
                    }
                }
            });
    }

    // Register magic spell cast callback for visual broadcasts
    if (magic_)
    {
        magic_->on_spell_cast(
            [this](entity::entity caster, const magic::spell_template& spell, const magic::spell_effect_result& result)
            { on_spell_cast(caster, spell, result); });
    }

    // Register mining node spawn/despawn callbacks
    if (mining_)
    {
        mining_->set_spawn_callback(
            [this](const crafting::mineral_node& node)
            {
                auto* perf = subsystems().get<perf::perf_stats_system>();
                PERF_TIMER(perf, perf::metric_category::broadcast);

                if (!players_ || !ws_server_ || !world_)
                    return;

                auto* map = world_->get_map_by_name(node.map_name);
                if (!map)
                    return;

                auto* mining_reg = subsystems().get<mining_registry>();
                auto* type_config = mining_reg ? mining_reg->get_type(node.type_id) : nullptr;
                uint8_t visual = type_config ? type_config->visual_type : 1;

                auto msg = network::make_mineral_spawn(node.node_id, visual, node.x, node.y);
                auto pos = world::position{node.x, node.y};
                auto players = players_->get_players_who_can_see(map->id(), pos);
                for (auto pid : players)
                {
                    auto* p = players_->get_player(pid);
                    if (!p || p->connection.value == 0)
                        continue;
                    auto* conn = ws_server_->get_connection(p->connection);
                    if (conn && conn->is_open())
                    {
                        conn->send(msg);
                    }
                }
            });

        mining_->set_despawn_callback(
            [this](const crafting::mineral_node& node)
            {
                auto* perf = subsystems().get<perf::perf_stats_system>();
                PERF_TIMER(perf, perf::metric_category::broadcast);

                if (!players_ || !ws_server_ || !world_)
                    return;

                auto* map = world_->get_map_by_name(node.map_name);
                if (!map)
                    return;

                auto msg = network::make_mineral_despawn(node.node_id, node.x, node.y);
                auto pos = world::position{node.x, node.y};
                auto players = players_->get_players_who_can_see(map->id(), pos);
                for (auto pid : players)
                {
                    auto* p = players_->get_player(pid);
                    if (!p || p->connection.value == 0)
                        continue;
                    auto* conn = ws_server_->get_connection(p->connection);
                    if (conn && conn->is_open())
                    {
                        conn->send(msg);
                    }
                }
            });
    }

    // Register fishing node spawn/despawn/engagement callbacks
    if (fishing_)
    {
        fishing_->set_spawn_callback(
            [this](const crafting::fish_node& node)
            {
                auto* perf = subsystems().get<perf::perf_stats_system>();
                PERF_TIMER(perf, perf::metric_category::broadcast);

                if (!players_ || !ws_server_ || !world_)
                    return;

                auto* map = world_->get_map_by_name(node.map_name);
                if (!map)
                    return;

                auto msg = network::make_fish_spawn_broadcast(
                    node.index, node.config ? node.config->visual_type : 2, node.x, node.y);
                auto pos = world::position{node.x, node.y};
                auto players = players_->get_players_who_can_see(map->id(), pos);
                for (auto pid : players)
                {
                    auto* p = players_->get_player(pid);
                    if (!p || p->connection.value == 0)
                        continue;
                    auto* conn = ws_server_->get_connection(p->connection);
                    if (conn && conn->is_open())
                    {
                        conn->send(msg);
                    }
                }
            });

        fishing_->set_despawn_callback(
            [this](const crafting::fish_node& node)
            {
                auto* perf = subsystems().get<perf::perf_stats_system>();
                PERF_TIMER(perf, perf::metric_category::broadcast);

                if (!players_ || !ws_server_ || !world_)
                    return;

                auto* map = world_->get_map_by_name(node.map_name);
                if (!map)
                    return;

                auto msg = network::make_fish_despawn_broadcast(node.index, node.x, node.y);
                auto pos = world::position{node.x, node.y};
                auto players = players_->get_players_who_can_see(map->id(), pos);
                for (auto pid : players)
                {
                    auto* p = players_->get_player(pid);
                    if (!p || p->connection.value == 0)
                        continue;
                    auto* conn = ws_server_->get_connection(p->connection);
                    if (conn && conn->is_open())
                    {
                        conn->send(msg);
                    }
                }
            });

        fishing_->set_engaged_callback(
            [this](entity_id player_eid, const crafting::fish_type_config& config, int32_t initial_chance)
            {
                auto* perf = subsystems().get<perf::perf_stats_system>();
                PERF_TIMER(perf, perf::metric_category::broadcast);

                if (!players_ || !ws_server_)
                    return;

                auto pid = player_id{player_eid.value};
                auto* plr = players_->get_player(pid);
                if (!plr || plr->connection.value == 0)
                    return;

                auto msg = network::make_fish_engaged(player_eid, config.name, config.visual_type, initial_chance);
                auto* conn = ws_server_->get_connection(plr->connection);
                if (conn && conn->is_open())
                {
                    conn->send(msg);
                }
            });

        fishing_->set_chance_update_callback(
            [this](entity_id player_eid, int32_t chance)
            {
                if (!players_ || !ws_server_)
                    return;

                auto pid = player_id{player_eid.value};
                auto* plr = players_->get_player(pid);
                if (!plr || plr->connection.value == 0)
                    return;

                auto msg = network::make_fish_chance_update(player_eid, chance);
                auto* conn = ws_server_->get_connection(plr->connection);
                if (conn && conn->is_open())
                {
                    conn->send(msg);
                }
            });

        fishing_->set_catch_complete_callback(
            [this](entity_id player_eid, const crafting::fish_catch_result& result)
            {
                if (!players_ || !ws_server_)
                    return;

                auto pid = player_id{player_eid.value};
                auto* plr = players_->get_player(pid);
                if (!plr || plr->connection.value == 0)
                    return;

                auto result_str = [](crafting::catch_result r) -> std::string_view
                {
                    switch (r)
                    {
                    case crafting::catch_result::success:
                        return "success";
                    case crafting::catch_result::failure:
                        return "failure";
                    case crafting::catch_result::canceled_moved:
                        return "canceled_moved";
                    case crafting::catch_result::canceled_stolen:
                        return "canceled_stolen";
                    case crafting::catch_result::canceled_timeout:
                        return "canceled_timeout";
                    case crafting::catch_result::no_fish:
                        return "no_fish";
                    case crafting::catch_result::no_rod:
                        return "no_rod";
                    case crafting::catch_result::rod_broken:
                        return "rod_broken";
                    default:
                        return "failure";
                    }
                }(result.result);

                auto msg =
                    network::make_fish_catch_response(player_eid, result_str, result.item_name, result.template_id);
                auto* conn = ws_server_->get_connection(plr->connection);
                if (conn && conn->is_open())
                {
                    conn->send(msg);
                }
            });
    }

    // Register and start ground item despawn timer (every 30 seconds, expire after 3 minutes)
    if (scheduler_ && world_)
    {
        scheduler_->register_task("ground_item_cleanup",
                                  "Remove ground items older than 3 minutes",
                                  duration_ms{30000},
                                  true,
                                  [this]() -> task_callback
                                  {
                                      return [this]()
                                      {
                                          auto expired = world_->remove_expired_ground_items(std::chrono::seconds(180));
                                          for (const auto& [map, pos, item] : expired)
                                          {
                                              if (players_ && ws_server_)
                                              {
                                                  network::ground_item_removed_data data{.picker_id = 0,
                                                                                         .picker_name = "",
                                                                                         .item_id = item.value,
                                                                                         .item_name = "",
                                                                                         .x = pos.x,
                                                                                         .y = pos.y};
                                                  auto msg = network::make_ground_item_removed(data);

                                                  auto players = players_->get_players_who_can_see(map, pos);
                                                  for (auto pid : players)
                                                  {
                                                      auto* p = players_->get_player(pid);
                                                      if (!p || p->connection.value == 0)
                                                          continue;
                                                      auto* conn = ws_server_->get_connection(p->connection);
                                                      if (conn && conn->is_open())
                                                      {
                                                          conn->send(msg);
                                                      }
                                                  }
                                              }

                                              if (item_)
                                              {
                                                  item_->destroy_item(item);
                                              }
                                          }

                                          if (!expired.empty())
                                          {
                                              LOG_DEBUG(bridge, "Despawned {} expired ground items", expired.size());
                                          }
                                      };
                                  });
        scheduler_->start_task("ground_item_cleanup");
    }

    // Register and start environment tick (weather cycling + broadcast to all players)
    if (scheduler_ && world_ && players_)
    {
        scheduler_->register_task("environment_tick",
                                  "Weather cycling and day/night sync",
                                  duration_ms{10000},
                                  true,
                                  [this]() -> task_callback
                                  {
                                      return [this]()
                                      {
                                          tick_weather();
                                          broadcast_environment_update();
                                      };
                                  });
        scheduler_->start_task("environment_tick");
    }

    LOG_INFO(bridge,
             "Game handlers initialized (chat: {}, admin: {}, combat: {}, npc: {}, inventory: {}, item: {})",
             social_ != nullptr ? "yes" : "no",
             admin_ != nullptr ? "yes" : "no",
             combat_ != nullptr ? "yes" : "no",
             npc_ != nullptr ? "yes" : "no",
             inventory_ != nullptr ? "yes" : "no",
             item_ != nullptr ? "yes" : "no");
}

void game_handlers::handle_message(connection_id conn_id, const network::json_message& msg)
{
    auto* perf = subsystems().get<perf::perf_stats_system>();
    PERF_TIMER(perf, perf::metric_category::message_handler);

    switch (msg.type)
    {
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

    // Fishing
    case network::json_message_type::fish_skill_request:
        handle_fish_skill_request(conn_id, msg);
        break;
    case network::json_message_type::fish_catch_request:
        handle_fish_catch_request(conn_id, msg);
        break;

    // Crusade
    case network::json_message_type::select_duty_request:
        handle_select_duty(conn_id, msg);
        break;
    case network::json_message_type::summon_war_unit_request:
        handle_summon_war_unit(conn_id, msg);
        break;
    case network::json_message_type::crusade_map_status:
        handle_crusade_map_status(conn_id, msg);
        break;
    case network::json_message_type::crusade_set_guild_teleport_request:
        handle_set_guild_teleport(conn_id, msg);
        break;
    case network::json_message_type::crusade_guild_teleport_request:
        handle_guild_teleport(conn_id, msg);
        break;

    // Friends
    case network::json_message_type::friend_request_send_request:
        handle_friend_request_send(conn_id, msg);
        break;
    case network::json_message_type::friend_request_accept_request:
        handle_friend_request_accept(conn_id, msg);
        break;
    case network::json_message_type::friend_request_decline_request:
        handle_friend_request_decline(conn_id, msg);
        break;
    case network::json_message_type::friend_request_cancel_request:
        handle_friend_request_cancel(conn_id, msg);
        break;
    case network::json_message_type::friend_remove_request:
        handle_friend_remove(conn_id, msg);
        break;
    case network::json_message_type::friend_block_request:
        handle_friend_block(conn_id, msg);
        break;
    case network::json_message_type::friend_unblock_request:
        handle_friend_unblock(conn_id, msg);
        break;
    case network::json_message_type::friend_list_request:
        handle_friend_list(conn_id, msg);
        break;

    // Guilds
    case network::json_message_type::guild_create_request:
        handle_guild_create(conn_id, msg);
        break;
    case network::json_message_type::guild_disband_request:
        handle_guild_disband(conn_id, msg);
        break;
    case network::json_message_type::guild_leave_request:
        handle_guild_leave(conn_id, msg);
        break;
    case network::json_message_type::guild_kick_request:
        handle_guild_kick(conn_id, msg);
        break;
    case network::json_message_type::guild_invite_request:
        handle_guild_invite(conn_id, msg);
        break;
    case network::json_message_type::guild_invite_respond_request:
        handle_guild_invite_respond(conn_id, msg);
        break;
    case network::json_message_type::guild_promote_request:
        handle_guild_promote(conn_id, msg);
        break;
    case network::json_message_type::guild_demote_request:
        handle_guild_demote(conn_id, msg);
        break;
    case network::json_message_type::guild_set_motd_request:
        handle_guild_set_motd(conn_id, msg);
        break;
    case network::json_message_type::guild_info_request:
        handle_guild_info(conn_id, msg);
        break;

    // Item usage
    case network::json_message_type::player_use_item_request:
        handle_player_use_item(conn_id, msg);
        break;

    // Combat mode
    case network::json_message_type::combat_mode_change_request:
        handle_combat_mode_change(conn_id, msg);
        break;

    // Item upgrade
    case network::json_message_type::item_upgrade_request:
        handle_item_upgrade(conn_id, msg);
        break;

    // Special ability
    case network::json_message_type::activate_ability_request:
        handle_activate_ability(conn_id, msg);
        break;

    // Death/Respawn
    case network::json_message_type::respawn_request:
        handle_respawn_request(conn_id, msg);
        break;

    default:
        LOG_WARN(bridge, "Unhandled game message type: {}", network::to_string(msg.type));
        send_error(conn_id, msg.seq, "unknown_message_type", "Message type not recognized by game handler");
        break;
    }
}

void game_handlers::handle_player_move(connection_id conn_id, const network::json_message& msg)
{
    auto* conn = require_in_game(conn_id, msg.seq);
    if (!conn)
        return;

    if (!players_)
    {
        send_error(conn_id, msg.seq, "internal_error", "Player system unavailable");
        return;
    }

    // Parse request
    auto data_result = network::player_move_request_data::from_json(msg.data);
    if (data_result.is_err())
    {
        send_error(conn_id, msg.seq, "invalid_request", data_result.error());
        return;
    }

    auto& data = data_result.value();
    auto pid = conn->player();

    // Get current player
    auto* player = players_->get_player(pid);
    if (!player)
    {
        send_error(conn_id, msg.seq, "invalid_player", "Player not found");
        return;
    }

    // Dead players cannot move
    if (player->is_dead())
    {
        send_error(conn_id, msg.seq, "dead", "Cannot move while dead");
        return;
    }

    // Validate client position matches server position (anti-cheat)
    if (std::abs(player->pos.x - data.x) > 1 || std::abs(player->pos.y - data.y) > 1)
    {
        LOG_WARN(bridge,
                 "Player {} position mismatch: client ({},{}) server ({},{})",
                 pid.value,
                 data.x,
                 data.y,
                 player->pos.x,
                 player->pos.y);
        // Clear destination - desync interrupts movement
        conn->clear_destination();
        // Send correction - client should resync
        conn->send(network::make_player_move_response(
            msg.seq, false, player->pos.x, player->pos.y, static_cast<int16_t>(player->facing), "position_desync"));
        return;
    }

    // Store old position for visibility calculations
    auto old_pos = player->pos;

    // Calculate target position from client's reported position
    // This ensures exactly 1-tile movement from where the client thinks they are
    // The desync check above ensures client position is within tolerance
    auto dir = static_cast<world::direction>(data.direction & 7); // Clamp to 0-7
    world::position client_pos{data.x, data.y};
    world::position target_pos = client_pos.move(dir);

    auto move_result = players_->try_move(pid, target_pos, dir);

    switch (move_result.result)
    {
    case player::player_system::move_result::success:
    {
        // Update destination on connection if provided
        if (data.dest_x.has_value() && data.dest_y.has_value())
        {
            conn->set_destination(*data.dest_x, *data.dest_y);
        }

        // Check if player has reached destination
        std::optional<int16_t> broadcast_dest_x;
        std::optional<int16_t> broadcast_dest_y;

        if (conn->has_destination())
        {
            if (target_pos.x == conn->dest_x() && target_pos.y == conn->dest_y())
            {
                // Reached destination - clear it
                conn->clear_destination();
            }
            else
            {
                // Still moving toward destination - include in broadcast
                broadcast_dest_x = conn->dest_x();
                broadcast_dest_y = conn->dest_y();
            }
        }

        // Send success response to moving player
        auto response = network::make_player_move_response(msg.seq, true, target_pos.x, target_pos.y, data.direction);
        conn->send(response);

        // Broadcast position to nearby players (with destination if still moving)
        broadcast_position_update(
            pid, target_pos.x, target_pos.y, data.direction, data.is_running, broadcast_dest_x, broadcast_dest_y);

        // Update entity visibility for all affected players
        update_entity_visibility(pid, old_pos, target_pos);

        LOG_DEBUG(bridge,
                  "Player {} {} to ({}, {})",
                  pid.value,
                  data.is_running ? "ran" : "walked",
                  target_pos.x,
                  target_pos.y);
        break;
    }

    case player::player_system::move_result::teleport:
    {
        // Clear destination - teleport interrupts movement
        conn->clear_destination();

        // Execute the teleport with full handling
        execute_player_teleport(pid,
                                conn_id,
                                msg.seq,
                                move_result.teleport_dest_map,
                                move_result.teleport_dest_pos,
                                move_result.teleport_dest_dir);
        break;
    }

    case player::player_system::move_result::blocked_terrain:
        // Clear destination - movement was interrupted
        conn->clear_destination();
        conn->send(network::make_player_move_response(
            msg.seq, false, player->pos.x, player->pos.y, static_cast<int16_t>(player->facing), "blocked_terrain"));
        break;

    case player::player_system::move_result::blocked_occupied:
        // Clear destination - movement was interrupted (bumped)
        conn->clear_destination();
        conn->send(network::make_player_move_response(
            msg.seq, false, player->pos.x, player->pos.y, static_cast<int16_t>(player->facing), "blocked_occupied"));
        break;

    case player::player_system::move_result::blocked_out_of_bounds:
        // Clear destination - movement was interrupted
        conn->clear_destination();
        conn->send(network::make_player_move_response(
            msg.seq, false, player->pos.x, player->pos.y, static_cast<int16_t>(player->facing), "out_of_bounds"));
        break;

    case player::player_system::move_result::blocked_status:
        // Clear destination - movement was interrupted
        conn->clear_destination();
        conn->send(network::make_player_move_response(
            msg.seq, false, player->pos.x, player->pos.y, static_cast<int16_t>(player->facing), "cannot_move"));
        break;

    case player::player_system::move_result::blocked_dead:
        // Clear destination - movement was interrupted
        conn->clear_destination();
        conn->send(network::make_player_move_response(
            msg.seq, false, player->pos.x, player->pos.y, static_cast<int16_t>(player->facing), "dead"));
        break;

    default:
        // Clear destination - movement was interrupted
        conn->clear_destination();
        conn->send(network::make_player_move_response(
            msg.seq, false, player->pos.x, player->pos.y, static_cast<int16_t>(player->facing), "move_failed"));
        break;
    }
}

void game_handlers::handle_player_stop(connection_id conn_id, const network::json_message& msg)
{
    auto* conn = require_in_game(conn_id, msg.seq);
    if (!conn)
        return;

    if (!players_)
    {
        send_error(conn_id, msg.seq, "internal_error", "Player system unavailable");
        return;
    }

    auto data_result = network::player_stop_request_data::from_json(msg.data);
    if (data_result.is_err())
    {
        send_error(conn_id, msg.seq, "invalid_request", data_result.error());
        return;
    }

    auto& data = data_result.value();
    auto pid = conn->player();

    auto* player = players_->get_player(pid);
    if (!player)
    {
        send_error(conn_id, msg.seq, "invalid_player", "Player not found");
        return;
    }

    // Clear destination - player explicitly stopped
    conn->clear_destination();

    // Update facing direction if provided
    if (data.direction.has_value())
    {
        player->facing = static_cast<world::direction>(data.direction.value() & 7);
    }

    auto direction = static_cast<int16_t>(player->facing);

    // Acknowledge the stop to the sender
    conn->send(network::make_player_stop_response(msg.seq, true, player->pos.x, player->pos.y, direction));

    // Broadcast position update to nearby players (not running = stopped, no destination)
    broadcast_position_update(pid, player->pos.x, player->pos.y, direction, false);

    LOG_DEBUG(bridge, "Player {} stopped at ({}, {}) facing {}", pid.value, player->pos.x, player->pos.y, direction);
}

void game_handlers::handle_player_attack(connection_id conn_id, const network::json_message& msg)
{
    auto* conn = require_in_game(conn_id, msg.seq);
    if (!conn)
        return;

    if (!players_)
    {
        send_error(conn_id, msg.seq, "internal_error", "Player system unavailable");
        return;
    }

    if (!combat_)
    {
        send_error(conn_id, msg.seq, "internal_error", "Combat system unavailable");
        return;
    }

    auto data_result = network::player_attack_request_data::from_json(msg.data);
    if (data_result.is_err())
    {
        send_error(conn_id, msg.seq, "invalid_request", data_result.error());
        return;
    }

    auto& data = data_result.value();
    auto pid = conn->player();

    auto* attacker = players_->get_player(pid);
    if (!attacker)
    {
        send_error(conn_id, msg.seq, "invalid_player", "Player not found");
        return;
    }

    // Check if attacker is alive
    if (attacker->is_dead())
    {
        network::attack_result_msg result{
            .hit = false, .target_id = data.target_id, .attacker_x = attacker->pos.x, .attacker_y = attacker->pos.y};
        conn->send(network::make_player_attack_response(msg.seq, false, &result, "attacker_dead"));
        return;
    }

    // Check if attacker has movement/action blocking status
    if (attacker->has_status(player::player_status::stunned) ||
        attacker->has_status(player::player_status::paralyzed) || attacker->has_status(player::player_status::frozen))
    {
        network::attack_result_msg result{
            .hit = false, .target_id = data.target_id, .attacker_x = attacker->pos.x, .attacker_y = attacker->pos.y};
        conn->send(network::make_player_attack_response(msg.seq, false, &result, "cannot_attack"));
        return;
    }

    // Enforce attack cooldown (100ms minimum between attacks)
    auto now = std::chrono::steady_clock::now();
    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - attacker->last_attack_time);
    if (elapsed.count() < 100)
    {
        network::attack_result_msg result{
            .hit = false, .target_id = data.target_id, .attacker_x = attacker->pos.x, .attacker_y = attacker->pos.y};
        conn->send(network::make_player_attack_response(msg.seq, false, &result, "attack_too_fast"));
        return;
    }

    // Peace mode — broadcast the attack action (client renders bow animation) but no damage
    if (!attacker->combat_mode)
    {
        attacker->last_attack_time = now;

        broadcast_player_action(*attacker,
                                {.entity_id = attacker->ecs_entity.id,
                                 .action = "attack",
                                 .direction = static_cast<int16_t>(attacker->facing),
                                 .target_id = data.target_id});

        network::attack_result_msg result{
            .hit = false, .target_id = data.target_id, .attacker_x = attacker->pos.x, .attacker_y = attacker->pos.y};
        conn->send(network::make_player_attack_response(msg.seq, true, &result));
        return;
    }

    // Validate target type
    if (data.tgt_type != network::target_type::player && data.tgt_type != network::target_type::npc)
    {
        network::attack_result_msg result{
            .hit = false, .target_id = data.target_id, .attacker_x = attacker->pos.x, .attacker_y = attacker->pos.y};
        conn->send(network::make_player_attack_response(msg.seq, false, &result, "invalid_target_type"));
        return;
    }

    // Resolve target — either player or NPC
    bool target_is_npc = (data.tgt_type == network::target_type::npc);
    entity::entity target_entity{data.target_id};
    world::position target_pos{};
    map_id target_map{};
    uint32_t target_broadcast_eid = data.target_id; // Entity ID for broadcast messages
    int16_t target_hp = 0;
    int16_t target_hp_max = 0;

    // Player target pointers (only set for PvP)
    std::optional<player_id> target_pid_opt;
    player::player* target_player = nullptr;

    // NPC target pointer (only set for PvE)
    npc::npc* target_npc = nullptr;

    if (target_is_npc)
    {
        if (!npc_)
        {
            send_error(conn_id, msg.seq, "internal_error", "NPC system unavailable");
            return;
        }
        target_npc = npc_->get_npc(target_entity);
        if (!target_npc || target_npc->is_dead())
        {
            // Dead or despawned NPC — allow the swing but deal 0 damage
            attacker->last_attack_time = now;
            network::attack_result_msg result{.hit = false,
                                              .target_id = data.target_id,
                                              .attacker_x = attacker->pos.x,
                                              .attacker_y = attacker->pos.y};
            conn->send(network::make_player_attack_response(msg.seq, true, &result));
            broadcast_player_action(*attacker,
                                    {.entity_id = attacker->ecs_entity.id,
                                     .action = "attack",
                                     .direction = static_cast<int16_t>(attacker->facing),
                                     .target_id = data.target_id});
            return;
        }

        // Cannot attack friendly NPCs (merchants, guards, trainers, etc.)
        if (target_npc->is_friendly())
        {
            network::attack_result_msg result{.hit = false,
                                              .target_id = data.target_id,
                                              .attacker_x = attacker->pos.x,
                                              .attacker_y = attacker->pos.y};
            conn->send(network::make_player_attack_response(msg.seq, false, &result, "cannot_attack_friendly"));
            return;
        }

        if (attacker->current_map != target_npc->current_map)
        {
            network::attack_result_msg result{.hit = false,
                                              .target_id = data.target_id,
                                              .attacker_x = attacker->pos.x,
                                              .attacker_y = attacker->pos.y};
            conn->send(network::make_player_attack_response(msg.seq, false, &result, "target_not_in_range"));
            return;
        }

        target_pos = target_npc->pos;
        target_map = target_npc->current_map;
        target_broadcast_eid = target_npc->entity_id.id;
        target_hp = static_cast<int16_t>(target_npc->hp);
        target_hp_max = static_cast<int16_t>(target_npc->max_hp);
    }
    else
    {
        // Player target
        target_pid_opt = players_->get_player_id_by_entity(target_entity);
        target_player = target_pid_opt ? players_->get_player(*target_pid_opt) : nullptr;
        if (!target_player)
        {
            network::attack_result_msg result{.hit = false,
                                              .target_id = data.target_id,
                                              .attacker_x = attacker->pos.x,
                                              .attacker_y = attacker->pos.y};
            conn->send(network::make_player_attack_response(msg.seq, false, &result, "target_not_found"));
            return;
        }

        if (target_player->is_dead())
        {
            // Dead player — allow the swing but deal 0 damage
            attacker->last_attack_time = now;
            network::attack_result_msg result{.hit = false,
                                              .target_id = data.target_id,
                                              .attacker_x = attacker->pos.x,
                                              .attacker_y = attacker->pos.y};
            conn->send(network::make_player_attack_response(msg.seq, true, &result));
            broadcast_player_action(*attacker,
                                    {.entity_id = attacker->ecs_entity.id,
                                     .action = "attack",
                                     .direction = static_cast<int16_t>(attacker->facing),
                                     .target_id = data.target_id});
            return;
        }

        if (attacker->current_map != target_player->current_map)
        {
            network::attack_result_msg result{.hit = false,
                                              .target_id = data.target_id,
                                              .attacker_x = attacker->pos.x,
                                              .attacker_y = attacker->pos.y};
            conn->send(network::make_player_attack_response(msg.seq, false, &result, "target_not_in_range"));
            return;
        }

        target_pos = target_player->pos;
        target_map = target_player->current_map;
        target_broadcast_eid = target_player->ecs_entity.id;
        target_hp = static_cast<int16_t>(target_player->hp);
        target_hp_max = static_cast<int16_t>(target_player->computed.max_hp);
    }

    // Update facing direction from client request
    attacker->facing = static_cast<world::direction>(data.direction);

    // Determine if this is a ranged attack (bow equipped)
    bool is_ranged = false;
    network::projectile_type projectile = network::projectile_type::none;
    const item_template* weapon_tmpl = nullptr;

    auto* item_reg = subsystems().get<item_registry>();
    if (item_reg && attacker->equipment.has_equipped(player::equip_slot::weapon))
    {
        weapon_tmpl = item_reg->get(attacker->equipment.weapon().id);
        if (weapon_tmpl && weapon_tmpl->is_bow())
        {
            is_ranged = true;
        }
    }

    // For ranged attacks: check arrows and determine projectile type
    uint32_t arrow_template_id = 0;
    int32_t ammo_remaining = -1;

    if (is_ranged)
    {
        if (!inventory_)
        {
            send_error(conn_id, msg.seq, "internal_error", "Inventory system unavailable");
            return;
        }

        // Find arrows in inventory - prefer poison arrows (78), then normal (77)
        auto attacker_entity = entity_id{pid.value};
        constexpr uint32_t poison_arrow_id = 78;
        constexpr uint32_t normal_arrow_id = 77;

        if (inventory_->has_item(attacker_entity, item_id{poison_arrow_id}))
        {
            arrow_template_id = poison_arrow_id;
            projectile = network::projectile_type::poison_arrow;
        }
        else if (inventory_->has_item(attacker_entity, item_id{normal_arrow_id}))
        {
            arrow_template_id = normal_arrow_id;
            projectile = network::projectile_type::arrow;
        }
        else
        {
            network::attack_result_msg result{.hit = false,
                                              .target_id = data.target_id,
                                              .attacker_x = attacker->pos.x,
                                              .attacker_y = attacker->pos.y,
                                              .is_ranged = true};
            conn->send(network::make_player_attack_response(msg.seq, false, &result, "no_ammo"));
            return;
        }
    }

    // Calculate distance
    int distance = attacker->pos.chebyshev_distance(target_pos);

    // Validate range based on attack type and weapon
    int max_range = 1; // Melee default
    if (is_ranged)
    {
        max_range = 10; // Bow range (standard Helbreath bow range)
    }
    else if (data.type == network::attack_type::dash)
    {
        max_range = 2; // Dash attack range
    }

    if (distance > max_range)
    {
        network::attack_result_msg result{.hit = false,
                                          .target_id = data.target_id,
                                          .attacker_x = attacker->pos.x,
                                          .attacker_y = attacker->pos.y,
                                          .is_ranged = is_ranged};
        conn->send(network::make_player_attack_response(msg.seq, false, &result, "target_not_in_range"));
        return;
    }

    // Ranged attacks require minimum distance (can't fire at melee range)
    if (is_ranged && distance < 2)
    {
        network::attack_result_msg result{.hit = false,
                                          .target_id = data.target_id,
                                          .attacker_x = attacker->pos.x,
                                          .attacker_y = attacker->pos.y,
                                          .is_ranged = true};
        conn->send(network::make_player_attack_response(msg.seq, false, &result, "target_too_close"));
        return;
    }

    // Consume arrow before processing attack
    if (is_ranged && arrow_template_id > 0)
    {
        inventory_->remove_item(entity_id{pid.value}, item_id{arrow_template_id}, 1);
        ammo_remaining = inventory_->count_item(entity_id{pid.value}, item_id{arrow_template_id});
    }

    // Build attack event - defender entity depends on target type
    combat::attack_event attack;
    attack.attacker = attacker->ecs_entity;
    attack.defender = target_is_npc ? target_entity : target_player->ecs_entity;
    attack.type = combat::damage_type::physical;
    attack.base_damage = 0; // Let combat_system calculate from stats
    attack.is_skill = false;
    attack.is_ranged = is_ranged;
    attack.is_dash = (data.type == network::attack_type::dash);
    attack.distance = distance;

    // Process the attack through combat system
    auto combat_result = combat_->process_attack(attack);

    // Update last attack time
    attacker->last_attack_time = now;

    // Re-read NPC HP after damage application (may have changed)
    if (target_is_npc && target_npc)
    {
        target_hp = static_cast<int16_t>(target_npc->hp);
        target_hp_max = static_cast<int16_t>(target_npc->max_hp);
    }
    else if (target_player)
    {
        target_hp = static_cast<int16_t>(target_player->hp);
        target_hp_max = static_cast<int16_t>(target_player->computed.max_hp);
    }

    // Build response
    network::attack_result_msg result{.hit = combat_result.hit.is_hit(),
                                      .critical = combat_result.hit.is_critical(),
                                      .damage = combat_result.hit.final_damage,
                                      .target_id = data.target_id,
                                      .target_hp = target_hp,
                                      .target_hp_max = target_hp_max,
                                      .attacker_x = attacker->pos.x,
                                      .attacker_y = attacker->pos.y,
                                      .is_ranged = is_ranged,
                                      .ammo_count = ammo_remaining,
                                      .ammo_template_id = arrow_template_id};

    // Send response to attacker
    conn->send(network::make_player_attack_response(msg.seq, true, &result));

    // Broadcast action animation to nearby players
    std::string action_type = (data.type == network::attack_type::dash) ? "dash_attack" : "attack";
    broadcast_player_action(*attacker,
                            {.entity_id = attacker->ecs_entity.id,
                             .action = std::move(action_type),
                             .direction = static_cast<int16_t>(attacker->facing),
                             .target_id = target_broadcast_eid});

    // Broadcast attack to players who can see the attacker
    auto nearby = players_->get_players_who_can_see(attacker->current_map, attacker->pos);

    // Create broadcast message (includes projectile info for ranged)
    auto broadcast_msg = network::make_combat_attack_broadcast(attacker->ecs_entity.id,
                                                               target_broadcast_eid,
                                                               attacker->pos.x,
                                                               attacker->pos.y,
                                                               target_pos.x,
                                                               target_pos.y,
                                                               static_cast<int16_t>(attacker->facing),
                                                               combat_result.hit.is_hit(),
                                                               combat_result.hit.is_critical(),
                                                               combat_result.hit.final_damage,
                                                               projectile);

    for (auto other_id : nearby)
    {
        auto* other = players_->get_player(other_id);
        if (!other || other->connection.value == 0)
            continue;

        auto* other_conn = ws_server_->get_connection(other->connection);
        if (other_conn && other_conn->is_open())
        {
            other_conn->send(broadcast_msg);
        }
    }

    // Weapon durability loss on hit
    if (combat_result.hit.is_hit() && item_ && attacker->equipment.has_equipped(player::equip_slot::weapon))
    {
        auto weapon_item_id = attacker->equipment.weapon().id;
        item_->damage_durability(weapon_item_id, 1);

        // Check if weapon broke
        auto* weapon = item_->get_item(weapon_item_id);
        if (weapon && weapon->is_broken())
        {
            auto old_equipped = players_->unequip_item(pid, player::equip_slot::weapon);
            players_->recalculate_equipment_modifiers(pid);
            broadcast_equipment_change(pid, player::equip_slot::weapon, item_id{});
            send_stat_update(conn_id, *attacker);
            LOG_INFO(bridge, "Player {} weapon broke during attack", pid.value);
        }
    }

    // Grant weapon skill exp on hit
    if (combat_result.hit.is_hit() && skills_)
    {
        auto weapon_skill = skill::skill_type::hand_attack;
        if (item_ && attacker->equipment.has_equipped(player::equip_slot::weapon))
        {
            auto* weapon_item = item_->get_item(attacker->equipment.weapon().id);
            if (weapon_item)
            {
                weapon_skill = skill::weapon_type_to_skill_type(weapon_item->weapon);
            }
        }
        skills_->record_skill_use(pid, weapon_skill);
    }

    // Flush deferred NPC deaths now that attack broadcasts have been sent
    // This ensures clients receive: attack_broadcast → entity_death → loot
    if (npc_ && combat_result.target_killed)
    {
        npc_->flush_pending_deaths();
    }

    LOG_DEBUG(bridge,
              "Player {} {} {} {} (hit={}, crit={}, dmg={}, target_hp={}, ranged={})",
              pid.value,
              is_ranged ? "shot" : "attacked",
              target_is_npc ? "npc" : "player",
              target_entity.id,
              combat_result.hit.is_hit(),
              combat_result.hit.is_critical(),
              combat_result.hit.final_damage,
              target_hp,
              is_ranged);
}

void game_handlers::handle_player_magic(connection_id conn_id, const network::json_message& msg)
{
    auto* conn = require_in_game(conn_id, msg.seq);
    if (!conn)
        return;

    if (!players_)
    {
        send_error(conn_id, msg.seq, "internal_error", "Player system unavailable");
        return;
    }

    auto data_result = network::player_magic_request_data::from_json(msg.data);
    if (data_result.is_err())
    {
        send_error(conn_id, msg.seq, "invalid_request", data_result.error());
        return;
    }

    auto& data = data_result.value();
    auto pid = conn->player();

    auto* player = players_->get_player(pid);
    if (!player)
    {
        send_error(conn_id, msg.seq, "invalid_player", "Player not found");
        return;
    }

    // Dead players cannot cast spells
    if (player->is_dead())
    {
        send_error(conn_id, msg.seq, "dead", "Cannot cast while dead");
        return;
    }

    if (!magic_)
    {
        send_error(conn_id, msg.seq, "internal_error", "Magic system unavailable");
        return;
    }

    // Build cast target from request data
    magic::cast_target target{};
    if (data.tgt_type == network::target_type::player || data.tgt_type == network::target_type::npc)
    {
        target.target = entity::entity{data.target_id};
    }
    if (data.target_x != 0 || data.target_y != 0)
    {
        target.target_pos = world::position{data.target_x, data.target_y};
    }

    // Look up spell to determine cast type
    auto sid = spell_id(static_cast<int>(data.spell_id));
    auto* spell_tmpl = magic_->get_spell(sid);
    if (!spell_tmpl)
    {
        conn->send(network::make_player_magic_response(msg.seq, false, nullptr, "unknown_spell"));
        return;
    }

    if (spell_tmpl->cast_time_ms > 0)
    {
        // Channeled/cast time spell
        auto cast_result = magic_->begin_cast(player->ecs_entity, sid, target);
        if (cast_result.is_err())
        {
            conn->send(network::make_player_magic_response(msg.seq, false, nullptr, cast_result.error()));
            return;
        }

        // Cast started - result will come via callback when cast completes
        if (skills_)
            skills_->record_skill_use(pid, skill::skill_type::magic);

        network::magic_result_msg result{.success = true,
                                         .spell_id = data.spell_id,
                                         .mana_cost = spell_tmpl->mana_cost,
                                         .damage = 0,
                                         .heal = 0,
                                         .target_id = data.target_id,
                                         .caster_mp = static_cast<int16_t>(player->mp)};
        conn->send(network::make_player_magic_response(msg.seq, true, &result));
    }
    else
    {
        // Instant cast
        auto cast_result = magic_->instant_cast(player->ecs_entity, sid, target);
        if (cast_result.is_err())
        {
            conn->send(network::make_player_magic_response(msg.seq, false, nullptr, cast_result.error()));
            return;
        }

        auto& effect = cast_result.value();
        if (effect.success && skills_)
        {
            skills_->record_skill_use(pid, skill::skill_type::magic);
        }

        network::magic_result_msg result{.success = effect.success,
                                         .spell_id = data.spell_id,
                                         .mana_cost = spell_tmpl->mana_cost,
                                         .damage = effect.damage_dealt,
                                         .heal = effect.heal_applied,
                                         .target_id = data.target_id,
                                         .caster_mp = static_cast<int16_t>(player->mp)};

        conn->send(network::make_player_magic_response(
            msg.seq,
            effect.success,
            &result,
            effect.success ? std::optional<std::string_view>{} : std::optional<std::string_view>{"cast_failed"}));
    }

    // Broadcast cast animation to nearby players
    broadcast_player_action(*player,
                            {.entity_id = player->ecs_entity.id,
                             .action = "magic",
                             .direction = static_cast<int16_t>(player->facing),
                             .target_id = data.target_id,
                             .spell_id = data.spell_id});

    // Flush deferred NPC deaths now that spell broadcasts have been sent
    if (npc_ && npc_->has_pending_deaths())
    {
        npc_->flush_pending_deaths();
    }

    LOG_DEBUG(bridge, "Player {} magic request (spell={}, target={})", pid.value, data.spell_id, data.target_id);
}

void game_handlers::handle_player_skill(connection_id conn_id, const network::json_message& msg)
{
    auto* conn = require_in_game(conn_id, msg.seq);
    if (!conn)
        return;

    if (!players_)
    {
        send_error(conn_id, msg.seq, "internal_error", "Player system unavailable");
        return;
    }

    auto data_result = network::player_skill_request_data::from_json(msg.data);
    if (data_result.is_err())
    {
        send_error(conn_id, msg.seq, "invalid_request", data_result.error());
        return;
    }

    auto& data = data_result.value();
    auto pid = conn->player();

    auto* player = players_->get_player(pid);
    if (!player)
    {
        send_error(conn_id, msg.seq, "invalid_player", "Player not found");
        return;
    }

    // Dead players cannot use skills
    if (player->is_dead())
    {
        send_error(conn_id, msg.seq, "dead", "Cannot use skills while dead");
        return;
    }

    // TODO: Implement actual skill use through skill_system
    // Placeholder response
    network::skill_result_msg result{
        .success = false, .skill_id = data.skill_id, .effect_value = 0, .target_id = data.target_id};

    conn->send(network::make_player_skill_response(msg.seq, false, &result, "not_implemented"));
    LOG_DEBUG(bridge, "Player {} skill request (skill={}, target={})", pid.value, data.skill_id, data.target_id);
}

void game_handlers::handle_player_pickup(connection_id conn_id, const network::json_message& msg)
{
    auto* conn = require_in_game(conn_id, msg.seq);
    if (!conn)
        return;

    if (!players_ || !world_ || !inventory_)
    {
        send_error(conn_id, msg.seq, "internal_error", "Required subsystems unavailable");
        return;
    }

    auto data_result = network::player_pickup_request_data::from_json(msg.data);
    if (data_result.is_err())
    {
        send_error(conn_id, msg.seq, "invalid_request", data_result.error());
        return;
    }

    auto& data = data_result.value();
    auto pid = conn->player();

    auto* player = players_->get_player(pid);
    if (!player)
    {
        send_error(conn_id, msg.seq, "invalid_player", "Player not found");
        return;
    }

    // Dead players cannot pick up items
    if (player->is_dead())
    {
        send_error(conn_id, msg.seq, "dead", "Cannot pick up items while dead");
        return;
    }

    // No items on ground - silently ignore (no-op)
    if (!world_->has_ground_items(player->current_map, player->pos))
    {
        return;
    }

    // Check if inventory has space
    auto player_entity = entity_id{pid.value};
    if (inventory_->is_full(player_entity))
    {
        send_error(conn_id, msg.seq, "inventory_full", "Cannot carry more items");
        return;
    }

    // Remove top-most item from ground
    auto item_id_opt = world_->remove_top_ground_item(player->current_map, player->pos);
    if (!item_id_opt.has_value())
    {
        send_error(conn_id, msg.seq, "item_not_found", "Item no longer available");
        return;
    }

    auto picked_item_id = item_id_opt.value();

    // Add item to inventory
    auto add_result = inventory_->add_item(player_entity, picked_item_id);
    if (add_result != inventory::inventory_result::success)
    {
        // Failed to add - put item back on ground
        world_->add_ground_item(player->current_map, player->pos, picked_item_id);
        send_error(conn_id, msg.seq, "inventory_full", "Failed to add item to inventory");
        return;
    }

    // Get item details for response
    std::string item_name = "Unknown";
    int16_t quantity = 1;
    item::item_attribute attr{};
    if (item_)
    {
        auto* itm = item_->get_item(picked_item_id);
        if (itm)
        {
            item_name = itm->name;
            quantity = itm->count;
            attr = itm->attribute;
            if (item_registry_)
            {
                if (auto* tmpl = item_registry_->get(itm->template_id))
                {
                    item_name = network::get_display_name(tmpl->name, attr);
                }
            }
        }
    }

    // Success! Send response to player
    network::pickup_result_msg result{
        .success = true,
        .item_id = picked_item_id.value,
        .item_name = item_name,
        .quantity = quantity,
        .inventory_slot = 0, // TODO: Get actual slot from inventory system
        .attribute = attr,
    };

    conn->send(network::make_player_pickup_response(msg.seq, true, &result, std::nullopt));

    // Broadcast pickup animation to nearby players
    broadcast_player_action(
        *player,
        {.entity_id = player->ecs_entity.id, .action = "pickup", .direction = static_cast<int16_t>(player->facing)});

    // Broadcast item removal to nearby players
    broadcast_ground_item_removed(pid, player->current_map, player->pos, picked_item_id);

    // If there's another item underneath, send the new top item to nearby players
    if (world_->has_ground_items(player->current_map, player->pos))
    {
        auto remaining = world_->get_ground_items(player->current_map, player->pos);
        if (!remaining.empty() && item_)
        {
            auto next_id = remaining.back();
            auto* next_itm = item_->get_item(next_id);
            if (next_itm)
            {
                std::string display_name = next_itm->name;
                if (item_registry_)
                {
                    if (auto* tmpl = item_registry_->get(next_itm->template_id))
                    {
                        display_name = network::get_display_name(tmpl->name, next_itm->attribute);
                    }
                }
                network::ground_item_spawn_data spawn_data{.item_id = next_id.value,
                                                           .template_id = next_itm->template_id.value,
                                                           .item_name = std::move(display_name),
                                                           .count = next_itm->count,
                                                           .x = player->pos.x,
                                                           .y = player->pos.y,
                                                           .attribute = next_itm->attribute,
                                                           .reason = "existing"};
                auto spawn_msg = network::make_ground_item_spawn(spawn_data);
                auto viewers = players_->get_players_who_can_see(player->current_map, player->pos);
                for (auto vid : viewers)
                {
                    auto* vp = players_->get_player(vid);
                    if (!vp || vp->connection.value == 0)
                        continue;
                    auto* vc = ws_server_->get_connection(vp->connection);
                    if (vc && vc->is_open())
                        vc->send(spawn_msg);
                }
            }
        }
    }

    // Audit item pickup
    if (audit_ && item_)
    {
        auto* itm = item_->get_item(picked_item_id);
        if (itm && itm->audited)
        {
            std::string map_name;
            if (auto* map = world_->get_map(player->current_map))
            {
                map_name = map->name();
            }
            audit_->log_item(player->character_id.value,
                             itm->name,
                             picked_item_id.value,
                             item_log_type::get,
                             itm->count,
                             0,
                             map_name,
                             player->pos.x,
                             player->pos.y);
        }
    }

    LOG_INFO(bridge,
             "Player {} picked up item {} ({}) at ({}, {})",
             pid.value,
             picked_item_id.value,
             item_name,
             player->pos.x,
             player->pos.y);
}

void game_handlers::handle_player_interact(connection_id conn_id, const network::json_message& msg)
{
    auto* conn = require_in_game(conn_id, msg.seq);
    if (!conn)
        return;

    // Dead players cannot interact with NPCs
    if (players_)
    {
        auto pid = conn->player();
        auto* player = players_->get_player(pid);
        if (player && player->is_dead())
        {
            send_error(conn_id, msg.seq, "dead", "Cannot interact while dead");
            return;
        }
    }

    auto data_result = network::player_interact_request_data::from_json(msg.data);
    if (data_result.is_err())
    {
        send_error(conn_id, msg.seq, "invalid_request", data_result.error());
        return;
    }

    auto& data = data_result.value();
    auto check = validate_npc_interaction(conn_id, msg.seq, data.target_id);
    if (!check.valid)
        return;

    auto* player = check.plr;
    auto* target = check.target_npc;

    // Route based on NPC type and available registries

    // 1. Check if NPC has a shop
    if (shop_registry_)
    {
        auto* shop = shop_registry_->get_shop(target->name);
        if (shop)
        {
            // Check territory restriction
            if (world_)
            {
                auto* map = world_->get_map(player->current_map);
                if (map && !npc::can_buy_in_territory(player->faction, map->location_name()))
                {
                    send_error(conn_id, msg.seq, "hostile_territory", "You cannot trade in hostile territory");
                    return;
                }
            }

            // Build shop item list for client
            auto* item_reg = subsystems().get<item_registry>();
            nlohmann::json shop_data;
            shop_data["shop_type"] = target->name;
            auto items_array = nlohmann::json::array();
            for (const auto& entry : shop->items)
            {
                nlohmann::json item_json;
                item_json["item_id"] = entry.item.value;
                item_json["count"] = entry.default_count;
                if (item_reg)
                {
                    if (auto* tmpl = item_reg->get(entry.item))
                    {
                        item_json["name"] = tmpl->name;
                        item_json["price"] = npc::calculate_buy_price(tmpl->price, 1, player->base.charisma);
                        item_json["base_price"] = tmpl->price;
                    }
                }
                items_array.push_back(std::move(item_json));
            }
            shop_data["items"] = std::move(items_array);

            network::interact_result_msg result{.success = true,
                                                .target_id = data.target_id,
                                                .interaction_type = "shop",
                                                .interaction_data = std::move(shop_data)};
            conn->send(network::make_player_interact_response(msg.seq, true, &result));
            LOG_DEBUG(bridge, "Player {} opened shop at NPC '{}'", player->id.value, target->name);
            return;
        }
    }

    // 2. Check if NPC is a banker/warehouse
    if (target->category == npc::npc_category::banker || target->category == npc::npc_category::warehouse)
    {
        // Send bank contents
        nlohmann::json bank_data;
        bank_data["npc_name"] = target->name;

        auto items_array = nlohmann::json::array();
        if (inventory_)
        {
            auto* bank = inventory_->get_bank(entity_id(player->id.value));
            if (bank)
            {
                for (int16_t i = 0; i < bank->capacity(); ++i)
                {
                    auto* slot = bank->get_slot(i);
                    if (slot && !slot->is_empty())
                    {
                        nlohmann::json slot_json;
                        slot_json["slot"] = i;
                        slot_json["item_id"] = slot->item.value;
                        slot_json["count"] = slot->count;
                        if (item_)
                        {
                            auto* itm = item_->get_item(slot->item);
                            if (itm)
                            {
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

        network::interact_result_msg result{.success = true,
                                            .target_id = data.target_id,
                                            .interaction_type = "bank",
                                            .interaction_data = std::move(bank_data)};
        conn->send(network::make_player_interact_response(msg.seq, true, &result));
        LOG_DEBUG(bridge, "Player {} opened bank at NPC '{}'", player->id.value, target->name);
        return;
    }

    // 3. Check if NPC has a dialog tree
    if (dialog_registry_)
    {
        auto* dialog = dialog_registry_->get_dialog(target->name);
        if (dialog)
        {
            auto* start = dialog_registry_->get_node(target->name, dialog->start_node);
            nlohmann::json dialog_data;
            dialog_data["npc_name"] = target->name;
            dialog_data["greeting"] = dialog->greeting;
            if (start)
            {
                dialog_data["node_id"] = start->id;
                dialog_data["text"] = start->text;
                auto opts = nlohmann::json::array();
                for (const auto& opt : start->options)
                {
                    nlohmann::json opt_json;
                    opt_json["label"] = opt.label;
                    opt_json["action"] = static_cast<int>(opt.action);
                    if (!opt.next_node.empty())
                    {
                        opt_json["next_node"] = opt.next_node;
                    }
                    opts.push_back(std::move(opt_json));
                }
                dialog_data["options"] = std::move(opts);
            }

            network::interact_result_msg result{.success = true,
                                                .target_id = data.target_id,
                                                .interaction_type = "dialog",
                                                .interaction_data = std::move(dialog_data)};
            conn->send(network::make_player_interact_response(msg.seq, true, &result));
            LOG_DEBUG(bridge, "Player {} opened dialog with NPC '{}'", player->id.value, target->name);
            return;
        }
    }

    // No interaction available
    network::interact_result_msg result{.success = false,
                                        .target_id = data.target_id,
                                        .interaction_type = "none",
                                        .interaction_data = nlohmann::json::object()};
    conn->send(network::make_player_interact_response(msg.seq, false, &result, "npc_no_interaction"));
    LOG_DEBUG(
        bridge, "Player {} interact request with NPC '{}' - no interaction available", player->id.value, target->name);
}

auto game_handlers::validate_npc_interaction(connection_id conn_id,
                                             uint32_t seq,
                                             uint32_t npc_entity_id) -> npc_interaction_check
{
    auto* conn = require_in_game(conn_id, seq);
    if (!conn)
        return {.valid = false, .error = "not_in_game"};

    if (!players_ || !npc_)
    {
        send_error(conn_id, seq, "internal_error", "Required systems unavailable");
        return {.valid = false, .error = "internal_error"};
    }

    auto pid = conn->player();
    auto* player = players_->get_player(pid);
    if (!player)
    {
        send_error(conn_id, seq, "invalid_player", "Player not found");
        return {.valid = false, .error = "invalid_player"};
    }

    auto* target = npc_->get_npc(entity::entity{npc_entity_id});
    if (!target)
    {
        send_error(conn_id, seq, "npc_not_found", "NPC not found");
        return {.valid = false, .error = "npc_not_found"};
    }

    if (!target->is_alive())
    {
        send_error(conn_id, seq, "npc_dead", "NPC is dead");
        return {.valid = false, .error = "npc_dead"};
    }

    if (!target->is_friendly())
    {
        send_error(conn_id, seq, "npc_hostile", "Cannot interact with hostile NPC");
        return {.valid = false, .error = "npc_hostile"};
    }

    // Check range (must be within 3 tiles)
    if (player->current_map != target->current_map || player->pos.distance(target->pos) > 3)
    {
        send_error(conn_id, seq, "too_far", "NPC is too far away");
        return {.valid = false, .error = "too_far"};
    }

    return {.plr = player, .target_npc = target, .valid = true};
}

void game_handlers::handle_shop_buy(connection_id conn_id, const network::json_message& msg)
{
    auto data_result = network::shop_buy_request_data::from_json(msg.data);
    if (data_result.is_err())
    {
        send_error(conn_id, msg.seq, "invalid_request", data_result.error());
        return;
    }
    auto& data = data_result.value();

    auto check = validate_npc_interaction(conn_id, msg.seq, data.npc_entity_id);
    if (!check.valid)
        return;

    if (!shop_registry_ || !inventory_ || !item_)
    {
        send_error(conn_id, msg.seq, "internal_error", "Shop system unavailable");
        return;
    }

    auto* shop = shop_registry_->get_shop(check.target_npc->name);
    if (!shop)
    {
        send_error(conn_id, msg.seq, "not_a_shop", "This NPC does not have a shop");
        return;
    }

    // Territory check
    if (world_)
    {
        auto* map = world_->get_map(check.plr->current_map);
        if (map && !npc::can_buy_in_territory(check.plr->faction, map->location_name()))
        {
            send_error(conn_id, msg.seq, "hostile_territory", "Cannot buy in hostile territory");
            return;
        }
    }

    // Verify the item is in the shop
    bool item_in_shop = false;
    for (const auto& entry : shop->items)
    {
        if (entry.item.value == data.item_template_id)
        {
            item_in_shop = true;
            break;
        }
    }
    if (!item_in_shop)
    {
        send_error(conn_id, msg.seq, "item_not_in_shop", "Item not available in this shop");
        return;
    }

    // Look up item template for price
    auto* item_reg = subsystems().get<item_registry>();
    if (!item_reg)
    {
        send_error(conn_id, msg.seq, "internal_error", "Item registry unavailable");
        return;
    }

    auto* tmpl = item_reg->get(item_id{data.item_template_id});
    if (!tmpl)
    {
        send_error(conn_id, msg.seq, "item_not_found", "Item template not found");
        return;
    }

    int16_t count = std::max<int16_t>(data.count, 1);
    int32_t total_price = npc::calculate_buy_price(tmpl->price, count, check.plr->base.charisma);

    auto owner_id = entity_id(check.plr->id.value);

    // Check gold
    if (!inventory_->has_gold(owner_id, total_price))
    {
        send_error(conn_id, msg.seq, "insufficient_gold", "Not enough gold");
        return;
    }

    // Check inventory space
    if (inventory_->is_full(owner_id))
    {
        send_error(conn_id, msg.seq, "inventory_full", "Inventory is full");
        return;
    }

    // Create the item
    auto create_result = item_->create_from_template(item_id{data.item_template_id}, count);
    if (create_result.is_err())
    {
        send_error(conn_id, msg.seq, "create_failed", "Failed to create item");
        return;
    }

    auto new_item_id = create_result.value();

    // Add to inventory
    auto add_result = inventory_->add_item(owner_id, new_item_id, count);
    if (add_result != inventory::inventory_result::success)
    {
        item_->destroy_item(new_item_id);
        send_error(conn_id, msg.seq, "add_failed", "Failed to add item to inventory");
        return;
    }

    // Deduct gold
    inventory_->remove_gold(owner_id, total_price);

    auto* conn = ws_server_->get_connection(conn_id);
    if (conn)
    {
        conn->send(network::make_shop_buy_response(
            msg.seq, true, tmpl->name, count, total_price, inventory_->get_gold(owner_id)));
    }

    // Audit shop buy
    if (audit_)
    {
        auto* bought = item_->get_item(new_item_id);
        if (bought && bought->audited)
        {
            audit_->log_item(check.plr->character_id.value, tmpl->name, new_item_id.value, item_log_type::buy, count);
        }
        audit_->log_gold(check.plr->character_id.value,
                         item_log_type::gold_shop_spend,
                         -total_price,
                         0,
                         {},
                         0,
                         0,
                         {{"item", tmpl->name}, {"count", count}});
    }

    LOG_DEBUG(bridge, "Player {} bought {}x '{}' for {} gold", check.plr->id.value, count, tmpl->name, total_price);
}

void game_handlers::handle_shop_sell(connection_id conn_id, const network::json_message& msg)
{
    auto data_result = network::shop_sell_request_data::from_json(msg.data);
    if (data_result.is_err())
    {
        send_error(conn_id, msg.seq, "invalid_request", data_result.error());
        return;
    }
    auto& data = data_result.value();

    auto check = validate_npc_interaction(conn_id, msg.seq, data.npc_entity_id);
    if (!check.valid)
        return;

    if (!shop_registry_ || !inventory_ || !item_)
    {
        send_error(conn_id, msg.seq, "internal_error", "Shop system unavailable");
        return;
    }

    auto* shop = shop_registry_->get_shop(check.target_npc->name);
    if (!shop)
    {
        send_error(conn_id, msg.seq, "not_a_shop", "This NPC does not have a shop");
        return;
    }

    auto owner_id = entity_id(check.plr->id.value);
    auto* inv = inventory_->get_inventory(owner_id);
    if (!inv)
    {
        send_error(conn_id, msg.seq, "no_inventory", "No inventory found");
        return;
    }

    auto* slot = inv->get_slot(data.inventory_slot);
    if (!slot || slot->is_empty())
    {
        send_error(conn_id, msg.seq, "empty_slot", "No item in that slot");
        return;
    }

    auto* itm = item_->get_item(slot->item);
    if (!itm)
    {
        send_error(conn_id, msg.seq, "item_not_found", "Item not found");
        return;
    }

    // Check category
    auto* item_reg = subsystems().get<item_registry>();
    if (item_reg)
    {
        auto* tmpl = item_reg->get(itm->template_id);
        if (tmpl && !npc::is_category_accepted(*shop, static_cast<uint8_t>(tmpl->category)))
        {
            send_error(conn_id, msg.seq, "category_rejected", "Shop does not buy this type of item");
            return;
        }
    }

    // Calculate sell price (quote)
    bool is_neutral = false;
    if (world_)
    {
        auto* map = world_->get_map(check.plr->current_map);
        if (map)
        {
            is_neutral = npc::is_neutral_territory(map->location_name());
        }
    }

    int32_t offered_price = 0;
    if (itm->is_equipment())
    {
        offered_price =
            npc::calculate_sell_price_equipment(itm->price, itm->durability, itm->max_durability, is_neutral);
    }
    else
    {
        offered_price = npc::calculate_sell_price_consumable(itm->price, itm->count, is_neutral);
    }

    auto* conn = ws_server_->get_connection(conn_id);
    if (conn)
    {
        conn->send(network::make_shop_sell_response(msg.seq, true, itm->name, offered_price, itm->durability));
    }
}

void game_handlers::handle_shop_sell_confirm(connection_id conn_id, const network::json_message& msg)
{
    auto data_result = network::shop_sell_confirm_request_data::from_json(msg.data);
    if (data_result.is_err())
    {
        send_error(conn_id, msg.seq, "invalid_request", data_result.error());
        return;
    }
    auto& data = data_result.value();

    auto check = validate_npc_interaction(conn_id, msg.seq, data.npc_entity_id);
    if (!check.valid)
        return;

    if (!shop_registry_ || !inventory_ || !item_)
    {
        send_error(conn_id, msg.seq, "internal_error", "Shop system unavailable");
        return;
    }

    auto* shop = shop_registry_->get_shop(check.target_npc->name);
    if (!shop)
    {
        send_error(conn_id, msg.seq, "not_a_shop", "This NPC does not have a shop");
        return;
    }

    auto owner_id = entity_id(check.plr->id.value);
    auto* inv = inventory_->get_inventory(owner_id);
    if (!inv)
    {
        send_error(conn_id, msg.seq, "no_inventory", "No inventory found");
        return;
    }

    auto* slot = inv->get_slot(data.inventory_slot);
    if (!slot || slot->is_empty())
    {
        send_error(conn_id, msg.seq, "empty_slot", "No item in that slot");
        return;
    }

    auto sell_item_id = slot->item;
    auto* itm = item_->get_item(sell_item_id);
    if (!itm)
    {
        send_error(conn_id, msg.seq, "item_not_found", "Item not found");
        return;
    }

    // Recalculate price (don't trust client)
    bool is_neutral = false;
    if (world_)
    {
        auto* map = world_->get_map(check.plr->current_map);
        if (map)
        {
            is_neutral = npc::is_neutral_territory(map->location_name());
        }
    }

    int32_t sell_price = 0;
    if (itm->is_equipment())
    {
        sell_price = npc::calculate_sell_price_equipment(itm->price, itm->durability, itm->max_durability, is_neutral);
    }
    else
    {
        sell_price = npc::calculate_sell_price_consumable(itm->price, itm->count, is_neutral);
    }

    if (sell_price <= 0)
    {
        send_error(conn_id, msg.seq, "worthless", "This item has no value");
        return;
    }

    // Save audit data before destroying item
    std::string sold_item_name = itm->name;
    int16_t sold_count = itm->count;
    bool sold_audited = itm->audited;

    // Remove item from inventory
    inv->clear_slot(data.inventory_slot);
    item_->destroy_item(sell_item_id);

    // Add gold
    inventory_->add_gold(owner_id, sell_price);

    auto* conn = ws_server_->get_connection(conn_id);
    if (conn)
    {
        conn->send(network::make_shop_sell_confirm_response(msg.seq, true, sell_price, inventory_->get_gold(owner_id)));
    }

    // Audit shop sell
    if (audit_)
    {
        if (sold_audited)
        {
            audit_->log_item(
                check.plr->character_id.value, sold_item_name, sell_item_id.value, item_log_type::sell, sold_count);
        }
        audit_->log_gold(check.plr->character_id.value,
                         item_log_type::gold_shop_earn,
                         sell_price,
                         0,
                         {},
                         0,
                         0,
                         {{"item", sold_item_name}});
    }

    LOG_DEBUG(bridge, "Player {} sold item for {} gold", check.plr->id.value, sell_price);
}

void game_handlers::handle_shop_repair(connection_id conn_id, const network::json_message& msg)
{
    auto data_result = network::shop_repair_request_data::from_json(msg.data);
    if (data_result.is_err())
    {
        send_error(conn_id, msg.seq, "invalid_request", data_result.error());
        return;
    }
    auto& data = data_result.value();

    auto check = validate_npc_interaction(conn_id, msg.seq, data.npc_entity_id);
    if (!check.valid)
        return;

    if (!shop_registry_ || !inventory_ || !item_)
    {
        send_error(conn_id, msg.seq, "internal_error", "Shop system unavailable");
        return;
    }

    auto* shop = shop_registry_->get_shop(check.target_npc->name);
    if (!shop)
    {
        send_error(conn_id, msg.seq, "not_a_shop", "This NPC does not have a shop");
        return;
    }

    auto owner_id = entity_id(check.plr->id.value);
    auto* inv = inventory_->get_inventory(owner_id);
    if (!inv)
    {
        send_error(conn_id, msg.seq, "no_inventory", "No inventory found");
        return;
    }

    auto* slot = inv->get_slot(data.inventory_slot);
    if (!slot || slot->is_empty())
    {
        send_error(conn_id, msg.seq, "empty_slot", "No item in that slot");
        return;
    }

    auto* itm = item_->get_item(slot->item);
    if (!itm)
    {
        send_error(conn_id, msg.seq, "item_not_found", "Item not found");
        return;
    }

    if (!itm->is_equipment())
    {
        send_error(conn_id, msg.seq, "not_repairable", "This item cannot be repaired");
        return;
    }

    // Check if this shop can repair this category
    auto* item_reg = subsystems().get<item_registry>();
    if (item_reg)
    {
        auto* tmpl = item_reg->get(itm->template_id);
        if (tmpl && !npc::is_category_repairable(*shop, static_cast<uint8_t>(tmpl->category)))
        {
            send_error(conn_id, msg.seq, "cant_repair_type", "Shop cannot repair this type of item");
            return;
        }
    }

    if (itm->durability >= itm->max_durability)
    {
        send_error(conn_id, msg.seq, "already_repaired", "Item is already at full durability");
        return;
    }

    int32_t repair_cost = npc::calculate_repair_cost(itm->price, itm->durability, itm->max_durability);

    auto* conn = ws_server_->get_connection(conn_id);
    if (conn)
    {
        conn->send(network::make_shop_repair_response(msg.seq, true, itm->name, repair_cost, itm->durability));
    }
}

void game_handlers::handle_shop_repair_confirm(connection_id conn_id, const network::json_message& msg)
{
    auto data_result = network::shop_repair_confirm_request_data::from_json(msg.data);
    if (data_result.is_err())
    {
        send_error(conn_id, msg.seq, "invalid_request", data_result.error());
        return;
    }
    auto& data = data_result.value();

    auto check = validate_npc_interaction(conn_id, msg.seq, data.npc_entity_id);
    if (!check.valid)
        return;

    if (!shop_registry_ || !inventory_ || !item_)
    {
        send_error(conn_id, msg.seq, "internal_error", "Shop system unavailable");
        return;
    }

    auto owner_id = entity_id(check.plr->id.value);
    auto* inv = inventory_->get_inventory(owner_id);
    if (!inv)
    {
        send_error(conn_id, msg.seq, "no_inventory", "No inventory found");
        return;
    }

    auto* slot = inv->get_slot(data.inventory_slot);
    if (!slot || slot->is_empty())
    {
        send_error(conn_id, msg.seq, "empty_slot", "No item in that slot");
        return;
    }

    auto* itm = item_->get_item(slot->item);
    if (!itm)
    {
        send_error(conn_id, msg.seq, "item_not_found", "Item not found");
        return;
    }

    if (itm->durability >= itm->max_durability)
    {
        send_error(conn_id, msg.seq, "already_repaired", "Item is already at full durability");
        return;
    }

    int32_t repair_cost = npc::calculate_repair_cost(itm->price, itm->durability, itm->max_durability);

    if (!inventory_->has_gold(owner_id, repair_cost))
    {
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
    if (conn)
    {
        conn->send(network::make_shop_repair_confirm_response(
            msg.seq, true, new_dur, repair_cost, inventory_->get_gold(owner_id)));
    }

    // Audit repair gold spend
    if (audit_)
    {
        if (itm && itm->audited)
        {
            audit_->log_item(check.plr->character_id.value, itm->name, slot->item.value, item_log_type::repair, 1);
        }
        audit_->log_gold(check.plr->character_id.value,
                         item_log_type::gold_shop_spend,
                         -repair_cost,
                         0,
                         {},
                         0,
                         0,
                         {{"action", "repair"}, {"item", itm ? itm->name : "unknown"}});
    }

    LOG_DEBUG(bridge, "Player {} repaired item for {} gold", check.plr->id.value, repair_cost);
}

void game_handlers::handle_bank_deposit(connection_id conn_id, const network::json_message& msg)
{
    auto data_result = network::bank_deposit_request_data::from_json(msg.data);
    if (data_result.is_err())
    {
        send_error(conn_id, msg.seq, "invalid_request", data_result.error());
        return;
    }
    auto& data = data_result.value();

    auto check = validate_npc_interaction(conn_id, msg.seq, data.npc_entity_id);
    if (!check.valid)
        return;

    if (check.target_npc->category != npc::npc_category::banker &&
        check.target_npc->category != npc::npc_category::warehouse)
    {
        send_error(conn_id, msg.seq, "not_a_bank", "This NPC is not a banker");
        return;
    }

    if (!inventory_ || !item_)
    {
        send_error(conn_id, msg.seq, "internal_error", "Inventory system unavailable");
        return;
    }

    auto owner_id = entity_id(check.plr->id.value);

    // Get item name before deposit (for response)
    auto* inv = inventory_->get_inventory(owner_id);
    if (!inv)
    {
        send_error(conn_id, msg.seq, "no_inventory", "No inventory found");
        return;
    }

    auto* slot = inv->get_slot(data.inventory_slot);
    if (!slot || slot->is_empty())
    {
        send_error(conn_id, msg.seq, "empty_slot", "No item in that slot");
        return;
    }

    std::string item_name;
    auto deposit_item_id = slot->item;
    bool deposit_audited = false;
    int16_t deposit_count = 1;
    if (auto* itm = item_->get_item(deposit_item_id))
    {
        item_name = itm->name;
        deposit_audited = itm->audited;
        deposit_count = itm->count;
    }

    auto result = inventory_->deposit_item(owner_id, data.inventory_slot);
    if (result != inventory::inventory_result::success)
    {
        send_error(conn_id, msg.seq, "deposit_failed", "Failed to deposit item");
        return;
    }

    auto* conn = ws_server_->get_connection(conn_id);
    if (conn)
    {
        conn->send(network::make_bank_deposit_response(msg.seq, true, item_name));
    }

    // Audit bank deposit
    if (audit_ && deposit_audited)
    {
        audit_->log_item(
            check.plr->character_id.value, item_name, deposit_item_id.value, item_log_type::deposit, deposit_count);
    }

    LOG_DEBUG(bridge, "Player {} deposited '{}'", check.plr->id.value, item_name);
}

void game_handlers::handle_bank_withdraw(connection_id conn_id, const network::json_message& msg)
{
    auto data_result = network::bank_withdraw_request_data::from_json(msg.data);
    if (data_result.is_err())
    {
        send_error(conn_id, msg.seq, "invalid_request", data_result.error());
        return;
    }
    auto& data = data_result.value();

    auto check = validate_npc_interaction(conn_id, msg.seq, data.npc_entity_id);
    if (!check.valid)
        return;

    if (check.target_npc->category != npc::npc_category::banker &&
        check.target_npc->category != npc::npc_category::warehouse)
    {
        send_error(conn_id, msg.seq, "not_a_bank", "This NPC is not a banker");
        return;
    }

    if (!inventory_ || !item_)
    {
        send_error(conn_id, msg.seq, "internal_error", "Inventory system unavailable");
        return;
    }

    auto owner_id = entity_id(check.plr->id.value);

    // Get item name before withdraw (for response)
    auto* bank = inventory_->get_bank(owner_id);
    if (!bank)
    {
        send_error(conn_id, msg.seq, "no_bank", "No bank storage found");
        return;
    }

    auto* slot = bank->get_slot(data.bank_slot);
    if (!slot || slot->is_empty())
    {
        send_error(conn_id, msg.seq, "empty_slot", "No item in that bank slot");
        return;
    }

    std::string item_name;
    auto withdraw_item_id = slot->item;
    bool withdraw_audited = false;
    int16_t withdraw_count = 1;
    if (auto* itm = item_->get_item(withdraw_item_id))
    {
        item_name = itm->name;
        withdraw_audited = itm->audited;
        withdraw_count = itm->count;
    }

    auto result = inventory_->withdraw_item(owner_id, data.bank_slot);
    if (result != inventory::inventory_result::success)
    {
        std::string error_msg = "Failed to withdraw item";
        if (result == inventory::inventory_result::inventory_full)
        {
            error_msg = "Inventory is full";
        }
        send_error(conn_id, msg.seq, "withdraw_failed", error_msg);
        return;
    }

    auto* conn = ws_server_->get_connection(conn_id);
    if (conn)
    {
        conn->send(network::make_bank_withdraw_response(msg.seq, true, item_name));
    }

    // Audit bank withdraw
    if (audit_ && withdraw_audited)
    {
        audit_->log_item(
            check.plr->character_id.value, item_name, withdraw_item_id.value, item_log_type::retrieve, withdraw_count);
    }

    LOG_DEBUG(bridge, "Player {} withdrew '{}'", check.plr->id.value, item_name);
}

void game_handlers::handle_dialog_choice(connection_id conn_id, const network::json_message& msg)
{
    auto data_result = network::dialog_choice_request_data::from_json(msg.data);
    if (data_result.is_err())
    {
        send_error(conn_id, msg.seq, "invalid_request", data_result.error());
        return;
    }
    auto& data = data_result.value();

    auto check = validate_npc_interaction(conn_id, msg.seq, data.npc_entity_id);
    if (!check.valid)
        return;

    if (!dialog_registry_)
    {
        send_error(conn_id, msg.seq, "internal_error", "Dialog system unavailable");
        return;
    }

    auto* dialog = dialog_registry_->get_dialog(check.target_npc->name);
    if (!dialog)
    {
        send_error(conn_id, msg.seq, "no_dialog", "NPC has no dialog");
        return;
    }

    auto* node = dialog_registry_->get_node(check.target_npc->name, data.node_id);
    if (!node)
    {
        send_error(conn_id, msg.seq, "invalid_node", "Dialog node not found");
        return;
    }

    if (data.choice_index < 0 || data.choice_index >= static_cast<int16_t>(node->options.size()))
    {
        send_error(conn_id, msg.seq, "invalid_choice", "Choice index out of range");
        return;
    }

    auto& chosen = node->options[static_cast<size_t>(data.choice_index)];
    auto* conn = ws_server_->get_connection(conn_id);
    if (!conn)
        return;

    switch (chosen.action)
    {
    case npc::dialog_action::goto_node:
    {
        auto* next = dialog_registry_->get_node(check.target_npc->name, chosen.next_node);
        if (!next)
        {
            send_error(conn_id, msg.seq, "invalid_node", "Next dialog node not found");
            return;
        }
        std::vector<network::dialog_option_msg> opts;
        for (const auto& opt : next->options)
        {
            std::string action_str;
            switch (opt.action)
            {
            case npc::dialog_action::goto_node:
                action_str = "goto_node";
                break;
            case npc::dialog_action::close:
                action_str = "close";
                break;
            case npc::dialog_action::open_shop:
                action_str = "open_shop";
                break;
            case npc::dialog_action::open_bank:
                action_str = "open_bank";
                break;
            case npc::dialog_action::open_quests:
                action_str = "open_quests";
                break;
            case npc::dialog_action::offer_citizenship:
                action_str = "offer_citizenship";
                break;
            case npc::dialog_action::select_crusade_job:
                action_str = "select_crusade_job";
                break;
            case npc::dialog_action::claim_rewards:
                action_str = "claim_rewards";
                break;
            case npc::dialog_action::open_manufacturing:
                action_str = "open_manufacturing";
                break;
            case npc::dialog_action::open_alchemy:
                action_str = "open_alchemy";
                break;
            }
            opts.push_back({opt.label, action_str, opt.next_node});
        }
        conn->send(network::make_dialog_choice_response(msg.seq, true, "goto_node", next->id, next->text, opts));
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

    case npc::dialog_action::open_manufacturing:
    {
        // Send manufacturing recipe list directly
        conn->send(network::make_dialog_choice_response(msg.seq, true, "open_manufacturing"));
        if (manufacturing_)
        {
            auto recipes = manufacturing_->get_available_recipes(entity_id{check.plr->id.value});
            nlohmann::json recipe_list = nlohmann::json::array();
            for (const auto* recipe : recipes)
            {
                nlohmann::json r;
                r["id"] = recipe->id;
                r["name"] = recipe->result;
                r["skill_req"] = recipe->skill_req;
                r["success_rate"] = recipe->success_rate;
                nlohmann::json ings = nlohmann::json::array();
                for (const auto& ing : recipe->ingredients)
                {
                    ings.push_back({{"item_id", ing.item_id}, {"count", ing.count}});
                }
                r["ingredients"] = ings;
                recipe_list.push_back(r);
            }
            conn->send(network::make_manufacture_list_response(0, recipe_list));
        }
        break;
    }

    case npc::dialog_action::open_alchemy:
    {
        conn->send(network::make_dialog_choice_response(msg.seq, true, "open_alchemy"));
        if (alchemy_)
        {
            auto recipes = alchemy_->get_available_recipes(entity_id{check.plr->id.value});
            nlohmann::json recipe_list = nlohmann::json::array();
            for (const auto* recipe : recipes)
            {
                nlohmann::json r;
                r["id"] = recipe->id;
                r["name"] = recipe->result;
                r["skill_limit"] = recipe->skill_limit;
                r["difficulty"] = recipe->difficulty;
                nlohmann::json ings = nlohmann::json::array();
                for (const auto& ing : recipe->ingredients)
                {
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
    case npc::dialog_action::claim_rewards:
    {
        // Stub actions - infrastructure is in place for when backend systems are ready
        std::string action_name;
        switch (chosen.action)
        {
        case npc::dialog_action::open_quests:
            action_name = "open_quests";
            break;
        case npc::dialog_action::offer_citizenship:
            action_name = "offer_citizenship";
            break;
        case npc::dialog_action::select_crusade_job:
            action_name = "select_crusade_job";
            break;
        case npc::dialog_action::claim_rewards:
            action_name = "claim_rewards";
            break;
        default:
            action_name = "unknown";
            break;
        }
        conn->send(network::make_dialog_choice_response(
            msg.seq, true, "not_implemented", "", "This feature is not yet available."));
        LOG_DEBUG(bridge, "Player {} triggered stub dialog action '{}'", check.plr->id.value, action_name);
        break;
    }
    }
}

void game_handlers::broadcast_position_update(player_id moved_player,
                                              int16_t x,
                                              int16_t y,
                                              int16_t direction,
                                              bool is_running,
                                              std::optional<int16_t> dest_x,
                                              std::optional<int16_t> dest_y)
{
    auto* perf = subsystems().get<perf::perf_stats_system>();
    PERF_TIMER(perf, perf::metric_category::broadcast);

    if (!players_ || !ws_server_)
        return;

    auto* player = players_->get_player(moved_player);
    if (!player)
        return;

    // Get players who can see this movement
    auto nearby = players_->get_players_who_can_see(player->current_map, player->pos);

    auto update_msg =
        network::make_player_position_update(player->ecs_entity.id, x, y, direction, is_running, dest_x, dest_y);

    for (auto other_id : nearby)
    {
        if (other_id == moved_player)
            continue;

        auto* other = players_->get_player(other_id);
        if (!other || other->connection.value == 0)
            continue;

        auto* other_conn = ws_server_->get_connection(other->connection);
        if (other_conn && other_conn->is_open())
        {
            other_conn->send(update_msg);
        }
    }

    // Forward to admin spectators
    for (auto admin_conn : ws_server_->get_admin_subscribers(player->current_map))
    {
        ws_server_->send(admin_conn, update_msg);
    }
}

void game_handlers::update_entity_visibility(player_id moved_player,
                                             const world::position& old_pos,
                                             const world::position& new_pos)
{
    auto* perf = subsystems().get<perf::perf_stats_system>();
    PERF_TIMER(perf, perf::metric_category::visibility_update);

    if (!players_ || !ws_server_)
        return;

    auto* player = players_->get_player(moved_player);
    if (!player)
        return;

    // Get all players who could possibly be affected (use max radius as coarse filter)
    constexpr int coarse_radius = network::max_visibility_radius + 5;
    auto old_nearby = players_->get_players_in_range(moved_player, coarse_radius);

    // For each player in the expanded range, check visibility changes
    for (auto other_id : old_nearby)
    {
        if (other_id == moved_player)
            continue;

        auto* other = players_->get_player(other_id);
        if (!other || other->connection.value == 0)
            continue;

        // Check if other player could see the moved player at old/new positions
        // Uses other's rectangular visibility (sees_all always sees everything)
        auto orx = other->visibility_radius_x;
        auto ory = other->visibility_radius_y;
        bool was_visible =
            other->sees_all || (std::abs(old_pos.x - other->pos.x) <= orx && std::abs(old_pos.y - other->pos.y) <= ory);
        bool is_visible =
            other->sees_all || (std::abs(new_pos.x - other->pos.x) <= orx && std::abs(new_pos.y - other->pos.y) <= ory);

        auto* other_conn = ws_server_->get_connection(other->connection);
        if (!other_conn || !other_conn->is_open())
            continue;

        if (!was_visible && is_visible)
        {
            // Player just came into view - send entity_spawn to other
            other_conn->send(network::make_entity_spawn(
                0,
                build_player_spawn(
                    *player, player_hostility(other->faction, player->faction), item_, item_registry_, effects_)));

            // Also send the other player info to the moving player
            auto* my_conn = ws_server_->get_connection(player->connection);
            if (my_conn && my_conn->is_open())
            {
                my_conn->send(network::make_entity_spawn(
                    0,
                    build_player_spawn(
                        *other, player_hostility(player->faction, other->faction), item_, item_registry_, effects_)));
            }
        }
        else if (was_visible && !is_visible)
        {
            // Player just left view - send entity_despawn to other
            auto despawn_msg = network::make_entity_despawn(0, player->ecs_entity.id);
            other_conn->send(despawn_msg);

            // Also despawn the other player from the moving player's view
            auto* my_conn = ws_server_->get_connection(player->connection);
            if (my_conn && my_conn->is_open())
            {
                auto other_despawn_msg = network::make_entity_despawn(0, other->ecs_entity.id);
                my_conn->send(other_despawn_msg);
            }
        }
    }
}

void game_handlers::send_error(connection_id conn_id,
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

auto game_handlers::get_connection(connection_id conn_id) -> network::ws_connection*
{
    if (!ws_server_)
        return nullptr;
    return ws_server_->get_connection(conn_id);
}

auto game_handlers::require_in_game(connection_id conn_id, uint32_t seq) -> network::ws_connection*
{
    auto* conn = get_connection(conn_id);
    if (!conn)
    {
        LOG_WARN(bridge, "Message from unknown connection {}", conn_id.value);
        return nullptr;
    }

    if (conn->state() != network::ws_connection_state::in_game)
    {
        LOG_WARN(bridge, "In-game request from non-in-game connection {}", conn_id.value);
        send_error(conn_id, seq, "not_in_game", "You must be in-game to perform this action");
        return nullptr;
    }

    return conn;
}

// ========== Chat Handling ==========

namespace
{

// Parse chat channel from prefix or explicit channel name
auto parse_chat_channel(std::string_view content, const std::optional<std::string>& explicit_channel)
    -> std::pair<social::chat_channel, std::string>
{
    // If explicit channel provided, use that
    if (explicit_channel.has_value())
    {
        const auto& ch = *explicit_channel;
        if (ch == "local")
            return {social::chat_channel::local, std::string(content)};
        if (ch == "shout")
            return {social::chat_channel::shout, std::string(content)};
        if (ch == "guild")
            return {social::chat_channel::guild, std::string(content)};
        if (ch == "party")
            return {social::chat_channel::party, std::string(content)};
        if (ch == "whisper")
            return {social::chat_channel::whisper, std::string(content)};
        if (ch == "global")
            return {social::chat_channel::global, std::string(content)};
        if (ch == "trade")
            return {social::chat_channel::trade, std::string(content)};
        if (ch == "faction")
            return {social::chat_channel::faction, std::string(content)};
        // Default to local for unknown channels
        return {social::chat_channel::local, std::string(content)};
    }

    // Check for prefix-based channel
    if (!content.empty())
    {
        char prefix = content[0];
        std::string msg_content = std::string(content.substr(1));

        switch (prefix)
        {
        case '!': // Shout - server-wide
            return {social::chat_channel::shout, msg_content};
        case '@': // Guild
            return {social::chat_channel::guild, msg_content};
        case '$': // Party
            return {social::chat_channel::party, msg_content};
        case '#': // Say override (local) during whisper mode
            return {social::chat_channel::local, msg_content};
        case '%': // Trade channel
            return {social::chat_channel::trade, msg_content};
        case '~': // Faction chat
            return {social::chat_channel::faction, msg_content};
        case '^': // GM chat
            return {social::chat_channel::gm, msg_content};
        default:
            break;
        }
    }

    // Default to local chat
    return {social::chat_channel::local, std::string(content)};
}

auto channel_to_string(social::chat_channel channel) -> std::string
{
    switch (channel)
    {
    case social::chat_channel::local:
        return "local";
    case social::chat_channel::global:
        return "global";
    case social::chat_channel::guild:
        return "guild";
    case social::chat_channel::party:
        return "party";
    case social::chat_channel::whisper:
        return "whisper";
    case social::chat_channel::trade:
        return "trade";
    case social::chat_channel::shout:
        return "shout";
    case social::chat_channel::alliance:
        return "alliance";
    case social::chat_channel::faction:
        return "faction";
    case social::chat_channel::gm:
        return "gm";
    case social::chat_channel::system:
        return "system";
    default:
        return "unknown";
    }
}

auto format_timestamp(std::chrono::system_clock::time_point tp) -> std::string
{
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

auto filter_result_to_string(social::filter_result result) -> std::string_view
{
    switch (result)
    {
    case social::filter_result::allowed:
        return "allowed";
    case social::filter_result::censored:
        return "censored";
    case social::filter_result::blocked:
        return "blocked";
    case social::filter_result::rate_limited:
        return "rate_limited";
    default:
        return "unknown";
    }
}

} // namespace

void game_handlers::handle_chat_message(connection_id conn_id, const network::json_message& msg)
{
    auto* conn = require_in_game(conn_id, msg.seq);
    if (!conn)
        return;

    if (!social_ || !players_)
    {
        send_error(conn_id, msg.seq, "internal_error", "Chat system unavailable");
        return;
    }

    // Parse request
    auto data_result = network::chat_message_request_data::from_json(msg.data);
    if (data_result.is_err())
    {
        send_error(conn_id, msg.seq, "invalid_request", data_result.error());
        return;
    }

    auto& data = data_result.value();
    auto pid = conn->player();

    auto* player = players_->get_player(pid);
    if (!player)
    {
        send_error(conn_id, msg.seq, "invalid_player", "Player not found");
        return;
    }

    // Parse channel and extract message content
    auto [channel, content] = parse_chat_channel(data.content, data.channel);

    // Validate content is not empty after prefix removal
    if (content.empty())
    {
        conn->send(network::make_chat_message_response(msg.seq, false, "empty_message"));
        return;
    }

    // Intercept /commands from chat and route to command handler
    if (!content.empty() && content[0] == '/')
    {
        auto parsed = admin::command_parser::parse(content, '/');
        if (parsed.valid)
        {
            // Build a synthetic command_request message and route it
            network::json_message cmd_msg;
            cmd_msg.type = network::json_message_type::command_request;
            cmd_msg.seq = msg.seq;
            cmd_msg.data = nlohmann::json{{"command", parsed.command_name}, {"args", parsed.raw_args}};
            handle_command(conn_id, cmd_msg);
            return;
        }
    }

    // Handle based on channel type
    social::filter_result result;

    switch (channel)
    {
    case social::chat_channel::local:
        result = social_->send_local_chat(pid, content, player->current_map, player->pos.x, player->pos.y);
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

    case social::chat_channel::whisper:
    {
        // Need recipient for whisper
        if (!data.recipient_name.has_value() || data.recipient_name->empty())
        {
            conn->send(network::make_chat_message_response(msg.seq, false, "no_recipient"));
            return;
        }

        // Find recipient by name
        auto* recipient = players_->get_player_by_name(*data.recipient_name);
        if (!recipient)
        {
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
        if (!player->is_gm())
        {
            conn->send(network::make_chat_message_response(msg.seq, false, "not_authorized"));
            return;
        }
        result = social_->send_global_chat(pid, content);
        break;

    default:
        result = social_->send_local_chat(pid, content, player->current_map, player->pos.x, player->pos.y);
        break;
    }

    // Send response to sender
    if (result == social::filter_result::allowed || result == social::filter_result::censored)
    {
        conn->send(network::make_chat_message_response(msg.seq, true));
    }
    else
    {
        conn->send(network::make_chat_message_response(msg.seq, false, filter_result_to_string(result)));
    }

    LOG_DEBUG(bridge, "Player {} sent {} chat: {}", pid.value, channel_to_string(channel), content.substr(0, 50));
}

namespace
{

auto guild_result_string(social::guild_result r) -> std::string
{
    switch (r)
    {
    case social::guild_result::success:
        return "success";
    case social::guild_result::guild_not_found:
        return "guild_not_found";
    case social::guild_result::player_not_member:
        return "not_member";
    case social::guild_result::insufficient_permissions:
        return "insufficient_permissions";
    case social::guild_result::guild_full:
        return "guild_full";
    case social::guild_result::already_in_guild:
        return "already_in_guild";
    case social::guild_result::name_taken:
        return "name_taken";
    case social::guild_result::invalid_name:
        return "invalid_name";
    case social::guild_result::cannot_kick_self:
        return "cannot_kick_self";
    case social::guild_result::cannot_kick_higher_rank:
        return "cannot_kick_higher_rank";
    case social::guild_result::cannot_promote_higher:
        return "cannot_promote_higher";
    case social::guild_result::insufficient_gold:
        return "insufficient_gold";
    default:
        return "unknown_error";
    }
}

} // namespace

void game_handlers::handle_command(connection_id conn_id, const network::json_message& msg)
{
    auto* conn = require_in_game(conn_id, msg.seq);
    if (!conn)
        return;

    if (!players_)
    {
        send_error(conn_id, msg.seq, "internal_error", "Player system unavailable");
        return;
    }

    // Parse request
    auto data_result = network::command_request_data::from_json(msg.data);
    if (data_result.is_err())
    {
        send_error(conn_id, msg.seq, "invalid_request", data_result.error());
        return;
    }

    auto& data = data_result.value();
    auto pid = conn->player();

    auto* player = players_->get_player(pid);
    if (!player)
    {
        send_error(conn_id, msg.seq, "invalid_player", "Player not found");
        return;
    }

    LOG_DEBUG(bridge, "Player {} command: {} args={}", pid.value, data.command, data.args.size());

    // Simple built-in commands (available to all players)
    if (data.command == "online")
    {
        // Return online player count
        auto count = players_->active_player_count();
        conn->send(network::make_command_response(
            msg.seq, true, data.command, std::to_string(count) + " players online", nlohmann::json{{"count", count}}));
        return;
    }

    if (data.command == "time")
    {
        // Return server time
        auto now = std::chrono::system_clock::now();
        conn->send(network::make_command_response(
            msg.seq, true, data.command, format_timestamp(now), nlohmann::json{{"timestamp", format_timestamp(now)}}));
        return;
    }

    if (data.command == "pos" || data.command == "position")
    {
        // Return player position
        std::string map_name = "unknown";
        if (world_)
        {
            auto* current_map = world_->get_map(player->current_map);
            if (current_map)
            {
                map_name = std::string(current_map->name());
            }
        }
        conn->send(network::make_command_response(msg.seq,
                                                  true,
                                                  data.command,
                                                  "Position: " + map_name + " (" + std::to_string(player->pos.x) +
                                                      ", " + std::to_string(player->pos.y) + ")",
                                                  nlohmann::json{{"x", player->pos.x},
                                                                 {"y", player->pos.y},
                                                                 {"map", player->current_map.value},
                                                                 {"map_name", map_name}}));
        return;
    }

    // Guild commands (available to all players)
    if (data.command == "gcreate" && social_)
    {
        if (data.args.empty())
        {
            conn->send(network::make_command_response(msg.seq, false, data.command, "Usage: /gcreate <guild_name>"));
            return;
        }
        std::string guild_name = data.args[0];
        // Use first 3 chars of name as default tag
        std::string guild_tag = guild_name.substr(0, std::min(guild_name.size(), size_t{3}));
        if (data.args.size() > 1)
        {
            guild_tag = data.args[1];
        }

        auto result = social_->create_guild(pid, guild_name, guild_tag);
        if (result.is_err())
        {
            conn->send(network::make_command_response(
                msg.seq, false, data.command, "Failed to create guild: " + guild_result_string(result.error())));
            return;
        }
        auto gid = result.value();
        auto* g = social_->get_guild(gid);
        if (g)
        {
            player->guild_name = g->name;
            player->guild_tag = g->tag;
            player->guild_rank = static_cast<uint8_t>(social::guild_rank::guild_master);
        }
        conn->send(network::make_command_response(
            msg.seq, true, data.command, "Guild '" + guild_name + "' [" + guild_tag + "] created!"));
        return;
    }

    if (data.command == "gdisband" && social_)
    {
        auto gid = social_->get_player_guild(pid);
        if (!gid.is_valid())
        {
            conn->send(network::make_command_response(msg.seq, false, data.command, "You are not in a guild"));
            return;
        }
        auto* g = social_->get_guild(gid);
        if (!g)
        {
            conn->send(network::make_command_response(msg.seq, false, data.command, "Guild not found"));
            return;
        }
        std::string guild_name = g->name;

        // Collect member PIDs before disband
        std::vector<player_id> member_pids;
        for (const auto& m : g->members)
        {
            member_pids.push_back(m.player);
        }

        auto result = social_->disband_guild(pid, gid);
        if (result != social::guild_result::success)
        {
            conn->send(
                network::make_command_response(msg.seq, false, data.command, "Failed: " + guild_result_string(result)));
            return;
        }

        // Clear guild fields for all members
        for (auto mid : member_pids)
        {
            auto* member_plr = players_->get_player(mid);
            if (member_plr)
            {
                member_plr->guild_name.clear();
                member_plr->guild_tag.clear();
                member_plr->guild_rank = 0;
            }
        }

        // Notify all online members
        auto disbanded_msg = network::make_guild_update("guild_disbanded", guild_name);
        for (auto mid : member_pids)
        {
            auto* member_conn = ws_server_->get_connection_by_player(mid);
            if (member_conn)
            {
                member_conn->send(disbanded_msg);
            }
        }

        conn->send(network::make_command_response(
            msg.seq, true, data.command, "Guild '" + guild_name + "' has been disbanded"));
        return;
    }

    if (data.command == "ginvite" && social_)
    {
        if (data.args.empty())
        {
            conn->send(network::make_command_response(msg.seq, false, data.command, "Usage: /ginvite <player_name>"));
            return;
        }

        auto* target = players_->get_player_by_name(data.args[0]);
        if (!target)
        {
            conn->send(network::make_command_response(msg.seq, false, data.command, "Player not found"));
            return;
        }

        auto gid = social_->get_player_guild(pid);
        if (!gid.is_valid())
        {
            conn->send(network::make_command_response(msg.seq, false, data.command, "You are not in a guild"));
            return;
        }

        auto result = social_->invite_to_guild(pid, gid, target->id);
        if (result != social::guild_result::success)
        {
            conn->send(
                network::make_command_response(msg.seq, false, data.command, "Failed: " + guild_result_string(result)));
            return;
        }

        // Push invite notification to target
        auto* g = social_->get_guild(gid);
        auto* target_conn = ws_server_->get_connection_by_player(target->id);
        if (target_conn && g)
        {
            target_conn->send(network::make_guild_invite_received(g->name, g->tag, player->name));
        }

        conn->send(network::make_command_response(
            msg.seq, true, data.command, "Invited " + data.args[0] + " to your guild. They must /gaccept to join."));
        return;
    }

    if (data.command == "gkick" && social_)
    {
        if (data.args.empty())
        {
            conn->send(network::make_command_response(msg.seq, false, data.command, "Usage: /gkick <player_name>"));
            return;
        }

        auto* target = players_->get_player_by_name(data.args[0]);
        if (!target)
        {
            conn->send(network::make_command_response(msg.seq, false, data.command, "Player not found"));
            return;
        }

        auto gid = social_->get_player_guild(pid);
        if (!gid.is_valid())
        {
            conn->send(network::make_command_response(msg.seq, false, data.command, "You are not in a guild"));
            return;
        }

        auto* g = social_->get_guild(gid);
        std::string guild_name = g ? g->name : "";

        auto result = social_->kick_from_guild(pid, target->id);
        if (result != social::guild_result::success)
        {
            conn->send(
                network::make_command_response(msg.seq, false, data.command, "Failed: " + guild_result_string(result)));
            return;
        }

        // Clear target's guild fields
        target->guild_name.clear();
        target->guild_tag.clear();
        target->guild_rank = 0;

        // Notify kicked player
        auto* target_conn = ws_server_->get_connection_by_player(target->id);
        if (target_conn)
        {
            target_conn->send(network::make_guild_update("you_were_kicked", guild_name, player->name));
        }

        // Broadcast to guild
        broadcast_guild_update(gid, "member_kicked", guild_name, target->name);

        conn->send(
            network::make_command_response(msg.seq, true, data.command, "Kicked " + data.args[0] + " from the guild"));
        return;
    }

    if (data.command == "gaccept" && social_)
    {
        auto accept_result = social_->accept_guild_invite(pid);
        if (accept_result.is_err())
        {
            std::string err = (accept_result.error() == social::guild_result::guild_not_found)
                                  ? "No pending guild invite"
                                  : guild_result_string(accept_result.error());
            conn->send(network::make_command_response(msg.seq, false, data.command, err));
            return;
        }

        auto gid = accept_result.value();
        auto* g = social_->get_guild(gid);
        if (g)
        {
            player->guild_name = g->name;
            player->guild_tag = g->tag;
            player->guild_rank = static_cast<uint8_t>(social::guild_rank::recruit);

            broadcast_guild_update(gid, "member_joined", g->name, player->name);

            conn->send(network::make_command_response(
                msg.seq, true, data.command, "You joined guild '" + g->name + "' [" + g->tag + "]!"));
        }
        else
        {
            conn->send(network::make_command_response(msg.seq, true, data.command, "You joined the guild!"));
        }
        return;
    }

    if (data.command == "gdecline" && social_)
    {
        auto result = social_->decline_guild_invite(pid);
        if (result != social::guild_result::success)
        {
            conn->send(network::make_command_response(msg.seq, false, data.command, "No pending guild invite"));
            return;
        }
        conn->send(network::make_command_response(msg.seq, true, data.command, "Guild invite declined"));
        return;
    }

    if (data.command == "gquit" && social_)
    {
        auto gid = social_->get_player_guild(pid);
        if (!gid.is_valid())
        {
            conn->send(network::make_command_response(msg.seq, false, data.command, "You are not in a guild"));
            return;
        }

        // Check if guild master
        auto* g = social_->get_guild(gid);
        if (g && g->master == pid)
        {
            conn->send(network::make_command_response(
                msg.seq,
                false,
                data.command,
                "Guild masters cannot quit. Use /gdisband or transfer leadership first."));
            return;
        }

        std::string guild_name = g ? g->name : "";

        auto result = social_->leave_guild(pid);
        if (result != social::guild_result::success)
        {
            conn->send(
                network::make_command_response(msg.seq, false, data.command, "Failed: " + guild_result_string(result)));
            return;
        }

        player->guild_name.clear();
        player->guild_tag.clear();
        player->guild_rank = 0;

        broadcast_guild_update(gid, "member_left", guild_name, player->name);

        conn->send(network::make_command_response(msg.seq, true, data.command, "You left the guild"));
        return;
    }

    // Route to admin system for admin commands
    if (admin_)
    {
        // Build command string: /<command> [args...]
        std::string cmd_string = "/" + data.command;
        for (const auto& arg : data.args)
        {
            cmd_string += " " + arg;
        }

        auto result = admin_->execute(pid, cmd_string);

        // Send response
        conn->send(network::make_command_response(msg.seq, result.success, data.command, result.message));
        return;
    }

    // Unknown command (no admin system available)
    conn->send(network::make_command_response(msg.seq, false, data.command, "Unknown command: " + data.command));
}

void game_handlers::on_chat_message(const social::chat_message_event& event)
{
    if (!ws_server_ || !players_)
        return;

    const auto& msg = event.message;

    // Build broadcast data
    network::chat_message_broadcast_data broadcast;
    broadcast.channel = channel_to_string(msg.channel);
    broadcast.sender_id = msg.sender.value;
    broadcast.sender_name = msg.sender_name;
    broadcast.content = msg.content;
    broadcast.timestamp = format_timestamp(msg.timestamp);

    // Add flags
    if (social::has_flag(msg.flags, social::chat_flags::emote))
    {
        broadcast.flags.push_back("emote");
    }
    if (social::has_flag(msg.flags, social::chat_flags::censored))
    {
        broadcast.flags.push_back("censored");
    }
    if (social::has_flag(msg.flags, social::chat_flags::system))
    {
        broadcast.flags.push_back("system");
    }
    if (social::has_flag(msg.flags, social::chat_flags::gm))
    {
        broadcast.flags.push_back("gm");
    }

    // Route based on channel
    switch (msg.channel)
    {
    case social::chat_channel::local:
        // Send to nearby players
        send_chat_to_nearby(msg.sender, 15, broadcast); // 15 tile range
        break;

    case social::chat_channel::shout:
    case social::chat_channel::global:
    {
        // Send to all online players
        auto all_players = players_->get_all_players();
        for (auto pid : all_players)
        {
            send_chat_to_player(pid, broadcast);
        }
        break;
    }

    case social::chat_channel::guild:
    {
        // Send to guild members
        if (!social_)
            break;

        auto guild_id = social_->get_player_guild(msg.sender);
        if (!guild_id.is_valid())
            break;

        auto* guild = social_->get_guild(guild_id);
        if (!guild)
            break;

        for (const auto& member : guild->members)
        {
            send_chat_to_player(member.player, broadcast);
        }
        break;
    }

    case social::chat_channel::party:
    {
        // Send to party members
        if (!social_)
            break;

        auto party_id = social_->get_player_party(msg.sender);
        if (!party_id.is_valid())
            break;

        auto* party = social_->get_party(party_id);
        if (!party)
            break;

        for (const auto& member : party->members)
        {
            send_chat_to_player(member.player, broadcast);
        }
        break;
    }

    case social::chat_channel::whisper:
    {
        // Send to sender (confirmation) and recipient
        broadcast.recipient_name = msg.recipient_name;
        send_chat_to_player(msg.sender, broadcast);    // Echo to sender
        send_chat_to_player(msg.recipient, broadcast); // Send to recipient
        break;
    }

    case social::chat_channel::system:
    {
        // System messages to specific recipient
        send_chat_to_player(msg.recipient, broadcast);
        break;
    }

    case social::chat_channel::trade:
    case social::chat_channel::faction:
    case social::chat_channel::alliance:
    {
        // Broadcast to all for now
        auto all_players = players_->get_all_players();
        for (auto pid : all_players)
        {
            send_chat_to_player(pid, broadcast);
        }
        break;
    }

    case social::chat_channel::gm:
    {
        // Only deliver to players with GM permissions
        auto all_players = players_->get_all_players();
        for (auto pid : all_players)
        {
            auto* p = players_->get_player(pid);
            if (p && p->is_gm())
            {
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

void game_handlers::send_chat_to_player(player_id target, const network::chat_message_broadcast_data& data)
{
    if (!ws_server_ || !players_)
        return;

    auto* player = players_->get_player(target);
    if (!player || player->connection.value == 0)
        return;

    auto* conn = ws_server_->get_connection(player->connection);
    if (!conn || !conn->is_open())
        return;

    // Check if player has this channel enabled
    if (social_)
    {
        auto* settings = social_->get_chat_settings(target);
        if (settings)
        {
            // Convert string channel back to enum for settings check
            social::chat_channel ch = social::chat_channel::local;
            if (data.channel == "local")
                ch = social::chat_channel::local;
            else if (data.channel == "global")
                ch = social::chat_channel::global;
            else if (data.channel == "guild")
                ch = social::chat_channel::guild;
            else if (data.channel == "party")
                ch = social::chat_channel::party;
            else if (data.channel == "whisper")
                ch = social::chat_channel::whisper;
            else if (data.channel == "trade")
                ch = social::chat_channel::trade;
            else if (data.channel == "shout")
                ch = social::chat_channel::shout;
            else if (data.channel == "system")
                ch = social::chat_channel::system;

            if (!settings->is_channel_enabled(ch) && ch != social::chat_channel::system)
            {
                return; // Player has this channel disabled
            }

            // Check if sender is blocked
            if (settings->is_player_blocked(player_id{data.sender_id}))
            {
                return; // Player has blocked the sender
            }
        }
    }

    conn->send(network::make_chat_message_broadcast(data));
}

void game_handlers::send_chat_to_nearby(player_id sender,
                                        int16_t range,
                                        const network::chat_message_broadcast_data& data)
{
    if (!players_)
        return;

    auto nearby = players_->get_players_in_range(sender, range);
    for (auto pid : nearby)
    {
        send_chat_to_player(pid, data);
    }
}

// ========== View Range Handling ==========

void game_handlers::handle_set_view_range(connection_id conn_id, const network::json_message& msg)
{
    auto* conn = require_in_game(conn_id, msg.seq);
    if (!conn)
        return;

    if (!players_)
    {
        send_error(conn_id, msg.seq, "internal_error", "Player system unavailable");
        return;
    }

    auto data_result = network::set_view_range_request_data::from_json(msg.data);
    if (data_result.is_err())
    {
        send_error(conn_id, msg.seq, "invalid_request", data_result.error());
        return;
    }

    auto& data = data_result.value();
    auto pid = conn->player();

    auto* player = players_->get_player(pid);
    if (!player)
    {
        send_error(conn_id, msg.seq, "invalid_player", "Player not found");
        return;
    }

    // GM override prevents client from recalculating radii (e.g. after /setviewrange)
    if (player->gm_view_override)
    {
        LOG_DEBUG(bridge, "Player {} set_view_range ignored (GM override active)", pid.value);
        return;
    }

    auto radii = network::calculate_visibility_radius(data.screen_width, data.screen_height);
    player->visibility_radius_x = radii.x;
    player->visibility_radius_y = radii.y;

    LOG_DEBUG(bridge,
              "Player {} updated visibility radii to {}x{} (screen {}x{})",
              pid.value,
              radii.x,
              radii.y,
              data.screen_width,
              data.screen_height);
}

// ========== Teleportation Handling ==========

void game_handlers::execute_player_teleport(player_id pid,
                                            connection_id conn_id,
                                            uint32_t seq,
                                            const std::string& dest_map,
                                            const world::position& dest_pos,
                                            world::direction dest_dir)
{
    auto* perf = subsystems().get<perf::perf_stats_system>();
    PERF_TIMER(perf, perf::metric_category::visibility_update);

    if (!players_ || !ws_server_ || !world_)
    {
        send_error(conn_id, seq, "internal_error", "System unavailable");
        return;
    }

    auto* player = players_->get_player(pid);
    if (!player)
    {
        send_error(conn_id, seq, "invalid_player", "Player not found");
        return;
    }

    // Store old position for despawn notifications
    auto old_map_id = player->current_map;
    auto old_pos = player->pos;

    // Execute the teleport
    auto teleport_result = players_->execute_teleport(pid, dest_map, dest_pos, dest_dir);
    if (!teleport_result.success)
    {
        send_error(conn_id, seq, "teleport_failed", teleport_result.error);
        return;
    }

    // Use the resolved position from the teleport result (handles -1,-1 → initial point)
    auto resolved_pos = teleport_result.new_pos;

    // Check if this is a cross-map teleport
    bool is_cross_map = (old_map_id != teleport_result.new_map);

    // Despawn from players who could see OLD position
    auto old_viewers = players_->get_players_who_can_see(old_map_id, old_pos);
    auto despawn_msg = network::make_entity_despawn(0, player->ecs_entity.id);
    for (auto other_id : old_viewers)
    {
        if (other_id == pid)
            continue;

        auto* other = players_->get_player(other_id);
        if (!other || other->connection.value == 0)
            continue;

        auto* other_conn = ws_server_->get_connection(other->connection);
        if (other_conn && other_conn->is_open())
        {
            other_conn->send(despawn_msg);
        }
    }

    // Forward despawn to admin spectators on old map
    for (auto admin_conn : ws_server_->get_admin_subscribers(old_map_id))
    {
        ws_server_->send(admin_conn, despawn_msg);
    }

    // Build and send player_teleport message
    network::player_teleport_msg teleport_msg{.dest_map = dest_map,
                                              .dest_x = resolved_pos.x,
                                              .dest_y = resolved_pos.y,
                                              .dest_dir = static_cast<int16_t>(dest_dir)};

    auto* conn = ws_server_->get_connection(conn_id);
    if (conn && conn->is_open())
    {
        conn->send(network::make_player_teleport(seq, teleport_msg));

        // Send full stat update so client resyncs HP/MP/SP/XP/gold after teleport
        send_full_stat_update(conn_id, *player);

        // Send individual entity_spawn / npc_spawn for visible entities at destination
        send_visible_entity_spawns_at(conn, pid, teleport_result.new_map, resolved_pos);

        // If cross-map, also send map_teleporters for the new map
        if (is_cross_map)
        {
            auto* new_map = world_->get_map(teleport_result.new_map);
            if (new_map)
            {
                send_map_teleporters(conn_id, *new_map);
            }
        }

        // Send visible ground items at destination
        send_visible_ground_items(
            conn_id, teleport_result.new_map, resolved_pos, player->visibility_radius_x, player->visibility_radius_y);

        // Send environment update for destination map
        if (scheduler_)
        {
            auto* dest_map_ptr = world_->get_map(teleport_result.new_map);
            if (dest_map_ptr)
            {
                auto& clock = scheduler_->game_time();
                network::environment_update_data env{.hour = static_cast<uint8_t>(clock.hour()),
                                                     .minute = static_cast<uint8_t>(clock.minute()),
                                                     .is_day = clock.is_day(),
                                                     .weather = static_cast<uint8_t>(dest_map_ptr->weather())};
                if (dest_map_ptr->config().is_fixed_day_mode)
                {
                    env.is_day = true;
                    env.weather = 0;
                }
                conn->send(network::make_environment_update(env));
            }
        }

        // Send updated skills data
        send_skills_data(conn_id, pid);
    }

    // Spawn to players who can see NEW position
    auto new_viewers = players_->get_players_who_can_see(teleport_result.new_map, resolved_pos);

    for (auto other_id : new_viewers)
    {
        if (other_id == pid)
            continue;

        auto* other = players_->get_player(other_id);
        if (!other || other->connection.value == 0)
            continue;

        auto* other_conn = ws_server_->get_connection(other->connection);
        if (!other_conn || !other_conn->is_open())
            continue;

        other_conn->send(network::make_entity_spawn(
            0,
            build_player_spawn(
                *player, player_hostility(other->faction, player->faction), item_, item_registry_, effects_)));
    }

    // Forward spawn to admin spectators on new map (neutral perspective)
    auto admin_spawn_msg =
        network::make_entity_spawn(0, build_player_spawn(*player, "neutral", item_, item_registry_, effects_));
    for (auto admin_conn : ws_server_->get_admin_subscribers(teleport_result.new_map))
    {
        ws_server_->send(admin_conn, admin_spawn_msg);
    }

    LOG_INFO(bridge, "Player {} teleported to {} ({}, {})", pid.value, dest_map, resolved_pos.x, resolved_pos.y);
}

void game_handlers::send_map_teleporters(connection_id conn_id, const world::map& map)
{
    if (!ws_server_)
        return;

    auto* conn = ws_server_->get_connection(conn_id);
    if (!conn || !conn->is_open())
        return;

    const auto& teleports = map.get_all_teleports();

    network::map_teleporters_msg teleporters_msg;
    teleporters_msg.map_name = std::string(map.name());

    for (const auto& [pos, dest] : teleports)
    {
        network::teleporter_info_msg tp_info{.id = (static_cast<uint32_t>(pos.x) << 16) |
                                                   static_cast<uint32_t>(static_cast<uint16_t>(pos.y)),
                                             .x = pos.x,
                                             .y = pos.y,
                                             .dest_map = dest.dest_map,
                                             .dest_x = dest.dest_x,
                                             .dest_y = dest.dest_y,
                                             .dest_dir = static_cast<int16_t>(dest.dest_dir)};
        teleporters_msg.teleporters.push_back(tp_info);
    }

    conn->send(network::make_map_teleporters(teleporters_msg));

    LOG_DEBUG(bridge,
              "Sent {} teleporters for map {} to connection {}",
              teleporters_msg.teleporters.size(),
              map.name(),
              conn_id.value);
}

void game_handlers::broadcast_teleporter_update(map_id map,
                                                const std::string& action,
                                                const world::position& pos,
                                                const world::teleport_dest* dest)
{
    if (!players_ || !ws_server_ || !world_)
        return;

    auto* m = world_->get_map(map);
    if (!m)
        return;

    network::teleporter_update_msg update_msg;
    update_msg.action = action;
    update_msg.map_name = std::string(m->name());
    update_msg.teleporter.id =
        (static_cast<uint32_t>(pos.x) << 16) | static_cast<uint32_t>(static_cast<uint16_t>(pos.y));
    update_msg.teleporter.x = pos.x;
    update_msg.teleporter.y = pos.y;

    if (dest)
    {
        update_msg.teleporter.dest_map = dest->dest_map;
        update_msg.teleporter.dest_x = dest->dest_x;
        update_msg.teleporter.dest_y = dest->dest_y;
        update_msg.teleporter.dest_dir = static_cast<int16_t>(dest->dest_dir);
    }

    auto msg = network::make_teleporter_update(update_msg);

    // Send to all players on this map
    players_->for_each_player(
        [&](player_id pid, const player::player& p)
        {
            if (p.current_map != map)
                return;
            if (p.connection.value == 0)
                return;

            auto* conn = ws_server_->get_connection(p.connection);
            if (conn && conn->is_open())
            {
                conn->send(msg);
            }
        });

    LOG_DEBUG(bridge, "Broadcast teleporter {} at ({},{}) on map {}", action, pos.x, pos.y, m->name());
}

auto game_handlers::build_visible_entities_at(map_id map,
                                              const world::position& pos,
                                              int visibility_radius_x,
                                              int visibility_radius_y) -> std::vector<network::visible_entity_msg>
{
    auto* perf = subsystems().get<perf::perf_stats_system>();
    PERF_TIMER(perf, perf::metric_category::visibility_update);

    std::vector<network::visible_entity_msg> entities;

    if (!players_ || !world_)
        return entities;

    auto* m = world_->get_map(map);
    if (!m)
        return entities;

    // Use max of X/Y as coarse filter for spatial index, then rect-filter below
    auto coarse_radius = std::max(visibility_radius_x, visibility_radius_y);
    auto nearby_entities = m->get_entities_in_range(pos, coarse_radius);

    for (auto eid : nearby_entities)
    {
        // Spatial index stores ecs_entity.index() - resolve via entity lookup
        auto* p = players_->get_player_by_entity(entity::entity{eid.value});
        if (p)
        {
            // Rect-filter: skip entities outside the rectangular viewport
            if (std::abs(p->pos.x - pos.x) > visibility_radius_x || std::abs(p->pos.y - pos.y) > visibility_radius_y)
                continue;

            entities.push_back(build_player_spawn(*p, "neutral", item_, item_registry_, effects_));
        }
    }

    // Include NPCs from npc_system
    if (npc_)
    {
        npc_->for_each_npc_on_map(map,
                                  [&](auto /*id*/, const hb::npc::npc& n)
                                  {
                                      if (n.is_dead())
                                          return; // Dead NPCs handled separately below

                                      // Rect-filter: skip NPCs outside the rectangular viewport
                                      if (std::abs(n.pos.x - pos.x) > visibility_radius_x ||
                                          std::abs(n.pos.y - pos.y) > visibility_radius_y)
                                          return;

                                      entities.push_back(build_npc_spawn(n, "neutral"));
                                  });

        // Include dead NPCs (corpses visible within range)
        npc_->for_each_npc_on_map(map,
                                  [&](auto /*id*/, const hb::npc::npc& n)
                                  {
                                      if (!n.is_dead())
                                          return; // Only dead NPCs in this pass

                                      // Rect-filter: skip NPCs outside the rectangular viewport
                                      if (std::abs(n.pos.x - pos.x) > visibility_radius_x ||
                                          std::abs(n.pos.y - pos.y) > visibility_radius_y)
                                          return;

                                      entities.push_back(build_npc_spawn(n, "neutral", true));
                                  });
    }

    return entities;
}

void game_handlers::send_visible_entity_spawns_at(network::ws_connection* conn,
                                                  player_id viewer,
                                                  map_id map,
                                                  const world::position& pos)
{
    if (!conn || !conn->is_open() || !players_ || !world_)
        return;

    auto* player = players_->get_player(viewer);
    if (!player)
        return;

    int rx = player->visibility_radius_x;
    int ry = player->visibility_radius_y;

    auto* m = world_->get_map(map);
    if (!m)
        return;

    // Send individual entity_spawn for nearby players
    auto coarse_radius = std::max(rx, ry);
    auto nearby_entities = m->get_entities_in_range(pos, coarse_radius);

    for (auto eid : nearby_entities)
    {
        auto* p = players_->get_player_by_entity(entity::entity{eid.value});
        if (!p)
            continue;
        if (p == player)
            continue; // Skip self

        if (std::abs(p->pos.x - pos.x) > rx || std::abs(p->pos.y - pos.y) > ry)
            continue;

        conn->send(network::make_entity_spawn(
            0, build_player_spawn(*p, player_hostility(player->faction, p->faction), item_, item_registry_, effects_)));
    }

    // Send individual npc_spawn for nearby NPCs
    if (npc_)
    {
        npc_->for_each_npc_on_map(map,
                                  [&](auto /*id*/, const hb::npc::npc& n)
                                  {
                                      if (n.is_dead())
                                          return;

                                      if (std::abs(n.pos.x - pos.x) > rx || std::abs(n.pos.y - pos.y) > ry)
                                          return;

                                      network::npc_spawn_data data{
                                          .entity_id = n.entity_id.id,
                                          .template_id = n.template_id.value,
                                          .sprite_id = n.sprite_id,
                                          .name = n.name,
                                          .x = n.pos.x,
                                          .y = n.pos.y,
                                          .direction = static_cast<uint8_t>(n.facing),
                                          .hp = n.hp,
                                          .max_hp = n.max_hp,
                                          .level = n.level,
                                          .category = std::string(npc::npc_category_to_string(n.category)),
                                          .hostility = std::string(npc::npc_hostility_for_player(
                                              n, player->faction, player->pk.is_criminal(), player->pk.is_murderer()))};
                                      conn->send(network::make_npc_spawn_message(data));
                                  });
    }
}

// ========== Combat Event Callbacks ==========

namespace
{

auto damage_type_to_string(combat::damage_type dt) -> std::string_view
{
    switch (dt)
    {
    case combat::damage_type::physical:
        return "physical";
    case combat::damage_type::magic:
        return "magic";
    case combat::damage_type::fire:
        return "fire";
    case combat::damage_type::ice:
        return "ice";
    case combat::damage_type::lightning:
        return "lightning";
    case combat::damage_type::poison:
        return "poison";
    case combat::damage_type::holy:
        return "holy";
    case combat::damage_type::dark:
        return "dark";
    case combat::damage_type::pure:
        return "pure";
    default:
        return "physical";
    }
}

auto spell_element_to_damage_type_string(magic::spell_element elem) -> std::string_view
{
    switch (elem)
    {
    case magic::spell_element::fire:
        return "fire";
    case magic::spell_element::ice:
        return "ice";
    case magic::spell_element::lightning:
        return "lightning";
    case magic::spell_element::holy:
        return "holy";
    case magic::spell_element::dark:
        return "dark";
    default:
        return "magic";
    }
}

// Use npc::npc::npc_category_to_string() from npc.h

} // namespace

void game_handlers::on_damage_dealt(const combat::damage_event& event)
{
    if (!players_ || !ws_server_)
        return;

    // Determine hit effect type for visual feedback
    auto& hr = event.result;
    std::string effect_type;
    if (hr.is_miss())
    {
        effect_type = "miss";
    }
    else if (hr.is_dodged())
    {
        effect_type = "dodge";
    }
    else if (hr.is_blocked())
    {
        effect_type = "block";
    }
    else if (hr.is_hit())
    {
        effect_type = "damage";
    }
    else
    {
        return; // No visual effect for this result
    }

    // Source entity_id is already an ecs_entity — use directly
    uint32_t source_eid = event.source.id;

    // Try player target first
    auto* target = players_->get_player_by_entity(event.target);
    if (target)
    {
        // Player target: interrupt movement
        if (target->connection.value != 0)
        {
            auto* conn = ws_server_->get_connection(target->connection);
            if (conn)
            {
                conn->clear_destination();
            }
        }

        // Skip HP update and combat_effect for killing blows — entity_death carries the damage
        if (target->is_dead())
            return;

        auto target_pid = players_->get_player_id_by_entity(event.target);
        if (target_pid)
            broadcast_hp_update(*target_pid, target->hp, target->computed.max_hp);

        network::combat_effect_data effect{.source_id = source_eid,
                                           .target_id = target->ecs_entity.id,
                                           .effect_type = std::move(effect_type),
                                           .value = hr.final_damage,
                                           .damage_type = std::string(damage_type_to_string(hr.type)),
                                           .spell_id = std::nullopt,
                                           .is_critical = hr.is_critical(),
                                           .target_x = target->pos.x,
                                           .target_y = target->pos.y};

        broadcast_combat_effect(target->current_map, target->pos, effect);
        return;
    }

    // Try NPC target — HP update is handled by on_damage_callback, but combat_effect
    // (floating damage numbers) needs to be broadcast here
    // Skip for killing blows — entity_death carries the damage info
    if (npc_)
    {
        auto* target_npc = npc_->get_npc(event.target);
        if (target_npc && !target_npc->is_dead())
        {
            network::combat_effect_data effect{.source_id = source_eid,
                                               .target_id = target_npc->entity_id.id,
                                               .effect_type = std::move(effect_type),
                                               .value = hr.final_damage,
                                               .damage_type = std::string(damage_type_to_string(hr.type)),
                                               .spell_id = std::nullopt,
                                               .is_critical = hr.is_critical(),
                                               .target_x = target_npc->pos.x,
                                               .target_y = target_npc->pos.y};

            broadcast_combat_effect(target_npc->current_map, target_npc->pos, effect);
        }
    }
}

void game_handlers::on_entity_death(const combat::death_event& event)
{
    if (!players_ || !ws_server_)
        return;

    // Debug log for every entity death
    {
        // Identify victim
        std::string victim_name = "unknown";
        world::position victim_pos{};
        auto v_pid = players_->get_player_id_by_entity(event.victim);
        if (v_pid)
        {
            if (auto* p = players_->get_player(*v_pid))
            {
                victim_name = "player:" + p->name;
                victim_pos = p->pos;
            }
        }
        else if (npc_)
        {
            if (auto* n = npc_->get_npc(event.victim))
            {
                victim_name = "npc:" + n->name;
                victim_pos = n->pos;
            }
        }

        // Identify killer
        std::string killer_name = "unknown";
        world::position killer_pos{};
        auto k_pid = players_->get_player_id_by_entity(event.killer);
        if (k_pid)
        {
            if (auto* p = players_->get_player(*k_pid))
            {
                killer_name = "player:" + p->name;
                killer_pos = p->pos;
            }
        }
        else if (npc_)
        {
            if (auto* n = npc_->get_npc(event.killer))
            {
                killer_name = "npc:" + n->name;
                killer_pos = n->pos;
            }
        }

        const char* method_str = "misc";
        switch (event.method)
        {
        case combat::kill_method::melee:
            method_str = "melee";
            break;
        case combat::kill_method::dash:
            method_str = "dash";
            break;
        case combat::kill_method::bow:
            method_str = "bow";
            break;
        case combat::kill_method::magic:
            method_str = "magic";
            break;
        case combat::kill_method::misc:
            method_str = "misc";
            break;
        }

        LOG_DEBUG(bridge,
                  "ENTITY_DEATH: victim={} at ({},{}) killed_by={} at ({},{}) damage={} method={}",
                  victim_name,
                  victim_pos.x,
                  victim_pos.y,
                  killer_name,
                  killer_pos.x,
                  killer_pos.y,
                  event.killing_damage,
                  method_str);
    }

    // Only handle player deaths beyond this point
    auto victim_pid_opt = players_->get_player_id_by_entity(event.victim);
    if (!victim_pid_opt)
        return;
    auto victim_pid = *victim_pid_opt;
    auto* victim = players_->get_player(victim_pid);
    if (!victim)
        return;

    // Move from live occupant slot to dead slot on tile
    if (world_)
    {
        auto* m = world_->get_map(victim->current_map);
        if (m)
        {
            m->clear_occupant(victim->pos);
            m->set_dead_entity(victim->pos, hb::entity_id{victim->ecs_entity.index()}, world::owner_type::player);
        }
    }

    auto killer_pid_opt = players_->get_player_id_by_entity(event.killer);
    player_id killer_pid = killer_pid_opt.value_or(player_id{0});

    // Broadcast death to nearby players
    broadcast_entity_death(victim_pid, killer_pid, event.killing_damage);

    // Handle death penalties and respawn
    handle_player_death(victim_pid, event);
}

void game_handlers::handle_player_death(player_id pid, const combat::death_event& event)
{
    auto* perf = subsystems().get<perf::perf_stats_system>();
    PERF_TIMER(perf, perf::metric_category::player_death);

    if (!players_ || !ws_server_ || !world_)
        return;

    auto* player = players_->get_player(pid);
    if (!player)
        return;

    // 1. Clear status effects and combat target
    player->status = player::player_status::none;
    player->target = {};

    int64_t xp_lost = 0;
    int32_t pk_points_change = 0;
    int32_t gold_reward = 0;
    std::string killer_name;

    player_id killer_pid{event.killer.id};
    auto* killer = players_->get_player(killer_pid);
    if (killer)
    {
        killer_name = killer->name;
    }

    // 2. PvP-specific penalties
    if (event.is_pvp && killer)
    {
        // XP penalty for victim
        int64_t penalty = calculate_death_xp_penalty(player->experience.level);
        xp_lost = player->experience.remove_experience(penalty);

        // If victim is innocent, killer gains PK points
        if (player->pk.is_innocent())
        {
            killer->pk.add_kill();
            pk_points_change = 50;
            LOG_INFO(bridge, "Player {} gained PK point for killing innocent {}", killer_pid.value, pid.value);
        }

        // If killer is innocent and victim is a PKer, award bounty
        if (killer->pk.is_innocent() && (player->pk.is_criminal() || player->pk.is_murderer()))
        {
            gold_reward = calculate_pk_bounty_reward(player->experience.level);
            // Cap at max reward gold (from game config default)
            gold_reward = std::min(gold_reward, static_cast<int32_t>(99999999));
            // TODO: Actually add gold to killer's inventory when economy wiring is complete
            LOG_INFO(bridge,
                     "Player {} earned {} gold bounty for killing PKer {}",
                     killer_pid.value,
                     gold_reward,
                     pid.value);
        }

        // Crusade PvP kill rewards (construction points + contribution)
        if (crusade_ && crusade_->is_active())
        {
            // Base exp reward approximation for contribution calculation
            int32_t pvp_exp_reward =
                static_cast<int32_t>(player->experience.level) * static_cast<int32_t>(player->experience.level) / 2;
            crusade_->on_player_kill(killer_pid, pid, static_cast<int32_t>(player->experience.level), pvp_exp_reward);
        }
    }

    // 3. Determine respawn location
    std::string spawn_map = get_respawn_map_name(player->faction);
    world::position spawn_pos = get_respawn_position(spawn_map);

    // 4. Save player state after applying penalties
    if (save_callback_)
    {
        save_callback_(pid);
    }

    // 5. Send death info to the dead player (client-initiated respawn, no auto-timer)
    uint32_t killer_eid = 0;
    if (killer)
    {
        killer_eid = killer->ecs_entity.id;
    }

    network::player_death_info_data death_info{.killer_id = killer_eid,
                                               .killer_name = killer_name,
                                               .is_pvp = event.is_pvp,
                                               .xp_lost = xp_lost,
                                               .pk_points_change = pk_points_change,
                                               .gold_reward = gold_reward,
                                               .respawn_delay_ms = 0,
                                               .respawn_map = spawn_map,
                                               .respawn_x = spawn_pos.x,
                                               .respawn_y = spawn_pos.y};

    auto* conn = ws_server_->get_connection(player->connection);
    if (conn && conn->is_open())
    {
        conn->send(network::make_player_death_info(death_info));
    }

    // Player stays dead in-place — respawn is client-initiated (respawn_request)
    // or triggered by another player's resurrection spell.

    LOG_INFO(bridge,
             "Player {} died (pvp={}, xp_lost={}, pk_change={}, bounty={}), awaiting respawn request",
             pid.value,
             event.is_pvp,
             xp_lost,
             pk_points_change,
             gold_reward);
}

void game_handlers::execute_respawn(player_id pid, const std::string& map_name, const world::position& pos)
{
    auto* perf = subsystems().get<perf::perf_stats_system>();
    PERF_TIMER(perf, perf::metric_category::player_death);

    if (!players_ || !ws_server_ || !combat_)
        return;

    auto* player = players_->get_player(pid);
    if (!player)
        return; // Player disconnected during respawn delay

    // Clear dead slot at death position (if still ours)
    if (world_)
    {
        auto* m = world_->get_map(player->current_map);
        if (m)
        {
            auto dead_eid = m->get_dead_entity(player->pos);
            if (dead_eid && dead_eid->value == pid.value)
            {
                m->clear_dead_entity(player->pos);
            }
        }
    }

    // Restore HP/MP to 50%
    player->hp = player->computed.max_hp / 2;
    player->mp = player->computed.max_mp / 2;

    // Set 3-second invulnerability
    entity::entity player_entity{pid.value};
    combat_->set_invulnerable(player_entity, 3000);

    // Execute teleport to spawn
    execute_player_teleport(pid, player->connection, 0, map_name, pos, world::direction::south);
}

auto game_handlers::calculate_death_xp_penalty(uint8_t level) -> int64_t
{
    if (level <= 1)
        return 0;

    // Legacy Helbreath formula: random(1, level/2+1) * 50
    static thread_local std::mt19937 rng{std::random_device{}()};
    int max_roll = level / 2 + 1;
    std::uniform_int_distribution<int> dist(1, max_roll);
    return static_cast<int64_t>(dist(rng)) * 50;
}

auto game_handlers::calculate_pk_bounty_reward(uint8_t level) -> int32_t
{
    return static_cast<int32_t>(level) * 3;
}

auto game_handlers::get_respawn_map_name(hb::faction f) -> std::string
{
    switch (f)
    {
    case faction::aresden:
        return "aresden";
    case faction::elvine:
        return "elvine";
    default:
        return "default";
    }
}

auto game_handlers::get_respawn_position(const std::string& map_name) -> world::position
{
    if (!world_)
        return {18, 18};

    auto* m = world_->get_map_by_name(map_name);
    if (!m)
    {
        // Map not found - try to fall back
        return {18, 18};
    }

    auto pos = m->get_random_initial_point();
    if (pos.has_value())
    {
        return *pos;
    }

    // No initial points defined - fallback
    return {18, 18};
}

// ========== Item Usage ==========

namespace
{

auto dice_roll(int count, int sides, int bonus) -> int32_t
{
    if (count <= 0 || sides <= 0)
        return bonus;
    thread_local std::mt19937 rng{std::random_device{}()};
    std::uniform_int_distribution<int> dist(1, sides);
    int32_t total = bonus;
    for (int i = 0; i < count; ++i)
    {
        total += dist(rng);
    }
    return total;
}

} // namespace

void game_handlers::handle_player_use_item(connection_id conn_id, const network::json_message& msg)
{
    auto* conn = require_in_game(conn_id, msg.seq);
    if (!conn)
        return;

    if (!players_ || !inventory_ || !ws_server_)
    {
        send_error(conn_id, msg.seq, "internal_error", "Required subsystems unavailable");
        return;
    }

    auto data_result = network::use_item_request_data::from_json(msg.data);
    if (data_result.is_err())
    {
        send_error(conn_id, msg.seq, "invalid_request", data_result.error());
        return;
    }
    const auto& data = data_result.value();

    auto pid = conn->player();
    auto* plr = players_->get_player(pid);
    if (!plr)
    {
        send_error(conn_id, msg.seq, "invalid_player", "Player not found");
        return;
    }

    if (plr->is_dead())
    {
        conn->send(network::make_use_item_response(msg.seq, false, {}, {}, 0, 0, 0, "dead"));
        return;
    }

    // Validate slot
    auto owner_id = entity_id(plr->id.value);
    auto* inv = inventory_->get_inventory(owner_id);
    if (!inv)
    {
        send_error(conn_id, msg.seq, "internal_error", "Inventory unavailable");
        return;
    }

    auto* slot = inv->get_slot(data.slot);
    if (!slot || slot->is_empty())
    {
        conn->send(network::make_use_item_response(msg.seq, false, {}, {}, 0, 0, 0, "empty_slot"));
        return;
    }

    // Look up item template
    auto* item_reg = subsystems().get<item_registry>();
    if (!item_reg)
    {
        send_error(conn_id, msg.seq, "internal_error", "Item registry unavailable");
        return;
    }

    const auto* tmpl = item_reg->get(slot->item);
    if (!tmpl)
    {
        conn->send(network::make_use_item_response(msg.seq, false, {}, {}, 0, 0, 0, "unknown_item"));
        return;
    }

    if (!tmpl->is_usable())
    {
        conn->send(network::make_use_item_response(msg.seq, false, {}, {}, 0, 0, 0, "not_consumable"));
        return;
    }

    const auto& eff = tmpl->use_effect;

    // Save pre-use state for audit
    auto use_item_id = slot->item;
    int16_t pre_use_count = slot->count;
    bool use_audited = false;
    if (auto* use_itm = item_ ? item_->get_item(use_item_id) : nullptr)
    {
        use_audited = use_itm->audited;
    }

    // Get current map for restriction checks
    auto* current_map = world_ ? world_->get_map(plr->current_map) : nullptr;

    switch (eff.type)
    {
    case consumable_effect_type::hp_restore:
    case consumable_effect_type::mp_restore:
    case consumable_effect_type::sp_restore:
    {
        // Map restriction check
        if (current_map && current_map->config().is_potions_disabled)
        {
            conn->send(network::make_use_item_response(msg.seq, false, {}, {}, 0, 0, 0, "potions_disabled"));
            return;
        }

        // Anti-cheat check
        auto now = std::chrono::steady_clock::now();
        plr->potion_tracker.record_use(now);
        bool speed_hack = plr->potion_tracker.is_speed_hack();

        // Consume the item regardless of speed hack
        if (slot->count <= 1)
        {
            slot->clear();
        }
        else
        {
            --slot->count;
        }

        if (speed_hack)
        {
            // Item consumed but no effect applied
            conn->send(network::make_use_item_response(msg.seq, true, tmpl->name, "none", 0, 0, 0));
            return;
        }

        int32_t amount = dice_roll(eff.v1, eff.v2, eff.v3);

        std::string effect_name;
        int32_t current = 0;
        int32_t max_val = 0;

        if (eff.type == consumable_effect_type::hp_restore)
        {
            plr->heal_hp(amount);
            effect_name = "hp";
            current = plr->hp;
            max_val = plr->computed.max_hp;
        }
        else if (eff.type == consumable_effect_type::mp_restore)
        {
            plr->heal_mp(amount);
            effect_name = "mp";
            current = plr->mp;
            max_val = plr->computed.max_mp;
        }
        else
        {
            plr->heal_sp(amount);
            // SP potions also cure poison (legacy behavior)
            plr->remove_status(player::player_status::poisoned);
            effect_name = "sp";
            current = plr->sp;
            max_val = plr->computed.max_sp;
        }

        conn->send(network::make_use_item_response(msg.seq, true, tmpl->name, effect_name, amount, current, max_val));
        break;
    }

    case consumable_effect_type::food:
    {
        int32_t amount = dice_roll(eff.v1, eff.v2, eff.v3);

        // Consume the item
        if (slot->count <= 1)
        {
            slot->clear();
        }
        else
        {
            --slot->count;
        }

        players_->restore_hunger(pid, static_cast<int8_t>(std::min(amount, static_cast<int32_t>(127))));

        conn->send(
            network::make_use_item_response(msg.seq, true, tmpl->name, "hunger", amount, plr->hunger.level, 100));
        break;
    }

    case consumable_effect_type::magic_scroll:
    {
        // Recall scroll (v1 == 1)
        if (eff.v1 == 1)
        {
            // Map restriction check
            if (current_map && current_map->config().is_recall_impossible)
            {
                conn->send(network::make_use_item_response(msg.seq, false, {}, {}, 0, 0, 0, "recall_impossible"));
                return;
            }

            // Consume scroll FIRST (legacy ordering)
            if (slot->count <= 1)
            {
                slot->clear();
            }
            else
            {
                --slot->count;
            }

            // Teleport to faction home
            std::string dest_map = get_respawn_map_name(plr->faction);
            world::position dest_pos = get_respawn_position(dest_map);

            execute_player_teleport(pid, conn_id, msg.seq, dest_map, dest_pos, world::direction::south);

            // execute_player_teleport sends its own response — no use_item response needed
            LOG_INFO(
                bridge, "Player {} used recall scroll -> {} ({}, {})", pid.value, dest_map, dest_pos.x, dest_pos.y);
            return;
        }

        // Other scroll subtypes not yet implemented
        conn->send(network::make_use_item_response(msg.seq, false, {}, {}, 0, 0, 0, "unsupported_item_type"));
        break;
    }

    default:
        conn->send(network::make_use_item_response(msg.seq, false, {}, {}, 0, 0, 0, "unsupported_item_type"));
        break;
    }

    // Audit item use (check if count changed = item was consumed)
    if (audit_ && use_audited)
    {
        int16_t post_count = slot->is_empty() ? 0 : slot->count;
        if (post_count < pre_use_count)
        {
            audit_->log_item(
                plr->character_id.value, tmpl->name, use_item_id.value, item_log_type::use, pre_use_count - post_count);
        }
    }
}

void game_handlers::handle_combat_mode_change(connection_id conn_id, const network::json_message& msg)
{
    auto* conn = require_in_game(conn_id, msg.seq);
    if (!conn)
        return;

    auto pid = conn->player();
    auto* plr = players_->get_player(pid);
    if (!plr)
        return;

    if (plr->is_dead())
    {
        send_error(conn_id, msg.seq, "dead", "Cannot change combat mode while dead");
        return;
    }

    // Toggle combat mode
    plr->combat_mode = !plr->combat_mode;

    // Confirm to the toggling player
    conn->send(network::make_combat_mode_change_response(msg.seq, plr->combat_mode));

    // Broadcast to nearby players
    network::combat_mode_change_broadcast_data data{.entity_id = plr->ecs_entity.id, .combat_mode = plr->combat_mode};
    auto broadcast_msg = network::make_combat_mode_change_broadcast(data);

    auto nearby = players_->get_players_who_can_see(plr->current_map, plr->pos);
    for (auto nearby_pid : nearby)
    {
        if (nearby_pid == pid)
            continue;
        auto* np = players_->get_player(nearby_pid);
        if (!np || np->connection.value == 0)
            continue;
        auto* nc = ws_server_->get_connection(np->connection);
        if (nc && nc->is_open())
        {
            nc->send(broadcast_msg);
        }
    }

    // Forward to admin spectators
    for (auto admin_conn : ws_server_->get_admin_subscribers(plr->current_map))
    {
        ws_server_->send(admin_conn, broadcast_msg);
    }
}

void game_handlers::handle_item_upgrade(connection_id conn_id, const network::json_message& msg)
{
    auto* conn = require_in_game(conn_id, msg.seq);
    if (!conn)
        return;

    if (!players_ || !inventory_ || !item_ || !ws_server_)
    {
        send_error(conn_id, msg.seq, "internal_error", "Required subsystems unavailable");
        return;
    }

    auto data_result = network::item_upgrade_request_data::from_json(msg.data);
    if (data_result.is_err())
    {
        send_error(conn_id, msg.seq, "invalid_request", data_result.error());
        return;
    }
    const auto& data = data_result.value();

    auto pid = conn->player();
    auto* plr = players_->get_player(pid);
    if (!plr)
    {
        send_error(conn_id, msg.seq, "invalid_player", "Player not found");
        return;
    }

    if (plr->is_dead())
    {
        conn->send(network::make_item_upgrade_response(msg.seq, false, data.item_slot, 0, "dead"));
        return;
    }

    // Get inventory
    auto owner_id = entity_id(plr->id.value);
    auto* inv = inventory_->get_inventory(owner_id);
    if (!inv)
    {
        send_error(conn_id, msg.seq, "internal_error", "Inventory unavailable");
        return;
    }

    // Validate target item slot
    auto* target_slot = inv->get_slot(data.item_slot);
    if (!target_slot || target_slot->is_empty())
    {
        conn->send(network::make_item_upgrade_response(msg.seq, false, data.item_slot, 0, "empty_slot"));
        return;
    }

    auto* target_item = item_->get_item(target_slot->item);
    if (!target_item)
    {
        conn->send(network::make_item_upgrade_response(msg.seq, false, data.item_slot, 0, "invalid_item"));
        return;
    }

    // Item must be equipment
    if (!target_item->is_equipment())
    {
        conn->send(network::make_item_upgrade_response(msg.seq, false, data.item_slot, 0, "not_equipment"));
        return;
    }

    // Already at max level
    if (target_item->attribute.upgrade_level >= item::max_upgrade_level)
    {
        conn->send(network::make_item_upgrade_response(
            msg.seq, false, data.item_slot, target_item->attribute.upgrade_level, "max_level"));
        return;
    }

    // Find appropriate upgrade stone in inventory
    int16_t stone_slot = -1;
    item_id stone_item_id{};
    uint32_t stone_template = 0;

    for (int16_t i = 0; i < inv->capacity(); ++i)
    {
        auto* slot = inv->get_slot(i);
        if (!slot || slot->is_empty())
            continue;

        auto* itm = item_->get_item(slot->item);
        if (!itm)
            continue;

        // Check for Xelima (weapons) or Merien (armor/accessories)
        if (itm->template_id.value == item::xelima_stone_id || itm->template_id.value == item::merien_stone_id)
        {
            if (item::is_valid_upgrade_stone(*target_item, itm->template_id.value))
            {
                stone_slot = i;
                stone_item_id = slot->item;
                stone_template = itm->template_id.value;
                break;
            }
        }
    }

    if (stone_slot < 0)
    {
        conn->send(network::make_item_upgrade_response(
            msg.seq, false, data.item_slot, target_item->attribute.upgrade_level, "no_stone"));
        return;
    }

    // Attempt the upgrade
    auto result = item::attempt_upgrade(*target_item);

    // Consume stone (always consumed)
    auto* s_slot = inv->get_slot(stone_slot);
    if (s_slot)
    {
        if (s_slot->count <= 1)
        {
            s_slot->clear();
            item_->destroy_item(stone_item_id);
        }
        else
        {
            s_slot->count -= 1;
        }
    }

    // If successful, recalculate equipment modifiers
    if (result.success)
    {
        players_->recalculate_equipment_modifiers(pid);
    }

    conn->send(network::make_item_upgrade_response(msg.seq, result.success, data.item_slot, result.new_level));
}

void game_handlers::handle_activate_ability(connection_id conn_id, const network::json_message& msg)
{
    auto* conn = require_in_game(conn_id, msg.seq);
    if (!conn)
        return;

    if (!players_ || !ws_server_)
    {
        send_error(conn_id, msg.seq, "internal_error", "Required subsystems unavailable");
        return;
    }

    auto pid = conn->player();
    auto* plr = players_->get_player(pid);
    if (!plr)
    {
        send_error(conn_id, msg.seq, "invalid_player", "Player not found");
        return;
    }

    if (plr->is_dead())
    {
        conn->send(network::make_activate_ability_response(msg.seq, false, 0, 0, "dead"));
        return;
    }

    auto& ability = plr->special_ability;

    // No ability equipped
    if (ability.type == item::special_ability_type::none)
    {
        conn->send(network::make_activate_ability_response(msg.seq, false, 0, 0, "no_ability"));
        return;
    }

    auto now = std::chrono::steady_clock::now();

    // Check if on cooldown (and possibly transition to ready)
    ability.check_cooldown(now);

    if (ability.is_on_cooldown(now))
    {
        auto remaining = ability.cooldown_remaining(now);
        conn->send(network::make_activate_ability_response(
            msg.seq, false, static_cast<uint8_t>(ability.type), remaining, "on_cooldown"));
        return;
    }

    if (ability.is_active())
    {
        conn->send(network::make_activate_ability_response(
            msg.seq, false, static_cast<uint8_t>(ability.type), 0, "already_active"));
        return;
    }

    if (!ability.is_ready())
    {
        conn->send(network::make_activate_ability_response(
            msg.seq, false, static_cast<uint8_t>(ability.type), 0, "not_ready"));
        return;
    }

    // Activate
    ability.activate(now);

    conn->send(network::make_activate_ability_response(
        msg.seq, true, static_cast<uint8_t>(ability.type), item::ability_cooldown_seconds));

    // Broadcast status update to the player
    conn->send(network::make_special_ability_status("active", static_cast<uint8_t>(ability.type), 0));
}

void game_handlers::handle_respawn_request(connection_id conn_id, const network::json_message& msg)
{
    auto* conn = require_in_game(conn_id, msg.seq);
    if (!conn)
        return;

    if (!players_ || !ws_server_ || !combat_)
    {
        send_error(conn_id, msg.seq, "internal_error", "Required subsystems unavailable");
        return;
    }

    auto pid = conn->player();
    auto* player = players_->get_player(pid);
    if (!player)
    {
        send_error(conn_id, msg.seq, "invalid_player", "Player not found");
        return;
    }

    if (!player->is_dead())
    {
        conn->send(network::make_respawn_response(msg.seq, false, "", 0, 0, "not_dead"));
        return;
    }

    std::string spawn_map = get_respawn_map_name(player->faction);
    world::position spawn_pos = get_respawn_position(spawn_map);

    execute_respawn(pid, spawn_map, spawn_pos);

    conn->send(network::make_respawn_response(msg.seq, true, spawn_map, spawn_pos.x, spawn_pos.y));

    LOG_INFO(bridge,
             "Player {} respawned at {} ({}, {}) via client request",
             pid.value,
             spawn_map,
             spawn_pos.x,
             spawn_pos.y);
}

void game_handlers::broadcast_player_action(const player::player& plr,
                                            const network::player_action_broadcast_data& data)
{
    if (!players_ || !ws_server_)
        return;

    auto msg = network::make_player_action_broadcast(data);

    auto nearby = players_->get_players_who_can_see(plr.current_map, plr.pos);
    for (auto other_id : nearby)
    {
        if (other_id == plr.id)
            continue;
        auto* other = players_->get_player(other_id);
        if (!other || other->connection.value == 0)
            continue;
        auto* conn = ws_server_->get_connection(other->connection);
        if (conn && conn->is_open())
        {
            conn->send(msg);
        }
    }

    for (auto admin_conn : ws_server_->get_admin_subscribers(plr.current_map))
    {
        ws_server_->send(admin_conn, msg);
    }
}

void game_handlers::broadcast_hp_update(player_id target, int32_t hp, int32_t hp_max)
{
    if (!players_ || !ws_server_)
        return;

    auto* player = players_->get_player(target);
    if (!player)
        return;

    auto hp_msg = network::make_entity_hp_update(player->ecs_entity.id, hp, hp_max);

    // Broadcast to players who can see this target
    auto viewers = players_->get_players_who_can_see(player->current_map, player->pos);

    for (auto other_id : viewers)
    {
        auto* other = players_->get_player(other_id);
        if (!other || other->connection.value == 0)
            continue;

        auto* other_conn = ws_server_->get_connection(other->connection);
        if (other_conn && other_conn->is_open())
        {
            other_conn->send(hp_msg);
        }
    }

    // Forward to admin spectators
    for (auto admin_conn : ws_server_->get_admin_subscribers(player->current_map))
    {
        ws_server_->send(admin_conn, hp_msg);
    }
}

void game_handlers::broadcast_entity_death(player_id victim, player_id killer, int32_t killing_damage)
{
    if (!players_ || !ws_server_)
        return;

    auto* victim_player = players_->get_player(victim);
    if (!victim_player)
        return;

    // Resolve killer's entity_id (may be offline/disconnected)
    uint32_t killer_entity_id = 0;
    auto* killer_player = players_->get_player(killer);
    if (killer_player)
    {
        killer_entity_id = killer_player->ecs_entity.id;
    }

    auto death_msg = network::make_entity_death(
        victim_player->ecs_entity.id, killer_entity_id, victim_player->pos.x, victim_player->pos.y, killing_damage);

    // Broadcast to players who can see the victim's position
    auto viewers = players_->get_players_who_can_see(victim_player->current_map, victim_player->pos);

    for (auto other_id : viewers)
    {
        auto* other = players_->get_player(other_id);
        if (!other || other->connection.value == 0)
            continue;

        auto* other_conn = ws_server_->get_connection(other->connection);
        if (other_conn && other_conn->is_open())
        {
            other_conn->send(death_msg);
        }
    }

    // Forward to admin spectators
    for (auto admin_conn : ws_server_->get_admin_subscribers(victim_player->current_map))
    {
        ws_server_->send(admin_conn, death_msg);
    }
}

// ========== Combat Effect Broadcast Methods ==========

void game_handlers::broadcast_combat_effect(map_id map,
                                            const world::position& pos,
                                            const network::combat_effect_data& data)
{
    if (!players_ || !ws_server_)
        return;

    auto msg = network::make_combat_effect(data);
    auto viewers = players_->get_players_who_can_see(map, pos);

    for (auto pid : viewers)
    {
        auto* p = players_->get_player(pid);
        if (!p || p->connection.value == 0)
            continue;

        auto* conn = ws_server_->get_connection(p->connection);
        if (conn && conn->is_open())
        {
            conn->send(msg);
        }
    }

    // Forward to admin spectators
    for (auto admin_conn : ws_server_->get_admin_subscribers(map))
    {
        ws_server_->send(admin_conn, msg);
    }
}

void game_handlers::broadcast_combat_effect_to_faction(map_id map,
                                                       const world::position& pos,
                                                       hb::faction fac,
                                                       const network::combat_effect_data& data)
{
    if (!players_ || !ws_server_)
        return;

    auto msg = network::make_combat_effect(data);
    auto viewers = players_->get_players_who_can_see(map, pos);

    for (auto pid : viewers)
    {
        auto* p = players_->get_player(pid);
        if (!p || p->connection.value == 0)
            continue;
        if (p->faction != fac)
            continue;

        auto* conn = ws_server_->get_connection(p->connection);
        if (conn && conn->is_open())
        {
            conn->send(msg);
        }
    }

    // Forward to admin spectators (admins see all factions)
    for (auto admin_conn : ws_server_->get_admin_subscribers(map))
    {
        ws_server_->send(admin_conn, msg);
    }
}

void game_handlers::on_spell_cast(entity::entity caster,
                                  const magic::spell_template& spell,
                                  const magic::spell_effect_result& result)
{
    if (!players_ || !ws_server_)
        return;
    if (!result.success)
        return; // Failed casts aren't visible

    // Find caster position - could be player or NPC
    auto* caster_player = players_->get_player(player_id{caster.id});
    if (!caster_player)
        return; // Only handle player casters for now

    auto caster_map = caster_player->current_map;
    auto caster_pos = caster_player->pos;
    auto caster_eid = caster_player->ecs_entity.id;
    auto dmg_type = std::string(spell_element_to_damage_type_string(spell.element));

    // Determine broadcast based on spell category
    switch (spell.category)
    {
    case magic::spell_category::attack:
    case magic::spell_category::debuff:
    {
        for (auto target_ent : result.affected_targets)
        {
            // Determine target position for the broadcast
            int16_t tx = caster_pos.x;
            int16_t ty = caster_pos.y;
            uint32_t target_eid = target_ent.id;

            // Try to get target position from player or NPC
            if (auto* tp = players_->get_player(player_id{target_ent.id}))
            {
                tx = tp->pos.x;
                ty = tp->pos.y;
                target_eid = tp->ecs_entity.id;
            }
            else if (npc_)
            {
                if (auto* tn = npc_->get_npc(entity::entity{target_ent.id}))
                {
                    tx = tn->pos.x;
                    ty = tn->pos.y;
                    target_eid = tn->entity_id.id;
                }
            }

            auto effect_type = spell.category == magic::spell_category::attack ? "damage" : "debuff";
            auto value = spell.category == magic::spell_category::attack ? result.damage_dealt : spell.effect_value;

            network::combat_effect_data effect{.source_id = caster_eid,
                                               .target_id = target_eid,
                                               .effect_type = effect_type,
                                               .value = value,
                                               .damage_type = dmg_type,
                                               .spell_id = spell.id.value,
                                               .is_critical = false,
                                               .target_x = tx,
                                               .target_y = ty};

            if (spell.category == magic::spell_category::debuff)
            {
                broadcast_combat_effect_to_faction(caster_map, world::position{tx, ty}, caster_player->faction, effect);
            }
            else
            {
                broadcast_combat_effect(caster_map, world::position{tx, ty}, effect);
            }
        }
        break;
    }

    case magic::spell_category::healing:
    {
        for (auto target_ent : result.affected_targets)
        {
            int16_t tx = caster_pos.x;
            int16_t ty = caster_pos.y;
            uint32_t target_eid = target_ent.id;

            if (auto* tp = players_->get_player(player_id{target_ent.id}))
            {
                tx = tp->pos.x;
                ty = tp->pos.y;
                target_eid = tp->ecs_entity.id;
            }

            network::combat_effect_data effect{.source_id = caster_eid,
                                               .target_id = target_eid,
                                               .effect_type = "heal",
                                               .value = result.heal_applied,
                                               .damage_type = dmg_type,
                                               .spell_id = spell.id.value,
                                               .is_critical = false,
                                               .target_x = tx,
                                               .target_y = ty};

            broadcast_combat_effect(caster_map, world::position{tx, ty}, effect);
        }
        break;
    }

    case magic::spell_category::buff:
    {
        for (auto target_ent : result.affected_targets)
        {
            int16_t tx = caster_pos.x;
            int16_t ty = caster_pos.y;
            uint32_t target_eid = target_ent.id;

            if (auto* tp = players_->get_player(player_id{target_ent.id}))
            {
                tx = tp->pos.x;
                ty = tp->pos.y;
                target_eid = tp->ecs_entity.id;
            }

            network::combat_effect_data effect{.source_id = caster_eid,
                                               .target_id = target_eid,
                                               .effect_type = "buff",
                                               .value = spell.effect_value,
                                               .damage_type = {},
                                               .spell_id = spell.id.value,
                                               .is_critical = false,
                                               .target_x = tx,
                                               .target_y = ty};

            broadcast_combat_effect_to_faction(caster_map, world::position{tx, ty}, caster_player->faction, effect);
        }
        break;
    }

    default:
        break;
    }
}

// ========== NPC Broadcast Methods ==========

void game_handlers::broadcast_npc_spawn(const npc::npc& n)
{
    auto* perf = subsystems().get<perf::perf_stats_system>();
    PERF_TIMER(perf, perf::metric_category::broadcast);

    if (!players_ || !ws_server_)
        return;

    auto cat_str = std::string(npc::npc_category_to_string(n.category));

    // Send per-player (hostility is viewer-relative)
    auto players = players_->get_players_who_can_see(n.current_map, n.pos);
    for (auto pid : players)
    {
        auto* p = players_->get_player(pid);
        if (!p || p->connection.value == 0)
            continue;

        auto* conn = ws_server_->get_connection(p->connection);
        if (!conn || !conn->is_open())
            continue;

        network::npc_spawn_data data{.entity_id = n.entity_id.id,
                                     .template_id = n.template_id.value,
                                     .sprite_id = n.sprite_id,
                                     .name = n.name,
                                     .x = n.pos.x,
                                     .y = n.pos.y,
                                     .direction = static_cast<uint8_t>(n.facing),
                                     .hp = n.hp,
                                     .max_hp = n.max_hp,
                                     .level = n.level,
                                     .category = cat_str,
                                     .hostility = std::string(npc::npc_hostility_for_player(
                                         n, p->faction, p->pk.is_criminal(), p->pk.is_murderer()))};

        conn->send(network::make_npc_spawn_message(data));
    }

    // Forward to admin spectators (neutral perspective)
    network::npc_spawn_data admin_data{.entity_id = n.entity_id.id,
                                       .template_id = n.template_id.value,
                                       .sprite_id = n.sprite_id,
                                       .name = n.name,
                                       .x = n.pos.x,
                                       .y = n.pos.y,
                                       .direction = static_cast<uint8_t>(n.facing),
                                       .hp = n.hp,
                                       .max_hp = n.max_hp,
                                       .level = n.level,
                                       .category = cat_str,
                                       .hostility = "neutral"};
    auto admin_msg = network::make_npc_spawn_message(admin_data);
    for (auto admin_conn : ws_server_->get_admin_subscribers(n.current_map))
    {
        ws_server_->send(admin_conn, admin_msg);
    }
}

void game_handlers::broadcast_npc_move(const npc::npc& n)
{
    auto* perf = subsystems().get<perf::perf_stats_system>();
    PERF_TIMER(perf, perf::metric_category::broadcast);

    if (!players_ || !ws_server_)
        return;

    network::npc_move_data data{
        .entity_id = n.entity_id.id, .x = n.pos.x, .y = n.pos.y, .direction = static_cast<uint8_t>(n.facing)};

    auto msg = network::make_npc_move_message(data);

    auto players = players_->get_players_who_can_see(n.current_map, n.pos);
    for (auto pid : players)
    {
        auto* p = players_->get_player(pid);
        if (!p || p->connection.value == 0)
            continue;

        auto* conn = ws_server_->get_connection(p->connection);
        if (conn && conn->is_open())
        {
            conn->send(msg);
        }
    }

    // Forward to admin spectators
    for (auto admin_conn : ws_server_->get_admin_subscribers(n.current_map))
    {
        ws_server_->send(admin_conn, msg);
    }
}

void game_handlers::broadcast_npc_attack(const npc::npc& n, entity::entity target, int32_t damage)
{
    auto* perf = subsystems().get<perf::perf_stats_system>();
    PERF_TIMER(perf, perf::metric_category::broadcast);

    if (!players_ || !ws_server_)
        return;

    // Resolve target position for projectile visuals
    int16_t tgt_x = 0, tgt_y = 0;
    if (auto target_pid = players_->get_player_id_by_entity(target))
    {
        if (auto* tgt = players_->get_player(*target_pid))
        {
            tgt_x = tgt->pos.x;
            tgt_y = tgt->pos.y;
        }
    }

    network::npc_attack_data data{.attacker_id = n.entity_id.id,
                                  .target_id = target.id,
                                  .damage = damage,
                                  .is_critical = false, // NPCs don't crit for now
                                  .is_ranged = n.ai.attack_range > 1,
                                  .attacker_x = n.pos.x,
                                  .attacker_y = n.pos.y,
                                  .target_x = tgt_x,
                                  .target_y = tgt_y};

    auto msg = network::make_npc_attack_message(data);

    auto players = players_->get_players_who_can_see(n.current_map, n.pos);
    for (auto pid : players)
    {
        auto* p = players_->get_player(pid);
        if (!p || p->connection.value == 0)
            continue;

        auto* conn = ws_server_->get_connection(p->connection);
        if (conn && conn->is_open())
        {
            conn->send(msg);
        }
    }

    // Forward to admin spectators
    for (auto admin_conn : ws_server_->get_admin_subscribers(n.current_map))
    {
        ws_server_->send(admin_conn, msg);
    }
}

void game_handlers::broadcast_npc_death(const npc::npc& n, entity::entity killer, int32_t killing_damage)
{
    auto* perf = subsystems().get<perf::perf_stats_system>();
    PERF_TIMER(perf, perf::metric_category::broadcast);

    if (!players_ || !ws_server_)
        return;

    auto msg = network::make_entity_death(n.entity_id.id, killer.id, n.pos.x, n.pos.y, killing_damage);

    auto players = players_->get_players_who_can_see(n.current_map, n.pos);
    for (auto pid : players)
    {
        auto* p = players_->get_player(pid);
        if (!p || p->connection.value == 0)
            continue;

        auto* conn = ws_server_->get_connection(p->connection);
        if (conn && conn->is_open())
        {
            conn->send(msg);
        }
    }

    // Forward to admin spectators
    for (auto admin_conn : ws_server_->get_admin_subscribers(n.current_map))
    {
        ws_server_->send(admin_conn, msg);
    }
}

void game_handlers::broadcast_npc_hp_update(const npc::npc& n)
{
    if (!players_ || !ws_server_)
        return;

    // Use entity_hp_update message for NPCs too
    auto msg = network::make_entity_hp_update(n.entity_id.id, n.hp, n.max_hp);

    auto players = players_->get_players_who_can_see(n.current_map, n.pos);
    for (auto pid : players)
    {
        auto* p = players_->get_player(pid);
        if (!p || p->connection.value == 0)
            continue;

        auto* conn = ws_server_->get_connection(p->connection);
        if (conn && conn->is_open())
        {
            conn->send(msg);
        }
    }

    // Forward to admin spectators
    for (auto admin_conn : ws_server_->get_admin_subscribers(n.current_map))
    {
        ws_server_->send(admin_conn, msg);
    }
}

// ========== Ground Item Broadcast ==========

void game_handlers::broadcast_ground_item_removed(player_id picker,
                                                  map_id map,
                                                  const world::position& pos,
                                                  item_id item)
{
    if (!players_ || !ws_server_)
        return;

    auto* picker_player = players_->get_player(picker);
    if (!picker_player)
        return;

    // Get item name for display
    std::string item_name = "Unknown";
    if (item_)
    {
        auto* itm = item_->get_item(item);
        if (itm)
        {
            item_name = itm->name;
        }
    }

    // Build the broadcast data
    network::ground_item_removed_data data{.picker_id = picker.value,
                                           .picker_name = picker_player->name,
                                           .item_id = item.value,
                                           .item_name = item_name,
                                           .x = pos.x,
                                           .y = pos.y};

    auto msg = network::make_ground_item_removed(data);

    // Broadcast to all players who can see this position (including the picker)
    auto nearby = players_->get_players_who_can_see(picker_player->current_map, pos);

    for (auto other_id : nearby)
    {
        auto* other = players_->get_player(other_id);
        if (!other || other->connection.value == 0)
            continue;

        auto* other_conn = ws_server_->get_connection(other->connection);
        if (other_conn && other_conn->is_open())
        {
            other_conn->send(msg);
        }
    }

    // Forward to admin spectators
    for (auto admin_conn : ws_server_->get_admin_subscribers(map))
    {
        ws_server_->send(admin_conn, msg);
    }

    LOG_DEBUG(
        bridge, "Broadcast ground item {} removed at ({}, {}) by player {}", item.value, pos.x, pos.y, picker.value);
}

void game_handlers::broadcast_ground_item_spawn(map_id map, const world::position& pos, item_id item)
{
    if (!players_ || !ws_server_ || !item_)
        return;

    auto* itm = item_->get_item(item);
    if (!itm)
        return;

    // Build display name with upgrade suffix
    std::string display_name = itm->name;
    if (item_registry_)
    {
        if (auto* tmpl = item_registry_->get(itm->template_id))
        {
            display_name = network::get_display_name(tmpl->name, itm->attribute);
        }
    }

    network::ground_item_spawn_data data{.item_id = item.value,
                                         .template_id = itm->template_id.value,
                                         .item_name = std::move(display_name),
                                         .count = itm->count,
                                         .x = pos.x,
                                         .y = pos.y,
                                         .attribute = itm->attribute,
                                         .reason = "drop"};

    auto msg = network::make_ground_item_spawn(data);

    auto players = players_->get_players_who_can_see(map, pos);
    for (auto pid : players)
    {
        auto* p = players_->get_player(pid);
        if (!p || p->connection.value == 0)
            continue;

        auto* conn = ws_server_->get_connection(p->connection);
        if (conn && conn->is_open())
        {
            conn->send(msg);
        }
    }

    // Forward to admin spectators
    for (auto admin_conn : ws_server_->get_admin_subscribers(map))
    {
        ws_server_->send(admin_conn, msg);
    }
}

void game_handlers::handle_npc_loot_drop(const npc::npc& n, entity::entity killer)
{
    auto* perf = subsystems().get<perf::perf_stats_system>();
    PERF_TIMER(perf, perf::metric_category::loot_generation);

    if (!item_ || !world_ || !loot_registry_)
        return;

    // Copy needed data from the npc reference (only valid during callback)
    auto npc_map = n.current_map;
    auto npc_pos = n.pos;
    auto npc_name = n.name;

    // Generate on_kill loot using config-driven system
    auto drop = npc::generate_kill_loot(*loot_registry_, n.sprite_id, n.gold_min, n.gold_max, n.has_owner());

    // Award gold directly to killer
    if (drop.gold > 0 && inventory_)
    {
        auto killer_entity = entity_id{killer.id};
        inventory_->add_gold(killer_entity, drop.gold);

        // Audit gold loot
        if (audit_)
        {
            if (auto* plr = players_ ? players_->get_player(player_id{killer.id}) : nullptr)
            {
                std::string map_str;
                if (auto* map = world_->get_map(npc_map))
                    map_str = map->name();
                audit_->log_gold(plr->character_id.value,
                                 item_log_type::gold_loot,
                                 drop.gold,
                                 0,
                                 map_str,
                                 npc_pos.x,
                                 npc_pos.y,
                                 {{"npc", npc_name}});
            }
        }

        LOG_DEBUG(bridge, "NPC '{}' dropped {} gold to killer {}", npc_name, drop.gold, killer.id);
    }

    // Place item drops on ground
    for (const auto& loot_item : drop.items)
    {
        auto create_result = item_->create_from_template(loot_item.template_id, 1);
        if (create_result.is_err())
        {
            LOG_WARN(bridge,
                     "Failed to create drop item from template {}: {}",
                     loot_item.template_id.value,
                     create_result.error());
            continue;
        }

        auto dropped_item_id = create_result.value();

        // Apply loot-generated attributes if present
        if (!loot_item.attribute.is_empty())
        {
            if (auto* itm = item_->get_item(dropped_item_id))
            {
                itm->attribute = loot_item.attribute;
            }
        }

        world_->add_ground_item(npc_map, npc_pos, dropped_item_id);
        broadcast_ground_item_spawn(npc_map, npc_pos, dropped_item_id);

        LOG_DEBUG(bridge,
                  "NPC '{}' dropped item {} (template {}) at ({}, {})",
                  npc_name,
                  dropped_item_id.value,
                  loot_item.template_id.value,
                  npc_pos.x,
                  npc_pos.y);
    }
}

void game_handlers::handle_npc_despawn_drop(const npc::npc& n)
{
    auto* perf = subsystems().get<perf::perf_stats_system>();
    PERF_TIMER(perf, perf::metric_category::loot_generation);

    if (!item_ || !world_ || !loot_registry_)
        return;

    // Copy needed data from the npc reference (only valid during callback)
    auto npc_map = n.current_map;
    auto npc_pos = n.pos;
    auto npc_name = n.name;

    // Generate on_despawn loot (body parts, rares, boss multi-drops)
    auto drop = npc::generate_despawn_loot(*loot_registry_, n.sprite_id);

    // Place item drops on ground
    for (const auto& loot_item : drop.items)
    {
        auto create_result = item_->create_from_template(loot_item.template_id, 1);
        if (create_result.is_err())
        {
            LOG_WARN(bridge,
                     "Failed to create despawn drop item from template {}: {}",
                     loot_item.template_id.value,
                     create_result.error());
            continue;
        }

        auto dropped_item_id = create_result.value();

        // Apply loot-generated attributes if present
        if (!loot_item.attribute.is_empty())
        {
            if (auto* itm = item_->get_item(dropped_item_id))
            {
                itm->attribute = loot_item.attribute;
            }
        }

        world_->add_ground_item(npc_map, npc_pos, dropped_item_id);
        broadcast_ground_item_spawn(npc_map, npc_pos, dropped_item_id);

        LOG_DEBUG(bridge,
                  "NPC '{}' despawn dropped item {} (template {}) at ({}, {})",
                  npc_name,
                  dropped_item_id.value,
                  loot_item.template_id.value,
                  npc_pos.x,
                  npc_pos.y);
    }

    if (!drop.items.empty())
    {
        LOG_DEBUG(bridge, "NPC '{}' corpse despawned with {} item drops", npc_name, drop.items.size());
    }
}

void game_handlers::send_visible_ground_items(
    connection_id conn_id, map_id map, const world::position& pos, int radius_x, int radius_y)
{
    auto* perf = subsystems().get<perf::perf_stats_system>();
    PERF_TIMER(perf, perf::metric_category::visibility_update);

    if (!world_ || !ws_server_ || !item_)
        return;

    auto* conn = ws_server_->get_connection(conn_id);
    if (!conn || !conn->is_open())
        return;

    // Scan tiles in rectangular visibility for ground items
    for (int16_t dx = static_cast<int16_t>(-radius_x); dx <= radius_x; ++dx)
    {
        for (int16_t dy = static_cast<int16_t>(-radius_y); dy <= radius_y; ++dy)
        {
            world::position tile_pos{static_cast<int16_t>(pos.x + dx), static_cast<int16_t>(pos.y + dy)};

            auto items = world_->get_ground_items(map, tile_pos);
            if (items.empty())
                continue;

            // Only send the top-most item (last in FILO stack)
            auto top_item = items.back();
            auto* itm = item_->get_item(top_item);
            if (!itm)
                continue;

            std::string display_name = itm->name;
            if (item_registry_)
            {
                if (auto* tmpl = item_registry_->get(itm->template_id))
                {
                    display_name = network::get_display_name(tmpl->name, itm->attribute);
                }
            }

            network::ground_item_spawn_data data{.item_id = top_item.value,
                                                 .template_id = itm->template_id.value,
                                                 .item_name = std::move(display_name),
                                                 .count = itm->count,
                                                 .x = tile_pos.x,
                                                 .y = tile_pos.y,
                                                 .attribute = itm->attribute};

            conn->send(network::make_ground_item_spawn(data));
        }
    }
}

// ========== Entity Info ==========

void game_handlers::handle_entity_info_request(connection_id conn_id, const network::json_message& msg)
{
    auto* conn = require_in_game(conn_id, msg.seq);
    if (!conn)
        return;

    if (!players_)
    {
        send_error(conn_id, msg.seq, "internal_error", "Player system unavailable");
        return;
    }

    auto data_result = network::entity_info_request_data::from_json(msg.data);
    if (data_result.is_err())
    {
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

    if (target_player)
    {
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
        switch (target_player->faction)
        {
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

        // Hostility relative to the requester
        if (auto* requester = players_->get_player(requester_pid))
        {
            response.hostility = std::string(player_hostility(requester->faction, target_player->faction));
        }

        // Guild name if player has one
        if (social_)
        {
            auto guild_id = social_->get_player_guild(*target_pid_opt);
            if (guild_id.is_valid())
            {
                auto* guild = social_->get_guild(guild_id);
                if (guild)
                {
                    response.guild_name = guild->name;
                }
            }
        }

        conn->send(network::make_entity_info_response(msg.seq, true, &response));
        LOG_DEBUG(bridge,
                  "Player {} requested info about player {} ({})",
                  requester_pid.value,
                  data.entity_id,
                  target_player->name);
        return;
    }

    // Not a player - check if it's an NPC
    if (npc_)
    {
        entity::entity entity_id{data.entity_id};
        auto* target_npc = npc_->get_npc(entity_id);

        if (target_npc)
        {
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
            response.sprite_id = target_npc->sprite_id;
            response.npc_type = std::string(npc::npc_category_to_string(target_npc->category));

            // Hostility relative to the requester
            if (auto* requester = players_->get_player(requester_pid))
            {
                response.hostility = std::string(npc::npc_hostility_for_player(
                    *target_npc, requester->faction, requester->pk.is_criminal(), requester->pk.is_murderer()));
            }

            conn->send(network::make_entity_info_response(msg.seq, true, &response));
            LOG_DEBUG(bridge,
                      "Player {} requested info about NPC {} ({})",
                      requester_pid.value,
                      data.entity_id,
                      target_npc->name);
            return;
        }
    }

    // Entity not found
    conn->send(network::make_entity_info_response(msg.seq, false, nullptr, "entity_not_found"));
    LOG_DEBUG(bridge, "Player {} requested info about unknown entity {}", requester_pid.value, data.entity_id);
}

// ========== Hunger Update ==========

void game_handlers::send_hunger_update(player_id pid, int8_t level)
{
    if (!players_ || !ws_server_)
        return;

    auto* player = players_->get_player(pid);
    if (!player || player->connection.value == 0)
        return;

    auto* conn = ws_server_->get_connection(player->connection);
    if (!conn || !conn->is_open())
        return;

    conn->send(network::make_hunger_update(level));

    LOG_DEBUG(bridge, "Sent hunger update to player {}: level={}, starving={}", pid.value, level, level <= 0);
}

// ========== Skill Data Sync ==========

void game_handlers::send_skills_data(connection_id conn_id, player_id pid)
{
    if (!skills_ || !ws_server_)
        return;

    auto* ps = skills_->get_player_skills(pid);
    if (!ps)
        return;

    auto* conn = ws_server_->get_connection(conn_id);
    if (!conn || !conn->is_open())
        return;

    std::vector<network::skill_entry_msg> skill_list;
    for (uint8_t i = 0; i < static_cast<uint8_t>(skill::skill_type::skill_count); ++i)
    {
        auto type = static_cast<skill::skill_type>(i);
        const auto& ss = ps->get(type);
        skill_list.push_back({.skill_id = i,
                              .level = ss.level,
                              .total_uses = ss.total_uses,
                              .uses_this_level = ss.uses_this_level,
                              .uses_to_next_level = skills_->uses_to_next_level(type, ss.level)});
    }
    conn->send(network::make_skills_data(0, skill_list));
}

// ========== Environment (Day/Night + Weather) ==========

void game_handlers::tick_weather()
{
    if (!world_)
        return;

    thread_local std::mt19937 rng{std::random_device{}()};
    auto now = std::chrono::steady_clock::now();

    world_->for_each_map(
        [&](map_id, world::map& m)
        {
            if (m.config().is_fixed_day_mode)
                return;

            // Expire active weather
            if (m.weather_active() && now >= m.weather_end_time())
            {
                m.clear_weather();
            }

            // Chance to start new weather (1-in-30 per 10s tick ≈ legacy 1-in-300 per 1s tick)
            if (!m.weather_active())
            {
                std::uniform_int_distribution<int> chance_dist(1, 30);
                if (chance_dist(rng) != 1)
                    return;

                // Pick weather type based on map type
                world::weather_type weather;
                if (m.config().is_snow_enabled)
                {
                    std::uniform_int_distribution<int> type_dist(4, 6);
                    weather = static_cast<world::weather_type>(type_dist(rng));
                }
                else
                {
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

void game_handlers::broadcast_environment_update()
{
    if (!players_ || !ws_server_ || !world_ || !scheduler_)
        return;

    auto& clock = scheduler_->game_time();
    auto hour = static_cast<uint8_t>(clock.hour());
    auto minute = static_cast<uint8_t>(clock.minute());
    bool is_day = clock.is_day();

    players_->for_each_player(
        [&](player_id, player::player& plr)
        {
            if (plr.connection.value == 0)
                return;

            auto* map = world_->get_map(plr.current_map);
            if (!map)
                return;

            network::environment_update_data env{
                .hour = hour, .minute = minute, .is_day = is_day, .weather = static_cast<uint8_t>(map->weather())};

            // Fixed-day maps always show daytime and clear weather
            if (map->config().is_fixed_day_mode)
            {
                env.is_day = true;
                env.weather = 0;
            }

            auto* conn = ws_server_->get_connection(plr.connection);
            if (conn && conn->is_open())
            {
                conn->send(network::make_environment_update(env));
            }
        });

    // Send environment to admin spectators for each map they're subscribed to
    for (auto admin_conn_id : ws_server_->get_all_admin_connections())
    {
        auto* admin_conn = ws_server_->get_connection(admin_conn_id);
        if (!admin_conn || !admin_conn->is_open())
            continue;
        auto& sub = admin_conn->subscription();
        if (sub.sub_mode == network::admin_subscription::mode::none)
            continue;
        auto* map = world_->get_map(sub.target_map);
        if (!map)
            continue;

        network::environment_update_data env{.hour = static_cast<uint8_t>(scheduler_->game_time().hour()),
                                             .minute = static_cast<uint8_t>(scheduler_->game_time().minute()),
                                             .is_day = scheduler_->game_time().is_day(),
                                             .weather = static_cast<uint8_t>(map->weather())};
        if (map->config().is_fixed_day_mode)
        {
            env.is_day = true;
            env.weather = 0;
        }
        admin_conn->send(network::make_environment_update(env));
    }
}

// ========== Equipment Handling ==========

void game_handlers::handle_player_equip(connection_id conn_id, const network::json_message& msg)
{
    auto* conn = require_in_game(conn_id, msg.seq);
    if (!conn)
        return;

    if (!players_ || !inventory_ || !item_)
    {
        send_error(conn_id, msg.seq, "internal_error", "Required systems unavailable");
        return;
    }

    auto data_result = network::player_equip_request_data::from_json(msg.data);
    if (data_result.is_err())
    {
        send_error(conn_id, msg.seq, "invalid_request", data_result.error());
        return;
    }

    auto& data = data_result.value();
    auto pid = conn->player();
    auto* plr = players_->get_player(pid);
    if (!plr)
    {
        send_error(conn_id, msg.seq, "invalid_player", "Player not found");
        return;
    }

    // Check alive
    if (plr->is_dead())
    {
        send_error(conn_id, msg.seq, "player_dead", "Cannot equip while dead");
        return;
    }

    // Check not trading
    auto trade_partner = inventory_->get_trade_partner(plr->ecs_entity.id);
    if (trade_partner.is_valid())
    {
        send_error(conn_id, msg.seq, "player_busy", "Cannot equip while trading");
        return;
    }

    // Get inventory and validate slot
    auto* inv = inventory_->get_inventory(plr->ecs_entity.id);
    if (!inv)
    {
        send_error(conn_id, msg.seq, "internal_error", "Inventory not found");
        return;
    }

    auto* inv_slot = inv->get_slot(data.inventory_slot);
    if (!inv_slot || inv_slot->is_empty())
    {
        send_error(conn_id, msg.seq, "invalid_slot", "Inventory slot is empty");
        return;
    }

    // Get the item
    auto* itm = item_->get_item(inv_slot->item);
    if (!itm)
    {
        send_error(conn_id, msg.seq, "item_not_found", "Item not found");
        return;
    }

    // Validate item is equipment
    if (!itm->is_equipment() || itm->equip_position == item::equip_pos::none)
    {
        send_error(conn_id, msg.seq, "not_equippable", "Item cannot be equipped");
        return;
    }

    // Validate durability
    if (itm->is_broken())
    {
        send_error(conn_id, msg.seq, "item_broken", "Item is broken and cannot be equipped");
        return;
    }

    // Validate target slot
    auto target_slot = static_cast<player::equip_slot>(data.target_slot);
    if (!player::is_valid_slot_for_item(itm->equip_position, target_slot))
    {
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
    if (!req.can_use())
    {
        send_error(conn_id, msg.seq, "requirements_not_met", "You do not meet the requirements");
        return;
    }

    network::equip_result_msg result;
    result.slot = data.target_slot;

    // Two-handed weapon logic: if equipping a 2H weapon and shield is occupied
    if (itm->two_handed && plr->equipment.has_equipped(player::equip_slot::shield))
    {
        // Need an extra free inventory slot for the shield (beyond the one being freed)
        if (inv->free_slots() < 1)
        {
            send_error(conn_id, msg.seq, "inventory_full", "Need a free slot to unequip shield for two-handed weapon");
            return;
        }
        // Unequip shield to inventory
        auto shield_equipped = players_->unequip_item(pid, player::equip_slot::shield);
        auto shield_inv_slot = inv->find_empty_slot();
        if (shield_inv_slot.has_value())
        {
            inv->get_slot(*shield_inv_slot)->set(shield_equipped.id, 1);
            result.unequipped_shield_id = shield_equipped.id.value;
            result.shield_to_inv_slot = static_cast<uint8_t>(*shield_inv_slot);
            broadcast_equipment_change(pid, player::equip_slot::shield, item_id{});
        }
    }

    // Shield equip + 2H weapon currently equipped: check the weapon in weapon slot
    if (target_slot == player::equip_slot::shield && plr->equipment.has_equipped(player::equip_slot::weapon))
    {
        auto* weapon_itm = item_->get_item(plr->equipment.weapon().id);
        if (weapon_itm && weapon_itm->two_handed)
        {
            send_error(
                conn_id, msg.seq, "two_handed_weapon_equipped", "Cannot equip shield while using a two-handed weapon");
            return;
        }
    }

    // Swap logic: if target equipment slot is occupied
    if (plr->equipment.has_equipped(target_slot))
    {
        auto old_equipped = players_->unequip_item(pid, target_slot);
        // Place old item in the inventory slot being freed by the new equip
        inv_slot->set(old_equipped.id, 1);
        result.swapped_item_id = old_equipped.id.value;
        result.swapped_to_inv_slot = static_cast<uint8_t>(data.inventory_slot);
    }
    else
    {
        // Just clear the inventory slot
        inv_slot->clear();
    }

    // Equip new item
    players_->equip_item(
        pid, target_slot, itm->id, static_cast<uint16_t>(itm->durability), static_cast<uint16_t>(itm->max_durability));

    // Recalculate stats
    players_->recalculate_equipment_modifiers(pid);

    // Build success response
    result.success = true;
    result.item_id = itm->id.value;
    result.attribute = itm->attribute;
    if (item_registry_)
    {
        if (auto* tmpl = item_registry_->get(itm->template_id))
        {
            result.item_name = network::get_display_name(tmpl->name, itm->attribute);
        }
        else
        {
            result.item_name = itm->name;
        }
    }
    else
    {
        result.item_name = itm->name;
    }
    result.durability = itm->durability;
    result.max_durability = itm->max_durability;

    conn->send(network::make_player_equip_response(msg.seq, result));

    // Send stat update
    // Re-fetch player since recalculate may have changed computed stats
    plr = players_->get_player(pid);
    if (plr)
    {
        send_stat_update(conn_id, *plr);
    }

    // Broadcast to nearby players
    broadcast_equipment_change(pid, target_slot, itm->id);

    LOG_DEBUG(
        bridge, "Player {} equipped item {} ('{}') to slot {}", pid.value, itm->id.value, itm->name, data.target_slot);
}

void game_handlers::handle_player_unequip(connection_id conn_id, const network::json_message& msg)
{
    auto* conn = require_in_game(conn_id, msg.seq);
    if (!conn)
        return;

    if (!players_ || !inventory_ || !item_)
    {
        send_error(conn_id, msg.seq, "internal_error", "Required systems unavailable");
        return;
    }

    auto data_result = network::player_unequip_request_data::from_json(msg.data);
    if (data_result.is_err())
    {
        send_error(conn_id, msg.seq, "invalid_request", data_result.error());
        return;
    }

    auto& data = data_result.value();
    auto pid = conn->player();
    auto* plr = players_->get_player(pid);
    if (!plr)
    {
        send_error(conn_id, msg.seq, "invalid_player", "Player not found");
        return;
    }

    // Check alive
    if (plr->is_dead())
    {
        send_error(conn_id, msg.seq, "player_dead", "Cannot unequip while dead");
        return;
    }

    // Check not trading
    auto trade_partner = inventory_->get_trade_partner(plr->ecs_entity.id);
    if (trade_partner.is_valid())
    {
        send_error(conn_id, msg.seq, "player_busy", "Cannot unequip while trading");
        return;
    }

    // Validate slot
    if (data.equip_slot >= static_cast<uint8_t>(player::equip_slot::count))
    {
        send_error(conn_id, msg.seq, "invalid_slot", "Invalid equipment slot");
        return;
    }

    auto slot = static_cast<player::equip_slot>(data.equip_slot);
    if (!plr->equipment.has_equipped(slot))
    {
        send_error(conn_id, msg.seq, "slot_empty", "Nothing equipped in that slot");
        return;
    }

    // Check inventory space
    auto* inv = inventory_->get_inventory(plr->ecs_entity.id);
    if (!inv)
    {
        send_error(conn_id, msg.seq, "internal_error", "Inventory not found");
        return;
    }

    if (inv->is_full())
    {
        send_error(conn_id, msg.seq, "inventory_full", "Inventory is full");
        return;
    }

    // Unequip
    auto equipped = players_->unequip_item(pid, slot);

    // Add to inventory
    auto inv_slot = inv->find_empty_slot();
    if (!inv_slot.has_value())
    {
        // Rollback - re-equip
        players_->equip_item(pid, slot, equipped.id, equipped.durability, equipped.max_durability);
        send_error(conn_id, msg.seq, "inventory_full", "Failed to find inventory slot");
        return;
    }

    inv->get_slot(*inv_slot)->set(equipped.id, 1);

    // Recalculate stats
    players_->recalculate_equipment_modifiers(pid);

    // Get item details for response
    auto* itm = item_->get_item(equipped.id);
    std::string item_name = itm ? itm->name : "Unknown";
    item::item_attribute attr{};
    if (itm)
    {
        attr = itm->attribute;
        if (item_registry_)
        {
            if (auto* tmpl = item_registry_->get(itm->template_id))
            {
                item_name = network::get_display_name(tmpl->name, attr);
            }
        }
    }

    // Build response
    network::unequip_result_msg result;
    result.success = true;
    result.slot = data.equip_slot;
    result.item_id = equipped.id.value;
    result.item_name = item_name;
    result.inventory_slot = static_cast<uint8_t>(*inv_slot);
    result.attribute = attr;

    conn->send(network::make_player_unequip_response(msg.seq, result));

    // Send stat update
    plr = players_->get_player(pid);
    if (plr)
    {
        send_stat_update(conn_id, *plr);
    }

    // Broadcast to nearby (slot now empty)
    broadcast_equipment_change(pid, slot, item_id{});

    LOG_DEBUG(bridge,
              "Player {} unequipped item {} ('{}') from slot {}",
              pid.value,
              equipped.id.value,
              item_name,
              data.equip_slot);
}

void game_handlers::broadcast_equipment_change(player_id pid, player::equip_slot slot, item_id itm)
{
    if (!players_ || !ws_server_)
        return;

    auto* plr = players_->get_player(pid);
    if (!plr)
        return;

    uint32_t template_id = 0;
    if (itm.is_valid() && item_)
    {
        auto* item_inst = item_->get_item(itm);
        if (item_inst)
        {
            template_id = item_inst->template_id.value;
        }
    }

    network::equipment_change_broadcast_data data{.entity_id = plr->ecs_entity.id,
                                                  .slot = static_cast<uint8_t>(slot),
                                                  .item_id = itm.value,
                                                  .template_id = template_id};
    auto msg = network::make_equipment_change_broadcast(data);

    auto nearby = players_->get_players_who_can_see(plr->current_map, plr->pos);
    for (auto nearby_pid : nearby)
    {
        if (nearby_pid == pid)
            continue; // Don't send to self
        auto* np = players_->get_player(nearby_pid);
        if (!np || np->connection.value == 0)
            continue;
        auto* conn = ws_server_->get_connection(np->connection);
        if (conn && conn->is_open())
        {
            conn->send(msg);
        }
    }

    // Forward to admin spectators
    for (auto admin_conn : ws_server_->get_admin_subscribers(plr->current_map))
    {
        ws_server_->send(admin_conn, msg);
    }
}

void game_handlers::send_stat_update(connection_id conn_id, const player::player& plr)
{
    if (!ws_server_)
        return;

    auto* conn = ws_server_->get_connection(conn_id);
    if (!conn || !conn->is_open())
        return;

    network::stat_update_data data{.max_hp = plr.computed.max_hp,
                                   .max_mp = plr.computed.max_mp,
                                   .max_sp = plr.computed.max_sp,
                                   .attack_power = plr.computed.attack_power,
                                   .magic_power = plr.computed.magic_power,
                                   .defense = plr.computed.defense,
                                   .magic_defense = plr.computed.magic_defense,
                                   .hit_rate = plr.computed.hit_rate,
                                   .dodge_rate = plr.computed.dodge_rate,
                                   .critical_rate = plr.computed.critical_rate};
    conn->send(network::make_stat_update(data));
}

void game_handlers::send_full_stat_update(connection_id conn_id, const player::player& plr)
{
    if (!ws_server_)
        return;

    auto* conn = ws_server_->get_connection(conn_id);
    if (!conn || !conn->is_open())
        return;

    int32_t gold = 0;
    if (inventory_)
    {
        gold = static_cast<int32_t>(inventory_->get_gold(entity_id(plr.ecs_entity.id)));
    }

    network::stat_update_data data{.max_hp = plr.computed.max_hp,
                                   .max_mp = plr.computed.max_mp,
                                   .max_sp = plr.computed.max_sp,
                                   .attack_power = plr.computed.attack_power,
                                   .magic_power = plr.computed.magic_power,
                                   .defense = plr.computed.defense,
                                   .magic_defense = plr.computed.magic_defense,
                                   .hit_rate = plr.computed.hit_rate,
                                   .dodge_rate = plr.computed.dodge_rate,
                                   .critical_rate = plr.computed.critical_rate,
                                   .hp = plr.hp,
                                   .mp = plr.mp,
                                   .sp = plr.sp,
                                   .experience = plr.experience.experience,
                                   .gold = gold,
                                   .level = plr.experience.level,
                                   .pk_count = plr.pk.count,
                                   .hunger_level = static_cast<uint8_t>(plr.hunger.level),
                                   .contribution = plr.experience.contribution,
                                   .enemy_kill_count = plr.experience.enemy_kill_count};
    conn->send(network::make_stat_update(data));
}

void game_handlers::distribute_npc_kill_exp(entity::entity killer, int32_t base_exp)
{
    if (!players_ || base_exp <= 0)
        return;

    // Resolve killer to player_id
    auto killer_pid_opt = players_->get_player_id_by_entity(killer);
    if (!killer_pid_opt)
        return;
    auto killer_pid = *killer_pid_opt;

    auto* killer_player = players_->get_player(killer_pid);
    if (!killer_player)
        return;

    // Check party membership
    social::party* pt = nullptr;
    if (social_)
    {
        auto party_id = social_->get_player_party(killer_pid);
        if (party_id.is_valid())
        {
            pt = social_->get_party(party_id);
        }
    }

    // No party, or individual mode, or low XP — award all to killer
    if (!pt || pt->experience == social::exp_mode::individual || base_exp < 10)
    {
        players_->add_experience(killer_pid, base_exp);
        LOG_DEBUG(bridge, "Awarded {} XP to player '{}' (solo kill)", base_exp, killer_player->name);
        return;
    }

    // Get eligible party members: same map, alive (hp > 0)
    auto same_map_ids = pt->members_in_map(killer_player->current_map);
    std::vector<std::pair<player_id, int16_t>> eligible; // pid, level
    int32_t total_levels = 0;
    for (auto pid : same_map_ids)
    {
        auto* p = players_->get_player(pid);
        if (p && !p->is_dead())
        {
            eligible.emplace_back(pid, p->experience.level);
            total_levels += p->experience.level;
        }
    }

    if (eligible.empty())
        return;

    // Single eligible member gets full XP (no bonus)
    if (eligible.size() == 1)
    {
        players_->add_experience(eligible[0].first, base_exp);
        LOG_DEBUG(bridge, "Awarded {} XP to player (party of 1 eligible)", base_exp);
        return;
    }

    auto eligible_count = static_cast<int>(eligible.size());

    if (pt->experience == social::exp_mode::equal_split)
    {
        auto per_member = social::calculate_party_exp_share(base_exp, eligible_count);
        for (auto& [pid, level] : eligible)
        {
            players_->add_experience(pid, per_member);
        }
        LOG_DEBUG(
            bridge, "Party equal split: {} base XP -> {} each for {} members", base_exp, per_member, eligible_count);
    }
    else if (pt->experience == social::exp_mode::level_weighted)
    {
        for (auto& [pid, level] : eligible)
        {
            auto share = social::calculate_level_weighted_exp(base_exp, eligible_count, level, total_levels);
            players_->add_experience(pid, share);
        }
        LOG_DEBUG(bridge,
                  "Party level-weighted: {} base XP for {} members (total levels {})",
                  base_exp,
                  eligible_count,
                  total_levels);
    }
}

// === Crafting: Manufacturing handlers ===

void game_handlers::handle_manufacture_list_request(connection_id conn_id, const network::json_message& msg)
{
    auto* conn = require_in_game(conn_id, msg.seq);
    if (!conn)
        return;

    if (!manufacturing_)
    {
        send_error(conn_id, msg.seq, "not_available", "Manufacturing is not available");
        return;
    }

    auto pid = conn->player();
    auto recipes = manufacturing_->get_available_recipes(entity_id{pid.value});

    nlohmann::json recipe_list = nlohmann::json::array();
    for (const auto* recipe : recipes)
    {
        nlohmann::json r;
        r["id"] = recipe->id;
        r["name"] = recipe->result;
        r["skill_req"] = recipe->skill_req;
        r["success_rate"] = recipe->success_rate;

        nlohmann::json ings = nlohmann::json::array();
        for (const auto& ing : recipe->ingredients)
        {
            ings.push_back({{"item_id", ing.item_id}, {"count", ing.count}});
        }
        r["ingredients"] = ings;

        recipe_list.push_back(r);
    }

    conn->send(network::make_manufacture_list_response(msg.seq, recipe_list));
}

void game_handlers::handle_manufacture_request(connection_id conn_id, const network::json_message& msg)
{
    auto* conn = require_in_game(conn_id, msg.seq);
    if (!conn)
        return;

    if (!manufacturing_)
    {
        send_error(conn_id, msg.seq, "not_available", "Manufacturing is not available");
        return;
    }

    auto parse = network::manufacture_request_data::from_json(msg.data);
    if (parse.is_err())
    {
        send_error(conn_id, msg.seq, "invalid_data", parse.error());
        return;
    }

    auto& data = parse.value();
    auto pid = conn->player();
    auto result = manufacturing_->attempt_craft(entity_id{pid.value}, data.recipe_index);

    if (result.reason == skill::skill_use_result::insufficient_skill)
    {
        conn->send(network::make_manufacture_response(msg.seq, false, "", "insufficient_skill"));
        return;
    }
    if (result.reason == skill::skill_use_result::insufficient_materials)
    {
        conn->send(network::make_manufacture_response(msg.seq, false, "", "insufficient_materials"));
        return;
    }
    if (!result.success && result.reason == skill::skill_use_result::failure)
    {
        conn->send(network::make_manufacture_response(msg.seq, false, "", "inventory_full"));
        return;
    }

    // Look up recipe name for response
    std::string item_name;
    auto* registry = subsystems().get<build_recipe_registry>();
    if (registry)
    {
        auto* recipe = registry->get(data.recipe_index);
        if (recipe)
            item_name = recipe->result;
    }

    conn->send(network::make_manufacture_response(msg.seq, result.success, item_name));

    // Audit crafted item
    if (result.success && audit_ && result.created_item.is_valid())
    {
        if (auto* crafted = item_ ? item_->get_item(result.created_item) : nullptr)
        {
            if (crafted->audited)
            {
                audit_->log_item(players_->get_player(pid)->character_id.value,
                                 item_name.empty() ? crafted->name : item_name,
                                 result.created_item.value,
                                 item_log_type::make,
                                 1);
            }
        }
    }

    // Fire quest event on success
    if (result.success && quests_ && result.created_item.is_valid())
    {
        quests_->on_item_crafted({.player = pid, .item = result.created_item, .count = 1});
    }
}

// === Crafting: Alchemy handlers ===

void game_handlers::handle_alchemy_list_request(connection_id conn_id, const network::json_message& msg)
{
    auto* conn = require_in_game(conn_id, msg.seq);
    if (!conn)
        return;

    if (!alchemy_)
    {
        send_error(conn_id, msg.seq, "not_available", "Alchemy is not available");
        return;
    }

    auto pid = conn->player();
    auto recipes = alchemy_->get_available_recipes(entity_id{pid.value});

    nlohmann::json recipe_list = nlohmann::json::array();
    for (const auto* recipe : recipes)
    {
        nlohmann::json r;
        r["id"] = recipe->id;
        r["name"] = recipe->result;
        r["skill_limit"] = recipe->skill_limit;
        r["difficulty"] = recipe->difficulty;

        nlohmann::json ings = nlohmann::json::array();
        for (const auto& ing : recipe->ingredients)
        {
            ings.push_back({{"item_id", ing.item_id}, {"count", ing.count}});
        }
        r["ingredients"] = ings;

        recipe_list.push_back(r);
    }

    conn->send(network::make_alchemy_list_response(msg.seq, recipe_list));
}

void game_handlers::handle_alchemy_request(connection_id conn_id, const network::json_message& msg)
{
    auto* conn = require_in_game(conn_id, msg.seq);
    if (!conn)
        return;

    if (!alchemy_)
    {
        send_error(conn_id, msg.seq, "not_available", "Alchemy is not available");
        return;
    }

    auto parse = network::alchemy_request_data::from_json(msg.data);
    if (parse.is_err())
    {
        send_error(conn_id, msg.seq, "invalid_data", parse.error());
        return;
    }

    auto& data = parse.value();
    auto pid = conn->player();
    auto result = alchemy_->attempt_craft(entity_id{pid.value}, data.recipe_id);

    if (result.reason == skill::skill_use_result::insufficient_skill)
    {
        conn->send(network::make_alchemy_response(msg.seq, false, "", "insufficient_skill"));
        return;
    }
    if (result.reason == skill::skill_use_result::insufficient_materials)
    {
        conn->send(network::make_alchemy_response(msg.seq, false, "", "insufficient_materials"));
        return;
    }
    if (!result.success && result.reason == skill::skill_use_result::failure)
    {
        conn->send(network::make_alchemy_response(msg.seq, false, "", "inventory_full"));
        return;
    }

    // Look up recipe name for response
    std::string item_name;
    auto* registry = subsystems().get<craft_recipe_registry>();
    if (registry)
    {
        auto* recipe = registry->get(data.recipe_id);
        if (recipe)
            item_name = recipe->result;
    }

    conn->send(network::make_alchemy_response(msg.seq, result.success, item_name));

    // Audit crafted item
    if (result.success && audit_ && result.created_item.is_valid())
    {
        if (auto* crafted = item_ ? item_->get_item(result.created_item) : nullptr)
        {
            if (crafted->audited)
            {
                audit_->log_item(players_->get_player(pid)->character_id.value,
                                 item_name.empty() ? crafted->name : item_name,
                                 result.created_item.value,
                                 item_log_type::make,
                                 1);
            }
        }
    }

    // Fire quest event on success
    if (result.success && quests_ && result.created_item.is_valid())
    {
        quests_->on_item_crafted({.player = pid, .item = result.created_item, .count = 1});
    }
}

// === Mining ===

void game_handlers::handle_mine_request(connection_id conn_id, const network::json_message& msg)
{
    auto* conn = require_in_game(conn_id, msg.seq);
    if (!conn)
        return;

    if (!mining_)
    {
        send_error(conn_id, msg.seq, "not_available", "Mining is not available");
        return;
    }

    auto parse = network::mine_request_data::from_json(msg.data);
    if (parse.is_err())
    {
        send_error(conn_id, msg.seq, "invalid_data", parse.error());
        return;
    }

    auto& data = parse.value();
    auto pid = conn->player();
    auto* plr = players_->get_player(pid);
    if (!plr)
    {
        send_error(conn_id, msg.seq, "internal_error", "Player not found");
        return;
    }

    auto* map = world_->get_map(plr->current_map);
    if (!map)
    {
        send_error(conn_id, msg.seq, "internal_error", "Map not found");
        return;
    }
    auto map_name = std::string(map->name());
    auto result = mining_->attempt_mine(entity_id(pid.value), data.target_x, data.target_y, map_name);

    if (result.reason == skill::skill_use_result::invalid_target)
    {
        conn->send(network::make_mine_response(msg.seq, false, "", 0, false, "invalid_target"));
        return;
    }
    if (result.reason == skill::skill_use_result::insufficient_materials)
    {
        conn->send(network::make_mine_response(msg.seq, false, "", 0, false, "no_pickaxe"));
        return;
    }
    if (result.reason == skill::skill_use_result::insufficient_skill)
    {
        conn->send(network::make_mine_response(msg.seq, false, "", 0, false, "insufficient_skill"));
        return;
    }

    if (!result.success)
    {
        conn->send(network::make_mine_response(msg.seq, false, "", 0, result.node_depleted, "miss"));
        return;
    }

    conn->send(network::make_mine_response(msg.seq, true, result.item_name, result.template_id, result.node_depleted));
}

// === Fishing ===

void game_handlers::handle_fish_skill_request(connection_id conn_id, const network::json_message& msg)
{
    auto* conn = require_in_game(conn_id, msg.seq);
    if (!conn)
        return;

    if (!fishing_)
    {
        send_error(conn_id, msg.seq, "not_available", "Fishing is not available");
        return;
    }

    auto pid = conn->player();
    auto* plr = players_->get_player(pid);
    if (!plr)
    {
        send_error(conn_id, msg.seq, "internal_error", "Player not found");
        return;
    }

    // Check if player has fishing skill
    if (skills_)
    {
        auto skill_level = skills_->get_skill_level(pid, skill::skill_type::fishing);
        if (skill_level == 0)
        {
            conn->send(network::make_fish_skill_response(msg.seq, false, "You don't know how to fish"));
            return;
        }
    }

    // Check if already fishing
    if (plr->fishing.fish_node_index != 0)
    {
        conn->send(network::make_fish_skill_response(msg.seq, false, "Already fishing"));
        return;
    }

    auto* map = world_->get_map(plr->current_map);
    if (!map)
    {
        send_error(conn_id, msg.seq, "internal_error", "Map not found");
        return;
    }

    auto map_name = std::string(map->name());
    auto fish_index = fishing_->check_fish_nearby(entity_id(pid.value), map_name, plr->pos.x, plr->pos.y);

    if (!fish_index)
    {
        conn->send(network::make_fish_skill_response(msg.seq, false, "No fish nearby"));
        return;
    }

    // Success — engaged callback already fired by fishing_system
    conn->send(network::make_fish_skill_response(msg.seq, true));
}

void game_handlers::handle_fish_catch_request(connection_id conn_id, const network::json_message& msg)
{
    auto* conn = require_in_game(conn_id, msg.seq);
    if (!conn)
        return;

    if (!fishing_)
    {
        send_error(conn_id, msg.seq, "not_available", "Fishing is not available");
        return;
    }

    auto pid = conn->player();
    auto* plr = players_->get_player(pid);
    if (!plr)
    {
        send_error(conn_id, msg.seq, "internal_error", "Player not found");
        return;
    }

    // attempt_catch handles all validation and fires callbacks
    fishing_->attempt_catch(entity_id(pid.value));
    // Response sent via catch_complete_callback
}

// ========== Crusade Handlers ==========

void game_handlers::handle_select_duty(connection_id conn_id, const network::json_message& msg)
{
    auto* conn = require_in_game(conn_id, msg.seq);
    if (!conn)
        return;

    if (!crusade_ || !crusade_->is_active())
    {
        conn->send(network::make_select_duty_response(msg.seq, false, 0, 0, "no_active_crusade"));
        return;
    }

    auto duty_val = msg.data.value("duty", 0);
    if (duty_val < 1 || duty_val > 3)
    {
        conn->send(network::make_select_duty_response(msg.seq, false, 0, 0, "invalid_duty"));
        return;
    }

    auto duty = static_cast<war::crusade_duty>(duty_val);
    auto pid = conn->player();

    // Auto-join the crusade if not already in it
    if (!crusade_->is_in_crusade(pid))
    {
        if (auto* plr = players_->get_player(pid))
        {
            auto faction = war::war_faction::neutral;
            if (plr->faction == hb::faction::aresden)
                faction = war::war_faction::aresden;
            else if (plr->faction == hb::faction::elvine)
                faction = war::war_faction::elvine;

            auto join_result = crusade_->join_crusade(pid, faction);
            if (join_result != war::crusade_result::success)
            {
                conn->send(network::make_select_duty_response(msg.seq, false, 0, 0, "cannot_join"));
                return;
            }
        }
    }

    auto result = crusade_->select_duty(pid, duty);
    if (result != war::crusade_result::success)
    {
        std::string_view err = "unknown_error";
        switch (result)
        {
        case war::crusade_result::already_has_duty:
            err = "already_has_duty";
            break;
        case war::crusade_result::not_in_crusade:
            err = "not_in_crusade";
            break;
        default:
            break;
        }
        conn->send(network::make_select_duty_response(msg.seq, false, 0, 0, err));
        return;
    }

    auto* data = crusade_->get_player_data(pid);
    conn->send(network::make_select_duty_response(
        msg.seq, true, static_cast<uint8_t>(duty), data ? data->construction_points : 0));
}

void game_handlers::handle_summon_war_unit(connection_id conn_id, const network::json_message& msg)
{
    auto* conn = require_in_game(conn_id, msg.seq);
    if (!conn)
        return;

    if (!crusade_ || !crusade_->is_active())
    {
        conn->send(network::make_summon_war_unit_response(msg.seq, false, 0, 0, "no_active_crusade"));
        return;
    }

    auto unit_val = msg.data.value("unit_type", 0);
    if (unit_val < 1 || unit_val > 11)
    {
        conn->send(network::make_summon_war_unit_response(msg.seq, false, 0, 0, "invalid_unit_type"));
        return;
    }

    auto unit_type = static_cast<war::war_unit_type>(unit_val);
    auto pid = conn->player();

    auto map_name = msg.data.value("map", std::string{});
    auto x = msg.data.value("x", int16_t{0});
    auto y = msg.data.value("y", int16_t{0});

    auto result = crusade_->summon_war_unit(pid, unit_type, map_name, x, y);
    if (result != war::crusade_result::success)
    {
        std::string_view err = "unknown_error";
        switch (result)
        {
        case war::crusade_result::not_constructor:
            err = "not_constructor";
            break;
        case war::crusade_result::insufficient_points:
            err = "insufficient_points";
            break;
        case war::crusade_result::not_in_crusade:
            err = "not_in_crusade";
            break;
        case war::crusade_result::invalid_unit:
            err = "invalid_unit";
            break;
        case war::crusade_result::restricted_map:
            err = "restricted_map";
            break;
        case war::crusade_result::no_construct_location:
            err = "no_construct_location";
            break;
        case war::crusade_result::too_far_from_construct_location:
            err = "too_far_from_construct_location";
            break;
        case war::crusade_result::guild_build_limit:
            err = "guild_build_limit";
            break;
        case war::crusade_result::too_close_to_tower:
            err = "too_close_to_tower";
            break;
        case war::crusade_result::invalid_position:
            err = "invalid_position";
            break;
        case war::crusade_result::wrong_map:
            err = "wrong_map";
            break;
        default:
            break;
        }
        conn->send(network::make_summon_war_unit_response(msg.seq, false, 0, 0, err));
        return;
    }

    auto* data = crusade_->get_player_data(pid);
    conn->send(network::make_summon_war_unit_response(
        msg.seq, true, static_cast<uint8_t>(unit_type), data ? data->construction_points : 0));
}

void game_handlers::handle_crusade_map_status(connection_id conn_id, const network::json_message& msg)
{
    auto* conn = require_in_game(conn_id, msg.seq);
    if (!conn)
        return;

    if (!crusade_ || !crusade_->is_active())
        return;

    auto pid = conn->player();
    auto* data = crusade_->get_player_data(pid);
    if (!data || data->duty != war::crusade_duty::commander)
        return;

    // Build structure list
    nlohmann::json structures = nlohmann::json::array();
    for (const auto& ws : crusade_->get_war_structures())
    {
        structures.push_back({{"type", static_cast<int>(ws.type)},
                              {"faction", static_cast<int>(ws.faction)},
                              {"map", ws.map_name},
                              {"x", ws.x},
                              {"y", ws.y}});
    }

    network::json_message response;
    response.type = network::json_message_type::crusade_map_status;
    response.seq = msg.seq;
    response.data = {{"structures", std::move(structures)}};

    conn->send(response);
}

void game_handlers::handle_set_guild_teleport(connection_id conn_id, const network::json_message& msg)
{
    auto* conn = require_in_game(conn_id, msg.seq);
    if (!conn)
        return;

    if (!crusade_ || !crusade_->is_active())
    {
        conn->send(network::make_set_guild_teleport_response(msg.seq, false, "no_active_crusade"));
        return;
    }

    auto map_name = msg.data.value("map", std::string{});
    auto x = msg.data.value("x", int16_t{0});
    auto y = msg.data.value("y", int16_t{0});

    auto result = crusade_->set_guild_teleport_location(conn->player(), map_name, x, y);
    if (result != war::crusade_result::success)
    {
        std::string_view err = "unknown_error";
        switch (result)
        {
        case war::crusade_result::not_in_crusade:
            err = "not_in_crusade";
            break;
        case war::crusade_result::not_guild_master:
            err = "not_guild_master";
            break;
        case war::crusade_result::invalid_position:
            err = "invalid_position";
            break;
        default:
            break;
        }
        conn->send(network::make_set_guild_teleport_response(msg.seq, false, err));
        return;
    }

    conn->send(network::make_set_guild_teleport_response(msg.seq, true));
}

void game_handlers::handle_guild_teleport(connection_id conn_id, const network::json_message& msg)
{
    auto* conn = require_in_game(conn_id, msg.seq);
    if (!conn)
        return;

    if (!crusade_ || !crusade_->is_active())
    {
        conn->send(network::make_guild_teleport_response(msg.seq, false, {}, 0, 0, "no_active_crusade"));
        return;
    }

    auto pid = conn->player();
    auto result = crusade_->use_guild_teleport(pid);
    if (result != war::crusade_result::success)
    {
        std::string_view err = "unknown_error";
        switch (result)
        {
        case war::crusade_result::not_in_crusade:
            err = "not_in_crusade";
            break;
        case war::crusade_result::no_construct_location:
            err = "no_teleport_set";
            break;
        default:
            break;
        }
        conn->send(network::make_guild_teleport_response(msg.seq, false, {}, 0, 0, err));
        return;
    }

    auto* dest = crusade_->get_guild_teleport_dest(pid);
    if (!dest)
    {
        conn->send(network::make_guild_teleport_response(msg.seq, false, {}, 0, 0, "no_teleport_set"));
        return;
    }

    conn->send(network::make_guild_teleport_response(msg.seq, true, dest->map_name, dest->x, dest->y));

    // Execute the teleport
    execute_player_teleport(pid, conn_id, msg.seq, dest->map_name, {dest->x, dest->y}, world::direction::south);
}

// ========== Friend System Handlers ==========

void game_handlers::handle_friend_request_send(connection_id conn_id, const network::json_message& msg)
{
    auto* conn = require_in_game(conn_id, msg.seq);
    if (!conn)
        return;

    auto data_result = network::friend_target_request_data::from_json(msg.data);
    if (data_result.is_err())
    {
        send_error(conn_id, msg.seq, "invalid_request", data_result.error());
        return;
    }

    auto* plr = players_->get_player(conn->player());
    if (!plr)
    {
        send_error(conn_id, msg.seq, "internal_error", "Player not found");
        return;
    }

    // Look up target character
    auto [target_char_id, target_name] = social_->lookup_character_by_name(data_result.value().target_name);
    if (!target_char_id.is_valid())
    {
        conn->send(network::make_friend_response(
            msg.seq, network::json_message_type::friend_request_send_response, false, "player_not_found"));
        return;
    }

    auto result = social_->send_friend_request(plr->character_id, target_char_id);
    bool success = result == social::friend_result::success;
    conn->send(network::make_friend_response(msg.seq,
                                             network::json_message_type::friend_request_send_response,
                                             success,
                                             std::string(social::to_string_view(result))));

    // If success, notify target if online
    if (success)
    {
        auto target_runtime = social_->get_friend_runtime_id(target_char_id);
        if (target_runtime.is_valid())
        {
            auto* target_conn = ws_server_->get_connection_by_player(target_runtime);
            if (target_conn)
            {
                target_conn->send(network::make_friend_request_notification(plr->name));
            }
        }
    }
}

void game_handlers::handle_friend_request_accept(connection_id conn_id, const network::json_message& msg)
{
    auto* conn = require_in_game(conn_id, msg.seq);
    if (!conn)
        return;

    auto data_result = network::friend_target_request_data::from_json(msg.data);
    if (data_result.is_err())
    {
        send_error(conn_id, msg.seq, "invalid_request", data_result.error());
        return;
    }

    auto* plr = players_->get_player(conn->player());
    if (!plr)
    {
        send_error(conn_id, msg.seq, "internal_error", "Player not found");
        return;
    }

    auto [requester_char_id, requester_name] = social_->lookup_character_by_name(data_result.value().target_name);
    if (!requester_char_id.is_valid())
    {
        conn->send(network::make_friend_response(
            msg.seq, network::json_message_type::friend_request_accept_response, false, "player_not_found"));
        return;
    }

    auto result = social_->accept_friend_request(plr->character_id, requester_char_id);
    bool success = result == social::friend_result::success;
    conn->send(network::make_friend_response(msg.seq,
                                             network::json_message_type::friend_request_accept_response,
                                             success,
                                             std::string(social::to_string_view(result))));

    // If success, notify requester if online
    if (success)
    {
        auto requester_runtime = social_->get_friend_runtime_id(requester_char_id);
        if (requester_runtime.is_valid())
        {
            auto* req_conn = ws_server_->get_connection_by_player(requester_runtime);
            if (req_conn)
            {
                req_conn->send(network::make_friend_accepted_notification(plr->name));
            }
        }
    }
}

void game_handlers::handle_friend_request_decline(connection_id conn_id, const network::json_message& msg)
{
    auto* conn = require_in_game(conn_id, msg.seq);
    if (!conn)
        return;

    auto data_result = network::friend_target_request_data::from_json(msg.data);
    if (data_result.is_err())
    {
        send_error(conn_id, msg.seq, "invalid_request", data_result.error());
        return;
    }

    auto* plr = players_->get_player(conn->player());
    if (!plr)
    {
        send_error(conn_id, msg.seq, "internal_error", "Player not found");
        return;
    }

    auto [requester_char_id, _] = social_->lookup_character_by_name(data_result.value().target_name);
    if (!requester_char_id.is_valid())
    {
        conn->send(network::make_friend_response(
            msg.seq, network::json_message_type::friend_request_decline_response, false, "player_not_found"));
        return;
    }

    auto result = social_->decline_friend_request(plr->character_id, requester_char_id);
    bool success = result == social::friend_result::success;
    conn->send(network::make_friend_response(msg.seq,
                                             network::json_message_type::friend_request_decline_response,
                                             success,
                                             std::string(social::to_string_view(result))));
}

void game_handlers::handle_friend_request_cancel(connection_id conn_id, const network::json_message& msg)
{
    auto* conn = require_in_game(conn_id, msg.seq);
    if (!conn)
        return;

    auto data_result = network::friend_target_request_data::from_json(msg.data);
    if (data_result.is_err())
    {
        send_error(conn_id, msg.seq, "invalid_request", data_result.error());
        return;
    }

    auto* plr = players_->get_player(conn->player());
    if (!plr)
    {
        send_error(conn_id, msg.seq, "internal_error", "Player not found");
        return;
    }

    auto [target_char_id, _] = social_->lookup_character_by_name(data_result.value().target_name);
    if (!target_char_id.is_valid())
    {
        conn->send(network::make_friend_response(
            msg.seq, network::json_message_type::friend_request_cancel_response, false, "player_not_found"));
        return;
    }

    auto result = social_->cancel_friend_request(plr->character_id, target_char_id);
    bool success = result == social::friend_result::success;
    conn->send(network::make_friend_response(msg.seq,
                                             network::json_message_type::friend_request_cancel_response,
                                             success,
                                             std::string(social::to_string_view(result))));
}

void game_handlers::handle_friend_remove(connection_id conn_id, const network::json_message& msg)
{
    auto* conn = require_in_game(conn_id, msg.seq);
    if (!conn)
        return;

    auto data_result = network::friend_target_request_data::from_json(msg.data);
    if (data_result.is_err())
    {
        send_error(conn_id, msg.seq, "invalid_request", data_result.error());
        return;
    }

    auto* plr = players_->get_player(conn->player());
    if (!plr)
    {
        send_error(conn_id, msg.seq, "internal_error", "Player not found");
        return;
    }

    auto [target_char_id, _] = social_->lookup_character_by_name(data_result.value().target_name);
    if (!target_char_id.is_valid())
    {
        conn->send(network::make_friend_response(
            msg.seq, network::json_message_type::friend_remove_response, false, "player_not_found"));
        return;
    }

    auto result = social_->remove_friend(plr->character_id, target_char_id);
    bool success = result == social::friend_result::success;
    conn->send(network::make_friend_response(msg.seq,
                                             network::json_message_type::friend_remove_response,
                                             success,
                                             std::string(social::to_string_view(result))));
}

void game_handlers::handle_friend_block(connection_id conn_id, const network::json_message& msg)
{
    auto* conn = require_in_game(conn_id, msg.seq);
    if (!conn)
        return;

    auto data_result = network::friend_target_request_data::from_json(msg.data);
    if (data_result.is_err())
    {
        send_error(conn_id, msg.seq, "invalid_request", data_result.error());
        return;
    }

    auto* plr = players_->get_player(conn->player());
    if (!plr)
    {
        send_error(conn_id, msg.seq, "internal_error", "Player not found");
        return;
    }

    auto [target_char_id, _] = social_->lookup_character_by_name(data_result.value().target_name);
    if (!target_char_id.is_valid())
    {
        conn->send(network::make_friend_response(
            msg.seq, network::json_message_type::friend_block_response, false, "player_not_found"));
        return;
    }

    auto result = social_->block_player_friend(plr->character_id, target_char_id);
    bool success = result == social::friend_result::success;
    conn->send(network::make_friend_response(msg.seq,
                                             network::json_message_type::friend_block_response,
                                             success,
                                             std::string(social::to_string_view(result))));
}

void game_handlers::handle_friend_unblock(connection_id conn_id, const network::json_message& msg)
{
    auto* conn = require_in_game(conn_id, msg.seq);
    if (!conn)
        return;

    auto data_result = network::friend_target_request_data::from_json(msg.data);
    if (data_result.is_err())
    {
        send_error(conn_id, msg.seq, "invalid_request", data_result.error());
        return;
    }

    auto* plr = players_->get_player(conn->player());
    if (!plr)
    {
        send_error(conn_id, msg.seq, "internal_error", "Player not found");
        return;
    }

    auto [target_char_id, _] = social_->lookup_character_by_name(data_result.value().target_name);
    if (!target_char_id.is_valid())
    {
        conn->send(network::make_friend_response(
            msg.seq, network::json_message_type::friend_unblock_response, false, "player_not_found"));
        return;
    }

    auto result = social_->unblock_player_friend(plr->character_id, target_char_id);
    bool success = result == social::friend_result::success;
    conn->send(network::make_friend_response(msg.seq,
                                             network::json_message_type::friend_unblock_response,
                                             success,
                                             std::string(social::to_string_view(result))));
}

void game_handlers::handle_friend_list(connection_id conn_id, const network::json_message& msg)
{
    auto* conn = require_in_game(conn_id, msg.seq);
    if (!conn)
        return;

    auto* plr = players_->get_player(conn->player());
    if (!plr)
    {
        send_error(conn_id, msg.seq, "internal_error", "Player not found");
        return;
    }

    auto char_id = plr->character_id;

    // Build friend list
    std::vector<network::friend_list_entry_msg> friends_list;
    auto* friends = social_->get_friends(char_id);
    if (friends)
    {
        for (const auto& f : *friends)
        {
            network::friend_list_entry_msg entry;
            entry.name = f.name;
            entry.is_online = f.is_online();
            friends_list.push_back(std::move(entry));
        }
    }

    // Build incoming requests
    std::vector<network::friend_request_msg> incoming;
    auto* in_reqs = social_->get_incoming_requests(char_id);
    if (in_reqs)
    {
        for (const auto& r : *in_reqs)
        {
            network::friend_request_msg req_msg;
            req_msg.name = r.requester_name;
            incoming.push_back(std::move(req_msg));
        }
    }

    // Build outgoing requests
    std::vector<network::friend_request_msg> outgoing;
    auto* out_reqs = social_->get_outgoing_requests(char_id);
    if (out_reqs)
    {
        for (const auto& r : *out_reqs)
        {
            network::friend_request_msg req_msg;
            req_msg.name = r.requestee_name;
            if (req_msg.name.empty())
            {
                // Fallback: look up from online players
                auto target_runtime = social_->get_friend_runtime_id(r.requestee_char_id);
                if (target_runtime.is_valid() && players_)
                {
                    auto* target_plr = players_->get_player(target_runtime);
                    if (target_plr)
                        req_msg.name = target_plr->name;
                }
            }
            outgoing.push_back(std::move(req_msg));
        }
    }

    // Build blocked list
    std::vector<std::string> blocked;
    auto* block_set = social_->get_blocked_players(char_id);
    if (block_set)
    {
        for (auto blocked_char_id : *block_set)
        {
            auto name = social_->lookup_character_name(blocked_char_id);
            blocked.push_back(name.empty() ? "Unknown" : name);
        }
    }

    conn->send(network::make_friend_list_response(msg.seq, friends_list, incoming, outgoing, blocked));
}

// ========== Guild Handlers ==========

void game_handlers::broadcast_guild_update(guild_id gid,
                                           const std::string& action,
                                           const std::string& guild_name,
                                           const std::string& player_name,
                                           const nlohmann::json& extra)
{
    if (!social_ || !ws_server_)
        return;

    auto* g = social_->get_guild(gid);
    if (!g)
        return;

    auto msg = network::make_guild_update(action, guild_name, player_name, extra);
    for (const auto& member : g->members)
    {
        if (member.player.is_valid())
        {
            auto* member_conn = ws_server_->get_connection_by_player(member.player);
            if (member_conn)
            {
                member_conn->send(msg);
            }
        }
    }
}

void game_handlers::handle_guild_create(connection_id conn_id, const network::json_message& msg)
{
    auto* conn = require_in_game(conn_id, msg.seq);
    if (!conn)
        return;

    auto data_result = network::guild_create_request_data::from_json(msg.data);
    if (data_result.is_err())
    {
        send_error(conn_id, msg.seq, "invalid_request", data_result.error());
        return;
    }

    auto* plr = players_->get_player(conn->player());
    if (!plr)
    {
        send_error(conn_id, msg.seq, "internal_error", "Player not found");
        return;
    }

    auto& data = data_result.value();
    auto result = social_->create_guild(conn->player(), data.name, data.tag);
    if (result.is_err())
    {
        conn->send(network::make_guild_response(
            msg.seq, network::json_message_type::guild_create_response, false, guild_result_string(result.error())));
        return;
    }

    plr->guild_name = data.name;
    plr->guild_tag = data.tag;
    plr->guild_rank = static_cast<uint8_t>(social::guild_rank::guild_master);

    conn->send(network::make_guild_response(msg.seq,
                                            network::json_message_type::guild_create_response,
                                            true,
                                            {},
                                            {{"guild_name", data.name}, {"tag", data.tag}}));

    send_guild_command_update(conn->player());
}

void game_handlers::handle_guild_disband(connection_id conn_id, const network::json_message& msg)
{
    auto* conn = require_in_game(conn_id, msg.seq);
    if (!conn)
        return;

    auto* plr = players_->get_player(conn->player());
    if (!plr)
    {
        send_error(conn_id, msg.seq, "internal_error", "Player not found");
        return;
    }

    auto gid = social_->get_player_guild(conn->player());
    if (!gid.is_valid())
    {
        conn->send(network::make_guild_response(
            msg.seq, network::json_message_type::guild_disband_response, false, "not_in_guild"));
        return;
    }

    auto* g = social_->get_guild(gid);
    if (!g)
    {
        conn->send(network::make_guild_response(
            msg.seq, network::json_message_type::guild_disband_response, false, "guild_not_found"));
        return;
    }

    // Collect member connections BEFORE disband
    std::string guild_name = g->name;
    std::vector<player_id> member_pids;
    for (const auto& member : g->members)
    {
        if (member.player.is_valid())
        {
            member_pids.push_back(member.player);
        }
    }

    auto result = social_->disband_guild(conn->player(), gid);
    if (result != social::guild_result::success)
    {
        conn->send(network::make_guild_response(
            msg.seq, network::json_message_type::guild_disband_response, false, guild_result_string(result)));
        return;
    }

    // Clear guild fields for all members
    for (auto mid : member_pids)
    {
        auto* member_plr = players_->get_player(mid);
        if (member_plr)
        {
            member_plr->guild_name.clear();
            member_plr->guild_tag.clear();
            member_plr->guild_rank = 0;
        }
    }

    // Notify all former members
    auto update_msg = network::make_guild_update("guild_disbanded", guild_name);
    for (auto mid : member_pids)
    {
        auto* member_conn = ws_server_->get_connection_by_player(mid);
        if (member_conn)
        {
            member_conn->send(update_msg);
        }
        send_guild_command_update(mid);
    }

    conn->send(network::make_guild_response(msg.seq, network::json_message_type::guild_disband_response, true));
}

void game_handlers::handle_guild_leave(connection_id conn_id, const network::json_message& msg)
{
    auto* conn = require_in_game(conn_id, msg.seq);
    if (!conn)
        return;

    auto* plr = players_->get_player(conn->player());
    if (!plr)
    {
        send_error(conn_id, msg.seq, "internal_error", "Player not found");
        return;
    }

    auto gid = social_->get_player_guild(conn->player());
    if (!gid.is_valid())
    {
        conn->send(network::make_guild_response(
            msg.seq, network::json_message_type::guild_leave_response, false, "not_in_guild"));
        return;
    }

    auto* g = social_->get_guild(gid);
    std::string guild_name = g ? g->name : "";

    auto result = social_->leave_guild(conn->player());
    if (result != social::guild_result::success)
    {
        conn->send(network::make_guild_response(
            msg.seq, network::json_message_type::guild_leave_response, false, guild_result_string(result)));
        return;
    }

    std::string player_name = plr->name;
    plr->guild_name.clear();
    plr->guild_tag.clear();
    plr->guild_rank = 0;

    conn->send(network::make_guild_response(msg.seq, network::json_message_type::guild_leave_response, true));

    send_guild_command_update(conn->player());

    // Broadcast to remaining members
    broadcast_guild_update(gid, "member_left", guild_name, player_name);
}

void game_handlers::handle_guild_kick(connection_id conn_id, const network::json_message& msg)
{
    auto* conn = require_in_game(conn_id, msg.seq);
    if (!conn)
        return;

    auto data_result = network::guild_target_request_data::from_json(msg.data);
    if (data_result.is_err())
    {
        send_error(conn_id, msg.seq, "invalid_request", data_result.error());
        return;
    }

    auto* plr = players_->get_player(conn->player());
    if (!plr)
    {
        send_error(conn_id, msg.seq, "internal_error", "Player not found");
        return;
    }

    // Resolve target by name
    auto* target = players_->get_player_by_name(data_result.value().target_name);
    if (!target)
    {
        conn->send(network::make_guild_response(
            msg.seq, network::json_message_type::guild_kick_response, false, "player_not_found"));
        return;
    }

    auto gid = social_->get_player_guild(conn->player());
    if (!gid.is_valid())
    {
        conn->send(network::make_guild_response(
            msg.seq, network::json_message_type::guild_kick_response, false, "not_in_guild"));
        return;
    }

    auto* g = social_->get_guild(gid);
    std::string guild_name = g ? g->name : "";

    auto result = social_->kick_from_guild(conn->player(), target->id);
    if (result != social::guild_result::success)
    {
        conn->send(network::make_guild_response(
            msg.seq, network::json_message_type::guild_kick_response, false, guild_result_string(result)));
        return;
    }

    std::string target_name = target->name;
    target->guild_name.clear();
    target->guild_tag.clear();
    target->guild_rank = 0;

    conn->send(network::make_guild_response(msg.seq, network::json_message_type::guild_kick_response, true));

    // Broadcast to remaining members
    broadcast_guild_update(gid, "member_kicked", guild_name, target_name);

    // Notify the kicked player
    auto* target_conn = ws_server_->get_connection_by_player(target->id);
    if (target_conn)
    {
        target_conn->send(network::make_guild_update("you_were_kicked", guild_name));
    }
    send_guild_command_update(target->id);
}

void game_handlers::handle_guild_invite(connection_id conn_id, const network::json_message& msg)
{
    auto* conn = require_in_game(conn_id, msg.seq);
    if (!conn)
        return;

    auto data_result = network::guild_target_request_data::from_json(msg.data);
    if (data_result.is_err())
    {
        send_error(conn_id, msg.seq, "invalid_request", data_result.error());
        return;
    }

    auto* plr = players_->get_player(conn->player());
    if (!plr)
    {
        send_error(conn_id, msg.seq, "internal_error", "Player not found");
        return;
    }

    // Resolve target by name
    auto* target = players_->get_player_by_name(data_result.value().target_name);
    if (!target)
    {
        conn->send(network::make_guild_response(
            msg.seq, network::json_message_type::guild_invite_response, false, "player_not_found"));
        return;
    }

    auto gid = social_->get_player_guild(conn->player());
    if (!gid.is_valid())
    {
        conn->send(network::make_guild_response(
            msg.seq, network::json_message_type::guild_invite_response, false, "not_in_guild"));
        return;
    }

    auto* g = social_->get_guild(gid);
    if (!g)
    {
        conn->send(network::make_guild_response(
            msg.seq, network::json_message_type::guild_invite_response, false, "guild_not_found"));
        return;
    }

    // Store pending invite (target must /gaccept)
    auto result = social_->invite_to_guild(conn->player(), gid, target->id);
    if (result != social::guild_result::success)
    {
        conn->send(network::make_guild_response(
            msg.seq, network::json_message_type::guild_invite_response, false, guild_result_string(result)));
        return;
    }

    conn->send(network::make_guild_response(msg.seq, network::json_message_type::guild_invite_response, true));

    // Push invite notification to target
    auto* target_conn = ws_server_->get_connection_by_player(target->id);
    if (target_conn)
    {
        target_conn->send(network::make_guild_invite_received(g->name, g->tag, plr->name));
    }
    send_guild_command_update(target->id);
}

void game_handlers::handle_guild_invite_respond(connection_id conn_id, const network::json_message& msg)
{
    auto* conn = require_in_game(conn_id, msg.seq);
    if (!conn)
        return;

    auto data_result = network::guild_invite_respond_request_data::from_json(msg.data);
    if (data_result.is_err())
    {
        send_error(conn_id, msg.seq, "invalid_request", data_result.error());
        return;
    }

    auto* plr = players_->get_player(conn->player());
    if (!plr)
    {
        send_error(conn_id, msg.seq, "internal_error", "Player not found");
        return;
    }

    if (!data_result.value().accept)
    {
        // Decline
        social_->decline_guild_invite(conn->player());
        conn->send(network::make_guild_response(msg.seq,
                                                network::json_message_type::guild_invite_respond_response,
                                                true,
                                                {},
                                                nlohmann::json{{"accepted", false}}));
        send_guild_command_update(conn->player());
        return;
    }

    // Accept
    auto accept_result = social_->accept_guild_invite(conn->player());
    if (accept_result.is_err())
    {
        conn->send(network::make_guild_response(msg.seq,
                                                network::json_message_type::guild_invite_respond_response,
                                                false,
                                                guild_result_string(accept_result.error())));
        return;
    }

    auto gid = accept_result.value();
    auto* g = social_->get_guild(gid);
    if (g)
    {
        plr->guild_name = g->name;
        plr->guild_tag = g->tag;
        plr->guild_rank = static_cast<uint8_t>(social::guild_rank::recruit);

        conn->send(network::make_guild_response(
            msg.seq,
            network::json_message_type::guild_invite_respond_response,
            true,
            {},
            nlohmann::json{{"accepted", true}, {"guild_name", g->name}, {"guild_tag", g->tag}}));

        broadcast_guild_update(gid, "member_joined", g->name, plr->name);
    }
    else
    {
        conn->send(network::make_guild_response(msg.seq,
                                                network::json_message_type::guild_invite_respond_response,
                                                true,
                                                {},
                                                nlohmann::json{{"accepted", true}}));
    }

    send_guild_command_update(conn->player());
}

void game_handlers::handle_guild_promote(connection_id conn_id, const network::json_message& msg)
{
    auto* conn = require_in_game(conn_id, msg.seq);
    if (!conn)
        return;

    auto data_result = network::guild_target_request_data::from_json(msg.data);
    if (data_result.is_err())
    {
        send_error(conn_id, msg.seq, "invalid_request", data_result.error());
        return;
    }

    auto* plr = players_->get_player(conn->player());
    if (!plr)
    {
        send_error(conn_id, msg.seq, "internal_error", "Player not found");
        return;
    }

    auto* target = players_->get_player_by_name(data_result.value().target_name);
    if (!target)
    {
        conn->send(network::make_guild_response(
            msg.seq, network::json_message_type::guild_promote_response, false, "player_not_found"));
        return;
    }

    auto gid = social_->get_player_guild(conn->player());
    std::string guild_name = plr->guild_name;

    auto result = social_->promote_member(conn->player(), target->id);
    if (result != social::guild_result::success)
    {
        conn->send(network::make_guild_response(
            msg.seq, network::json_message_type::guild_promote_response, false, guild_result_string(result)));
        return;
    }

    // Update target's guild_rank from the actual guild data
    auto* g = social_->get_guild(gid);
    if (g)
    {
        auto* member = g->get_member(target->id);
        if (member)
        {
            target->guild_rank = static_cast<uint8_t>(member->rank);
        }
    }

    conn->send(network::make_guild_response(msg.seq, network::json_message_type::guild_promote_response, true));

    send_guild_command_update(target->id);

    broadcast_guild_update(gid, "member_promoted", guild_name, target->name);
}

void game_handlers::handle_guild_demote(connection_id conn_id, const network::json_message& msg)
{
    auto* conn = require_in_game(conn_id, msg.seq);
    if (!conn)
        return;

    auto data_result = network::guild_target_request_data::from_json(msg.data);
    if (data_result.is_err())
    {
        send_error(conn_id, msg.seq, "invalid_request", data_result.error());
        return;
    }

    auto* plr = players_->get_player(conn->player());
    if (!plr)
    {
        send_error(conn_id, msg.seq, "internal_error", "Player not found");
        return;
    }

    auto* target = players_->get_player_by_name(data_result.value().target_name);
    if (!target)
    {
        conn->send(network::make_guild_response(
            msg.seq, network::json_message_type::guild_demote_response, false, "player_not_found"));
        return;
    }

    auto gid = social_->get_player_guild(conn->player());
    std::string guild_name = plr->guild_name;

    auto result = social_->demote_member(conn->player(), target->id);
    if (result != social::guild_result::success)
    {
        conn->send(network::make_guild_response(
            msg.seq, network::json_message_type::guild_demote_response, false, guild_result_string(result)));
        return;
    }

    auto* g = social_->get_guild(gid);
    if (g)
    {
        auto* member = g->get_member(target->id);
        if (member)
        {
            target->guild_rank = static_cast<uint8_t>(member->rank);
        }
    }

    conn->send(network::make_guild_response(msg.seq, network::json_message_type::guild_demote_response, true));

    send_guild_command_update(target->id);

    broadcast_guild_update(gid, "member_demoted", guild_name, target->name);
}

void game_handlers::handle_guild_set_motd(connection_id conn_id, const network::json_message& msg)
{
    auto* conn = require_in_game(conn_id, msg.seq);
    if (!conn)
        return;

    auto data_result = network::guild_set_motd_request_data::from_json(msg.data);
    if (data_result.is_err())
    {
        send_error(conn_id, msg.seq, "invalid_request", data_result.error());
        return;
    }

    auto* plr = players_->get_player(conn->player());
    if (!plr)
    {
        send_error(conn_id, msg.seq, "internal_error", "Player not found");
        return;
    }

    auto gid = social_->get_player_guild(conn->player());
    if (!gid.is_valid())
    {
        conn->send(network::make_guild_response(
            msg.seq, network::json_message_type::guild_set_motd_response, false, "not_in_guild"));
        return;
    }

    auto result = social_->set_guild_motd(conn->player(), data_result.value().motd);
    if (result != social::guild_result::success)
    {
        conn->send(network::make_guild_response(
            msg.seq, network::json_message_type::guild_set_motd_response, false, guild_result_string(result)));
        return;
    }

    conn->send(network::make_guild_response(msg.seq, network::json_message_type::guild_set_motd_response, true));

    broadcast_guild_update(gid, "motd_changed", plr->guild_name, {}, {{"motd", data_result.value().motd}});
}

void game_handlers::handle_guild_info(connection_id conn_id, const network::json_message& msg)
{
    auto* conn = require_in_game(conn_id, msg.seq);
    if (!conn)
        return;

    auto* plr = players_->get_player(conn->player());
    if (!plr)
    {
        send_error(conn_id, msg.seq, "internal_error", "Player not found");
        return;
    }

    auto gid = social_->get_player_guild(conn->player());
    if (!gid.is_valid())
    {
        conn->send(network::make_guild_info_response(msg.seq, false, {}, {}, {}, 0, {}, {}, "not_in_guild"));
        return;
    }

    auto* g = social_->get_guild(gid);
    if (!g)
    {
        conn->send(network::make_guild_info_response(msg.seq, false, {}, {}, {}, 0, {}, {}, "guild_not_found"));
        return;
    }

    // Find master name
    std::string master_name;
    for (const auto& member : g->members)
    {
        if (member.rank == social::guild_rank::guild_master)
        {
            master_name = member.name;
            break;
        }
    }

    // Build member list
    std::vector<network::guild_member_info_msg> members;
    for (const auto& member : g->members)
    {
        auto rank_idx = static_cast<size_t>(member.rank);
        std::string rank_name = rank_idx < g->ranks.size() ? g->ranks[rank_idx].name : "Unknown";

        members.push_back(network::guild_member_info_msg{.name = member.name,
                                                         .rank = static_cast<uint8_t>(member.rank),
                                                         .rank_name = rank_name,
                                                         .is_online = member.player.is_valid()});
    }

    conn->send(network::make_guild_info_response(
        msg.seq, true, g->name, g->tag, g->motd, g->member_count(), master_name, members));
}

// === Command list registry and helpers ===

auto game_handlers::get_player_commands() -> const std::vector<command_descriptor>&
{
    static const std::vector<command_descriptor> commands = []()
    {
        std::vector<command_descriptor> cmds;

        // General commands (always enabled)
        cmds.push_back({"online", "Show online player count", "/online", command_category::general, nullptr});
        cmds.push_back({"time", "Show server time", "/time", command_category::general, nullptr});
        cmds.push_back({"pos", "Show your position", "/pos", command_category::general, nullptr});

        // Guild commands
        cmds.push_back({"gcreate",
                        "Create a new guild",
                        "/gcreate <name> [tag]",
                        command_category::guild,
                        [](const player::player& plr, const social::social_system* social) -> bool
                        {
                            if (!social)
                                return false;
                            return !social->get_player_guild(plr.id).is_valid();
                        }});

        cmds.push_back({"gdisband",
                        "Disband your guild",
                        "/gdisband",
                        command_category::guild,
                        [](const player::player& plr, const social::social_system* social) -> bool
                        {
                            if (!social)
                                return false;
                            auto gid = social->get_player_guild(plr.id);
                            if (!gid.is_valid())
                                return false;
                            auto* g = social->get_guild(gid);
                            return g && g->has_permission(plr.id, social::guild_permission::disband);
                        }});

        cmds.push_back({"ginvite",
                        "Invite a player to your guild",
                        "/ginvite <player>",
                        command_category::guild,
                        [](const player::player& plr, const social::social_system* social) -> bool
                        {
                            if (!social)
                                return false;
                            auto gid = social->get_player_guild(plr.id);
                            if (!gid.is_valid())
                                return false;
                            auto* g = social->get_guild(gid);
                            return g && g->has_permission(plr.id, social::guild_permission::invite);
                        }});

        cmds.push_back({"gkick",
                        "Kick a member from your guild",
                        "/gkick <player>",
                        command_category::guild,
                        [](const player::player& plr, const social::social_system* social) -> bool
                        {
                            if (!social)
                                return false;
                            auto gid = social->get_player_guild(plr.id);
                            if (!gid.is_valid())
                                return false;
                            auto* g = social->get_guild(gid);
                            return g && g->has_permission(plr.id, social::guild_permission::kick);
                        }});

        cmds.push_back({"gaccept", "Accept a guild invite", "/gaccept", command_category::guild, nullptr});

        cmds.push_back({"gdecline", "Decline a guild invite", "/gdecline", command_category::guild, nullptr});

        cmds.push_back({"gquit",
                        "Leave your guild",
                        "/gquit",
                        command_category::guild,
                        [](const player::player& plr, const social::social_system* social) -> bool
                        {
                            if (!social)
                                return false;
                            auto gid = social->get_player_guild(plr.id);
                            if (!gid.is_valid())
                                return false;
                            auto* g = social->get_guild(gid);
                            // Guild master cannot /gquit — must /gdisband
                            return g && g->master != plr.id;
                        }});

        return cmds;
    }();

    return commands;
}

auto game_handlers::evaluate_guild_commands(player_id pid) -> std::vector<std::pair<std::string, bool>>
{
    std::vector<std::pair<std::string, bool>> result;
    auto* plr = players_ ? players_->get_player(pid) : nullptr;
    if (!plr)
        return result;

    for (const auto& cmd : get_player_commands())
    {
        if (cmd.category != command_category::guild)
            continue;
        bool enabled = cmd.enabled_check ? cmd.enabled_check(*plr, social_) : true;
        result.emplace_back(cmd.name, enabled);
    }
    return result;
}

void game_handlers::send_available_commands(player_id pid, connection_id conn_id)
{
    auto* conn = get_connection(conn_id);
    if (!conn)
        return;
    auto* plr = players_ ? players_->get_player(pid) : nullptr;
    if (!plr)
        return;

    auto category_str = [](command_category cat) -> std::string
    {
        switch (cat)
        {
        case command_category::general:
            return "general";
        case command_category::guild:
            return "guild";
        case command_category::social:
            return "social";
        case command_category::gm:
            return "gm";
        case command_category::admin:
            return "admin";
        }
        return "general";
    };

    std::vector<network::command_entry_msg> entries;

    // Add player commands
    for (const auto& cmd : get_player_commands())
    {
        bool enabled = cmd.enabled_check ? cmd.enabled_check(*plr, social_) : true;
        entries.push_back({cmd.name, cmd.description, cmd.usage, category_str(cmd.category), enabled});
    }

    // Add admin commands for GM+ players
    if (plr->is_gm() && admin_)
    {
        auto admin_lvl = admin_->get_admin_level(pid);
        auto admin_cmds = admin_->get_commands_for_level(admin_lvl);
        for (const auto* info : admin_cmds)
        {
            if (info->hidden)
                continue;
            std::string cat = (info->required_level >= admin::admin_level::admin) ? "admin" : "gm";
            entries.push_back({info->name, info->description, info->usage, cat, true});
            for (const auto& alias : info->aliases)
            {
                entries.push_back({alias, info->description, info->usage, cat, true});
            }
        }
    }

    conn->send(network::make_available_commands(entries));
}

void game_handlers::send_guild_command_update(player_id pid)
{
    auto* plr = players_ ? players_->get_player(pid) : nullptr;
    if (!plr || plr->connection.value == 0)
        return;
    auto* conn = ws_server_ ? ws_server_->get_connection(plr->connection) : nullptr;
    if (!conn || !conn->is_open())
        return;

    auto changes = evaluate_guild_commands(pid);
    if (!changes.empty())
    {
        conn->send(network::make_command_availability_update(changes));
    }
}

} // namespace hb::bridge
