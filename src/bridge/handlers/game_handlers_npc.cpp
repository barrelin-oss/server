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
                                             n, p.faction, p.pk.is_criminal(), p.pk.is_murderer()))};
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
    broadcast_to_visible(players_, ws_server_, n.current_map, n.pos, msg);

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
    broadcast_to_visible(players_, ws_server_, map, pos, msg);

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

} // namespace hb::bridge
