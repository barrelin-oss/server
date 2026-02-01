# Legacy Skill System Documentation

**System:** Skill System
**Primary Files:** `Skill.cpp/h`, skill handling in `Game.cpp`, `Client.h`
**Estimated Lines:** ~1,500 across codebase
**Complexity:** Medium

---

## Table of Contents

1. [Overview](#overview)
2. [Data Structures](#data-structures)
3. [Skill Types Reference](#skill-types-reference)
4. [Skill Effect Types](#skill-effect-types)
5. [Per-Player Skill Data](#per-player-skill-data)
6. [Skill Learning & Training](#skill-learning--training)
7. [Skill Advancement System](#skill-advancement-system)
8. [Skill Usage & Activation](#skill-usage--activation)
9. [Weapon Skill System](#weapon-skill-system)
10. [Combo Attack System](#combo-attack-system)
11. [Super Attack System](#super-attack-system)
12. [Down Skill (Recovery Move)](#down-skill-recovery-move)
13. [Taming System](#taming-system)
14. [Stat-Based Skill Caps](#stat-based-skill-caps)
15. [Configuration Format](#configuration-format)
16. [Key Functions Reference](#key-functions-reference)
17. [Constants Reference](#constants-reference)
18. [Interactions with Other Systems](#interactions-with-other-systems)

---

## Overview

The skill system manages all player skills including combat proficiencies, crafting abilities, gathering skills, and special abilities. Each player can learn up to 60 different skills, with mastery levels ranging from 0-100 and a total skill point cap of 700.

### Core Concepts

- **Skill Mastery:** Level 0-100 per skill, determines effectiveness
- **Skill Points:** Total of all skill levels cannot exceed 700
- **SSN (Skill Special Number):** Experience points toward next skill level
- **Skill Effect Types:** Categories defining how skills function
- **Weapon Skills:** Combat skills tied to equipped weapon type
- **Stat Caps:** Each skill has a governing stat that limits maximum level

---

## Data Structures

### CSkill Class

**File:** `Skill.h`

```cpp
class CSkill {
public:
    CSkill();

    char  m_cName[21];      // Skill name (20 chars max + null)
    short m_sType;          // Effect type (1=GET, 2=PRETEND, 3=TAMING)
    short m_sValue1;        // Effect parameter 1
    short m_sValue2;        // Effect parameter 2
    short m_sValue3;        // Effect parameter 3
    short m_sValue4;        // Effect parameter 4
    short m_sValue5;        // Effect parameter 5
    short m_sValue6;        // Effect parameter 6
};
```

### Skill Configuration Storage (CGame)

**File:** `Game.h`

```cpp
class CGame {
    // Skill configuration list - loaded from config files
    CSkill* m_pSkillConfigList[DEF_MAXSKILLTYPE];  // Index 0-59

    // SSN threshold lookup table - calculated at startup
    int m_iSkillSSNpoint[102];  // SSN needed for each level 0-100
};
```

---

## Skill Types Reference

### Combat Skills (Weapon Proficiencies)

| Index | Name | Governing Stat | Description |
|-------|------|----------------|-------------|
| 5 | Hand-to-Hand | STR | Unarmed combat (Fist) |
| 6 | Long Sword | DEX | Standard sword proficiency |
| 7 | Short Sword | DEX | Dagger proficiency |
| 8 | Fencing | DEX | Rapier/thin blade proficiency |
| 9 | Axe | DEX | Axe weapon proficiency |
| 10 | Hammer | DEX | Blunt weapon proficiency |
| 11 | Staff | DEX | Staff weapon proficiency |
| 12 | Bow | DEX | Ranged archery |
| 14 | Two-Handed | DEX | Two-handed sword proficiency |
| 21 | Wand | MAG | Magic wand proficiency |

### Magic Skills

| Index | Name | Governing Stat | Description |
|-------|------|----------------|-------------|
| 3 | Magic Resistance | Level | Resistance to magical damage |
| 4 | Magic | MAG | Spellcasting ability |

### Crafting Skills

| Index | Name | Governing Stat | Description |
|-------|------|----------------|-------------|
| 0 | Mining | STR | Ore extraction |
| 1 | Fishing | DEX | Fish catching |
| 2 | Manufacturing | DEX | Item crafting |
| 15 | Farming | VIT | Agriculture |

### Defensive Skills

| Index | Name | Governing Stat | Description |
|-------|------|----------------|-------------|
| 13 | Shield | STR | Shield blocking |
| 19 | Physical Resistance | VIT | Physical damage resistance |

### Special Skills

| Index | Name | Governing Stat | Description |
|-------|------|----------------|-------------|
| 16 | Pretend Corpse | INT | Play dead ability |
| 17 | Critical Hit | DEX | Critical strike chance |

---

## Skill Effect Types

Skills are categorized by their effect type, which determines how they function:

### Type 1: GET (Gathering)

```cpp
#define DEF_SKILLEFFECTTYPE_GET  1
```

**Purpose:** Item gathering and extraction skills

**Skills Using This Type:**
- Mining (ore extraction)
- Fishing (catching fish)
- Manufacturing (creating items)
- Alchemy (potion creation)

**Behavior:**
- Success based on skill level roll
- Produces items on success
- May have cooldowns or resource consumption

### Type 2: PRETEND (Play Dead)

```cpp
#define DEF_SKILLEFFECTTYPE_PRETEND  2
```

**Purpose:** Fake death to avoid combat

**Skills Using This Type:**
- Pretend Corpse

**Behavior:**
```cpp
// From UseSkillHandler
case DEF_SKILLEFFECTTYPE_PRETEND:
    switch (m_pSkillConfigList[iV1]->m_sValue1) {
    case 1:
        // Cannot use in fight zones
        if (m_pMapList[m_pClientList[iClientH]->m_cMapIndex]->m_bIsFightZone == TRUE) {
            SendNotifyMsg(NULL, iClientH, DEF_NOTIFY_SKILLUSINGEND, NULL, NULL, NULL, NULL);
            return;
        }
        // Check for actual corpses nearby (cannot pretend if corpses exist)
        // If valid, set invisibility/pretend status
        break;
    }
    break;
```

**Restrictions:**
- Cannot be used in fight zones
- Cannot be used if actual corpses are nearby
- Cancelled by movement or action

### Type 3: TAMING (Creature Control)

```cpp
#define DEF_SKILLEFFECTTYPE_TAMING  3
```

**Purpose:** Tame and summon creatures

**Skills Using This Type:**
- Taming (through Magic skill)

**Behavior:**
- Searches area for tameable NPCs
- Success based on skill level vs creature difficulty
- Tamed creatures follow and assist player

---

## Per-Player Skill Data

### Storage in CClient

**File:** `Client.h`

```cpp
class CClient {
    // Skill mastery levels (0-100 for each skill)
    unsigned char m_cSkillMastery[DEF_MAXSKILLTYPE];  // [60]

    // Skill experience (SSN - Skill Special Number)
    int m_iSkillSSN[DEF_MAXSKILLTYPE];  // [60]

    // Active skill flags (TRUE if skill currently in use)
    BOOL m_bSkillUsingStatus[DEF_MAXSKILLTYPE];  // [60]

    // Skill timer IDs for duration tracking (v1.12)
    int m_iSkillUsingTimeID[DEF_MAXSKILLTYPE];  // [60]

    // Currently equipped weapon's skill index
    short m_sUsingWeaponSkill;

    // Down skill (recovery move when knocked down)
    int m_iDownSkillIndex;

    // Super attack tracking
    int m_iSuperAttackLeft;    // Remaining super attacks
    int m_iSuperAttackCount;   // Current combo counter
};
```

### Initialization

When a new character is created or loaded:

```cpp
// Initialize all skills to 0
for (int i = 0; i < DEF_MAXSKILLTYPE; i++) {
    m_cSkillMastery[i] = 0;
    m_iSkillSSN[i] = 0;
    m_bSkillUsingStatus[i] = FALSE;
    m_iSkillUsingTimeID[i] = NULL;
}

m_sUsingWeaponSkill = 0;
m_iDownSkillIndex = -1;
m_iSuperAttackLeft = 0;
m_iSuperAttackCount = 0;
```

---

## Skill Learning & Training

### Training from NPCs

Players learn skills from NPC trainers. The process involves:

1. Player interacts with skill trainer NPC
2. Server validates prerequisites (gold, stat requirements)
3. Training request sent to log server
4. Response processed by `TrainSkillResponse()`

### TrainSkillResponse Function

```cpp
void CGame::TrainSkillResponse(BOOL bSuccess, int iClientH, int iSkillNum, int iSkillLevel) {
    // Validate client exists and is fully initialized
    if (m_pClientList[iClientH] == NULL) return;
    if (m_pClientList[iClientH]->m_bIsInitComplete == FALSE) return;

    // Validate skill parameters
    if ((iSkillNum < 0) || (iSkillNum > 100)) return;
    if ((iSkillLevel < 0) || (iSkillLevel > 100)) return;

    if (bSuccess == TRUE) {
        // Can only learn skills not already known
        if (m_pClientList[iClientH]->m_cSkillMastery[iSkillNum] != 0) return;

        // Assign initial skill level from training
        m_pClientList[iClientH]->m_cSkillMastery[iSkillNum] = iSkillLevel;

        // Validate total skill points don't exceed limit
        bCheckTotalSkillMasteryPoints(iClientH, iSkillNum);

        // Notify client of new skill
        SendNotifyMsg(NULL, iClientH, DEF_NOTIFY_SKILLTRAINRESULT,
                      iSkillNum, iSkillLevel, NULL, NULL);
    }
}
```

### Skill Point Validation

```cpp
BOOL CGame::bCheckTotalSkillMasteryPoints(int iClientH, int iSkill) {
    int iTotalPoints = 0;

    // Sum all skill mastery levels
    for (int i = 1; i < DEF_MAXSKILLTYPE; i++) {
        iTotalPoints += m_pClientList[iClientH]->m_cSkillMastery[i];
    }

    // Check against maximum (700)
    if (iTotalPoints > DEF_MAXSKILLPOINTS) {
        // Reduce the newly added skill to fit within limit
        int iExcess = iTotalPoints - DEF_MAXSKILLPOINTS;
        m_pClientList[iClientH]->m_cSkillMastery[iSkill] -= iExcess;

        if (m_pClientList[iClientH]->m_cSkillMastery[iSkill] < 0) {
            m_pClientList[iClientH]->m_cSkillMastery[iSkill] = 0;
        }

        return FALSE;
    }

    return TRUE;
}
```

---

## Skill Advancement System

### SSN (Skill Special Number) Explained

SSN is the experience system for individual skills. Each skill action generates SSN points, and when enough are accumulated, the skill levels up.

### SSN Threshold Calculation

```cpp
int CGame::_iCalcSkillSSNpoint(int iLevel) {
    if (iLevel < 1) return 1;

    // Linear progression up to level 50
    if (iLevel <= 50) {
        return iLevel;
    }
    // Double cost for levels 51-100
    else if (iLevel > 50) {
        return (iLevel * 2);
    }
}
```

### SSN Threshold Table

| Level Range | SSN Required | Formula |
|-------------|--------------|---------|
| 1-50 | 1-50 | Level |
| 51-100 | 102-200 | Level * 2 |

### SSN Accumulation and Level-Up

```cpp
void CGame::CalculateSSN_SkillIndex(int iClientH, short sSkillIndex, int iValue) {
    // Validate parameters
    if (m_pClientList[iClientH] == NULL) return;
    if (m_pClientList[iClientH]->m_cSkillMastery[sSkillIndex] == 0) return;
    if ((sSkillIndex < 0) || (sSkillIndex >= DEF_MAXSKILLTYPE)) return;

    // Store old SSN for potential rollback
    int iOldSSN = m_pClientList[iClientH]->m_iSkillSSN[sSkillIndex];

    // Add new SSN points
    m_pClientList[iClientH]->m_iSkillSSN[sSkillIndex] += iValue;

    // Get threshold for next level
    int iCurrentLevel = m_pClientList[iClientH]->m_cSkillMastery[sSkillIndex];
    int iSSNpoint = m_iSkillSSNpoint[iCurrentLevel + 1];

    // Check for level-up (max level is 100)
    if ((iCurrentLevel < 100) &&
        (m_pClientList[iClientH]->m_iSkillSSN[sSkillIndex] > iSSNpoint)) {

        // Increment skill level
        m_pClientList[iClientH]->m_cSkillMastery[sSkillIndex]++;

        // Check stat-based caps (see next section)
        // If cap exceeded, rollback level and SSN
        // Otherwise, reset SSN to 0

        // Notify client of level-up
        SendNotifyMsg(NULL, iClientH, DEF_NOTIFY_SKILLUP,
                      sSkillIndex,
                      m_pClientList[iClientH]->m_cSkillMastery[sSkillIndex],
                      NULL, NULL);
    }
}
```

### SSN Generation Events

SSN is earned through various activities:

| Skill | SSN Trigger | SSN Amount |
|-------|-------------|------------|
| Weapon Skills | Hit an enemy | 1-3 based on damage |
| Mining | Mine ore successfully | 1-2 |
| Fishing | Catch fish | 1-2 |
| Manufacturing | Craft item | 1-5 based on complexity |
| Magic | Cast spell | 1-3 based on spell level |
| Shield | Block attack | 1-2 |

---

## Skill Usage & Activation

### UseSkillHandler

Main function for activating skills:

```cpp
void CGame::UseSkillHandler(int iClientH, int iV1, int iV2, int iV3) {
    // Parameters:
    // iV1 = skill index
    // iV2 = target X coordinate
    // iV3 = target Y coordinate

    // Validate client
    if (m_pClientList[iClientH] == NULL) return;

    // Validate skill index
    if ((iV1 < 0) || (iV1 >= DEF_MAXSKILLTYPE)) return;

    // Validate skill is configured
    if (m_pSkillConfigList[iV1] == NULL) return;

    // Check if skill already in use (prevent spam)
    if (m_pClientList[iClientH]->m_bSkillUsingStatus[iV1] == TRUE) return;

    // Get player's skill level
    int iPlayerSkillLevel = m_pClientList[iClientH]->m_cSkillMastery[iV1];

    // Success roll: 1d100 vs skill level
    int iResult = iDice(1, 100);

    if (iResult > iPlayerSkillLevel) {
        // FAILED - skill level too low for this roll
        SendNotifyMsg(NULL, iClientH, DEF_NOTIFY_SKILLUSINGEND, NULL, NULL, NULL, NULL);
        return;
    }

    // SUCCESS - process skill effect based on type
    switch (m_pSkillConfigList[iV1]->m_sType) {

    case DEF_SKILLEFFECTTYPE_GET:
        // Gathering skills - handled elsewhere
        break;

    case DEF_SKILLEFFECTTYPE_PRETEND:
        // Pretend corpse (play dead)
        ProcessPretendCorpse(iClientH, iV1);
        break;

    case DEF_SKILLEFFECTTYPE_TAMING:
        // Creature taming
        _TamingHandler(iClientH, iV1,
                       m_pClientList[iClientH]->m_cMapIndex,
                       iV2, iV3);
        break;
    }

    // Mark skill as in use
    m_pClientList[iClientH]->m_bSkillUsingStatus[iV1] = TRUE;
}
```

### Skill Success Formula

```
Success = (1d100) <= Skill_Level

Where:
- 1d100 = Random roll 1-100
- Skill_Level = Current mastery (0-100)

Examples:
- Level 50 skill: 50% success rate
- Level 80 skill: 80% success rate
- Level 100 skill: 100% success rate (always succeeds)
```

### ClearSkillUsingStatus

Ends an active skill:

```cpp
void CGame::ClearSkillUsingStatus(int iClientH) {
    if (m_pClientList[iClientH] == NULL) return;

    for (int i = 0; i < DEF_MAXSKILLTYPE; i++) {
        m_pClientList[iClientH]->m_bSkillUsingStatus[i] = FALSE;
        m_pClientList[iClientH]->m_iSkillUsingTimeID[i] = NULL;
    }

    // Notify client that all skills are now available
    SendNotifyMsg(NULL, iClientH, DEF_NOTIFY_SKILLUSINGEND, NULL, NULL, NULL, NULL);
}
```

---

## Weapon Skill System

### Weapon-to-Skill Mapping

When a weapon is equipped, the server determines which skill applies:

```cpp
short CGame::_iGetWeaponSkillType(int iClientH) {
    // Check equipped weapon (slot DEF_EQUIPPOS_RHAND or DEF_EQUIPPOS_TWOHAND)
    int iWeaponIndex = m_pClientList[iClientH]->m_sItemEquipmentStatus[DEF_EQUIPPOS_RHAND];

    if (iWeaponIndex == -1) {
        // No weapon equipped - use Hand-to-Hand
        return 5;
    }

    CItem* pWeapon = m_pClientList[iClientH]->m_pItemList[iWeaponIndex];

    // Determine skill from weapon category
    switch (pWeapon->m_sCategory) {
        case DEF_ITEMCAT_SWORD:
            return 6;   // Long Sword
        case DEF_ITEMCAT_SHORTSWORD:
            return 7;   // Short Sword/Dagger
        case DEF_ITEMCAT_AXE:
            return 9;   // Axe
        case DEF_ITEMCAT_HAMMER:
            return 10;  // Hammer
        case DEF_ITEMCAT_STAFF:
            return 11;  // Staff
        case DEF_ITEMCAT_BOW:
            return 12;  // Bow
        case DEF_ITEMCAT_TWOHANDSWORD:
            return 14;  // Two-Handed Sword
        case DEF_ITEMCAT_WAND:
            return 21;  // Wand
        default:
            return 5;   // Default to Hand-to-Hand
    }
}
```

### Weapon Skill Effects on Combat

The equipped weapon skill affects:

1. **Attack Power Bonus:**
   ```cpp
   int iSkillBonus = m_pClientList[iClientH]->m_cSkillMastery[iWeaponSkill] / 10;
   iAttackPower += iSkillBonus;
   ```

2. **Hit Ratio Bonus:**
   ```cpp
   int iHitBonus = m_pClientList[iClientH]->m_cSkillMastery[iWeaponSkill] / 5;
   iHitRatio += iHitBonus;
   ```

3. **Critical Hit Chance:**
   ```cpp
   int iCritBonus = m_pClientList[iClientH]->m_cSkillMastery[iWeaponSkill] / 20;
   iCriticalRate += iCritBonus;
   ```

---

## Combo Attack System

### Combo Counter Tracking

Players build combo counts by landing consecutive hits:

```cpp
// In CClient
int m_iComboAttackCount;  // Current combo count (1-6)
```

### Combo Bonus Calculation

```cpp
int CGame::iGetComboAttackBonus(int iSkill, int iComboCount) {
    // No bonus for first hit or invalid counts
    if (iComboCount <= 1) return 0;
    if (iComboCount > 6) return 0;

    // Lookup bonus by weapon skill type
    switch (iSkill) {
    case 5:   // Hand-to-hand
        return ___iCAB5[iComboCount];
    case 6:   // Long sword
        return ___iCAB6[iComboCount];
    case 7:   // Short sword
        return ___iCAB7[iComboCount];
    case 8:   // Fencing
        return ___iCAB8[iComboCount];
    case 9:   // Axe
        return ___iCAB9[iComboCount];
    case 10:  // Hammer
        return ___iCAB10[iComboCount];
    case 14:  // Two-handed (uses sword table)
        return ___iCAB6[iComboCount];
    case 21:  // Wand (uses hammer table)
        return ___iCAB10[iComboCount];
    }

    return 0;
}
```

### Combo Bonus Tables

```cpp
// Damage bonus per combo count
// Index: [0=unused][1=first][2=second][3=third][4=fourth][5=fifth][6=sixth]

static int ___iCAB5[]  = {0, 0, 1, 2, 3, 4, 5};  // Hand-to-hand
static int ___iCAB6[]  = {0, 0, 1, 2, 3, 4, 5};  // Long sword / Two-handed
static int ___iCAB7[]  = {0, 0, 2, 3, 4, 5, 6};  // Short sword (faster buildup)
static int ___iCAB8[]  = {0, 0, 1, 3, 4, 5, 6};  // Fencing
static int ___iCAB9[]  = {0, 0, 1, 2, 4, 5, 7};  // Axe (heavy finisher)
static int ___iCAB10[] = {0, 0, 1, 2, 3, 4, 5};  // Hammer / Wand
```

### Combo Mechanics

1. **Starting a Combo:** First hit sets `m_iComboAttackCount = 1`
2. **Building Combo:** Each consecutive hit increments counter (max 6)
3. **Combo Break:** Missing or switching targets resets to 0
4. **Damage Application:** `FinalDamage += iGetComboAttackBonus(skill, combo)`

---

## Super Attack System

### Super Attack Overview

Super attacks are powerful enhanced strikes that consume special resources:

```cpp
// In CClient
int m_iSuperAttackLeft;   // Remaining super attack charges
int m_iSuperAttackCount;  // For tracking (may differ from combo)
```

### Super Attack Trigger

Super attacks are activated when attack mode >= 20:

```cpp
// In damage calculation
if (m_iSuperAttackLeft > 0 && iAttackMode >= 20) {
    // Apply super attack bonuses
    ApplySuperAttackBonus(iClientH, &iAttackPower, &iHitRatio);

    // Consume one charge
    m_pClientList[iClientH]->m_iSuperAttackLeft--;
}
```

### Super Attack Bonuses

```cpp
void CGame::ApplySuperAttackBonus(int iClientH, int* pAP, int* pHitRatio) {
    int iLevel = m_pClientList[iClientH]->m_iLevel;
    short iWeaponSkill = m_pClientList[iClientH]->m_sUsingWeaponSkill;

    // Base bonuses (all weapons)
    *pAP += (*pAP * iLevel / 100);  // Level-based % bonus
    *pHitRatio += 100;               // Flat +100 hit bonus

    // Weapon-specific bonuses
    switch (iWeaponSkill) {
    case 6:   // Long sword
    case 8:   // Fencing
        *pAP += (*pAP / 10);        // +10% damage
        *pHitRatio += 30;           // +30 hit
        break;

    case 9:   // Axe
    case 14:  // Two-handed
        *pAP += (*pAP / 5);         // +20% damage
        break;

    case 10:  // Hammer
        *pAP += (*pAP / 5);         // +20% damage
        *pHitRatio += 50;           // +50 hit
        break;

    case 21:  // Wand
        // Magic-based bonus calculated separately
        break;
    }
}
```

### Super Attack Bonus Summary

| Weapon Skill | AP Bonus | Hit Bonus | Notes |
|--------------|----------|-----------|-------|
| All | +Level% | +100 | Base bonus |
| Long Sword | +10% | +30 | Balanced |
| Fencing | +10% | +30 | Balanced |
| Axe | +20% | +0 | Heavy damage |
| Two-Handed | +20% | +0 | Heavy damage |
| Hammer | +20% | +50 | High accuracy |
| Wand | Magic-based | Magic-based | Special |

---

## Down Skill (Recovery Move)

### Overview

When a player is knocked down, they can execute a designated "down skill" to recover and counterattack.

### Setting Down Skill

```cpp
void CGame::SetDownSkillIndexHandler(int iClientH, int iSkillIndex) {
    if (m_pClientList[iClientH] == NULL) return;

    // Validate skill index
    if ((iSkillIndex < 0) || (iSkillIndex >= DEF_MAXSKILLTYPE)) return;

    // Can only set a skill the player knows
    if (m_pClientList[iClientH]->m_cSkillMastery[iSkillIndex] > 0) {
        m_pClientList[iClientH]->m_iDownSkillIndex = iSkillIndex;
    }

    // Notify client of current down skill
    SendNotifyMsg(NULL, iClientH, DEF_NOTIFY_DOWNSKILLINDEXSET,
                  m_pClientList[iClientH]->m_iDownSkillIndex,
                  NULL, NULL, NULL);
}
```

### Down Skill Activation

When player is knocked down and inputs recovery command:

```cpp
void CGame::ExecuteDownSkill(int iClientH) {
    int iDownSkill = m_pClientList[iClientH]->m_iDownSkillIndex;

    if (iDownSkill < 0) return;  // No down skill set

    int iSkillLevel = m_pClientList[iClientH]->m_cSkillMastery[iDownSkill];
    if (iSkillLevel <= 0) return;  // Skill not learned

    // Execute the weapon skill as a recovery attack
    // Usually hits nearby enemies with reduced power

    // Success roll (harder than normal)
    int iResult = iDice(1, 100);
    if (iResult > (iSkillLevel / 2)) {
        // Failed recovery
        return;
    }

    // Success - apply recovery attack
    // ... damage nearby enemies
    // ... restore player to standing state
}
```

### Down Skill Storage

```cpp
// In CClient
int m_iDownSkillIndex;  // -1 = none, otherwise skill index (5-14, 21 for weapons)
```

---

## Taming System

### Overview

The taming system allows players to temporarily control NPC creatures.

### Taming Handler

```cpp
void CGame::_TamingHandler(int iClientH, int iSkillNum, char cMapIndex, int dX, int dY) {
    if (m_pClientList[iClientH] == NULL) return;

    // Get player's taming skill level
    int iSkillLevel = (int)m_pClientList[iClientH]->m_cSkillMastery[iSkillNum];

    // Calculate search range from skill level
    int iRange = iSkillLevel / 12;  // Max range ~8 tiles at level 100
    if (iRange < 1) iRange = 1;

    // Maximum followers based on skill level
    int iMaxFollowers = iSkillLevel / 20;  // Max 5 at level 100
    if (iMaxFollowers < 1) iMaxFollowers = 1;

    // Scan area for tameable NPCs
    for (int iX = dX - iRange; iX <= dX + iRange; iX++) {
        for (int iY = dY - iRange; iY <= dY + iRange; iY++) {
            // Check if valid tile
            if (!bIsValidPosition(cMapIndex, iX, iY)) continue;

            // Get NPC at position
            int iNpcH = GetNpcAtPosition(cMapIndex, iX, iY);
            if (iNpcH == 0) continue;

            CNpc* pNpc = m_pNpcList[iNpcH];
            if (pNpc == NULL) continue;

            // Check if tameable (not already tamed, not boss, etc.)
            if (!bIsTameable(pNpc)) continue;

            // Get taming difficulty from NPC type
            int iTamingLevel = GetNpcTamingDifficulty(pNpc->m_sType);

            // Taming success roll
            int iRoll = iDice(1, iSkillLevel / 10);
            int iThreshold = iSkillLevel / 20;

            if (iRoll < iThreshold) {
                // Failed taming attempt
                continue;
            }

            if ((iSkillLevel / 10) < iTamingLevel) {
                // Skill too low for this creature
                continue;
            }

            // Success! Tame the creature
            TameCreature(iClientH, iNpcH);

            // Check follower limit
            if (GetFollowerCount(iClientH) >= iMaxFollowers) {
                break;  // Can't tame more
            }
        }
    }
}
```

### Taming Difficulty by Creature

| Difficulty | Example Creatures |
|------------|-------------------|
| 1 | Slimes, Rabbits |
| 2-3 | Wolves, Zombies |
| 4-5 | Orcs, Skeletons |
| 6-7 | Trolls, Ogres |
| 8-9 | Demons, Wyverns |
| 10 | Bosses (usually untameable) |

### Tamed Creature Behavior

- Follows player within range
- Attacks player's targets
- Despawns after time limit (`DEF_SUMMONTIME` = 5 minutes)
- Dies if player disconnects

---

## Stat-Based Skill Caps

### Governing Stats

Each skill has a governing stat that limits maximum level:

```
Maximum Skill Level = Governing_Stat * 2
```

### Stat Mapping

```cpp
// In CalculateSSN_SkillIndex, after level-up:
switch (sSkillIndex) {

// STR-based skills
case 0:    // Mining
case 5:    // Hand-to-Hand (Fist)
case 13:   // Shield
    iStatCap = m_pClientList[iClientH]->m_iStr * 2;
    break;

// DEX-based skills
case 1:    // Fishing
case 2:    // Manufacturing
case 6:    // Long Sword
case 7:    // Short Sword
case 8:    // Fencing
case 9:    // Axe
case 10:   // Hammer
case 11:   // Staff
case 12:   // Bow
case 14:   // Two-handed
case 17:   // Critical Hit
    iStatCap = m_pClientList[iClientH]->m_iDex * 2;
    break;

// MAG-based skills
case 4:    // Magic
case 21:   // Wand
    iStatCap = m_pClientList[iClientH]->m_iMag * 2;
    break;

// VIT-based skills
case 15:   // Farming
case 19:   // Physical Resistance
    iStatCap = m_pClientList[iClientH]->m_iVit * 2;
    break;

// INT-based skills
case 16:   // Pretend Corpse
    iStatCap = m_pClientList[iClientH]->m_iInt * 2;
    break;

// Level-based skills
case 3:    // Magic Resistance
    iStatCap = m_pClientList[iClientH]->m_iLevel * 2;
    break;
}

// Apply cap
if (m_pClientList[iClientH]->m_cSkillMastery[sSkillIndex] > iStatCap) {
    // Rollback level-up
    m_pClientList[iClientH]->m_cSkillMastery[sSkillIndex]--;
    m_pClientList[iClientH]->m_iSkillSSN[sSkillIndex] = iOldSSN;
}
```

### Stat Cap Summary

| Skill Category | Governing Stat | Max at Stat 50 |
|----------------|----------------|----------------|
| Mining, Shield, Fist | STR | 100 |
| Weapon Skills, Fishing, Crafting | DEX | 100 |
| Magic, Wand | MAG | 100 |
| Farming, Physical Resistance | VIT | 100 |
| Pretend Corpse | INT | 100 |
| Magic Resistance | Level | 100 (at level 50) |

---

## Configuration Format

### Skill Configuration File

Skills are loaded from configuration files sent by the log server.

### Message Processing

```cpp
BOOL CGame::_bDecodeSkillConfigFileContents(char* pData, DWORD dwMsgSize) {
    // Message ID: MSGID_SKILLCONFIGURATIONCONTENTS

    CStrTok* pStrTok = new CStrTok(pData, seps);
    char* token;

    int iModeA = 0;  // Outer mode (0=outside, 1=in skill block)
    int iModeB = 0;  // Inner mode (field index)
    int iSkillIndex = 0;

    while ((token = pStrTok->pGet()) != NULL) {
        if (iModeA == 0) {
            // Looking for [SKILL] block
            if (memcmp(token, "[SKILL]", 7) == 0) {
                iModeA = 1;
                iModeB = 1;
            }
        }
        else if (iModeA == 1) {
            // Inside skill block
            switch (iModeB) {
            case 1:  // Skill index
                iSkillIndex = atoi(token);
                if (iSkillIndex < 0 || iSkillIndex >= DEF_MAXSKILLTYPE) {
                    return FALSE;
                }
                m_pSkillConfigList[iSkillIndex] = new CSkill;
                iModeB = 2;
                break;

            case 2:  // Skill name
                strncpy(m_pSkillConfigList[iSkillIndex]->m_cName, token, 20);
                iModeB = 3;
                break;

            case 3:  // Effect type
                m_pSkillConfigList[iSkillIndex]->m_sType = atoi(token);
                iModeB = 4;
                break;

            case 4:  // Value1
                m_pSkillConfigList[iSkillIndex]->m_sValue1 = atoi(token);
                iModeB = 5;
                break;

            // ... cases 5-9 for Value2-Value6 ...

            case 9:  // Value6 (last field)
                m_pSkillConfigList[iSkillIndex]->m_sValue6 = atoi(token);
                iModeB = 1;  // Ready for next skill
                break;
            }
        }
    }

    delete pStrTok;
    return TRUE;
}
```

### Expected Configuration Format

```
[SKILL]
0 = Mining 1 0 0 0 0 0 0
1 = Fishing 1 0 0 0 0 0 0
2 = Manufacturing 1 0 0 0 0 0 0
3 = MagicResistance 0 0 0 0 0 0 0
4 = Magic 0 0 0 0 0 0 0
5 = HandToHand 0 0 0 0 0 0 0
6 = LongSword 0 0 0 0 0 0 0
7 = ShortSword 0 0 0 0 0 0 0
8 = Fencing 0 0 0 0 0 0 0
9 = Axe 0 0 0 0 0 0 0
10 = Hammer 0 0 0 0 0 0 0
11 = Staff 0 0 0 0 0 0 0
12 = Bow 0 0 0 0 0 0 0
13 = Shield 0 0 0 0 0 0 0
14 = TwoHanded 0 0 0 0 0 0 0
15 = Farming 1 0 0 0 0 0 0
16 = PretendCorpse 2 1 0 0 0 0 0
...
```

### Field Definitions

| Field | Description |
|-------|-------------|
| Index | Skill array index (0-59) |
| Name | Display name (20 chars max) |
| Type | Effect type (1=GET, 2=PRETEND, 3=TAMING) |
| Value1-6 | Type-specific parameters |

---

## Key Functions Reference

### Skill Learning

| Function | Purpose |
|----------|---------|
| `TrainSkillResponse(BOOL, int, int, int)` | Process skill learning from NPC trainer |
| `_iGetSkillNumber(char*)` | Lookup skill index by name |
| `bCheckTotalSkillMasteryPoints(int, int)` | Validate total points within limit |

### Skill Progression

| Function | Purpose |
|----------|---------|
| `CalculateSSN_SkillIndex(int, short, int)` | Add SSN and check for level-up |
| `_iCalcSkillSSNpoint(int)` | Calculate SSN threshold for level |
| `m_iSkillSSNpoint[102]` | Pre-calculated SSN thresholds |

### Skill Usage

| Function | Purpose |
|----------|---------|
| `UseSkillHandler(int, int, int, int)` | Execute skill with target |
| `ClearSkillUsingStatus(int)` | End all active skill effects |
| `SetDownSkillIndexHandler(int, int)` | Set recovery move skill |

### Weapon Skills

| Function | Purpose |
|----------|---------|
| `_iGetWeaponSkillType(int)` | Get skill index for equipped weapon |
| `iGetComboAttackBonus(int, int)` | Calculate combo damage bonus |

### Special Skills

| Function | Purpose |
|----------|---------|
| `_TamingHandler(int, int, char, int, int)` | Process taming skill use |

### Configuration

| Function | Purpose |
|----------|---------|
| `_bDecodeSkillConfigFileContents(char*, DWORD)` | Parse skill config from log server |

---

## Constants Reference

### Limits

| Constant | Value | Description |
|----------|-------|-------------|
| `DEF_MAXSKILLTYPE` | 60 | Maximum skill types |
| `DEF_MAXSKILLPOINTS` | 700 | Total skill point cap |
| `DEF_MAXCOMBOCOUNT` | 6 | Maximum combo chain |

### Effect Types

| Constant | Value | Description |
|----------|-------|-------------|
| `DEF_SKILLEFFECTTYPE_GET` | 1 | Gathering/extraction |
| `DEF_SKILLEFFECTTYPE_PRETEND` | 2 | Play dead |
| `DEF_SKILLEFFECTTYPE_TAMING` | 3 | Creature control |

### Mastery Ranges

| Range | Description |
|-------|-------------|
| 0 | Not learned |
| 1-20 | Novice |
| 21-40 | Beginner |
| 41-60 | Apprentice |
| 61-80 | Journeyman |
| 81-90 | Expert |
| 91-99 | Master |
| 100 | Grand Master |

---

## Interactions with Other Systems

### Combat System

- Weapon skills affect damage and hit rates
- Combo bonuses applied during combat
- Super attacks consume resources
- Down skill for recovery attacks

### Magic System

- Magic skill affects spell success
- Magic Resistance reduces spell damage
- Wand skill for staff-type weapons

### Item System

- Weapons determine active weapon skill
- Some items grant skill bonuses
- Crafting consumes materials via skills

### NPC System

- NPC trainers teach skills
- Tamed NPCs follow player
- NPC difficulty affects taming

### Player System

- Stats cap skill levels
- Level affects Magic Resistance cap
- Skill points part of character data

### Experience System

- Skills generate SSN through use
- No direct EXP from skill usage
- Indirect EXP from skill-enabled activities

---

## Persistence Format

### Character Save Data

Skills are saved as part of character data:

```cpp
// Skill mastery array
for (int i = 0; i < DEF_MAXSKILLTYPE; i++) {
    // Save format: skill_index:mastery_level:ssn
    fprintf(pFile, "skill %d %d %d\n",
            i,
            m_cSkillMastery[i],
            m_iSkillSSN[i]);
}

// Down skill index
fprintf(pFile, "downskill %d\n", m_iDownSkillIndex);
```

---

## Known Issues and Edge Cases

1. **Skill Point Overflow:** If total exceeds 700, newest skill is reduced
2. **Stat Cap Race:** If stat decreases (debuff), skill may exceed cap temporarily
3. **Down Skill Validation:** Setting invalid skill index silently fails
4. **SSN Loss:** On level-up failure (stat cap), SSN is rolled back
5. **Taming Range:** At very low skill levels, range rounds to 1

---

## Modernization Notes

The modern implementation (`src/skill/skill.h`) introduces:

- Type-safe `skill_type` enum (24 defined skills)
- `skill_state` structure with cleaner interface
- Extended mastery levels (0-200 vs legacy 0-100)
- Simplified experience formula: `(level + 1) * 100`
- `player_skills` container with array storage
- Built-in level-up checking via `can_level_up()`

Key differences from legacy:
- No SSN system (simpler linear experience)
- Higher level cap (200 vs 100)
- Fewer skill types (24 vs 60)
- No stat-based caps (simplified)
