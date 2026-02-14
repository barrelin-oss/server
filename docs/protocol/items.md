# Items

[← Back to Protocol Index](../JSON_PROTOCOL.md)

## Item Attribute Object

Many item messages include an optional `attribute` object describing per-instance enchantments and upgrades. This is only present when the item has non-default attributes (upgraded, enchanted, or custom-made).

```json
{
  "upgrade": 7,
  "main_type": 7,
  "main_value": 1,
  "sub_type": 1,
  "sub_value": 5,
  "custom_made": true,
  "custom_quality": 42
}
```

| Field | Type | Description |
|-------|------|-------------|
| `upgrade` | uint8 | Upgrade level (0-15), shown as "+N" in item name |
| `main_type` | uint8 | Main enchantment type (see enchantment types) |
| `main_value` | uint8 | Main enchantment value (0-15) |
| `sub_type` | uint8 | Sub enchantment type (see sub enchantment types) |
| `sub_value` | uint8 | Sub enchantment value (0-15) |
| `custom_made` | bool | Whether the item was player-crafted |
| `custom_quality` | int8 | Crafting quality (-100 to +100) |

**Main Enchantment Types:** 1=critical_bonus, 2=poison, 3=righteous, 4=spell_on_hit, 5=damage_reduction, 6=light, 7=sharp, 8=fire, 9=ancient, 10=magic_damage, 11=mana_conversion, 12=charge_critical

**Sub Enchantment Types:** 1=physical_resist, 2=attack_rating, 3=defense_rating, 4=hp_recovery, 5=sp_recovery, 6=mp_recovery, 7=magic_resist, 8=physical_absorption, 9=magic_absorption, 10=critical_damage, 11=exp_bonus, 12=gold_bonus

**Notes:**
- Only non-empty attributes are serialized; items with default attributes have no `attribute` key
- Display names include "+N" suffix for upgraded items (e.g., "Iron Sword +3")

---

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
      "inventory_slot": 5,
      "attribute": {"upgrade": 3, "main_type": 7, "main_value": 1}
    }
  }
}
```

| Field | Type | Description |
|-------|------|-------------|
| `success` | bool | Whether pickup succeeded |
| `item_id` | uint32 | ID of item picked up |
| `item_name` | string | Display name of item (includes "+N" for upgraded items) |
| `quantity` | int16 | Stack count of item |
| `inventory_slot` | uint8 | Slot where item was placed |
| `attribute` | object? | Item attributes (see Item Attribute Object above) |

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
    "item_name": "Short Sword +3",
    "count": 1,
    "x": 100,
    "y": 150,
    "attribute": {"upgrade": 3, "sub_type": 1, "sub_value": 5}
  }
}
```

| Field | Type | Description |
|-------|------|-------------|
| `item_id` | uint32 | Unique item instance ID |
| `template_id` | uint32 | Item template ID (for sprite lookup) |
| `item_name` | string | Display name of item (includes "+N" for upgraded items) |
| `count` | int16 | Stack count |
| `x` | int16 | X coordinate on map |
| `y` | int16 | Y coordinate on map |
| `attribute` | object? | Item attributes (see Item Attribute Object above) |

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

## Item Upgrade Messages

### `item_upgrade_request`

Upgrade an item using a Stone of Xelima (weapons) or Stone of Merien (armor).

**Direction:** Client → Server

```json
{
  "type": "item_upgrade_request",
  "seq": 300,
  "data": {
    "item_slot": 5
  }
}
```

| Field | Type | Description |
|-------|------|-------------|
| `item_slot` | int16 | Inventory slot of item to upgrade |

**Notes:**
- Player must have a matching upgrade stone in inventory (Xelima for weapons, Merien for armor)
- Stone is consumed on both success and failure

---

### `item_upgrade_response`

Result of an upgrade attempt.

**Direction:** Server → Client

**Success:**
```json
{
  "type": "item_upgrade_response",
  "seq": 300,
  "data": {
    "success": true,
    "item_slot": 5,
    "new_level": 4
  }
}
```

**Failure:**
```json
{
  "type": "item_upgrade_response",
  "seq": 300,
  "data": {
    "success": false,
    "item_slot": 5,
    "new_level": 3,
    "error": "upgrade_failed"
  }
}
```

| Field | Type | Description |
|-------|------|-------------|
| `success` | bool | Whether upgrade succeeded |
| `item_slot` | int16 | Inventory slot of item |
| `new_level` | uint8 | Current upgrade level after attempt |
| `error` | string? | Error code on failure |

**Error codes:** `no_stone`, `not_equipment`, `max_level`, `wrong_stone_type`, `upgrade_failed`

**Upgrade probability:** Decreases as level increases. Level 0→1 has ~30% chance, level 10+ has ~1% chance. Custom-made items with high quality get a small bonus.

---

## Special Ability Messages

### `activate_ability_request`

Activate the special ability granted by an equipped weapon.

**Direction:** Client → Server

```json
{
  "type": "activate_ability_request",
  "seq": 400,
  "data": {}
}
```

---

### `activate_ability_response`

Result of an activation attempt.

**Direction:** Server → Client

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

| Field | Type | Description |
|-------|------|-------------|
| `success` | bool | Whether activation succeeded |
| `ability_type` | uint8 | Ability type (see special ability types) |
| `cooldown_sec` | int32 | Cooldown duration in seconds (1200 = 20 minutes) |
| `error` | string? | Error code on failure |

**Error codes:** `no_ability`, `on_cooldown`, `already_active`, `not_ready`

---

### `special_ability_status`

Server push notification when special ability state changes.

**Direction:** Server → Client

```json
{
  "type": "special_ability_status",
  "seq": 0,
  "data": {
    "status": "ready",
    "ability_type": 1,
    "cooldown_remaining_sec": 0
  }
}
```

| Field | Type | Description |
|-------|------|-------------|
| `status` | string | `"disabled"`, `"ready"`, `"active"`, `"cooldown"` |
| `ability_type` | uint8 | Ability type (0 when disabled) |
| `cooldown_remaining_sec` | int32 | Remaining cooldown time |

**Special Ability Types:** 1=hp_halve, 2=poison, 3=paralyze, 4=warrior_boost, 5=life_drain

**Notes:**
- Sent when equipping/unequipping SPECABLTY items, on activation, and when cooldown expires
- Attack abilities (1-5) are consumed on the next successful melee hit, then enter 20-minute cooldown
- Defense abilities (50+) remain active for their duration

---
