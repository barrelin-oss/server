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
#include "item/item_ops.h"
#include "item/item_serialization.h"
#include "crafting/manufacturing_system.h"
#include "crafting/alchemy_system.h"
#include "audit/item_audit_system.h"
#include "core/subsystem.h"
#include "core/logger.h"
#include "perf/perf_stats.h"

namespace hb::bridge
{

namespace
{
    // Compute total inventory weight and send update to client
    void send_weight_update(
        inventory::inventory_system* inv_sys,
        player::player_system* players,
        item_registry* item_reg,
        entity_id entity,
        player_id pid,
        network::websocket_server* ws)
    {
        if (!inv_sys || !players || !item_reg)
            return;

        auto* inv = inv_sys->get_inventory(entity);
        auto* player = players->get_player(pid);
        if (!inv || !player)
            return;

        int32_t total = 0;
        for (const auto& entry : inv->items())
        {
            auto* tmpl = item_reg->get(entry.item);
            if (tmpl)
                total += tmpl->weight * entry.count;
        }

        auto str = player->base.strength;
        auto lvl = player->experience.level;
        auto max_w = inventory::inventory::max_weight(str, lvl);
        inv_sys->set_weight(entity, total, max_w);

        if (auto* conn = ws->get_connection_by_player(pid))
            conn->send(network::make_inventory_weight_update(total, max_w));
    }

    // Find the nearest friendly shop NPC within 3 tiles of the player.
    // Used by v2 shop handlers which don't send an npc_entity_id.
    struct shop_npc_lookup
    {
        npc::npc* target{nullptr};
        const npc::shop_config* shop{nullptr};
    };

    auto find_nearest_shop_npc(
        player::player* plr,
        npc::npc_system* npc_sys,
        shop_registry* shop_reg) -> shop_npc_lookup
    {
        if (!plr || !npc_sys || !shop_reg)
            return {};

        auto nearby = npc_sys->get_npcs_in_range(plr->current_map, plr->pos, 3);
        for (const auto& ent : nearby)
        {
            auto* npc_ptr = npc_sys->get_npc(ent);
            if (!npc_ptr || !npc_ptr->is_alive() || !npc_ptr->is_friendly())
                continue;

            auto* shop = shop_reg->get_shop(npc_ptr->name);
            if (shop)
            {
                return shop_npc_lookup{.target = npc_ptr, .shop = shop};
            }
        }
        return {};
    }
    // Find the nearest friendly bank NPC within 3 tiles of the player.
    // Used by v2 bank handlers which don't send an npc_entity_id.
    auto find_nearest_bank_npc(
        player::player* plr,
        npc::npc_system* npc_sys) -> npc::npc*
    {
        if (!plr || !npc_sys)
            return nullptr;

        auto nearby = npc_sys->get_npcs_in_range(plr->current_map, plr->pos, 3);
        for (const auto& ent : nearby)
        {
            auto* npc_ptr = npc_sys->get_npc(ent);
            if (!npc_ptr || !npc_ptr->is_alive() || !npc_ptr->is_friendly())
                continue;

            if (npc_ptr->category == npc::npc_category::banker ||
                npc_ptr->category == npc::npc_category::warehouse)
            {
                return npc_ptr;
            }
        }
        return nullptr;
    }
} // namespace

void game_handlers::handle_player_pickup(connection_id conn_id, const network::json_message& msg)
{
    auto* conn = require_in_game(conn_id, msg.seq);
    if (!conn)
        return;

    if (!players_ || !world_ || !inventory_ || !item_)
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

    if (player->is_dead())
    {
        send_error(conn_id, msg.seq, "dead", "Cannot pick up items while dead");
        return;
    }

    LOG_DEBUG(bridge, "Pickup request from player {} (pid={})", player->name, pid.value);

    // Resolve pickup position: v2 uses map/x/y from message, v1 uses player position
    auto pickup_map = player->current_map;
    auto pickup_pos = player->pos;

    if (msg.type == network::json_message_type::pickup_request)
    {
        auto data_result = network::pickup_request_data::from_json(msg.data);
        if (data_result.is_err())
        {
            conn->send_raw(network::make_pickup_result(false).dump());
            return;
        }
        auto& data = data_result.value();
        // v2 sends map name — resolve to map_id
        auto* map_ptr = world_->get_map_by_name(data.map);
        if (map_ptr)
        {
            pickup_map = map_ptr->id();
            pickup_pos = world::position{static_cast<int16_t>(data.x), static_cast<int16_t>(data.y)};
        }
    }
    else
    {
        // v1: parse but we only use player position
        auto data_result = network::player_pickup_request_data::from_json(msg.data);
        if (data_result.is_err())
        {
            send_error(conn_id, msg.seq, "invalid_request", data_result.error());
            return;
        }
    }

    LOG_DEBUG(bridge, "Pickup checking map={} pos=({},{})", pickup_map.value, pickup_pos.x, pickup_pos.y);

    auto owner_eid = entity_id(player->id.value);

    // Call item_ops
    auto result = item_ops::pickup_item(owner_eid, pickup_map, pickup_pos, item_, inventory_, world_);

    if (result.is_err())
    {
        using item_ops::pickup_failure;
        switch (result.error())
        {
        case pickup_failure::no_items:
            // Silent — nothing to pick up
            return;
        case pickup_failure::inventory_full:
            conn->send(network::make_chat_message_broadcast({
                .channel = "system",
                .sender_id = 0,
                .sender_name = "",
                .content = "Inventory full",
                .flags = {"system"},
            }));
            if (msg.type == network::json_message_type::pickup_request)
                conn->send_raw(network::make_pickup_result(false).dump());
            else
                send_error(conn_id, msg.seq, "pickup_failed", "Inventory full");
            return;
        case pickup_failure::too_heavy:
            conn->send(network::make_chat_message_broadcast({
                .channel = "system",
                .sender_id = 0,
                .sender_name = "",
                .content = "Too heavy to carry",
                .flags = {"system"},
            }));
            if (msg.type == network::json_message_type::pickup_request)
                conn->send_raw(network::make_pickup_result(false).dump());
            else
                send_error(conn_id, msg.seq, "pickup_failed", "Too heavy to carry");
            return;
        case pickup_failure::item_unavailable:
        case pickup_failure::add_failed:
        case pickup_failure::subsystem_error:
            if (msg.type == network::json_message_type::pickup_request)
                conn->send_raw(network::make_pickup_result(false).dump());
            else
                send_error(conn_id, msg.seq, "pickup_failed", "Pickup failed");
            return;
        }
        return; // unreachable, but satisfies compiler
    }

    auto& pick = result.value();
    auto picked_item_id = pick.picked_up;
    auto* itm = item_->get_item(picked_item_id);
    std::string item_name = itm ? itm->name : "Unknown";

    // Send pickup ack
    if (msg.type == network::json_message_type::pickup_request)
    {
        conn->send_raw(network::make_pickup_result(true).dump());
    }
    else
    {
        conn->send(network::make_player_pickup_response(msg.seq, true, nullptr, std::nullopt));
    }

    // Send inventory item add (v2)
    if (itm)
    {
        conn->send_raw(
            network::make_inventory_item_add(*itm, pick.pos_x, pick.pos_y, pick.z_order).dump());
    }

    // Broadcast pickup animation to nearby players (always — the action was confirmed)
    broadcast_player_action(
        *player,
        {.entity_id = player->ecs_entity.id, .action = "pickup", .direction = static_cast<int16_t>(player->facing)});

    if (itm)
    {
        // Send weight update (v2)
        conn->send_raw(
            network::make_inventory_weight_update_v2(pick.new_weight, pick.max_weight).dump());

        // Broadcast item removal (v2) to visible players
        std::string map_name;
        if (auto* map_ptr = world_->get_map(pickup_map))
        {
            map_name = map_ptr->name();
        }
        auto remove_msg = network::make_ground_item_removed_v2(picked_item_id, map_name, pickup_pos.x, pickup_pos.y);
        for_each_visible_connection(
            players_, ws_server_, pickup_map, pickup_pos,
            [&remove_msg](player_id, player::player&, network::ws_connection& c)
            {
                c.send_raw(remove_msg.dump());
            });

        // If there's another item underneath, broadcast the new top item to nearby players
        if (world_->has_ground_items(pickup_map, pickup_pos))
        {
            auto remaining = world_->get_ground_items(pickup_map, pickup_pos);
            if (!remaining.empty() && item_)
            {
                auto next_id = remaining.back();
                auto* next_itm = item_->get_item(next_id);
                if (next_itm)
                {
                    auto spawn_msg = network::make_ground_item_spawn_v2(
                        *next_itm, map_name, pickup_pos.x, pickup_pos.y);
                    for_each_visible_connection(
                        players_, ws_server_, pickup_map, pickup_pos,
                        [&spawn_msg](player_id, player::player&, network::ws_connection& c)
                        {
                            c.send_raw(spawn_msg.dump());
                        });
                }
            }
        }

        // Audit item pickup
        if (audit_ && itm->audited)
        {
            audit_->log_item(static_cast<int32_t>(player->character_id.value),
                             itm->name,
                             static_cast<int32_t>(picked_item_id.value),
                             item_log_type::get,
                             itm->count,
                             0,
                             map_name,
                             pickup_pos.x,
                             pickup_pos.y);
        }

        LOG_INFO(bridge,
                 "Player {} picked up item {} ({}) at ({}, {})",
                 pid.value,
                 picked_item_id.value,
                 item_name,
                 pickup_pos.x,
                 pickup_pos.y);
    }
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
        auto owner_eid = entity_id(player->id.value);

        // Build paginated bank data using serialize_item
        std::vector<std::vector<nlohmann::json>> pages;
        if (inventory_)
        {
            auto* bank = inventory_->get_bank(owner_eid);
            if (bank)
            {
                for (int16_t p = 0; p < bank->total_pages(); ++p)
                {
                    std::vector<nlohmann::json> slots;
                    for (int16_t s = 0; s < bank->slots_per_page(); ++s)
                    {
                        auto slot_item = bank->get_slot(p, s);
                        if (slot_item.has_value() && item_)
                        {
                            auto* itm = item_->get_item(*slot_item);
                            if (itm)
                                slots.push_back(item::serialize_item(*itm));
                            else
                                slots.push_back(nlohmann::json{});
                        }
                        else
                        {
                            slots.push_back(nlohmann::json{});
                        }
                    }
                    pages.push_back(std::move(slots));
                }

                // Send v2 bank_open message
                auto bank_open_msg = network::make_bank_open_v2(pages, bank->total_pages());
                conn->send_raw(bank_open_msg.dump());
            }
        }

        // Also send v1 response for backward compatibility
        nlohmann::json bank_data;
        bank_data["npc_name"] = target->name;

        auto items_array = nlohmann::json::array();
        if (inventory_)
        {
            auto* bank = inventory_->get_bank(owner_eid);
            if (bank)
            {
                for (int16_t p = 0; p < bank->total_pages(); ++p)
                {
                    for (int16_t s = 0; s < bank->slots_per_page(); ++s)
                    {
                        auto slot_item = bank->get_slot(p, s);
                        if (slot_item)
                        {
                            nlohmann::json slot_json;
                            slot_json["page"] = p;
                            slot_json["slot"] = s;
                            slot_json["item_id"] = slot_item->value;
                            if (item_)
                            {
                                auto* itm = item_->get_item(*slot_item);
                                if (itm)
                                {
                                    slot_json["name"] = itm->name;
                                    slot_json["count"] = itm->count;
                                    slot_json["durability"] = itm->durability;
                                    slot_json["max_durability"] = itm->max_durability;
                                }
                            }
                            items_array.push_back(std::move(slot_json));
                        }
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

    // create_from_template leaves owner unset (entity_id{}), and item_ops::equip_item
    // rejects any item whose owner != player with "Item not owned by player". Without
    // this, every weapon bought through the v1 shop path was impossible to equip.
    // The v2 path gets this for free via item_ops::shop_buy -> set_owner.
    item_->set_owner(new_item_id, owner_id);

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

        // Send inventory item update for the new item
        auto* bought_inv = inventory_->get_inventory(owner_id);
        if (bought_inv)
        {
            auto* bought_entry = bought_inv->get_item(new_item_id);
            if (bought_entry)
            {
                auto item_msg = network::build_inventory_item_msg(new_item_id, item_, item_registry_, bought_entry);
                if (item_msg)
                    conn->send(network::make_inventory_item_update(*item_msg));
            }
        }

        // Send gold_update
        conn->send(network::make_gold_update({
            .gold = static_cast<int64_t>(inventory_->get_gold(owner_id)),
            .change = static_cast<int64_t>(-total_price),
            .reason = "shop_buy"}));

        // Update weight
        send_weight_update(inventory_, players_, item_registry_, owner_id, check.plr->id, ws_server_);
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

    auto* entry = inv->get_item(item_id{data.item_id});
    if (!entry)
    {
        send_error(conn_id, msg.seq, "item_not_found", "No item with that ID in inventory");
        return;
    }

    auto* itm = item_->get_item(entry->item);
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

    auto* entry = inv->get_item(item_id{data.item_id});
    if (!entry)
    {
        send_error(conn_id, msg.seq, "item_not_found", "No item with that ID in inventory");
        return;
    }

    auto sell_item_id = entry->item;
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
    inv->remove_item(sell_item_id);
    item_->destroy_item(sell_item_id);

    // Add gold
    inventory_->add_gold(owner_id, sell_price);

    auto* conn = ws_server_->get_connection(conn_id);
    if (conn)
    {
        conn->send(network::make_shop_sell_confirm_response(msg.seq, true, sell_price, inventory_->get_gold(owner_id)));

        // Send inventory item removed
        conn->send(network::make_inventory_item_removed(sell_item_id.value));

        // Send gold_update
        conn->send(network::make_gold_update({
            .gold = static_cast<int64_t>(inventory_->get_gold(owner_id)),
            .change = static_cast<int64_t>(sell_price),
            .reason = "shop_sell"}));

        // Update weight
        send_weight_update(inventory_, players_, item_registry_, owner_id, check.plr->id, ws_server_);
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

    auto* entry = inv->get_item(item_id{data.item_id});
    if (!entry)
    {
        send_error(conn_id, msg.seq, "item_not_found", "No item with that ID in inventory");
        return;
    }

    auto repair_item_id = entry->item;
    auto* itm = item_->get_item(repair_item_id);
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

    auto* entry = inv->get_item(item_id{data.item_id});
    if (!entry)
    {
        send_error(conn_id, msg.seq, "item_not_found", "No item with that ID in inventory");
        return;
    }

    auto repair_item_id = entry->item;
    auto* itm = item_->get_item(repair_item_id);
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
    item_->repair_item_full(repair_item_id);
    inventory_->remove_gold(owner_id, repair_cost);

    // Re-fetch for updated durability
    itm = item_->get_item(repair_item_id);
    int16_t new_dur = itm ? itm->durability : 0;

    auto* conn = ws_server_->get_connection(conn_id);
    if (conn)
    {
        conn->send(network::make_shop_repair_confirm_response(
            msg.seq, true, new_dur, repair_cost, inventory_->get_gold(owner_id)));

        // Send inventory item update (item has new durability)
        auto repair_item_msg = network::build_inventory_item_msg(repair_item_id, item_, item_registry_, entry);
        if (repair_item_msg)
            conn->send(network::make_inventory_item_update(*repair_item_msg));

        // Send gold_update
        conn->send(network::make_gold_update({
            .gold = static_cast<int64_t>(inventory_->get_gold(owner_id)),
            .change = static_cast<int64_t>(-repair_cost),
            .reason = "shop_repair"}));
    }

    // Audit repair gold spend
    if (audit_)
    {
        if (itm && itm->audited)
        {
            audit_->log_item(static_cast<int32_t>(check.plr->character_id.value), itm->name, static_cast<int32_t>(repair_item_id.value), item_log_type::repair, 1);
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

// ========== Shop v2 handlers (single-step via item_ops) ==========

void game_handlers::handle_shop_buy_v2(connection_id conn_id, const network::json_message& msg)
{
    auto* conn = require_in_game(conn_id, msg.seq);
    if (!conn)
        return;

    auto data_result = network::shop_buy_request_data_v2::from_json(msg.data);
    if (data_result.is_err())
    {
        conn->send_raw(network::make_shop_buy_result(false).dump());
        return;
    }
    auto& data = data_result.value();

    if (!players_ || !npc_ || !shop_registry_ || !inventory_ || !item_)
    {
        conn->send_raw(network::make_shop_buy_result(false).dump());
        return;
    }

    auto pid = conn->player();
    auto* plr = players_->get_player(pid);
    if (!plr || plr->is_dead())
    {
        conn->send_raw(network::make_shop_buy_result(false).dump());
        return;
    }

    // Find nearest shop NPC
    auto lookup = find_nearest_shop_npc(plr, npc_, shop_registry_);
    if (!lookup.shop)
    {
        conn->send_raw(network::make_shop_buy_result(false).dump());
        return;
    }

    // Territory check
    if (world_)
    {
        auto* map = world_->get_map(plr->current_map);
        if (map && !npc::can_buy_in_territory(plr->faction, map->location_name()))
        {
            conn->send_raw(network::make_shop_buy_result(false).dump());
            return;
        }
    }

    // Verify the item is in the shop
    auto template_id = item_id{static_cast<uint32_t>(data.template_id)};
    bool item_in_shop = false;
    for (const auto& entry : lookup.shop->items)
    {
        if (entry.item == template_id)
        {
            item_in_shop = true;
            break;
        }
    }
    if (!item_in_shop)
    {
        conn->send_raw(network::make_shop_buy_result(false).dump());
        return;
    }

    // Look up item template for price
    auto* item_reg = subsystems().get<item_registry>();
    if (!item_reg)
    {
        conn->send_raw(network::make_shop_buy_result(false).dump());
        return;
    }

    auto* tmpl = item_reg->get(template_id);
    if (!tmpl)
    {
        conn->send_raw(network::make_shop_buy_result(false).dump());
        return;
    }

    int32_t buy_price = npc::calculate_buy_price(tmpl->price, 1, plr->base.charisma);
    auto owner_eid = entity_id(plr->id.value);

    // Call item_ops
    auto result = item_ops::shop_buy(owner_eid, template_id, buy_price, item_, inventory_);

    if (!result.success)
    {
        conn->send_raw(network::make_shop_buy_result(false).dump());
        return;
    }

    // Send success ack
    conn->send_raw(network::make_shop_buy_result(true).dump());

    // Send inventory item add
    auto* itm = item_->get_item(result.created);
    if (itm)
    {
        conn->send_raw(
            network::make_inventory_item_add(*itm, result.pos_x, result.pos_y, result.z_order).dump());
    }

    // Send gold update
    conn->send_raw(network::make_inventory_gold_update(result.new_gold).dump());

    // Send weight update
    conn->send_raw(
        network::make_inventory_weight_update_v2(result.new_weight, result.max_weight).dump());

    // Audit shop buy
    if (audit_)
    {
        if (itm && itm->audited)
        {
            audit_->log_item(
                static_cast<int32_t>(plr->character_id.value),
                tmpl->name,
                static_cast<int32_t>(result.created.value),
                item_log_type::buy,
                1);
        }
        audit_->log_gold(
            static_cast<int32_t>(plr->character_id.value),
            item_log_type::gold_shop_spend,
            -buy_price,
            0,
            {},
            0,
            0,
            {{"item", tmpl->name}, {"count", 1}});
    }

    LOG_DEBUG(bridge, "Player {} bought '{}' for {} gold (v2)", plr->id.value, tmpl->name, buy_price);
}

void game_handlers::handle_shop_sell_v2(connection_id conn_id, const network::json_message& msg)
{
    auto* conn = require_in_game(conn_id, msg.seq);
    if (!conn)
        return;

    auto data_result = network::shop_sell_request_data_v2::from_json(msg.data);
    if (data_result.is_err())
    {
        conn->send_raw(network::make_shop_sell_result(false).dump());
        return;
    }
    auto& data = data_result.value();

    if (!players_ || !npc_ || !shop_registry_ || !inventory_ || !item_)
    {
        conn->send_raw(network::make_shop_sell_result(false).dump());
        return;
    }

    auto pid = conn->player();
    auto* plr = players_->get_player(pid);
    if (!plr || plr->is_dead())
    {
        conn->send_raw(network::make_shop_sell_result(false).dump());
        return;
    }

    // Find nearest shop NPC
    auto lookup = find_nearest_shop_npc(plr, npc_, shop_registry_);
    if (!lookup.shop)
    {
        conn->send_raw(network::make_shop_sell_result(false).dump());
        return;
    }

    auto sell_item_id = item_id{static_cast<uint32_t>(data.item_id)};

    // Check the shop accepts this item category
    auto* item_reg = subsystems().get<item_registry>();
    if (item_reg)
    {
        auto* itm = item_->get_item(sell_item_id);
        if (itm)
        {
            auto* tmpl = item_reg->get(itm->template_id);
            if (tmpl && !npc::is_category_accepted(*lookup.shop, static_cast<uint8_t>(tmpl->category)))
            {
                conn->send_raw(network::make_shop_sell_result(false).dump());
                return;
            }
        }
    }

    // Save audit data before item_ops destroys the item
    auto* pre_itm = item_->get_item(sell_item_id);
    std::string sold_item_name = pre_itm ? pre_itm->name : "Unknown";
    int16_t sold_count = pre_itm ? pre_itm->count : 1;
    bool sold_audited = pre_itm ? pre_itm->audited : false;
    int32_t sold_price = pre_itm ? pre_itm->price : 0;

    auto owner_eid = entity_id(plr->id.value);

    // Call item_ops
    auto result = item_ops::shop_sell(owner_eid, sell_item_id, item_, inventory_, &plr->equipment);

    if (!result.success)
    {
        conn->send_raw(network::make_shop_sell_result(false).dump());
        return;
    }

    // Send success ack
    conn->send_raw(network::make_shop_sell_result(true).dump());

    // Send inventory item removed
    conn->send_raw(network::make_inventory_item_removed_v2(sell_item_id).dump());

    // Send gold update
    conn->send_raw(network::make_inventory_gold_update(result.new_gold).dump());

    // Send weight update
    conn->send_raw(
        network::make_inventory_weight_update_v2(result.new_weight, result.max_weight).dump());

    // Audit shop sell
    if (audit_)
    {
        if (sold_audited)
        {
            audit_->log_item(
                static_cast<int32_t>(plr->character_id.value),
                sold_item_name,
                static_cast<int32_t>(sell_item_id.value),
                item_log_type::sell,
                sold_count);
        }
        // item_ops::shop_sell uses item price directly as sell_price
        audit_->log_gold(
            static_cast<int32_t>(plr->character_id.value),
            item_log_type::gold_shop_earn,
            sold_price,
            0,
            {},
            0,
            0,
            {{"item", sold_item_name}});
    }

    LOG_DEBUG(bridge, "Player {} sold item for gold (v2)", plr->id.value);
}

void game_handlers::handle_shop_repair_v2(connection_id conn_id, const network::json_message& msg)
{
    auto* conn = require_in_game(conn_id, msg.seq);
    if (!conn)
        return;

    auto data_result = network::shop_repair_request_data_v2::from_json(msg.data);
    if (data_result.is_err())
    {
        conn->send_raw(network::make_shop_repair_result(false).dump());
        return;
    }
    auto& data = data_result.value();

    if (!players_ || !npc_ || !shop_registry_ || !inventory_ || !item_)
    {
        conn->send_raw(network::make_shop_repair_result(false).dump());
        return;
    }

    auto pid = conn->player();
    auto* plr = players_->get_player(pid);
    if (!plr || plr->is_dead())
    {
        conn->send_raw(network::make_shop_repair_result(false).dump());
        return;
    }

    // Find nearest shop NPC
    auto lookup = find_nearest_shop_npc(plr, npc_, shop_registry_);
    if (!lookup.shop)
    {
        conn->send_raw(network::make_shop_repair_result(false).dump());
        return;
    }

    auto repair_item_id = item_id{static_cast<uint32_t>(data.item_id)};

    // Check the shop can repair this item category
    auto* item_reg = subsystems().get<item_registry>();
    if (item_reg)
    {
        auto* itm = item_->get_item(repair_item_id);
        if (itm)
        {
            auto* tmpl = item_reg->get(itm->template_id);
            if (tmpl && !npc::is_category_repairable(*lookup.shop, static_cast<uint8_t>(tmpl->category)))
            {
                conn->send_raw(network::make_shop_repair_result(false).dump());
                return;
            }
        }
    }

    auto owner_eid = entity_id(plr->id.value);

    // Capture gold before repair for audit cost calculation
    auto gold_before = inventory_->get_gold(owner_eid);

    // Call item_ops
    auto result = item_ops::shop_repair(owner_eid, repair_item_id, item_, inventory_);

    if (!result.success)
    {
        conn->send_raw(network::make_shop_repair_result(false).dump());
        return;
    }

    auto repair_cost = static_cast<int32_t>(gold_before - result.new_gold);

    // Send success ack
    conn->send_raw(network::make_shop_repair_result(true).dump());

    // Send full item update (durability changed)
    auto* itm = item_->get_item(repair_item_id);
    if (itm)
    {
        auto* inv = inventory_->get_inventory(owner_eid);
        if (inv)
        {
            auto* entry = inv->get_item(repair_item_id);
            if (entry)
            {
                conn->send_raw(
                    network::make_inventory_item_update_v2(
                        *itm, entry->pos_x, entry->pos_y, entry->z_order).dump());
            }
        }
    }

    // Send gold update
    conn->send_raw(network::make_inventory_gold_update(result.new_gold).dump());

    // Audit repair
    if (audit_)
    {
        if (itm && itm->audited)
        {
            audit_->log_item(
                static_cast<int32_t>(plr->character_id.value),
                itm->name,
                static_cast<int32_t>(repair_item_id.value),
                item_log_type::repair,
                1);
        }
        audit_->log_gold(
            static_cast<int32_t>(plr->character_id.value),
            item_log_type::gold_shop_spend,
            -repair_cost,
            0,
            {},
            0,
            0,
            {{"action", "repair"}, {"item", itm ? itm->name : "unknown"}});
    }

    LOG_DEBUG(bridge, "Player {} repaired item for {} gold (v2)", plr->id.value, repair_cost);
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

    auto* entry = inv->get_item(item_id{data.item_id});
    if (!entry)
    {
        send_error(conn_id, msg.seq, "item_not_found", "No item with that ID in inventory");
        return;
    }

    std::string item_name;
    auto deposit_item_id = entry->item;
    bool deposit_audited = false;
    int16_t deposit_count = 1;
    if (auto* itm = item_->get_item(deposit_item_id))
    {
        item_name = itm->name;
        deposit_audited = itm->audited;
        deposit_count = itm->count;
    }

    auto result = inventory_->deposit_item(owner_id, deposit_item_id);
    if (result != inventory::inventory_result::success)
    {
        send_error(conn_id, msg.seq, "deposit_failed", "Failed to deposit item");
        return;
    }

    auto* conn = ws_server_->get_connection(conn_id);
    if (conn)
    {
        conn->send(network::make_bank_deposit_response(msg.seq, true, item_name));

        // Send inventory item removed (item left inventory)
        conn->send(network::make_inventory_item_removed(deposit_item_id.value));

        // Send bank_slot_update (item now in bank)
        auto* bank_after = inventory_->get_bank(owner_id);
        if (bank_after)
        {
            if (auto bank_loc = bank_after->find_item(deposit_item_id); bank_loc)
            {
                auto bank_msg = network::build_inventory_item_msg(deposit_item_id, item_, item_registry_);
                if (bank_msg)
                    conn->send(network::make_bank_slot_update(bank_loc->page, bank_loc->slot, &*bank_msg));
            }
        }
    }

    // Update weight (item left inventory)
    send_weight_update(inventory_, players_, item_registry_, owner_id, check.plr->id, ws_server_);

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

    auto slot_item = bank->get_slot(data.bank_page, data.bank_slot);
    if (!slot_item)
    {
        send_error(conn_id, msg.seq, "empty_slot", "No item in that bank slot");
        return;
    }

    std::string item_name;
    auto withdraw_item_id = *slot_item;
    bool withdraw_audited = false;
    int16_t withdraw_count = 1;
    if (auto* itm = item_->get_item(withdraw_item_id))
    {
        item_name = itm->name;
        withdraw_audited = itm->audited;
        withdraw_count = itm->count;
    }

    auto result = inventory_->withdraw_item(owner_id, data.bank_page, data.bank_slot);
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

        // Send bank_slot_update (bank slot cleared)
        conn->send(network::make_bank_slot_update(data.bank_page, data.bank_slot, nullptr));

        // Send inventory item update (item now in inventory)
        auto* inv_after = inventory_->get_inventory(owner_id);
        if (inv_after)
        {
            auto* inv_entry = inv_after->get_item(withdraw_item_id);
            if (inv_entry)
            {
                auto inv_msg = network::build_inventory_item_msg(withdraw_item_id, item_, item_registry_, inv_entry);
                if (inv_msg)
                    conn->send(network::make_inventory_item_update(*inv_msg));
            }
        }
    }

    // Update weight (item entered inventory)
    send_weight_update(inventory_, players_, item_registry_, owner_id, check.plr->id, ws_server_);

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

// ========== Inventory Reposition ==========

void game_handlers::handle_inventory_reposition(connection_id conn_id, const network::json_message& msg)
{
    auto* conn = require_in_game(conn_id, msg.seq);
    if (!conn)
        return;

    if (!inventory_)
        return;

    auto data_result = network::inventory_reposition_request_data::from_json(msg.data);
    if (data_result.is_err())
        return; // Silent fail — no response for reposition

    auto& data = data_result.value();
    auto pid = conn->player();
    auto entity = entity_id{pid.value};

    auto* inv = inventory_->get_inventory(entity);
    if (!inv)
        return;

    auto* entry = inv->get_item(item_id{data.item_id});
    if (!entry)
        return;

    // Update pixel position and z-order
    entry->pos_x = data.pos_x;
    entry->pos_y = data.pos_y;
    entry->z_order = inv->next_z_order();
}

// ========== Drop Item ==========

void game_handlers::handle_player_drop_item(connection_id conn_id, const network::json_message& msg)
{
    auto* conn = require_in_game(conn_id, msg.seq);
    if (!conn)
        return;

    if (!players_ || !inventory_ || !world_ || !item_)
    {
        send_error(conn_id, msg.seq, "internal_error", "Required subsystems unavailable");
        return;
    }

    // Parse item_id from v1 or v2 format
    item_id target_item_id{};
    if (msg.type == network::json_message_type::drop_request)
    {
        auto data_result = network::drop_request_data::from_json(msg.data);
        if (data_result.is_err())
        {
            conn->send_raw(network::make_drop_result(false).dump());
            return;
        }
        target_item_id = item_id{static_cast<uint32_t>(data_result.value().item_id)};
    }
    else
    {
        auto data_result = network::drop_item_request_data::from_json(msg.data);
        if (data_result.is_err())
        {
            send_error(conn_id, msg.seq, "invalid_request", data_result.error());
            return;
        }
        target_item_id = item_id{data_result.value().item_id};
    }

    auto pid = conn->player();
    auto* player = players_->get_player(pid);
    if (!player)
    {
        send_error(conn_id, msg.seq, "invalid_player", "Player not found");
        return;
    }

    if (player->is_dead())
    {
        if (msg.type == network::json_message_type::drop_request)
        {
            conn->send_raw(network::make_drop_result(false).dump());
        }
        else
        {
            send_error(conn_id, msg.seq, "dead", "Cannot drop items while dead");
        }
        return;
    }

    auto owner_eid = entity_id(player->id.value);

    // Capture item data before drop for audit logging
    auto* itm = item_->get_item(target_item_id);
    std::string item_name = itm ? itm->name : "Unknown";
    int16_t count = itm ? itm->count : 1;
    bool audited = itm ? itm->audited : false;

    // Call item_ops
    auto result = item_ops::drop_item(
        owner_eid, target_item_id, player->current_map, player->pos,
        item_, inventory_, world_, &player->equipment);

    if (!result.success)
    {
        if (msg.type == network::json_message_type::drop_request)
        {
            conn->send_raw(network::make_drop_result(false).dump());
        }
        else
        {
            send_error(conn_id, msg.seq, "drop_failed", result.error);
        }
        return;
    }

    // Send drop ack
    if (msg.type == network::json_message_type::drop_request)
    {
        conn->send_raw(network::make_drop_result(true).dump());
    }
    else
    {
        conn->send(network::make_player_drop_item_response(msg.seq, true));
    }

    // Send inventory item removed (v2)
    conn->send_raw(network::make_inventory_item_removed_v2(target_item_id).dump());

    // Send weight update (v2)
    auto new_weight = inventory_->get_current_weight(owner_eid);
    auto max_weight = inventory_->get_max_weight(owner_eid);
    conn->send_raw(network::make_inventory_weight_update_v2(new_weight, max_weight).dump());

    // If was equipped, send force_unequip + broadcast empty equipment slot
    if (result.was_equipped && result.unequipped_slot.has_value())
    {
        auto slot = *result.unequipped_slot;
        // Slot name lookup — same table as equipment handlers
        constexpr const char* slot_names[] = {
            "head", "body", "arms", "pants", "boots", "weapon", "shield",
            "twohand", "ring_left", "ring_right", "amulet", "cape", "angel", "fullbody"};
        auto slot_idx = static_cast<size_t>(slot);
        std::string_view slot_name = slot_idx < std::size(slot_names)
            ? slot_names[slot_idx] : "unknown";

        conn->send_raw(network::make_force_unequip(slot_name, "dropped").dump());

        // Recalculate stats after unequip
        players_->recalculate_equipment_modifiers(pid);
        players_->recalculate_appearance(pid);

        // Broadcast empty equipment slot to nearby
        broadcast_equipment_change(pid, slot, item_id{});
    }

    // Broadcast ground item spawn (v2) to visible players
    // Re-fetch item pointer (item_ops cleared owner but item still exists)
    itm = item_->get_item(target_item_id);
    std::string map_name;
    if (auto* map_ptr = world_->get_map(player->current_map))
    {
        map_name = map_ptr->name();
    }

    if (itm)
    {
        auto spawn_msg = network::make_ground_item_spawn_v2(
            *itm, map_name, player->pos.x, player->pos.y);
        for_each_visible_connection(
            players_, ws_server_, player->current_map, player->pos,
            [&spawn_msg](player_id, player::player&, network::ws_connection& c)
            {
                c.send_raw(spawn_msg.dump());
            });
    }

    // Audit item drop
    if (audit_ && audited)
    {
        audit_->log_item(static_cast<int32_t>(player->character_id.value),
                         item_name,
                         static_cast<int32_t>(target_item_id.value),
                         item_log_type::drop,
                         count,
                         0,
                         map_name,
                         player->pos.x,
                         player->pos.y);
    }

    LOG_INFO(bridge,
             "Player {} dropped item {} ({}) at ({}, {})",
             pid.value,
             target_item_id.value,
             item_name,
             player->pos.x,
             player->pos.y);
}

// ========== Bank v2 handlers (proximity-based via item_ops) ==========

void game_handlers::handle_bank_deposit_v2(connection_id conn_id, const network::json_message& msg)
{
    auto* conn = require_in_game(conn_id, msg.seq);
    if (!conn)
        return;

    auto data_result = network::bank_deposit_request_data_v2::from_json(msg.data);
    if (data_result.is_err())
    {
        conn->send_raw(network::make_bank_deposit_result(false).dump());
        return;
    }
    auto& data = data_result.value();

    if (!players_ || !npc_ || !inventory_ || !item_)
    {
        conn->send_raw(network::make_bank_deposit_result(false).dump());
        return;
    }

    auto pid = conn->player();
    auto* plr = players_->get_player(pid);
    if (!plr || plr->is_dead())
    {
        conn->send_raw(network::make_bank_deposit_result(false).dump());
        return;
    }

    // Check nearby bank NPC
    auto* bank_npc = find_nearest_bank_npc(plr, npc_);
    if (!bank_npc)
    {
        conn->send_raw(network::make_bank_deposit_result(false).dump());
        return;
    }

    auto deposit_item_id = item_id{static_cast<uint32_t>(data.item_id)};
    auto owner_eid = entity_id(plr->id.value);

    // Capture audit data before deposit
    auto* pre_itm = item_->get_item(deposit_item_id);
    std::string item_name = pre_itm ? pre_itm->name : "Unknown";
    int16_t deposit_count = pre_itm ? pre_itm->count : 1;
    bool deposit_audited = pre_itm ? pre_itm->audited : false;

    // Convert optional page/slot
    std::optional<int16_t> target_page;
    std::optional<int16_t> target_slot;
    if (data.page.has_value())
        target_page = static_cast<int16_t>(data.page.value());
    if (data.slot.has_value())
        target_slot = static_cast<int16_t>(data.slot.value());

    // Call item_ops
    auto result = item_ops::bank_deposit(
        owner_eid, deposit_item_id, target_page, target_slot,
        item_, inventory_, &plr->equipment);

    if (!result.success)
    {
        conn->send_raw(network::make_bank_deposit_result(false).dump());
        return;
    }

    // Send success ack
    conn->send_raw(network::make_bank_deposit_result(true).dump());

    // Send inventory item removed
    conn->send_raw(network::make_inventory_item_removed_v2(deposit_item_id).dump());

    // Send weight update
    conn->send_raw(
        network::make_inventory_weight_update_v2(result.new_weight, result.max_weight).dump());

    // Send bank slot update with the deposited item
    auto* itm = item_->get_item(deposit_item_id);
    if (itm)
    {
        conn->send_raw(
            network::make_bank_slot_update_v2(result.page, result.slot, *itm).dump());
    }

    // Audit bank deposit
    if (audit_ && deposit_audited)
    {
        audit_->log_item(
            static_cast<int32_t>(plr->character_id.value),
            item_name,
            static_cast<int32_t>(deposit_item_id.value),
            item_log_type::deposit,
            deposit_count);
    }

    LOG_DEBUG(bridge, "Player {} deposited '{}' to bank page {} slot {} (v2)",
              plr->id.value, item_name, result.page, result.slot);
}

void game_handlers::handle_bank_withdraw_v2(connection_id conn_id, const network::json_message& msg)
{
    auto* conn = require_in_game(conn_id, msg.seq);
    if (!conn)
        return;

    auto data_result = network::bank_withdraw_request_data_v2::from_json(msg.data);
    if (data_result.is_err())
    {
        conn->send_raw(network::make_bank_withdraw_result(false).dump());
        return;
    }
    auto& data = data_result.value();

    if (!players_ || !npc_ || !inventory_ || !item_)
    {
        conn->send_raw(network::make_bank_withdraw_result(false).dump());
        return;
    }

    auto pid = conn->player();
    auto* plr = players_->get_player(pid);
    if (!plr || plr->is_dead())
    {
        conn->send_raw(network::make_bank_withdraw_result(false).dump());
        return;
    }

    // Check nearby bank NPC
    auto* bank_npc = find_nearest_bank_npc(plr, npc_);
    if (!bank_npc)
    {
        conn->send_raw(network::make_bank_withdraw_result(false).dump());
        return;
    }

    auto owner_eid = entity_id(plr->id.value);
    auto page = static_cast<int16_t>(data.page);
    auto slot = static_cast<int16_t>(data.slot);

    // Capture item info before withdraw for audit
    std::string item_name;
    int16_t withdraw_count = 1;
    bool withdraw_audited = false;
    item_id withdrawn_id{};

    auto* bank = inventory_->get_bank(owner_eid);
    if (bank)
    {
        auto slot_item = bank->get_slot(page, slot);
        if (slot_item.has_value())
        {
            withdrawn_id = *slot_item;
            auto* itm = item_->get_item(withdrawn_id);
            if (itm)
            {
                item_name = itm->name;
                withdraw_count = itm->count;
                withdraw_audited = itm->audited;
            }
        }
    }

    // Call item_ops
    auto result = item_ops::bank_withdraw(owner_eid, page, slot, item_, inventory_);

    if (!result.success)
    {
        conn->send_raw(network::make_bank_withdraw_result(false).dump());
        return;
    }

    // Send success ack
    conn->send_raw(network::make_bank_withdraw_result(true).dump());

    // Send bank slot cleared
    conn->send_raw(network::make_bank_slot_cleared(page, slot).dump());

    // Send inventory item add
    auto* itm = item_->get_item(result.withdrawn);
    if (itm)
    {
        conn->send_raw(
            network::make_inventory_item_add(*itm, result.pos_x, result.pos_y, result.z_order).dump());
    }

    // Send weight update
    conn->send_raw(
        network::make_inventory_weight_update_v2(result.new_weight, result.max_weight).dump());

    // Audit bank withdraw
    if (audit_ && withdraw_audited)
    {
        audit_->log_item(
            static_cast<int32_t>(plr->character_id.value),
            item_name,
            static_cast<int32_t>(withdrawn_id.value),
            item_log_type::retrieve,
            withdraw_count);
    }

    LOG_DEBUG(bridge, "Player {} withdrew '{}' from bank page {} slot {} (v2)",
              plr->id.value, item_name, page, slot);
}

void game_handlers::handle_bank_reposition(connection_id conn_id, const network::json_message& msg)
{
    auto* conn = require_in_game(conn_id, msg.seq);
    if (!conn)
        return;

    auto data_result = network::bank_reposition_request_data::from_json(msg.data);
    if (data_result.is_err())
    {
        conn->send_raw(network::make_bank_reposition_result(false).dump());
        return;
    }
    auto& data = data_result.value();

    if (!players_ || !npc_ || !inventory_)
    {
        conn->send_raw(network::make_bank_reposition_result(false).dump());
        return;
    }

    auto pid = conn->player();
    auto* plr = players_->get_player(pid);
    if (!plr || plr->is_dead())
    {
        conn->send_raw(network::make_bank_reposition_result(false).dump());
        return;
    }

    // Check nearby bank NPC
    auto* bank_npc = find_nearest_bank_npc(plr, npc_);
    if (!bank_npc)
    {
        conn->send_raw(network::make_bank_reposition_result(false).dump());
        return;
    }

    auto owner_eid = entity_id(plr->id.value);
    auto from_page = static_cast<int16_t>(data.from_page);
    auto from_slot = static_cast<int16_t>(data.from_slot);
    auto to_page = static_cast<int16_t>(data.to_page);
    auto to_slot = static_cast<int16_t>(data.to_slot);

    // Check if destination was occupied before reposition (determines swap vs move)
    bool was_swap = false;
    auto* bank = inventory_->get_bank(owner_eid);
    if (bank)
    {
        auto dest_item = bank->get_slot(to_page, to_slot);
        was_swap = dest_item.has_value();
    }

    // Call item_ops
    auto result = item_ops::bank_reposition(
        owner_eid, from_page, from_slot, to_page, to_slot, inventory_);

    if (!result.success)
    {
        conn->send_raw(network::make_bank_reposition_result(false).dump());
        return;
    }

    // Send success ack
    conn->send_raw(network::make_bank_reposition_result(true).dump());

    // Send slot updates — re-read bank after reposition
    bank = inventory_->get_bank(owner_eid);
    if (bank && item_)
    {
        // Destination slot now has the moved item
        auto dest_item_id = bank->get_slot(to_page, to_slot);
        if (dest_item_id.has_value())
        {
            auto* dest_itm = item_->get_item(*dest_item_id);
            if (dest_itm)
            {
                conn->send_raw(
                    network::make_bank_slot_update_v2(to_page, to_slot, *dest_itm).dump());
            }
        }

        if (was_swap)
        {
            // Source slot now has the swapped item
            auto source_item_id = bank->get_slot(from_page, from_slot);
            if (source_item_id.has_value())
            {
                auto* source_itm = item_->get_item(*source_item_id);
                if (source_itm)
                {
                    conn->send_raw(
                        network::make_bank_slot_update_v2(from_page, from_slot, *source_itm).dump());
                }
            }
        }
        else
        {
            // Source slot is now empty
            conn->send_raw(network::make_bank_slot_cleared(from_page, from_slot).dump());
        }
    }

    LOG_DEBUG(bridge, "Player {} repositioned bank item ({},{}) -> ({},{}) {}",
              plr->id.value, from_page, from_slot, to_page, to_slot,
              was_swap ? "(swap)" : "(move)");
}

} // namespace hb::bridge
