# Legacy Delayed Events System

**Document Version:** 1.0
**Applies To:** Legacy Helbreath Server (v2.03)
**Primary Files:** `DelayEvent.cpp/h`, `Game.cpp`
**Estimated Lines:** ~500 lines across all files

---

## Table of Contents

1. [Overview](#overview)
2. [Data Structures](#data-structures)
3. [Event Types](#event-types)
4. [Core Functions](#core-functions)
5. [Magic Release Events](#magic-release-events)
6. [Skill Item Events](#skill-item-events)
7. [Meteor Strike Sequence](#meteor-strike-sequence)
8. [Ancient Tablet Events](#ancient-tablet-events)
9. [Event Processing Loop](#event-processing-loop)
10. [Parameter Reference](#parameter-reference)
11. [Constants and Limits](#constants-and-limits)
12. [Related Systems](#related-systems)
13. [Implementation Notes](#implementation-notes)

---

## Overview

The Delayed Events system provides a mechanism for scheduling game events to execute after a specified delay. This is essential for:

1. **Magic Effect Durations** - Buffs and debuffs that expire after a set time
2. **Skill Usage Timing** - Item-based skills that take time to complete
3. **Multi-Stage Events** - Complex sequences like Meteor Strike that unfold over several seconds
4. **Special Item Effects** - Ancient Tablet buffs with timed durations

The system maintains an array of up to 60,000 pending events, each with a trigger timestamp. Every second, the processor checks for expired events and executes their associated logic.

---

## Data Structures

### CDelayEvent

**File:** `DelayEvent.h`

The core class representing a single delayed event.

```cpp
class CDelayEvent
{
public:
    CDelayEvent();
    virtual ~CDelayEvent();

    int m_iDelayType;           // Event type (1-7)
    int m_iEffectType;          // Effect subtype (context-dependent)

    char m_cMapIndex;           // Map where event occurs
    int m_dX, m_dY;             // Coordinates (if applicable)

    int  m_iTargetH;            // Target handle (player/NPC index)
    char m_cTargetType;         // Target type (DEF_OWNERTYPE_PLAYER or DEF_OWNERTYPE_NPC)

    int m_iV1, m_iV2, m_iV3;    // Generic parameters (meaning varies by event type)

    DWORD m_dwTriggerTime;      // Absolute time when event should fire (milliseconds)
};
```

### Event Storage in CGame

**File:** `Game.h`

```cpp
class CGame {
    // Array of delay event pointers (NULL = empty slot)
    class CDelayEvent * m_pDelayEventList[DEF_MAXDELAYEVENTS];  // 60,000 slots

    // Timer for processing
    DWORD m_dwGameTime6;  // Last process time (events processed every 1 second)
};
```

---

## Event Types

### Type Definitions

**File:** `DelayEvent.h`

```cpp
#define DEF_DELAYEVENTTYPE_DAMAGEOBJECT             1  // Damage object (intentionally empty)
#define DEF_DELAYEVENTTYPE_MAGICRELEASE             2  // Magic effect expiration
#define DEF_DELAYEVENTTYPE_USEITEM_SKILL            3  // Item skill completion
#define DEF_DELAYEVENTTYPE_METEORSTRIKE             4  // Start meteor strike
#define DEF_DELAYEVENTTYPE_DOMETEORSTRIKEDAMAGE     5  // Apply meteor damage
#define DEF_DELAYEVENTTYPE_CALCMETEORSTRIKEEFFECT   6  // Calculate meteor results
#define DEF_DELAYEVENTTYPE_ANCIENT_TABLET           7  // Ancient tablet effect expiry
```

### Type Summary

| Type | Name | Purpose | Duration |
|------|------|---------|----------|
| 1 | DAMAGEOBJECT | Reserved for damage objects (no-op) | N/A |
| 2 | MAGICRELEASE | Remove magic buff/debuff | Varies by spell |
| 3 | USEITEM_SKILL | Execute item skill effect | Skill-dependent |
| 4 | METEORSTRIKE | Initiate meteor strike sequence | 5 seconds |
| 5 | DOMETEORSTRIKEDAMAGE | Apply damage to players | 1s, 4s after start |
| 6 | CALCMETEORSTRIKEEFFECT | Finalize meteor strike | 6 seconds after start |
| 7 | ANCIENT_TABLET | Remove slate buff | 600 seconds (10 min) |

---

## Core Functions

### bRegisterDelayEvent

**File:** `Game.cpp` (Line ~29562)
**Signature:**
```cpp
BOOL CGame::bRegisterDelayEvent(
    int iDelayType,      // Event type (1-7)
    int iEffectType,     // Effect subtype
    DWORD dwLastTime,    // Absolute trigger time (use timeGetTime() + delay)
    int iTargetH,        // Target handle
    char cTargetType,    // DEF_OWNERTYPE_PLAYER or DEF_OWNERTYPE_NPC
    char cMapIndex,      // Map index
    int dX, int dY,      // Coordinates
    int iV1, int iV2, int iV3  // Context-specific parameters
);
```

**Returns:** TRUE if registered successfully, FALSE if no slots available

**Implementation:**
```cpp
BOOL CGame::bRegisterDelayEvent(int iDelayType, int iEffectType, DWORD dwLastTime,
    int iTargetH, char cTargetType, char cMapIndex, int dX, int dY,
    int iV1, int iV2, int iV3)
{
    for (int i = 0; i < DEF_MAXDELAYEVENTS; i++) {
        if (m_pDelayEventList[i] == NULL) {
            m_pDelayEventList[i] = new CDelayEvent;
            m_pDelayEventList[i]->m_iDelayType    = iDelayType;
            m_pDelayEventList[i]->m_iEffectType   = iEffectType;
            m_pDelayEventList[i]->m_cMapIndex     = cMapIndex;
            m_pDelayEventList[i]->m_dX            = dX;
            m_pDelayEventList[i]->m_dY            = dY;
            m_pDelayEventList[i]->m_iTargetH      = iTargetH;
            m_pDelayEventList[i]->m_cTargetType   = cTargetType;
            m_pDelayEventList[i]->m_iV1           = iV1;
            m_pDelayEventList[i]->m_iV2           = iV2;
            m_pDelayEventList[i]->m_iV3           = iV3;
            m_pDelayEventList[i]->m_dwTriggerTime = dwLastTime;
            return TRUE;
        }
    }
    return FALSE;  // No available slots
}
```

### DelayEventProcessor

**File:** `Game.cpp` (Line ~29585)
**Signature:** `void CGame::DelayEventProcessor()`

Called every 1 second from the main game loop. Processes all expired events.

**Call Location:**
```cpp
// Game.cpp:45544-45547
if ((dwTime - m_dwGameTime6) > 1000) {
    DelayEventProcessor();
    SendStockMsgToGateServer();
    m_dwGameTime6 = dwTime;
}
```

### bRemoveFromDelayEventList

**File:** `Game.cpp` (Line ~29766)
**Signature:**
```cpp
BOOL CGame::bRemoveFromDelayEventList(
    int iH,           // Target handle
    char cType,       // Target type
    int iEffectType   // Effect type to remove (NULL = remove all for target)
);
```

**Purpose:** Removes pending events for a target entity. Used when:
- Player/NPC dies
- Effect is dispelled early
- Player logs out or is deleted

**Implementation:**
```cpp
BOOL CGame::bRemoveFromDelayEventList(int iH, char cType, int iEffectType)
{
    for (int i = 0; i < DEF_MAXDELAYEVENTS; i++) {
        if (m_pDelayEventList[i] != NULL) {
            if (iEffectType == NULL) {
                // Remove all events for this target
                if ((m_pDelayEventList[i]->m_iTargetH == iH) &&
                    (m_pDelayEventList[i]->m_cTargetType == cType)) {
                    delete m_pDelayEventList[i];
                    m_pDelayEventList[i] = NULL;
                }
            }
            else {
                // Remove only specific effect type
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

## Magic Release Events

### Overview

`DEF_DELAYEVENTTYPE_MAGICRELEASE` (type 2) is the most commonly used event type. It schedules the removal of magic effects after their duration expires.

### Supported Magic Types

| Magic Type | Constant | Effect | Typical Duration |
|------------|----------|--------|------------------|
| Ice | `DEF_MAGICTYPE_ICE` | Movement restriction | 5-30 seconds |
| Polymorph | `DEF_MAGICTYPE_POLYMORPH` | Form change | Spell-dependent |
| Inhibition | `DEF_MAGICTYPE_INHIBITION` | Magic blocking | Spell-dependent |
| Invisibility | `DEF_MAGICTYPE_INVISIBILITY` | Stealth | Spell-dependent |
| Berserk | `DEF_MAGICTYPE_BERSERK` | Attack boost | 600 seconds |
| Protection | `DEF_MAGICTYPE_PROTECT` | Damage reduction | Spell-dependent |
| Confusion | `DEF_MAGICTYPE_CONFUSE` | Target illusion | Spell-dependent |
| Hold Object | `DEF_MAGICTYPE_HOLDOBJECT` | Paralysis | Spell-dependent |

### Registration Examples

**Ice Effect (5 seconds):**
```cpp
bRegisterDelayEvent(DEF_DELAYEVENTTYPE_MAGICRELEASE, DEF_MAGICTYPE_ICE,
    dwTime + (5*1000),  // 5 second delay
    iTargetH, DEF_OWNERTYPE_PLAYER,
    NULL, NULL, NULL,   // No map/position needed
    NULL, NULL, NULL);  // No extra parameters
```

**Protection Spell:**
```cpp
bRegisterDelayEvent(DEF_DELAYEVENTTYPE_MAGICRELEASE, DEF_MAGICTYPE_PROTECT,
    dwTime + (m_pMagicConfigList[sType]->m_dwLastTime * 1000),
    iClientH, DEF_OWNERTYPE_PLAYER,
    NULL, NULL, NULL,
    iProtectionSubtype,  // V1 = protection variant (1=arrow, 2/5=magic, 3/4=shield)
    NULL, NULL);
```

### Processing Logic

```cpp
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

        // Type-specific cleanup
        if (m_pDelayEventList[i]->m_iEffectType == DEF_MAGICTYPE_INHIBITION)
            m_pClientList[m_pDelayEventList[i]->m_iTargetH]->m_bInhibition = FALSE;

        if (m_pDelayEventList[i]->m_iEffectType == DEF_MAGICTYPE_INVISIBILITY)
            SetInvisibilityFlag(m_pDelayEventList[i]->m_iTargetH, DEF_OWNERTYPE_PLAYER, FALSE);

        if (m_pDelayEventList[i]->m_iEffectType == DEF_MAGICTYPE_BERSERK)
            SetBerserkFlag(m_pDelayEventList[i]->m_iTargetH, DEF_OWNERTYPE_PLAYER, FALSE);

        // Protection variants use V1 to determine subtype
        if (m_pDelayEventList[i]->m_iEffectType == DEF_MAGICTYPE_PROTECT) {
            switch(m_pDelayEventList[i]->m_iV1) {
                case 1:  // Protection from Arrow
                    SetProtectionFromArrowFlag(..., FALSE);
                    break;
                case 2:
                case 5:  // Magic Protection
                    SetMagicProtectionFlag(..., FALSE);
                    break;
                case 3:
                case 4:  // Defense Shield
                    SetDefenseShieldFlag(..., FALSE);
                    break;
            }
        }

        // Polymorph restores original form
        if (m_pDelayEventList[i]->m_iEffectType == DEF_MAGICTYPE_POLYMORPH) {
            m_pClientList[...->m_iTargetH]->m_sType =
                m_pClientList[...->m_iTargetH]->m_sOriginalType;
            SendEventToNearClient_TypeA(..., MSGID_EVENT_MOTION, DEF_OBJECTNULLACTION, ...);
        }

        if (m_pDelayEventList[i]->m_iEffectType == DEF_MAGICTYPE_ICE)
            SetIceFlag(m_pDelayEventList[i]->m_iTargetH, DEF_OWNERTYPE_PLAYER, FALSE);

        // Confusion variants
        if (m_pDelayEventList[i]->m_iEffectType == DEF_MAGICTYPE_CONFUSE)
            switch(m_pDelayEventList[i]->m_iV1) {
                case 3: SetIllusionFlag(..., FALSE); break;
                case 4: SetIllusionMovementFlag(..., FALSE); break;
            }
        break;

    case DEF_OWNERTYPE_NPC:
        // Similar logic for NPCs...
        break;
    }
    break;
```

---

## Skill Item Events

### Overview

`DEF_DELAYEVENTTYPE_USEITEM_SKILL` (type 3) handles items that require a channeling time before their effect is calculated.

### How It Works

1. Player uses skill item (e.g., fishing rod, mining pick)
2. Server validates item and sets skill "using" status
3. Delay event is registered with channeling time
4. When event fires, skill effect is calculated
5. Result is sent to player

### Registration

```cpp
// Game.cpp:27555-27558
bRegisterDelayEvent(
    DEF_DELAYEVENTTYPE_USEITEM_SKILL,
    m_pClientList[iClientH]->m_pItemList[sItemIndex]->m_sRelatedSkill,  // Effect = skill number
    dwTime + m_pSkillConfigList[iRelatedSkill]->m_sValue2 * 1000,      // Channeling time
    iClientH, DEF_OWNERTYPE_PLAYER,
    m_pClientList[iClientH]->m_cMapIndex,
    dX, dY,
    m_pClientList[iClientH]->m_cSkillMastery[iRelatedSkill],  // V1 = skill mastery
    iSkillUsingTimeID,                                         // V2 = unique time ID
    NULL                                                       // V3 = unused
);
```

### Processing Logic

```cpp
case DEF_DELAYEVENTTYPE_USEITEM_SKILL:
    switch (m_pDelayEventList[i]->m_cTargetType) {
    case DEF_OWNERTYPE_PLAYER:
        iSkillNum = m_pDelayEventList[i]->m_iEffectType;

        // Validate player still exists
        if (m_pClientList[m_pDelayEventList[i]->m_iTargetH] == NULL) break;

        // Check if skill was cancelled (e.g., player moved or was hit)
        if (m_pClientList[...->m_iTargetH]->m_bSkillUsingStatus[iSkillNum] == FALSE) break;

        // Validate time ID matches (prevents exploits from rapid re-use)
        if (m_pClientList[...->m_iTargetH]->m_iSkillUsingTimeID[iSkillNum] !=
            m_pDelayEventList[i]->m_iV2) break;

        // Clear skill using status
        m_pClientList[...->m_iTargetH]->m_bSkillUsingStatus[iSkillNum] = FALSE;
        m_pClientList[...->m_iTargetH]->m_iSkillUsingTimeID[iSkillNum] = NULL;

        // Calculate skill effect
        iResult = iCalculateUseSkillItemEffect(
            m_pDelayEventList[i]->m_iTargetH,
            m_pDelayEventList[i]->m_cTargetType,
            m_pDelayEventList[i]->m_iV1,        // Skill mastery
            iSkillNum,
            m_pDelayEventList[i]->m_cMapIndex,
            m_pDelayEventList[i]->m_dX,
            m_pDelayEventList[i]->m_dY
        );

        // Notify player of result
        SendNotifyMsg(NULL, m_pDelayEventList[i]->m_iTargetH,
            DEF_NOTIFY_SKILLUSINGEND, iResult, NULL, NULL, NULL);
        break;
    }
    break;
```

### Skill Cancellation

The skill can be cancelled before the delay event fires if:
- Player moves
- Player takes damage
- Player uses another skill
- Player logs out

When cancelled, `m_bSkillUsingStatus[skill]` is set to FALSE, and the delay event simply exits without effect when it fires.

---

## Meteor Strike Sequence

### Overview

Meteor Strike is a powerful Crusade-only ability that attacks enemy city structures. It demonstrates the event system's ability to orchestrate multi-stage sequences.

### Sequence Timeline

```
T+0s   : METEORSTRIKE event registered (initiated by Crusade system)
T+5s   : MeteorStrikeHandler() executes
         ├── Notifies clients of meteor visual effect
         ├── Damages strike points (buildings)
         ├── Registers DOMETEORSTRIKEDAMAGE at T+6s (1s later)
         ├── Registers DOMETEORSTRIKEDAMAGE at T+9s (4s later)
         └── Registers CALCMETEORSTRIKEEFFECT at T+11s (6s later)
T+6s   : First DoMeteorStrikeDamageHandler() - damages players
T+9s   : Second DoMeteorStrikeDamageHandler() - damages players again
T+11s  : CalcMeteorStrikeEffectHandler() - determines war outcome
```

### Initiation

```cpp
// Game.cpp:53089 - Elvine city attack
bRegisterDelayEvent(DEF_DELAYEVENTTYPE_METEORSTRIKE, NULL,
    dwTime + 5000,  // 5 second warning
    NULL, NULL,
    m_iElvineMapIndex,  // Target map
    NULL, NULL, NULL, NULL, NULL);

// Game.cpp:53110 - Aresden city attack
bRegisterDelayEvent(DEF_DELAYEVENTTYPE_METEORSTRIKE, NULL,
    dwTime + 1000*5, NULL, NULL,
    m_iAresdenMapIndex,
    NULL, NULL, NULL, NULL, NULL);
```

### MeteorStrikeHandler

**File:** `Game.cpp` (Line ~46189)

```cpp
void CGame::MeteorStrikeHandler(int iMapIndex)
{
    // Validate
    if (iMapIndex == -1) return;
    if (m_pMapList[iMapIndex] == NULL) return;
    if (m_pMapList[iMapIndex]->m_iTotalStrikePoints == 0) return;

    // Find active strike points (buildings with HP > 0)
    int iIndex = 0;
    for (int i = 1; i <= m_pMapList[iMapIndex]->m_iTotalStrikePoints; i++) {
        if (m_pMapList[iMapIndex]->m_stStrikePoint[i].iHP > 0) {
            iTargetArray[iIndex] = i;
            iIndex++;
        }
    }

    // Clear result tracking
    m_stMeteorStrikeResult.iCasualties = 0;
    m_stMeteorStrikeResult.iCrashedStructureNum = 0;
    m_stMeteorStrikeResult.iStructureDamageAmount = 0;

    if (iIndex == 0) {
        // All buildings destroyed - skip to effect calculation
        bRegisterDelayEvent(DEF_DELAYEVENTTYPE_CALCMETEORSTRIKEEFFECT, NULL,
            dwTime + 6000, NULL, NULL, iMapIndex, NULL, NULL, NULL, NULL, NULL);
    }
    else {
        // Notify clients of meteor strike visual
        for (int i = 1; i < DEF_MAXCLIENTS; i++) {
            if ((m_pClientList[i] != NULL) &&
                (m_pClientList[i]->m_bIsInitComplete == TRUE) &&
                (m_pClientList[i]->m_cMapIndex == iMapIndex)) {
                SendNotifyMsg(NULL, i, DEF_NOTIFY_METEORSTRIKEHIT, NULL, NULL, NULL, NULL);
            }
        }

        // Damage each strike point
        for (int i = 0; i < iIndex; i++) {
            // Check for Energy Shield Generators (ESG) nearby
            int iTotalESG = 0;
            for (int ix = dX-10; ix <= dX+10; ix++)
            for (int iy = dY-10; iy <= dY+10; iy++) {
                m_pMapList[iMapIndex]->GetOwner(&sOwnerH, &cOwnerType, ix, iy);
                if ((cOwnerType == DEF_OWNERTYPE_NPC) &&
                    (m_pNpcList[sOwnerH] != NULL) &&
                    (m_pNpcList[sOwnerH]->m_sType == 40)) {  // ESG NPC type
                    iTotalESG++;
                }
            }

            // 2+ ESGs = protected, <2 = takes damage
            if (iTotalESG < 2) {
                m_pMapList[iMapIndex]->m_stStrikePoint[iTargetIndex].iHP -= (2 - iTotalESG);
                if (m_pMapList[iMapIndex]->m_stStrikePoint[iTargetIndex].iHP <= 0) {
                    // Building destroyed
                    m_pMapList[iMapIndex]->m_stStrikePoint[iTargetIndex].iHP = 0;
                    m_pMapList[...->m_iMapIndex]->m_bIsDisabled = TRUE;
                    m_stMeteorStrikeResult.iCrashedStructureNum++;
                }
                else {
                    // Building damaged - spawn fire effect
                    m_stMeteorStrikeResult.iStructureDamageAmount += (2 - iTotalESG);
                    iAddDynamicObjectList(NULL, DEF_OWNERTYPE_PLAYER_INDIRECT,
                        DEF_DYNAMICOBJECT_FIRE2, iMapIndex, effectX, effectY, 60*1000*50);
                }
            }
        }

        // Schedule follow-up events
        bRegisterDelayEvent(DEF_DELAYEVENTTYPE_DOMETEORSTRIKEDAMAGE, NULL,
            dwTime + 1000, NULL, NULL, iMapIndex, NULL, NULL, NULL, NULL, NULL);
        bRegisterDelayEvent(DEF_DELAYEVENTTYPE_DOMETEORSTRIKEDAMAGE, NULL,
            dwTime + 4000, NULL, NULL, iMapIndex, NULL, NULL, NULL, NULL, NULL);
        bRegisterDelayEvent(DEF_DELAYEVENTTYPE_CALCMETEORSTRIKEEFFECT, NULL,
            dwTime + 6000, NULL, NULL, iMapIndex, NULL, NULL, NULL, NULL, NULL);
    }
}
```

### DoMeteorStrikeDamageHandler

**File:** `Game.cpp` (Line ~42697)

Applies damage to all players on the affected map.

```cpp
void CGame::DoMeteorStrikeDamageHandler(int iMapIndex)
{
    for (int i = 1; i < DEF_MAXCLIENTS; i++) {
        if ((m_pClientList[i] != NULL) &&
            (m_pClientList[i]->m_cSide != 0) &&  // Has faction allegiance
            (m_pClientList[i]->m_cMapIndex == iMapIndex)) {

            // Calculate damage based on level
            int iDamage = iDice(1, m_pClientList[i]->m_iLevel) + m_pClientList[i]->m_iLevel;
            if (iDamage > 255) iDamage = 255;  // Cap at 255

            // Magic protection reduces damage
            if (m_pClientList[i]->m_cMagicEffectStatus[DEF_MAGICTYPE_PROTECT] == 2)
                iDamage = (iDamage / 2) - 2;

            // Full protection negates damage
            if (m_pClientList[i]->m_cMagicEffectStatus[DEF_MAGICTYPE_PROTECT] == 5)
                iDamage = 0;

            // Admins take no damage
            if (m_pClientList[i]->m_iAdminUserLevel > 0)
                iDamage = 0;

            m_pClientList[i]->m_iHP -= iDamage;

            if (m_pClientList[i]->m_iHP <= 0) {
                ClientKilledHandler(i, NULL, NULL, iDamage);
                m_stMeteorStrikeResult.iCasualties++;
            }
            else {
                if (iDamage > 0) {
                    SendNotifyMsg(NULL, i, DEF_NOTIFY_HP, NULL, NULL, NULL, NULL);
                    SendEventToNearClient_TypeA(i, DEF_OWNERTYPE_PLAYER,
                        MSGID_EVENT_MOTION, DEF_OBJECTDAMAGE, iDamage, NULL, NULL);

                    // Damage breaks Hold Person/Paralyze
                    if (m_pClientList[i]->m_cMagicEffectStatus[DEF_MAGICTYPE_HOLDOBJECT] != 0) {
                        SendNotifyMsg(NULL, i, DEF_NOTIFY_MAGICEFFECTOFF,
                            DEF_MAGICTYPE_HOLDOBJECT,
                            m_pClientList[i]->m_cMagicEffectStatus[DEF_MAGICTYPE_HOLDOBJECT],
                            NULL, NULL);
                        m_pClientList[i]->m_cMagicEffectStatus[DEF_MAGICTYPE_HOLDOBJECT] = NULL;
                        bRemoveFromDelayEventList(i, DEF_OWNERTYPE_PLAYER, DEF_MAGICTYPE_HOLDOBJECT);
                    }
                }
            }
        }
    }
}
```

### CalcMeteorStrikeEffectHandler

**File:** `Game.cpp` (Line ~48540)

Determines if the Crusade war has ended based on structure damage.

```cpp
void CGame::CalcMeteorStrikeEffectHandler(int iMapIndex)
{
    if (m_bIsCrusadeMode == FALSE) return;

    // Count remaining active structures
    int iActiveStructure = 0;
    for (int i = 1; i <= m_pMapList[iMapIndex]->m_iTotalStrikePoints; i++) {
        if (m_pMapList[iMapIndex]->m_stStrikePoint[i].iHP > 0) {
            iActiveStructure++;
        }
    }

    if (iActiveStructure == 0) {
        // All structures destroyed - end Crusade
        char cWinnerSide;
        if (iMapIndex == m_iAresdenMapIndex) {
            cWinnerSide = 2;  // Elvine wins
            LocalEndCrusadeMode(2);
        }
        else if (iMapIndex == m_iElvineMapIndex) {
            cWinnerSide = 1;  // Aresden wins
            LocalEndCrusadeMode(1);
        }
        else {
            cWinnerSide = 0;
            LocalEndCrusadeMode(0);
        }

        // Notify other servers of Crusade end
        // Send GSM_ENDCRUSADE message with results...
    }
}
```

---

## Ancient Tablet Events

### Overview

`DEF_DELAYEVENTTYPE_ANCIENT_TABLET` (type 7) handles the expiration of Ancient Slate (Full Ancient Tablet) buffs.

### Slate Types and Status Flags

| Type | Name | Status Flag | Effect |
|------|------|-------------|--------|
| 1 | Invincible | `0x400000` | Damage immunity |
| 2 | Berserk | (uses MAGICRELEASE) | Attack boost |
| 3 | Mana | `0x800000` | Mana regeneration boost |
| 4 | Exp | `0x10000` | Experience gain boost |

### Registration

```cpp
// Game.cpp:27147-27149 - For types 1, 3, 4
SetSlateFlag(iClientH, slateType, TRUE);
bRegisterDelayEvent(DEF_DELAYEVENTTYPE_ANCIENT_TABLET,
    m_pClientList[iClientH]->m_pItemList[sItemIndex]->m_sItemSpecEffectValue2,  // Slate type
    dwTime + (1000 * 600),  // 10 minute duration
    iClientH, DEF_OWNERTYPE_PLAYER,
    NULL, NULL, NULL,
    1, NULL, NULL);  // V1 = 1 (unused marker)

// Type 2 (Berserk) uses MAGICRELEASE instead:
bRegisterDelayEvent(DEF_DELAYEVENTTYPE_MAGICRELEASE, DEF_MAGICTYPE_BERSERK,
    dwTime + (1000 * 600),
    iClientH, DEF_OWNERTYPE_PLAYER, NULL, NULL, NULL, 1, NULL, NULL);
```

### Processing Logic

```cpp
case DEF_DELAYEVENTTYPE_ANCIENT_TABLET:
    int iTemp;

    // Determine which flag to clear based on current status
    if ((m_pClientList[m_pDelayEventList[i]->m_iTargetH]->m_iStatus & 0x400000) != 0) {
        iTemp = 1;  // Invincible slate
    }
    else if ((m_pClientList[m_pDelayEventList[i]->m_iTargetH]->m_iStatus & 0x800000) != 0) {
        iTemp = 3;  // Mana slate
    }
    else if ((m_pClientList[m_pDelayEventList[i]->m_iTargetH]->m_iStatus & 0x10000) != 0) {
        iTemp = 4;  // Exp slate
    }

    // Notify client and clear flag
    SendNotifyMsg(NULL, m_pDelayEventList[i]->m_iTargetH,
        DEF_NOTIFY_SLATE_STATUS, iTemp, NULL, NULL, NULL);
    SetSlateFlag(m_pDelayEventList[i]->m_iTargetH, iTemp, FALSE);
    break;
```

---

## Event Processing Loop

### Main Processor Implementation

**File:** `Game.cpp` (Line ~29585)

```cpp
void CGame::DelayEventProcessor()
{
    DWORD dwTime = timeGetTime();

    for (int i = 0; i < DEF_MAXDELAYEVENTS; i++) {
        if ((m_pDelayEventList[i] != NULL) &&
            (m_pDelayEventList[i]->m_dwTriggerTime < dwTime)) {

            // Event ready to fire
            switch (m_pDelayEventList[i]->m_iDelayType) {
            case DEF_DELAYEVENTTYPE_ANCIENT_TABLET:
                // Handle slate expiry...
                break;

            case DEF_DELAYEVENTTYPE_CALCMETEORSTRIKEEFFECT:
                CalcMeteorStrikeEffectHandler(m_pDelayEventList[i]->m_cMapIndex);
                break;

            case DEF_DELAYEVENTTYPE_DOMETEORSTRIKEDAMAGE:
                DoMeteorStrikeDamageHandler(m_pDelayEventList[i]->m_cMapIndex);
                break;

            case DEF_DELAYEVENTTYPE_METEORSTRIKE:
                MeteorStrikeHandler(m_pDelayEventList[i]->m_cMapIndex);
                break;

            case DEF_DELAYEVENTTYPE_USEITEM_SKILL:
                // Handle skill item completion...
                break;

            case DEF_DELAYEVENTTYPE_DAMAGEOBJECT:
                // Intentionally empty - handled by dynamic objects system
                break;

            case DEF_DELAYEVENTTYPE_MAGICRELEASE:
                // Handle magic effect removal...
                break;
            }

            // Delete processed event
            delete m_pDelayEventList[i];
            m_pDelayEventList[i] = NULL;
        }
    }
}
```

### Dead Code Note

There is an empty function `DelayEventProcess()` at line 16633 that appears to be dead code - possibly from an incomplete refactoring attempt. Only `DelayEventProcessor()` is actually used.

---

## Parameter Reference

### Event Type Parameter Mapping

| Event Type | m_iEffectType | m_iV1 | m_iV2 | m_iV3 |
|------------|---------------|-------|-------|-------|
| DAMAGEOBJECT (1) | - | - | - | - |
| MAGICRELEASE (2) | Magic type constant | Protection/Confusion subtype | - | - |
| USEITEM_SKILL (3) | Skill number | Skill mastery level | Skill using time ID | - |
| METEORSTRIKE (4) | - | - | - | - |
| DOMETEORSTRIKEDAMAGE (5) | - | - | - | - |
| CALCMETEORSTRIKEEFFECT (6) | - | - | - | - |
| ANCIENT_TABLET (7) | Slate type (1/3/4) | 1 (marker) | - | - |

### MAGICRELEASE V1 Meanings

**For DEF_MAGICTYPE_PROTECT:**

| V1 Value | Protection Type |
|----------|-----------------|
| 1 | Protection from Arrow |
| 2 | Magic Protection |
| 3 | Defense Shield |
| 4 | Defense Shield |
| 5 | Magic Protection |

**For DEF_MAGICTYPE_CONFUSE:**

| V1 Value | Confusion Type |
|----------|----------------|
| 3 | Illusion |
| 4 | Illusion Movement |

---

## Constants and Limits

### System Limits

```cpp
// Game.h:80
#define DEF_MAXDELAYEVENTS  60000  // Maximum concurrent delayed events
```

### Timing Constants

| Operation | Interval |
|-----------|----------|
| Processor check | 1000ms (1 second) |
| Meteor strike warning | 5000ms |
| Meteor damage wave 1 | 1000ms after start |
| Meteor damage wave 2 | 4000ms after start |
| Meteor result calculation | 6000ms after start |
| Ancient Tablet duration | 600000ms (10 minutes) |

### Owner Type Constants

```cpp
#define DEF_OWNERTYPE_PLAYER  1
#define DEF_OWNERTYPE_NPC     2
```

---

## Related Systems

### Magic System

The delayed events system works closely with the magic system:

```cpp
// Setting magic effect status
m_pClientList[iClientH]->m_cMagicEffectStatus[DEF_MAGICTYPE_ICE] = value;

// Clearing via flags
SetIceFlag(iClientH, DEF_OWNERTYPE_PLAYER, FALSE);
SetInvisibilityFlag(iClientH, DEF_OWNERTYPE_PLAYER, FALSE);
SetBerserkFlag(iClientH, DEF_OWNERTYPE_PLAYER, FALSE);
```

### Skill System

Item skills use delayed events for channeling:

```cpp
// Track skill usage state
m_pClientList[iClientH]->m_bSkillUsingStatus[iSkillNum] = TRUE;
m_pClientList[iClientH]->m_iSkillUsingTimeID[iSkillNum] = timeID;

// Effect calculation
iCalculateUseSkillItemEffect(iClientH, cTargetType, iMastery, iSkillNum, iMapIndex, dX, dY);
```

### Dynamic Objects System

`DEF_DELAYEVENTTYPE_DAMAGEOBJECT` (type 1) appears to be a placeholder related to dynamic damage objects like Spike Field. The actual damage logic is handled by the Dynamic Objects system rather than the delay event processor.

### Crusade System

Meteor strikes are initiated by the Crusade system:

```cpp
// Triggered when faction accumulates enough mana
bRegisterDelayEvent(DEF_DELAYEVENTTYPE_METEORSTRIKE, NULL, dwTime + 5000,
    NULL, NULL, iEnemyMapIndex, NULL, NULL, NULL, NULL, NULL);
```

---

## Implementation Notes

### Memory Management

- Events are allocated with `new CDelayEvent` when registered
- Events are deallocated with `delete` after processing or removal
- No memory pooling - each event is a separate allocation

```cpp
// Allocation
m_pDelayEventList[i] = new class CDelayEvent;

// Deallocation
delete m_pDelayEventList[i];
m_pDelayEventList[i] = NULL;
```

### Thread Safety

- No mutex/locking on event operations
- Single-threaded processing assumed
- Events processed in array order (not chronologically sorted)

### Performance Characteristics

- Linear scan of 60,000 slots every second: O(n)
- No priority queue or sorted list
- Events fire in slot order, not timestamp order
- Slot reuse via NULL checking

### Edge Cases

**Target Dies Before Event Fires:**
```cpp
// Always check target validity
if (m_pClientList[m_pDelayEventList[i]->m_iTargetH] == NULL) break;
```

**Multiple Events Same Target:**
- Each magic effect can have its own pending removal event
- `bRemoveFromDelayEventList()` can remove all events for a target or just specific types

**Skill Cancellation:**
```cpp
// Time ID prevents stale events from firing
if (m_pClientList[...->m_iTargetH]->m_iSkillUsingTimeID[iSkillNum] !=
    m_pDelayEventList[i]->m_iV2) break;
```

### Potential Improvements

1. **Priority Queue** - Sort by trigger time for O(log n) insertion and O(1) retrieval
2. **Memory Pool** - Pre-allocate events to reduce allocation overhead
3. **Bucketed Time** - Group events by second for faster processing
4. **Type-Specific Arrays** - Separate arrays per event type for better cache locality

---

## Summary

The Legacy Delayed Events system provides a simple but effective mechanism for scheduling timed game events. Key features:

1. **Fixed Array Storage** - 60,000 event slots with NULL indicating empty
2. **Polling Processor** - Scans all events every second
3. **Seven Event Types** - Magic release, skills, meteor strikes, tablets
4. **Generic Parameters** - V1/V2/V3 provide flexible per-type data
5. **Target Validation** - All handlers verify target exists before processing

The system is central to magic buff/debuff timing, skill channeling, and complex multi-stage events like Crusade meteor strikes.
