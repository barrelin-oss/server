# War System

[← Back to Protocol Index](../JSON_PROTOCOL.md)

## War System Messages

The war system covers three distinct war modes: **Crusade** (faction vs faction territory war), **Heldenian** (arena-based faction battle), and **Apocalypse** (PvE wave defense). This section documents all protocol messages across Phases 1-6 of the war system implementation.

### Crusade Messages (Phases 1-3)

#### `crusade_started`

**Direction:** Server → Client (broadcast)

Broadcast to all online players when a Crusade war begins.

```json
{
  "type": "crusade_started",
  "seq": 0,
  "data": {
    "war_id": 42,
    "crusade_guid": "a1b2c3d4-e5f6-7890-abcd-ef1234567890"
  }
}
```

| Field | Type | Description |
|-------|------|-------------|
| `war_id` | uint32 | Unique war instance identifier |
| `crusade_guid` | string | Globally unique Crusade identifier |

---

#### `crusade_ended`

**Direction:** Server → Client (broadcast)

Broadcast to all online players when a Crusade concludes.

```json
{
  "type": "crusade_ended",
  "seq": 0,
  "data": {
    "war_id": 42,
    "winner": "aresden",
    "your_contribution": 150
  }
}
```

| Field | Type | Description |
|-------|------|-------------|
| `war_id` | uint32 | War instance identifier |
| `winner` | string | Winning faction: `"aresden"`, `"elvine"`, or `"draw"` |
| `your_contribution` | int32 | The receiving player's personal contribution score |

---

#### `crusade_status_update`

**Direction:** Server → Client (push)

Periodic status update sent to players participating in an active Crusade.

```json
{
  "type": "crusade_status_update",
  "seq": 0,
  "data": {
    "active": true,
    "elapsed_s": 300,
    "duty": 2,
    "construction_pts": 45,
    "contribution": 80
  }
}
```

| Field | Type | Description |
|-------|------|-------------|
| `active` | bool | Whether the Crusade is currently active |
| `elapsed_s` | uint32 | Seconds elapsed since Crusade started |
| `duty` | uint8 | Player's assigned duty: 1=soldier, 2=constructor, 3=commander |
| `construction_pts` | int32 | Player's available construction points |
| `contribution` | int32 | Player's accumulated contribution score |

---

#### `select_duty_request`

**Direction:** Client → Server

Player selects their role/duty for the active Crusade.

```json
{
  "type": "select_duty_request",
  "seq": 1,
  "data": {
    "duty": 2
  }
}
```

| Field | Type | Description |
|-------|------|-------------|
| `duty` | uint8 | Duty to select: 1=soldier, 2=constructor, 3=commander |

---

#### `select_duty_response`

**Direction:** Server → Client

Result of duty selection.

```json
{
  "type": "select_duty_response",
  "seq": 1,
  "data": {
    "success": true,
    "duty": 2,
    "construction_points": 100
  }
}
```

**Error response:**

```json
{
  "type": "select_duty_response",
  "seq": 1,
  "data": {
    "success": false,
    "error": "commander_slots_full"
  }
}
```

| Field | Type | Description |
|-------|------|-------------|
| `success` | bool | Whether duty selection succeeded |
| `duty` | uint8 | Assigned duty (on success) |
| `construction_points` | int32 | Initial construction points granted for this duty (on success) |
| `error` | string? | Error reason if failed: `"commander_slots_full"`, `"no_active_crusade"`, `"already_assigned"` |

---

#### `crusade_strike_point_update`

**Direction:** Server → Client (push)

Sent to participants with current HP status of all strike points for a faction.

```json
{
  "type": "crusade_strike_point_update",
  "seq": 0,
  "data": {
    "faction": "aresden",
    "strike_points": [
      { "id": 1, "hp": 800, "max_hp": 1000 },
      { "id": 2, "hp": 1000, "max_hp": 1000 },
      { "id": 3, "hp": 200, "max_hp": 1000 }
    ]
  }
}
```

| Field | Type | Description |
|-------|------|-------------|
| `faction` | string | Faction owning these strike points: `"aresden"` or `"elvine"` |
| `strike_points` | array | Array of strike point status objects |
| `strike_points[].id` | uint32 | Strike point identifier |
| `strike_points[].hp` | int32 | Current hit points |
| `strike_points[].max_hp` | int32 | Maximum hit points |

---

#### `crusade_meteor_warning`

**Direction:** Server → Client (broadcast)

Warning broadcast before a meteor strike hits a faction's territory.

```json
{
  "type": "crusade_meteor_warning",
  "seq": 0,
  "data": {
    "target_faction": "elvine",
    "target_map": "elvine_farm",
    "time_until_impact_ms": 10000
  }
}
```

| Field | Type | Description |
|-------|------|-------------|
| `target_faction` | string | Faction being targeted |
| `target_map` | string | Map where the meteor will land |
| `time_until_impact_ms` | uint32 | Milliseconds until meteor impact |

---

#### `crusade_meteor_hit`

**Direction:** Server → Client (broadcast)

Broadcast when a meteor strike lands.

```json
{
  "type": "crusade_meteor_hit",
  "seq": 0,
  "data": {
    "target_map": "elvine_farm",
    "damage_per_strike_point": 250,
    "esg_count": 3
  }
}
```

| Field | Type | Description |
|-------|------|-------------|
| `target_map` | string | Map where the meteor landed |
| `damage_per_strike_point` | int32 | Damage dealt to each strike point on this map |
| `esg_count` | int32 | Number of Energy Shield Generators active (reduces damage) |

---

#### `crusade_meteor_result`

**Direction:** Server → Client (broadcast)

Sent after a meteor strike resolves, summarizing the aftermath.

```json
{
  "type": "crusade_meteor_result",
  "seq": 0,
  "data": {
    "faction": "elvine",
    "structures_remaining": 2,
    "casualties": 15,
    "is_victory": false
  }
}
```

| Field | Type | Description |
|-------|------|-------------|
| `faction` | string | Faction that was hit |
| `structures_remaining` | int32 | Number of structures still standing |
| `casualties` | int32 | Number of casualties from the strike |
| `is_victory` | bool | Whether this strike resulted in a Crusade victory for the attacking side |

---

#### `crusade_mana_update`

**Direction:** Server → Client (push, commanders only)

Sent to players with the commander duty, showing each faction's accumulated mana.

```json
{
  "type": "crusade_mana_update",
  "seq": 0,
  "data": {
    "aresden_mana": 4500,
    "elvine_mana": 3200
  }
}
```

| Field | Type | Description |
|-------|------|-------------|
| `aresden_mana` | int32 | Aresden faction's total accumulated mana |
| `elvine_mana` | int32 | Elvine faction's total accumulated mana |

---

#### `crusade_mp_restore`

**Direction:** Server → Client (broadcast)

Sent to all players who can see a mana collector during MP restoration tick (every 5s).

```json
{
  "type": "crusade_mp_restore",
  "seq": 0,
  "data": {
    "source_x": 128,
    "source_y": 130,
    "radius": 5,
    "your_restore": 12
  }
}
```

| Field | Type | Description |
|-------|------|-------------|
| `source_x` | int16 | Mana collector X position |
| `source_y` | int16 | Mana collector Y position |
| `radius` | int32 | MP restoration radius in tiles (rectangular) |
| `your_restore` | int32 | MP restored to this player (0 if out of range or wrong faction) |

---

#### `crusade_construction_point_update`

**Direction:** Server → Client (push)

Sent when a player's construction points or contribution change during a Crusade.

```json
{
  "type": "crusade_construction_point_update",
  "seq": 0,
  "data": {
    "construction_pts": 75,
    "contribution": 120
  }
}
```

| Field | Type | Description |
|-------|------|-------------|
| `construction_pts` | int32 | Player's current available construction points |
| `contribution` | int32 | Player's current contribution score |

---

#### `summon_war_unit_request`

**Direction:** Client → Server

Commander requests to summon a war unit using construction points.

```json
{
  "type": "summon_war_unit_request",
  "seq": 1,
  "data": {
    "unit_type": 3
  }
}
```

| Field | Type | Description |
|-------|------|-------------|
| `unit_type` | uint8 | War unit type to summon (1-7) |

---

#### `summon_war_unit_response`

**Direction:** Server → Client

Result of a war unit summon attempt.

```json
{
  "type": "summon_war_unit_response",
  "seq": 1,
  "data": {
    "success": true,
    "unit_type": 3,
    "remaining_points": 50
  }
}
```

**Error response:**

```json
{
  "type": "summon_war_unit_response",
  "seq": 1,
  "data": {
    "success": false,
    "error": "insufficient_points"
  }
}
```

| Field | Type | Description |
|-------|------|-------------|
| `success` | bool | Whether the unit was summoned |
| `unit_type` | uint8 | Type of unit summoned (on success) |
| `remaining_points` | int32 | Construction points remaining after summon (on success) |
| `error` | string? | Error reason if failed: `"insufficient_points"`, `"not_commander"`, `"invalid_unit_type"`, `"no_active_crusade"` |

---

#### `crusade_map_status`

**Direction:** Server → Client (push)

Provides the current layout of structures on the Crusade battlefield.

```json
{
  "type": "crusade_map_status",
  "seq": 0,
  "data": {
    "structures": [
      { "type": "guard_tower", "faction": "aresden", "x": 120, "y": 85 },
      { "type": "esg", "faction": "elvine", "x": 200, "y": 150 }
    ]
  }
}
```

| Field | Type | Description |
|-------|------|-------------|
| `structures` | array | Array of structure objects on the map |
| `structures[].type` | string | Structure type (e.g., `"guard_tower"`, `"esg"`, `"barracks"`) |
| `structures[].faction` | string | Owning faction: `"aresden"` or `"elvine"` |
| `structures[].x` | int16 | X position on map |
| `structures[].y` | int16 | Y position on map |

---

### Heldenian Messages (Phase 4)

#### `heldenian_started`

**Direction:** Server → Client (broadcast)

Broadcast to all online players when a Heldenian battle begins.

```json
{
  "type": "heldenian_started",
  "seq": 0,
  "data": {
    "mode": "death_match",
    "war_id": 43
  }
}
```

| Field | Type | Description |
|-------|------|-------------|
| `mode` | string | Heldenian mode: `"death_match"` or `"capture_the_flag"` |
| `war_id` | uint32 | Unique war instance identifier |

---

#### `heldenian_ended`

**Direction:** Server → Client (broadcast)

Broadcast when a Heldenian battle concludes.

```json
{
  "type": "heldenian_ended",
  "seq": 0,
  "data": {
    "winner": "elvine",
    "mode": "death_match"
  }
}
```

| Field | Type | Description |
|-------|------|-------------|
| `winner` | string | Winning faction: `"aresden"`, `"elvine"`, or `"draw"` |
| `mode` | string | Heldenian mode that was played |

---

#### `heldenian_status_update`

**Direction:** Server → Client (push)

Periodic status update during an active Heldenian battle.

Sent only to players currently on the war map (BtField or HRampart).

```json
{
  "type": "heldenian_status_update",
  "seq": 0,
  "data": {
    "mode": "death_match",
    "aresden_surviving": 18,
    "elvine_surviving": 12,
    "aresden_deaths": 5,
    "elvine_deaths": 8,
    "elapsed_s": 180
  }
}
```

| Field | Type | Description |
|-------|------|-------------|
| `mode` | string | Current Heldenian mode |
| `aresden_surviving` | int32 | Number of surviving Aresden towers/doors |
| `elvine_surviving` | int32 | Number of surviving Elvine towers/doors |
| `aresden_deaths` | int32 | Total Aresden player deaths this war |
| `elvine_deaths` | int32 | Total Elvine player deaths this war |
| `elapsed_s` | uint32 | Seconds elapsed since the battle started |

---

### Apocalypse & Force Recall Messages (Phase 5)

#### `apocalypse_started`

**Direction:** Server → Client (broadcast)

Broadcast when an Apocalypse event begins. Gates open on designated maps.

```json
{
  "type": "apocalypse_started",
  "seq": 0,
  "data": {
    "active": true
  }
}
```

| Field | Type | Description |
|-------|------|-------------|
| `active` | bool | Always `true` when event starts |

---

#### `apocalypse_ended`

**Direction:** Server → Client (broadcast)

Broadcast when an Apocalypse event concludes. Players are ejected from apocalypse maps.

```json
{
  "type": "apocalypse_ended",
  "seq": 0,
  "data": {
    "active": false
  }
}
```

| Field | Type | Description |
|-------|------|-------------|
| `active` | bool | Always `false` when event ends |

---

#### `apocalypse_gate_open`

**Direction:** Server → Client (push to individual player)

Sent periodically to players on maps with active apocalypse gates.

```json
{
  "type": "apocalypse_gate_open",
  "seq": 0,
  "data": {
    "map": "icebound",
    "gate_x": 89,
    "gate_y": 31
  }
}
```

| Field | Type | Description |
|-------|------|-------------|
| `map` | string | Map name where the gate is located |
| `gate_x` | int16 | X coordinate of the gate |
| `gate_y` | int16 | Y coordinate of the gate |

---

#### `force_recall_timer`

**Direction:** Server → Client (push)

Sent to warn players that they will be forcibly recalled to their home town.

```json
{
  "type": "force_recall_timer",
  "seq": 0,
  "data": {
    "time_remaining_seconds": 30
  }
}
```

| Field | Type | Description |
|-------|------|-------------|
| `time_remaining_seconds` | int32 | Seconds remaining before the force recall executes |

---

#### `force_recall_execute`

**Direction:** Server → Client (push)

Sent when the force recall triggers, teleporting the player to their home town.

```json
{
  "type": "force_recall_execute",
  "seq": 0,
  "data": {}
}
```

No data fields. The client should handle this as an incoming teleport to the player's home town.

---

### War Rewards & Admin Messages (Phase 6)

#### `crusade_reward_summary`

**Direction:** Server → Client (push)

Sent to each participant at the end of a Crusade with their personal reward breakdown.

```json
{
  "type": "crusade_reward_summary",
  "seq": 0,
  "data": {
    "winner_faction": "aresden",
    "contribution": 150,
    "reward_exp": 5000,
    "reward_gold": 2500,
    "reward_contribution": 30
  }
}
```

| Field | Type | Description |
|-------|------|-------------|
| `winner_faction` | string | Faction that won: `"aresden"`, `"elvine"`, or `"draw"` |
| `contribution` | int32 | Player's personal contribution score |
| `reward_exp` | int32 | Experience points awarded |
| `reward_gold` | int32 | Gold awarded |
| `reward_contribution` | int32 | Contribution points awarded (for faction ranking) |

---

#### `admin_start_war_request`

**Direction:** Client → Server

Admin request to manually start a war event.

```json
{
  "type": "admin_start_war_request",
  "seq": 1,
  "data": {
    "war_type": 0
  }
}
```

| Field | Type | Description |
|-------|------|-------------|
| `war_type` | uint8 | War type to start: 0=Crusade, 1=Heldenian, 2=Apocalypse |

---

#### `admin_start_war_response`

**Direction:** Server → Client

Result of an admin war start request.

```json
{
  "type": "admin_start_war_response",
  "seq": 1,
  "data": {
    "success": true,
    "war_id": 44,
    "war_type": 0
  }
}
```

| Field | Type | Description |
|-------|------|-------------|
| `success` | bool | Whether the war was started |
| `war_id` | uint32 | Assigned war instance identifier (on success) |
| `war_type` | uint8 | War type that was started |

---

#### `admin_end_war_request`

**Direction:** Client → Server

Admin request to forcibly end an active war.

```json
{
  "type": "admin_end_war_request",
  "seq": 1,
  "data": {
    "war_id": 44
  }
}
```

| Field | Type | Description |
|-------|------|-------------|
| `war_id` | uint32 | War instance to end |

---

#### `admin_end_war_response`

**Direction:** Server → Client

Result of an admin war end request.

```json
{
  "type": "admin_end_war_response",
  "seq": 1,
  "data": {
    "success": true,
    "war_id": 44
  }
}
```

| Field | Type | Description |
|-------|------|-------------|
| `success` | bool | Whether the war was ended |
| `war_id` | uint32 | War instance that was ended |

---

#### `admin_war_history_request`

**Direction:** Client → Server

Admin request to retrieve war history records with optional filtering.

```json
{
  "type": "admin_war_history_request",
  "seq": 1,
  "data": {
    "limit": 20,
    "offset": 0,
    "war_type": 0
  }
}
```

| Field | Type | Description |
|-------|------|-------------|
| `limit` | int32 | Maximum number of records to return |
| `offset` | int32 | Number of records to skip (for pagination) |
| `war_type` | uint8? | Optional filter by war type: 0=Crusade, 1=Heldenian, 2=Apocalypse |

---

#### `admin_war_history_response`

**Direction:** Server → Client

Paginated list of past war records.

```json
{
  "type": "admin_war_history_response",
  "seq": 1,
  "data": {
    "wars": [
      {
        "war_id": 42,
        "war_type": 0,
        "winner": "aresden",
        "started_at": "2026-02-10T14:00:00Z",
        "ended_at": "2026-02-10T15:30:00Z"
      }
    ],
    "count": 1,
    "total": 15
  }
}
```

| Field | Type | Description |
|-------|------|-------------|
| `wars` | array | Array of war history records |
| `wars[].war_id` | uint32 | War instance identifier |
| `wars[].war_type` | uint8 | War type: 0=Crusade, 1=Heldenian, 2=Apocalypse |
| `wars[].winner` | string | Winning faction or `"draw"` |
| `wars[].started_at` | string | ISO 8601 timestamp of war start |
| `wars[].ended_at` | string | ISO 8601 timestamp of war end |
| `count` | int32 | Number of records in this response |
| `total` | int32 | Total number of matching records (for pagination) |

---

#### `admin_war_participants_request`

**Direction:** Client → Server

Admin request to retrieve the participant list for a specific war.

```json
{
  "type": "admin_war_participants_request",
  "seq": 1,
  "data": {
    "war_id": 42
  }
}
```

| Field | Type | Description |
|-------|------|-------------|
| `war_id` | uint32 | War instance to look up participants for |

---

#### `admin_war_participants_response`

**Direction:** Server → Client

List of participants for a specific war instance.

```json
{
  "type": "admin_war_participants_response",
  "seq": 1,
  "data": {
    "war_id": 42,
    "participants": [
      {
        "character_name": "Alice",
        "faction": "aresden",
        "contribution": 150,
        "kills": 5,
        "deaths": 2
      }
    ],
    "count": 1
  }
}
```

| Field | Type | Description |
|-------|------|-------------|
| `war_id` | uint32 | War instance identifier |
| `participants` | array | Array of participant records |
| `participants[].character_name` | string | Character name |
| `participants[].faction` | string | Player's faction |
| `participants[].contribution` | int32 | Player's contribution score |
| `participants[].kills` | int32 | Kill count during this war |
| `participants[].deaths` | int32 | Death count during this war |
| `count` | int32 | Number of participants returned |

---
