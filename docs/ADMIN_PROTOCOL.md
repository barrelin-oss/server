# Admin Web Tool Protocol

> See [JSON_PROTOCOL.md](JSON_PROTOCOL.md) for the base message envelope, authentication, and game protocol.

## Overview

The admin web tool API allows authorized admin users to connect via WebSocket, authenticate with an admin-level account (no character needed), and manage the game server. Two modes are available:

1. **Dashboard mode**: Request/response queries for players, maps, NPCs, guilds, inventory, accounts, server stats
2. **Spectator mode**: Subscribe to a map or follow a player, receiving real-time game broadcasts

### Connection Flow

```
login_request → login_response (standard auth)
enter_admin_mode_request → enter_admin_mode_response
  [connection state: authenticated → admin_dashboard]
  → dashboard requests OR spectator subscribe
```

Minimum admin level required: `gamemaster` (10).

---

## `enter_admin_mode_request`

Transitions an authenticated connection to admin dashboard mode. No character selection needed.

**Request:**
```json
{
  "type": "enter_admin_mode_request",
  "seq": 1,
  "data": {}
}
```

**Response (`enter_admin_mode_response`):**
```json
{
  "type": "enter_admin_mode_response",
  "seq": 1,
  "data": {
    "success": true,
    "admin_level": 10
  }
}
```

**Error Response:**
```json
{
  "type": "enter_admin_mode_response",
  "seq": 1,
  "data": {
    "success": false,
    "error": "insufficient_permissions"
  }
}
```

---

## Admin Response Pattern

All admin request/response pairs follow the same pattern. The response merges `success` and optional `error` into the response data alongside any result fields.

```json
{
  "type": "admin_<action>_response",
  "seq": 1,
  "data": {
    "success": true,
    "field1": "value1",
    "field2": "value2"
  }
}
```

On failure:
```json
{
  "type": "admin_<action>_response",
  "seq": 1,
  "data": {
    "success": false,
    "error": "error description"
  }
}
```

---

## Server Stats

### `admin_server_stats_request` / `admin_server_stats_response`

Returns server statistics.

**Request:** `{ "data": {} }`

**Response data:**

| Field | Type | Description |
|-------|------|-------------|
| `uptime_seconds` | int | Server uptime in seconds |
| `player_count` | int | Online player count |
| `npc_count` | int | Total active NPCs |
| `map_count` | int | Loaded maps |
| `guild_count` | int | Total guilds |
| `game_hour` | int | In-game hour (0-23) |
| `game_minute` | int | In-game minute (0-59) |
| `is_day` | bool | Whether it is daytime |

---

## Player Management

### `admin_list_players_request` / `admin_list_players_response`

Lists all connected accounts and their characters. Includes both in-game players and accounts sitting at character select.

**Request:** `{ "data": {} }`

**Response data:**

| Field | Type | Description |
|-------|------|-------------|
| `accounts` | array | Array of account objects |
| `count` | int | Number of connected accounts |

**Account object:**

| Field | Type | Description |
|-------|------|-------------|
| `account` | string | Account username |
| `ip` | string | Remote IP address |
| `in_game` | bool | Whether a character is actively in-game |
| `characters` | array | All characters on this account |

**Character object:**

| Field | Type | Description |
|-------|------|-------------|
| `id` | uint32 | Character ID |
| `name` | string | Character name |
| `level` | int16 | Character level |
| `faction` | int | Faction (0=neutral, 1=aresden, 2=elvine) |
| `gender` | int16 | Gender |
| `map` | string | Map name (last known or current) |
| `active` | bool | Whether this is the in-game character |

**Additional fields on active characters:**

| Field | Type | Description |
|-------|------|-------------|
| `x` | int16 | Current X position |
| `y` | int16 | Current Y position |
| `hp` | int | Current HP |
| `max_hp` | int | Maximum HP |
| `mp` | int | Current MP |
| `max_mp` | int | Maximum MP |
| `guild` | string | Guild name |
| `pk_count` | int | PK kill count |

### `admin_get_player_request` / `admin_get_player_response`

Gets detailed player information.

**Request:**

| Field | Type | Required | Description |
|-------|------|----------|-------------|
| `player_name` | string | One required | Player name |
| `player_id` | uint32 | One required | Player ID |

**Response data:** Full player detail (stats, equipment, skills, buffs, guild, party, pk state, position, etc.)

### `admin_kick_player_request` / `admin_kick_player_response`

Kicks a player from the server.

**Request:**

| Field | Type | Required | Description |
|-------|------|----------|-------------|
| `player_name` | string | Yes | Player to kick |
| `reason` | string | No | Kick reason |

### `admin_ban_player_request` / `admin_ban_player_response`

Bans a player's account.

**Request:**

| Field | Type | Required | Description |
|-------|------|----------|-------------|
| `player_name` | string | Yes | Player to ban |
| `reason` | string | No | Ban reason |
| `duration_hours` | int32 | No | Ban duration (0 = permanent) |

### `admin_unban_player_request` / `admin_unban_player_response`

Unbans a player's account.

**Request:**

| Field | Type | Required | Description |
|-------|------|----------|-------------|
| `player_name` | string | Yes | Player to unban |

### `admin_teleport_player_request` / `admin_teleport_player_response`

Teleports a player to a destination.

**Request:**

| Field | Type | Required | Description |
|-------|------|----------|-------------|
| `player_name` | string | Yes | Player to teleport |
| `dest_map` | string | Yes | Destination map name |
| `dest_x` | int16 | No | X coordinate |
| `dest_y` | int16 | No | Y coordinate |

### `admin_modify_player_request` / `admin_modify_player_response`

Modifies player attributes.

**Request:**

| Field | Type | Required | Description |
|-------|------|----------|-------------|
| `player_name` | string | Yes | Target player |
| `modifications` | object | Yes | Key-value pairs of fields to modify (e.g., `{"hp": 500, "level": 50}`) |

---

## World / NPC Management

### `admin_list_maps_request` / `admin_list_maps_response`

Lists all loaded maps with summary info.

**Request:** `{ "data": {} }`

**Response data:** `{ "maps": [ { "name", "player_count", "npc_count", "width", "height" }, ... ] }`

### `admin_get_map_request` / `admin_get_map_response`

Gets detailed map information.

**Request:**

| Field | Type | Required | Description |
|-------|------|----------|-------------|
| `map_name` | string | Yes | Map name |

**Response data:** Map details including spawners, teleports, safe zones, entity list.

### `admin_spawn_npc_request` / `admin_spawn_npc_response`

Spawns NPCs on a map.

**Request:**

| Field | Type | Required | Description |
|-------|------|----------|-------------|
| `npc_name` | string | Yes | NPC template name |
| `map_name` | string | Yes | Target map |
| `x` | int16 | No | X coordinate |
| `y` | int16 | No | Y coordinate |
| `count` | int16 | No | Number to spawn (default: 1) |

### `admin_kill_npc_request` / `admin_kill_npc_response`

Kills a specific NPC by entity ID.

**Request:**

| Field | Type | Required | Description |
|-------|------|----------|-------------|
| `entity_id` | uint32 | Yes | NPC entity ID |

---

## Inventory Management

### `admin_get_inventory_request` / `admin_get_inventory_response`

Gets a player's full inventory, bank, equipment, and gold.

**Request:**

| Field | Type | Required | Description |
|-------|------|----------|-------------|
| `player_name` | string | Yes | Player name |

### `admin_give_item_request` / `admin_give_item_response`

Gives an item to a player.

**Request:**

| Field | Type | Required | Description |
|-------|------|----------|-------------|
| `player_name` | string | Yes | Player name |
| `item_template_id` | uint32 | Yes | Item template ID |
| `count` | int16 | No | Stack count (default: 1) |

### `admin_remove_item_request` / `admin_remove_item_response`

Removes an item from a player's inventory.

**Request:**

| Field | Type | Required | Description |
|-------|------|----------|-------------|
| `player_name` | string | Yes | Player name |
| `inventory_slot` | int16 | Yes | Inventory slot index |
| `count` | int16 | No | Amount to remove (0 = entire stack) |

---

## Social Management

### `admin_list_guilds_request` / `admin_list_guilds_response`

Lists all guilds.

**Request:** `{ "data": {} }`

**Response data:** `{ "guilds": [ { "name", "faction", "member_count", "leader" }, ... ] }`

### `admin_get_guild_request` / `admin_get_guild_response`

Gets detailed guild information.

**Request:**

| Field | Type | Required | Description |
|-------|------|----------|-------------|
| `guild_name` | string | Yes | Guild name |

**Response data:** Full guild detail (members, ranks, wars, stats).

---

## Account Management

### `admin_get_account_request` / `admin_get_account_response`

Gets account details and character list.

**Request:**

| Field | Type | Required | Description |
|-------|------|----------|-------------|
| `username` | string | Yes | Account username |

---

## Spectator System

### `admin_subscribe_map_request` / `admin_subscribe_map_response`

Subscribes to a map for real-time updates. Receives all entity updates on the entire map (no viewport filtering). Server sends an `admin_spectator_init` with full map state after subscribing.

**Request:**

| Field | Type | Required | Description |
|-------|------|----------|-------------|
| `map_name` | string | Yes | Map to subscribe to |

### `admin_subscribe_player_request` / `admin_subscribe_player_response`

Follows a specific player, auto-switching maps on teleport. Full-map visibility.

**Request:**

| Field | Type | Required | Description |
|-------|------|----------|-------------|
| `player_name` | string | Yes | Player to follow |

### `admin_unsubscribe_request` / `admin_unsubscribe_response`

Unsubscribes from current map/player subscription.

**Request:** `{ "data": {} }`

### `admin_spectator_init`

Server-push message sent after subscribing. Contains full map state snapshot (all entities, ground items, environment).

---

## Push Notifications

These are server-initiated broadcasts (seq: 0) sent to all admin dashboard connections.

### `admin_player_connected`

Sent when a player enters the game.

```json
{
  "type": "admin_player_connected",
  "seq": 0,
  "data": {
    "name": "PlayerName",
    "level": 50,
    "map": "aresden"
  }
}
```

### `admin_player_disconnected`

Sent when a player leaves the game.

```json
{
  "type": "admin_player_disconnected",
  "seq": 0,
  "data": {
    "name": "PlayerName"
  }
}
```

### `admin_chat_log`

Sent for all chat messages server-wide (for monitoring).

```json
{
  "type": "admin_chat_log",
  "seq": 0,
  "data": {
    "channel": "global",
    "sender": "PlayerName",
    "content": "Hello world"
  }
}
```

---

## Server Broadcast

### `admin_broadcast_request` / `admin_broadcast_response`

Send a server-wide system announcement to all connected players.

**Request:**
```json
{
  "type": "admin_broadcast_request",
  "seq": 100,
  "data": { "message": "Server restart in 5 minutes" }
}
```

**Response:**
```json
{
  "type": "admin_broadcast_response",
  "seq": 100,
  "data": { "success": true, "message": "Server restart in 5 minutes" }
}
```

---

## Mute Management

### `admin_mute_player_request` / `admin_mute_player_response`

Mute a player. Duration 0 = permanent.

**Request:**
```json
{
  "type": "admin_mute_player_request",
  "seq": 101,
  "data": { "player_name": "BadPlayer", "duration_minutes": 30 }
}
```

### `admin_unmute_player_request` / `admin_unmute_player_response`

Remove mute from a player.

**Request:**
```json
{
  "type": "admin_unmute_player_request",
  "seq": 102,
  "data": { "player_name": "BadPlayer" }
}
```

---

## Template Browsing

### `admin_list_item_templates_request` / `admin_list_item_templates_response`

Full dump of all item templates (client-side filtering expected).

**Request:** `{ }` (no parameters)

**Response:**
```json
{
  "type": "admin_list_item_templates_response",
  "seq": 103,
  "data": {
    "success": true,
    "count": 500,
    "items": [
      { "id": 1, "name": "Iron Sword", "type": 1, "equip_pos": 0, "level_limit": 10, "price": 500, "weight": 30 }
    ]
  }
}
```

### `admin_get_item_template_request` / `admin_get_item_template_response`

Full detail for a single item template. Lookup by `item_id` or `item_name`.

**Request:**
```json
{ "item_id": 42 }
```
or
```json
{ "item_name": "Iron Sword" }
```

**Response:** All fields from `item_template` (stats, requirements, bonuses, flags).

### `admin_list_npc_templates_request` / `admin_list_npc_templates_response`

Full dump of all NPC templates.

**Request:** `{ }` (no parameters)

**Response:**
```json
{
  "type": "admin_list_npc_templates_response",
  "seq": 104,
  "data": {
    "success": true,
    "count": 100,
    "npcs": [
      { "id": 1, "name": "Goblin", "type": 0, "level": 5, "hp": 100, "exp_reward": 50, "is_boss": false, "is_aggressive": true }
    ]
  }
}
```

### `admin_get_npc_template_request` / `admin_get_npc_template_response`

Full detail for a single NPC template. Lookup by `npc_id` or `npc_name`.

**Request:**
```json
{ "npc_id": 1 }
```
or
```json
{ "npc_name": "Goblin" }
```

**Response:** All fields from `npc_template` (stats, combat, resistances, flags).

---

## War Status

### `admin_get_war_status_request` / `admin_get_war_status_response`

All active wars with full status.

**Request:** `{ }` (no parameters)

**Response:**
```json
{
  "type": "admin_get_war_status_response",
  "seq": 105,
  "data": {
    "success": true,
    "count": 1,
    "wars": [
      {
        "id": 1,
        "type": 0,
        "state": 1,
        "phase": 2,
        "elapsed_seconds": 600,
        "participant_count": 42,
        "aresden_score": { "kills": 10, "deaths": 8, "objectives": 2, "total_score": 30, "participant_count": 20 },
        "elvine_score": { "kills": 8, "deaths": 10, "objectives": 1, "total_score": 18, "participant_count": 22 }
      }
    ]
  }
}
```

---

## Party Inspection

### `admin_list_parties_request` / `admin_list_parties_response`

All active parties with member details.

**Request:** `{ }` (no parameters)

**Response:**
```json
{
  "type": "admin_list_parties_response",
  "seq": 106,
  "data": {
    "success": true,
    "count": 5,
    "parties": [
      {
        "id": 1,
        "leader": 100,
        "loot_mode": 0,
        "exp_mode": 1,
        "member_count": 3,
        "members": [
          { "player_id": 100, "name": "Leader", "level": 80, "map": "aresden", "is_leader": true },
          { "player_id": 101, "name": "Member1", "level": 75, "map": "aresden", "is_leader": false }
        ]
      }
    ]
  }
}
```

---

## Player Search

### `admin_search_players_request` / `admin_search_players_response`

Substring search across online players (case-insensitive).

**Request:**
```json
{ "query": "zer" }
```

**Response:**
```json
{
  "type": "admin_search_players_response",
  "seq": 107,
  "data": {
    "success": true,
    "query": "zer",
    "count": 1,
    "players": [
      { "id": 1, "name": "zero", "level": 100, "map": "aresden", "faction": 1, "guild": "TestGuild" }
    ]
  }
}
```

---

## Enhanced Existing Responses

### `admin_server_stats_response` — additional fields

- `scheduled_tasks` (int): Number of pending scheduled tasks
- `total_gold` (int64): Sum of gold across all online player inventories
- `active_admin_count` (int): Number of active admin connections

### `admin_list_players_response` — additional fields per account

- `online_seconds` (int64): Duration since WebSocket connection established

### `admin_get_player_response` — additional fields

- `effects` (array): Active buff/debuff effects on the player
  - Each effect: `{ "type": int, "group": int, "magnitude": int, "remaining_ms": int64, "source_spell": uint16 (optional) }`

---

## Audit Log

### `admin_get_audit_log_request` / `admin_get_audit_log_response`

View GM command audit log. Admin level 10.

**Request:**

| Field | Type | Required | Description |
|-------|------|----------|-------------|
| `count` | int | No | Max entries to return (default: 100) |
| `executor_name` | string | No | Filter to entries by this admin |

**Response data:**

| Field | Type | Description |
|-------|------|-------------|
| `entries` | array | Log entry objects |
| `count` | int | Number of entries returned |

**Log entry object:**

| Field | Type | Description |
|-------|------|-------------|
| `timestamp` | string | ISO-8601 timestamp |
| `executor` | string | Admin who ran the command |
| `executor_level` | int | Admin level at execution time |
| `command` | string | Command name |
| `full_command` | string | Full command string with arguments |
| `success` | bool | Whether command succeeded |
| `result` | string | Result message |

---

## Server Configuration

### `admin_get_config_request` / `admin_get_config_response`

View current server config (passwords sanitized). Admin level 10.

**Request:** `{ "data": {} }`

**Response data:** Full server config JSON tree with sensitive values replaced by `"***"`.

### `admin_set_config_request` / `admin_set_config_response`

Patch server config values and save to disk. Admin level 20.

**Request:**

| Field | Type | Required | Description |
|-------|------|----------|-------------|
| `values` | object | Yes | Dot-notation key-value pairs (e.g., `{"auto_save.interval_seconds": 600}`) |

Sentinel check: if any value equals `"***"`, that field is skipped (prevents accidental password overwrite from sanitized view).

**Response data:**

| Field | Type | Description |
|-------|------|-------------|
| `applied` | array | Keys that were applied |
| `skipped` | array | Keys skipped (sentinel values) |
| `restart_required_for` | array | Keys that require restart to take effect |

### `admin_reload_config_request` / `admin_reload_config_response`

Re-read config from disk and apply hot-reloadable settings. Admin level 20.

**Request:** `{ "data": {} }`

**Response data:**

| Field | Type | Description |
|-------|------|-------------|
| `reloaded` | bool | Whether reload succeeded |
| `hot_applied` | array | Settings applied immediately |
| `restart_required_for` | array | Settings that require restart |

Hot-reloadable: `auto_save.*`, `logging.*`. NOT hot-reloadable: `database.*`, `websocket.*`, `legacy_port`.

---

## Scheduled Task Management

### `admin_list_scheduled_tasks_request` / `admin_list_scheduled_tasks_response`

List all scheduled tasks. Admin level 10.

**Request:** `{ "data": {} }`

**Response data:**

| Field | Type | Description |
|-------|------|-------------|
| `tasks` | array | Running task info objects |
| `count` | int | Number of running tasks |
| `definitions` | array | All registered task definitions |

**Task info object:**

| Field | Type | Description |
|-------|------|-------------|
| `id` | uint64 | Task ID |
| `tag` | string | Task tag (may be empty) |
| `next_fire_ms` | int64 | Milliseconds until next fire |
| `interval_ms` | int64 | Repeat interval (0 = one-shot) |
| `repeating` | bool | Whether the task repeats |

**Task definition object:**

| Field | Type | Description |
|-------|------|-------------|
| `tag` | string | Task tag |
| `description` | string | Human-readable description |
| `default_interval_ms` | int64 | Default interval in ms |
| `repeating` | bool | Whether the task repeats |
| `running` | bool | Whether the task is currently running |

**Known task definitions:**

| Tag | Description | Default Interval |
|-----|-------------|-----------------|
| `auto_save` | Periodic player state persistence to database | config-driven |
| `ground_item_cleanup` | Remove ground items older than 3 minutes | 30000ms |
| `environment_tick` | Weather cycling and day/night sync | 10000ms |
| `mineral_gen` | Spawn mineral nodes for mining | 10000ms |

### `admin_cancel_scheduled_task_request` / `admin_cancel_scheduled_task_response`

Cancel a scheduled task by tag. Admin level 20. The task definition remains registered and can be restarted.

**Request:**

| Field | Type | Required | Description |
|-------|------|----------|-------------|
| `tag` | string | Yes | Tag of the task to cancel |

**Response data:**

| Field | Type | Description |
|-------|------|-------------|
| `tag` | string | The tag that was cancelled |
| `cancelled` | bool | Whether cancellation succeeded |

### `admin_start_task_request` / `admin_start_task_response`

Start a registered task definition. Admin level 20. Rejects if tag is unknown or task is already running.

**Request:**

| Field | Type | Required | Description |
|-------|------|----------|-------------|
| `tag` | string | Yes | Task definition tag |
| `interval_ms` | int64 | No | Override the default interval |

**Response data:**

| Field | Type | Description |
|-------|------|-------------|
| `tag` | string | Task tag |
| `started` | bool | Whether the task was started |
| `interval_ms` | int64 | Actual interval used |

---

## Database Queries

### `admin_run_query_request` / `admin_run_query_response`

Execute a predefined read-only database query. No raw SQL. Admin level 20.

**Request:**

| Field | Type | Required | Description |
|-------|------|----------|-------------|
| `query_name` | string | Yes | Name of the predefined query |
| `params` | object | No | Query parameters |

**Available queries:**

| Query Name | Parameters | Description |
|------------|-----------|-------------|
| `top_players_by_level` | `limit` (default 20) | Characters ordered by level descending |
| `top_players_by_gold` | `limit` (default 20) | Characters ordered by gold descending |
| `recent_logins` | `limit` (default 20) | Accounts ordered by last login descending |
| `account_search` | `query`, `limit` (default 20) | Accounts where username matches (ILIKE) |
| `character_search` | `query`, `limit` (default 20) | Characters where name matches (ILIKE) |
| `ban_list` | (none) | All banned accounts |
| `guild_rankings` | `limit` (default 20) | Guilds ordered by member count descending |
| `faction_distribution` | (none) | Character count grouped by faction |
| `recent_characters` | `limit` (default 20) | Characters ordered by creation date descending |

**Response data:**

| Field | Type | Description |
|-------|------|-------------|
| `query_name` | string | The executed query |
| `columns` | array | Column name strings |
| `rows` | array | Array of arrays (each row is an array of string values) |
| `row_count` | int | Number of rows returned |

---

## Live NPC State

### `admin_list_map_npcs_request` / `admin_list_map_npcs_response`

List all NPCs on a specific map. Admin level 10.

**Request:**

| Field | Type | Required | Description |
|-------|------|----------|-------------|
| `map_name` | string | Yes | Map to inspect |

**Response data:**

| Field | Type | Description |
|-------|------|-------------|
| `map_name` | string | The queried map |
| `npcs` | array | NPC state objects |
| `count` | int | Number of NPCs |

**NPC state object:**

| Field | Type | Description |
|-------|------|-------------|
| `entity_id` | uint32 | Entity ID |
| `template_id` | uint16 | NPC template ID |
| `name` | string | NPC name |
| `level` | int16 | NPC level |
| `hp` | int | Current HP |
| `max_hp` | int | Maximum HP |
| `x` | int16 | X position |
| `y` | int16 | Y position |
| `category` | int | NPC category |
| `is_alive` | bool | Whether NPC is alive |
| `ai_state` | string | Current AI state name |
| `facing` | int | Facing direction |

---

## Ground Item Management

### `admin_list_map_ground_items_request` / `admin_list_map_ground_items_response`

List all ground items on a specific map. Admin level 10.

**Request:**

| Field | Type | Required | Description |
|-------|------|----------|-------------|
| `map_name` | string | Yes | Map to inspect |

**Response data:**

| Field | Type | Description |
|-------|------|-------------|
| `map_name` | string | The queried map |
| `items` | array | Ground item objects |
| `count` | int | Number of items |

**Ground item object:**

| Field | Type | Description |
|-------|------|-------------|
| `x` | int16 | X position |
| `y` | int16 | Y position |
| `item_id` | uint32 | Item instance ID |
| `template_id` | uint32 | Item template ID |
| `name` | string | Item name |
| `age_seconds` | int64 | Seconds since item was dropped |

### `admin_remove_ground_item_request` / `admin_remove_ground_item_response`

Remove a specific ground item from a map. Broadcasts removal to nearby players. Admin level 10.

**Request:**

| Field | Type | Required | Description |
|-------|------|----------|-------------|
| `map_name` | string | Yes | Map name |
| `x` | int16 | Yes | X position |
| `y` | int16 | Yes | Y position |
| `item_id` | uint32 | Yes | Item instance ID to remove |

**Response data:**

| Field | Type | Description |
|-------|------|-------------|
| `removed` | bool | Whether removal succeeded |

---

## Guild Mutations

### `admin_guild_action_request` / `admin_guild_action_response`

Perform admin-level guild operations (bypasses permission checks). Admin level 20.

**Request:**

| Field | Type | Required | Description |
|-------|------|----------|-------------|
| `guild_name` | string | Yes | Target guild name |
| `action` | string | Yes | One of: `disband`, `kick`, `set_rank` |
| `target_player` | string | For kick/set_rank | Target member name |
| `rank` | string | For set_rank | One of: `master`, `officer`, `veteran`, `member`, `recruit` |

**Response data:**

| Field | Type | Description |
|-------|------|-------------|
| `action` | string | Action performed |
| `guild_name` | string | Target guild |
| `target_player` | string | Target member (if applicable) |

---

## Player Messaging

### `admin_message_player_request` / `admin_message_player_response`

Send a system message directly to a specific player. Admin level 10.

**Request:**

| Field | Type | Required | Description |
|-------|------|----------|-------------|
| `player_name` | string | Yes | Target player |
| `message` | string | Yes | Message content |

**Response data:**

| Field | Type | Description |
|-------|------|-------------|
| `player_name` | string | Target player |
| `delivered` | bool | Whether the message was delivered |

The message arrives as a `chat_message_broadcast` with channel `"system"`.

---

## Environment Override

### `admin_set_environment_request` / `admin_set_environment_response`

Override time and/or weather. Admin level 10.

**Request:** All fields optional.

| Field | Type | Required | Description |
|-------|------|----------|-------------|
| `map_name` | string | No | Target map (omit for global time / all maps weather) |
| `hour` | int | No | Set game hour (0-23) |
| `minute` | int | No | Set game minute (0-59) |
| `weather` | int | No | Set weather type (0=clear, 1-3=rain, 4-6=snow). 0 clears weather. |

**Response data:**

| Field | Type | Description |
|-------|------|-------------|
| `time_set` | bool | Whether time was changed |
| `weather_set` | bool | Whether weather was changed |
| `maps_affected` | int | Number of maps with weather change |

---

## Server Shutdown

### `admin_shutdown_server_request` / `admin_shutdown_server_response`

Initiate server shutdown with optional countdown. Admin level 20.

**Request:**

| Field | Type | Required | Description |
|-------|------|----------|-------------|
| `countdown_seconds` | int | No | Seconds before shutdown (default: 0 = immediate) |
| `reason` | string | No | Shutdown reason (broadcast to players) |
| `cancel` | bool | No | If true, cancels an active countdown |

When `countdown_seconds > 0`, warning broadcasts are sent at 5m, 3m, 1m, 30s, and 10s remaining. Previous countdowns are automatically cancelled before starting a new one.

**Response data (start/immediate):**

| Field | Type | Description |
|-------|------|-------------|
| `countdown_seconds` | int | Countdown duration |
| `reason` | string | Shutdown reason |

**Response data (cancel):**

| Field | Type | Description |
|-------|------|-------------|
| `cancelled` | bool | Whether cancellation succeeded |

---

## Enhanced Existing Responses (Phase 4)

### `admin_get_player_response` — additional fields

| Field | Type | Description |
|-------|------|-------------|
| `appearance` | object | `{ hair_style, hair_color, skin_color, underwear_color }` |
| `stat_points_available` | int16 | Unspent stat points |
| `contribution` | int32 | Contribution points |
| `enemy_kill_count` | int32 | Enemy kill count |
| `connection_id` | uint32 | Network connection ID |
| `is_in_combat` | bool | Whether player has an active target |
| `skills` | array | `[{ type, type_name, level, experience }]` — from skill_system |
| `spells` | array | `[{ spell_id, name, level, total_casts }]` — from magic_system |
| `active_quests` | array | `[{ quest_id, name, status, objectives: [{ id, current, required, complete }] }]` |
| `completed_quest_count` | int | Number of completed quests |

### `admin_get_inventory_response` — additional fields

| Field | Type | Description |
|-------|------|-------------|
| `bank` | array | Bank storage slots: `[{ slot, item_id, count, name }]` |

### `admin_modify_player_request` — additional modifiable fields

| Field | Type | Description |
|-------|------|-------------|
| `experience` | int64 | Raw experience value |
| `faction` | int | Faction (0=neutral, 1=aresden, 2=elvine) |
| `hunger` | int8 | Hunger level (0-100) |
| `stat_points` | int16 | Available stat points |
| `contribution` | int32 | Contribution points |
| `enemy_kill_count` | int32 | Enemy kill count |
| `hair_style` | int16 | Hair style |
| `hair_color` | int16 | Hair color |
| `skin_color` | int16 | Skin color |
| `underwear_color` | int16 | Underwear color |

### `admin_search_players_request` — additional filter fields

| Field | Type | Required | Description |
|-------|------|----------|-------------|
| `level_min` | int16 | No | Filter: minimum level |
| `level_max` | int16 | No | Filter: maximum level |
| `map_name` | string | No | Filter: current map |
| `faction` | int | No | Filter: faction |
| `guild_name` | string | No | Filter: guild name (case-insensitive) |

When filters are provided, `query` can be empty to match all players that pass the filters.

---

## Skill Management

### `admin_modify_skills_request` / `admin_modify_skills_response`

Modify player skills. Admin level 20.

**Request:**

| Field | Type | Required | Description |
|-------|------|----------|-------------|
| `player_name` | string | Yes | Target player |
| `action` | string | Yes | One of: `set`, `reset`, `reset_all`, `add_exp` |
| `skill_type` | int | For set/reset/add_exp | Skill type index (0-23) |
| `value` | int | For set/add_exp | Level to set or experience to add |

**Response data:**

| Field | Type | Description |
|-------|------|-------------|
| `player_name` | string | Target player |
| `action` | string | Action performed |
| `skill_type` | int | Skill type |
| `new_level` | int16 | Skill level after change |

---

## Spell Management

### `admin_modify_spells_request` / `admin_modify_spells_response`

Modify player spell knowledge. Admin level 20.

**Request:**

| Field | Type | Required | Description |
|-------|------|----------|-------------|
| `player_name` | string | Yes | Target player |
| `action` | string | Yes | One of: `learn`, `forget`, `level_up`, `reset_cooldowns` |
| `spell_id` | uint32 | For learn/forget/level_up | Spell ID |

**Response data:**

| Field | Type | Description |
|-------|------|-------------|
| `player_name` | string | Target player |
| `action` | string | Action performed |
| `spell_id` | uint32 | Spell ID |

---

## Quest Management

### `admin_get_player_quests_request` / `admin_get_player_quests_response`

Get player quest state. Admin level 10.

**Request:**

| Field | Type | Required | Description |
|-------|------|----------|-------------|
| `player_name` | string | Yes | Target player |

**Response data:**

| Field | Type | Description |
|-------|------|-------------|
| `player_name` | string | Target player |
| `active_quests` | array | Active quest objects |
| `completed_quests` | array | Array of completed quest IDs |
| `completed_count` | int | Number of completed quests |

**Active quest object:**

| Field | Type | Description |
|-------|------|-------------|
| `quest_id` | uint16 | Quest template ID |
| `name` | string | Quest name |
| `status` | int | Quest status enum value |
| `elapsed_seconds` | int32 | Time since quest accepted |
| `time_limit` | int32 | Quest time limit in seconds (0 = no limit) |
| `objectives` | array | `[{ id, current, required, complete, description }]` |

### `admin_quest_action_request` / `admin_quest_action_response`

Manipulate player quests. Admin level 20.

**Request:**

| Field | Type | Required | Description |
|-------|------|----------|-------------|
| `player_name` | string | Yes | Target player |
| `action` | string | Yes | One of: `accept`, `abandon`, `complete` |
| `quest_id` | uint32 | Yes | Quest ID |

**Response data:**

| Field | Type | Description |
|-------|------|-------------|
| `player_name` | string | Target player |
| `action` | string | Action performed |
| `quest_id` | uint32 | Quest ID |
| `result` | string | Result: `accepted`, `abandoned`, `completed`, or `failed` |

---

## Effect Management

### `admin_remove_effects_request` / `admin_remove_effects_response`

Remove active effects from a player. Admin level 10.

**Request:**

| Field | Type | Required | Description |
|-------|------|----------|-------------|
| `player_name` | string | Yes | Target player |
| `mode` | string | Yes | One of: `all`, `group`, `single` |
| `group` | uint32 | For group mode | Magic type group ID |
| `effect_id` | uint32 | For single mode | Specific effect ID |

**Response data:**

| Field | Type | Description |
|-------|------|-------------|
| `player_name` | string | Target player |
| `mode` | string | Removal mode used |
| `removed_count` | int | Number of effects removed |

---

## Account Management

### `admin_create_account_request` / `admin_create_account_response`

Create a new account. Admin level 20.

**Request:**

| Field | Type | Required | Description |
|-------|------|----------|-------------|
| `username` | string | Yes | Account username |
| `password` | string | Yes | Account password |
| `admin_level` | int | No | Admin level (default: 0 = player) |

**Response data:**

| Field | Type | Description |
|-------|------|-------------|
| `username` | string | Created account username |
| `account_id` | uint32 | Account ID |

### `admin_change_password_request` / `admin_change_password_response`

Reset a player's password (admin bypass, no old password required). Admin level 20.

**Request:**

| Field | Type | Required | Description |
|-------|------|----------|-------------|
| `username` | string | Yes | Account username |
| `new_password` | string | Yes | New password |

**Response data:**

| Field | Type | Description |
|-------|------|-------------|
| `username` | string | Account username |

### `admin_set_admin_level_request` / `admin_set_admin_level_response`

Change a user's admin level. Cannot set level >= your own. Admin level 20.

**Request:**

| Field | Type | Required | Description |
|-------|------|----------|-------------|
| `username` | string | Yes | Account username |
| `admin_level` | int | Yes | New admin level (0=player, 10=gamemaster, 20=administrator) |

**Response data:**

| Field | Type | Description |
|-------|------|-------------|
| `username` | string | Account username |
| `admin_level` | int | New admin level |
| `level_name` | string | Human-readable level name |

Note: Player must re-login for admin level changes to take effect at runtime.

---

## Spawn Point Inspection

### `admin_list_spawn_points_request` / `admin_list_spawn_points_response`

List NPC spawn points. Admin level 10.

**Request:**

| Field | Type | Required | Description |
|-------|------|----------|-------------|
| `map_name` | string | No | Filter to specific map (empty = all) |

**Response data:**

| Field | Type | Description |
|-------|------|-------------|
| `map_name` | string | Filter map name (empty if showing all) |
| `spawn_points` | array | Spawn point objects |
| `count` | int | Number of spawn points |

**Spawn point object:**

| Field | Type | Description |
|-------|------|-------------|
| `npc_type` | uint16 | NPC template ID |
| `npc_name` | string | NPC name (from registry) |
| `map_name` | string | Map name |
| `center_x` | int16 | Center X coordinate |
| `center_y` | int16 | Center Y coordinate |
| `radius` | int16 | Spawn radius |
| `max_count` | int16 | Maximum spawn count |
| `current_count` | int16 | Currently alive count |
| `respawn_time_ms` | int32 | Respawn delay in milliseconds |

---

## Spell Template Browsing

### `admin_list_spell_templates_request` / `admin_list_spell_templates_response`

List all spell templates. Admin level 10.

**Request:** `{ }` (no parameters)

**Response data:**

| Field | Type | Description |
|-------|------|-------------|
| `spells` | array | Spell summary objects |
| `count` | int | Number of spells |

**Spell summary object:**

| Field | Type | Description |
|-------|------|-------------|
| `id` | int | Spell ID |
| `name` | string | Spell name |
| `category` | int | Spell category |
| `target_type` | int | Target type |
| `element` | int | Element type |
| `mana_cost` | int16 | Mana cost |
| `cast_time_ms` | int32 | Cast time |
| `cooldown_ms` | int32 | Cooldown |
| `level_requirement` | int16 | Level requirement |

### `admin_get_spell_template_request` / `admin_get_spell_template_response`

Get full spell template detail. Admin level 10.

**Request:** (one of)

| Field | Type | Required | Description |
|-------|------|----------|-------------|
| `spell_id` | uint32 | One required | Spell ID |
| `spell_name` | string | One required | Spell name (case-insensitive) |

**Response data:** All spell_template fields including costs, timing, range, scaling, requirements, and flags.

---

## Maintenance Mode

### `admin_set_maintenance_mode_request` / `admin_set_maintenance_mode_response`

Toggle server maintenance mode. When enabled, non-admin logins are blocked. Admin level 20.

**Request:**

| Field | Type | Required | Description |
|-------|------|----------|-------------|
| `enabled` | bool | Yes | Enable or disable maintenance mode |
| `message` | string | No | Message shown to players attempting login |

**Response data:**

| Field | Type | Description |
|-------|------|-------------|
| `enabled` | bool | New maintenance mode state |
| `message` | string | Maintenance message |

Login attempts during maintenance receive error: `maintenance:<message>`.

---

## Character Management (Admin)

### `admin_create_character_request_admin` / `admin_create_character_response_admin`

Create a character on an account. Admin level 20.

**Request:**

| Field | Type | Required | Description |
|-------|------|----------|-------------|
| `username` | string | Yes | Account username |
| `name` | string | Yes | Character name |
| `gender` | int16 | No | Gender (1=male, 2=female, default: 1) |
| `hair_style` | int16 | No | Hair style (default: 0) |
| `hair_color` | int16 | No | Hair color (default: 0) |
| `skin_color` | int16 | No | Skin color (default: 0) |
| `underwear_color` | int16 | No | Underwear color (default: 0) |

**Response data:**

| Field | Type | Description |
|-------|------|-------------|
| `username` | string | Account username |
| `character_name` | string | Created character name |
| `character_id` | uint32 | Character ID |

### `admin_delete_character_request_admin` / `admin_delete_character_response_admin`

Delete a character from an account. Blocked if character is online. Admin level 20.

**Request:**

| Field | Type | Required | Description |
|-------|------|----------|-------------|
| `username` | string | Yes | Account username |
| `character_name` | string | Yes | Character to delete |

**Response data:**

| Field | Type | Description |
|-------|------|-------------|
| `username` | string | Account username |
| `character_name` | string | Deleted character name |
| `deleted` | bool | Whether deletion succeeded |

---

## IP Ban Management

### `admin_manage_ip_bans_request` / `admin_manage_ip_bans_response`

Manage server IP bans. Admin level 20.

**Request:**

| Field | Type | Required | Description |
|-------|------|----------|-------------|
| `action` | string | Yes | One of: `list`, `add`, `remove` |
| `ip` | string | For add/remove | IP address |
| `reason` | string | For add | Ban reason |

**Response data (list):**

| Field | Type | Description |
|-------|------|-------------|
| `action` | string | `"list"` |
| `banned_ips` | array | Array of banned IP strings |
| `count` | int | Number of banned IPs |

**Response data (add/remove):**

| Field | Type | Description |
|-------|------|-------------|
| `action` | string | Action performed |
| `ip` | string | IP address |

IP bans are checked at login time. Banned IPs receive error: `ip_banned`.
