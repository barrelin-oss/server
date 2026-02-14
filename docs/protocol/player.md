# Player State & Entities

[← Back to Protocol Index](../JSON_PROTOCOL.md)

## World State Messages

These messages can be sent individually during teleports or map changes.

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
    "item_name": "Iron Sword +3",
    "durability": 100,
    "max_durability": 100,
    "attribute": {"upgrade": 3, "main_type": 7, "main_value": 1},
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
| `item_name` | string | Item display name (includes "+N" for upgraded items) |
| `durability` | int16 | Current durability |
| `max_durability` | int16 | Maximum durability |
| `attribute` | object? | Item attributes (see [Item Attribute Object](items.md#item-attribute-object)) |
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
    "item_name": "Iron Sword +3",
    "inventory_slot": 3,
    "attribute": {"upgrade": 3, "main_type": 7, "main_value": 1}
  }
}
```

| Field | Type | Description |
|-------|------|-------------|
| `success` | bool | Whether unequip succeeded |
| `slot` | uint8 | Equipment slot |
| `item_id` | uint32 | Unequipped item ID |
| `item_name` | string | Item display name (includes "+N" for upgraded items) |
| `inventory_slot` | uint8 | Inventory slot where item was placed |
| `attribute` | object? | Item attributes (see [Item Attribute Object](items.md#item-attribute-object)) |
| `error` | string? | Error code on failure |

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

### `spell_list_update`

Sent to a player when their known spell list changes (e.g., after learning a new spell or equipment changes that affect available spells). Contains the full list of known spells.

**Server Message:**
```json
{
  "type": "spell_list_update",
  "seq": 0,
  "data": {
    "spells": [
      { "spell_id": 10, "level": 3, "total_casts": 142 },
      { "spell_id": 24, "level": 1, "total_casts": 5 }
    ]
  }
}
```

| Field | Type | Description |
|-------|------|-------------|
| `spells` | array | Complete list of known spells |
| `spells[].spell_id` | uint16 | Spell identifier (from magic.yaml) |
| `spells[].level` | int16 | Current spell level |
| `spells[].total_casts` | int32 | Total times this spell has been cast |

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
      { "skill_id": 1, "level": 50, "total_uses": 15000, "uses_this_level": 320, "uses_to_next_level": 5100 },
      { "skill_id": 3, "level": 35, "total_uses": 8000, "uses_this_level": 0, "uses_to_next_level": 900 }
    ]
  }
}
```

| Field | Type | Description |
|-------|------|-------------|
| `skills[].skill_id` | uint8 | Skill type identifier |
| `skills[].level` | int16 | Current skill level |
| `skills[].total_uses` | int32 | Lifetime use count for this skill |
| `skills[].uses_this_level` | int32 | Uses accumulated toward next level |
| `skills[].uses_to_next_level` | int32 | Uses required to reach next level |

---

## Entity Visibility Messages

### `entity_spawn`

A new entity entered visibility range. Player entities include full appearance data (base appearance, equipment visuals, status effects, active buffs). NPC entities include `category`, `hostility`, `template_id`, `sprite_id`, and `level`.

**Server Broadcast (player):**
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
    "direction": 4,
    "level": 50,
    "faction": "elvine",
    "hostility": "enemy",
    "pk_status": "innocent",
    "combat_mode": false,
    "guild_name": "BloodGuard",
    "guild_tag": "BG",
    "gender": 1,
    "skin_color": 1,
    "hair_style": 3,
    "hair_color": 7,
    "underwear_color": 2,
    "equipment": {
      "weapon": { "appr": 9, "color": 2, "name": "LongSword+2", "rarity": "rare" },
      "shield": { "appr": 3, "color": 0, "name": "TowerShield", "rarity": "common" },
      "body": { "appr": 5, "color": 1, "name": "PlateArmor", "rarity": "uncommon" },
      "pants": { "appr": 0, "color": 0 },
      "head": { "appr": 0, "color": 0 },
      "arms": { "appr": 0, "color": 0 },
      "boots": { "appr": 0, "color": 0 },
      "cape": { "appr": 0, "color": 0 }
    },
    "weapon_glow": 1,
    "shield_glow": 0,
    "weapon_speed": 7,
    "status_effects": ["poisoned", "protection"],
    "active_buffs": [
      { "type": "buff_defense", "spell_id": 42, "magnitude": 20, "remaining_ms": 30000 }
    ]
  }
}
```

| Field | Type | Description |
|-------|------|-------------|
| `faction` | string | Player's faction: `neutral`, `aresden`, `elvine` |
| `hostility` | string | Hostility relative to receiving player: `enemy`, `friendly`, `neutral` |
| `pk_status` | string | PK status: `innocent`, `criminal`, `murderer` |
| `combat_mode` | bool | `true` = attack stance, `false` = peace mode |
| `guild_name` | string | (Optional) Player's guild name, omitted if not in a guild |
| `guild_tag` | string | (Optional) Player's guild tag, omitted if not in a guild |
| `gender` | int | 1=male, 2=female |
| `skin_color` | int | Skin color index (1-3) |
| `hair_style` | int | Hair style index (0-7) |
| `hair_color` | int | Hair color index (0-15) |
| `underwear_color` | int | Underwear color index (0-15) |
| `level` | int | Player level |
| `equipment` | object | Per-slot equipment visuals (see below) |
| `weapon_glow` | int | Weapon glow effect: 0=none, 1=sparkle, 2=ice, 3=green |
| `shield_glow` | int | Shield glow effect: 0=none, 1=GM, 2=green, 3=ice |
| `weapon_speed` | int | Attack animation speed (0-15) |
| `status_effects` | string[] | (Optional) Active status flags: `poisoned`, `berserk`, `protection`, etc. |
| `active_buffs` | array | (Optional) Active buff details (see below) |

**Equipment slot object:**

| Field | Type | Description |
|-------|------|-------------|
| `appr` | int | Sprite variant index from item template |
| `color` | int | Color tint index (0-15) |
| `name` | string | (Optional) Item name for tooltips |
| `rarity` | string | (Optional) `common`, `uncommon`, `rare`, `epic`, `legendary`, `ancient` |

**Buff object:**

| Field | Type | Description |
|-------|------|-------------|
| `type` | string | Effect type: `buff_attack`, `buff_defense`, `poison`, etc. |
| `spell_id` | int | Source spell ID |
| `magnitude` | int | Effect magnitude |
| `remaining_ms` | int | Milliseconds remaining |

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
- This is informational -- the server enforces the radii regardless. The client uses them to adjust rendering (e.g., fog of war distance, entity culling).
- When `sees_all` is `true`, the radii still reflect the last computed values but are effectively ignored server-side.
- Triggered by `/setviewrange` admin command. Not sent during normal `set_view_range` requests from the client.

### Admin Visibility Override

Admins can use `sees_all` mode which bypasses all distance checks, receiving every event on their current map regardless of position. This is controlled via the `/setviewrange` admin command.

---
