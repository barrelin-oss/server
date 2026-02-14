# Items

[← Back to Protocol Index](../JSON_PROTOCOL.md)

## Item Pickup Messages

### `player_pickup_request`

Request to pick up an item from the ground.

**Request:**
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
| `x` | int16 | Yes | Current position X (for validation) |
| `y` | int16 | Yes | Current position Y (for validation) |
| `item_id` | uint32 | Yes | Ground item ID to pick up |
| `timestamp` | uint64 | No | Client timestamp in milliseconds |

**Notes:**
- Player must be standing on or near the tile with the item
- Item is only picked up if inventory has space

---

### `player_pickup_response`

Server confirms or rejects the pickup attempt.

**Success Response:**
```json
{
  "type": "player_pickup_response",
  "seq": 200,
  "data": {
    "success": true,
    "result": {
      "success": true,
      "item_id": 456,
      "item_name": "Gold Coin",
      "quantity": 10,
      "inventory_slot": 5
    }
  }
}
```

| Field | Type | Description |
|-------|------|-------------|
| `success` | bool | Whether pickup succeeded |
| `item_id` | uint32 | ID of item picked up |
| `item_name` | string | Display name of item |
| `quantity` | int16 | Stack count of item |
| `inventory_slot` | uint8 | Slot where item was placed |

**Failure Response:**
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

**Possible Errors:**

| Error Code | Description |
|------------|-------------|
| `item_not_found` | No items on ground at position |
| `inventory_full` | Cannot carry more items |
| `invalid_player` | Player not found |
| `internal_error` | Required subsystems unavailable |

---

### `ground_item_spawn`

Broadcast to nearby players when an item appears on the ground (NPC loot drop, or sent on enter game / teleport for existing ground items).

**Server Broadcast:**
```json
{
  "type": "ground_item_spawn",
  "seq": 0,
  "data": {
    "item_id": 456,
    "template_id": 12,
    "item_name": "Short Sword",
    "count": 1,
    "x": 100,
    "y": 150
  }
}
```

| Field | Type | Description |
|-------|------|-------------|
| `item_id` | uint32 | Unique item instance ID |
| `template_id` | uint32 | Item template ID (for sprite lookup) |
| `item_name` | string | Display name of item |
| `count` | int16 | Stack count |
| `x` | int16 | X coordinate on map |
| `y` | int16 | Y coordinate on map |

**Notes:**
- Sent to all players within visibility radius when an NPC drops loot
- Also sent individually to players on enter game and teleport for pre-existing ground items
- Items despawn automatically after 3 minutes (server sends `ground_item_removed` with `picker_id: 0`)

---

### `ground_item_removed`

Broadcast to nearby players when an item is removed from the ground (picked up or despawned).

**Server Broadcast:**
```json
{
  "type": "ground_item_removed",
  "seq": 0,
  "data": {
    "picker_id": 1001,
    "picker_name": "Warrior1",
    "item_id": 456,
    "item_name": "Gold Coin",
    "x": 100,
    "y": 150
  }
}
```

| Field | Type | Description |
|-------|------|-------------|
| `picker_id` | uint32 | Player ID who picked up the item |
| `picker_name` | string | Display name of picker |
| `item_id` | uint32 | ID of item that was removed |
| `item_name` | string | Display name of item |
| `x` | int16 | X coordinate where item was |
| `y` | int16 | Y coordinate where item was |

**Notes:**
- Sent only to players within visibility radius
- When `picker_id` is non-zero: a player picked up the item (they get the pickup response instead of this broadcast)
- When `picker_id` is 0: the item despawned (3-minute ground lifetime expired)
- Clients should remove the item from their ground item cache

---

## Item Usage Messages

### `player_use_item_request`

Use a consumable item from inventory (potions, food, recall scrolls).

**Direction:** Client → Server

```json
{
  "type": "player_use_item_request",
  "seq": 1,
  "data": {
    "slot": 5
  }
}
```

| Field | Type | Description |
|-------|------|-------------|
| `slot` | int16 | Inventory slot index (0-49) |

### `player_use_item_response`

Result of using an item.

**Direction:** Server → Client

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

**Error:**
```json
{
  "type": "player_use_item_response",
  "seq": 1,
  "data": {
    "success": false,
    "error": "potions_disabled"
  }
}
```

| Field | Type | Description |
|-------|------|-------------|
| `success` | bool | Whether the item was used |
| `item_name` | string | Name of the item used (success only) |
| `effect` | string | Effect type: `"hp"`, `"mp"`, `"sp"`, `"hunger"`, `"none"` |
| `amount` | int32 | Amount restored |
| `current` | int32 | Current value after restoration |
| `max` | int32 | Maximum value |
| `error` | string | Error code (failure only) |

**Effect types:**
- `"hp"` — HP potion restored health
- `"mp"` — MP potion restored mana
- `"sp"` — SP potion restored stamina (also cures poison)
- `"hunger"` — Food restored hunger
- `"none"` — Item consumed but no effect (speed hack detected)

**Error codes:**
- `"dead"` — Player is dead
- `"empty_slot"` — Slot is empty
- `"not_consumable"` — Item is not a consumable
- `"unsupported_item_type"` — Consumable type not yet implemented
- `"potions_disabled"` — Map does not allow potions
- `"recall_impossible"` — Map does not allow recall scrolls

**Notes:**
- Recall scrolls do not send a `player_use_item_response`; they trigger `player_teleport` instead
- Potion speed anti-cheat: rapid potion use (avg interval < 180ms) consumes the item but applies no effect
- SP potions also cure the poison status effect (legacy behavior)
- Item stack count decrements by 1; when count reaches 0, slot is cleared

---
