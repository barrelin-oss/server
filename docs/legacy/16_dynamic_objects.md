# Legacy Dynamic Objects System

**Document Version:** 1.0
**Applies To:** Legacy Helbreath Server (v2.03)
**Primary Files:** `DynamicObject.cpp/h`, `DynamicObjectID.h`, `Game.cpp`
**Estimated Lines:** ~600 lines across all files

---

## Table of Contents

1. [Overview](#overview)
2. [Data Structures](#data-structures)
3. [Object Types](#object-types)
4. [Core Functions](#core-functions)
5. [Effect Processing](#effect-processing)
6. [Fire Objects](#fire-objects)
7. [Ice Storm Objects](#ice-storm-objects)
8. [Poison Cloud Objects](#poison-cloud-objects)
9. [Spike Objects](#spike-objects)
10. [Gathering Objects](#gathering-objects)
11. [War Flag Objects](#war-flag-objects)
12. [Weather Interactions](#weather-interactions)
13. [Network Protocol](#network-protocol)
14. [Constants and Limits](#constants-and-limits)
15. [Related Systems](#related-systems)
16. [Implementation Notes](#implementation-notes)

---

## Overview

The Dynamic Objects system manages temporary world objects that exist independently of players and NPCs. These objects are placed on map tiles and can interact with entities that occupy or move through those tiles. The system handles:

1. **Damaging Effects** - Fire fields, ice storms, poison clouds, and spikes
2. **Resource Nodes** - Fishing spots and mineral deposits for gathering
3. **War Markers** - Faction flags for territory control during wars
4. **Visual Effects** - Crusade-related visual effects on structures

Dynamic objects are stored in a global array of 60,000 slots. Each object tracks its position, type, owner, creation time, and lifespan. The server processes active objects periodically to apply damage effects and remove expired objects.

---

## Data Structures

### CDynamicObject

**File:** `DynamicObject.h`

The core class representing a single dynamic object in the world.

```cpp
class CDynamicObject
{
public:
    CDynamicObject(short sOwner, char cOwnerType, short sType,
                   char cMapIndex, short sX, short sY,
                   DWORD dwRegisterTime, DWORD dwLastTime, int iV1);
    virtual ~CDynamicObject();

    short m_sOwner;              // Owner handle (player/NPC index, or fish/mineral index)
    char  m_cOwnerType;          // Owner type (DEF_OWNERTYPE_PLAYER, DEF_OWNERTYPE_NPC, etc.)

    short m_sType;               // Object type (see DynamicObjectID.h)
    char  m_cMapIndex;           // Map index where object exists
    short m_sX, m_sY;            // Tile coordinates

    DWORD m_dwRegisterTime;      // Timestamp when object was created (milliseconds)
    DWORD m_dwLastTime;          // Duration/lifespan in milliseconds (NULL = permanent)

    int   m_iCount;              // Counter for effect processing (fire spreading)
    int   m_iV1;                 // Additional data (spell power, mineral index, etc.)
};
```

### Tile Storage

**File:** `Tile.h`

Each map tile can hold one dynamic object reference:

```cpp
class CTile
{
    // ... other fields ...

    WORD  m_wDynamicObjectID;             // Index into m_pDynamicObjectList (0 = none)
    short m_sDynamicObjectType;           // Object type for quick lookup
    DWORD m_dwDynamicObjectRegisterTime;  // Creation timestamp for validation
};
```

### CGame Storage

**File:** `Game.h`

```cpp
class CGame {
    // Global dynamic object array (NULL = empty slot)
    class CDynamicObject * m_pDynamicObjectList[DEF_MAXDYNAMICOBJECTS];  // 60,000 slots
};
```

---

## Object Types

### Type Definitions

**File:** `DynamicObjectID.h`

```cpp
#define DEF_DYNAMICOBJECT_FIRE           1   // Fire field (deals 1d6 damage)
#define DEF_DYNAMICOBJECT_FISH           2   // Fishing node marker
#define DEF_DYNAMICOBJECT_FISHOBJECT     3   // Caught fish display
#define DEF_DYNAMICOBJECT_MINERAL1       4   // Mineral deposit (type 1)
#define DEF_DYNAMICOBJECT_MINERAL2       5   // Mineral deposit (type 2)
#define DEF_DYNAMICOBJECT_ARESDENFLAG1   6   // Aresden territory flag
#define DEF_DYNAMICOBJECT_ELVINEFLAG1    7   // Elvine territory flag
#define DEF_DYNAMICOBJECT_ICESTORM       8   // Ice storm field (3d3+5 damage + freeze)
#define DEF_DYNAMICOBJECT_SPIKE          9   // Spike trap (2d4 damage on movement)
#define DEF_DYNAMICOBJECT_PCLOUD_BEGIN   10  // Poison cloud (start animation)
#define DEF_DYNAMICOBJECT_PCLOUD_LOOP    11  // Poison cloud (loop animation)
#define DEF_DYNAMICOBJECT_PCLOUD_END     12  // Poison cloud (end animation)
#define DEF_DYNAMICOBJECT_FIRE2          13  // Crusade visual effect (no damage)
#define DEF_DYNAMICOBJECT_FIRE3          14  // Fire variant (deals 1d6 damage)
```

### Type Categories

| Category | Types | Purpose |
|----------|-------|---------|
| **Damaging Fields** | FIRE, FIRE3, ICESTORM, SPIKE, PCLOUD_* | Deal damage to entities in area |
| **Visual Only** | FIRE2 | Crusade meteor strike effect on structures (no damage) |
| **Gathering Nodes** | FISH, FISHOBJECT, MINERAL1, MINERAL2 | Resource collection points |
| **War Markers** | ARESDENFLAG1, ELVINEFLAG1 | Territory control during faction wars |

### Type Comparison

| Type | Damage | Area | Movement Trigger | Duration | Special |
|------|--------|------|------------------|----------|---------|
| FIRE | 1d6 | 3x3 | No | Timed | Spreads via coal |
| FIRE3 | 1d6 | 3x3 | No | Timed | Same as FIRE |
| FIRE2 | None | N/A | No | Timed | Visual only |
| ICESTORM | 3d3+5 | 5x5 | No | Timed | Applies freeze, extinguishes fire |
| SPIKE | 2d4 | 1x1 | Yes | Timed (spell config) | Triggers on step |
| PCLOUD_* | 1d6/1d8 | 3x3 | No | Timed | Applies poison status |

---

## Core Functions

### iAddDynamicObjectList

**File:** `Game.cpp` (Line ~24923)

Creates a new dynamic object at the specified location.

```cpp
int CGame::iAddDynamicObjectList(
    short sOwner,        // Owner handle (can be NULL)
    char cOwnerType,     // Owner type
    short sType,         // Object type (DEF_DYNAMICOBJECT_*)
    char cMapIndex,      // Map index
    short sX, short sY,  // Tile coordinates
    DWORD dwLastTime,    // Duration in ms (NULL = permanent)
    int iV1              // Additional data (spell power, index, etc.)
);
```

**Returns:** Object index (1-59999) on success, NULL (0) on failure

**Validation by Type:**

| Type | Validation |
|------|------------|
| FIRE, FIRE3 | Tile must be walkable; duration reduced by weather |
| FISH, FISHOBJECT | Tile must be water |
| MINERAL1, MINERAL2, flags | Tile must be moveable; blocks movement after placed |

**Implementation:**
```cpp
int CGame::iAddDynamicObjectList(short sOwner, char cOwnerType, short sType,
    char cMapIndex, short sX, short sY, DWORD dwLastTime, int iV1)
{
    // Check if tile already has a dynamic object
    short sPreType;
    DWORD dwRegisterTime;
    m_pMapList[cMapIndex]->bGetDynamicObject(sX, sY, &sPreType, &dwRegisterTime);
    if (sPreType != NULL) return NULL;

    // Type-specific validation
    switch (sType) {
    case DEF_DYNAMICOBJECT_FIRE3:
    case DEF_DYNAMICOBJECT_FIRE:
        // Must be walkable tile
        if (m_pMapList[cMapIndex]->bGetIsMoveAllowedTile(sX, sY) == FALSE)
            return NULL;
        // Weather reduces fire duration
        if (dwLastTime != NULL) {
            switch (m_pMapList[cMapIndex]->m_cWhetherStatus) {
            case 1: dwLastTime = dwLastTime - (dwLastTime / 2);       break;  // Light rain
            case 2: dwLastTime = (dwLastTime / 2) - (dwLastTime / 3); break;  // Medium rain
            case 3: dwLastTime = (dwLastTime / 3) - (dwLastTime / 4); break;  // Heavy rain
            }
            if (dwLastTime == NULL) dwLastTime = 1000;  // Minimum 1 second
        }
        break;

    case DEF_DYNAMICOBJECT_FISHOBJECT:
    case DEF_DYNAMICOBJECT_FISH:
        // Must be water tile
        if (m_pMapList[cMapIndex]->bGetIsWater(sX, sY) == FALSE)
            return NULL;
        break;

    case DEF_DYNAMICOBJECT_ARESDENFLAG1:
    case DEF_DYNAMICOBJECT_ELVINEFLAG1:
    case DEF_DYNAMICOBJECT_MINERAL1:
    case DEF_DYNAMICOBJECT_MINERAL2:
        // Must be moveable, then block movement
        if (m_pMapList[cMapIndex]->bGetMoveable(sX, sY) == FALSE)
            return NULL;
        m_pMapList[cMapIndex]->SetTempMoveAllowedFlag(sX, sY, FALSE);
        break;
    }

    // Find empty slot (starting from 1, not 0)
    for (int i = 1; i < DEF_MAXDYNAMICOBJECTS; i++) {
        if (m_pDynamicObjectList[i] == NULL) {
            DWORD dwTime = timeGetTime();

            // Add small random variance to duration (1-4 seconds)
            if (dwLastTime != NULL)
                dwLastTime += (iDice(1,4) * 1000);

            // Create the object
            m_pDynamicObjectList[i] = new CDynamicObject(sOwner, cOwnerType, sType,
                cMapIndex, sX, sY, dwTime, dwLastTime, iV1);

            // Register on map tile
            m_pMapList[cMapIndex]->SetDynamicObject(i, sType, sX, sY, dwTime);

            // Notify nearby clients
            SendEventToNearClient_TypeB(MSGID_DYNAMICOBJECT, DEF_MSGTYPE_CONFIRM,
                cMapIndex, sX, sY, sType, i, NULL);

            return i;
        }
    }
    return NULL;  // No empty slots
}
```

### CheckDynamicObjectList

**File:** `Game.cpp` (Line ~24981)

Processes object lifespans and removes expired objects. Called every game tick.

```cpp
void CGame::CheckDynamicObjectList()
{
    DWORD dwTime = timeGetTime();

    // First pass: Apply weather effects to fire duration
    for (int i = 1; i < DEF_MAXDYNAMICOBJECTS; i++) {
        if (m_pDynamicObjectList[i] != NULL && m_pDynamicObjectList[i]->m_dwLastTime != NULL) {
            switch (m_pDynamicObjectList[i]->m_sType) {
            case DEF_DYNAMICOBJECT_FIRE3:
            case DEF_DYNAMICOBJECT_FIRE:
                // Rain reduces remaining fire duration
                switch (m_pMapList[m_pDynamicObjectList[i]->m_cMapIndex]->m_cWhetherStatus) {
                case 1:  // Light rain
                case 2:  // Medium rain
                case 3:  // Heavy rain
                    m_pDynamicObjectList[i]->m_dwLastTime -=
                        (m_pDynamicObjectList[i]->m_dwLastTime / 10) *
                        m_pMapList[m_pDynamicObjectList[i]->m_cMapIndex]->m_cWhetherStatus;
                    break;
                }
                break;
            }
        }
    }

    // Second pass: Remove expired objects
    for (int i = 1; i < DEF_MAXDYNAMICOBJECTS; i++) {
        if (m_pDynamicObjectList[i] != NULL &&
            m_pDynamicObjectList[i]->m_dwLastTime != NULL &&
            (dwTime - m_pDynamicObjectList[i]->m_dwRegisterTime) >= m_pDynamicObjectList[i]->m_dwLastTime) {

            // Verify object still exists on tile (compare registration times)
            short sType;
            DWORD dwRegisterTime;
            m_pMapList[m_pDynamicObjectList[i]->m_cMapIndex]->bGetDynamicObject(
                m_pDynamicObjectList[i]->m_sX, m_pDynamicObjectList[i]->m_sY,
                &sType, &dwRegisterTime);

            if (dwRegisterTime == m_pDynamicObjectList[i]->m_dwRegisterTime) {
                // Notify clients of removal
                SendEventToNearClient_TypeB(MSGID_DYNAMICOBJECT, DEF_MSGTYPE_REJECT, ...);
                // Clear from map
                m_pMapList[...]->SetDynamicObject(NULL, NULL, sX, sY, dwTime);
            }

            // Type-specific cleanup
            switch (sType) {
            case DEF_DYNAMICOBJECT_FISHOBJECT:
            case DEF_DYNAMICOBJECT_FISH:
                bDeleteFish(m_pDynamicObjectList[i]->m_sOwner, 2);  // Timeout removal
                break;
            }

            // Delete from list
            delete m_pDynamicObjectList[i];
            m_pDynamicObjectList[i] = NULL;
        }
    }
}
```

### DynamicObjectEffectProcessor

**File:** `Game.cpp` (Line ~29841)

Applies damage and status effects from active dynamic objects. Called every game tick.

```cpp
void CGame::DynamicObjectEffectProcessor()
{
    DWORD dwTime = timeGetTime();

    for (int i = 0; i < DEF_MAXDYNAMICOBJECTS; i++) {
        if (m_pDynamicObjectList[i] != NULL) {
            switch (m_pDynamicObjectList[i]->m_sType) {
            case DEF_DYNAMICOBJECT_PCLOUD_BEGIN:
                // Process poison cloud effects (3x3 area)
                ProcessPoisonCloud(i, dwTime);
                break;

            case DEF_DYNAMICOBJECT_ICESTORM:
                // Process ice storm effects (5x5 area)
                ProcessIceStorm(i, dwTime);
                break;

            case DEF_DYNAMICOBJECT_FIRE3:
            case DEF_DYNAMICOBJECT_FIRE:
                // Process fire effects (3x3 area)
                ProcessFire(i, dwTime);
                break;
            }
        }
    }
}
```

---

## Effect Processing

### Processing Loop

The main game loop calls these functions every tick:

```cpp
// In main game loop (every tick, ~1 second intervals)
CheckDynamicObjectList();      // Remove expired objects
DynamicObjectEffectProcessor();  // Apply damage effects
```

### Damage Application

All damaging dynamic objects follow this pattern:

1. Iterate through affected tiles (varies by type)
2. Get owner (player/NPC) on each tile
3. Calculate damage
4. Apply damage (skip admins with level > 0)
5. Check for death
6. Apply secondary effects (poison, freeze, break hold)
7. Send notifications to affected players

### Immune NPCs

Certain special NPCs are immune to field damage:

| NPC Type | Name | Reason |
|----------|------|--------|
| 40 | Energy Shield Generator | War structure |
| 41 | Grand Magic Generator | War structure |
| 67 | McGaffin | Special NPC |
| 68 | Perry | Special NPC |
| 69 | Devlin | Special NPC |

### Action Limit Processing

NPC damage is filtered by action limit:

| Action Limit | Description | Takes Damage |
|--------------|-------------|--------------|
| 0 | Normal | Yes |
| 3 | Dummy/Training | Yes |
| 5 | Building/Structure | Yes |
| Others | Various | No |

---

## Fire Objects

### Types

| Type | Purpose | Damage |
|------|---------|--------|
| FIRE (1) | Magic spell fire field | 1d6 |
| FIRE3 (14) | Alternative fire variant | 1d6 |
| FIRE2 (13) | Crusade visual effect | None |

### Effect Processing

```cpp
case DEF_DYNAMICOBJECT_FIRE3:
case DEF_DYNAMICOBJECT_FIRE:
    // Fire spreading check (only on first tick)
    if (m_pDynamicObjectList[i]->m_iCount == 1) {
        CheckFireBluring(cMapIndex, sX, sY);
    }
    m_pDynamicObjectList[i]->m_iCount++;
    if (m_pDynamicObjectList[i]->m_iCount > 10)
        m_pDynamicObjectList[i]->m_iCount = 10;

    // Damage all entities in 3x3 area
    for (ix = sX-1; ix <= sX+1; ix++)
    for (iy = sY-1; iy <= sY+1; iy++) {
        // Get owner on tile
        GetOwner(&sOwnerH, &cOwnerType, ix, iy);
        if (sOwnerH != NULL) {
            iDamage = iDice(1, 6);  // 1d6 damage
            // Apply damage and effects...
        }

        // Also damage "playing dead" players
        GetDeadOwner(&sOwnerH, &cOwnerType, ix, iy);
        // ...

        // Ice storms near fire have reduced duration
        if (nearbyIceStorm) {
            iceStorm->m_dwLastTime -= iceStorm->m_dwLastTime / 10;
        }
    }
    break;
```

### Fire Spreading (CheckFireBluring)

Fire can ignite flammable items on adjacent tiles:

```cpp
void CGame::CheckFireBluring(char cMapIndex, int sX, int sY)
{
    for (ix = sX-1; ix <= sX+1; ix++)
    for (iy = sY-1; iy <= sY+1; iy++) {
        int iItemNum = m_pMapList[cMapIndex]->iCheckItem(ix, iy);

        switch (iItemNum) {
        case 355:  // Coal
            // Remove the coal item
            pItem = m_pMapList[cMapIndex]->pGetItem(ix, iy, ...);
            if (pItem != NULL) delete pItem;

            // Create new fire at that location
            iAddDynamicObjectList(NULL, NULL, DEF_DYNAMICOBJECT_FIRE,
                cMapIndex, ix, iy, 6000);  // 6 second duration

            // Notify clients of item removal
            SendEventToNearClient_TypeB(MSGID_EVENT_COMMON, DEF_COMMONTYPE_SETITEM, ...);
            break;
        }
    }
}
```

**Flammable Items:**
- Item 355: Coal (석탄) - Creates 6-second fire when ignited

### Special Effects

Fire damage breaks Hold-Person and Paralyze effects:

```cpp
if (m_pClientList[sOwnerH]->m_cMagicEffectStatus[DEF_MAGICTYPE_HOLDOBJECT] != 0) {
    SendNotifyMsg(NULL, sOwnerH, DEF_NOTIFY_MAGICEFFECTOFF,
        DEF_MAGICTYPE_HOLDOBJECT, ...);
    m_pClientList[sOwnerH]->m_cMagicEffectStatus[DEF_MAGICTYPE_HOLDOBJECT] = NULL;
    bRemoveFromDelayEventList(sOwnerH, DEF_OWNERTYPE_PLAYER, DEF_MAGICTYPE_HOLDOBJECT);
}
```

---

## Ice Storm Objects

### Effect Area

Ice storms affect a 5x5 area (larger than fire's 3x3):

```cpp
for (ix = sX-2; ix <= sX+2; ix++)
for (iy = sY-2; iy <= sY+2; iy++) {
    // Process each tile...
}
```

### Damage and Freeze

```cpp
case DEF_DYNAMICOBJECT_ICESTORM:
    iDamage = iDice(3, 3) + 5;  // 3d3+5 damage (8-14 range)

    // Apply damage
    m_pClientList[sOwnerH]->m_iHP -= iDamage;

    // Freeze effect (if not already frozen and fails resist check)
    if (bCheckResistingIceSuccess(1, sOwnerH, DEF_OWNERTYPE_PLAYER, m_iV1) == FALSE &&
        m_pClientList[sOwnerH]->m_cMagicEffectStatus[DEF_MAGICTYPE_ICE] == 0) {

        m_pClientList[sOwnerH]->m_cMagicEffectStatus[DEF_MAGICTYPE_ICE] = 1;
        SetIceFlag(sOwnerH, cOwnerType, TRUE);

        // Register expiry event (20 seconds)
        bRegisterDelayEvent(DEF_DELAYEVENTTYPE_MAGICRELEASE, DEF_MAGICTYPE_ICE,
            dwTime + (20*1000), sOwnerH, cOwnerType, ...);

        SendNotifyMsg(NULL, sOwnerH, DEF_NOTIFY_MAGICEFFECTON, DEF_MAGICTYPE_ICE, 1, ...);
    }
    break;
```

### Fire/Ice Interaction

Ice storms and fire objects reduce each other's duration when adjacent:

```cpp
// In ice storm processing:
m_pMapList[...]->bGetDynamicObject(ix, iy, &sType, &dwRegisterTime, &iIndex);
if ((sType == DEF_DYNAMICOBJECT_FIRE || sType == DEF_DYNAMICOBJECT_FIRE3) &&
    m_pDynamicObjectList[iIndex] != NULL) {
    // Reduce fire duration by 10%
    m_pDynamicObjectList[iIndex]->m_dwLastTime -= m_pDynamicObjectList[iIndex]->m_dwLastTime / 10;
}

// In fire processing:
if (sType == DEF_DYNAMICOBJECT_ICESTORM && m_pDynamicObjectList[iIndex] != NULL) {
    // Reduce ice storm duration by 10%
    m_pDynamicObjectList[iIndex]->m_dwLastTime -= m_pDynamicObjectList[iIndex]->m_dwLastTime / 10;
}
```

---

## Poison Cloud Objects

### Types

All three poison cloud types process identically (different visual states):

| Type | Purpose |
|------|---------|
| PCLOUD_BEGIN (10) | Cloud formation animation |
| PCLOUD_LOOP (11) | Sustained cloud animation |
| PCLOUD_END (12) | Cloud dissipation animation |

### Effect Processing

```cpp
case DEF_DYNAMICOBJECT_PCLOUD_BEGIN:
    // 3x3 effect area
    for (ix = sX-1; ix <= sX+1; ix++)
    for (iy = sY-1; iy <= sY+1; iy++) {
        // Get entity on tile
        GetOwner(&sOwnerH, &cOwnerType, ix, iy);

        if (sOwnerH != NULL) {
            // Damage based on spell power (m_iV1)
            if (m_pDynamicObjectList[i]->m_iV1 < 20)
                iDamage = iDice(1, 6);   // Low power: 1d6
            else
                iDamage = iDice(1, 8);   // High power: 1d8

            m_pClientList[sOwnerH]->m_iHP -= iDamage;

            // Poison status effect (if not already poisoned and fails resist)
            if (bCheckResistingMagicSuccess(1, sOwnerH, DEF_OWNERTYPE_PLAYER, 100) == FALSE &&
                m_pClientList[sOwnerH]->m_bIsPoisoned == FALSE) {

                m_pClientList[sOwnerH]->m_bIsPoisoned = TRUE;
                m_pClientList[sOwnerH]->m_iPoisonLevel = m_pDynamicObjectList[i]->m_iV1;
                m_pClientList[sOwnerH]->m_dwPoisonTime = dwTime;

                SetPoisonFlag(sOwnerH, cOwnerType, TRUE);
                SendNotifyMsg(NULL, sOwnerH, DEF_NOTIFY_MAGICEFFECTON,
                    DEF_MAGICTYPE_POISON, m_iPoisonLevel, ...);
            }
        }
    }
    break;
```

### Poison Level

The `m_iV1` field stores the spell power, which affects:
- Damage: 1d6 (power < 20) or 1d8 (power >= 20)
- Poison level applied to victims

---

## Spike Objects

### Unique Behavior

Unlike other damaging objects, spikes only trigger when an entity **moves onto** the tile, not continuously. They are processed in the movement handler, not `DynamicObjectEffectProcessor`.

### Trigger on Movement

**File:** `Game.cpp` (Movement handler, line ~1214)

```cpp
// After player moves to new position
if (sDOtype == DEF_DYNAMICOBJECT_SPIKE) {
    // Neutral players not in combat mode are immune
    if ((m_pClientList[iClientH]->m_bIsNeutral == TRUE) &&
        ((m_pClientList[iClientH]->m_sAppr2 & 0xF000) == 0)) {
        // No damage
    }
    else {
        iDamage = iDice(2, 4);  // 2d4 damage (2-8 range)

        if (m_pClientList[iClientH]->m_iAdminUserLevel == 0)
            m_pClientList[iClientH]->m_iHP -= iDamage;
    }
}
```

### Characteristics

| Property | Value |
|----------|-------|
| Damage | 2d4 (2-8) |
| Trigger | On movement only |
| Duration | Timed (from spell's `m_dwLastTime` config) |
| Area | Single tile |
| Neutral immunity | Yes (if not in combat mode) |

**Note:** Spikes cannot be destroyed early - they simply expire when their duration runs out via `CheckDynamicObjectList`.

---

## Gathering Objects

### Fish Objects

**Types:**
- `DEF_DYNAMICOBJECT_FISH` (2): Active fishing spot
- `DEF_DYNAMICOBJECT_FISHOBJECT` (3): Caught fish display

**Requirements:**
- Must be placed on water tiles
- Created by the Fishing system
- Links to `CFish` data via `m_sOwner`

**Lifecycle:**
```cpp
// Creation
int iDynamicHandle = iAddDynamicObjectList(
    fishIndex,                    // m_sOwner = fish data index
    NULL,                         // m_cOwnerType
    DEF_DYNAMICOBJECT_FISH,       // or FISHOBJECT
    cMapIndex, sX, sY,
    dwLastTime                    // Duration
);

m_pFish[fishIndex]->m_sDynamicObjectHandle = iDynamicHandle;

// Deletion (when caught or expired)
bDeleteFish(m_pDynamicObjectList[i]->m_sOwner, 2);  // Reason: timeout
```

### Mineral Objects

**Types:**
- `DEF_DYNAMICOBJECT_MINERAL1` (4): Standard mineral deposit
- `DEF_DYNAMICOBJECT_MINERAL2` (5): Alternative mineral type

**Requirements:**
- Must be placed on moveable tiles
- Blocks movement after placement
- Links to `CMineral` data via `m_iV1`

**Mining Interaction:**
```cpp
// Get mineral data from dynamic object
int iMineralIndex = m_pDynamicObjectList[iDynamicIndex]->m_iV1;

// Process mining attempt
iSkillLevel -= m_pMineral[iMineralIndex]->m_iDifficulty;

// On successful extraction
m_pMineral[iMineralIndex]->m_iRemain--;
if (m_pMineral[iMineralIndex]->m_iRemain <= 0) {
    bDeleteMineral(iMineralIndex);
    delete m_pDynamicObjectList[iDynamicIndex];
    m_pDynamicObjectList[iDynamicIndex] = NULL;
}
```

---

## War Flag Objects

### Types

| Type | Faction |
|------|---------|
| ARESDENFLAG1 (6) | Aresden |
| ELVINEFLAG1 (7) | Elvine |

### OccupyFlag Integration

War flags link to the territory control system:

```cpp
// Create flag dynamic object
int iDynamicObjectIndex;
switch (iSide) {
case 1:
    iDynamicObjectIndex = iAddDynamicObjectList(NULL, NULL,
        DEF_DYNAMICOBJECT_ARESDENFLAG1, cMapIndex, dX, dY, NULL, NULL);
    break;
case 2:
    iDynamicObjectIndex = iAddDynamicObjectList(NULL, NULL,
        DEF_DYNAMICOBJECT_ELVINEFLAG1, cMapIndex, dX, dY, NULL, NULL);
    break;
}

// Register with territory control system
int iIndex = m_pMapList[cMapIndex]->iRegisterOccupyFlag(dX, dY, iSide, iEKNum, iDynamicObjectIndex);
```

### COccupyFlag Structure

```cpp
class COccupyFlag
{
public:
    char m_cSide;                   // Faction (1=Aresden, 2=Elvine)
    int  m_iEKCount;                // Enemy kill count
    int  m_sX, m_sY;                // Position
    int  m_iDynamicObjectIndex;     // Link to dynamic object
};
```

### Lifecycle

- **Permanent**: Flags have `m_dwLastTime = NULL` (no expiration)
- **Movement Blocking**: Blocks tile movement after placement
- **War Events**: Removed when territory changes hands or war ends

---

## Weather Interactions

### Weather States

```cpp
m_cWhetherStatus:
0 = Clear
1 = Light rain
2 = Medium rain
3 = Heavy rain
```

### Fire Duration Reduction

Rain reduces fire duration at creation and during existence:

**At Creation:**
```cpp
switch (m_pMapList[cMapIndex]->m_cWhetherStatus) {
case 1: dwLastTime = dwLastTime - (dwLastTime / 2);       break;  // 50% reduction
case 2: dwLastTime = (dwLastTime / 2) - (dwLastTime / 3); break;  // ~17% of original
case 3: dwLastTime = (dwLastTime / 3) - (dwLastTime / 4); break;  // ~8% of original
}
```

**During Existence (CheckDynamicObjectList):**
```cpp
// Rain reduces remaining duration each tick
m_pDynamicObjectList[i]->m_dwLastTime -=
    (m_pDynamicObjectList[i]->m_dwLastTime / 10) * m_cWhetherStatus;
```

| Weather | Initial Reduction | Per-Tick Reduction |
|---------|-------------------|-------------------|
| Clear | 0% | 0% |
| Light rain | 50% | 10% |
| Medium rain | ~83% | 20% |
| Heavy rain | ~92% | 30% |

---

## Network Protocol

### Message Types

```cpp
MSGID_DYNAMICOBJECT  // Dynamic object create/destroy notification
```

### Message Subtypes

| Subtype | Purpose |
|---------|---------|
| DEF_MSGTYPE_CONFIRM | Object created |
| DEF_MSGTYPE_REJECT | Object destroyed |

### Create Notification

```cpp
SendEventToNearClient_TypeB(
    MSGID_DYNAMICOBJECT,
    DEF_MSGTYPE_CONFIRM,
    cMapIndex,
    sX, sY,
    sType,      // Object type
    iIndex,     // Object index
    NULL
);
```

### Destroy Notification

```cpp
SendEventToNearClient_TypeB(
    MSGID_DYNAMICOBJECT,
    DEF_MSGTYPE_REJECT,
    cMapIndex,
    sX, sY,
    sType,
    iIndex,
    NULL
);
```

### Tile Data Inclusion

When sending tile data to clients (map visibility), dynamic objects are included:

```cpp
if (pTile->m_sDynamicObjectType != NULL) {
    ucHeader = ucHeader | 0x08;  // Set dynamic object flag

    // Write object data
    *wp = pTile->m_wDynamicObjectID;
    *sp = pTile->m_sDynamicObjectType;
}
```

---

## Constants and Limits

### Array Limits

| Constant | Value | Description |
|----------|-------|-------------|
| DEF_MAXDYNAMICOBJECTS | 60,000 | Maximum simultaneous objects |

### Owner Types

```cpp
#define DEF_OWNERTYPE_PLAYER           1  // Player character
#define DEF_OWNERTYPE_NPC              2  // NPC/Monster
#define DEF_OWNERTYPE_PLAYER_INDIRECT  3  // Indirect player reference (spells)
```

### Timing

| Effect | Damage Interval |
|--------|-----------------|
| Fire | Every game tick (~1 second) |
| Ice Storm | Every game tick |
| Poison Cloud | Every game tick |
| Spike | On movement only |

### Duration Examples

| Object | Typical Duration |
|--------|------------------|
| Fire (spell) | Variable + 1-4 seconds random |
| Fire (from coal) | 6 seconds |
| Fire3 (skill) | (1d7+3) * 1000 ms |
| Ice Storm | Spell-dependent |
| Poison Cloud | Spell-dependent |
| Spike | Spell-dependent (from magic config) |
| Flags | Permanent |
| Minerals | Permanent (until depleted) |
| Fish | Timed (fishing mechanics) |

---

## Related Systems

### Integrated Systems

| System | Relationship |
|--------|--------------|
| **Magic System** | Creates fire, ice storm, poison cloud, spike objects |
| **Combat System** | Damage effects from fields |
| **Status Effects** | Poison, freeze applied by objects |
| **Gathering System** | Fish and mineral node visualization |
| **War System** | Territory flags for faction control |
| **Delayed Events** | Freeze duration expiry |
| **Weather System** | Affects fire duration |

### Key Functions in Other Files

| File | Function | Purpose |
|------|----------|---------|
| `Map.cpp` | SetDynamicObject | Register object on tile |
| `Map.cpp` | bGetDynamicObject | Query tile for object |
| `Map.cpp` | iRegisterOccupyFlag | Link flag to territory system |
| `Magic.cpp` | Spell handlers | Create spell-based objects |

---

## Implementation Notes

### Index 0 Reserved

The dynamic object array starts iteration from index 1, not 0:

```cpp
for (i = 1; i < DEF_MAXDYNAMICOBJECTS; i++)
```

Index 0 is effectively reserved/unused.

### Registration Time Validation

When removing objects, the code validates the registration time matches to prevent race conditions:

```cpp
if (dwRegisterTime == m_pDynamicObjectList[i]->m_dwRegisterTime) {
    // Safe to remove - this is the same object
}
```

### Memory Management

Objects are manually allocated and deleted:

```cpp
// Creation
m_pDynamicObjectList[i] = new CDynamicObject(...);

// Deletion
delete m_pDynamicObjectList[i];
m_pDynamicObjectList[i] = NULL;
```

### Tile Blocking

Mineral deposits and war flags block movement:

```cpp
// At creation
m_pMapList[cMapIndex]->SetTempMoveAllowedFlag(sX, sY, FALSE);

// At deletion (minerals only)
m_pMapList[...]->SetTempMoveAllowedFlag(sX, sY, TRUE);
```

### Admin Immunity

Admin users (level > 0) are immune to dynamic object damage:

```cpp
if (m_pClientList[sOwnerH]->m_iAdminUserLevel == 0)
    m_pClientList[sOwnerH]->m_iHP -= iDamage;
```

### Dead Player Damage

Fire and ice storms can damage players who are "playing dead" (feigning death):

```cpp
m_pMapList[...]->GetDeadOwner(&sOwnerH, &cOwnerType, ix, iy);
if (cOwnerType == DEF_OWNERTYPE_PLAYER && m_pClientList[sOwnerH] != NULL &&
    m_pClientList[sOwnerH]->m_iHP > 0) {
    // Apply reduced damage to "dead" player
    iDamage = iDice(3, 2);  // or iDice(1, 6) for fire
    m_pClientList[sOwnerH]->m_iHP -= iDamage;
}
```

### Break Hold Effects

Fire damage breaks Hold-Person and Paralyze effects, providing tactical value for fire spells.

### Fire-Ice Mutual Destruction

Fire and ice storm objects reduce each other's duration when in adjacent tiles, creating tactical interactions between opposing elements.

### Processing Order

1. `CheckDynamicObjectList()` - Apply weather effects, remove expired objects
2. `DynamicObjectEffectProcessor()` - Apply damage and status effects

This order ensures expired objects don't deal damage on their final tick.
