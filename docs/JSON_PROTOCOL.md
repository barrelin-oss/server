# Helbreath WebSocket JSON Protocol

## Overview

This document describes the JSON-based WebSocket protocol used for client-server communication in the Helbreath game server.

### Transport

- **Protocol:** WebSocket (RFC 6455)
- **Default Port:** 2848 (configurable)
- **Message Format:** JSON over WebSocket text frames

### Message Structure

All messages follow a common envelope structure:

```json
{
  "type": "message_type",
  "seq": 123,
  "data": { ... }
}
```

| Field | Type | Description |
|-------|------|-------------|
| `type` | string | Message type identifier |
| `seq` | uint32 | Sequence number for request/response matching |
| `data` | object | Message-specific payload |

### Request/Response Pattern

- Client sends requests with a unique `seq` number
- Server responds with the same `seq` to enable matching
- Server-initiated messages (broadcasts) use `seq: 0`

---

## Implementation Notes

### Confirm/Reject Response Pattern

**IMPORTANT:** When implementing new client request handlers, most actions require both a **confirm** (success) and **reject** (failure) response path. Before implementing any new packet handler, verify with the project lead whether the action needs:

1. **Confirm response** - Sent when action succeeds (e.g., item picked up, attack landed)
2. **Reject response** - Sent when action fails (e.g., inventory full, target out of range)
3. **Broadcast** - Sent to other nearby players to inform them of the action

**Examples of confirm/reject patterns:**

| Action | Needs Confirm | Needs Reject | Needs Broadcast |
|--------|--------------|--------------|-----------------|
| Movement | Yes | Yes (blocked) | Yes (position update) |
| Attack | Yes | Yes (out of range) | Yes (combat broadcast) |
| Pickup | Yes | Yes (inventory full) | Yes (item removed) |
| Chat | Yes | Yes (rate limited) | Yes (message broadcast) |
| Teleport | Yes | Yes (invalid dest) | Yes (despawn/spawn) |

**When to skip reject:**
- Some broadcasts don't need client-side confirmation (e.g., HP updates)
- Pure informational messages from server don't need responses

---

## Message Types

### System Messages

#### `ping`

Client sends to check connection and measure latency.

**Request:**
```json
{
  "type": "ping",
  "seq": 1,
  "data": {}
}
```

**Response:** See `pong`

---

#### `pong`

Server response to `ping`.

**Response:**
```json
{
  "type": "pong",
  "seq": 1,
  "data": {
    "timestamp": 1706500000000
  }
}
```

| Field | Type | Description |
|-------|------|-------------|
| `timestamp` | int64 | Server timestamp in milliseconds since epoch |

---

#### `error`

Generic error response for any failed request.

**Response:**
```json
{
  "type": "error",
  "seq": 5,
  "data": {
    "error_code": "INVALID_REQUEST",
    "message": "Description of what went wrong"
  }
}
```

| Field | Type | Description |
|-------|------|-------------|
| `error_code` | string | Machine-readable error code |
| `message` | string | Human-readable error description |

**Common Error Codes:**

| Code | Description |
|------|-------------|
| `NOT_AUTHENTICATED` | Action requires authentication |
| `NOT_IN_GAME` | Action requires being in-game |
| `INVALID_REQUEST` | Malformed request data |
| `INTERNAL_ERROR` | Server-side error |
| `RATE_LIMITED` | Too many requests |

---

## Authentication Messages

### `login_request`

Authenticate with username and password.

**Request:**
```json
{
  "type": "login_request",
  "seq": 1,
  "data": {
    "username": "player1",
    "password": "secret123"
  }
}
```

| Field | Type | Required | Description |
|-------|------|----------|-------------|
| `username` | string | Yes | Account username (3-20 chars) |
| `password` | string | Yes | Account password |

---

### `login_response`

**Success Response:**
```json
{
  "type": "login_response",
  "seq": 1,
  "data": {
    "success": true,
    "session_token": "eyJhbGciOiJIUzI1NiIsInR5cCI6IkpXVCJ9..."
  }
}
```

**Failure Response:**
```json
{
  "type": "login_response",
  "seq": 1,
  "data": {
    "success": false,
    "error": "Invalid username or password"
  }
}
```

| Field | Type | Description |
|-------|------|-------------|
| `success` | bool | Whether login succeeded |
| `session_token` | string | Session token (on success) |
| `error` | string | Error message (on failure) |

**Possible Errors:**
- `Invalid username or password`
- `Account is banned`
- `Account is locked`
- `Too many login attempts`

---

### `logout_request`

End the current session.

**Request:**
```json
{
  "type": "logout_request",
  "seq": 10,
  "data": {}
}
```

---

### `logout_response`

**Response:**
```json
{
  "type": "logout_response",
  "seq": 10,
  "data": {
    "success": true
  }
}
```

---

### `create_account_request`

Register a new account.

**Request:**
```json
{
  "type": "create_account_request",
  "seq": 1,
  "data": {
    "username": "newplayer",
    "password": "securepass123"
  }
}
```

| Field | Type | Required | Description |
|-------|------|----------|-------------|
| `username` | string | Yes | Desired username (3-20 chars, alphanumeric) |
| `password` | string | Yes | Password (min 6 chars) |

---

### `create_account_response`

**Success Response:**
```json
{
  "type": "create_account_response",
  "seq": 1,
  "data": {
    "success": true,
    "account_id": 42
  }
}
```

**Failure Response:**
```json
{
  "type": "create_account_response",
  "seq": 1,
  "data": {
    "success": false,
    "error": "Username already exists"
  }
}
```

**Possible Errors:**
- `Username already exists`
- `Invalid username format`
- `Password too weak`
- `Registration is disabled`

---

## Character Management Messages

### `get_characters_request`

Get list of characters on the account. Requires authentication.

**Request:**
```json
{
  "type": "get_characters_request",
  "seq": 2,
  "data": {}
}
```

---

### `get_characters_response`

**Response:**
```json
{
  "type": "get_characters_response",
  "seq": 2,
  "data": {
    "success": true,
    "characters": [
      {
        "id": 1,
        "name": "Warrior1",
        "level": 45,
        "class_type": 1,
        "nation": 1,
        "gender": 1,
        "map_name": "aresden",
        "experience": 125000,
        "hair_style": 2,
        "hair_color": 3,
        "skin_color": 1
      },
      {
        "id": 2,
        "name": "Mage2",
        "level": 30,
        "class_type": 2,
        "nation": 2,
        "gender": 2,
        "map_name": "elvine",
        "experience": 50000,
        "hair_style": 1,
        "hair_color": 5,
        "skin_color": 0
      }
    ]
  }
}
```

#### Character Summary Object

| Field | Type | Description |
|-------|------|-------------|
| `id` | uint32 | Character ID |
| `name` | string | Character name |
| `level` | int16 | Character level (1-180) |
| `class_type` | int16 | Class (0=Warrior, 1=Mage, 2=Archer, etc.) |
| `nation` | int16 | Nation (1=Aresden, 2=Elvine) |
| `gender` | int16 | Gender (1=Male, 2=Female) |
| `map_name` | string | Last map location |
| `experience` | int64 | Total experience points |
| `hair_style` | int16 | Hair style ID (0-7) |
| `hair_color` | int16 | Hair color ID (0-15) |
| `skin_color` | int16 | Skin color ID (0-3) |

---

### `create_character_request`

Create a new character on the account.

**Request:**
```json
{
  "type": "create_character_request",
  "seq": 3,
  "data": {
    "name": "NewHero",
    "class_type": 0,
    "nation": 1,
    "gender": 1,
    "hair_style": 2,
    "hair_color": 4,
    "skin_color": 1,
    "underwear_color": 0,
    "strength": 14,
    "dexterity": 10,
    "vitality": 12,
    "intelligence": 10,
    "magic": 10,
    "charisma": 10
  }
}
```

| Field | Type | Required | Default | Description |
|-------|------|----------|---------|-------------|
| `name` | string | Yes | - | Character name (3-20 chars) |
| `class_type` | int16 | No | 0 | Starting class |
| `nation` | int16 | No | 0 | Nation affiliation |
| `gender` | int16 | No | 1 | Gender (1=Male, 2=Female) |
| `hair_style` | int16 | No | 0 | Hair style ID (0-7) |
| `hair_color` | int16 | No | 0 | Hair color ID (0-15) |
| `skin_color` | int16 | No | 0 | Skin color ID (0-3) |
| `underwear_color` | int16 | No | 0 | Underwear color ID |
| `strength` | int16 | No | 10 | Starting STR |
| `dexterity` | int16 | No | 10 | Starting DEX |
| `vitality` | int16 | No | 10 | Starting VIT |
| `intelligence` | int16 | No | 10 | Starting INT |
| `magic` | int16 | No | 10 | Starting MAG |
| `charisma` | int16 | No | 10 | Starting CHA |

**Stat Allocation Rules:**
- Base total: 60 points
- Minimum per stat: 10
- Maximum per stat: 14 at creation
- Total allocated must equal 70 (60 base + 10 bonus)

---

### `create_character_response`

**Success Response:**
```json
{
  "type": "create_character_response",
  "seq": 3,
  "data": {
    "success": true,
    "character_id": 5
  }
}
```

**Failure Response:**
```json
{
  "type": "create_character_response",
  "seq": 3,
  "data": {
    "success": false,
    "error": "Character name already exists"
  }
}
```

**Possible Errors:**
- `Character name already exists`
- `Invalid character name`
- `Maximum characters reached`
- `Invalid stat allocation`

**Note:** On success, the server also sends an unsolicited `get_characters_response` (seq=0) with the updated character list so the client can refresh immediately without a separate fetch.

---

### `delete_character_request`

Delete a character from the account.

**Request:**
```json
{
  "type": "delete_character_request",
  "seq": 4,
  "data": {
    "character_id": 5
  }
}
```

| Field | Type | Required | Description |
|-------|------|----------|-------------|
| `character_id` | uint32 | Yes | ID of character to delete |

---

### `delete_character_response`

**Success Response:**
```json
{
  "type": "delete_character_response",
  "seq": 4,
  "data": {
    "success": true
  }
}
```

**Failure Response:**
```json
{
  "type": "delete_character_response",
  "seq": 4,
  "data": {
    "success": false,
    "error": "Character not found"
  }
}
```

**Note:** On success, the server also sends an unsolicited `get_characters_response` (seq=0) with the updated character list so the client can refresh immediately without a separate fetch.

---

## Game Entry Messages

### `enter_game_request`

Enter the game world with a character.

**Request:**
```json
{
  "type": "enter_game_request",
  "seq": 5,
  "data": {
    "character_id": 1,
    "force_disconnect": false,
    "screen_width": 1920,
    "screen_height": 1080
  }
}
```

| Field | Type | Required | Default | Description |
|-------|------|----------|---------|-------------|
| `character_id` | uint32 | Yes | - | ID of character to play |
| `force_disconnect` | bool | No | false | Disconnect existing session for this account |
| `screen_width` | int16 | No | 800 | Client effective viewport width (see [View Mode System](#view-mode-system)) |
| `screen_height` | int16 | No | 600 | Client effective viewport height (see [View Mode System](#view-mode-system)) |

---

### `enter_game_response`

Returns complete game state on success. This is the primary payload for game initialization.

**Success Response:**
```json
{
  "type": "enter_game_response",
  "seq": 5,
  "data": {
    "success": true,
    "character": {
      "id": 1,
      "name": "Warrior1",
      "level": 45,
      "class_type": 1,
      "nation": 1,
      "gender": 1,
      "map_name": "aresden",
      "pos_x": 100,
      "pos_y": 150,
      "hp": 450,
      "hp_max": 500,
      "mp": 80,
      "mp_max": 100,
      "sp": 200,
      "sp_max": 200,
      "gold": 15000,
      "str": 45,
      "dex": 30,
      "vit": 35,
      "int": 20,
      "mag": 15,
      "cha": 25,
      "hair_style": 2,
      "hair_color": 3,
      "skin_color": 1,
      "experience": 125000,
      "pk_count": 5,
      "hunger_level": 85
    },
    "inventory": {
      "items": [
        {
          "slot": 0,
          "item_id": 101,
          "name": "Long Sword",
          "count": 1,
          "durability": 45,
          "max_durability": 50
        },
        {
          "slot": 5,
          "item_id": 205,
          "name": "Health Potion",
          "count": 10,
          "durability": 0,
          "max_durability": 0
        }
      ],
      "gold": 15000
    },
    "equipment": [
      {
        "slot": 5,
        "item_id": 101,
        "name": "Long Sword",
        "durability": 45,
        "max_durability": 50
      },
      {
        "slot": 1,
        "item_id": 301,
        "name": "Leather Armor",
        "durability": 80,
        "max_durability": 100
      }
    ],
    "skills": [
      { "skill_id": 1, "level": 50 },
      { "skill_id": 3, "level": 35 },
      { "skill_id": 10, "level": 20 }
    ],
    "spells": [
      { "spell_id": 0, "level": 1, "total_casts": 42 },
      { "spell_id": 1, "level": 3, "total_casts": 150 },
      { "spell_id": 20, "level": 2, "total_casts": 88 }
    ],
    "quests": {
      "active": [
        {
          "quest_id": 1,
          "status": 1,
          "objectives": [
            { "id": 0, "status": 0, "current": 3, "required": 10 },
            { "id": 1, "status": 1, "current": 1, "required": 1 }
          ]
        }
      ],
      "completed": [2, 5, 8]
    },
    "world": {
      "entities": [
        {
          "entity_id": 1001,
          "type": "player",
          "name": "OtherPlayer",
          "x": 105,
          "y": 148,
          "hp_percent": 100,
          "direction": 4
        },
        {
          "entity_id": 5001,
          "type": "npc",
          "name": "Guard",
          "x": 95,
          "y": 155,
          "hp_percent": 100,
          "direction": 2,
          "template_id": 100,
          "level": 50
        }
      ],
      "environment": {
        "hour": 14,
        "minute": 30,
        "is_day": true,
        "weather": 0
      }
    }
  }
}
```

**Failure Response:**
```json
{
  "type": "enter_game_response",
  "seq": 5,
  "data": {
    "success": false,
    "error": "Character not found"
  }
}
```

---

### Game State Objects

#### Character Data Object

| Field | Type | Description |
|-------|------|-------------|
| `id` | uint32 | Character ID |
| `name` | string | Character name |
| `level` | int16 | Current level (1-180) |
| `class_type` | int16 | Character class |
| `nation` | int16 | Nation (1=Aresden, 2=Elvine) |
| `gender` | int16 | Gender (1=Male, 2=Female) |
| `map_name` | string | Current map name |
| `pos_x` | int16 | X coordinate on map |
| `pos_y` | int16 | Y coordinate on map |
| `hp` | int32 | Current hit points |
| `hp_max` | int32 | Maximum hit points |
| `mp` | int32 | Current mana points |
| `mp_max` | int32 | Maximum mana points |
| `sp` | int32 | Current stamina points |
| `sp_max` | int32 | Maximum stamina points |
| `gold` | int32 | Gold amount |
| `str` | int16 | Strength stat |
| `dex` | int16 | Dexterity stat |
| `vit` | int16 | Vitality stat |
| `int` | int16 | Intelligence stat |
| `mag` | int16 | Magic stat |
| `cha` | int16 | Charisma stat |
| `hair_style` | int16 | Hair style ID |
| `hair_color` | int16 | Hair color ID |
| `skin_color` | int16 | Skin color ID |
| `experience` | int64 | Total experience |
| `pk_count` | int32 | Player kill count |
| `hunger_level` | int32 | Hunger (0-100) |

#### Inventory Item Object

| Field | Type | Description |
|-------|------|-------------|
| `slot` | uint8 | Inventory slot (0-49) |
| `item_id` | uint32 | Item template ID |
| `name` | string | Item display name |
| `count` | int16 | Stack count |
| `durability` | int16 | Current durability |
| `max_durability` | int16 | Maximum durability |

#### Equipment Item Object

| Field | Type | Description |
|-------|------|-------------|
| `slot` | uint8 | Equipment slot (see below) |
| `item_id` | uint32 | Item template ID |
| `name` | string | Item display name |
| `durability` | int16 | Current durability |
| `max_durability` | int16 | Maximum durability |

**Equipment Slots:**

| Slot | Name |
|------|------|
| 0 | Head |
| 1 | Body |
| 2 | Arms |
| 3 | Pants |
| 4 | Boots |
| 5 | Weapon |
| 6 | Shield |
| 7 | Ring 1 |
| 8 | Ring 2 |
| 9 | Amulet |
| 10 | Cape |
| 11 | Accessory |

#### Skill Object

| Field | Type | Description |
|-------|------|-------------|
| `skill_id` | uint8 | Skill identifier |
| `level` | int16 | Skill level (0-200) |

#### Known Spell Object

| Field | Type | Description |
|-------|------|-------------|
| `spell_id` | uint16 | Spell identifier (from magic.yaml) |
| `level` | int16 | Spell mastery level |
| `total_casts` | int32 | Lifetime cast count |

#### Quest Data Object

The `quests` field contains two sub-fields:

| Field | Type | Description |
|-------|------|-------------|
| `active` | array | Array of active quest objects |
| `completed` | array | Array of completed quest IDs (uint16) |

#### Active Quest Object

| Field | Type | Description |
|-------|------|-------------|
| `quest_id` | uint16 | Quest template ID |
| `status` | uint8 | 0=available, 1=active, 2=complete, 3=turned_in, 4=failed, 5=abandoned |
| `objectives` | array | Array of quest objective objects |

#### Quest Objective Object

| Field | Type | Description |
|-------|------|-------------|
| `id` | uint16 | Objective ID within quest |
| `status` | uint8 | 0=incomplete, 1=complete, 2=failed |
| `current` | int32 | Current progress count |
| `required` | int32 | Required count for completion |

#### Visible Entity Object

| Field | Type | Description |
|-------|------|-------------|
| `entity_id` | uint32 | Unique entity ID |
| `type` | string | Entity type: `"player"` or `"npc"` |
| `name` | string | Entity display name |
| `x` | int16 | X coordinate |
| `y` | int16 | Y coordinate |
| `hp_percent` | int16 | Health percentage (0-100) |
| `direction` | int16 | Facing direction (0-7) |
| `template_id` | uint32 | NPC template ID (only for type="npc") |
| `level` | int16 | NPC level (only for type="npc") |

**Direction Values:**

| Value | Direction |
|-------|-----------|
| 0 | North |
| 1 | Northeast |
| 2 | East |
| 3 | Southeast |
| 4 | South |
| 5 | Southwest |
| 6 | West |
| 7 | Northwest |

---

## World State Messages

These messages can be sent individually during teleports or map changes.

### `world_init`

Sent after teleport to provide entities in the new area.

**Server Message:**
```json
{
  "type": "world_init",
  "seq": 0,
  "data": {
    "entities": [
      {
        "entity_id": 1001,
        "type": "player",
        "name": "OtherPlayer",
        "x": 105,
        "y": 148,
        "hp_percent": 100,
        "direction": 4
      }
    ]
  }
}
```

---

### `inventory_data`

Update full inventory state (e.g., after trade).

**Server Message:**
```json
{
  "type": "inventory_data",
  "seq": 0,
  "data": {
    "items": [...],
    "gold": 15000
  }
}
```

---

### `equipment_data`

Update full equipment state.

**Server Message:**
```json
{
  "type": "equipment_data",
  "seq": 0,
  "data": {
    "equipment": [...]
  }
}
```

---

### `player_equip_request`

Equip an item from inventory to an equipment slot.

**Client Request:**
```json
{
  "type": "player_equip_request",
  "seq": 100,
  "data": {
    "inventory_slot": 3,
    "target_slot": 5
  }
}
```

| Field | Type | Description |
|-------|------|-------------|
| `inventory_slot` | int16 | Source inventory slot index (0-49) |
| `target_slot` | uint8 | Target equipment slot (see equip_slot enum) |

**Equipment Slots:**
| Value | Slot |
|-------|------|
| 0 | Head |
| 1 | Body |
| 2 | Arms |
| 3 | Pants |
| 4 | Boots |
| 5 | Weapon |
| 6 | Shield |
| 7 | Ring (Left) |
| 8 | Ring (Right) |
| 9 | Amulet |
| 10 | Cape |

---

### `player_equip_response`

Result of an equip attempt.

**Server Response:**
```json
{
  "type": "player_equip_response",
  "seq": 100,
  "data": {
    "success": true,
    "slot": 5,
    "item_id": 1234,
    "item_name": "Iron Sword",
    "durability": 100,
    "max_durability": 100,
    "swapped_item_id": 1200,
    "swapped_to_inv_slot": 3
  }
}
```

| Field | Type | Description |
|-------|------|-------------|
| `success` | bool | Whether equip succeeded |
| `slot` | uint8 | Equipment slot |
| `item_id` | uint32 | Equipped item ID |
| `item_name` | string | Item display name |
| `durability` | int16 | Current durability |
| `max_durability` | int16 | Maximum durability |
| `swapped_item_id` | uint32? | Old item ID if slot was occupied |
| `swapped_to_inv_slot` | uint8? | Inventory slot where old item went |
| `unequipped_shield_id` | uint32? | Shield ID if 2H weapon forced removal |
| `shield_to_inv_slot` | uint8? | Where shield went |
| `error` | string? | Error code on failure |

**Error codes:** `not_equippable`, `item_broken`, `invalid_slot`, `requirements_not_met`, `inventory_full`, `two_handed_weapon_equipped`, `player_dead`, `player_busy`

---

### `player_unequip_request`

Unequip an item from an equipment slot to inventory.

**Client Request:**
```json
{
  "type": "player_unequip_request",
  "seq": 101,
  "data": {
    "equip_slot": 5
  }
}
```

---

### `player_unequip_response`

Result of an unequip attempt.

**Server Response:**
```json
{
  "type": "player_unequip_response",
  "seq": 101,
  "data": {
    "success": true,
    "slot": 5,
    "item_id": 1234,
    "item_name": "Iron Sword",
    "inventory_slot": 3
  }
}
```

**Error codes:** `invalid_slot`, `slot_empty`, `inventory_full`, `player_dead`, `player_busy`

---

### `equipment_change_broadcast`

Broadcast to nearby players when a player's equipment changes.

**Server Broadcast:**
```json
{
  "type": "equipment_change_broadcast",
  "seq": 0,
  "data": {
    "entity_id": 42,
    "slot": 5,
    "item_id": 1234,
    "template_id": 100
  }
}
```

| Field | Type | Description |
|-------|------|-------------|
| `entity_id` | uint32 | Player's entity ID |
| `slot` | uint8 | Equipment slot that changed |
| `item_id` | uint32 | New item ID (0 = now empty) |
| `template_id` | uint32 | Item template for sprite lookup |

---

### `stat_update`

Sent to a player after equipment changes to reflect updated computed stats.

**Server Message:**
```json
{
  "type": "stat_update",
  "seq": 0,
  "data": {
    "max_hp": 350,
    "max_mp": 200,
    "max_sp": 250,
    "attack_power": 85,
    "magic_power": 45,
    "defense": 30,
    "magic_defense": 15,
    "hit_rate": 120,
    "dodge_rate": 60,
    "critical_rate": 15
  }
}
```

---

### `skills_data`

Update skill levels.

**Server Message:**
```json
{
  "type": "skills_data",
  "seq": 0,
  "data": {
    "skills": [
      { "skill_id": 1, "level": 50 },
      { "skill_id": 3, "level": 35 }
    ]
  }
}
```

---

## Entity Visibility Messages

### `entity_spawn`

A new entity entered visibility range.

**Server Broadcast:**
```json
{
  "type": "entity_spawn",
  "seq": 0,
  "data": {
    "entity_id": 1002,
    "type": "player",
    "name": "NewArrival",
    "x": 110,
    "y": 145,
    "hp_percent": 100,
    "direction": 4
  }
}
```

---

### `entity_despawn`

An entity left visibility range or disconnected.

**Server Broadcast:**
```json
{
  "type": "entity_despawn",
  "seq": 0,
  "data": {
    "entity_id": 1002
  }
}
```

---

## Movement Messages

### `player_move_request`

Request to move the player character (walk or run).

**Request:**
```json
{
  "type": "player_move_request",
  "seq": 100,
  "data": {
    "x": 101,
    "y": 151,
    "direction": 2,
    "is_running": false,
    "timestamp": 1234567890,
    "dest_x": 110,
    "dest_y": 160
  }
}
```

| Field | Type | Required | Description |
|-------|------|----------|-------------|
| `x` | int16 | Yes | Current X coordinate (for validation) |
| `y` | int16 | Yes | Current Y coordinate (for validation) |
| `direction` | int16 | Yes | Direction to move (0-7) |
| `is_running` | bool | No | If true, moves 2 tiles; if false/omitted, moves 1 tile |
| `timestamp` | uint64 | No | Client timestamp in milliseconds |
| `dest_x` | int16 | No | Mouse destination tile X (where player clicked) |
| `dest_y` | int16 | No | Mouse destination tile Y (where player clicked) |

**Notes:**
- The `dest_x`/`dest_y` fields represent the final destination tile where the player clicked
- The server stores this destination and includes it in broadcasts until the player reaches it
- Destination is automatically cleared when:
  - Player reaches the destination coordinates
  - Move is blocked (bumped, terrain, etc.)
  - Player sends a stop request
  - Player is teleported
  - Player takes damage (interrupts movement)

---

### `player_move_response`

Server confirms or rejects move.

**Success Response:**
```json
{
  "type": "player_move_response",
  "seq": 100,
  "data": {
    "success": true,
    "x": 101,
    "y": 151,
    "direction": 2
  }
}
```

**Failure Response:**
```json
{
  "type": "player_move_response",
  "seq": 100,
  "data": {
    "success": false,
    "error": "Tile is not walkable"
  }
}
```

**Possible Errors:**
- `Tile is not walkable`
- `Tile is occupied`
- `Move too far`
- `Player is stunned`

---

### `player_stop_request`

Request to stop moving and optionally change facing direction.

**Request:**
```json
{
  "type": "player_stop_request",
  "seq": 100,
  "data": {
    "x": 101,
    "y": 151,
    "direction": 4,
    "timestamp": 1234567890
  }
}
```

| Field | Type | Required | Description |
|-------|------|----------|-------------|
| `x` | int16 | Yes | Current X coordinate |
| `y` | int16 | Yes | Current Y coordinate |
| `direction` | int16 | No | New facing direction (0-7). If omitted, keeps current direction |
| `timestamp` | uint64 | No | Client timestamp in milliseconds |

---

### `player_stop_response`

Server confirms the stop and current position/direction.

**Success Response:**
```json
{
  "type": "player_stop_response",
  "seq": 100,
  "data": {
    "success": true,
    "x": 101,
    "y": 151,
    "direction": 4
  }
}
```

| Field | Type | Description |
|-------|------|-------------|
| `success` | bool | Always true for stop requests |
| `x` | int16 | Confirmed X coordinate |
| `y` | int16 | Confirmed Y coordinate |
| `direction` | int16 | Confirmed facing direction (0-7) |

---

### `player_position_update`

Broadcast to nearby players when someone moves or stops.

**Server Broadcast:**
```json
{
  "type": "player_position_update",
  "seq": 0,
  "data": {
    "entity_id": 1001,
    "x": 101,
    "y": 151,
    "direction": 2,
    "is_running": false,
    "dest_x": 110,
    "dest_y": 160
  }
}
```

| Field | Type | Description |
|-------|------|-------------|
| `entity_id` | uint32 | Entity ID of the player who moved |
| `x` | int16 | New X coordinate |
| `y` | int16 | New Y coordinate |
| `direction` | int16 | Facing direction (0-7) |
| `is_running` | bool | True if running, false if walking or stopped |
| `dest_x` | int16 | Mouse destination tile X (optional, omitted if no destination) |
| `dest_y` | int16 | Mouse destination tile Y (optional, omitted if no destination) |

**Notes:**
- `dest_x`/`dest_y` are only included when the player has an active movement destination
- Clients can use this to predict the player's path or display movement indicators
- When the player stops or reaches their destination, these fields are omitted

---

## Combat Messages

### `player_attack_request`

Request to attack a target.

**Request:**
```json
{
  "type": "player_attack_request",
  "seq": 150,
  "data": {
    "x": 100,
    "y": 150,
    "direction": 2,
    "attack_type": "regular",
    "target_type": "npc",
    "target_id": 5001,
    "timestamp": 1706500000000
  }
}
```

| Field | Type | Required | Description |
|-------|------|----------|-------------|
| `x` | int16 | Yes | Current X coordinate (for validation) |
| `y` | int16 | Yes | Current Y coordinate (for validation) |
| `direction` | int16 | No | Direction facing (0-7) |
| `attack_type` | string/int | No | Attack type: `"regular"` (0), `"dash"` (1), or `"super"` (2) |
| `target_type` | string/int | No | Target type: `"none"` (0), `"player"` (1), `"npc"` (2), `"ground"` (3), `"item"` (4) |
| `target_id` | uint32 | No | Target entity ID |
| `timestamp` | uint64 | No | Client timestamp in milliseconds |

**Attack Types:**

| Value | Name | Description |
|-------|------|-------------|
| 0 / `"regular"` | Regular | Normal melee attack |
| 1 / `"dash"` | Dash | Dash attack (requires 100% skill, 1 tile gap) |
| 2 / `"super"` | Super | Super attack (requires 100% skill + charges, ranged) |

---

### `player_attack_response`

Server confirms or rejects attack.

**Success Response:**
```json
{
  "type": "player_attack_response",
  "seq": 150,
  "data": {
    "success": true,
    "result": {
      "hit": true,
      "critical": false,
      "damage": 45,
      "target_id": 5001,
      "target_hp": 155,
      "target_hp_max": 200,
      "attacker_x": 100,
      "attacker_y": 150
    }
  }
}
```

**Failure Response:**
```json
{
  "type": "player_attack_response",
  "seq": 150,
  "data": {
    "success": false,
    "error": "Target out of range"
  }
}
```

#### Attack Result Object

| Field | Type | Description |
|-------|------|-------------|
| `hit` | bool | Whether attack connected |
| `critical` | bool | Whether it was a critical hit |
| `damage` | int32 | Damage dealt |
| `target_id` | uint32 | Target entity ID |
| `target_hp` | int16 | Target's remaining HP |
| `target_hp_max` | int16 | Target's maximum HP |
| `attacker_x` | int16 | Confirmed attacker X position |
| `attacker_y` | int16 | Confirmed attacker Y position |

---

### `combat_attack_broadcast`

Broadcast to nearby players when an attack occurs.

**Server Broadcast:**
```json
{
  "type": "combat_attack_broadcast",
  "seq": 0,
  "data": {
    "attacker_id": 1001,
    "target_id": 5001,
    "attacker_x": 100,
    "attacker_y": 150,
    "target_x": 101,
    "target_y": 150,
    "hit": true,
    "critical": false,
    "damage": 45
  }
}
```

| Field | Type | Description |
|-------|------|-------------|
| `attacker_id` | uint32 | Attacker entity ID |
| `target_id` | uint32 | Target entity ID |
| `attacker_x` | int16 | Attacker X position |
| `attacker_y` | int16 | Attacker Y position |
| `target_x` | int16 | Target X position |
| `target_y` | int16 | Target Y position |
| `hit` | bool | Whether attack connected |
| `critical` | bool | Whether it was a critical hit |
| `damage` | int32 | Damage dealt |

---

### `entity_hp_update`

Broadcast when an entity's HP changes.

**Server Broadcast:**
```json
{
  "type": "entity_hp_update",
  "seq": 0,
  "data": {
    "entity_id": 5001,
    "hp": 155,
    "hp_max": 200
  }
}
```

| Field | Type | Description |
|-------|------|-------------|
| `entity_id` | uint32 | Entity ID |
| `hp` | int32 | Current HP |
| `hp_max` | int32 | Maximum HP |

---

### `entity_death`

Broadcast when an entity dies.

**Server Broadcast:**
```json
{
  "type": "entity_death",
  "seq": 0,
  "data": {
    "victim_id": 5001,
    "killer_id": 1001,
    "x": 101,
    "y": 150
  }
}
```

| Field | Type | Description |
|-------|------|-------------|
| `victim_id` | uint32 | Entity that died |
| `killer_id` | uint32 | Entity that killed (0 if environmental/unknown) |
| `x` | int16 | Death location X |
| `y` | int16 | Death location Y |

---

### `combat_effect`

Unified visual effect broadcast for all combat and spell events. Covers melee damage, spell damage, healing, misses, dodges, blocks, resists, buffs, and debuffs. Clients use this to render floating damage numbers, spell animations, and status effect indicators.

**Server Broadcast:**
```json
{
  "type": "combat_effect",
  "seq": 0,
  "data": {
    "source_id": 12345,
    "target_id": 67890,
    "effect_type": "damage",
    "value": 42,
    "damage_type": "fire",
    "spell_id": 5,
    "is_critical": true,
    "target_x": 100,
    "target_y": 200
  }
}
```

| Field | Type | Description |
|-------|------|-------------|
| `source_id` | uint32 | Entity that caused the effect |
| `target_id` | uint32 | Entity that received the effect |
| `effect_type` | string | One of: `"damage"`, `"heal"`, `"miss"`, `"dodge"`, `"block"`, `"resist"`, `"buff"`, `"debuff"` |
| `value` | int32 | Amount (damage/heal for damage/heal, 0 for miss/dodge/block) |
| `damage_type` | string | *(optional)* One of: `"physical"`, `"magic"`, `"fire"`, `"ice"`, `"lightning"`, `"poison"`, `"holy"`, `"dark"`, `"pure"`. Omitted for non-damage effects |
| `spell_id` | uint32 | *(optional)* Spell ID if caused by a spell. Omitted (or 0) for melee |
| `is_critical` | bool | Whether this was a critical hit |
| `target_x` | int16 | Target position X (for positioning floating text) |
| `target_y` | int16 | Target position Y |

**Visibility Rules:**
- `damage`, `heal`, `miss`, `dodge`, `block`, `resist` — broadcast to all nearby players (via `get_players_who_can_see`)
- `buff`, `debuff` — broadcast only to same-faction players nearby

**When Emitted:**
- Melee attacks: emitted from `on_damage_dealt` callback (effect_type derived from hit_result flags)
- Spell casts: emitted from `on_spell_cast` callback (effect_type derived from spell category)

---

### `player_death_info`

Sent to the dead player with death details, penalties applied, and respawn information.

**Server Message:**
```json
{
  "type": "player_death_info",
  "seq": 0,
  "data": {
    "killer_id": 1001,
    "killer_name": "EnemyPlayer",
    "is_pvp": true,
    "xp_lost": 2500,
    "pk_points_change": 50,
    "gold_reward": 0,
    "respawn_delay_ms": 5000,
    "respawn_map": "aresden",
    "respawn_x": 42,
    "respawn_y": 88
  }
}
```

| Field | Type | Description |
|-------|------|-------------|
| `killer_id` | uint32 | Entity that killed the player (0 if NPC/environmental) |
| `killer_name` | string | Killer's display name (empty if NPC) |
| `is_pvp` | bool | Whether this was a player-vs-player kill |
| `xp_lost` | int64 | Experience points removed as death penalty |
| `pk_points_change` | int32 | PK points the killer gained (50 if victim was innocent) |
| `gold_reward` | int32 | Gold bounty earned by killer (if victim was a PKer) |
| `respawn_delay_ms` | uint32 | Milliseconds until respawn teleport |
| `respawn_map` | string | Map name where player will respawn |
| `respawn_x` | int16 | Respawn position X |
| `respawn_y` | int16 | Respawn position Y |

After `respawn_delay_ms` elapses, the server sends a `player_teleport` message to move the player to the respawn location.

---

## Magic Messages

### `player_magic_request`

Request to cast a spell.

**Request:**
```json
{
  "type": "player_magic_request",
  "seq": 160,
  "data": {
    "x": 100,
    "y": 150,
    "direction": 2,
    "spell_id": 10,
    "target_type": "npc",
    "target_id": 5001,
    "target_x": 105,
    "target_y": 150,
    "timestamp": 1706500000000
  }
}
```

| Field | Type | Required | Description |
|-------|------|----------|-------------|
| `x` | int16 | Yes | Current X coordinate (for validation) |
| `y` | int16 | Yes | Current Y coordinate (for validation) |
| `direction` | int16 | No | Direction facing (0-7) |
| `spell_id` | uint32 | Yes | Spell to cast |
| `target_type` | string/int | No | Target type (see attack types) |
| `target_id` | uint32 | No | Target entity ID (for targeted spells) |
| `target_x` | int16 | No | Target X location (for ground-targeted spells) |
| `target_y` | int16 | No | Target Y location (for ground-targeted spells) |
| `timestamp` | uint64 | No | Client timestamp in milliseconds |

---

### `player_magic_response`

Server confirms or rejects spell cast.

**Success Response:**
```json
{
  "type": "player_magic_response",
  "seq": 160,
  "data": {
    "success": true,
    "result": {
      "success": true,
      "spell_id": 10,
      "mana_cost": 25,
      "damage": 80,
      "heal": 0,
      "target_id": 5001,
      "caster_mp": 75
    }
  }
}
```

**Failure Response:**
```json
{
  "type": "player_magic_response",
  "seq": 160,
  "data": {
    "success": false,
    "error": "Not enough mana"
  }
}
```

#### Magic Result Object

| Field | Type | Description |
|-------|------|-------------|
| `success` | bool | Whether spell cast succeeded |
| `spell_id` | uint32 | Spell that was cast |
| `mana_cost` | int32 | Mana consumed |
| `damage` | int32 | Damage dealt (if damage spell) |
| `heal` | int32 | HP healed (if heal spell) |
| `target_id` | uint32 | Target entity ID (if targeted) |
| `caster_mp` | int16 | Caster's remaining MP |

---

## Skill Messages

### `player_skill_request`

Request to use a skill.

**Request:**
```json
{
  "type": "player_skill_request",
  "seq": 170,
  "data": {
    "x": 100,
    "y": 150,
    "direction": 2,
    "skill_id": 5,
    "target_type": "none",
    "target_id": 0,
    "timestamp": 1706500000000
  }
}
```

| Field | Type | Required | Description |
|-------|------|----------|-------------|
| `x` | int16 | Yes | Current X coordinate (for validation) |
| `y` | int16 | Yes | Current Y coordinate (for validation) |
| `direction` | int16 | No | Direction facing (0-7) |
| `skill_id` | uint32 | Yes | Skill to use |
| `target_type` | string/int | No | Target type (see attack types) |
| `target_id` | uint32 | No | Target entity ID (if applicable) |
| `timestamp` | uint64 | No | Client timestamp in milliseconds |

---

### `player_skill_response`

Server confirms or rejects skill use.

**Success Response:**
```json
{
  "type": "player_skill_response",
  "seq": 170,
  "data": {
    "success": true,
    "result": {
      "success": true,
      "skill_id": 5,
      "effect_value": 10,
      "target_id": 0
    }
  }
}
```

**Failure Response:**
```json
{
  "type": "player_skill_response",
  "seq": 170,
  "data": {
    "success": false,
    "error": "Skill on cooldown"
  }
}
```

#### Skill Result Object

| Field | Type | Description |
|-------|------|-------------|
| `success` | bool | Whether skill use succeeded |
| `skill_id` | uint32 | Skill that was used |
| `effect_value` | int32 | Skill-specific effect value |
| `target_id` | uint32 | Target entity ID (if targeted) |

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

## Interact Messages

### `player_interact_request`

Request to interact with an NPC or object.

**Request:**
```json
{
  "type": "player_interact_request",
  "seq": 210,
  "data": {
    "x": 100,
    "y": 150,
    "target_type": "npc",
    "target_id": 5001,
    "timestamp": 1706500000000
  }
}
```

| Field | Type | Required | Description |
|-------|------|----------|-------------|
| `x` | int16 | Yes | Current X coordinate (for validation) |
| `y` | int16 | Yes | Current Y coordinate (for validation) |
| `target_type` | string/int | No | Target type |
| `target_id` | uint32 | Yes | Target NPC or object ID |
| `timestamp` | uint64 | No | Client timestamp in milliseconds |

---

### `player_interact_response`

Server responds with interaction result. The response includes structured `interaction_data` whose contents depend on the `interaction_type`.

**Success Response (Shop):**
```json
{
  "type": "player_interact_response",
  "seq": 210,
  "data": {
    "success": true,
    "result": {
      "success": true,
      "target_id": 5001,
      "interaction_type": "shop",
      "interaction_data": {
        "npc_name": "Blacksmith",
        "items": [
          {
            "template_id": 10,
            "name": "Short Sword",
            "price": 500,
            "category": "weapon"
          },
          {
            "template_id": 301,
            "name": "Leather Armor",
            "price": 1200,
            "category": "armor"
          }
        ]
      }
    }
  }
}
```

**Success Response (Bank):**
```json
{
  "type": "player_interact_response",
  "seq": 211,
  "data": {
    "success": true,
    "result": {
      "success": true,
      "target_id": 5002,
      "interaction_type": "bank",
      "interaction_data": {
        "npc_name": "Banker",
        "items": [
          {
            "slot": 0,
            "item_id": 101,
            "name": "Long Sword",
            "count": 1,
            "durability": 45,
            "max_durability": 50
          }
        ]
      }
    }
  }
}
```

**Success Response (Dialog):**
```json
{
  "type": "player_interact_response",
  "seq": 212,
  "data": {
    "success": true,
    "result": {
      "success": true,
      "target_id": 5003,
      "interaction_type": "dialog",
      "interaction_data": {
        "npc_name": "Elder",
        "node_id": "start",
        "text": "Greetings, traveler. How may I help you?",
        "options": [
          { "label": "Tell me about this town", "action": "goto_node", "next_node": "town_info" },
          { "label": "Open shop", "action": "open_shop" },
          { "label": "Goodbye", "action": "close" }
        ]
      }
    }
  }
}
```

**Failure Response:**
```json
{
  "type": "player_interact_response",
  "seq": 210,
  "data": {
    "success": false,
    "error": "Target out of range"
  }
}
```

#### Interact Result Object

| Field | Type | Description |
|-------|------|-------------|
| `success` | bool | Whether interaction succeeded |
| `target_id` | uint32 | Target entity ID |
| `interaction_type` | string | Type: `"dialog"`, `"shop"`, `"bank"` |
| `interaction_data` | object | Type-specific data (see below) |

#### Shop Interaction Data

| Field | Type | Description |
|-------|------|-------------|
| `npc_name` | string | NPC display name |
| `items` | array | Array of shop item objects |

**Shop Item Object:**

| Field | Type | Description |
|-------|------|-------------|
| `template_id` | uint32 | Item template ID |
| `name` | string | Item display name |
| `price` | int32 | Buy price in gold |
| `category` | string | Item category (e.g., `"weapon"`, `"armor"`, `"potion"`) |

#### Bank Interaction Data

| Field | Type | Description |
|-------|------|-------------|
| `npc_name` | string | NPC display name |
| `items` | array | Array of bank item objects (same format as Inventory Item Object) |

#### Dialog Interaction Data

| Field | Type | Description |
|-------|------|-------------|
| `npc_name` | string | NPC display name |
| `node_id` | string | Current dialog node ID |
| `text` | string | Dialog text to display |
| `options` | array | Array of dialog option objects |

**Dialog Option Object:**

| Field | Type | Description |
|-------|------|-------------|
| `label` | string | Display text for the option |
| `action` | string | Action type: `"goto_node"`, `"close"`, `"open_shop"`, `"open_bank"` |
| `next_node` | string | Next dialog node ID (only for `"goto_node"` action) |

---

## Chat Messages

### `chat_message`

Client sends a chat message.

**Request:**
```json
{
  "type": "chat_message",
  "seq": 300,
  "data": {
    "content": "Hello everyone!",
    "channel": "local",
    "recipient": "PlayerName",
    "timestamp": 1706500000000
  }
}
```

| Field | Type | Required | Description |
|-------|------|----------|-------------|
| `content` | string | Yes | Message content (may include prefix like `!` for shout) |
| `channel` | string | No | Explicit channel override |
| `recipient` | string | No | Recipient name (for whispers) |
| `timestamp` | uint64 | No | Client timestamp in milliseconds |

**Chat Channels:**

| Channel | Prefix | Description |
|---------|--------|-------------|
| `local` | (none) | Nearby players (default) |
| `shout` | `!` | Server-wide |
| `guild` | `@` | Guild members |
| `party` | `$` | Party members |
| `whisper` | `#` or recipient name | Private message |
| `global` | - | Global channel |
| `trade` | - | Trade channel |
| `faction` | - | Faction channel (Aresden/Elvine) |
| `system` | - | System messages (server-generated only) |

**Response:**
```json
{
  "type": "chat_message",
  "seq": 300,
  "data": {
    "success": true
  }
}
```

---

### `chat_message_broadcast`

Server broadcasts chat message to recipients.

**Server Broadcast:**
```json
{
  "type": "chat_message_broadcast",
  "seq": 0,
  "data": {
    "channel": "local",
    "sender_id": 1001,
    "sender_name": "Warrior1",
    "content": "Hello everyone!",
    "flags": [],
    "timestamp": "2024-01-29T12:00:00Z",
    "recipient_name": null
  }
}
```

| Field | Type | Description |
|-------|------|-------------|
| `channel` | string | Chat channel |
| `sender_id` | uint32 | Sender player ID (0 for system) |
| `sender_name` | string | Sender display name |
| `content` | string | Message content |
| `flags` | array | Flags: `"emote"`, `"censored"`, `"system"`, `"gm"` |
| `timestamp` | string | ISO 8601 timestamp |
| `recipient_name` | string | Recipient name (for whisper, optional) |

---

## Command Messages

### `command_request`

Client sends a command (e.g., /help, /who).

**Request:**
```json
{
  "type": "command_request",
  "seq": 400,
  "data": {
    "command": "teleport",
    "args": ["aresden", "100", "150"],
    "params": {},
    "timestamp": 1706500000000
  }
}
```

| Field | Type | Required | Description |
|-------|------|----------|-------------|
| `command` | string | Yes | Command name (without /) |
| `args` | array | No | Command arguments |
| `params` | object | No | Named parameters |
| `timestamp` | uint64 | No | Client timestamp in milliseconds |

---

### `command_response`

Server responds to command.

**Response:**
```json
{
  "type": "command_response",
  "seq": 400,
  "data": {
    "success": true,
    "command": "teleport",
    "message": "Teleported to aresden (100, 150)",
    "result": {}
  }
}
```

| Field | Type | Description |
|-------|------|-------------|
| `success` | bool | Whether command succeeded |
| `command` | string | Echo of the command |
| `message` | string | Success/error message |
| `result` | object | Command-specific result data |

---

## Teleportation Messages

### `map_teleporters`

Server sends full teleporter list for a map.

**Server Message:**
```json
{
  "type": "map_teleporters",
  "seq": 0,
  "data": {
    "map_name": "aresden",
    "teleporters": [
      {
        "id": 6553700,
        "x": 100,
        "y": 50,
        "dest_map": "aresden2",
        "dest_x": 200,
        "dest_y": 100,
        "dest_dir": 4
      }
    ]
  }
}
```

#### Teleporter Info Object

| Field | Type | Description |
|-------|------|-------------|
| `id` | uint32 | Teleporter ID (computed from position) |
| `x` | int16 | Source X coordinate |
| `y` | int16 | Source Y coordinate |
| `dest_map` | string | Destination map name |
| `dest_x` | int16 | Destination X coordinate |
| `dest_y` | int16 | Destination Y coordinate |
| `dest_dir` | int16 | Destination facing direction (0-7) |

---

### `teleporter_update`

Live update when a teleporter is added, removed, or modified.

**Server Message:**
```json
{
  "type": "teleporter_update",
  "seq": 0,
  "data": {
    "action": "add",
    "map_name": "aresden",
    "teleporter": {
      "id": 6553700,
      "x": 100,
      "y": 50,
      "dest_map": "aresden2",
      "dest_x": 200,
      "dest_y": 100,
      "dest_dir": 4
    }
  }
}
```

| Field | Type | Description |
|-------|------|-------------|
| `action` | string | Action: `"add"`, `"remove"`, `"modify"` |
| `map_name` | string | Map where teleporter is located |
| `teleporter` | object | Teleporter info |

---

### `player_teleport`

Sent to player when they are teleported.

**Server Message:**
```json
{
  "type": "player_teleport",
  "seq": 0,
  "data": {
    "dest_map": "aresden2",
    "dest_x": 200,
    "dest_y": 100,
    "dest_dir": 4,
    "entities": [
      {
        "entity_id": 1002,
        "type": "player",
        "name": "OtherPlayer",
        "x": 205,
        "y": 98,
        "hp_percent": 100,
        "direction": 2
      }
    ]
  }
}
```

| Field | Type | Description |
|-------|------|-------------|
| `dest_map` | string | Destination map name |
| `dest_x` | int16 | Destination X coordinate |
| `dest_y` | int16 | Destination Y coordinate |
| `dest_dir` | int16 | Destination facing direction (0-7) |
| `entities` | array | Visible entities at destination |

---

## Player State Updates

### `hunger_update`

Sent when the player's hunger level changes (due to decay or consuming food).

**Server Message:**
```json
{
  "type": "hunger_update",
  "seq": 0,
  "data": {
    "level": 85,
    "is_starving": false
  }
}
```

| Field | Type | Description |
|-------|------|-------------|
| `level` | int8 | Current hunger level (0-100) |
| `is_starving` | bool | True if level <= 0 (blocks regeneration) |

**Hunger Effects:**
- **Hunger >= 30:** Normal HP/MP/SP regeneration
- **Hunger 1-29:** Regeneration delayed by `(30 - hunger) * 1000` ms
- **Hunger <= 0 (Starving):** All regeneration blocked

---

### `environment_update`

Sent every ~10 seconds with current day/night cycle and weather state. Also sent immediately after teleportation and as part of the initial `enter_game_response`.

**Server Message:**
```json
{
  "type": "environment_update",
  "seq": 0,
  "data": {
    "hour": 14,
    "minute": 30,
    "is_day": true,
    "weather": 0
  }
}
```

| Field | Type | Description |
|-------|------|-------------|
| `hour` | uint8 | Game clock hour (0-23) |
| `minute` | uint8 | Game clock minute (0-59) |
| `is_day` | bool | Whether the game clock is in daytime (6:00-17:59) |
| `weather` | uint8 | Weather type (see below) |

**Weather Types:**

| Value | Name | Description |
|-------|------|-------------|
| 0 | `clear` | No weather effects |
| 1 | `light_rain` | Light rain |
| 2 | `rain` | Moderate rain |
| 3 | `heavy_rain` | Heavy rain |
| 4 | `light_snow` | Light snow |
| 5 | `snow` | Moderate snow |
| 6 | `heavy_snow` | Heavy snow |
| 7 | `windy` | Windy conditions |
| 8 | `stormy` | Stormy conditions |

---

## View Mode System

The server controls how clients render the game world to prevent higher-resolution displays from gaining a competitive advantage.

### Rendering Modes

| Mode | Description | Visibility | Use Case |
|------|-------------|------------|----------|
| **`scaled`** | Game world renders at fair resolution internally, upscaled to display. All players see identical game area. | Fair resolution only | Competitive PvP zones |
| **`extended`** | Native resolution rendering, but entities/effects only visible within centered fair zone. Dark fog overlay outside. Terrain visible everywhere. | Fair zone for entities, full display for terrain | Semi-competitive areas |
| **`special`** | Unrestricted native resolution with zoom support. Current/legacy behavior. | Full display resolution | Towns, safe areas, admin mode |

### `set_render_mode`

Server tells the client which rendering mode to use. Sent on login, zone entry, or admin override. If never sent, client defaults to `special` mode (backward compatible).

**Server -> Client:**
```json
{
  "type": "set_render_mode",
  "data": {
    "mode": "scaled",
    "fair_width": 800,
    "fair_height": 600
  }
}
```

| Field | Type | Description |
|-------|------|-------------|
| `mode` | string | `"scaled"`, `"extended"`, or `"special"` |
| `fair_width` | int16 | Fair zone width in pixels (ignored in `special` mode) |
| `fair_height` | int16 | Fair zone height in pixels (ignored in `special` mode) |

**Notes:**
- The server is the authority on mode and fair resolution. Clients cannot override.
- Fair resolution is a server-wide default (configurable in `game_config`) with optional per-map overrides.
- Players can configure cosmetic preferences (letterbox/stretch, nearest/bilinear, UI scale) that don't affect gameplay.
- Mode can be switched mid-session. Client handles transitions cleanly.
- After receiving `set_render_mode`, the client immediately sends `set_view_range` with updated viewport dimensions.

### `set_view_range`

Client updates its effective viewport dimensions. Sent when resolution changes, view mode changes, or after receiving `set_render_mode`.

**Request:**
```json
{
  "type": "set_view_range",
  "seq": 500,
  "data": {
    "screen_width": 800,
    "screen_height": 600
  }
}
```

| Field | Type | Required | Description |
|-------|------|----------|-------------|
| `screen_width` | int16 | No | Effective viewport width in pixels |
| `screen_height` | int16 | No | Effective viewport height in pixels |

**What the client sends per mode:**
- **`scaled`/`extended`:** Sends the fair resolution (e.g., 800x600), not the display resolution.
- **`special`:** Sends actual display resolution (e.g., 1920x1080).

**Server visibility calculation:**
```
tiles_wide  = screen_width / 32
tiles_high  = screen_height / 32
base_x      = tiles_wide / 2
base_y      = tiles_high / 2
buffer_x    = max(5, base_x * 0.2)   // 20% proportional buffer, minimum 5 tiles
buffer_y    = max(5, base_y * 0.2)
radius_x    = clamp(base_x + buffer_x, 15, 80)
radius_y    = clamp(base_y + buffer_y, 15, 80)
```

The server uses rectangular visibility (`abs(dx) <= radius_x && abs(dy) <= radius_y`) to determine which entities, NPCs, ground items, and events to send to the player. Each player has their own visibility radii based on their reported viewport. Widescreen resolutions produce a wider X radius than Y, matching the actual viewport shape and reducing unnecessary bandwidth.

### `view_range_update`

Server informs the client of its effective visibility radii. Sent when an admin overrides the player's view range, or when `sees_all` mode is toggled.

**Server -> Client:**
```json
{
  "type": "view_range_update",
  "data": {
    "radius_x": 36,
    "radius_y": 21,
    "sees_all": false
  }
}
```

| Field | Type | Description |
|-------|------|-------------|
| `radius_x` | int16 | Current server-side horizontal visibility radius in tiles (15-80) |
| `radius_y` | int16 | Current server-side vertical visibility radius in tiles (15-80) |
| `sees_all` | bool | If `true`, player receives all events on their current map regardless of distance |

**Notes:**
- This is informational — the server enforces the radii regardless. The client uses them to adjust rendering (e.g., fog of war distance, entity culling).
- When `sees_all` is `true`, the radii still reflect the last computed values but are effectively ignored server-side.
- Triggered by `/setviewrange` admin command. Not sent during normal `set_view_range` requests from the client.

### Admin Visibility Override

Admins can use `sees_all` mode which bypasses all distance checks, receiving every event on their current map regardless of position. This is controlled via the `/setviewrange` admin command.

---

## NPC Messages

### `npc_spawn`

An NPC entered visibility range.

**Server Broadcast:**
```json
{
  "type": "npc_spawn",
  "seq": 0,
  "data": {
    "entity_id": 5001,
    "template_id": 100,
    "name": "Orc Warrior",
    "x": 105,
    "y": 150,
    "direction": 4,
    "hp": 200,
    "max_hp": 200,
    "level": 25
  }
}
```

| Field | Type | Description |
|-------|------|-------------|
| `entity_id` | uint32 | Unique NPC entity ID |
| `template_id` | uint32 | NPC template/type ID |
| `name` | string | NPC display name |
| `x` | int16 | X coordinate |
| `y` | int16 | Y coordinate |
| `direction` | uint8 | Facing direction (0-7) |
| `hp` | int32 | Current HP |
| `max_hp` | int32 | Maximum HP |
| `level` | int16 | NPC level |

---

### `npc_despawn`

An NPC left visibility range.

**Server Broadcast:**
```json
{
  "type": "npc_despawn",
  "seq": 0,
  "data": {
    "entity_id": 5001
  }
}
```

| Field | Type | Description |
|-------|------|-------------|
| `entity_id` | uint32 | NPC entity ID |

---

### `npc_move`

An NPC moved.

**Server Broadcast:**
```json
{
  "type": "npc_move",
  "seq": 0,
  "data": {
    "entity_id": 5001,
    "x": 106,
    "y": 151,
    "direction": 2
  }
}
```

| Field | Type | Description |
|-------|------|-------------|
| `entity_id` | uint32 | NPC entity ID |
| `x` | int16 | New X coordinate |
| `y` | int16 | New Y coordinate |
| `direction` | uint8 | Facing direction (0-7) |

---

### `npc_attack`

An NPC attacked something.

**Server Broadcast:**
```json
{
  "type": "npc_attack",
  "seq": 0,
  "data": {
    "attacker_id": 5001,
    "target_id": 1001,
    "damage": 25,
    "is_critical": false
  }
}
```

| Field | Type | Description |
|-------|------|-------------|
| `attacker_id` | uint32 | NPC entity ID |
| `target_id` | uint32 | Target entity ID |
| `damage` | int32 | Damage dealt |
| `is_critical` | bool | Whether it was a critical hit |

---

### `npc_death`

An NPC died.

**Server Broadcast:**
```json
{
  "type": "npc_death",
  "seq": 0,
  "data": {
    "entity_id": 5001,
    "killer_id": 1001,
    "x": 106,
    "y": 151
  }
}
```

| Field | Type | Description |
|-------|------|-------------|
| `entity_id` | uint32 | NPC that died |
| `killer_id` | uint32 | Entity that killed (0 if environmental/unknown) |
| `x` | int16 | Death location X |
| `y` | int16 | Death location Y |

---

## Entity Info Messages

### `entity_info_request`

Request detailed information about an entity (player or NPC).

**Request:**
```json
{
  "type": "entity_info_request",
  "seq": 155,
  "data": {
    "entity_id": 1221
  }
}
```

| Field | Type | Required | Description |
|-------|------|----------|-------------|
| `entity_id` | uint32 | Yes | Entity ID to query |

---

### `entity_info_response`

Server responds with detailed entity information.

**Success Response (Player):**
```json
{
  "type": "entity_info_response",
  "seq": 155,
  "data": {
    "success": true,
    "entity": {
      "entity_id": 1221,
      "entity_type": "player",
      "name": "DarkKnight",
      "level": 45,
      "hp": 1250,
      "hp_max": 2000,
      "x": 128,
      "y": 256,
      "direction": 4,
      "faction": "aresden",
      "class_type": 0,
      "pk_count": 12,
      "guild_name": "BloodGuard"
    }
  }
}
```

**Success Response (NPC):**
```json
{
  "type": "entity_info_response",
  "seq": 156,
  "data": {
    "success": true,
    "entity": {
      "entity_id": 50001,
      "entity_type": "npc",
      "name": "Orc Warrior",
      "level": 25,
      "hp": 450,
      "hp_max": 800,
      "x": 95,
      "y": 142,
      "direction": 2,
      "template_id": 10
    }
  }
}
```

**Failure Response:**
```json
{
  "type": "entity_info_response",
  "seq": 157,
  "data": {
    "success": false,
    "error": "entity_not_found"
  }
}
```

#### Entity Info Response Object

**Common Fields (all entities):**

| Field | Type | Description |
|-------|------|-------------|
| `entity_id` | uint32 | Entity ID |
| `entity_type` | string | `"player"` or `"npc"` |
| `name` | string | Entity display name |
| `level` | int16 | Entity level |
| `hp` | int32 | Current hit points |
| `hp_max` | int32 | Maximum hit points |
| `x` | int16 | X coordinate |
| `y` | int16 | Y coordinate |
| `direction` | int16 | Facing direction (0-7) |

**Player-specific Fields (only when `entity_type` = `"player"`):**

| Field | Type | Description |
|-------|------|-------------|
| `faction` | string | `"aresden"`, `"elvine"`, or `"neutral"` |
| `class_type` | int16 | Class (0=Warrior, 1=Mage, 2=Archer) |
| `pk_count` | int32 | Total player kill count |
| `guild_name` | string | Guild name (only if player is in a guild) |

**NPC-specific Fields (only when `entity_type` = `"npc"`):**

| Field | Type | Description |
|-------|------|-------------|
| `template_id` | uint32 | NPC template ID |
| `npc_type` | string | NPC type (e.g., `"monster"`, `"vendor"`, `"guard"`) |

**Possible Errors:**

| Error Code | Description |
|------------|-------------|
| `entity_not_found` | No entity with the given ID exists |
| `invalid_request` | Missing or invalid entity_id field |
| `not_in_game` | Client is not in-game |
| `internal_error` | Required subsystems unavailable |

---

## NPC Interaction Messages

These messages handle shop transactions, bank operations, and dialog choices after the initial `player_interact_request`/`player_interact_response` exchange has opened the interaction.

### `shop_buy_request`

Request to buy an item from a shop NPC.

**Request:**
```json
{
  "type": "shop_buy_request",
  "seq": 220,
  "data": {
    "npc_entity_id": 5001,
    "item_template_id": 10,
    "count": 1
  }
}
```

| Field | Type | Required | Default | Description |
|-------|------|----------|---------|-------------|
| `npc_entity_id` | uint32 | Yes | - | Entity ID of the shop NPC |
| `item_template_id` | uint32 | Yes | - | Template ID of the item to buy |
| `count` | int16 | No | 1 | Number of items to buy |

---

### `shop_buy_response`

Server confirms or rejects the purchase.

**Success Response:**
```json
{
  "type": "shop_buy_response",
  "seq": 220,
  "data": {
    "success": true,
    "item_name": "Short Sword",
    "count": 1,
    "price_paid": 500,
    "gold_remaining": 14500
  }
}
```

**Failure Response:**
```json
{
  "type": "shop_buy_response",
  "seq": 220,
  "data": {
    "success": false,
    "error": "Not enough gold"
  }
}
```

| Field | Type | Description |
|-------|------|-------------|
| `success` | bool | Whether purchase succeeded |
| `item_name` | string | Name of item purchased (on success) |
| `count` | int16 | Number of items purchased (on success) |
| `price_paid` | int32 | Total gold spent (on success) |
| `gold_remaining` | int32 | Player's gold after purchase (on success) |
| `error` | string | Error message (on failure) |

**Possible Errors:**
- `Not enough gold`
- `Inventory full`
- `Item not sold here`
- `NPC not found`
- `Not in range`

---

### `shop_sell_request`

Request a price quote for selling an item to a shop NPC. This does not complete the sale -- the client must follow up with `shop_sell_confirm_request` to finalize.

**Request:**
```json
{
  "type": "shop_sell_request",
  "seq": 221,
  "data": {
    "npc_entity_id": 5001,
    "inventory_slot": 3,
    "count": 1
  }
}
```

| Field | Type | Required | Default | Description |
|-------|------|----------|---------|-------------|
| `npc_entity_id` | uint32 | Yes | - | Entity ID of the shop NPC |
| `inventory_slot` | int16 | Yes | - | Inventory slot of the item to sell |
| `count` | int16 | No | - | Number of items to sell (for stackable items; omit for all) |

---

### `shop_sell_response`

Server responds with the offered price for the item.

**Success Response:**
```json
{
  "type": "shop_sell_response",
  "seq": 221,
  "data": {
    "success": true,
    "item_name": "Short Sword",
    "offered_price": 250,
    "durability": 45
  }
}
```

**Failure Response:**
```json
{
  "type": "shop_sell_response",
  "seq": 221,
  "data": {
    "success": false,
    "error": "Item cannot be sold"
  }
}
```

| Field | Type | Description |
|-------|------|-------------|
| `success` | bool | Whether price quote succeeded |
| `item_name` | string | Name of item (on success) |
| `offered_price` | int32 | Gold the NPC will pay (on success) |
| `durability` | int16 | Current durability of the item (on success) |
| `error` | string | Error message (on failure) |

**Possible Errors:**
- `Item cannot be sold`
- `Invalid inventory slot`
- `NPC not found`
- `Not in range`

---

### `shop_sell_confirm_request`

Confirm the sale of an item to a shop NPC after receiving a price quote via `shop_sell_response`.

**Request:**
```json
{
  "type": "shop_sell_confirm_request",
  "seq": 222,
  "data": {
    "npc_entity_id": 5001,
    "inventory_slot": 3,
    "count": 1
  }
}
```

| Field | Type | Required | Default | Description |
|-------|------|----------|---------|-------------|
| `npc_entity_id` | uint32 | Yes | - | Entity ID of the shop NPC |
| `inventory_slot` | int16 | Yes | - | Inventory slot of the item to sell |
| `count` | int16 | No | - | Number of items to sell (for stackable items; omit for all) |

---

### `shop_sell_confirm_response`

Server confirms or rejects the finalized sale.

**Success Response:**
```json
{
  "type": "shop_sell_confirm_response",
  "seq": 222,
  "data": {
    "success": true,
    "gold_received": 250,
    "gold_total": 14750
  }
}
```

**Failure Response:**
```json
{
  "type": "shop_sell_confirm_response",
  "seq": 222,
  "data": {
    "success": false,
    "error": "Item no longer in slot"
  }
}
```

| Field | Type | Description |
|-------|------|-------------|
| `success` | bool | Whether sale succeeded |
| `gold_received` | int32 | Gold earned from the sale (on success) |
| `gold_total` | int32 | Player's total gold after sale (on success) |
| `error` | string | Error message (on failure) |

**Possible Errors:**
- `Item no longer in slot`
- `Item cannot be sold`
- `NPC not found`
- `Not in range`

---

### `shop_repair_request`

Request a cost quote for repairing an item at a shop NPC. This does not complete the repair -- the client must follow up with `shop_repair_confirm_request` to finalize.

**Request:**
```json
{
  "type": "shop_repair_request",
  "seq": 223,
  "data": {
    "npc_entity_id": 5001,
    "inventory_slot": 0
  }
}
```

| Field | Type | Required | Description |
|-------|------|----------|-------------|
| `npc_entity_id` | uint32 | Yes | Entity ID of the shop NPC |
| `inventory_slot` | int16 | Yes | Inventory slot of the item to repair |

---

### `shop_repair_response`

Server responds with the repair cost estimate.

**Success Response:**
```json
{
  "type": "shop_repair_response",
  "seq": 223,
  "data": {
    "success": true,
    "item_name": "Long Sword",
    "repair_cost": 350,
    "durability": 25
  }
}
```

**Failure Response:**
```json
{
  "type": "shop_repair_response",
  "seq": 223,
  "data": {
    "success": false,
    "error": "Item does not need repair"
  }
}
```

| Field | Type | Description |
|-------|------|-------------|
| `success` | bool | Whether repair quote succeeded |
| `item_name` | string | Name of item (on success) |
| `repair_cost` | int32 | Gold cost to repair (on success) |
| `durability` | int16 | Current durability before repair (on success) |
| `error` | string | Error message (on failure) |

**Possible Errors:**
- `Item does not need repair`
- `Item cannot be repaired`
- `Invalid inventory slot`
- `NPC not found`
- `Not in range`

---

### `shop_repair_confirm_request`

Confirm the repair of an item after receiving a cost quote via `shop_repair_response`.

**Request:**
```json
{
  "type": "shop_repair_confirm_request",
  "seq": 224,
  "data": {
    "npc_entity_id": 5001,
    "inventory_slot": 0
  }
}
```

| Field | Type | Required | Description |
|-------|------|----------|-------------|
| `npc_entity_id` | uint32 | Yes | Entity ID of the shop NPC |
| `inventory_slot` | int16 | Yes | Inventory slot of the item to repair |

---

### `shop_repair_confirm_response`

Server confirms or rejects the finalized repair.

**Success Response:**
```json
{
  "type": "shop_repair_confirm_response",
  "seq": 224,
  "data": {
    "success": true,
    "new_durability": 50,
    "gold_spent": 350,
    "gold_remaining": 14150
  }
}
```

**Failure Response:**
```json
{
  "type": "shop_repair_confirm_response",
  "seq": 224,
  "data": {
    "success": false,
    "error": "Not enough gold"
  }
}
```

| Field | Type | Description |
|-------|------|-------------|
| `success` | bool | Whether repair succeeded |
| `new_durability` | int16 | Item's durability after repair (on success) |
| `gold_spent` | int32 | Gold spent on repair (on success) |
| `gold_remaining` | int32 | Player's gold after repair (on success) |
| `error` | string | Error message (on failure) |

**Possible Errors:**
- `Not enough gold`
- `Item no longer in slot`
- `Item cannot be repaired`
- `NPC not found`
- `Not in range`

---

### `bank_deposit_request`

Request to deposit an item from inventory into the bank.

**Request:**
```json
{
  "type": "bank_deposit_request",
  "seq": 230,
  "data": {
    "npc_entity_id": 5002,
    "inventory_slot": 5
  }
}
```

| Field | Type | Required | Description |
|-------|------|----------|-------------|
| `npc_entity_id` | uint32 | Yes | Entity ID of the bank NPC |
| `inventory_slot` | int16 | Yes | Inventory slot of the item to deposit |

---

### `bank_deposit_response`

Server confirms or rejects the deposit.

**Success Response:**
```json
{
  "type": "bank_deposit_response",
  "seq": 230,
  "data": {
    "success": true,
    "item_name": "Health Potion"
  }
}
```

**Failure Response:**
```json
{
  "type": "bank_deposit_response",
  "seq": 230,
  "data": {
    "success": false,
    "error": "Bank is full"
  }
}
```

| Field | Type | Description |
|-------|------|-------------|
| `success` | bool | Whether deposit succeeded |
| `item_name` | string | Name of item deposited (on success) |
| `error` | string | Error message (on failure) |

**Possible Errors:**
- `Bank is full`
- `Invalid inventory slot`
- `NPC not found`
- `Not in range`

---

### `bank_withdraw_request`

Request to withdraw an item from the bank into inventory.

**Request:**
```json
{
  "type": "bank_withdraw_request",
  "seq": 231,
  "data": {
    "npc_entity_id": 5002,
    "bank_slot": 0
  }
}
```

| Field | Type | Required | Description |
|-------|------|----------|-------------|
| `npc_entity_id` | uint32 | Yes | Entity ID of the bank NPC |
| `bank_slot` | int16 | Yes | Bank slot of the item to withdraw |

---

### `bank_withdraw_response`

Server confirms or rejects the withdrawal.

**Success Response:**
```json
{
  "type": "bank_withdraw_response",
  "seq": 231,
  "data": {
    "success": true,
    "item_name": "Health Potion"
  }
}
```

**Failure Response:**
```json
{
  "type": "bank_withdraw_response",
  "seq": 231,
  "data": {
    "success": false,
    "error": "Inventory full"
  }
}
```

| Field | Type | Description |
|-------|------|-------------|
| `success` | bool | Whether withdrawal succeeded |
| `item_name` | string | Name of item withdrawn (on success) |
| `error` | string | Error message (on failure) |

**Possible Errors:**
- `Inventory full`
- `Invalid bank slot`
- `NPC not found`
- `Not in range`

---

### `dialog_choice_request`

Send a dialog choice to an NPC during a dialog interaction.

**Request:**
```json
{
  "type": "dialog_choice_request",
  "seq": 240,
  "data": {
    "npc_entity_id": 5003,
    "node_id": "start",
    "choice_index": 0
  }
}
```

| Field | Type | Required | Description |
|-------|------|----------|-------------|
| `npc_entity_id` | uint32 | Yes | Entity ID of the dialog NPC |
| `node_id` | string | Yes | Current dialog node ID |
| `choice_index` | int16 | Yes | Index of the chosen option in the `options` array |

---

### `dialog_choice_response`

Server responds with the result of the dialog choice.

**Success Response (goto_node -- continues dialog):**
```json
{
  "type": "dialog_choice_response",
  "seq": 240,
  "data": {
    "success": true,
    "action": "goto_node",
    "node_id": "town_info",
    "text": "This town was founded many years ago by the great king...",
    "options": [
      { "label": "Tell me more", "action": "goto_node", "next_node": "town_history" },
      { "label": "Back", "action": "goto_node", "next_node": "start" },
      { "label": "Goodbye", "action": "close" }
    ]
  }
}
```

**Success Response (open_shop -- transitions to shop):**
```json
{
  "type": "dialog_choice_response",
  "seq": 241,
  "data": {
    "success": true,
    "action": "open_shop"
  }
}
```

**Success Response (open_bank -- transitions to bank):**
```json
{
  "type": "dialog_choice_response",
  "seq": 242,
  "data": {
    "success": true,
    "action": "open_bank"
  }
}
```

**Success Response (close -- ends dialog):**
```json
{
  "type": "dialog_choice_response",
  "seq": 243,
  "data": {
    "success": true,
    "action": "close"
  }
}
```

**Success Response (not_implemented):**
```json
{
  "type": "dialog_choice_response",
  "seq": 244,
  "data": {
    "success": true,
    "action": "not_implemented"
  }
}
```

**Failure Response:**
```json
{
  "type": "dialog_choice_response",
  "seq": 240,
  "data": {
    "success": false,
    "error": "Invalid choice index"
  }
}
```

| Field | Type | Description |
|-------|------|-------------|
| `success` | bool | Whether the choice was processed |
| `action` | string | Result action: `"goto_node"`, `"close"`, `"open_shop"`, `"open_bank"`, `"not_implemented"` |
| `node_id` | string | New dialog node ID (only for `"goto_node"`) |
| `text` | string | Dialog text for the new node (only for `"goto_node"`) |
| `options` | array | Array of dialog option objects (only for `"goto_node"`, see Dialog Option Object) |
| `error` | string | Error message (on failure) |

**Possible Errors:**
- `Invalid choice index`
- `Invalid node`
- `NPC not found`
- `Not in range`
- `No active dialog`

---

## Crafting Messages

### `manufacture_list_request`

Request available manufacturing recipes for the player.

```json
{
  "type": "manufacture_list_request",
  "seq": 1,
  "data": {}
}
```

### `manufacture_list_response`

```json
{
  "type": "manufacture_list_response",
  "seq": 1,
  "data": {
    "recipes": [
      {
        "id": 0,
        "name": "Sword",
        "skill_req": 10,
        "success_rate": 60,
        "ingredients": [
          { "item_id": 500, "count": 3 },
          { "item_id": 501, "count": 1 }
        ]
      }
    ]
  }
}
```

| Field | Type | Description |
|-------|------|-------------|
| `recipes` | array | Array of available recipe objects |
| `recipes[].id` | int | Recipe index (0-based) |
| `recipes[].name` | string | Result item name |
| `recipes[].skill_req` | int | Minimum manufacturing skill required |
| `recipes[].success_rate` | int | Base success percentage (0-100) |
| `recipes[].ingredients` | array | Array of { item_id, count } |

### `manufacture_request`

Attempt to craft a manufacturing recipe.

```json
{
  "type": "manufacture_request",
  "seq": 2,
  "data": {
    "recipe_index": 0
  }
}
```

| Field | Type | Description |
|-------|------|-------------|
| `recipe_index` | int | Index of the recipe to craft |

### `manufacture_response`

```json
{
  "type": "manufacture_response",
  "seq": 2,
  "data": {
    "success": true,
    "item_name": "Sword",
    "exp_gained": 3
  }
}
```

```json
{
  "type": "manufacture_response",
  "seq": 2,
  "data": {
    "success": false,
    "reason": "insufficient_materials"
  }
}
```

| Field | Type | Description |
|-------|------|-------------|
| `success` | bool | Whether crafting succeeded |
| `item_name` | string | Name of crafted item (on success) |
| `exp_gained` | int | Manufacturing XP gained |
| `reason` | string | Failure reason: `"insufficient_skill"`, `"insufficient_materials"`, `"inventory_full"` |

### `alchemy_list_request`

Request available alchemy recipes for the player.

```json
{
  "type": "alchemy_list_request",
  "seq": 3,
  "data": {}
}
```

### `alchemy_list_response`

```json
{
  "type": "alchemy_list_response",
  "seq": 3,
  "data": {
    "recipes": [
      {
        "id": 1,
        "name": "HealthPotion",
        "skill_limit": 0,
        "difficulty": 20,
        "ingredients": [
          { "item_id": 500, "count": 1 }
        ]
      }
    ]
  }
}
```

| Field | Type | Description |
|-------|------|-------------|
| `recipes` | array | Array of available recipe objects |
| `recipes[].id` | int | Recipe ID (from YAML) |
| `recipes[].name` | string | Result item name |
| `recipes[].skill_limit` | int | Minimum alchemy skill required |
| `recipes[].difficulty` | int | Recipe difficulty (affects success chance) |
| `recipes[].ingredients` | array | Array of { item_id, count } |

### `alchemy_request`

Attempt to craft an alchemy recipe.

```json
{
  "type": "alchemy_request",
  "seq": 4,
  "data": {
    "recipe_id": 1
  }
}
```

| Field | Type | Description |
|-------|------|-------------|
| `recipe_id` | int | ID of the recipe to craft |

### `alchemy_response`

```json
{
  "type": "alchemy_response",
  "seq": 4,
  "data": {
    "success": true,
    "item_name": "HealthPotion",
    "exp_gained": 2
  }
}
```

```json
{
  "type": "alchemy_response",
  "seq": 4,
  "data": {
    "success": false,
    "reason": "insufficient_skill"
  }
}
```

| Field | Type | Description |
|-------|------|-------------|
| `success` | bool | Whether crafting succeeded |
| `item_name` | string | Name of crafted item (on success) |
| `exp_gained` | int | Alchemy XP gained |
| `reason` | string | Failure reason: `"insufficient_skill"`, `"insufficient_materials"`, `"inventory_full"` |

---

## Connection State Flow

```
Connected
    |
    v
[login_request] --> Authenticated
    |
    v
[get_characters_request] --> Character List
    |
    v
[enter_game_request] --> In Game
    |
    +-- [player_move_request] --> Movement
    |
    +-- [player_attack_request] --> Combat
    |
    +-- [chat_message] --> Chat
    |
    +-- [command_request] --> Commands
    |
    +-- [logout_request] --> Authenticated
    |
    +-- [disconnect] --> (connection closed)
```

## Visibility System

- **Visibility Radius:** Dynamic based on client screen resolution
- **Default Radius:** 20 tiles (for 640x480)
- **Calculation:** `max(screen_width, screen_height) / 32 / 2 + 5` tiles
- **When player moves:**
  - Entities entering range: `entity_spawn` or `npc_spawn` sent to player
  - Entities leaving range: `entity_despawn` or `npc_despawn` sent to player
  - Player's position: `player_position_update` broadcast to nearby
- **When player enters game:** `enter_game_response` contains all visible entities
- **When player teleports/changes map:** `player_teleport` with new area entities

## Error Handling

1. **Request Errors:** Responded with same `seq` and error field
2. **Connection Errors:** Connection closed by server
3. **Rate Limiting:** `error` message with `RATE_LIMITED` code

## Best Practices for Clients

1. **Track sequence numbers** for request/response matching
2. **Handle broadcasts** (seq=0) separately from responses
3. **Maintain entity cache** updated by spawn/despawn/position messages
4. **Reconnect logic:** Re-authenticate and enter game on disconnect
5. **Optimistic movement:** Show movement immediately, correct on rejection
6. **Update view range** when resolution or view mode changes using `set_view_range`
7. **Handle `set_render_mode`** to switch rendering modes as directed by the server
