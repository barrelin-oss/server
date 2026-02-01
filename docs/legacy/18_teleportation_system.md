# Legacy Teleportation System

**Document Version:** 1.0
**Applies To:** Legacy Helbreath Server (v2.03)
**Primary Files:** `TeleportLoc.cpp/h`, `Teleport.cpp/h`, `Game.cpp`, `Map.cpp/h`, `Tile.h`
**Estimated Lines:** ~800 lines across all files

---

## Table of Contents

1. [Overview](#overview)
2. [Teleportation Types](#teleportation-types)
3. [Data Structures](#data-structures)
4. [Core Functions](#core-functions)
5. [Tile-Based Teleportation](#tile-based-teleportation)
6. [Magic Recall Teleportation](#magic-recall-teleportation)
7. [NPC-Based Teleportation](#npc-based-teleportation)
8. [Guild Teleportation (Crusade)](#guild-teleportation-crusade)
9. [Heldenian Teleportation](#heldenian-teleportation)
10. [Administrative Teleportation](#administrative-teleportation)
11. [Map Configuration](#map-configuration)
12. [Server Transfer](#server-transfer)
13. [Restrictions and Validation](#restrictions-and-validation)
14. [Message Protocol](#message-protocol)
15. [Constants and Limits](#constants-and-limits)
16. [Related Systems](#related-systems)

---

## Overview

The teleportation system in Helbreath provides multiple ways for players to move between locations:

1. **Tile-Based Teleports** - Walking onto special tiles triggers automatic teleportation
2. **Magic Recall** - Casting recall spell returns player to home city
3. **NPC Teleports** - Talking to NPCs for paid teleportation services
4. **Guild Teleports** - Crusade war feature for guild-based rally points
5. **Heldenian Teleports** - Special war event teleportation
6. **Admin Teleports** - GM commands for instant relocation

The system handles both same-server teleports and cross-server transfers when the destination map exists on a different game server.

---

## Teleportation Types

### Type Codes in RequestTeleportHandler

The `RequestTeleportHandler` function uses a type code in `pData[0]`:

| Code | Type | Description |
|------|------|-------------|
| `'0'` | Forced Recall | Level restriction violation, returns to safe zone |
| `'1'` | Normal Recall | Magic-based recall to home city |
| `'2'` | Directed Teleport | Specific destination (map, x, y coordinates) |
| `'3'` | (Reserved) | Similar behavior to '1' in some checks |

### Teleport Trigger Sources

```cpp
// Message IDs that trigger teleportation
#define MSGID_REQUEST_TELEPORT           0x0EA03201  // General teleport request
#define MSGID_REQUEST_CITYHALLTELEPORT   0x0EA03202  // City hall dungeon teleport
#define MSGID_REQUEST_HELDENIANTELEPORT  0x0EA03206  // Heldenian war teleport

// Common type commands
#define DEF_COMMONTYPE_SETGUILDTELEPORTLOC  0x0A54  // Set guild teleport location
#define DEF_COMMONTYPE_GUILDTELEPORT        0x0A55  // Use guild teleport
```

---

## Data Structures

### CTeleportLoc (Tile-Based Teleport)

**File:** `TeleportLoc.h`

Defines a teleport location attached to a specific tile on a map.

```cpp
class CTeleportLoc
{
public:
    CTeleportLoc();
    virtual ~CTeleportLoc();

    // Source coordinates (tile that triggers teleport)
    short m_sSrcX, m_sSrcY;

    // Primary destination
    char  m_cDestMapName[11];   // Destination map name
    short m_sDestX, m_sDestY;   // Destination coordinates
    char  m_cDir;               // Direction player faces after teleport

    // Secondary destination (unused in most cases)
    char  m_cDestMapName2[11];
    short m_sDestX2, m_sDestY2;

    // Conditional values (purpose varies)
    int   m_iV1, m_iV2;
    DWORD m_dwTime, m_dwTime2;  // Timing/cooldown data
};
```

**Constructor Initialization:**
```cpp
CTeleportLoc::CTeleportLoc()
{
    ZeroMemory(m_cDestMapName, sizeof(m_cDestMapName));
    m_sSrcX   = -1;
    m_sSrcY   = -1;
    m_sDestX  = -1;
    m_sDestY  = -1;
    m_sDestX2 = -1;
    m_sDestY2 = -1;
    m_iV1     = NULL;
    m_iV2     = NULL;
    m_dwTime  = NULL;
    m_dwTime2 = NULL;
}
```

### CTeleport (NPC-Based Teleport)

**File:** `Teleport.h`

Defines NPC teleportation service with requirements and costs.

```cpp
class CTeleport
{
public:
    CTeleport();
    virtual ~CTeleport();

    // NPC Configuration
    char  m_cTeleportNum;           // Teleport service ID
    char  m_cTeleportNpcName[21];   // NPC that provides this service
    char  m_cSourceMap[10];         // Map where NPC is located

    // Destination
    char  m_cTargetMap[10];         // Target map name
    short m_sDestinationX;          // Target X coordinate
    short m_sDestinationY;          // Target Y coordinate

    // Requirements
    short m_sTeleportCost;          // Gold cost
    short m_sTeleportMinLevel;      // Minimum player level
    short m_sTeleportMaxLevel;      // Maximum player level

    // Faction restrictions
    char  m_cTeleportSide[7];       // "both", "elvine", or "aresden"

    // Player type restrictions
    BOOL m_bTeleportHunt;           // Allow hunters/combatants
    BOOL m_bTeleportNtrl;           // Allow neutrals
    BOOL m_bTeleportCrmnl;          // Allow criminals
};
```

### CTile Teleport Flag

**File:** `Tile.h`

Each tile has a teleport flag:

```cpp
class CTile
{
    // ... other members ...

    BOOL m_bIsTeleport;  // TRUE if stepping on this tile triggers teleport

    // ... other members ...
};
```

### Guild Teleport Storage

**File:** `Game.h`

Guild teleport locations for Crusade wars:

```cpp
class CGame {
    // Array of guild teleport locations (one per guild)
    class CTeleportLoc m_pGuildTeleportLoc[DEF_MAXGUILDS];  // 1000 guilds

    // ...
};
```

---

## Core Functions

### RequestTeleportHandler

**File:** `Game.cpp` (Line ~19975)
**Signature:** `void CGame::RequestTeleportHandler(int iClientH, char * pData, char * cMapName = NULL, int dX = -1, int dY = -1)`

Central teleportation handler that processes all teleport requests.

**Parameters:**
- `iClientH` - Client handle (player index)
- `pData` - Data buffer with teleport type code
- `cMapName` - Optional destination map name (for directed teleports)
- `dX, dY` - Optional destination coordinates (-1 = use initial point)

**Process Flow:**

```
1. Validate player state
   ├── Check if client exists
   ├── Check if initialization complete
   ├── Check if player is killed
   └── Check if waiting on another process

2. Check recall restrictions
   ├── Apocalypse mode restrictions
   ├── Force recall timer in enemy territory
   └── Neutral player restrictions

3. Cancel exchange mode if active

4. Remove player from current location
   ├── Clear targeting by NPCs
   ├── Clear tile ownership
   └── Notify nearby clients of departure

5. Determine destination
   ├── Check for tile-based teleport
   ├── Apply locked map restrictions (Crusade)
   └── Process by teleport type code

6. Execute teleport
   ├── Same-server: Update position and send map data
   └── Different-server: Save data and initiate server transfer
```

**Teleport Type Processing:**

```cpp
switch (pData[0]) {
case '0':  // Forced Recall
    // Player was in restricted area, return to safe zone
    // Uses m_cLocation to determine home city
    // Hunters go to "arefarm" or "elvfarm"
    break;

case '1':  // Normal Recall (magic spell)
    // Returns player to home city
    // Level > 80: Main city ("aresden" or "elvine")
    // Level <= 80: Farm area ("arefarm" or "elvfarm")
    break;

case '2':  // Directed Teleport
    // Uses provided cMapName and coordinates
    // If dX/dY = -1, uses map initial point
    break;
}
```

### bSearchTeleportDest

**File:** `Map.cpp` (Line ~560)
**Signature:** `BOOL CMap::bSearchTeleportDest(int sX, int sY, char * pMapName, int * pDx, int * pDy, char * pDir)`

Searches for teleport destination when player stands on a teleport tile.

```cpp
BOOL CMap::bSearchTeleportDest(int sX, int sY, char * pMapName,
                                int * pDx, int * pDy, char * pDir)
{
    for (i = 0; i < DEF_MAXTELEPORTLOC; i++) {
        if ((m_pTeleportLoc[i] != NULL) &&
            (m_pTeleportLoc[i]->m_sSrcX == sX) &&
            (m_pTeleportLoc[i]->m_sSrcY == sY)) {

            memcpy(pMapName, m_pTeleportLoc[i]->m_cDestMapName, 10);
            *pDx  = m_pTeleportLoc[i]->m_sDestX;
            *pDy  = m_pTeleportLoc[i]->m_sDestY;
            *pDir = m_pTeleportLoc[i]->m_cDir;
            return TRUE;
        }
    }
    return FALSE;
}
```

### bGetIsTeleport

**File:** `Map.cpp` (Line ~315)
**Signature:** `BOOL CMap::bGetIsTeleport(short dX, short dY)`

Checks if a tile is a teleport tile.

```cpp
BOOL CMap::bGetIsTeleport(short dX, short dY)
{
    CTile * pTile = m_pTile[dX][dY];
    if (pTile == NULL) return FALSE;
    if (pTile->m_bIsTeleport == FALSE) return FALSE;
    return TRUE;
}
```

---

## Tile-Based Teleportation

### How It Works

1. Player moves to a tile position
2. Server checks `bGetIsTeleport()` for the tile
3. If TRUE, `bSearchTeleportDest()` finds destination
4. `RequestTeleportHandler()` executes the teleport

### Tile Flag Setting

When map data is loaded, tiles are marked:

```cpp
// In map loading code (Map.cpp:534)
if (/* tile has teleport attribute */)
    pTile->m_bIsTeleport = TRUE;
else
    pTile->m_bIsTeleport = FALSE;
```

### Empty Position Finding

After teleport, server finds valid spawn position:

```cpp
// Game.cpp:9963 - Finding empty position near teleport
if ((m_pMapList[cMapIndex]->bGetIsTeleport(*pX + _tmp_cEmptyPosX[i],
                                            *pY + _tmp_cEmptyPosY[i]) == FALSE)) {
    // Valid position found
}
```

---

## Magic Recall Teleportation

### Trigger

Magic type `DEF_MAGICTYPE_TELEPORT` (value: 8) triggers recall.

**File:** `Magic.h`
```cpp
#define DEF_MAGICTYPE_TELEPORT  8
```

### Handling in Magic System

```cpp
// Game.cpp:18669
case DEF_MAGICTYPE_TELEPORT:
    // Process teleport magic effect
    RequestTeleportHandler(iClientH, "1   ");  // Normal recall
    break;
```

### Level-Based Destinations

```cpp
// Game.cpp:20169-20178
if (m_pClientList[iClientH]->m_iLevel > 80) {
    // High level players go to main city
    if (memcmp(m_pClientList[iClientH]->m_cLocation, "are", 3) == 0)
        strcpy(cTempMapName, "aresden");
    else
        strcpy(cTempMapName, "elvine");
} else {
    // Lower level players go to farm area
    if (memcmp(m_pClientList[iClientH]->m_cLocation, "are", 3) == 0)
        strcpy(cTempMapName, "arefarm");
    else
        strcpy(cTempMapName, "elvfarm");
}
```

### Recall Restrictions

**Maps with recall disabled:**
```cpp
// Game.cpp:19990
if (m_pMapList[m_pClientList[iClientH]->m_cMapIndex]->m_bIsRecallImpossible == TRUE)
    // Cannot use recall magic
```

**Neutral players cannot recall:**
```cpp
// Game.cpp:20022
if ((memcmp(m_pClientList[iClientH]->m_cLocation, "NONE", 4) == 0) && (pData[0] == '1'))
    return;  // Neutrals blocked from recall
```

---

## NPC-Based Teleportation

### CTeleport Configuration

NPCs can offer teleportation services with:
- Gold cost
- Level requirements
- Faction restrictions
- Player type restrictions (hunter/neutral/criminal)

### Validation Checks

1. **Gold Check** - Player must have enough gold
2. **Level Check** - Player level within min/max range
3. **Side Check** - Player faction matches allowed factions
4. **Type Check** - Player combat status allowed

### Example NPC Teleport Flow

```
Player talks to teleport NPC
    ↓
Server validates requirements
    ↓
Deduct gold from player
    ↓
Call RequestTeleportHandler("2   ", targetMap, x, y)
    ↓
Player teleported to destination
```

---

## Guild Teleportation (Crusade)

### Overview

During Crusade wars, guilds can set rally points that guild members can teleport to. This is stored in `m_pGuildTeleportLoc[]` array.

### Setting Guild Teleport Location

**Function:** `RequestSetGuildTeleportLocHandler()`
**File:** `Game.cpp` (Line ~46048)

**Requirements:**
- Crusade mode must be active
- Player must be a guild master
- Player must be in middleland

```cpp
void CGame::RequestSetGuildTeleportLocHandler(int iClientH, int dX, int dY,
                                               int iGuildGUID, char * pMapName)
{
    // Validate crusade mode
    if (!m_bIsCrusadeMode) {
        // Log hack attempt
        PutHackLogFileList(G_cTxt);
        DeleteClient(iClientH, TRUE, TRUE);
        return;
    }

    // Validate guild master
    if (m_pClientList[iClientH]->m_iGuildRank != 0) {
        // Not a guild master
        return;
    }

    // Store teleport location
    // Find existing or create new entry for this guild
    for (i = 0; i < DEF_MAXGUILDS; i++) {
        if (m_pGuildTeleportLoc[i].m_iV1 == iGuildGUID) {
            // Update existing
            m_pGuildTeleportLoc[i].m_sDestX = dX;
            m_pGuildTeleportLoc[i].m_sDestY = dY;
            strcpy(m_pGuildTeleportLoc[i].m_cDestMapName, pMapName);
            m_pGuildTeleportLoc[i].m_dwTime = dwTime;
            return;
        }
    }

    // Create new entry...
}
```

### Using Guild Teleport

**Function:** `RequestGuildTeleportHandler()`
**File:** `Game.cpp` (Line ~45907)

```cpp
void CGame::RequestGuildTeleportHandler(int iClientH)
{
    // Validate crusade mode
    if (!m_bIsCrusadeMode) {
        // Log hack attempt - teleporting without crusade
        PutHackLogFileList(G_cTxt);
        DeleteClient(iClientH, TRUE, TRUE);
        return;
    }

    // Validate player has crusade duty
    if (m_pClientList[iClientH]->m_iCrusadeDuty == 0) {
        // Log hack attempt - not in a guild
        return;
    }

    // Cannot teleport from middleland
    if (m_pClientList[iClientH]->m_cMapIndex == m_iMiddlelandMapIndex)
        return;

    // Find guild's teleport location
    for (i = 0; i < DEF_MAXGUILDS; i++) {
        if (m_pGuildTeleportLoc[i].m_iV1 == m_pClientList[iClientH]->m_iGuildGUID) {
            strcpy(cMapName, m_pGuildTeleportLoc[i].m_cDestMapName);
            RequestTeleportHandler(iClientH, "2   ", cMapName,
                                   m_pGuildTeleportLoc[i].m_sDestX,
                                   m_pGuildTeleportLoc[i].m_sDestY);
            return;
        }
    }
}
```

### Cross-Server Synchronization

Guild teleport locations sync across servers:

```cpp
// Game.cpp:42282 - Message from other game server
case GSM_SETGUILDTELEPORTLOC:
    GSM_SetGuildTeleportLoc(iV1, iV2, iV3, cTemp);
    break;

// GSM message constant
#define GSM_SETGUILDTELEPORTLOC  0x09
```

---

## Heldenian Teleportation

### Overview

Heldenian is a special war event with dedicated teleportation to battlefields.

### Heldenian Teleport Handler

**Function:** `RequestHeldenianTeleport()`
**File:** `Game.cpp` (Line ~53959)

```cpp
void CGame::RequestHeldenianTeleport(int iClientH, char * pData, DWORD dwMsgSize)
{
    // Check if Heldenian mode is active
    if ((m_bIsHeldenianMode == 1) &&
        (m_pClientList[iClientH]->m_bIsPlayerCivil != TRUE) &&
        (m_pClientList[iClientH]->m_cSide == 2 || m_pClientList[iClientH]->m_cSide == 1)) {

        if (m_cHeldenianType == 1) {
            // Type 1: BtField map
            memcpy(cMapName, "BtField", 10);
            if (m_pClientList[iClientH]->m_cSide == 1) {  // Aresden
                tX = 68; tY = 225; cLoc = 1;
            } else {  // Elvine
                tX = 202; tY = 70; cLoc = 2;
            }
        }
        else if (m_cHeldenianType == 2) {
            // Type 2: HRampart map
            memcpy(cMapName, "HRampart", 10);
            if (m_pClientList[iClientH]->m_cSide == m_sLastHeldenianWinner) {
                // Winner side: defender position
                tX = 81; tY = 42; cLoc = 3;
            } else {
                // Loser side: attacker position
                tX = 156; tY = 153; cLoc = 4;
            }
        }
    }
}
```

### Heldenian Notification

When Heldenian mode starts, players receive notification:

```cpp
// Game.cpp:2106
if (m_bIsHeldenianMode == TRUE)
    SendNotifyMsg(NULL, iClientH, DEF_NOTIFY_HELDENIANTELEPORT, NULL, NULL, NULL, NULL, NULL);
```

---

## Administrative Teleportation

### GM Teleport Command

**Command:** `/teleport <mapname> [x] [y]` or `/tp <mapname> [x] [y]`
**Function:** `AdminOrder_Teleport()`
**File:** `Game.cpp` (Line ~33676)

```cpp
void CGame::AdminOrder_Teleport(int iClientH, char * pData, DWORD dwMsgSize)
{
    // Check admin level
    if (m_pClientList[iClientH]->m_iAdminUserLevel < m_iAdminLevelTeleport) {
        SendNotifyMsg(NULL, iClientH, DEF_NOTIFY_ADMINUSERLEVELLOW, NULL, NULL, NULL, NULL);
        return;
    }

    // Parse: /teleport mapname x y
    token = pStrTok->pGet();  // Skip command
    token = pStrTok->pGet();  // Map name
    strcpy(cMapName, token);

    token = pStrTok->pGet();  // X coordinate (optional)
    if (token != NULL) dX = atoi(token);

    token = pStrTok->pGet();  // Y coordinate (optional)
    if (token != NULL) dY = atoi(token);

    // Validate map name against whitelist
    if (bFlag == TRUE)
        RequestTeleportHandler(iClientH, "2   ", cMapName, dX, dY);
}
```

### Allowed Maps List

The admin teleport validates against a whitelist of ~70 maps:

```cpp
// Partial list from Game.cpp:33718-33806
if (strcmp("aresden", cMapName) == 0) bFlag = TRUE;
if (strcmp("elvine", cMapName) == 0)  bFlag = TRUE;
if (strcmp("middleland", cMapName) == 0) bFlag = TRUE;
if (strcmp("bisle", cMapName) == 0)   bFlag = TRUE;
if (strcmp("huntzone1", cMapName) == 0) bFlag = TRUE;
if (strcmp("fightzone1", cMapName) == 0) bFlag = TRUE;
if (strcmp("dglv2", cMapName) == 0) bFlag = TRUE;
// ... many more maps ...
if (strcmp("GMMap", cMapName) == 0) bFlag = TRUE;
```

### Admin Level Requirement

```cpp
// Game.h:970
int m_iAdminLevelTeleport;  // Minimum admin level for /teleport

// Configuration parsing (Game.cpp:47704)
// Admin-Level-/teleport = 2
m_iAdminLevelTeleport = atoi(token);  // Default: 2
```

---

## Map Configuration

### Teleport Location Definition

Teleport locations are defined in map configuration files:

**File Format:** `mapdata/<mapname>.txt`

```
teleport-loc = <srcX> <srcY> <destMap> <destX> <destY> <direction>
```

**Example:**
```
teleport-loc = 100 50 elvine 200 150 3
```

### Parsing Code

```cpp
// Game.cpp:23071-23072
if (memcmp(token, "teleport-loc", 12) == 0) {
    m_pMapList[iMapIndex]->m_pTeleportLoc[iTeleportLocIndex] = new class CTeleportLoc;
    // Continue parsing fields...
}

// Field parsing (Game.cpp:21454-21515)
// Field 1: Source X
m_pMapList[iMapIndex]->m_pTeleportLoc[iTeleportLocIndex]->m_sSrcX = atoi(token);
// Field 2: Source Y
m_pMapList[iMapIndex]->m_pTeleportLoc[iTeleportLocIndex]->m_sSrcY = atoi(token);
// Field 3: Destination map name
strcpy(m_pMapList[iMapIndex]->m_pTeleportLoc[iTeleportLocIndex]->m_cDestMapName, token);
// Field 4: Destination X
m_pMapList[iMapIndex]->m_pTeleportLoc[iTeleportLocIndex]->m_sDestX = atoi(token);
// Field 5: Destination Y
m_pMapList[iMapIndex]->m_pTeleportLoc[iTeleportLocIndex]->m_sDestY = atoi(token);
// Field 6: Direction (0-7)
m_pMapList[iMapIndex]->m_pTeleportLoc[iTeleportLocIndex]->m_cDir = atoi(token);
```

### Log Output

Successful map loading shows teleport count:

```cpp
// Game.cpp:23256
wsprintf(cTxt, "(!) Map info file decoding(%s) - success! TL(%d) WP(%d) ...",
         cFn, iTeleportLocIndex, iWayPointCfgIndex, ...);
// TL = Teleport Locations count
```

---

## Server Transfer

### Cross-Server Teleportation

When destination map is on a different server:

```cpp
// Game.cpp:20081-20099
// Map not found on current server
m_pClientList[iClientH]->m_sX = iDestX;
m_pClientList[iClientH]->m_sY = iDestY;
m_pClientList[iClientH]->m_cDir = cDir;
memcpy(m_pClientList[iClientH]->m_cMapName, cDestMapName, 10);

// Clear magic effects before transfer
SendNotifyMsg(NULL, iClientH, DEF_NOTIFY_MAGICEFFECTOFF, DEF_MAGICTYPE_CONFUSE, ...);
SetSlateFlag(iClientH, DEF_NOTIFY_SLATECLEAR, FALSE);

// Save player data and initiate server transfer
bSendMsgToLS(MSGID_REQUEST_SAVEPLAYERDATA_REPLY, iClientH, FALSE);

m_pClientList[iClientH]->m_bIsOnServerChange = TRUE;
m_pClientList[iClientH]->m_bIsOnWaitingProcess = TRUE;
```

### Process Flags

```cpp
m_bIsOnServerChange    // TRUE when transferring to another server
m_bIsOnWaitingProcess  // TRUE while waiting for transfer confirmation
```

---

## Restrictions and Validation

### Pre-Teleport Checks

```cpp
// Must pass all these checks to teleport:
if (m_pClientList[iClientH] == NULL) return;
if (m_pClientList[iClientH]->m_bIsInitComplete == FALSE) return;
if (m_pClientList[iClientH]->m_bIsKilled == TRUE) return;
if (m_pClientList[iClientH]->m_bIsOnWaitingProcess == TRUE) return;
```

### Map-Based Restrictions

**Recall Impossible Flag:**
```cpp
// Map.h:259
BOOL m_bIsRecallImpossible;  // TRUE = recall magic blocked on this map
```

**Locked Map (Crusade):**
```cpp
// Player locked to specific map during crusade
if ((strcmp(m_pClientList[iClientH]->m_cLockedMapName, "NONE") != 0) &&
    (m_pClientList[iClientH]->m_iLockedMapTime > 0)) {
    // Override destination to locked map
    strcpy(cDestMapName, m_pClientList[iClientH]->m_cLockedMapName);
    bIsLockedMapNotify = TRUE;
}
```

### Faction Territory Restrictions

```cpp
// Game.cpp:19996-20009
// Elvine player cannot recall from Aresden territory during non-crusade
if ((memcmp(m_pClientList[iClientH]->m_cLocation, "elvine", 6) == 0) &&
    (m_pClientList[iClientH]->m_iTimeLeft_ForceRecall > 0) &&
    (memcmp(m_pMapList[...], "aresden", 7) == 0) &&
    (m_bIsCrusadeMode == FALSE))
    return;

// Same for Aresden in Elvine territory
```

### Building Side Checks

```cpp
// Game.cpp:20052-20063
iMapSide = iGetMapLocationSide(cDestMapName);
if ((iMapSide != 0) && (m_pClientList[iClientH]->m_cSide == iMapSide)) {
    // Same-side building: allowed
} else {
    // Different-side building: redirect to locked map
    iDestX = -1;
    iDestY = -1;
    bIsLockedMapNotify = TRUE;
}
```

---

## Message Protocol

### Client to Server Messages

| Message ID | Purpose |
|------------|---------|
| `MSGID_REQUEST_TELEPORT` (0x0EA03201) | General teleport request |
| `MSGID_REQUEST_CITYHALLTELEPORT` (0x0EA03202) | City hall dungeon teleport |
| `MSGID_REQUEST_HELDENIANTELEPORT` (0x0EA03206) | Heldenian war teleport |
| `DEF_COMMONTYPE_GUILDTELEPORT` (0x0A55) | Guild teleport request |
| `DEF_COMMONTYPE_SETGUILDTELEPORTLOC` (0x0A54) | Set guild teleport point |

### Server to Client Messages

| Message ID | Purpose |
|------------|---------|
| `MSGID_RESPONSE_INITDATA` | Map data sent after teleport |
| `DEF_NOTIFY_HELDENIANTELEPORT` (0x0BE6) | Heldenian teleport available |
| `DEF_NOTIFY_LOCKEDMAP` | Player locked to specific map |
| `DEF_NOTIFY_NORECALL` | Cannot recall on this map |
| `DEF_NOTIFY_TCLOC` | Guild teleport location info |

### Post-Teleport Data Packet

After teleporting, server sends full map initialization:

```cpp
// Game.cpp:20285-20358
*dwp = MSGID_RESPONSE_INITDATA;
*wp  = DEF_MSGTYPE_CONFIRM;

// Player data:
*sp = iClientH;           // Player Object ID
*sp = m_pClientList[iClientH]->m_sX - 14 - 5;  // View X origin
*sp = m_pClientList[iClientH]->m_sY - 12 - 5;  // View Y origin
*sp = m_pClientList[iClientH]->m_sType;
*sp = m_pClientList[iClientH]->m_sAppr1;       // Appearance data
*sp = m_pClientList[iClientH]->m_sAppr2;
*sp = m_pClientList[iClientH]->m_sAppr3;
*sp = m_pClientList[iClientH]->m_sAppr4;
*ip = m_pClientList[iClientH]->m_iApprColor;
*ip = m_pClientList[iClientH]->m_iStatus;

// Map data:
memcpy(cp, m_pClientList[iClientH]->m_cMapName, 10);
memcpy(cp, m_pMapList[...]->m_cLocationName, 10);
*cp = m_cDayOrNight;       // Day/night mode
*cp = weather_status;      // Weather mode
```

---

## Constants and Limits

### Teleport Location Limits

```cpp
// Map.h:25
#define DEF_MAXTELEPORTLOC  200  // Max teleport locations per map
```

### Guild Limits

```cpp
// GlobalDef.h (assumed)
#define DEF_MAXGUILDS  1000  // Max guild teleport locations
```

### Direction Values

```cpp
// Post-teleport facing direction (0-7)
// 0 = North, 1 = NE, 2 = East, 3 = SE, 4 = South, 5 = SW, 6 = West, 7 = NW
```

### Configuration Variables

```cpp
// Game.h:970
int m_iAdminLevelTeleport;  // Admin level required for /teleport command

// Game.h:783
BOOL m_bIsTeleportAvailable;  // System-wide teleport enable/disable

// Game.h:1032
BOOL m_bIsHeldenianTeleport;  // Heldenian teleport availability
```

---

## Related Systems

### Exchange Mode Cancellation

Teleporting cancels any active item exchange:

```cpp
// Game.cpp:20014-20018
if (m_pClientList[iClientH]->m_bIsExchangeMode == TRUE) {
    iExH = m_pClientList[iClientH]->m_iExchangeH;
    _ClearExchangeStatus(iExH);
    _ClearExchangeStatus(iClientH);
}
```

### Target Removal

NPCs targeting the teleporting player are cleared:

```cpp
// Game.cpp:20026
RemoveFromTarget(iClientH, DEF_OWNERTYPE_PLAYER);
```

### Tile Ownership

Player is removed from current tile:

```cpp
// Game.cpp:20029-20031
m_pMapList[m_pClientList[iClientH]->m_cMapIndex]->ClearOwner(
    13, iClientH, DEF_OWNERTYPE_PLAYER,
    m_pClientList[iClientH]->m_sX,
    m_pClientList[iClientH]->m_sY);
```

### Client Notification

Nearby clients notified of departure:

```cpp
// Game.cpp:20034
SendEventToNearClient_TypeA(iClientH, DEF_OWNERTYPE_PLAYER,
                            MSGID_EVENT_LOG, DEF_MSGTYPE_REJECT, NULL, NULL, NULL);
```

### Magic Effect Clearing

Confusion and slate effects cleared on server transfer:

```cpp
// Game.cpp:20090-20092
SendNotifyMsg(NULL, iClientH, DEF_NOTIFY_MAGICEFFECTOFF,
              DEF_MAGICTYPE_CONFUSE, ...);
SetSlateFlag(iClientH, DEF_NOTIFY_SLATECLEAR, FALSE);
```

### Status Update

Player status recalculated after teleport:

```cpp
// Game.cpp:20274-20280
SetPlayingStatus(iClientH);
iTemp = m_pClientList[iClientH]->m_iStatus;
iTemp = 0x0FFFFFFF & iTemp;
iTemp2 = iGetPlayerABSStatus(iClientH);
iTemp = iTemp | (iTemp2 << 28);
m_pClientList[iClientH]->m_iStatus = iTemp;
```

---

## Common Teleport Destinations

### Special Locations Used in Code

| Map Name | Purpose |
|----------|---------|
| `aresden` | Aresden main city |
| `elvine` | Elvine main city |
| `arefarm` | Aresden farming area (low level recall) |
| `elvfarm` | Elvine farming area (low level recall) |
| `arejail` | Aresden jail (criminal teleport) |
| `elvjail` | Elvine jail (criminal teleport) |
| `middleland` | Crusade war zone |
| `BtField` | Heldenian battlefield |
| `HRampart` | Heldenian rampart map |
| `bisle` | Blood Isle |
| `resurr1`, `resurr2` | Resurrection zones |
| `dglv2`, `dglv3`, `dglv4` | Dungeon levels |
| `druncncity` | Drunken City |
| `default` | Default spawn map |

### Jail Teleports

Criminal players can be teleported to jail:

```cpp
// Game.cpp:24367 - Aresden criminal
RequestTeleportHandler(sAttackerH, "2   ", "arejail", -1, -1);

// Game.cpp:24386 - Elvine criminal
RequestTeleportHandler(sAttackerH, "2   ", "elvjail", -1, -1);
```

---

## Implementation Notes

### Memory Management

- `CTeleportLoc` objects created with `new` during map loading
- Destroyed in map destructor:
  ```cpp
  // Map.cpp:170-171
  for (i = 0; i < DEF_MAXTELEPORTLOC; i++)
      if (m_pTeleportLoc[i] != NULL) delete m_pTeleportLoc[i];
  ```

### Thread Safety

- No explicit mutex/locking on teleport operations
- Single-threaded message processing assumed
- Server change flags prevent concurrent operations

### Error Handling

- Return early on validation failures
- No exceptions used
- Hack attempts logged to file:
  ```cpp
  PutHackLogFileList(G_cTxt);
  DeleteClient(iClientH, TRUE, TRUE);
  ```

### Performance Considerations

- Linear search for teleport locations O(n) where n = DEF_MAXTELEPORTLOC
- Guild teleport search O(n) where n = DEF_MAXGUILDS
- Consider hash map for large deployments

---

## Summary

The legacy teleportation system provides comprehensive player movement functionality through:

1. **Tile triggers** - Automatic teleportation when stepping on marked tiles
2. **Magic recall** - Level-based destination selection for home city return
3. **NPC services** - Gold-based teleportation with requirements
4. **Crusade features** - Guild rally points and map locking
5. **War events** - Heldenian-specific battlefield teleportation
6. **Admin tools** - GM commands for instant relocation

The system handles both intra-server and inter-server teleportation with proper state management, effect clearing, and client notification.
