# Legacy Guild System Documentation

## Table of Contents

1. [Overview](#overview)
2. [Constants and Limits](#constants-and-limits)
3. [Data Structures](#data-structures)
   - [CGuildsMan Class](#cguildsman-class)
   - [CClient Guild Fields](#cclient-guild-fields)
   - [CNpc Guild Fields](#cnpc-guild-fields)
   - [Guild Teleport Location (CTeleportLoc)](#guild-teleport-location-cteleportloc)
4. [Guild Creation](#guild-creation)
5. [Guild Dissolution](#guild-dissolution)
6. [Member Management](#member-management)
   - [Joining a Guild](#joining-a-guild)
   - [Leaving a Guild](#leaving-a-guild)
   - [Banning Guild Members](#banning-guild-members)
7. [Rank System](#rank-system)
8. [Guild Teleportation](#guild-teleportation)
9. [Guild Construction System](#guild-construction-system)
10. [Guild War and Combat](#guild-war-and-combat)
11. [Cross-Server Communication](#cross-server-communication)
12. [Network Protocol Messages](#network-protocol-messages)
13. [All Related Functions](#all-related-functions)
14. [Persistence](#persistence)

---

## Overview

The Helbreath Guild System allows players to form organizations with hierarchical rank structures. Guilds are primarily used during Crusade (war) events, where guild members can coordinate teleportation points, construct war units, and accumulate construction points.

**Key Characteristics:**
- Maximum 1000 guilds per server
- Maximum 128 members per guild
- 12-tier rank system (0 = Guild Master, 12 = Lowest, -1 = No Guild)
- Guild-based teleportation during Crusade mode
- Construction point system for war unit building
- Cross-server guild synchronization via Gate Server

---

## Constants and Limits

### From `Game.h`

```cpp
#define DEF_GUILDSTARTRANK      12      // Starting rank for new guild members
#define DEF_MAXGUILDS           1000    // Maximum guilds that can exist simultaneously
```

### From `Client.h`

```cpp
#define DEF_MAXGUILDSMAN        128     // Maximum members per guild
```

### From `NetMessages.h`

```cpp
// Guild-related message identifiers
#define MSGID_REQUEST_CREATENEWGUILD              0x0FC94208
#define MSGID_RESPONSE_CREATENEWGUILD             0x0FC94209
#define MSGID_REQUEST_DISBANDGUILD                0x0FC9420A
#define MSGID_RESPONSE_DISBANDGUILD               0x0FC9420B
#define MSGID_REQUEST_UPDATEGUILDINFO_NEWGUILDSMAN    0x0FC9420C
#define MSGID_REQUEST_UPDATEGUILDINFO_DELGUILDSMAN    0x0FC9420D
#define MSGID_GUILDNOTIFY                         0x0DF30760
#define DEF_GUILDNOTIFY_NEWGUILDSMAN              0x1F00

// Gate Server messages
#define GSM_SETGUILDTELEPORTLOC                   0x09
#define GSM_SETGUILDCONSTRUCTLOC                  0x0B
#define GSM_REQUEST_SUMMONGUILD                   0x15
```

### Common Type Messages

```cpp
#define DEF_COMMONTYPE_JOINGUILDAPPROVE           0x0A06
#define DEF_COMMONTYPE_JOINGUILDREJECT            0x0A07
#define DEF_COMMONTYPE_DISMISSGUILDAPPROVE        0x0A08
#define DEF_COMMONTYPE_DISMISSGUILDREJECT         0x0A09
#define DEF_COMMONTYPE_BANGUILD                   0x0A26
#define DEF_COMMONTYPE_SETGUILDTELEPORTLOC        0x0A54
#define DEF_COMMONTYPE_GUILDTELEPORT              0x0A55
#define DEF_COMMONTYPE_SETGUILDCONSTRUCTLOC       0x0A57
#define DEF_COMMONTYPE_REQGUILDNAME               0x0A59
```

### Notify Messages

```cpp
#define DEF_NOTIFY_QUERY_JOINGUILDREQPERMISSION   0x0B02
#define DEF_NOTIFY_QUERY_DISMISSGUILDREQPERMISSION 0x0B03
#define DEF_NOTIFY_WAITFORGUILDOPERATION          0x0B04
#define DEF_NOTIFY_GUILDDISBANDED                 0x0B0B
#define DEF_NOTIFY_CANNOTJOINMOREGUILDSMAN        0x0B0D
#define DEF_NOTIFY_NEWGUILDSMAN                   0x0B0E
#define DEF_NOTIFY_DISMISSGUILDSMAN               0x0B0F
#define DEF_NOTIFY_NOGUILDMASTERLEVEL             0x0B77
#define DEF_NOTIFY_SUCCESSBANGUILDMAN             0x0B78
#define DEF_NOTIFY_CANNOTBANGUILDMAN              0x0B79
#define DEF_NOTIFY_REQGUILDNAMEANSWER             0x0BA6
```

---

## Data Structures

### CGuildsMan Class

**File:** `GuildsMan.h`, `GuildsMan.cpp`

The CGuildsMan class is a simple structure representing a guild member entry:

```cpp
class CGuildsMan
{
public:
    CGuildsMan();
    virtual ~CGuildsMan();

    char m_cName[11];   // Member's character name (10 chars + null terminator)
    int  m_iRank;       // Member's rank within the guild
};
```

**Constructor (GuildsMan.cpp):**
```cpp
CGuildsMan::CGuildsMan()
{
    ZeroMemory(m_cName, sizeof(m_cName));
}
```

**Note:** This class appears to be used for log server communication rather than in-game guild member tracking. The actual guild membership is tracked via the `m_cGuildName`, `m_iGuildRank`, and `m_iGuildGUID` fields in the CClient class.

### CClient Guild Fields

**File:** `Client.h`

Each client (player) has the following guild-related fields:

```cpp
class CClient {
    // ...
    char  m_cGuildName[21];     // Guild name (20 chars + null)
    char  m_cLocation[11];      // Player's town/location (determines guild allegiance)
    int   m_iGuildRank;         // Guild rank: 0=Master, 12=Lowest, -1=No guild
    int   m_iGuildGUID;         // Unique Guild ID (generated at creation time)
    // ...
};
```

**Guild Rank Values:**
| Value | Meaning |
|-------|---------|
| -1 | Not in a guild |
| 0 | Guild Master |
| 1-11 | Intermediate ranks (higher = lower authority) |
| 12 | Lowest rank (new member starting rank) |

### CNpc Guild Fields

**File:** `Npc.h`

NPCs (particularly summoned war units) can be associated with guilds:

```cpp
class CNpc {
    // ...
    int   m_iGuildGUID;  // Guild GUID if this NPC is a guild-summoned war unit
    // ...
};
```

War units summoned during Crusade mode store the summoning guild's GUID so they don't attack their own guild members.

### Guild Teleport Location (CTeleportLoc)

**File:** `TeleportLoc.h`

The CTeleportLoc class is repurposed to store guild teleport and construction locations:

```cpp
class CTeleportLoc
{
public:
    CTeleportLoc();
    virtual ~CTeleportLoc();

    short m_sSrcX, m_sSrcY;             // Source coordinates (not used for guild teleport)

    // Primary teleport location
    char  m_cDestMapName[11], m_cDir;   // Destination map name
    short m_sDestX, m_sDestY;           // Teleport destination coordinates

    // Secondary location (used for construction)
    char  m_cDestMapName2[11];          // Construction location map
    short m_sDestX2, m_sDestY2;         // Construction coordinates

    int   m_iV1, m_iV2;                 // V1 = Guild GUID, V2 = construction count
    DWORD m_dwTime, m_dwTime2;          // Last access timestamps
};
```

**In CGame:**
```cpp
class CTeleportLoc m_pGuildTeleportLoc[DEF_MAXGUILDS];  // Array of 1000 guild teleport entries
```

**Field Usage:**
| Field | Purpose |
|-------|---------|
| `m_iV1` | Guild GUID |
| `m_iV2` | Construction counter |
| `m_sDestX/Y` | Teleport destination |
| `m_cDestMapName` | Teleport map |
| `m_sDestX2/Y2` | Construction location |
| `m_cDestMapName2` | Construction map |
| `m_dwTime` | Teleport timestamp |
| `m_dwTime2` | Construction timestamp |

---

## Guild Creation

### Requirements

To create a guild, a player must meet these conditions:
1. **Level requirement:** Level >= 20
2. **Charisma requirement:** Charisma >= 20
3. **Citizenship:** Must belong to a town (location != "NONE")
4. **Location match:** Must be in their assigned town
5. **Guild status:** Must not already be in a guild (m_iGuildRank == -1)
6. **Game mode:** Cannot create guild during Crusade mode

### Process Flow

```
Player -> RequestCreateNewGuildHandler() -> Log Server (MSGID_REQUEST_CREATENEWGUILD)
                                                    |
                                                    v
Player <- ResponseCreateNewGuildHandler() <- Log Server (response)
```

### Function: `RequestCreateNewGuildHandler`

**Location:** Game.cpp, Line 12801

```cpp
void CGame::RequestCreateNewGuildHandler(int iClientH, char * pData, DWORD dwMsgSize)
```

**Logic:**
1. Check if Crusade mode is active (reject if true)
2. Skip to guild name in packet data
3. Verify player is not already in a guild
4. Validate level, charisma, and location requirements
5. If valid:
   - Store guild name temporarily in `m_cGuildName`
   - Store location in `m_cLocation`
   - Generate Guild GUID: `(Year + Month + Day + Hour + Minute + timeGetTime())`
   - Forward request to Log Server

**Guild GUID Generation:**
```cpp
SYSTEMTIME SysTime;
GetLocalTime(&SysTime);
m_pClientList[iClientH]->m_iGuildGUID = (int)(SysTime.wYear + SysTime.wMonth +
    SysTime.wDay + SysTime.wHour + SysTime.wMinute + timeGetTime());
```

### Function: `ResponseCreateNewGuildHandler`

**Location:** Game.cpp, Line 12733

```cpp
void CGame::ResponseCreateNewGuildHandler(char * pData, DWORD dwMsgSize)
```

**Logic:**
1. Find client by character name
2. If Log Server confirms (`DEF_LOGRESMSGTYPE_CONFIRM`):
   - Set `m_iGuildRank = 0` (Guild Master)
   - Guild name was already stored during request
3. If Log Server rejects (`DEF_LOGRESMSGTYPE_REJECT`):
   - Reset `m_cGuildName` to "NONE"
   - Set `m_iGuildRank = -1`
   - Set `m_iGuildGUID = -1`
4. Send response to client

---

## Guild Dissolution

### Requirements

- Player must be Guild Master (`m_iGuildRank == 0`)
- Guild name must match
- Cannot disband during Crusade mode

### Process Flow

```
Guild Master -> RequestDisbandGuildHandler() -> Log Server (MSGID_REQUEST_DISBANDGUILD)
                                                        |
                                                        v
All Guild Members <- ResponseDisbandGuildHandler() <- Log Server (response)
```

### Function: `RequestDisbandGuildHandler`

**Location:** Game.cpp, Line 12870

```cpp
void CGame::RequestDisbandGuildHandler(int iClientH, char * pData, DWORD dwMsgSize)
```

**Logic:**
1. Check if Crusade mode (reject if true)
2. Verify caller is Guild Master and guild names match
3. Forward disband request to Log Server

### Function: `ResponseDisbandGuildHandler`

**Location:** Game.cpp, Line 12897

```cpp
void CGame::ResponseDisbandGuildHandler(char * pData, DWORD dwMsgSize)
```

**Logic:**
1. Find the Guild Master by character name
2. If confirmed:
   - Notify all guild members via `SendGuildMsg(DEF_NOTIFY_GUILDDISBANDED)`
   - Clear guild data for the Guild Master
3. Send response to the Guild Master

### Function: `SendGuildMsg` (for dissolution notification)

**Location:** Game.cpp, Line 16638

When `DEF_NOTIFY_GUILDDISBANDED` is sent:
```cpp
case DEF_NOTIFY_GUILDDISBANDED:
    if (i == iClientH) break;  // Skip Guild Master (already cleared)
    memcpy(cp, m_pClientList[iClientH]->m_cGuildName, 20);
    // ... send message ...
    // Clear member's guild data
    ZeroMemory(m_pClientList[i]->m_cGuildName, sizeof(m_pClientList[i]->m_cGuildName));
    strcpy(m_pClientList[i]->m_cGuildName, "NONE");
    m_pClientList[i]->m_iGuildRank = -1;
    m_pClientList[i]->m_iGuildGUID = -1;
    break;
```

---

## Member Management

### Joining a Guild

Guild joining uses special items (item ID 88 - Guild Join Request item) given to the Guild Master.

#### Process Flow

```
Non-guild Player gives Item 88 to Guild Master
         |
         v
SendNotifyMsg(DEF_NOTIFY_QUERY_JOINGUILDREQPERMISSION) -> Guild Master
         |
         v
Guild Master approves/rejects
         |
         v
JoinGuildApproveHandler() or JoinGuildRejectHandler()
```

#### Requirements to Join

From Game.cpp Line 13513:
```cpp
if ((m_pClientList[iClientH]->m_iGuildRank == -1) &&  // Not in a guild
    (memcmp(m_pClientList[iClientH]->m_cLocation, "NONE", 4) != 0) &&  // Has a town
    (memcmp(m_pClientList[iClientH]->m_cLocation, m_pClientList[sOwnerH]->m_cLocation, 10) == 0) &&  // Same town as GM
    (m_pClientList[sOwnerH]->m_iGuildRank == 0))  // Target is Guild Master
```

#### Function: `JoinGuildApproveHandler`

**Location:** Game.cpp, Line 15225

```cpp
void CGame::JoinGuildApproveHandler(int iClientH, char * pName)
```

**Logic:**
1. Find target player by name
2. Verify same location (town) requirement
3. Copy guild data to new member:
   - `m_cGuildName` from Guild Master
   - `m_iGuildGUID` from Guild Master
   - `m_cLocation` from Guild Master
   - Set `m_iGuildRank = DEF_GUILDSTARTRANK` (12)
4. Send `DEF_COMMONTYPE_JOINGUILDAPPROVE` to new member
5. Update visual appearance (guild insignia)
6. Notify all guild members via `SendGuildMsg(DEF_NOTIFY_NEWGUILDSMAN)`
7. Update Log Server with new member

### Leaving a Guild

Players can leave a guild by giving Item 89 (Resignation Letter) to:
1. The Guild Master (requires approval)
2. NPC "Kennedy" (automatic, costs 300 EXP)

#### Via Guild Master

Uses `DEF_NOTIFY_QUERY_DISMISSGUILDREQPERMISSION` and `DismissGuildApproveHandler()`

#### Via NPC Kennedy

**Location:** Game.cpp, Line 13681

```cpp
if (memcmp(m_pNpcList[sOwnerH]->m_cNpcName, "Kennedy", 7) == 0) {
    if ((m_pClientList[iClientH]->m_iGuildRank != 0) &&
        (m_pClientList[iClientH]->m_iGuildRank != -1)) {
        // Approve dismiss
        SendNotifyMsg(iClientH, iClientH, DEF_COMMONTYPE_DISMISSGUILDAPPROVE, ...);

        // Clear guild data
        ZeroMemory(m_pClientList[iClientH]->m_cGuildName, ...);
        memcpy(m_pClientList[iClientH]->m_cGuildName, "NONE", 4);
        m_pClientList[iClientH]->m_iGuildRank = -1;
        m_pClientList[iClientH]->m_iGuildGUID = -1;

        // Penalty
        m_pClientList[iClientH]->m_iExp -= 300;
        if (m_pClientList[iClientH]->m_iExp < 0)
            m_pClientList[iClientH]->m_iExp = 0;
    }
}
```

### Banning Guild Members

Guild Masters can forcefully remove members using the `/banguild` command.

#### Function: `UserCommand_BanGuildsman`

**Location:** Game.cpp, Line 33369

```cpp
void CGame::UserCommand_BanGuildsman(int iClientH, char * pData, DWORD dwMsgSize)
```

**Logic:**
1. Verify caller is Guild Master (`m_iGuildRank == 0`)
2. Parse target name from command
3. Find target player online
4. Verify target is in the same guild
5. Update Log Server to remove member
6. Notify guild with `DEF_NOTIFY_DISMISSGUILDSMAN`
7. Clear target's guild data
8. Send success notification to Guild Master

**Command Syntax:**
```
/banguild <playername>
```

---

## Rank System

### Rank Values

| Rank | Name | Permissions |
|------|------|-------------|
| -1 | No Guild | No guild membership |
| 0 | Guild Master | Full control, can disband, set teleport points, summon guild |
| 1-11 | Officer Ranks | Intermediate ranks (specific permissions undefined) |
| 12 | Initiate | New member starting rank (DEF_GUILDSTARTRANK) |

### Rank Checking

Guild rank is frequently checked in combat and interaction code:

```cpp
// Check if player is Guild Master
if (m_pClientList[iClientH]->m_iGuildRank == 0) { ... }

// Check if player is in a guild
if (m_pClientList[iClientH]->m_iGuildRank != -1) { ... }

// Check if player is NOT Guild Master but IS in a guild (for leaving)
if ((m_pClientList[iClientH]->m_iGuildRank != 0) &&
    (m_pClientList[iClientH]->m_iGuildRank != -1)) { ... }
```

### Rank Promotion/Demotion

The codebase does not contain explicit rank promotion/demotion functions. The only rank changes occur when:
- A new guild is created (creator gets rank 0)
- A player joins a guild (gets rank 12)
- A player leaves a guild (gets rank -1)

---

## Guild Teleportation

Guild teleportation is a Crusade-mode feature allowing Guild Masters to set rally points.

### Setting Teleport Location

#### Function: `RequestSetGuildTeleportLocHandler`

**Location:** Game.cpp, Line 46048

```cpp
void CGame::RequestSetGuildTeleportLocHandler(int iClientH, int dX, int dY, int iGuildGUID, char * pMapName)
```

**Requirements:**
- Must be in Crusade mode
- Player must have Crusade Duty 3 (Commander/Guild Master)

**Logic:**
1. Validate Crusade mode and commander status
2. Clamp Y coordinate (100-600)
3. Create Gate Server message
4. Search `m_pGuildTeleportLoc` array for existing guild entry
5. If found: update coordinates and timestamp
6. If not found: allocate new slot or replace oldest entry
7. Broadcast to other servers via Gate Server

### Using Guild Teleport

#### Function: `RequestGuildTeleportHandler`

**Location:** Game.cpp, Line 45907

```cpp
void CGame::RequestGuildTeleportHandler(int iClientH)
```

**Requirements:**
- Must be in Crusade mode
- Player must have a Crusade Duty assigned
- Player must be in a guild
- Must not be on locked map or in Middleland

**Logic:**
1. Validate Crusade mode and guild membership
2. Search `m_pGuildTeleportLoc` for matching guild GUID
3. If found: call `RequestTeleportHandler()` with stored coordinates
4. If not found: use default faction teleport location

### Cross-Server Teleport Sync

#### Function: `GSM_SetGuildTeleportLoc`

**Location:** Game.cpp, Line 45977

```cpp
void CGame::GSM_SetGuildTeleportLoc(int iGuildGUID, int dX, int dY, char * pMapName)
```

Receives teleport location updates from other game servers via Gate Server.

---

## Guild Construction System

During Crusade mode, guilds can accumulate construction points to build war units.

### Setting Construction Location

#### Function: `RequestSetGuildConstructLocHandler`

**Location:** Game.cpp, Line 41470

```cpp
void CGame::RequestSetGuildConstructLocHandler(int iClientH, int dX, int dY, int iGuildGUID, char * pMapName)
```

Uses the secondary fields in `CTeleportLoc`:
- `m_sDestX2`, `m_sDestY2` - Construction coordinates
- `m_cDestMapName2` - Construction map name
- `m_dwTime2` - Construction timestamp

### Construction Points

#### Function: `CheckCommanderConstructionPoint`

**Location:** Game.cpp, Line 42880

```cpp
void CGame::CheckCommanderConstructionPoint(int iClientH)
```

Transfers construction points from Fighters (Duty 1) and Constructors (Duty 2) to the Guild Master Commander (Duty 3).

**Logic:**
1. If player is Fighter or Constructor:
   - Find Guild Master Commander on same server
   - Add construction points to Commander
   - Add 1/10 of points as war contribution
2. If Commander is on different server:
   - Send points via Gate Server

#### Function: `GSM_ConstructionPoint`

**Location:** Game.cpp, Line 42932

Receives construction points from other servers for the guild's Commander.

---

## Guild War and Combat

### Guild-Based Combat Checks

#### Same Guild Check

Players in the same guild cannot damage each other during certain attacks:

```cpp
// From attack calculations
if (m_pClientList[sAttackerH]->m_iGuildGUID == m_pClientList[sTargetH]->m_iGuildGUID) {
    return 0;  // No damage
}
```

**Location:** Game.cpp, Line 51996

#### Guild Experience Sharing

#### Function: `CalculateGuildEffect`

**Location:** Game.cpp, Line 16516

```cpp
void CGame::CalculateGuildEffect(int iVictimH, char cVictimType, short sAttackerH)
```

**Note:** This function is disabled (`return;` at the start). When active, it would:
- Share experience with nearby guild members during NPC kills
- Only grant XP to lower-level guild members
- Use 1D3 roll where 2 = 1/3 of monster XP shared

### War Unit Guild Association

NPCs spawned as war units store the summoning guild's GUID:

```cpp
// In bCreateNewNpc()
m_pNpcList[i]->m_iGuildGUID = iGuildGUID;

// War units don't attack their guild members
if ((m_pNpcList[i]->m_iGuildGUID != NULL) && (cTargetType == DEF_OWNERTYPE_PLAYER) &&
    (m_pClientList[sTargetH]->m_iGuildGUID == m_pNpcList[i]->m_iGuildGUID)) {
    // Skip attack
}
```

---

## Cross-Server Communication

### Gate Server Messages

Guild operations are synchronized across multiple game servers via the Gate Server.

| Message ID | Purpose |
|------------|---------|
| `GSM_SETGUILDTELEPORTLOC` (0x09) | Set/update guild teleport location |
| `GSM_SETGUILDCONSTRUCTLOC` (0x0B) | Set/update guild construction location |
| `GSM_CONSTRUCTIONPOINT` | Transfer construction points to Commander |
| `GSM_REQUEST_SUMMONGUILD` (0x15) | Summon guild members (admin command) |

### Function: `SendGuildMsg`

**Location:** Game.cpp, Line 16638

```cpp
void CGame::SendGuildMsg(int iClientH, WORD wNotifyMsgType, short sV1, short sV2, char * pString)
```

Broadcasts messages to all online guild members on the current server.

**Supported message types:**
- `DEF_NOTIFY_GUILDDISBANDED` - Guild has been disbanded
- `DEF_NOTIFY_EVENTMSGSTRING` - Event message string
- `DEF_NOTIFY_NEWGUILDSMAN` - New member joined
- `DEF_NOTIFY_DISMISSGUILDSMAN` - Member removed/left

### Function: `GuildNotifyHandler`

**Location:** Game.cpp, Line 16716

```cpp
void CGame::GuildNotifyHandler(char * pData, DWORD dwMsgSize)
```

Handles guild events received from other game servers. Currently a stub (not fully implemented).

---

## Network Protocol Messages

### Client to Server Messages

| Message | Description |
|---------|-------------|
| `MSGID_REQUEST_CREATENEWGUILD` | Request to create new guild |
| `MSGID_REQUEST_DISBANDGUILD` | Request to disband guild |
| `DEF_COMMONTYPE_JOINGUILDAPPROVE` | Approve guild join request |
| `DEF_COMMONTYPE_JOINGUILDREJECT` | Reject guild join request |
| `DEF_COMMONTYPE_DISMISSGUILDAPPROVE` | Approve member dismissal |
| `DEF_COMMONTYPE_DISMISSGUILDREJECT` | Reject member dismissal |
| `DEF_COMMONTYPE_BANGUILD` | Ban guild member |
| `DEF_COMMONTYPE_SETGUILDTELEPORTLOC` | Set guild teleport point |
| `DEF_COMMONTYPE_GUILDTELEPORT` | Use guild teleport |
| `DEF_COMMONTYPE_SETGUILDCONSTRUCTLOC` | Set construction location |
| `DEF_COMMONTYPE_REQGUILDNAME` | Request player's guild name |

### Server to Client Messages

| Message | Description |
|---------|-------------|
| `MSGID_RESPONSE_CREATENEWGUILD` | Guild creation result |
| `MSGID_RESPONSE_DISBANDGUILD` | Guild disbandment result |
| `DEF_NOTIFY_QUERY_JOINGUILDREQPERMISSION` | Prompt for join approval |
| `DEF_NOTIFY_QUERY_DISMISSGUILDREQPERMISSION` | Prompt for dismissal approval |
| `DEF_NOTIFY_WAITFORGUILDOPERATION` | Guild operation in progress |
| `DEF_NOTIFY_GUILDDISBANDED` | Guild has been disbanded |
| `DEF_NOTIFY_CANNOTJOINMOREGUILDSMAN` | Guild is full |
| `DEF_NOTIFY_NEWGUILDSMAN` | New member joined |
| `DEF_NOTIFY_DISMISSGUILDSMAN` | Member was dismissed |
| `DEF_NOTIFY_NOGUILDMASTERLEVEL` | Not Guild Master error |
| `DEF_NOTIFY_SUCCESSBANGUILDMAN` | Ban successful |
| `DEF_NOTIFY_CANNOTBANGUILDMAN` | Cannot ban (not in guild) |
| `DEF_NOTIFY_REQGUILDNAMEANSWER` | Response with guild name |

---

## All Related Functions

### Guild Creation/Dissolution

| Function | Location | Purpose |
|----------|----------|---------|
| `RequestCreateNewGuildHandler` | Game.cpp:12801 | Process guild creation request |
| `ResponseCreateNewGuildHandler` | Game.cpp:12733 | Handle Log Server response for creation |
| `RequestDisbandGuildHandler` | Game.cpp:12870 | Process guild dissolution request |
| `ResponseDisbandGuildHandler` | Game.cpp:12897 | Handle Log Server response for dissolution |

### Member Management

| Function | Location | Purpose |
|----------|----------|---------|
| `JoinGuildApproveHandler` | Game.cpp:15225 | Process join approval |
| `JoinGuildRejectHandler` | Game.cpp:15271 | Process join rejection |
| `DismissGuildApproveHandler` | Game.cpp:15292 | Process dismissal approval |
| `DismissGuildRejectHandler` | Game.cpp:15319 | Process dismissal rejection |
| `UserCommand_BanGuildsman` | Game.cpp:33369 | Handle /banguild command |
| `UserCommand_DissmissGuild` | Game.cpp:33433 | Handle /dismissguild (stub) |

### Guild Communication

| Function | Location | Purpose |
|----------|----------|---------|
| `SendGuildMsg` | Game.cpp:16638 | Broadcast message to guild members |
| `GuildNotifyHandler` | Game.cpp:16716 | Handle cross-server guild events |
| `RequestGuildNameHandler` | Game.cpp:43926 | Return player's guild info |

### Guild Teleportation

| Function | Location | Purpose |
|----------|----------|---------|
| `RequestGuildTeleportHandler` | Game.cpp:45907 | Use guild teleport |
| `RequestSetGuildTeleportLocHandler` | Game.cpp:46048 | Set guild teleport location |
| `GSM_SetGuildTeleportLoc` | Game.cpp:45977 | Cross-server teleport sync |

### Guild Construction

| Function | Location | Purpose |
|----------|----------|---------|
| `RequestSetGuildConstructLocHandler` | Game.cpp:41470 | Set construction location |
| `GSM_SetGuildConstructLoc` | Game.cpp:42810 | Cross-server construction sync |
| `CheckCommanderConstructionPoint` | Game.cpp:42880 | Transfer construction points |
| `GSM_ConstructionPoint` | Game.cpp:42932 | Receive construction points |

### Combat/Effects

| Function | Location | Purpose |
|----------|----------|---------|
| `CalculateGuildEffect` | Game.cpp:16516 | Guild experience sharing (disabled) |

### Admin Commands

| Function | Location | Purpose |
|----------|----------|---------|
| `AdminOrder_SummonGuild` | Game.cpp:48447 | Summon all guild members |

### NPC Interaction

| Function | Location | Purpose |
|----------|----------|---------|
| `_iTalkToNpcResult_GuildHall` | Game.cpp:38240 | Guild Hall NPC (stub) |

---

## Persistence

### Character File Format

Guild data is saved in character files with these keys:

```
character-guild-name = <guild_name>
character-guild-rank = <rank_number>
character-guild-GUID = <guild_guid>
```

### Loading Guild Data

**Location:** Game.cpp, Line 5831 (character loading)

```cpp
// Read mode 12: Guild Name
ZeroMemory(m_pClientList[iClientH]->m_cGuildName, sizeof(m_pClientList[iClientH]->m_cGuildName));
strcpy(m_pClientList[iClientH]->m_cGuildName, token);

// Read mode 13: Guild Rank
m_pClientList[iClientH]->m_iGuildRank = atoi(token);

// Read mode 48: Guild GUID
m_pClientList[iClientH]->m_iGuildGUID = atoi(token);
```

### Saving Guild Data

**Location:** Game.cpp, Line 7386

```cpp
strcat(pData, "character-guild-name = ");
if (m_pClientList[iClientH]->m_iGuildRank != -1) {
    strcat(pData, m_pClientList[iClientH]->m_cGuildName);
}
// ...
strcat(pData, "character-guild-GUID = ");
if (m_pClientList[iClientH]->m_iGuildRank != -1) {
    wsprintf(cTxt, "%d", m_pClientList[iClientH]->m_iGuildGUID);
    strcat(pData, cTxt);
}
// ...
strcat(pData, "character-guild-rank = ");
itoa(m_pClientList[iClientH]->m_iGuildRank, cTxt, 10);
strcat(pData, cTxt);
```

### Log Server Communication

Guild creation and disbandment involve the Log Server for persistent storage:
- `MSGID_REQUEST_CREATENEWGUILD` - Register new guild
- `MSGID_RESPONSE_CREATENEWGUILD` - Guild registration result
- `MSGID_REQUEST_DISBANDGUILD` - Delete guild record
- `MSGID_RESPONSE_DISBANDGUILD` - Deletion result
- `MSGID_REQUEST_UPDATEGUILDINFO_NEWGUILDSMAN` - Add member to guild record
- `MSGID_REQUEST_UPDATEGUILDINFO_DELGUILDSMAN` - Remove member from guild record

### Configuration

**GServer.cfg settings:**
```
summonguild-cost = <gold_amount>    // Cost for guild summoning (default: 0)
Admin-Level-/summonguild = <level>  // Admin level required for /summonguild
```

---

## Summary

The Guild System in Helbreath is primarily designed for:
1. **Social Organization** - Grouping players under a common banner
2. **Crusade War Support** - Coordinating teleportation and construction during wars
3. **Cross-Server Coordination** - Synchronizing guild operations across multiple servers

Key limitations:
- No rank promotion system implemented
- Guild experience sharing is disabled
- Maximum 128 members per guild
- Cannot create/disband guilds during Crusade mode

The system uses a simple flat structure where all meaningful permissions belong to the Guild Master (rank 0), with intermediate ranks (1-11) having no documented special abilities.
