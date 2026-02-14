# Command List

[← Back to Protocol Index](../JSON_PROTOCOL.md)

Server pushes available commands to the client for autocomplete and UI display.

---

## Protocol Messages

### `available_commands`

**Direction:** Server → Client (push, sent on enter_game)

Full list of commands the player can see, with current enabled state.

```json
{
  "type": "available_commands",
  "seq": 0,
  "data": {
    "commands": [
      {
        "name": "online",
        "description": "Show online player count",
        "usage": "/online",
        "category": "general",
        "enabled": true
      },
      {
        "name": "gcreate",
        "description": "Create a new guild",
        "usage": "/gcreate <name> [tag]",
        "category": "guild",
        "enabled": true
      }
    ]
  }
}
```

| Field | Type | Description |
|-------|------|-------------|
| `commands[].name` | string | Command name (without `/` prefix) |
| `commands[].description` | string | Brief description |
| `commands[].usage` | string | Usage example with args |
| `commands[].category` | string | `"general"`, `"guild"`, `"gm"`, or `"admin"` |
| `commands[].enabled` | bool | Whether the command is currently usable |

**Categories:**
- `general` — Always-available commands (online, time, pos)
- `guild` — Guild management commands (enabled/disabled based on guild state)
- `gm` — Game Master commands (only included for GM+ players, requires `game_master` level or below)
- `admin` — Admin commands (only included for GM+ players, requires `admin` level)

### `command_availability_update`

**Direction:** Server → Client (push, sent on state changes)

Partial update with only commands whose enabled state changed.

```json
{
  "type": "command_availability_update",
  "seq": 0,
  "data": {
    "commands": [
      { "name": "gcreate", "enabled": false },
      { "name": "gquit", "enabled": true },
      { "name": "ginvite", "enabled": true }
    ]
  }
}
```

**Trigger events:**
- Guild created/joined/left/disbanded/kicked
- Guild invite received/declined
- Guild rank changed (promote/demote)

---

## Command Reference

### General Commands

Always available to all players. Category: `general`.

| Command | Usage | Description |
|---------|-------|-------------|
| `online` | `/online` | Show online player count |
| `time` | `/time` | Show server time |
| `pos` | `/pos` | Show your current position (alias: `position`) |

### Guild Commands

Enabled/disabled based on guild membership and rank. Category: `guild`.

| Command | Usage | Description | Enabled When |
|---------|-------|-------------|--------------|
| `gcreate` | `/gcreate <name> [tag]` | Create a new guild | Not in a guild |
| `gdisband` | `/gdisband` | Disband your guild | Guild master only |
| `ginvite` | `/ginvite <player>` | Invite a player to your guild | In guild + has invite permission |
| `gkick` | `/gkick <player>` | Kick a member from your guild | In guild + has kick permission |
| `gaccept` | `/gaccept` | Accept a guild invite | Always (client grays contextually) |
| `gdecline` | `/gdecline` | Decline a guild invite | Always (client grays contextually) |
| `gquit` | `/gquit` | Leave your guild | In guild + not guild master |

### GM Commands

Only sent to players with GM+ account level. Category: `gm`.

#### Information (Helper+)

| Command | Aliases | Usage | Description |
|---------|---------|-------|-------------|
| `help` | `?`, `commands` | `/help [command]` | Show available commands or help for a specific command |
| `who` | `admins`, `gms` | `/who` | List online administrators |
| `getinfo` | `info`, `playerinfo` | `/getinfo <player_name>` | View detailed player information (level, stats, location, gold, PK count) |
| `where` | `find`, `locate` | `/where <player_name>` | Find a player's map and coordinates |

#### Moderation (Moderator+)

| Command | Aliases | Usage | Description |
|---------|---------|-------|-------------|
| `mute` | `silence` | `/mute <player_id> [duration_seconds] [reason]` | Mute a player (0 duration = permanent) |
| `unmute` | — | `/unmute <player_id>` | Unmute a player |
| `kick` | — | `/kick <player_id> [reason]` | Kick a player from the server |

#### Game Master (Game Master+)

| Command | Aliases | Usage | Description |
|---------|---------|-------|-------------|
| `goto` | `warp`, `gotp` | `/goto <player_name>` | Teleport to a player's location |
| `summonplayer` | `bring`, `summon` | `/summonplayer <player_name>` | Teleport a player to your location |
| `teleport` | `tp`, `move` | `/teleport <map_name> <x> <y>` | Teleport to specific map coordinates |
| `heal` | `restore` | `/heal [player_name]` | Restore HP/MP/SP to full (default: self) |
| `invisible` | `invis`, `hide` | `/invisible [on\|off]` | Toggle invisibility (toggles if no argument) |
| `invincible` | `god`, `godmode` | `/invincible [on\|off]` | Toggle invincibility (toggles if no argument) |
| `announce` | `broadcast`, `bc` | `/announce <message>` | Broadcast a message to all players |

#### Senior GM (Senior GM+)

| Command | Aliases | Usage | Description |
|---------|---------|-------|-------------|
| `kill` | `slay` | `/kill <player_name>` | Kill a player (sets HP to 0) |
| `ban` | — | `/ban <player_id> [duration_seconds] [reason]` | Ban a player (0 duration = permanent) |
| `log` | `cmdlog` | `/log [count]` | View command log (default: last 20 entries) |

### Admin Commands

Only sent to players with GM+ account level. Category: `admin`. Requires `admin` level (level 5).

| Command | Aliases | Usage | Description |
|---------|---------|-------|-------------|
| `setlevel` | `level` | `/setlevel <player_name> <level>` | Set a player's level (1-180) |
| `setstats` | `stat`, `setstat` | `/setstats <player_name> <stat> <value>` | Modify a player's stat (str/dex/vit/int/mag/cha/hp/mp/sp) |
| `setgold` | `gold` | `/setgold <player_name> <amount>` | Set a player's gold amount |
| `setviewrange` | `viewrange`, `svr` | `/setviewrange <player_name> <radius\|WxH\|all\|reset>` | Override visibility radius (1-80 tiles, WxH, `all` for full map, `reset` to restore) |
| `setviewmode` | `viewmode`, `rendermode` | `/setviewmode <player_name> <scaled\|extended\|special>` | Set a player's rendering mode |
| `learnallspells` | `allspells`, `grantspells` | `/learnallspells [player_name]` | Grant all magic spells (default: self) |
| `grantallskills` | `allskills`, `maxskills` | `/grantallskills [player_name]` | Set all skills to 100 (default: self) |
| `settime` | — | `/settime <hour> [minute]` | Set the game clock time (0-23 hours, 0-59 minutes) |
| `setweather` | — | `/setweather <type> [map_name]` | Set weather for a map (default: current map) |

**Weather types:** `clear`, `light_rain`, `rain`, `heavy_rain`, `light_snow`, `snow`, `heavy_snow`, `windy`, `storm`

---

## Admin Level Hierarchy

Commands are filtered by the account's admin level. Each level includes all commands from lower levels.

| Level | Name | Description |
|-------|------|-------------|
| 0 | Player | No admin commands |
| 1 | Helper | Information commands (help, who, getinfo, where) |
| 2 | Moderator | + Mute, unmute, kick |
| 3 | Game Master | + Teleport, heal, invisibility, invincibility, announce |
| 4 | Senior GM | + Kill, ban, command log |
| 5 | Admin | + Set stats/level/gold, view/render overrides, grant spells/skills, time/weather |
| 6 | Owner | Full access |

**Account level mapping:**

| Account DB Value | auth::admin_level | admin::admin_level | player::admin_level |
|------------------|-------------------|---------------------|---------------------|
| 0 | player | player (0) | player (0) |
| 1 | helper | helper (1) | gamemaster (1) |
| 10 | gamemaster | game_master (3) | gamemaster (1) |
| 15 | senior_gm | senior_gm (4) | admin (2) |
| 20 | administrator | admin (5) | admin (2) |
