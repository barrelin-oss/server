#include "platform/platform.h"
#include "bridge/handlers/game_handlers.h"
#include "bridge/handlers/broadcast_util.h"
#include "network/websocket_server.h"
#include "player/player_system.h"
#include "player/equip_mapping.h"
#include "world/world_subsystem.h"
#include "inventory/inventory_system.h"
#include "item/item_system.h"
#include "item/item_effect.h"
#include "item/item_ops.h"
#include "item/item_serialization.h"
#include "item/item_upgrade.h"
#include "item/special_ability.h"
#include "registry/item_registry.h"
#include "effect/effect_system.h"
#include "combat/combat_system.h"
#include "audit/item_audit_system.h"
#include "core/subsystem.h"
#include "core/logger.h"
#include "perf/perf_stats.h"

#include <random>

namespace hb::bridge
{

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

// Lowercase slot names matching the v2 protocol.
// Indexed by equip_slot enum value.
constexpr const char* slot_names[] = {
    "head", "body", "arms", "pants", "boots", "weapon", "shield",
    "twohand", "ring_left", "ring_right", "amulet", "cape", "angel", "fullbody"};

auto slot_to_name(player::equip_slot slot) -> std::string_view
{
    auto idx = static_cast<size_t>(slot);
    if (idx < std::size(slot_names))
    {
        return slot_names[idx];
    }
    return "unknown";
}

auto parse_equip_slot(std::string_view name) -> std::optional<player::equip_slot>
{
    for (size_t i = 0; i < std::size(slot_names); ++i)
    {
        if (name == slot_names[i])
        {
            return static_cast<player::equip_slot>(i);
        }
    }
    return std::nullopt;
}

// Send a v2 inventory_item_update for the given item to the connection.
void send_item_update_v2(network::ws_connection* conn,
                         item_id iid,
                         item::item_system* items,
                         inventory::inventory_system* inv,
                         entity_id owner)
{
    if (!conn || !items || !inv)
        return;

    auto* itm = items->get_item(iid);
    if (!itm)
        return;

    auto* inv_data = inv->get_inventory(owner);
    if (!inv_data)
        return;

    auto* entry = inv_data->get_item(iid);
    if (!entry)
        return;

    auto msg = network::make_inventory_item_update_v2(*itm, entry->pos_x, entry->pos_y, entry->z_order);
    conn->send_raw(msg.dump());
}

} // namespace

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

    // Parse request — support both v1 (player_equip_request) and v2 (equip_request) formats
    item_id target_item_id{};
    player::equip_slot target_slot{};
    std::string slot_name;

    if (msg.type == network::json_message_type::equip_request)
    {
        // v2 format: item_id + string slot name
        auto data_result = network::equip_request_data::from_json(msg.data);
        if (data_result.is_err())
        {
            send_error(conn_id, msg.seq, "invalid_request", data_result.error());
            return;
        }
        auto& data = data_result.value();
        target_item_id = item_id{static_cast<uint32_t>(data.item_id)};
        slot_name = data.slot;
        auto slot_opt = parse_equip_slot(data.slot);
        if (!slot_opt)
        {
            conn->send_raw(network::make_equip_result(false, data.slot).dump());
            return;
        }
        target_slot = *slot_opt;
    }
    else
    {
        // v1 format: item_id + numeric target_slot
        auto data_result = network::player_equip_request_data::from_json(msg.data);
        if (data_result.is_err())
        {
            send_error(conn_id, msg.seq, "invalid_request", data_result.error());
            return;
        }
        auto& data = data_result.value();
        target_item_id = item_id{data.item_id};
        target_slot = static_cast<player::equip_slot>(data.target_slot);
        slot_name = std::string(slot_to_name(target_slot));
    }

    auto pid = conn->player();
    auto* plr = players_->get_player(pid);
    if (!plr)
    {
        send_error(conn_id, msg.seq, "invalid_player", "Player not found");
        return;
    }

    auto owner_eid = entity_id(plr->id.value);

    // Check alive
    if (plr->is_dead())
    {
        LOG_WARN(bridge, "Player {} equip rejected for item {}: player is dead", pid.value, target_item_id.value);
        conn->send_raw(network::make_equip_result(false, slot_name).dump());
        return;
    }

    // Check not trading
    auto trade_partner = inventory_->get_trade_partner(owner_eid);
    if (trade_partner.is_valid())
    {
        LOG_WARN(bridge, "Player {} equip rejected for item {}: player is trading", pid.value, target_item_id.value);
        conn->send_raw(network::make_equip_result(false, slot_name).dump());
        return;
    }

    // Get the item
    auto* itm = item_->get_item(target_item_id);
    if (!itm)
    {
        LOG_WARN(bridge, "Player {} equip rejected for item {}: item not found in item_system", pid.value, target_item_id.value);
        conn->send_raw(network::make_equip_result(false, slot_name).dump());
        return;
    }

    // Validate item is equippable
    if (!itm->is_equipment() || itm->equip_position == item::equip_pos::none)
    {
        LOG_WARN(bridge, "Player {} equip rejected for item {}: item is not equipment", pid.value, target_item_id.value);
        conn->send_raw(network::make_equip_result(false, slot_name).dump());
        return;
    }

    // Validate target slot is compatible with item
    if (!player::is_valid_slot_for_item(itm->equip_position, target_slot))
    {
        LOG_WARN(bridge, "Player {} equip rejected for item {}: slot does not match item equip_pos", pid.value, target_item_id.value);
        conn->send_raw(network::make_equip_result(false, slot_name).dump());
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
        LOG_WARN(bridge, "Player {} equip rejected for item {}: stat/level requirements not met", pid.value, target_item_id.value);
        conn->send_raw(network::make_equip_result(false, slot_name).dump());
        return;
    }

    // Two-handed weapon logic: if equipping a 2H weapon and shield is occupied, auto-unequip shield
    if (itm->two_handed && plr->equipment.has_equipped(player::equip_slot::shield))
    {
        auto shield_result = item_ops::unequip_item(owner_eid, player::equip_slot::shield, &plr->equipment);
        if (shield_result.success)
        {
            players_->recalculate_equipment_modifiers(pid);
            players_->recalculate_appearance(pid);

            // Send inventory update for the shield (no longer equipped)
            send_item_update_v2(conn, shield_result.unequipped, item_, inventory_, owner_eid);

            // Broadcast empty shield slot to nearby
            broadcast_equipment_change(pid, player::equip_slot::shield, item_id{});
        }
    }

    // Shield equip + 2H weapon currently equipped: block it
    if (target_slot == player::equip_slot::shield && plr->equipment.has_equipped(player::equip_slot::weapon))
    {
        auto weapon_id = plr->equipment.get_equipped(player::equip_slot::weapon);
        auto* weapon_itm = weapon_id ? item_->get_item(*weapon_id) : nullptr;
        if (weapon_itm && weapon_itm->two_handed)
        {
            conn->send_raw(network::make_equip_result(false, slot_name).dump());
            return;
        }
    }

    // Call item_ops to equip (handles swap if slot occupied)
    auto result = item_ops::equip_item(owner_eid, target_item_id, target_slot, item_, inventory_, &plr->equipment);
    if (!result.success)
    {
        // O motivo vinha sendo descartado: o cliente so recebia um ack false sem contexto,
        // entao uma falha de equipar era indistinguivel de um pacote perdido.
        LOG_WARN(bridge, "Player {} failed to equip item {} to slot {}: {}",
                 pid.value, target_item_id.value, slot_name, result.error);
        conn->send_raw(network::make_equip_result(false, slot_name).dump());
        return;
    }

    // Recalculate stats and appearance after equip
    players_->recalculate_equipment_modifiers(pid);
    players_->recalculate_appearance(pid);

    // Send v2 ack
    conn->send_raw(network::make_equip_result(true, slot_name).dump());

    // Send inventory_item_updates for affected items
    // 1. Swapped-out item (if slot was occupied)
    if (result.swapped_out.has_value())
    {
        send_item_update_v2(conn, *result.swapped_out, item_, inventory_, owner_eid);
    }

    // 2. Newly equipped item
    send_item_update_v2(conn, target_item_id, item_, inventory_, owner_eid);

    // Send stat update (re-fetch player since recalculate may have changed computed stats)
    plr = players_->get_player(pid);
    if (plr)
    {
        send_stat_update(conn_id, *plr);
    }

    // Broadcast equipment change to nearby players
    broadcast_equipment_change(pid, target_slot, target_item_id);

    LOG_DEBUG(bridge, "Player {} equipped item {} to slot '{}'", pid.value, target_item_id.value, slot_name);
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

    // Parse request — support both v1 (player_unequip_request) and v2 (unequip_request) formats
    player::equip_slot slot{};
    std::string slot_name;
    std::optional<int16_t> bag_pos_x;
    std::optional<int16_t> bag_pos_y;

    if (msg.type == network::json_message_type::unequip_request)
    {
        // v2 format: string slot name
        auto data_result = network::unequip_request_data::from_json(msg.data);
        if (data_result.is_err())
        {
            send_error(conn_id, msg.seq, "invalid_request", data_result.error());
            return;
        }
        auto& data = data_result.value();
        slot_name = data.slot;
        bag_pos_x = data.pos_x;
        bag_pos_y = data.pos_y;
        auto slot_opt = parse_equip_slot(data.slot);
        if (!slot_opt)
        {
            conn->send_raw(network::make_unequip_result(false, data.slot).dump());
            return;
        }
        slot = *slot_opt;
    }
    else
    {
        // v1 format: numeric equip_slot
        auto data_result = network::player_unequip_request_data::from_json(msg.data);
        if (data_result.is_err())
        {
            send_error(conn_id, msg.seq, "invalid_request", data_result.error());
            return;
        }
        auto& data = data_result.value();
        if (data.equip_slot >= static_cast<uint8_t>(player::equip_slot::count))
        {
            send_error(conn_id, msg.seq, "invalid_slot", "Invalid equipment slot");
            return;
        }
        slot = static_cast<player::equip_slot>(data.equip_slot);
        slot_name = std::string(slot_to_name(slot));
    }

    auto pid = conn->player();
    auto* plr = players_->get_player(pid);
    if (!plr)
    {
        send_error(conn_id, msg.seq, "invalid_player", "Player not found");
        return;
    }

    auto owner_eid = entity_id(plr->id.value);

    // Check alive
    if (plr->is_dead())
    {
        conn->send_raw(network::make_unequip_result(false, slot_name).dump());
        return;
    }

    // Check not trading
    auto trade_partner = inventory_->get_trade_partner(owner_eid);
    if (trade_partner.is_valid())
    {
        conn->send_raw(network::make_unequip_result(false, slot_name).dump());
        return;
    }

    // Call item_ops to unequip
    auto result = item_ops::unequip_item(owner_eid, slot, &plr->equipment);
    if (!result.success)
    {
        conn->send_raw(network::make_unequip_result(false, slot_name).dump());
        return;
    }

    // Recalculate stats and appearance
    players_->recalculate_equipment_modifiers(pid);
    players_->recalculate_appearance(pid);

    // Reposition item in bag if drop coordinates were provided
    if (bag_pos_x && bag_pos_y)
    {
        auto* inv_data = inventory_->get_inventory(owner_eid);
        if (inv_data)
        {
            auto* entry = inv_data->get_item(result.unequipped);
            if (entry)
            {
                entry->pos_x = *bag_pos_x;
                entry->pos_y = *bag_pos_y;
            }
        }
    }

    // Send v2 ack
    conn->send_raw(network::make_unequip_result(true, slot_name).dump());

    // Send inventory_item_update (item stays in inventory, just no longer equipped)
    send_item_update_v2(conn, result.unequipped, item_, inventory_, owner_eid);

    // Send stat update
    plr = players_->get_player(pid);
    if (plr)
    {
        send_stat_update(conn_id, *plr);
    }

    // Broadcast to nearby (slot now empty)
    broadcast_equipment_change(pid, slot, item_id{});

    LOG_DEBUG(bridge, "Player {} unequipped item {} from slot '{}'", pid.value, result.unequipped.value, slot_name);
}

void game_handlers::broadcast_equipment_change(player_id pid, player::equip_slot slot, item_id itm)
{
    if (!players_ || !ws_server_)
        return;

    auto* plr = players_->get_player(pid);
    if (!plr)
        return;

    // Build v2 equipment_change message with serialized item (or null if slot cleared)
    nlohmann::json item_json = nullptr;
    if (itm.is_valid() && item_)
    {
        auto* item_inst = item_->get_item(itm);
        if (item_inst)
        {
            item_json = item::serialize_item(*item_inst);
        }
    }

    // Get updated appearance for this slot (appearance is recalculated before this call)
    int8_t appr_val = 0;
    int8_t color_val = 0;
    switch (slot)
    {
    case player::equip_slot::weapon: appr_val = plr->appearance.weapon.appr; color_val = plr->appearance.weapon.color; break;
    case player::equip_slot::shield: appr_val = plr->appearance.shield.appr; color_val = plr->appearance.shield.color; break;
    case player::equip_slot::body: appr_val = plr->appearance.body.appr; color_val = plr->appearance.body.color; break;
    case player::equip_slot::pants: appr_val = plr->appearance.pants.appr; color_val = plr->appearance.pants.color; break;
    case player::equip_slot::head: appr_val = plr->appearance.head.appr; color_val = plr->appearance.head.color; break;
    case player::equip_slot::arms: appr_val = plr->appearance.arms.appr; color_val = plr->appearance.arms.color; break;
    case player::equip_slot::boots: appr_val = plr->appearance.boots.appr; color_val = plr->appearance.boots.color; break;
    case player::equip_slot::cape: appr_val = plr->appearance.cape.appr; color_val = plr->appearance.cape.color; break;
    default: break;
    }

    auto v2_msg = network::make_equipment_change(plr->ecs_entity.id, slot_to_name(slot), item_json, appr_val, color_val);
    auto v2_str = v2_msg.dump();

    // Also build the v1 broadcast for backward compatibility
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
    auto v1_msg = network::make_equipment_change_broadcast(data);

    // Broadcast v1 to all visible (existing clients)
    broadcast_to_visible(players_, ws_server_, plr->current_map, plr->pos, v1_msg, pid);

    // Send v2 to all visible players including the acting player
    for_each_visible_connection(
        players_, ws_server_, plr->current_map, plr->pos,
        [&v2_str](player_id, player::player&, network::ws_connection& conn)
        {
            conn.send_raw(v2_str);
        });

    // Forward to admin spectators
    for (auto admin_conn : ws_server_->get_admin_subscribers(plr->current_map))
    {
        ws_server_->send(admin_conn, v1_msg);
    }
}

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

    // Parse request — support both v1 (player_use_item_request) and v2 (use_item_request) formats
    uint32_t raw_item_id = 0;
    bool is_v2 = (msg.type == network::json_message_type::use_item_request);

    if (is_v2)
    {
        auto data_result = network::use_item_request_data_v2::from_json(msg.data);
        if (data_result.is_err())
        {
            send_error(conn_id, msg.seq, "invalid_request", data_result.error());
            return;
        }
        raw_item_id = static_cast<uint32_t>(data_result.value().item_id);
    }
    else
    {
        auto data_result = network::use_item_request_data::from_json(msg.data);
        if (data_result.is_err())
        {
            send_error(conn_id, msg.seq, "invalid_request", data_result.error());
            return;
        }
        raw_item_id = data_result.value().item_id;
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
        conn->send_raw(network::make_use_item_result(false).dump());
        return;
    }

    // Validate item
    auto owner_id = entity_id(plr->id.value);
    auto* inv = inventory_->get_inventory(owner_id);
    if (!inv)
    {
        send_error(conn_id, msg.seq, "internal_error", "Inventory unavailable");
        return;
    }

    auto use_item_id = item_id{raw_item_id};
    auto* entry = inv->get_item(use_item_id);
    if (!entry)
    {
        conn->send_raw(network::make_use_item_result(false).dump());
        return;
    }

    // Look up item instance and template
    auto* item_reg = subsystems().get<item_registry>();
    if (!item_reg)
    {
        send_error(conn_id, msg.seq, "internal_error", "Item registry unavailable");
        return;
    }

    auto* use_itm = item_ ? item_->get_item(use_item_id) : nullptr;
    const auto* tmpl = use_itm ? item_reg->get(use_itm->template_id) : nullptr;
    if (!tmpl)
    {
        conn->send_raw(network::make_use_item_result(false).dump());
        return;
    }

    // Check legacy item types for usability
    if (tmpl->type != item_type::eat && tmpl->type != item_type::use_deplete)
    {
        conn->send_raw(network::make_use_item_result(false).dump());
        return;
    }

    // Interpret consumable effect from raw template fields:
    // effect_type maps to consumable_effect_type (4=hp, 5=mp, 6=sp, 7=food, 11=scroll)
    struct consumable_params
    {
        int16_t type{0};
        int16_t v1{0};
        int16_t v2{0};
        int16_t v3{0};
        int16_t v4{0};
        int16_t v5{0};
    };
    consumable_params eff;
    eff.type = tmpl->effect_type;
    eff.v1 = tmpl->effect_value1;
    eff.v2 = tmpl->effect_value2;
    eff.v3 = tmpl->effect_value3;
    eff.v4 = tmpl->effect_value4;
    eff.v5 = tmpl->effect_value5;

    // Save pre-use state for audit
    int16_t pre_use_count = entry->count;
    bool use_audited = use_itm ? use_itm->audited : false;

    // Get current map for restriction checks
    auto* current_map = world_ ? world_->get_map(plr->current_map) : nullptr;

    // Raw effect_type values from legacy: 4=hp, 5=mp, 6=sp, 7=food, 11=magic_scroll
    constexpr int16_t eff_hp_restore = 4;
    constexpr int16_t eff_mp_restore = 5;
    constexpr int16_t eff_sp_restore = 6;
    constexpr int16_t eff_food = 7;
    constexpr int16_t eff_magic_scroll = 11;

    // Helper lambda: consume one stack of the item and send the appropriate inventory update
    auto consume_one = [&]()
    {
        bool fully_consumed = (entry->count <= 1);
        if (fully_consumed)
        {
            inv->remove_item(use_item_id);
            entry = nullptr;
            conn->send_raw(network::make_inventory_item_removed_v2(use_item_id).dump());
        }
        else
        {
            --entry->count;
            conn->send_raw(network::make_inventory_item_delta(use_item_id, entry->count, std::nullopt).dump());
        }
        return fully_consumed;
    };

    // Helper lambda: audit item consumption
    auto audit_consumption = [&]()
    {
        if (audit_ && use_audited)
        {
            auto* post_entry = inv->get_item(use_item_id);
            int16_t post_count = post_entry ? post_entry->count : 0;
            if (post_count < pre_use_count)
            {
                audit_->log_item(
                    static_cast<int32_t>(plr->character_id.value), tmpl->name, static_cast<int32_t>(use_item_id.value), item_log_type::use, pre_use_count - post_count);
            }
        }
    };

    if (eff.type == eff_hp_restore || eff.type == eff_mp_restore || eff.type == eff_sp_restore)
    {
        // Map restriction check
        if (current_map && current_map->config().is_potions_disabled)
        {
            conn->send_raw(network::make_use_item_result(false).dump());
            return;
        }

        // Anti-cheat check
        auto now = std::chrono::steady_clock::now();
        plr->potion_tracker.record_use(now);
        bool speed_hack = plr->potion_tracker.is_speed_hack();

        // Consume the item regardless of speed hack
        consume_one();

        if (speed_hack)
        {
            // Item consumed but no effect applied
            conn->send_raw(network::make_use_item_result(true).dump());
            audit_consumption();
            return;
        }

        int32_t amount = dice_roll(eff.v1, eff.v2, eff.v3);

        if (eff.type == eff_hp_restore)
        {
            plr->heal_hp(amount);
        }
        else if (eff.type == eff_mp_restore)
        {
            plr->heal_mp(amount);
        }
        else
        {
            plr->heal_sp(amount);
            // SP potions also cure poison (legacy behavior)
            plr->remove_status(player::player_status::poisoned);
        }

        conn->send_raw(network::make_use_item_result(true).dump());

        // Send stat update so client sees HP/MP/SP change
        send_stat_update(conn_id, *plr);
    }
    else if (eff.type == eff_food)
    {
        int32_t amount = dice_roll(eff.v1, eff.v2, eff.v3);

        // Consume the item
        consume_one();

        // Legacy: food adds to both hunger AND hp_stock (bonus HP spread over regen ticks)
        players_->restore_hunger(pid, static_cast<int8_t>(std::min(amount, static_cast<int32_t>(127))));
        players_->add_hp_stock(pid, amount);

        conn->send_raw(network::make_use_item_result(true).dump());

        // Send stat update
        send_stat_update(conn_id, *plr);
    }
    else if (eff.type == eff_magic_scroll)
    {
        // Recall scroll (v1 == 1)
        if (eff.v1 == 1)
        {
            // Map restriction check
            if (current_map && current_map->config().is_recall_impossible)
            {
                conn->send_raw(network::make_use_item_result(false).dump());
                return;
            }

            // Consume scroll FIRST (legacy ordering)
            consume_one();

            // Teleport to faction home
            std::string dest_map = get_respawn_map_name(plr->faction);
            world::position dest_pos = get_respawn_position(dest_map);

            execute_player_teleport(pid, conn_id, msg.seq, dest_map, dest_pos, world::direction::south);

            // execute_player_teleport sends its own response -- no use_item ack needed
            LOG_INFO(
                bridge, "Player {} used recall scroll -> {} ({}, {})", pid.value, dest_map, dest_pos.x, dest_pos.y);

            audit_consumption();
            return;
        }

        // Other scroll subtypes not yet implemented
        conn->send_raw(network::make_use_item_result(false).dump());
    }
    else
    {
        conn->send_raw(network::make_use_item_result(false).dump());
    }

    audit_consumption();
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

    // Parse request — support both v1 (item_upgrade_request) and v2 (upgrade_request) formats
    item_id target_iid{};
    item_id stone_iid{};
    bool is_v2 = (msg.type == network::json_message_type::upgrade_request);

    if (is_v2)
    {
        auto data_result = network::upgrade_request_data::from_json(msg.data);
        if (data_result.is_err())
        {
            send_error(conn_id, msg.seq, "invalid_request", data_result.error());
            return;
        }
        auto& data = data_result.value();
        target_iid = item_id{static_cast<uint32_t>(data.target_id)};
        stone_iid = item_id{static_cast<uint32_t>(data.stone_id)};
    }
    else
    {
        // v1 format: item_id only, stone is auto-found
        auto data_result = network::item_upgrade_request_data::from_json(msg.data);
        if (data_result.is_err())
        {
            send_error(conn_id, msg.seq, "invalid_request", data_result.error());
            return;
        }
        target_iid = item_id{data_result.value().item_id};
        // stone_iid stays invalid — will be auto-found below
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
        conn->send_raw(network::make_upgrade_result_v2(false).dump());
        return;
    }

    auto owner_eid = entity_id(plr->id.value);
    auto* inv = inventory_->get_inventory(owner_eid);
    if (!inv)
    {
        send_error(conn_id, msg.seq, "internal_error", "Inventory unavailable");
        return;
    }

    // Validate target item exists and is equipment
    auto* target_item = item_->get_item(target_iid);
    if (!target_item)
    {
        conn->send_raw(network::make_upgrade_result_v2(false).dump());
        return;
    }

    if (!target_item->is_equipment())
    {
        conn->send_raw(network::make_upgrade_result_v2(false).dump());
        return;
    }

    // Already at max level
    if (target_item->attribute.upgrade_level >= item::max_upgrade_level)
    {
        conn->send_raw(network::make_upgrade_result_v2(false).dump());
        return;
    }

    // If stone not specified (v1), auto-find an appropriate upgrade stone
    if (!stone_iid.is_valid())
    {
        for (auto& inv_entry : inv->items())
        {
            auto* itm = item_->get_item(inv_entry.item);
            if (!itm)
                continue;

            if (itm->template_id.value == item::xelima_stone_id || itm->template_id.value == item::merien_stone_id)
            {
                if (item::is_valid_upgrade_stone(*target_item, itm->template_id.value))
                {
                    stone_iid = inv_entry.item;
                    break;
                }
            }
        }
    }

    if (!stone_iid.is_valid())
    {
        conn->send_raw(network::make_upgrade_result_v2(false).dump());
        return;
    }

    // Attempt the upgrade (probability-based)
    auto upgrade_result = item::attempt_upgrade(*target_item);

    // Consume stone (always consumed regardless of success)
    auto* stone_entry = inv->get_item(stone_iid);
    if (stone_entry)
    {
        if (stone_entry->count <= 1)
        {
            inv->remove_item(stone_iid);
            item_->destroy_item(stone_iid);
            stone_entry = nullptr;
            conn->send_raw(network::make_inventory_item_removed_v2(stone_iid).dump());
        }
        else
        {
            stone_entry->count -= 1;
            conn->send_raw(network::make_inventory_item_delta(stone_iid, stone_entry->count, std::nullopt).dump());
        }
    }

    // If successful, recalculate equipment modifiers
    if (upgrade_result.success)
    {
        players_->recalculate_equipment_modifiers(pid);
    }

    // Send v2 ack
    conn->send_raw(network::make_upgrade_result_v2(upgrade_result.success).dump());

    // Send inventory update for the target item (attribute may have changed)
    send_item_update_v2(conn, target_iid, item_, inventory_, owner_eid);
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
        conn->send_raw(network::make_activate_ability_failed("dead").dump());
        return;
    }

    auto& ability = plr->special_ability;

    // No ability equipped
    if (ability.type == item::special_ability_type::none)
    {
        conn->send_raw(network::make_activate_ability_failed("no_ability").dump());
        return;
    }

    auto now = std::chrono::steady_clock::now();

    // Check if on cooldown (and possibly transition to ready)
    ability.check_cooldown(now);

    if (ability.is_on_cooldown(now))
    {
        conn->send_raw(network::make_activate_ability_failed("on_cooldown").dump());
        return;
    }

    if (ability.is_active())
    {
        conn->send_raw(network::make_activate_ability_failed("already_active").dump());
        return;
    }

    if (!ability.is_ready())
    {
        conn->send_raw(network::make_activate_ability_failed("not_ready").dump());
        return;
    }

    // Activate
    ability.activate(now);

    auto ability_name = std::string(item::special_ability_type_to_string(ability.type));
    constexpr int32_t duration_ms = item::ability_cooldown_seconds * 1000;

    // Broadcast ability_activated to all visible players (including the activator)
    auto activated_msg = network::make_ability_activated(plr->ecs_entity.id, ability_name, duration_ms);
    auto activated_str = activated_msg.dump();

    for_each_visible_connection(
        players_, ws_server_, plr->current_map, plr->pos,
        [&activated_str](player_id, player::player&, network::ws_connection& c)
        {
            c.send_raw(activated_str);
        });

    // Also send special_ability_status to the activator (for UI state tracking)
    conn->send(network::make_special_ability_status("active", static_cast<uint8_t>(ability.type), 0));
}

} // namespace hb::bridge
