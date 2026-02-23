# Item Protocol Specification v2

**Date:** 2026-02-22
**Status:** Draft
**Design Doc:** [../plans/2026-02-22-item-system-redesign.md](../plans/2026-02-22-item-system-redesign.md)

This document defines the exact JSON wire format for all item-related client-server messages. The client should implement against this spec.

---

## Table of Contents

1. [Universal Item Object](#1-universal-item-object)
2. [Login / Initial State](#2-login--initial-state)
3. [State Update Channels](#3-state-update-channels)
4. [Inventory Management](#4-inventory-management)
5. [Equipment](#5-equipment)
6. [Ground Items](#6-ground-items)
7. [Shops](#7-shops)
8. [Banking](#8-banking)
9. [Trading](#9-trading)
10. [Item Use / Consume](#10-item-use--consume)
11. [Item Upgrade](#11-item-upgrade)
12. [Special Abilities](#12-special-abilities)
13. [Party Loot](#13-party-loot)
14. [Enums](#14-enums)

---

## 1. Universal Item Object

Every item the client receives uses this exact shape, regardless of context (inventory, ground, trade, bank, shop). Fields that don't apply are omitted (not null).

```json
{
  "item_id": 12345,
  "template_id": 100,
  "name": "Barbarian Sword",
  "type": "weapon",
  "equip_pos": "weapon",
  "weapon_type": "sword",
  "rarity": "rare",
  "count": 1,
  "weight": 800,
  "price": 15000,
  "damage_min": 18,
  "damage_max": 66,
  "defense": 0,
  "magic_defense": 0,
  "durability": 85,
  "max_durability": 100,
  "level_req": 30,
  "str_req": 50,
  "dex_req": 20,
  "int_req": 0,
  "mag_req": 0,
  "effects": [
    { "type": "str_bonus", "value": 3 },
    { "type": "hit_bonus", "value": 5 }
  ],
  "attribute": {
    "upgrade_level": 3,
    "main_enchant": { "type": "sharp", "value": 2 },
    "sub_enchant": null,
    "custom_made": false
  },
  "special_ability": "paralyze_on_hit",
  "color": 0,
  "sprite_id": 14,
  "sprite_frame": 0,
  "tradeable": true,
  "droppable": true,
  "two_handed": false
}
```

### Field Reference

| Field | Type | Required | Description |
|-------|------|----------|-------------|
| `item_id` | integer | yes | Server-assigned unique instance ID. Used by client for all item operations. |
| `template_id` | integer | yes | Which item template this is (matches items.yaml). |
| `name` | string | yes | Display name. May include "+N" suffix for upgraded items. |
| `type` | string | yes | Item type enum. See [Enums](#14-enums). |
| `equip_pos` | string | yes | Equipment slot this item fits. `"none"` if not equippable. |
| `weapon_type` | string | no | Present only when `type` is `"weapon"`. See [Enums](#14-enums). |
| `count` | integer | yes | Stack count. Always 1 for non-stackable items. |
| `weight` | integer | yes | Item weight (per unit if stacked). |
| `price` | integer | yes | Base sell price to NPC shops. |
| `rarity` | string | yes | Rarity tier. See [Enums](#14-enums). |
| `damage_min` | integer | yes | Minimum physical damage. 0 for non-weapons. |
| `damage_max` | integer | yes | Maximum physical damage. 0 for non-weapons. |
| `defense` | integer | yes | Physical defense value. 0 for non-armor. |
| `magic_defense` | integer | yes | Magic defense value. |
| `durability` | integer | yes | Current durability. |
| `max_durability` | integer | yes | Maximum durability. 0 = indestructible. |
| `level_req` | integer | yes | Level required to equip. 0 = no requirement. |
| `str_req` | integer | yes | Strength required to equip. |
| `dex_req` | integer | yes | Dexterity required to equip. |
| `int_req` | integer | yes | Intelligence required to equip. |
| `mag_req` | integer | yes | Magic required to equip. |
| `effects` | array | yes | Stat bonus effects. Empty array `[]` if none. See [Effect Object](#effect-object). |
| `attribute` | object | no | Upgrade/enchantment data. Omitted if item has no upgrades. See [Attribute Object](#attribute-object). |
| `special_ability` | string | no | Activatable ability type. Omitted if item has no ability. See [Enums](#14-enums). |
| `color` | integer | yes | Visual color modifier. |
| `sprite_id` | integer | yes | Sprite index within the PAK file for this interface context. |
| `sprite_frame` | integer | yes | Frame within the sprite. |
| `tradeable` | boolean | yes | Whether item can be traded or sold. |
| `droppable` | boolean | yes | Whether item can be dropped on ground. |
| `bound_to` | integer | no | Entity ID this item is bound to. Omitted if not bound. |
| `two_handed` | boolean | yes | Whether equipping uses both weapon and shield slots. |

### Effect Object

```json
{ "type": "str_bonus", "value": 3 }
```

| Field | Type | Description |
|-------|------|-------------|
| `type` | string | Effect type enum. See [Enums](#14-enums). |
| `value` | integer | Bonus value (can be negative). |

Only non-zero effects are included in the array. Maximum 6 effects per item.

### Attribute Object

```json
{
  "upgrade_level": 3,
  "main_enchant": { "type": "sharp", "value": 2 },
  "sub_enchant": { "type": "critical_damage", "value": 5 },
  "custom_made": false
}
```

| Field | Type | Description |
|-------|------|-------------|
| `upgrade_level` | integer | 0-15. Upgrade level (+0 to +15). |
| `main_enchant` | object or null | Main enchantment. `null` if none. |
| `main_enchant.type` | string | Enchantment type. See [Enums](#14-enums). |
| `main_enchant.value` | integer | Enchantment strength (0-15). |
| `sub_enchant` | object or null | Sub enchantment. `null` if none. |
| `sub_enchant.type` | string | Sub enchantment type. See [Enums](#14-enums). |
| `sub_enchant.value` | integer | Sub enchantment strength (0-15). |
| `custom_made` | boolean | Whether this item was player-crafted. |

---

## 2. Login / Initial State

### `inventory_data` (server → client)

Sent once after enter-game. Contains all inventory items, equipment slot assignments, gold, and weight.

```json
{
  "type": "inventory_data",
  "data": {
    "items": [
      {
        "item": { ... },
        "pos_x": 30,
        "pos_y": 40,
        "z_order": 0
      },
      {
        "item": { ... },
        "pos_x": 100,
        "pos_y": 80,
        "z_order": 1
      }
    ],
    "equipment_slots": {
      "weapon": 12345,
      "body": 12350,
      "head": 12351
    },
    "gold": 50000,
    "weight": 3200,
    "max_weight": 5500
  }
}
```

| Field | Type | Description |
|-------|------|-------------|
| `items` | array | All inventory items with position data. |
| `items[].item` | Item | Full universal item object. |
| `items[].pos_x` | integer | X position in inventory UI. |
| `items[].pos_y` | integer | Y position in inventory UI. |
| `items[].z_order` | integer | Layering order (higher = on top). |
| `equipment_slots` | object | Map of slot name → item_id. Only occupied slots present. |
| `gold` | integer | Current gold amount. |
| `weight` | integer | Current carry weight. |
| `max_weight` | integer | Maximum carry weight. |

**Notes:**
- Equipped items appear in BOTH `items` (with their inventory position) and `equipment_slots` (with their slot assignment). The item lives in inventory; the slot references it.
- Bank contents are NOT included. They are lazy-loaded via `bank_open`.
- Shop contents are NOT included. They are lazy-loaded via `shop_open`.

---

## 3. State Update Channels

These are the dedicated messages for communicating state changes. Action results (`*_result` messages) are acknowledgments only — they contain `success` and nothing else. All actual state changes arrive through these channels.

### `inventory_item_add` (server → client)

A new item was added to the player's inventory.

```json
{
  "type": "inventory_item_add",
  "data": {
    "item": { ... },
    "pos_x": 30,
    "pos_y": 40,
    "z_order": 5
  }
}
```

**Triggered by:** pickup, shop buy, bank withdraw, trade receive, quest reward, admin give.

### `inventory_item_update` (server → client)

An existing inventory item was modified. Client should replace its local copy entirely.

```json
{
  "type": "inventory_item_update",
  "data": {
    "item": { ... },
    "pos_x": 30,
    "pos_y": 40,
    "z_order": 5
  }
}
```

**Triggered by:** upgrade, repair, enchantment, any mutation that changes item properties.

### `inventory_item_removed` (server → client)

An item was removed from the player's inventory.

```json
{
  "type": "inventory_item_removed",
  "data": {
    "item_id": 12345
  }
}
```

**Triggered by:** drop, shop sell, bank deposit, trade give, item consumed, item destroyed.

### `inventory_item_delta` (server → client)

Hot-path partial update. Only changed fields are sent. Client should patch its local copy.

```json
{
  "type": "inventory_item_delta",
  "data": {
    "item_id": 12345,
    "count": 47,
    "durability": 82
  }
}
```

| Field | Type | Required | Description |
|-------|------|----------|-------------|
| `item_id` | integer | yes | Which item changed. |
| `count` | integer | no | New stack count. Present only if count changed. |
| `durability` | integer | no | New durability. Present only if durability changed. |

**Triggered by:** arrow consumption during combat, durability damage from combat hits.

### `inventory_gold_update` (server → client)

Player's gold amount changed.

```json
{
  "type": "inventory_gold_update",
  "data": {
    "gold": 48500
  }
}
```

**Triggered by:** shop buy/sell, trade, gold pickup, quest reward, admin action.

### `inventory_weight_update` (server → client)

Player's carry weight changed.

```json
{
  "type": "inventory_weight_update",
  "data": {
    "weight": 3200,
    "max_weight": 5500
  }
}
```

**Triggered by:** any item added/removed from inventory, stat change affecting max weight.

### `force_unequip` (server → client)

Server forced an item to be unequipped. This is NOT a response to a player action.

```json
{
  "type": "force_unequip",
  "data": {
    "slot": "body",
    "reason": "broken"
  }
}
```

| Field | Type | Description |
|-------|------|-------------|
| `slot` | string | Which equipment slot was cleared. |
| `reason` | string | Why: `"broken"`, `"hammer_strip"`, or `"armor_break"`. |

**Client action:** Remove the item_id reference from the equipment slot. The item remains in inventory (now unequipped). If reason is `"broken"`, the item's durability is 0 — client may show a broken indicator.

### `equipment_change` (server → broadcast)

A player's visible equipment changed. Sent to all players who can see the entity.

```json
{
  "type": "equipment_change",
  "data": {
    "entity_id": 1001,
    "slot": "weapon",
    "item": { ... }
  }
}
```

When an item is unequipped, `item` is `null`:

```json
{
  "type": "equipment_change",
  "data": {
    "entity_id": 1001,
    "slot": "weapon",
    "item": null
  }
}
```

**Note:** The receiving client uses this to update the visual appearance of other players. The `item` object contains sprite/color data needed for rendering.

### `ground_item_spawn` (server → broadcast)

An item appeared on the ground. Sent to all players who can see the tile.

```json
{
  "type": "ground_item_spawn",
  "data": {
    "item": { ... },
    "map": "default",
    "x": 150,
    "y": 200
  }
}
```

### `ground_item_removed` (server → broadcast)

An item was removed from the ground (picked up or expired). Sent to all players who can see the tile.

```json
{
  "type": "ground_item_removed",
  "data": {
    "item_id": 12345,
    "map": "default",
    "x": 150,
    "y": 200
  }
}
```

### `bank_slot_update` (server → client)

A bank slot now contains an item.

```json
{
  "type": "bank_slot_update",
  "data": {
    "page": 0,
    "slot": 3,
    "item": { ... }
  }
}
```

### `bank_slot_cleared` (server → client)

A bank slot is now empty.

```json
{
  "type": "bank_slot_cleared",
  "data": {
    "page": 0,
    "slot": 3
  }
}
```

### `ability_activated` (server → broadcast)

A player activated a special ability. Sent to all players who can see the entity, including the activator.

```json
{
  "type": "ability_activated",
  "data": {
    "entity_id": 1001,
    "ability_type": "paralyze_on_hit",
    "duration_ms": 20000
  }
}
```

**Note:** The activator receives this as both acknowledgment and broadcast. No separate `activate_ability_result` on success.

### `ability_expired` (server → broadcast)

A special ability expired. Sent to all players who can see the entity, including the owner.

```json
{
  "type": "ability_expired",
  "data": {
    "entity_id": 1001,
    "ability_type": "paralyze_on_hit"
  }
}
```

---

## 4. Inventory Management

### `inventory_reposition` (client → server)

Player dragged an item to a new position in the inventory UI. No response — server applies the change silently.

```json
{
  "type": "inventory_reposition",
  "data": {
    "item_id": 12345,
    "pos_x": 80,
    "pos_y": 60
  }
}
```

---

## 5. Equipment

### `equip_request` (client → server)

```json
{
  "type": "equip_request",
  "data": {
    "item_id": 12345,
    "slot": "weapon"
  }
}
```

### `equip_result` (server → client)

```json
{
  "type": "equip_result",
  "data": {
    "success": true,
    "slot": "weapon"
  }
}
```

On success, the client should:
1. Set `equipment_slots[slot] = item_id` from the request
2. Wait for `equipment_change` broadcast (updates visual for all players)
3. Wait for stat update messages (if stats changed)

If the slot was already occupied (swap), the server sends:
1. `equip_result` with `success: true`
2. `equipment_change` broadcast for the new item

The previously equipped item remains in inventory — only its equipment slot reference is cleared.

### `unequip_request` (client → server)

```json
{
  "type": "unequip_request",
  "data": {
    "slot": "weapon"
  }
}
```

### `unequip_result` (server → client)

```json
{
  "type": "unequip_result",
  "data": {
    "success": true,
    "slot": "weapon"
  }
}
```

On success, the client should:
1. Clear `equipment_slots[slot]`
2. The item remains in inventory (linked model — unequipping doesn't move the item)

---

## 6. Ground Items

### `pickup_request` (client → server)

```json
{
  "type": "pickup_request",
  "data": {
    "map": "default",
    "x": 150,
    "y": 200
  }
}
```

Picks up the top item at the specified tile (LIFO — most recently dropped).

### `pickup_result` (server → client)

```json
{
  "type": "pickup_result",
  "data": {
    "success": true
  }
}
```

On success, the following state updates follow:
1. `ground_item_removed` broadcast (all nearby players including picker)
2. `inventory_item_add` (to picker only)
3. `inventory_weight_update` (to picker only)

### `drop_request` (client → server)

```json
{
  "type": "drop_request",
  "data": {
    "item_id": 12345
  }
}
```

Drops the item on the ground at the player's current position.

### `drop_result` (server → client)

```json
{
  "type": "drop_result",
  "data": {
    "success": true
  }
}
```

On success, the following state updates follow:
1. `inventory_item_removed` (to dropper only)
2. `inventory_weight_update` (to dropper only)
3. If item was equipped: `force_unequip` + `equipment_change` broadcast
4. `ground_item_spawn` broadcast (all nearby players including dropper)

---

## 7. Shops

### `shop_open` (server → client)

Sent when a player interacts with a shop NPC. Contains the full shop catalog.

```json
{
  "type": "shop_open",
  "data": {
    "npc_name": "William the Blacksmith",
    "shop_type": "weapon",
    "items": [
      {
        "item": { ... },
        "buy_price": 15000
      },
      {
        "item": { ... },
        "buy_price": 8000
      }
    ]
  }
}
```

| Field | Type | Description |
|-------|------|-------------|
| `npc_name` | string | Display name of the shop NPC. |
| `shop_type` | string | Shop category for UI filtering. |
| `items` | array | Available items for purchase. |
| `items[].item` | Item | Full universal item object (template-based, no instance ID). |
| `items[].buy_price` | integer | Price to buy this item. |

**Note:** Shop items are template-based catalog entries. They have `item_id: 0` (or omitted) since no instance exists until purchase. The `template_id` is what the client sends back in `shop_buy_request`.

### `shop_buy_request` (client → server)

```json
{
  "type": "shop_buy_request",
  "data": {
    "template_id": 100
  }
}
```

### `shop_buy_result` (server → client)

```json
{
  "type": "shop_buy_result",
  "data": {
    "success": true
  }
}
```

On success:
1. `inventory_item_add` (new item instance with server-assigned `item_id`)
2. `inventory_gold_update`
3. `inventory_weight_update`

### `shop_sell_request` (client → server)

```json
{
  "type": "shop_sell_request",
  "data": {
    "item_id": 12345
  }
}
```

### `shop_sell_result` (server → client)

```json
{
  "type": "shop_sell_result",
  "data": {
    "success": true
  }
}
```

On success:
1. `inventory_item_removed`
2. `inventory_gold_update`
3. `inventory_weight_update`

### `shop_repair_request` (client → server)

```json
{
  "type": "shop_repair_request",
  "data": {
    "item_id": 12345
  }
}
```

### `shop_repair_result` (server → client)

```json
{
  "type": "shop_repair_result",
  "data": {
    "success": true
  }
}
```

On success:
1. `inventory_item_update` (full item resend with restored durability)
2. `inventory_gold_update`

---

## 8. Banking

### `bank_open` (server → client)

Sent when player interacts with a bank NPC.

```json
{
  "type": "bank_open",
  "data": {
    "pages": [
      {
        "page_num": 0,
        "slots": [
          { ... },
          null,
          { ... },
          null,
          null,
          null,
          null,
          null,
          null,
          null,
          null,
          null
        ]
      },
      {
        "page_num": 1,
        "slots": [
          null, null, null, null, null, null,
          null, null, null, null, null, null
        ]
      }
    ],
    "total_pages": 4
  }
}
```

| Field | Type | Description |
|-------|------|-------------|
| `pages` | array | Bank pages with their slot contents. |
| `pages[].page_num` | integer | Page index (0-based). |
| `pages[].slots` | array | Array of Item objects or `null` for empty slots. |
| `total_pages` | integer | Total number of pages available. |

Each slot in `slots` is either a full universal item object or `null` (empty).

### `bank_deposit_request` (client → server)

**Auto-deposit** (server picks slot):
```json
{
  "type": "bank_deposit_request",
  "data": {
    "item_id": 12345
  }
}
```

**Targeted deposit** (client picks slot):
```json
{
  "type": "bank_deposit_request",
  "data": {
    "item_id": 12345,
    "page": 1,
    "slot": 5
  }
}
```

| Field | Type | Required | Description |
|-------|------|----------|-------------|
| `item_id` | integer | yes | Inventory item to deposit. |
| `page` | integer | no | Target bank page. Omit for auto-placement. |
| `slot` | integer | no | Target bank slot. Omit for auto-placement. |

### `bank_deposit_result` (server → client)

```json
{
  "type": "bank_deposit_result",
  "data": {
    "success": true
  }
}
```

On success:
1. `inventory_item_removed` (item leaves inventory)
2. `inventory_weight_update`
3. `bank_slot_update` (item appears in bank at assigned slot)

### `bank_withdraw_request` (client → server)

```json
{
  "type": "bank_withdraw_request",
  "data": {
    "page": 0,
    "slot": 3
  }
}
```

### `bank_withdraw_result` (server → client)

```json
{
  "type": "bank_withdraw_result",
  "data": {
    "success": true
  }
}
```

On success:
1. `bank_slot_cleared` (slot emptied)
2. `inventory_item_add` (item appears in inventory)
3. `inventory_weight_update`

### `bank_reposition_request` (client → server)

Move or swap items within the bank.

```json
{
  "type": "bank_reposition_request",
  "data": {
    "from_page": 0,
    "from_slot": 3,
    "to_page": 1,
    "to_slot": 7
  }
}
```

### `bank_reposition_result` (server → client)

```json
{
  "type": "bank_reposition_result",
  "data": {
    "success": true
  }
}
```

On success:
1. `bank_slot_update` for destination (item moved there)
2. `bank_slot_cleared` for source (if move, not swap)
3. If swapping two items: `bank_slot_update` for both slots

---

## 9. Trading

Three-phase protocol: **offer → lock → confirm → exchange**.

### Phase 0: Initiating a Trade

#### `trade_request` (client → server)

```json
{
  "type": "trade_request",
  "data": {
    "target_entity_id": 1002
  }
}
```

#### `trade_invite` (server → target)

```json
{
  "type": "trade_invite",
  "data": {
    "from_entity_id": 1001,
    "from_name": "PlayerOne"
  }
}
```

#### `trade_accept` (client → server)

```json
{
  "type": "trade_accept",
  "data": {
    "from_entity_id": 1001
  }
}
```

#### `trade_decline` (client → server)

```json
{
  "type": "trade_decline",
  "data": {
    "from_entity_id": 1001
  }
}
```

#### `trade_opened` (server → both players)

Sent when the trade window opens after acceptance.

```json
{
  "type": "trade_opened",
  "data": {
    "partner_entity_id": 1002,
    "partner_name": "PlayerTwo"
  }
}
```

### Phase 1: Offer

Both players can add/remove items and set gold. Each change triggers a `trade_update` to both sides.

#### `trade_add_item` (client → server)

```json
{
  "type": "trade_add_item",
  "data": {
    "item_id": 12345
  }
}
```

#### `trade_remove_item` (client → server)

```json
{
  "type": "trade_remove_item",
  "data": {
    "item_id": 12345
  }
}
```

#### `trade_set_gold` (client → server)

```json
{
  "type": "trade_set_gold",
  "data": {
    "amount": 5000
  }
}
```

#### `trade_update` (server → client)

Sent after any change to either side's offer. Contains the full current state of one side.

```json
{
  "type": "trade_update",
  "data": {
    "side": "mine",
    "items": [
      { ... },
      { ... }
    ],
    "gold": 5000
  }
}
```

```json
{
  "type": "trade_update",
  "data": {
    "side": "theirs",
    "items": [
      { ... }
    ],
    "gold": 0
  }
}
```

| Field | Type | Description |
|-------|------|-------------|
| `side` | string | `"mine"` or `"theirs"` — whose offer this represents. |
| `items` | array | Full universal item objects in the offer. |
| `gold` | integer | Gold amount offered. |

**Note:** Both players receive two `trade_update` messages after each change — one for `"mine"` and one for `"theirs"`.

### Phase 2: Lock

Once satisfied, a player locks their offer. No changes allowed after locking. Both must lock before confirming.

#### `trade_lock` (client → server)

```json
{
  "type": "trade_lock",
  "data": {}
}
```

#### `trade_lock_status` (server → both)

```json
{
  "type": "trade_lock_status",
  "data": {
    "my_locked": true,
    "their_locked": false
  }
}
```

### Phase 3: Confirm

After both sides are locked, both must confirm to execute the trade.

#### `trade_confirm` (client → server)

```json
{
  "type": "trade_confirm",
  "data": {}
}
```

#### `trade_complete` (server → both)

```json
{
  "type": "trade_complete",
  "data": {
    "success": true
  }
}
```

On success, the following state updates follow for each player:
1. `inventory_item_removed` for each item they gave away
2. `inventory_item_add` for each item they received
3. `inventory_gold_update`
4. `inventory_weight_update`

### Cancellation

Trade can be canceled at any phase.

#### `trade_cancel` (client → server)

```json
{
  "type": "trade_cancel",
  "data": {}
}
```

#### `trade_canceled` (server → both)

```json
{
  "type": "trade_canceled",
  "data": {
    "reason": "player_canceled"
  }
}
```

| Reason | Description |
|--------|-------------|
| `player_canceled` | A player explicitly canceled. |
| `out_of_range` | Players moved too far apart. |
| `disconnected` | A player disconnected. |

---

## 10. Item Use / Consume

### `use_item_request` (client → server)

```json
{
  "type": "use_item_request",
  "data": {
    "item_id": 12345
  }
}
```

### `use_item_result` (server → client)

```json
{
  "type": "use_item_result",
  "data": {
    "success": true
  }
}
```

On success:
1. `inventory_item_removed` (if item fully consumed) or `inventory_item_delta` (if stack decremented)
2. Existing stat messages for the effect (NOT part of this message):
   - HP potion → `hp_update`
   - MP potion → `mp_update`
   - SP potion → `sp_update`
   - Recall scroll → teleport message
   - Buff scroll → `buff_applied`

---

## 11. Item Upgrade

### `upgrade_request` (client → server)

```json
{
  "type": "upgrade_request",
  "data": {
    "item_id": 12345,
    "stone_item_id": 12400
  }
}
```

| Field | Type | Description |
|-------|------|-------------|
| `item_id` | integer | The item to upgrade. |
| `stone_item_id` | integer | The upgrade stone (Xelima for weapons, Merien for armor). |

### `upgrade_result` (server → client)

```json
{
  "type": "upgrade_result",
  "data": {
    "success": true
  }
}
```

On success:
1. `inventory_item_removed` (stone consumed)
2. `inventory_item_update` (target item with new upgrade level / attribute)

On failure (`success: false`):
1. `inventory_item_removed` (stone still consumed)
2. No item update (target unchanged)

**Note:** The upgrade stone is always consumed regardless of success/failure.

---

## 12. Special Abilities

### `activate_ability_request` (client → server)

```json
{
  "type": "activate_ability_request",
  "data": {
    "item_id": 12345
  }
}
```

### On Success: `ability_activated` (server → broadcast)

Sent to all players who can see the activator, including the activator. Serves as both acknowledgment and broadcast.

```json
{
  "type": "ability_activated",
  "data": {
    "entity_id": 1001,
    "ability_type": "paralyze_on_hit",
    "duration_ms": 20000
  }
}
```

### On Failure: `activate_ability_failed` (server → client)

```json
{
  "type": "activate_ability_failed",
  "data": {
    "reason": "on_cooldown"
  }
}
```

| Reason | Description |
|--------|-------------|
| `on_cooldown` | 20-minute cooldown hasn't expired. |
| `not_equipped` | Item is not currently equipped. |
| `no_ability` | Item does not have a special ability. |

### `ability_expired` (server → broadcast)

Sent when a special ability's duration ends.

```json
{
  "type": "ability_expired",
  "data": {
    "entity_id": 1001,
    "ability_type": "paralyze_on_hit"
  }
}
```

---

## 13. Party Loot

### Loot Rules

Default: `disabled`. Party leader can change.

| Rule | Behavior |
|------|----------|
| `disabled` | Items drop to ground normally. Loot system not involved. |
| `greed` | Items enter loot storage. All party members roll 1-100. Highest wins. Timer with auto-pass. |
| `master` | Items enter loot storage. Party leader assigns each item to a player. |

Loot rule is **snapshotted at NPC death time**. Both kill loot and despawn loot from the same kill use the snapshotted rule. Changing rules mid-fight only affects future kills.

### `set_loot_rule` (client → server)

```json
{
  "type": "set_loot_rule",
  "data": {
    "rule": "greed"
  }
}
```

### `loot_rule_changed` (server → party)

```json
{
  "type": "loot_rule_changed",
  "data": {
    "rule": "greed",
    "set_by": 1001
  }
}
```

### `loot_available` (server → party)

Items are awaiting distribution.

```json
{
  "type": "loot_available",
  "data": {
    "loot_id": "abc-123",
    "items": [
      { ... },
      { ... }
    ],
    "source_map": "default",
    "source_x": 150,
    "source_y": 200,
    "rule": "greed",
    "timeout_ms": 30000
  }
}
```

| Field | Type | Description |
|-------|------|-------------|
| `loot_id` | string | Unique identifier for this loot batch. |
| `items` | array | Full universal item objects awaiting distribution. |
| `source_map` | string | Map where the NPC died. |
| `source_x` | integer | X coordinate of NPC death. |
| `source_y` | integer | Y coordinate of NPC death. |
| `rule` | string | Loot rule governing this batch (snapshotted at death). |
| `timeout_ms` | integer | How long before unclaimed items expire. |

### `loot_roll` (client → server)

Player rolls for an item (greed mode).

```json
{
  "type": "loot_roll",
  "data": {
    "loot_id": "abc-123",
    "item_id": 12345
  }
}
```

### `loot_roll_result` (server → party)

Shows each roll as it happens.

```json
{
  "type": "loot_roll_result",
  "data": {
    "loot_id": "abc-123",
    "item_id": 12345,
    "entity_id": 1001,
    "player_name": "PlayerOne",
    "roll": 87
  }
}
```

### `loot_pass` (client → server)

Player explicitly passes on an item.

```json
{
  "type": "loot_pass",
  "data": {
    "loot_id": "abc-123",
    "item_id": 12345
  }
}
```

### `loot_assign` (client → server)

Master looter assigns an item to a player.

```json
{
  "type": "loot_assign",
  "data": {
    "loot_id": "abc-123",
    "item_id": 12345,
    "target_entity_id": 1002
  }
}
```

### `loot_awarded` (server → party)

An item was awarded to a player.

```json
{
  "type": "loot_awarded",
  "data": {
    "loot_id": "abc-123",
    "item_id": 12345,
    "winner_entity_id": 1002,
    "winner_name": "PlayerTwo"
  }
}
```

On award, the winner receives:
1. `inventory_item_add` (if inventory has space)
2. `inventory_weight_update`

If winner's inventory is full, item stays in loot storage until space is made.

### `loot_expired` (server → party)

Unclaimed items have expired from loot storage.

```json
{
  "type": "loot_expired",
  "data": {
    "loot_id": "abc-123"
  }
}
```

**Expiration behavior:**
- Awarded but unclaimed (winner's inventory full): item drops at the winner's current position as a ground item
- Unawarded (nobody rolled / master didn't assign): item drops at the party leader's current position

---

## 14. Enums

### Rarity (`rarity`)

| Value | Description |
|-------|-------------|
| `"common"` | Common item |
| `"uncommon"` | Uncommon item |
| `"rare"` | Rare item |
| `"epic"` | Epic item |
| `"legendary"` | Legendary item |
| `"none"` | No rarity assigned |

### Item Type (`type`)

| Value | Description |
|-------|-------------|
| `"weapon"` | Equippable weapon |
| `"armor"` | Equippable armor (body, arms, pants, boots, head) |
| `"accessory"` | Equippable accessory (ring, amulet, cape, angel) |
| `"consumable"` | Usable item that is consumed (potions, scrolls, food) |
| `"material"` | Crafting material |
| `"none"` | Untyped / miscellaneous |

### Equip Position (`equip_pos`)

| Value | Description |
|-------|-------------|
| `"head"` | Helmet / hat |
| `"body"` | Chest armor |
| `"arms"` | Gloves / gauntlets |
| `"pants"` | Leg armor |
| `"boots"` | Footwear |
| `"weapon"` | Right hand (one-handed weapon) |
| `"shield"` | Left hand (shield) |
| `"twohand"` | Both hands (two-handed weapon) |
| `"ring_left"` | Left ring slot |
| `"ring_right"` | Right ring slot |
| `"amulet"` | Necklace / amulet |
| `"cape"` | Back / cape |
| `"angel"` | Angel slot (stat stick) |
| `"full_body"` | Full body armor (mutually exclusive with head + body + arms + pants + boots) |
| `"none"` | Not equippable |

**Mutual exclusion rules:**
- **twohand ↔ weapon + shield**: Equipping a two-handed weapon unequips weapon and shield. Equipping a one-handed weapon or shield unequips twohand.
- **full_body ↔ head + body + arms + pants + boots**: Equipping a full_body item unequips all 5 armor slots. Equipping any armor piece unequips full_body.

### Weapon Type (`weapon_type`)

| Value | Description |
|-------|-------------|
| `"sword"` | Sword |
| `"axe"` | Axe |
| `"hammer"` | Hammer (can strip armor) |
| `"staff"` | Staff |
| `"wand"` | Wand |
| `"bow"` | Bow (ranged) |
| `"dagger"` | Dagger |
| `"fist"` | Fist weapon |
| `"none"` | No weapon type |

### Effect Type (`effects[].type`)

| Value | Description |
|-------|-------------|
| `"str_bonus"` | Strength bonus |
| `"dex_bonus"` | Dexterity bonus |
| `"int_bonus"` | Intelligence bonus |
| `"mag_bonus"` | Magic bonus |
| `"vit_bonus"` | Vitality bonus |
| `"chr_bonus"` | Charisma bonus |
| `"hp_bonus"` | Max HP bonus |
| `"mp_bonus"` | Max MP bonus |
| `"sp_bonus"` | Max SP bonus |
| `"hit_bonus"` | Hit probability bonus |
| `"dodge_bonus"` | Dodge probability bonus |

### Main Enchantment Type (`attribute.main_enchant.type`)

| Value | Description |
|-------|-------------|
| `"critical_bonus"` | Increased critical hit chance |
| `"poison"` | Poison damage on hit |
| `"righteous"` | Bonus damage to undead |
| `"spell_on_hit"` | Chance to cast spell on hit |
| `"damage_reduction"` | Flat damage reduction |
| `"sharp"` | Increased attack dice |
| `"ancient"` | Further increased attack dice |
| `"mana_conversion"` | Convert mana to damage |
| `"charge_critical"` | Critical chance on charge attacks |
| `"light"` | Light radius |
| `"magic_damage"` | Bonus magic damage |

### Sub Enchantment Type (`attribute.sub_enchant.type`)

| Value | Description |
|-------|-------------|
| `"physical_resist"` | Physical damage resistance % |
| `"magic_resist"` | Magic damage resistance % |
| `"attack_rating"` | Attack rating bonus |
| `"defense_rating"` | Defense rating bonus |
| `"hp_recovery"` | HP regeneration bonus |
| `"sp_recovery"` | SP regeneration bonus |
| `"mp_recovery"` | MP regeneration bonus |
| `"physical_absorption"` | Physical damage absorption % |
| `"magic_absorption"` | Magic damage absorption % |
| `"critical_damage"` | Critical hit damage bonus |
| `"exp_bonus"` | Experience gain bonus % |
| `"gold_bonus"` | Gold drop bonus % |

### Special Ability Type (`special_ability`)

| Value | Description |
|-------|-------------|
| `"paralyze_on_hit"` | All attacks paralyze target (Sword of Medusa) |
| `"invincibility"` | Become invincible for duration (Merien Shield) |
| `"weapon_break_on_melee"` | Break attacker's weapon on melee hit (Medusa Chest Plate) |
| `"hp_halve"` | Deal 50% of target's current HP + weapon damage (Xelima weapons) |

*This list is extensible — new ability types will be added over time.*

### Force Unequip Reason

| Value | Description |
|-------|-------------|
| `"broken"` | Item durability reached 0 |
| `"hammer_strip"` | Hammer weapon combat effect stripped the armor |
| `"armor_break"` | Armor Break spell reduced durability to 0 |

### Trade Cancel Reason

| Value | Description |
|-------|-------------|
| `"player_canceled"` | A player explicitly canceled the trade |
| `"out_of_range"` | Players moved too far apart |
| `"disconnected"` | A player disconnected |

### Ability Activation Failure Reason

| Value | Description |
|-------|-------------|
| `"on_cooldown"` | 20-minute cooldown has not expired |
| `"not_equipped"` | The item is not currently equipped |
| `"no_ability"` | The item does not have a special ability |

### Loot Rule

| Value | Description |
|-------|-------------|
| `"disabled"` | Items drop to ground normally (default) |
| `"greed"` | Roll-based distribution |
| `"master"` | Party leader assigns items |
