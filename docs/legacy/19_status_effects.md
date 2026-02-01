# Legacy Status Effects System Documentation

## Table of Contents

1. [Overview](#overview)
2. [Data Structures](#data-structures)
   - [Player Effect Storage](#player-effect-storage)
   - [NPC Effect Storage](#npc-effect-storage)
   - [Visual Status Flags](#visual-status-flags)
   - [Delay Event System](#delay-event-system)
3. [Effect Types](#effect-types)
   - [Magic Type Constants](#magic-type-constants)
   - [Complete Effect Reference](#complete-effect-reference)
4. [Positive Effects (Buffs)](#positive-effects-buffs)
   - [Protection Effects](#protection-effects)
   - [Berserk Mode](#berserk-mode)
   - [Invisibility](#invisibility)
5. [Negative Effects (Debuffs)](#negative-effects-debuffs)
   - [Poison System](#poison-system)
   - [Ice/Freeze Effect](#icefreeze-effect)
   - [Hold Object (Paralysis)](#hold-object-paralysis)
   - [Inhibition Casting (Silence)](#inhibition-casting-silence)
   - [Confusion/Illusion](#confusionillusion)
6. [Transformation Effects](#transformation-effects)
   - [Polymorph System](#polymorph-system)
7. [Environmental Effects](#environmental-effects)
   - [Hunger System](#hunger-system)
   - [Time-Based Modifiers](#time-based-modifiers)
8. [Effect Processing](#effect-processing)
   - [Tick Processing](#tick-processing)
   - [DelayEvent System](#delayevent-system)
   - [Effect Removal](#effect-removal)
9. [Status Flag Functions](#status-flag-functions)
10. [Cancellation System](#cancellation-system)
11. [Constants Reference](#constants-reference)
12. [Related Functions](#related-functions)

---

## Overview

The Helbreath legacy status effects system manages temporary and persistent effects applied to players and NPCs. Effects are stored in two primary mechanisms:

1. **`m_cMagicEffectStatus[100]`** - Array storing active magic effects (indexed by magic type)
2. **`m_iStatus`** - Bit flags for visual status display

Effects are processed via:
- **Tick Processing** - Main game loop updates (every 3 seconds)
- **DelayEvent System** - Scheduled effect expiration and triggers
- **Direct Application** - Immediate effect application from spells/items

---

## Data Structures

### Player Effect Storage

Located in `Client.h` and `Client.cpp`:

```cpp
// Client.h - Status Effect Fields
class CClient {
    // Visual status flags (bit flags)
    int   m_iStatus;                              // Line 85

    // Magic effect array - 100 concurrent slots
    char  m_cMagicEffectStatus[DEF_MAXMAGICEFFECTS];  // Line 150

    // Poison-specific fields
    BOOL  m_bIsPoisoned;           // Line 160 - Poison active flag
    int   m_iPoisonLevel;          // Line 161 - Poison severity/duration
    DWORD m_dwPoisonTime;          // Line 162 - Last poison tick time

    // Hunger system
    int   m_iHungerStatus;         // Line 155 - Hunger level (0-100)
    DWORD m_dwHungerTime;          // Line 87 - Last hunger tick time

    // Casting inhibition
    BOOL  m_bInhibition;           // Line 126 - Cannot cast spells

    // Polymorph
    short m_sType;                 // Line 78 - Current visual type
    short m_sOriginalType;         // Line 79 - Original type (before polymorph)

    // Time-based effect counters
    int   m_iTimeLeft_ShutUp;          // Line 174 - Chat silenced time
    int   m_iTimeLeft_Rating;          // Line 175 - Rating cooldown
    int   m_iTimeLeft_ForceRecall;     // Line 176 - Force recall timer
    int   m_iTimeLeft_FirmStaminar;    // Line 177 - Stamina protection time
};
```

**Initialization in `Client.cpp`:**
```cpp
// Line 123-141
for (i = 0; i < DEF_MAXMAGICEFFECTS; i++)
    m_cMagicEffectStatus[i] = 0;

m_iHungerStatus  = 100;   // Full hunger
m_bIsPoisoned    = FALSE;
m_iPoisonLevel   = NULL;
m_bInhibition    = FALSE;
m_iStatus        = 0;

m_iTimeLeft_ShutUp       = 0;
m_iTimeLeft_Rating       = 0;
m_iTimeLeft_ForceRecall  = 0;
m_iTimeLeft_FirmStaminar = 0;
```

### NPC Effect Storage

Located in `Npc.h` and `Npc.cpp`:

```cpp
// Npc.h - Status Effect Fields
class CNpc {
    int   m_iStatus;                              // Line 58
    char  m_cMagicEffectStatus[DEF_MAXMAGICEFFECTS];  // Line 111
    short m_sType;                                // Line 55
    short m_sOriginalType;                        // Line 56
};

// Npc.cpp initialization (Line 20-34)
for (i = 0; i < DEF_MAXMAGICEFFECTS; i++)
    m_cMagicEffectStatus[i] = 0;
m_iStatus        = NULL;
m_sOriginalType  = NULL;
```

### Visual Status Flags

The `m_iStatus` field uses bit flags for visual effects displayed to clients:

| Bit | Hex Value | Effect | Set Function |
|-----|-----------|--------|--------------|
| 4 | `0x00000010` | Invisibility | `SetInvisibilityFlag()` |
| 5 | `0x00000020` | Berserk | `SetBerserkFlag()` |
| 6 | `0x00000040` | Frozen/Ice | `SetIceFlag()` |
| 7 | `0x00000080` | Poisoned | `SetPoisonFlag()` |
| 16 | `0x00010000` | Experience Boost (3x) | Special event |
| 17 | `0x00020000` | Hero Item Aura | `SetHeroFlag()` |
| 20 | `0x00100000` | Inhibit Casting | `SetInhibitionCastingFlag()` |
| 21 | `0x00200000` | Illusion Movement | `SetIllusionMovementFlag()` |
| 22 | `0x00400000` | Angel Status | Special |
| 23 | `0x00800000` | Demon Status | Special |
| 24 | `0x01000000` | Illusion | `SetIllusionFlag()` |
| 25 | `0x02000000` | Defense Shield | `SetDefenseShieldFlag()` |
| 26 | `0x04000000` | Magic Protection | `SetMagicProtectionFlag()` |
| 27 | `0x08000000` | Protection From Arrow | `SetProtectionFromArrowFlag()` |

### Delay Event System

Located in `DelayEvent.h`:

```cpp
// DelayEvent.h - Effect Timer Structure
#define DEF_DELAYEVENTTYPE_DAMAGEOBJECT             1
#define DEF_DELAYEVENTTYPE_MAGICRELEASE             2  // Most status effect expirations
#define DEF_DELAYEVENTTYPE_USEITEM_SKILL            3
#define DEF_DELAYEVENTTYPE_METEORSTRIKE             4
#define DEF_DELAYEVENTTYPE_DOMETEORSTRIKEDAMAGE     5
#define DEF_DELAYEVENTTYPE_CALCMETEORSTRIKEEFFECT   6
#define DEF_DELAYEVENTTYPE_ANCIENT_TABLET           7

class CDelayEvent {
    int   m_iDelayType;      // Type of delayed action
    int   m_iEffectType;     // Magic type being released
    char  m_cMapIndex;       // Map where event occurs
    int   m_dX, m_dY;        // Position
    int   m_iTargetH;        // Target handle (player/NPC index)
    char  m_cTargetType;     // DEF_OWNERTYPE_PLAYER or DEF_OWNERTYPE_NPC
    int   m_iV1, m_iV2, m_iV3;  // Effect-specific parameters
    DWORD m_dwTriggerTime;   // Time when event triggers
};
```

**Maximum Delayed Events:** `DEF_MAXDELAYEVENTS = 60000`

---

## Effect Types

### Magic Type Constants

Defined in `Magic.h`:

```cpp
// Magic effect type indices (also used as m_cMagicEffectStatus array indices)
#define DEF_MAGICTYPE_DAMAGE_SPOT              1
#define DEF_MAGICTYPE_HPUP_SPOT                2
#define DEF_MAGICTYPE_DAMAGE_AREA              3
#define DEF_MAGICTYPE_SPDOWN_SPOT              4
#define DEF_MAGICTYPE_SPDOWN_AREA              5
#define DEF_MAGICTYPE_SPUP_SPOT                6
#define DEF_MAGICTYPE_SPUP_AREA                7
#define DEF_MAGICTYPE_TELEPORT                 8
#define DEF_MAGICTYPE_SUMMON                   9
#define DEF_MAGICTYPE_CREATE                   10
#define DEF_MAGICTYPE_PROTECT                  11   // Protection buffs
#define DEF_MAGICTYPE_HOLDOBJECT               12   // Paralysis/Hold
#define DEF_MAGICTYPE_INVISIBILITY             13
#define DEF_MAGICTYPE_CREATE_DYNAMIC           14
#define DEF_MAGICTYPE_POSSESSION               15
#define DEF_MAGICTYPE_CONFUSE                  16   // Illusion effects
#define DEF_MAGICTYPE_POISON                   17
#define DEF_MAGICTYPE_BERSERK                  18
#define DEF_MAGICTYPE_DAMAGE_LINEAR            19
#define DEF_MAGICTYPE_POLYMORPH                20
#define DEF_MAGICTYPE_DAMAGE_AREA_NOSPOT       21
#define DEF_MAGICTYPE_TREMOR                   22
#define DEF_MAGICTYPE_ICE                      23   // Freeze effect
// 24 unused
#define DEF_MAGICTYPE_DAMAGE_AREA_NOSPOT_SPDOWN 25
#define DEF_MAGICTYPE_ICE_LINEAR               26   // Blizzard
// 27 unused
#define DEF_MAGICTYPE_DAMAGE_AREA_ARMOR_BREAK  28
#define DEF_MAGICTYPE_CANCELLATION             29   // Removes all buffs
#define DEF_MAGICTYPE_DAMAGE_LINEAR_SPDOWN     30
#define DEF_MAGICTYPE_INHIBITION               31   // Silence (cannot cast)
#define DEF_MAGICTYPE_RESURRECTION             32
#define DEF_MAGICTYPE_SCAN                     33

#define DEF_MAXMAGICEFFECTS                    100
```

### Complete Effect Reference

| Index | Effect | Duration | Stacking | Notes |
|-------|--------|----------|----------|-------|
| 11 | Protection | Config-based | No | Subtypes 1-5 |
| 12 | Hold Object | Config-based | No | Paralysis |
| 13 | Invisibility | 60 seconds typical | No | Breaks on attack |
| 16 | Confuse/Illusion | Config-based | No | Subtypes 1-4 |
| 17 | Poison | Ticks down | No | Uses separate tracking |
| 18 | Berserk | Config-based | No | Attack boost |
| 20 | Polymorph | Config-based | No | Visual transform |
| 23 | Ice/Freeze | Config-based | No | Movement locked |
| 31 | Inhibition | Config-based | No | Cannot cast spells |

---

## Positive Effects (Buffs)

### Protection Effects

Protection magic (`DEF_MAGICTYPE_PROTECT = 11`) has subtypes stored in `m_sValue4`:

| Subtype | Name | Visual Flag | Description |
|---------|------|-------------|-------------|
| 1 | Protection From Arrow | `0x08000000` | Reduces arrow damage |
| 2 | Protection From Magic | `0x04000000` | Magic resistance |
| 3 | Defense Shield | `0x02000000` | Physical defense boost |
| 4 | Great Defense Shield | `0x02000000` | Enhanced physical defense |
| 5 | Absolute Magic Protection | `0x04000000` | Complete magic immunity |

**Application (Game.cpp:18802-18860):**
```cpp
case DEF_MAGICTYPE_PROTECT:
    m_pMapList[m_pClientList[iClientH]->m_cMapIndex]->GetOwner(&sOwnerH, &cOwnerType, dX, dY);

    switch (cOwnerType) {
    case DEF_OWNERTYPE_PLAYER:
        if (m_pClientList[sOwnerH] == NULL) goto MAGIC_NOEFFECT;
        // Check if protection already active
        if (m_pClientList[sOwnerH]->m_cMagicEffectStatus[DEF_MAGICTYPE_PROTECT] != 0)
            goto MAGIC_NOEFFECT;

        // Store protection level
        m_pClientList[sOwnerH]->m_cMagicEffectStatus[DEF_MAGICTYPE_PROTECT] =
            (char)m_pMagicConfigList[sType]->m_sValue4;

        // Set visual flag based on subtype
        switch (m_pMagicConfigList[sType]->m_sValue4) {
        case 1:
            SetProtectionFromArrowFlag(sOwnerH, DEF_OWNERTYPE_PLAYER, TRUE);
            break;
        case 2:
        case 5:
            SetMagicProtectionFlag(sOwnerH, DEF_OWNERTYPE_PLAYER, TRUE);
            break;
        case 3:
        case 4:
            SetDefenseShieldFlag(sOwnerH, DEF_OWNERTYPE_PLAYER, TRUE);
            break;
        }
        break;
    }

    // Register expiration event
    bRegisterDelayEvent(DEF_DELAYEVENTTYPE_MAGICRELEASE, DEF_MAGICTYPE_PROTECT,
        dwTime + (m_pMagicConfigList[sType]->m_dwLastTime * 1000),
        sOwnerH, cOwnerType, NULL, NULL, NULL,
        m_pMagicConfigList[sType]->m_sValue4, NULL, NULL);

    // Notify client
    if (cOwnerType == DEF_OWNERTYPE_PLAYER)
        SendNotifyMsg(NULL, sOwnerH, DEF_NOTIFY_MAGICEFFECTON,
            DEF_MAGICTYPE_PROTECT, m_pMagicConfigList[sType]->m_sValue4, NULL, NULL);
    break;
```

### Berserk Mode

Berserk (`DEF_MAGICTYPE_BERSERK = 18`) provides attack power boost.

**Application (Game.cpp:19384-19418):**
```cpp
case DEF_MAGICTYPE_BERSERK:
    switch (m_pMagicConfigList[sType]->m_sValue4) {
    case 1:  // Berserk mode activation
        m_pMapList[m_pClientList[iClientH]->m_cMapIndex]->GetOwner(&sOwnerH, &cOwnerType, dX, dY);

        switch (cOwnerType) {
        case DEF_OWNERTYPE_PLAYER:
            if (m_pClientList[sOwnerH] == NULL) goto MAGIC_NOEFFECT;
            // Check for existing berserk
            if (m_pClientList[sOwnerH]->m_cMagicEffectStatus[DEF_MAGICTYPE_BERSERK] != 0)
                goto MAGIC_NOEFFECT;

            // Apply berserk
            m_pClientList[sOwnerH]->m_cMagicEffectStatus[DEF_MAGICTYPE_BERSERK] =
                (char)m_pMagicConfigList[sType]->m_sValue4;
            SetBerserkFlag(sOwnerH, cOwnerType, TRUE);
            break;

        case DEF_OWNERTYPE_NPC:
            if (m_pNpcList[sOwnerH] == NULL) goto MAGIC_NOEFFECT;
            if (m_pNpcList[sOwnerH]->m_cMagicEffectStatus[DEF_MAGICTYPE_BERSERK] != 0)
                goto MAGIC_NOEFFECT;
            // NPCs with action limits cannot be berserked
            if (m_pNpcList[sOwnerH]->m_cActionLimit != 0) goto MAGIC_NOEFFECT;
            // Only same-side NPCs can be berserked
            if (m_pClientList[iClientH]->m_cSide != m_pNpcList[sOwnerH]->m_cSide)
                goto MAGIC_NOEFFECT;

            m_pNpcList[sOwnerH]->m_cMagicEffectStatus[DEF_MAGICTYPE_BERSERK] =
                (char)m_pMagicConfigList[sType]->m_sValue4;
            SetBerserkFlag(sOwnerH, cOwnerType, TRUE);
            break;
        }

        // Register expiration
        bRegisterDelayEvent(DEF_DELAYEVENTTYPE_MAGICRELEASE, DEF_MAGICTYPE_BERSERK,
            dwTime + (m_pMagicConfigList[sType]->m_dwLastTime * 1000),
            sOwnerH, cOwnerType, NULL, NULL, NULL,
            m_pMagicConfigList[sType]->m_sValue4, NULL, NULL);

        if (cOwnerType == DEF_OWNERTYPE_PLAYER)
            SendNotifyMsg(NULL, sOwnerH, DEF_NOTIFY_MAGICEFFECTON,
                DEF_MAGICTYPE_BERSERK, m_pMagicConfigList[sType]->m_sValue4, NULL, NULL);
        break;
    }
    break;
```

**Visual Flag Function (Game.cpp:43696-43715):**
```cpp
void CGame::SetBerserkFlag(short sOwnerH, char cOwnerType, BOOL bStatus)
{
    switch (cOwnerType) {
    case DEF_OWNERTYPE_PLAYER:
        if (m_pClientList[sOwnerH] == NULL) return;
        if (bStatus == TRUE)
            m_pClientList[sOwnerH]->m_iStatus = m_pClientList[sOwnerH]->m_iStatus | 0x00000020;
        else
            m_pClientList[sOwnerH]->m_iStatus = m_pClientList[sOwnerH]->m_iStatus & 0xFFFFFFDF;
        SendEventToNearClient_TypeA(sOwnerH, DEF_OWNERTYPE_PLAYER, MSGID_EVENT_MOTION,
            DEF_OBJECTNULLACTION, NULL, NULL, NULL);
        break;

    case DEF_OWNERTYPE_NPC:
        if (m_pNpcList[sOwnerH] == NULL) return;
        if (bStatus == TRUE)
            m_pNpcList[sOwnerH]->m_iStatus = m_pNpcList[sOwnerH]->m_iStatus | 0x00000020;
        else
            m_pNpcList[sOwnerH]->m_iStatus = m_pNpcList[sOwnerH]->m_iStatus & 0xFFFFFFDF;
        SendEventToNearClient_TypeA(sOwnerH, DEF_OWNERTYPE_NPC, MSGID_EVENT_MOTION,
            DEF_OBJECTNULLACTION, NULL, NULL, NULL);
        break;
    }
}
```

### Invisibility

Invisibility (`DEF_MAGICTYPE_INVISIBILITY = 13`) hides the entity from view.

**Key Properties:**
- Breaks when attacking
- Breaks when casting certain spells
- Can be detected by NPCs with "Penetrating Invisibility" ability
- Broken by Detect-Invisibility spell
- Stored in `m_cMagicEffectStatus[13]` with strength level (1 or 2)

**Breaking Invisibility on Attack (Game.cpp:51691-51692):**
```cpp
if ((m_pClientList[sAttackerH]->m_iStatus & 0x10) != 0) {
    SetInvisibilityFlag(sAttackerH, DEF_OWNERTYPE_PLAYER, FALSE);
}
```

**Breaking Invisibility on Move (Game.cpp:17005-17008):**
```cpp
if ((m_pClientList[iClientH]->m_iStatus & 0x10) != 0) {
    SetInvisibilityFlag(iClientH, DEF_OWNERTYPE_PLAYER, FALSE);
}
m_pClientList[iClientH]->m_cMagicEffectStatus[DEF_MAGICTYPE_INVISIBILITY] = NULL;
```

**Visual Flag Function (Game.cpp:43642-43661):**
```cpp
void CGame::SetInvisibilityFlag(short sOwnerH, char cOwnerType, BOOL bStatus)
{
    switch (cOwnerType) {
    case DEF_OWNERTYPE_PLAYER:
        if (m_pClientList[sOwnerH] == NULL) return;
        if (bStatus == TRUE)
            m_pClientList[sOwnerH]->m_iStatus = m_pClientList[sOwnerH]->m_iStatus | 0x00000010;
        else
            m_pClientList[sOwnerH]->m_iStatus = m_pClientList[sOwnerH]->m_iStatus & 0xFFFFFFEF;
        SendEventToNearClient_TypeA(sOwnerH, DEF_OWNERTYPE_PLAYER, MSGID_EVENT_MOTION,
            DEF_OBJECTNULLACTION, NULL, NULL, NULL);
        break;

    case DEF_OWNERTYPE_NPC:
        if (m_pNpcList[sOwnerH] == NULL) return;
        if (bStatus == TRUE)
            m_pNpcList[sOwnerH]->m_iStatus = m_pNpcList[sOwnerH]->m_iStatus | 0x00000010;
        else
            m_pNpcList[sOwnerH]->m_iStatus = m_pNpcList[sOwnerH]->m_iStatus & 0xFFFFFFEF;
        SendEventToNearClient_TypeA(sOwnerH, DEF_OWNERTYPE_NPC, MSGID_EVENT_MOTION,
            DEF_OBJECTNULLACTION, NULL, NULL, NULL);
        break;
    }
}
```

---

## Negative Effects (Debuffs)

### Poison System

Poison is the most complex status effect with dedicated tracking fields.

**Storage Fields:**
- `m_bIsPoisoned` - Boolean flag for poison state
- `m_iPoisonLevel` - Severity (damage per tick, also serves as duration counter)
- `m_dwPoisonTime` - Last tick timestamp

**Constants:**
- `DEF_POISONTIME = 12000` (12 seconds between ticks)

**Poison Sources:**
1. **Poison Spell** (`DEF_MAGICTYPE_POISON`)
2. **Poisonous NPCs** (Special ability 5 or 6)
3. **Dynamic Objects** (Poison clouds)
4. **Poison Weapons** (Special weapon effect type 61)

**Poison Spell Application (Game.cpp:19315-19382):**
```cpp
case DEF_MAGICTYPE_POISON:
    m_pMapList[m_pClientList[iClientH]->m_cMapIndex]->GetOwner(&sOwnerH, &cOwnerType, dX, dY);

    if (m_pMagicConfigList[sType]->m_sValue4 == 1) {
        // Apply poison
        switch (cOwnerType) {
        case DEF_OWNERTYPE_PLAYER:
            if (m_pClientList[sOwnerH] == NULL) goto MAGIC_NOEFFECT;
            // Check criminal action
            bAnalyzeCriminalAction(iClientH, dX, dY);

            if (bCheckResistingMagicSuccess(m_pClientList[iClientH]->m_cDir,
                    sOwnerH, cOwnerType, iResult) == FALSE) {
                // Magic resistance failed, check poison resistance
                if (bCheckResistingPoisonSuccess(sOwnerH, cOwnerType) == FALSE) {
                    // Poisoned!
                    m_pClientList[sOwnerH]->m_bIsPoisoned  = TRUE;
                    m_pClientList[sOwnerH]->m_iPoisonLevel = m_pMagicConfigList[sType]->m_sValue5;
                    m_pClientList[sOwnerH]->m_dwPoisonTime = dwTime;
                    SetPoisonFlag(sOwnerH, cOwnerType, TRUE);
                    SendNotifyMsg(NULL, sOwnerH, DEF_NOTIFY_MAGICEFFECTON,
                        DEF_MAGICTYPE_POISON, m_pMagicConfigList[sType]->m_sValue5, NULL, NULL);
                }
            }
            break;
        }
    }
    else if (m_pMagicConfigList[sType]->m_sValue4 == 0) {
        // Cure poison
        switch (cOwnerType) {
        case DEF_OWNERTYPE_PLAYER:
            if (m_pClientList[sOwnerH] == NULL) goto MAGIC_NOEFFECT;
            if (m_pClientList[sOwnerH]->m_bIsPoisoned == TRUE) {
                m_pClientList[sOwnerH]->m_bIsPoisoned = FALSE;
                SetPoisonFlag(sOwnerH, cOwnerType, FALSE);
                SendNotifyMsg(NULL, sOwnerH, DEF_NOTIFY_MAGICEFFECTOFF,
                    DEF_MAGICTYPE_POISON, NULL, NULL, NULL);
            }
            break;
        }
    }
    break;
```

**Poison Tick Processing (Game.cpp:3622-3625):**
```cpp
if ((m_pClientList[i]->m_bIsPoisoned == TRUE) &&
    ((dwTime - m_pClientList[i]->m_dwPoisonTime) > DEF_POISONTIME)) {
    PoisonEffect(i, NULL);
    m_pClientList[i]->m_dwPoisonTime = dwTime;
}
```

**Poison Effect Function (Game.cpp:32459-32488):**
```cpp
void CGame::PoisonEffect(int iClientH, int iV1)
{
    int iPoisonLevel, iDamage, iPrevHP, iProb;

    // Cannot die from poison - minimum HP is 1
    if (m_pClientList[iClientH] == NULL) return;
    if (m_pClientList[iClientH]->m_bIsKilled == TRUE) return;
    if (m_pClientList[iClientH]->m_bIsInitComplete == FALSE) return;

    iPoisonLevel = m_pClientList[iClientH]->m_iPoisonLevel;

    // Random damage based on poison level
    iDamage = iDice(1, iPoisonLevel);

    iPrevHP = m_pClientList[iClientH]->m_iHP;
    m_pClientList[iClientH]->m_iHP -= iDamage;
    if (m_pClientList[iClientH]->m_iHP <= 0)
        m_pClientList[iClientH]->m_iHP = 1;  // Cannot die from poison

    if (iPrevHP != m_pClientList[iClientH]->m_iHP)
        SendNotifyMsg(NULL, iClientH, DEF_NOTIFY_HP, NULL, NULL, NULL, NULL);

    // Chance to recover based on Poison Resistance skill
    iProb = m_pClientList[iClientH]->m_cSkillMastery[23] - 10 +
            m_pClientList[iClientH]->m_iAddPR;
    if (iProb <= 10) iProb = 10;

    if (iDice(1, 100) <= iProb) {
        m_pClientList[iClientH]->m_bIsPoisoned = FALSE;
        SetPoisonFlag(iClientH, DEF_OWNERTYPE_PLAYER, FALSE);
        SendNotifyMsg(NULL, iClientH, DEF_NOTIFY_MAGICEFFECTOFF,
            DEF_MAGICTYPE_POISON, NULL, NULL, NULL);
    }
}
```

**Poison Resistance Check (Game.cpp:32490-32508):**
```cpp
BOOL CGame::bCheckResistingPoisonSuccess(short sOwnerH, char cOwnerType)
{
    int iResist, iResult;

    switch (cOwnerType) {
    case DEF_OWNERTYPE_PLAYER:
        if (m_pClientList[sOwnerH] == NULL) return FALSE;
        iResist = m_pClientList[sOwnerH]->m_cSkillMastery[23] +
                  m_pClientList[sOwnerH]->m_iAddPR;
        break;

    case DEF_OWNERTYPE_NPC:
        if (m_pNpcList[sOwnerH] == NULL) return FALSE;
        iResist = 0;  // NPCs have no poison resistance
        break;
    }

    iResult = iDice(1, 100);
    if (iResult >= iResist) // Poison resistance failed
        return FALSE;
    return TRUE;
}
```

**Poison Levels from Weapons (Game.cpp:52535-52544):**
```cpp
// Special ability 5 = Poisonous (level 15)
// Special ability 6 = Extremely Poisonous (level 40)
// Special ability 61 = Custom poison (value-based)
if ((m_pClientList[sTargetH]->m_bIsPoisoned == FALSE) &&
    ((cAttackerSA == 5) || (cAttackerSA == 6) || (cAttackerSA == 61))) {
    m_pClientList[sTargetH]->m_bIsPoisoned = TRUE;
    if (cAttackerSA == 5)      m_pClientList[sTargetH]->m_iPoisonLevel = 15;
    else if (cAttackerSA == 6) m_pClientList[sTargetH]->m_iPoisonLevel = 40;
    else if (cAttackerSA == 61) m_pClientList[sTargetH]->m_iPoisonLevel = iAttackerSAvalue;

    m_pClientList[sTargetH]->m_dwPoisonTime = dwTime;
    SendNotifyMsg(NULL, sTargetH, DEF_NOTIFY_MAGICEFFECTON,
        DEF_MAGICTYPE_POISON, m_pClientList[sTargetH]->m_iPoisonLevel, NULL, NULL);
}
```

### Ice/Freeze Effect

Ice/Freeze (`DEF_MAGICTYPE_ICE = 23`) restricts target movement.

**Visual Flag Function (Game.cpp:43723-43742):**
```cpp
void CGame::SetIceFlag(short sOwnerH, char cOwnerType, BOOL bStatus)
{
    switch (cOwnerType) {
    case DEF_OWNERTYPE_PLAYER:
        if (m_pClientList[sOwnerH] == NULL) return;
        if (bStatus == TRUE)
            m_pClientList[sOwnerH]->m_iStatus = m_pClientList[sOwnerH]->m_iStatus | 0x00000040;
        else
            m_pClientList[sOwnerH]->m_iStatus = m_pClientList[sOwnerH]->m_iStatus & 0xFFFFFFBF;
        SendEventToNearClient_TypeA(sOwnerH, DEF_OWNERTYPE_PLAYER, MSGID_EVENT_MOTION,
            DEF_OBJECTNULLACTION, NULL, NULL, NULL);
        break;

    case DEF_OWNERTYPE_NPC:
        if (m_pNpcList[sOwnerH] == NULL) return;
        if (bStatus == TRUE)
            m_pNpcList[sOwnerH]->m_iStatus = m_pNpcList[sOwnerH]->m_iStatus | 0x00000040;
        else
            m_pNpcList[sOwnerH]->m_iStatus = m_pNpcList[sOwnerH]->m_iStatus & 0xFFFFFFBF;
        SendEventToNearClient_TypeA(sOwnerH, DEF_OWNERTYPE_NPC, MSGID_EVENT_MOTION,
            DEF_OBJECTNULLACTION, NULL, NULL, NULL);
        break;
    }
}
```

**Ice Application (Example from Game.cpp:17602-17619):**
```cpp
if (m_pClientList[sOwnerH]->m_cMagicEffectStatus[DEF_MAGICTYPE_ICE] == 0) {
    m_pClientList[sOwnerH]->m_cMagicEffectStatus[DEF_MAGICTYPE_ICE] = 1;
    bRegisterDelayEvent(DEF_DELAYEVENTTYPE_MAGICRELEASE, DEF_MAGICTYPE_ICE,
        dwTime + (m_pMagicConfigList[sType]->m_dwLastTime * 1000),
        sOwnerH, cOwnerType, NULL, NULL, NULL,
        m_pMagicConfigList[sType]->m_sValue4, NULL, NULL);
    SetIceFlag(sOwnerH, cOwnerType, TRUE);
}
```

**NPC Ice Movement Check (Game.cpp:8724):**
```cpp
if (m_pNpcList[i]->m_cMagicEffectStatus[DEF_MAGICTYPE_ICE] != 0)
    continue;  // Skip movement for frozen NPCs
```

### Hold Object (Paralysis)

Hold Object (`DEF_MAGICTYPE_HOLDOBJECT = 12`) completely prevents movement and actions.

**NPC Movement Check (Game.cpp:9990, 10220):**
```cpp
if (m_pNpcList[iNpcH]->m_cMagicEffectStatus[DEF_MAGICTYPE_HOLDOBJECT] != 0)
    return;  // Cannot move when held
```

**Player Movement Check (Game.cpp:1198):**
```cpp
if (m_pClientList[iClientH]->m_cMagicEffectStatus[DEF_MAGICTYPE_HOLDOBJECT] != 0)
    // Movement blocked
```

### Inhibition Casting (Silence)

Inhibition (`DEF_MAGICTYPE_INHIBITION = 31`) prevents spell casting.

**Application (Game.cpp:18153-18168):**
```cpp
case DEF_MAGICTYPE_INHIBITION:
    m_pMapList[m_pClientList[iClientH]->m_cMapIndex]->GetOwner(&sOwnerH, &cOwnerType, dX, dY);
    switch (cOwnerType) {
    case DEF_OWNERTYPE_PLAYER:
        if (m_pClientList[sOwnerH] == NULL) goto MAGIC_NOEFFECT;
        // Check for existing inhibition
        if (m_pClientList[sOwnerH]->m_cMagicEffectStatus[DEF_MAGICTYPE_INHIBITION] != 0)
            goto MAGIC_NOEFFECT;
        // Cannot silence if has absolute magic protection
        if (m_pClientList[sOwnerH]->m_cMagicEffectStatus[DEF_MAGICTYPE_PROTECT] == 5)
            goto MAGIC_NOEFFECT;
        // Cannot silence same side
        if (m_pClientList[iClientH]->m_cSide == m_pClientList[sOwnerH]->m_cSide)
            goto MAGIC_NOEFFECT;

        m_pClientList[sOwnerH]->m_bInhibition = TRUE;
        bRegisterDelayEvent(DEF_DELAYEVENTTYPE_MAGICRELEASE, DEF_MAGICTYPE_INHIBITION,
            dwTime + (m_pMagicConfigList[sType]->m_dwLastTime * 1000),
            sOwnerH, cOwnerType, NULL, NULL, NULL,
            m_pMagicConfigList[sType]->m_sValue4, NULL, NULL);
        break;
    }
    break;
```

**Casting Check (Game.cpp:17118):**
```cpp
if (m_pClientList[iClientH]->m_bInhibition == TRUE) {
    // Cannot cast spells while silenced
    return;
}
```

**Visual Flag Function (Game.cpp:43669-43688):**
```cpp
void CGame::SetInhibitionCastingFlag(short sOwnerH, char cOwnerType, BOOL bStatus)
{
    switch (cOwnerType) {
    case DEF_OWNERTYPE_PLAYER:
        if (m_pClientList[sOwnerH] == NULL) return;
        if (bStatus == TRUE)
            m_pClientList[sOwnerH]->m_iStatus = m_pClientList[sOwnerH]->m_iStatus | 0x00100000;
        else
            m_pClientList[sOwnerH]->m_iStatus = m_pClientList[sOwnerH]->m_iStatus & 0xFFEFFFFF;
        SendEventToNearClient_TypeA(sOwnerH, DEF_OWNERTYPE_PLAYER, MSGID_EVENT_MOTION,
            DEF_OBJECTNULLACTION, NULL, NULL, NULL);
        break;

    case DEF_OWNERTYPE_NPC:
        if (m_pNpcList[sOwnerH] == NULL) return;
        if (bStatus == TRUE)
            m_pNpcList[sOwnerH]->m_iStatus = m_pNpcList[sOwnerH]->m_iStatus | 0x00100000;
        else
            m_pNpcList[sOwnerH]->m_iStatus = m_pNpcList[sOwnerH]->m_iStatus & 0xFFEFFFFF;
        SendEventToNearClient_TypeA(sOwnerH, DEF_OWNERTYPE_NPC, MSGID_EVENT_MOTION,
            DEF_OBJECTNULLACTION, NULL, NULL, NULL);
        break;
    }
}
```

### Confusion/Illusion

Confusion (`DEF_MAGICTYPE_CONFUSE = 16`) has multiple subtypes:

| Subtype | Name | Effect |
|---------|------|--------|
| 1 | Confuse Language | Garbles chat messages |
| 2 | Confusion/Mass Confusion | Random movement direction |
| 3 | Illusion/Mass Illusion | Visual flag 0x01000000 |
| 4 | Illusion Movement | Visual flag 0x00200000 |

**Random Movement Effect (Game.cpp:9475):**
```cpp
if ((m_pClientList[iClientH]->m_cMagicEffectStatus[DEF_MAGICTYPE_CONFUSE] == 1) &&
    (iDice(1, 3) != 2)) {
    // 66% chance to move in random direction when confused
}
```

---

## Transformation Effects

### Polymorph System

Polymorph (`DEF_MAGICTYPE_POLYMORPH = 20`) changes the visual appearance.

**Application (Game.cpp:17352-17368):**
```cpp
case DEF_MAGICTYPE_POLYMORPH:
    switch (cOwnerType) {
    case DEF_OWNERTYPE_PLAYER:
        if (m_pClientList[sOwnerH] == NULL) goto MAGIC_NOEFFECT;
        // Cannot polymorph if already polymorphed
        if (m_pClientList[sOwnerH]->m_cMagicEffectStatus[DEF_MAGICTYPE_POLYMORPH] != 0)
            goto MAGIC_NOEFFECT;

        m_pClientList[sOwnerH]->m_cMagicEffectStatus[DEF_MAGICTYPE_POLYMORPH] =
            (char)m_pMagicConfigList[sType]->m_sValue4;
        // Save original type
        m_pClientList[sOwnerH]->m_sOriginalType = m_pClientList[sOwnerH]->m_sType;
        // Change to new type
        m_pClientList[sOwnerH]->m_sType = m_pMagicConfigList[sType]->m_sValue5;
        break;

    case DEF_OWNERTYPE_NPC:
        if (m_pNpcList[sOwnerH] == NULL) goto MAGIC_NOEFFECT;
        if (m_pNpcList[sOwnerH]->m_cMagicEffectStatus[DEF_MAGICTYPE_POLYMORPH] != 0)
            goto MAGIC_NOEFFECT;

        m_pNpcList[sOwnerH]->m_cMagicEffectStatus[DEF_MAGICTYPE_POLYMORPH] =
            (char)m_pMagicConfigList[sType]->m_sValue4;
        m_pNpcList[sOwnerH]->m_sOriginalType = m_pNpcList[sOwnerH]->m_sType;
        m_pNpcList[sOwnerH]->m_sType = m_pMagicConfigList[sType]->m_sValue5;
        break;
    }
    break;
```

**Polymorph Expiration (Game.cpp:29701-29728):**
```cpp
// Polymorph effect expiration - restore original type
if (m_pDelayEventList[i]->m_iEffectType == DEF_MAGICTYPE_POLYMORPH) {
    m_pClientList[m_pDelayEventList[i]->m_iTargetH]->m_sType =
        m_pClientList[m_pDelayEventList[i]->m_iTargetH]->m_sOriginalType;
    SendEventToNearClient_TypeA(m_pDelayEventList[i]->m_iTargetH, DEF_OWNERTYPE_PLAYER,
        MSGID_EVENT_MOTION, DEF_OBJECTNULLACTION, NULL, NULL, NULL);
}
```

---

## Environmental Effects

### Hunger System

Hunger decreases over time and affects regeneration rates.

**Constants:**
- `DEF_HUNGERTIME = 60000` (60 seconds between hunger ticks)
- Initial value: 100 (full)
- Critical threshold: 30 (warnings start)

**Storage:**
- `m_iHungerStatus` - Current hunger level (0-100)
- `m_dwHungerTime` - Last hunger update time

**Hunger Processing (Game.cpp:3580-3600):**
```cpp
if (((dwTime - m_pClientList[i]->m_dwHungerTime) > DEF_HUNGERTIME) &&
    (m_pClientList[i]->m_bIsKilled == FALSE)) {

    // High level players (above DEF_LEVELLIMIT) or admins don't lose hunger
    if ((m_pClientList[i]->m_iLevel < DEF_LEVELLIMIT) ||
        (m_pClientList[i]->m_iAdminUserLevel >= 1)) {
        // No hunger loss
    }
    else {
        m_pClientList[i]->m_iHungerStatus--;
    }

    if (m_pClientList[i]->m_iHungerStatus <= 0)
        m_pClientList[i]->m_iHungerStatus = 0;

    m_pClientList[i]->m_dwHungerTime = dwTime;

    // Warn player when hungry
    if ((m_pClientList[i]->m_iHP > 0) && (m_pClientList[i]->m_iHungerStatus < 30)) {
        SendNotifyMsg(NULL, i, DEF_NOTIFY_HUNGER,
            m_pClientList[i]->m_iHungerStatus, NULL, NULL, NULL);
    }
}
```

**Hunger Affects Regeneration (Game.cpp:3598-3620):**
```cpp
// Calculate regeneration delay penalty
if ((m_pClientList[i]->m_iHungerStatus <= 30) &&
    (m_pClientList[i]->m_iHungerStatus >= 0))
    iPlusTime = (30 - m_pClientList[i]->m_iHungerStatus) * 1000;
else
    iPlusTime = 0;

// HP regeneration with hunger penalty
if ((dwTime - m_pClientList[i]->m_dwHPTime) > (DWORD)(DEF_HPUPTIME + iPlusTime)) {
    TimeHitPointsUp(i);
    m_pClientList[i]->m_dwHPTime = dwTime;
}

// MP regeneration with hunger penalty
if ((dwTime - m_pClientList[i]->m_dwMPTime) > (DWORD)(DEF_MPUPTIME + iPlusTime)) {
    TimeManaPointsUp(i);
    m_pClientList[i]->m_dwMPTime = dwTime;
}

// SP regeneration with hunger penalty
if ((dwTime - m_pClientList[i]->m_dwSPTime) > (DWORD)(DEF_SPUPTIME + iPlusTime)) {
    TimeStaminarPointsUp(i);
    m_pClientList[i]->m_dwSPTime = dwTime;
}
```

**Hunger Attack Penalty (Game.cpp:17217):**
```cpp
if (((m_pClientList[iClientH]->m_iHungerStatus <= 10) ||
     (m_pClientList[iClientH]->m_iSP <= 0)) && (iDice(1, 1000) <= 100)) {
    // 10% chance to fail attack when starving or exhausted
}
```

### Time-Based Modifiers

Several time-limited status effects use countdown timers:

| Field | Purpose | Decrement Rate |
|-------|---------|---------------|
| `m_iTimeLeft_ShutUp` | Chat muted | Every tick |
| `m_iTimeLeft_Rating` | Rating cooldown | Every tick |
| `m_iTimeLeft_ForceRecall` | Force teleport timer | War zones |
| `m_iTimeLeft_FirmStaminar` | Stamina protection | Every tick |

**Time Counter Processing (Game.cpp:3574-3578, 3869-3870):**
```cpp
// Chat mute countdown
m_pClientList[i]->m_iTimeLeft_ShutUp--;
if (m_pClientList[i]->m_iTimeLeft_ShutUp < 0)
    m_pClientList[i]->m_iTimeLeft_ShutUp = 0;

// Rating cooldown
m_pClientList[i]->m_iTimeLeft_Rating--;
if (m_pClientList[i]->m_iTimeLeft_Rating < 0)
    m_pClientList[i]->m_iTimeLeft_Rating = 0;

// Firm stamina countdown
m_pClientList[i]->m_iTimeLeft_FirmStaminar--;
if (m_pClientList[i]->m_iTimeLeft_FirmStaminar < 0)
    m_pClientList[i]->m_iTimeLeft_FirmStaminar = 0;
```

---

## Effect Processing

### Tick Processing

Main game loop processes status effects approximately every 3 seconds.

**Main Processing Loop Location:** Game.cpp:3574-3700

**Key Processing Steps:**
1. Decrement time-based counters
2. Process hunger decay
3. Process regeneration (HP/MP/SP) with hunger penalty
4. Process poison damage
5. Process special ability timers
6. Process crusade timers
7. Process death penalty timer

### DelayEvent System

The DelayEvent system handles scheduled effect expirations.

**Registering Effect Expiration:**
```cpp
bRegisterDelayEvent(
    DEF_DELAYEVENTTYPE_MAGICRELEASE,  // Event type
    DEF_MAGICTYPE_BERSERK,            // Effect being released
    dwTime + (duration * 1000),        // Trigger time (milliseconds)
    sOwnerH,                           // Target handle
    cOwnerType,                        // Player or NPC
    NULL, NULL, NULL,                  // Position (unused for magic release)
    m_sValue4,                         // Effect subtype
    NULL, NULL                         // Additional params
);
```

**DelayEvent Processing (Game.cpp:29591-29765):**
```cpp
void CGame::DelayEventProcessor()
{
    for (i = 0; i < DEF_MAXDELAYEVENTS; i++) {
        if (m_pDelayEventList[i] == NULL) continue;

        if (m_pDelayEventList[i]->m_dwTriggerTime < dwTime) {
            switch (m_pDelayEventList[i]->m_iDelayType) {
            case DEF_DELAYEVENTTYPE_MAGICRELEASE:
                switch (m_pDelayEventList[i]->m_cTargetType) {
                case DEF_OWNERTYPE_PLAYER:
                    if (m_pClientList[m_pDelayEventList[i]->m_iTargetH] == NULL) break;

                    // Notify client of effect removal
                    SendNotifyMsg(NULL, m_pDelayEventList[i]->m_iTargetH,
                        DEF_NOTIFY_MAGICEFFECTOFF,
                        m_pDelayEventList[i]->m_iEffectType,
                        m_pClientList[m_pDelayEventList[i]->m_iTargetH]->
                            m_cMagicEffectStatus[m_pDelayEventList[i]->m_iEffectType],
                        NULL, NULL);

                    // Clear effect status
                    m_pClientList[m_pDelayEventList[i]->m_iTargetH]->
                        m_cMagicEffectStatus[m_pDelayEventList[i]->m_iEffectType] = NULL;

                    // Handle specific effect cleanup
                    if (m_pDelayEventList[i]->m_iEffectType == DEF_MAGICTYPE_INHIBITION)
                        m_pClientList[m_pDelayEventList[i]->m_iTargetH]->m_bInhibition = FALSE;

                    if (m_pDelayEventList[i]->m_iEffectType == DEF_MAGICTYPE_INVISIBILITY)
                        SetInvisibilityFlag(m_pDelayEventList[i]->m_iTargetH,
                            DEF_OWNERTYPE_PLAYER, FALSE);

                    // ... additional effect-specific cleanup
                    break;
                }
                break;
            }

            delete m_pDelayEventList[i];
            m_pDelayEventList[i] = NULL;
        }
    }
}
```

### Effect Removal

**Manual Removal Function (Game.cpp:29766-29791):**
```cpp
BOOL CGame::bRemoveFromDelayEventList(int iH, char cType, int iEffectType)
{
    for (i = 0; i < DEF_MAXDELAYEVENTS; i++) {
        if (m_pDelayEventList[i] != NULL) {
            if (iEffectType == NULL) {
                // Remove all effects for target
                if ((m_pDelayEventList[i]->m_iTargetH == iH) &&
                    (m_pDelayEventList[i]->m_cTargetType == cType)) {
                    delete m_pDelayEventList[i];
                    m_pDelayEventList[i] = NULL;
                }
            }
            else {
                // Remove specific effect
                if ((m_pDelayEventList[i]->m_iTargetH == iH) &&
                    (m_pDelayEventList[i]->m_cTargetType == cType) &&
                    (m_pDelayEventList[i]->m_iEffectType == iEffectType)) {
                    delete m_pDelayEventList[i];
                    m_pDelayEventList[i] = NULL;
                }
            }
        }
    }
    return TRUE;
}
```

---

## Status Flag Functions

Complete list of visual status flag manipulation functions:

| Function | Flag | Set Mask | Clear Mask |
|----------|------|----------|------------|
| `SetInvisibilityFlag()` | Invisibility | `0x00000010` | `0xFFFFFFEF` |
| `SetBerserkFlag()` | Berserk | `0x00000020` | `0xFFFFFFDF` |
| `SetIceFlag()` | Frozen | `0x00000040` | `0xFFFFFFBF` |
| `SetPoisonFlag()` | Poisoned | `0x00000080` | `0xFFFFFF7F` |
| `SetHeroFlag()` | Hero Aura | `0x00020000` | `0xFFFDFFFF` |
| `SetInhibitionCastingFlag()` | Silence | `0x00100000` | `0xFFEFFFFF` |
| `SetIllusionMovementFlag()` | Illusion Move | `0x00200000` | `0xFFDFFFFF` |
| `SetIllusionFlag()` | Illusion | `0x01000000` | `0xFEFFFFFF` |
| `SetDefenseShieldFlag()` | Defense Shield | `0x02000000` | `0xFDFFFFFF` |
| `SetMagicProtectionFlag()` | Magic Protection | `0x04000000` | `0xFBFFFFFF` |
| `SetProtectionFromArrowFlag()` | Arrow Protection | `0x08000000` | `0xF7FFFFFF` |

**Common Pattern (Game.cpp:43750-43769):**
```cpp
void CGame::SetPoisonFlag(short sOwnerH, char cOwnerType, BOOL bStatus)
{
    switch (cOwnerType) {
    case DEF_OWNERTYPE_PLAYER:
        if (m_pClientList[sOwnerH] == NULL) return;
        if (bStatus == TRUE)
            m_pClientList[sOwnerH]->m_iStatus = m_pClientList[sOwnerH]->m_iStatus | 0x00000080;
        else
            m_pClientList[sOwnerH]->m_iStatus = m_pClientList[sOwnerH]->m_iStatus & 0xFFFFFF7F;
        SendEventToNearClient_TypeA(sOwnerH, DEF_OWNERTYPE_PLAYER, MSGID_EVENT_MOTION,
            DEF_OBJECTNULLACTION, NULL, NULL, NULL);
        break;

    case DEF_OWNERTYPE_NPC:
        if (m_pNpcList[sOwnerH] == NULL) return;
        if (bStatus == TRUE)
            m_pNpcList[sOwnerH]->m_iStatus = m_pNpcList[sOwnerH]->m_iStatus | 0x00000080;
        else
            m_pNpcList[sOwnerH]->m_iStatus = m_pNpcList[sOwnerH]->m_iStatus & 0xFFFFFF7F;
        SendEventToNearClient_TypeA(sOwnerH, DEF_OWNERTYPE_NPC, MSGID_EVENT_MOTION,
            DEF_OBJECTNULLACTION, NULL, NULL, NULL);
        break;
    }
}
```

---

## Cancellation System

The Cancellation spell (`DEF_MAGICTYPE_CANCELLATION = 29`) removes all buffs from a target.

**Implementation (Game.cpp:17383-17447):**
```cpp
case DEF_MAGICTYPE_CANCELLATION:
    m_pMapList[m_pClientList[iClientH]->m_cMapIndex]->GetOwner(&sOwnerH, &cOwnerType, dX, dY);

    if ((cOwnerType == DEF_OWNERTYPE_PLAYER) &&
        (m_pClientList[sOwnerH] != NULL) &&
        (m_pClientList[sOwnerH]->m_iHP > 0) &&
        (m_pClientList[sOwnerH]->m_iAdminUserLevel == 0)) {  // Cannot cancel admins

        // Remove all visual flags
        SetInvisibilityFlag(sOwnerH, cOwnerType, FALSE);
        SetIllusionFlag(sOwnerH, cOwnerType, FALSE);
        SetDefenseShieldFlag(sOwnerH, cOwnerType, FALSE);
        SetMagicProtectionFlag(sOwnerH, cOwnerType, FALSE);
        SetProtectionFromArrowFlag(sOwnerH, cOwnerType, FALSE);
        SetIllusionMovementFlag(sOwnerH, cOwnerType, FALSE);
        SetBerserkFlag(sOwnerH, cOwnerType, FALSE);
        SetIceFlag(sOwnerH, cOwnerType, FALSE);

        // Remove all delayed effect expirations and register immediate ones
        bRemoveFromDelayEventList(sOwnerH, DEF_OWNERTYPE_PLAYER, DEF_MAGICTYPE_ICE);
        bRegisterDelayEvent(DEF_DELAYEVENTTYPE_MAGICRELEASE, DEF_MAGICTYPE_ICE,
            dwTime + (m_pMagicConfigList[sType]->m_dwLastTime),
            sOwnerH, cOwnerType, NULL, NULL, NULL,
            m_pMagicConfigList[sType]->m_sValue4, NULL, NULL);

        bRemoveFromDelayEventList(sOwnerH, DEF_OWNERTYPE_PLAYER, DEF_MAGICTYPE_HOLDOBJECT);
        bRegisterDelayEvent(DEF_DELAYEVENTTYPE_MAGICRELEASE, DEF_MAGICTYPE_HOLDOBJECT,
            dwTime + (m_pMagicConfigList[sType]->m_dwLastTime),
            sOwnerH, cOwnerType, NULL, NULL, NULL,
            m_pMagicConfigList[sType]->m_sValue4, NULL, NULL);

        bRemoveFromDelayEventList(sOwnerH, DEF_OWNERTYPE_PLAYER, DEF_MAGICTYPE_INHIBITION);
        bRegisterDelayEvent(DEF_DELAYEVENTTYPE_MAGICRELEASE, DEF_MAGICTYPE_INHIBITION,
            dwTime + (m_pMagicConfigList[sType]->m_dwLastTime),
            sOwnerH, cOwnerType, NULL, NULL, NULL,
            m_pMagicConfigList[sType]->m_sValue4, NULL, NULL);

        bRemoveFromDelayEventList(sOwnerH, DEF_OWNERTYPE_PLAYER, DEF_MAGICTYPE_INVISIBILITY);
        bRegisterDelayEvent(DEF_DELAYEVENTTYPE_MAGICRELEASE, DEF_MAGICTYPE_INVISIBILITY,
            dwTime + (m_pMagicConfigList[sType]->m_dwLastTime),
            sOwnerH, cOwnerType, NULL, NULL, NULL,
            m_pMagicConfigList[sType]->m_sValue4, NULL, NULL);

        bRemoveFromDelayEventList(sOwnerH, DEF_OWNERTYPE_PLAYER, DEF_MAGICTYPE_BERSERK);
        bRegisterDelayEvent(DEF_DELAYEVENTTYPE_MAGICRELEASE, DEF_MAGICTYPE_BERSERK,
            dwTime + (m_pMagicConfigList[sType]->m_dwLastTime),
            sOwnerH, cOwnerType, NULL, NULL, NULL,
            m_pMagicConfigList[sType]->m_sValue4, NULL, NULL);

        bRemoveFromDelayEventList(sOwnerH, DEF_OWNERTYPE_PLAYER, DEF_MAGICTYPE_PROTECT);
        bRegisterDelayEvent(DEF_DELAYEVENTTYPE_MAGICRELEASE, DEF_MAGICTYPE_PROTECT,
            dwTime + (m_pMagicConfigList[sType]->m_dwLastTime),
            sOwnerH, cOwnerType, NULL, NULL, NULL,
            m_pMagicConfigList[sType]->m_sValue4, NULL, NULL);

        bRemoveFromDelayEventList(sOwnerH, DEF_OWNERTYPE_PLAYER, DEF_MAGICTYPE_CONFUSE);
        bRegisterDelayEvent(DEF_DELAYEVENTTYPE_MAGICRELEASE, DEF_MAGICTYPE_CONFUSE,
            dwTime + (m_pMagicConfigList[sType]->m_dwLastTime),
            sOwnerH, cOwnerType, NULL, NULL, NULL,
            m_pMagicConfigList[sType]->m_sValue4, NULL, NULL);

        // Update client display
        SendEventToNearClient_TypeA(sOwnerH, DEF_OWNERTYPE_PLAYER, MSGID_EVENT_MOTION,
            DEF_OBJECTNULLACTION, NULL, NULL, NULL);
    }
    break;
```

---

## Constants Reference

### Timing Constants (Game.h)

```cpp
#define DEF_SPUPTIME      10000   // Stamina regeneration interval (10 seconds)
#define DEF_POISONTIME    12000   // Poison tick interval (12 seconds)
#define DEF_HPUPTIME      15000   // HP regeneration interval (15 seconds)
#define DEF_MPUPTIME      20000   // MP regeneration interval (20 seconds)
#define DEF_HUNGERTIME    60000   // Hunger decay interval (60 seconds)
```

### System Limits (Game.h, Magic.h)

```cpp
#define DEF_MAXMAGICEFFECTS   100     // Maximum concurrent effect slots
#define DEF_MAXDELAYEVENTS    60000   // Maximum scheduled events
```

### Owner Type Constants

```cpp
#define DEF_OWNERTYPE_PLAYER   1
#define DEF_OWNERTYPE_NPC      2
```

### Special Status Values

```cpp
// Angel/Demon/Experience boost (from Game.cpp:45109-45134)
#define STATUS_ANGEL       0x400000   // Angel transformation
#define STATUS_DEMON       0x800000   // Demon transformation
#define STATUS_EXP_BOOST   0x10000    // 3x experience bonus
```

---

## Related Functions

### Status Effect Functions in Game.h

```cpp
// Line 353-354
void SetStatusFlag(short sOwnerH, char cOwnerType, BOOL bStatus, int iPass);
void SetPoisonFlag(short sOwnerH, char cOwnerType, BOOL bStatus);

// Line 417
void SetIceFlag(short sOwnerH, char cOwnerType, BOOL bStatus);

// Line 487
void SetBerserkFlag(short sOwnerH, char cOwnerType, BOOL bStatus);

// Line 540
void SetInvisibilityFlag(short sOwnerH, char cOwnerType, BOOL bStatus);

// Line 233
void SetInhibitionCastingFlag(short sOwnerH, char cOwnerType, BOOL bStatus);

// Line 517
void PoisonEffect(int iClientH, int iV1);

// Line 516
BOOL bCheckResistingPoisonSuccess(short sOwnerH, char cOwnerType);

// Line 541-543
BOOL bRemoveFromDelayEventList(int iH, char cType, int iEffectType);
void DelayEventProcessor();
BOOL bRegisterDelayEvent(int iDelayType, int iEffectType, DWORD dwLastTime,
    int iTargetH, char cTargetType, char cMapIndex, int dX, int dY,
    int iV1, int iV2, int iV3);
```

### Notification Messages

```cpp
// NetMessages.h
#define DEF_NOTIFY_MAGICEFFECTON   0x0B27  // Effect applied
#define DEF_NOTIFY_MAGICEFFECTOFF  0x0B28  // Effect removed
#define DEF_NOTIFY_HUNGER          0x0B39  // Hunger status update
#define DEF_NOTIFY_HP              0x0B01  // HP changed (from poison)
```

---

## Summary

The Helbreath status effects system is a dual-storage system using:

1. **`m_cMagicEffectStatus[100]`** array for tracking active magic effects by type index
2. **`m_iStatus`** bit flags for visual client-side rendering

Key characteristics:
- **No Stacking**: Effects generally do not stack - if an effect already exists, new applications fail
- **Timed Expiration**: Most effects use the DelayEvent system for automatic removal
- **Poison is Special**: Uses dedicated fields (`m_bIsPoisoned`, `m_iPoisonLevel`, `m_dwPoisonTime`) separate from the magic effect array
- **Hunger Affects Regeneration**: Low hunger adds delay to HP/MP/SP regeneration ticks
- **Cancellation Dispels All**: The Cancellation spell removes all buff effects simultaneously
- **Admin Immunity**: Admins (`m_iAdminUserLevel > 0`) are immune to many negative effects

The maximum capacity of 100 concurrent effect slots (`DEF_MAXMAGICEFFECTS`) allows for extensive effect combinations, though in practice most characters have only a handful of active effects at any time.
