# Legacy Quest System Documentation

## Table of Contents

1. [Overview](#overview)
2. [Quest Type Constants](#quest-type-constants)
3. [Data Structures](#data-structures)
   - [CQuest Class](#cquest-class)
   - [CClient Quest Fields](#cclient-quest-fields)
4. [Quest Configuration](#quest-configuration)
   - [Configuration File Format](#configuration-file-format)
   - [Field Descriptions](#field-descriptions)
   - [Configuration Parsing](#configuration-parsing)
5. [Quest NPCs](#quest-npcs)
   - [NPC Types and iWho Values](#npc-types-and-iwho-values)
   - [NPC Talk Handler Flow](#npc-talk-handler-flow)
6. [Quest Assignment](#quest-assignment)
   - [Search Algorithm](#search-algorithm)
   - [Eligibility Criteria](#eligibility-criteria)
   - [Reward Selection](#reward-selection)
7. [Quest Progress Tracking](#quest-progress-tracking)
   - [Monster Hunt Progress](#monster-hunt-progress)
   - [Go-Place Progress](#go-place-progress)
   - [Location Flag Optimization](#location-flag-optimization)
8. [Quest Completion](#quest-completion)
   - [Completion Check Logic](#completion-check-logic)
   - [Reward Distribution](#reward-distribution)
   - [Reward Types](#reward-types)
9. [Quest Cancellation and Abort](#quest-cancellation-and-abort)
10. [Crusade Mode Quests](#crusade-mode-quests)
11. [Response Modes](#response-modes)
12. [Network Messages](#network-messages)
13. [Persistence](#persistence)
14. [All Related Functions](#all-related-functions)
15. [Constants Reference](#constants-reference)

---

## Overview

The Quest System in Helbreath is a relatively simple mission/task system that allows players to receive quests from NPCs, complete objectives (primarily monster hunting or visiting locations), and receive rewards including items, experience, gold, and contribution points.

**Key Characteristics:**
- Maximum of 200 quest definitions (`DEF_MAXQUESTTYPE`)
- Players can only have 1 active quest at a time
- Quest configuration loaded from Log Server at startup
- 12 quest types defined (though only 2 are fully implemented: Monster Hunt and Go-Place)
- Crusade-specific quests available during war mode
- Quests filtered by faction (side), level, skill requirements, and contribution

**Files Involved:**
- `Quest.h` - CQuest class definition and quest type constants
- `Game.cpp` - All quest logic (lines ~35700-41110)
- `Client.h` - Player quest state fields
- `NetMessages.h` - Quest-related message IDs

---

## Quest Type Constants

Defined in `Quest.h`:

| Constant | Value | Description | Implementation Status |
|----------|-------|-------------|----------------------|
| `DEF_QUESTTYPE_MONSTERHUNT` | 1 | Hunt specific monsters in a specific map | **Fully Implemented** |
| `DEF_QUESTTYPE_MONSTERHUNT_TIMELIMIT` | 2 | Monster hunt with time limit | Not implemented |
| `DEF_QUESTTYPE_ASSASSINATION` | 3 | Assassinate target | Not implemented |
| `DEF_QUESTTYPE_DELIVERY` | 4 | Deliver items to location | Not implemented |
| `DEF_QUESTTYPE_ESCORT` | 5 | Escort/protect a character | Not implemented |
| `DEF_QUESTTYPE_GUARD` | 6 | Guard location from enemies | Not implemented |
| `DEF_QUESTTYPE_GOPLACE` | 7 | Visit specific location (spy mission) | **Fully Implemented** |
| `DEF_QUESTTYPE_BUILDSTRUCTURE` | 8 | Build a crusade structure | Crusade only |
| `DEF_QUESTTYPE_SUPPLYBUILDSTRUCTURE` | 9 | Supply structure building | Crusade only |
| `DEF_QUESTTYPE_STRATEGICSTRIKE` | 10 | Strategic strike mission | Teleport-based |
| `DEF_QUESTTYPE_SENDTOBATTLE` | 11 | Send to battlefield | Crusade only |
| `DEF_QUESTTYPE_SETOCCUPYFLAG` | 12 | Place occupy flag | Crusade only |

---

## Data Structures

### CQuest Class

Defined in `Quest.h` (lines 32-73):

```cpp
class CQuest
{
public:
    char m_cSide;               // Which faction this quest is for (1=Aresden, 2=Elvine)

    int m_iType;                // Quest type (DEF_QUESTTYPE_*)
    int m_iTargetType;          // Target type (NPC type ID for monster hunts)
    int m_iMaxCount;            // Kill count required or other objective count

    int m_iFrom;                // NPC type that gives this quest (4=CityHall, 21=Guard, etc.)

    int m_iMinLevel;            // Minimum player level required
    int m_iMaxLevel;            // Maximum player level allowed

    int m_iRequiredSkillNum;    // Required skill index (-1 = none)
    int m_iRequiredSkillLevel;  // Required skill level

    int m_iTimeLimit;           // Time limit in seconds (-1 = no limit)
    int m_iAssignType;          // -1=anytime, 1=crusade mode only

    // Rewards - 3 reward slots, randomly selected (index 0 unused)
    int m_iRewardType[4];       // Item ID, -1=exp, -2=scaled exp, 0=contribution only
    int m_iRewardAmount[4];     // Amount of reward

    int m_iContribution;        // Contribution points gained on completion
    int m_iContributionLimit;   // Max contribution to receive this quest

    int m_iResponseMode;        // Dialog mode: 0=OK, 1=Accept/Decline, 2=Next

    char m_cTargetName[21];     // Target map name or character name
    int  m_sX, m_sY, m_iRange;  // Target coordinates and range

    int  m_iQuestID;            // Unique quest ID (for save/load validation)

    int  m_iReqContribution;    // Minimum contribution required to receive quest
};
```

### CClient Quest Fields

Defined in `Client.h` (lines 240-251):

```cpp
// Active quest tracking
int   m_iQuest;              // Current quest index (0 = no quest)
int   m_iQuestID;            // Quest ID for validation
int   m_iAskedQuest;         // Pending quest (not yet accepted)
int   m_iCurQuestCount;      // Current progress (kill count, etc.)

// Reward info (stored for completion)
int   m_iQuestRewardType;    // Selected reward type
int   m_iQuestRewardAmount;  // Selected reward amount

// Contribution (permanent stat)
int   m_iContribution;       // Total contribution to faction

// Optimization flags
BOOL  m_bQuestMatchFlag_Loc; // TRUE if player is on correct map
BOOL  m_bIsQuestCompleted;   // TRUE when objective is met
```

---

## Quest Configuration

### Configuration File Format

Quests are configured in `Quest.cfg`, loaded via Log Server. Each line defines one quest:

```
quest = [index] [side] [type] [target_type] [max_count] [from] [min_level] [max_level] [req_skill] [req_skill_level] [time_limit] [assign_type] [reward1_type] [reward1_amt] [reward2_type] [reward2_amt] [reward3_type] [reward3_amt] [contribution] [contrib_limit] [response_mode] [target_name] [x] [y] [range] [quest_id] [req_contribution]
```

### Example Configuration

```
quest = 1 1 1 17 150 4 50 310 -1 -1 -1 -1 12000 90 18000 90 14000 120 99999 1 aresden 0 0 0 101 0
```

**Breakdown:**
- Quest #1
- Side 1 (Aresden)
- Type 1 (Monster Hunt)
- Target Type 17 (Scorpion)
- Kill 150 monsters
- From NPC 4 (City Hall)
- Level range 50-310
- No skill requirement (-1)
- No time limit (-1)
- Not crusade-specific (-1)
- Rewards: 12000 gold (item 90), 18000 gold, 14000 gold
- 120 contribution points
- Max 99999 contribution to receive
- Response mode 1 (Accept/Decline)
- Target map: "aresden"
- Quest ID 101
- No contribution requirement (0)

### Field Descriptions

| Field | Description | Values |
|-------|-------------|--------|
| index | Quest number | 1-199 |
| side | Faction | 1=Aresden, 2=Elvine |
| type | Quest type | See DEF_QUESTTYPE_* |
| target_type | NPC/monster type to hunt | NPC m_sType value |
| max_count | Kill/objective count | Any positive integer |
| from | Quest giver NPC type | 4=CityHall, 21=Guard, etc. |
| min_level | Minimum player level | 1-180 |
| max_level | Maximum player level | 1-180 |
| req_skill | Required skill index | -1 or skill index |
| req_skill_level | Required skill level | 0-200 |
| time_limit | Time limit (seconds) | -1 or seconds |
| assign_type | When available | -1=always, 1=crusade only |
| reward*_type | Reward item ID | Item ID, -1=exp, -2=scaled exp, 0=none |
| reward*_amt | Reward amount | Count or exp amount |
| contribution | Contribution gained | Points |
| contrib_limit | Max contribution to receive | Points |
| response_mode | Dialog type | 0=OK, 1=Accept/Decline, 2=Next |
| target_name | Map or target name | 20 char max |
| x, y | Coordinates for GOPLACE | Map coords |
| range | Range for GOPLACE | Tile radius |
| quest_id | Unique identifier | Any integer |
| req_contribution | Min contribution needed | Points |

### Configuration Parsing

Function: `_bDecodeQuestConfigFileContents()` (Game.cpp:37651-38024)

```cpp
BOOL CGame::_bDecodeQuestConfigFileContents(char * pData, DWORD dwMsgSize)
{
    // Tokenizes config file
    // cReadModeB tracks which field is being read (1-27)
    // Creates new CQuest objects in m_pQuestConfigList[]

    // Field parsing order:
    // 1: quest index
    // 2: m_cSide
    // 3: m_iType
    // 4: m_iTargetType
    // 5: m_iMaxCount
    // 6: m_iFrom
    // 7: m_iMinLevel
    // 8: m_iMaxLevel
    // 9: m_iRequiredSkillNum
    // 10: m_iRequiredSkillLevel
    // 11: m_iTimeLimit
    // 12: m_iAssignType
    // 13-18: m_iRewardType[1-3], m_iRewardAmount[1-3]
    // 19: m_iContribution
    // 20: m_iContributionLimit
    // 21: m_iResponseMode
    // 22: m_cTargetName
    // 23: m_sX
    // 24: m_sY
    // 25: m_iRange
    // 26: m_iQuestID
    // 27: m_iReqContribution
}
```

---

## Quest NPCs

### NPC Types and iWho Values

The `iWho` parameter identifies which NPC type the player is talking to:

| iWho | NPC Type | Quest Function | Notes |
|------|----------|----------------|-------|
| 1 | Shop Keeper | None | No quests |
| 2 | Unknown | None | No quests |
| 3 | Unknown | None | No quests |
| 4 | City Hall | `_iTalkToNpcResult_Cityhall()` | **Main quest giver** |
| 5 | Unknown | None | No quests |
| 6 | Unknown | None | No quests |
| 21 | Guard | `_iTalkToNpcResult_Guard()` | Faction messages only |
| 32 | Unknown | None | No quests |

**Note:** Only City Hall (iWho=4) actually gives quests. Other NPCs have stub functions that return -4 (no quest).

### NPC Talk Handler Flow

`NpcTalkHandler()` (Game.cpp:35691-35743):

```cpp
void CGame::NpcTalkHandler(int iClientH, int iWho)
{
    // 1. Check NPC type (iWho)
    switch (iWho) {
    case 4:  // City Hall
        iQuestNum = _iTalkToNpcResult_Cityhall(...);
        break;
    case 21: // Guard
        iQuestNum = _iTalkToNpcResult_Guard(...);
        if (iQuestNum >= 1000) return; // Guard messages
        break;
    // Other cases: no quest handling
    }

    // 2. If quest found (iQuestNum > 0)
    if (iQuestNum > 0) {
        // Store pending quest
        m_pClientList[iClientH]->m_iAskedQuest = iQuestNum;
        m_pClientList[iClientH]->m_iQuestRewardType = iRewardType;
        m_pClientList[iClientH]->m_iQuestRewardAmount = iRewardAmount;

        // Send quest offer to client
        SendNotifyMsg(iClientH, DEF_NOTIFY_NPCTALK, ...);
    }
    else {
        // Handle error codes
        switch (iQuestNum) {
        case 0:  // Generic NPC message
        case -1: // No matching quest
        case -2: // Wrong location
        case -3: // Player has PK count
        case -4: // No quest available
        case -5: // Quest completed (reward given)
        }
    }
}
```

---

## Quest Assignment

### Search Algorithm

`__iSearchForQuest()` (Game.cpp:38027-38088):

```cpp
int CGame::__iSearchForQuest(int iClientH, int iWho, ...)
{
    int iQuestList[DEF_MAXQUESTTYPE];
    int iIndex = 0;

    // Build list of eligible quests
    for (i = 1; i < DEF_MAXQUESTTYPE; i++) {
        if (m_pQuestConfigList[i] == NULL) continue;

        // Check all criteria
        if (!CheckEligibility(i, iClientH, iWho)) continue;

        // Quest is eligible, add to list
        iQuestList[iIndex++] = i;
    }

    // Randomly select from eligible quests
    if (iIndex == 0) return -1;

    int iQuest = iDice(1, iIndex) - 1;
    int iQuestIndex = iQuestList[iQuest];

    // Randomly select 1 of 3 rewards
    int iReward = iDice(1, 3);

    // Return quest data via output parameters
    return iQuestIndex;
}
```

### Eligibility Criteria

All criteria must pass for a quest to be offered:

| Criteria | Code | Description |
|----------|------|-------------|
| NPC Match | `m_iFrom == iWho` | Quest giver matches NPC talked to |
| Faction Match | `m_cSide == player->m_cSide` | Quest side matches player's faction |
| Level Range | `m_iMinLevel <= level <= m_iMaxLevel` | Player in level range |
| Contribution Min | `m_iReqContribution <= player contribution` | Has enough contribution |
| Skill Requirement | `skill_level >= m_iRequiredSkillLevel` | Has required skill (if any) |
| Crusade Mode | `m_iAssignType` matches game mode | Quest type matches war state |
| Contribution Max | `m_iContributionLimit >= player contribution` | Under contribution cap |

```cpp
// Eligibility check code (Game.cpp:38041-38058)
if (m_pQuestConfigList[i]->m_iFrom != iWho) goto SFQ_SKIP;
if (m_pQuestConfigList[i]->m_cSide != m_pClientList[iClientH]->m_cSide) goto SFQ_SKIP;
if (m_pQuestConfigList[i]->m_iMinLevel > m_pClientList[iClientH]->m_iLevel) goto SFQ_SKIP;
if (m_pQuestConfigList[i]->m_iMaxLevel < m_pClientList[iClientH]->m_iLevel) goto SFQ_SKIP;
if (m_pQuestConfigList[i]->m_iReqContribution > m_pClientList[iClientH]->m_iContribution) goto SFQ_SKIP;

if (m_pQuestConfigList[i]->m_iRequiredSkillNum != -1) {
    if (m_pClientList[iClientH]->m_cSkillMastery[m_pQuestConfigList[i]->m_iRequiredSkillNum] <
        m_pQuestConfigList[i]->m_iRequiredSkillLevel) goto SFQ_SKIP;
}

// Crusade mode filtering
if ((m_bIsCrusadeMode == TRUE) && (m_pQuestConfigList[i]->m_iAssignType != 1)) goto SFQ_SKIP;
if ((m_bIsCrusadeMode == FALSE) && (m_pQuestConfigList[i]->m_iAssignType == 1)) goto SFQ_SKIP;

if (m_pQuestConfigList[i]->m_iContributionLimit < m_pClientList[iClientH]->m_iContribution) goto SFQ_SKIP;
```

### Reward Selection

When a quest is offered, one of three rewards is randomly selected:

```cpp
// Random reward selection (Game.cpp:38072-38076)
int iReward = iDice(1, 3);  // Rolls 1, 2, or 3
*pRewardType   = m_pQuestConfigList[iQuestIndex]->m_iRewardType[iReward];
*pRewardAmount = m_pQuestConfigList[iQuestIndex]->m_iRewardAmount[iReward];
```

---

## Quest Progress Tracking

### Monster Hunt Progress

Progress is tracked in `NpcKillHandler()` when a player kills an NPC (Game.cpp:10784-10800):

```cpp
// Quest progress on NPC kill
iQuestIndex = m_pClientList[sAttackerH]->m_iQuest;
if (iQuestIndex != NULL) {
    if (m_pQuestConfigList[iQuestIndex] != NULL) {
        switch (m_pQuestConfigList[iQuestIndex]->m_iType) {
        case DEF_QUESTTYPE_MONSTERHUNT:
            // Check if on correct map AND killed correct monster type
            if ((m_pClientList[sAttackerH]->m_bQuestMatchFlag_Loc == TRUE) &&
                (m_pQuestConfigList[iQuestIndex]->m_iTargetType == m_pNpcList[iNpcH]->m_sType)) {

                // Increment kill count
                m_pClientList[sAttackerH]->m_iCurQuestCount++;

                // Calculate remaining
                cQuestRemain = m_pQuestConfigList[...] ->m_iMaxCount - m_pClientList[...]->m_iCurQuestCount;

                // Notify client of progress
                SendNotifyMsg(NULL, sAttackerH, DEF_NOTIFY_QUESTCOUNTER, cQuestRemain, ...);

                // Check if completed
                _bCheckIsQuestCompleted(sAttackerH);
            }
            break;
        }
    }
}
```

### Go-Place Progress

For GOPLACE quests, completion is checked whenever the player moves or teleports:

- On teleport completion (`RequestTeleportHandler`)
- On initial player data load
- Map change events

```cpp
// Check go-place completion (Game.cpp:38222-38233)
case DEF_QUESTTYPE_GOPLACE:
    if ((m_pClientList[iClientH]->m_bQuestMatchFlag_Loc == TRUE) &&
        (m_pClientList[iClientH]->m_sX >= m_pQuestConfigList[iQuestIndex]->m_sX - m_pQuestConfigList[iQuestIndex]->m_iRange) &&
        (m_pClientList[iClientH]->m_sX <= m_pQuestConfigList[iQuestIndex]->m_sX + m_pQuestConfigList[iQuestIndex]->m_iRange) &&
        (m_pClientList[iClientH]->m_sY >= m_pQuestConfigList[iQuestIndex]->m_sY - m_pQuestConfigList[iQuestIndex]->m_iRange) &&
        (m_pClientList[iClientH]->m_sY <= m_pQuestConfigList[iQuestIndex]->m_sY + m_pQuestConfigList[iQuestIndex]->m_iRange)) {
        // Quest completed!
        m_pClientList[iClientH]->m_bIsQuestCompleted = TRUE;
        SendNotifyMsg(NULL, iClientH, DEF_NOTIFY_QUESTCOMPLETED, ...);
        return TRUE;
    }
    break;
```

### Location Flag Optimization

To avoid string comparisons on every kill, the map name is checked once and a flag is set:

`_CheckQuestEnvironment()` (Game.cpp:38155-38198):

```cpp
void CGame::_CheckQuestEnvironment(int iClientH)
{
    // Quest ID validation check
    if (m_pQuestConfigList[iIndex]->m_iQuestID != m_pClientList[iClientH]->m_iQuestID) {
        // Quest configuration changed - abort quest
        _ClearQuestStatus(iClientH);
        SendNotifyMsg(iClientH, DEF_NOTIFY_QUESTABORTED, ...);
        return;
    }

    // Set location match flag for optimization
    switch (m_pQuestConfigList[iIndex]->m_iType) {
    case DEF_QUESTTYPE_MONSTERHUNT:
    case DEF_QUESTTYPE_GOPLACE:
        if (memcmp(currentMapName, targetMapName, 10) == 0)
            m_pClientList[iClientH]->m_bQuestMatchFlag_Loc = TRUE;
        else
            m_pClientList[iClientH]->m_bQuestMatchFlag_Loc = FALSE;
        break;
    }
}
```

**Special Case:** Quest indices 35-40 are auto-aborted (likely reserved or deprecated):

```cpp
if (iIndex >= 35 && iIndex <= 40) {
    // Clear quest and notify player
    m_pClientList[iClientH]->m_iQuest = 0;
    SendNotifyMsg(iClientH, DEF_NOTIFY_QUESTABORTED, ...);
    return;
}
```

---

## Quest Completion

### Completion Check Logic

`_bCheckIsQuestCompleted()` (Game.cpp:38200-38238):

```cpp
BOOL CGame::_bCheckIsQuestCompleted(int iClientH)
{
    if (m_pClientList[iClientH]->m_bIsQuestCompleted == TRUE) return FALSE;

    iQuestIndex = m_pClientList[iClientH]->m_iQuest;

    switch (m_pQuestConfigList[iQuestIndex]->m_iType) {
    case DEF_QUESTTYPE_MONSTERHUNT:
        if ((m_bQuestMatchFlag_Loc == TRUE) &&
            (m_iCurQuestCount >= m_iMaxCount)) {
            m_bIsQuestCompleted = TRUE;
            SendNotifyMsg(DEF_NOTIFY_QUESTCOMPLETED);
            return TRUE;
        }
        break;

    case DEF_QUESTTYPE_GOPLACE:
        if ((m_bQuestMatchFlag_Loc == TRUE) &&
            (player is within range of target coords)) {
            m_bIsQuestCompleted = TRUE;
            SendNotifyMsg(DEF_NOTIFY_QUESTCOMPLETED);
            return TRUE;
        }
        break;
    }

    return FALSE;
}
```

### Reward Distribution

Rewards are given when player returns to NPC after completing quest:

`_iTalkToNpcResult_Cityhall()` (Game.cpp:37510-37599):

```cpp
int CGame::_iTalkToNpcResult_Cityhall(...)
{
    // Check if player has active quest from this NPC
    if (m_pClientList[iClientH]->m_iQuest != NULL) {
        // Verify quest is from City Hall (m_iFrom == 4)
        if (m_pQuestConfigList[questIndex]->m_iFrom == 4) {
            // Check if completed
            if (m_pClientList[iClientH]->m_bIsQuestCompleted == TRUE) {

                // Give reward based on type
                if (m_iQuestRewardType > 0) {
                    // Item reward
                    pItem = new CItem();
                    pItem->m_dwCount = m_iQuestRewardAmount;

                    if (_bCheckItemReceiveCondition(iClientH, pItem)) {
                        _bAddClientItemList(iClientH, pItem);
                        // Add contribution
                        m_iContribution += quest->m_iContribution;
                        SendNotifyMsg(DEF_NOTIFY_QUESTREWARD, success=1);
                    }
                    else {
                        // Cannot carry more items
                        SendNotifyMsg(DEF_NOTIFY_QUESTREWARD, success=0);
                    }
                }
                else if (m_iQuestRewardType == -1) {
                    // Fixed experience reward
                    m_iExpStock += m_iQuestRewardAmount;
                    m_iContribution += quest->m_iContribution;
                    SendNotifyMsg(DEF_NOTIFY_QUESTREWARD, "Experience");
                }
                else if (m_iQuestRewardType == -2) {
                    // Scaled experience (level-based)
                    iExp = iDice(1, 10*level) * m_iQuestRewardAmount;
                    m_iExpStock += iExp;
                    m_iContribution += quest->m_iContribution;
                    SendNotifyMsg(DEF_NOTIFY_QUESTREWARD, actual_exp);
                }
                else {
                    // Contribution only (no item/exp)
                    m_iContribution += quest->m_iContribution;
                    SendNotifyMsg(DEF_NOTIFY_QUESTREWARD, "");
                }

                _ClearQuestStatus(iClientH);
                return -5;  // Quest completed and rewarded
            }
            else {
                return -1;  // Quest not yet completed
            }
        }
        return -4;  // Quest not from this NPC
    }

    // No active quest - offer new one
    return __iSearchForQuest(iClientH, 4, ...);
}
```

### Reward Types

| RewardType Value | Meaning | Handling |
|-----------------|---------|----------|
| > 0 | Item ID | Create item with count = RewardAmount |
| -1 | Fixed Experience | Add RewardAmount to ExpStock |
| -2 | Scaled Experience | `iDice(1, 10*level) * RewardAmount` |
| 0 | Contribution Only | No item/exp, just contribution |

---

## Quest Cancellation and Abort

### Player-Initiated Cancel

`CancelQuestHandler()` (Game.cpp:41102-41110):

```cpp
void CGame::CancelQuestHandler(int iClientH)
{
    if (m_pClientList[iClientH] == NULL) return;

    // Clear all quest state
    _ClearQuestStatus(iClientH);

    // Notify client
    SendNotifyMsg(NULL, iClientH, DEF_NOTIFY_QUESTABORTED, ...);
}
```

Triggered by client message: `DEF_COMMONTYPE_REQUEST_CANCELQUEST`

### _ClearQuestStatus Helper

`_ClearQuestStatus()` (Game.cpp:38415-38424):

```cpp
void CGame::_ClearQuestStatus(int iClientH)
{
    m_pClientList[iClientH]->m_iQuest = NULL;
    m_pClientList[iClientH]->m_iQuestID = NULL;
    m_pClientList[iClientH]->m_iQuestRewardType = NULL;
    m_pClientList[iClientH]->m_iQuestRewardAmount = NULL;
    m_pClientList[iClientH]->m_bIsQuestCompleted = FALSE;
}
```

### System-Initiated Abort

Quest is automatically aborted when:

1. **Quest ID Mismatch** - Quest configuration changed on server restart
2. **Quest Index 35-40** - Reserved/deprecated quest indices
3. **Server-side validation failure**

All cases send `DEF_NOTIFY_QUESTABORTED` to client.

---

## Crusade Mode Quests

### Crusade Quest Assignment

During Crusade mode (`m_bIsCrusadeMode == TRUE`):

- Only quests with `m_iAssignType == 1` are available
- Normal quests (`m_iAssignType == -1`) are blocked
- Special quest type 10 (Strategic Strike) provides teleport

### Strategic Strike Quest (Type 10)

`QuestAcceptedHandler()` (Game.cpp:38091-38120):

```cpp
void CGame::QuestAcceptedHandler(int iClientH)
{
    // Crusade quest special handling
    if (m_pQuestConfigList[questIndex]->m_iAssignType == 1) {
        switch (m_pQuestConfigList[questIndex]->m_iType) {
        case 10: // Strategic Strike - one-time teleport
            _ClearQuestStatus(iClientH);
            RequestTeleportHandler(iClientH, "2   ",
                                   quest->m_cTargetName,
                                   quest->m_sX,
                                   quest->m_sY);
            return;
        }
    }

    // Normal quest acceptance
    m_pClientList[iClientH]->m_iQuest = m_iAskedQuest;
    m_pClientList[iClientH]->m_iQuestID = quest->m_iQuestID;
    m_pClientList[iClientH]->m_iCurQuestCount = 0;
    m_pClientList[iClientH]->m_bIsQuestCompleted = FALSE;

    _CheckQuestEnvironment(iClientH);
    _SendQuestContents(iClientH);
}
```

---

## Response Modes

The `m_iResponseMode` field determines the dialog interface:

| Mode | Value | Description |
|------|-------|-------------|
| OK | 0 | Single "OK" button (info only) |
| Accept/Decline | 1 | Two buttons for quest offer |
| Next | 2 | "Next" button for multi-page dialog |

Most quests use mode 1 (Accept/Decline).

---

## Network Messages

### Client to Server

| Message | Constant | Description |
|---------|----------|-------------|
| Talk to NPC | `DEF_COMMONTYPE_TALKTONPC` | Initiates NPC dialog |
| Accept Quest | `DEF_COMMONTYPE_QUESTACCEPTED` | Player accepts offered quest |
| Cancel Quest | `DEF_COMMONTYPE_REQUEST_CANCELQUEST` | Player abandons current quest |

### Server to Client

| Message | Constant | Value | Description |
|---------|----------|-------|-------------|
| Quest Contents | `DEF_NOTIFY_QUESTCONTENTS` | 0x0B66 | Send active quest details |
| Quest Aborted | `DEF_NOTIFY_QUESTABORTED` | 0x0B67 | Quest was cancelled/aborted |
| Quest Completed | `DEF_NOTIFY_QUESTCOMPLETED` | 0x0B68 | Objective achieved |
| Quest Reward | `DEF_NOTIFY_QUESTREWARD` | 0x0B69 | Reward given |
| Quest Counter | `DEF_NOTIFY_QUESTCOUNTER` | 0x0BE2 | Progress update (remaining kills) |
| NPC Talk | `DEF_NOTIFY_NPCTALK` | 0x0B57 | Quest offer or NPC message |

### _SendQuestContents Format

`_SendQuestContents()` (Game.cpp:38123-38153):

```cpp
void CGame::_SendQuestContents(int iClientH)
{
    if (m_pClientList[iClientH]->m_iQuest == NULL) {
        // No active quest
        SendNotifyMsg(iClientH, DEF_NOTIFY_QUESTCONTENTS,
                      NULL, NULL, NULL, NULL, ...);
    }
    else {
        // Send quest details
        SendNotifyMsg(iClientH, DEF_NOTIFY_QUESTCONTENTS,
                      iWho,           // Quest giver NPC type
                      iQuestType,     // Quest type
                      iContribution,  // Contribution reward
                      NULL,
                      iTargetType,    // Monster type to kill
                      iTargetCount,   // Kill count required
                      iX, iY,         // Target coordinates
                      iRange,         // GOPLACE range
                      iQuestCompleted,// Completion status
                      cTargetName);   // Target map name
    }
}
```

---

## Persistence

Quest state is saved and loaded with player data.

### Saved Fields (Game.cpp:7602-7632)

```
character-quest-number = [m_iQuest]
character-quest-ID = [m_iQuestID]
current-quest-count = [m_iCurQuestCount]
quest-reward-type = [m_iQuestRewardType]
quest-reward-amount = [m_iQuestRewardAmount]
character-contribution = [m_iContribution]
character-quest-completed = [m_bIsQuestCompleted]
```

### Loaded Fields (Game.cpp:7125-7133)

Read mode mappings:
- Mode 56: `character-quest-number` -> `m_iQuest`
- Mode 57: `current-quest-count` -> `m_iCurQuestCount`
- Mode 59: `quest-reward-type` -> `m_iQuestRewardType`
- Mode 60: `quest-reward-amount` -> `m_iQuestRewardAmount`
- Mode 61: `character-contribution` -> `m_iContribution`
- Mode 62: `character-quest-ID` -> `m_iQuestID`
- Mode 63: `character-quest-completed` -> `m_bIsQuestCompleted`

### Post-Load Validation

On player login/init (Game.cpp:2035-2036, 4617-4621):

```cpp
// Send quest state to client
_SendQuestContents(iClientH);
_CheckQuestEnvironment(iClientH);

// Update quest counter
if (m_pClientList[iClientH]->m_iQuest != NULL) {
    cQuestRemain = m_pQuestConfigList[questIndex]->m_iMaxCount - m_iCurQuestCount;
    SendNotifyMsg(iClientH, DEF_NOTIFY_QUESTCOUNTER, cQuestRemain, ...);
    _bCheckIsQuestCompleted(iClientH);
}
```

---

## All Related Functions

### CGame Class Quest Functions

| Function | Location | Description |
|----------|----------|-------------|
| `_bDecodeQuestConfigFileContents()` | 37651-38024 | Parse quest configuration |
| `__iSearchForQuest()` | 38027-38088 | Find eligible quests for player |
| `QuestAcceptedHandler()` | 38091-38120 | Handle quest acceptance |
| `_SendQuestContents()` | 38123-38153 | Send quest state to client |
| `_CheckQuestEnvironment()` | 38155-38198 | Validate quest and set location flag |
| `_bCheckIsQuestCompleted()` | 38200-38238 | Check if quest objective is met |
| `_ClearQuestStatus()` | 38415-38424 | Clear all quest fields |
| `CancelQuestHandler()` | 41102-41110 | Handle quest cancellation |
| `NpcTalkHandler()` | 35691-35743 | Main NPC interaction handler |
| `_iTalkToNpcResult_Cityhall()` | 37510-37599 | City Hall quest logic |
| `_iTalkToNpcResult_Guard()` | 37602-37648 | Guard NPC messages |
| `_iTalkToNpcResult_GuildHall()` | 38240-38243 | Stub (returns -4) |
| `_iTalkToNpcResult_GShop()` | 38245-38248 | Stub (returns -4) |
| `_iTalkToNpcResult_BSmith()` | 38250-38253 | Stub (returns -4) |
| `_iTalkToNpcResult_WHouse()` | 38255-38258 | Stub (returns -4) |
| `_iTalkToNpcResult_WTower()` | 38260-38263 | Stub (returns -4) |

### Quest-Related Code Locations

| Purpose | File | Lines |
|---------|------|-------|
| Quest on NPC kill | Game.cpp | 10784-10800 |
| Quest on player init | Game.cpp | 2035-2036, 4617-4621 |
| Quest on teleport | Game.cpp | 20585-20589 |
| Quest config loading | Game.cpp | 11486-11489 |
| Quest accept message | Game.cpp | 11707-11711 |
| Quest cancel message | Game.cpp | 11683-11686 |

---

## Constants Reference

### Quest Type Constants (Quest.h)

```cpp
#define DEF_QUESTTYPE_MONSTERHUNT               1
#define DEF_QUESTTYPE_MONSTERHUNT_TIMELIMIT     2
#define DEF_QUESTTYPE_ASSASSINATION             3
#define DEF_QUESTTYPE_DELIVERY                  4
#define DEF_QUESTTYPE_ESCORT                    5
#define DEF_QUESTTYPE_GUARD                     6
#define DEF_QUESTTYPE_GOPLACE                   7
#define DEF_QUESTTYPE_BUILDSTRUCTURE            8
#define DEF_QUESTTYPE_SUPPLYBUILDSTRUCTURE      9
#define DEF_QUESTTYPE_STRATEGICSTRIKE           10
#define DEF_QUESTTYPE_SENDTOBATTLE              11
#define DEF_QUESTTYPE_SETOCCUPYFLAG             12
```

### System Constants (Game.h)

```cpp
#define DEF_MAXQUESTTYPE    200  // Maximum quest definitions
```

### Message Constants (NetMessages.h)

```cpp
// Server -> Client Notifications
#define DEF_NOTIFY_QUESTCONTENTS    0x0B66
#define DEF_NOTIFY_QUESTABORTED     0x0B67
#define DEF_NOTIFY_QUESTCOMPLETED   0x0B68
#define DEF_NOTIFY_QUESTREWARD      0x0B69
#define DEF_NOTIFY_QUESTCOUNTER     0x0BE2
#define DEF_NOTIFY_NPCTALK          0x0B57

// Client -> Server Commands
#define DEF_COMMONTYPE_QUESTACCEPTED           0x0A22
#define DEF_COMMONTYPE_REQUEST_CANCELQUEST     0x0A50
#define DEF_COMMONTYPE_TALKTONPC               0x0A1A

// Configuration
#define MSGID_QUESTCONFIGURATIONCONTENTS       0x0FA40001
```

---

## Flow Diagrams

### Quest Assignment Flow

```
Player talks to City Hall NPC
           |
           v
    NpcTalkHandler(iWho=4)
           |
           v
_iTalkToNpcResult_Cityhall()
           |
    +------+------+
    |             |
    v             v
Has Quest?     No Quest
    |             |
    v             v
Completed?   __iSearchForQuest()
    |             |
    v             v
Give Reward   Found Quest?
    |             |
    v             v
Clear Quest   Store as m_iAskedQuest
              Send DEF_NOTIFY_NPCTALK
```

### Quest Acceptance Flow

```
Client sends DEF_COMMONTYPE_QUESTACCEPTED
           |
           v
    QuestAcceptedHandler()
           |
           v
    Crusade Quest Type 10?
           |
    +------+------+
    |             |
    v             v
   Yes           No
    |             |
    v             v
Teleport     Normal Accept
Clear Quest  Set m_iQuest = m_iAskedQuest
             Set m_iQuestID
             Set m_iCurQuestCount = 0
             _CheckQuestEnvironment()
             _SendQuestContents()
```

### Quest Progress Flow (Monster Hunt)

```
Player kills NPC
       |
       v
NpcKillHandler()
       |
       v
Has Quest & Type = MONSTERHUNT?
       |
       v
On correct map (m_bQuestMatchFlag_Loc)?
       |
       v
Killed correct monster type?
       |
       v
Increment m_iCurQuestCount
       |
       v
Send DEF_NOTIFY_QUESTCOUNTER
       |
       v
_bCheckIsQuestCompleted()
       |
       v
Count >= MaxCount?
       |
       v
Set m_bIsQuestCompleted = TRUE
Send DEF_NOTIFY_QUESTCOMPLETED
```

### Quest Completion Flow

```
Player talks to City Hall (with completed quest)
           |
           v
_iTalkToNpcResult_Cityhall()
           |
           v
m_bIsQuestCompleted == TRUE?
           |
           v
Determine reward type
           |
    +------+------+------+
    |      |      |      |
    v      v      v      v
Item    Exp    Scaled  Contrib
 >0     =-1    Exp=-2   Only=0
    |      |      |      |
    v      v      v      v
Create  Add to  Calc &  Just add
Item    ExpStock Add    contrib
           |
           v
Add contribution points
           |
           v
Send DEF_NOTIFY_QUESTREWARD
           |
           v
_ClearQuestStatus()
```

---

## Monster Type Reference (from NPC.cfg)

Common quest targets based on the configuration:

| m_sType | Name | Notes |
|---------|------|-------|
| 10 | Slime | Low level |
| 11 | Skeleton | Low-mid level |
| 12 | Stone-Golem | Mid level |
| 17 | Scorpion | Mid level |
| 18 | Zombie | Mid level |
| 28 | Troll | Mid-high level |
| 29 | Orge | High level |
| 31 | Demon | Boss-tier |
| 33 | WereWolf | High level |
| 54 | Dark-Elf | Mid-high level |
| 59 | Ettin | High level |
| 63 | Frost | High level (Ice) |
| 65 | Ice-Golem | High level (Ice) |

---

## Implementation Notes

### Limitations

1. **Single Quest Slot** - Players can only have one active quest
2. **No Quest Log** - Quests are not tracked historically
3. **Simple Objectives** - Only kill counts and location visits implemented
4. **No Quest Chains** - Quests are independent, no prerequisites beyond contribution
5. **Limited NPCs** - Only City Hall gives quests despite multiple NPC handler stubs

### Potential Issues

1. **Quest ID Validation** - If server restarts with modified Quest.cfg, player quests may be aborted
2. **No Progress Save on Kill** - Progress only saved on character save intervals
3. **Hard-coded Quest Abort** - Indices 35-40 are always aborted (reserved?)

### Suggested Modernization

1. Implement quest log/history
2. Support multiple active quests
3. Add quest chains and prerequisites
4. Implement remaining quest types (delivery, escort, etc.)
5. Add timed quest support
6. Enable additional NPC quest givers
7. Add repeatable/daily quest support
