# Authentication & Game Entry

[← Back to Protocol Index](../JSON_PROTOCOL.md)

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
      "hunger_level": 85,
      "guild_name": "Knights",
      "guild_tag": "KNT",
      "guild_rank": 3
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
      { "skill_id": 1, "level": 50, "total_uses": 15000, "uses_this_level": 320, "uses_to_next_level": 5100 },
      { "skill_id": 3, "level": 35, "total_uses": 8000, "uses_this_level": 0, "uses_to_next_level": 900 },
      { "skill_id": 10, "level": 20, "total_uses": 3000, "uses_this_level": 100, "uses_to_next_level": 525 }
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
| `total_uses` | int32 | Lifetime use count |
| `uses_this_level` | int32 | Uses accumulated toward next level |
| `uses_to_next_level` | int32 | Uses required to reach next level |

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

#### Environment Object

The `world.environment` sub-object in `enter_game_response` provides the initial day/night and weather state for the player's current map. After login, the server sends periodic [`environment_update`](#environment_update) broadcasts every ~10 seconds and immediately on teleport.

| Field | Type | Description |
|-------|------|-------------|
| `hour` | uint8 | Game clock hour (0-23) |
| `minute` | uint8 | Game clock minute (0-59) |
| `is_day` | bool | Whether the game clock is in daytime (6:00-17:59) |
| `weather` | uint8 | Weather type for current map (see [Weather Types](#environment_update)) |

**Notes:**
- Maps with `is_fixed_day_mode` always report `is_day: true` and `weather: 0`
- Weather types 1-3 (rain) appear on normal maps; types 4-6 (snow) appear on snow-enabled maps
- Weather cycling is per-map: each map independently starts/stops weather events (1-in-30 chance per 10s tick, 3-10 minute duration)

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
