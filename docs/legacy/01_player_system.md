# Legacy Player System Documentation

**System:** Player System
**Primary Files:** `Client.cpp`, `Client.h`, `Player.h`, portions of `Game.cpp`
**Estimated Lines:** ~5,000+ across Game.cpp
**Complexity:** High

This document provides exhaustive documentation of the legacy Helbreath player system for use in modernization efforts.

---

## Table of Contents

1. [Overview](#1-overview)
2. [CClient Class Structure](#2-cclient-class-structure)
3. [Core Identity Fields](#3-core-identity-fields)
4. [Character Appearance](#4-character-appearance)
5. [Vital Statistics](#5-vital-statistics)
6. [Core Attributes](#6-core-attributes)
7. [Experience and Leveling](#7-experience-and-leveling)
8. [Combat Statistics](#8-combat-statistics)
9. [Weapon and Equipment](#9-weapon-and-equipment)
10. [Inventory System](#10-inventory-system)
11. [Magic and Skills](#11-magic-and-skills)
12. [Status Effects](#12-status-effects)
13. [Guild System](#13-guild-system)
14. [Party System](#14-party-system)
15. [Quest System](#15-quest-system)
16. [War and Territory](#16-war-and-territory)
17. [Special Abilities](#17-special-abilities)
18. [Item Modifiers](#18-item-modifiers)
19. [Admin and Moderation](#19-admin-and-moderation)
20. [Anti-Cheat System](#20-anti-cheat-system)
21. [Session Management](#21-session-management)
22. [Persistence Format](#22-persistence-format)
23. [Core Functions Reference](#23-core-functions-reference)
24. [Formulas and Calculations](#24-formulas-and-calculations)
25. [Constants Reference](#25-constants-reference)

---

## 1. Overview

The Player System manages all aspects of player characters in Helbreath, including:

- Character identity and appearance
- Vital statistics (HP, MP, SP)
- Core attributes (STR, INT, VIT, DEX, MAG, CHR, LCK)
- Experience, leveling, and stat point allocation
- Combat statistics and damage calculation
- Inventory and equipment (50 slots + 15 equipment positions)
- Bank storage (200 slots)
- Skill mastery (60 skill types)
- Magic mastery (100 spell types)
- Status effects and buffs
- Guild and party membership
- Quest progress
- PvP tracking and penalties
- Session state and anti-cheat

The player state is encapsulated in the `CClient` class, with most game logic residing in `Game.cpp`.

---

## 2. CClient Class Structure

The `CClient` class contains approximately 380+ member variables organized into functional groups. Each instance represents a connected player's complete game state.

### Class Declaration Pattern

```cpp
class CClient {
public:
    CClient();
    ~CClient();

    // Member variables (see sections below)
    // No significant methods - logic is in CGame
};
```

### Memory Layout

- Instance size: ~8KB estimated (due to fixed arrays)
- Allocation: Array of pointers `CClient* m_pClientList[DEF_MAXCLIENTS]`
- Maximum clients: 2,000 (`DEF_MAXCLIENTS`)

---

## 3. Core Identity Fields

### Character Identity

| Field | Type | Size | Description |
|-------|------|------|-------------|
| `m_cCharName` | `char[]` | 11 | Character name (10 chars + null) |
| `m_cAccountName` | `char[]` | 11 | Account name (10 chars + null) |
| `m_cAccountPassword` | `char[]` | 11 | Account password (stored plaintext!) |
| `m_sCharIDnum1` | `short` | 2 | Unique ID component 1 (item ownership) |
| `m_sCharIDnum2` | `short` | 2 | Unique ID component 2 (item ownership) |
| `m_sCharIDnum3` | `short` | 2 | Unique ID component 3 (item ownership) |

### Location

| Field | Type | Size | Description |
|-------|------|------|-------------|
| `m_cMapName` | `char[]` | 11 | Current map name (10 chars + null) |
| `m_cMapIndex` | `char` | 1 | Current map index (0-99) |
| `m_sX` | `short` | 2 | X coordinate on map |
| `m_sY` | `short` | 2 | Y coordinate on map |
| `m_cDir` | `char` | 1 | Facing direction (0-7) |
| `m_cLocation` | `char[]` | 11 | Named location string |

### Direction Values

```
0 = North
1 = Northeast
2 = East
3 = Southeast
4 = South
5 = Southwest
6 = West
7 = Northwest
```

---

## 4. Character Appearance

### Physical Attributes

| Field | Type | Description | Valid Range |
|-------|------|-------------|-------------|
| `m_cSex` | `char` | Gender | 0=Male, 1=Female |
| `m_cSkin` | `char` | Skin color | 0-2 |
| `m_cHairStyle` | `char` | Hair style | 0-7 |
| `m_cHairColor` | `char` | Hair color | 0-15 |
| `m_cUnderwear` | `char` | Underwear/base appearance | 0-3 |

### Visual Representation

| Field | Type | Description |
|-------|------|-------------|
| `m_sType` | `short` | Current appearance type (affected by polymorph) |
| `m_sOriginalType` | `short` | Original type (before transformation) |
| `m_sAppr1` | `short` | Appearance data 1 (equipment visuals) |
| `m_sAppr2` | `short` | Appearance data 2 (equipment visuals) |
| `m_sAppr3` | `short` | Appearance data 3 (equipment visuals) |
| `m_sAppr4` | `short` | Appearance data 4 (equipment visuals) |
| `m_iApprColor` | `int` | Appearance color (palette, v1.4+) |
| `m_cAura` | `char` | Aura/glow effect around character |

### Appearance Encoding

The `m_sAppr1-4` fields encode equipped item visuals in a bit-packed format:
- Appr1: Upper body, arms
- Appr2: Lower body, head
- Appr3: Weapon, shield
- Appr4: Special effects, accessories

---

## 5. Vital Statistics

### Current Values

| Field | Type | Description |
|-------|------|-------------|
| `m_iHP` | `int` | Current health points |
| `m_iMP` | `int` | Current mana points |
| `m_iSP` | `int` | Current stamina points |
| `m_iHPstock` | `int` | HP overflow/stock buffer |

### Maximum Value Formulas

```cpp
// Maximum HP
int iMaxHP = (m_iVit * 3) + (m_iLevel * 2) + (m_iStr / 2);
if (m_iSideEffect_MaxHPdown != 0) {
    iMaxHP -= (iMaxHP / m_iSideEffect_MaxHPdown);
}

// Maximum MP
int iMaxMP = (m_iMag * 2) + (m_iLevel * 2) + (m_iInt / 2);

// Maximum SP
int iMaxSP = (m_iStr * 2) + (m_iLevel * 2);
```

### Regeneration Timers

| Field | Type | Interval | Description |
|-------|------|----------|-------------|
| `m_dwHPTime` | `DWORD` | 15000ms | Last HP regen tick |
| `m_dwMPTime` | `DWORD` | 20000ms | Last MP regen tick |
| `m_dwSPTime` | `DWORD` | 10000ms | Last SP regen tick |

### HP Regeneration Algorithm

```cpp
void TimeHitPointsUp(int iClientH) {
    // Check preconditions
    if (m_bIsKilled) return;
    if (!m_bIsInitComplete) return;
    if (m_iHungerStatus <= 0) return;
    if (m_bSkillUsingStatus[19]) return;  // Skill 19 blocks regen

    // Calculate interval with hunger modifier
    int iPlusTime = (30 - m_iHungerStatus) * 1000;  // 0-30 seconds extra

    if (dwCurrentTime - m_dwHPTime > DEF_HPUPTIME + iPlusTime) {
        int iMaxHP = iGetMaxHP(iClientH);

        if (m_iHP < iMaxHP) {
            // Roll for recovery amount
            int iTemp = iDice(1, m_iVit);
            if (iTemp < m_iVit / 2) {
                iTemp = m_iVit / 2;
            }

            // Apply side effect reduction
            if (m_iSideEffect_MaxHPdown != 0) {
                iTemp -= (iTemp / m_iSideEffect_MaxHPdown);
            }

            // Apply recovery (capped at max)
            m_iHP += iTemp;
            if (m_iHP > iMaxHP) m_iHP = iMaxHP;

            // Notify client
            SendNotifyMsg(iClientH, DEF_NOTIFY_HP, m_iHP, 0, 0, NULL);
        }

        m_dwHPTime = dwCurrentTime;
    }
}
```

### MP Regeneration Algorithm

```cpp
void TimeManaPointsUp(int iClientH) {
    if (m_bIsKilled) return;
    if (!m_bIsInitComplete) return;
    if (m_iHungerStatus <= 0) return;
    if (m_bSkillUsingStatus[19]) return;

    int iPlusTime = (30 - m_iHungerStatus) * 1000;

    if (dwCurrentTime - m_dwMPTime > DEF_MPUPTIME + iPlusTime) {
        int iMaxMP = iGetMaxMP(iClientH);

        if (m_iMP < iMaxMP) {
            int iTotal = iDice(1, m_iMag / 2);

            m_iMP += iTotal;
            if (m_iMP > iMaxMP) m_iMP = iMaxMP;

            SendNotifyMsg(iClientH, DEF_NOTIFY_MP, m_iMP, 0, 0, NULL);
        }

        m_dwMPTime = dwCurrentTime;
    }
}
```

### SP Regeneration Algorithm

```cpp
void TimeStaminarPointsUp(int iClientH) {
    if (m_bIsKilled) return;
    if (!m_bIsInitComplete) return;
    if (m_iHungerStatus <= 0) return;
    if (m_bSkillUsingStatus[19]) return;

    int iPlusTime = (30 - m_iHungerStatus) * 1000;

    if (dwCurrentTime - m_dwSPTime > DEF_SPUPTIME + iPlusTime) {
        int iMaxSP = iGetMaxSP(iClientH);

        if (m_iSP < iMaxSP) {
            // Complex calculation based on STR and VIT
            int iTotal = iDice(1, m_iStr / 10);
            if (m_iVit > 50) iTotal += (m_iVit - 50) / 5;

            m_iSP += iTotal;
            if (m_iSP > iMaxSP) m_iSP = iMaxSP;
            if (m_iSP < 0) m_iSP = 0;

            SendNotifyMsg(iClientH, DEF_NOTIFY_SP, m_iSP, 0, 0, NULL);
        }

        m_dwSPTime = dwCurrentTime;
    }
}
```

---

## 6. Core Attributes

### Base Attributes

| Field | Type | Min | Max | Description |
|-------|------|-----|-----|-------------|
| `m_iStr` | `int` | 10 | 1000 | Strength - Physical damage, carry weight |
| `m_iDex` | `int` | 10 | 1000 | Dexterity - Accuracy, dodge |
| `m_iInt` | `int` | 10 | 1000 | Intelligence - Magic power |
| `m_iVit` | `int` | 10 | 1000 | Vitality - HP, HP regen |
| `m_iMag` | `int` | 10 | 1000 | Magic - MP, spell casting |
| `m_iCharisma` | `int` | 10 | 1000 | Charisma - NPC interactions |
| `m_iLuck` | `int` | 10 | 1000 | Luck - Critical hits, drops |

### Level and Progression

| Field | Type | Description |
|-------|------|-------------|
| `m_iLevel` | `int` | Current level (1-180) |
| `m_iLU_Pool` | `int` | Unallocated stat points |

### Stat Point Allocation

```cpp
// Total available stat points at any level
int iTotalPoints = ((m_iLevel - 1) * 3) + 70;

// Points gained per level
int iPointsPerLevel = 3;  // DEF_TOTALLEVELUPPOINT

// Base starting points
int iBasePoints = 70;

// Maximum per stat
int iMaxPerStat = 1000;  // DEF_CHARPOINTLIMIT (note: docs say 200, code says 1000)
```

### Stat Restoration Algorithm

```cpp
void ___RestorePlayerCharacteristics(int iClientH) {
    // Validate each stat has minimum 10
    if (m_iStr < 10) m_iStr = 10;
    if (m_iDex < 10) m_iDex = 10;
    if (m_iInt < 10) m_iInt = 10;
    if (m_iVit < 10) m_iVit = 10;
    if (m_iMag < 10) m_iMag = 10;
    if (m_iCharisma < 10) m_iCharisma = 10;

    // Calculate total allocated
    int iTotal = m_iStr + m_iDex + m_iInt + m_iVit + m_iMag + m_iCharisma;
    int iExpected = ((m_iLevel - 1) * 3) + 70;

    // If excess points, restore to expected
    if (iTotal > iExpected) {
        // Proportionally reduce stats
        // ... complex redistribution logic
    }

    // Validate skill-stat requirements
    // Skill 5 (HandToHand): STR >= Skill_Max/2
    if (m_cSkillMastery[5] > 0) {
        int iReq = m_cSkillMastery[5] / 2;
        if (m_iStr < iReq) m_iStr = iReq;
    }

    // Skills 6-9 (Weapon): DEX >= max(Skill_Max)/2
    int iMaxWeaponSkill = 0;
    for (int i = 6; i <= 9; i++) {
        if (m_cSkillMastery[i] > iMaxWeaponSkill)
            iMaxWeaponSkill = m_cSkillMastery[i];
    }
    if (iMaxWeaponSkill > 0) {
        int iReq = iMaxWeaponSkill / 2;
        if (m_iDex < iReq) m_iDex = iReq;
    }

    // Skill 19: INT >= Skill_Max/2
    if (m_cSkillMastery[19] > 0) {
        int iReq = m_cSkillMastery[19] / 2;
        if (m_iInt < iReq) m_iInt = iReq;
    }

    // Skills 3-4 (Magic): MAG >= max(Skill_Max)/2
    int iMaxMagicSkill = max(m_cSkillMastery[3], m_cSkillMastery[4]);
    if (iMaxMagicSkill > 0) {
        int iReq = iMaxMagicSkill / 2;
        if (m_iMag < iReq) m_iMag = iReq;
    }
}
```

---

## 7. Experience and Leveling

### Experience Fields

| Field | Type | Description |
|-------|------|-------------|
| `m_iExp` | `int` | Current experience points |
| `m_iNextLevelExp` | `int` | Experience needed for next level |
| `m_iExpStock` | `int` | Pending experience to distribute |
| `m_iAutoExpAmount` | `int` | Auto-exp pending amount |
| `m_dwExpStockTime` | `DWORD` | Last exp stock distribution time |
| `m_dwAutoExpTime` | `DWORD` | Last auto-exp time |

### Global Level Table

```cpp
int m_iLevelExpTable[200];  // Experience thresholds per level
```

### Level-Up Algorithm

```cpp
BOOL bCheckLevelUp(int iClientH) {
    // Check max level cap
    if (m_iLevel >= 180) {  // DEF_PLAYERMAXLEVEL
        return FALSE;
    }

    if (m_iExp >= m_iNextLevelExp) {
        // Level up!
        m_iLevel++;

        // Award stat points
        m_iLU_Pool += 3;  // DEF_TOTALLEVELUPPOINT

        // Special reward for early levels
        if (m_iLevel <= 5) {
            // Create 100,000 gold item and give to player
            CreateGoldItem(iClientH, 100000);
        }

        // Update next level threshold
        m_iNextLevelExp = m_iLevelExpTable[m_iLevel + 1];

        // Notify client
        SendNotifyMsg(iClientH, DEF_NOTIFY_LEVELUP, m_iLevel, 0, 0, NULL);

        // Recursively check for multiple level-ups
        bCheckLevelUp(iClientH);

        return TRUE;
    }

    return FALSE;
}
```

### Experience Award

```cpp
void GetExp(int iClientH, int iExp, BOOL bIsAttackerOwn) {
    // Check for limited user restrictions
    if (bCheckLimitedUser(iClientH)) {
        // Apply restrictions
    }

    // Add experience
    m_iExp += iExp;

    // Apply bonus modifiers
    if (m_iAddExp > 0) {
        m_iExp += (iExp * m_iAddExp / 100);
    }

    // Notify client
    SendNotifyMsg(iClientH, DEF_NOTIFY_EXP, m_iExp, iExp, 0, NULL);

    // Check for level up
    bCheckLevelUp(iClientH);
}
```

### Experience Stock System

```cpp
void CalcExpStock(int iClientH) {
    // Distribute pending experience
    if (m_iExpStock > 0) {
        m_iExp += m_iExpStock;
        m_iAutoExpAmount += m_iExpStock;
        m_iExpStock = 0;

        SendNotifyMsg(iClientH, DEF_NOTIFY_EXP, m_iExp, 0, 0, NULL);
        bCheckLevelUp(iClientH);
    }
}
```

### Level Experience Lookup

```cpp
int iGetExpLevel(int iExp) {
    for (int i = 1; i < 200; i++) {
        if (iExp < m_iLevelExpTable[i]) {
            return i - 1;
        }
    }
    return 180;  // Max level
}

int iGetLevelExp(int iLevel) {
    if (iLevel < 1) return 0;
    if (iLevel > 180) return m_iLevelExpTable[180];
    return m_iLevelExpTable[iLevel];
}
```

---

## 8. Combat Statistics

### Offense

| Field | Type | Description |
|-------|------|-------------|
| `m_iHitRatio` | `int` | Base hit rate |
| `m_cAttackDiceThrow_SM` | `char` | Attack dice count (small/medium) |
| `m_cAttackDiceRange_SM` | `char` | Attack dice range (small/medium) |
| `m_cAttackDiceThrow_L` | `char` | Attack dice count (large) |
| `m_cAttackDiceRange_L` | `char` | Attack dice range (large) |
| `m_cAttackBonus_SM` | `char` | Attack bonus (small/medium) |
| `m_cAttackBonus_L` | `char` | Attack bonus (large) |
| `m_iComboAttackCount` | `int` | Current combo counter |
| `m_iSuperAttackLeft` | `int` | Super attacks remaining |
| `m_iSuperAttackCount` | `int` | Super attack counter |

### Defense

| Field | Type | Description |
|-------|------|-------------|
| `m_iDefenseRatio` | `int` | Base defense value |
| `m_iDamageAbsorption_Armor[15]` | `int[]` | Per-armor piece absorption |
| `m_iDamageAbsorption_Shield` | `int` | Shield/parry absorption |

### PvP Statistics

| Field | Type | Description |
|-------|------|-------------|
| `m_iEnemyKillCount` | `int` | NPC/monster kills |
| `m_iPKCount` | `int` | Player kills |
| `m_iRewardGold` | `int` | Gold earned from kills |
| `m_iRating` | `int` | Reputation (-10000 to +10000) |
| `m_iLastDamage` | `int` | Last damage taken |
| `m_dwRecentAttackTime` | `DWORD` | Last attack timestamp |

### Attack Power Calculation

```cpp
// Unarmed (wWeaponType == 0)
int iAP_SM = iDice(1, m_iStr / 12);
if (iAP_SM < 1) iAP_SM = 1;
int iAP_L = iAP_SM;
// Add HandToHand skill (skill 5) bonus to hit ratio

// Melee Weapons (wWeaponType 1-39)
int iAP_SM = iDice(m_cAttackDiceThrow_SM, m_cAttackDiceRange_SM) + m_cAttackBonus_SM;
int iAP_L = iDice(m_cAttackDiceThrow_L, m_cAttackDiceRange_L) + m_cAttackBonus_L;
// Apply STR bonus
iAP_SM += (int)(iAP_SM * (m_iStr / 5.0) / 100.0);
iAP_L += (int)(iAP_L * (m_iStr / 5.0) / 100.0);

// Ranged Weapons (wWeaponType 40+)
// Same as melee, plus:
iAP_SM += iDice(1, m_iStr / 20);
iAP_L += iDice(1, m_iStr / 20);
```

### Super Attack Bonuses

```cpp
if (m_iSuperAttackLeft > 0 && iAttackMode >= 20) {
    // Base super attack bonus
    iAP += (iAP * m_iLevel / 100);
    iHitRatio += 100;

    // Skill-specific bonuses
    switch (m_sUsingWeaponSkill) {
        case 6:  // Long sword
        case 8:  // Fencing
            iAP += iAP / 10;
            iHitRatio += 30;
            break;
        case 7:  // Short sword
            // No bonus
            break;
        case 10: // Axe
        case 14: // Two-handed sword
            iAP += iAP / 5;
            break;
        case 21: // Hammer
            iAP += iAP / 5;
            iHitRatio += 50;
            break;
    }

    // Custom item bonus
    iHitRatio += m_iCustomItemValue_Attack;

    m_iSuperAttackLeft--;
}
```

### Hit Resolution

```cpp
// Hit ratio bounds
#define DEF_MINIMUMHITRATIO 15
#define DEF_MAXIMUMHITRATIO 99

int iHitRate = iAttackerHitRatio - iTargetDefenseRatio;
if (iHitRate < DEF_MINIMUMHITRATIO) iHitRate = DEF_MINIMUMHITRATIO;
if (iHitRate > DEF_MAXIMUMHITRATIO) iHitRate = DEF_MAXIMUMHITRATIO;

// Roll for hit
if (iDice(1, 100) <= iHitRate) {
    // Hit successful
    // Apply damage...
} else {
    // Miss
}
```

---

## 9. Weapon and Equipment

### Equipment Tracking

| Field | Type | Description |
|-------|------|-------------|
| `m_sItemEquipmentStatus[15]` | `short[]` | Item index per equipment slot |
| `m_bIsItemEquipped[50]` | `BOOL[]` | Flag per inventory slot if equipped |
| `m_sUsingWeaponSkill` | `short` | Current weapon skill index |
| `m_cArrowIndex` | `char` | Arrow slot index (-1 if none) |

### Equipment Slot Indices

```cpp
#define DEF_EQUIPPOS_NONE       0
#define DEF_EQUIPPOS_RHAND      1   // Right hand (one-handed weapon)
#define DEF_EQUIPPOS_LHAND      2   // Left hand (shield, secondary)
#define DEF_EQUIPPOS_TWOHAND    3   // Two-handed weapon
#define DEF_EQUIPPOS_BODY       4   // Body armor
#define DEF_EQUIPPOS_ARMS       5   // Arm guards
#define DEF_EQUIPPOS_PANTS      6   // Leg armor
#define DEF_EQUIPPOS_LEGGINGS   7   // Boots/leggings
#define DEF_EQUIPPOS_NECK       8   // Necklace/amulet
#define DEF_EQUIPPOS_HEAD       9   // Helmet
#define DEF_EQUIPPOS_BACK       10  // Cape/cloak
#define DEF_EQUIPPOS_LFINGER    11  // Left ring
#define DEF_EQUIPPOS_RFINGER    12  // Right ring
#define DEF_EQUIPPOS_HANDS      13  // Gloves
#define DEF_EQUIPPOS_ANGELS     14  // Special slot

#define DEF_MAXITEMEQUIPPOS     15
```

### Weapon Types

```
Type 0: Unarmed
Types 1-39: Melee weapons
  - 1-5: Swords
  - 6-10: Axes
  - 11-15: Hammers
  - 16-20: Staffs
  - 21-25: Daggers
  - etc.
Types 40+: Ranged weapons
  - 40-45: Bows
  - 46-50: Crossbows
  - etc.
```

---

## 10. Inventory System

### Storage Arrays

| Field | Type | Size | Description |
|-------|------|------|-------------|
| `m_pItemList[50]` | `CItem*[]` | 50 | Main inventory slots |
| `m_ItemPosList[50]` | `POINT[]` | 50 | Grid positions (x,y) |
| `m_pItemInBankList[200]` | `CItem*[]` | 200 | Bank storage slots |
| `m_iCurWeightLoad` | `int` | - | Current carried weight |

### Inventory Slot Management

```cpp
BOOL bAddItem(int iClientH, CItem* pItem, char cMode) {
    // Find first empty slot
    int iSlot = -1;
    for (int i = 0; i < DEF_MAXITEMS; i++) {
        if (m_pItemList[i] == NULL) {
            iSlot = i;
            break;
        }
    }

    if (iSlot == -1) {
        // Inventory full
        return FALSE;
    }

    // Check weight limit
    int iMaxWeight = iCalcMaxLoad(iClientH);
    if (m_iCurWeightLoad + pItem->m_wWeight > iMaxWeight) {
        // Too heavy
        return FALSE;
    }

    // Check for stackable items
    if (pItem->m_bStackable) {
        for (int i = 0; i < DEF_MAXITEMS; i++) {
            if (m_pItemList[i] != NULL &&
                strcmp(m_pItemList[i]->m_cName, pItem->m_cName) == 0) {
                // Stack with existing
                m_pItemList[i]->m_dwCount += pItem->m_dwCount;
                delete pItem;
                return TRUE;
            }
        }
    }

    // Place in slot
    m_pItemList[iSlot] = pItem;
    m_ItemPosList[iSlot].x = 40;  // Default position
    m_ItemPosList[iSlot].y = 30;

    // Update weight
    m_iCurWeightLoad += pItem->m_wWeight * pItem->m_dwCount;

    return TRUE;
}
```

### Weight Calculation

```cpp
int iCalcMaxLoad(int iClientH) {
    // Base weight capacity from STR
    int iMaxWeight = m_iStr * 500 + 1000;

    // Apply item bonuses
    // ... equipment modifiers

    return iMaxWeight;
}
```

### Bank Operations

```cpp
BOOL bPlayerItemToBank(int iClientH, int iItemIndex) {
    // Validate item exists
    if (m_pItemList[iItemIndex] == NULL) return FALSE;

    // Find empty bank slot
    int iBankSlot = -1;
    for (int i = 0; i < DEF_MAXBANKITEMS; i++) {
        if (m_pItemInBankList[i] == NULL) {
            iBankSlot = i;
            break;
        }
    }

    if (iBankSlot == -1) {
        // Bank full
        return FALSE;
    }

    // Transfer item
    m_pItemInBankList[iBankSlot] = m_pItemList[iItemIndex];
    m_pItemList[iItemIndex] = NULL;

    // Update weight
    m_iCurWeightLoad -= m_pItemInBankList[iBankSlot]->m_wWeight;

    return TRUE;
}
```

---

## 11. Magic and Skills

### Skill Mastery

| Field | Type | Size | Description |
|-------|------|------|-------------|
| `m_cSkillMastery[60]` | `unsigned char[]` | 60 | Skill mastery levels (0-100) |
| `m_iSkillSSN[60]` | `int[]` | 60 | Skill special numbers |
| `m_bSkillUsingStatus[60]` | `BOOL[]` | 60 | Skill active flags |
| `m_iSkillUsingTimeID[60]` | `int[]` | 60 | Skill timer IDs |

### Magic Mastery

| Field | Type | Size | Description |
|-------|------|------|-------------|
| `m_cMagicMastery[100]` | `char[]` | 100 | Magic mastery flags |
| `m_cMagicEffectStatus[100]` | `char[]` | 100 | Active magic effects |

### Key Skill Indices

```cpp
// Combat Skills
#define SKILL_MINING            0
#define SKILL_FISHING           1
#define SKILL_MANUFACTURING     2
#define SKILL_MAGIC             3
#define SKILL_MAGIC_RESIST      4
#define SKILL_HAND_TO_HAND      5   // Unarmed combat
#define SKILL_LONG_SWORD        6
#define SKILL_SHORT_SWORD       7
#define SKILL_FENCING           8
#define SKILL_AXE               9
#define SKILL_HAMMER            10
#define SKILL_STAFF             11
#define SKILL_BOW               12
#define SKILL_SHIELD            13
#define SKILL_TWO_HANDED        14
// ... more skills up to 59
```

### Skill SSN Calculation

```cpp
int _iCalcSkillSSNpoint(int iClientH, int iSkillIndex) {
    // SSN Points = Level * 2 per skill
    return m_iLevel * 2;
}
```

### Magic Learning

```cpp
void RequestStudyMagicHandler(int iClientH, char* pName, BOOL bIsPurchase) {
    // Find magic by name
    CMagic* pMagic = FindMagicByName(pName);
    if (pMagic == NULL) return;

    // Check INT requirement
    if (m_iInt < pMagic->m_iReqInt) {
        SendNotifyMsg(iClientH, DEF_NOTIFY_STUDYMAGICFAIL, 0, 0, 0, NULL);
        return;
    }

    // Check if already learned
    if (m_cMagicMastery[pMagic->m_iIndex] != 0) {
        SendNotifyMsg(iClientH, DEF_NOTIFY_MAGICALREADYLEARNED, 0, 0, 0, NULL);
        return;
    }

    // Deduct cost if purchasing
    if (bIsPurchase) {
        if (!DeductGold(iClientH, pMagic->m_iCost)) {
            return;
        }
    }

    // Learn the magic
    m_cMagicMastery[pMagic->m_iIndex] = 1;

    // Notify client
    SendNotifyMsg(iClientH, DEF_NOTIFY_STUDYMAGICSUCCESS,
                  pMagic->m_iIndex, 0, 0, NULL);
}
```

---

## 12. Status Effects

### Boolean Status Flags

| Field | Type | Description |
|-------|------|-------------|
| `m_bIsKilled` | `BOOL` | Death status |
| `m_bIsPoisoned` | `BOOL` | Poison active |
| `m_bInhibition` | `BOOL` | Movement inhibited/stunned |
| `m_bMagicConfirm` | `BOOL` | Awaiting magic confirmation |
| `m_bIsSafeAttackMode` | `BOOL` | Safe attack mode (no friendly fire) |
| `m_bIsNeutral` | `BOOL` | Neutral faction flag |
| `m_bIsObserverMode` | `BOOL` | Observer/spectate mode |

### Timed Status Effects

| Field | Type | Description |
|-------|------|-------------|
| `m_iPoisonLevel` | `int` | Poison severity |
| `m_dwPoisonTime` | `DWORD` | Poison tick timer |
| `m_iTimeLeft_ShutUp` | `int` | Silence duration remaining |
| `m_iTimeLeft_Rating` | `int` | Rating change cooldown |
| `m_iTimeLeft_ForceRecall` | `int` | Force recall lockout |
| `m_iTimeLeft_FirmStaminar` | `int` | Stamina buff duration |

### Status Bit Flags

```cpp
// m_iStatus bit flags
#define STATUS_INVISIBLE    0x00000010  // Invisible
#define STATUS_INVULNERABLE 0x00400000  // God mode
// ... other status flags
```

### Hunger System

| Field | Type | Description |
|-------|------|-------------|
| `m_iHungerStatus` | `int` | Hunger level (0-100) |
| `m_dwHungerTime` | `DWORD` | Last hunger tick |

```cpp
// Hunger affects regen intervals
int iPlusTime = (30 - m_iHungerStatus) * 1000;
// At hunger 0: +30 seconds to regen interval
// At hunger 30+: no additional delay

// At hunger <= 0: No HP/MP/SP regeneration
```

### Poison System

```cpp
void SetPoisonFlag(int sOwnerH, char cOwnerType, BOOL bStatus) {
    if (bStatus == TRUE) {
        m_bIsPoisoned = TRUE;
        m_dwPoisonTime = dwCurrentTime;
    } else {
        m_bIsPoisoned = FALSE;
        m_iPoisonLevel = 0;
    }

    // Update status flags
    // Notify client
}

void PoisonEffect(int iClientH, int iV1) {
    if (!m_bIsPoisoned) return;

    // Calculate poison damage
    int iDamage = m_iPoisonLevel * 2;

    // Apply damage
    m_iHP -= iDamage;
    if (m_iHP <= 0) {
        // Player killed by poison
        ClientKilledHandler(iClientH, 0, 0, iDamage);
    }

    // Decrement poison level
    m_iPoisonLevel--;
    if (m_iPoisonLevel <= 0) {
        SetPoisonFlag(iClientH, DEF_OWNERTYPE_PLAYER, FALSE);
    }

    // Reset timer
    m_dwPoisonTime = dwCurrentTime;
}

BOOL bCheckResistingPoisonSuccess(int sOwnerH, char cOwnerType) {
    // Calculate resistance
    int iResist = m_iVit / 2;
    iResist += m_iAddResistMagic;
    // ... equipment bonuses

    // Roll for resistance
    if (iDice(1, 100) <= iResist) {
        return TRUE;  // Resisted
    }
    return FALSE;
}
```

---

## 13. Guild System

### Guild Fields

| Field | Type | Description |
|-------|------|-------------|
| `m_cGuildName[21]` | `char[]` | Guild name (20 chars + null) |
| `m_iGuildRank` | `int` | Rank within guild (-1 = no guild) |
| `m_iGuildGUID` | `int` | Guild unique ID |
| `m_iContribution` | `int` | Contribution points to guild |

### Rank Values

```cpp
#define DEF_GUILDSTARTRANK 12  // Initial rank on join

// Rank hierarchy (lower = higher rank)
// 0 = Guild Master
// 1-11 = Officer ranks
// 12 = New member
```

### Guild Validation

```cpp
// On player load, check if guild still exists
if (m_iGuildRank != -1) {
    CGuild* pGuild = FindGuildByName(m_cGuildName);
    if (pGuild == NULL) {
        // Guild was disbanded
        memset(m_cGuildName, 0, sizeof(m_cGuildName));
        m_iGuildRank = -1;
        m_iGuildGUID = -1;
        SendNotifyMsg(iClientH, DEF_NOTIFY_GUILDDISBANDED, 0, 0, 0, NULL);
    }
}
```

---

## 14. Party System

### Party Fields

| Field | Type | Description |
|-------|------|-------------|
| `m_iPartyID` | `int` | Party ID |
| `m_iPartyStatus` | `int` | Party status code |
| `m_iPartyRank` | `int` | Rank (-1=none, 1=leader, 2+=member) |
| `m_iPartyMemberCount` | `int` | Current member count |
| `m_iPartyGUID` | `int` | Party unique ID |

### Party Member List

```cpp
struct {
    int iIndex;         // Client index
    char cName[11];     // Character name
} m_stPartyMemberName[DEF_MAXPARTYMEMBERS];  // 9 max

#define DEF_MAXPARTYMEMBERS 9
```

### Party Request Fields

| Field | Type | Description |
|-------|------|-------------|
| `m_iReqJoinPartyClientH` | `int` | Pending join request handle |
| `m_cReqJoinPartyName[12]` | `char[]` | Pending requester name |

---

## 15. Quest System

### Quest Fields

| Field | Type | Description |
|-------|------|-------------|
| `m_iQuest` | `int` | Current quest number |
| `m_iQuestID` | `int` | Quest ID |
| `m_iAskedQuest` | `int` | Quest being offered |
| `m_iCurQuestCount` | `int` | Progress counter |
| `m_iQuestRewardType` | `int` | Reward type ID |
| `m_iQuestRewardAmount` | `int` | Reward amount |
| `m_iContribution` | `int` | Quest contribution points |
| `m_bQuestMatchFlag_Loc` | `BOOL` | Location match flag |
| `m_bIsQuestCompleted` | `BOOL` | Completion status |

---

## 16. War and Territory

### War Fields

| Field | Type | Description |
|-------|------|-------------|
| `m_dwWarBeginTime` | `DWORD` | War start timestamp |
| `m_bIsWarLocation` | `BOOL` | Currently in war zone |
| `m_iCrusadeDuty` | `int` | Crusade role (1=attack, 2=defend, 3=build) |
| `m_dwCrusadeGUID` | `DWORD` | Crusade unique ID |
| `m_dwHeldenianGUID` | `DWORD` | Heldenian war ID |
| `m_iWarContribution` | `int` | War contribution points |
| `m_iConstructionPoint` | `int` | Construction points |

### Faction

| Field | Type | Description |
|-------|------|-------------|
| `m_cSide` | `char` | Faction (0=Neutral, 1=Aresden, 2=Elvine) |

### Crusade Structure Visibility

```cpp
struct {
    char cType;     // Structure type
    char cSide;     // Owning faction
    short sX;       // X position
    short sY;       // Y position
} m_stCrusadeStructureInfo[DEF_MAXCRUSADESTRUCTURES];

int m_iCSIsendPoint;  // Send progress index
```

---

## 17. Special Abilities

### Special Ability Fields

| Field | Type | Description |
|-------|------|-------------|
| `m_bIsSpecialAbilityEnabled` | `BOOL` | Ability active |
| `m_dwSpecialAbilityStartTime` | `DWORD` | Activation time |
| `m_iSpecialAbilityLastSec` | `int` | Duration remaining |
| `m_iSpecialAbilityType` | `int` | Ability type (0-52+) |
| `m_iSpecialAbilityEquipPos` | `int` | Triggering equipment slot |
| `m_iSpecialAbilityTime` | `int` | Time counter |
| `m_iSpecialEventID` | `int` | Related event ID |
| `m_iSpecialWeaponEffectType` | `int` | Weapon effect type (0-9) |
| `m_iSpecialWeaponEffectValue` | `int` | Weapon effect value |

### Special Ability Types

```cpp
// Type 0: None
// Type 1: HP 50% reduction
// Type 2: Poison effect
// Type 3: Paralysis
// Type 4: Weapon skill boost
// Type 5: Berserk-like HP scaling
// ...
// Type 50: Spell immunity
// Type 51: Specific spell immunity (skill linked)
// Type 52: Level 5+ spell immunity
```

---

## 18. Item Modifiers

### Additive Bonuses

| Field | Type | Description |
|-------|------|-------------|
| `m_iAddHP` | `int` | Bonus HP from items |
| `m_iAddSP` | `int` | Bonus SP from items |
| `m_iAddMP` | `int` | Bonus MP from items |
| `m_iAddAR` | `int` | Attack ratio bonus |
| `m_iAddPR` | `int` | Physical resistance |
| `m_iAddDR` | `int` | Defense ratio bonus |
| `m_iAddMR` | `int` | Magic resistance |
| `m_iAddAbsPD` | `int` | Physical damage absorption |
| `m_iAddAbsMD` | `int` | Magic damage absorption |
| `m_iAddCD` | `int` | Critical damage bonus |
| `m_iAddExp` | `int` | Experience gain % modifier |
| `m_iAddGold` | `int` | Gold gain % modifier |
| `m_iAddResistMagic` | `int` | Magic resistance |
| `m_iAddPhysicalDamage` | `int` | Physical damage modifier |
| `m_iAddMagicalDamage` | `int` | Magic damage modifier |

### Elemental Absorption

| Field | Type | Description |
|-------|------|-------------|
| `m_iAddAbsAir` | `int` | Air element absorption |
| `m_iAddAbsEarth` | `int` | Earth element absorption |
| `m_iAddAbsFire` | `int` | Fire element absorption |
| `m_iAddAbsWater` | `int` | Water element absorption |

### Custom Item Values

| Field | Type | Description |
|-------|------|-------------|
| `m_iCustomItemValue_Attack` | `int` | Custom attack value |
| `m_iCustomItemValue_Defense` | `int` | Custom defense value |
| `m_iMinAP_SM` | `int` | Minimum attack power (S/M) |
| `m_iMinAP_L` | `int` | Minimum attack power (L) |
| `m_iMaxAP_SM` | `int` | Maximum attack power (S/M) |
| `m_iMaxAP_L` | `int` | Maximum attack power (L) |

### Recalculation Function

```cpp
void CalcTotalItemEffect(int iClientH, int iEquipItemID, BOOL bNotify) {
    // Reset all bonus fields to 0
    m_iAddHP = 0;
    m_iAddSP = 0;
    m_iAddMP = 0;
    // ... reset all others

    // Iterate equipped items
    for (int i = 0; i < DEF_MAXITEMEQUIPPOS; i++) {
        int iItemIndex = m_sItemEquipmentStatus[i];
        if (iItemIndex == -1) continue;

        CItem* pItem = m_pItemList[iItemIndex];
        if (pItem == NULL) continue;

        // Add item bonuses
        m_iAddHP += pItem->m_iAddHP;
        m_iAddSP += pItem->m_iAddSP;
        m_iAddMP += pItem->m_iAddMP;
        m_iAddAR += pItem->m_iAddAR;
        m_iAddDR += pItem->m_iAddDR;
        // ... sum all item effects

        // Calculate damage absorption per armor slot
        m_iDamageAbsorption_Armor[i] = pItem->m_iAbsorption;
    }

    // Recalculate effective stats
    // ...

    if (bNotify) {
        SendNotifyMsg(iClientH, DEF_NOTIFY_ITEMEFFECT, 0, 0, 0, NULL);
    }
}
```

---

## 19. Admin and Moderation

### Admin Fields

| Field | Type | Description |
|-------|------|-------------|
| `m_iAdminUserLevel` | `int` | Admin level (0 = normal player) |
| `m_bIsAdminCommandEnabled` | `BOOL` | Admin commands active |
| `m_bIsAdminOrderGoto` | `BOOL` | Admin goto in progress |
| `m_iAlterItemDropIndex` | `int` | Altered drop item index |

### Admin Level Permissions

```cpp
// Level 0: Normal player
// Level 1: Basic GM commands
// Level 2: Advanced GM commands
// Level 3: Server admin commands
// Higher levels have more privileges
```

---

## 20. Anti-Cheat System

### Frequency Tracking

| Field | Type | Description |
|-------|------|-------------|
| `m_dwMagicFreqTime` | `DWORD` | Last magic cast time |
| `m_dwMoveFreqTime` | `DWORD` | Last movement time |
| `m_dwAttackFreqTime` | `DWORD` | Last attack time |
| `m_bIsMoveBlocked` | `BOOL` | Movement blocked |
| `m_iSpellCount` | `int` | Spell cast counter |
| `m_bMagicPauseTime` | `BOOL` | Magic paused |

### Speed Hack Detection

| Field | Type | Description |
|-------|------|-------------|
| `m_dwSpeedHackCheckTime` | `DWORD` | Speed check time |
| `m_iSpeedHackCheckExp` | `int` | Expected position |
| `m_dwLogoutHackCheck` | `DWORD` | Logout check time |

### Connection Validation

| Field | Type | Description |
|-------|------|-------------|
| `m_dwInitCCTimeRcv` | `DWORD` | Initial connection time received |
| `m_dwInitCCTime` | `DWORD` | Initial connection time |
| `dwClientTime` | `DWORD` | Client timestamp |

### Validation Functions

```cpp
BOOL bCheckClientMoveFrequency(int iClientH) {
    DWORD dwTimeDiff = dwCurrentTime - m_dwMoveFreqTime;
    if (dwTimeDiff < DEF_MOVEMSGRECVTIME) {  // Too fast
        m_bIsMoveBlocked = TRUE;
        return FALSE;
    }
    m_dwMoveFreqTime = dwCurrentTime;
    return TRUE;
}

BOOL bCheckClientMagicFrequency(int iClientH) {
    DWORD dwTimeDiff = dwCurrentTime - m_dwMagicFreqTime;
    if (dwTimeDiff < DEF_MAGICMSGRECVTIME) {  // Too fast
        return FALSE;
    }
    m_dwMagicFreqTime = dwCurrentTime;
    return TRUE;
}

BOOL bCheckClientAttackFrequency(int iClientH) {
    DWORD dwTimeDiff = dwCurrentTime - m_dwAttackFreqTime;
    if (dwTimeDiff < DEF_ATTACKMSGRECVTIME) {  // Too fast
        return FALSE;
    }
    m_dwAttackFreqTime = dwCurrentTime;
    return TRUE;
}
```

---

## 21. Session Management

### Connection State

| Field | Type | Description |
|-------|------|-------------|
| `m_bIsInitComplete` | `BOOL` | Character fully loaded |
| `m_bIsMsgSendAvailable` | `BOOL` | Can send messages |
| `m_pXSock` | `XSocket*` | Network socket |
| `m_cIPaddress[21]` | `char[]` | Client IP address |
| `m_bIsOnServerChange` | `BOOL` | Changing servers |
| `m_bIsOnWaitingProcess` | `BOOL` | Awaiting response |

### Timers

| Field | Type | Interval | Description |
|-------|------|----------|-------------|
| `m_dwTime` | `DWORD` | - | General timer |
| `m_dwAutoSaveTime` | `DWORD` | 600000ms | Last auto-save |
| `m_dwHungerTime` | `DWORD` | 60000ms | Last hunger tick |
| `m_dwWarmEffectTime` | `DWORD` | - | Warm effect timer |
| `m_dwLastActionTime` | `DWORD` | - | Last player action |

### Exchange System

| Field | Type | Description |
|-------|------|-------------|
| `m_bIsExchangeMode` | `BOOL` | In trade mode |
| `m_iExchangeH` | `int` | Trade partner handle |
| `m_cExchangeName[11]` | `char[]` | Trade partner name |
| `m_cExchangeItemName[4][21]` | `char[][]` | Trade item names |
| `m_cExchangeItemIndex[4]` | `char[]` | Trade item indices |
| `m_iExchangeItemAmount[4]` | `int[]` | Trade amounts |
| `m_bIsExchangeConfirm` | `BOOL` | Trade confirmed |

---

## 22. Persistence Format

### File Sections

The player data file uses an INI-like format with the following sections:

```ini
[FILE-DATE]
file-saved-date = <year> <month> <day> <hour> <minute>

[NAME-ACCOUNT]
character-name = <name>
account-name = <account>

[STATUS]
character-profile = <profile text>
character-location = <location name>
character-guild-name = <guild name or NONE>
character-guild-GUID = <guild id or -1>
character-guild-rank = <rank>
character-loc-map = <map name>
character-loc-x = <x>
character-loc-y = <y>
character-HP = <hp>
character-MP = <mp>
character-SP = <sp>
character-LEVEL = <level>
character-RATING = <rating>
character-STR = <str>
character-INT = <int>
character-VIT = <vit>
character-DEX = <dex>
character-MAG = <mag>
character-CHARISMA = <cha>
character-LUCK = <luck>
character-EXP = <exp>
character-LU_Pool = <unallocated points>
character-EK-Count = <enemy kills>
character-PK-Count = <player kills>
character-reward-gold = <gold>
character-downskillindex = <skill>
character-IDnum1 = <id1>
character-IDnum2 = <id2>
character-IDnum3 = <id3>
sex-status = <sex>
skin-status = <skin>
hairstyle-status = <hairstyle>
haircolor-status = <haircolor>
underwear-status = <underwear>
hunger-status = <hunger>
timeleft-shutup = <time>
timeleft-rating = <time>
timeleft-force-recall = <time>
timeleft-firm-staminar = <time>
admin-user-level = <level>
penalty-block-date = <year> <month> <day>
character-quest-number = <quest>
character-quest-ID = <id>
current-quest-count = <count>
quest-reward-type = <type>
quest-reward-amount = <amount>
character-contribution = <contribution>
character-war-contribution = <war contribution>
character-quest-completed = <0 or 1>
special-event-id = <event>
super-attack-left = <count>
reserved-fightzone-id = <zone> <time> <ticket>
special-ability-time = <time>
locked-map-name = <map>
locked-map-time = <time>
crusade-job = <duty>
crusade-GUID = <guid>
construct-point = <points>
dead-penalty-time = <time>
party-id = <id>
gizon-item-upgade-left = <count>

[Appearance]
appr1 = <value>
appr2 = <value>
appr3 = <value>
appr4 = <value>
appr-color = <color>

[ITEMLIST]
character-item = <name> <count> <effect_type> <v1> <v2> <v3> <color> <spec1> <spec2> <spec3> <durability> <attribute>
character-item = ...
character-bank-item = <name> <count> <effect_type> <v1> <v2> <v3> <color> <spec1> <spec2> <spec3> <durability> <attribute>
character-bank-item = ...

[ITEM-EQUIP-STATUS]
item-equip-status = <50 characters of 0 or 1>

[ITEM-POSITION]
item-position-x = <50 space-separated x values>
item-position-y = <50 space-separated y values>

[MAGIC-SKILL-MASTERY]
magic-mastery = <100 single-digit characters>
skill-mastery = <60 space-separated values>
skill-SSN = <60 space-separated values>

[EOF]
```

### Item Field Format

```
<name>          - Item name (21 chars max)
<count>         - Quantity (DWORD)
<effect_type>   - Touch effect type (short)
<v1>            - Touch effect value 1 (short)
<v2>            - Touch effect value 2 (short)
<v3>            - Touch effect value 3 (short)
<color>         - Item color (char)
<spec1>         - Special effect value 1 (short)
<spec2>         - Special effect value 2 (short)
<spec3>         - Special effect value 3 (short)
<durability>    - Current lifespan (WORD)
<attribute>     - Item flags (DWORD)
```

---

## 23. Core Functions Reference

### Data Management

| Function | Purpose |
|----------|---------|
| `InitPlayerData()` | Initialize player after loading from log server |
| `ResponsePlayerDataHandler()` | Handle save/load response |
| `_bDecodePlayerDatafileContents()` | Parse player data format |
| `_iComposePlayerDataFileContents()` | Create player data format |
| `DeleteClient()` | Cleanup on disconnect |

### Experience & Leveling

| Function | Purpose |
|----------|---------|
| `bCheckLevelUp()` | Check and process level-up |
| `GetExp()` | Award experience points |
| `CalcExpStock()` | Distribute pending experience |
| `iGetExpLevel()` | Get level from experience value |
| `iGetLevelExp()` | Get experience threshold for level |

### Vital Statistics

| Function | Purpose |
|----------|---------|
| `iGetMaxHP()` | Calculate maximum HP |
| `iGetMaxMP()` | Calculate maximum MP |
| `iGetMaxSP()` | Calculate maximum SP |
| `TimeHitPointsUp()` | HP regeneration tick |
| `TimeManaPointsUp()` | MP regeneration tick |
| `TimeStaminarPointsUp()` | SP regeneration tick |

### Character Stats

| Function | Purpose |
|----------|---------|
| `___RestorePlayerCharacteristics()` | Validate and restore stats |
| `___RestorePlayerRating()` | Validate and restore reputation |
| `StateChangeHandler()` | Process stat point allocation |
| `bChangeState()` | Validate stat change request |

### Combat & Damage

| Function | Purpose |
|----------|---------|
| `iCalculateAttackEffect()` | Execute attack and calculate damage |
| `Effect_Damage_Spot()` | Apply area damage effect |
| `Effect_Damage_Spot_DamageMove()` | Apply knockback damage |
| `ClientKilledHandler()` | Process player death |
| `EnemyKillRewardHandler()` | Reward for NPC kill |
| `PK_KillRewardHandler()` | Reward/penalty for PvP kill |
| `ApplyPKpenalty()` | Apply PK death penalties |
| `ApplyCombatKilledPenalty()` | Apply death penalties |

### Inventory

| Function | Purpose |
|----------|---------|
| `bAddItem()` | Add item to inventory |
| `DropItemHandler()` | Drop item from inventory |
| `ReleaseItemHandler()` | Delete/destroy item |
| `GiveItemHandler()` | Give item to player |
| `UseItemHandler()` | Use consumable item |
| `CalcTotalItemEffect()` | Recalculate all item bonuses |
| `iCalcMaxLoad()` | Calculate max carry weight |
| `bPlayerItemToBank()` | Deposit to bank |
| `bBankItemToPlayer()` | Withdraw from bank |

### Skills & Magic

| Function | Purpose |
|----------|---------|
| `RequestStudyMagicHandler()` | Learn magic spell |
| `UseSkillHandler()` | Execute skill |
| `ClearSkillUsingStatus()` | End skill duration |
| `CalculateSSN_SkillIndex()` | Update skill SSN |

### Status Effects

| Function | Purpose |
|----------|---------|
| `SetPoisonFlag()` | Apply/remove poison |
| `PoisonEffect()` | Apply poison damage |
| `bCheckResistingPoisonSuccess()` | Check poison resistance |
| `SetBerserkFlag()` | Apply berserk |
| `SetInvisibilityFlag()` | Apply invisibility |
| `SetDefenseShieldFlag()` | Apply shield buff |
| `SetMagicProtectionFlag()` | Apply magic protection |
| `SetIceFlag()` | Apply ice effect |
| `SetStatusFlag()` | Generic status flag |

### Guild & Party

| Function | Purpose |
|----------|---------|
| `RequestCreateNewGuildHandler()` | Create guild |
| `RequestDisbandGuildHandler()` | Disband guild |
| `RequestCreatePartyHandler()` | Create party |
| `RequestJoinPartyHandler()` | Join party |
| `RequestDismissPartyHandler()` | Leave party |

### Admin

| Function | Purpose |
|----------|---------|
| `AdminOrder_Kill()` | Admin kill player |
| `AdminOrder_Revive()` | Admin revive player |
| `AdminOrder_GoTo()` | Admin teleport |
| `AdminOrder_SetStatus()` | Admin set status |
| `AdminOrder_CreateItem()` | Admin create items |
| `RequestAdminUserMode()` | Request admin access |

---

## 24. Formulas and Calculations

### Combat Formulas

```cpp
// Maximum HP
MaxHP = (VIT * 3) + (Level * 2) + (STR / 2) - SideEffect

// Maximum MP
MaxMP = (MAG * 2) + (Level * 2) + (INT / 2)

// Maximum SP
MaxSP = (STR * 2) + (Level * 2)

// Unarmed Attack Power
AP = iDice(1, STR / 12)  // Minimum 1

// Melee Attack Power
AP = iDice(DiceThrow, DiceRange) + Bonus
AP += (AP * STR / 5) / 100

// Ranged Attack Power
AP = MeleeAP + iDice(1, STR / 20)

// Hit Rate Calculation
HitRate = AttackerHitRatio - TargetDefenseRatio
HitRate = clamp(HitRate, 15, 99)  // 15-99%

// Damage with Absorption
FinalDamage = RawDamage - ArmorAbsorption - ShieldAbsorption
```

### Experience Formulas

```cpp
// Total stat points available
TotalPoints = ((Level - 1) * 3) + 70

// Points per level
PointsPerLevel = 3

// SSN Points per skill
SSN = Level * 2
```

### Regeneration Intervals

```cpp
// Base intervals (milliseconds)
HP_INTERVAL = 15000 + ((30 - Hunger) * 1000)
MP_INTERVAL = 20000 + ((30 - Hunger) * 1000)
SP_INTERVAL = 10000 + ((30 - Hunger) * 1000)

// At Hunger = 30: No delay
// At Hunger = 0: +30 second delay
```

---

## 25. Constants Reference

### Player Limits

| Constant | Value | Description |
|----------|-------|-------------|
| `DEF_MAXCLIENTS` | 2000 | Maximum connected players |
| `DEF_MAXITEMS` | 50 | Inventory slots |
| `DEF_MAXBANKITEMS` | 200 | Bank slots |
| `DEF_MAXITEMEQUIPPOS` | 15 | Equipment slots |
| `DEF_MAXMAGICTYPE` | 100 | Magic types |
| `DEF_MAXSKILLTYPE` | 60 | Skill types |
| `DEF_MAXMAGICEFFECTS` | 100 | Concurrent magic effects |
| `DEF_MAXPARTYMEMBERS` | 9 | Party size |
| `DEF_PLAYERMAXLEVEL` | 180 | Maximum level |
| `DEF_CHARPOINTLIMIT` | 1000 | Max per stat |
| `DEF_MAXSKILLPOINTS` | 700 | Total skill points |
| `DEF_GUILDSTARTRANK` | 12 | Starting guild rank |
| `DEF_TOTALLEVELUPPOINT` | 3 | Points per level |

### Timing Constants

| Constant | Value (ms) | Description |
|----------|------------|-------------|
| `DEF_HPUPTIME` | 15000 | HP regen interval |
| `DEF_MPUPTIME` | 20000 | MP regen interval |
| `DEF_SPUPTIME` | 10000 | SP regen interval |
| `DEF_HUNGERTIME` | 60000 | Hunger tick |
| `DEF_POISONTIME` | 12000 | Poison tick |
| `DEF_EXPSTOCKTIME` | 10000 | Exp stock distribution |
| `DEF_AUTOEXPTIME` | 360000 | Auto-exp (6 min) |
| `DEF_AUTOSAVETIME` | 600000 | Auto-save (10 min) |
| `DEF_CLIENTTIMEOUT` | 10000 | Client timeout |

### Combat Constants

| Constant | Value | Description |
|----------|-------|-------------|
| `DEF_MINIMUMHITRATIO` | 15 | Minimum hit chance (%) |
| `DEF_MAXIMUMHITRATIO` | 99 | Maximum hit chance (%) |

---

## Appendix: Field Inventory

Total CClient member variables: **~380+**

Categorized breakdown:
- Identity fields: ~15
- Appearance fields: ~12
- Vital statistics: ~10
- Core attributes: ~12
- Experience/Level: ~8
- Combat offense: ~15
- Combat defense: ~10
- PvP statistics: ~8
- Equipment: ~10
- Inventory arrays: ~6
- Magic/Skills: ~12
- Status effects: ~20
- Hunger/Environmental: ~4
- Quest: ~10
- Guild: ~5
- Party: ~10
- War/Territory: ~15
- Special abilities: ~10
- Item modifiers: ~25
- Admin: ~5
- Anti-cheat: ~15
- Session management: ~20
- Exchange system: ~10
- Miscellaneous: ~40

---

*Document generated from legacy Helbreath server source code v2.03*
