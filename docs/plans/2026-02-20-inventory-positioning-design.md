# Inventory Positioning & Z-Order Design

## Overview

Refactor inventory from slot-indexed array to item_id-keyed collection with free-form pixel positioning (x, y) and z-order layering. Add weight capacity system using legacy formula. All client-server communication switches from slot indices to item IDs.

## Data Model

### inventory_entry (replaces inventory_slot)

```cpp
struct inventory_entry
{
    item_id item{};
    int16_t count{0};
    int16_t pos_x{0};              // Pixel X within inventory UI
    int16_t pos_y{0};              // Pixel Y within inventory UI
    int32_t z_order{0};            // Higher = on top, compacted on save
    std::optional<uint8_t> equipped_as{};  // If set, item is equipped in this slot
};
```

### inventory class changes

Replace `inventory_slot slots_[50]` with `std::vector<inventory_entry> entries_` plus `int32_t next_z_order_`.

**New API:**
- `get_item(item_id)` → `inventory_entry*` — primary lookup
- `add_item(item_id, count, pos_x, pos_y)` → `inventory_entry*` — adds entry, assigns z_order
- `remove_item(item_id)` → `bool` — removes entry
- `find_equipped(uint8_t equip_slot)` → `inventory_entry*` — find by equip slot
- `items()` → range over entries (no index loops)
- `count()` / `max_items()` — item count cap (50)
- `next_z_order()` → `int32_t` — returns and increments counter
- `compact_z_order()` — renumber 0..N-1 sorted by current z_order
- `total_weight(item_system&)` — sum of item weights * counts
- `max_weight(int str, int level)` — `str * 5 + level * 5` (legacy formula)

**Removed API:**
- `get_slot(int16_t index)` — no more slot indices
- `find_empty_slot()` — replaced by count check
- `move_item(from_slot, to_slot)` — replaced by reposition (update pos_x/pos_y/z_order)
- `swap_slots(a, b)` — no longer needed
- `clear_slot(index)` — replaced by `remove_item(item_id)`

### Equipment stays in inventory

Equipped items live in the same `entries_` vector with `equipped_as` set. Equipping an item only flips the flag — the item retains its x/y position and z_order. The client can render an equipped badge overlay.

### Bank unchanged

Bank storage (`bank_storage`) keeps its slot-based model. The `items.slot` DB column is still used for bank items. For inventory items, `slot` is written as 0 (ignored on load).

## Protocol Changes

### inventory_item_msg (server → client)

- **Remove:** `slot` (uint8_t)
- **Add:** `z_order` (int32_t)
- **Keep:** `item_id`, `name`, `count`, `durability`, `max_durability`, `attribute`, `item_type`, `equip_pos`, `sprite`, `sprite_frame`, `color`, `weight`, `level_limit`, `pos_x`, `pos_y`, `equipped_slot`

### Request messages — all switch inventory_slot → item_id

| Message | Old Field | New Field |
|---------|-----------|-----------|
| `player_equip_request_data` | `int16_t inventory_slot` | `uint32_t item_id` |
| `use_item_request_data` | `int16_t slot` | `uint32_t item_id` |
| `drop_item_request_data` | `int16_t slot` | `uint32_t item_id` |
| `shop_sell_request_data` | `int16_t inventory_slot` | `uint32_t item_id` |
| `shop_sell_confirm_request_data` | `int16_t inventory_slot` | `uint32_t item_id` |
| `shop_repair_request_data` | `int16_t inventory_slot` | `uint32_t item_id` |
| `shop_repair_confirm_request_data` | `int16_t inventory_slot` | `uint32_t item_id` |
| `bank_deposit_request_data` | `int16_t inventory_slot` | `uint32_t item_id` |
| `admin_remove_item_request_data` | `int16_t inventory_slot` | `uint32_t item_id` |

### inventory_reposition_request_data (client → server)

**Old:** `{ from_slot, to_slot, pos_x, pos_y }`
**New:** `{ uint32_t item_id, int16_t pos_x, int16_t pos_y }`

Server updates the entry's pos_x, pos_y, and assigns `next_z_order()` (drag always brings to top).

### Response messages

- `equip_result_msg`: Remove `swapped_to_inv_slot`, `shield_to_inv_slot`. Send full `inventory_item_msg` for each changed item.
- `unequip_result_msg`: Remove `inventory_slot`. Send updated `inventory_item_msg`.
- `make_inventory_slot_update(slot, msg)` → `make_inventory_item_update(msg)` for add/modify
- New: `make_inventory_item_removed(item_id)` for removal

### New message: inventory_weight_update (server → client)

```json
{ "type": "inventory_weight_update", "data": { "current_weight": 350, "max_weight": 500 } }
```

Sent on login (within `inventory_data`) and whenever weight changes.

## Database Changes

### Migration

```sql
-- up
ALTER TABLE items ADD COLUMN z_order SMALLINT NOT NULL DEFAULT 0;

-- down
ALTER TABLE items DROP COLUMN z_order;
```

The `slot` column is kept for bank items. For inventory items (location=0), `slot` is written as 0 and ignored on load.

### Save logic

- Compact z_order before save: sort entries by z_order, renumber 0..N-1
- Write inventory items with `slot=0`, `z_order` from compacted value
- Write bank items with `slot` as before, `z_order=0`

### Load logic

- Inventory items: ignore `slot`, reconstruct entries from `pos_x`, `pos_y`, `z_order`, `equip_slot`
- Bank items: use `slot` as before, ignore `z_order`
- After loading, set `next_z_order_` to max(z_order) + 1

## Handler Refactor

### Pattern change

**Before:**
```cpp
auto* slot = inv->get_slot(data.inventory_slot);
if (!slot || slot->is_empty()) return error;
auto* itm = item_->get_item(slot->item);
```

**After:**
```cpp
auto* entry = inv->get_item(item_id{data.item_id});
if (!entry) return error;
auto* itm = item_->get_item(entry->item);
```

### Affected handlers (10)

1. `handle_player_equip` — `inventory_slot` → `item_id`
2. `handle_player_use_item` — `slot` → `item_id`
3. `handle_player_drop_item` — `slot` → `item_id`
4. `handle_shop_sell` — `inventory_slot` → `item_id`
5. `handle_shop_sell_confirm` — `inventory_slot` → `item_id`
6. `handle_shop_repair` — `inventory_slot` → `item_id`
7. `handle_shop_repair_confirm` — `inventory_slot` → `item_id`
8. `handle_bank_deposit` — `inventory_slot` → `item_id`
9. `handle_inventory_reposition` — complete rewrite (no swap, just update pos + z_order)
10. `handle_remove_item` (admin) — `inventory_slot` → `item_id`

### Crafting snapshot

Currently loops slots to diff before/after. New approach: snapshot item_ids before craft, compare against item_ids after, send `inventory_item_update` for changed/added and `inventory_item_removed` for removed.

### Player system

- `equip_item(player_id, item_id, equip_slot)` — find entry by item_id
- `unequip_item(player_id, equip_slot)` — find entry where `equipped_as == equip_slot`, clear flag
- `rebuild_equipment_cache()` — iterate entries instead of slot indices

## Weight System

### Formula (legacy)

```
max_weight = strength * 5 + level * 5
```

Weight units are from item templates (items.yaml `weight` field), already loaded.

### Enforcement points

- **Reject pickup** if `current_weight + item_weight * count > max_weight`:
  - Loot pickup
  - Shop buy
  - Bank withdraw
  - Craft output
  - Admin give item
- **No enforcement** on:
  - Equip/unequip (item already in inventory)
  - Reposition (just moving within UI)
  - Drop (reduces weight)

### Weight tracking

- `inventory` maintains `current_weight_` cache, updated on add/remove
- Requires `item_system*` reference to look up template weights
- Send `inventory_weight_update` whenever weight changes

## Files Affected

| File | Changes |
|------|---------|
| `src/inventory/inventory.h` | Replace slot array with entry vector, new API |
| `src/network/json_protocol.h` | Update all request/response structs, add z_order |
| `src/network/json_protocol.cpp` | Update serialization, new message builders |
| `src/bridge/handlers/auth_handlers.cpp` | Load/save logic, login inventory build |
| `src/bridge/handlers/game_handlers_equipment.cpp` | Equip, use, upgrade handlers |
| `src/bridge/handlers/game_handlers_shop.cpp` | Shop, bank, drop, reposition handlers |
| `src/bridge/handlers/game_handlers_crafting.cpp` | Crafting snapshot logic |
| `src/bridge/handlers/admin_web_handlers.cpp` | Admin give/remove item |
| `src/player/player_system.h` | equip/unequip signatures |
| `src/player/player_system.cpp` | equip/unequip/rebuild implementation |
| `src/database/schema.sql` | Add z_order column |
| `tests/test_player.cpp` | Update all inventory-related tests |
| `tests/test_json_protocol.cpp` | Update protocol serialization tests |
| New migration file | Add z_order column migration |
