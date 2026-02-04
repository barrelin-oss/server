# Legacy Administrative System Documentation

**System:** Administrative System (GM Commands)
**Primary Files:** `AdminOrder_*` functions in `Game.cpp`
**Estimated Lines:** ~1,500 across Game.cpp
**Complexity:** Medium

This document provides exhaustive documentation of the legacy Helbreath administrative system (Game Master commands) for use in modernization efforts.

---

## Table of Contents

1. [Overview](#1-overview)
2. [Permission System](#2-permission-system)
3. [Admin Level Configuration](#3-admin-level-configuration)
4. [Two-Factor Authentication](#4-two-factor-authentication)
5. [Admin List Configuration](#5-admin-list-configuration)
6. [GM Commands Reference](#6-gm-commands-reference)
7. [Player Management Commands](#7-player-management-commands)
8. [NPC/Creature Commands](#8-npccreature-commands)
9. [Item Commands](#9-item-commands)
10. [Teleportation Commands](#10-teleportation-commands)
11. [Status Effect Commands](#11-status-effect-commands)
12. [Server Control Commands](#12-server-control-commands)
13. [War/Event Commands](#13-warevent-commands)
14. [Information Commands](#14-information-commands)
15. [Logging and Auditing](#15-logging-and-auditing)
16. [Banned List System](#16-banned-list-system)
17. [Data Structures](#17-data-structures)
18. [Constants Reference](#18-constants-reference)
19. [Core Functions Reference](#19-core-functions-reference)

---

## 1. Overview

The Administrative System provides Game Masters (GMs) with powerful commands to manage players, spawn creatures, create items, control server events, and maintain game integrity. The system features:

- **Tiered permission levels** - Commands require different admin levels (1-4+)
- **Two-factor authentication** - High-privilege commands require security code activation
- **Comprehensive logging** - All GM actions are logged to the log server
- **Configurable thresholds** - Admin level requirements can be customized per command
- **Admin verification list** - Character names can be pre-verified as GMs

### Key Characteristics

| Aspect | Value |
|--------|-------|
| Maximum Admin Characters | 50 (`DEF_MAXADMINS`) |
| Maximum Banned IPs | 500 (`DEF_MAXBANNED`) |
| Admin Levels | 0 (normal) to 4+ (full access) |
| Security Code Length | Up to 10 characters |

---

## 2. Permission System

### Admin User Level

Each player has an `m_iAdminUserLevel` field that determines their permissions:

```cpp
// In CClient class
int m_iAdminUserLevel;  // 0 = normal player, higher = more permissions
```

| Level | Typical Role | Example Commands |
|-------|--------------|------------------|
| 0 | Normal Player | None (user commands only) |
| 1 | Junior GM | `/who`, `/checkrep`, `/checkstatus`, `/summonplayer` |
| 2 | Standard GM | `/teleport`, `/shutup`, `/checkip`, `/setinvi` |
| 3 | Senior GM | `/kill`, `/revive`, `/summon`, `/observer`, `/shutdown` |
| 4+ | Lead GM | `/createitem`, `/summonall`, `/disconnectall` |

### Permission Check Pattern

All admin commands follow this pattern:

```cpp
void CGame::AdminOrder_SomeCommand(int iClientH, char* pData, DWORD dwMsgSize)
{
    if (m_pClientList[iClientH] == NULL) return;
    if ((dwMsgSize) <= 0) return;

    // Check admin level
    if (m_pClientList[iClientH]->m_iAdminUserLevel < m_iAdminLevelSomeCommand) {
        SendNotifyMsg(NULL, iClientH, DEF_NOTIFY_ADMINUSERLEVELLOW, NULL, NULL, NULL, NULL);
        return;
    }

    // Execute command...
}
```

### Two-Factor Requirement

Some dangerous commands require `m_bIsAdminCommandEnabled` to be TRUE, achieved via `/enableadmincommand`:

```cpp
// Commands requiring two-factor authentication:
if (m_pClientList[iClientH]->m_bIsAdminCommandEnabled == FALSE) return;
```

**Commands requiring two-factor:**
- `/summon` - Spawn NPCs/creatures
- `/summonall` - Summon all players of a faction
- `/summondemon` - Summon demon NPC
- `/summondeath` - Summon death NPC
- `/disconnectall` - Disconnect all players of a faction
- `/createitem` - Create items
- `/storm` - Summon storm

---

## 3. Admin Level Configuration

Admin level requirements are loaded from `GServer.cfg` and can be customized:

### Configuration Variables

| Variable | Default | Description |
|----------|---------|-------------|
| `m_iAdminLevelWho` | 1 | `/who` - Show total users |
| `m_iAdminLevelGMCheckRep` | 1 | `/checkrep` - Check player reputation |
| `m_iAdminLevelSummonPlayer` | 1 | `/summonplayer` - Summon single player |
| `m_iAdminLevelShutup` | 2 | `/shutup` - Mute player |
| `m_iAdminLevelTeleport` | 2 | `/teleport` - Teleport self |
| `m_iAdminLevelCheckIP` | 2 | `/checkip` - Check IP info |
| `m_iAdminLevelPolymorph` | 2 | `/polymorph` - Transform self |
| `m_iAdminLevelSetInvis` | 2 | `/setinvi` - Toggle invisibility |
| `m_iAdminLevelSetZerk` | 2 | `/setzerk` - Set berserk status |
| `m_iAdminLevelSetIce` | 2 | `/setfreeze` - Set frozen status |
| `m_iAdminLevelGetNpcStatus` | 2 | `/gns` - Get NPC status |
| `m_iAdminLevelSetAttackMode` | 2 | `/setattackmode` - Set NPC attack mode |
| `m_iAdminLevelCallGaurd` | 2 | `/attack` - Call guard to attack player |
| `m_iAdminLevelReserveFightzone` | 2 | `/reservefightzone` - Reserve arena |
| `m_iAdminLevelCreateFish` | 2 | `/createfish` - Create fishing node |
| `m_iAdminLevelEnergySphere` | 2 | `/energysphere` - Spawn energy sphere |
| `m_iAdminLevelDisconnectAll` | 2 | `/disconnectall` - Disconnect faction |
| `m_iAdminLevelGMKill` | 3 | `/kill` - Kill player |
| `m_iAdminLevelGMRevive` | 3 | `/revive` - Revive player |
| `m_iAdminLevelGMCloseconn` | 3 | `/closeconn` - Close connection |
| `m_iAdminLevelObserver` | 3 | `/setobservermode` - Toggle observer |
| `m_iAdminLevelShutdown` | 3 | `/shutdownthisserverrightnow` - Shutdown |
| `m_iAdminLevelSummon` | 3 | `/summon` - Spawn NPC |
| `m_iAdminLevelSummonDemon` | 3 | `/summondemon` - Spawn demon |
| `m_iAdminLevelSummonDeath` | 3 | `/summondeath` - Spawn death |
| `m_iAdminLevelUnsummonAll` | 3 | `/unsummonall` - Remove all NPCs |
| `m_iAdminLevelUnsummonDemon` | 3 | `/unsummondemon` - Remove demon |
| `m_iAdminLevelEnableCreateItem` | 3 | Enable item creation capability |
| `m_iAdminLevelSummonAll` | 4 | `/summonall` - Summon all faction players |
| `m_iAdminLevelCreateItem` | 4 | `/createitem` - Create items |
| `m_iAdminLevelStorm` | varies | `/storm` - Weather effects |
| `m_iAdminLevelWeather` | varies | `/weather` - Change weather |
| `m_iAdminLevelSetStatus` | varies | `/setstatus` - Set status effects |
| `m_iAdminLevelGoto` | varies | `/goto` - Teleport to player |
| `m_iAdminLevelMonsterCount` | varies | `/monstercount` - Count monsters |
| `m_iAdminLevelSetRecallTime` | varies | `/setforcerecalltime` - Set recall time |
| `m_iAdminLevelUnsummonBoss` | varies | `/unsummonboss` - Remove boss NPCs |
| `m_iAdminLevelClearNpc` | varies | `/clearnpc` - Clear all NPCs |
| `m_iAdminLevelTime` | varies | `/time` - Set game time |
| `m_iAdminLevelPushPlayer` | varies | `/send` - Push player to location |
| `m_iAdminLevelSummonGuild` | varies | `/summonguild` - Summon guild members |
| `m_iAdminLevelCheckStatus` | varies | `/checkstatus` - Check player stats |
| `m_iAdminLevelCleanMap` | varies | `/cleanmap` - Clean map objects |

### Configuration File Format

```
// In GServer.cfg
admin-level-kill = 3
admin-level-revive = 3
admin-level-closeconn = 3
admin-level-checkrep = 1
admin-level-who = 1
admin-level-energysphere = 2
admin-level-shutdown = 3
admin-level-observer = 3
// ... etc
```

---

## 4. Two-Factor Authentication

High-privilege commands require activation via `/enableadmincommand <security_code>`:

### Security Code

```cpp
char m_cSecurityNumber[11];  // Server-side security code (up to 10 chars)
```

The security code is configured in `GServer.cfg`:
```
security-code = mysecret123
```

### Activation Process

```cpp
void CGame::AdminOrder_EnableAdminCommand(int iClientH, char *pData, DWORD dwMsgSize)
{
    // Parse security code from command
    if (token != NULL) {
        len = strlen(token);
        if(len > 10) len = 10;

        // Compare with server's security code
        if (memcmp(token, m_cSecurityNumber, len) == 0) {
            m_pClientList[iClientH]->m_bIsAdminCommandEnabled = TRUE;
        }
        else {
            // Log failed attempt
            wsprintf(G_cTxt, "(%s) Player(%s) attempts to access /enableadmincommand with %s",
                     m_pClientList[iClientH]->m_cIPaddress,
                     m_pClientList[iClientH]->m_cCharName, token);
            PutHackLogFileList(G_cTxt);
        }
    }
}
```

### Client State

```cpp
// In CClient class
BOOL m_bIsAdminCommandEnabled;  // Default: FALSE, reset on login
```

---

## 5. Admin List Configuration

GMs can be pre-verified by character name in `AdminList.cfg`:

### Configuration Format

```
// AdminList.cfg
verified-admin = GMCharName1
verified-admin = GMCharName2
verified-admin = AdminChar3
```

### Verification Logic

When a character logs in, the server checks if their name is in the admin list:

```cpp
for (i = 0; i < DEF_MAXADMINS; i++) {
    if(strlen(m_stAdminList[i].m_cGMName) == 0) break;
    if ((strlen(m_stAdminList[i].m_cGMName)) == (strlen(m_pClientList[iClientH]->m_cCharName))) {
        if(memcmp(m_stAdminList[i].m_cGMName, m_pClientList[iClientH]->m_cCharName,
                  strlen(m_pClientList[iClientH]->m_cCharName)) == 0) {
            // Character is a verified admin
        }
    }
}
```

---

## 6. GM Commands Reference

### Command Parsing

Commands are parsed in `ChatMsgHandler()` at line ~9080 of Game.cpp:

```cpp
if (memcmp(cp, "/commandname ", length) == 0) {
    AdminOrder_CommandName(iClientH, cp, dwMsgSize - 21);
    return;
}
```

### Complete Command List

| Command | Function | Admin Level | Two-Factor | Description |
|---------|----------|-------------|------------|-------------|
| `/who` | Inline | 1 | No | Show total users online |
| `/checkrep` | `AdminOrder_CheckRep` | 1 | No | Check player reputation |
| `/checkstatus <name>` | `AdminOrder_CheckStats` | 1 | No | Check player stats |
| `/summonplayer <name>` | `AdminOrder_SummonPlayer` | 1 | No | Summon player to your location |
| `/shutup <name> <mins>` | `ShutUpPlayer` | 2 | No | Mute player for X minutes |
| `/teleport <map> [x] [y]` | `AdminOrder_Teleport` | 2 | No | Teleport self to location |
| `/tp <map> [x] [y]` | `AdminOrder_Teleport` | 2 | No | Alias for /teleport |
| `/goto <name>` | `AdminOrder_GoTo` | varies | No | Teleport to player |
| `/checkip <ip>` | `AdminOrder_CheckIP` | 2 | No | Get info about IP |
| `/polymorph <type>` | `AdminOrder_Polymorph` | 2 | No | Transform into creature |
| `/setinvi <0/1>` | `AdminOrder_SetInvi` | 2 | No | Toggle invisibility |
| `/setzerk <name>` | `AdminOrder_SetZerk` | 2 | No | Apply berserk status |
| `/setfreeze <name>` | `AdminOrder_SetFreeze` | 2 | No | Apply frozen status |
| `/setstatus <code>` | `AdminOrder_SetStatus` | varies | No | Set status effects |
| `/gns <name>` | `AdminOrder_GetNpcStatus` | 2 | No | Get NPC status |
| `/setattackmode <name>` | `AdminOrder_SetAttackMode` | 2 | No | Set NPC attack mode |
| `/attack <name>` | `AdminOrder_CallGuard` | 2 | No | Spawn guard to attack player |
| `/reservefightzone` | `AdminOrder_ReserveFightzone` | 2 | No | Reserve arena slot |
| `/createfish <type> [x] [y]` | `AdminOrder_CreateFish` | 2 | No | Create fishing node |
| `/energysphere` | Inline | 2 | No | Spawn energy sphere |
| `/kill <name> [damage]` | `AdminOrder_Kill` | 3 | No | Kill player |
| `/revive <name> [damage] [hp]` | `AdminOrder_Revive` | 3 | No | Revive player |
| `/closeconn <name>` | `AdminOrder_CloseConn` | 3 | No | Disconnect player |
| `/setobservermode` | `AdminOrder_SetObserverMode` | 3 | No | Toggle observer mode |
| `/summon <npc> [count]` | `AdminOrder_Summon` | 3 | Yes | Spawn NPC(s) |
| `/summondemon` | `AdminOrder_SummonDemon` | 3 | Yes | Spawn demon |
| `/summondeath` | `AdminOrder_SummonDeath` | 3 | Yes | Spawn death NPC |
| `/unsummonall` | `AdminOrder_UnsummonAll` | 3 | No | Remove all spawned NPCs |
| `/unsummondemon` | `AdminOrder_UnsummonDemon` | 3 | No | Remove demon NPC |
| `/unsummonboss` | `AdminOrder_UnsummonBoss` | varies | No | Remove boss NPCs |
| `/clearnpc` | `AdminOrder_ClearNpc` | varies | No | Clear all NPCs on map |
| `/shutdownthisserverrightnow` | Inline | 3 | No | Shutdown server |
| `/summonall <faction>` | `AdminOrder_SummonAll` | 4 | Yes | Summon all faction players |
| `/summonguild` | `AdminOrder_SummonGuild` | varies | No | Summon guild members |
| `/disconnectall <faction>` | `AdminOrder_DisconnectAll` | 2 | Yes | Disconnect faction |
| `/createitem <name> [attr] [val]` | `AdminOrder_CreateItem` | 4 | Yes | Create item |
| `/enableadmincommand <code>` | `AdminOrder_EnableAdminCommand` | varies | N/A | Enable two-factor |
| `/storm <type>` | `AdminOrder_SummonStorm` | varies | Yes | Create storm effect |
| `/weather <type>` | `AdminOrder_Weather` | varies | No | Change weather |
| `/time <value>` | `AdminOrder_Time` | varies | No | Set game time |
| `/send <name> <map> [x] [y]` | `AdminOrder_Pushplayer` | varies | No | Force teleport player |
| `/monstercount` | `AdminOrder_MonsterCount` | varies | No | Count monsters |
| `/setforcerecalltime <mins>` | `AdminOrder_SetForceRecallTime` | varies | No | Set recall timer |
| `/getticket` | `AdminOrder_GetFightzoneTicket` | 2 | No | Get arena ticket |
| `/begincrusadetotalwar` | `ManualStartCrusadeMode` | 3+ | No | Start crusade |
| `/endcrusadetotalwar` | `ManualEndCrusadeMode` | 3+ | No | End crusade |
| `/beginheldenian` | `ManualStartHeldenianMode` | 3+ | No | Start Heldenian war |
| `/endheldenian` | `ManualEndHeldenianMode` | 3+ | No | End Heldenian war |

---

## 7. Player Management Commands

### /kill - Kill Player

```cpp
void CGame::AdminOrder_Kill(int iClientH, char * pData, DWORD dwMsgSize)
```

**Syntax:** `/kill <player_name> [damage_amount]`

**Effect:**
- Sets target player's HP to 0
- Sets `m_bIsKilled = TRUE`
- Clears any exchange mode
- Removes from target lists
- Sends death notification
- Broadcasts death animation

### /revive - Revive Player

```cpp
void CGame::AdminOrder_Revive(int iClientH, char * pData, DWORD dwMsgSize)
```

**Syntax:** `/revive <player_name> [damage_visual] [hp_amount]`

**Effect:**
- Restores HP to specified amount (capped at max HP)
- Sets `m_bIsKilled = FALSE`
- Sends HP notification
- Broadcasts damage animation (visual only)

### /closeconn - Disconnect Player

```cpp
void CGame::AdminOrder_CloseConn(int iClientH, char * pData, DWORD dwMsgSize)
```

**Syntax:** `/closeconn <player_name>`

**Effect:**
- Forces player disconnection
- Saves character data before disconnect

### /shutup - Mute Player

```cpp
void CGame::ShutUpPlayer(int iClientH, char * pData, DWORD dwMsgSize)
```

**Syntax:** `/shutup <player_name> <minutes>`

**Effect:**
- Sets `m_iTimeLeft_ShutUp = minutes * 20` (ticks)
- Player cannot send chat messages
- Works cross-server via gate server
- Logs action to admin log

### /summonplayer - Summon Single Player

```cpp
void CGame::AdminOrder_SummonPlayer(int iClientH, char *pData, DWORD dwMsgSize)
```

**Syntax:** `/summonplayer <player_name>`

**Effect:**
- Teleports target player to GM's current location
- Works cross-server via gate server
- Logs action to log server

### /summonall - Summon Faction

```cpp
void CGame::AdminOrder_SummonAll(int iClientH, char *pData, DWORD dwMsgSize)
```

**Syntax:** `/summonall <faction_name>` (e.g., "aresden" or "elvine")

**Effect:**
- Teleports all online players of specified faction to GM's location
- Requires two-factor authentication
- Works cross-server via gate server
- Logs action to log server

### /summonguild - Summon Guild Members

```cpp
void CGame::AdminOrder_SummonGuild(int iClientH, char *pData, DWORD dwMsgSize)
```

**Syntax:** `/summonguild`

**Effect:**
- Teleports all online guild members to GM's location
- Costs 100,000 gold (50,000 actually deducted)
- Only available to guild rank 0 (leader)
- Not available during crusade mode

### /disconnectall - Disconnect Faction

```cpp
void CGame::AdminOrder_DisconnectAll(int iClientH, char *pData, DWORD dwMsgSize)
```

**Syntax:** `/disconnectall <faction_name>`

**Effect:**
- Disconnects all players of specified faction
- Requires two-factor authentication

### /send - Force Teleport Player

```cpp
void CGame::AdminOrder_Pushplayer(int iClientH, char * pData, DWORD dwMsgSize)
```

**Syntax:** `/send <player_name> <map_name> [x] [y]`

**Effect:**
- Forces player to teleport to specified location
- Player has no control over destination

---

## 8. NPC/Creature Commands

### /summon - Spawn NPC

```cpp
void CGame::AdminOrder_Summon(int iClientH, char *pData, DWORD dwMsgSize)
```

**Syntax:** `/summon <npc_name> [count]`

**Effect:**
- Creates NPC(s) at GM's current location
- Maximum 20 NPCs per command
- Uses random movement type
- Requires two-factor authentication

### /summondemon - Spawn Demon

```cpp
void CGame::AdminOrder_SummonDemon(int iClientH)
```

**Syntax:** `/summondemon`

**Effect:**
- Spawns a Demon NPC at GM's location
- Requires two-factor authentication

### /summondeath - Spawn Death

```cpp
void CGame::AdminOrder_SummonDeath(int iClientH)
```

**Syntax:** `/summondeath`

**Effect:**
- Spawns a Death NPC at GM's location
- Requires two-factor authentication

### /attack - Call Guard

```cpp
void CGame::AdminOrder_CallGuard(int iClientH, char * pData, DWORD dwMsgSize)
```

**Syntax:** `/attack <player_name>`

**Effect:**
- Spawns a city guard (faction-appropriate) at target player's location
- Guard immediately attacks the target player
- Guard type: Aresden in Aresden, Elvine in Elvine, Neutral elsewhere

### /unsummonall - Remove All NPCs

```cpp
void CGame::AdminOrder_UnsummonAll(int iClientH)
```

**Syntax:** `/unsummonall`

**Effect:**
- Removes all GM-summoned NPCs on current map

### /unsummondemon - Remove Demon

```cpp
void CGame::AdminOrder_UnsummonDemon(int iClientH)
```

**Syntax:** `/unsummondemon`

**Effect:**
- Removes demon NPC(s) on current map

### /unsummonboss - Remove Boss

```cpp
void CGame::AdminOrder_UnsummonBoss(int iClientH)
```

**Syntax:** `/unsummonboss`

**Effect:**
- Removes boss NPC(s) on current map

### /clearnpc - Clear All NPCs

```cpp
void CGame::AdminOrder_ClearNpc(int iClientH)
```

**Syntax:** `/clearnpc`

**Effect:**
- Removes all NPCs on current map

### /gns - Get NPC Status

```cpp
void CGame::AdminOrder_GetNpcStatus(int iClientH, char * pData, DWORD dwMsgSize)
```

**Syntax:** `/gns <npc_name>`

**Effect:**
- Returns detailed status information about NPC

### /setattackmode - Set NPC Attack Mode

```cpp
void CGame::AdminOrder_SetAttackMode(int iClientH, char *pData, DWORD dwMsgSize)
```

**Syntax:** `/setattackmode <npc_name> <mode>`

**Effect:**
- Changes NPC's attack behavior mode

---

## 9. Item Commands

### /createitem - Create Item

```cpp
void CGame::AdminOrder_CreateItem(int iClientH, char *pData, DWORD dwMsgSize)
```

**Syntax:** `/createitem <item_name> [attribute] [value]`

**Effect:**
- Creates item with specified name
- Optional attribute (enchantment code)
- Optional value (for specific enchantment level)
- Requires two-factor authentication
- Assigns unique item ID with timestamp
- Logs creation to log server

**Attribute System:**
- Attribute = 1: Quality modifier (value 1-200, centered at 100)
- Other attributes: Elemental/enchantment codes
  - Color coding based on attribute type (element)

**Example:**
```
/createitem Excalibur        // Basic item
/createitem Excalibur 1 150  // +50% quality
/createitem Excalibur 65536  // With elemental attribute
```

### /createfish - Create Fishing Node

```cpp
void CGame::AdminOrder_CreateFish(int iClientH, char * pData, DWORD dwMsgSize)
```

**Syntax:** `/createfish <fish_type> [x] [y]`

**Effect:**
- Creates a fishing node at specified or current location

### /getticket - Get Arena Ticket

```cpp
void CGame::AdminOrder_GetFightzoneTicket(int iClientH)
```

**Syntax:** `/getticket`

**Effect:**
- Gives GM an arena/fightzone ticket

---

## 10. Teleportation Commands

### /teleport - Teleport Self

```cpp
void CGame::AdminOrder_Teleport(int iClientH, char * pData, DWORD dwMsgSize)
```

**Syntax:** `/teleport <map_name> [x] [y]` or `/tp <map_name> [x] [y]`

**Effect:**
- Teleports GM to specified map
- Optional coordinates (random spawn if omitted)
- Validates map name against whitelist

**Valid Maps:**
```
2ndmiddle, abaddon, arebrk11, arebrk12, arebrk21, arebrk22, arefarm,
arejail, aremidl, aremidr, aresden, aresdend1, areuni, arewrhus,
bisle, bsmith_1, bsmith_1f, bsmith_2, bsmith_2f, BtField, cath_1,
cath_2, cityhall_1, cityhall_2, CmdHall_1, CmdHall_2, default,
dglv2, dglv3, dglv4, druncncity, elvbrk11, elvbrk12, elvbrk21,
elvbrk22, elvfarm, elvine, elvined1, elvjail, elvmidl, elvmidr,
elvuni, elvwrhus, fightzone1-10, gldhall_1, gldhall_2, GodH,
gshop_1, gshop_1f, gshop_2, gshop_2f, HRampart, huntzone1-4,
icebound, inferniaA, inferniaB, maze, middled1n, middled1x,
middleland, penalty, procella, resurr1, resurr2, toh1, toh2, toh3,
wrhus_1, wrhus_1f, wrhus_2, wrhus_2f, wzdtwr_1, wzdtwr_2, Test,
GMMap, dv, HBX
```

### /goto - Teleport to Player

```cpp
void CGame::AdminOrder_GoTo(int iClientH, char* pData, DWORD dwMsgSize)
```

**Syntax:** `/goto <player_name>`

**Effect:**
- Teleports GM to target player's current location
- Works cross-server via lookup
- Logs action to log server

---

## 11. Status Effect Commands

### /setobservermode - Toggle Observer

```cpp
void CGame::AdminOrder_SetObserverMode(int iClientH)
```

**Syntax:** `/setobservermode`

**Effect:**
- Toggles observer mode on/off
- When ON: GM becomes invisible and non-interactable
- When OFF: GM reappears at current position

```cpp
if (m_pClientList[iClientH]->m_bIsObserverMode == TRUE) {
    // Reappear on map
    m_pMapList[...].SetOwner(...);
    SendEventToNearClient_TypeA(..., DEF_MSGTYPE_CONFIRM, ...);
    m_pClientList[iClientH]->m_bIsObserverMode = FALSE;
}
else {
    // Disappear from map
    m_pMapList[...].ClearOwner(...);
    SendEventToNearClient_TypeA(..., DEF_MSGTYPE_REJECT, ...);
    m_pClientList[iClientH]->m_bIsObserverMode = TRUE;
}
```

### /setinvi - Toggle Invisibility

```cpp
void CGame::AdminOrder_SetInvi(int iClientH, char *pData, DWORD dwMsgSize)
```

**Syntax:** `/setinvi <0|1>`

**Effect:**
- 0: Remove invisibility
- 1: Apply invisibility aura

### /setzerk - Apply Berserk

```cpp
void CGame::AdminOrder_SetZerk(int iClientH, char *pData, DWORD dwMsgSize)
```

**Syntax:** `/setzerk <player_name>`

**Effect:**
- Applies berserk status to target player

### /setfreeze - Apply Frozen

```cpp
void CGame::AdminOrder_SetFreeze(int iClientH, char *pData, DWORD dwMsgSize)
```

**Syntax:** `/setfreeze <player_name>`

**Effect:**
- Applies frozen/ice status to target player

### /setstatus - Set Status Effects

```cpp
void CGame::AdminOrder_SetStatus(int iClientH, char *pData, DWORD dwMsgSize)
```

**Syntax:** `/setstatus <code>`

**Effect codes:**
| Code | Effect |
|------|--------|
| 0 | Clear all status effects |
| 1 | Set poison |
| 2 | Set illusion |
| 3 | Set defense shield |
| 4 | Set magic protection |
| 5 | Set protection from arrows |
| 6 | Set illusion movement |
| 7 | Set inhibition casting |
| 8 | Set hero status |

### /polymorph - Transform

```cpp
void CGame::AdminOrder_Polymorph(int iClientH, char *pData, DWORD dwMsgSize)
```

**Syntax:** `/polymorph <creature_type>`

**Effect:**
- Transforms GM into specified creature type
- Visual only, stats unchanged

---

## 12. Server Control Commands

### /shutdownthisserverrightnow - Server Shutdown

```cpp
// Inline in ChatMsgHandler
if ((memcmp(cp, "/shutdownthisserverrightnow ", 28) == 0) &&
    (m_pClientList[iClientH]->m_iAdminUserLevel >= m_iAdminLevelShutdown)) {
    m_cShutDownCode      = 2;
    m_bOnExitProcess     = TRUE;
    m_dwExitProcessTime  = timeGetTime();
    PutLogList("(!) GAME SERVER SHUTDOWN PROCESS BEGIN(by Admin-Command)!!!");
    bSendMsgToLS(MSGID_GAMESERVERSHUTDOWNED, NULL);
    // Save crusade data if applicable
    if (m_iMiddlelandMapIndex > 0) {
        SaveOccupyFlagData();
    }
}
```

**Syntax:** `/shutdownthisserverrightnow`

**Effect:**
- Initiates graceful server shutdown
- Notifies log server
- Saves crusade/territory data

### /time - Set Game Time

```cpp
void CGame::AdminOrder_Time(int iClientH, char * pData, DWORD dwMsgSize)
```

**Syntax:** `/time <value>`

**Effect:**
- Sets the current game time value
- Affects day/night cycle

### /weather - Change Weather

```cpp
void CGame::AdminOrder_Weather(int iClientH, char * pData, DWORD dwMsgSize)
```

**Syntax:** `/weather <type>`

**Effect:**
- Changes current weather conditions

### /monstercount - Count Monsters

```cpp
void CGame::AdminOrder_MonsterCount(int iClientH, char* pData, DWORD dwMsgSize)
```

**Syntax:** `/monstercount`

**Effect:**
- Returns count of monsters on current map

### /setforcerecalltime - Set Recall Timer

```cpp
void CGame::AdminOrder_SetForceRecallTime(int iClientH, char *pData, DWORD dwMsgSize)
```

**Syntax:** `/setforcerecalltime <minutes>`

**Effect:**
- Sets the force recall timer for the server

---

## 13. War/Event Commands

### /begincrusadetotalwar - Start Crusade

**Syntax:** `/begincrusadetotalwar`

**Admin Level:** 3+

**Effect:**
- Starts crusade mode (faction war)
- Logs action to log server

### /endcrusadetotalwar - End Crusade

**Syntax:** `/endcrusadetotalwar`

**Admin Level:** 3+

**Effect:**
- Ends crusade mode
- Logs action to log server

### /beginheldenian - Start Heldenian

**Syntax:** `/beginheldenian`

**Admin Level:** 3+

**Effect:**
- Starts Heldenian war event
- Logs action to log server

### /endheldenian - End Heldenian

**Syntax:** `/endheldenian`

**Admin Level:** 3+

**Effect:**
- Ends Heldenian war event
- Logs action to log server

### /reservefightzone - Reserve Arena

```cpp
void CGame::AdminOrder_ReserveFightzone(int iClientH, char * pData, DWORD dwMsgSize)
```

**Syntax:** `/reservefightzone`

**Effect:**
- Reserves an arena/fightzone slot

### /energysphere - Spawn Energy Sphere

**Syntax:** `/energysphere`

**Admin Level:** 2

**Effect:**
- Spawns energy sphere event object
- Calls `EnergySphereProcessor(TRUE, iClientH)`

### /storm - Summon Storm

```cpp
void CGame::AdminOrder_SummonStorm(int iClientH, char* pData, DWORD dwMsgSize)
```

**Syntax:** `/storm <type>`

**Effect:**
- Creates storm weather effect
- Requires two-factor authentication

---

## 14. Information Commands

### /who - User Count

**Syntax:** `/who`

**Admin Level:** 1

**Effect:**
- Returns total number of online users

```cpp
if (m_pClientList[iClientH]->m_iAdminUserLevel >= m_iAdminLevelWho) {
    SendNotifyMsg(NULL, iClientH, DEF_NOTIFY_TOTALUSERS, NULL, NULL, NULL, NULL);
}
```

### /checkrep - Check Reputation

```cpp
void CGame::AdminOrder_CheckRep(int iClientH, char *pData, DWORD dwMsgSize)
```

**Syntax:** `/checkrep <player_name>`

**Admin Level:** 1

**Effect:**
- Returns target player's reputation value

### /checkstatus - Check Stats

```cpp
void CGame::AdminOrder_CheckStats(int iClientH, char *pData, DWORD dwMsgSize)
```

**Syntax:** `/checkstatus <player_name>`

**Admin Level:** 1

**Effect:**
- Returns target player's core stats:
  - STR, DEX, VIT, INT, MAG, CHR

```cpp
wsprintf(cStatMessage, "Str:%d Dex:%d Vit:%d Int:%d Mag:%d Chr:%d",
         m_pClientList[i]->m_iStr, m_pClientList[i]->m_iDex,
         m_pClientList[i]->m_iVit, m_pClientList[i]->m_iInt,
         m_pClientList[i]->m_iMag, m_pClientList[i]->m_iCharisma);
```

### /checkip - Check IP Info

```cpp
void CGame::AdminOrder_CheckIP(int iClientH, char *pData, DWORD dwMsgSize)
```

**Syntax:** `/checkip <ip_address>`

**Admin Level:** 2

**Effect:**
- Returns all players matching IP address prefix
- Shows: Account name, Character name, Location (map, x, y), Level, Init status, Full IP

```cpp
wsprintf(cInfoString, "Name(%s/%s) Loc(%s: %d %d) Level(%d:%d) Init(%d) IP(%s)",
         m_pClientList[i]->m_cAccountName, m_pClientList[i]->m_cCharName,
         m_pClientList[i]->m_cMapName, m_pClientList[i]->m_sX, m_pClientList[i]->m_sY,
         m_pClientList[i]->m_iLevel, NULL, m_pClientList[i]->m_bIsInitComplete,
         m_pClientList[i]->m_cIPaddress);
```

---

## 15. Logging and Auditing

### GM Action Logging

All significant GM actions are logged to the log server via `bSendMsgToLS`:

```cpp
wsprintf(G_cTxt, "GM Order(%s): <action details>", m_pClientList[iClientH]->m_cCharName);
bSendMsgToLS(MSGID_GAMEMASTERLOG, iClientH, FALSE, G_cTxt);
```

### Logged Actions

| Action | Log Format |
|--------|------------|
| Create Item | `"(%s) GM Order(%s): Create ItemName(%s)"` |
| GoTo Player | `"GM Order(%s): GoTo MapName(%s)(%d %d)"` |
| Summon Player | `"GM Order(%s): PC(%s) Summoned to (%s)"` |
| Summon All | `"GM Order(%s): PC(%s) Summoned to (%s)"` |
| Summon Guild | `"GM Order(%s): PC(%s) Summoned to (%s)"` |
| Begin Crusade | `"(%s) GM Order(%s): begincrusadetotalwar"` |
| End Crusade | `"(%s) GM Order(%s): endcrusadetotalwar"` |
| Begin Heldenian | `"GM Order(%s): begin Heldenian"` |
| End Heldenian | `"GM Order(%s): end Heldenian"` |
| Time Change | Logged via `MSGID_GAMEMASTERLOG` |

### Admin Log File

Local logging via `PutAdminLogFileList`:

```cpp
wsprintf(G_cTxt, "GM Order(%s): Shutup PC(%s) (%d)Min",
         m_pClientList[iClientH]->m_cCharName,
         m_pClientList[i]->m_cCharName, iTime);
PutAdminLogFileList(G_cTxt);
```

### Security Logging

Failed security code attempts are logged:

```cpp
wsprintf(G_cTxt, "(%s) Player(%s) attempts to access /enableadmincommand with %s",
         m_pClientList[iClientH]->m_cIPaddress,
         m_pClientList[iClientH]->m_cCharName, token);
PutHackLogFileList(G_cTxt);
```

---

## 16. Banned List System

### IP Banning

The server maintains a list of banned IP addresses:

```cpp
struct {
    char m_cBannedIPaddress[21];
} m_stBannedList[DEF_MAXBANNED];
```

### Configuration File

```
// BannedList.cfg
banned-ip = 192.168.1.100
banned-ip = 10.0.0.50
```

### Ban Check

On client connection, IP is checked against ban list:

```cpp
for (i = 0; i < DEF_MAXBANNED; i++) {
    if(strlen(m_stBannedList[i].m_cBannedIPaddress) == 0) break;
    if ((strlen(m_stBannedList[i].m_cBannedIPaddress)) ==
        (strlen(m_pClientList[iClientH]->m_cIPaddress))) {
        if(memcmp(m_stBannedList[i].m_cBannedIPaddress,
                  m_pClientList[iClientH]->m_cIPaddress,
                  strlen(m_pClientList[iClientH]->m_cIPaddress)) == 0) {
            // Reject connection
        }
    }
}
```

---

## 17. Data Structures

### Admin List Entry

```cpp
struct {
    char m_cGMName[11];  // Character name (10 chars + null)
} m_stAdminList[DEF_MAXADMINS];
```

### Banned List Entry

```cpp
struct {
    char m_cBannedIPaddress[21];  // IP address (20 chars + null)
} m_stBannedList[DEF_MAXBANNED];
```

### Client Admin Fields

```cpp
// In CClient class
int   m_iAdminUserLevel;         // Permission level (0 = normal)
BOOL  m_bIsAdminCommandEnabled;  // Two-factor enabled
BOOL  m_bIsObserverMode;         // Observer mode active
int   m_iTimeLeft_ShutUp;        // Mute timer remaining (ticks)
```

### Server Admin Configuration

```cpp
// In CGame class
char  m_cSecurityNumber[11];         // Two-factor security code
int   m_iAdminLevelWho;              // Required level for /who
int   m_iAdminLevelGMKill;           // Required level for /kill
int   m_iAdminLevelGMRevive;         // Required level for /revive
// ... (43+ configurable admin level variables)
```

---

## 18. Constants Reference

| Constant | Value | Description |
|----------|-------|-------------|
| `DEF_MAXADMINS` | 50 | Maximum verified admin characters |
| `DEF_MAXBANNED` | 500 | Maximum banned IP addresses |
| `DEF_NOTIFY_ADMINUSERLEVELLOW` | varies | Notification: insufficient permissions |
| `DEF_NOTIFY_TOTALUSERS` | varies | Notification: user count |
| `DEF_NOTIFY_PLAYERSHUTUP` | varies | Notification: player muted |
| `DEF_NOTIFY_PLAYERNOTONGAME` | varies | Notification: player not online |
| `DEF_NOTIFY_OBSERVERMODE` | varies | Notification: observer mode toggle |
| `DEF_NOTIFY_IPACCOUNTINFO` | varies | Notification: IP lookup result |
| `MSGID_GAMEMASTERLOG` | varies | Log server message type |

---

## 19. Core Functions Reference

### Admin Command Functions

| Function | Location | Description |
|----------|----------|-------------|
| `AdminOrder_Kill()` | Game.cpp:32983 | Kill player |
| `AdminOrder_Revive()` | Game.cpp:33081 | Revive player |
| `AdminOrder_CloseConn()` | Game.cpp:33320 | Disconnect player |
| `AdminOrder_Teleport()` | Game.cpp:33676 | Teleport self |
| `AdminOrder_GoTo()` | Game.cpp:44517 | Teleport to player |
| `AdminOrder_CheckIP()` | Game.cpp:34280 | Check IP info |
| `AdminOrder_Polymorph()` | Game.cpp:34388 | Transform self |
| `AdminOrder_SetInvi()` | Game.cpp:34664 | Toggle invisibility |
| `AdminOrder_SetZerk()` | Game.cpp:34706 | Apply berserk |
| `AdminOrder_SetFreeze()` | Game.cpp:34744 | Apply frozen |
| `AdminOrder_SetStatus()` | Game.cpp:43571 | Set status effects |
| `AdminOrder_SetObserverMode()` | Game.cpp:40188 | Toggle observer |
| `AdminOrder_EnableAdminCommand()` | Game.cpp:40288 | Enable two-factor |
| `AdminOrder_CreateItem()` | Game.cpp:40323 | Create item |
| `AdminOrder_Summon()` | Game.cpp:39442 | Spawn NPC |
| `AdminOrder_SummonAll()` | Game.cpp:39531 | Summon faction |
| `AdminOrder_SummonPlayer()` | Game.cpp:39605 | Summon player |
| `AdminOrder_SummonGuild()` | Game.cpp:48362 | Summon guild |
| `AdminOrder_SummonDemon()` | Game.cpp:33166 | Spawn demon |
| `AdminOrder_SummonDeath()` | Game.cpp:33211 | Spawn death |
| `AdminOrder_SummonStorm()` | Game.cpp:43227 | Create storm |
| `AdminOrder_UnsummonAll()` | Game.cpp:39311 | Remove NPCs |
| `AdminOrder_UnsummonDemon()` | Game.cpp:39330 | Remove demon |
| `AdminOrder_UnsummonBoss()` | Game.cpp:44672 | Remove boss |
| `AdminOrder_ClearNpc()` | Game.cpp:44701 | Clear all NPCs |
| `AdminOrder_CallGuard()` | Game.cpp:32906 | Spawn attacking guard |
| `AdminOrder_GetNpcStatus()` | Game.cpp:35773 | Get NPC info |
| `AdminOrder_SetAttackMode()` | Game.cpp:39268 | Set NPC mode |
| `AdminOrder_DisconnectAll()` | Game.cpp:39733 | Disconnect faction |
| `AdminOrder_ReserveFightzone()` | Game.cpp:33257 | Reserve arena |
| `AdminOrder_CreateFish()` | Game.cpp:33617 | Create fish node |
| `AdminOrder_GetFightzoneTicket()` | Game.cpp:54401 | Get arena ticket |
| `AdminOrder_Weather()` | Game.cpp:43480 | Change weather |
| `AdminOrder_Time()` | Game.cpp:48079 | Set game time |
| `AdminOrder_CheckRep()` | Game.cpp:48132 | Check reputation |
| `AdminOrder_Pushplayer()` | Game.cpp:48192 | Force teleport |
| `AdminOrder_CheckStats()` | Game.cpp:48751 | Check stats |
| `AdminOrder_MonsterCount()` | Game.cpp:44601 | Count monsters |
| `AdminOrder_SetForceRecallTime()` | Game.cpp:44618 | Set recall time |
| `ShutUpPlayer()` | Game.cpp:~32550 | Mute player |
| `SetPlayerReputation()` | Game.cpp:32652 | Modify reputation (player command) |

### Support Functions

| Function | Description |
|----------|-------------|
| `bReadAdminListConfigFile()` | Load AdminList.cfg |
| `bReadBannedListConfigFile()` | Load BannedList.cfg |
| `bSendMsgToLS()` | Send message to log server |
| `PutAdminLogFileList()` | Write to admin log file |
| `PutHackLogFileList()` | Write to hack log file |
| `SendNotifyMsg()` | Send notification to client |

---

## Notes for Modernization

1. **Replace integer levels with role-based permissions** - Use enum/flags for clearer permission management
2. **Hash security codes** - Don't store plaintext security numbers
3. **Add rate limiting** - Prevent command spam/abuse
4. **Implement command aliasing** - Allow configurable command names
5. **Add undo capability** - For reversible actions like teleports
6. **Enhance logging** - Add structured logging with correlation IDs
7. **Add confirmation prompts** - For dangerous operations
8. **Implement ban duration** - Currently bans are permanent
9. **Add IP range banning** - Support CIDR notation
10. **Separate admin from game client** - Consider dedicated admin interface
