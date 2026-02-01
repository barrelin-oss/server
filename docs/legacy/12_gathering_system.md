# Legacy Gathering System Documentation

## Table of Contents
1. [Overview](#overview)
2. [System Constants](#system-constants)
3. [Data Structures](#data-structures)
   - [CFish Class](#cfish-class)
   - [CMineral Class](#cmineral-class)
   - [CDynamicObject Class](#cdynamicobject-class)
   - [Map Gathering Data](#map-gathering-data)
   - [Player Gathering State](#player-gathering-state)
4. [Fishing System](#fishing-system)
   - [Fish Types and Items](#fish-types-and-items)
   - [Fish Generation](#fish-generation)
   - [Fishing Process](#fishing-process)
   - [Fishing Success Calculation](#fishing-success-calculation)
   - [Fish Deletion](#fish-deletion)
5. [Mining System](#mining-system)
   - [Mineral Types](#mineral-types)
   - [Mineral Generation](#mineral-generation)
   - [Mining Process](#mining-process)
   - [Mining Success Calculation](#mining-success-calculation)
   - [Mineral Depletion](#mineral-depletion)
6. [Farming System](#farming-system)
   - [Crop Types](#crop-types)
   - [Planting Crops](#planting-crops)
   - [Harvesting Crops](#harvesting-crops)
7. [Skill Integration](#skill-integration)
8. [Dynamic Object Management](#dynamic-object-management)
9. [Network Messages](#network-messages)
10. [Map Configuration](#map-configuration)
11. [All Related Functions](#all-related-functions)

---

## Overview

The Gathering System in Helbreath encompasses three distinct resource collection mechanics:

1. **Fishing** - Catching fish and special items from water tiles
2. **Mining** - Extracting ores and gems from mineral nodes
3. **Farming** - Planting and harvesting crops in designated farm areas

Each system uses:
- **Dynamic Objects** to represent gatherable nodes on the map
- **Skill checks** to determine success rates
- **Item generation** to reward players
- **Node regeneration** to replenish resources over time

**Files Involved:**
- `Fish.h` / `Fish.cpp` - CFish class
- `Mineral.h` / `Mineral.cpp` - CMineral class
- `DynamicObject.h` / `DynamicObjectID.h` - Dynamic object management
- `Game.cpp` - All gathering logic (lines ~30260-35700, ~51300-51440)
- `Game.h` - Function declarations and member arrays
- `Map.h` - Fish/mineral point storage

---

## System Constants

### Maximum Limits (Game.h)

```cpp
#define DEF_MAXFISHS                200     // Maximum fish nodes globally
#define DEF_MAXMINERALS             200     // Maximum mineral nodes globally
#define DEF_MAXCROPS                200     // Maximum crop entities per map
#define DEF_MAXENGAGINGFISH         30      // Max players fishing same node simultaneously
#define DEF_MAXAGRICULTURE          200     // Max crops per map (agriculture limit)
```

### Map Point Limits (Map.h)

```cpp
#define DEF_MAXFISHPOINT            200     // Max fish spawn points per map
#define DEF_MAXMINERALPOINT         200     // Max mineral spawn points per map
```

### Dynamic Object Types (DynamicObjectID.h)

```cpp
#define DEF_DYNAMICOBJECT_FISH          2   // Fish node (actual fish)
#define DEF_DYNAMICOBJECT_FISHOBJECT    3   // Fish node (non-fish item)
#define DEF_DYNAMICOBJECT_MINERAL1      4   // Ore-type mineral (types 1-4)
#define DEF_DYNAMICOBJECT_MINERAL2      5   // Gem-type mineral (types 5-6)
```

### Skill Indices

```cpp
// Skill index constants (from skill analysis)
#define SKILL_MINING     0   // Mining skill - limited by STR * 2
#define SKILL_FISHING    1   // Fishing skill - limited by DEX * 2
#define SKILL_FARMING    2   // Farming skill - limited by INT * 2
```

### Item Effect Types (Item.h)

```cpp
#define DEF_ITEMEFFECTTYPE_GET          8   // Get items (fishing/mining)
#define DEF_ITEMEFFECTTYPE_FARMING      30  // Farming seed bags
```

### Skill Effect Types (Skill.h)

```cpp
#define DEF_SKILLEFFECTTYPE_GET         1   // Get resource skill type
#define DEF_SKILLEFFECTTYPE_PRETEND     2   // Pretend skill type
#define DEF_SKILLEFFECTTYPE_TAMING      3   // Taming skill type
```

---

## Data Structures

### CFish Class

**File:** `Fish.h`, `Fish.cpp`

The CFish class represents a fish node (fishing spot) in the game world.

```cpp
class CFish
{
public:
    CFish(char cMapIndex, short sX, short sY, short sType,
          class CItem * pItem, int iDifficulty);
    virtual ~CFish();

    char  m_cMapIndex;              // Map index where fish is located
    short m_sX, m_sY;               // Position on map (X, Y coordinates)
    short m_sType;                  // Fish type (unused, always 1)
    class CItem * m_pItem;          // Item allocated to this fish node
    short m_sDynamicObjectHandle;   // Handle to associated dynamic object
    short m_sEngagingCount;         // Number of players currently fishing this node
    int   m_iDifficulty;            // Difficulty rating (affects skill check)
};
```

**Constructor Logic:**
```cpp
CFish::CFish(char cMapIndex, short sX, short sY, short sType,
             class CItem * pItem, int iDifficulty)
{
    m_cMapIndex      = cMapIndex;
    m_sX             = sX;
    m_sY             = sY;
    m_sType          = sType;
    m_pItem          = pItem;
    m_sEngagingCount = 0;
    m_iDifficulty    = iDifficulty;

    // Ensure minimum difficulty of 1
    if (m_iDifficulty <= 0)
        m_iDifficulty = 1;
}
```

**Destructor:** Deletes the associated item if not NULL.

### CMineral Class

**File:** `Mineral.h`, `Mineral.cpp`

The CMineral class represents a mineral node (mining spot) in the game world.

```cpp
class CMineral
{
public:
    CMineral(char cType, char cMapIndex, int sX, int sY, int iRemain);
    virtual ~CMineral();

    char  m_cType;                  // Mineral type (1-6)
    char  m_cMapIndex;              // Map index where mineral is located
    int   m_sX, m_sY;               // Position on map (X, Y coordinates)
    int   m_iDifficulty;            // Difficulty rating for mining
    short m_sDynamicObjectHandle;   // Handle to associated dynamic object
    int   m_iRemain;                // Remaining extraction count
};
```

**Constructor Logic:**
```cpp
CMineral::CMineral(char cType, char cMapIndex, int sX, int sY, int iRemain)
{
    m_cType      = cType;
    m_cMapIndex  = cMapIndex;
    m_sX         = sX;
    m_sY         = sY;
    m_iRemain    = iRemain;
    m_iDifficulty = 0;  // Set later based on type
}
```

### CDynamicObject Class

**File:** `DynamicObject.h`

Dynamic objects are used to represent temporary world objects including fish and mineral nodes.

```cpp
class CDynamicObject
{
public:
    CDynamicObject(short sOwner, char cOwnerType, short sType,
                   char cMapIndex, short sX, short sY,
                   DWORD dwRegisterTime, DWORD dwLastTime, int iV1);
    virtual ~CDynamicObject();

    short m_sOwner;         // Owner index (Fish index for fish nodes)
    char  m_cOwnerType;     // Owner type (NULL for fish/minerals)
    short m_sType;          // Dynamic object type (see DynamicObjectID.h)
    char  m_cMapIndex;      // Map index
    short m_sX, m_sY;       // Position
    DWORD m_dwRegisterTime; // Creation time
    DWORD m_dwLastTime;     // Duration (0 = permanent until depleted)
    int   m_iCount;         // Counter for special uses
    int   m_iV1;            // Extra data (Mineral index for mineral nodes)
};
```

### Map Gathering Data

**File:** `Map.h`

```cpp
class CMap {
    // Fish spawn points
    POINT m_FishPointList[DEF_MAXFISHPOINT];    // Array of (x,y) spawn positions
    int   m_iTotalFishPoint;    // Number of defined fish points
    int   m_iMaxFish;           // Maximum fish allowed on this map
    int   m_iCurFish;           // Current fish count on this map

    // Mineral spawn points
    BOOL  m_bMineralGenerator;          // Whether this map generates minerals
    char  m_cMineralGeneratorLevel;     // Max mineral type that can spawn (1-6)
    POINT m_MineralPointList[DEF_MAXMINERALPOINT];  // Array of spawn positions
    int   m_iTotalMineralPoint; // Number of defined mineral points
    int   m_iMaxMineral;        // Maximum minerals allowed on this map
    int   m_iCurMineral;        // Current mineral count on this map

    // Farming
    int   m_iTotalAgriculture;  // Current crop count on map
};
```

### Player Gathering State

**File:** `Client.h`

```cpp
class CClient {
    // Fishing state
    int   m_iAllocatedFish;     // Fish node index being fished (0 = not fishing)
    int   m_iFishChance;        // Current catch success percentage (1-99)

    // Skill usage tracking
    BOOL  m_bSkillUsingStatus[DEF_MAXSKILLTYPE];  // Active skill states
    int   m_iSkillUsingTimeID[DEF_MAXSKILLTYPE];  // Skill timing

    // Skill levels
    unsigned char m_cSkillMastery[DEF_MAXSKILLTYPE];  // Skill mastery 0-100
    int   m_iSkillSSN[DEF_MAXSKILLTYPE];  // Skill experience points
};
```

---

## Fishing System

### Fish Types and Items

Fishing can yield various items based on random rolls during generation:

#### Standard Fish (1d9 roll, cases 1-8)

| Roll | Item Name | Korean Name | Difficulty |
|------|-----------|-------------|------------|
| 1 | RedCarp | Red Fish | 1d10 + 20 |
| 2 | GreenCarp | Green Fish | 1d5 + 10 |
| 3 | GoldCarp | Gold Fish | 1d10 + 1 |
| 4 | CrucianCarp | Crucian Carp | 1 |
| 5 | BlueSeaBream | Blue Sea Bream | 1d15 + 1 |
| 6 | RedSeaBream | Red Sea Bream | 1d18 + 1 |
| 7 | Salmon | Salmon | 1d12 + 1 |
| 8 | GrayMullet | Gray Mullet | 1d10 + 1 |

#### Special Items (Roll 9, then 1d150 for sub-roll)

| Sub-Roll | Item Name | Difficulty |
|----------|-----------|------------|
| 1-3 | PowerGreenPotion | 5d4 + 30 |
| 10-11 | SuperPowerGreenPotion | 5d4 + 50 |
| 20 | Dagger+2 | 5d4 + 30 |
| 30 | LongSword+2 | 5d4 + 40 |
| 40 | Scimitar+2 | 5d4 + 50 |
| 50 | Rapier+2 | 5d4 + 60 |
| 60 | Flameberge+2 | 5d4 + 60 |
| 70 | WarAxe+2 | 5d4 + 50 |
| 90 | Ruby | 5d4 + 40 |
| 95 | Diamond | 5d4 + 40 |

#### Fish Item IDs (for dynamic object type selection)

```cpp
// These item IDs create DEF_DYNAMICOBJECT_FISH type
case 101: // Red Fish
case 102: // Green Fish
case 103: // Gold Fish
case 570-577: // Various fish types

// All other items create DEF_DYNAMICOBJECT_FISHOBJECT type
```

### Fish Generation

**Function:** `CGame::FishGenerator()`

**Location:** Game.cpp:33870-33971

**Called:** Every 4 seconds from `GameProcess()` via `m_dwFishTime` timer

**Algorithm:**
```cpp
void CGame::FishGenerator()
{
    for (i = 0; i < DEF_MAXMAPS; i++) {
        // 10% chance per tick (1d10 == 5)
        // Map must exist
        // Current fish count must be below maximum
        if ((iDice(1,10) == 5) && (m_pMapList[i] != NULL) &&
            (m_pMapList[i]->m_iCurFish < m_pMapList[i]->m_iMaxFish)) {

            // Select random fish point
            iP = iDice(1, m_pMapList[i]->m_iTotalFishPoint) - 1;
            if ((m_pMapList[i]->m_FishPointList[iP].x == -1) ||
                (m_pMapList[i]->m_FishPointList[iP].y == -1)) break;

            // Add small random offset (-1 to +1)
            tX = m_pMapList[i]->m_FishPointList[iP].x + (iDice(1,3) - 2);
            tY = m_pMapList[i]->m_FishPointList[iP].y + (iDice(1,3) - 2);

            // Create item and determine type/difficulty
            pItem = new class CItem;
            // ... item type selection (see Fish Types above)

            // Duration: 10-30 minutes
            dwLastTime = (60000 * 10) + (iDice(1,3) - 1)*(60000 * 10);

            if (_bInitItemAttr(pItem, cItemName) == TRUE) {
                iRet = iCreateFish(i, tX, tY, 1, pItem, sDifficulty, dwLastTime);
            }
        }
    }
}
```

**Fish Creation Function:** `CGame::iCreateFish()`

**Location:** Game.cpp:33439-33487

```cpp
int CGame::iCreateFish(char cMapIndex, short sX, short sY, short sType,
                       class CItem * pItem, int iDifficulty, DWORD dwLastTime)
{
    // Validate position is water
    if (m_pMapList[cMapIndex]->bGetIsWater(sX, sY) == FALSE) return NULL;

    // Find empty slot in fish array
    for (i = 1; i < DEF_MAXFISHS; i++)
    if (m_pFish[i] == NULL) {
        // Create fish node
        m_pFish[i] = new class CFish(cMapIndex, sX, sY, sType, pItem, iDifficulty);

        // Create associated dynamic object
        // Type depends on whether item is actual fish or other object
        switch (pItem->m_sIDnum) {
        case 101: case 102: case 103:  // Fish items
        case 570-577:
            iDynamicHandle = iAddDynamicObjectList(i, NULL,
                DEF_DYNAMICOBJECT_FISH, cMapIndex, sX, sY, dwLastTime);
            break;
        default:
            // Non-fish items
            iDynamicHandle = iAddDynamicObjectList(i, NULL,
                DEF_DYNAMICOBJECT_FISHOBJECT, cMapIndex, sX, sY, dwLastTime);
            break;
        }

        m_pFish[i]->m_sDynamicObjectHandle = iDynamicHandle;
        m_pMapList[cMapIndex]->m_iCurFish++;
        return i;
    }
    return NULL;
}
```

### Fishing Process

**Engagement Function:** `CGame::iCheckFish()`

**Location:** Game.cpp:33530-33574

When a player uses the fishing skill near water, this function checks if a fish node is nearby:

```cpp
int CGame::iCheckFish(int iClientH, char cMapIndex, short dX, short dY)
{
    // Search all dynamic objects
    for (i = 1; i < DEF_MAXDYNAMICOBJECTS; i++)
    if (m_pDynamicObjectList[i] != NULL) {
        sDistX = abs(m_pDynamicObjectList[i]->m_sX - dX);
        sDistY = abs(m_pDynamicObjectList[i]->m_sY - dY);

        // Check if within 2 tiles and is a fish object
        if ((m_pDynamicObjectList[i]->m_cMapIndex == cMapIndex) &&
            ((m_pDynamicObjectList[i]->m_sType == DEF_DYNAMICOBJECT_FISH) ||
             (m_pDynamicObjectList[i]->m_sType == DEF_DYNAMICOBJECT_FISHOBJECT)) &&
            (sDistX <= 2) && (sDistY <= 2)) {

            // Check engagement limit
            if (m_pFish[m_pDynamicObjectList[i]->m_sOwner]->m_sEngagingCount >=
                DEF_MAXENGAGINGFISH) return 0;

            // Check player not already fishing
            if (m_pClientList[iClientH]->m_iAllocatedFish != NULL) return 0;

            // Allocate fish to player
            m_pClientList[iClientH]->m_iAllocatedFish = m_pDynamicObjectList[i]->m_sOwner;
            m_pClientList[iClientH]->m_iFishChance = 1;  // Start at 1%
            m_pClientList[iClientH]->m_bSkillUsingStatus[1] = TRUE;  // Fishing skill active

            // Notify client of fishing mode with item preview
            SendNotifyMsg(NULL, iClientH, DEF_NOTIFY_EVENTFISHMODE,
                (fish->m_pItem->m_wPrice/2),  // Value preview
                fish->m_pItem->m_sSprite,      // Item sprite
                fish->m_pItem->m_sSpriteFrame, // Sprite frame
                fish->m_pItem->m_cName);       // Item name

            // Increment engaging count
            m_pFish[m_pDynamicObjectList[i]->m_sOwner]->m_sEngagingCount++;
            return i;
        }
    }
    return 0;
}
```

**Chance Processor:** `CGame::FishProcessor()`

**Location:** Game.cpp:33576-33615

Called every 4 seconds to update fishing chance for all active fishers:

```cpp
void CGame::FishProcessor()
{
    for (i = 1; i < DEF_MAXCLIENTS; i++) {
        if ((m_pClientList[i] != NULL) &&
            (m_pClientList[i]->m_bIsInitComplete == TRUE) &&
            (m_pClientList[i]->m_iAllocatedFish != NULL)) {

            // Get skill level
            iSkillLevel = m_pClientList[i]->m_cSkillMastery[1];  // Fishing skill

            // Reduce by fish difficulty
            iSkillLevel -= m_pFish[m_pClientList[i]->m_iAllocatedFish]->m_iDifficulty;
            if (iSkillLevel <= 0) iSkillLevel = 1;

            // Calculate change amount (skill/10, minimum 1)
            iChangeValue = iSkillLevel / 10;
            if (iChangeValue <= 0) iChangeValue = 1;
            iChangeValue = iDice(1, iChangeValue);

            // Roll against skill
            iResult = iDice(1, 100);
            if (iSkillLevel > iResult) {
                // Success - increase catch chance
                m_pClientList[i]->m_iFishChance += iChangeValue;
                if (m_pClientList[i]->m_iFishChance > 99)
                    m_pClientList[i]->m_iFishChance = 99;
            }
            else if (iSkillLevel < iResult) {
                // Failure - decrease catch chance
                m_pClientList[i]->m_iFishChance -= iChangeValue;
                if (m_pClientList[i]->m_iFishChance < 1)
                    m_pClientList[i]->m_iFishChance = 1;
            }

            // Notify client of new chance
            SendNotifyMsg(NULL, i, DEF_NOTIFY_FISHCHANCE,
                m_pClientList[i]->m_iFishChance, NULL, NULL, NULL);
        }
    }
}
```

### Fishing Success Calculation

**Catch Attempt Handler:** `CGame::ReqGetFishThisTimeHandler()`

**Location:** Game.cpp:33816-33867

When player attempts to catch the fish:

```cpp
void CGame::ReqGetFishThisTimeHandler(int iClientH)
{
    if (m_pClientList[iClientH]->m_iAllocatedFish == NULL) return;

    // Clear skill using status
    m_pClientList[iClientH]->m_bSkillUsingStatus[1] = FALSE;

    // Roll against current catch chance
    iResult = iDice(1, 100);
    if (m_pClientList[iClientH]->m_iFishChance >= iResult) {
        // SUCCESS!

        // Award experience based on difficulty
        GetExp(iClientH, iDice(fish->m_iDifficulty, 5));

        // Increase fishing skill
        CalculateSSN_SkillIndex(iClientH, 1, fish->m_iDifficulty);

        // Get item from fish node
        pItem = m_pFish[m_pClientList[iClientH]->m_iAllocatedFish]->m_pItem;
        m_pFish[m_pClientList[iClientH]->m_iAllocatedFish]->m_pItem = NULL;

        // Drop item at player's feet
        m_pMapList[m_pClientList[iClientH]->m_cMapIndex]->bSetItem(
            m_pClientList[iClientH]->m_sX,
            m_pClientList[iClientH]->m_sY,
            pItem);

        // Notify nearby clients of item drop
        SendEventToNearClient_TypeB(MSGID_EVENT_COMMON, DEF_COMMONTYPE_ITEMDROP, ...);

        // Notify success
        SendNotifyMsg(NULL, iClientH, DEF_NOTIFY_FISHSUCCESS, NULL, NULL, NULL, NULL);

        iFishH = m_pClientList[iClientH]->m_iAllocatedFish;
        m_pClientList[iClientH]->m_iAllocatedFish = NULL;

        // Delete the fish node
        bDeleteFish(iFishH, 1);
        return;
    }

    // FAILURE
    m_pFish[m_pClientList[iClientH]->m_iAllocatedFish]->m_sEngagingCount--;
    SendNotifyMsg(NULL, iClientH, DEF_NOTIFY_FISHFAIL, NULL, NULL, NULL, NULL);
    m_pClientList[iClientH]->m_iAllocatedFish = NULL;
}
```

### Fish Deletion

**Function:** `CGame::bDeleteFish()`

**Location:** Game.cpp:33490-33527

```cpp
BOOL CGame::bDeleteFish(int iHandle, int iDelMode)
{
    if (m_pFish[iHandle] == NULL) return FALSE;

    dwTime = timeGetTime();
    iH = m_pFish[iHandle]->m_sDynamicObjectHandle;

    if (m_pDynamicObjectList[iH] != NULL) {
        // Notify nearby clients of removal
        SendEventToNearClient_TypeB(MSGID_DYNAMICOBJECT, DEF_MSGTYPE_REJECT, ...);

        // Remove from map
        m_pMapList[...]->SetDynamicObject(NULL, NULL, sX, sY, dwTime);
        m_pMapList[...]->m_iCurFish--;

        delete m_pDynamicObjectList[iH];
        m_pDynamicObjectList[iH] = NULL;
    }

    // Notify all players fishing this node
    for (i = 1; i < DEF_MAXCLIENTS; i++) {
        if ((m_pClientList[i] != NULL) &&
            (m_pClientList[i]->m_bIsInitComplete == TRUE) &&
            (m_pClientList[i]->m_iAllocatedFish == iHandle)) {

            SendNotifyMsg(NULL, i, DEF_NOTIFY_FISHCANCELED, iDelMode, NULL, NULL, NULL);
            ClearSkillUsingStatus(i);  // Clear fishing skill state
        }
    }

    delete m_pFish[iHandle];
    m_pFish[iHandle] = NULL;
    return TRUE;
}
```

**Deletion Modes (iDelMode):**
- `1` = Fish was caught successfully
- `2` = Fish despawned due to timeout (from DynamicObjectEffectProcessor)

---

## Mining System

### Mineral Types

| Type | Visual | Difficulty | Remaining | Primary Resources |
|------|--------|------------|-----------|-------------------|
| 1 | MINERAL1 | 10 | 20 | Coal, IronOre, BlondeStone |
| 2 | MINERAL1 | 15 | 15 | Coal, IronOre, SilverNugget, BlondeStone |
| 3 | MINERAL1 | 20 | 10 | Coal, IronOre, SilverNugget |
| 4 | MINERAL1 | 50 | 8 | Coal, IronOre, SilverNugget, GoldNugget, Mithral |
| 5 | MINERAL2 | 70 | 6 | Crystal, Sapphire |
| 6 | MINERAL2 | 90 | 4 | Crystal, Ruby, Emerald, Sapphire, Diamond |

### Mineral Generation

**Function:** `CGame::MineralGenerator()`

**Location:** Game.cpp:35302-35320

**Called:** Every 10 seconds from `GameProcess()` via `m_dwMapSectorInfoTime` timer

```cpp
void CGame::MineralGenerator()
{
    for (i = 0; i < DEF_MAXMAPS; i++) {
        // 25% chance per tick (1d4 == 1)
        // Map must have mineral generation enabled
        // Current count below maximum
        if ((iDice(1,4) == 1) && (m_pMapList[i] != NULL) &&
            (m_pMapList[i]->m_bMineralGenerator == TRUE) &&
            (m_pMapList[i]->m_iCurMineral < m_pMapList[i]->m_iMaxMineral)) {

            // Select random mineral point
            iP = iDice(1, m_pMapList[i]->m_iTotalMineralPoint) - 1;
            if ((m_pMapList[i]->m_MineralPointList[iP].x == -1) ||
                (m_pMapList[i]->m_MineralPointList[iP].y == -1)) break;

            tX = m_pMapList[i]->m_MineralPointList[iP].x;
            tY = m_pMapList[i]->m_MineralPointList[iP].y;

            iRet = iCreateMineral(i, tX, tY, m_pMapList[i]->m_cMineralGeneratorLevel);
        }
    }
}
```

**Mineral Creation Function:** `CGame::iCreateMineral()`

**Location:** Game.cpp:35322-35382

```cpp
int CGame::iCreateMineral(char cMapIndex, int tX, int tY, char cLevel)
{
    for (i = 1; i < DEF_MAXMINERALS; i++)
    if (m_pMineral[i] == NULL) {
        // Random type from 1 to map's generator level
        iMineralType = iDice(1, cLevel);

        m_pMineral[i] = new class CMineral(iMineralType, cMapIndex, tX, tY, 1);

        // Create appropriate dynamic object
        switch (iMineralType) {
        case 1: case 2: case 3: case 4:  // Ore types
            iDynamicHandle = iAddDynamicObjectList(NULL, NULL,
                DEF_DYNAMICOBJECT_MINERAL1, cMapIndex, tX, tY, NULL, i);
            break;
        case 5: case 6:  // Gem types
            iDynamicHandle = iAddDynamicObjectList(NULL, NULL,
                DEF_DYNAMICOBJECT_MINERAL2, cMapIndex, tX, tY, NULL, i);
            break;
        default:
            iDynamicHandle = iAddDynamicObjectList(NULL, NULL,
                DEF_DYNAMICOBJECT_MINERAL1, cMapIndex, tX, tY, NULL, i);
            break;
        }

        m_pMineral[i]->m_sDynamicObjectHandle = iDynamicHandle;

        // Set difficulty and remaining extractions
        switch (iMineralType) {
        case 1: m_pMineral[i]->m_iDifficulty = 10; m_pMineral[i]->m_iRemain = 20; break;
        case 2: m_pMineral[i]->m_iDifficulty = 15; m_pMineral[i]->m_iRemain = 15; break;
        case 3: m_pMineral[i]->m_iDifficulty = 20; m_pMineral[i]->m_iRemain = 10; break;
        case 4: m_pMineral[i]->m_iDifficulty = 50; m_pMineral[i]->m_iRemain = 8; break;
        case 5: m_pMineral[i]->m_iDifficulty = 70; m_pMineral[i]->m_iRemain = 6; break;
        case 6: m_pMineral[i]->m_iDifficulty = 90; m_pMineral[i]->m_iRemain = 4; break;
        default: m_pMineral[i]->m_iDifficulty = 10; m_pMineral[i]->m_iRemain = 20; break;
        }

        m_pMapList[cMapIndex]->m_iCurMineral++;
        return i;
    }
    return NULL;
}
```

### Mining Process

**Function:** `CGame::_CheckMiningAction()`

**Location:** Game.cpp:35385-35660

Mining is triggered through normal attack actions when holding a pickaxe.

```cpp
void CGame::_CheckMiningAction(int iClientH, int dX, int dY)
{
    // Get dynamic object at target location
    m_pMapList[...]->bGetDynamicObject(dX, dY, &sType, &dwRegisterTime, &iDynamicIndex);

    // Clear invisibility if mining while invisible
    if ((m_pClientList[iClientH]->m_iStatus & 0x10) != 0) {
        SetInvisibilityFlag(iClientH, DEF_OWNERTYPE_PLAYER, FALSE);
        // ... remove invisibility effect
    }

    switch (sType) {
    case DEF_DYNAMICOBJECT_MINERAL1:
    case DEF_DYNAMICOBJECT_MINERAL2:
        // Check if holding pickaxe (weapon type 25)
        wWeaponType = ((m_pClientList[iClientH]->m_sAppr2 & 0x0FF0) >> 4);
        if (wWeaponType != 25) return;  // Not holding pickaxe

        // Must be in combat stance
        if ((m_pClientList[iClientH]->m_sAppr2 & 0xF000) == 0) return;

        // Get mining skill level
        iSkillLevel = m_pClientList[iClientH]->m_cSkillMastery[0];  // Mining = 0
        if (iSkillLevel == 0) break;

        // Reduce by mineral difficulty
        iSkillLevel -= m_pMineral[...]->m_iDifficulty;
        if (iSkillLevel <= 0) iSkillLevel = 1;

        // Roll for success
        iResult = iDice(1, 100);
        if (iResult <= iSkillLevel) {
            // SUCCESS - award skill XP
            CalculateSSN_SkillIndex(iClientH, 0, 1);  // Mining skill

            // Determine resource based on mineral type
            // (see detailed loot tables below)

            // Create and drop item
            pItem = new class CItem;
            if (_bInitItemAttr(pItem, iItemID) == TRUE) {
                m_pMapList[...]->bSetItem(playerX, playerY, pItem);
                SendEventToNearClient_TypeB(MSGID_EVENT_COMMON,
                    DEF_COMMONTYPE_ITEMDROP, ...);
            }

            // Decrement remaining extractions
            m_pMineral[...]->m_iRemain--;
            if (m_pMineral[...]->m_iRemain <= 0) {
                // Mineral depleted
                bDeleteMineral(mineralIndex);
                delete m_pDynamicObjectList[iDynamicIndex];
                m_pDynamicObjectList[iDynamicIndex] = NULL;
            }
        }
        break;
    }
}
```

### Mining Success Calculation

**Success Formula:**
```
Effective Skill = Mining Skill - Mineral Difficulty
if (Effective Skill <= 0) Effective Skill = 1
Roll = 1d100
Success if Roll <= Effective Skill
```

**Example:**
- Player Mining Skill: 70
- Type 4 Mineral Difficulty: 50
- Effective Skill: 70 - 50 = 20
- Success chance: 20%

### Mining Loot Tables

#### Type 1 Mineral (Difficulty 10, Remain 20)

| Roll (1d5) | Item | Item ID | Experience |
|------------|------|---------|------------|
| 1-3 | Coal | 355 | 1d3 |
| 4 | IronOre | 357 | 1d3 |
| 5 | BlondeStone | 507 | 1d3 |

#### Type 2 Mineral (Difficulty 15, Remain 15)

| Roll (1d5) | Item | Item ID | Experience |
|------------|------|---------|------------|
| 1-2 | Coal | 355 | 1d3 |
| 3-4 | IronOre | 357 | 1d3 |
| 5 | SilverNugget (33%) or BlondeStone (67%) | 356/507 | 1d4/1d3 |

#### Type 3 Mineral (Difficulty 20, Remain 10)

| Roll (1d6) | Item | Item ID | Experience |
|------------|------|---------|------------|
| 1 | Coal | 355 | 1d3 |
| 2-5 | IronOre | 357 | 1d3 |
| 6 | 12.5% SilverNugget, else IronOre | 356/357 | 1d4/1d3 |

#### Type 4 Mineral (Difficulty 50, Remain 8)

| Roll (1d6) | Item | Item ID | Experience |
|------------|------|---------|------------|
| 1 | Coal | 355 | 1d3 |
| 2 | 33% SilverNugget | 356 | 1d4 |
| 3-5 | IronOre | 357 | 1d3 |
| 6 | Complex: Mithral/GoldNugget/SilverNugget/IronOre | 508/354/356/357 | Various |

#### Type 5 Mineral (Difficulty 70, Remain 6)

| Roll (1d19) | Item | Item ID | Experience |
|-------------|------|---------|------------|
| 3 | Sapphire | 352 | 2d3 |
| Other | Crystal | 358 | 2d3 |

#### Type 6 Mineral (Difficulty 90, Remain 4)

| Roll (1d5) | Sub-Roll | Item | Item ID | Experience |
|------------|----------|------|---------|------------|
| 1 | 16.7% Emerald, else Crystal | 353/358 | 2d4/2d3 |
| 2 | 16.7% Sapphire, else Crystal | 352/358 | 2d4/2d3 |
| 3 | 16.7% Ruby, else Crystal | 351/358 | 2d4/2d3 |
| 4 | Crystal | 358 | 2d3 |
| 5 | 8.3% Diamond, else Crystal | 350/358 | 2d5/2d3 |

### Mineral Depletion

**Function:** `CGame::bDeleteMineral()`

**Location:** Game.cpp:35662-35689

```cpp
BOOL CGame::bDeleteMineral(int iIndex)
{
    if (m_pMineral[iIndex] == NULL) return FALSE;

    iDynamicIndex = m_pMineral[iIndex]->m_sDynamicObjectHandle;

    // Notify nearby clients
    SendEventToNearClient_TypeB(MSGID_DYNAMICOBJECT, DEF_MSGTYPE_REJECT, ...);

    // Remove from map
    m_pMapList[...]->SetDynamicObject(NULL, NULL, sX, sY, dwTime);

    // Re-enable movement on tile
    m_pMapList[...]->SetTempMoveAllowedFlag(sX, sY, TRUE);

    // Decrement count
    m_pMapList[...]->m_iCurMineral--;

    delete m_pMineral[iIndex];
    m_pMineral[iIndex] = NULL;
    return TRUE;
}
```

---

## Farming System

### Crop Types

Farming uses the NPC system with "Crops" type NPCs. Crops have types that determine yield:

| Crop Type | Item Name | Item ID | Experience |
|-----------|-----------|---------|------------|
| 1 | WaterMelon | 820 | 3d10 |
| 2 | Pumpkin | 821 | 3d10 |
| 3 | Garlic | 822 | 4d10 |
| 4 | Barley | 823 | 4d10 |
| 5 | Carrot | 824 | 5d10 |
| 6 | Radish | 825 | 5d10 |
| 7 | Corn | 826 | 6d10 |
| 8 | ChineseBellflower | 827 | 6d10 |
| 9 | Melone | 828 | 7d10 |
| 10 | Tommato | 829 | 7d10 |
| 11 | Grapes | 830 | 8d10 |
| 12 | BlueGrapes | 831 | 8d10 |
| 13 | Mushroom | 832 | 9d10 |
| Default | Ginseng | 721 | 10d10 |

### Planting Crops

**Function:** `CGame::bPlantSeedBag()`

**Location:** Game.cpp:51313-51390

```cpp
BOOL CGame::bPlantSeedBag(int iMapIndex, int dX, int dY,
                          int iItemEffectValue1, int iItemEffectValue2, int iClientH)
{
    // Check agriculture limit (200 per map)
    if (m_pMapList[...]->m_iTotalAgriculture >= 200) {
        SendNotifyMsg(NULL, iClientH, DEF_NOTIFY_NOMOREAGRICULTURE, ...);
        return FALSE;
    }

    // Check farming skill requirement
    // iItemEffectValue2 is required skill level for this seed
    if (iItemEffectValue2 > m_pClientList[iClientH]->m_cSkillMastery[2]) {
        SendNotifyMsg(NULL, iClientH, DEF_NOTIFY_AGRICULTURESKILLLIMIT, ...);
        return FALSE;
    }

    // Check if position is valid farm area
    if (m_pMapList[...]->bGetIsFarm(dX, dY) == FALSE) {
        SendNotifyMsg(NULL, iClientH, DEF_NOTIFY_AGRICULTURENOAREA, ...);
        return FALSE;
    }

    // Create crop NPC
    strcpy(cNpcName, "Crops");
    bRet = bCreateNewNpc(cNpcName, cName, mapName, 0, 0,
                         DEF_MOVETYPE_RANDOM, &tX, &tY, ...);

    if (bRet == TRUE) {
        // Set crop type and skill requirement
        m_pNpcList[sOwnerH]->m_cCropType = iItemEffectValue1;
        m_pNpcList[sOwnerH]->m_cCropSkill = iItemEffectValue2;
        m_pNpcList[sOwnerH]->m_sAppr2 = 1;  // Set appearance

        return TRUE;
    }
    return FALSE;
}
```

**Trigger:** Using an item with `DEF_ITEMEFFECTTYPE_FARMING` effect type.

### Harvesting Crops

**Function:** `CGame::_CheckFarmingAction()`

**Location:** Game.cpp:51392-51437

Harvesting is triggered by attacking mature crop NPCs with a sickle (weapon type 27):

```cpp
// In iCalculateAttackEffect (line ~52082):
if ((wWeaponType == 27) &&                          // Sickle
    (m_pNpcList[sTargetH]->m_cCropType != 0) &&     // Is a crop
    (m_pNpcList[sTargetH]->m_cActionLimit == 5) &&  // Is mature
    (m_pNpcList[sTargetH]->m_iBuildCount > 0)) {    // Has yield

    cCropSkill = m_pNpcList[sTargetH]->m_cCropSkill;
    cFarmingSkill = m_pClientList[sAttackerH]->m_cSkillMastery[2];

    // Skill check
    if (cFarmingSkill > cCropSkill) {
        // Success
        CalculateSSN_SkillIndex(sAttackerH, 2, cFarmingSkill <= cCropSkill + 10);
        _CheckFarmingAction(sAttackerH, sTargetH, 1);  // Drop at crop
    }
    else if (cFarmingSkill == cCropSkill) {
        // Partial success
        CalculateSSN_SkillIndex(sAttackerH, 2, cFarmingSkill <= cCropSkill + 10);
        _CheckFarmingAction(sAttackerH, sTargetH, 0);  // Drop at player
    }
    else {
        // Skill not high enough
        CalculateSSN_SkillIndex(sAttackerH, 2, cFarmingSkill <= cCropSkill + 10);
        _CheckFarmingAction(sAttackerH, sTargetH, 0);
    }
}
```

```cpp
void CGame::_CheckFarmingAction(short sAttackerH, short sTargetH, BOOL bType)
{
    // Get crop type
    cCropType = m_pNpcList[sTargetH]->m_cCropType;

    // Determine item and experience based on crop type
    switch (cCropType) {
    case 1: GetExp(sAttackerH, iDice(3,10)); iItemID = 820; break;  // WaterMelon
    case 2: GetExp(sAttackerH, iDice(3,10)); iItemID = 821; break;  // Pumpkin
    // ... etc for all crop types
    }

    // Create item
    pItem = new class CItem;
    _bInitItemAttr(pItem, iItemID);

    // Drop location depends on bType
    if (bType == 0) {
        // Drop at player position
        m_pMapList[...]->bSetItem(playerX, playerY, pItem);
    }
    else if (bType == 1) {
        // Drop at crop position
        m_pMapList[...]->bSetItem(cropX, cropY, pItem);
    }

    // Notify nearby clients
    SendEventToNearClient_TypeB(MSGID_EVENT_COMMON, DEF_COMMONTYPE_ITEMDROP, ...);
}
```

---

## Skill Integration

### Skill Indices

| Index | Skill Name | Stat Limit | Gathering Use |
|-------|------------|------------|---------------|
| 0 | Mining | STR * 2 | Extracting minerals |
| 1 | Fishing | DEX * 2 | Catching fish |
| 2 | Farming | INT * 2 | Planting/harvesting crops |

### Skill Experience Formula

**Function:** `CGame::CalculateSSN_SkillIndex()`

**Location:** Game.cpp:25162-25290

```cpp
void CGame::CalculateSSN_SkillIndex(int iClientH, short sSkillIndex, int iValue)
{
    if (m_pClientList[iClientH]->m_cSkillMastery[sSkillIndex] == 0) return;

    // Add experience to skill
    iOldSSN = m_pClientList[iClientH]->m_iSkillSSN[sSkillIndex];
    m_pClientList[iClientH]->m_iSkillSSN[sSkillIndex] += iValue;

    // Get threshold for next level
    iSSNpoint = m_iSkillSSNpoint[m_pClientList[iClientH]->m_cSkillMastery[sSkillIndex]+1];

    // Check for level up (cap at 100)
    if ((m_pClientList[iClientH]->m_cSkillMastery[sSkillIndex] < 100) &&
        (m_pClientList[iClientH]->m_iSkillSSN[sSkillIndex] > iSSNpoint)) {

        m_pClientList[iClientH]->m_cSkillMastery[sSkillIndex]++;

        // Check stat limits
        switch (sSkillIndex) {
        case 0:  // Mining - STR * 2
            if (m_pClientList[iClientH]->m_cSkillMastery[0] >
                (m_pClientList[iClientH]->m_iStr * 2)) {
                m_pClientList[iClientH]->m_cSkillMastery[0]--;
                m_pClientList[iClientH]->m_iSkillSSN[0] = iOldSSN;
            }
            else m_pClientList[iClientH]->m_iSkillSSN[0] = 0;
            break;

        case 1:  // Fishing - DEX * 2
            if (m_pClientList[iClientH]->m_cSkillMastery[1] >
                (m_pClientList[iClientH]->m_iDex * 2)) {
                m_pClientList[iClientH]->m_cSkillMastery[1]--;
                m_pClientList[iClientH]->m_iSkillSSN[1] = iOldSSN;
            }
            else m_pClientList[iClientH]->m_iSkillSSN[1] = 0;
            break;

        case 2:  // Farming - INT * 2
            if (m_pClientList[iClientH]->m_cSkillMastery[2] >
                (m_pClientList[iClientH]->m_iInt * 2)) {
                m_pClientList[iClientH]->m_cSkillMastery[2]--;
                m_pClientList[iClientH]->m_iSkillSSN[2] = iOldSSN;
            }
            else m_pClientList[iClientH]->m_iSkillSSN[2] = 0;
            break;
        }

        if (m_pClientList[iClientH]->m_iSkillSSN[sSkillIndex] == 0) {
            bCheckTotalSkillMasteryPoints(iClientH, sSkillIndex);
            SendNotifyMsg(NULL, iClientH, DEF_NOTIFY_SKILL, sSkillIndex,
                m_pClientList[iClientH]->m_cSkillMastery[sSkillIndex], NULL, NULL);
        }
    }
}
```

### Clearing Skill States

**Function:** `CGame::ClearSkillUsingStatus()`

**Location:** Game.cpp:30228-30265

Called when player dies, moves, or takes other actions that interrupt gathering:

```cpp
void CGame::ClearSkillUsingStatus(int iClientH)
{
    // Clear all skill using flags
    for (i = 0; i < DEF_MAXSKILLTYPE; i++) {
        m_pClientList[iClientH]->m_bSkillUsingStatus[i] = FALSE;
        m_pClientList[iClientH]->m_iSkillUsingTimeID[i] = NULL;
    }

    // Special handling for fishing
    if (m_pClientList[iClientH]->m_iAllocatedFish != NULL) {
        if (m_pFish[m_pClientList[iClientH]->m_iAllocatedFish] != NULL)
            m_pFish[m_pClientList[iClientH]->m_iAllocatedFish]->m_sEngagingCount--;

        m_pClientList[iClientH]->m_iAllocatedFish = NULL;
        SendNotifyMsg(NULL, iClientH, DEF_NOTIFY_FISHCANCELED, NULL, NULL, NULL, NULL);
    }
}
```

---

## Dynamic Object Management

### Creation

**Function:** `CGame::iAddDynamicObjectList()`

**Location:** Game.cpp:24923-24990

```cpp
int CGame::iAddDynamicObjectList(short sOwner, char cOwnerType, short sType,
                                  char cMapIndex, short sX, short sY,
                                  DWORD dwLastTime, int iV1)
{
    // Check if position already has dynamic object
    m_pMapList[cMapIndex]->bGetDynamicObject(sX, sY, &sPreType, &dwRegisterTime);
    if (sPreType != NULL) return NULL;

    // Type-specific validation
    switch (sType) {
    case DEF_DYNAMICOBJECT_FISHOBJECT:
    case DEF_DYNAMICOBJECT_FISH:
        // Must be water tile
        if (m_pMapList[cMapIndex]->bGetIsWater(sX, sY) == FALSE)
            return NULL;
        break;

    case DEF_DYNAMICOBJECT_MINERAL1:
    case DEF_DYNAMICOBJECT_MINERAL2:
        // Must be moveable tile (will block movement after creation)
        if (m_pMapList[cMapIndex]->bGetMoveable(sX, sY) == FALSE)
            return NULL;
        m_pMapList[cMapIndex]->SetTempMoveAllowedFlag(sX, sY, FALSE);
        break;
    }

    // Find empty slot
    for (i = 1; i < DEF_MAXDYNAMICOBJECTS; i++)
    if (m_pDynamicObjectList[i] == NULL) {
        dwTime = timeGetTime();

        // Add random variance to duration
        if (dwLastTime != NULL)
            dwLastTime += (iDice(1,4)*1000);

        m_pDynamicObjectList[i] = new class CDynamicObject(sOwner, cOwnerType,
            sType, cMapIndex, sX, sY, dwTime, dwLastTime, iV1);
        m_pMapList[cMapIndex]->SetDynamicObject(i, sType, sX, sY, dwTime);

        // Notify nearby clients
        SendEventToNearClient_TypeB(MSGID_DYNAMICOBJECT, DEF_MSGTYPE_CONFIRM, ...);
        return i;
    }
    return NULL;
}
```

### Timeout Processing

**Function:** `CGame::DynamicObjectEffectProcessor()` (partial)

**Location:** Game.cpp:25015-25030

```cpp
// In CheckDynamicObjectList (called periodically):
if (m_pDynamicObjectList[i]->m_dwLastTime != 0) {
    if ((dwTime - m_pDynamicObjectList[i]->m_dwRegisterTime) >
        m_pDynamicObjectList[i]->m_dwLastTime) {

        // Object has expired
        switch (m_pDynamicObjectList[i]->m_sType) {
        case DEF_DYNAMICOBJECT_FISH:
        case DEF_DYNAMICOBJECT_FISHOBJECT:
            bDeleteFish(m_pDynamicObjectList[i]->m_sOwner, 2);  // Mode 2 = timeout
            break;
        }
    }
}
```

---

## Network Messages

### Fishing Messages (NetMessages.h)

```cpp
#define DEF_NOTIFY_EVENTFISHMODE    0x0B47  // Enter fishing mode (with item preview)
#define DEF_NOTIFY_FISHCHANCE       0x0B48  // Update catch chance percentage
#define DEF_NOTIFY_FISHSUCCESS      0x0B4A  // Successfully caught fish
#define DEF_NOTIFY_FISHFAIL         0x0B4B  // Failed to catch fish
#define DEF_NOTIFY_FISHCANCELED     0x0B4C  // Fishing canceled (fish despawned/moved)
```

### Farming Messages

```cpp
#define DEF_NOTIFY_NOMOREAGRICULTURE      // Map at crop limit
#define DEF_NOTIFY_AGRICULTURESKILLLIMIT  // Farming skill too low
#define DEF_NOTIFY_AGRICULTURENOAREA      // Not in farm area
```

### Dynamic Object Messages

```cpp
#define MSGID_DYNAMICOBJECT        // Dynamic object creation/deletion
#define DEF_MSGTYPE_CONFIRM        // Object created
#define DEF_MSGTYPE_REJECT         // Object removed
```

---

## Map Configuration

### Fish Point Configuration

In map info files, fish points are defined:

```
FishPoint = <index>
fish-x = <x coordinate>
fish-y = <y coordinate>
MaxFish = <maximum fish on map>
```

**Loading Code (Game.cpp:22041-22089):**
```cpp
if (memcmp(token, "FishPoint", 9) == 0) {
    iFishPointIndex = atoi(token);
    m_pMapList[iMapIndex]->m_iTotalFishPoint++;
}
else if (memcmp(token, "fish-x", 6) == 0) {
    m_pMapList[iMapIndex]->m_FishPointList[iFishPointIndex].x = atoi(token);
}
else if (memcmp(token, "fish-y", 6) == 0) {
    m_pMapList[iMapIndex]->m_FishPointList[iFishPointIndex].y = atoi(token);
}
else if (memcmp(token, "MaxFish", 7) == 0) {
    m_pMapList[iMapIndex]->m_iMaxFish = atoi(token);
}
```

### Mineral Point Configuration

```
MineralGenerator = <0/1>
MineralGeneratorLevel = <1-6>
MineralPoint = <index>
mineral-x = <x coordinate>
mineral-y = <y coordinate>
MaxMineral = <maximum minerals on map>
```

**Loading Code (Game.cpp:22128-22203):**
```cpp
if (memcmp(token, "MineralGenerator", 16) == 0) {
    m_pMapList[iMapIndex]->m_bMineralGenerator = (BOOL)atoi(token);
}
else if (memcmp(token, "MineralGeneratorLevel", 21) == 0) {
    m_pMapList[iMapIndex]->m_cMineralGeneratorLevel = atoi(token);
}
else if (memcmp(token, "MineralPoint", 12) == 0) {
    iMineralPointIndex = atoi(token);
    m_pMapList[iMapIndex]->m_iTotalMineralPoint++;
}
else if (memcmp(token, "mineral-x", 9) == 0) {
    m_pMapList[iMapIndex]->m_MineralPointList[iMineralPointIndex].x = atoi(token);
}
else if (memcmp(token, "mineral-y", 9) == 0) {
    m_pMapList[iMapIndex]->m_MineralPointList[iMineralPointIndex].y = atoi(token);
}
else if (memcmp(token, "MaxMineral", 10) == 0) {
    m_pMapList[iMapIndex]->m_iMaxMineral = atoi(token);
}
```

---

## All Related Functions

### Fishing Functions

| Function | Location | Purpose |
|----------|----------|---------|
| `iCreateFish()` | Game.cpp:33439 | Create a fish node |
| `bDeleteFish()` | Game.cpp:33490 | Delete a fish node |
| `iCheckFish()` | Game.cpp:33530 | Check/engage fish for player |
| `FishProcessor()` | Game.cpp:33576 | Process fishing chance updates |
| `FishGenerator()` | Game.cpp:33870 | Generate new fish on maps |
| `ReqGetFishThisTimeHandler()` | Game.cpp:33816 | Handle catch attempt |
| `AdminOrder_CreateFish()` | Game.cpp:33617 | GM command to spawn fish |
| `iCalculateUseSkillItemEffect()` | Game.cpp:30267 | Skill item effect (fishing rod) |
| `UseSkillHandler()` | Game.cpp:30361 | General skill use handler |
| `ClearSkillUsingStatus()` | Game.cpp:30228 | Clear active skill states |

### Mining Functions

| Function | Location | Purpose |
|----------|----------|---------|
| `iCreateMineral()` | Game.cpp:35322 | Create a mineral node |
| `bDeleteMineral()` | Game.cpp:35662 | Delete a mineral node |
| `MineralGenerator()` | Game.cpp:35302 | Generate new minerals on maps |
| `_CheckMiningAction()` | Game.cpp:35385 | Process mining attack action |

### Farming Functions

| Function | Location | Purpose |
|----------|----------|---------|
| `bPlantSeedBag()` | Game.cpp:51313 | Plant seeds to create crop |
| `_CheckFarmingAction()` | Game.cpp:51392 | Process crop harvesting |
| `bGetIsFarm()` | Map.cpp | Check if tile is farmable |
| `bAddCropsTotalSum()` | Map.cpp | Increment map crop count |
| `bRemoveCropsTotalSum()` | Map.cpp | Decrement map crop count |

### Skill Functions

| Function | Location | Purpose |
|----------|----------|---------|
| `CalculateSSN_SkillIndex()` | Game.cpp:25162 | Add skill experience |
| `CalculateSSN_ItemIndex()` | Game.cpp:25041 | Add skill XP from item use |
| `bCheckTotalSkillMasteryPoints()` | Game.cpp | Check skill cap |

### Dynamic Object Functions

| Function | Location | Purpose |
|----------|----------|---------|
| `iAddDynamicObjectList()` | Game.cpp:24923 | Create dynamic object |
| `CheckDynamicObjectList()` | Game.cpp | Process dynamic object timeouts |
| `DynamicObjectEffectProcessor()` | Game.cpp | Process dynamic object effects |

### Timer/Process Functions

| Timer Variable | Interval | Functions Called |
|---------------|----------|------------------|
| `m_dwFishTime` | 4 seconds | `FishProcessor()`, `FishGenerator()` |
| `m_dwMapSectorInfoTime` | 10 seconds | `MineralGenerator()` |

---

## Summary

The Helbreath gathering system is a complex interconnected set of mechanics that:

1. **Uses Dynamic Objects** to represent temporary world entities (fish spots, mineral nodes)
2. **Skill-based Success** with difficulty modifiers affecting chance
3. **Map Configuration** defines spawn points and limits
4. **Timed Regeneration** replenishes resources periodically
5. **Item Rewards** vary by node type and random rolls
6. **Experience Gains** for both player level and gathering skills

The system is deeply integrated with:
- Attack system (mining uses attack actions)
- NPC system (crops are special NPCs)
- Item system (rewards and required tools)
- Skill system (success rates and progression)
- Network system (client notifications)
