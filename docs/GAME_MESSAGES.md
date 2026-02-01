# Game Messages Protocol

This document describes all in-game WebSocket messages for movement, combat, and actions.

All messages follow the standard envelope format:
```json
{
  "type": "message_type",
  "seq": 123,
  "data": { ... }
}
```

---

## Table of Contents

1. [Enums and Constants](#enums-and-constants)
2. [Movement Messages](#movement-messages)
   - [player_move_request](#player_move_request)
   - [player_move_response](#player_move_response)
   - [player_run_request](#player_run_request)
   - [player_run_response](#player_run_response)
   - [player_stop_request](#player_stop_request)
   - [player_stop_response](#player_stop_response)
   - [player_position_update](#player_position_update)
3. [Combat Messages](#combat-messages)
   - [player_attack_request](#player_attack_request)
   - [player_attack_response](#player_attack_response)
4. [Magic Messages](#magic-messages)
   - [player_magic_request](#player_magic_request)
   - [player_magic_response](#player_magic_response)
5. [Skill Messages](#skill-messages)
   - [player_skill_request](#player_skill_request)
   - [player_skill_response](#player_skill_response)
6. [Item Messages](#item-messages)
   - [player_pickup_request](#player_pickup_request)
   - [player_pickup_response](#player_pickup_response)
7. [Interaction Messages](#interaction-messages)
   - [player_interact_request](#player_interact_request)
   - [player_interact_response](#player_interact_response)
8. [Chat Messages](#chat-messages)
   - [chat_message](#chat_message)
   - [chat_message_broadcast](#chat_message_broadcast)
9. [Command Messages](#command-messages)
   - [command_request](#command_request)
   - [command_response](#command_response)
10. [Error Handling](#error-handling)

---

## Enums and Constants

### Direction Values

Used for movement and facing direction. Maps to 8-directional movement.

| Value | Name | Delta (dx, dy) |
|-------|------|----------------|
| 0 | none | (0, 0) |
| 1 | north | (0, -1) |
| 2 | north_east | (1, -1) |
| 3 | east | (1, 0) |
| 4 | south_east | (1, 1) |
| 5 | south | (0, 1) |
| 6 | south_west | (-1, 1) |
| 7 | west | (-1, 0) |
| 8 | north_west | (-1, -1) |

**Note:** When sending direction in requests, use values 0-7 (the server masks with `& 7`).

```
    NW(8)  N(1)  NE(2)
       \   |   /
    W(7)--[P]--E(3)
       /   |   \
    SW(6)  S(5)  SE(4)
```

### Attack Type

| Value | Name | Description |
|-------|------|-------------|
| 0 | `regular` | Normal melee attack |
| 1 | `dash` | Dash attack - requires 100% skill, must have exactly 1 tile gap to target |
| 2 | `super` | Super attack - requires 100% skill + charges, enables ranged attack |

Can be sent as integer (0, 1, 2) or string ("regular", "dash", "super").

### Target Type

| Value | Name | Description |
|-------|------|-------------|
| 0 | `none` | No target |
| 1 | `player` | Target is another player |
| 2 | `npc` | Target is an NPC |
| 3 | `ground` | Target is a ground location (for AoE spells) |
| 4 | `item` | Target is a ground item |

Can be sent as integer or string.

---

## Movement Messages

### player_move_request

Walk one tile in the specified direction.

**Direction:** Client → Server

**Data Fields:**

| Field | Type | Required | Description |
|-------|------|----------|-------------|
| `x` | int16 | Yes | Client's current X position (for validation) |
| `y` | int16 | Yes | Client's current Y position (for validation) |
| `direction` | int16 | Yes | Direction to move (0-7) |
| `timestamp` | uint64 | No | Client timestamp in milliseconds |

**Example:**
```json
{
  "type": "player_move_request",
  "seq": 10,
  "data": {
    "x": 150,
    "y": 200,
    "direction": 3,
    "timestamp": 1706620000000
  }
}
```

**Notes:**
- Server validates that client position matches server position (±1 tile tolerance)
- If position mismatch detected, server responds with `position_desync` error
- Target position is calculated server-side from direction

---

### player_move_response

Server response to walk request.

**Direction:** Server → Client

**Data Fields (Success):**

| Field | Type | Description |
|-------|------|-------------|
| `success` | bool | `true` |
| `x` | int16 | New X position |
| `y` | int16 | New Y position |
| `direction` | int16 | Direction facing |

**Data Fields (Failure):**

| Field | Type | Description |
|-------|------|-------------|
| `success` | bool | `false` |
| `x` | int16 | Current X position (unchanged) |
| `y` | int16 | Current Y position (unchanged) |
| `direction` | int16 | Current direction facing |
| `error` | string | Error code |

**Example (Success):**
```json
{
  "type": "player_move_response",
  "seq": 10,
  "data": {
    "success": true,
    "x": 151,
    "y": 200,
    "direction": 3
  }
}
```

**Example (Blocked):**
```json
{
  "type": "player_move_response",
  "seq": 10,
  "data": {
    "success": false,
    "x": 150,
    "y": 200,
    "direction": 3,
    "error": "blocked_terrain"
  }
}
```

**Error Codes:**

| Code | Description |
|------|-------------|
| `blocked_terrain` | Tile is not walkable |
| `blocked_occupied` | Tile occupied by another entity |
| `out_of_bounds` | Target position outside map bounds |
| `cannot_move` | Player has status preventing movement |
| `dead` | Player is dead |
| `position_desync` | Client position doesn't match server |
| `move_failed` | Generic movement failure |

---

### player_run_request

Run two tiles in the specified direction.

**Direction:** Client → Server

**Data Fields:**

| Field | Type | Required | Description |
|-------|------|----------|-------------|
| `x` | int16 | Yes | Client's current X position |
| `y` | int16 | Yes | Client's current Y position |
| `direction` | int16 | Yes | Direction to run (0-7) |
| `timestamp` | uint64 | No | Client timestamp in milliseconds |

**Example:**
```json
{
  "type": "player_run_request",
  "seq": 11,
  "data": {
    "x": 151,
    "y": 200,
    "direction": 3,
    "timestamp": 1706620000100
  }
}
```

**Notes:**
- Running moves 2 tiles in the direction
- May require stamina (implementation pending)
- Server checks intermediate tile for obstacles

---

### player_run_response

Server response to run request.

**Direction:** Server → Client

**Data Fields:** Same as `player_move_response`

**Example (Success):**
```json
{
  "type": "player_run_response",
  "seq": 11,
  "data": {
    "success": true,
    "x": 153,
    "y": 200,
    "direction": 3
  }
}
```

---

### player_stop_request

Signal that player has stopped moving.

**Direction:** Client → Server

**Data Fields:**

| Field | Type | Required | Description |
|-------|------|----------|-------------|
| `x` | int16 | Yes | Position where stopped |
| `y` | int16 | Yes | Position where stopped |
| `timestamp` | uint64 | No | Client timestamp in milliseconds |

**Example:**
```json
{
  "type": "player_stop_request",
  "seq": 12,
  "data": {
    "x": 153,
    "y": 200,
    "timestamp": 1706620000200
  }
}
```

---

### player_stop_response

Server acknowledgment of stop.

**Direction:** Server → Client

**Data Fields:**

| Field | Type | Description |
|-------|------|-------------|
| `success` | bool | Always `true` |
| `x` | int16 | Server-confirmed X position |
| `y` | int16 | Server-confirmed Y position |

**Example:**
```json
{
  "type": "player_stop_response",
  "seq": 12,
  "data": {
    "success": true,
    "x": 153,
    "y": 200
  }
}
```

---

### player_position_update

Broadcast to nearby players when someone moves.

**Direction:** Server → Client (broadcast, no seq matching)

**Data Fields:**

| Field | Type | Description |
|-------|------|-------------|
| `entity_id` | uint32 | ID of the entity that moved |
| `x` | int16 | New X position |
| `y` | int16 | New Y position |
| `direction` | int16 | Direction facing |
| `is_running` | bool | `true` if running, `false` if walking |

**Example:**
```json
{
  "type": "player_position_update",
  "seq": 0,
  "data": {
    "entity_id": 1234,
    "x": 155,
    "y": 200,
    "direction": 3,
    "is_running": false
  }
}
```

**Notes:**
- Sent to all players within 20-tile visibility radius
- `seq` is always 0 for broadcasts
- Use `is_running` to animate appropriately

---

## Combat Messages

### player_attack_request

Request to attack a target.

**Direction:** Client → Server

**Data Fields:**

| Field | Type | Required | Description |
|-------|------|----------|-------------|
| `x` | int16 | Yes | Client's current X position |
| `y` | int16 | Yes | Client's current Y position |
| `direction` | int16 | No | Direction facing (defaults to current) |
| `attack_type` | int/string | No | Attack type: 0/`regular`, 1/`dash`, 2/`super` |
| `target_type` | int/string | No | Target type: 1/`player`, 2/`npc` |
| `target_id` | uint32 | No | Entity ID of the target |
| `timestamp` | uint64 | No | Client timestamp in milliseconds |

**Example (Regular Attack):**
```json
{
  "type": "player_attack_request",
  "seq": 20,
  "data": {
    "x": 150,
    "y": 200,
    "direction": 3,
    "attack_type": "regular",
    "target_type": "npc",
    "target_id": 5001,
    "timestamp": 1706620001000
  }
}
```

**Example (Dash Attack):**
```json
{
  "type": "player_attack_request",
  "seq": 21,
  "data": {
    "x": 150,
    "y": 200,
    "direction": 3,
    "attack_type": "dash",
    "target_type": "player",
    "target_id": 1002,
    "timestamp": 1706620001500
  }
}
```

**Example (Super Attack):**
```json
{
  "type": "player_attack_request",
  "seq": 22,
  "data": {
    "x": 150,
    "y": 200,
    "direction": 3,
    "attack_type": "super",
    "target_type": "npc",
    "target_id": 5002,
    "timestamp": 1706620002000
  }
}
```

**Attack Type Requirements:**

| Type | Requirements |
|------|--------------|
| `regular` | None - standard melee attack |
| `dash` | 100% weapon skill, exactly 1 tile gap between attacker and target |
| `super` | 100% weapon skill, available super attack charges |

---

### player_attack_response

Server response to attack request.

**Direction:** Server → Client

**Data Fields (Success):**

| Field | Type | Description |
|-------|------|-------------|
| `success` | bool | `true` |
| `result` | object | Attack result details |

**Result Object Fields:**

| Field | Type | Description |
|-------|------|-------------|
| `hit` | bool | Whether attack hit the target |
| `critical` | bool | Whether it was a critical hit |
| `damage` | int32 | Damage dealt (0 if miss) |
| `target_id` | uint32 | ID of target attacked |
| `target_hp` | int16 | Target's remaining HP |
| `target_hp_max` | int16 | Target's maximum HP |
| `attacker_x` | int16 | Attacker's confirmed X position |
| `attacker_y` | int16 | Attacker's confirmed Y position |

**Data Fields (Failure):**

| Field | Type | Description |
|-------|------|-------------|
| `success` | bool | `false` |
| `result` | object | Partial result (position info) |
| `error` | string | Error code |

**Example (Hit):**
```json
{
  "type": "player_attack_response",
  "seq": 20,
  "data": {
    "success": true,
    "result": {
      "hit": true,
      "critical": false,
      "damage": 45,
      "target_id": 5001,
      "target_hp": 255,
      "target_hp_max": 300,
      "attacker_x": 150,
      "attacker_y": 200
    }
  }
}
```

**Example (Miss):**
```json
{
  "type": "player_attack_response",
  "seq": 20,
  "data": {
    "success": true,
    "result": {
      "hit": false,
      "critical": false,
      "damage": 0,
      "target_id": 5001,
      "target_hp": 300,
      "target_hp_max": 300,
      "attacker_x": 150,
      "attacker_y": 200
    }
  }
}
```

**Example (Critical Hit):**
```json
{
  "type": "player_attack_response",
  "seq": 21,
  "data": {
    "success": true,
    "result": {
      "hit": true,
      "critical": true,
      "damage": 120,
      "target_id": 1002,
      "target_hp": 80,
      "target_hp_max": 500,
      "attacker_x": 150,
      "attacker_y": 200
    }
  }
}
```

**Error Codes:**

| Code | Description |
|------|-------------|
| `invalid_target` | Target doesn't exist or is invalid |
| `out_of_range` | Target is too far away |
| `cannot_attack` | Player has status preventing attacks |
| `dead` | Player is dead |
| `cooldown` | Attack is on cooldown |
| `no_charges` | Super attack requires charges |
| `skill_too_low` | Dash/super requires 100% skill |
| `invalid_distance` | Dash requires exactly 1 tile gap |
| `not_implemented` | Feature not yet implemented |

---

## Magic Messages

### player_magic_request

Request to cast a spell.

**Direction:** Client → Server

**Data Fields:**

| Field | Type | Required | Description |
|-------|------|----------|-------------|
| `x` | int16 | Yes | Client's current X position |
| `y` | int16 | Yes | Client's current Y position |
| `direction` | int16 | No | Direction facing |
| `spell_id` | uint32 | Yes | ID of spell to cast |
| `target_type` | int/string | No | Target type |
| `target_id` | uint32 | No | Target entity ID (for single-target spells) |
| `target_x` | int16 | No | Target X position (for ground-targeted spells) |
| `target_y` | int16 | No | Target Y position (for ground-targeted spells) |
| `timestamp` | uint64 | No | Client timestamp in milliseconds |

**Example (Single Target Spell):**
```json
{
  "type": "player_magic_request",
  "seq": 30,
  "data": {
    "x": 150,
    "y": 200,
    "direction": 3,
    "spell_id": 10,
    "target_type": "npc",
    "target_id": 5001,
    "timestamp": 1706620003000
  }
}
```

**Example (Ground Target AoE):**
```json
{
  "type": "player_magic_request",
  "seq": 31,
  "data": {
    "x": 150,
    "y": 200,
    "direction": 3,
    "spell_id": 70,
    "target_type": "ground",
    "target_x": 155,
    "target_y": 205,
    "timestamp": 1706620003500
  }
}
```

**Example (Self-Target Buff):**
```json
{
  "type": "player_magic_request",
  "seq": 32,
  "data": {
    "x": 150,
    "y": 200,
    "spell_id": 5,
    "target_type": "none",
    "timestamp": 1706620004000
  }
}
```

---

### player_magic_response

Server response to magic request.

**Direction:** Server → Client

**Data Fields (Success):**

| Field | Type | Description |
|-------|------|-------------|
| `success` | bool | `true` |
| `result` | object | Magic result details |

**Result Object Fields:**

| Field | Type | Description |
|-------|------|-------------|
| `success` | bool | Whether spell was cast successfully |
| `spell_id` | uint32 | ID of spell cast |
| `mana_cost` | int32 | MP consumed |
| `damage` | int32 | Damage dealt (if damage spell) |
| `heal` | int32 | HP healed (if heal spell) |
| `target_id` | uint32 | Target entity ID (if targeted) |
| `caster_mp` | int16 | Caster's remaining MP |

**Example (Damage Spell):**
```json
{
  "type": "player_magic_response",
  "seq": 30,
  "data": {
    "success": true,
    "result": {
      "success": true,
      "spell_id": 10,
      "mana_cost": 15,
      "damage": 80,
      "heal": 0,
      "target_id": 5001,
      "caster_mp": 185
    }
  }
}
```

**Example (Heal Spell):**
```json
{
  "type": "player_magic_response",
  "seq": 32,
  "data": {
    "success": true,
    "result": {
      "success": true,
      "spell_id": 5,
      "mana_cost": 10,
      "damage": 0,
      "heal": 50,
      "target_id": 0,
      "caster_mp": 190
    }
  }
}
```

**Error Codes:**

| Code | Description |
|------|-------------|
| `insufficient_mp` | Not enough mana |
| `spell_not_learned` | Player hasn't learned this spell |
| `invalid_target` | Invalid target for spell |
| `out_of_range` | Target too far |
| `cooldown` | Spell on cooldown |
| `silenced` | Player is silenced |
| `not_implemented` | Feature not yet implemented |

---

## Skill Messages

### player_skill_request

Request to use a skill.

**Direction:** Client → Server

**Data Fields:**

| Field | Type | Required | Description |
|-------|------|----------|-------------|
| `x` | int16 | Yes | Client's current X position |
| `y` | int16 | Yes | Client's current Y position |
| `direction` | int16 | No | Direction facing |
| `skill_id` | uint32 | Yes | ID of skill to use |
| `target_type` | int/string | No | Target type (if skill requires target) |
| `target_id` | uint32 | No | Target entity ID |
| `timestamp` | uint64 | No | Client timestamp in milliseconds |

**Example:**
```json
{
  "type": "player_skill_request",
  "seq": 40,
  "data": {
    "x": 150,
    "y": 200,
    "direction": 3,
    "skill_id": 3,
    "target_type": "npc",
    "target_id": 5001,
    "timestamp": 1706620005000
  }
}
```

---

### player_skill_response

Server response to skill request.

**Direction:** Server → Client

**Data Fields (Success):**

| Field | Type | Description |
|-------|------|-------------|
| `success` | bool | `true` |
| `result` | object | Skill result details |

**Result Object Fields:**

| Field | Type | Description |
|-------|------|-------------|
| `success` | bool | Whether skill executed successfully |
| `skill_id` | uint32 | ID of skill used |
| `effect_value` | int32 | Skill-specific effect value |
| `target_id` | uint32 | Target entity ID (if targeted) |

**Example:**
```json
{
  "type": "player_skill_response",
  "seq": 40,
  "data": {
    "success": true,
    "result": {
      "success": true,
      "skill_id": 3,
      "effect_value": 25,
      "target_id": 5001
    }
  }
}
```

**Error Codes:**

| Code | Description |
|------|-------------|
| `skill_not_learned` | Player hasn't learned this skill |
| `skill_too_low` | Skill level too low |
| `invalid_target` | Invalid target for skill |
| `cooldown` | Skill on cooldown |
| `not_implemented` | Feature not yet implemented |

---

## Item Messages

### player_pickup_request

Request to pick up an item from the ground.

**Direction:** Client → Server

**Data Fields:**

| Field | Type | Required | Description |
|-------|------|----------|-------------|
| `x` | int16 | Yes | Client's current X position |
| `y` | int16 | Yes | Client's current Y position |
| `item_id` | uint32 | Yes | Ground item ID to pick up |
| `timestamp` | uint64 | No | Client timestamp in milliseconds |

**Example:**
```json
{
  "type": "player_pickup_request",
  "seq": 50,
  "data": {
    "x": 150,
    "y": 200,
    "item_id": 99001,
    "timestamp": 1706620006000
  }
}
```

---

### player_pickup_response

Server response to pickup request.

**Direction:** Server → Client

**Data Fields (Success):**

| Field | Type | Description |
|-------|------|-------------|
| `success` | bool | `true` |
| `result` | object | Pickup result details |

**Result Object Fields:**

| Field | Type | Description |
|-------|------|-------------|
| `success` | bool | Whether pickup succeeded |
| `item_id` | uint32 | Item template ID |
| `item_name` | string | Display name of item |
| `quantity` | int16 | Number of items picked up |
| `inventory_slot` | uint8 | Inventory slot where item was placed |

**Example:**
```json
{
  "type": "player_pickup_response",
  "seq": 50,
  "data": {
    "success": true,
    "result": {
      "success": true,
      "item_id": 501,
      "item_name": "Gold Coin",
      "quantity": 100,
      "inventory_slot": 5
    }
  }
}
```

**Error Codes:**

| Code | Description |
|------|-------------|
| `item_not_found` | Item doesn't exist at location |
| `inventory_full` | No room in inventory |
| `too_far` | Item too far from player |
| `cannot_pickup` | Item cannot be picked up |
| `not_implemented` | Feature not yet implemented |

---

## Interaction Messages

### player_interact_request

Request to interact with an NPC or object.

**Direction:** Client → Server

**Data Fields:**

| Field | Type | Required | Description |
|-------|------|----------|-------------|
| `x` | int16 | Yes | Client's current X position |
| `y` | int16 | Yes | Client's current Y position |
| `target_type` | int/string | No | Target type: `npc` or `item` |
| `target_id` | uint32 | Yes | Target NPC or object ID |
| `timestamp` | uint64 | No | Client timestamp in milliseconds |

**Example (NPC Dialog):**
```json
{
  "type": "player_interact_request",
  "seq": 60,
  "data": {
    "x": 150,
    "y": 200,
    "target_type": "npc",
    "target_id": 1001,
    "timestamp": 1706620007000
  }
}
```

---

### player_interact_response

Server response to interaction request.

**Direction:** Server → Client

**Data Fields (Success):**

| Field | Type | Description |
|-------|------|-------------|
| `success` | bool | `true` |
| `result` | object | Interaction result details |

**Result Object Fields:**

| Field | Type | Description |
|-------|------|-------------|
| `success` | bool | Whether interaction succeeded |
| `target_id` | uint32 | Target that was interacted with |
| `interaction_type` | string | Type of interaction |
| `interaction_data` | object | Type-specific data |

**Interaction Types:**

| Type | Description | Data Contents |
|------|-------------|---------------|
| `dialog` | NPC dialog | Dialog text, options |
| `shop` | Shop interface | Shop inventory, prices |
| `bank` | Bank interface | Bank contents |
| `quest` | Quest NPC | Quest info, objectives |
| `teleport` | Teleport NPC | Destinations |

**Example (Shop):**
```json
{
  "type": "player_interact_response",
  "seq": 60,
  "data": {
    "success": true,
    "result": {
      "success": true,
      "target_id": 1001,
      "interaction_type": "shop",
      "interaction_data": {
        "shop_name": "Blacksmith",
        "items": [
          {"id": 100, "name": "Iron Sword", "price": 500},
          {"id": 101, "name": "Steel Sword", "price": 1500}
        ]
      }
    }
  }
}
```

**Example (Dialog):**
```json
{
  "type": "player_interact_response",
  "seq": 60,
  "data": {
    "success": true,
    "result": {
      "success": true,
      "target_id": 1001,
      "interaction_type": "dialog",
      "interaction_data": {
        "npc_name": "Guard Captain",
        "dialog_text": "Welcome to Aresden, traveler.",
        "options": [
          {"id": 1, "text": "Tell me about this city"},
          {"id": 2, "text": "I need a quest"},
          {"id": 3, "text": "Goodbye"}
        ]
      }
    }
  }
}
```

**Error Codes:**

| Code | Description |
|------|-------------|
| `target_not_found` | Target doesn't exist |
| `too_far` | Target too far away |
| `not_interactable` | Target cannot be interacted with |
| `not_implemented` | Feature not yet implemented |

---

## Chat Messages

Chat supports multiple channels with prefix-based routing.

### Chat Channels

| Channel | Prefix | Description |
|---------|--------|-------------|
| `local` | (none) | Nearby players (15-tile range) |
| `shout` | `!` | Server-wide broadcast |
| `guild` | `@` | Guild members only |
| `party` | `$` | Party members only |
| `whisper` | `#` or recipient field | Private message |
| `global` | (explicit only) | All players |
| `trade` | `~` | Trade channel |

### chat_message

Send a chat message. Content can include prefix for channel routing, or use explicit `channel` field.

**Direction:** Client → Server

**Data Fields:**

| Field | Type | Required | Description |
|-------|------|----------|-------------|
| `content` | string | Yes | Message content (may include prefix) |
| `channel` | string | No | Explicit channel override |
| `recipient` | string | No | Recipient name (for whispers) |
| `timestamp` | uint64 | No | Client timestamp in milliseconds |

**Example (Local Chat - default):**
```json
{
  "type": "chat_message",
  "seq": 100,
  "data": {
    "content": "Hello everyone!",
    "timestamp": 1706620010000
  }
}
```

**Example (Shout - prefix):**
```json
{
  "type": "chat_message",
  "seq": 101,
  "data": {
    "content": "!Looking for party!",
    "timestamp": 1706620011000
  }
}
```

**Example (Guild Chat - prefix):**
```json
{
  "type": "chat_message",
  "seq": 102,
  "data": {
    "content": "@Guild meeting at 8pm",
    "timestamp": 1706620012000
  }
}
```

**Example (Party Chat - prefix):**
```json
{
  "type": "chat_message",
  "seq": 103,
  "data": {
    "content": "$Follow me to the boss",
    "timestamp": 1706620013000
  }
}
```

**Example (Whisper - with recipient):**
```json
{
  "type": "chat_message",
  "seq": 104,
  "data": {
    "content": "Hey, want to trade?",
    "channel": "whisper",
    "recipient": "PlayerName",
    "timestamp": 1706620014000
  }
}
```

**Example (Explicit Channel):**
```json
{
  "type": "chat_message",
  "seq": 105,
  "data": {
    "content": "Selling rare sword!",
    "channel": "trade",
    "timestamp": 1706620015000
  }
}
```

**Response (Success):**
```json
{
  "type": "chat_message",
  "seq": 100,
  "data": {
    "success": true
  }
}
```

**Response (Error):**
```json
{
  "type": "chat_message",
  "seq": 102,
  "data": {
    "success": false,
    "error": "blocked"
  }
}
```

**Error Codes:**

| Code | Description |
|------|-------------|
| `empty_message` | Message content is empty |
| `no_recipient` | Whisper requires recipient name |
| `recipient_not_found` | Whisper recipient not online |
| `blocked` | Message blocked (not in guild, blocked by recipient, etc.) |
| `rate_limited` | Sending messages too fast |
| `censored` | Message contained filtered words (message still sent with `*`) |

---

### chat_message_broadcast

Server broadcasts chat messages to recipients.

**Direction:** Server → Client (broadcast, no seq matching)

**Data Fields:**

| Field | Type | Description |
|-------|------|-------------|
| `channel` | string | Channel name |
| `sender_id` | uint32 | Sender player ID (0 for system) |
| `sender_name` | string | Sender display name |
| `content` | string | Message content |
| `timestamp` | string | ISO 8601 timestamp |
| `flags` | array | Optional flags: `emote`, `censored`, `system`, `gm` |
| `recipient_name` | string | For whispers - shows recipient's name |

**Example (Local Chat):**
```json
{
  "type": "chat_message_broadcast",
  "seq": 0,
  "data": {
    "channel": "local",
    "sender_id": 1234,
    "sender_name": "PlayerOne",
    "content": "Hello everyone!",
    "timestamp": "2026-01-31T12:34:56Z"
  }
}
```

**Example (Shout):**
```json
{
  "type": "chat_message_broadcast",
  "seq": 0,
  "data": {
    "channel": "shout",
    "sender_id": 1234,
    "sender_name": "PlayerOne",
    "content": "Looking for party!",
    "timestamp": "2026-01-31T12:35:00Z"
  }
}
```

**Example (Guild):**
```json
{
  "type": "chat_message_broadcast",
  "seq": 0,
  "data": {
    "channel": "guild",
    "sender_id": 1234,
    "sender_name": "PlayerOne",
    "content": "Guild meeting at 8pm",
    "timestamp": "2026-01-31T12:36:00Z"
  }
}
```

**Example (Whisper - received):**
```json
{
  "type": "chat_message_broadcast",
  "seq": 0,
  "data": {
    "channel": "whisper",
    "sender_id": 1234,
    "sender_name": "PlayerOne",
    "content": "Hey, want to trade?",
    "timestamp": "2026-01-31T12:37:00Z",
    "recipient_name": "PlayerTwo"
  }
}
```

**Example (System Message):**
```json
{
  "type": "chat_message_broadcast",
  "seq": 0,
  "data": {
    "channel": "system",
    "sender_id": 0,
    "sender_name": "System",
    "content": "Server will restart in 5 minutes",
    "timestamp": "2026-01-31T12:38:00Z",
    "flags": ["system"]
  }
}
```

**Example (Censored Message):**
```json
{
  "type": "chat_message_broadcast",
  "seq": 0,
  "data": {
    "channel": "local",
    "sender_id": 1234,
    "sender_name": "PlayerOne",
    "content": "What the ****!",
    "timestamp": "2026-01-31T12:39:00Z",
    "flags": ["censored"]
  }
}
```

**Example (GM Message):**
```json
{
  "type": "chat_message_broadcast",
  "seq": 0,
  "data": {
    "channel": "shout",
    "sender_id": 1,
    "sender_name": "[GM] Admin",
    "content": "Please follow the rules",
    "timestamp": "2026-01-31T12:40:00Z",
    "flags": ["gm"]
  }
}
```

---

## Command Messages

Commands are separate from chat - they are structured requests that bypass chat parsing.
This allows clients to send commands with typed parameters rather than parsing `/command arg1 arg2`.

### command_request

Execute a server command.

**Direction:** Client → Server

**Data Fields:**

| Field | Type | Required | Description |
|-------|------|----------|-------------|
| `command` | string | Yes | Command name (without `/`) |
| `args` | array | No | Positional arguments |
| `params` | object | No | Named parameters |
| `timestamp` | uint64 | No | Client timestamp in milliseconds |

**Example (Simple Command):**
```json
{
  "type": "command_request",
  "seq": 200,
  "data": {
    "command": "who",
    "timestamp": 1706620020000
  }
}
```

**Example (Command with Args):**
```json
{
  "type": "command_request",
  "seq": 201,
  "data": {
    "command": "whisper",
    "args": ["PlayerName", "Hello there!"],
    "timestamp": 1706620021000
  }
}
```

**Example (Command with Named Params):**
```json
{
  "type": "command_request",
  "seq": 202,
  "data": {
    "command": "party_invite",
    "params": {
      "player": "PlayerName"
    },
    "timestamp": 1706620022000
  }
}
```

**Example (GM Command):**
```json
{
  "type": "command_request",
  "seq": 203,
  "data": {
    "command": "teleport",
    "args": ["aresden", "100", "150"],
    "timestamp": 1706620023000
  }
}
```

---

### command_response

Server response to a command.

**Direction:** Server → Client

**Data Fields:**

| Field | Type | Description |
|-------|------|-------------|
| `success` | bool | Whether command executed successfully |
| `command` | string | Echo of the command name |
| `message` | string | Human-readable result message |
| `result` | object | Command-specific result data (optional) |

**Example (Success - /who):**
```json
{
  "type": "command_response",
  "seq": 200,
  "data": {
    "success": true,
    "command": "who",
    "message": "42 players online",
    "result": {
      "count": 42
    }
  }
}
```

**Example (Success - /time):**
```json
{
  "type": "command_response",
  "seq": 201,
  "data": {
    "success": true,
    "command": "time",
    "message": "2026-01-31T12:45:00Z",
    "result": {
      "timestamp": "2026-01-31T12:45:00Z"
    }
  }
}
```

**Example (Success - /pos):**
```json
{
  "type": "command_response",
  "seq": 202,
  "data": {
    "success": true,
    "command": "pos",
    "message": "Position: (150, 200)",
    "result": {
      "x": 150,
      "y": 200,
      "map": 1
    }
  }
}
```

**Example (Failure - Unknown Command):**
```json
{
  "type": "command_response",
  "seq": 203,
  "data": {
    "success": false,
    "command": "teleport",
    "message": "Unknown command: teleport"
  }
}
```

**Example (Failure - Permission Denied):**
```json
{
  "type": "command_response",
  "seq": 203,
  "data": {
    "success": false,
    "command": "teleport",
    "message": "Permission denied: requires GM level 2"
  }
}
```

### Built-in Commands

| Command | Args | Description |
|---------|------|-------------|
| `who` / `online` | - | Show online player count |
| `time` | - | Show server time |
| `pos` / `position` | - | Show current position |

More commands will be added as systems are implemented (party, guild, admin, etc.).

---

## Error Handling

### Generic Error Response

When a request cannot be processed, the server may send a generic error:

```json
{
  "type": "error",
  "seq": 123,
  "data": {
    "error_code": "invalid_request",
    "message": "Detailed error message"
  }
}
```

### Common Error Codes

| Code | Description |
|------|-------------|
| `invalid_request` | Malformed request data |
| `invalid_player` | Player not found |
| `not_in_game` | Player not in game state |
| `internal_error` | Server internal error |
| `unknown_message_type` | Unrecognized message type |
| `position_desync` | Client/server position mismatch |

---

## Client Implementation Notes

### Position Validation

The server validates client position on every request:
- Tolerance is ±1 tile from server position
- On mismatch, server responds with `position_desync`
- Client should resync position on desync errors

### Timestamps

Timestamps are optional but recommended:
- Use milliseconds since epoch (Unix time * 1000)
- Server may use for lag compensation
- Server may use for speed hack detection

### Movement Flow

1. Client sends `player_move_request` with current position and direction
2. Server validates position, calculates target, checks obstacles
3. Server responds with new position or error
4. On success, server broadcasts `player_position_update` to nearby players

### Attack Flow

1. Client sends `player_attack_request` with target info
2. Server validates range, line of sight, cooldowns
3. Server calculates hit/miss, damage
4. Server responds with `player_attack_response`
5. Server may broadcast damage effects to nearby players

### Sequence Numbers

- Every request should have a unique `seq` value
- Server echoes `seq` in response for matching
- Broadcasts (like `player_position_update`) have `seq: 0`
