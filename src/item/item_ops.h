#pragma once

// item_ops.h
// Business logic layer for item pickup and drop operations.
// Orchestrates between item_system, inventory_system, world_subsystem, and equipment_state.

#include "core/types.h"
#include "item/item_ops_types.h"
#include "world/position.h"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace hb::item { class item_system; }
namespace hb::inventory { class inventory_system; }
namespace hb::world { class world_subsystem; }
namespace hb::player { struct equipment_state; }

namespace hb::item_ops
{

// Pick up the top ground item at the player's position and place it in inventory.
//
// Steps:
//   1. Check for ground items at (map, pos)
//   2. Check inventory capacity
//   3. Check weight limit (if enabled)
//   4. Remove item from ground
//   5. Add to inventory at (30, 40) — or last consumable position for consumables
//   6. Set item owner
//   7. Return placement info and weight
auto pickup_item(
    entity_id player,
    map_id map,
    world::position pos,
    item::item_system* items,
    inventory::inventory_system* inv,
    world::world_subsystem* world
) -> pickup_result;

// Drop an item from inventory (or equipment) onto the ground at the player's position.
//
// Steps:
//   1. Validate item exists and is owned by player
//   2. Check droppable flag
//   3. Auto-unequip if equipped
//   4. Remove from inventory
//   5. Clear owner
//   6. Add to ground
//   7. Return drop info
auto drop_item(
    entity_id player,
    item_id id,
    map_id map,
    world::position pos,
    item::item_system* items,
    inventory::inventory_system* inv,
    world::world_subsystem* world,
    player::equipment_state* equipment
) -> drop_result;

// Equip an item from inventory into an equipment slot.
//
// Steps:
//   1. Validate item exists and is owned by player
//   2. Verify item is in player's inventory
//   3. Verify item is equippable (equip_position != none)
//   4. Verify item is not broken (durability > 0 or indestructible)
//   5. If slot occupied, unequip old item first (swap)
//   6. Equip item into slot
//   7. Return success with slot and equipped item_id
//
// NOTE: Does NOT validate slot-item compatibility (twohand/weapon+shield mutual exclusion).
// The caller is responsible for providing a valid slot.
auto equip_item(
    entity_id player,
    item_id id,
    player::equip_slot slot,
    item::item_system* items,
    inventory::inventory_system* inv,
    player::equipment_state* equipment
) -> equip_result;

// Unequip an item from an equipment slot.
// The item stays in inventory (linked model — unequipping just clears the slot reference).
auto unequip_item(
    entity_id player,
    player::equip_slot slot,
    player::equipment_state* equipment
) -> unequip_result;

// Force-unequip an item from an equipment slot with a reason (e.g. item broke, hammer strip).
// Like unequip_item but carries a reason for the forced removal.
auto force_unequip(
    entity_id player,
    player::equip_slot slot,
    force_unequip_reason reason,
    player::equipment_state* equipment
) -> force_unequip_result;

// Buy an item from a shop NPC.
//
// Steps:
//   1. Check player has enough gold
//   2. Check inventory not full
//   3. Check weight limit (template weight from registry)
//   4. Create new item instance from template
//   5. Deduct gold
//   6. Add item to inventory at default position (30, 40)
//   7. Set item owner
//   8. Return success with created item_id, placement, new gold, weight
auto shop_buy(
    entity_id player,
    item_id template_id,
    int32_t buy_price,
    item::item_system* items,
    inventory::inventory_system* inv
) -> shop_buy_result;

// Sell an item to a shop NPC.
//
// Steps:
//   1. Get item and verify ownership
//   2. Verify item is tradeable
//   3. Auto-unequip if equipped
//   4. Remove from inventory
//   5. Calculate sell price from item's price field
//   6. Add gold
//   7. Destroy item instance
//   8. Return success with sold item_id, new gold, weight
auto shop_sell(
    entity_id player,
    item_id id,
    item::item_system* items,
    inventory::inventory_system* inv,
    player::equipment_state* equipment
) -> shop_sell_result;

// Repair an item at a shop NPC.
//
// Steps:
//   1. Get item and verify ownership
//   2. Verify item is damaged (durability < max_durability)
//   3. Verify not indestructible
//   4. Calculate repair cost (fraction of item price based on damage)
//   5. Check player has enough gold
//   6. Deduct gold and repair to full durability
//   7. Return success with repaired item_id, new gold
auto shop_repair(
    entity_id player,
    item_id id,
    item::item_system* items,
    inventory::inventory_system* inv
) -> shop_repair_result;

// Deposit an item from inventory into the bank.
//
// Steps:
//   1. Validate item exists and is owned by player
//   2. Get bank — error if no bank
//   3. If target_page/slot specified: verify slot empty — error if occupied
//   4. If auto-deposit (no target): find_empty_slot() — error if bank full
//   5. If equipped, auto-unequip first
//   6. Remove from inventory
//   7. Set bank slot to item_id
//   8. Return success with page/slot, weight info
auto bank_deposit(
    entity_id player,
    item_id id,
    std::optional<int16_t> target_page,
    std::optional<int16_t> target_slot,
    item::item_system* items,
    inventory::inventory_system* inv,
    player::equipment_state* equipment
) -> bank_deposit_result;

// Withdraw an item from the bank into inventory.
//
// Steps:
//   1. Get bank — error if no bank
//   2. Get slot contents — error if empty
//   3. Check inventory not full — error
//   4. Check weight limit — error
//   5. Clear bank slot
//   6. Add item to inventory at default position (30, 40)
//   7. Return success with placement, weight
auto bank_withdraw(
    entity_id player,
    int16_t page,
    int16_t slot,
    item::item_system* items,
    inventory::inventory_system* inv
) -> bank_withdraw_result;

// Execute a trade between two players.
//
// Steps:
//   1. Get both trade windows — error if either missing
//   2. Verify both locked AND confirmed — error if not
//   3. For each item in A's offer: remove from A's inventory, transfer ownership to B, add to B's inventory
//   4. For each item in B's offer: remove from B's inventory, transfer ownership to A, add to A's inventory
//   5. Transfer gold: remove from A add to B (for A's gold), remove from B add to A (for B's gold)
//   6. Cancel/reset both trade windows
//   7. Return success with both player_side results
auto execute_trade(
    entity_id player_a,
    entity_id player_b,
    item::item_system* items,
    inventory::inventory_system* inv
) -> trade_result;

// Reposition an item within the bank (move or swap).
//
// Steps:
//   1. Get bank — error if no bank
//   2. Get source slot — error if empty
//   3. If dest slot empty: move (clear source, set dest)
//   4. If dest slot occupied: swap both
//   5. Return success
auto bank_reposition(
    entity_id player,
    int16_t from_page, int16_t from_slot,
    int16_t to_page, int16_t to_slot,
    inventory::inventory_system* inv
) -> bank_reposition_result;

// Upgrade an item using an upgrade stone.
//
// Steps:
//   1. Get target item and verify ownership
//   2. Get stone item and verify ownership
//   3. Verify target is equipment
//   4. Consume stone (remove from inventory, destroy)
//   5. Increment target's attribute.upgrade_level by 1
//   6. Return success with target and stone item_ids
//
// NOTE: This is a simplified always-succeeds upgrade. The probability-based
// upgrade system in item_upgrade.h can be wired in later.
auto upgrade_item(
    entity_id player,
    item_id target_id,
    item_id stone_id,
    item::item_system* items,
    inventory::inventory_system* inv
) -> upgrade_result;

// Activate a special ability on an equipped item.
//
// Steps:
//   1. Get item and verify it exists
//   2. Verify item is equipped
//   3. Look up template to check special_effect
//   4. Return success with ability type and duration (20 min = 1200000 ms)
//
// NOTE: Cooldown tracking is not implemented here — just validation that
// the item is equipped and has a special ability.
auto activate_ability(
    entity_id player,
    item_id id,
    item::item_system* items,
    player::equipment_state* equipment
) -> activate_ability_result;

// Drop a single loot item on the ground (from NPC death, despawn, etc.).
//
// Steps:
//   1. Clear item owner (loot has no owner until picked up)
//   2. Add to ground at (map, x, y)
//   3. Return result with item_id and ground position
auto drop_loot(
    item_id id,
    map_id map,
    int16_t x, int16_t y,
    item::item_system* items,
    world::world_subsystem* world
) -> loot_drop_result;

// Drop multiple loot items in a 3x3 grid pattern around a center position (boss drops).
//
// Steps:
//   1. Generate 3x3 grid positions around center
//   2. For each item, assign a grid position (overflow items stack on center tile)
//   3. For each item: clear owner, add to ground
//   4. Return vector of results
//
// TODO: Add walkability checks for grid positions when map tile data is accessible.
auto drop_loot_multi(
    const std::vector<item_id>& item_ids,
    map_id map,
    int16_t center_x, int16_t center_y,
    item::item_system* items,
    world::world_subsystem* world
) -> std::vector<loot_drop_result>;

// Give an item to a player (admin command, quest reward, etc.).
//
// Steps:
//   1. Create item from template
//   2. Check inventory not full — error if full (destroy the created item)
//   3. Set owner to player
//   4. Add to inventory at default position (30, 40)
//   5. Return success with item_id and placement
auto give_item(
    entity_id player,
    item_id template_id,
    int16_t count,
    item::item_system* items,
    inventory::inventory_system* inv
) -> give_result;

// Apply durability damage to an equipped item. May trigger break (durability hits 0).
//
// Steps:
//   1. Get item_id from equipment slot — failure if empty
//   2. Get item from item_system
//   3. If indestructible, return success with broke=false
//   4. Apply durability damage (reduce by amount, clamp to 0)
//   5. If durability hits 0: set broke=true
//   6. Return result with new_durability, broke status, slot
//
// NOTE: Auto-unequip on break is handled by the caller (handler), not here.
auto damage_equipment(
    entity_id player,
    player::equip_slot slot,
    int16_t amount,
    item::item_system* items,
    player::equipment_state* equipment
) -> damage_equipment_result;

} // namespace hb::item_ops
