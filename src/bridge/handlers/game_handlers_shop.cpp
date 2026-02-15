#include "platform/platform.h"
#include "bridge/handlers/game_handlers.h"
#include "bridge/handlers/broadcast_util.h"
#include "network/websocket_server.h"
#include "player/player_system.h"
#include "world/world_subsystem.h"
#include "npc/npc_system.h"
#include "npc/npc.h"
#include "npc/shop_pricing.h"
#include "registry/shop_registry.h"
#include "registry/dialog_registry.h"
#include "registry/item_registry.h"
#include "inventory/inventory_system.h"
#include "item/item_system.h"
#include "crafting/manufacturing_system.h"
#include "crafting/alchemy_system.h"
#include "audit/item_audit_system.h"
#include "core/subsystem.h"
#include "core/logger.h"
#include "perf/perf_stats.h"

namespace hb::bridge
{

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
                broadcast_to_visible(players_, ws_server_, player->current_map, player->pos, spawn_msg);
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
            audit_->log_item(static_cast<int32_t>(player->character_id.value),
                             itm->name,
                             static_cast<int32_t>(picked_item_id.value),
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
            audit_->log_item(static_cast<int32_t>(check.plr->character_id.value), tmpl->name, static_cast<int32_t>(new_item_id.value), item_log_type::buy, count);
        }
        audit_->log_gold(static_cast<int32_t>(check.plr->character_id.value),
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
                static_cast<int32_t>(check.plr->character_id.value), sold_item_name, static_cast<int32_t>(sell_item_id.value), item_log_type::sell, sold_count);
        }
        audit_->log_gold(static_cast<int32_t>(check.plr->character_id.value),
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
            audit_->log_item(static_cast<int32_t>(check.plr->character_id.value), itm->name, static_cast<int32_t>(slot->item.value), item_log_type::repair, 1);
        }
        audit_->log_gold(static_cast<int32_t>(check.plr->character_id.value),
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
            static_cast<int32_t>(check.plr->character_id.value), item_name, static_cast<int32_t>(deposit_item_id.value), item_log_type::deposit, deposit_count);
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
            static_cast<int32_t>(check.plr->character_id.value), item_name, static_cast<int32_t>(withdraw_item_id.value), item_log_type::retrieve, withdraw_count);
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

} // namespace hb::bridge
