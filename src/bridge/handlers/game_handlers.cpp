// game_handlers.cpp
// Core message dispatching and utility methods for game_handlers.
//
// Implementation split across multiple files:
//   game_handlers_movement.cpp  - Movement, teleport, visibility
//   game_handlers_combat.cpp    - Combat, damage, death, respawn
//   game_handlers_npc.cpp       - NPC broadcasts, ground items, loot
//   game_handlers_shop.cpp      - Shops, banking, dialog, pickup
//   game_handlers_chat.cpp      - Chat, commands, command registry
//   game_handlers_social.cpp    - Friends, guilds
//   game_handlers_equipment.cpp - Equip/unequip, use item, upgrade, abilities
//   game_handlers_crafting.cpp  - Manufacturing, alchemy, mining, fishing, war

// Include platform header first to define NOMINMAX before Windows headers
#include "platform/platform.h"

#include "bridge/handlers/game_handlers.h"
#include "bridge/handlers/broadcast_util.h"
#include "network/websocket_server.h"
#include "player/player_system.h"
#include "world/world_subsystem.h"
#include "social/social_system.h"
#include "combat/combat_system.h"
#include "combat/combat_events.h"
#include "npc/npc_system.h"
#include "npc/npc.h"
#include "inventory/inventory_system.h"
#include "item/item_system.h"
#include "magic/magic_system.h"
#include "magic/spell.h"
#include "crafting/mining_system.h"
#include "crafting/fishing_system.h"
#include "registry/mining_registry.h"
#include "skill/skill_system.h"
#include "scheduler/scheduler.h"
#include "core/subsystem.h"
#include "core/logger.h"
#include "perf/perf_stats.h"
#include "war/crusade/crusade_system.h"
#include "config/config_system.h"

#include <chrono>
#include <random>

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
                               audit::item_audit_system* audit,
                               config_system* config)
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
    config_ = config;

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

    // Register regen and hunger change callbacks
    if (players_)
    {
        players_->on_regen([this](player_id pid, int32_t hp, int32_t mp, int32_t sp)
                           { send_vitals_update(pid, hp, mp, sp); });
        players_->on_hunger_change([this](player_id pid, int8_t, int8_t new_level)
                                   { send_hunger_update(pid, new_level); });
    }

    // Register skill level-up callback — broadcast to visible players
    if (skills_ && players_ && ws_server_)
    {
        skills_->on_level_up(
            [this](const skill::skill_level_event& event)
            {
                auto* plr = players_->get_player(event.player);
                if (!plr)
                    return;

                auto* ps = skills_->get_player_skills(event.player);
                if (!ps)
                    return;

                const auto& ss = ps->get(event.skill);
                auto skill_idx = static_cast<uint8_t>(event.skill);
                network::skill_entry_msg entry{
                    .skill_id = skill_idx,
                    .level = event.new_level,
                    .total_uses = ss.total_uses,
                    .uses_this_level = ss.uses_this_level,
                    .uses_to_next_level = skills_->uses_to_next_level(event.skill, event.new_level)
                };

                auto msg = network::make_skill_update(event.player.value, entry, event.old_level);
                broadcast_to_visible(players_, ws_server_, plr->current_map, plr->pos, msg);
            });

        skills_->on_skill_progress(
            [this](const skill::skill_progress_event& event)
            {
                auto* plr = players_->get_player(event.player);
                if (!plr)
                    return;

                auto* conn = ws_server_->get_connection(plr->connection);
                if (!conn || !conn->is_open())
                    return;

                conn->send(network::make_skill_progress(
                    static_cast<uint8_t>(event.skill),
                    event.uses_this_level,
                    event.uses_to_next_level,
                    event.percent));
            });
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
            [this](const npc::npc& n, const world::position& old_pos)
            {
                broadcast_npc_move(n, old_pos); // Copies data immediately
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
                auto msg = network::make_entity_despawn(0, n.entity_id.id);
                broadcast_to_visible(players_, ws_server_, n.current_map, n.pos, msg);
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
                broadcast_to_visible(players_, ws_server_, map->id(), pos, msg);
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
                broadcast_to_visible(players_, ws_server_, map->id(), pos, msg);
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
                broadcast_to_visible(players_, ws_server_, map->id(), pos, msg);
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
                broadcast_to_visible(players_, ws_server_, map->id(), pos, msg);
            });

        fishing_->set_engaged_callback(
            [this](entity_id player_eid, const crafting::fish_type_config& fish_cfg, int32_t initial_chance)
            {
                auto* perf = subsystems().get<perf::perf_stats_system>();
                PERF_TIMER(perf, perf::metric_category::broadcast);

                if (!players_ || !ws_server_)
                    return;

                auto pid = player_id{player_eid.value};
                auto* plr = players_->get_player(pid);
                if (!plr || plr->connection.value == 0)
                    return;

                auto msg = network::make_fish_engaged(player_eid, fish_cfg.name, fish_cfg.visual_type, initial_chance);
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

    // Register and start ground item despawn timer (every 30 seconds, expiry from config)
    if (scheduler_ && world_)
    {
        scheduler_->register_task("ground_item_cleanup",
                                  "Remove expired ground items",
                                  duration_ms{30000},
                                  true,
                                  [this]() -> task_callback
                                  {
                                      return [this]()
                                      {
                                          uint32_t expiry_secs = 600; // default 10 minutes
                                          if (config_)
                                          {
                                              expiry_secs = config_->server().ground_item_expiry_seconds;
                                          }
                                          auto expired = world_->remove_expired_ground_items(
                                              std::chrono::seconds(expiry_secs));
                                          for (const auto& [map, pos, expired_item] : expired)
                                          {
                                              {
                                                  network::ground_item_removed_data data{.picker_id = 0,
                                                                                         .picker_name = "",
                                                                                         .item_id = expired_item.value,
                                                                                         .item_name = "",
                                                                                         .x = pos.x,
                                                                                         .y = pos.y};
                                                  auto msg = network::make_ground_item_removed(data);
                                                  broadcast_to_visible(players_, ws_server_, map, pos, msg);
                                              }

                                              if (item_)
                                              {
                                                  item_->destroy_item(expired_item);
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

bool game_handlers::handle_message(connection_id conn_id, const network::json_message& msg)
{
    auto* perf = subsystems().get<perf::perf_stats_system>();
    PERF_TIMER(perf, perf::metric_category::message_handler);

    switch (msg.type)
    {
    // Movement
    case network::json_message_type::player_move_request:
        handle_player_move(conn_id, msg);
        return true;
    case network::json_message_type::player_stop_request:
        handle_player_stop(conn_id, msg);
        return true;

    // Combat
    case network::json_message_type::player_attack_request:
        handle_player_attack(conn_id, msg);
        return true;

    // Actions
    case network::json_message_type::player_magic_request:
        handle_player_magic(conn_id, msg);
        return true;
    case network::json_message_type::player_skill_request:
        handle_player_skill(conn_id, msg);
        return true;
    case network::json_message_type::player_pickup_request:
    case network::json_message_type::pickup_request:
        handle_player_pickup(conn_id, msg);
        return true;
    case network::json_message_type::player_interact_request:
        handle_player_interact(conn_id, msg);
        return true;

    // Equipment (v1 + v2)
    case network::json_message_type::player_equip_request:
    case network::json_message_type::equip_request:
        handle_player_equip(conn_id, msg);
        return true;
    case network::json_message_type::player_unequip_request:
    case network::json_message_type::unequip_request:
        handle_player_unequip(conn_id, msg);
        return true;

    // NPC interaction - shops (v1: quote/confirm cycle)
    case network::json_message_type::shop_buy_request:
        handle_shop_buy(conn_id, msg);
        return true;
    case network::json_message_type::shop_sell_request:
        handle_shop_sell(conn_id, msg);
        return true;
    case network::json_message_type::shop_sell_confirm_request:
        handle_shop_sell_confirm(conn_id, msg);
        return true;
    case network::json_message_type::shop_repair_request:
        handle_shop_repair(conn_id, msg);
        return true;
    case network::json_message_type::shop_repair_confirm_request:
        handle_shop_repair_confirm(conn_id, msg);
        return true;

    // NPC interaction - shops (v2: single-step via item_ops)
    case network::json_message_type::shop_buy_request_v2:
        handle_shop_buy_v2(conn_id, msg);
        return true;
    case network::json_message_type::shop_sell_request_v2:
        handle_shop_sell_v2(conn_id, msg);
        return true;
    case network::json_message_type::shop_repair_request_v2:
        handle_shop_repair_v2(conn_id, msg);
        return true;

    // NPC interaction - banking (v1)
    case network::json_message_type::bank_deposit_request:
        handle_bank_deposit(conn_id, msg);
        return true;
    case network::json_message_type::bank_withdraw_request:
        handle_bank_withdraw(conn_id, msg);
        return true;

    // NPC interaction - banking (v2)
    case network::json_message_type::bank_deposit_request_v2:
        handle_bank_deposit_v2(conn_id, msg);
        return true;
    case network::json_message_type::bank_withdraw_request_v2:
        handle_bank_withdraw_v2(conn_id, msg);
        return true;
    case network::json_message_type::bank_reposition_request:
        handle_bank_reposition(conn_id, msg);
        return true;

    // NPC interaction - dialog
    case network::json_message_type::dialog_choice_request:
        handle_dialog_choice(conn_id, msg);
        return true;

    // Chat
    case network::json_message_type::chat_message:
        handle_chat_message(conn_id, msg);
        return true;

    // Commands
    case network::json_message_type::command_request:
        handle_command(conn_id, msg);
        return true;

    // View range
    case network::json_message_type::set_view_range:
        handle_set_view_range(conn_id, msg);
        return true;

    // Entity info
    case network::json_message_type::entity_info_request:
        handle_entity_info_request(conn_id, msg);
        return true;

    // Crafting - manufacturing
    case network::json_message_type::manufacture_list_request:
        handle_manufacture_list_request(conn_id, msg);
        return true;
    case network::json_message_type::manufacture_request:
        handle_manufacture_request(conn_id, msg);
        return true;

    // Crafting - alchemy
    case network::json_message_type::alchemy_list_request:
        handle_alchemy_list_request(conn_id, msg);
        return true;
    case network::json_message_type::alchemy_request:
        handle_alchemy_request(conn_id, msg);
        return true;

    // Mining
    case network::json_message_type::mine_request:
        handle_mine_request(conn_id, msg);
        return true;

    // Fishing
    case network::json_message_type::fish_skill_request:
        handle_fish_skill_request(conn_id, msg);
        return true;
    case network::json_message_type::fish_catch_request:
        handle_fish_catch_request(conn_id, msg);
        return true;

    // Crusade
    case network::json_message_type::select_duty_request:
        handle_select_duty(conn_id, msg);
        return true;
    case network::json_message_type::summon_war_unit_request:
        handle_summon_war_unit(conn_id, msg);
        return true;
    case network::json_message_type::crusade_map_status:
        handle_crusade_map_status(conn_id, msg);
        return true;
    case network::json_message_type::crusade_set_guild_teleport_request:
        handle_set_guild_teleport(conn_id, msg);
        return true;
    case network::json_message_type::crusade_guild_teleport_request:
        handle_guild_teleport(conn_id, msg);
        return true;

    // Friends
    case network::json_message_type::friend_request_send_request:
        handle_friend_request_send(conn_id, msg);
        return true;
    case network::json_message_type::friend_request_accept_request:
        handle_friend_request_accept(conn_id, msg);
        return true;
    case network::json_message_type::friend_request_decline_request:
        handle_friend_request_decline(conn_id, msg);
        return true;
    case network::json_message_type::friend_request_cancel_request:
        handle_friend_request_cancel(conn_id, msg);
        return true;
    case network::json_message_type::friend_remove_request:
        handle_friend_remove(conn_id, msg);
        return true;
    case network::json_message_type::friend_block_request:
        handle_friend_block(conn_id, msg);
        return true;
    case network::json_message_type::friend_unblock_request:
        handle_friend_unblock(conn_id, msg);
        return true;
    case network::json_message_type::friend_list_request:
        handle_friend_list(conn_id, msg);
        return true;

    // Guilds
    case network::json_message_type::guild_create_request:
        handle_guild_create(conn_id, msg);
        return true;
    case network::json_message_type::guild_disband_request:
        handle_guild_disband(conn_id, msg);
        return true;
    case network::json_message_type::guild_leave_request:
        handle_guild_leave(conn_id, msg);
        return true;
    case network::json_message_type::guild_kick_request:
        handle_guild_kick(conn_id, msg);
        return true;
    case network::json_message_type::guild_invite_request:
        handle_guild_invite(conn_id, msg);
        return true;
    case network::json_message_type::guild_invite_respond_request:
        handle_guild_invite_respond(conn_id, msg);
        return true;
    case network::json_message_type::guild_promote_request:
        handle_guild_promote(conn_id, msg);
        return true;
    case network::json_message_type::guild_demote_request:
        handle_guild_demote(conn_id, msg);
        return true;
    case network::json_message_type::guild_set_motd_request:
        handle_guild_set_motd(conn_id, msg);
        return true;
    case network::json_message_type::guild_info_request:
        handle_guild_info(conn_id, msg);
        return true;

    // Item usage (v1 + v2)
    case network::json_message_type::player_use_item_request:
    case network::json_message_type::use_item_request:
        handle_player_use_item(conn_id, msg);
        return true;

    // Combat mode
    case network::json_message_type::combat_mode_change_request:
        handle_combat_mode_change(conn_id, msg);
        return true;

    // Item upgrade (v1 + v2)
    case network::json_message_type::item_upgrade_request:
    case network::json_message_type::upgrade_request:
        handle_item_upgrade(conn_id, msg);
        return true;

    // Special ability (v1 + v2)
    case network::json_message_type::activate_ability_request:
    case network::json_message_type::activate_ability_request_v2:
        handle_activate_ability(conn_id, msg);
        return true;

    // Inventory management
    case network::json_message_type::inventory_reposition_request:
        handle_inventory_reposition(conn_id, msg);
        return true;

    case network::json_message_type::player_drop_item_request:
    case network::json_message_type::drop_request:
        handle_player_drop_item(conn_id, msg);
        return true;

    // Death/Respawn
    case network::json_message_type::respawn_request:
        handle_respawn_request(conn_id, msg);
        return true;

    // Trading (v2: 3-phase offer -> lock -> confirm)
    case network::json_message_type::trade_request:
        handle_trade_request_v2(conn_id, msg);
        return true;
    case network::json_message_type::trade_accept:
        handle_trade_accept_v2(conn_id, msg);
        return true;
    case network::json_message_type::trade_decline:
        handle_trade_decline_v2(conn_id, msg);
        return true;
    case network::json_message_type::trade_add_item:
        handle_trade_add_item_v2(conn_id, msg);
        return true;
    case network::json_message_type::trade_remove_item:
        handle_trade_remove_item_v2(conn_id, msg);
        return true;
    case network::json_message_type::trade_set_gold:
        handle_trade_set_gold_v2(conn_id, msg);
        return true;
    case network::json_message_type::trade_lock:
        handle_trade_lock_v2(conn_id, msg);
        return true;
    case network::json_message_type::trade_confirm:
        handle_trade_confirm_v2(conn_id, msg);
        return true;
    case network::json_message_type::trade_cancel:
        handle_trade_cancel_v2(conn_id, msg);
        return true;

    default:
        return false;
    }
}

// ========== Core Utility Methods ==========

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

// ========== Broadcast Helpers ==========

void game_handlers::broadcast_player_action(const player::player& plr,
                                            const network::player_action_broadcast_data& data)
{
    if (!players_ || !ws_server_)
        return;

    auto msg = network::make_player_action_broadcast(data);
    broadcast_to_visible(players_, ws_server_, plr.current_map, plr.pos, msg, plr.id);

    for (auto admin_conn : ws_server_->get_admin_subscribers(plr.current_map))
    {
        ws_server_->send(admin_conn, msg);
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
    // First check if it's a player (resolve ecs entity_id -> player_id)
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

// ========== Vitals Update (regen) ==========

void game_handlers::send_vitals_update(player_id pid, int32_t hp, int32_t mp, int32_t sp)
{
    if (!players_ || !ws_server_)
        return;

    auto* player = players_->get_player(pid);
    if (!player || player->connection.value == 0)
        return;

    auto* conn = ws_server_->get_connection(player->connection);
    if (!conn || !conn->is_open())
        return;

    network::stat_update_data data
    {
        .max_hp = player->computed.max_hp,
        .max_mp = player->computed.max_mp,
        .max_sp = player->computed.max_sp,
        .hp = hp,
        .mp = mp,
        .sp = sp,
    };
    conn->send(network::make_stat_update(data));
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

            // Chance to start new weather (1-in-30 per 10s tick ~ legacy 1-in-300 per 1s tick)
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

// ========== Stat Updates ==========

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
                                   .critical_rate = plr.computed.critical_rate,
                                   .max_weight = plr.computed.strength * 500 + plr.experience.level * 500};
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
        gold = static_cast<int32_t>(inventory_->get_gold(entity_id(plr.id.value)));
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
                                   .max_weight = plr.computed.strength * 500 + plr.experience.level * 500,
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

} // namespace hb::bridge
