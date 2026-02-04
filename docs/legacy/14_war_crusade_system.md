# 14. War & Crusade System

**Document Version:** 1.0
**Status:** Complete
**Complexity:** Very High (~4,000+ lines)
**Primary Files:** `CrusadeCore.cpp/h`, `OccupyFlag.cpp/h`, `StrategicPoint.cpp/h`, war handling in `Game.cpp`

---

## Overview

The War & Crusade System is the **core PvP feature** of Helbreath, implementing large-scale factional warfare between Aresden and Elvine. The system provides three distinct war modes:

1. **Crusade** - Large-scale scheduled city sieges with meteor strike mechanics
2. **Heldenian** - Smaller-scale battleground warfare with tower/door defense
3. **Apocalypse** - PvE world event (variant mode)

The war system integrates with nearly every other game system: NPCs, combat, magic, guilds, construction, and territory control.

---

## Table of Contents

1. [Faction System](#1-faction-system)
2. [Crusade Warfare](#2-crusade-warfare)
3. [Grand Magic & Meteor Strikes](#3-grand-magic--meteor-strikes)
4. [War Structures](#4-war-structures)
5. [Construction System](#5-construction-system)
6. [Duty Roles](#6-duty-roles)
7. [Heldenian Warfare](#7-heldenian-warfare)
8. [Occupy Flag System](#8-occupy-flag-system)
9. [Strategic Points](#9-strategic-points)
10. [War Contribution & Rewards](#10-war-contribution--rewards)
11. [Force Recall System](#11-force-recall-system)
12. [Data Structures](#12-data-structures)
13. [Key Functions](#13-key-functions)
14. [Configuration](#14-configuration)
15. [Constants & Limits](#15-constants--limits)
16. [Message Protocol](#16-message-protocol)

---

## 1. Faction System

### 1.1 Faction Identifiers

```cpp
// Player side (m_cSide)
0 = Neutral (Traveler, no faction)
1 = Aresden (Purple city)
2 = Elvine (Green city)
```

### 1.2 Faction Assignment

Players choose their faction during character creation. This determines:
- Home city (spawn location)
- Allied players and NPCs
- Enemy faction and hostile territories
- Available quests and rewards

### 1.3 City Maps

| Map Name | Faction | Purpose |
|----------|---------|---------|
| `aresden` | Aresden | Home city, siege target |
| `elvine` | Elvine | Home city, siege target |
| `middleland` | Contested | Neutral battleground |
| `BtField` | Contested | Heldenian Mode 1 battlefield |
| `HRampart` | Contested | Heldenian Mode 2 battlefield |

---

## 2. Crusade Warfare

### 2.1 Overview

Crusade is the premier large-scale PvP event where factions battle to destroy the enemy city's infrastructure using accumulated magical energy. The war is won by completely destroying all Strike Points (critical buildings) in the enemy city via meteor strikes.

### 2.2 Crusade Lifecycle

```
                    ┌──────────────────────────────────────────────┐
                    │              CRUSADE LIFECYCLE               │
                    └──────────────────────────────────────────────┘
                                         │
                    ┌────────────────────▼─────────────────────┐
                    │  1. CrusadeWarStarter() - Scheduled Check │
                    │     - Checks m_stCrusadeWarSchedule[]     │
                    │     - Validates no other wars active      │
                    └────────────────────┬─────────────────────┘
                                         │ Schedule matches
                    ┌────────────────────▼─────────────────────┐
                    │  2. GlobalStartCrusadeMode()              │
                    │     - Sends GSM_BEGINCRUSADE to gate      │
                    │     - All game servers synchronized       │
                    └────────────────────┬─────────────────────┘
                                         │
                    ┌────────────────────▼─────────────────────┐
                    │  3. LocalStartCrusadeMode()               │
                    │     - Creates CrusadeGUID file            │
                    │     - Resets all client war data          │
                    │     - Restores Strike Point HP            │
                    │     - Spawns war structures               │
                    └────────────────────┬─────────────────────┘
                                         │
                    ┌────────────────────▼─────────────────────┐
                    │  4. WAR IN PROGRESS                       │
                    │     - Players select duties               │
                    │     - Mana collection active              │
                    │     - Grand Magic triggers meteors        │
                    │     - Strike Points take damage           │
                    └────────────────────┬─────────────────────┘
                                         │ All Strike Points = 0
                    ┌────────────────────▼─────────────────────┐
                    │  5. LocalEndCrusadeMode(winnerSide)       │
                    │     - Removes all war structures          │
                    │     - Records winner in GUID file         │
                    │     - Notifies all clients                │
                    │     - Logs result                         │
                    └──────────────────────────────────────────┘
```

### 2.3 Crusade Initialization

**Function:** `LocalStartCrusadeMode(DWORD dwCrusadeGUID)`

```cpp
void CGame::LocalStartCrusadeMode(DWORD dwCrusadeGUID)
{
    // 1. Set crusade mode active
    m_bIsCrusadeMode = TRUE;
    m_iCrusadeWinnerSide = 0;

    // 2. Create session GUID file
    _CreateCrusadeGUID(dwCrusadeGUID, NULL);
    m_dwCrusadeGUID = dwCrusadeGUID;

    // 3. Reset all player war data
    for (i = 1; i < DEF_MAXCLIENTS; i++) {
        if (m_pClientList[i] != NULL) {
            m_pClientList[i]->m_iCrusadeDuty = 0;
            m_pClientList[i]->m_iConstructionPoint = 0;
            m_pClientList[i]->m_dwCrusadeGUID = m_dwCrusadeGUID;
            SendNotifyMsg(NULL, i, DEF_NOTIFY_CRUSADE, m_bIsCrusadeMode, 0, NULL, NULL);
        }
    }

    // 4. Restore Strike Point HP for all maps
    for (i = 0; i < DEF_MAXMAPS; i++) {
        if (m_pMapList[i] != NULL) {
            m_pMapList[i]->RestoreStrikePoints();
        }
    }

    // 5. Spawn war structures
    CreateCrusadeStructures();
}
```

### 2.4 Victory Condition

The Crusade ends when **all Strike Points** in one faction's city are destroyed:

```cpp
// In CalcMeteorStrikeEffectHandler()
if (iActiveStructure == 0) {
    // No buildings left - Crusade ends
    if (iMapIndex == m_iAresdenMapIndex) {
        cWinnerSide = 2;  // Elvine wins
        LocalEndCrusadeMode(2);
    }
    else if (iMapIndex == m_iElvineMapIndex) {
        cWinnerSide = 1;  // Aresden wins
        LocalEndCrusadeMode(1);
    }
}
```

### 2.5 Crusade End Processing

**Function:** `LocalEndCrusadeMode(int iWinnerSide)`

```cpp
void CGame::LocalEndCrusadeMode(int iWinnerSide)
{
    m_bIsCrusadeMode = FALSE;

    // Remove all war structures
    RemoveCrusadeStructures();
    RemoveCrusadeNpcs();

    // Record winner
    _CreateCrusadeGUID(m_dwCrusadeGUID, iWinnerSide);
    m_iCrusadeWinnerSide = iWinnerSide;
    m_iLastCrusadeWinner = iWinnerSide;

    // Notify all clients
    for (i = 1; i < DEF_MAXCLIENTS; i++) {
        if (m_pClientList[i] != NULL) {
            m_pClientList[i]->m_iCrusadeDuty = 0;
            m_pClientList[i]->m_iConstructionPoint = 0;
            SendNotifyMsg(NULL, i, DEF_NOTIFY_CRUSADE, FALSE, NULL, NULL, NULL,
                          m_iCrusadeWinnerSide);
        }
    }

    RemoveCrusadeRecallTime();
}
```

---

## 3. Grand Magic & Meteor Strikes

### 3.1 Mana Collection Chain

The meteor strike system uses a multi-stage mana collection pipeline:

```
┌─────────────────────────────────────────────────────────────────────────┐
│                        MANA COLLECTION CHAIN                            │
└─────────────────────────────────────────────────────────────────────────┘

  ┌─────────────┐      ┌───────────────────┐      ┌──────────────────────┐
  │ MANA STONES │ ──▶  │  MANA COLLECTORS  │ ──▶  │ GRAND MAGIC GENERATOR│
  │   (Type 42) │      │    (Type 38)      │      │      (Type 41)       │
  │             │      │                   │      │                      │
  │ m_iV1 = mana│      │ Harvests 3 mana   │      │ Consumes 15 mana per │
  │  stored     │      │ per tick from     │      │ tick to charge.      │
  │             │      │ nearby stones     │      │                      │
  │ Neutral     │      │ Also restores     │      │ When m_iManaStock >  │
  │ resource    │      │ allied player MP  │      │ m_iMaxMana: FIRE!    │
  └─────────────┘      └───────────────────┘      └──────────────────────┘
                              │                            │
                              ▼                            ▼
                    m_iCollectedMana[side]       MeteorStrikeMsgHandler()
                              │                            │
                              ▼                            ▼
                    SendCollectedMana()          Enemy city receives damage
                              │
                              ▼
                    m_iAresdenMana / m_iElvineMana
```

### 3.2 Mana Collector Behavior

**Function:** `_bNpcBehavior_ManaCollector(int iNpcH)`

```cpp
BOOL CGame::_bNpcBehavior_ManaCollector(int iNpcH)
{
    // Scan 5-tile radius
    for (dX = sX-5; dX <= sX+5; dX++)
    for (dY = sY-5; dY <= sY+5; dY++) {
        m_pMapList[mapIndex]->GetOwner(&sOwnerH, &cOwnerType, dX, dY);

        switch (cOwnerType) {
        case DEF_OWNERTYPE_PLAYER:
            // Restore MP to allied players
            if (m_pNpcList[iNpcH]->m_cSide == m_pClientList[sOwnerH]->m_cSide) {
                iMaxMP = (2*MAG) + (2*Level) + (INT/2);
                iTotal = iDice(1, MAG) + (AddMP bonus);
                m_pClientList[sOwnerH]->m_iMP += iTotal;
            }
            break;

        case DEF_OWNERTYPE_NPC:
            // Harvest from Mana Stones (type 42)
            if (m_pNpcList[sOwnerH]->m_sType == 42 && m_pNpcList[sOwnerH]->m_iV1 > 0) {
                if (m_pNpcList[sOwnerH]->m_iV1 >= 3) {
                    m_iCollectedMana[side] += 3;
                    m_pNpcList[sOwnerH]->m_iV1 -= 3;
                } else {
                    m_iCollectedMana[side] += m_pNpcList[sOwnerH]->m_iV1;
                    m_pNpcList[sOwnerH]->m_iV1 = 0;
                }
            }
            break;
        }
    }
}
```

### 3.3 Grand Magic Generator Behavior

**Function:** `_NpcBehavior_GrandMagicGenerator(int iNpcH)`

```cpp
void CGame::_NpcBehavior_GrandMagicGenerator(int iNpcH)
{
    switch (m_pNpcList[iNpcH]->m_cSide) {
    case 1: // Aresden GMG
        if (m_iAresdenMana > DEF_GMGMANACONSUMEUNIT) {  // 15 mana threshold
            m_iAresdenMana = 0;
            m_pNpcList[iNpcH]->m_iManaStock++;

            if (m_pNpcList[iNpcH]->m_iManaStock > m_pNpcList[iNpcH]->m_iMaxMana) {
                // LAUNCH METEOR STRIKE!
                _GrandMagicLaunchMsgSend(1, 1);
                MeteorStrikeMsgHandler(1);  // Attack Elvine
                m_pNpcList[iNpcH]->m_iManaStock = 0;
            }
        }
        break;

    case 2: // Elvine GMG (same logic, attacks Aresden)
        // ...
    }
}
```

### 3.4 Meteor Strike Execution

**Function:** `MeteorStrikeHandler(int iMapIndex)`

The meteor strike follows this sequence:

```
Time 0:    MeteorStrikeMsgHandler() - Announce incoming strike
                    │
                    ▼
Time +5s:  MeteorStrikeHandler() - Main strike execution
                    │
           ┌────────┴────────┐
           ▼                 ▼
    Count ESG shields    Apply damage
           │                 │
           ▼                 ▼
    ESG < 2 near        Strike Points lose HP
    Strike Point?       (2 - ESGcount) damage
           │
Time +1s:  DoMeteorStrikeDamageHandler() - Player damage wave 1
                    │
Time +4s:  DoMeteorStrikeDamageHandler() - Player damage wave 2
                    │
Time +6s:  CalcMeteorStrikeEffectHandler() - Calculate results
                    │
           ┌────────┴────────┐
           ▼                 ▼
    Active structures   All structures
    remain > 0          destroyed = 0
           │                 │
           ▼                 ▼
    Report results      END CRUSADE
    to all players      (winner decided)
```

### 3.5 Strike Point Protection

Strike Points are protected by Energy Shield Generators (ESG, type 40):

```cpp
// For each strike point target
iTotalESG = 0;
for (ix = dX-10; ix <= dX+10; ix++)
for (iy = dY-10; iy <= dY+10; iy++) {
    GetOwner(&sOwnerH, &cOwnerType, ix, iy);
    if (cOwnerType == DEF_OWNERTYPE_NPC && m_pNpcList[sOwnerH]->m_sType == 40) {
        iTotalESG++;  // Count nearby ESGs
    }
}

// Need at least 2 ESGs for full protection
if (iTotalESG < 2) {
    m_pMapList[iMapIndex]->m_stStrikePoint[target].iHP -= (2 - iTotalESG);
}
```

### 3.6 Player Meteor Damage

**Function:** `DoMeteorStrikeDamageHandler(int iMapIndex)`

```cpp
void CGame::DoMeteorStrikeDamageHandler(int iMapIndex)
{
    for (i = 1; i < DEF_MAXCLIENTS; i++) {
        if (m_pClientList[i] == NULL) continue;
        if (m_pClientList[i]->m_cMapIndex != iMapIndex) continue;

        // Calculate damage based on level
        if (m_pClientList[i]->m_iLevel < 80)
            iDamage = Level + iDice(1,10);
        else
            iDamage = Level*2 + iDice(1,10);

        // Magic protection reduces damage
        if (m_cMagicEffectStatus[DEF_MAGICTYPE_PROTECT] == 2) {
            iDamage = (iDamage/2) - 2;
        }
        if (m_cMagicEffectStatus[DEF_MAGICTYPE_PROTECT] == 5) {
            iDamage = 0;  // Full protection
        }

        // Admin immunity
        if (m_pClientList[i]->m_iAdminUserLevel > 0) {
            iDamage = 0;
        }

        // Apply damage
        m_pClientList[i]->m_iHP -= iDamage;
        if (m_pClientList[i]->m_iHP <= 0) {
            ClientKilledHandler(i, NULL, NULL, iDamage);
            m_stMeteorStrikeResult.iCasualties++;
        }
    }
}
```

---

## 4. War Structures

### 4.1 Structure Types

| Type ID | Name | Faction Variant | Purpose |
|---------|------|-----------------|---------|
| 36 | Arrow Guard Tower | AGT-Aresden/AGT-Elvine | Ranged defense |
| 37 | Cannon Guard Tower | CGT-Aresden/CGT-Elvine | Heavy defense |
| 38 | Mana Collector | MS-Aresden/MS-Elvine | Harvests mana from stones |
| 39 | Detector | DT-Aresden/DT-Elvine | Reveals invisible enemies |
| 40 | Energy Shield Generator | ESG-Aresden/ESG-Elvine | Protects Strike Points |
| 41 | Grand Magic Generator | GMG-Aresden/GMG-Elvine | Launches meteor strikes |
| 42 | Mana Stone | Neutral | Mana source for collectors |
| 43 | Light War Beetle | LWB-Aresden/LWB-Elvine | Mobile unit |
| 51 | Catapult | CP-Aresden/CP-Elvine | Siege weapon |
| 44 | GHK | Neutral | Special unit |
| 45 | GHKABS | Neutral | Special unit |

### 4.2 Structure Spawning

**Function:** `CreateCrusadeStructures()`

Structures are spawned from the `crusade_structures.cfg` configuration:

```cpp
void CGame::CreateCrusadeStructures()
{
    for (i = 0; i < DEF_MAXCRUSADESTRUCTURES; i++) {
        if (m_stCrusadeStructures[i].cType == NULL) continue;

        // Find matching map
        for (z = 0; z < DEF_MAXMAPS; z++) {
            if (strcmp(m_pMapList[z]->m_cName, m_stCrusadeStructures[i].cMapName) == 0) {
                // Get naming value for unique NPC name
                iNamingValue = m_pMapList[z]->iGetEmptyNamingValue();

                // Create faction-specific name
                switch (m_stCrusadeStructures[i].cType) {
                case 36:  // Arrow Guard Tower
                    if (strcmp(mapName, "aresden") == 0)
                        strcpy(cNpcName, "AGT-Aresden");
                    else if (strcmp(mapName, "elvine") == 0)
                        strcpy(cNpcName, "AGT-Elvine");
                    break;
                // ... other types
                }

                // Spawn the NPC
                bCreateNewNpc(cNpcName, cName, mapName, 0, 0,
                              DEF_MOVETYPE_RANDOM, &tX, &tY, ...);
            }
        }
    }
}
```

### 4.3 Detector Behavior

**Function:** `_bNpcBehavior_Detector(int iNpcH)`

The Detector (type 39) reveals invisible enemies within 10 tiles:

```cpp
BOOL CGame::_bNpcBehavior_Detector(int iNpcH)
{
    for (dX = sX-10; dX <= sX+10; dX++)
    for (dY = sY-10; dY <= sY+10; dY++) {
        GetOwner(&sOwnerH, &cOwnerType, dX, dY);

        // Check if enemy faction
        if (cSide != 0 && cSide != m_pNpcList[iNpcH]->m_cSide) {
            // Remove invisibility
            if (cOwnerType == DEF_OWNERTYPE_PLAYER) {
                if (m_pClientList[sOwnerH]->m_cMagicEffectStatus[DEF_MAGICTYPE_INVISIBILITY]) {
                    m_pClientList[sOwnerH]->m_cMagicEffectStatus[DEF_MAGICTYPE_INVISIBILITY] = NULL;
                    SetInvisibilityFlag(sOwnerH, cOwnerType, FALSE);
                }
            }
        }
    }
}
```

---

## 5. Construction System

### 5.1 Summoning War Units

Players with Constructor duty can summon temporary war structures using Construction Points.

**Function:** `RequestSummonWarUnitHandler(int iClientH, int dX, int dY, char cType, char cNum, char cMode)`

```cpp
void CGame::RequestSummonWarUnitHandler(int iClientH, int dX, int dY, char cType, char cNum, char cMode)
{
    // Validation
    if (m_pClientList[iClientH]->m_iCrusadeDuty != 2) return;  // Must be Constructor
    if (m_pClientList[iClientH]->m_iConstructionPoint < m_iNpcConstructionPoint[cType]) return;

    // Force single unit per summon
    cNum = 1;

    // Create faction-specific NPC name
    switch (cType) {
    case 43:  // Light War Beetle
        switch (m_pClientList[iClientH]->m_cSide) {
        case 1: strcpy(cNpcName, "LWB-Aresden"); break;
        case 2: strcpy(cNpcName, "LWB-Elvine"); break;
        }
        break;
    case 36:  // Arrow Guard Tower
        switch (m_pClientList[iClientH]->m_cSide) {
        case 1: strcpy(cNpcName, "AGT-Aresden"); break;
        case 2: strcpy(cNpcName, "AGT-Elvine"); break;
        }
        break;
    // ... other types
    }

    // Spawn the unit
    bCreateNewNpc(cNpcName, cName, mapName, ...);

    // Deduct construction points
    m_pClientList[iClientH]->m_iConstructionPoint -= m_iNpcConstructionPoint[cType];
}
```

### 5.2 Summonable Unit Types

| Type ID | Unit | Summonable |
|---------|------|------------|
| 36 | Arrow Guard Tower | Yes |
| 37 | Cannon Guard Tower | Yes |
| 38 | Mana Collector | Yes |
| 39 | Detector | Yes |
| 43 | Light War Beetle | Yes |
| 44 | GHK | Yes |
| 45 | GHKABS | Yes |
| 51 | Catapult | Yes |
| 82-88 | Various war units | Yes |

### 5.3 Guild Teleport & Construction Locations

Commanders can set two special guild locations:

1. **Guild Teleport Location** - Where guild members teleport during war
2. **Guild Construction Location** - Where structures are built

**Function:** `RequestSetGuildTeleportLocHandler()`
**Function:** `RequestSetGuildConstructLocHandler()`

---

## 6. Duty Roles

### 6.1 Role Selection

**Function:** `SelectCrusadeDutyHandler(int iClientH, int iDuty)`

```cpp
void CGame::SelectCrusadeDutyHandler(int iClientH, int iDuty)
{
    // Only guild masters can be commanders
    if ((m_pClientList[iClientH]->m_iGuildRank != 0) && (iDuty == 3)) return;

    // Previous winners get bonus if selecting commander
    if (m_iLastCrusadeWinner == m_pClientList[iClientH]->m_cSide &&
        m_pClientList[iClientH]->m_dwCrusadeGUID == 0 && iDuty == 3) {
        m_pClientList[iClientH]->m_iConstructionPoint = 3000;
    }

    m_pClientList[iClientH]->m_iCrusadeDuty = iDuty;
}
```

### 6.2 Role Descriptions

| Duty ID | Role | Description | Special Abilities |
|---------|------|-------------|-------------------|
| 1 | Fighter | Combat specialist | Earns construction points through combat |
| 2 | Constructor | Builder | Can summon war structures |
| 3 | Commander | Guild leader | Receives pooled points, sets locations, sees map status |

### 6.3 Construction Point Flow

Fighters and Constructors accumulate points → Points transferred to guild Commander:

**Function:** `CheckCommanderConstructionPoint(int iClientH)`

```cpp
void CGame::CheckCommanderConstructionPoint(int iClientH)
{
    if (m_pClientList[iClientH]->m_iCrusadeDuty == 1 ||
        m_pClientList[iClientH]->m_iCrusadeDuty == 2) {

        // Find guild commander
        for (i = 0; i < DEF_MAXCLIENTS; i++) {
            if (m_pClientList[i] != NULL &&
                m_pClientList[i]->m_iCrusadeDuty == 3 &&
                m_pClientList[i]->m_iGuildGUID == m_pClientList[iClientH]->m_iGuildGUID) {

                // Transfer points to commander
                m_pClientList[i]->m_iConstructionPoint += m_pClientList[iClientH]->m_iConstructionPoint;
                m_pClientList[i]->m_iWarContribution += (m_pClientList[iClientH]->m_iConstructionPoint / 10);

                // Apply caps
                if (m_pClientList[i]->m_iConstructionPoint > DEF_MAXCONSTRUCTIONPOINT)
                    m_pClientList[i]->m_iConstructionPoint = DEF_MAXCONSTRUCTIONPOINT;
                if (m_pClientList[i]->m_iWarContribution > DEF_MAXWARCONTRIBUTION)
                    m_pClientList[i]->m_iWarContribution = DEF_MAXWARCONTRIBUTION;

                m_pClientList[iClientH]->m_iConstructionPoint = 0;
                return;
            }
        }

        // Commander not on this server - send via gate server
        bStockMsgToGateServer(GSM_CONSTRUCTIONPOINT, ...);
    }
}
```

---

## 7. Heldenian Warfare

### 7.1 Overview

Heldenian is a smaller-scale battleground war with two alternating modes:

| Mode | Map | Objective |
|------|-----|-----------|
| Mode 1 | BtField | Destroy enemy towers |
| Mode 2 | HRampart | Defend/destroy gate doors |

### 7.2 Heldenian Lifecycle

**Function:** `LocalStartHeldenianMode(short sV1, short sV2, DWORD dwHeldenianGUID)`

```cpp
void CGame::LocalStartHeldenianMode(short sV1, short sV2, DWORD dwHeldenianGUID)
{
    m_bIsHeldenianMode = TRUE;
    m_cHeldenianModeType = sV1;  // 1 or 2

    // Initialize counters
    m_iHeldenianAresdenLeftTower = 0;
    m_iHeldenianElvineLeftTower = 0;
    m_iHeldenianAresdenDead = 0;
    m_iHeldenianElvineDead = 0;

    // Reset all players
    for (i = 0; i < DEF_MAXCLIENTS; i++) {
        if (m_pClientList[i] != NULL) {
            m_pClientList[i]->m_iWarContribution = 0;
            // Construction points based on Charisma (max 12000)
            m_pClientList[i]->m_iConstructionPoint = min(m_pClientList[i]->m_iCharisma * 300, 12000);
        }
    }

    // Mode-specific setup
    if (m_cHeldenianModeType == 1) {
        // BtField tower defense - spawn towers for each faction
        for (i = 0; i < MAX_HELDENIANTOWER; i++) {
            // Spawn tower NPCs from m_stHeldenianTower[] config
        }
    }
    else if (m_cHeldenianModeType == 2) {
        // HRampart door defense - spawn doors for defending faction
        for (i = 0; i < DEF_MAXHELDENIANDOOR; i++) {
            // Spawn door NPCs (type 91)
        }
    }

    // Teleport players out of battlefield
    // Remove non-Heldenian NPCs
}
```

### 7.3 Heldenian Victory

**Function:** `bNotifyHeldenianWinner()`

Victory is determined by remaining structures:
- **Mode 1**: Faction with more remaining towers wins
- **Mode 2**: Attacking faction wins if all doors destroyed, defender wins otherwise

### 7.4 Status Updates

**Function:** `UpdateHeldenianStatus()`

Broadcasts remaining tower/door counts to all players.

---

## 8. Occupy Flag System

### 8.1 Overview

The Occupy Flag system allows players to claim territory by planting faction flags. This persists between sessions.

### 8.2 Data Structure

**Class:** `COccupyFlag`

```cpp
class COccupyFlag
{
public:
    char m_cSide;               // 1=Aresden, 2=Elvine
    int  m_iEKCount;            // Enemy Kill cost to plant
    int  m_sX, m_sY;            // Position
    int  m_iDynamicObjectIndex; // Visual representation
};
```

### 8.3 Getting a Flag

**Function:** `GetOccupyFlagHandler(int iClientH)`

Players with 3+ Enemy Kills can acquire a flag item:

```cpp
void CGame::GetOccupyFlagHandler(int iClientH)
{
    if (m_pClientList[iClientH]->m_iEnemyKillCount < 3) return;
    if (m_pClientList[iClientH]->m_cSide == 0) return;  // Neutral can't get flags

    // Create flag item
    switch (m_pClientList[iClientH]->m_cSide) {
    case 1: strcpy(cItemName, "Aresden Flag"); break;
    case 2: strcpy(cItemName, "Elvine Flag"); break;
    }

    pItem = new CItem;
    _bInitItemAttr(pItem, cItemName);

    // EK cost recorded on flag (max 12)
    if (m_pClientList[iClientH]->m_iEnemyKillCount > 12) {
        pItem->m_sItemSpecEffectValue1 = 12;
        m_pClientList[iClientH]->m_iEnemyKillCount -= 12;
    } else {
        pItem->m_sItemSpecEffectValue1 = m_pClientList[iClientH]->m_iEnemyKillCount;
        m_pClientList[iClientH]->m_iEnemyKillCount = 0;
    }

    _bAddClientItemList(iClientH, pItem, ...);
}
```

### 8.4 Planting a Flag

**Function:** `__bSetOccupyFlag(char cMapIndex, int dX, int dY, int iSide, int iEKNum, int iClientH, BOOL bAdminFlag)`

- Validates tile not already occupied
- Checks no nearby same-faction flags
- Creates dynamic object (visual)
- Updates tile occupation status

### 8.5 Persistence

**Function:** `SaveOccupyFlagData()`

Flag data is saved to `GameData/OccupyFlag.txt` every 3 minutes and on server shutdown.

---

## 9. Strategic Points

### 9.1 Data Structure

**Class:** `CStrategicPoint`

```cpp
class CStrategicPoint
{
public:
    int m_iSide;    // Controlling faction (0=neutral, 1=Aresden, 2=Elvine)
    int m_iValue;   // Strategic importance
    int m_iX, m_iY; // Position
};
```

### 9.2 Occupation Status

**Function:** `_CheckStrategicPointOccupyStatus(char cMapIndex)`

Calculates current control based on nearby flags and player presence.

---

## 10. War Contribution & Rewards

### 10.1 Contribution Tracking

```cpp
// Client.h
int m_iWarContribution;      // Total war contribution (max 200,000)
int m_iConstructionPoint;    // Construction points (max 30,000)
```

### 10.2 Earning Contribution

| Activity | Points Earned |
|----------|---------------|
| Enemy player kills | Based on enemy level |
| War structure destruction | Fixed amount |
| Points transfer to commander | 1/10 of construction points |
| Successful constructor actions | Variable |

### 10.3 Previous Winner Bonus

If a player's faction won the last Crusade and they select Commander duty:
```cpp
if (m_iLastCrusadeWinner == m_pClientList[iClientH]->m_cSide && iDuty == 3) {
    m_pClientList[iClientH]->m_iConstructionPoint = 3000;  // Bonus starting points
}
```

---

## 11. Force Recall System

### 11.1 Overview

Players in enemy territory are subject to time-limited raids. After the time expires, they are teleported back to their home city.

### 11.2 Time Calculation

**Function:** `SetForceRecallTime(int iClientH)` / `CheckForceRecallTime(int iClientH)`

```cpp
void CGame::CheckForceRecallTime(int iClientH)
{
    if (m_pClientList[iClientH]->m_iTimeLeft_ForceRecall == 0) {
        // Calculate based on day of week
        switch (SysTime.wDayOfWeek) {
        case 1: m_pClientList[iClientH]->m_iTimeLeft_ForceRecall = 20*m_sRaidTimeMonday; break;
        case 2: m_pClientList[iClientH]->m_iTimeLeft_ForceRecall = 20*m_sRaidTimeTuesday; break;
        // ...
        }
    }
    m_pClientList[iClientH]->m_bIsWarLocation = TRUE;
}
```

### 11.3 Configurable Raid Times

Raid duration can be configured per day of week:
- `m_sRaidTimeMonday` through `m_sRaidTimeSunday`
- Or override with `m_sForceRecallTime` for all days

---

## 12. Data Structures

### 12.1 Player War Fields (Client.h)

```cpp
class CClient
{
    int   m_iCrusadeDuty;                    // 0=none, 1=Fighter, 2=Constructor, 3=Commander
    DWORD m_dwCrusadeGUID;                   // Current crusade session ID
    DWORD m_dwHeldenianGUID;                 // Current heldenian session ID
    int   m_iWarContribution;                // Total war contribution (max 200,000)
    int   m_iConstructionPoint;              // Construction points (max 30,000)

    // Visible crusade structures (for commanders)
    struct {
        char cType;
        char cSide;
        short sX, sY;
    } m_stCrusadeStructureInfo[DEF_MAXCRUSADESTRUCTURES];
    int m_iCSIsendPoint;

    // Guild construction location
    char m_cConstructMapName[11];
    int  m_iConstructLocX, m_iConstructLocY;

    // Force recall
    int  m_iTimeLeft_ForceRecall;
    BOOL m_bIsWarLocation;
};
```

### 12.2 Game State Variables (Game.h)

```cpp
class CGame
{
    // War mode flags
    BOOL   m_bIsCrusadeMode;
    BOOL   m_bIsHeldenianMode;
    BOOL   m_bIsApocalypseMode;

    // Session IDs
    DWORD  m_dwCrusadeGUID;
    DWORD  m_dwHeldenianGUID;

    // Results
    int    m_iCrusadeWinnerSide;
    int    m_iLastCrusadeWinner;

    // Mana tracking
    int    m_iAresdenMana, m_iElvineMana;
    int    m_iCollectedMana[3];  // Index 1=Aresden, 2=Elvine

    // Structure definitions
    struct {
        char cMapName[11];
        char cType;
        int  dX, dY;
    } m_stCrusadeStructures[DEF_MAXCRUSADESTRUCTURES];

    // Middleland structure info (synced across servers)
    struct {
        char cType;
        char cSide;
        short sX, sY;
    } m_stMiddleCrusadeStructureInfo[DEF_MAXCRUSADESTRUCTURES];
    int m_iTotalMiddleCrusadeStructures;

    // Meteor strike results
    struct {
        int iCrashedStructureNum;
        int iStructureDamageAmount;
        int iCasualties;
    } m_stMeteorStrikeResult;

    // Heldenian tracking
    int m_iHeldenianAresdenLeftTower;
    int m_iHeldenianElvineLeftTower;
    int m_iHeldenianAresdenDead;
    int m_iHeldenianElvineDead;
    char m_cHeldenianModeType;
    char m_cHeldenianVictoryType;

    // Schedule arrays
    struct {
        int iDay, iHour, iMinute;
    } m_stCrusadeWarSchedule[DEF_MAXSCHEDULE];

    struct {
        int iDay, StartiHour, StartiMinute, EndiHour, EndiMinute;
    } m_stHeldenianSchedule[DEF_MAXHELDENIAN];

    // Guild teleport locations
    CTeleportLoc m_pGuildTeleportLoc[DEF_MAXGUILDS];

    // NPC construction costs
    int m_iNpcConstructionPoint[DEF_MAXNPCTYPES];

    // Map indices
    int m_iMiddlelandMapIndex;
    int m_iAresdenMapIndex;
    int m_iElvineMapIndex;
    int m_iBTFieldMapIndex;
};
```

---

## 13. Key Functions

### 13.1 Crusade Functions

| Function | Line | Purpose |
|----------|------|---------|
| `CrusadeWarStarter()` | 45488 | Scheduled crusade trigger |
| `GlobalStartCrusadeMode()` | 45858 | Server-wide start notification |
| `LocalStartCrusadeMode()` | 41283 | Local crusade initialization |
| `LocalEndCrusadeMode()` | 41317 | Local crusade termination |
| `ManualEndCrusadeMode()` | 43103 | Admin-triggered end |
| `CreateCrusadeStructures()` | 41391 | Spawn initial structures |
| `RemoveCrusadeStructures()` | 42066 | Despawn all structures |
| `SelectCrusadeDutyHandler()` | 41892 | Player duty selection |
| `RequestSummonWarUnitHandler()` | 41576 | Summon war units |
| `CheckCommanderConstructionPoint()` | 42880 | Point transfer to commander |

### 13.2 Meteor Strike Functions

| Function | Line | Purpose |
|----------|------|---------|
| `_bNpcBehavior_ManaCollector()` | 53012 | Mana collection from stones |
| `_NpcBehavior_GrandMagicGenerator()` | 53123 | GMG charge and fire |
| `SendCollectedMana()` | 48479 | Broadcast mana to gate |
| `CollectedManaHandler()` | 48516 | Receive mana from other servers |
| `MeteorStrikeMsgHandler()` | 53072 | Announce incoming strike |
| `MeteorStrikeHandler()` | 46189 | Execute strike on city |
| `DoMeteorStrikeDamageHandler()` | 42697 | Apply player damage |
| `CalcMeteorStrikeEffectHandler()` | 48540 | Calculate results, check win |
| `GrandMagicResultHandler()` | 48741 | Broadcast results |

### 13.3 Heldenian Functions

| Function | Line | Purpose |
|----------|------|---------|
| `HeldenianWarStarter()` | 54047 | Scheduled heldenian trigger |
| `GlobalStartHeldenianMode()` | 54091 | Server-wide start |
| `LocalStartHeldenianMode()` | 54128 | Local initialization |
| `LocalEndHeldenianMode()` | 53724 | Local termination |
| `GlobalEndHeldenianMode()` | 53708 | Server-wide end |
| `UpdateHeldenianStatus()` | 54548 | Update tower counts |
| `bNotifyHeldenianWinner()` | 53781 | Determine winner |
| `RequestHeldenianTeleport()` | 53959 | Teleport handling |

### 13.4 Territory Functions

| Function | Line | Purpose |
|----------|------|---------|
| `GetOccupyFlagHandler()` | 36190 | Player acquires flag |
| `__bSetOccupyFlag()` | 36011 | Plant flag on tile |
| `SaveOccupyFlagData()` | 41365 | Persist flags to disk |
| `RemoveOccupyFlags()` | 53817 | Remove all flags |
| `_DeleteRandomOccupyFlag()` | 40595 | Remove random flag |
| `_CheckStrategicPointOccupyStatus()` | 38491 | Update control status |

### 13.5 Utility Functions

| Function | Line | Purpose |
|----------|------|---------|
| `_bNpcBehavior_Detector()` | 53159 | Reveal invisible enemies |
| `MapStatusHandler()` | 41913 | Send structure info to commander |
| `_SendMapStatus()` | 42005 | Transmit structure data |
| `GSM_SetGuildTeleportLoc()` | 45977 | Receive teleport location |
| `GSM_SetGuildConstructLoc()` | 42810 | Receive construction location |
| `GSM_ConstructionPoint()` | 42932 | Receive construction points |
| `_CreateCrusadeGUID()` | 42997 | Create session file |
| `bReadCrusadeGUIDFile()` | 43037 | Load previous session |
| `_LinkStrikePointMapIndex()` | 48664 | Initialize strike point refs |
| `SyncMiddlelandMapInfo()` | 42756 | Sync middleland structures |

---

## 14. Configuration

### 14.1 Crusade Schedule (Schedule.cfg)

```ini
[CRUSADE-SCHEDULE]
schedule = 1,20,00    ; Day, Hour, Minute (Sunday=0)
schedule = 4,20,00    ; Multiple entries allowed
; Max 10 schedules
```

### 14.2 Crusade Structures (crusade_structures.cfg)

```ini
[CRUSADE-STRUCTURES]
structure = aresden,36,128,256    ; Map, Type, X, Y
structure = aresden,40,130,258
structure = elvine,36,128,256
structure = elvine,40,130,258
structure = middleland,42,500,500 ; Mana Stone
; Max 300 structures
```

### 14.3 Heldenian Schedule (Schedule.cfg)

```ini
[HELDENIAN-SCHEDULE]
schedule = 2,19,00,21,00    ; Day, StartHour, StartMin, EndHour, EndMin
schedule = 5,19,00,21,00
; Max 10 schedules
```

### 14.4 Raid Time Configuration (Settings.cfg)

```ini
[RAID-TIMES]
Monday = 3        ; Minutes allowed in enemy territory
Tuesday = 3
Wednesday = 3
Thursday = 3
Friday = 15
Saturday = 45
Sunday = 60
ForceRecallTime = 0   ; Override all days (0 = use per-day values)
```

---

## 15. Constants & Limits

### 15.1 Crusade Constants

```cpp
#define DEF_MAXCRUSADESTRUCTURES   300    // Max configured structures
#define DEF_MAXSTRIKEPOINTS        30     // Strike points per map
#define DEF_MAXCONSTRUCTIONPOINT   30000  // Max construction points
#define DEF_MAXWARCONTRIBUTION     200000 // Max war contribution
#define DEF_GMGMANACONSUMEUNIT     15     // Mana per GMG tick
#define DEF_MAXSCHEDULE            10     // Max crusade schedules
```

### 15.2 Heldenian Constants

```cpp
#define DEF_MAXHELDENIAN           10     // Max heldenian schedules
#define MAX_HELDENIANTOWER         200    // Towers per map
#define DEF_MAXHELDENIANDOOR       10     // Doors per heldenian
```

### 15.3 Apocalypse Constants

```cpp
#define DEF_MAXAPOCALYPSE          7      // Max apocalypse schedules
```

---

## 16. Message Protocol

### 16.1 Gate Server Messages

| Code | Name | Direction | Purpose |
|------|------|-----------|---------|
| 0x06 | GSM_BEGINCRUSADE | GS→Gate→GS | Start crusade |
| 0x07 | GSM_ENDCRUSADE | GS→Gate→GS | End crusade |
| 0x13 | GSM_BEGINAPOCALYPSE | GS→Gate→GS | Start apocalypse |
| 0x14 | GSM_ENDAPOCALYPSE | GS→Gate→GS | End apocalypse |
| 0x17 | GSM_ENDHELDENIAN | GS→Gate→GS | End heldenian |
| 0x19 | GSM_STARTHELDENIAN | GS→Gate→GS | Start heldenian |
| - | GSM_COLLECTEDMANA | GS→Gate→GS | Sync mana |
| - | GSM_CONSTRUCTIONPOINT | GS→Gate→GS | Transfer points |
| - | GSM_MIDDLEMAPSTATUS | GS→Gate→GS | Sync structures |
| - | GSM_GRANDMAGICRESULT | GS→Gate→GS | Strike results |
| - | GSM_SETGUILDTELEPORTLOC | GS→Gate→GS | Teleport loc |
| - | GSM_SETGUILDCONSTRUCTLOC | GS→Gate→GS | Construct loc |

### 16.2 Client Notifications

| Code | Name | Purpose |
|------|------|---------|
| DEF_NOTIFY_CRUSADE | Crusade state | Mode on/off, duty, winner |
| DEF_NOTIFY_METEORSTRIKECOMING | Strike warning | Incoming meteor |
| DEF_NOTIFY_METEORSTRIKEHIT | Strike hit | Strike landed |
| DEF_NOTIFY_GRANDMAGICRESULT | Strike result | Damage summary |
| DEF_NOTIFY_CONSTRUCTIONPOINT | Points update | Construction points |
| DEF_NOTIFY_HELDENIANCOUNT | Heldenian status | Tower counts |
| DEF_NOTIFY_HELDENIANTELEPORT | Heldenian TP | Teleport prompt |
| DEF_NOTIFY_HELDENIANSTART | Heldenian start | War starting |
| DEF_NOTIFY_TCLOC | Teleport loc | Guild teleport location |
| DEF_NOTIFY_MAPSTATUSNEXT | Map status | Structure data (more coming) |
| DEF_NOTIFY_MAPSTATUSLAST | Map status | Structure data (final) |
| DEF_NOTIFY_FORCERECALLTIME | Recall time | Time remaining |
| DEF_NOTIFY_TOBERECALLED | Force recall | Being teleported |

---

## Summary

The War & Crusade System is the central competitive feature of Helbreath, implementing:

1. **Large-scale factional warfare** between Aresden and Elvine
2. **Automated scheduling** for fair, predictable war times
3. **Multi-stage mana collection** powering devastating meteor strikes
4. **Role-based participation** (Fighter, Constructor, Commander)
5. **Persistent territory control** through flag placement
6. **Two distinct war modes** (Crusade for city sieges, Heldenian for battlegrounds)
7. **Victory through strategic destruction** of enemy infrastructure
8. **Cross-server synchronization** for consistent state

The system's complexity reflects its importance as the game's primary endgame PvP content, requiring coordination between players, guilds, and game systems to achieve victory.
