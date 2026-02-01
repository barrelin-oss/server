# Legacy Combat System Documentation

**System:** Combat System
**Complexity:** High (~2,500+ lines across Game.cpp)
**Primary File:** `Game.cpp` (functions scattered throughout 42,704 lines)
**Supporting Files:** `Client.h`, `Npc.h`, `Item.h`, `ActionID.h`

---

## Table of Contents

1. [Overview](#1-overview)
2. [Core Combat Functions](#2-core-combat-functions)
3. [Combat Constants](#3-combat-constants)
4. [Attack Power Calculation](#4-attack-power-calculation)
5. [Hit/Miss Resolution](#5-hitmiss-resolution)
6. [Armor & Damage Absorption](#6-armor--damage-absorption)
7. [Critical Hits & Combo System](#7-critical-hits--combo-system)
8. [Magic Damage System](#8-magic-damage-system)
9. [Status Effects in Combat](#9-status-effects-in-combat)
10. [PK System & Penalties](#10-pk-system--penalties)
11. [Combat Modes & Restrictions](#11-combat-modes--restrictions)
12. [Combat Data Structures](#12-combat-data-structures)
13. [Combat Message Protocol](#13-combat-message-protocol)
14. [Anti-Cheat & Rate Limiting](#14-anti-cheat--rate-limiting)
15. [Special Weapons & Abilities](#15-special-weapons--abilities)
16. [Combat Examples](#16-combat-examples)
17. [Modernization Notes](#17-modernization-notes)

---

## 1. Overview

The Helbreath combat system is a dice-based damage calculation engine that handles:

- **Physical attacks** (melee and ranged weapons)
- **Magic attacks** (elemental damage spells)
- **Hit/miss resolution** with bounded probability (15%-99%)
- **Armor-based damage absorption** per equipment piece
- **PvP combat** with crime/penalty systems
- **Combo attacks** with escalating bonuses
- **Status effects** affecting combat (berserk, protection, etc.)

### Combat Flow

```
┌─────────────────────────────────────────────────────────────────┐
│                     ATTACK INITIATED                             │
└─────────────────────────────┬───────────────────────────────────┘
                              │
                              ▼
┌─────────────────────────────────────────────────────────────────┐
│  1. Validate Attack (range, frequency, safe mode)               │
│     - iClientMotion_Attack_Handler()                            │
└─────────────────────────────┬───────────────────────────────────┘
                              │
                              ▼
┌─────────────────────────────────────────────────────────────────┐
│  2. Calculate Attack Power (AP)                                  │
│     - Weapon dice + STR bonus + skill modifiers                 │
│     - iCalculateAttackEffect() lines 51721-51918                │
└─────────────────────────────┬───────────────────────────────────┘
                              │
                              ▼
┌─────────────────────────────────────────────────────────────────┐
│  3. Resolve Hit/Miss                                             │
│     - HitRatio = (AttackerHit / DefenderDef) * 50               │
│     - Bounded: 15% minimum, 99% maximum                         │
│     - iCalculateAttackEffect() lines 52221-52260                │
└─────────────────────────────┬───────────────────────────────────┘
                              │
                    ┌─────────┴─────────┐
                    │                   │
                    ▼                   ▼
              ┌─────────┐         ┌─────────┐
              │  MISS   │         │   HIT   │
              │ Return 0│         │         │
              └─────────┘         └────┬────┘
                                       │
                                       ▼
┌─────────────────────────────────────────────────────────────────┐
│  4. Apply Armor Absorption                                       │
│     - Random body part hit (50% body, 25% legs, etc.)           │
│     - Armor piece absorbs up to 80% of damage                   │
│     - Shield blocking with skill check                          │
│     - lines 52339-52410                                         │
└─────────────────────────────┬───────────────────────────────────┘
                              │
                              ▼
┌─────────────────────────────────────────────────────────────────┐
│  5. Apply Final Damage                                           │
│     - Deduct HP from target                                      │
│     - Check for death                                            │
│     - Update combo counter                                       │
│     - Handle kill rewards/penalties                             │
└─────────────────────────────────────────────────────────────────┘
```

---

## 2. Core Combat Functions

### Primary Attack Handler

**Function:** `iClientMotion_Attack_Handler()`
**Location:** Game.cpp:9728
**Purpose:** Entry point for all player-initiated attacks

```cpp
// Pseudocode reconstruction
int CGame::iClientMotion_Attack_Handler(int iClientH, short sX, short sY,
                                         short dX, short dY, short wType,
                                         char cDir, DWORD dwClientTime) {
    // 1. Validate client exists and is alive (lines 9740-9746)
    if (m_pClientList[iClientH] == NULL) return 0;
    if (m_pClientList[iClientH]->m_bIsKilled == TRUE) return 0;

    // 2. Attack frequency check - anti-speedhack (lines 9748-9757)
    m_pClientList[iClientH]->m_iAttackMsgRecvCount++;
    if (m_pClientList[iClientH]->m_iAttackMsgRecvCount >= 7) {
        if ((dwTime - m_pClientList[iClientH]->m_dwAttackLAT) < 3500) {
            DeleteClient(iClientH, TRUE, TRUE);  // Speed hack detected
            return 0;
        }
        m_pClientList[iClientH]->m_iAttackMsgRecvCount = 0;
    }
    m_pClientList[iClientH]->m_dwAttackLAT = dwTime;

    // 3. Validate attack direction and target position (lines 9759-9841)
    // Complex direction validation with 8-direction system

    // 4. Check for recent attack throttle (line 9846)
    if ((dwTime - m_pClientList[iClientH]->m_dwRecentAttackTime) <= 100)
        return 0;  // 100ms minimum between attacks

    // 5. Process attack based on target type (lines 9872-9880)
    // Target types: Player, NPC, Dynamic Object
    iCalculateAttackEffect(...);  // Main damage calculation

    // 6. Update timestamps and counters
    m_pClientList[iClientH]->m_dwRecentAttackTime = dwTime;
}
```

### Main Damage Calculator

**Function:** `iCalculateAttackEffect()`
**Location:** Game.cpp:51652
**Purpose:** THE critical damage calculation engine

This function is approximately 800 lines and handles:
- Attack power calculation for all weapon types
- Strength bonus application
- Hit/miss resolution
- Armor absorption
- Combo bonuses
- Special weapon effects
- Status effect modifiers

```cpp
// Function signature
int CGame::iCalculateAttackEffect(short sTargetH, char cTargetType,
                                   short sAttackerH, char cAttackerType,
                                   int tdX, int tdY, int iAttackMode,
                                   BOOL bNearAttack, BOOL bIsDash) {
    // Returns: Final damage dealt (0 = miss)
}
```

### Death Handlers

**Player Death:** `ClientKilledHandler()` (Game.cpp:15415)
```cpp
void CGame::ClientKilledHandler(int iClientH, int iAttackerH,
                                 char cAttackerType, short sDamage) {
    // 1. Mark player as killed
    m_pClientList[iClientH]->m_bIsKilled = TRUE;

    // 2. Calculate experience loss
    // 3. Drop items based on conditions
    // 4. Handle PK penalties if applicable
    // 5. Teleport to respawn point
    // 6. Send death notification
}
```

**NPC Death:** `NpcKilledHandler()` (Game.cpp:10717)
```cpp
void CGame::NpcKilledHandler(int iNpcH, int iAttackerH, char cAttackerType,
                              short sDamage) {
    // 1. Award experience to killer
    // 2. Generate loot drops
    // 3. Update quest progress
    // 4. Handle war contribution if applicable
    // 5. Remove NPC from world
}
```

### Magic Damage Functions

**Single Target:** `Effect_Damage_Spot()` (Game.cpp:27569)
```cpp
void CGame::Effect_Damage_Spot(short sAttackerH, char cAttackerType,
                                short sTargetH, char cTargetType,
                                short sV1, short sV2, short sV3,
                                BOOL bExp, int iAttr) {
    // sV1 = dice count
    // sV2 = dice sides
    // sV3 = flat bonus
    // iAttr = elemental type (1=Earth, 2=Air, 3=Fire, 4=Water)

    // Base damage: iDice(sV1, sV2) + sV3
    // Apply magic stat bonus
    // Apply elemental absorption
    // Apply damage to target
}
```

**Area Damage:** `Effect_Damage_Area()` (Game.cpp:28900+)
**Linear Damage:** `Effect_Damage_Linear()` (Game.cpp:29200+)
**Knockback Damage:** `Effect_Damage_Spot_DamageMove()` (Game.cpp:28505)

---

## 3. Combat Constants

### Hit Ratio Limits

```cpp
// Game.h:150-155
#define DEF_MINIMUMHITRATIO    15    // Minimum 15% hit chance (always possible to hit)
#define DEF_MAXIMUMHITRATIO    99    // Maximum 99% hit chance (never guaranteed)
```

**Design Rationale:** These bounds ensure combat always has uncertainty. A level 180 player can still miss a level 1 mob (1% chance), and a level 1 player can still hit a level 180 boss (15% chance).

### Combat AI Behavior Types

```cpp
// Game.h:93-95
#define DEF_ATTACKAI_NORMAL          1   // Standard single-target attack
#define DEF_ATTACKAI_EXCHANGEATTACK  2   // Multi-target switching pattern
#define DEF_ATTACKAI_TWOBYONEATTACK  3   // 2-vs-1 coordinated attack
```

### NPC Behavior States

```cpp
// Npc.h:26-30
#define DEF_BEHAVIOR_STOP    0   // Stationary
#define DEF_BEHAVIOR_MOVE    1   // Moving/patrolling
#define DEF_BEHAVIOR_ATTACK  2   // Engaging in combat
#define DEF_BEHAVIOR_FLEE    3   // Running away
#define DEF_BEHAVIOR_DEAD    4   // Dead/dying
```

### Capacity Limits

```cpp
// Game.h
#define DEF_MAXCLIENTS       2000   // Maximum simultaneous players
#define DEF_MAXNPCS          5000   // Maximum simultaneous NPCs
#define DEF_MAXFIGHTZONE     10     // Maximum battle zones per map
```

### Timing Constants

```cpp
#define DEF_RAGPROTECTIONTIME  7000    // 7 seconds invulnerability after resurrection
#define DEF_SPUPTIME           10000   // 10 seconds between SP regeneration ticks
#define DEF_HPUPTIME           15000   // 15 seconds between HP regeneration ticks
#define DEF_MPUPTIME           20000   // 20 seconds between MP regeneration ticks
#define DEF_POISONTIME         12000   // 12 seconds poison duration
```

### Level & Progression

```cpp
#define DEF_LEVELLIMIT        20     // Level difference cap for some calculations
#define DEF_PLAYERMAXLEVEL    180    // Maximum player level
#define DEF_TOTALLEVELUPPOINT 3      // Stat points gained per level
#define DEF_MAXSKILLPOINTS    700    // Maximum total skill points
```

---

## 4. Attack Power Calculation

Attack Power (AP) is calculated differently based on weapon type. The system uses "attack dice" - a D&D-style notation where damage = roll(count, sides) + bonus.

### Weapon Type Categories

| Type | Description | Damage Formula |
|------|-------------|----------------|
| 0 | Barehanded | `iDice(1, STR/12)` |
| 1-39 | Standard Weapons | `iDice(throw, range) + bonus + STR_modifier` |
| 40+ | Ranged Weapons | `iDice(throw, range) + bonus + iDice(1, STR/20)` |

### Type 0: Barehanded Combat

**Location:** Game.cpp:51721-51727

```cpp
// Unarmed attack damage
if (cAttackerType == DEF_OWNERTYPE_PLAYER) {
    iAP_SM = iDice(1, (m_pClientList[sAttackerH]->m_iStr / 12));
    iAP_L  = iDice(1, (m_pClientList[sAttackerH]->m_iStr / 12));

    if (iAP_SM < 1) iAP_SM = 1;  // Minimum 1 damage
    if (iAP_L < 1)  iAP_L = 1;

    // Hit ratio uses unarmed skill (skill index 5)
    iAttackerHitRatio = m_pClientList[sAttackerH]->m_cSkillMastery[5];
}
```

**Example:** Player with 120 STR fighting barehanded:
- Damage dice: `iDice(1, 10)` = 1-10 damage per hit
- Very weak compared to weapons

### Type 1-39: Standard Weapons

**Location:** Game.cpp:51729-51754

```cpp
// Standard weapon damage calculation
iAP_SM = iDice(cAttackDiceThrow_SM, cAttackDiceRange_SM) + cAttackBonus_SM;
iAP_L  = iDice(cAttackDiceThrow_L, cAttackDiceRange_L) + cAttackBonus_L;

// Apply STR bonus multiplier
if (m_pClientList[sAttackerH]->m_iStr <= 0) {
    dTmp1 = 1.0;  // No bonus if STR is 0 or negative
} else {
    dTmp1 = (double)m_pClientList[sAttackerH]->m_iStr;
    dTmp1 = dTmp1 / 5.0;  // STR bonus = STR / 5
}

// Apply multiplier to base damage
dTmp2 = (double)iAP_SM;
dTmp3 = (dTmp1 / 100.0) * dTmp2;
iAP_SM = iAP_SM + (int)dTmp3;

dTmp2 = (double)iAP_L;
dTmp3 = (dTmp1 / 100.0) * dTmp2;
iAP_L = iAP_L + (int)dTmp3;
```

**STR Bonus Formula:**
```
Bonus Multiplier = STR / 5
Final Damage = Base Damage + (Base Damage * Bonus Multiplier / 100)
             = Base Damage * (1 + STR / 500)
```

**Example:** Sword with 2d12+5, player has 100 STR:
- Base roll: `iDice(2, 12) + 5` = 7-29 damage
- STR multiplier: 100 / 5 = 20
- Final: 7-29 * 1.20 = 8-35 damage

### Type 40+: Ranged Weapons

**Location:** Game.cpp:51756-51768

```cpp
// Ranged weapon damage (bows, crossbows)
iAP_SM = iDice(cAttackDiceThrow_SM, cAttackDiceRange_SM) + cAttackBonus_SM;
iAP_L  = iDice(cAttackDiceThrow_L, cAttackDiceRange_L) + cAttackBonus_L;

// Ranged weapons get smaller STR bonus
iAP_SM += iDice(1, (m_pClientList[sAttackerH]->m_iStr / 20));
iAP_L  += iDice(1, (m_pClientList[sAttackerH]->m_iStr / 20));

bNormalMissileAttack = TRUE;  // Flag for ranged attack processing
```

**Ranged STR Bonus:** Much weaker than melee (STR/20 vs STR/5)

### Super Attack Bonus

**Location:** Game.cpp:51877-51906

Super attacks consume "super attack charges" and provide significant bonuses:

```cpp
if (m_pClientList[sAttackerH]->m_iSuperAttackLeft > 0) {
    if (iAttackMode >= 20) {  // Attack mode 20+ = super attack
        // Level-based AP bonus
        dTmp1 = (double)m_pClientList[sAttackerH]->m_iLevel;
        dTmp1 = dTmp1 / 100.0;
        iAP_SM = iAP_SM + (int)(dTmp1 * (double)iAP_SM);
        iAP_L  = iAP_L + (int)(dTmp1 * (double)iAP_L);

        // Weapon skill-specific bonuses
        switch (m_pClientList[sAttackerH]->m_sUsingWeaponSkill) {
            case 6:  // Skill 6: +10% AP, +30 hit
                iAP_SM += iAP_SM / 10;
                iAP_L  += iAP_L / 10;
                iAttackerHitRatio += 30;
                break;
            case 8:  // Skill 8: +10% AP, +30 hit
                iAP_SM += iAP_SM / 10;
                iAP_L  += iAP_L / 10;
                iAttackerHitRatio += 30;
                break;
            case 10: // Skill 10: +20% AP
                iAP_SM += iAP_SM / 5;
                iAP_L  += iAP_L / 5;
                break;
            case 14: // Skill 14: +20% AP, +20 hit
                iAP_SM += iAP_SM / 5;
                iAP_L  += iAP_L / 5;
                iAttackerHitRatio += 20;
                break;
            case 21: // Skill 21: +20% AP, +50 hit
                iAP_SM += iAP_SM / 5;
                iAP_L  += iAP_L / 5;
                iAttackerHitRatio += 50;
                break;
        }

        iAttackerHitRatio += 100;  // Flat +100 hit for all super attacks
        iAP_SM += m_pClientList[sAttackerH]->m_iCustomItemValue_Attack;
        iAP_L  += m_pClientList[sAttackerH]->m_iCustomItemValue_Attack;
    }
}
```

### Dash Attack Bonus

**Location:** Game.cpp:51908-51918

```cpp
if (bIsDash == TRUE) {
    iAttackerHitRatio += 20;  // +20 hit for dash attacks

    switch (m_pClientList[sAttackerH]->m_sUsingWeaponSkill) {
        case 8:  iAP_SM += iAP_SM / 10; iAP_L += iAP_L / 10; break;  // +10%
        case 10: iAP_SM += iAP_SM / 5;  iAP_L += iAP_L / 5;  break;  // +20%
        case 14: iAP_SM += iAP_SM / 5;  iAP_L += iAP_L / 5;  break;  // +20%
    }
}
```

### Berserk Status Multiplier

**Location:** Game.cpp:52229-52232

```cpp
if (bIsAttackerBerserk == TRUE) {
    if (iAttackMode < 20) {  // Only for normal attacks, not super attacks
        iAP_SM = iAP_SM * 2;  // DOUBLE DAMAGE
        iAP_L  = iAP_L * 2;
    }
}
```

### Zone Combat Bonuses

**Fight Zone Bonus (Game.cpp:52295-52298)**
```cpp
if (/* in fight zone */) {
    iAP_SM += iAP_SM / 3;  // +33% damage
    iAP_L  += iAP_L / 3;
}
```

**Heldenian Battlefield Bonus (Game.cpp:52300-52303)**
```cpp
if (/* in Heldenian battlefield */) {
    iAP_SM += iAP_SM / 3;  // +33% damage
    iAP_L  += iAP_L / 3;
}
```

**Crusade Mode Bonuses (Game.cpp:52305-52317)**
```cpp
if (cTargetType == DEF_OWNERTYPE_PLAYER && bIsCrusadeMode && iCrusadeDuty == 1) {
    if (iLevel <= 80) {
        iAP_SM += iAP_SM;      // +100% damage (level 1-80)
        iAP_L  += iAP_L;
    } else if (iLevel <= 100) {
        iAP_SM += (iAP_SM * 7) / 10;  // +70% damage (level 81-100)
        iAP_L  += (iAP_L * 7) / 10;
    } else {
        iAP_SM += iAP_SM / 3;  // +33% damage (level 101+)
        iAP_L  += iAP_L / 3;
    }
}
```

---

## 5. Hit/Miss Resolution

### Base Hit Ratio Formula

**Location:** Game.cpp:52221-52227

```cpp
// Calculate hit probability
dTmp1 = (double)iAttackerHitRatio;
dTmp2 = (double)iTargetDefenseRatio;
dTmp3 = (dTmp1 / dTmp2) * 50.0;
iDestHitRatio = (int)dTmp3;

// Apply bounds
if (iDestHitRatio < DEF_MINIMUMHITRATIO) iDestHitRatio = DEF_MINIMUMHITRATIO;  // Min 15%
if (iDestHitRatio > DEF_MAXIMUMHITRATIO) iDestHitRatio = DEF_MAXIMUMHITRATIO;  // Max 99%
```

**Formula:**
```
Hit Chance = (Attacker Hit Ratio / Target Defense Ratio) * 50
Bounded between 15% and 99%
```

### Hit Determination Roll

**Location:** Game.cpp:52258-52260

```cpp
iResult = iDice(1, 100);
if (iResult <= iDestHitRatio) {
    // HIT - proceed with damage calculation
} else {
    return 0;  // MISS - no damage
}
```

### Hit Ratio Modifiers

**Base Hit Ratio (Game.cpp:51870)**
```cpp
iAttackerHitRatio += 50;  // All attacks start with +50 base
```

**Dexterity Bonus (Game.cpp:52140-52142)**
```cpp
if (m_pClientList[sAttackerH]->m_iDex > 50) {
    iAttackerHitRatio += (m_pClientList[sAttackerH]->m_iDex - 50);
}
```

**Weather Penalties for Ranged (Game.cpp:52145-52152)**

| Weather | Code | Hit Penalty |
|---------|------|-------------|
| Clear | 0 | None |
| Rain | 1 | -5% (`HitRatio -= HitRatio / 20`) |
| Heavy Rain | 2 | -10% (`HitRatio -= HitRatio / 10`) |
| Storm | 3 | -25% (`HitRatio -= HitRatio / 4`) |

**Direction Bonus (Game.cpp:52218)**
```cpp
// Attacking from behind (same direction as target facing)
if (cAttackerDir == cTargetDir) {
    iTargetDefenseRatio = iTargetDefenseRatio / 2;  // Target defense HALVED
}
```

This is a significant tactical advantage - backstabs effectively double hit chance.

### Protection Spell Effects

**Location:** Game.cpp:52196-52213

| Protection Type | Effect |
|-----------------|--------|
| Type 1 | Complete protection (attack returns 0) |
| Type 2 | 50% damage reduction |
| Type 3 | +40 defense |
| Type 4 | +100 defense |

```cpp
// Arrow protection completely blocks ranged attacks
if (iProtect == 1 && bNormalMissileAttack == TRUE) {
    return 0;  // Attack blocked entirely
}
```

---

## 6. Armor & Damage Absorption

### Body Part Hit Distribution

**Location:** Game.cpp:52339-52393

When an attack hits, a random body part is struck:

```cpp
iTemp = iDice(1, 10000);

if (iTemp <= 5000) {
    // 50% chance: Body armor (DEF_EQUIPPOS_BODY)
    iIndex = DEF_EQUIPPOS_BODY;
} else if (iTemp <= 7500) {
    // 25% chance: Pants + Leggings (DEF_EQUIPPOS_PANTS, DEF_EQUIPPOS_LEGGINGS)
    iIndex = DEF_EQUIPPOS_PANTS;  // Absorbs for both pieces
} else if (iTemp <= 9000) {
    // 15% chance: Arms (DEF_EQUIPPOS_ARMS)
    iIndex = DEF_EQUIPPOS_ARMS;
} else {
    // 10% chance: Head (DEF_EQUIPPOS_HEAD)
    iIndex = DEF_EQUIPPOS_HEAD;
}
```

**Hit Distribution:**
| Body Part | Chance | Equipment Slot |
|-----------|--------|----------------|
| Body/Chest | 50% | `DEF_EQUIPPOS_BODY` (2) |
| Legs | 25% | `DEF_EQUIPPOS_PANTS` (4) + `DEF_EQUIPPOS_LEGGINGS` (5) |
| Arms | 15% | `DEF_EQUIPPOS_ARMS` (3) |
| Head | 10% | `DEF_EQUIPPOS_HEAD` (1) |

### Armor Absorption Calculation

```cpp
if (iDamageAbsorption_Armor[iIndex] > 0) {
    // Maximum absorption is 80%
    if (iDamageAbsorption_Armor[iIndex] > 80) {
        iDamageAbsorption_Armor[iIndex] = 80;
    }

    dTmp1 = (double)iDamageAbsorption_Armor[iIndex];
    dTmp2 = dTmp1 / 100.0;  // Convert to percentage
    dTmp3 = (double)iAP * dTmp2;

    iAbsorbedDamage = (int)dTmp3;
    iAP = iAP - iAbsorbedDamage;  // Reduce damage by absorbed amount
}
```

**Formula:**
```
Absorbed = MIN(Armor_Value, 80) / 100 * Damage
Final_Damage = Damage - Absorbed
```

**Example:** 100 damage vs 50 armor:
- Absorption: 50/100 * 100 = 50 absorbed
- Final damage: 100 - 50 = 50

### Shield Blocking

**Location:** Game.cpp:52395-52410

Shield blocking is a skill-based chance to absorb additional damage:

```cpp
if (iDamageAbsorption_Shield > 0) {
    // Block chance based on Shield skill mastery (Skill 11)
    if (iDice(1, 100) <= m_pClientList[sTargetH]->m_cSkillMastery[11]) {
        // Shield blocked the attack

        // Shield absorption (also capped at 80%)
        if (iDamageAbsorption_Shield > 80) {
            iDamageAbsorption_Shield = 80;
        }

        dTmp1 = (double)iDamageAbsorption_Shield / 100.0;
        iAbsorbedByShield = (int)((double)iAP * dTmp1);
        iAP = iAP - iAbsorbedByShield;

        // Skill progression on successful block
        CalculateSkillSSN(sTargetH, 11, ...);  // Train shield skill
    }
}
```

**Shield Block Formula:**
```
Block Chance = Shield_Skill_Mastery %
If blocked:
  Shield_Absorbed = MIN(Shield_Value, 80) / 100 * Remaining_Damage
  Final_Damage = Remaining_Damage - Shield_Absorbed
```

### Vitality-Based Damage Reduction

**Location:** Game.cpp:52245-52247

```cpp
// Players get VIT-based damage reduction
if (cTargetType == DEF_OWNERTYPE_PLAYER) {
    iAP_SM -= (iDice(1, m_pClientList[sTargetH]->m_iVit / 10) - 1);
    iAP_L  -= (iDice(1, m_pClientList[sTargetH]->m_iVit / 10) - 1);

    // Minimum damage is 1 for player attackers, 0 for NPC attackers
    if (cAttackerType == DEF_OWNERTYPE_PLAYER) {
        if (iAP_SM < 1) iAP_SM = 1;
        if (iAP_L < 1)  iAP_L = 1;
    } else {
        if (iAP_SM < 0) iAP_SM = 0;
        if (iAP_L < 0)  iAP_L = 0;
    }
}
```

**Formula:**
```
VIT_Reduction = iDice(1, VIT/10) - 1
Final_Damage = MAX(Damage - VIT_Reduction, minimum)
```

### Magical Damage Absorption

**Physical Damage Absorption (Game.cpp:52244)**
```cpp
if (iAddAbsMD != 0) {
    reduction = (iAddAbsMD / 100.0) * damage;
    final_damage -= reduction;
}
```

**Elemental Magic Absorption (Game.cpp:27717-27759)**

| Element | Attribute Code | Absorption Field |
|---------|----------------|------------------|
| Earth | 1 | `m_iAddAbsEarth` |
| Air | 2 | `m_iAddAbsAir` |
| Fire | 3 | `m_iAddAbsFire` |
| Water | 4 | `m_iAddAbsWater` |

```cpp
// Elemental absorption calculation
switch (iAttr) {
    case 1: iAbsorbPct = m_pClientList[sTargetH]->m_iAddAbsEarth; break;
    case 2: iAbsorbPct = m_pClientList[sTargetH]->m_iAddAbsAir;   break;
    case 3: iAbsorbPct = m_pClientList[sTargetH]->m_iAddAbsFire;  break;
    case 4: iAbsorbPct = m_pClientList[sTargetH]->m_iAddAbsWater; break;
}

if (iAbsorbPct > 0) {
    iAbsorbed = (iAbsorbPct / 100.0) * iDamage;
    iDamage -= iAbsorbed;
}
```

### Special Damage-Saving Items

**Location:** Game.cpp:27761-27788

Certain items provide special damage reduction:

| Item ID | Effect |
|---------|--------|
| 335 | -20% magic damage |
| 337 | -10% magic damage |

```cpp
if (m_pClientList[sTargetH]->m_iMagicDamageSaveItemIndex == 335) {
    iDamage = iDamage - (iDamage / 5);  // 20% reduction
}
if (m_pClientList[sTargetH]->m_iMagicDamageSaveItemIndex == 337) {
    iDamage = iDamage - (iDamage / 10); // 10% reduction
}
// Note: Item durability decreases with each hit absorbed
```

---

## 7. Critical Hits & Combo System

### Combo Attack System

**Location:** Game.cpp:52264-52274

The combo system rewards consecutive successful attacks:

```cpp
m_pClientList[sAttackerH]->m_iComboAttackCount++;

// Combo counter wraps at 5 (values 1-4 are valid combo stages)
if (m_pClientList[sAttackerH]->m_iComboAttackCount > 4) {
    m_pClientList[sAttackerH]->m_iComboAttackCount = 1;
}

// Get combo bonus based on weapon skill and combo stage
iComboBonus = iGetComboAttackBonus(
    m_pClientList[sAttackerH]->m_sUsingWeaponSkill,
    m_pClientList[sAttackerH]->m_iComboAttackCount
);

// Apply combo damage bonus from equipment
if (m_pClientList[sAttackerH]->m_iComboAttackCount > 1) {
    if (m_pClientList[sAttackerH]->m_iAddCD != 0) {
        iComboBonus += m_pClientList[sAttackerH]->m_iAddCD;
    }
}

// Add combo bonus to damage
iAP_SM += iComboBonus;
iAP_L  += iComboBonus;
```

**Combo Stages:**
| Combo Count | Stage | Description |
|-------------|-------|-------------|
| 1 | First hit | No combo bonus |
| 2 | Second hit | Small bonus |
| 3 | Third hit | Medium bonus |
| 4 | Fourth hit | Large bonus |
| 5+ | Wraps to 1 | Resets |

### Super Attack Charge-Up

**Location:** Game.cpp:27834-27840

Certain items have a chance to grant super attack charges when dealing damage:

```cpp
// On successful damage
if (iDice(1, 100) < m_pClientList[sAttackerH]->m_iAddChargeCritical) {
    // Chance to gain super attack charge
    iMaxSuperAttack = m_pClientList[sAttackerH]->m_iLevel / 10;

    if (m_pClientList[sAttackerH]->m_iSuperAttackLeft < iMaxSuperAttack) {
        m_pClientList[sAttackerH]->m_iSuperAttackLeft++;

        // Notify player of new super attack charge
        SendNotifyMsg(NULL, sAttackerH, DEF_NOTIFY_SUPERATTACKLEFT,
                      m_pClientList[sAttackerH]->m_iSuperAttackLeft, 0, 0, NULL);
    }
}
```

**Formula:**
```
Charge Chance = iAddChargeCritical %
Max Charges = Level / 10
```

### Lucky Effect (Survive Lethal Damage)

**Location:** Game.cpp:27802-27805

```cpp
if (m_pClientList[sTargetH]->m_bIsLuckyEffect == TRUE) {
    if (iDice(1, 10) == 5) {  // 10% chance
        if (iDamage >= m_pClientList[sTargetH]->m_iHP) {
            // Would have been lethal, but lucky effect saves them
            iDamage = m_pClientList[sTargetH]->m_iHP - 1;  // Survive with 1 HP
        }
    }
}
```

**Effect:** 10% chance to survive any lethal blow with 1 HP remaining.

### Mana Drain on Damage

**Location:** Game.cpp:27824-27832

```cpp
if (m_pClientList[sAttackerH]->m_iAddTransMana > 0) {
    // Calculate mana gained
    dTmp1 = (double)m_pClientList[sAttackerH]->m_iAddTransMana;
    dTmp2 = (dTmp1 / 100.0) * (double)iDamage + 1.0;

    // Apply mana gain
    iNewMP = m_pClientList[sAttackerH]->m_iMP + (int)dTmp2;

    // Cap at maximum MP
    iMaxMP = (2 * m_pClientList[sAttackerH]->m_iMag) +
             (2 * m_pClientList[sAttackerH]->m_iLevel) +
             (m_pClientList[sAttackerH]->m_iInt / 2);
    if (iNewMP > iMaxMP) iNewMP = iMaxMP;

    m_pClientList[sAttackerH]->m_iMP = iNewMP;
}
```

**Formula:**
```
Mana Gained = (TransMana% / 100) * Damage + 1
Max MP = (2 * MAG) + (2 * Level) + (INT / 2)
```

---

## 8. Magic Damage System

### Base Magic Damage Calculation

**Location:** Game.cpp:27587-27640

```cpp
// Base damage roll
iDamage = iDice(sV1, sV2) + sV3;
// sV1 = dice count
// sV2 = dice sides
// sV3 = flat bonus

// Apply magic stat bonus
if (cAttackerType == DEF_OWNERTYPE_PLAYER) {
    if (m_pClientList[sAttackerH]->m_iMag <= 0) {
        dTmp1 = 1.0;  // No bonus if MAG is 0 or negative
    } else {
        dTmp1 = (double)m_pClientList[sAttackerH]->m_iMag;
        dTmp1 = dTmp1 / 3.3;  // MAG bonus = MAG / 3.3
    }

    dTmp2 = (double)iDamage;
    dTmp3 = (dTmp1 / 100.0) * dTmp2;
    iDamage = iDamage + (int)dTmp3;
}

// Add magical damage bonus from equipment
iDamage += m_pClientList[sAttackerH]->m_iAddMagicalDamage;
```

**Formula:**
```
Base = iDice(V1, V2) + V3
MAG_Multiplier = MAG / 3.3
Final = Base + (Base * MAG_Multiplier / 100) + AddMagicalDamage
      = Base * (1 + MAG / 330) + AddMagicalDamage
```

### Magic Types

| Type ID | Constant | Description |
|---------|----------|-------------|
| 1 | `DEF_MAGICTYPE_DAMAGE_SPOT` | Single target direct damage |
| 3 | `DEF_MAGICTYPE_DAMAGE_AREA` | Area of effect damage |
| 19 | `DEF_MAGICTYPE_DAMAGE_LINEAR` | Line/beam damage |
| 21 | `DEF_MAGICTYPE_DAMAGE_AREA_NOSPOT` | Meteor Strike, Mass-Missile |
| 25 | `DEF_MAGICTYPE_DAMAGE_AREA_NOSPOT_SPDOWN` | AoE + SP drain |
| 28 | `DEF_MAGICTYPE_DAMAGE_ARMOR_BREAK` | Ignores armor |
| 30 | `DEF_MAGICTYPE_DAMAGE_LINEAR_SPDOWN` | Line + SP drain |

### Elemental Attributes

| Code | Element | Color Association |
|------|---------|-------------------|
| 0 | None | - |
| 1 | Earth | Brown/Green |
| 2 | Air | White/Gray |
| 3 | Fire | Red/Orange |
| 4 | Water | Blue |

---

## 9. Status Effects in Combat

### Berserk

**Effect:** Doubles all physical damage (non-super attacks only)
**Check:** `m_cMagicEffectStatus[DEF_MAGICTYPE_BERSERK]`

```cpp
if (bIsAttackerBerserk == TRUE && iAttackMode < 20) {
    iAP_SM *= 2;
    iAP_L *= 2;
}
```

### Invisibility

**Behavior:** Attacking while invisible reveals the attacker
**Check:** `m_cMagicEffectStatus[DEF_MAGICTYPE_INVISIBILITY]`

### Protection Spells

| Type | Effect on Target |
|------|------------------|
| 1 | Complete immunity (blocks attack) |
| 2 | 50% damage reduction |
| 3 | +40 defense ratio |
| 4 | +100 defense ratio |

### Hold Object

**Behavior:** Prevents magic effects from landing; broken when damaged

### Poison

**Duration:** 12 seconds (`DEF_POISONTIME`)
**Effect:** Periodic damage over time

### Special Abilities

**Location:** `m_iSpecialAbilityType` field

| Type | Description |
|------|-------------|
| 0 | No ability |
| 1 | Enemy HP reduced by 50% |
| 2 | Applies poison effect |
| 3 | Poison effect (variant formula) |
| 4 | Warrior skill enhancement |
| 5 | Life drain related |
| 50 | Wizard ability type 1 |
| 51 | Blocks all player attacks |
| 52 | Blocks specific player attacks |
| 61 | Super attack bonus |
| 62 | Bonus vs low reputation targets |

---

## 10. PK System & Penalties

### PK Penalty Application

**Function:** `ApplyPKpenalty()`
**Location:** Game.cpp:24307-24390

**Conditions for PK Penalty:**
1. Attacker is NOT in safe mode with 0 PK count
2. Attack occurs in main cities (aresden/elvine/hunter zones)
3. Victim has 0 PK count (innocent player)

```cpp
void CGame::ApplyPKpenalty(int iClientH, int iTargetH) {
    // Experience loss for both parties
    iExpLoss_Victim   = iDice((m_pClientList[iTargetH]->m_iLevel/2 + 1), 50);
    iExpLoss_Attacker = iDice((m_pClientList[iClientH]->m_iLevel/2 + 1), 50);

    // Apply experience loss
    m_pClientList[iTargetH]->m_iExp -= iExpLoss_Victim;
    m_pClientList[iClientH]->m_iExp -= iExpLoss_Attacker;

    // Minimum exp is 0
    if (m_pClientList[iTargetH]->m_iExp < 0)
        m_pClientList[iTargetH]->m_iExp = 0;
    if (m_pClientList[iClientH]->m_iExp < 0)
        m_pClientList[iClientH]->m_iExp = 0;

    // Rating decrease
    m_pClientList[iClientH]->m_iRating -= 10;
    if (m_pClientList[iClientH]->m_iRating < -10000)
        m_pClientList[iClientH]->m_iRating = -10000;

    // Increment crime count
    m_pClientList[iClientH]->m_iPKCount++;

    // Jail punishment for safe zone PKing
    if (/* in safe zone of city */) {
        // 3 minute jail sentence
        TeleportToJail(iClientH, "arejail/elvjail");
    }
}
```

**Experience Loss Formula:**
```
Victim EXP Loss = iDice((Victim_Level/2 + 1), 50)
Attacker EXP Loss = iDice((Attacker_Level/2 + 1), 50)
```

### PK Kill Reward

**Function:** `PK_KillRewardHandler()`
**Location:** Game.cpp:24394-24417

```cpp
void CGame::PK_KillRewardHandler(int iClientH, int iTargetH) {
    // Only innocent players (0 PK count) get rewards for killing PKers
    if (m_pClientList[iClientH]->m_iPKCount == 0) {
        // Gold reward based on victim's level
        iRewardGold = iGetExpLevel(m_pClientList[iTargetH]->m_iExp) * 3;

        // Cap at maximum reward
        if (iRewardGold > DEF_MAXREWARDGOLD)  // 99,999,999
            iRewardGold = DEF_MAXREWARDGOLD;

        // Award gold and notify
        m_pClientList[iClientH]->m_iGold += iRewardGold;
        SendNotifyMsg(NULL, iClientH, DEF_NOTIFY_PKCAPTURED,
                      m_pClientList[iTargetH]->m_iPKCount, 0, 0, NULL);
    }
    // PKers killing anyone get no reward
}
```

### Enemy Kill System

**Function:** `EnemyKillRewardHandler()`
**Location:** Game.cpp:24419-24456

**Deathmatch Mode:** Any kill counts as enemy kill

**Classic Mode:**
- Same territory kill = Enemy Kill awarded
- Enemy territory kill = No Enemy Kill
- Level 80+ requirement: Victim must be level 30+ (or 80+)

```cpp
void CGame::EnemyKillRewardHandler(int iClientH, int iTargetH) {
    // Deathmatch mode
    if (m_bDeathMatchMode == TRUE) {
        // All kills count
        m_pClientList[iClientH]->m_iEnemyKillCount++;
        return;
    }

    // Classic mode - check territory
    if (m_pClientList[iClientH]->m_cSide == m_pClientList[iTargetH]->m_cSide) {
        // Same faction - counts as enemy kill (traitor)
        m_pClientList[iClientH]->m_iEnemyKillCount++;
    }

    // Level requirement for high-level players
    if (m_pClientList[iClientH]->m_iLevel >= 80) {
        if (m_pClientList[iTargetH]->m_iLevel < 30) {
            return;  // No EK for killing low levels
        }
    }

    // Log the kill
    _bPKLog(DEF_PKLOG_BYENERMY, ...);
}
```

### Rating System

**Rating Bounds:**
- Minimum: -10,000
- Maximum: +10,000

**Rating Changes:**
- PK an innocent: -10 rating
- Kill a PKer (as innocent): No rating change, gold reward instead

---

## 11. Combat Modes & Restrictions

### Safe Attack Mode

**Toggle Function:** `ToggleSafeAttackModeHandler()`
**Location:** Game.cpp:34326

Safe attack mode restricts who players can attack to prevent accidental PvP.

```cpp
void CGame::ToggleSafeAttackModeHandler(int iClientH) {
    m_pClientList[iClientH]->m_bIsSafeAttackMode =
        !m_pClientList[iClientH]->m_bIsSafeAttackMode;

    SendNotifyMsg(NULL, iClientH, DEF_NOTIFY_SAFEATTACKMODE,
                  m_pClientList[iClientH]->m_bIsSafeAttackMode, 0, 0, NULL);
}
```

**Safe Mode Attack Restrictions (Game.cpp:27696-27710, 51988-52003):**

```cpp
if (m_pClientList[sAttackerH]->m_bIsSafeAttackMode == TRUE) {
    iSideCondition = iGetPlayerRelationship(sAttackerH, sTargetH);

    // Valid targets in safe mode:
    // - Condition 7: Enemy guild members
    // - Condition 2: Party members (can attack for duels)
    // - Condition 6: Specific relationship type

    switch (iSideCondition) {
        case 2:  // Party member
        case 6:  // Specific relation
        case 7:  // Enemy guild
            // Attack allowed
            break;
        default:
            // Attack blocked or damage halved
            if (/* in fight zone */) {
                // Only attack different guilds
                if (sameGuild) return 0;  // Block
            } else {
                iAP = iAP / 2;  // Halve damage
            }
    }
}
```

### Combat Mode Toggle

**Function:** `ToggleCombatModeHandler()`
**Location:** Game.cpp:16735

Toggles between peaceful and combat-ready states.

### Zone Types

**Peaceful Zones:**
- Cities (Aresden, Elvine)
- Safe areas
- No PvP damage

**Battle Zones:**
- Fight zones (`DEF_MAXFIGHTZONE = 10` per map)
- War zones
- Crusade battlefields
- Bonus damage (+33%)

**Neutral Zones:**
- Normal world areas
- Standard PvP rules apply

### Attack Type Validation

**Function:** `_CheckAttackType()`
**Location:** Game.cpp:34777

Validates that attack types are legal for the current context.

---

## 12. Combat Data Structures

### Player Combat Fields (Client.h)

```cpp
// Vital Stats
int m_iHP;                    // Current health points
int m_iMP;                    // Current mana points
int m_iSP;                    // Current stamina points

// Core Attributes
int m_iStr;                   // Strength - melee damage bonus
int m_iDex;                   // Dexterity - hit ratio bonus
int m_iVit;                   // Vitality - damage reduction
int m_iInt;                   // Intelligence - MP pool
int m_iMag;                   // Magic - magic damage bonus
int m_iChr;                   // Charisma - shop prices, NPC interaction
int m_iLuck;                  // Luck - random bonuses

// Combat Ratings
int m_iHitRatio;              // Base hit chance
int m_iDefenseRatio;          // Base defense value
int m_iLastDamage;            // Last damage taken

// Attack Timing
DWORD m_dwRecentAttackTime;   // Timestamp of last attack
DWORD m_dwAttackLAT;          // Attack latency tracking
int m_iAttackMsgRecvCount;    // Attack message counter (anti-cheat)

// Combo System
int m_iComboAttackCount;      // Current combo stage (1-4)
int m_iAddCD;                 // Combo damage bonus from equipment

// Super Attack
int m_iSuperAttackLeft;       // Remaining super attack charges
int m_iAddChargeCritical;     // % chance to gain charge on hit

// Weapon Data
short m_sUsingWeaponSkill;    // Current weapon skill index
char m_cAttackDiceThrow_SM;   // Weapon dice count (small targets)
char m_cAttackDiceThrow_L;    // Weapon dice count (large targets)
char m_cAttackDiceRange_SM;   // Weapon dice sides (small targets)
char m_cAttackDiceRange_L;    // Weapon dice sides (large targets)
char m_cAttackBonus_SM;       // Weapon flat bonus (small targets)
char m_cAttackBonus_L;        // Weapon flat bonus (large targets)

// Skills
char m_cSkillMastery[DEF_MAXSKILLTYPE];  // Skill mastery levels (0-255)
BOOL m_bSkillUsingStatus[DEF_MAXSKILLTYPE]; // Active skill states

// Damage Absorption
int m_iAddAbsPD;              // Physical damage absorption %
int m_iAddAbsMD;              // Magical damage absorption %
int m_iAddAbsAir;             // Air element absorption %
int m_iAddAbsEarth;           // Earth element absorption %
int m_iAddAbsFire;            // Fire element absorption %
int m_iAddAbsWater;           // Water element absorption %
int m_iDamageAbsorption_Armor[DEF_MAXITEMEQUIPPOS];  // Per-piece absorption
int m_iDamageAbsorption_Shield;  // Shield absorption value

// Special Effects
int m_iMagicDamageSaveItemIndex;  // Equipped damage-save item ID
char m_cHeroArmourBonus;      // Special armor bonus flag
int m_iSpecialAbilityType;    // Active special ability
BOOL m_bIsSpecialAbilityEnabled;  // Is ability active
BOOL m_bIsLuckyEffect;        // Lucky effect (survive lethal)
int m_iAddTransMana;          // Damage->Mana conversion %
int m_iAddMagicalDamage;      // Flat magic damage bonus

// Status Effects
char m_cMagicEffectStatus[DEF_MAXMAGICEFFECTS];  // Active spell effects

// PK System
int m_iRating;                // PK reputation (-10000 to +10000)
int m_iPKCount;               // Active PK count
BOOL m_bIsSafeAttackMode;     // Safe attack mode flag
BOOL m_bIsPlayerCivil;        // Innocent player flag

// Faction
char m_cSide;                 // 0=Neutral, 1=Aresden, 2=Elvine
BOOL m_bIsNeutral;            // In neutral zone

// Equipment
short m_sItemEquipmentStatus[DEF_MAXITEMEQUIPPOS];  // Equipped item indices

// State
BOOL m_bIsKilled;             // Death state
char m_cMapIndex;             // Current map
short m_sX, m_sY;             // Position
char m_cDir;                  // Facing direction (0-7)
```

### NPC Combat Fields (Npc.h)

```cpp
// Vital Stats
int m_iHP;                    // Current health
int m_iExp;                   // Experience reward on death

// Combat Ratings
int m_iHitDice;               // HP calculation dice
int m_iDefenseRatio;          // Defense value
int m_iHitRatio;              // Base hit chance
int m_iMagicHitRatio;         // Magic hit chance

// Attack Data
char m_cAttackDiceThrow;      // Attack dice count
char m_cAttackDiceRange;      // Attack dice sides
char m_cAttackBonus;          // Attack flat bonus
int m_iAttackRange;           // Attack distance (cells)
int m_iAttackCount;           // Attack counter

// AI & Behavior
char m_cBehavior;             // Current behavior state (0-4)
short m_sBehaviorTurnCount;   // Behavior countdown timer
int m_iTargetIndex;           // Current target ID
char m_cTargetType;           // Target type (Player/NPC)
char m_cTargetSearchRange;    // Detection radius
int m_iAttackStrategy;        // AI attack pattern
int m_iAILevel;               // Intelligence level

// Attributes
char m_cSize;                 // 0=Small/Medium, 1=Large
char m_cAttribute;            // Elemental attribute
char m_cResistMagic;          // Magic resistance (0-100)
char m_cMagicLevel;           // Magic skill level

// Courage
int m_iMinBravery;            // Minimum courage rating
char m_cBravery;              // Current courage level

// Rewards
int m_iExpDiceMin;            // Minimum EXP reward
int m_iExpDiceMax;            // Maximum EXP reward
int m_iGoldDiceMin;           // Minimum gold drop
int m_iGoldDiceMax;           // Maximum gold drop

// Special
char m_cSpecialAbility;       // Special ability ID
int m_iAbsDamage;             // Absolute damage modifier
int m_iLastDamage;            // Last damage taken

// Status Effects
char m_cMagicEffectStatus[DEF_MAXMAGICEFFECTS];  // Active effects
```

### Equipment Positions

```cpp
// Item.h:15-28
#define DEF_EQUIPPOS_NONE      0
#define DEF_EQUIPPOS_HEAD      1   // Helmet, Crown
#define DEF_EQUIPPOS_BODY      2   // Chest armor, Tunic
#define DEF_EQUIPPOS_ARMS      3   // Gauntlets, Sleeves
#define DEF_EQUIPPOS_PANTS     4   // Leg armor
#define DEF_EQUIPPOS_LEGGINGS  5   // Lower leg armor
#define DEF_EQUIPPOS_NECK      6   // Necklace, Amulet
#define DEF_EQUIPPOS_LHAND     7   // Left hand (shield)
#define DEF_EQUIPPOS_RHAND     8   // Right hand (weapon)
#define DEF_EQUIPPOS_TWOHAND   9   // Two-handed weapon
#define DEF_EQUIPPOS_RFINGER   10  // Right finger ring
#define DEF_EQUIPPOS_LFINGER   11  // Left finger ring
#define DEF_EQUIPPOS_BACK      12  // Cape, Cloak
#define DEF_EQUIPPOS_RELEASEALL 13 // Special: unequip all
```

---

## 13. Combat Message Protocol

### Object Actions (ActionID.h)

```cpp
#define DEF_OBJECTATTACK                 3    // Attack motion
#define DEF_OBJECTDAMAGE                 6    // Damage received
#define DEF_OBJECTDAMAGEMOVE             7    // Damage + knockback
#define DEF_OBJECTATTACKMOVE             8    // Attack while moving
#define DEF_OBJECTMOTION_ATTACK_CONFIRM  1030 // Attack confirmation
```

### Combat Notifications

```cpp
#define DEF_NOTIFY_SUPERATTACKLEFT    0x0B52  // Super attack charges remaining
#define DEF_NOTIFY_SAFEATTACKMODE     0x0B51  // Safe PvP mode toggle
#define DEF_NOTIFY_GLOBALATTACKMODE   0x0B73  // Global PvP mode
#define DEF_NOTIFY_DAMAGEMOVE         0x0B74  // Damage on movement
#define DEF_NOTIFY_METEORSTRIKEHIT    0x0B9C  // Meteor strike hit
#define DEF_NOTIFY_HP                 // HP update notification
#define DEF_NOTIFY_PKPENALTY          // PK penalty notification
#define DEF_NOTIFY_PKCAPTURED         // PK capture notification
```

### Combat Toggle Commands

```cpp
#define DEF_COMMONTYPE_TOGGLECOMBATMODE     0x0A0B  // PvP mode toggle
#define DEF_COMMONTYPE_TOGGLESAFEATTACKMODE 0x0A18  // Safe attack mode toggle
```

### Attack Message Flow

```
Client -> Server: Attack Request
  - Position (sX, sY)
  - Target position (dX, dY)
  - Attack type (wType)
  - Direction (cDir)
  - Client timestamp (dwClientTime)

Server Processing:
  1. Validate request
  2. Calculate damage
  3. Apply effects

Server -> Client: Attack Result
  - Attacker motion update
  - Target damage notification
  - HP updates
  - Status effect changes
  - Kill notifications (if applicable)
```

---

## 14. Anti-Cheat & Rate Limiting

### Attack Frequency Detection

**Location:** Game.cpp:9748-9757

```cpp
// Track attack messages
m_pClientList[iClientH]->m_iAttackMsgRecvCount++;

// Check for speed hacking
if (m_pClientList[iClientH]->m_iAttackMsgRecvCount >= 7) {
    DWORD dwElapsed = dwTime - m_pClientList[iClientH]->m_dwAttackLAT;

    if (dwElapsed < 3500) {
        // 7+ attacks in under 3.5 seconds = speed hack
        DeleteClient(iClientH, TRUE, TRUE);
        return 0;
    }

    // Reset counter
    m_pClientList[iClientH]->m_iAttackMsgRecvCount = 0;
}

// Update last attack timestamp
m_pClientList[iClientH]->m_dwAttackLAT = dwTime;
```

**Detection Threshold:** 7 attacks in 3500ms (3.5 seconds) = ban

### Minimum Attack Interval

**Location:** Game.cpp:9846

```cpp
if ((dwTime - m_pClientList[iClientH]->m_dwRecentAttackTime) <= 100) {
    return 0;  // Block attack - too fast
}
```

**Minimum Interval:** 100ms between attacks (10 attacks per second maximum)

### Attack Type Validation

**Function:** `_CheckAttackType()`
**Location:** Game.cpp:34777

Validates that attack types match equipped weapons and current state.

---

## 15. Special Weapons & Abilities

### Unique Weapon Effects

| Item ID | Name | Special Effect |
|---------|------|----------------|
| 732, 738 | BerserkWand (MS.20/MS.10) | +1 AP each |
| 847 | Night Sword | +4 AP during night |
| 848 | Day Sword | +4 AP during day |
| 849 | KlonessBlade | Bonus damage based on rating |
| 850 | KlonessAxe | Bonus damage based on rating |
| 851 | KlonessEsterk | Bonus damage based on rating |
| 859 | NecklaceOfKloness | Damage scales with target's rating |
| 863, 864 | KlonessWand | Rating-based bonuses |
| 873 | Firebow | Creates fire projectiles |
| 874 | DirectionalBow | Multi-direction attack |
| 845 | Special Ranged | 4-cell attack range |

### Day/Night Weapon Bonuses

**Location:** Game.cpp (weapon effect processing)

```cpp
// Night sword bonus
if (iItemID == 847 && bIsNightTime) {
    iAP_SM += 4;
    iAP_L += 4;
}

// Day sword bonus
if (iItemID == 848 && !bIsNightTime) {
    iAP_SM += 4;
    iAP_L += 4;
}
```

### Kloness Items (Rating-Based)

```cpp
// Kloness weapons deal bonus damage based on rating difference
if (iItemID == 849 || iItemID == 850 || iItemID == 851) {
    int iRatingDiff = m_pClientList[sAttackerH]->m_iRating -
                      m_pClientList[sTargetH]->m_iRating;
    // Positive rating diff = more damage
    iAP += iRatingDiff / 100;
}
```

### Hero Armor Bonus

**Location:** Game.cpp:52287-52293

```cpp
if (m_pClientList[sAttackerH]->m_cHeroArmourBonus == 1) {
    iAttackerHitRatio += 100;  // +100 hit
    iAP_SM += 5;               // +5 damage
    iAP_L += 5;
}
```

### Special Ability Types

| Type | Effect Description |
|------|---------------------|
| 0 | No special ability |
| 1 | Target HP halved |
| 2 | Applies poison |
| 3 | Poison variant |
| 4 | Warrior skill boost |
| 5 | Life drain effect |
| 50 | Wizard ability 1 |
| 51 | Blocks all player attacks |
| 52 | Blocks specific attacks |
| 61 | Super attack bonus |
| 62 | Bonus vs low reputation |

---

## 16. Combat Examples

### Example 1: Basic Melee Attack

**Scenario:** Level 50 Warrior with Sword (2d12+5) attacks Level 30 Goblin

```
ATTACKER:
- STR: 100
- DEX: 80
- Weapon: Sword (2d12+5 small, 3d15+8 large)
- Skill Mastery: 60

TARGET (Goblin):
- Size: Small (use _SM damage)
- Defense Ratio: 80
- Armor: 30%

STEP 1: Base Damage
  Base = iDice(2, 12) + 5 = 7-29 (avg 18)

STEP 2: STR Bonus
  Multiplier = 100 / 5 = 20
  Bonus = Base * 20 / 100 = Base * 0.20
  Final AP = 18 * 1.20 = 21.6 ≈ 22

STEP 3: Hit Calculation
  Attacker Hit = 50 (base) + (80-50) (DEX) = 80
  Hit Chance = (80 / 80) * 50 = 50%
  Roll: 35 → HIT (35 ≤ 50)

STEP 4: Armor Absorption
  Body part roll: 2500 → Body (50% chance)
  Armor absorbs: 30% of 22 = 6.6 ≈ 7

FINAL DAMAGE: 22 - 7 = 15 HP
```

### Example 2: Super Attack with Combo

**Scenario:** Level 80 player using super attack, combo stage 3

```
ATTACKER:
- Level: 80
- STR: 150
- Weapon Skill: 10 (Sword Mastery)
- Super Attack Charges: 3
- Combo Stage: 3
- Combo Damage Bonus: +5

BASE DAMAGE: 25 (after STR bonus)

SUPER ATTACK BONUSES:
  Level bonus = 80/100 = 0.80 → +80% = 25 * 1.80 = 45
  Skill 10 bonus = +20% = 45 * 1.20 = 54
  Hit bonus = +100

COMBO BONUS:
  Stage 3 combo bonus: +8 (from skill table)
  Equipment bonus: +5
  Total: 54 + 13 = 67

FINAL AP: 67 (before armor)
```

### Example 3: Backstab Attack

**Scenario:** Attacking from behind (same direction as target)

```
NORMAL ATTACK:
  Attacker Hit: 80
  Target Defense: 100
  Hit Chance = (80/100) * 50 = 40%

BACKSTAB ATTACK:
  Target Defense HALVED: 100 / 2 = 50
  Hit Chance = (80/50) * 50 = 80%

DIFFERENCE: 40% → 80% (doubled hit chance!)
```

### Example 4: Magic Damage with Elemental Absorption

**Scenario:** Fire spell (3d8+10) vs player with 40% fire resistance

```
SPELL:
  Element: Fire (3)
  Dice: 3d8+10

ATTACKER:
  MAG: 99

DEFENDER:
  Fire Absorption: 40%

STEP 1: Base Damage
  Base = iDice(3, 8) + 10 = 13-34 (avg 23.5)

STEP 2: MAG Bonus
  Multiplier = 99 / 3.3 = 30
  Bonus = 23 * 30 / 100 = 6.9
  Total = 23 + 7 = 30

STEP 3: Elemental Absorption
  Fire absorbs: 40% of 30 = 12

FINAL DAMAGE: 30 - 12 = 18 magic damage
```

---

## 17. Modernization Notes

### Key Insights for Porting

1. **Hit System Bounds**
   - Always enforce 15%-99% hit range
   - Never allow 0% or 100% hit chance
   - This is core to game balance

2. **Armor Per-Piece**
   - Absorption calculated per body part
   - Maximum 80% absorption per piece
   - Random body part selection adds variance

3. **Stat Scaling**
   - STR: Linear bonus to physical damage (STR/5 multiplier)
   - DEX: Linear bonus to hit (DEX-50 if >50)
   - VIT: Random reduction (1 to VIT/10)
   - MAG: Linear bonus to magic (MAG/3.3 multiplier)

4. **Combo Encouragement**
   - System rewards consecutive attacks
   - Equipment can boost combo damage
   - Resets after 4 hits

5. **PK Deterrent**
   - Heavy EXP penalties
   - Jail time in safe zones
   - Rating system tracks behavior

6. **Attack Throttling**
   - 100ms minimum between attacks
   - 7 attacks in 3.5s = speed hack ban
   - Critical for server stability

### Suggested Modern Architecture

```cpp
// combat_system.h
namespace hb::combat {

struct attack_result {
    bool hit;
    int raw_damage;
    int absorbed;
    int final_damage;
    bool critical;
    bool blocked;
    int combo_stage;
};

struct attack_context {
    entity_id attacker;
    entity_id target;
    weapon_data weapon;
    int attack_mode;
    bool is_dash;
    direction attack_dir;
};

class combat_system {
public:
    auto resolve_attack(const attack_context& ctx) -> attack_result;
    auto calculate_hit_chance(const attack_context& ctx) -> int;
    auto calculate_damage(const attack_context& ctx) -> int;
    auto apply_armor(int damage, entity_id target) -> int;

private:
    auto get_str_bonus(int str) -> double;
    auto get_dex_bonus(int dex) -> int;
    auto roll_body_part() -> equipment_slot;
};

} // namespace hb::combat
```

### Constants to Extract

```cpp
namespace hb::combat::constants {
    inline constexpr int min_hit_ratio = 15;
    inline constexpr int max_hit_ratio = 99;
    inline constexpr int max_armor_absorption = 80;
    inline constexpr int base_hit_bonus = 50;
    inline constexpr int backstab_defense_divisor = 2;
    inline constexpr int super_attack_hit_bonus = 100;
    inline constexpr int berserk_damage_multiplier = 2;
    inline constexpr int attack_throttle_ms = 100;
    inline constexpr int speedhack_threshold_attacks = 7;
    inline constexpr int speedhack_threshold_ms = 3500;
}
```

---

## Appendix A: Complete Function List

| Function | Location | Purpose |
|----------|----------|---------|
| `iClientMotion_Attack_Handler` | Game.cpp:9728 | Main attack entry point |
| `iCalculateAttackEffect` | Game.cpp:51652 | Damage calculation engine |
| `Effect_Damage_Spot` | Game.cpp:27569 | Single-target magic damage |
| `Effect_Damage_Spot_Type2` | Game.cpp:28059 | Type 2 magic damage |
| `Effect_Damage_Spot_DamageMove` | Game.cpp:28505 | Knockback damage |
| `Effect_Damage_Area` | Game.cpp:28900+ | AoE damage |
| `Effect_Damage_Linear` | Game.cpp:29200+ | Line damage |
| `ClientKilledHandler` | Game.cpp:15415 | Player death |
| `NpcKilledHandler` | Game.cpp:10717 | NPC death |
| `NpcBehavior_Attack` | Game.cpp:10212 | NPC attack AI |
| `ApplyPKpenalty` | Game.cpp:24307 | PK penalties |
| `PK_KillRewardHandler` | Game.cpp:24394 | PK kill rewards |
| `EnemyKillRewardHandler` | Game.cpp:24419 | Enemy kill rewards |
| `ToggleCombatModeHandler` | Game.cpp:16735 | Combat mode toggle |
| `ToggleSafeAttackModeHandler` | Game.cpp:34326 | Safe attack toggle |
| `SetDefenseShieldFlag` | Game.cpp:43831 | Shield blocking |
| `_CheckAttackType` | Game.cpp:34777 | Attack validation |
| `ArmorLifeDecrement` | Game.cpp:44410 | Armor durability |
| `bCalculateEnduranceDecrement` | Game.cpp:235 | Endurance calc |
| `iGetComboAttackBonus` | Game.cpp | Combo bonus lookup |
| `iGetPlayerRelationship` | Game.cpp | PvP relationship |

---

## Appendix B: Dice Function Reference

The `iDice(count, sides)` function simulates rolling dice:

```cpp
int iDice(int count, int sides) {
    if (count <= 0 || sides <= 0) return 0;

    int total = 0;
    for (int i = 0; i < count; i++) {
        total += (rand() % sides) + 1;
    }
    return total;
}
```

**Examples:**
- `iDice(1, 6)` = 1-6 (single d6)
- `iDice(2, 10)` = 2-20 (2d10)
- `iDice(3, 8) + 5` = 8-29 (3d8+5)

**Statistical Properties:**
- Average of NdS = N * (S+1) / 2
- Example: 2d12 average = 2 * 13 / 2 = 13
