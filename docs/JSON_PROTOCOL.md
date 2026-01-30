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
    "character_id": 1
  }
}
```

| Field | Type | Required | Description |
|-------|------|----------|-------------|
| `character_id` | uint32 | Yes | ID of character to play |

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
          "direction": 2
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

A new entity entered visibility range (20 tiles).

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

Request to move the player character.

**Request:**
```json
{
  "type": "player_move_request",
  "seq": 100,
  "data": {
    "x": 101,
    "y": 151,
    "direction": 2
  }
}
```

| Field | Type | Required | Description |
|-------|------|----------|-------------|
| `x` | int16 | Yes | Target X coordinate |
| `y` | int16 | Yes | Target Y coordinate |
| `direction` | int16 | No | Direction to face (0-7) |

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

### `player_position_update`

Broadcast to nearby players when someone moves.

**Server Broadcast:**
```json
{
  "type": "player_position_update",
  "seq": 0,
  "data": {
    "entity_id": 1001,
    "x": 101,
    "y": 151,
    "direction": 2
  }
}
```

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
    +-- [logout_request] --> Authenticated
    |
    +-- [disconnect] --> (connection closed)
```

## Visibility System

- **Visibility Radius:** 20 tiles (Chebyshev distance)
- **When player moves:**
  - Entities entering range: `entity_spawn` sent to player
  - Entities leaving range: `entity_despawn` sent to player
  - Player's position: `player_position_update` broadcast to nearby
- **When player enters game:** `world_init` contains all visible entities
- **When player teleports/changes map:** `world_init` with new area entities

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
