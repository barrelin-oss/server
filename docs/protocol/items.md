# Item System Wire Protocol

> **DEPRECATED:** This document describes the v1 item protocol. See [items-v2.md](items-v2.md) for the current specification.

[← Back to Protocol Index](../JSON_PROTOCOL.md)

This document describes **every** item-related message the server sends and receives, verified against the actual C++ source code. Each flow lists the exact JSON structure, field types, message ordering, and what broadcasts nearby players receive.

**Message envelope:** All messages are wrapped as `{"type": "<type>", "seq": <n>, "data": {...}}`. Broadcasts use `seq: 0`. Request responses echo the client's `seq`.

---

## Table of Contents

1. [Shared Data Objects](#shared-data-objects)
2. [Enter Game — Initial Inventory](#enter-game--initial-inventory)
3. [Item Pickup](#item-pickup)
4. [Item Drop](#item-drop)
5. [Equip Item](#equip-item)
6. [Unequip Item](#unequip-item)
7. [Use Item (Consumables)](#use-item-consumables)
8. [Shop Buy](#shop-buy)
9. [Shop Sell](#shop-sell)
10. [Shop Repair](#shop-repair)
11. [Bank Deposit](#bank-deposit)
12. [Bank Withdraw](#bank-withdraw)
13. [Inventory Reposition](#inventory-reposition)
14. [Item Upgrade](#item-upgrade)
15. [Special Ability](#special-ability)
16. [Manufacturing (Crafting)](#manufacturing-crafting)
17. [Alchemy](#alchemy)
18. [NPC Loot Drops](#npc-loot-drops)
19. [Admin Give/Remove Item](#admin-giveremove-item)
20. [Inventory Item Update / Removed / Weight Update](#inventory-item-update--removed--weight-update)
21. [Bank Slot Update](#bank-slot-update)
22. [Gold Update](#gold-update)
23. [Stat Update](#stat-update)

---

## Shared Data Objects

### Item Attribute Object

Per-instance enchantment data. **Only present when the item has non-default attributes** — items with no upgrades/enchantments omit the `attribute` key entirely.

```json
{
  "upgrade": 3,
  "main_type": 7,
  "main_value": 1,
  "sub_type": 1,
  "sub_value": 5,
  "custom_made": true,
  "custom_quality": 42
}
```

| Key | Type | Condition | Description |
|-----|------|-----------|-------------|
| `upgrade` | uint8 | Only if > 0 | Upgrade level (0-15), shown as "+N" in display name |
| `main_type` | uint8 | Only if main enchantment present | Main enchantment type enum value |
| `main_value` | uint8 | Always with `main_type` | Main enchantment value (0-15) |
| `sub_type` | uint8 | Only if sub enchantment present | Sub enchantment type enum value |
| `sub_value` | uint8 | Always with `sub_type` | Sub enchantment value (0-15) |
| `custom_made` | bool | Only if true | Custom-made (player-crafted) |
| `custom_quality` | int8 | Only if `custom_made` true and quality != 0 | Crafting quality (-100 to +100) |

**Main enchantment types (`main_type`):** 1=critical_bonus, 2=poison, 3=righteous, 4=spell_on_hit, 5=damage_reduction, 6=light, 7=sharp, 8=fire, 9=ancient, 10=magic_damage, 11=mana_conversion, 12=charge_critical

**Sub enchantment types (`sub_type`):** 1=physical_resist, 2=attack_rating, 3=defense_rating, 4=hp_recovery, 5=sp_recovery, 6=mp_recovery, 7=magic_resist, 8=physical_absorption, 9=magic_absorption, 10=critical_damage, 11=exp_bonus, 12=gold_bonus

*Source: `item_attribute::to_json()` in `src/item/item_attribute.cpp`*

---

### Inventory Item Object

Used in `game_state_msg`, `inventory_item_update`, and anywhere a full inventory item is described. Items are addressed by `item_id`, not by slot.

```json
{
  "item_id": 1234,
  "name": "Iron Sword +3",
  "count": 1,
  "durability": 80,
  "max_durability": 100,
  "item_type": 1,
  "equip_pos": 1,
  "sprite": 42,
  "sprite_frame": 0,
  "color": 0,
  "weight": 25,
  "level_limit": 20,
  "pos_x": 150,
  "pos_y": 200,
  "z_order": 0,
  "equipped_slot": 1,
  "attribute": {"upgrade": 3, "main_type": 7, "main_value": 1}
}
```

| Field | Type | Always | Description |
|-------|------|--------|-------------|
| `item_id` | uint32 | Yes | Unique item instance ID (primary identifier) |
| `name` | string | Yes | Display name (includes "+N" for upgraded items) |
| `count` | int16 | Yes | Stack count |
| `durability` | int16 | Yes | Current durability |
| `max_durability` | int16 | Yes | Maximum durability |
| `item_type` | uint8 | Yes | Item type enum (0=sword, 1=mace, 2=axe, etc.) |
| `equip_pos` | uint8 | Yes | Where it can be equipped (0=none, 1=head, 2=body, etc.) |
| `sprite` | int16 | Yes | Ground sprite category for rendering |
| `sprite_frame` | int16 | Yes | Frame within sprite category |
| `color` | int8 | Yes | Color tint index (0=none) |
| `weight` | int16 | Yes | Item weight |
| `level_limit` | int16 | Yes | Minimum level to equip |
| `pos_x` | int16 | Yes | Client-side pixel X position for inventory UI layout |
| `pos_y` | int16 | Yes | Client-side pixel Y position for inventory UI layout |
| `z_order` | int32 | Yes | Layering order for client rendering |
| `equipped_slot` | uint8 | **Optional** | If present, item is equipped in this slot. Absent = not equipped. |
| `attribute` | object | **Optional** | Item attributes (see above). Absent = no attributes. |

**Equipment is unified with inventory.** There is no separate equipment container. Equipped items are normal inventory items with `equipped_slot` set.

*Source: `inventory_item_msg::to_json()` in `src/network/json_protocol.cpp`*

---

### Ground Item Object

Used in `ground_item_spawn` broadcasts.

```json
{
  "item_id": 456,
  "template_id": 12,
  "item_name": "Short Sword +3",
  "count": 1,
  "x": 100,
  "y": 150,
  "ground_sprite": 1,
  "ground_sprite_frame": 0,
  "item_color": 0,
  "reason": "drop",
  "attribute": {"upgrade": 3}
}
```

| Field | Type | Always | Description |
|-------|------|--------|-------------|
| `item_id` | uint32 | Yes | Unique item instance ID |
| `template_id` | uint32 | Yes | Item template ID (for client sprite lookup) |
| `item_name` | string | Yes | Display name (includes "+N" for upgraded items) |
| `count` | int16 | Yes | Stack count |
| `x` | int16 | Yes | Map tile X coordinate |
| `y` | int16 | Yes | Map tile Y coordinate |
| `ground_sprite` | int16 | Yes | Ground sprite category (1=swords, 6=misc, etc.) |
| `ground_sprite_frame` | int16 | Yes | Frame within sprite category |
| `item_color` | int8 | Yes | Color tint index (0=none) |
| `reason` | string | Yes | `"drop"` = new drop (play SFX), `"existing"` = already on ground (silent) |
| `attribute` | object | **Optional** | Item attributes. Absent = no attributes. |

*Source: `ground_item_spawn_data::to_json()` in `src/network/json_protocol.cpp:2167`*

---

## Enter Game — Initial Inventory

When a player enters the game, the `enter_game_response` contains a `game_state_msg` with all inventory items (including equipped items).

**Inventory section of `game_state_msg`:**
```json
{
  "inventory": {
    "items": [
      { "item_id": 100, "name": "Iron Sword +1", "equipped_slot": 1, "z_order": 0, ... },
      { "item_id": 101, "name": "Leather Armor", "equipped_slot": 3, "z_order": 1, ... },
      { "item_id": 102, "name": "RedPotion", "count": 10, "z_order": 2, ... }
    ],
    "gold": 5000
  }
}
```

- Each item is a full [Inventory Item Object](#inventory-item-object)
- Equipped items have `equipped_slot` set; unequipped items do not
- **No separate `equipment` array** — all items are in `inventory.items`
- Bank items are NOT included (sent separately via `player_interact_response` when opening bank)

*Source: `game_state_msg::to_json()` in `src/network/json_protocol.cpp:1475`*
*Source: auth_handlers.cpp enter_game handler*

---

## Item Pickup

### Request: `player_pickup_request`

```json
{
  "type": "player_pickup_request",
  "seq": 200,
  "data": {
    "x": 100,
    "y": 150,
    "item_id": 456,
    "timestamp": 1706500000000
  }
}
```

| Field | Type | Required | Description |
|-------|------|----------|-------------|
| `x` | int16 | Yes | Player's current X position (for validation) |
| `y` | int16 | Yes | Player's current Y position (for validation) |
| `item_id` | uint32 | Yes | Ground item ID to pick up |
| `timestamp` | uint64 | No | Client timestamp in milliseconds |

### On Success — Messages sent to the **picker** (in order):

**1. `player_pickup_response`** (direct, echoes seq)
```json
{
  "type": "player_pickup_response",
  "seq": 200,
  "data": {
    "success": true
  }
}
```
Bare success — no item data in this message. The `result` field is always null/absent.

**2. `inventory_item_update`** (direct, seq=0)
```json
{
  "type": "inventory_item_update",
  "seq": 0,
  "data": { /* full inventory_item_msg */ }
}
```
Contains the full [Inventory Item Object](#inventory-item-object) with all template-derived fields populated.

**3. `inventory_weight_update`** (direct, seq=0)
```json
{
  "type": "inventory_weight_update",
  "seq": 0,
  "data": {
    "current_weight": 350,
    "max_weight": 500
  }
}
```

### On Success — Broadcasts to **nearby players** (in order):

**4. `player_action_broadcast`** (broadcast, seq=0)
```json
{
  "type": "player_action_broadcast",
  "seq": 0,
  "data": {
    "entity_id": 1001,
    "action": "pickup",
    "direction": 0
  }
}
```

**5. `ground_item_removed`** (broadcast, seq=0)
```json
{
  "type": "ground_item_removed",
  "seq": 0,
  "data": {
    "picker_id": 1001,
    "picker_name": "Warrior1",
    "item_id": 456,
    "item_name": "Iron Sword +1",
    "x": 100,
    "y": 150
  }
}
```

**6. `ground_item_spawn`** (broadcast, seq=0, **CONDITIONAL**)

Only sent if another item remains stacked on the same tile after the picked-up item is removed:
```json
{
  "type": "ground_item_spawn",
  "seq": 0,
  "data": { /* ground_item_spawn_data with reason: "existing" */ }
}
```

### On Failure — Single message to picker:

```json
{
  "type": "player_pickup_response",
  "seq": 200,
  "data": {
    "success": false,
    "error": "inventory_full"
  }
}
```

| Error Code | Description |
|------------|-------------|
| `inventory_full` | No space in inventory |
| `item_not_found` | No items on ground at position |
| `dead` | Player is dead |
| `invalid_player` | Player not found |
| `internal_error` | Required subsystems unavailable |

*Source: `handle_player_pickup()` in `src/bridge/handlers/game_handlers_shop.cpp:25-230`*

---

## Item Drop

### Request: `player_drop_item_request`

```json
{
  "type": "player_drop_item_request",
  "seq": 500,
  "data": {
    "item_id": 1234
  }
}
```

### On Success — Messages sent to the **dropper** (in order):

**1. `player_drop_item_response`** (direct, echoes seq)
```json
{
  "type": "player_drop_item_response",
  "seq": 500,
  "data": {
    "success": true
  }
}
```

**2. `inventory_item_removed`** (direct, seq=0)
```json
{
  "type": "inventory_item_removed",
  "seq": 0,
  "data": {
    "item_id": 1234
  }
}
```

**3. `inventory_weight_update`** (direct, seq=0)
```json
{
  "type": "inventory_weight_update",
  "seq": 0,
  "data": {
    "current_weight": 325,
    "max_weight": 500
  }
}
```

### On Success — Broadcast to **nearby players**:

**4. `ground_item_spawn`** (broadcast, seq=0)
```json
{
  "type": "ground_item_spawn",
  "seq": 0,
  "data": {
    "item_id": 789,
    "template_id": 12,
    "item_name": "Iron Sword +1",
    "count": 1,
    "x": 100,
    "y": 150,
    "ground_sprite": 1,
    "ground_sprite_frame": 0,
    "item_color": 0,
    "reason": "drop",
    "attribute": {"upgrade": 1}
  }
}
```

### On Failure:

```json
{
  "type": "player_drop_item_response",
  "seq": 500,
  "data": {
    "success": false,
    "error": "empty_slot"
  }
}
```

| Error Code | Description |
|------------|-------------|
| `dead` | Player is dead |
| `empty_slot` | Inventory slot is empty |
| `no_inventory` | No inventory found |
| `invalid_player` | Player not found |
| `internal_error` | Required subsystems unavailable |

*Source: `handle_player_drop_item()` in `src/bridge/handlers/game_handlers_shop.cpp:1386-1523`*

---

## Equip Item

### Request: `player_equip_request`

```json
{
  "type": "player_equip_request",
  "seq": 100,
  "data": {
    "item_id": 1234,
    "equip_slot": 1
  }
}
```

### On Success — Messages sent to the **player** (in order):

**1. `player_equip_response`** (direct, echoes seq)
```json
{
  "type": "player_equip_response",
  "seq": 100,
  "data": {
    "success": true,
    "slot": 1,
    "item_id": 1234,
    "item_name": "Iron Sword +3",
    "durability": 80,
    "max_durability": 100,
    "attribute": {"upgrade": 3, "main_type": 7, "main_value": 1},
    "swapped_item_id": 999,
    "unequipped_shield_id": 888
  }
}
```

| Field | Type | Condition | Description |
|-------|------|-----------|-------------|
| `success` | bool | Always | Whether equip succeeded |
| `slot` | uint8 | Always | Target equipment slot |
| `item_id` | uint32 | Success | Item instance ID |
| `item_name` | string | Success | Display name |
| `durability` | int16 | Success | Current durability |
| `max_durability` | int16 | Success | Max durability |
| `attribute` | object | Success, if non-empty | Item attributes |
| `swapped_item_id` | uint32 | If slot was occupied | Old item that was unequipped |
| `unequipped_shield_id` | uint32 | If 2H weapon unequipped shield | Shield that was auto-unequipped |

**2. `stat_update`** (direct, seq=0) — see [Stat Update](#stat-update)

### On Success — Broadcast to **nearby players**:

**3. `equipment_change_broadcast`** (broadcast, seq=0)
```json
{
  "type": "equipment_change_broadcast",
  "seq": 0,
  "data": {
    "entity_id": 1001,
    "slot": 1,
    "item_id": 1234,
    "template_id": 100
  }
}
```

### On Failure:

```json
{
  "type": "player_equip_response",
  "seq": 100,
  "data": {
    "success": false,
    "slot": 1,
    "error": "requirements_not_met"
  }
}
```

| Error Code | Description |
|------------|-------------|
| `player_dead` | Player is dead |
| `player_busy` | Player is in a trade |
| `invalid_slot` | Slot is empty or item can't go in target slot |
| `not_equippable` | Item has `equip_position == none` |
| `item_broken` | Item durability is 0 |
| `requirements_not_met` | Level/stat requirements not met |
| `two_handed_weapon_equipped` | Trying to equip shield while 2H weapon is in weapon slot |
| `item_not_found` | Item instance not found |

**No `inventory_item_update` is sent.** The client infers state from the response fields.

*Source: `handle_player_equip()` in `src/bridge/handlers/game_handlers_equipment.cpp`*

---

## Unequip Item

### Request: `player_unequip_request`

```json
{
  "type": "player_unequip_request",
  "seq": 100,
  "data": {
    "equip_slot": 1
  }
}
```

### On Success — Messages sent to the **player** (in order):

**1. `player_unequip_response`** (direct, echoes seq)
```json
{
  "type": "player_unequip_response",
  "seq": 100,
  "data": {
    "success": true,
    "slot": 1,
    "item_id": 1234,
    "item_name": "Iron Sword +3",
    "attribute": {"upgrade": 3}
  }
}
```

| Field | Type | Condition | Description |
|-------|------|-----------|-------------|
| `success` | bool | Always | Whether unequip succeeded |
| `slot` | uint8 | Always | Equipment slot that was cleared |
| `item_id` | uint32 | Success | Item instance ID |
| `item_name` | string | Success | Display name |
| `attribute` | object | Success, if non-empty | Item attributes |

**2. `stat_update`** (direct, seq=0) — see [Stat Update](#stat-update)

### On Success — Broadcast to **nearby players**:

**3. `equipment_change_broadcast`** (broadcast, seq=0)
```json
{
  "type": "equipment_change_broadcast",
  "seq": 0,
  "data": {
    "entity_id": 1001,
    "slot": 1,
    "item_id": 0,
    "template_id": 0
  }
}
```
`item_id: 0` and `template_id: 0` indicate the slot is now empty.

### On Failure:

| Error Code | Description |
|------------|-------------|
| `player_dead` | Player is dead |
| `player_busy` | Player is in a trade |
| `invalid_slot` | Equipment slot number out of range |
| `slot_empty` | Nothing equipped in that slot |

**Unequip can never fail with "inventory full"** — the item is already in inventory and just has its `equipped_slot` flag cleared.

**No `inventory_item_update` is sent.**

*Source: `handle_player_unequip()` in `src/bridge/handlers/game_handlers_equipment.cpp`*

---

## Use Item (Consumables)

### Request: `player_use_item_request`

```json
{
  "type": "player_use_item_request",
  "seq": 1,
  "data": {
    "item_id": 1234
  }
}
```

### Response: `player_use_item_response`

**Success:**
```json
{
  "type": "player_use_item_response",
  "seq": 1,
  "data": {
    "success": true,
    "item_name": "RedPotion",
    "effect": "hp",
    "amount": 25,
    "current": 80,
    "max": 100
  }
}
```

| Field | Type | Condition | Description |
|-------|------|-----------|-------------|
| `success` | bool | Always | Whether the item was used |
| `item_name` | string | Success | Display name of consumed item |
| `effect` | string | Success | `"hp"`, `"mp"`, `"sp"`, `"hunger"`, `"none"` |
| `amount` | int32 | Success | Amount restored (dice roll result) |
| `current` | int32 | Success | Current value after restoration |
| `max` | int32 | Success | Maximum value |
| `error` | string | Failure | Error code |

**Effect types:**
- `"hp"` — HP potion restored health
- `"mp"` — MP potion restored mana
- `"sp"` — SP potion restored stamina (also cures poison)
- `"hunger"` — Food item restored hunger level (0-100 scale)
- `"none"` — Item consumed but no effect applied (potion speed hack detected)

**Error codes:**

| Error Code | Item Consumed? | Description |
|------------|:--------------:|-------------|
| `dead` | No | Player is dead |
| `empty_slot` | No | Inventory slot is empty |
| `not_consumable` | No | Item is not a consumable type |
| `unsupported_item_type` | No | Consumable effect type not implemented |
| `potions_disabled` | **Yes** | Map has potions disabled flag |
| `recall_impossible` | **Yes** | Map has recall disabled flag |

**On success, `inventory_item_update` (count decremented) or `inventory_item_removed` (fully consumed) is sent.**

**No `stat_update` is sent.** The response `current`/`max` fields provide the new vital values.

**Recall scrolls** do not send `player_use_item_response` at all — they trigger `player_teleport` instead.

*Source: `handle_player_use_item()` in `src/bridge/handlers/game_handlers_equipment.cpp:364-595`*

---

## Shop Buy

### Request: `shop_buy_request`

```json
{
  "type": "shop_buy_request",
  "seq": 600,
  "data": {
    "npc_entity_id": 5001,
    "item_template_id": 100,
    "count": 1
  }
}
```

### Response: `shop_buy_response`

**Success:**
```json
{
  "type": "shop_buy_response",
  "seq": 600,
  "data": {
    "success": true,
    "item_name": "Iron Sword",
    "count": 1,
    "price_paid": 500,
    "gold_remaining": 4500
  }
}
```

| Field | Type | Condition | Description |
|-------|------|-----------|-------------|
| `success` | bool | Always | |
| `item_name` | string | Success | Name of purchased item |
| `count` | int16 | Success | Quantity purchased |
| `price_paid` | int32 | Success | Total gold spent |
| `gold_remaining` | int64 | Success | Player's gold after purchase |
| `error` | string | Failure | Error code |

**Error codes:** `not_a_shop`, `hostile_territory`, `item_not_in_shop`, `item_not_found`, `insufficient_gold`, `inventory_full`, `create_failed`, `add_failed`

**`inventory_item_update` and `inventory_weight_update` are sent** with the purchased item. Gold change is also communicated via `gold_remaining` in the response.

*Source: `handle_shop_buy()` in `src/bridge/handlers/game_handlers_shop.cpp:465-601`*

---

## Shop Sell

Two-step flow: quote, then confirm.

### Step 1: Quote — `shop_sell_request` / `shop_sell_response`

**Request:**
```json
{
  "type": "shop_sell_request",
  "seq": 700,
  "data": {
    "npc_entity_id": 5001,
    "item_id": 1234
  }
}
```

**Response:**
```json
{
  "type": "shop_sell_response",
  "seq": 700,
  "data": {
    "success": true,
    "item_name": "Iron Sword",
    "offered_price": 250,
    "durability": 80
  }
}
```

No state changes — this is just a price quote.

### Step 2: Confirm — `shop_sell_confirm_request` / `shop_sell_confirm_response`

**Request:**
```json
{
  "type": "shop_sell_confirm_request",
  "seq": 701,
  "data": {
    "npc_entity_id": 5001,
    "item_id": 1234
  }
}
```

**Response:**
```json
{
  "type": "shop_sell_confirm_response",
  "seq": 701,
  "data": {
    "success": true,
    "gold_received": 250,
    "gold_total": 5250
  }
}
```

| Field | Type | Description |
|-------|------|-------------|
| `gold_received` | int32 | Amount of gold gained |
| `gold_total` | int64 | Player's gold after sale |

On confirm success: item is destroyed, gold is added. **`inventory_item_removed` and `inventory_weight_update` are sent.**

**Error codes (both steps):** `not_a_shop`, `empty_slot`, `item_not_found`, `category_rejected`, `worthless`

*Source: `handle_shop_sell()` / `handle_shop_sell_confirm()` in `src/bridge/handlers/game_handlers_shop.cpp:603-807`*

---

## Shop Repair

Two-step flow: quote, then confirm.

### Step 1: Quote — `shop_repair_request` / `shop_repair_response`

**Request:**
```json
{
  "type": "shop_repair_request",
  "seq": 800,
  "data": {
    "npc_entity_id": 5001,
    "item_id": 1234
  }
}
```

**Response:**
```json
{
  "type": "shop_repair_response",
  "seq": 800,
  "data": {
    "success": true,
    "item_name": "Iron Sword +3",
    "repair_cost": 150,
    "durability": 45
  }
}
```

**Error codes:** `not_repairable`, `cant_repair_type`, `already_repaired`, `not_a_shop`, `empty_slot`

### Step 2: Confirm — `shop_repair_confirm_request` / `shop_repair_confirm_response`

**Request:**
```json
{
  "type": "shop_repair_confirm_request",
  "seq": 801,
  "data": {
    "npc_entity_id": 5001,
    "item_id": 1234
  }
}
```

**Response:**
```json
{
  "type": "shop_repair_confirm_response",
  "seq": 801,
  "data": {
    "success": true,
    "new_durability": 100,
    "gold_spent": 150,
    "gold_remaining": 4850
  }
}
```

| Field | Type | Description |
|-------|------|-------------|
| `new_durability` | int16 | Durability after repair (= max_durability) |
| `gold_spent` | int32 | Gold spent on repair |
| `gold_remaining` | int64 | Player's gold after repair |

On confirm success: item durability set to max, gold deducted. **`inventory_item_update` is sent** with updated durability.

**Error codes:** `already_repaired`, `insufficient_gold`

*Source: `handle_shop_repair()` / `handle_shop_repair_confirm()` in `src/bridge/handlers/game_handlers_shop.cpp:809-980`*

---

## Bank Deposit

### Request: `bank_deposit_request`

```json
{
  "type": "bank_deposit_request",
  "seq": 900,
  "data": {
    "npc_entity_id": 5001,
    "item_id": 1234
  }
}
```

### Response: `bank_deposit_response`

```json
{
  "type": "bank_deposit_response",
  "seq": 900,
  "data": {
    "success": true,
    "item_name": "Iron Sword +1"
  }
}
```

On success: item moves from inventory to bank. **`inventory_item_removed` and `inventory_weight_update` are sent.**

**Error codes:** `not_a_bank`, `empty_slot`, `deposit_failed`, `too_far`, `npc_dead`, `npc_hostile`

*Source: `handle_bank_deposit()` in `src/bridge/handlers/game_handlers_shop.cpp:982-1058`*

---

## Bank Withdraw

### Request: `bank_withdraw_request`

```json
{
  "type": "bank_withdraw_request",
  "seq": 910,
  "data": {
    "npc_entity_id": 5001,
    "bank_slot": 3
  }
}
```

### Response: `bank_withdraw_response`

```json
{
  "type": "bank_withdraw_response",
  "seq": 910,
  "data": {
    "success": true,
    "item_name": "Iron Sword +1"
  }
}
```

On success: item moves from bank to inventory. **`inventory_item_update` and `inventory_weight_update` are sent.** Fails if inventory is full.

**Error codes:** `not_a_bank`, `empty_slot`, `no_bank`, `withdraw_failed`, `inventory_full`

*Source: `handle_bank_withdraw()` in `src/bridge/handlers/game_handlers_shop.cpp:1060-1141`*

---

## Inventory Reposition

### Request: `inventory_reposition_request` (fire-and-forget)

```json
{
  "type": "inventory_reposition_request",
  "seq": 0,
  "data": {
    "item_id": 1234,
    "pos_x": 150,
    "pos_y": 200,
    "z_order": 3
  }
}
```

| Field | Type | Description |
|-------|------|-------------|
| `item_id` | uint32 | Item instance to reposition |
| `pos_x` | int16 | Pixel X position for client UI layout |
| `pos_y` | int16 | Pixel Y position for client UI layout |
| `z_order` | int32 | Layering order for client rendering |

**No response is sent.** The client applies the change optimistically. Invalid requests are silently ignored.

*Source: `handle_inventory_reposition()` in `src/bridge/handlers/game_handlers_shop.cpp:1344-1382`*

---

## Item Upgrade

### Request: `item_upgrade_request`

```json
{
  "type": "item_upgrade_request",
  "seq": 300,
  "data": {
    "item_id": 1234
  }
}
```

### Response: `item_upgrade_response`

**Success:**
```json
{
  "type": "item_upgrade_response",
  "seq": 300,
  "data": {
    "success": true,
    "item_id": 1234,
    "new_level": 4
  }
}
```

**Failure (roll failed):**
```json
{
  "type": "item_upgrade_response",
  "seq": 300,
  "data": {
    "success": false,
    "item_id": 1234,
    "new_level": 3
  }
}
```

| Field | Type | Always | Description |
|-------|------|--------|-------------|
| `success` | bool | Yes | Whether upgrade succeeded |
| `item_id` | uint32 | Yes | Item instance ID |
| `new_level` | uint8 | Yes | Upgrade level after attempt |
| `error` | string | Only validation errors | Error code |

**Error codes:** `dead`, `empty_slot`, `invalid_item`, `not_equipment`, `max_level`, `no_stone`

**Mechanics:**
- Xelima stone upgrades weapons; Merien stone upgrades armor/accessories
- Stone is **always consumed** (success and failure)
- Item is **never destroyed** on failure
- Max upgrade level: 15
- On success: `stat_update` sent (equipment modifiers recalculated)
- **`inventory_item_update` (item changed) and `inventory_item_removed` (stone consumed) are sent**

*Source: `handle_item_upgrade()` in `src/bridge/handlers/game_handlers_equipment.cpp:597-728`*

---

## Special Ability

### Request: `activate_ability_request`

```json
{
  "type": "activate_ability_request",
  "seq": 400,
  "data": {}
}
```

### On Success — Messages sent to the **player** (in order):

**1. `activate_ability_response`** (direct, echoes seq)
```json
{
  "type": "activate_ability_response",
  "seq": 400,
  "data": {
    "success": true,
    "ability_type": 1,
    "cooldown_sec": 1200
  }
}
```

**2. `special_ability_status`** (direct, seq=0)
```json
{
  "type": "special_ability_status",
  "seq": 0,
  "data": {
    "status": "active",
    "ability_type": 1,
    "cooldown_remaining_sec": 0
  }
}
```

| Status | Description |
|--------|-------------|
| `"disabled"` | No ability equipped |
| `"ready"` | Ready to activate |
| `"active"` | Activated, waiting to be consumed in combat |
| `"cooldown"` | On cooldown (20 minutes) |

**Error codes:** `dead`, `no_ability`, `on_cooldown`, `already_active`, `not_ready`

**Ability types:** 1=hp_halve, 2=poison, 3=paralyze, 4=warrior_boost, 5=life_drain. Attack abilities are consumed on next successful melee hit, then enter 20-minute cooldown.

*Source: `handle_activate_ability()` in `src/bridge/handlers/game_handlers_equipment.cpp:730-800`*

---

## Manufacturing (Crafting)

### Request: `manufacture_request`

```json
{
  "type": "manufacture_request",
  "seq": 1000,
  "data": {
    "recipe_index": 3
  }
}
```

### Response: `manufacture_response`

```json
{
  "type": "manufacture_response",
  "seq": 1000,
  "data": {
    "success": true,
    "item_name": "Iron Sword"
  }
}
```

| Field | Type | Condition | Description |
|-------|------|-----------|-------------|
| `success` | bool | Always | Whether crafting succeeded |
| `item_name` | string | If non-empty | Name of created item (success) or attempted item (some failures) |
| `error` | string | Failure | Error code |

**Error codes:** `insufficient_skill`, `insufficient_materials`, `inventory_full`

**Material consumption:**
- `insufficient_skill` / `insufficient_materials` → materials NOT consumed
- `inventory_full` → materials ARE consumed (crafting succeeded but item can't be placed)
- Success → materials consumed
- Roll failure → materials consumed (success=false, no error field)

**`inventory_item_update` (result item) and `inventory_item_removed` (consumed materials) are sent.**

*Source: `make_manufacture_response()` in `src/network/json_protocol.cpp:3010`*

---

## Alchemy

### Request: `alchemy_request`

```json
{
  "type": "alchemy_request",
  "seq": 1100,
  "data": {
    "recipe_id": 5
  }
}
```

### Response: `alchemy_response`

Same structure as manufacturing:
```json
{
  "type": "alchemy_response",
  "seq": 1100,
  "data": {
    "success": true,
    "item_name": "Health Elixir"
  }
}
```

Same error codes and material consumption rules as [Manufacturing](#manufacturing-crafting).

*Source: `make_alchemy_response()` in `src/network/json_protocol.cpp:3029`*

---

## NPC Loot Drops

When an NPC dies and drops loot, these messages are sent in order:

### 1. `entity_death` (broadcast)

```json
{
  "type": "entity_death",
  "seq": 0,
  "data": {
    "victim_id": 5001,
    "killer_id": 1001,
    "x": 100,
    "y": 150,
    "damage": 45
  }
}
```

| Field | Type | Always | Description |
|-------|------|--------|-------------|
| `victim_id` | uint32 | Yes | NPC's entity ID |
| `killer_id` | uint32 | Yes | Killer's entity ID |
| `x` | int16 | Yes | Death location X |
| `y` | int16 | Yes | Death location Y |
| `damage` | int32 | Only if > 0 | Killing blow damage |

### 2. Gold award (NO message)

Gold is added directly to the killer's inventory via `add_gold()`. **No message is sent to the player** for gold loot — the client must poll or receive a `stat_update` with `gold` field.

### 3. `ground_item_spawn` (broadcast, one per dropped item)

Each loot item that drops on the ground generates a [Ground Item Object](#ground-item-object) broadcast with `reason: "drop"`.

### 4. Experience award: `experience_update`

Each player credited with kill XP (the killer, or each eligible party member) receives an [`experience_update`](player.md#experience_update) message with the amount gained, new total, and level. On level-up it also carries the new `max_hp`/`max_mp`/`max_sp` and unspent `stat_points`.

*Source: `game_handlers_npc.cpp` death callback and `game_handlers_combat.cpp` kill reward logic; message sent via `player_system` experience-gain callback in `game_handlers.cpp`*

---

## Admin Give/Remove Item

### Admin Give: `admin_give_item_request` / `admin_give_item_response`

**Request:**
```json
{
  "type": "admin_give_item_request",
  "seq": 2000,
  "data": {
    "player_name": "Warrior1",
    "item_template_id": 100,
    "count": 1,
    "attribute": {"upgrade": 5}
  }
}
```

**Response (to admin only):**
```json
{
  "type": "admin_give_item_response",
  "seq": 2000,
  "data": {
    "success": true,
    "player_name": "Warrior1",
    "item_name": "Iron Sword +5",
    "count": 1
  }
}
```

**The target player receives NO notification.** The item is silently added to their inventory. The player must open their inventory or relog to see it.

### Admin Remove: `admin_remove_item_request` / `admin_remove_item_response`

**Request:**
```json
{
  "type": "admin_remove_item_request",
  "seq": 2001,
  "data": {
    "player_name": "Warrior1",
    "item_id": 1234,
    "count": 0
  }
}
```
`count: 0` means remove entire stack.

**Response (to admin only):**
```json
{
  "type": "admin_remove_item_response",
  "seq": 2001,
  "data": {
    "success": true,
    "player_name": "Warrior1",
    "item_name": "Iron Sword +5"
  }
}
```

**The target player receives NO notification.** The item is silently removed.

*Source: admin_web_handlers.cpp `handle_give_item()` / `handle_remove_item()`*

---

## Inventory Item Update / Removed / Weight Update

Three lightweight notifications for inventory changes. Items are identified by `item_id`, not by slot.

### `inventory_item_update`

Sent when an item is added to inventory or an existing item's data changes (count, durability, attributes, etc.).

**Type:** `inventory_item_update`, **Seq:** 0 (always)

```json
{
  "type": "inventory_item_update",
  "seq": 0,
  "data": { /* full inventory_item_msg */ }
}
```

The `data` payload is a full [Inventory Item Object](#inventory-item-object).

### `inventory_item_removed`

Sent when an item is removed from inventory (dropped, sold, deposited, fully consumed, etc.).

**Type:** `inventory_item_removed`, **Seq:** 0 (always)

```json
{
  "type": "inventory_item_removed",
  "seq": 0,
  "data": {
    "item_id": 1234
  }
}
```

### `inventory_weight_update`

Sent when the player's inventory weight changes (pickup, drop, buy, sell, deposit, withdraw).

**Type:** `inventory_weight_update`, **Seq:** 0 (always)

```json
{
  "type": "inventory_weight_update",
  "seq": 0,
  "data": {
    "current_weight": 350,
    "max_weight": 500
  }
}
```

### Which handlers send which messages:

| Handler | `inventory_item_update` | `inventory_item_removed` | `inventory_weight_update` |
|---------|:-:|:-:|:-:|
| **Pickup** | Yes | No | Yes |
| **Drop** | No | Yes | Yes |
| **Equip** | No (response has data) | No | No |
| **Unequip** | No (response has data) | No | No |
| **Use Item** | Yes (count change) | Yes (consumed) | No |
| **Shop Buy** | Yes | No | Yes |
| **Shop Sell** | No | Yes | Yes |
| **Shop Repair** | Yes (durability) | No | No |
| **Bank Deposit** | No | Yes | Yes |
| **Bank Withdraw** | Yes | No | Yes |
| **Crafting** | Yes (result) | Yes (materials) | No |
| **Item Upgrade** | Yes (stone consumed / item changed) | Yes (stone removed) | No |
| **Admin Give** | Yes | No | No |
| **Admin Remove** | Yes or No | Yes or No | No |

*Source: `make_inventory_item_update()`, `make_inventory_item_removed()`, `make_inventory_weight_update()` in `src/network/json_protocol.cpp`*

---

## Bank Slot Update

Mirrors `inventory_item_update`/`inventory_item_removed` but for the bank container. Sent during bank deposit and withdraw operations.

**Type:** `bank_slot_update`, **Seq:** 0 (always)

**Slot cleared:**
```json
{
  "type": "bank_slot_update",
  "seq": 0,
  "data": {
    "slot": 3,
    "item": null
  }
}
```

**Slot populated:**
```json
{
  "type": "bank_slot_update",
  "seq": 0,
  "data": {
    "slot": 3,
    "item": { /* full inventory_item_msg */ }
  }
}
```

| Handler | When |
|---------|------|
| **Bank Deposit** | Item placed in bank slot |
| **Bank Withdraw** | Bank slot cleared |

*Source: `make_bank_slot_update()` in `src/network/json_protocol.cpp`*

---

## Gold Update

Dedicated notification sent whenever gold changes. Provides the new total, delta, and reason.

**Type:** `gold_update`, **Seq:** 0 (always)

```json
{
  "type": "gold_update",
  "seq": 0,
  "data": {
    "gold": 5000,
    "change": -500,
    "reason": "shop_buy"
  }
}
```

| Field | Type | Description |
|-------|------|-------------|
| `gold` | int64 | New gold total after change |
| `change` | int64 | Amount changed (positive = gained, negative = spent) |
| `reason` | string | Why gold changed |

**Reason values:**

| Reason | Handler | Direction |
|--------|---------|-----------|
| `shop_buy` | Shop Buy | Negative |
| `shop_sell` | Shop Sell Confirm | Positive |
| `shop_repair` | Shop Repair Confirm | Negative |
| `npc_loot` | NPC Kill Loot | Positive |
| `admin` | Admin Gold Operations | Either |
| `trade` | Trade Completion | Either |

Existing `gold_remaining`/`gold_total` fields in shop responses are retained for backward compatibility.

*Source: `make_gold_update()` in `src/network/json_protocol.cpp`*

---

## Stat Update

Sent after equip, unequip, upgrade, and other events that change computed stats.

**Type:** `stat_update`, **Seq:** 0 (always)

```json
{
  "type": "stat_update",
  "seq": 0,
  "data": {
    "max_hp": 500,
    "max_mp": 200,
    "max_sp": 300,
    "attack_power": 150,
    "magic_power": 80,
    "defense": 120,
    "magic_defense": 60,
    "hit_rate": 75,
    "dodge_rate": 30,
    "critical_rate": 10,
    "max_weight": 25000
  }
}
```

**Always-present fields:**

| Field | Type | Description |
|-------|------|-------------|
| `max_hp` | int32 | Maximum HP |
| `max_mp` | int32 | Maximum MP |
| `max_sp` | int32 | Maximum SP |
| `attack_power` | int32 | Physical attack power |
| `magic_power` | int32 | Magic attack power |
| `defense` | int32 | Physical defense |
| `magic_defense` | int32 | Magic defense |
| `hit_rate` | int32 | Hit rate |
| `dodge_rate` | int32 | Dodge rate |
| `critical_rate` | int32 | Critical hit rate |
| `max_weight` | int32 | Max carry weight |

**Optional fields** (only present when relevant, e.g., teleport/respawn):

| Field | Type | Description |
|-------|------|-------------|
| `hp` | int32 | Current HP |
| `mp` | int32 | Current MP |
| `sp` | int32 | Current SP |
| `experience` | int64 | Current XP |
| `gold` | int64 | Current gold |
| `level` | int16 | Current level |
| `pk_count` | int32 | Player kill count |
| `hunger_level` | int32 | Hunger level (0-100) |
| `contribution` | int32 | War contribution |
| `enemy_kill_count` | int32 | Enemy kill count |

*Source: `stat_update_data::to_json()` in `src/network/json_protocol.cpp:2821`*

---

## Summary: Client Responsibilities

The server sends explicit `inventory_item_update`, `inventory_item_removed`, and `inventory_weight_update` messages for most inventory-changing operations. The client should:

1. **On pickup success:** Apply the `inventory_item_update` and `inventory_weight_update` that follow the response
2. **On drop success:** Apply the `inventory_item_removed` and `inventory_weight_update` that follow the response
3. **On equip/unequip success:** Apply changes from the response fields (no `inventory_item_update` sent)
4. **On use item success:** Apply `inventory_item_update` (count decremented) or `inventory_item_removed` (fully consumed)
5. **On shop buy:** Apply `inventory_item_update` and `inventory_weight_update`; update gold from `gold_remaining`
6. **On shop sell confirm:** Apply `inventory_item_removed` and `inventory_weight_update`; update gold from `gold_total`
7. **On shop repair confirm:** Apply `inventory_item_update` (updated durability); update gold from `gold_remaining`
8. **On bank deposit:** Apply `inventory_item_removed` and `inventory_weight_update` + `bank_slot_update`
9. **On bank withdraw:** Apply `inventory_item_update` and `inventory_weight_update` + `bank_slot_update`
10. **On crafting:** Apply `inventory_item_update` (result) and `inventory_item_removed` (consumed materials)
11. **On upgrade:** Apply `inventory_item_update` (upgraded item) and `inventory_item_removed` (consumed stone)
