# GM Commands Reference

This document lists all available GM/Admin commands, their parameters, required permission levels, and usage examples.

## Permission Levels

| Level | Value | Description |
|-------|-------|-------------|
| Player | 0 | Regular player (no admin commands) |
| Helper | 1 | Can answer questions, limited commands |
| Moderator | 2 | Can mute/kick players, view reports |
| Game Master | 3 | Full GM powers, can teleport, spawn, heal |
| Senior GM | 4 | Can ban, kill players, modify data |
| Admin | 5 | Server administration, config changes |
| Owner | 6 | Full access to everything |

---

## Player Information Commands

### /getinfo
View detailed information about a player.

| Property | Value |
|----------|-------|
| **Aliases** | `/info`, `/playerinfo` |
| **Level** | Helper |
| **Usage** | `/getinfo <player_name>` |

**Parameters:**
- `player_name` (required) - Name of the player to inspect

**Output includes:**
- Player ID, Level, HP/MP/SP
- Base stats (STR, DEX, VIT, INT, MAG, CHA)
- Current location (map and coordinates)
- Gold, PK count, Experience

**Example:**
```
/getinfo Warrior123
```

---

### /where
Find a player's current location.

| Property | Value |
|----------|-------|
| **Aliases** | `/find`, `/locate` |
| **Level** | Helper |
| **Usage** | `/where <player_name>` |

**Parameters:**
- `player_name` (required) - Name of the player to locate

**Example:**
```
/where Warrior123
> Warrior123 is at middleland (150, 200)
```

---

### /who
List online administrators.

| Property | Value |
|----------|-------|
| **Aliases** | `/admins`, `/gms` |
| **Level** | Helper |
| **Usage** | `/who` |

**Example:**
```
/who
> Online Administrators:
>   AdminName [Admin]
>   GMBob [Game Master]
```

---

## Teleportation Commands

All teleports (these commands, the admin web tool and system teleports such as the apocalypse) go through the bridge: the moved player receives `player_teleport`, the destination entities, teleporters, ground items and environment, exactly as a map portal does.

### /goto
Teleport yourself to another player's location.

| Property | Value |
|----------|-------|
| **Aliases** | `/warp`, `/gotp` |
| **Level** | Game Master |
| **Usage** | `/goto <player_name>` |

**Parameters:**
- `player_name` (required) - Name of the player to teleport to

**Example:**
```
/goto Warrior123
> Teleported to Warrior123 at middleland (150, 200)
```

---

### /summonplayer
Teleport a player to your current location.

| Property | Value |
|----------|-------|
| **Aliases** | `/bring`, `/summon` |
| **Level** | Game Master |
| **Usage** | `/summonplayer <player_name>` |

**Parameters:**
- `player_name` (required) - Name of the player to summon

**Example:**
```
/summonplayer Warrior123
> Summoned Warrior123 to your location
```

---

### /teleport
Teleport yourself to specific map coordinates.

| Property | Value |
|----------|-------|
| **Aliases** | `/tp`, `/move` |
| **Level** | Game Master |
| **Usage** | `/teleport <map_name> <x> <y>` |

**Parameters:**
- `map_name` (required) - Name of the destination map
- `x` (required) - X coordinate
- `y` (required) - Y coordinate

**Example:**
```
/teleport aresden 100 150
> Teleported to aresden (100, 150)
```

---

## Player State Commands

### /heal
Restore HP, MP, and SP to full for yourself or another player.

| Property | Value |
|----------|-------|
| **Aliases** | `/restore` |
| **Level** | Game Master |
| **Usage** | `/heal [player_name]` |

**Parameters:**
- `player_name` (optional) - Player to heal. If omitted, heals yourself.

**Examples:**
```
/heal
> Healed yourself to full HP/MP/SP

/heal Warrior123
> Healed Warrior123 to full HP/MP/SP
```

---

### /kill
Kill a player (set HP to 0).

| Property | Value |
|----------|-------|
| **Aliases** | `/slay` |
| **Level** | Senior GM |
| **Usage** | `/kill <player_name>` |

**Parameters:**
- `player_name` (required) - Player to kill

**Example:**
```
/kill Warrior123
> Killed Warrior123
```

---

### /invisible
Toggle invisibility mode (hide from other players).

| Property | Value |
|----------|-------|
| **Aliases** | `/invis`, `/hide` |
| **Level** | Game Master |
| **Usage** | `/invisible [on|off]` |

**Parameters:**
- `state` (optional) - `on` or `off`. If omitted, toggles current state.

**Examples:**
```
/invisible
> You are now invisible

/invisible off
> You are now visible
```

---

### /invincible
Toggle invincibility mode (cannot take damage).

| Property | Value |
|----------|-------|
| **Aliases** | `/god`, `/godmode` |
| **Level** | Game Master |
| **Usage** | `/invincible [on|off]` |

**Parameters:**
- `state` (optional) - `on` or `off`. If omitted, toggles current state.

**Examples:**
```
/invincible
> Invincibility enabled

/invincible off
> Invincibility disabled
```

---

## Player Modification Commands

### /setlevel
Set a player's level.

| Property | Value |
|----------|-------|
| **Aliases** | `/level` |
| **Level** | Admin |
| **Usage** | `/setlevel <player_name> <level>` |

**Parameters:**
- `player_name` (required) - Player to modify
- `level` (required) - New level (1-180)

**Example:**
```
/setlevel Warrior123 50
> Set Warrior123's level to 50
```

---

### /setstats
Modify a player's stats.

| Property | Value |
|----------|-------|
| **Aliases** | `/stat`, `/setstat` |
| **Level** | Admin |
| **Usage** | `/setstats <player_name> <stat> <value>` |

**Parameters:**
- `player_name` (required) - Player to modify
- `stat` (required) - Stat to modify (see table below)
- `value` (required) - New value

**Valid stats:**
| Stat | Aliases |
|------|---------|
| `str` | `strength` |
| `dex` | `dexterity` |
| `vit` | `vitality` |
| `int` | `intelligence` |
| `mag` | `magic` |
| `cha` | `charisma` |
| `hp` | - |
| `mp` | - |
| `sp` | - |

**Examples:**
```
/setstats Warrior123 str 100
> Set Warrior123's str to 100

/setstats Warrior123 hp 500
> Set Warrior123's hp to 500
```

---

### /setgold
Set a player's gold amount.

| Property | Value |
|----------|-------|
| **Aliases** | `/gold` |
| **Level** | Admin |
| **Usage** | `/setgold <player_name> <amount>` |

**Parameters:**
- `player_name` (required) - Player to modify
- `amount` (required) - Gold amount (must be positive)

**Example:**
```
/setgold Warrior123 100000
> Set Warrior123's gold to 100000
```

---

## Moderation Commands

### /mute
Mute a player (prevent them from chatting).

| Property | Value |
|----------|-------|
| **Aliases** | `/silence` |
| **Level** | Moderator |
| **Usage** | `/mute <player_id> [duration] [reason]` |

**Parameters:**
- `player_id` (required) - Player ID to mute
- `duration` (optional) - Duration in seconds. 0 = permanent. Default: 0
- `reason` (optional) - Reason for mute. Default: "No reason given"

**Examples:**
```
/mute 12345 3600 "Spamming chat"
> Muted player 12345 for 3600 seconds

/mute 12345 0 "Repeated offenses"
> Muted player 12345 permanently
```

---

### /unmute
Remove mute from a player.

| Property | Value |
|----------|-------|
| **Level** | Moderator |
| **Usage** | `/unmute <player_id>` |

**Parameters:**
- `player_id` (required) - Player ID to unmute

**Example:**
```
/unmute 12345
> Unmuted player 12345
```

---

### /kick
Kick a player from the server.

| Property | Value |
|----------|-------|
| **Level** | Moderator |
| **Usage** | `/kick <player_id> [reason]` |

**Parameters:**
- `player_id` (required) - Player ID to kick
- `reason` (optional) - Reason for kick. Default: "No reason given"

**Example:**
```
/kick 12345 "AFK too long"
> Kicked player 12345
```

---

### /ban
Ban a player from the server.

| Property | Value |
|----------|-------|
| **Level** | Senior GM |
| **Usage** | `/ban <player_id> [duration] [reason]` |

**Parameters:**
- `player_id` (required) - Player ID to ban
- `duration` (optional) - Duration in seconds. 0 = permanent. Default: 0
- `reason` (optional) - Reason for ban. Default: "No reason given"

**Examples:**
```
/ban 12345 86400 "Hacking"
> Banned player 12345 for 86400 seconds

/ban 12345 0 "Repeated rule violations"
> Banned player 12345 permanently
```

---

## Utility Commands

### /help
Show available commands or get help for a specific command.

| Property | Value |
|----------|-------|
| **Aliases** | `/?`, `/commands` |
| **Level** | Helper |
| **Usage** | `/help [command]` |

**Parameters:**
- `command` (optional) - Command to get detailed help for

**Examples:**
```
/help
> Available Commands:
>   /help - Show available commands
>   /who - List online administrators
>   ...

/help teleport
> Command: teleport
> Description: Teleport to specific coordinates
> Usage: /teleport <map_name> <x> <y>
> Required Level: Game Master
> Aliases: tp, move
```

---

### /log
View recent command log entries.

| Property | Value |
|----------|-------|
| **Aliases** | `/cmdlog` |
| **Level** | Senior GM |
| **Usage** | `/log [count]` |

**Parameters:**
- `count` (optional) - Number of entries to show. Default: 20

**Example:**
```
/log 10
> Command Log (last 10 entries):
> [AdminName] /teleport aresden 100 150
> [GMBob] /heal Warrior123
> ...
```

---

## Server Management Commands

### /reloadconfig
Re-read `server.yaml` from the path the server booted with. Sections read live
(logging, auto_save, tick_interval_ms, attack_speed) take effect at once; database,
websocket and legacy protocol settings need a restart.

| Property | Value |
|----------|-------|
| **Aliases** | `/reload` |
| **Level** | Admin |
| **Usage** | `/reloadconfig` |

**Example:**
```
/reloadconfig
> Reloaded D:\HelbreathX\server\bin\server.yaml. Applied now: logging, auto_save, tick_interval_ms, attack_speed. Restart needed for: database, websocket, legacy protocol.
```

### /shutdown
Stop the server immediately, or after a countdown with system-chat warnings at 5 min,
60 s, 30 s and 10 s. The countdown shares the `shutdown_countdown` scheduler tag with
the admin web API, so either side can cancel it.

| Property | Value |
|----------|-------|
| **Aliases** | `/stopserver` |
| **Level** | Admin |
| **Usage** | `/shutdown [seconds] [reason]` or `/shutdown cancel` |

**Parameters:**
- `seconds` (optional) - Countdown; `0` or omitted stops at once; `cancel` aborts a running countdown
- `reason` (optional) - Shown to players; defaults to "Server shutdown by <GM>"

**Examples:**
```
/shutdown
> Shutting down: Server shutdown by Admin

/shutdown 120 Maintenance window
> Shutdown in 120s: Maintenance window (/shutdown cancel to abort)

/shutdown cancel
> Shutdown countdown cancelled
```

---

## Built-in Player Commands

These commands are available to all players (no admin level required):

### /online
Show the number of players currently online.

| Property | Value |
|----------|-------|
| **Usage** | `/online` |

**Example:**
```
/online
> 42 players online
```

---

### /time
Show the current server time.

| Property | Value |
|----------|-------|
| **Usage** | `/time` |

**Example:**
```
/time
> 2026-02-02T03:45:00Z
```

---

### /pos
Show your current position.

| Property | Value |
|----------|-------|
| **Aliases** | `/position` |
| **Usage** | `/pos` |

**Example:**
```
/pos
> Position: middleland (150, 200)
```

---

## GM Chat Prefixes

GMs can use special chat prefixes for announcements:

| Prefix | Description |
|--------|-------------|
| `^` | Global GM announcement (sent to all players) |
| `!` | Shout (server-wide message) |

**Example:**
```
^Server will restart in 5 minutes!
```

---

## Setting Admin Levels

Admin levels are stored in the database `accounts` table in the `admin_level` column:

| Database Value | Level |
|----------------|-------|
| 0 | Player |
| 1 | Helper |
| 10 | Game Master |
| 15 | Senior GM |
| 20 | Administrator |

To grant admin access, update the account in the database:

```sql
UPDATE accounts SET admin_level = 10 WHERE username = 'YourUsername';
```
