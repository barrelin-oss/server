# Movement & Teleportation

[← Back to Protocol Index](../JSON_PROTOCOL.md)

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
