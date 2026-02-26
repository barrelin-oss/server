// item_ops.cpp
// Business logic layer for item pickup and drop operations.

#include "item/item_ops.h"

#include "item/item.h"
#include "item/item_system.h"
#include "inventory/inventory.h"
#include "inventory/inventory_system.h"
#include "world/world_subsystem.h"
#include "player/equipment.h"
#include "registry/item_registry.h"
#include "registry/item_template.h"
#include <spdlog/spdlog.h>

namespace hb::item_ops
{

auto pickup_item(
    entity_id player,
    map_id map,
    world::position pos,
    item::item_system* items,
    inventory::inventory_system* inv,
    world::world_subsystem* world
) -> pickup_result
{
    if (!items || !inv || !world)
    {
        return pickup_result::err(pickup_failure::subsystem_error);
    }

    // 1. Check for ground items
    if (!world->has_ground_items(map, pos))
    {
        spdlog::trace("pickup_item: no ground items at map={} ({},{})", map.value, pos.x, pos.y);
        return pickup_result::err(pickup_failure::no_items);
    }

    // 2. Check inventory capacity
    if (inv->is_full(player))
    {
        spdlog::debug("pickup_item: inventory full for player eid={}", player.value);
        return pickup_result::err(pickup_failure::inventory_full);
    }

    // 3. Remove the top-most item from ground
    auto item_id_opt = world->remove_top_ground_item(map, pos);
    if (!item_id_opt.has_value())
    {
        spdlog::debug("pickup_item: remove_top_ground_item returned nullopt at map={} ({},{})", map.value, pos.x, pos.y);
        return pickup_result::err(pickup_failure::item_unavailable);
    }

    auto picked_id = item_id_opt.value();

    // Look up the item instance for weight check and type info
    auto* itm = items->get_item(picked_id);
    spdlog::trace("pickup_item: removed item id={} ({})", picked_id.value, itm ? itm->name : "unknown");

    // 4. Check weight limit
    if (itm && !inv->can_carry(player, itm->weight))
    {
        spdlog::debug("pickup_item: item '{}' weight={} exceeds carry limit, putting back", itm->name, itm->weight);
        // Put item back on ground
        world->add_ground_item(map, pos, picked_id);
        return pickup_result::err(pickup_failure::too_heavy);
    }

    // 5. Determine placement position
    //    Default position (30, 40); consumables use the same default.
    int16_t place_x = 30;
    int16_t place_y = 40;

    // 6. Add to inventory
    auto add_result = inv->add_item(player, picked_id, itm ? itm->count : 1, place_x, place_y);
    if (add_result != inventory::inventory_result::success)
    {
        spdlog::debug("pickup_item: add_item failed with result={}, putting item back", static_cast<int>(add_result));
        // Put item back on ground
        world->add_ground_item(map, pos, picked_id);
        return pickup_result::err(pickup_failure::add_failed);
    }

    // 7. Set item owner
    items->set_owner(picked_id, player);

    // 8. Look up final placement info (z_order from inventory entry)
    int32_t z_order = 0;
    auto* inv_data = inv->get_inventory(player);
    if (inv_data)
    {
        auto* entry = inv_data->get_item(picked_id);
        if (entry)
        {
            place_x = entry->pos_x;
            place_y = entry->pos_y;
            z_order = entry->z_order;
        }
    }

    return pickup_result::ok({
        .picked_up = picked_id,
        .pos_x = place_x,
        .pos_y = place_y,
        .z_order = z_order,
        .new_weight = inv->get_current_weight(player),
        .max_weight = inv->get_max_weight(player),
    });
}

auto drop_item(
    entity_id player,
    item_id id,
    map_id map,
    world::position pos,
    item::item_system* items,
    inventory::inventory_system* inv,
    world::world_subsystem* world,
    player::equipment_state* equipment
) -> drop_result
{
    if (!items || !inv || !world)
    {
        return {.success = false, .error = "Required subsystems unavailable"};
    }

    // 1. Get the item
    auto* itm = items->get_item(id);
    if (!itm)
    {
        return {.success = false, .error = "Item not found"};
    }

    // 2. Verify ownership
    if (itm->owner != player)
    {
        return {.success = false, .error = "Item not owned by player"};
    }

    // 3. Check droppable flag
    if (!itm->droppable)
    {
        return {.success = false, .error = "Item cannot be dropped"};
    }

    // 4. Check if equipped — auto-unequip if so
    bool was_equipped = false;
    std::optional<player::equip_slot> unequipped_slot;

    if (equipment)
    {
        auto slot_opt = equipment->find_slot_for(id);
        if (slot_opt.has_value())
        {
            equipment->unequip(slot_opt.value());
            was_equipped = true;
            unequipped_slot = slot_opt.value();
        }
    }

    // 5. Remove from inventory
    inv->remove_item(player, id);

    // 6. Clear item owner
    items->clear_owner(id);

    // 7. Add to ground at player position
    world->add_ground_item(map, pos, id);

    return {
        .success = true,
        .error = {},
        .dropped = id,
        .was_equipped = was_equipped,
        .unequipped_slot = unequipped_slot,
    };
}

auto equip_item(
    entity_id player,
    item_id id,
    player::equip_slot slot,
    item::item_system* items,
    inventory::inventory_system* inv,
    player::equipment_state* equipment
) -> equip_result
{
    if (!items || !inv || !equipment)
    {
        return {.success = false, .error = "Required subsystems unavailable"};
    }

    // 1. Get item
    auto* itm = items->get_item(id);
    if (!itm)
    {
        return {.success = false, .error = "Item not found"};
    }

    // 2. Verify ownership
    if (itm->owner != player)
    {
        return {.success = false, .error = "Item not owned by player"};
    }

    // 3. Verify item is in inventory
    auto* inv_data = inv->get_inventory(player);
    if (!inv_data || !inv_data->has_item(id))
    {
        return {.success = false, .error = "Item not in inventory"};
    }

    // 4. Verify item is equippable
    if (itm->equip_position == item::equip_pos::none)
    {
        return {.success = false, .error = "Item is not equippable"};
    }

    // 5. Verify item is not broken
    if (itm->is_broken())
    {
        return {.success = false, .error = "Item is broken"};
    }

    // 6. If slot occupied, unequip old item first
    std::optional<item_id> swapped_out;
    auto existing = equipment->get_equipped(slot);
    if (existing.has_value())
    {
        equipment->unequip(slot);
        swapped_out = existing.value();
    }

    // 7. Equip the new item
    equipment->equip(slot, id);

    return {
        .success = true,
        .error = {},
        .slot = slot,
        .equipped = id,
        .swapped_out = swapped_out,
    };
}

auto unequip_item(
    entity_id player,
    player::equip_slot slot,
    player::equipment_state* equipment
) -> unequip_result
{
    (void)player; // Reserved for future validation

    if (!equipment)
    {
        return {.success = false, .error = "Equipment state unavailable"};
    }

    // 1. Check if slot has an item
    auto existing = equipment->get_equipped(slot);
    if (!existing.has_value())
    {
        return {.success = false, .error = "Slot is empty"};
    }

    auto unequipped_id = existing.value();

    // 2. Unequip
    equipment->unequip(slot);

    return {
        .success = true,
        .error = {},
        .slot = slot,
        .unequipped = unequipped_id,
    };
}

auto force_unequip(
    entity_id player,
    player::equip_slot slot,
    force_unequip_reason reason,
    player::equipment_state* equipment
) -> force_unequip_result
{
    (void)player; // Reserved for future validation

    if (!equipment)
    {
        return {.success = false};
    }

    // 1. Check if slot has an item
    auto existing = equipment->get_equipped(slot);
    if (!existing.has_value())
    {
        return {.success = false};
    }

    auto unequipped_id = existing.value();

    // 2. Unequip
    equipment->unequip(slot);

    return {
        .success = true,
        .slot = slot,
        .unequipped = unequipped_id,
        .reason = reason,
    };
}

auto shop_buy(
    entity_id player,
    item_id template_id,
    int32_t buy_price,
    item::item_system* items,
    inventory::inventory_system* inv
) -> shop_buy_result
{
    if (!items || !inv)
    {
        return {.success = false, .error = "Required subsystems unavailable"};
    }

    // 1. Check player has enough gold
    if (!inv->has_gold(player, buy_price))
    {
        return {.success = false, .error = "Not enough gold"};
    }

    // 2. Check inventory not full
    if (inv->is_full(player))
    {
        return {.success = false, .error = "Inventory full"};
    }

    // 3. Check weight limit — look up template weight from registry
    auto* registry = subsystems().get<item_registry>();
    if (registry)
    {
        auto* tmpl = registry->get(template_id);
        if (tmpl && !inv->can_carry(player, tmpl->weight))
        {
            return {.success = false, .error = "Too heavy to carry"};
        }
    }

    // 4. Create a new item instance from template
    item::item_create_info create_info;
    create_info.template_id = template_id;
    create_info.owner = player;
    create_info.full_durability = true;

    auto create_result = items->create_item(create_info);
    if (!create_result.is_ok())
    {
        return {.success = false, .error = "Failed to create item: " + create_result.error()};
    }

    auto created_id = create_result.value();

    // 5. Deduct gold
    inv->remove_gold(player, buy_price);

    // 6. Add item to inventory at default position (30, 40)
    int16_t place_x = 30;
    int16_t place_y = 40;

    auto add_result = inv->add_item(player, created_id, 1, place_x, place_y);
    if (add_result != inventory::inventory_result::success)
    {
        // Rollback: refund gold and destroy the item
        inv->add_gold(player, buy_price);
        items->destroy_item(created_id);
        return {.success = false, .error = "Failed to add item to inventory"};
    }

    // 7. Look up final placement info
    int32_t z_order = 0;
    auto* inv_data = inv->get_inventory(player);
    if (inv_data)
    {
        auto* entry = inv_data->get_item(created_id);
        if (entry)
        {
            place_x = entry->pos_x;
            place_y = entry->pos_y;
            z_order = entry->z_order;
        }
    }

    return {
        .success = true,
        .error = {},
        .created = created_id,
        .pos_x = place_x,
        .pos_y = place_y,
        .z_order = z_order,
        .new_gold = inv->get_gold(player),
        .new_weight = inv->get_current_weight(player),
        .max_weight = inv->get_max_weight(player),
    };
}

auto shop_sell(
    entity_id player,
    item_id id,
    item::item_system* items,
    inventory::inventory_system* inv,
    player::equipment_state* equipment
) -> shop_sell_result
{
    if (!items || !inv)
    {
        return {.success = false, .error = "Required subsystems unavailable"};
    }

    // 1. Get item
    auto* itm = items->get_item(id);
    if (!itm)
    {
        return {.success = false, .error = "Item not found"};
    }

    // 2. Verify ownership
    if (itm->owner != player)
    {
        return {.success = false, .error = "Item not owned by player"};
    }

    // 3. Verify tradeable
    if (!itm->tradeable)
    {
        return {.success = false, .error = "Item is not tradeable"};
    }

    // 4. Auto-unequip if equipped
    if (equipment)
    {
        auto slot_opt = equipment->find_slot_for(id);
        if (slot_opt.has_value())
        {
            equipment->unequip(slot_opt.value());
        }
    }

    // 5. Calculate sell price from item's price field
    int64_t sell_price = itm->price;

    // 6. Remove from inventory
    inv->remove_item(player, id);

    // 7. Add gold
    inv->add_gold(player, sell_price);

    // 8. Destroy item instance
    items->destroy_item(id);

    return {
        .success = true,
        .error = {},
        .sold = id,
        .new_gold = inv->get_gold(player),
        .new_weight = inv->get_current_weight(player),
        .max_weight = inv->get_max_weight(player),
    };
}

auto shop_repair(
    entity_id player,
    item_id id,
    item::item_system* items,
    inventory::inventory_system* inv
) -> shop_repair_result
{
    if (!items || !inv)
    {
        return {.success = false, .error = "Required subsystems unavailable"};
    }

    // 1. Get item
    auto* itm = items->get_item(id);
    if (!itm)
    {
        return {.success = false, .error = "Item not found"};
    }

    // 2. Verify ownership
    if (itm->owner != player)
    {
        return {.success = false, .error = "Item not owned by player"};
    }

    // 3. Verify not indestructible
    if (itm->indestructible)
    {
        return {.success = false, .error = "Item is indestructible"};
    }

    // 4. Verify item is damaged
    if (itm->durability >= itm->max_durability)
    {
        return {.success = false, .error = "Item is not damaged"};
    }

    // 5. Calculate repair cost: fraction of item price based on damage ratio
    //    cost = item_price * (damage / max_durability)
    //    Minimum cost of 1 gold if any repair is needed
    int16_t damage = itm->max_durability - itm->durability;
    int64_t repair_cost = 0;
    if (itm->max_durability > 0 && itm->price > 0)
    {
        repair_cost = static_cast<int64_t>(itm->price) * damage / itm->max_durability;
        if (repair_cost < 1)
        {
            repair_cost = 1;
        }
    }
    else
    {
        repair_cost = 1; // Minimum cost for items with no price
    }

    // 6. Check player has enough gold
    if (!inv->has_gold(player, repair_cost))
    {
        return {.success = false, .error = "Not enough gold"};
    }

    // 7. Deduct gold
    inv->remove_gold(player, repair_cost);

    // 8. Repair item to full durability
    items->repair_item_full(id);

    return {
        .success = true,
        .error = {},
        .repaired = id,
        .new_gold = inv->get_gold(player),
    };
}

auto execute_trade(
    entity_id player_a,
    entity_id player_b,
    item::item_system* items,
    inventory::inventory_system* inv
) -> trade_result
{
    if (!items || !inv)
    {
        return {.success = false, .error = "Required subsystems unavailable"};
    }

    // 1. Get both trade windows
    auto* window_a = inv->get_trade_window(player_a);
    auto* window_b = inv->get_trade_window(player_b);

    if (!window_a || !window_b)
    {
        return {.success = false, .error = "Trade window not found"};
    }

    // 2. Verify both locked AND confirmed
    if (!window_a->is_locked() || !window_b->is_locked())
    {
        return {.success = false, .error = "Trade not locked"};
    }

    if (!window_a->is_confirmed() || !window_b->is_confirmed())
    {
        return {.success = false, .error = "Trade not confirmed"};
    }

    trade_result result;
    result.success = true;

    // 3. Transfer items from A's offer to B
    for (auto offered_id : window_a->items())
    {
        inv->remove_item(player_a, offered_id);
        items->set_owner(offered_id, player_b);

        constexpr int16_t place_x = 30;
        constexpr int16_t place_y = 40;
        inv->add_item(player_b, offered_id, 1, place_x, place_y);

        result.player_a.lost_items.push_back(offered_id);
        result.player_b.received_items.push_back(offered_id);

        // Record placement for B
        int32_t z_order = 0;
        auto* inv_b = inv->get_inventory(player_b);
        if (inv_b)
        {
            auto* entry = inv_b->get_item(offered_id);
            if (entry)
            {
                z_order = entry->z_order;
            }
        }
        result.player_b.placements.push_back(inventory_placement{
            .item = offered_id,
            .pos_x = place_x,
            .pos_y = place_y,
            .z_order = z_order,
        });
    }

    // 4. Transfer items from B's offer to A
    for (auto offered_id : window_b->items())
    {
        inv->remove_item(player_b, offered_id);
        items->set_owner(offered_id, player_a);

        constexpr int16_t place_x = 30;
        constexpr int16_t place_y = 40;
        inv->add_item(player_a, offered_id, 1, place_x, place_y);

        result.player_b.lost_items.push_back(offered_id);
        result.player_a.received_items.push_back(offered_id);

        // Record placement for A
        int32_t z_order = 0;
        auto* inv_a = inv->get_inventory(player_a);
        if (inv_a)
        {
            auto* entry = inv_a->get_item(offered_id);
            if (entry)
            {
                z_order = entry->z_order;
            }
        }
        result.player_a.placements.push_back(inventory_placement{
            .item = offered_id,
            .pos_x = place_x,
            .pos_y = place_y,
            .z_order = z_order,
        });
    }

    // 5. Transfer gold
    auto gold_a = window_a->gold();
    auto gold_b = window_b->gold();

    if (gold_a > 0)
    {
        inv->remove_gold(player_a, gold_a);
        inv->add_gold(player_b, gold_a);
    }

    if (gold_b > 0)
    {
        inv->remove_gold(player_b, gold_b);
        inv->add_gold(player_a, gold_b);
    }

    result.player_a.gold_change = gold_b - gold_a;
    result.player_b.gold_change = gold_a - gold_b;

    // 6. Cancel/reset both trade windows
    inv->cancel_trade(player_a);

    // 7. Fill in weight info
    result.player_a.new_weight = inv->get_current_weight(player_a);
    result.player_a.max_weight = inv->get_max_weight(player_a);
    result.player_b.new_weight = inv->get_current_weight(player_b);
    result.player_b.max_weight = inv->get_max_weight(player_b);

    return result;
}

auto bank_deposit(
    entity_id player,
    item_id id,
    std::optional<int16_t> target_page,
    std::optional<int16_t> target_slot,
    item::item_system* items,
    inventory::inventory_system* inv,
    player::equipment_state* equipment
) -> bank_deposit_result
{
    if (!items || !inv)
    {
        return {.success = false, .error = "Required subsystems unavailable"};
    }

    // 1. Get item and verify ownership
    auto* itm = items->get_item(id);
    if (!itm)
    {
        return {.success = false, .error = "Item not found"};
    }

    if (itm->owner != player)
    {
        return {.success = false, .error = "Item not owned by player"};
    }

    // 2. Get bank
    auto* bank = inv->get_bank(player);
    if (!bank)
    {
        return {.success = false, .error = "No bank available"};
    }

    // 3. Determine target page/slot
    int16_t page = 0;
    int16_t slot = 0;

    if (target_page.has_value() && target_slot.has_value())
    {
        // Targeted deposit — verify slot is empty
        page = target_page.value();
        slot = target_slot.value();

        auto existing = bank->get_slot(page, slot);
        if (existing.has_value())
        {
            return {.success = false, .error = "Bank slot is occupied"};
        }
    }
    else
    {
        // Auto-deposit — find first empty slot
        auto empty = bank->find_empty_slot();
        if (!empty.has_value())
        {
            return {.success = false, .error = "Bank is full"};
        }
        page = empty->page;
        slot = empty->slot;
    }

    // 4. If equipped, auto-unequip first
    if (equipment)
    {
        auto slot_opt = equipment->find_slot_for(id);
        if (slot_opt.has_value())
        {
            equipment->unequip(slot_opt.value());
        }
    }

    // 5. Remove from inventory
    inv->remove_item(player, id);

    // 6. Set bank slot
    bank->set_slot(page, slot, id);

    return {
        .success = true,
        .error = {},
        .deposited = id,
        .page = page,
        .slot = slot,
        .new_weight = inv->get_current_weight(player),
        .max_weight = inv->get_max_weight(player),
    };
}

auto bank_withdraw(
    entity_id player,
    int16_t page,
    int16_t slot,
    item::item_system* items,
    inventory::inventory_system* inv
) -> bank_withdraw_result
{
    if (!items || !inv)
    {
        return {.success = false, .error = "Required subsystems unavailable"};
    }

    // 1. Get bank
    auto* bank = inv->get_bank(player);
    if (!bank)
    {
        return {.success = false, .error = "No bank available"};
    }

    // 2. Get slot contents
    auto slot_item = bank->get_slot(page, slot);
    if (!slot_item.has_value())
    {
        return {.success = false, .error = "Bank slot is empty"};
    }

    auto withdrawn_id = slot_item.value();

    // 3. Check inventory not full
    if (inv->is_full(player))
    {
        return {.success = false, .error = "Inventory full"};
    }

    // 4. Check weight limit
    auto* itm = items->get_item(withdrawn_id);
    if (itm && !inv->can_carry(player, itm->weight))
    {
        return {.success = false, .error = "Too heavy to carry"};
    }

    // 5. Clear bank slot
    bank->clear_slot(page, slot);

    // 6. Add item to inventory at default position (30, 40)
    int16_t place_x = 30;
    int16_t place_y = 40;

    auto add_result = inv->add_item(player, withdrawn_id, itm ? itm->count : 1, place_x, place_y);
    if (add_result != inventory::inventory_result::success)
    {
        // Rollback: put item back in bank
        bank->set_slot(page, slot, withdrawn_id);
        return {.success = false, .error = "Failed to add item to inventory"};
    }

    // 7. Look up final placement info
    int32_t z_order = 0;
    auto* inv_data = inv->get_inventory(player);
    if (inv_data)
    {
        auto* entry = inv_data->get_item(withdrawn_id);
        if (entry)
        {
            place_x = entry->pos_x;
            place_y = entry->pos_y;
            z_order = entry->z_order;
        }
    }

    return {
        .success = true,
        .error = {},
        .withdrawn = withdrawn_id,
        .pos_x = place_x,
        .pos_y = place_y,
        .z_order = z_order,
        .new_weight = inv->get_current_weight(player),
        .max_weight = inv->get_max_weight(player),
    };
}

auto bank_reposition(
    entity_id player,
    int16_t from_page, int16_t from_slot,
    int16_t to_page, int16_t to_slot,
    inventory::inventory_system* inv
) -> bank_reposition_result
{
    if (!inv)
    {
        return {.success = false, .error = "Required subsystems unavailable"};
    }

    // 1. Get bank
    auto* bank = inv->get_bank(player);
    if (!bank)
    {
        return {.success = false, .error = "No bank available"};
    }

    // 2. Get source slot
    auto source_item = bank->get_slot(from_page, from_slot);
    if (!source_item.has_value())
    {
        return {.success = false, .error = "Source bank slot is empty"};
    }

    // 3. Check dest slot
    auto dest_item = bank->get_slot(to_page, to_slot);

    if (!dest_item.has_value())
    {
        // Dest empty — move
        bank->clear_slot(from_page, from_slot);
        bank->set_slot(to_page, to_slot, source_item.value());
    }
    else
    {
        // Dest occupied — swap
        bank->set_slot(from_page, from_slot, dest_item.value());
        bank->set_slot(to_page, to_slot, source_item.value());
    }

    return {.success = true};
}

auto upgrade_item(
    entity_id player,
    item_id target_id,
    item_id stone_id,
    item::item_system* items,
    inventory::inventory_system* inv
) -> upgrade_result
{
    if (!items || !inv)
    {
        return {.success = false, .error = "Required subsystems unavailable"};
    }

    // 1. Get target item and verify ownership
    auto* target = items->get_item(target_id);
    if (!target)
    {
        return {.success = false, .error = "Target item not found"};
    }

    if (target->owner != player)
    {
        return {.success = false, .error = "Target item not owned by player"};
    }

    // 2. Get stone item and verify ownership
    auto* stone = items->get_item(stone_id);
    if (!stone)
    {
        return {.success = false, .error = "Stone not found"};
    }

    if (stone->owner != player)
    {
        return {.success = false, .error = "Stone not owned by player"};
    }

    // 3. Verify target is equipment
    if (!target->is_equipment())
    {
        return {.success = false, .error = "Target is not equipment"};
    }

    // 4. Consume stone (remove from inventory, destroy)
    inv->remove_item(player, stone_id);
    items->destroy_item(stone_id);

    // 5. Increment upgrade level
    target->attribute.upgrade_level += 1;

    return {
        .success = true,
        .error = {},
        .target = target_id,
        .stone = stone_id,
    };
}

auto activate_ability(
    entity_id player,
    item_id id,
    item::item_system* items,
    player::equipment_state* equipment
) -> activate_ability_result
{
    (void)player; // Reserved for future validation

    if (!items || !equipment)
    {
        return {.success = false, .error = "Required subsystems unavailable"};
    }

    // 1. Get item
    auto* itm = items->get_item(id);
    if (!itm)
    {
        return {.success = false, .error = "Item not found"};
    }

    // 2. Verify item is equipped
    auto slot_opt = equipment->find_slot_for(id);
    if (!slot_opt.has_value())
    {
        return {.success = false, .error = "not_equipped"};
    }

    // 3. Look up template to check special_effect
    auto* registry = subsystems().get<item_registry>();
    if (!registry)
    {
        return {.success = false, .error = "Item registry unavailable"};
    }

    auto* tmpl = registry->get(itm->template_id);
    if (!tmpl || tmpl->special_effect == 0)
    {
        return {.success = false, .error = "no_ability"};
    }

    // 4. Return success with ability type and duration (20 min)
    auto ability = static_cast<item::special_ability_type>(tmpl->special_effect);
    constexpr int32_t duration = item::ability_cooldown_seconds * 1000; // 1200s -> 1200000ms

    return {
        .success = true,
        .error = {},
        .ability = ability,
        .duration_ms = duration,
    };
}

auto drop_loot(
    item_id id,
    map_id map,
    int16_t x, int16_t y,
    item::item_system* items,
    world::world_subsystem* world
) -> loot_drop_result
{
    if (!items || !world)
    {
        return {};
    }

    // 1. Clear item owner (loot has no owner until picked up)
    items->clear_owner(id);

    // 2. Add to ground at (map, x, y)
    world::position pos{x, y};
    world->add_ground_item(map, pos, id);

    // 3. Return result
    return {
        .dropped = id,
        .ground_x = x,
        .ground_y = y,
    };
}

auto drop_loot_multi(
    const std::vector<item_id>& item_ids,
    map_id map,
    int16_t center_x, int16_t center_y,
    item::item_system* items,
    world::world_subsystem* world
) -> std::vector<loot_drop_result>
{
    std::vector<loot_drop_result> results;

    if (!items || !world || item_ids.empty())
    {
        return results;
    }

    // 1. Generate 3x3 grid positions around center
    // (-1,-1), (0,-1), (1,-1), (-1,0), (0,0), (1,0), (-1,1), (0,1), (1,1)
    constexpr int16_t offsets[][2] = {
        {-1, -1}, {0, -1}, {1, -1},
        {-1,  0}, {0,  0}, {1,  0},
        {-1,  1}, {0,  1}, {1,  1},
    };
    constexpr size_t grid_size = 9;

    results.reserve(item_ids.size());

    for (size_t i = 0; i < item_ids.size(); ++i)
    {
        // 2. Assign grid position; overflow items stack on center tile
        int16_t gx = center_x;
        int16_t gy = center_y;

        if (i < grid_size)
        {
            gx = static_cast<int16_t>(center_x + offsets[i][0]);
            gy = static_cast<int16_t>(center_y + offsets[i][1]);
        }

        // 3. Clear owner, add to ground
        items->clear_owner(item_ids[i]);
        world::position pos{gx, gy};
        world->add_ground_item(map, pos, item_ids[i]);

        results.push_back({
            .dropped = item_ids[i],
            .ground_x = gx,
            .ground_y = gy,
        });
    }

    // TODO: Add walkability checks for grid positions when map tile data is accessible.

    return results;
}

auto give_item(
    entity_id player,
    item_id template_id,
    int16_t count,
    item::item_system* items,
    inventory::inventory_system* inv
) -> give_result
{
    if (!items || !inv)
    {
        return {.success = false, .error = "Required subsystems unavailable"};
    }

    // 1. Create item from template
    item::item_create_info create_info;
    create_info.template_id = template_id;
    create_info.count = count;
    create_info.full_durability = true;

    auto create_result = items->create_item(create_info);
    if (!create_result.is_ok())
    {
        return {.success = false, .error = "Failed to create item: " + create_result.error()};
    }

    auto created_id = create_result.value();

    // 2. Check inventory not full
    if (inv->is_full(player))
    {
        items->destroy_item(created_id);
        return {.success = false, .error = "Inventory full"};
    }

    // 3. Set owner to player
    items->set_owner(created_id, player);

    // 4. Add to inventory at default position (30, 40)
    int16_t place_x = 30;
    int16_t place_y = 40;

    auto add_result = inv->add_item(player, created_id, count, place_x, place_y);
    if (add_result != inventory::inventory_result::success)
    {
        items->destroy_item(created_id);
        return {.success = false, .error = "Failed to add item to inventory"};
    }

    // 5. Look up final placement info
    int32_t z_order = 0;
    auto* inv_data = inv->get_inventory(player);
    if (inv_data)
    {
        auto* entry = inv_data->get_item(created_id);
        if (entry)
        {
            place_x = entry->pos_x;
            place_y = entry->pos_y;
            z_order = entry->z_order;
        }
    }

    return {
        .success = true,
        .error = {},
        .created = created_id,
        .pos_x = place_x,
        .pos_y = place_y,
        .z_order = z_order,
    };
}

auto damage_equipment(
    entity_id player,
    player::equip_slot slot,
    int16_t amount,
    item::item_system* items,
    player::equipment_state* equipment
) -> damage_equipment_result
{
    (void)player; // Reserved for future validation

    if (!items || !equipment)
    {
        return {.success = false};
    }

    // 1. Get item_id from equipment slot
    auto equipped_opt = equipment->get_equipped(slot);
    if (!equipped_opt.has_value())
    {
        return {.success = false};
    }

    auto equipped_id = equipped_opt.value();

    // 2. Get item from item_system
    auto* itm = items->get_item(equipped_id);
    if (!itm)
    {
        return {.success = false};
    }

    // 3. If indestructible, return success with broke=false
    if (itm->indestructible)
    {
        return {
            .success = true,
            .damaged = equipped_id,
            .new_durability = itm->durability,
            .broke = false,
            .slot = slot,
        };
    }

    // 4. Apply durability damage
    itm->damage_durability(amount);

    // 5. Check if broke
    bool broke = (itm->durability <= 0);

    return {
        .success = true,
        .damaged = equipped_id,
        .new_durability = itm->durability,
        .broke = broke,
        .slot = slot,
    };
}

} // namespace hb::item_ops
