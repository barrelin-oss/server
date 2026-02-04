# Legacy Special Events System

**Document Version:** 1.0
**Applies To:** Legacy Helbreath Server (v2.03)
**Primary Files:** `Game.cpp`, `Map.h`, `Client.h`
**Estimated Lines:** ~700 lines across all files

---

## Table of Contents

1. [Overview](#overview)
2. [System Architecture](#system-architecture)
3. [Special Event Handler](#special-event-handler)
4. [Mob Spawn Events](#mob-spawn-events)
5. [Energy Sphere System](#energy-sphere-system)
6. [Weather System](#weather-system)
7. [Day/Night Cycle](#daynight-cycle)
8. [Anniversary Events](#anniversary-events)
9. [Event Timing](#event-timing)
10. [Map Configuration](#map-configuration)
11. [Client Notifications](#client-notifications)
12. [Admin Commands](#admin-commands)
13. [Constants and Limits](#constants-and-limits)
14. [Related Systems](#related-systems)
15. [Implementation Notes](#implementation-notes)

---

## Overview

The Special Events system manages periodic server-wide events that add variety and engagement to gameplay. Unlike the Delayed Events system (which handles per-entity timers), Special Events operate on fixed server cycles affecting all maps and players simultaneously.

### Core Event Categories

| Category | Trigger | Effect |
|----------|---------|--------|
| **Mob Spawning Events** | 10-minute cycle | Extra or rare NPCs spawn across maps |
| **Energy Spheres** | Continuous (3-second check) | Capture-the-flag style faction objective |
| **Weather Changes** | 20-second cycle | Dynamic rain/snow affecting magic |
| **Day/Night Cycle** | Real-time minutes | Visual and gameplay changes |
| **Anniversary Events** | Account flag | One-time commemorative items |

---

## System Architecture

### Global State Variables

**File:** `Game.h`

```cpp
class CGame {
    // Timing
    DWORD m_dwSpecialEventTime;     // Last special event check
    DWORD m_dwWhetherTime;          // Last weather check
    DWORD m_dwGameTime3;            // 3-second timer (energy spheres)

    // Event State
    BOOL  m_bIsSpecialEventTime;    // Special mob spawning active
    char  m_cSpecialEventType;      // 1=mob swarm, 2=rare creatures
    BOOL  m_cDayOrNight;            // 1=day, 2=night

    // Population threshold
    int   m_iTotalGameServerClients; // Must be >= 500 for energy spheres
};
```

### Per-Map State

**File:** `Map.h`

```cpp
class CMap {
    // Weather
    char  m_cWhetherStatus;          // 0=clear, 1-3=rain, 4-6=snow, 7-9=storm
    DWORD m_dwWhetherStartTime;      // When weather started
    DWORD m_dwWhetherLastTime;       // Duration in ms

    // Map Modes
    BOOL  m_bIsFixedDayMode;         // Always daytime (no night cycle)
    BOOL  m_bIsSnowEnabled;          // Snow instead of rain

    // Energy Sphere Data
    struct {
        char  cType;
        short sX, sY;
    } m_stEnergySphereCreationList[DEF_MAXENERGYSPHERES];  // 10 spawn points

    struct {
        char  cResult;
        short aresdenX, aresdenY;
        short elvineX, elvineY;
    } m_stEnergySphereGoalList[DEF_MAXENERGYSPHERES];      // 10 goal points

    int  m_iTotalEnergySphereCreationPoint;
    int  m_iTotalEnergySphereGoalPoint;
    int  m_iCurEnergySphereGoalPointIndex;  // -1 = no active sphere
    BOOL m_bIsEnergySphereAutoCreation;
};
```

---

## Special Event Handler

### Function: SpecialEventHandler()

**File:** `Game.cpp:34263`

Called every 3 seconds (as part of `m_dwGameTime3` timer), but only triggers events on a 10-minute (600,000ms / 5-minute in some versions) cycle.

```cpp
void CGame::SpecialEventHandler()
{
    DWORD dwTime = timeGetTime();

    // DEF_SPECIALEVENTTIME = 300000 (5 min) or 600000 (10 min)
    if ((dwTime - m_dwSpecialEventTime) < DEF_SPECIALEVENTTIME) return;

    m_dwSpecialEventTime  = dwTime;
    m_bIsSpecialEventTime = TRUE;

    // Random event type selection (1/180 chance for type 2)
    switch (iDice(1, 180)) {
    case 98:
        m_cSpecialEventType = 2;  // Rare creatures (Demon/Unicorn)
        break;
    default:
        m_cSpecialEventType = 1;  // Mass mob spawning
        break;
    }
}
```

### Event Type Probabilities

| Type | Roll Required | Probability | Description |
|------|---------------|-------------|-------------|
| 1 | 1-97, 99-180 | 179/180 (99.4%) | Mass mob spawning event |
| 2 | 98 only | 1/180 (0.6%) | Rare creature spawning |

---

## Mob Spawn Events

When `m_bIsSpecialEventTime == TRUE`, the MobGenerator checks for special spawns.

### Activation Check

**File:** `Game.cpp:25913-25914`

```cpp
// During normal mob generation...
// 10% chance to activate special spawning when event is active
if ((m_bIsSpecialEventTime == TRUE) && (iDice(1, 10) == 3))
    bIsSpecialEvent = TRUE;
```

### Event Type 1: Mass Mob Spawning

**File:** `Game.cpp:25917-25948`

Spawns 12 additional mobs of the same type at the map's maximum player position.

```cpp
case 1:
    if (m_pMapList[i]->m_iMaxPx != 0) {
        pX = m_pMapList[i]->m_iMaxPx * 20 + 10;
        pY = m_pMapList[i]->m_iMaxPy * 20 + 10;

        // During Crusade mode, spawn faction defenders
        if (m_bIsCrusadeMode == TRUE) {
            if (strcmp(m_pMapList[i]->m_cName, "aresden") == 0)
                // Spawn Elvine attackers
                switch(iDice(1, 6)) {
                case 1: iResult = 20; break;  // YB-Elvine
                case 2: iResult = 53; break;  // YW-Elvine
                case 3: iResult = 55; break;  // YY-Elvine
                case 4: iResult = 57; break;  // XB-Elvine
                case 5: iResult = 59; break;  // XW-Elvine
                case 6: iResult = 61; break;  // XY-Elvine
                }
            else if (strcmp(m_pMapList[i]->m_cName, "elvine") == 0)
                // Spawn Aresden attackers
                switch(iDice(1, 6)) {
                case 1: iResult = 19; break;  // YB-Aresden
                case 2: iResult = 52; break;  // YW-Aresden
                case 3: iResult = 54; break;  // YY-Aresden
                case 4: iResult = 56; break;  // XB-Aresden
                case 5: iResult = 58; break;  // XW-Aresden
                case 6: iResult = 60; break;  // XY-Aresden
                }
        }
    }
    break;
```

#### Type 1 Spawn Configuration

| Condition | Mob Count | Location |
|-----------|-----------|----------|
| Normal maps | 12 mobs | MaxPx*20+10, MaxPy*20+10 |
| Crusade - Aresden | 12 faction NPCs | Same |
| Crusade - Elvine | 12 faction NPCs | Same |

#### Excluded NPCs from Mass Spawn

**File:** `Game.cpp:26169-26170**

Some powerful NPCs don't get the 12x multiplier:
- Hellclaw (35)
- Wyvern (36)
- Fire-Wyvern (37)
- Tigerworm (49)
- Abaddon (51)
- Liche (15)
- Demon (16)
- Gagoyle (21)

### Event Type 2: Rare Creature Spawning

**File:** `Game.cpp:25950-25966`

Spawns Demons or Unicorns based on location.

```cpp
case 2:
    if (iDice(1, 3) == 2) {
        // Safe areas: lower spawn chance, prefer Skeleton (common)
        if ((memcmp(m_pMapList[i]->m_cLocationName, "aresden", 7) == 0) ||
            (memcmp(m_pMapList[i]->m_cLocationName, "middled1n", 9) == 0) ||
            (memcmp(m_pMapList[i]->m_cLocationName, "arefarm", 7) == 0) ||
            (memcmp(m_pMapList[i]->m_cLocationName, "elvfarm", 7) == 0) ||
            (memcmp(m_pMapList[i]->m_cLocationName, "elvine", 6) == 0)) {
            if (iDice(1, 30) == 5)
                iResult = 16;  // Demon (1/30 chance)
            else
                iResult = 5;   // Skeleton (default)
        }
        else
            iResult = 16;      // Demon (wilderness)
    }
    else
        iResult = 17;          // Unicorn

    m_bIsSpecialEventTime = FALSE;  // One-shot, ends immediately
    break;
```

#### Type 2 Spawn Table

| Location | 1/3 Chance | 2/3 Chance |
|----------|------------|------------|
| Cities/Farms | 1/30 Demon, else Skeleton | Unicorn |
| Wilderness | Demon | Unicorn |

### Map Location Restrictions

**File:** `Game.cpp:26177-26184**

Type 2 events don't spawn in safe zones:

```cpp
case 2:
    if ((memcmp(m_pMapList[i]->m_cLocationName, "aresden", 7) == 0) ||
        (memcmp(m_pMapList[i]->m_cLocationName, "elvine",  6) == 0) ||
        (memcmp(m_pMapList[i]->m_cLocationName, "elvfarm",  7) == 0) ||
        (memcmp(m_pMapList[i]->m_cLocationName, "arefarm",  7) == 0)) {
        iTotalMob = 0;  // No spawns in safe areas
    }
    break;
```

### Complete NPC Spawn Table

**File:** `Game.cpp:25972-26034**

| Result ID | NPC Name | NPC ID | SA Prob | SA Kind |
|-----------|----------|--------|---------|---------|
| 1 | Slime | 10 | 5% | 1 |
| 2 | Giant-Ant | 16 | 10% | 2 |
| 3 | Orc | 14 | 15% | 1 |
| 4 | Zombie | 18 | 15% | 3 |
| 5 | Skeleton | 11 | 35% | 8 |
| 6 | Orc-Mage | 14 | 30% | 7 |
| 7 | Scorpion | 17 | 15% | 3 |
| 8 | Stone-Golem | 12 | 25% | 5 |
| 9 | Cyclops | 13 | 35% | 8 |
| 10 | Amphis | 22 | 20% | 3 |
| 11 | Clay-Golem | 23 | 20% | 5 |
| 12 | Troll | 28 | 25% | 3 |
| 13 | Orge | 29 | 25% | 1 |
| 14 | Hellbound | 27 | 25% | 8 |
| 15 | Liche | 30 | 30% | 8 |
| 16 | Demon | 31 | 20% | 8 |
| 17 | Unicorn | 32 | 35% | 7 |
| 18 | WereWolf | 33 | 25% | 1 |
| 19 | YB-Aresden | -1 | 15% | 1 |
| 20 | YB-Elvine | -1 | 15% | 1 |
| 21 | Gagoyle | 52 | 20% | 8 |
| 22 | Beholder | 53 | 20% | 5 |
| 23 | Dark-Elf | 54 | 20% | 3 |
| 24 | Rabbit | -1 | 5% | 1 |
| 25 | Cat | -1 | 10% | 2 |
| 26 | Giant-Frog | 57 | 10% | 2 |
| 27 | Mountain-Giant | 58 | 25% | 1 |
| 28 | Ettin | 59 | 20% | 8 |
| 29 | Cannibal-Plant | 60 | 20% | 5 |
| 30 | Rudolph | -1 | 20% | 5 |
| 31 | Ice-Golem | 65 | 35% | 8 |
| 32 | DireBoar | 62 | 20% | 5 |
| 33 | Frost | 63 | 30% | 8 |
| 34 | Stalker | 48 | 20% | 1 |
| 35 | Hellclaw | 49 | 20% | 1 |
| 36 | Wyvern | 66 | 20% | 1 |
| 37-51 | Various rare | -1 | 20% | 1 |
| 52-61 | Faction NPCs | -1 | 15% | 1 |

---

## Energy Sphere System

### Overview

Energy Spheres are special NPCs that spawn in the Middleland map. Players from each faction compete to escort the sphere to their goal point, earning contribution points.

### Spawn Conditions

**File:** `Game.cpp:40950-41010`

```cpp
void CGame::EnergySphereProcessor(BOOL bIsAdminCreate, int iClientH)
{
    if (bIsAdminCreate != TRUE) {
        // Natural spawning conditions:
        if (m_iMiddlelandMapIndex < 0) return;
        if (m_pMapList[m_iMiddlelandMapIndex] == NULL) return;

        // 1/2000 chance every 3 seconds
        if (iDice(1, 2000) != 123) return;

        // Requires 500+ players online
        if (m_iTotalGameServerClients < 500) return;

        // Only one sphere at a time
        if (m_pMapList[m_iMiddlelandMapIndex]->m_iCurEnergySphereGoalPointIndex >= 0)
            return;

        // Select random spawn point from configured list
        iCIndex = iDice(1, m_pMapList[m_iMiddlelandMapIndex]->m_iTotalEnergySphereCreationPoint);

        // Create the Energy-Sphere NPC
        bCreateNewNpc("Energy-Sphere", cName_Internal, mapName,
                      (rand() % 5), 0, DEF_MOVETYPE_RANDOM, &pX, &pY, ...);

        // Select random goal point
        iTemp = iDice(1, m_pMapList[m_iMiddlelandMapIndex]->m_iTotalEnergySphereGoalPoint);
        m_pMapList[m_iMiddlelandMapIndex]->m_iCurEnergySphereGoalPointIndex = iTemp;

        // Notify all clients
        SendNotifyMsg(NULL, i, DEF_NOTIFY_ENERGYSPHERECREATED, pX, pY, NULL, NULL);
    }
}
```

### Spawn Probability

| Check Interval | Roll | Required Players | Result |
|----------------|------|------------------|--------|
| Every 3 seconds | 1/2000 | 500+ | Sphere spawns |

Expected time between natural spawns: ~100 minutes (2000 * 3 seconds)

### Goal Mechanics

**File:** `Game.cpp:40803-40877`

When an Energy Sphere NPC is attacked near a goal point, the goal is checked:

```cpp
BOOL CGame::bCheckEnergySphereDestination(int iNpcH, short sAttackerH, char cAttackerType)
{
    // Check if sphere is within 2 tiles of Aresden goal
    if ((sX >= dX-2) && (sX <= dX+2) && (sY >= dY-2) && (sY <= dY+2)) {
        if (m_pClientList[sAttackerH]->m_cSide == 1) {  // Aresden
            m_pClientList[sAttackerH]->m_iContribution += 5;  // Own goal
        } else {
            m_pClientList[sAttackerH]->m_iContribution -= 10; // Enemy goal
        }

        // Notify all clients
        SendNotifyMsg(NULL, i, DEF_NOTIFY_ENERGYSPHEREGOALIN, cResult, side, ...);
        return TRUE;
    }

    // Similar check for Elvine goal...
}
```

### Contribution Rewards

| Action | Side Match | Contribution Change |
|--------|------------|---------------------|
| Score in own goal | Yes | +5 |
| Score in enemy goal | No | -10 |

### Map Configuration

**MapInfo file entries:**

```
energy-sphere-creation-point = <index> <type> <x> <y>
energy-sphere-goal = <index> <result> <aresden_x> <aresden_y> <elvine_x> <elvine_y>
energy-sphere-auto-creation
```

---

## Weather System

### Function: WhetherProcessor()

**File:** `Game.cpp:34001-34038`

Runs every 20 seconds, managing weather transitions per map.

```cpp
void CGame::WhetherProcessor()
{
    dwTime = timeGetTime();

    for (i = 0; i < DEF_MAXMAPS; i++) {
        if ((m_pMapList[i] != NULL) && (m_pMapList[i]->m_bIsFixedDayMode == FALSE)) {
            cPrevMode = m_pMapList[i]->m_cWhetherStatus;

            // Check if current weather should end
            if (m_pMapList[i]->m_cWhetherStatus != NULL) {
                if ((dwTime - m_pMapList[i]->m_dwWhetherStartTime) > m_pMapList[i]->m_dwWhetherLastTime)
                    m_pMapList[i]->m_cWhetherStatus = NULL;  // Clear weather
            }
            else {
                // 1/300 chance to start weather each 20 seconds
                if (iDice(1, 300) == 13) {
                    m_pMapList[i]->m_cWhetherStatus = iDice(1, 3);  // Rain types 1-3
                    m_pMapList[i]->m_dwWhetherStartTime = dwTime;
                    m_pMapList[i]->m_dwWhetherLastTime = 60000*3 + 60000*iDice(1, 7);
                    // Duration: 3-10 minutes
                }
            }

            // Override for snow-enabled maps
            if (m_pMapList[i]->m_bIsSnowEnabled == TRUE) {
                m_pMapList[i]->m_cWhetherStatus = iDice(1, 3) + 3;  // Snow types 4-6
                m_pMapList[i]->m_dwWhetherStartTime = dwTime;
                m_pMapList[i]->m_dwWhetherLastTime = 60000*3 + 60000*iDice(1, 7);
            }

            // Notify clients of changes
            if (cPrevMode != m_pMapList[i]->m_cWhetherStatus) {
                for (j = 1; j < DEF_MAXCLIENTS; j++)
                    if (m_pClientList[j]->m_cMapIndex == i)
                        SendNotifyMsg(NULL, j, DEF_NOTIFY_WHETHERCHANGE, ...);
            }
        }
    }
}
```

### Weather Types

| Code | Type | Effect on Magic |
|------|------|-----------------|
| 0 | Clear | No modifier |
| 1-3 | Light/Medium/Heavy Rain | Water +1, Fire -1 |
| 4-6 | Light/Medium/Heavy Snow | Ice +1, Fire -1 |
| 7-9 | Storm | Special effects |

### Weather Duration

- **Minimum:** 3 minutes (180,000 ms)
- **Maximum:** 10 minutes (600,000 ms)
- **Formula:** `60000*3 + 60000*iDice(1,7)` = 180-600 seconds

### Weather Probability

- Check interval: 20 seconds
- Chance per check: 1/300
- Expected time between weather: ~100 minutes

### Magic Weather Bonuses

**File:** `Game.cpp:34051-34069`

```cpp
int CGame::iGetWhetherMagicBonusEffect(short sType, char cWheatherStatus)
{
    switch (cWheatherStatus) {
    case 1: case 2: case 3:  // Rain
        switch (sType) {
        case 10: case 37: case 43: case 51:  // Water spells
            return 1;   // +1 damage
        case 20:        // Fire spells
            return -1;  // -1 damage
        }
        break;
    // Similar for snow...
    }
    return 0;
}
```

---

## Day/Night Cycle

### Time Calculation

**File:** `Game.cpp:722-724, 32542-32556`

Uses real-world minutes within each hour:

```cpp
SYSTEMTIME SysTime;
GetLocalTime(&SysTime);

if (SysTime.wMinute >= DEF_NIGHTTIME)   // DEF_NIGHTTIME = 40
    m_cDayOrNight = 2;  // Night (minutes 40-59)
else
    m_cDayOrNight = 1;  // Day (minutes 0-39)
```

### Cycle Timing

| Period | Minutes | Duration |
|--------|---------|----------|
| Day | 0-39 | 40 minutes |
| Night | 40-59 | 20 minutes |

### Map Overrides

Maps with `m_bIsFixedDayMode = TRUE` ignore the day/night cycle and always report daytime. This is used for:
- Indoor areas (dungeons)
- Special event maps
- Areas where weather shouldn't apply

### Client Notifications

```cpp
if (cPrevMode != m_cDayOrNight) {
    for (i = 1; i < DEF_MAXCLIENTS; i++)
        if ((m_pClientList[i] != NULL) &&
            (m_pClientList[i]->m_bIsInitComplete == TRUE) &&
            (m_pMapList[m_pClientList[i]->m_cMapIndex]->m_bIsFixedDayMode == FALSE))
            SendNotifyMsg(NULL, i, DEF_NOTIFY_TIMECHANGE, m_cDayOrNight, NULL, NULL, NULL);
}
```

---

## Anniversary Events

### MemorialRing Distribution

**File:** `Game.cpp:39683-39728`

A one-time event commemorating the game's first anniversary (August 1st, 2000). Players with the special event flag received a unique MemorialRing.

```cpp
void CGame::CheckSpecialEvent(int iClientH)
{
    // Event ID 200081 = August 1, 2000 anniversary
    if (m_pClientList[iClientH]->m_iSpecialEventID == 200081) {

        // Level requirement
        if (m_pClientList[iClientH]->m_iLevel < 11) {
            m_pClientList[iClientH]->m_iSpecialEventID = 0;
            return;
        }

        // Create MemorialRing
        pItem = new class CItem;
        if (_bInitItemAttr(pItem, "MemorialRing") == FALSE) {
            delete pItem;
            return;
        }

        // Add to inventory
        if (_bAddClientItemList(iClientH, pItem, &iEraseReq) == TRUE) {
            // Bind to character (unique owner)
            pItem->m_sTouchEffectType = DEF_ITET_UNIQUE_OWNER;
            pItem->m_sTouchEffectValue1 = m_pClientList[iClientH]->m_sCharIDnum1;
            pItem->m_sTouchEffectValue2 = m_pClientList[iClientH]->m_sCharIDnum2;
            pItem->m_sTouchEffectValue3 = m_pClientList[iClientH]->m_sCharIDnum3;
            pItem->m_cItemColor = 9;  // Special color

            // Clear flag (one-time only)
            m_pClientList[iClientH]->m_iSpecialEventID = 0;

            // Log
            wsprintf(G_cTxt, "(*) Get MemorialRing: Char(%s)", ...);
            PutLogFileList(G_cTxt);
        }
    }
}
```

### Event Requirements

| Requirement | Value |
|-------------|-------|
| Event ID | 200081 |
| Minimum Level | 11 |
| Item | MemorialRing |
| Binding | Character-bound (unique owner) |
| Distribution | One per character |

### Default Event ID

**File:** `Client.cpp:239`

```cpp
m_iSpecialEventID = 200081;  // Default value for new characters
```

---

## Event Timing

### Main Game Loop Integration

**File:** `Game.cpp:45563-45618`

```cpp
// 3-second timer
if ((dwTime - m_dwGameTime3) > 3000) {
    SyncMiddlelandMapInfo();
    CheckDynamicObjectList();
    DynamicObjectEffectProcessor();
    NoticeHandler();
    SpecialEventHandler();      // Check 10-minute mob events
    EnergySphereProcessor();    // Check energy sphere spawning
    m_dwGameTime3 = dwTime;
}

// 20-second timer
if ((dwTime - m_dwWhetherTime) > 1000*20) {
    WhetherProcessor();         // Weather updates
    m_dwWhetherTime = dwTime;
}
```

### Timer Summary

| Timer | Interval | Functions Called |
|-------|----------|------------------|
| `m_dwGameTime3` | 3 seconds | SpecialEventHandler, EnergySphereProcessor |
| `m_dwWhetherTime` | 20 seconds | WhetherProcessor |
| Day/Night | Real-time | Checked on minute boundaries |

---

## Map Configuration

### Snow-Enabled Maps

**File:** `Game.cpp:21407`

```
type = 55  ; Enables snow weather
```

Sets `m_bIsSnowEnabled = TRUE`, forcing snow instead of rain.

### Fixed Day Mode

**File:** `Game.cpp:22026-22028`

```
fixed-daymode = 1
```

Disables day/night cycle and weather changes.

### Energy Sphere Points

**MapInfo file:**

```
energy-sphere-creation-point = 1 1 234 567
energy-sphere-creation-point = 2 1 345 678
energy-sphere-goal = 1 1 100 200 300 400
energy-sphere-auto-creation
```

---

## Client Notifications

### Notification Types

| Constant | Value | Purpose |
|----------|-------|---------|
| `DEF_NOTIFY_WHETHERCHANGE` | 0x0B4D | Weather status changed |
| `DEF_NOTIFY_TIMECHANGE` | - | Day/night transition |
| `DEF_NOTIFY_ENERGYSPHERECREATED` | - | New sphere spawned |
| `DEF_NOTIFY_ENERGYSPHEREGOALIN` | - | Sphere reached goal |
| `DEF_NOTIFY_SPAWNEVENT` | - | Special mob spawn event |

---

## Admin Commands

### /weather

**File:** `Game.cpp:43480-43540`

Force weather change on all maps:

```
/weather <type>
```

| Type | Effect |
|------|--------|
| 0 | Clear |
| 1-3 | Rain levels |
| 4-6 | Snow levels |

### /energysphere

**File:** `Game.cpp:9410`

Spawn an energy sphere on the admin's current map:

```
/energysphere
```

Requires admin level >= `m_iAdminLevelEnergySphere` (default: 2).

### /setnight and /setday

Force time of day changes.

---

## Constants and Limits

### Time Constants

| Constant | Value | Description |
|----------|-------|-------------|
| `DEF_SPECIALEVENTTIME` | 300000 (5 min) | Special event cycle |
| `DEF_NIGHTTIME` | 40 | Minute threshold for night |

### Capacity Limits

| Limit | Value | Description |
|-------|-------|-------------|
| `DEF_MAXENERGYSPHERES` | 10 | Spawn/goal points per map |
| Energy sphere concurrent | 1 | Only one active per map |
| Weather types | 0-9 | Weather status range |

---

## Related Systems

| System | Relationship |
|--------|--------------|
| **NPC System** | Energy spheres are NPCs; mob events use NPC spawning |
| **Combat System** | Sphere goal detection via attack handlers |
| **Magic System** | Weather affects spell damage |
| **Crusade System** | Mob events spawn faction NPCs during war |
| **Dynamic Objects** | Weather affects fire object duration |

---

## Implementation Notes

### Key Considerations for Modernization

1. **Weather Spelling**: Legacy uses "Whether" instead of "Weather" - maintain for compatibility but consider renaming internally

2. **Population Threshold**: 500-player requirement for energy spheres may need adjustment for smaller servers

3. **Time-Based Events**: Day/night uses real-world clock; consider game-time alternative

4. **Event Types**: Only 2 special event types exist; system could be extended

5. **Map-Specific Weather**: Each map has independent weather; consider regional weather zones

6. **Anniversary System**: Currently hardcoded; could be generalized for recurring events

### Thread Safety

All special event processing occurs on the main game loop thread, avoiding concurrency issues. Weather and time notifications are broadcast synchronously.

### Performance Considerations

- Weather checks (1/300) and event checks (dice rolls) are lightweight
- Energy sphere spawning has multiple early exits to minimize processing
- Client notifications are batched per-map for weather changes
