# Legacy Party System Documentation

## Table of Contents

1. [Overview](#overview)
2. [Constants and Limits](#constants-and-limits)
3. [Data Structures](#data-structures)
   - [CClient Party Fields](#cclient-party-fields)
   - [CGame Party Info Structure](#cgame-party-info-structure)
4. [Party Status State Machine](#party-status-state-machine)
5. [Party Functions](#party-functions)
   - [Party Creation](#party-creation)
   - [Party Invitation and Joining](#party-invitation-and-joining)
   - [Party Member Queries](#party-member-queries)
   - [Party Dismissal and Leaving](#party-dismissal-and-leaving)
   - [Party Deletion](#party-deletion)
6. [Experience Distribution](#experience-distribution)
7. [Party Chat](#party-chat)
8. [Combat Party Checks](#combat-party-checks)
9. [Player Disconnect Handling](#player-disconnect-handling)
10. [Network Protocol](#network-protocol)
    - [Message IDs](#message-ids)
    - [Operation Codes](#operation-codes)
    - [Notification Sub-types](#notification-sub-types)
11. [Gate Server Communication](#gate-server-communication)
12. [Save/Load](#saveload)
13. [Implementation Notes](#implementation-notes)

---

## Overview

The Helbreath party system allows players to group together for coordinated gameplay. Key features:

- Maximum of **9 party members** (1 leader + 8 members)
- Experience sharing with bonus multipliers based on party size
- Party chat via `$` prefix
- Protection from friendly fire between party members
- Members must be on the same faction (side) to join
- Cross-server party tracking via Gate Server

The party system uses a distributed architecture:
- **Game Server**: Tracks local party members, handles local operations
- **Gate Server**: Maintains authoritative party membership across servers

---

## Constants and Limits

### Client.h

```cpp
#define DEF_MAXPARTYMEMBERS    9    // Maximum party members (including leader)
```

### NetMessages.h

```cpp
// Party operation message
#define MSGID_PARTYOPERATION           0x3C00123A

// Party status states
#define DEF_PARTYSTATUS_NULL           0    // Not in party, not processing
#define DEF_PARTYSTATUS_PROCESSING     1    // Waiting for Gate Server response
#define DEF_PARTYSTATUS_CONFIRM        2    // Confirmed party member

// Common type commands for client->server
#define DEF_COMMONTYPE_REQUEST_ACCEPTJOINPARTY    0x0A30    // Accept/reject party invite
#define DEF_COMMONTYPE_REQUEST_JOINPARTY          0x0A31    // Request to join party
#define DEF_COMMONTYPE_RESPONSE_JOINPARTY         0x0A32    // Response to join request

// Notification types for server->client
#define DEF_NOTIFY_RESPONSE_CREATENEWPARTY        0x0B80    // Party creation result
#define DEF_NOTIFY_QUERY_JOINPARTY                0x0B81    // Incoming party invite
#define DEF_NOTIFY_PARTY                          0x0BA2    // General party notification
```

---

## Data Structures

### CClient Party Fields

Located in `Client.h` (lines 346-358):

```cpp
// Party Stuff
int m_iPartyID;                    // Party identifier (0 = no party)
int m_iPartyStatus;                // Current party status (NULL/PROCESSING/CONFIRM)
int m_iReqJoinPartyClientH;        // Client handle of pending join requester
char m_cReqJoinPartyName[12];      // Character name of pending join requester

// Extended party info (partially commented out in legacy code)
int m_iPartyRank;                  // -1 = not in party, 0+ = party member rank
int m_iPartyMemberCount;           // Number of party members
int m_iPartyGUID;                  // Globally unique party identifier

struct {
    int iIndex;                    // Client handle of member
    char cName[11];                // Character name of member
} m_stPartyMemberName[DEF_MAXPARTYMEMBERS];
```

**Field Initialization** (Client.cpp, lines 184-196):

```cpp
m_iPartyID = 0;
m_iPartyStatus = 0;
m_iReqJoinPartyClientH = 0;
ZeroMemory(m_cReqJoinPartyName, sizeof(m_cReqJoinPartyName));

// Commented out in production:
// m_iPartyRank = -1;
// m_iPartyMemberCount = 0;
// m_iPartyGUID = 0;
```

### CGame Party Info Structure

Located in `Game.h` (lines 950-953):

```cpp
struct {
    int iTotalMembers;             // Current number of members in this party
    int iIndex[9];                 // Client handles of party members (indices)
} m_stPartyInfo[DEF_MAXCLIENTS];   // One party entry per possible client slot
```

**Initialization** (Game.cpp, lines 213-217):

```cpp
for (i = 0; i < DEF_MAXCLIENTS; i++) {
    m_stPartyInfo[i].iTotalMembers = 0;
    for (x = 0; x < DEF_MAXPARTYMEMBERS; x++)
        m_stPartyInfo[i].iIndex[x] = 0;
}
```

**Key Insight**: The `m_stPartyInfo` array is indexed by **party ID**, which is the same as the first member's client handle. This means party IDs are essentially client handles, making the maximum number of concurrent parties equal to `DEF_MAXCLIENTS`.

---

## Party Status State Machine

```
                    +-----------------+
                    |   NULL (0)      |
                    | Not in party    |
                    +--------+--------+
                             |
                             | RequestCreatePartyHandler()
                             | JoinPartyHandler() case 1
                             v
                    +--------+--------+
                    | PROCESSING (1)  |
                    | Waiting for     |
                    | Gate Server     |
                    +--------+--------+
                             |
        +--------------------+--------------------+
        |                                         |
        | PartyOperationResult_Create(fail)       | PartyOperationResult_Create(success)
        | PartyOperationResult_Join(fail)         | PartyOperationResult_Join(success)
        v                                         v
+-------+--------+                       +--------+--------+
|   NULL (0)     |                       |  CONFIRM (2)    |
| Operation      |                       | Active party    |
| failed         |                       | member          |
+----------------+                       +--------+--------+
                                                  |
                                                  | RequestDismissPartyHandler()
                                                  | RequestDeletePartyHandler()
                                                  v
                                         +--------+--------+
                                         | PROCESSING (1)  |
                                         | Leaving party   |
                                         +--------+--------+
                                                  |
                                                  | PartyOperationResult_Dismiss()
                                                  v
                                         +--------+--------+
                                         |   NULL (0)      |
                                         | Left party      |
                                         +----------------+
```

---

## Party Functions

### Party Creation

#### `bCreateNewParty()` - CClient method

**Location**: `Client.cpp`, lines 331-347

```cpp
BOOL CClient::bCreateNewParty()
{
    int i;

    if (m_iPartyRank != -1) return FALSE;  // Already in party

    m_iPartyRank = 0;                       // Set as leader
    m_iPartyMemberCount = 0;
    m_iPartyGUID = (rand() % 999999) + timeGetTime();  // Generate unique ID

    for (i = 0; i < DEF_MAXPARTYMEMBERS; i++) {
        m_stPartyMemberName[i].iIndex = 0;
        ZeroMemory(m_stPartyMemberName[i].cName, sizeof(m_stPartyMemberName[i].cName));
    }

    return TRUE;
}
```

#### `CreateNewPartyHandler()` - CGame method

**Location**: `Game.cpp`, lines 40686-40694

Called when a player clicks "Create Party" in the client. Invokes `bCreateNewParty()` on the client object and sends result.

```cpp
void CGame::CreateNewPartyHandler(int iClientH)
{
    BOOL bFlag;

    if (m_pClientList[iClientH] == NULL) return;

    bFlag = m_pClientList[iClientH]->bCreateNewParty();
    SendNotifyMsg(NULL, iClientH, DEF_NOTIFY_RESPONSE_CREATENEWPARTY, (int)bFlag, NULL, NULL, NULL);
}
```

#### `RequestCreatePartyHandler()` - CGame method

**Location**: `Game.cpp`, lines 49687-49726

Requests party creation from Gate Server. Sets status to PROCESSING while waiting.

```cpp
void CGame::RequestCreatePartyHandler(int iClientH)
{
    if (m_pClientList[iClientH] == NULL) return;
    if (m_pClientList[iClientH]->m_bIsInitComplete == FALSE) return;

    // Cannot create if already in processing state
    if (m_pClientList[iClientH]->m_iPartyStatus != DEF_PARTYSTATUS_NULL) return;

    m_pClientList[iClientH]->m_iPartyStatus = DEF_PARTYSTATUS_PROCESSING;

    // Build message for Gate Server
    // Operation code 1 = create party request
    // Send: MSGID_PARTYOPERATION, opcode=1, clientH, charName
    SendMsgToGateServer(MSGID_PARTYOPERATION, iClientH, cData);
}
```

#### `PartyOperationResult_Create()` - CGame method

**Location**: `Game.cpp`, lines 49878-49943

Handles Gate Server response to party creation request.

```cpp
void CGame::PartyOperationResult_Create(int iClientH, char *pName, int iResult, int iPartyID)
{
    // Validate client and name match
    if (m_pClientList[iClientH] == NULL) return;
    if (strcmp(m_pClientList[iClientH]->m_cCharName, pName) != 0) return;

    switch (iResult) {
    case 0: // Creation failed
        if (m_pClientList[iClientH]->m_iPartyStatus != DEF_PARTYSTATUS_PROCESSING) return;

        m_pClientList[iClientH]->m_iPartyID = NULL;
        m_pClientList[iClientH]->m_iPartyStatus = DEF_PARTYSTATUS_NULL;
        m_pClientList[iClientH]->m_iReqJoinPartyClientH = NULL;
        SendNotifyMsg(NULL, iClientH, DEF_NOTIFY_PARTY, 1, 0, NULL, NULL);
        break;

    case 1: // Creation succeeded
        if (m_pClientList[iClientH]->m_iPartyStatus != DEF_PARTYSTATUS_PROCESSING) return;

        m_pClientList[iClientH]->m_iPartyID = iPartyID;
        m_pClientList[iClientH]->m_iPartyStatus = DEF_PARTYSTATUS_CONFIRM;
        SendNotifyMsg(NULL, iClientH, DEF_NOTIFY_PARTY, 1, 1, NULL, NULL);

        // Register in local party list
        for (i = 0; i < DEF_MAXPARTYMEMBERS; i++)
            if (m_stPartyInfo[iPartyID].iIndex[i] == 0) {
                m_stPartyInfo[iPartyID].iIndex[i] = iClientH;
                m_stPartyInfo[iPartyID].iTotalMembers++;
                break;
            }

        // If someone was waiting to join, add them now
        if ((m_pClientList[iClientH]->m_iReqJoinPartyClientH != NULL) &&
            (strlen(m_pClientList[iClientH]->m_cReqJoinPartyName) != NULL)) {
            // Send join request to Gate Server for pending member
            SendMsgToGateServer(MSGID_PARTYOPERATION, opcode=3, ...);
        }
        break;
    }
}
```

### Party Invitation and Joining

#### `JoinPartyHandler()` - CGame method

**Location**: `Game.cpp`, lines 40696-40799

Handles party-related actions from client. Takes `iV1` parameter to determine action type.

```cpp
void CGame::JoinPartyHandler(int iClientH, int iV1, char *pMemberName)
{
    if (m_pClientList[iClientH] == NULL) return;
    if ((m_bAdminSecurity == TRUE) && (m_pClientList[iClientH]->m_iAdminUserLevel > 0)) return;

    switch (iV1) {
    case 0: // Leave party request
        RequestDeletePartyHandler(iClientH);
        break;

    case 1: // Join party request
        // Validation checks:
        // 1. Cannot join if already in party or processing
        if ((m_pClientList[iClientH]->m_iPartyID != NULL) ||
            (m_pClientList[iClientH]->m_iPartyStatus != DEF_PARTYSTATUS_NULL)) {
            SendNotifyMsg(NULL, iClientH, DEF_NOTIFY_PARTY, 7, 0, NULL, NULL);
            return;
        }

        // Find target player by name
        for (i = 1; i < DEF_MAXCLIENTS; i++)
            if ((m_pClientList[i] != NULL) &&
                (strcmp(m_pClientList[i]->m_cCharName, pMemberName) == 0)) {

                // 2. Cannot invite player in combat mode
                sAppr2 = (short)((m_pClientList[i]->m_sAppr2 & 0xF000) >> 12);
                if (sAppr2 != 0) {
                    SendNotifyMsg(NULL, iClientH, DEF_NOTIFY_PARTY, 7, 0, NULL, NULL);
                    return;
                }

                // 3. Must be same faction
                if (m_pClientList[i]->m_cSide != m_pClientList[iClientH]->m_cSide) {
                    SendNotifyMsg(NULL, iClientH, DEF_NOTIFY_PARTY, 7, 0, NULL, NULL);
                    return;
                }

                // 4. Target cannot be processing another party operation
                if (m_pClientList[i]->m_iPartyStatus == DEF_PARTYSTATUS_PROCESSING) {
                    SendNotifyMsg(NULL, iClientH, DEF_NOTIFY_PARTY, 7, 0, NULL, NULL);
                    return;
                }

                // Send join query to target player
                m_pClientList[i]->m_iReqJoinPartyClientH = iClientH;
                strcpy(m_pClientList[i]->m_cReqJoinPartyName,
                       m_pClientList[iClientH]->m_cCharName);
                SendNotifyMsg(NULL, i, DEF_NOTIFY_QUERY_JOINPARTY, NULL, NULL, NULL,
                              m_pClientList[iClientH]->m_cCharName);

                // Store pending info on requester
                m_pClientList[iClientH]->m_iReqJoinPartyClientH = i;
                strcpy(m_pClientList[iClientH]->m_cReqJoinPartyName,
                       m_pClientList[i]->m_cCharName);
                m_pClientList[iClientH]->m_iPartyStatus = DEF_PARTYSTATUS_PROCESSING;
                return;
            }
        break;

    case 2: // Request party member list
        if (m_pClientList[iClientH]->m_iPartyStatus == DEF_PARTYSTATUS_CONFIRM) {
            // Request member list from Gate Server
            // Operation code 6 = party member list request
            SendMsgToGateServer(MSGID_PARTYOPERATION, opcode=6, clientH, charName, partyID);
        }
        break;
    }
}
```

#### `RequestAcceptJoinPartyHandler()` - CGame method

**Location**: `Game.cpp`, lines 50270-50406

Handles response to party join invitation.

```cpp
void CGame::RequestAcceptJoinPartyHandler(int iClientH, int iResult)
{
    if (m_pClientList[iClientH] == NULL) return;

    switch (iResult) {
    case 0: // Rejected invitation
        iH = m_pClientList[iClientH]->m_iReqJoinPartyClientH;

        // Validate the requester still exists
        if (m_pClientList[iH] == NULL) return;
        if (strcmp(m_pClientList[iH]->m_cCharName,
                   m_pClientList[iClientH]->m_cReqJoinPartyName) != 0) return;
        if (m_pClientList[iH]->m_iPartyStatus != DEF_PARTYSTATUS_PROCESSING) return;

        // Notify requester of rejection
        SendNotifyMsg(NULL, iH, DEF_NOTIFY_PARTY, 7, 0, NULL, NULL);

        // Reset requester's party state
        m_pClientList[iH]->m_iPartyID = NULL;
        m_pClientList[iH]->m_iPartyStatus = DEF_PARTYSTATUS_NULL;
        m_pClientList[iH]->m_iReqJoinPartyClientH = NULL;
        ZeroMemory(m_pClientList[iH]->m_cReqJoinPartyName, ...);
        break;

    case 1: // Accepted invitation
        if ((m_pClientList[iClientH]->m_iPartyStatus == DEF_PARTYSTATUS_CONFIRM) &&
            (m_pClientList[iClientH]->m_iPartyID != NULL)) {
            // Already in party - add requester to existing party
            // Send join request to Gate Server
            SendMsgToGateServer(MSGID_PARTYOPERATION, opcode=3, requesterH,
                                requesterName, partyID);
        }
        else if (m_pClientList[iClientH]->m_iPartyStatus == DEF_PARTYSTATUS_NULL) {
            // Not in party - create new party with this player as leader
            RequestCreatePartyHandler(iClientH);
        }
        break;

    case 2: // Cancel pending join request
        if ((m_pClientList[iClientH]->m_iPartyID != NULL) &&
            (m_pClientList[iClientH]->m_iPartyStatus == DEF_PARTYSTATUS_CONFIRM)) {
            // Already joined party - leave it
            RequestDismissPartyHandler(iClientH);
        }
        else {
            // Clear pending join state
            iH = m_pClientList[iClientH]->m_iReqJoinPartyClientH;
            if (m_pClientList[iH] != NULL) {
                m_pClientList[iH]->m_iReqJoinPartyClientH = NULL;
                ZeroMemory(m_pClientList[iH]->m_cReqJoinPartyName, ...);
            }
            m_pClientList[iClientH]->m_iPartyID = NULL;
            m_pClientList[iClientH]->m_iPartyStatus = DEF_PARTYSTATUS_NULL;
        }
        break;
    }
}
```

#### `RequestJoinPartyHandler()` - CGame method

**Location**: `Game.cpp`, lines 50108-50167

Direct party join request (used when player knows party leader name).

```cpp
void CGame::RequestJoinPartyHandler(int iClientH, char *pData, DWORD dwMsgSize)
{
    if (m_pClientList[iClientH] == NULL) return;
    if (m_pClientList[iClientH]->m_iPartyStatus != DEF_PARTYSTATUS_NULL) return;

    // Parse target player name from pData
    // ...

    // Find target player
    for (i = 1; i < DEF_MAXCLIENTS; i++)
        if ((m_pClientList[i] != NULL) &&
            (strcmp(m_pClientList[i]->m_cCharName, cName) == 0)) {

            // Target must be confirmed party member (party leader)
            if ((m_pClientList[i]->m_iPartyID == NULL) ||
                (m_pClientList[i]->m_iPartyStatus != DEF_PARTYSTATUS_CONFIRM)) {
                return;
            }

            // Request join from Gate Server
            // Operation code 3 = add member to party
            SendMsgToGateServer(MSGID_PARTYOPERATION, opcode=3, clientH,
                                charName, targetPartyID);
            return;
        }

    // Target not found
    SendNotifyMsg(NULL, iClientH, DEF_NOTIFY_PLAYERNOTONGAME, NULL, NULL, NULL, cName);
}
```

#### `PartyOperationResult_Join()` - CGame method

**Location**: `Game.cpp`, lines 49947-49994

Handles Gate Server response to join request.

```cpp
void CGame::PartyOperationResult_Join(int iClientH, char *pName, int iResult, int iPartyID)
{
    if (m_pClientList[iClientH] == NULL) return;

    switch (iResult) {
    case 0: // Join failed
        if (m_pClientList[iClientH]->m_iPartyStatus != DEF_PARTYSTATUS_PROCESSING) return;

        m_pClientList[iClientH]->m_iPartyID = NULL;
        m_pClientList[iClientH]->m_iPartyStatus = DEF_PARTYSTATUS_NULL;
        SendNotifyMsg(NULL, iClientH, DEF_NOTIFY_PARTY, 4, 0, NULL, pName);
        break;

    case 1: // Join succeeded
        if (m_pClientList[iClientH]->m_iPartyStatus != DEF_PARTYSTATUS_PROCESSING) return;

        m_pClientList[iClientH]->m_iPartyID = iPartyID;
        m_pClientList[iClientH]->m_iPartyStatus = DEF_PARTYSTATUS_CONFIRM;
        SendNotifyMsg(NULL, iClientH, DEF_NOTIFY_PARTY, 4, 1, NULL, pName);

        // Add to local party list
        for (i = 0; i < DEF_MAXPARTYMEMBERS; i++)
            if (m_stPartyInfo[iPartyID].iIndex[i] == 0) {
                m_stPartyInfo[iPartyID].iIndex[i] = iClientH;
                m_stPartyInfo[iPartyID].iTotalMembers++;
                break;
            }

        // Notify other party members
        for (i = 1; i < DEF_MAXCLIENTS; i++)
            if ((i != iClientH) && (m_pClientList[i] != NULL) &&
                (m_pClientList[i]->m_iPartyID == iPartyID)) {
                SendNotifyMsg(NULL, i, DEF_NOTIFY_PARTY, 4, 1, NULL, pName);
            }
        break;
    }
}
```

### Party Member Queries

#### `GetPartyInfoHandler()` - CGame method

**Location**: `Game.cpp`, lines 50202-50228

Requests party member list from Gate Server.

```cpp
void CGame::GetPartyInfoHandler(int iClientH)
{
    if (m_pClientList[iClientH] == NULL) return;
    if (m_pClientList[iClientH]->m_iPartyStatus != DEF_PARTYSTATUS_CONFIRM) return;

    // Request from Gate Server
    // Operation code 5 = party info request
    SendMsgToGateServer(MSGID_PARTYOPERATION, opcode=5, clientH, charName, partyID);
}
```

#### `PartyOperationResult_Info()` - CGame method

**Location**: `Game.cpp`, lines 50231-50238

Handles party member list response.

```cpp
void CGame::PartyOperationResult_Info(int iClientH, char *pName, int iTotal, char *pNameList)
{
    if (m_pClientList[iClientH] == NULL) return;
    if (strcmp(m_pClientList[iClientH]->m_cCharName, pName) != 0) return;
    if (m_pClientList[iClientH]->m_iPartyStatus != DEF_PARTYSTATUS_CONFIRM) return;

    // Send member list to client
    // sV1=5, sV2=1 (success), sV3=iTotal, pString=pNameList
    SendNotifyMsg(NULL, iClientH, DEF_NOTIFY_PARTY, 5, 1, iTotal, pNameList);
}
```

### Party Dismissal and Leaving

#### `RequestDismissPartyHandler()` - CGame method

**Location**: `Game.cpp`, lines 50171-50199

Player requests to leave their current party.

```cpp
void CGame::RequestDismissPartyHandler(int iClientH)
{
    if (m_pClientList[iClientH] == NULL) return;
    if (m_pClientList[iClientH]->m_iPartyStatus != DEF_PARTYSTATUS_CONFIRM) return;

    // Request leave from Gate Server
    // Operation code 4 = remove member from party
    SendMsgToGateServer(MSGID_PARTYOPERATION, opcode=4, clientH, charName, partyID);

    m_pClientList[iClientH]->m_iPartyStatus = DEF_PARTYSTATUS_PROCESSING;
}
```

#### `RequestDeletePartyHandler()` - CGame method

**Location**: `Game.cpp`, lines 50240-50268

Alternative leave function (called from JoinPartyHandler case 0).

```cpp
void CGame::RequestDeletePartyHandler(int iClientH)
{
    if (m_pClientList[iClientH] == NULL) return;

    if (m_pClientList[iClientH]->m_iPartyID != NULL) {
        // Request leave from Gate Server
        // Operation code 4 = remove member
        SendMsgToGateServer(MSGID_PARTYOPERATION, opcode=4, clientH, charName, partyID);
        m_pClientList[iClientH]->m_iPartyStatus = DEF_PARTYSTATUS_PROCESSING;
    }
}
```

#### `PartyOperationResult_Dismiss()` - CGame method

**Location**: `Game.cpp`, lines 49996-50081

Handles Gate Server response to leave request.

```cpp
void CGame::PartyOperationResult_Dismiss(int iClientH, char *pName, int iResult, int iPartyID)
{
    switch (iResult) {
    case 0: // Leave failed (rare)
        break;

    case 1: // Leave succeeded
        // Handle case where client handle is NULL (forced removal during server change)
        if (iClientH == NULL) {
            // Find client by name
            for (i = 1; i < DEF_MAXCLIENTS; i++)
                if ((m_pClientList[i] != NULL) &&
                    (strcmp(m_pClientList[i]->m_cCharName, pName) == 0)) {
                    iClientH = i;
                    break;
                }
        }

        // Remove from local party list
        for (i = 0; i < DEF_MAXPARTYMEMBERS; i++)
            if (m_stPartyInfo[iPartyID].iIndex[i] == iClientH) {
                m_stPartyInfo[iPartyID].iIndex[i] = 0;
                m_stPartyInfo[iPartyID].iTotalMembers--;
                break;
            }

        // Compact the index array (remove gaps)
        for (i = 0; i < DEF_MAXPARTYMEMBERS-1; i++)
            if ((m_stPartyInfo[iPartyID].iIndex[i] == 0) &&
                (m_stPartyInfo[iPartyID].iIndex[i+1] != 0)) {
                m_stPartyInfo[iPartyID].iIndex[i] = m_stPartyInfo[iPartyID].iIndex[i+1];
                m_stPartyInfo[iPartyID].iIndex[i+1] = 0;
            }

        // Reset client party state
        if (m_pClientList[iClientH] != NULL) {
            m_pClientList[iClientH]->m_iPartyID = NULL;
            m_pClientList[iClientH]->m_iPartyStatus = DEF_PARTYSTATUS_NULL;
            m_pClientList[iClientH]->m_iReqJoinPartyClientH = NULL;
        }

        // Notify all remaining party members
        for (i = 1; i < DEF_MAXCLIENTS; i++)
            if ((m_pClientList[i] != NULL) &&
                (m_pClientList[i]->m_iPartyID == iPartyID)) {
                SendNotifyMsg(NULL, i, DEF_NOTIFY_PARTY, 6, 1, NULL, pName);
            }
        break;
    }
}
```

### Party Deletion

#### `PartyOperationResult_Delete()` - CGame method

**Location**: `Game.cpp`, lines 50083-50104

Handles party dissolution (all members removed).

```cpp
void CGame::PartyOperationResult_Delete(int iPartyID)
{
    int i;

    // Clear local party info
    for (i = 0; i < DEF_MAXPARTYMEMBERS; i++) {
        m_stPartyInfo[iPartyID].iIndex[i] = 0;
        m_stPartyInfo[iPartyID].iTotalMembers = 0;
    }

    // Notify all members that party was disbanded
    for (i = 1; i < DEF_MAXCLIENTS; i++)
        if ((m_pClientList[i] != NULL) && (m_pClientList[i]->m_iPartyID == iPartyID)) {
            SendNotifyMsg(NULL, i, DEF_NOTIFY_PARTY, 2, 0, NULL, NULL);
            m_pClientList[i]->m_iPartyID = NULL;
            m_pClientList[i]->m_iPartyStatus = DEF_PARTYSTATUS_NULL;
            m_pClientList[i]->m_iReqJoinPartyClientH = NULL;
        }
}
```

---

## Experience Distribution

**Location**: `Game.cpp`, lines 44271-44356

When a party member kills an enemy, experience is distributed among all living party members on the same map.

### Experience Sharing Formula

```cpp
// Check for party status
if ((m_pClientList[iClientH]->m_iPartyID != NULL) &&
    (m_pClientList[iClientH]->m_iPartyStatus == DEF_PARTYSTATUS_CONFIRM)) {

    // Only share if exp >= 10 and party has members
    if (iExp >= 10 && m_stPartyInfo[partyID].iTotalMembers > 0) {

        // Count eligible members (alive and on same map)
        iTotalPartyMembers = 0;
        for (i = 0; i < m_stPartyInfo[partyID].iTotalMembers; i++) {
            iH = m_stPartyInfo[partyID].iIndex[i];
            if ((m_pClientList[iH] != NULL) &&
                (m_pClientList[iH]->m_iHP > 0) &&
                (same map check)) {
                iTotalPartyMembers++;
            }
        }

        // Clamp to max 8 members (anti-cheat)
        if (iTotalPartyMembers > 8) iTotalPartyMembers = 8;

        // Calculate exp per member with bonus
        dV1 = (double)iExp;
        switch (iTotalPartyMembers) {
        case 1: dV2 = dV1; break;                           // 100%
        case 2: dV2 = (dV1 + (dV1 * 0.02)) / 2.0; break;    // 102% / 2 = 51% each
        case 3: dV2 = (dV1 + (dV1 * 0.05)) / 3.0; break;    // 105% / 3 = 35% each
        case 4: dV2 = (dV1 + (dV1 * 0.07)) / 4.0; break;    // 107% / 4 = 26.75% each
        case 5: dV2 = (dV1 + (dV1 * 0.10)) / 5.0; break;    // 110% / 5 = 22% each
        case 6: dV2 = (dV1 + (dV1 * 0.14)) / 6.0; break;    // 114% / 6 = 19% each
        case 7: dV2 = (dV1 + (dV1 * 0.17)) / 7.0; break;    // 117% / 7 = 16.7% each
        case 8: dV2 = (dV1 + (dV1 * 0.20)) / 8.0; break;    // 120% / 8 = 15% each
        }
        dV3 = dV2 + 0.5;  // Round up

        // Distribute to each eligible member
        for (i = 0; i < iTotalPartyMembers; i++) {
            iUnitValue = (int)dV3;
            iH = m_stPartyInfo[partyID].iIndex[i];

            if ((m_pClientList[iH] != NULL) &&
                (m_pClientList[iH]->m_bSkillUsingStatus[19] != 1) &&  // Not using invisibility
                (m_pClientList[iH]->m_iHP > 0)) {

                // Level-based multiplier
                if (m_pClientList[iH]->m_iLevel < 81) iUnitValue *= 2;
                else if (m_pClientList[iH]->m_iLevel < 101) iUnitValue *= 2;
                else if (m_pClientList[iH]->m_iLevel < 151) iUnitValue *= 2;
                else if (m_pClientList[iH]->m_iLevel < 500) iUnitValue *= 2;

                // Premium status multiplier
                if ((m_pClientList[iH]->m_iStatus & 0x10000) != 0) iUnitValue *= 3;

                m_pClientList[iH]->m_iExpStock += iUnitValue;
            }
        }
    }
}
```

### Party Experience Bonus Table

| Party Size | Bonus | Effective Share per Member |
|------------|-------|----------------------------|
| 1 | 0% | 100% |
| 2 | 2% | 51% |
| 3 | 5% | 35% |
| 4 | 7% | 26.75% |
| 5 | 10% | 22% |
| 6 | 14% | 19% |
| 7 | 17% | 16.7% |
| 8 | 20% | 15% |

**Note**: Each member also receives the standard level-based multiplier (x2 for all levels < 500).

---

## Party Chat

**Location**: `Game.cpp`, lines 8916-8935 and 9567-9571

### Chat Prefix

Party chat uses the `$` prefix character.

```cpp
case '$':  // Party chat
    *cp = 32;  // Replace $ with space

    if ((m_pClientList[iClientH]->m_iTimeLeft_ShutUp == 0) &&
        (m_pClientList[iClientH]->m_iSP >= 3)) {

        if (m_pClientList[iClientH]->m_iTimeLeft_FirmStaminar == 0) {
            m_pClientList[iClientH]->m_iSP -= 3;  // Costs 3 SP
            SendNotifyMsg(NULL, iClientH, DEF_NOTIFY_SP, NULL, NULL, NULL, NULL);
        }
        cSendMode = 4;  // Party chat mode
    }
    else {
        cSendMode = NULL;
    }

    if (m_pClientList[iClientH]->m_iTimeLeft_ShutUp > 0) {
        cSendMode = NULL;  // Muted players cannot use party chat
    }
    break;
```

### Party Chat Distribution

```cpp
case 4:  // Party chat send mode
    if (m_pClientList[i]->m_bIsInitComplete == FALSE) break;

    // Only send to players in same party
    if ((m_pClientList[i]->m_iPartyID != NULL) &&
        (m_pClientList[i]->m_iPartyID == m_pClientList[iClientH]->m_iPartyID))
        iRet = m_pClientList[i]->m_pXSock->iSendMsg(pData, dwMsgSize);
    break;
```

---

## Combat Party Checks

**Location**: `Game.cpp`, lines 27660-27692 and similar locations

Party members cannot attack each other. This check is performed in damage calculation:

```cpp
// In attack resolution code
switch (cAttackerType) {
case DEF_OWNERTYPE_PLAYER:
    iPartyID = m_pClientList[sAttackerH]->m_iPartyID;
    break;
// ...
}

switch (cTargetType) {
case DEF_OWNERTYPE_PLAYER:
    // ... other checks ...

    // Party member protection - cannot attack party members
    if ((m_pClientList[sTargetH]->m_iPartyID != NULL) &&
        (iPartyID == m_pClientList[sTargetH]->m_iPartyID))
        return;  // Abort attack
    break;
}
```

This check appears in multiple attack handlers:
- Physical attacks (line 27692)
- Magic attacks (line 28177)
- Area of effect attacks (line 28611)
- Special attacks (line 51983)

---

## Player Disconnect Handling

**Location**: `Game.cpp`, lines 2590-2704

When a player disconnects, their party membership is handled based on disconnect type.

### Normal Disconnect (with save)

```cpp
if (m_pClientList[iClientH]->m_iPartyID != NULL) {
    // Request member removal from Gate Server
    // Operation code 4 = remove member
    SendMsgToGateServer(MSGID_PARTYOPERATION, opcode=4, clientH, charName, partyID);
}
```

### Disconnect During Server Change

```cpp
if (m_pClientList[iClientH]->m_bIsOnServerChange == TRUE) {
    if (m_pClientList[iClientH]->m_iPartyID != NULL) {
        // Special operation code 7 = server change notification
        SendMsgToGateServer(MSGID_PARTYOPERATION, opcode=7, NULL, charName, partyID);
    }
}
```

### Local Cleanup

After notifying Gate Server, local party data is cleaned up:

```cpp
if (m_pClientList[iClientH]->m_iPartyID != NULL) {
    // Remove from local party list
    for (i = 0; i < DEF_MAXPARTYMEMBERS; i++)
        if (m_stPartyInfo[partyID].iIndex[i] == iClientH) {
            m_stPartyInfo[partyID].iIndex[i] = 0;
            m_stPartyInfo[partyID].iTotalMembers--;
            m_pClientList[iClientH]->m_iPartyID = NULL;
            m_pClientList[iClientH]->m_iPartyStatus = DEF_PARTYSTATUS_NULL;
            break;
        }

    // Compact array
    for (i = 0; i < DEF_MAXPARTYMEMBERS-1; i++)
        if ((m_stPartyInfo[partyID].iIndex[i] == 0) &&
            (m_stPartyInfo[partyID].iIndex[i+1] != 0)) {
            m_stPartyInfo[partyID].iIndex[i] = m_stPartyInfo[partyID].iIndex[i+1];
            m_stPartyInfo[partyID].iIndex[i+1] = 0;
        }
}
```

---

## Network Protocol

### Message IDs

| Constant | Value | Description |
|----------|-------|-------------|
| `MSGID_PARTYOPERATION` | `0x3C00123A` | Party operation message (Game <-> Gate) |

### Client -> Server Commands

| Constant | Value | Description |
|----------|-------|-------------|
| `DEF_COMMONTYPE_REQUEST_JOINPARTY` | `0x0A31` | Request to join/invite/leave party |
| `DEF_COMMONTYPE_REQUEST_ACCEPTJOINPARTY` | `0x0A30` | Response to party invitation |

### Server -> Client Notifications

| Constant | Value | Description |
|----------|-------|-------------|
| `DEF_NOTIFY_RESPONSE_CREATENEWPARTY` | `0x0B80` | Party creation result |
| `DEF_NOTIFY_QUERY_JOINPARTY` | `0x0B81` | Incoming party invitation |
| `DEF_NOTIFY_PARTY` | `0x0BA2` | General party notification |

### Operation Codes

Used in `MSGID_PARTYOPERATION` messages between Game Server and Gate Server.

| Code | Direction | Description |
|------|-----------|-------------|
| 1 | Game -> Gate | Create party request |
| 2 | Gate -> Game | Delete party notification |
| 3 | Game -> Gate | Add member to party |
| 4 | Game -> Gate | Remove member from party |
| 5 | Game -> Gate | Get party info request |
| 6 | Game -> Gate | Get party member list |
| 7 | Game -> Gate | Server change notification |

### Notification Sub-types (DEF_NOTIFY_PARTY)

The `sV1` parameter determines the notification type:

| sV1 | sV2 | Description |
|-----|-----|-------------|
| 1 | 0 | Party creation failed |
| 1 | 1 | Party creation succeeded |
| 2 | 0 | Party disbanded |
| 4 | 0 | Join failed |
| 4 | 1 | Join succeeded (+ member name in pString) |
| 5 | 1 | Party member list (sV3 = count, pString = names) |
| 6 | 1 | Member left party (+ member name in pString) |
| 7 | 0 | Join request rejected |
| 8 | 0 | Kicked from party |

---

## Gate Server Communication

### PartyOperationResultHandler()

**Location**: `Game.cpp`, lines 49729-49875

Main dispatcher for Gate Server responses.

```cpp
void CGame::PartyOperationResultHandler(char *pData)
{
    cp = (char *)(pData + 4);
    wp = (WORD *)cp;
    cp += 2;

    switch (*wp) {  // Operation code
    case 1:  // Create result
        cResult = *cp; cp++;
        iClientH = *wp; cp += 2;
        memcpy(cName, cp, 10); cp += 10;
        iPartyID = *wp; cp += 2;
        PartyOperationResult_Create(iClientH, cName, cResult, iPartyID);
        break;

    case 2:  // Delete party
        iPartyID = *wp; cp += 2;
        PartyOperationResult_Delete(iPartyID);
        break;

    case 3:  // Clear member (forced removal)
        iClientH = *wp; cp += 2;
        memcpy(cName, cp, 10); cp += 10;
        // ... remove member from local list ...
        break;

    case 4:  // Join result
        cResult = *cp; cp++;
        iClientH = *wp; cp += 2;
        memcpy(cName, cp, 10); cp += 10;
        iPartyID = *wp; cp += 2;
        PartyOperationResult_Join(iClientH, cName, cResult, iPartyID);
        break;

    case 5:  // Info result
        iClientH = *wp; cp += 2;
        memcpy(cName, cp, 10); cp += 10;
        iTotal = *wp; cp += 2;
        PartyOperationResult_Info(iClientH, cName, iTotal, cp);
        break;

    case 6:  // Dismiss result
        cResult = *cp; cp++;
        iClientH = *wp; cp += 2;
        memcpy(cName, cp, 10); cp += 10;
        iPartyID = *wp; cp += 2;
        PartyOperationResult_Dismiss(iClientH, cName, cResult, iPartyID);
        break;
    }
}
```

---

## Save/Load

### Save Format

**Location**: `Game.cpp`, line 7677

Party ID is saved in character data:

```cpp
wsprintf(cTxt, "party-id = %d", m_pClientList[iClientH]->m_iPartyID);
strcat(pData, cTxt);
strcat(pData, "\n");
```

### Load Format

**Location**: `Game.cpp`, lines 7013-7016

Party ID is loaded and status set to CONFIRM if non-zero:

```cpp
case 79:  // party-id field
    m_pClientList[iClientH]->m_iPartyID = atoi(token);
    if (m_pClientList[iClientH]->m_iPartyID != NULL)
        m_pClientList[iClientH]->m_iPartyStatus = DEF_PARTYSTATUS_CONFIRM;
    break;
```

**Note**: When a character is loaded with a party ID, they are placed in CONFIRM status. The Gate Server maintains the authoritative party membership, so the local m_stPartyInfo will be populated when the player reconnects and the Gate Server syncs the data.

---

## Implementation Notes

### Thread Safety

The party system has no explicit locking. All party operations are processed on the main game thread.

### Party ID Assignment

Party IDs appear to be assigned by the Gate Server and may correspond to client handles on the server that created the party. This creates a distributed party tracking system.

### Known Limitations

1. **Maximum 9 members**: Hardcoded limit in DEF_MAXPARTYMEMBERS
2. **Same faction only**: Players must have same `m_cSide` value
3. **No leader transfer**: If party leader disconnects, party behavior is unclear
4. **Local member list may desync**: The local `m_stPartyInfo` is a cache; Gate Server is authoritative

### Anti-Cheat Measures

1. Party member count is clamped to 8 maximum during exp distribution
2. Admin security check prevents admins from joining parties
3. Combat mode check prevents party invitations to players in combat

### Debugging

Party operations are extensively logged with `PutLogList()`:
- Party creation/join/leave events
- Member counts
- Error conditions

Example log format:
```
PartyID:123 member:456 New Total:3
PartyID:123 member:456 Out(Delete) Total:2
Party join reject(3) ClientH:789 ID:0
```

---

## Function Reference

### CClient Methods

| Function | Location | Description |
|----------|----------|-------------|
| `bCreateNewParty()` | Client.cpp:331 | Initialize new party as leader |

### CGame Methods

| Function | Location | Description |
|----------|----------|-------------|
| `CreateNewPartyHandler()` | Game.cpp:40686 | Handle create party button |
| `JoinPartyHandler()` | Game.cpp:40696 | Handle join/leave/list commands |
| `RequestCreatePartyHandler()` | Game.cpp:49687 | Request party from Gate Server |
| `PartyOperationResultHandler()` | Game.cpp:49729 | Dispatch Gate Server responses |
| `PartyOperationResult_Create()` | Game.cpp:49878 | Handle create response |
| `PartyOperationResult_Join()` | Game.cpp:49947 | Handle join response |
| `PartyOperationResult_Dismiss()` | Game.cpp:49996 | Handle leave response |
| `PartyOperationResult_Delete()` | Game.cpp:50083 | Handle party deletion |
| `RequestJoinPartyHandler()` | Game.cpp:50108 | Direct join request |
| `RequestDismissPartyHandler()` | Game.cpp:50171 | Request to leave party |
| `GetPartyInfoHandler()` | Game.cpp:50202 | Request member list |
| `PartyOperationResult_Info()` | Game.cpp:50231 | Handle member list response |
| `RequestDeletePartyHandler()` | Game.cpp:50240 | Alternative leave function |
| `RequestAcceptJoinPartyHandler()` | Game.cpp:50270 | Handle invite response |
