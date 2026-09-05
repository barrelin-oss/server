# Combat, Magic & Skills

[← Back to Protocol Index](../JSON_PROTOCOL.md)

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
| 2 / `"ranged"` | Ranged | Ranged attack (bow/crossbow, 2-10 tile range) |

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

A swing refused as `attack_too_fast` also carries the pace at top level, so the client can wait exactly that long:
```json
{
  "type": "player_attack_response",
  "seq": 151,
  "data": {
    "success": false,
    "error": "attack_too_fast",
    "attack_interval_ms": 1360
  }
}
```

#### Attack Result Object

| Field | Type | Required | Description |
|-------|------|----------|-------------|
| `hit` | bool | Yes | Whether attack connected |
| `critical` | bool | Yes | Whether it was a critical hit |
| `damage` | int32 | Yes | Damage dealt |
| `target_id` | uint32 | Yes | Target entity ID |
| `target_hp` | int16 | Yes | Target's remaining HP |
| `target_hp_max` | int16 | Yes | Target's maximum HP |
| `attacker_x` | int16 | Yes | Confirmed attacker X position |
| `attacker_y` | int16 | Yes | Confirmed attacker Y position |
| `is_ranged` | bool | No | Present and `true` for bow/crossbow attacks |
| `ammo_count` | int32 | No | Remaining arrows after this attack (only when `is_ranged`) |
| `ammo_template_id` | uint32 | No | Template ID of consumed arrow (only when `is_ranged`) |
| `attack_interval_ms` | int32 | No | Pace the server enforces for this attacker: ms between swings, from weapon speed and STR (see `attack_speed` in server.yaml). Swings faster than this minus `tolerance_ms` are refused with `attack_too_fast` |

**Ranged attack example:**
```json
{
  "type": "player_attack_response",
  "seq": 150,
  "data": {
    "success": true,
    "result": {
      "hit": true,
      "critical": false,
      "damage": 38,
      "target_id": 5001,
      "target_hp": 162,
      "target_hp_max": 200,
      "attacker_x": 100,
      "attacker_y": 150,
      "is_ranged": true,
      "ammo_count": 47,
      "ammo_template_id": 77
    }
  }
}
```

---

### `combat_attack_broadcast`

Broadcast to nearby players when an attack occurs. For ranged attacks (bow/crossbow), additional fields indicate projectile type so the client can render the projectile arc.

**Server Broadcast (melee):**
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
    "direction": 2,
    "hit": true,
    "critical": false,
    "damage": 45
  }
}
```

**Server Broadcast (ranged):**
```json
{
  "type": "combat_attack_broadcast",
  "seq": 0,
  "data": {
    "attacker_id": 1001,
    "target_id": 5001,
    "attacker_x": 100,
    "attacker_y": 150,
    "target_x": 106,
    "target_y": 150,
    "direction": 2,
    "hit": true,
    "critical": false,
    "damage": 38,
    "attack_mode": "ranged",
    "projectile_type": "arrow"
  }
}
```

| Field | Type | Required | Description |
|-------|------|----------|-------------|
| `attacker_id` | uint32 | Yes | Attacker entity ID |
| `target_id` | uint32 | Yes | Target entity ID |
| `attacker_x` | int16 | Yes | Attacker X position |
| `attacker_y` | int16 | Yes | Attacker Y position |
| `target_x` | int16 | Yes | Target X position |
| `target_y` | int16 | Yes | Target Y position |
| `direction` | int16 | Yes | Attacker's facing direction (0-7) |
| `hit` | bool | Yes | Whether attack connected |
| `critical` | bool | Yes | Whether it was a critical hit |
| `damage` | int32 | Yes | Damage dealt |
| `attack_mode` | string | No | `"ranged"` for bow/crossbow attacks (absent for melee) |
| `projectile_type` | string | No | `"arrow"` or `"poison_arrow"` (only when `attack_mode` is `"ranged"`) |

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
    "y": 150,
    "damage": 45
  }
}
```

| Field | Type | Description |
|-------|------|-------------|
| `victim_id` | uint32 | Entity that died |
| `killer_id` | uint32 | Entity that killed (0 if environmental/unknown) |
| `x` | int16 | Death location X |
| `y` | int16 | Death location Y |
| `damage` | int32 | Killing blow damage (omitted if 0) |

**Notes:**
- Used for **both player and NPC deaths** — there is no separate `npc_death` message at the protocol level
- The `damage` field carries the killing blow amount, so the client can display it as a final damage number (no separate `combat_effect` is sent for killing blows)
- For NPC deaths: the corpse lingers on the client for a configurable duration, then the server sends `entity_despawn` to remove it
- Dead entities cannot be attacked — the server rejects attacks against dead targets
- Message ordering for kills: `combat_attack_broadcast` → `entity_death` → `ground_item_spawn` (loot)

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

## Combat Mode Messages

### `combat_mode_change_request`

Toggle between attack stance and peace mode.

**Request:**
```json
{
  "type": "combat_mode_change_request",
  "seq": 200,
  "data": {}
}
```

No data fields required — the server toggles the current mode.

---

### `combat_mode_change_response`

Confirms the new combat mode.

**Response:**
```json
{
  "type": "combat_mode_change_response",
  "seq": 200,
  "data": {
    "combat_mode": true
  }
}
```

| Field | Type | Description |
|-------|------|-------------|
| `combat_mode` | bool | `true` = attack stance, `false` = peace mode |

---

### `combat_mode_change_broadcast`

Broadcast to nearby players when a player toggles combat mode.

**Server Broadcast:**
```json
{
  "type": "combat_mode_change_broadcast",
  "seq": 0,
  "data": {
    "entity_id": 1001,
    "combat_mode": true
  }
}
```

| Field | Type | Description |
|-------|------|-------------|
| `entity_id` | uint32 | Player entity ID |
| `combat_mode` | bool | New combat mode state |

---

## Player Action Broadcast

### `player_action_broadcast`

Broadcast to nearby players when a player performs a visible action. This replaces the legacy `MSGID_EVENT_MOTION` / `SendEventToNearClient_TypeA` system.

**Server Broadcast:**
```json
{
  "type": "player_action_broadcast",
  "seq": 0,
  "data": {
    "entity_id": 1001,
    "action": "attack",
    "direction": 3,
    "target_id": 5001
  }
}
```

| Field | Type | Required | Description |
|-------|------|----------|-------------|
| `entity_id` | uint32 | Yes | Player entity performing the action |
| `action` | string | Yes | Action type (see below) |
| `direction` | int16 | Yes | Facing direction during action (0-7) |
| `target_id` | uint32 | No | Target entity ID (for attack, magic, dash_attack) |
| `spell_id` | uint32 | No | Spell being cast (only for magic) |

**Action Types:**

| Action | Description | Optional Fields |
|--------|-------------|-----------------|
| `"attack"` | Melee attack swing | `target_id` |
| `"dash_attack"` | Dash attack | `target_id` |
| `"magic"` | Spell casting animation | `target_id`, `spell_id` |
| `"pickup"` | Item pickup animation | — |

**When Emitted:**
- Attack handler: "attack" or "dash_attack" with target_id
- Magic handler: "magic" with target_id and spell_id
- Pickup handler: "pickup" with no optional fields

**Note:** Damage flinch and death animations are NOT sent via this message — they are derived from `combat_effect` and `entity_death` broadcasts respectively.

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

## Dynamic Object Messages (Ground Fields)

Ground-field spells (`create_dynamic`, type 14: Spike-Field, Ice-Storm, Cloud-Kill) place temporary objects on map tiles. The server broadcasts their lifecycle to visible players and re-sends visible objects on game entry and teleport.

Field behavior is server-side (legacy semantics): fire fields tick 1d6 fire damage in 3x3, ice storms tick 3d3+5 ice damage in 5x5 and apply freeze, poison clouds tick 1d6/1d8 in 3x3 and apply poison, spikes deal 2d4 when an entity steps on their tile. Ticks run every ~1s. The client only needs to render the objects.

### `dynamic_object_spawn`

Broadcast to visible players when a ground-field object appears. Also sent per visible object after `enter_game` and teleport (same shape, direct to the player).

**Server Broadcast:**
```json
{
  "type": "dynamic_object_spawn",
  "seq": 0,
  "data": {
    "object_id": 17,
    "object_type": 9,
    "x": 120,
    "y": 85
  }
}
```

| Field | Type | Description |
|-------|------|-------------|
| `object_id` | uint16 | Unique object id (per server run) |
| `object_type` | uint8 | Object type (see table below) |
| `x` | int16 | Tile X |
| `y` | int16 | Tile Y |

**Object types:**

| Value | Type | Source |
|-------|------|--------|
| 1 | Fire field | (reserved: Wall-of-Fire family) |
| 8 | Ice storm | Ice-Storm spell |
| 9 | Spike trap | Spike-Field spell (one object per tile, 5x5) |
| 10 | Poison cloud (begin) | Cloud-Kill spell |
| 11 | Poison cloud (loop) | (animation variant) |
| 12 | Poison cloud (end) | (animation variant) |
| 13 | Crusade fire (visual only) | Crusade effects |
| 14 | Fire variant | (reserved) |

### `dynamic_object_removed`

Broadcast to visible players when a ground-field object expires or is removed.

**Server Broadcast:**
```json
{
  "type": "dynamic_object_removed",
  "seq": 0,
  "data": {
    "object_id": 17,
    "object_type": 9,
    "x": 120,
    "y": 85
  }
}
```

Fields are identical to `dynamic_object_spawn`.

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

### `skill_update`

Server → Client broadcast. Sent when a player's skill level changes (up or down). Broadcast to the player and all nearby visible players.

```json
{
  "type": "skill_update",
  "data": {
    "player_id": 42,
    "skill_id": 8,
    "old_level": 54,
    "level": 55,
    "total_uses": 1203,
    "uses_this_level": 0,
    "uses_to_next_level": 525
  }
}
```

| Field | Type | Description |
|-------|------|-------------|
| `player_id` | uint32 | Player whose skill changed |
| `skill_id` | uint8 | Skill type ID (0-23) |
| `old_level` | int16 | Previous skill level |
| `level` | int16 | New skill level |
| `total_uses` | int32 | Lifetime uses of this skill |
| `uses_this_level` | int32 | Uses accumulated in current level |
| `uses_to_next_level` | int32 | Uses required to reach next level |

The client computes the delta (`level - old_level`) for floating "+N%" text over the player. Nearby players receive this to show the visual effect; only the owning player needs the progress fields.

---

### `skill_progress`

Server → Client (owning player only). Sent when skill SSN (training progress) crosses a 5% threshold between level-ups. At most 20 messages per level.

```json
{
  "type": "skill_progress",
  "data": {
    "skill_id": 8,
    "uses_this_level": 55,
    "uses_to_next_level": 200,
    "percent": 25
  }
}
```

| Field | Type | Description |
|-------|------|-------------|
| `skill_id` | uint8 | Skill type ID (0-23) |
| `uses_this_level` | int32 | Current uses accumulated in this level |
| `uses_to_next_level` | int32 | Uses required to reach next level |
| `percent` | uint8 | Progress percentage (always a multiple of 5: 5, 10, 15, ... 95) |

---
