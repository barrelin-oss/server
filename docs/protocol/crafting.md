# Crafting, Mining & Fishing

[← Back to Protocol Index](../JSON_PROTOCOL.md)

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
    "item_name": "Sword"
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
    "item_name": "HealthPotion"
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
| `reason` | string | Failure reason: `"insufficient_skill"`, `"insufficient_materials"`, `"inventory_full"` |

---

## Mining Messages

### `mine_request`

Client requests to mine a mineral node at the specified position.

**Client Request:**
```json
{
  "type": "mine_request",
  "seq": 200,
  "data": {
    "target_x": 50,
    "target_y": 75
  }
}
```

| Field | Type | Description |
|-------|------|-------------|
| `target_x` | int16 | X position of mineral node to mine |
| `target_y` | int16 | Y position of mineral node to mine |

---

### `mine_response`

Server response to a mining attempt. On success, includes the mined item. On failure, includes the reason.

**Success Response:**
```json
{
  "type": "mine_response",
  "seq": 200,
  "data": {
    "success": true,
    "item_name": "Iron Ore",
    "template_id": 350,
    "node_depleted": false
  }
}
```

**Failure Response:**
```json
{
  "type": "mine_response",
  "seq": 200,
  "data": {
    "success": false,
    "error": "no_node_at_position"
  }
}
```

| Field | Type | Required | Description |
|-------|------|----------|-------------|
| `success` | bool | Yes | Whether mining succeeded |
| `item_name` | string | No | Name of mined item (on success) |
| `template_id` | int32 | No | Item template ID of mined item (on success) |
| `node_depleted` | bool | No | Whether the mineral node was exhausted by this action |
| `error` | string | No | Failure reason (on failure) |

---

### `mineral_spawn`

Broadcast when a mineral node appears on the map (initial spawn or respawn).

**Server Broadcast:**
```json
{
  "type": "mineral_spawn",
  "seq": 0,
  "data": {
    "node_id": 1001,
    "mineral_type": 1,
    "x": 50,
    "y": 75
  }
}
```

| Field | Type | Description |
|-------|------|-------------|
| `node_id` | uint32 | Unique node identifier |
| `mineral_type` | uint8 | Type of mineral (determines appearance and loot) |
| `x` | int16 | X position on map |
| `y` | int16 | Y position on map |

---

### `mineral_despawn`

Broadcast when a mineral node is removed (depleted or despawned).

**Server Broadcast:**
```json
{
  "type": "mineral_despawn",
  "seq": 0,
  "data": {
    "node_id": 1001,
    "x": 50,
    "y": 75
  }
}
```

| Field | Type | Description |
|-------|------|-------------|
| `node_id` | uint32 | Node identifier being removed |
| `x` | int16 | X position |
| `y` | int16 | Y position |

---

## Fishing Messages

Fishing uses an engagement-based mechanic: the player activates the fishing skill near water, engages a fish node, watches catch chance fluctuate every 4 seconds, then chooses when to attempt a catch.

### `fish_skill_request`

**Direction:** Client → Server

Activates the fishing skill. Player must be near a water tile with a fish node within 2 tiles.

```json
{
  "type": "fish_skill_request",
  "seq": 1,
  "data": {}
}
```

### `fish_skill_response`

**Direction:** Server → Client

Confirms whether fishing activation succeeded.

```json
{
  "type": "fish_skill_response",
  "seq": 1,
  "data": {
    "success": true
  }
}
```

**Error response:**

```json
{
  "type": "fish_skill_response",
  "seq": 1,
  "data": {
    "success": false,
    "error": "no_fish_nearby"
  }
}
```

| Field | Type | Description |
|-------|------|-------------|
| `success` | bool | Whether fishing was activated |
| `error` | string? | Error reason if failed: `"no_fish_nearby"`, `"already_fishing"`, `"no_fishing_skill"` |

### `fish_engaged`

**Direction:** Server → Client (push)

Sent when the player successfully engages a fish node. Opens the fishing UI with fish preview and initial catch chance.

```json
{
  "type": "fish_engaged",
  "seq": 0,
  "data": {
    "fish_name": "Trout",
    "visual_type": 2,
    "catch_chance": 1
  }
}
```

| Field | Type | Description |
|-------|------|-------------|
| `fish_name` | string | Display name of the engaged fish |
| `visual_type` | uint8 | Visual/sprite type for the fish |
| `catch_chance` | int32 | Initial catch chance percentage (starts at 1%) |

### `fish_chance_update`

**Direction:** Server → Client (push, every 4s)

Periodic update of the fluctuating catch chance while engaged with a fish.

```json
{
  "type": "fish_chance_update",
  "seq": 0,
  "data": {
    "catch_chance": 35
  }
}
```

| Field | Type | Description |
|-------|------|-------------|
| `catch_chance` | int32 | Current catch chance percentage (0-100) |

### `fish_catch_request`

**Direction:** Client → Server

Player attempts to catch the currently engaged fish.

```json
{
  "type": "fish_catch_request",
  "seq": 2,
  "data": {}
}
```

### `fish_catch_response`

**Direction:** Server → Client

Result of the catch attempt.

**Success:**

```json
{
  "type": "fish_catch_response",
  "seq": 0,
  "data": {
    "result": "success",
    "item_name": "Trout",
    "template_id": 300
  }
}
```

**Failure:**

```json
{
  "type": "fish_catch_response",
  "seq": 0,
  "data": {
    "result": "fail"
  }
}
```

| Field | Type | Description |
|-------|------|-------------|
| `result` | string | `"success"`, `"fail"`, or `"canceled"` |
| `item_name` | string? | Name of caught item (on success) |
| `template_id` | int32? | Item template ID of caught item (on success) |

### `fish_spawn_broadcast`

**Direction:** Server → Clients

A fish node has appeared on the map, visible to nearby players.

```json
{
  "type": "fish_spawn_broadcast",
  "seq": 0,
  "data": {
    "fish_index": 5,
    "visual_type": 2,
    "x": 128,
    "y": 256
  }
}
```

| Field | Type | Description |
|-------|------|-------------|
| `fish_index` | uint32 | Unique fish node identifier |
| `visual_type` | uint8 | Visual/sprite type for the fish |
| `x` | int16 | X tile position |
| `y` | int16 | Y tile position |

### `fish_despawn_broadcast`

**Direction:** Server → Clients

A fish node has been removed (caught, timed out, or despawned).

```json
{
  "type": "fish_despawn_broadcast",
  "seq": 0,
  "data": {
    "fish_index": 5,
    "x": 128,
    "y": 256
  }
}
```

| Field | Type | Description |
|-------|------|-------------|
| `fish_index` | uint32 | Fish node identifier being removed |
| `x` | int16 | X position |
| `y` | int16 | Y position |

---
