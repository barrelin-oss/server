#include "platform/platform.h"
#include "bridge/handlers/game_handlers.h"
#include "bridge/handlers/broadcast_util.h"
#include "network/websocket_server.h"
#include "player/player_system.h"
#include "world/world_subsystem.h"
#include "npc/npc.h"
#include "npc/loot_generator.h"
#include "registry/loot_registry.h"
#include "registry/item_registry.h"
#include "inventory/inventory_system.h"
#include "item/item_system.h"
#include "item/item_ops.h"
#include "item/item_serialization.h"
#include "audit/item_audit_system.h"
#include "core/subsystem.h"
#include "core/logger.h"
#include "perf/perf_stats.h"

namespace hb::bridge
{

void game_handlers::broadcast_npc_spawn(const npc::npc& n)
{
    auto* perf = subsystems().get<perf::perf_stats_system>();
    PERF_TIMER(perf, perf::metric_category::broadcast);

    if (!players_ || !ws_server_)
        return;

    auto cat_str = std::string(npc::npc_category_to_string(n.category));

    auto attr_strs = npc::npc_special_ability_strings(n.special_ability);

    // Send per-player (hostility is viewer-relative)
    for_each_visible_connection(
        players_,
        ws_server_,
        n.current_map,
        n.pos,
        [&](player_id, player::player& p, network::ws_connection& conn)
        {
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
                                             n, p.faction, p.pk.is_criminal(), p.pk.is_murderer())),
                                         .attributes = attr_strs};
            conn.send(network::make_npc_spawn_message(data));
        });

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
                                       .hostility = "neutral",
                                       .attributes = std::move(attr_strs)};
    auto admin_msg = network::make_npc_spawn_message(admin_data);
    for (auto admin_conn : ws_server_->get_admin_subscribers(n.current_map))
    {
        ws_server_->send(admin_conn, admin_msg);
    }
}

void game_handlers::broadcast_npc_move(const npc::npc& n, const world::position& old_pos)
{
    auto* perf = subsystems().get<perf::perf_stats_system>();
    PERF_TIMER(perf, perf::metric_category::broadcast);

    if (!players_ || !ws_server_)
        return;

    auto cat_str = std::string(npc::npc_category_to_string(n.category));
    auto attr_strs = npc::npc_special_ability_strings(n.special_ability);

    network::npc_move_data move_data{
        .entity_id = n.entity_id.id, .x = n.pos.x, .y = n.pos.y, .direction = static_cast<uint8_t>(n.facing)};

    auto move_msg = network::make_npc_move_message(move_data);

    // For each player who can see the NPC's new position, check if they could also
    // see the old position. If not, this NPC just entered their view — send a full
    // npc_spawn instead of just a move.
    for_each_visible_connection(
        players_,
        ws_server_,
        n.current_map,
        n.pos,
        [&](player_id, player::player& p, network::ws_connection& conn)
        {
            bool could_see_old = p.sees_all
                || (std::abs(old_pos.x - p.pos.x) <= p.visibility_radius_x
                    && std::abs(old_pos.y - p.pos.y) <= p.visibility_radius_y);

            if (could_see_old)
            {
                // Player already knows about this NPC — just send the move
                conn.send(move_msg);
            }
            else
            {
                // NPC just entered this player's view — send full spawn
                network::npc_spawn_data spawn{.entity_id = n.entity_id.id,
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
                                                  n, p.faction, p.pk.is_criminal(), p.pk.is_murderer())),
                                              .attributes = attr_strs};
                conn.send(network::make_npc_spawn_message(spawn));
            }
        });

    // Forward move to admin spectators
    for (auto admin_conn : ws_server_->get_admin_subscribers(n.current_map))
    {
        ws_server_->send(admin_conn, move_msg);
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
    broadcast_to_visible(players_, ws_server_, n.current_map, n.pos, msg);

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
    broadcast_to_visible(players_, ws_server_, n.current_map, n.pos, msg);

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
    broadcast_to_visible(players_, ws_server_, n.current_map, n.pos, msg);

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
    broadcast_to_visible(players_, ws_server_, picker_player->current_map, pos, msg);

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

    // Build display name with upgrade suffix and get sprite data from template
    std::string display_name = itm->name;
    int16_t ground_sprite = 0;
    int16_t ground_sprite_frame = 0;
    int8_t item_color = 0;
    if (item_registry_)
    {
        if (auto* tmpl = item_registry_->get(itm->template_id))
        {
            display_name = network::get_display_name(tmpl->name, itm->attribute);
            ground_sprite = tmpl->sprite;
            ground_sprite_frame = tmpl->sprite_frame;
            item_color = tmpl->item_color;
        }
    }

    network::ground_item_spawn_data data{.item_id = item.value,
                                         .template_id = itm->template_id.value,
                                         .item_name = std::move(display_name),
                                         .count = itm->count,
                                         .x = pos.x,
                                         .y = pos.y,
                                         .ground_sprite = ground_sprite,
                                         .ground_sprite_frame = ground_sprite_frame,
                                         .item_color = item_color,
                                         .attribute = itm->attribute,
                                         .reason = "drop"};

    auto msg = network::make_ground_item_spawn(data);
    broadcast_to_visible(players_, ws_server_, map, pos, msg);

    // Also send v2 ground_item_spawn with full serialized item data
    std::string map_name;
    if (world_)
    {
        if (auto* map_ptr = world_->get_map(map))
        {
            map_name = map_ptr->name();
        }
    }

    auto v2_msg = network::make_ground_item_spawn_v2(*itm, map_name, pos.x, pos.y);
    for_each_visible_connection(
        players_, ws_server_, map, pos,
        [&v2_msg](player_id, player::player&, network::ws_connection& c)
        {
            c.send_raw(v2_msg.dump());
        });

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
    // killer is an ECS entity — resolve to player_id first (inventory buckets and
    // connections are keyed by player_id, not by ecs entity id)
    std::optional<player_id> killer_pid;
    if (players_)
        killer_pid = players_->get_player_id_by_entity(killer);
    if (drop.gold > 0 && inventory_ && killer_pid)
    {
        auto killer_entity = entity_id{killer_pid->value};
        inventory_->add_gold(killer_entity, drop.gold);

        // Send gold_update to killer
        if (ws_server_)
        {
            if (auto* killer_conn = ws_server_->get_connection_by_player(*killer_pid))
            {
                killer_conn->send(network::make_gold_update({
                    .gold = static_cast<int64_t>(inventory_->get_gold(killer_entity)),
                    .change = static_cast<int64_t>(drop.gold),
                    .reason = "npc_loot"}));
            }
        }

        // Audit gold loot
        if (audit_)
        {
            if (auto* plr = players_ ? players_->get_player(*killer_pid) : nullptr)
            {
                std::string map_str;
                if (auto* map = world_->get_map(npc_map))
                    map_str = map->name();
                audit_->log_gold(static_cast<int32_t>(plr->character_id.value),
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

    // Place item drops on ground via item_ops
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

        // Use item_ops to clear owner and place on ground
        item_ops::drop_loot(dropped_item_id, npc_map, npc_pos.x, npc_pos.y, item_, world_);

        // Broadcast v1 + v2 ground item spawn to visible players
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

    if (drop.items.empty())
        return;

    // Create all item instances first
    std::vector<item_id> created_ids;
    created_ids.reserve(drop.items.size());

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

        created_ids.push_back(dropped_item_id);
    }

    if (created_ids.empty())
        return;

    // Use drop_loot_multi for boss multi-drops (spreads items in 3x3 grid)
    // For single items this still works fine (just places at center)
    auto results = item_ops::drop_loot_multi(
        created_ids, npc_map, npc_pos.x, npc_pos.y, item_, world_);

    // Broadcast each dropped item to visible players
    for (const auto& result : results)
    {
        world::position drop_pos{result.ground_x, result.ground_y};
        broadcast_ground_item_spawn(npc_map, drop_pos, result.dropped);

        LOG_DEBUG(bridge,
                  "NPC '{}' despawn dropped item {} at ({}, {})",
                  npc_name,
                  result.dropped.value,
                  result.ground_x,
                  result.ground_y);
    }

    LOG_DEBUG(bridge, "NPC '{}' corpse despawned with {} item drops", npc_name, results.size());
}

} // namespace hb::bridge
