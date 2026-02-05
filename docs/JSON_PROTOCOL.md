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
        "gender": 0,
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
        "gender": 1,
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
| `gender` | int16 | Gender (0=Male, 1=Female) |
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
    "gender": 0,
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
| `gender` | int16 | No | 0 | Gender (0=Male, 1=Female) |
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
| `screen_width` | int16 | No | 640 | Client screen width for visibility calculation |
| `screen_height` | int16 | No | 480 | Client screen height for visibility calculation |

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
      "gender": 0,
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
      ]
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
| `gender` | int16 | Gender (0=Male, 1=Female) |
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

### `ground_item_removed`

Broadcast to nearby players (excluding the picker) when an item is picked up from the ground.

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
- The picker does NOT receive this broadcast (they get the response instead)
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

Server responds with interaction result.

**Success Response:**
```json
{
  "type": "player_interact_response",
  "seq": 210,
  "data": {
    "success": true,
    "result": {
      "success": true,
      "target_id": 5001,
      "interaction_type": "dialog",
      "interaction_data": {
        "npc_name": "Shop Keeper",
        "dialog_text": "Welcome to my shop!",
        "options": ["Buy", "Sell", "Leave"]
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
| `interaction_type` | string | Type: `"dialog"`, `"shop"`, `"bank"`, etc. |
| `interaction_data` | object | Type-specific data |

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

## View/Resolution Messages

### `set_view_range`

Client updates visibility radius based on screen resolution.

**Request:**
```json
{
  "type": "set_view_range",
  "seq": 500,
  "data": {
    "screen_width": 1920,
    "screen_height": 1080
  }
}
```

| Field | Type | Required | Description |
|-------|------|----------|-------------|
| `screen_width` | int16 | No | Client screen width in pixels |
| `screen_height` | int16 | No | Client screen height in pixels |

The server calculates visibility radius as: `max(width, height) / 32 / 2 + 5` tiles.

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
6. **Update view range** when resolution changes using `set_view_range`
