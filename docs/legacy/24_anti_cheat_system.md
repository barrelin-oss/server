# Legacy Anti-Cheat System Documentation

## Table of Contents

1. [Overview](#overview)
2. [Constants and Limits](#constants-and-limits)
3. [Data Structures](#data-structures)
   - [CClient Anti-Cheat Fields](#cclient-anti-cheat-fields)
4. [Anti-Cheat Mechanisms](#anti-cheat-mechanisms)
   - [Speed Hack Detection](#speed-hack-detection)
   - [Attack Frequency Validation](#attack-frequency-validation)
   - [Magic Casting Validation](#magic-casting-validation)
   - [Movement Validation](#movement-validation)
5. [Data Integrity Checks](#data-integrity-checks)
   - [Experience Validation](#experience-validation)
   - [Skill Point Validation](#skill-point-validation)
   - [Weapon Skill Checks](#weapon-skill-checks)
6. [Memory/Hex Edit Detection](#memoryhex-edit-detection)
7. [Behavior Pattern Detection](#behavior-pattern-detection)
   - [Logout Hack Detection](#logout-hack-detection)
   - [Traveler Level Hack](#traveler-level-hack)
   - [Resurrection Hack](#resurrection-hack)
   - [Guild Teleport Hack](#guild-teleport-hack)
   - [Slate Creation Hack](#slate-creation-hack)
   - [Fullswing Skill Hack](#fullswing-skill-hack)
8. [Session Management](#session-management)
   - [Client Timeout](#client-timeout)
   - [IP Address Tracking](#ip-address-tracking)
9. [Logging System](#logging-system)
10. [Penalty System](#penalty-system)
11. [Admin Bypass](#admin-bypass)
12. [Implementation Notes](#implementation-notes)

---

## Overview

The Helbreath anti-cheat system provides server-side validation to detect and prevent various forms of cheating and exploits. Key features:

- **Speed hack detection** for movement, attacks, and magic casting
- **Frequency validation** to prevent packet flooding and automation
- **Data integrity checks** for experience, skills, and items
- **Behavior pattern detection** for known exploits
- **IP and session tracking** for audit trails
- **Centralized logging** via `PutHackLogFileList()`
- **Immediate disconnection** for detected violations

The anti-cheat system operates entirely server-side, making it resistant to client-side tampering. All timing checks use `timeGetTime()` for millisecond-precision timestamps.

---

## Constants and Limits

### Game.h

```cpp
#define DEF_CLIENTTIMEOUT        10000    // Client timeout in milliseconds (10 seconds)
```

### Timing Thresholds (Hardcoded in Game.cpp)

| Check | Minimum Interval | Description |
|-------|------------------|-------------|
| Walk speed | 590ms | Minimum time between walk messages |
| Run speed | 290ms | Minimum time between run messages |
| Attack (weapon) | 450ms | Minimum time between weapon attacks |
| Attack (batch) | 3500ms | 7 attacks must take at least 3.5 seconds |
| Magic cast | 1500ms | Minimum time between spell completions |
| Attack → Magic | 1000ms | Minimum delay from attack to magic |
| Recent attack | 100ms | Minimum time between any attacks |

---

## Data Structures

### CClient Anti-Cheat Fields

Located in `Client.h` (lines 48-55 and 290-311):

```cpp
// Frequency tracking
DWORD m_dwMagicFreqTime;          // Last magic cast timestamp
DWORD m_dwMoveFreqTime;           // Last movement timestamp
DWORD m_dwAttackFreqTime;         // Last attack timestamp (client-reported)
BOOL m_bIsMoveBlocked;            // Movement was blocked (reset timing)
BOOL m_bMagicItem;                // Using magic item (bypass some checks)
DWORD dwClientTime;               // Client-reported timestamp
BOOL m_bMagicConfirm;             // Magic cast confirmed (anti-bypass)
int m_iSpellCount;                // Active spell count (precast tracking)
BOOL m_bMagicPauseTime;           // Magic in pause state

// Message counters (batched frequency checks)
int m_iMoveMsgRecvCount;          // Walk message counter (resets at 3)
int m_iAttackMsgRecvCount;        // Attack message counter (resets at 7)
int m_iRunMsgRecvCount;           // Run message counter (resets at 3)
int m_iSkillMsgRecvCount;         // Skill message counter

// Latency tracking
DWORD m_dwMoveLAT;                // Last movement check timestamp
DWORD m_dwRunLAT;                 // Last run check timestamp
DWORD m_dwAttackLAT;              // Last attack batch timestamp

// Session timing
DWORD m_dwTime;                   // Last activity timestamp
DWORD m_dwLastActionTime;         // Last action timestamp
DWORD m_dwRecentAttackTime;       // Most recent attack timestamp

// Anti-cheat specific
DWORD m_dwSpeedHackCheckTime;     // Speed hack check timestamp
int m_iSpeedHackCheckExp;         // Expected experience for speed check
DWORD m_dwLogoutHackCheck;        // Logout hack detection timestamp

// Other tracking
char m_cIPaddress[21];            // Client IP address
int m_iAbuseCount;                // Abuse/hack attempt counter
```

**Field Initialization** (Client.cpp, lines 46 and 257-267):

```cpp
m_dwLogoutHackCheck = 0;

// ...

m_iAttackMsgRecvCount = 0;

// ...

m_dwMoveLAT = m_dwRunLAT = m_dwAttackLAT = 0;
```

---

## Anti-Cheat Mechanisms

### Speed Hack Detection

The server uses multiple timing checks to detect speed hacks. Each mechanism validates that actions occur at physically possible rates.

#### Movement Speed Check

**Location**: `Game.cpp`, lines 1110-1164 (currently commented out)

```cpp
// Walking speed check (batched: every 3 messages)
if (bIsBlocked == FALSE) m_pClientList[iClientH]->m_iMoveMsgRecvCount++;
if (m_pClientList[iClientH]->m_iMoveMsgRecvCount >= 3) {
    if (m_pClientList[iClientH]->m_dwMoveLAT != 0) {
        if ((dwTime - m_pClientList[iClientH]->m_dwMoveLAT) < 590) {
            // Speed hack detected - 3 walks in under 590ms
            bIsBlocked = TRUE;
        }
    }
    m_pClientList[iClientH]->m_dwMoveLAT = dwTime;
    m_pClientList[iClientH]->m_iMoveMsgRecvCount = 0;
}

// Running speed check (batched: every 3 messages)
if (bIsBlocked == FALSE) m_pClientList[iClientH]->m_iRunMsgRecvCount++;
if (m_pClientList[iClientH]->m_iRunMsgRecvCount >= 3) {
    if (m_pClientList[iClientH]->m_dwRunLAT != 0) {
        if ((dwTime - m_pClientList[iClientH]->m_dwRunLAT) < 290) {
            // Speed hack detected - 3 runs in under 290ms
            bIsBlocked = TRUE;
        }
    }
    m_pClientList[iClientH]->m_dwRunLAT = dwTime;
    m_pClientList[iClientH]->m_iRunMsgRecvCount = 0;
}
```

**Note**: This code is currently commented out in the production build but demonstrates the detection approach.

### Attack Frequency Validation

#### bCheckClientAttackFrequency()

**Location**: `Game.cpp`, lines 45370-45403

Validates weapon attack timing using client-reported timestamps.

```cpp
BOOL CGame::bCheckClientAttackFrequency(int iClientH, DWORD dwClientTime)
{
    DWORD dwTimeGap;

    if (m_pClientList[iClientH] == NULL) return FALSE;
    if (m_pClientList[iClientH]->m_iAdminUserLevel > 0) return FALSE;  // Admin bypass

    if (m_pClientList[iClientH]->m_dwAttackFreqTime == NULL)
        m_pClientList[iClientH]->m_dwAttackFreqTime = dwClientTime;
    else {
        dwTimeGap = dwClientTime - m_pClientList[iClientH]->m_dwAttackFreqTime;
        m_pClientList[iClientH]->m_dwAttackFreqTime = dwClientTime;

        if (dwTimeGap < 450) {
            // Attack speed too fast - disconnect
            wsprintf(G_cTxt, "Swing Hack: (%s) Player: (%s) - attacking with weapon at irregular rates.",
                     m_pClientList[iClientH]->m_cIPaddress,
                     m_pClientList[iClientH]->m_cCharName);
            PutHackLogFileList(G_cTxt);
            DeleteClient(iClientH, TRUE, TRUE);
            return FALSE;
        }
    }
    return FALSE;
}
```

#### Attack Message Counter

**Location**: `Game.cpp`, lines 9746-9756

Validates attack frequency using batched message counting.

```cpp
m_pClientList[iClientH]->m_dwLastActionTime = dwTime;
m_pClientList[iClientH]->m_iAttackMsgRecvCount++;
if (m_pClientList[iClientH]->m_iAttackMsgRecvCount >= 7) {
    if (m_pClientList[iClientH]->m_dwAttackLAT != 0) {
        if ((dwTime - m_pClientList[iClientH]->m_dwAttackLAT) < 3500) {
            // 7 attacks in under 3.5 seconds - disconnect
            DeleteClient(iClientH, TRUE, TRUE, TRUE);
            return 0;
        }
    }
    m_pClientList[iClientH]->m_dwAttackLAT = dwTime;
    m_pClientList[iClientH]->m_iAttackMsgRecvCount = 0;
}
```

#### Recent Attack Validation

**Location**: `Game.cpp`, lines 9846-9883

Prevents attacks faster than 100ms apart.

```cpp
if ((wType != 0) && ((dwTime - m_pClientList[iClientH]->m_dwRecentAttackTime) > 100)) {
    // Process attack...
    iExp += iCalculateAttackEffect(...);
    m_pClientList[iClientH]->m_dwRecentAttackTime = dwTime;
}
```

### Magic Casting Validation

#### bCheckClientMagicFrequency()

**Location**: `Game.cpp`, lines 45405-45440

Validates magic casting speed.

```cpp
BOOL CGame::bCheckClientMagicFrequency(int iClientH, DWORD dwClientTime)
{
    DWORD dwTimeGap;

    if (m_pClientList[iClientH] == NULL) return FALSE;

    if (m_pClientList[iClientH]->m_dwMagicFreqTime == NULL)
        m_pClientList[iClientH]->m_dwMagicFreqTime = dwClientTime;
    else {
        dwTimeGap = dwClientTime - m_pClientList[iClientH]->m_dwMagicFreqTime;
        m_pClientList[iClientH]->m_dwMagicFreqTime = dwClientTime;

        // Requires m_bMagicConfirm to prevent false positives
        if ((dwTimeGap < 1500) && (m_pClientList[iClientH]->m_bMagicConfirm == TRUE)) {
            wsprintf(G_cTxt, "Speed Cast: (%s) Player: (%s) - casting magic at irregular rates.",
                     m_pClientList[iClientH]->m_cIPaddress,
                     m_pClientList[iClientH]->m_cCharName);
            PutHackLogFileList(G_cTxt);
            DeleteClient(iClientH, TRUE, TRUE);
            return FALSE;
        }

        m_pClientList[iClientH]->m_iSpellCount--;
        m_pClientList[iClientH]->m_bMagicConfirm = FALSE;
        m_pClientList[iClientH]->m_bMagicPauseTime = FALSE;
    }
    return FALSE;
}
```

#### Cast Delay Validation

**Location**: `Game.cpp`, lines 1054-1064

Detects casting without proper delay.

```cpp
else if (m_pClientList[iClientH]->m_bMagicPauseTime == TRUE) {
    wsprintf(G_cTxt, "Cast Delay Hack: (%s) Player: (%s) - player casting too fast.",
             m_pClientList[iClientH]->m_cIPaddress,
             m_pClientList[iClientH]->m_cCharName);
    PutHackLogFileList(G_cTxt);
    DeleteClient(iClientH, TRUE, TRUE);
}
```

#### Attack-to-Magic Delay

**Location**: `Game.cpp`, lines 17064-17077

Validates transition from attack to magic casting.

```cpp
if (((dwTime - m_pClientList[iClientH]->m_dwRecentAttackTime) < 1000) && (bItemEffect == 0)) {
    wsprintf(G_cTxt, "3.51 Detection: (%s) Player: (%s) - Magic casting speed is too fast! Hack?",
             m_pClientList[iClientH]->m_cIPaddress,
             m_pClientList[iClientH]->m_cCharName);
    PutHackLogFileList(G_cTxt);
    DeleteClient(iClientH, TRUE, TRUE);
    return;
}
m_pClientList[iClientH]->m_dwRecentAttackTime = dwTime;
```

#### TSearch Spell Hack Detection

**Location**: `Game.cpp`, lines 17105-17116

Detects memory-edited spell casting.

```cpp
if ((m_pClientList[iClientH]->m_iSpellCount > 1) && (bItemEffect == FALSE)) {
    // More than 1 pending spell without proper precasting
    wsprintf(G_cTxt, "TSearch Spell Hack: (%s) Player: (%s) - casting magic without precasting.",
             m_pClientList[iClientH]->m_cIPaddress,
             m_pClientList[iClientH]->m_cCharName);
    PutHackLogFileList(G_cTxt);
    DeleteClient(iClientH, TRUE, TRUE);
    return;
}
```

### Movement Validation

#### bCheckClientMoveFrequency()

**Location**: `Game.cpp`, lines 45442-45486

Validates movement speed.

```cpp
BOOL CGame::bCheckClientMoveFrequency(int iClientH, DWORD dwClientTime)
{
    DWORD dwTimeGap;

    if (m_pClientList[iClientH] == NULL) return FALSE;
    if (m_pClientList[iClientH]->m_iAdminUserLevel > 0) return FALSE;  // Admin bypass

    if (m_pClientList[iClientH]->m_dwMoveFreqTime == NULL)
        m_pClientList[iClientH]->m_dwMoveFreqTime = dwClientTime;
    else {
        // Reset timing if movement was blocked
        if (m_pClientList[iClientH]->m_bIsMoveBlocked == TRUE) {
            m_pClientList[iClientH]->m_dwMoveFreqTime = NULL;
            m_pClientList[iClientH]->m_bIsMoveBlocked = FALSE;
            return FALSE;
        }

        // Reset timing if attack mode changed
        if (m_pClientList[iClientH]->m_bIsAttackModeChange == TRUE) {
            m_pClientList[iClientH]->m_dwMoveFreqTime = NULL;
            m_pClientList[iClientH]->m_bIsAttackModeChange = FALSE;
            return FALSE;
        }

        dwTimeGap = dwClientTime - m_pClientList[iClientH]->m_dwMoveFreqTime;
        m_pClientList[iClientH]->m_dwMoveFreqTime = dwClientTime;

        if ((dwTimeGap < 200) && (dwTimeGap >= 0)) {
            wsprintf(G_cTxt, "Speed Hack: (%s) Player: (%s) - running too fast.",
                     m_pClientList[iClientH]->m_cIPaddress,
                     m_pClientList[iClientH]->m_cCharName);
            PutHackLogFileList(G_cTxt);
            DeleteClient(iClientH, TRUE, TRUE);
            return FALSE;
        }
    }
    return FALSE;
}
```

---

## Data Integrity Checks

### Experience Validation

**Location**: `Game.cpp`, lines 4574-4586

Validates that character experience matches their level.

```cpp
if ((m_pClientList[iClientH]->m_iLevel > 2) &&
    (m_pClientList[iClientH]->m_iAdminUserLevel == 0) &&
    (m_pClientList[iClientH]->m_iExp < iGetLevelExp(m_pClientList[iClientH]->m_iLevel - 1) - 3000)) {

    wsprintf(G_cTxt, "Data Error: (%s) Player: (%s) CurrentExp: %d --- Minimum Exp: %d",
             m_pClientList[iClientH]->m_cIPaddress,
             m_pClientList[iClientH]->m_cCharName,
             m_pClientList[iClientH]->m_iExp,
             (iGetLevelExp(m_pClientList[iClientH]->m_iLevel) - 1));
    PutHackLogFileList(G_cTxt);
    DeleteClient(iClientH, TRUE, TRUE);
    return;
}
```

**Logic**: If a player above level 2 has less experience than required for (level - 1), their data has been tampered with.

### Skill Point Validation

**Location**: `Game.cpp`, lines 4588-4602

Validates total skill points don't exceed the limit.

```cpp
iTotalPoints = 0;
for (i = 0; i < DEF_MAXSKILLTYPE; i++)
    iTotalPoints += m_pClientList[iClientH]->m_cSkillMastery[i];

if ((iTotalPoints - 21 > m_sCharSkillLimit) && (m_pClientList[iClientH]->m_iAdminUserLevel == 0)) {
    wsprintf(G_cTxt, "Packet Editing: (%s) Player: (%s) - has more than allowed skill points (%d).",
             m_pClientList[iClientH]->m_cIPaddress,
             m_pClientList[iClientH]->m_cCharName,
             iTotalPoints);
    PutHackLogFileList(G_cTxt);
    DeleteClient(iClientH, TRUE, TRUE);
    return;
}
```

**Note**: The `-21` offset accounts for starting skill points that don't count toward the limit.

### Weapon Skill Checks

#### Fullswing Skill Validation

**Location**: `Game.cpp`, lines 51705-51717

Validates that dash/fullswing attacks require 100% weapon skill mastery.

```cpp
sSkillUsed = m_pClientList[sAttackerH]->m_sUsingWeaponSkill;
if ((bIsDash == TRUE) &&
    (m_pClientList[sAttackerH]->m_cSkillMastery[sSkillUsed] != 100) &&
    (wWeaponType != 25) && (wWeaponType != 27)) {

    wsprintf(G_cTxt, "TSearch Fullswing Hack: (%s) Player: (%s) - dashing with only (%d) weapon skill.",
             m_pClientList[sAttackerH]->m_cIPaddress,
             m_pClientList[sAttackerH]->m_cCharName,
             m_pClientList[sAttackerH]->m_cSkillMastery[sSkillUsed]);
    PutHackLogFileList(G_cTxt);
    DeleteClient(sAttackerH, TRUE, TRUE);
    return 0;
}
```

**Exception**: Weapon types 25 and 27 (specific bow types) are exempt from this check.

---

## Memory/Hex Edit Detection

### Connection Check Byte (0x3203203)

**Location**: `Game.cpp`, lines 925-931 (commented out) and 11219-11221

Detects removal of client integrity check bytes.

```cpp
// Version 1: Connection counter check
m_pClientList[iClientH]->m_cConnectionCheck++;
if (m_pClientList[iClientH]->m_cConnectionCheck > 50) {
    wsprintf(G_cTxt, "Hex: (%s) Player: (%s) - removed 03203203h, vital to hack detection.",
             m_pClientList[iClientH]->m_cIPaddress,
             m_pClientList[iClientH]->m_cCharName);
    PutHackLogFileList(G_cTxt);
    DeleteClient(iClientH, TRUE, TRUE);
    return;
}

// Version 2: Direct detection
wsprintf(G_cTxt, "Client Hex Edit: (%s) Player: (%s) - has removed 3203203 (check connection handler).",
         m_pClientList[iClientH]->m_cIPaddress,
         m_pClientList[iClientH]->m_cCharName);
PutHackLogFileList(G_cTxt);
```

**Note**: The `0x3203203` value is a magic number embedded in the client that the server expects to see periodically.

---

## Behavior Pattern Detection

### Logout Hack Detection

**Location**: `Game.cpp`, lines 368-377 and 27693, 28178, 28612, 51987

Detects players disconnecting immediately after taking damage (combat logging).

```cpp
// On socket close
if ((dwTime - m_pClientList[iClientH]->m_dwLogoutHackCheck) < 1000) {
    wsprintf(G_cTxt, "Logout Hack: (%s) Player: (%s) - disconnected within 10 seconds of most recent damage. Hack? Lag?",
             m_pClientList[iClientH]->m_cIPaddress,
             m_pClientList[iClientH]->m_cCharName);
    PutHackLogFileList(G_cTxt);
}

// When player takes damage, update timestamp
m_pClientList[sTargetH]->m_dwLogoutHackCheck = dwTime;
```

**Threshold**: Disconnecting within 1000ms (1 second) of taking damage triggers the detection. The log message says "10 seconds" but the code uses 1000ms.

### Traveler Level Hack

**Location**: `Game.cpp`, lines 3760-3769

Detects traveler characters exceeding level 19.

```cpp
if ((strcmp(m_pClientList[i]->m_cLocation, "elvine") != 0) &&
    (strcmp(m_pClientList[i]->m_cLocation, "elvhunter") != 0) &&
    (strcmp(m_pClientList[i]->m_cLocation, "arehunter") != 0) &&
    (strcmp(m_pClientList[i]->m_cLocation, "aresden") != 0) &&
    (m_pClientList[i]->m_iLevel >= 20) &&
    (m_pClientList[i]->m_iAdminUserLevel == 0)) {

    wsprintf(G_cTxt, "Traveller Hack: (%s) Player: (%s) is a traveller and is greater than level 19.",
             m_pClientList[i]->m_cIPaddress,
             m_pClientList[i]->m_cCharName);
    PutHackLogFileList(G_cTxt);
    DeleteClient(i, TRUE, TRUE);
}
```

**Logic**: Travelers (players who haven't joined a city) cannot exceed level 19. If they do, their data has been modified.

### Resurrection Hack

**Location**: `Game.cpp`, lines 45340-45347

Detects invalid resurrection attempts.

```cpp
// On invalid resurrection state
wsprintf(buff, "(!!!) Player(%s) Tried To Use Resurrection Hack", m_pClientList[iClientH]->m_cCharName);
PutHackLogFileList(G_cTxt);
DeleteClient(iClientH, TRUE, TRUE, TRUE, TRUE);
```

### Guild Teleport Hack

**Location**: `Game.cpp`, lines 45918-45945

Detects unauthorized guild teleport usage.

```cpp
// Check 1: Crusade mode must be active
if (m_bIsCrusadeMode != TRUE) {
    wsprintf(G_cTxt, "Accessing crusade teleport: (%s) Player: (%s) - setting teleport location when crusade is disabled.",
             m_pClientList[iClientH]->m_cIPaddress,
             m_pClientList[iClientH]->m_cCharName);
    PutHackLogFileList(G_cTxt);
    DeleteClient(iClientH, TRUE, TRUE);
    return;
}

// Check 2: Player must have crusade duty (guild membership)
if (m_pClientList[iClientH]->m_iCrusadeDuty == 0) {
    wsprintf(G_cTxt, "Accessing crusade teleport: (%s) Player: (%s) - teleporting when not in a guild",
             m_pClientList[iClientH]->m_cIPaddress,
             m_pClientList[iClientH]->m_cCharName);
    PutHackLogFileList(G_cTxt);
    DeleteClient(iClientH, TRUE, TRUE);
    return;
}
```

### Slate Creation Hack

**Location**: `Game.cpp`, lines 44925-44931

Detects attempting to create slates without required items.

```cpp
catch(...) {
    // Exception during slate creation indicates invalid items
    bIsSlatePresent = FALSE;
    SendNotifyMsg(NULL, iClientH, DEF_NOTIFY_SLATE_CREATEFAIL, NULL, NULL, NULL, NULL);
    wsprintf(G_cTxt, "TSearch Slate Hack: (%s) Player: (%s) - creating slates without correct item!",
             m_pClientList[iClientH]->m_cIPaddress,
             m_pClientList[iClientH]->m_cCharName);
    PutHackLogFileList(G_cTxt);
    DeleteClient(iClientH, TRUE, TRUE);
    return;
}
```

### Fullswing Skill Hack

**Location**: `Game.cpp`, lines 51705-51717

See [Weapon Skill Checks](#weapon-skill-checks) above.

---

## Session Management

### Client Timeout

**Location**: `Game.cpp`, lines 3533-3572

The `CheckClientResponseTime()` function runs every 3 seconds and disconnects inactive clients.

```cpp
void CGame::CheckClientResponseTime()
{
    DWORD dwTime = timeGetTime();

    for (i = 1; i < DEF_MAXCLIENTS; i++) {
        if (m_pClientList[i] != NULL) {

            if ((dwTime - m_pClientList[i]->m_dwTime) > DEF_CLIENTTIMEOUT) {
                if (m_pClientList[i]->m_bIsInitComplete == TRUE) {
                    // Initialized client timed out - save and disconnect
                    wsprintf(G_cTxt, "Client Timeout: %s", m_pClientList[i]->m_cIPaddress);
                    PutLogList(G_cTxt);
                    DeleteClient(i, TRUE, TRUE);
                }
                else if ((dwTime - m_pClientList[i]->m_dwTime) > DEF_CLIENTTIMEOUT) {
                    // Uninitialized client timed out - disconnect without save
                    DeleteClient(i, FALSE, FALSE);
                }
            }
            // ... other periodic checks ...
        }
    }
}
```

**Timeout Value**: `DEF_CLIENTTIMEOUT` = 10000ms (10 seconds)

### IP Address Tracking

**Location**: `Game.cpp`, lines 305-308

IP addresses are captured on connection and stored for logging.

```cpp
ZeroMemory(m_pClientList[i]->m_cIPaddress, sizeof(m_pClientList[i]->m_cIPaddress));
m_pClientList[i]->m_pXSock->iGetPeerAddress(m_pClientList[i]->m_cIPaddress);

wsprintf(G_cTxt, "<%d> Client Connected: (%s)", i, m_pClientList[i]->m_cIPaddress);
```

**IP Search Function**: `Game.cpp`, line 34309

```cpp
if ((m_pClientList[i] != NULL) && (memcmp(m_pClientList[i]->m_cIPaddress, cIP, strlen(cIP)) == 0)) {
    // Found client with matching IP
}
```

---

## Logging System

### PutHackLogFileList()

**Declaration**: `Game.cpp`, line 17 (extern reference)

```cpp
extern void PutHackLogFileList(char * cStr);
```

This function writes hack detection messages to a dedicated log file. All anti-cheat detections use this function.

### Log Message Format

All hack log messages follow a consistent format:

```
<Detection Type>: (<IP Address>) Player: (<Character Name>) - <Description>
```

**Examples**:

```
Swing Hack: (192.168.1.100) Player: (Hacker123) - attacking with weapon at irregular rates.
Speed Cast: (192.168.1.100) Player: (Hacker123) - casting magic at irregular rates.
Speed Hack: (192.168.1.100) Player: (Hacker123) - running too fast.
Logout Hack: (192.168.1.100) Player: (Hacker123) - disconnected within 10 seconds of most recent damage. Hack? Lag?
Traveller Hack: (192.168.1.100) Player: (Hacker123) is a traveller and is greater than level 19.
TSearch Spell Hack: (192.168.1.100) Player: (Hacker123) - casting magic without precasting.
TSearch Fullswing Hack: (192.168.1.100) Player: (Hacker123) - dashing with only (50) weapon skill.
Data Error: (192.168.1.100) Player: (Hacker123) CurrentExp: 1000 --- Minimum Exp: 50000
Packet Editing: (192.168.1.100) Player: (Hacker123) - has more than allowed skill points (800).
```

---

## Penalty System

### Immediate Disconnection

All detected hack attempts result in immediate disconnection via `DeleteClient()`.

**Typical call signature**:
```cpp
DeleteClient(iClientH, TRUE, TRUE);      // Save character, delete completely
DeleteClient(iClientH, TRUE, TRUE, TRUE); // Additional flag for severe violations
```

### Abuse Counter

**Location**: `Game.cpp`, lines 30377-30384 (commented out)

```cpp
// Abuse tracking for suspicious skill usage
m_pClientList[iClientH]->m_iAbuseCount++;
if ((m_pClientList[iClientH]->m_iAbuseCount % 30) == 0) {
    wsprintf(G_cTxt, "(!) Hack suspect (%s) Skill(%d) Tries(%d)",
             m_pClientList[iClientH]->m_cCharName,
             iV1,
             m_pClientList[iClientH]->m_iAbuseCount);
    PutLogFileList(G_cTxt);
}
```

**Note**: This code is commented out in production but shows the abuse tracking mechanism.

---

## Admin Bypass

Several anti-cheat checks include admin level bypass conditions:

```cpp
if (m_pClientList[iClientH]->m_iAdminUserLevel > 0) return FALSE;  // Skip check for admins
```

Admin bypass is present in:
- `bCheckClientAttackFrequency()` - Attack speed check
- `bCheckClientMoveFrequency()` - Movement speed check
- Experience validation check
- Skill point validation check
- Traveler level check

**Security Note**: Admin commands are logged separately:

```cpp
wsprintf(G_cTxt, "(%s) GM Order(%s): <command>",
         m_pClientList[iClientH]->m_cIPaddress,
         m_pClientList[iClientH]->m_cCharName);
```

---

## Implementation Notes

### Thread Safety

All anti-cheat checks run on the main game thread. No explicit locking is required.

### Timing Source

All timing uses `timeGetTime()` which provides millisecond precision on Windows.

```cpp
DWORD dwTime = timeGetTime();
```

### False Positive Prevention

Several mechanisms reduce false positives:

1. **Movement block reset**: When movement is blocked by collision, timing is reset
2. **Attack mode change reset**: Switching attack modes resets movement timing
3. **Magic confirm flag**: Magic speed checks require prior confirmation
4. **Batch checking**: Attack speed uses batches of 7 messages instead of per-message

### Known Cheat Tools Referenced

The code references several known cheat tools:
- **TSearch**: Memory editor used to modify client values
- **Speed Hack**: Generic term for programs that accelerate game speed
- **Hex Editor**: Used to modify client binary

### Detection Timing Summary

| Action | Per-Message Check | Batch Check |
|--------|-------------------|-------------|
| Walk | N/A (commented) | 3 messages / 590ms |
| Run | N/A (commented) | 3 messages / 290ms |
| Attack | 450ms min | 7 messages / 3500ms |
| Magic | 1500ms min | N/A |
| Attack→Magic | 1000ms min | N/A |
| Any Attack | 100ms min | N/A |

---

## Function Reference

### CGame Anti-Cheat Methods

| Function | Location | Description |
|----------|----------|-------------|
| `bCheckClientAttackFrequency()` | Game.cpp:45370 | Validate weapon attack timing |
| `bCheckClientMagicFrequency()` | Game.cpp:45405 | Validate magic casting timing |
| `bCheckClientMoveFrequency()` | Game.cpp:45442 | Validate movement timing |
| `CheckClientResponseTime()` | Game.cpp:3533 | Check client timeouts (runs every 3s) |
| `PutHackLogFileList()` | extern | Write hack detection to log file |

### Detection Locations in iClientMotion_Attack_Handler()

| Line Range | Check |
|------------|-------|
| 9746-9757 | Attack message batch counter |
| 9846 | Recent attack timestamp check |

### Detection Locations in Magic Handler

| Line Range | Check |
|------------|-------|
| 1052 | Magic frequency call |
| 1054-1064 | Cast delay hack |
| 17064-17077 | Attack→Magic timing |
| 17105-17116 | Spell count validation |

### Detection Locations During Character Load

| Line Range | Check |
|------------|-------|
| 4574-4586 | Experience validation |
| 4588-4602 | Skill point validation |

---

## Anti-Cheat Check Summary Table

| Detection Type | Threshold | Action | Admin Bypass |
|----------------|-----------|--------|--------------|
| Walk speed | 3 walks < 590ms | Block | N/A (commented) |
| Run speed | 3 runs < 290ms | Block | N/A (commented) |
| Attack frequency (per-attack) | < 450ms | Disconnect | Yes |
| Attack frequency (batch) | 7 attacks < 3500ms | Disconnect | No |
| Recent attack | < 100ms | Skip attack | No |
| Magic cast speed | < 1500ms | Disconnect | No |
| Attack→Magic delay | < 1000ms | Disconnect | No |
| Movement speed | < 200ms | Disconnect | Yes |
| Spell count | > 1 pending | Disconnect | No |
| Cast delay | In pause state | Disconnect | No |
| Experience mismatch | Below level minimum | Disconnect | Yes |
| Skill points | Above limit | Disconnect | Yes |
| Fullswing skill | < 100% mastery | Disconnect | No |
| Traveler level | >= 20 | Disconnect | Yes |
| Logout timing | < 1000ms after damage | Log only | No |
| Client timeout | > 10000ms inactive | Disconnect | No |
