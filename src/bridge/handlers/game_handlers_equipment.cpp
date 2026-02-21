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
    auto trade_partner = inventory_->get_trade_partner(entity_id(plr->ecs_entity.id));
    if (trade_partner.is_valid())
    {
        send_error(conn_id, msg.seq, "player_busy", "Cannot equip while trading");
        return;
    }

    // Get inventory and validate slot
    auto* inv = inventory_->get_inventory(entity_id(plr->ecs_entity.id));
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
        // Just clear the shield's equipped_as flag — item stays in its inventory slot
        auto shield_item_id = players_->unequip_item(pid, player::equip_slot::shield);
        if (shield_item_id.is_valid())
        {
            result.unequipped_shield_id = shield_item_id.value;
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

    // Swap logic: if target equipment slot is occupied, just clear its equipped_as flag
    if (plr->equipment.has_equipped(target_slot))
    {
        auto old_item_id = players_->unequip_item(pid, target_slot);
        result.swapped_item_id = old_item_id.value;
    }

    // Equip new item — sets equipped_as on the inventory slot, rebuilds cache + recalculates
    players_->equip_item(pid, data.inventory_slot, target_slot);

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

    // Send inventory_slot_updates for affected items
    // 1. Shield that was unequipped (if 2H weapon equipped)
    if (result.unequipped_shield_id.has_value() && *result.unequipped_shield_id != 0)
    {
        auto sid = item_id(*result.unequipped_shield_id);
        if (auto shield_slot = inv->find_item(sid); shield_slot)
        {
            auto shield_msg = network::build_inventory_item_msg(*shield_slot, sid, item_, item_registry_);
            if (shield_msg)
                conn->send(network::make_inventory_slot_update(*shield_slot, &*shield_msg));
        }
    }
    // 2. Old item swapped out of target slot
    if (result.swapped_item_id.has_value() && *result.swapped_item_id != 0)
    {
        auto oid = item_id(*result.swapped_item_id);
        if (auto old_slot = inv->find_item(oid); old_slot)
        {
            auto old_msg = network::build_inventory_item_msg(*old_slot, oid, item_, item_registry_);
            if (old_msg)
                conn->send(network::make_inventory_slot_update(*old_slot, &*old_msg));
        }
    }
    // 3. Newly equipped item
    {
        auto equip_msg = network::build_inventory_item_msg(
            data.inventory_slot, inv_slot->item, item_, item_registry_,
            static_cast<uint8_t>(target_slot));
        if (equip_msg)
            conn->send(network::make_inventory_slot_update(data.inventory_slot, &*equip_msg));
    }

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
    auto trade_partner = inventory_->get_trade_partner(entity_id(plr->ecs_entity.id));
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

    // Get the inventory slot index from the equipment cache (item is already in inventory)
    auto inv_idx = plr->equipment.get(slot).inv_index;
    auto equipped_id = plr->equipment.get(slot).id;

    // Unequip — clears equipped_as flag, item stays in its inventory slot
    players_->unequip_item(pid, slot);

    // Get item details for response
    auto* itm = item_->get_item(equipped_id);
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
    result.item_id = equipped_id.value;
    result.item_name = item_name;
    result.inventory_slot = static_cast<uint8_t>(inv_idx >= 0 ? inv_idx : 0);
    result.attribute = attr;

    conn->send(network::make_player_unequip_response(msg.seq, result));

    // Send inventory_slot_update (item stays in same slot, just no longer equipped)
    {
        auto item_msg = network::build_inventory_item_msg(
            static_cast<int16_t>(inv_idx), equipped_id, item_, item_registry_);
        if (item_msg)
            conn->send(network::make_inventory_slot_update(static_cast<int16_t>(inv_idx), &*item_msg));
    }

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
              equipped_id.value,
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
    broadcast_to_visible(players_, ws_server_, plr->current_map, plr->pos, msg, pid);

    // Forward to admin spectators
    for (auto admin_conn : ws_server_->get_admin_subscribers(plr->current_map))
    {
        ws_server_->send(admin_conn, msg);
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
            // Send inventory_slot_update for consumed potion
            if (slot->is_empty())
            {
                conn->send(network::make_inventory_slot_update(data.slot, nullptr));
            }
            else
            {
                auto potion_msg = network::build_inventory_item_msg(data.slot, slot->item, item_, item_registry_);
                if (potion_msg)
                    conn->send(network::make_inventory_slot_update(data.slot, &*potion_msg));
            }
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

        // Legacy: food adds to both hunger AND hp_stock (bonus HP spread over regen ticks)
        players_->restore_hunger(pid, static_cast<int8_t>(std::min(amount, static_cast<int32_t>(127))));
        players_->add_hp_stock(pid, amount);

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

            // Send inventory_slot_update for consumed scroll
            if (slot->is_empty())
            {
                conn->send(network::make_inventory_slot_update(data.slot, nullptr));
            }
            else
            {
                auto scroll_msg = network::build_inventory_item_msg(data.slot, slot->item, item_, item_registry_);
                if (scroll_msg)
                    conn->send(network::make_inventory_slot_update(data.slot, &*scroll_msg));
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

    // Send inventory_slot_update after item consumption
    if (slot->is_empty())
    {
        conn->send(network::make_inventory_slot_update(data.slot, nullptr));
    }
    else
    {
        auto item_msg = network::build_inventory_item_msg(data.slot, slot->item, item_, item_registry_);
        if (item_msg)
            conn->send(network::make_inventory_slot_update(data.slot, &*item_msg));
    }

    // Audit item use (check if count changed = item was consumed)
    if (audit_ && use_audited)
    {
        int16_t post_count = slot->is_empty() ? 0 : slot->count;
        if (post_count < pre_use_count)
        {
            audit_->log_item(
                static_cast<int32_t>(plr->character_id.value), tmpl->name, static_cast<int32_t>(use_item_id.value), item_log_type::use, pre_use_count - post_count);
        }
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

    // Send inventory_slot_update for consumed stone
    if (stone_slot >= 0)
    {
        auto* stone_s = inv->get_slot(stone_slot);
        if (!stone_s || stone_s->is_empty())
        {
            conn->send(network::make_inventory_slot_update(stone_slot, nullptr));
        }
        else
        {
            auto stone_msg = network::build_inventory_item_msg(stone_slot, stone_s->item, item_, item_registry_);
            if (stone_msg)
                conn->send(network::make_inventory_slot_update(stone_slot, &*stone_msg));
        }
    }

    // Send inventory_slot_update for upgraded/target item (attribute may have changed)
    {
        auto* ts = inv->get_slot(data.item_slot);
        if (ts && !ts->is_empty())
        {
            auto upgraded_msg = network::build_inventory_item_msg(data.item_slot, ts->item, item_, item_registry_);
            if (upgraded_msg)
                conn->send(network::make_inventory_slot_update(data.item_slot, &*upgraded_msg));
        }
    }
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

} // namespace hb::bridge
