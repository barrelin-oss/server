# Legacy World & Map System Documentation

**Document Version:** 1.0
**System Complexity:** High
**Estimated Lines:** ~2,500+ across multiple files
**Primary Files:** `Map.cpp/h`, `Tile.cpp/h`, portions of `Game.cpp`

---

## Table of Contents

1. [Overview](#overview)
2. [Core Classes](#core-classes)
3. [Data Structures](#data-structures)
4. [Tile System](#tile-system)
5. [Map Configuration](#map-configuration)
6. [Teleportation System](#teleportation-system)
7. [Spawn System](#spawn-system)
8. [Sector System](#sector-system)
9. [Occupy Flags & War Integration](#occupy-flags--war-integration)
10. [Movement & Pathfinding](#movement--pathfinding)
11. [Map Data Format](#map-data-format)
12. [Configuration Files](#configuration-files)
13. [Key Functions Reference](#key-functions-reference)
14. [Constants Reference](#constants-reference)
15. [Modernization Notes](#modernization-notes)

---

## Overview

The World & Map System manages the game world's spatial representation, including:

- **Map loading and initialization** from binary `.amd` files and text configuration `.txt` files
- **Tile-based collision detection** and walkability
- **Entity placement** (players, NPCs, items) with ownership tracking
- **Teleportation** between maps and within maps
- **NPC/Mob spawning** via random generators and spot generators
- **Sector-based activity tracking** for AI and game events
- **War system integration** via occupy flags and strategic points
- **Dynamic objects** (dropped items, temporary effects)

### Architecture Summary

```
CGame (orchestrator)
    │
    ├── m_pMapList[DEF_MAXMAPS]  ─────────► CMap instances
    │                                           │
    │                                           ├── m_pTile[]  ─────────► CTile array
    │                                           ├── m_pTeleportLoc[]  ──► CTeleportLoc array
    │                                           ├── m_pOccupyFlag[]  ───► COccupyFlag array
    │                                           └── m_pStrategicPointList[] ► CStrategicPoint array
    │
    └── m_Misc  ──────────────────────────► CMisc (pathfinding utilities)
```

---

## Core Classes

### CMap (Map.h/cpp)

The primary class representing a single game map.

```cpp
class CMap {
public:
    // Initialization
    BOOL bInit(char * pName);

    // Tile access and ownership
    void SetOwner(short sOwner, char cOwnerClass, short sX, short sY);
    void GetOwner(short * pOwner, char * pOwnerClass, short sX, short sY);
    void ClearOwner(int iDebugCode, short sOwnerH, char cOwnerType, short sX, short sY);
    void SetDeadOwner(short sOwner, char cOwnerClass, short sX, short sY);
    void GetDeadOwner(short * pOwner, char * pOwnerClass, short sX, short sY);
    void ClearDeadOwner(short sX, short sY);
    void SetBigOwner(short sOwner, char cOwnerClass, short sX, short sY, char cArea);

    // Movement queries
    BOOL bGetMoveable(short dX, short dY, short * pDOtype = NULL, short * pTopItem = NULL);
    BOOL bGetIsMoveAllowedTile(short dX, short dY);
    BOOL bIsValidLoc(short sX, short sY);
    BOOL bCheckFlySpaceAvailable(short sX, char sY, char cDir, short sOwner);

    // Tile properties
    BOOL bGetIsTeleport(short dX, short dY);
    BOOL bGetIsWater(short dX, short dY);
    BOOL bGetIsFarm(short tX, short tY);
    int  iGetAttribute(int dX, int dY, int iBitMask);

    // Item management on tiles
    BOOL bSetItem(short sX, short sY, class CItem * pItem);
    class CItem * pGetItem(short sX, short sY, short * pRemainItemSprite,
                           short * pRemainItemSpriteFrame, char * pRemainItemColor);
    int  iCheckItem(short sX, short sY);

    // Dynamic objects
    void SetDynamicObject(WORD wID, short sType, short sX, short sY, DWORD dwRegisterTime);
    BOOL bGetDynamicObject(short sX, short sY, short * pType,
                           DWORD * pRegisterTime, int * pIndex = NULL);

    // Teleportation
    BOOL bSearchTeleportDest(int sX, int sY, char * pMapName,
                             int * pDx, int * pDy, char * pDir);

    // Naming system (for NPC unique identifiers)
    int  iGetEmptyNamingValue();
    void SetNamingValueEmpty(int iValue);

    // Occupy flags (war system)
    int  iRegisterOccupyFlag(int dX, int dY, int iSide, int iEKNum, int iDOI);

    // Sector info
    void ClearSectorInfo();
    void ClearTempSectorInfo();

    // Crusade structures
    BOOL bAddCrusadeStructureInfo(char cType, short sX, short sY, char cSide);
    BOOL bRemoveCrusadeStructureInfo(short sX, short sY);

    // Agriculture
    BOOL bAddCropsTotalSum();
    BOOL bRemoveCropsTotalSum();

    // Strike points (war)
    void RestoreStrikePoints();

    // Temporary movement flags
    void SetTempMoveAllowedFlag(int dX, int dY, BOOL bFlag);

    // Analysis
    int  iAnalyze(char cType, int * pX, int * pY, int * pV1, int * pV2, int * pV3);
    void _SetupNoAttackArea();

private:
    BOOL _bDecodeMapDataFileContents();
};
```

### CTile (Tile.h/cpp)

Represents a single tile in the map grid.

```cpp
class CTile {
public:
    CTile();
    virtual ~CTile();

    // Ownership (who occupies this tile)
    char  m_cOwnerClass;      // DEF_OWNERTYPE_PLAYER or DEF_OWNERTYPE_NPC
    short m_sOwner;           // Handle to player/NPC

    // Dead body tracking (for corpse looting)
    char  m_cDeadOwnerClass;
    short m_sDeadOwner;

    // Items on this tile (up to 12 stacked)
    class CItem * m_pItem[DEF_TILE_PER_ITEMS];  // DEF_TILE_PER_ITEMS = 12
    char  m_cTotalItem;

    // Dynamic object reference
    WORD  m_wDynamicObjectID;
    short m_sDynamicObjectType;
    DWORD m_dwDynamicObjectRegisterTime;

    // Tile flags
    BOOL  m_bIsMoveAllowed;       // Can entities walk here?
    BOOL  m_bIsTeleport;          // Is this a teleport tile?
    BOOL  m_bIsWater;             // Is this water?
    BOOL  m_bIsFarm;              // Is this farmable land?
    BOOL  m_bIsTempMoveAllowed;   // Temporary movement block (doors, etc.)

    // War system
    int   m_iOccupyStatus;        // Negative=Aresden, Positive=Elvine
    int   m_iOccupyFlagIndex;     // Index of occupy flag on this tile

    // Attribute bits for special behaviors
    int   m_iAttribute;
    // Bit 0: Unknown
    // Bit 1: Unknown
    // Bit 2: No-attack zone (safe area)
};
```

### CTeleportLoc (TeleportLoc.h/cpp)

Defines teleport source and destination pairs.

```cpp
class CTeleportLoc {
public:
    CTeleportLoc();
    virtual ~CTeleportLoc();

    // Source coordinates (on current map)
    short m_sSrcX, m_sSrcY;

    // Primary destination
    char  m_cDestMapName[11];
    short m_sDestX, m_sDestY;
    char  m_cDir;              // Direction facing after teleport (1-8)

    // Secondary destination (for two-way or alternate)
    char  m_cDestMapName2[11];
    short m_sDestX2, m_sDestY2;

    // Additional parameters
    int   m_iV1, m_iV2;        // Custom values
    DWORD m_dwTime, m_dwTime2; // Time-based restrictions
};
```

### COccupyFlag (OccupyFlag.h/cpp)

War system territory marker.

```cpp
class COccupyFlag {
public:
    COccupyFlag(int dX, int dY, char cSide, int iEKNum, int iDOI);
    virtual ~COccupyFlag();

    char m_cSide;               // 1=Aresden, 2=Elvine
    int  m_iEKCount;            // Enemy kill count for this flag
    int  m_sX, m_sY;            // Position

    int  m_iDynamicObjectIndex; // Reference to dynamic object
};
```

### CStrategicPoint (StrategicPoint.h/cpp)

Strategic locations for war calculations.

```cpp
class CStrategicPoint {
public:
    CStrategicPoint();
    virtual ~CStrategicPoint();

    int m_iSide;   // Faction ownership (0=neutral)
    int m_iValue;  // Strategic importance multiplier
    int m_iX, m_iY; // Position
};
```

---

## Data Structures

### CMap Member Variables

#### Basic Properties

```cpp
class CTile * m_pTile;           // Tile array [m_sSizeX * m_sSizeY]
class CGame * m_pGame;           // Back-reference to game
char  m_cName[11];               // Map internal name (e.g., "aresden")
char  m_cLocationName[11];       // Display name (e.g., "Aresden City")
short m_sSizeX, m_sSizeY;        // Map dimensions in tiles
short m_sTileDataSize;           // Bytes per tile in .amd file
```

#### Map Type and Mode

```cpp
char  m_cType;                   // DEF_MAPTYPE_NORMAL or DEF_MAPTYPE_NOPENALTY_NOREWARD
BOOL  m_bIsFixedDayMode;         // Always daytime (no night cycle)
BOOL  m_bIsFightZone;            // PvP arena map
BOOL  m_bIsAttackEnabled;        // Can attacks occur?
BOOL  m_bIsDisabled;             // Map disabled?
BOOL  m_bIsSnowEnabled;          // Snow weather instead of rain
BOOL  m_bIsRecallImpossible;     // Recall scroll blocked
BOOL  m_bIsApocalypseMap;        // Apocalypse event map
BOOL  m_bIsHeldenianMap;         // Heldenian war map
BOOL  m_bIsCitizenLimit;         // Faction-restricted entry
```

#### Level Restrictions

```cpp
int   m_iLevelLimit;             // Minimum level to enter
int   m_iUpperLevelLimit;        // Maximum level to enter
```

#### NPC and Object Tracking

```cpp
int   m_iTotalActiveObject;      // Currently active NPCs
int   m_iTotalAliveObject;       // Currently living NPCs
int   m_iMaximumObject;          // Maximum NPCs allowed on map
BOOL  m_bNamingValueUsingStatus[1000]; // NPC name slot availability
```

#### Teleportation

```cpp
class CTeleportLoc * m_pTeleportLoc[DEF_MAXTELEPORTLOC]; // 200 max
```

#### Initial Spawn Points

```cpp
POINT m_pInitialPoint[DEF_MAXINITIALPOINT];  // 20 spawn points
```

#### Waypoints (for NPC pathing)

```cpp
POINT m_WaypointList[DEF_MAXWAYPOINTCFG];    // 200 waypoints
```

#### Mob Generation

```cpp
BOOL  m_bRandomMobGenerator;           // Random mob spawning enabled
char  m_cRandomMobGeneratorLevel;      // Mob difficulty level

RECT  m_rcMobGenAvoidRect[DEF_MAXMGAR]; // 50 no-spawn rectangles

struct {
    BOOL bDefined;
    char cType;                // 1=RANDOMAREA, 2=RANDOMWAYPOINT
    char cWaypoint[10];        // Waypoint indices for type 2
    RECT rcRect;               // Spawn area for type 1
    int  iTotalActiveMob;
    int  iMobType;             // NPC type ID
    int  iMaxMobs;             // Maximum spawned at once
    int  iCurMobs;             // Currently spawned
} m_stSpotMobGenerator[DEF_MAXSPOTMOBGENERATOR]; // 100 generators
```

#### No-Attack Zones

```cpp
RECT  m_rcNoAttackRect[DEF_MAXNMR];    // 50 safe zones
```

#### Fishing Points

```cpp
POINT m_FishPointList[DEF_MAXFISHPOINT]; // 200 fishing spots
int   m_iTotalFishPoint;
int   m_iMaxFish;
int   m_iCurFish;
```

#### Mining Points

```cpp
BOOL  m_bMineralGenerator;
char  m_cMineralGeneratorLevel;
POINT m_MineralPointList[DEF_MAXMINERALPOINT]; // 200 mining spots
int   m_iTotalMineralPoint;
int   m_iMaxMineral;
int   m_iCurMineral;
```

#### Weather

```cpp
char  m_cWhetherStatus;          // 0=clear, 1-3=rain, 4-6=heavy, 7-9=storm
DWORD m_dwWhetherLastTime;       // Last weather change
DWORD m_dwWhetherStartTime;      // Weather started
```

#### War System

```cpp
class COccupyFlag * m_pOccupyFlag[DEF_MAXOCCUPYFLAG];  // 20,001 flags
int   m_iTotalOccupyFlags;

class CStrategicPoint * m_pStrategicPointList[DEF_MAXSTRATEGICPOINTS]; // 200

struct {
    char cType;
    char cSide;
    short sX, sY;
} m_stCrusadeStructureInfo[DEF_MAXCRUSADESTRUCTURES]; // 300
int m_iTotalCrusadeStructures;

struct {
    char  cRelatedMapName[11];
    int   iMapIndex;
    int   dX, dY;
    int   iHP, iInitHP;
    int   iEffectX[5];
    int   iEffectY[5];
} m_stStrikePoint[DEF_MAXSTRIKEPOINTS]; // 20
int m_iTotalStrikePoints;
```

#### Sector Activity Tracking

```cpp
struct {
    int iPlayerActivity;
    int iNeutralActivity;
    int iAresdenActivity;
    int iElvineActivity;
    int iMonsterActivity;
} m_stSectorInfo[DEF_MAXSECTORS][DEF_MAXSECTORS];      // 60x60
  m_stTempSectorInfo[DEF_MAXSECTORS][DEF_MAXSECTORS];

int m_iMaxNx, m_iMaxNy;  // Max neutral activity location
int m_iMaxAx, m_iMaxAy;  // Max Aresden activity location
int m_iMaxEx, m_iMaxEy;  // Max Elvine activity location
int m_iMaxMx, m_iMaxMy;  // Max monster activity location
int m_iMaxPx, m_iMaxPy;  // Max player activity location
```

#### Energy Spheres (Special Events)

```cpp
struct {
    char cType;
    int sX, sY;
} m_stEnergySphereCreationList[DEF_MAXENERGYSPHERES]; // 10

struct {
    char cResult;
    int aresdenX, aresdenY;
    int elvineX, elvineY;
} m_stEnergySphereGoalList[DEF_MAXENERGYSPHERES]; // 10

int  m_iTotalEnergySphereCreationPoint;
int  m_iTotalEnergySphereGoalPoint;
BOOL m_bIsEnergySphereGoalEnabled;
int  m_iCurEnergySphereGoalPointIndex;
BOOL m_bIsEnergySphereAutoCreation;
```

#### Dynamic Gates (Heldenian/Events)

```cpp
char  m_cDynamicGateType;
short m_sDynamicGateCoordRectX1, m_sDynamicGateCoordRectY1;
short m_sDynamicGateCoordRectX2, m_sDynamicGateCoordRectY2;
char  m_cDynamicGateCoordDestMap[11];
short m_sDynamicGateCoordTgtX, m_sDynamicGateCoordTgtY;

struct {
    BOOL m_bIsGateMap;
    char m_cDynamicGateMap[11];
    int  m_iDynamicGateX;
    int  m_iDynamicGateY;
} m_stDynamicGateCoords[DEF_MAXDYNAMICGATES]; // 10
```

#### Heldenian War

```cpp
short m_sHeldenianTowerType;
short m_sHeldenianTowerXPos, m_sHeldenianTowerYPos;
char  m_cHeldenianTowerSide;
char  m_cHeldenianModeMap;

struct {
    char  cDir;
    short dX, dY;
} m_stHeldenianGateDoor[DEF_MAXHELDENIANDOOR]; // 200

struct {
    short sTypeID;
    short dX, dY;
    char  cSide;
} m_stHeldenianTower[DEF_MAXHELDENIANTOWER]; // 200
```

#### Apocalypse Event

```cpp
int   m_iApocalypseMobGenType;
int   m_iApocalypseBossMobNpcID;
short m_sApocalypseBossMobRectX1, m_sApocalypseBossMobRectY1;
short m_sApocalypseBossMobRectX2, m_sApocalypseBossMobRectY2;
```

#### Item Events

```cpp
int m_iTotalItemEvents;
short sMobEventAmount;

struct {
    char cItemName[21];
    int  iAmount;
    int  iTotal;
    int  iMonth;
    int  iDay;
    int  iTotalNum;
} m_stItemEventList[DEF_MAXITEMEVENTS]; // 200
```

#### Agriculture

```cpp
int m_iTotalAgriculture;
```

---

## Tile System

### Tile Memory Layout

Tiles are stored as a flat array accessed by: `tile[x + y * sizeY]`

```cpp
// Accessing a tile
pTile = (class CTile *)(m_pTile + sX + sY * m_sSizeY);
```

**Note:** The formula uses `m_sSizeY` as the row stride, not `m_sSizeX`. This is an unusual row-major ordering.

### Ownership Types

```cpp
#define DEF_OWNERTYPE_PLAYER           1
#define DEF_OWNERTYPE_NPC              2
#define DEF_OWNERTYPE_PLAYER_INDIRECT  3  // For indirect references
```

### Setting/Getting Ownership

When an entity moves onto a tile:

```cpp
// Set new position
m_pMapList[mapIndex]->SetOwner(entityHandle, ownerType, newX, newY);

// Clear old position
m_pMapList[mapIndex]->ClearOwner(debugCode, entityHandle, ownerType, oldX, oldY);
```

### Movement Validation

```cpp
BOOL CMap::bGetMoveable(short dX, short dY, short * pDOtype, short * pTopItem)
{
    // Bounds check (20-tile border)
    if ((dX < 20) || (dX >= m_sSizeX - 20) ||
        (dY < 20) || (dY >= m_sSizeY - 20))
        return FALSE;

    pTile = (class CTile *)(m_pTile + dX + dY * m_sSizeY);

    // Optionally return dynamic object type and item count
    if (pDOtype != NULL) *pDOtype = pTile->m_sDynamicObjectType;
    if (pTopItem != NULL) *pTopItem = pTile->m_cTotalItem;

    // Check conditions
    if (pTile->m_sOwner != NULL) return FALSE;          // Occupied
    if (pTile->m_bIsMoveAllowed == FALSE) return FALSE; // Blocked terrain
    if (pTile->m_bIsTempMoveAllowed == FALSE) return FALSE; // Temp blocked

    return TRUE;
}
```

### Items on Tiles

Each tile can hold up to 12 stacked items (`DEF_TILE_PER_ITEMS = 12`).

```cpp
// Adding item to tile (pushes to front, deletes oldest if full)
BOOL CMap::bSetItem(short sX, short sY, class CItem * pItem)
{
    pTile = (class CTile *)(m_pTile + sX + sY * m_sSizeY);

    // If full, delete oldest item
    if (pTile->m_pItem[DEF_TILE_PER_ITEMS-1] != NULL)
        delete pTile->m_pItem[DEF_TILE_PER_ITEMS-1];
    else
        pTile->m_cTotalItem++;

    // Shift items down
    for (i = DEF_TILE_PER_ITEMS-2; i >= 0; i--)
        pTile->m_pItem[i+1] = pTile->m_pItem[i];

    // Insert new item at front
    pTile->m_pItem[0] = pItem;
    return TRUE;
}

// Getting item from tile (removes from front)
class CItem * CMap::pGetItem(short sX, short sY, ...)
{
    pItem = pTile->m_pItem[0];

    // Shift remaining items up
    for (i = 0; i <= DEF_TILE_PER_ITEMS-2; i++)
        pTile->m_pItem[i] = pTile->m_pItem[i+1];

    pTile->m_cTotalItem--;
    pTile->m_pItem[pTile->m_cTotalItem] = NULL;

    return pItem;
}
```

### Tile Attributes

The `m_iAttribute` field uses bitmasks:

```cpp
// Bit 2 (0x04): No-attack zone
pTile->m_iAttribute = pTile->m_iAttribute | 0x00000004;

// Checking attribute
int CMap::iGetAttribute(int dX, int dY, int iBitMask)
{
    pTile = (class CTile *)(m_pTile + dX + dY * m_sSizeY);
    return (pTile->m_iAttribute & iBitMask);
}
```

---

## Map Configuration

### Loading Sequence

1. **Binary map data** (`.amd` file) - tile geometry and attributes
2. **Text configuration** (`.txt` file) - NPCs, teleports, spawn areas

### Binary Map Format (`.amd`)

Located in: `mapdata/{mapname}.amd`

**Header (256 bytes):**
```
MAPSIZEX = {width}
MAPSIZEY = {height}
TILESIZE = {bytes_per_tile}
```

**Tile Data (per tile, variable size defined by TILESIZE):**
```
Offset 0-1: short - Ground sprite ID
Offset 8:   byte  - Tile flags
    Bit 7 (0x80): Movement blocked
    Bit 6 (0x40): Teleport tile
    Bit 5 (0x20): Farm tile

If ground sprite ID == 19: Water tile
```

```cpp
BOOL CMap::_bDecodeMapDataFileContents()
{
    // Read 256-byte header
    ReadFile(hFile, cHeader, 256, &nRead, NULL);

    // Parse header for dimensions
    // MAPSIZEX, MAPSIZEY, TILESIZE

    // Allocate tile array
    m_pTile = new class CTile[m_sSizeX * m_sSizeY];

    // Read tile data
    for (iy = 0; iy < m_sSizeY; iy++)
    for (ix = 0; ix < m_sSizeX; ix++) {
        ReadFile(hFile, cTemp, m_sTileDataSize, &nRead, NULL);

        pTile = (class CTile *)(m_pTile + ix + iy * m_sSizeY);

        // Bit 7: Movement blocked
        if ((cTemp[8] & 0x80) != 0)
            pTile->m_bIsMoveAllowed = FALSE;
        else
            pTile->m_bIsMoveAllowed = TRUE;

        // Bit 6: Teleport
        if ((cTemp[8] & 0x40) != 0)
            pTile->m_bIsTeleport = TRUE;
        else
            pTile->m_bIsTeleport = FALSE;

        // Bit 5: Farm
        if ((cTemp[8] & 0x20) != 0)
            pTile->m_bIsFarm = TRUE;
        else
            pTile->m_bIsFarm = FALSE;

        // Water check (sprite ID 19)
        sp = (short *)&cTemp[0];
        if (*sp == 19)
            pTile->m_bIsWater = TRUE;
        else
            pTile->m_bIsWater = FALSE;
    }
}
```

### Text Configuration Format (`.txt`)

Located in: `mapdata/{mapname}.txt`

**Keywords:**

| Keyword | Description | Parameters |
|---------|-------------|------------|
| `teleport-loc` | Teleport definition | srcX, srcY, destMap, destX, destY, direction |
| `waypoint` | NPC waypoint | index, x, y |
| `npc` | Static NPC spawn | npcName, moveType, waypoint0-9, namePrefix |
| `random-mob-generator` | Random spawning | enabled(0/1), level |
| `max-object` | Max NPCs on map | count |
| `mgar` | Mob generation avoid rect | index, left, top, right, bottom |
| `smgr` | Spot mob generator | index, type, [rect/waypoints], mobType, maxMobs |
| `location` | Map display name | name |
| `initial-point` | Player spawn point | index, x, y |
| `noattack-rect` | Safe zone | index, left, top, right, bottom |
| `fixed-daymode` | Disable night | 0/1 |
| `fish-point` | Fishing location | index, x, y |
| `mineral-point` | Mining location | index, x, y |
| `strategic-point` | War control point | side, value, x, y |
| `energy-sphere-creation-point` | Event spawn | type, x, y |
| `energy-sphere-goal-point` | Event goal | result, aresX, aresY, elvX, elvY |
| `strike-point` | War target | mapName, x, y, hp, effectX[5], effectY[5] |

**Example Configuration:**
```
location = aresden

initial-point = 0 296 593
initial-point = 1 300 589

waypoint = 0  296 593
waypoint = 1  300 589
waypoint = 10 285 600

teleport-loc = 296 590 default 350 350 5

npc = Guard 1 0 1 2 -1 -1 -1 -1 -1 -1 -1 G

random-mob-generator = 1 5
max-object = 100

mgar = 0  280 580 320 620

smgr = 0 1 100 100 200 200 7 20

noattack-rect = 0 285 585 315 605

fish-point = 0 250 400
mineral-point = 0 320 450

fixed-daymode = 0
```

---

## Teleportation System

### How Teleportation Works

1. Player steps on tile with `m_bIsTeleport == TRUE`
2. Server searches `m_pTeleportLoc[]` for matching source coordinates
3. If found, player is moved to destination map and coordinates
4. Player faces direction specified by `m_cDir`

```cpp
BOOL CMap::bSearchTeleportDest(int sX, int sY, char * pMapName,
                               int * pDx, int * pDy, char * pDir)
{
    for (i = 0; i < DEF_MAXTELEPORTLOC; i++)
        if ((m_pTeleportLoc[i] != NULL) &&
            (m_pTeleportLoc[i]->m_sSrcX == sX) &&
            (m_pTeleportLoc[i]->m_sSrcY == sY)) {

            memcpy(pMapName, m_pTeleportLoc[i]->m_cDestMapName, 10);
            *pDx  = m_pTeleportLoc[i]->m_sDestX;
            *pDy  = m_pTeleportLoc[i]->m_sDestY;
            *pDir = m_pTeleportLoc[i]->m_cDir;
            return TRUE;
        }

    return FALSE;
}
```

### Direction Values

```
    8   1   2
     \  |  /
  7 -- [X] -- 3
     /  |  \
    6   5   4
```

---

## Spawn System

### Initial Points (Player Spawning)

Up to 20 spawn points per map. When a player enters, one is randomly selected (or first for neutral players).

```cpp
void CGame::GetMapInitialPoint(int iMapIndex, short *pX, short *pY,
                               char * pPlayerLocation)
{
    // Build list of defined points
    iTotalPoint = 0;
    for (i = 0; i < DEF_MAXINITIALPOINT; i++)
        if (m_pMapList[iMapIndex]->m_pInitialPoint[i].x != -1) {
            pList[iTotalPoint].x = m_pMapList[iMapIndex]->m_pInitialPoint[i].x;
            pList[iTotalPoint].y = m_pMapList[iMapIndex]->m_pInitialPoint[i].y;
            iTotalPoint++;
        }

    // Neutral players always go to first point
    if ((pPlayerLocation != NULL) && (memcmp(pPlayerLocation, "NONE", 4) == 0))
        i = 0;
    else
        i = iDice(1, iTotalPoint) - 1;

    *pX = pList[i].x;
    *pY = pList[i].y;
}
```

### Finding Empty Position Near Target

Searches in expanding spiral pattern:

```cpp
// Search pattern offsets (25 positions)
char _tmp_cEmptyPosX[] = {
    0,                        // center
    1, 1, 0, -1, -1, -1, 0, 1, // ring 1
    2, 2, 2, 2, 1, 0, -1, -2, -2, -2, -2, -2, -1, 0, 1, 2  // ring 2
};
char _tmp_cEmptyPosY[] = {
    0,
    0, 1, 1, 1, 0, -1, -1, -1,
    -1, 0, 1, 2, 2, 2, 2, 2, 1, 0, -1, -2, -2, -2, -2, -2
};

BOOL CGame::bGetEmptyPosition(short * pX, short * pY, char cMapIndex)
{
    for (i = 0; i < 25; i++)
        if (m_pMapList[cMapIndex]->bGetMoveable(*pX + _tmp_cEmptyPosX[i],
                                                 *pY + _tmp_cEmptyPosY[i]) == TRUE &&
            m_pMapList[cMapIndex]->bGetIsTeleport(*pX + _tmp_cEmptyPosX[i],
                                                   *pY + _tmp_cEmptyPosY[i]) == FALSE) {
            *pX = *pX + _tmp_cEmptyPosX[i];
            *pY = *pY + _tmp_cEmptyPosY[i];
            return TRUE;
        }

    // Fallback to initial point
    GetMapInitialPoint(cMapIndex, pX, pY);
    return FALSE;
}
```

### Spot Mob Generators

Two types:

**Type 1: RANDOMAREA** - Spawns mobs within a rectangle
```
smgr = {index} 1 {left} {top} {right} {bottom} {mobType} {maxMobs}
```

**Type 2: RANDOMWAYPOINT** - Spawns mobs at random waypoints
```
smgr = {index} 2 {wp0} {wp1} ... {wp9} {mobType} {maxMobs}
```

### Mob Generation Avoid Rectangles

Areas where random mob generation is suppressed (typically town centers):

```
mgar = {index} {left} {top} {right} {bottom}
```

---

## Sector System

Maps are divided into a 60x60 grid of sectors for activity tracking.

### Sector Size Calculation

```cpp
sectorX = playerX / 20;
sectorY = playerY / 20;
```

### Activity Tracking

Each sector tracks activity counts for:
- Players (overall)
- Neutral players
- Aresden players
- Elvine players
- Monsters

### Updating Sector Info

Called periodically to aggregate activity:

```cpp
void CGame::UpdateMapSectorInfo()
{
    for (i = 0; i < DEF_MAXMAPS; i++)
    if (m_pMapList[i] != NULL) {
        // Find max activity locations from temp info
        for (ix = 0; ix < DEF_MAXSECTORS; ix++)
        for (iy = 0; iy < DEF_MAXSECTORS; iy++) {
            if (m_pMapList[i]->m_stTempSectorInfo[ix][iy].iPlayerActivity > iMaxPlayerActivity) {
                iMaxPlayerActivity = m_pMapList[i]->m_stTempSectorInfo[ix][iy].iPlayerActivity;
                m_pMapList[i]->m_iMaxPx = ix;
                m_pMapList[i]->m_iMaxPy = iy;
            }
            // ... similar for other activity types
        }

        // Clear temp, update permanent
        m_pMapList[i]->ClearTempSectorInfo();

        // Increment permanent sector counters
        if (m_pMapList[i]->m_iMaxPx > 0)
            m_pMapList[i]->m_stSectorInfo[m_pMapList[i]->m_iMaxPx][m_pMapList[i]->m_iMaxPy].iPlayerActivity++;
    }
}
```

### Aging Sector Info

Decays activity counts over time:

```cpp
void CGame::AgingMapSectorInfo()
{
    for (i = 0; i < DEF_MAXMAPS; i++)
    if (m_pMapList[i] != NULL) {
        for (ix = 0; ix < DEF_MAXSECTORS; ix++)
        for (iy = 0; iy < DEF_MAXSECTORS; iy++) {
            m_pMapList[i]->m_stSectorInfo[ix][iy].iPlayerActivity--;
            if (m_pMapList[i]->m_stSectorInfo[ix][iy].iPlayerActivity < 0)
                m_pMapList[i]->m_stSectorInfo[ix][iy].iPlayerActivity = 0;
            // ... similar for other types
        }
    }
}
```

---

## Occupy Flags & War Integration

### Strategic Points

Locations with strategic value for war calculations:

```cpp
void CGame::_CheckStrategicPointOccupyStatus(char cMapIndex)
{
    m_iStrategicStatus = 0;

    for (i = 0; i < DEF_MAXSTRATEGICPOINTS; i++)
    if (m_pMapList[cMapIndex]->m_pStrategicPointList[i] != NULL) {
        iSide  = m_pMapList[cMapIndex]->m_pStrategicPointList[i]->m_iSide;
        iValue = m_pMapList[cMapIndex]->m_pStrategicPointList[i]->m_iValue;
        iX = m_pMapList[cMapIndex]->m_pStrategicPointList[i]->m_iX;
        iY = m_pMapList[cMapIndex]->m_pStrategicPointList[i]->m_iY;

        pTile = (class CTile *)(m_pMapList[cMapIndex]->m_pTile + iX + iY * m_pMapList[cMapIndex]->m_sSizeY);

        // Weighted by strategic value
        m_iStrategicStatus += pTile->m_iOccupyStatus * iValue;
    }
}
```

### Occupy Flags

Territory markers that track faction control:

```cpp
int CMap::iRegisterOccupyFlag(int dX, int dY, int iSide, int iEKNum, int iDOI)
{
    for (i = 1; i < DEF_MAXOCCUPYFLAG; i++)
    if (m_pOccupyFlag[i] == NULL) {
        m_pOccupyFlag[i] = new class COccupyFlag(dX, dY, iSide, iEKNum, iDOI);
        return i;
    }
    return -1;
}
```

---

## Movement & Pathfinding

### Direction Vectors

```cpp
// Direction index:  0  1  2  3  4  5  6   7   8
char _tmp_cTmpDirX[9] = { 0, 0, 1, 1, 1, 0, -1, -1, -1 };
char _tmp_cTmpDirY[9] = { 0,-1,-1, 0, 1, 1,  1,  0, -1 };

// Movement pattern:
//      8   1   2
//       \  |  /
//    7 -- [X] -- 3
//       /  |  \
//      6   5   4
```

### Simple Direction Calculation

```cpp
char CMisc::cGetNextMoveDir(short sX, short sY, short dX, short dY)
{
    absX = sX - dX;
    absY = sY - dY;

    if ((absX == 0) && (absY == 0)) return 0;

    if (absX == 0) {
        if (absY > 0) return 1;  // North
        if (absY < 0) return 5;  // South
    }
    if (absY == 0) {
        if (absX > 0) return 7;  // West
        if (absX < 0) return 3;  // East
    }
    if ((absX > 0) && (absY > 0)) return 8;  // NW
    if ((absX < 0) && (absY > 0)) return 2;  // NE
    if ((absX > 0) && (absY < 0)) return 6;  // SW
    if ((absX < 0) && (absY < 0)) return 4;  // SE

    return 0;
}
```

### Pathfinding (Bresenham Line)

Uses Bresenham's line algorithm for pathfinding:

```cpp
void CMisc::GetPoint(int x0, int y0, int x1, int y1,
                     int * pX, int * pY, int * pError)
{
    if ((x0 == x1) && (y0 == y1)) {
        *pX = x0; *pY = y0;
        return;
    }

    error = *pError;
    iResultX = x0;
    iResultY = y0;

    dx = x1 - x0;
    dy = y1 - y0;

    x_inc = (dx >= 0) ? 1 : -1;
    y_inc = (dy >= 0) ? 1 : -1;
    dx = abs(dx);
    dy = abs(dy);

    if (dx > dy) {
        error += dy;
        if (error > dx) {
            error -= dx;
            iResultY += y_inc;
        }
        iResultX += x_inc;
    } else {
        error += dx;
        if (error > dy) {
            error -= dy;
            iResultX += x_inc;
        }
        iResultY += y_inc;
    }

    *pX = iResultX;
    *pY = iResultY;
    *pError = error;
}
```

### NPC Movement with Obstacle Avoidance

```cpp
char CGame::cGetNextMoveDir(short sX, short sY, short dstX, short dstY,
                            char cMapIndex, char cTurn, int * pError)
{
    dX = sX; dY = sY;

    // Get next point towards destination
    if (*pError == 0)
        m_Misc.GetPoint(dX, dY, dstX, dstY, &iResX, &iResY, pError);
    else
        m_Misc.GetPoint(dX, dY, dstX, dstY, &iResX, &iResY, pError);

    cDir = m_Misc.cGetNextMoveDir(dX, dY, iResX, iResY);

    // Check if path is blocked
    if (m_pMapList[cMapIndex]->bGetMoveable(sX + _tmp_cTmpDirX[cDir],
                                             sY + _tmp_cTmpDirY[cDir]) == FALSE) {
        // Try turning left or right
        if (cTurn == 0) {
            cTmpDir = cDir - 1;
            if (cTmpDir < 1) cTmpDir = 8;
            // Check alternate direction...
        } else {
            cTmpDir = cDir + 1;
            if (cTmpDir > 8) cTmpDir = 1;
            // Check alternate direction...
        }
    }

    return cDir;
}
```

---

## Map Data Format

### Client Data Packet

When sending map data to clients, a 21x16 tile area is transmitted:

```cpp
#define DEF_MAPDATASIZEX  30  // Client view width
#define DEF_MAPDATASIZEY  25  // Client view height

int CGame::iComposeInitMapData(short sX, short sY, int iClientH, char * pData)
{
    for (iy = 0; iy < 16; iy++)
    for (ix = 0; ix < 21; ix++) {
        pTile = (class CTile *)(pTileSrc + ix + iy * m_pMapList[mapIndex]->m_sSizeY);

        // Only send if tile has content
        if ((pTile->m_sOwner != NULL) || (pTile->m_sDeadOwner != NULL) ||
            (pTile->m_pItem[0] != NULL) || (pTile->m_sDynamicObjectType != NULL)) {

            // Write tile position
            *sp++ = ix;
            *sp++ = iy;

            // Write header flags
            ucHeader = 0;
            if (pTile->m_sOwner != NULL) ucHeader |= 0x01;      // Has owner
            if (pTile->m_sDeadOwner != NULL) ucHeader |= 0x02;  // Has dead body
            if (pTile->m_pItem[0] != NULL) ucHeader |= 0x04;    // Has item
            if (pTile->m_sDynamicObjectType != NULL) ucHeader |= 0x08; // Has dynamic object

            *cp++ = ucHeader;

            // Write owner data if present
            if (ucHeader & 0x01) {
                // Object ID, type, direction, appearance, status, name...
            }

            // Write dead owner data if present
            if (ucHeader & 0x02) { ... }

            // Write item data if present
            if (ucHeader & 0x04) { ... }

            // Write dynamic object if present
            if (ucHeader & 0x08) { ... }
        }
    }
}
```

---

## Configuration Files

### Map List (in server config)

Maps are loaded from a configuration list in the main server config.

### Per-Map Files

| File Pattern | Description |
|--------------|-------------|
| `mapdata/{name}.amd` | Binary tile data |
| `mapdata/{name}.txt` | Configuration (NPCs, teleports, etc.) |

---

## Key Functions Reference

### CMap Functions

| Function | Description |
|----------|-------------|
| `bInit(name)` | Initialize map from files |
| `SetOwner/GetOwner/ClearOwner` | Tile ownership management |
| `bGetMoveable(x, y)` | Check if position is walkable |
| `bGetIsTeleport(x, y)` | Check if position is teleport |
| `bSearchTeleportDest(...)` | Find teleport destination |
| `bSetItem/pGetItem` | Item placement on tiles |
| `SetDynamicObject/bGetDynamicObject` | Dynamic object management |
| `iRegisterOccupyFlag(...)` | Register war territory flag |
| `iGetAttribute(x, y, mask)` | Get tile attributes |
| `_SetupNoAttackArea()` | Initialize safe zones |

### CGame Map Functions

| Function | Description |
|----------|-------------|
| `_bReadMapInfoFiles(index)` | Load map from configuration |
| `__bReadMapInfo(index)` | Parse map .txt configuration |
| `GetMapInitialPoint(...)` | Get spawn point for player |
| `bGetEmptyPosition(...)` | Find walkable position near target |
| `iComposeInitMapData(...)` | Build map data packet for client |
| `iComposeMoveMapData(...)` | Build movement update packet |
| `UpdateMapSectorInfo()` | Update sector activity |
| `AgingMapSectorInfo()` | Decay sector activity |
| `cGetNextMoveDir(...)` | Calculate movement direction |
| `iGetMapIndex(name)` | Get map index by name |

### CMisc Pathfinding

| Function | Description |
|----------|-------------|
| `cGetNextMoveDir(...)` | Simple direction to target |
| `GetPoint(...)` | Bresenham line step |
| `GetPoint2(...)` | Multi-step Bresenham |
| `GetDirPoint(dir, x, y)` | Apply direction to position |

---

## Constants Reference

### Map Limits

```cpp
#define DEF_MAXMAPS                100   // Maximum maps
#define DEF_MAXTELEPORTLOC         200   // Teleports per map
#define DEF_MAXWAYPOINTCFG         200   // Waypoints per map
#define DEF_MAXINITIALPOINT        20    // Spawn points per map
#define DEF_MAXSPOTMOBGENERATOR    100   // Spot generators per map
#define DEF_MAXMGAR                50    // Mob avoid rects per map
#define DEF_MAXNMR                 50    // No-attack rects per map
#define DEF_MAXFISHPOINT           200   // Fish points per map
#define DEF_MAXMINERALPOINT        200   // Mineral points per map
#define DEF_MAXSTRATEGICPOINTS     200   // Strategic points per map
#define DEF_MAXOCCUPYFLAG          20001 // Occupy flags per map
#define DEF_MAXSECTORS             60    // Sectors per axis (60x60 grid)
#define DEF_MAXSTRIKEPOINTS        20    // Strike points per map
#define DEF_MAXENERGYSPHERES       10    // Energy sphere points
#define DEF_MAXDYNAMICGATES        10    // Dynamic gates per map
#define DEF_MAXHELDENIANDOOR       200   // Heldenian doors per map
#define DEF_MAXHELDENIANTOWER      200   // Heldenian towers per map
#define DEF_MAXCRUSADESTRUCTURES   300   // Crusade structures per map
```

### Tile Limits

```cpp
#define DEF_TILE_PER_ITEMS         12    // Items per tile
```

### Map Types

```cpp
#define DEF_MAPTYPE_NORMAL              0
#define DEF_MAPTYPE_NOPENALTY_NOREWARD  1
```

### Owner Types

```cpp
#define DEF_OWNERTYPE_PLAYER            1
#define DEF_OWNERTYPE_NPC               2
#define DEF_OWNERTYPE_PLAYER_INDIRECT   3
```

---

## Modernization Notes

### Issues with Legacy Design

1. **Unusual array indexing**: `tile[x + y * sizeY]` instead of standard `tile[y * sizeX + x]`
2. **Raw pointer arrays**: `CTile*`, `CTeleportLoc*[]`, etc. with manual memory management
3. **Magic numbers**: Hardcoded offsets like 20-tile border, 21x16 view area
4. **Tight coupling**: `CMap` holds reference to `CGame`
5. **No spatial indexing**: Linear search for many operations
6. **Fixed limits**: All arrays are statically sized
7. **Windows-specific**: `POINT`, `RECT`, file APIs

### Modern Improvements

1. **Use standard containers**: `std::vector<CTile>`, `std::array` for fixed sizes
2. **Smart pointers**: `std::unique_ptr` for owned objects
3. **Spatial indexing**: Quadtree or grid-based spatial hash for entity queries
4. **Separation of concerns**: Split into `WorldManager`, `MapData`, `TileSystem`
5. **Type safety**: Strong types for coordinates, directions, map indices
6. **Cross-platform**: Replace Windows types with portable alternatives
7. **Efficient pathfinding**: A* or jump point search for complex paths

### Suggested Modern Structure

```cpp
namespace hb::world {

struct position { int16_t x; int16_t y; };
struct tile_coord { int16_t x; int16_t y; };

enum class direction : uint8_t {
    none = 0, north = 1, northeast = 2, east = 3,
    southeast = 4, south = 5, southwest = 6, west = 7, northwest = 8
};

class tile {
    std::optional<entity_id> owner;
    std::optional<entity_id> dead_owner;
    std::vector<item_id> items;  // Or small_vector<item_id, 4>
    std::optional<dynamic_object_ref> dynamic_object;
    tile_flags flags;
    tile_attributes attributes;
};

class map {
    std::string name;
    std::string location_name;
    int16_t width, height;
    std::vector<tile> tiles;

    std::vector<teleport_location> teleports;
    std::vector<spawn_point> spawn_points;
    std::vector<mob_generator> mob_generators;

    // Spatial indexing
    spatial_hash<entity_id> entity_grid;

    auto get_tile(tile_coord pos) -> std::optional<tile&>;
    auto is_walkable(tile_coord pos) const -> bool;
    auto find_path(tile_coord from, tile_coord to) -> std::vector<tile_coord>;
};

class world_manager {
    std::vector<std::unique_ptr<map>> maps;

    auto get_map(map_id id) -> map*;
    auto get_map_by_name(std::string_view name) -> map*;
    auto get_entities_in_range(map_id, position, float radius) -> std::vector<entity_id>;
};

} // namespace hb::world
```
