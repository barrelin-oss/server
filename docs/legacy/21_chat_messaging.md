# Legacy Chat and Messaging System Documentation

## Table of Contents

1. [Overview](#overview)
2. [Data Structures](#data-structures)
   - [CMsg Class](#cmsg-class)
   - [Client Chat-Related Fields](#client-chat-related-fields)
   - [Message Queue](#message-queue)
3. [Chat Types and Send Modes](#chat-types-and-send-modes)
4. [Chat Handler Functions](#chat-handler-functions)
   - [ChatMsgHandler](#chatmsghandler)
   - [ChatMsgHandlerGSM](#chatmsghandlergsm)
5. [Whisper System](#whisper-system)
   - [ToggleWhisperPlayer](#togglewhisperplayer)
   - [Cross-Server Whispers](#cross-server-whispers)
6. [Guild Chat](#guild-chat)
7. [Party Chat](#party-chat)
8. [Faction/Side Chat](#factionside-chat)
9. [Area/Local Chat](#arealocal-chat)
10. [Global Broadcast](#global-broadcast)
11. [Player Profiles](#player-profiles)
12. [Silence/Mute System](#silencemute-system)
13. [Bad Word Filtering](#bad-word-filtering)
14. [Admin/GM Chat Features](#admingm-chat-features)
15. [Notification Messages](#notification-messages)
16. [Inter-Server Communication](#inter-server-communication)
17. [Chat Commands](#chat-commands)
18. [Constants and Definitions](#constants-and-definitions)
19. [Message Flow Diagrams](#message-flow-diagrams)

---

## Overview

The Helbreath legacy chat system handles all player-to-player communication including:
- **Local/Area Chat**: Messages visible only to nearby players
- **Whisper (Private Messages)**: Direct messages between two players
- **Guild Chat**: Messages visible only to guild members
- **Party Chat**: Messages visible only to party members
- **Faction/Side Chat**: Messages visible to all players of the same faction
- **Global Broadcast**: Messages visible to all players (admin only)
- **System Notices**: Server-generated messages to players

The chat system is tightly integrated with the message queue system and supports cross-server communication via the Gate Server.

---

## Data Structures

### CMsg Class

**File**: `Msg.h`, `Msg.cpp`

The `CMsg` class represents a queued message in the server's message processing pipeline.

```cpp
class CMsg {
public:
    void Get(char * pFrom, char * pData, DWORD * pSize, int * pIndex, char * pKey);
    BOOL bPut(char cFrom, char * pData, DWORD dwSize, int iIndex, char cKey);
    CMsg();
    virtual ~CMsg();

    char   m_cFrom;      // Message source type
    char * m_pData;      // Message data buffer (dynamically allocated)
    DWORD  m_dwSize;     // Size of message data
    int    m_iIndex;     // Client index (for client messages)
    char   m_cKey;       // Encryption/verification key
};
```

**Message Source Types** (`Msg.h`):
| Constant | Value | Description |
|----------|-------|-------------|
| `DEF_MSGFROM_CLIENT` | 1 | Message from a game client |
| `DEF_MSGFROM_LOGSERVER` | 2 | Message from log server |
| `DEF_MSGFROM_GATESERVER` | 3 | Message from gate server |
| `DEF_MSGFROM_BOT` | 4 | Message from bot/automated system |

### Client Chat-Related Fields

**File**: `Client.h`

Each connected client (`CClient`) has these chat-related fields:

```cpp
class CClient {
    // Whisper system
    int   m_iWhisperPlayerIndex;      // Index of whisper target (-1 = none, 10000 = cross-server)
    char  m_cWhisperPlayerName[11];   // Name of whisper target
    BOOL  m_bIsCheckingWhisperPlayer; // TRUE when waiting for cross-server lookup

    // Profile system
    char  m_cProfile[256];            // Player's profile text

    // Silence/mute system
    int   m_iTimeLeft_ShutUp;         // Remaining silence time in ticks (0 = can chat)
    int   m_iTimeLeft_Rating;         // Cooldown for rating other players

    // Bad word monitoring
    BOOL  m_bIsBWMonitor;             // TRUE if this client is a bad word monitor

    // Admin level (affects chat permissions)
    int   m_iAdminUserLevel;          // 0 = normal player, higher = more admin powers
};
```

### Message Queue

**File**: `Game.h`

The server maintains a circular queue for processing all incoming messages:

```cpp
#define DEF_MSGQUENESIZE    100000   // Queue can hold 100,000 messages
#define DEF_MSGBUFFERSIZE   30000    // Individual message buffer size

class CGame {
    class CMsg * m_pMsgQuene[DEF_MSGQUENESIZE];  // Message queue array
    int m_iQueneHead;                             // Read position
    int m_iQueneTail;                             // Write position
    char m_pMsgBuffer[DEF_MSGBUFFERSIZE+1];      // Temporary message buffer
};
```

**Queue Functions**:

```cpp
// Add message to queue (Game.cpp:11536)
BOOL CGame::bPutMsgQuene(char cFrom, char * pData, DWORD dwMsgSize, int iIndex, char cKey);

// Retrieve message from queue (Game.cpp:11562)
BOOL CGame::bGetMsgQuene(char * pFrom, char * pData, DWORD * pMsgSize, int * pIndex, char * pKey);
```

---

## Chat Types and Send Modes

The `cSendMode` variable determines how a chat message is distributed:

| Mode Value | Description | Prefix Character | Requirements |
|------------|-------------|------------------|--------------|
| `NULL` (0) | Local/Area chat | None | Nearby players only |
| 1 | Guild chat | `@` or `^` | Must be in a guild |
| 2 | Global broadcast | `!` | Level > 10, 5 SP cost |
| 3 | Faction/Side chat | `~` | Level > 1, 3 SP cost |
| 4 | Party chat | `$` | Must be in a party |
| 10 | Admin broadcast | `^` or `!` | Admin level > 0 |
| 20 | Whisper mode | None | Whisper target set |

---

## Chat Handler Functions

### ChatMsgHandler

**File**: `Game.cpp:8795`

**Signature**:
```cpp
void CGame::ChatMsgHandler(int iClientH, char * pData, DWORD dwMsgSize);
```

This is the main entry point for processing chat messages from clients.

**Flow**:

1. **Validation**:
   - Check client exists and is initialized
   - Verify message size (max 113 bytes: 83 + 30)
   - Check if player is silenced (`m_iTimeLeft_ShutUp`)
   - Verify character name matches sender
   - Block chat in observer mode (unless admin)

2. **Activity Tracking**:
   - Update map sector activity counters
   - Track activity by faction (Neutral/Aresden/Elvine)

3. **Chat Logging** (based on `m_bLogChatOption`):
   - Option 1: Log player chat only
   - Option 2: Log GM chat only
   - Option 3: Log all chat
   - Option 4: No logging

4. **Parse Chat Type** (first character):
   ```cpp
   switch (*cp) {
       case '@':  // Guild chat (guild master)
       case '$':  // Party chat
       case '^':  // Guild chat (any member) or admin broadcast
       case '!':  // Global broadcast
       case '~':  // Faction/side chat
       case '/':  // Command
       default:   // Local chat or whisper
   }
   ```

5. **Confuse Effect**:
   - If player has `DEF_MAGICTYPE_CONFUSE` effect, text may be garbled

6. **Message Distribution**:
   - Set appropriate `cSendMode`
   - Deduct stamina (SP) for certain chat types
   - Distribute based on mode

### ChatMsgHandlerGSM

**File**: `Game.cpp:9659`

**Signature**:
```cpp
void CGame::ChatMsgHandlerGSM(int iMsgType, int iV1, char * pName, char * pData, DWORD dwMsgSize);
```

Handles chat messages received from the Gate Server (cross-server messages).

**Message Types**:
- Type 1: Guild chat from another server
- Type 2/10: Global broadcast from another server

**Flow**:
1. Build `MSGID_COMMAND_CHATMSG` packet
2. Find matching clients based on message type
3. Send to appropriate recipients

---

## Whisper System

### ToggleWhisperPlayer

**File**: `Game.cpp:31258`

**Signature**:
```cpp
void CGame::ToggleWhisperPlayer(int iClientH, char * pMsg, DWORD dwMsgSize);
```

**Command**: `/to <playername>` or `/to` (to disable)

**Flow**:

1. **Disable Whisper Mode** (no name provided):
   ```cpp
   m_pClientList[iClientH]->m_iWhisperPlayerIndex = -1;
   ZeroMemory(m_pClientList[iClientH]->m_cWhisperPlayerName, ...);
   m_pClientList[iClientH]->m_bIsCheckingWhisperPlayer = FALSE;
   SendNotifyMsg(NULL, iClientH, DEF_NOTIFY_WHISPERMODEOFF, ...);
   ```

2. **Enable Whisper Mode** (name provided):
   - Search local server for player
   - If found: Set `m_iWhisperPlayerIndex` to their client index
   - If not found: Send `GSM_REQUEST_FINDCHARACTER` to Gate Server
   - Set `m_bIsCheckingWhisperPlayer = TRUE` while waiting

3. **Whisper Index Values**:
   - `-1`: Whisper mode disabled
   - `1-1999`: Local server client index
   - `10000`: Cross-server whisper (player on another server)

### Cross-Server Whispers

**File**: `Game.cpp:42523` (GSM_WHISFERMSG handler)

**Protocol**:

1. **Request** (sending server):
   ```
   GSM_REQUEST_FINDCHARACTER (0x01)
   - Server ID (WORD)
   - Client Handle (WORD)
   - Target Name (10 bytes)
   - Sender Name (10 bytes)
   ```

2. **Response** (target server):
   ```
   GSM_RESPONSE_FINDCHARACTER (0x02)
   - Request Server ID (WORD)
   - Request Client Handle (WORD)
   - Target Name (10 bytes)
   - Finder Name (10 bytes)
   - Map Name (10 bytes) - for GMs
   - X Position (WORD) - for GMs
   - Y Position (WORD) - for GMs
   ```

3. **Whisper Message** (via Gate Server):
   ```
   GSM_WHISFERMSG (0x0D)
   - Target Name (10 bytes)
   - Message Size (WORD)
   - Message Data (variable)
   ```

---

## Guild Chat

**Trigger Character**: `@` (guild master) or `^` (any guild member)

**Requirements**:
- Must be in a guild (`m_iGuildRank != -1`)
- Message length < 90 characters
- Guild master (`m_iGuildRank == 0`) for `@` prefix
- Level > 10 for `^` prefix
- 3 SP cost (unless admin)

**Cross-Server Guild Chat**:

The server sends guild chat to the Gate Server for distribution to other game servers:

```cpp
*cp2 = GSM_CHATMSG;
cp2++;
*cp2 = 1;  // Guild chat type
cp2++;
ip = (int *)cp2;
*ip = m_pClientList[iClientH]->m_iGuildGUID;  // Guild ID
// ... sender name and message
bStockMsgToGateServer(cTemp, strlen(cp) + 18);
```

**Local Distribution**:
```cpp
// Send to all local guild members
for (i = 1; i < DEF_MAXCLIENTS; i++)
    if (m_pClientList[i] != NULL) {
        if ((memcmp(m_pClientList[i]->m_cGuildName, m_pClientList[iClientH]->m_cGuildName, 20) == 0) &&
            (memcmp(m_pClientList[i]->m_cGuildName, "NONE", 4) != 0)) {
            // During Crusade, check faction
            if (m_bIsCrusadeMode == TRUE) {
                // Block enemy faction
            }
            iRet = m_pClientList[i]->m_pXSock->iSendMsg(pData, dwMsgSize);
        }
    }
```

---

## Party Chat

**Trigger Character**: `$`

**Requirements**:
- Must be in a party (`m_iPartyID != NULL`)
- No silence penalty active
- 3 SP cost

**Distribution**:
```cpp
case 4:  // Party chat
    if (m_pClientList[i]->m_bIsInitComplete == FALSE) break;
    if ((m_pClientList[i]->m_iPartyID != NULL) &&
        (m_pClientList[i]->m_iPartyID == m_pClientList[iClientH]->m_iPartyID))
        iRet = m_pClientList[i]->m_pXSock->iSendMsg(pData, dwMsgSize);
    break;
```

---

## Faction/Side Chat

**Trigger Character**: `~`

**Requirements**:
- Level > 1
- 3 SP cost
- Not dead (HP > 0)

**Distribution**:
```cpp
case 3:  // Side chat
    if (m_pClientList[i]->m_bIsInitComplete == FALSE) break;
    if (m_pClientList[i]->m_cSide == m_pClientList[iClientH]->m_cSide)
        iRet = m_pClientList[i]->m_pXSock->iSendMsg(pData, dwMsgSize);
    break;
```

**Side Values**:
- 0: Neutral/Traveler
- 1: Aresden
- 2: Elvine

---

## Area/Local Chat

**Default chat type** (no prefix character)

**Requirements**:
- No silence penalty active

**Range**:
- X: sender's X +/- 10 tiles
- Y: sender's Y +/- 7 tiles
- Same map

**Distribution**:
```cpp
case NULL:  // Local chat
    if (m_pClientList[i]->m_bIsInitComplete == FALSE) break;
    if ((m_pClientList[i]->m_cMapIndex == m_pClientList[iClientH]->m_cMapIndex) &&
        (m_pClientList[i]->m_sX > m_pClientList[iClientH]->m_sX - 10) &&
        (m_pClientList[i]->m_sX < m_pClientList[iClientH]->m_sX + 10) &&
        (m_pClientList[i]->m_sY > m_pClientList[iClientH]->m_sY - 7) &&
        (m_pClientList[i]->m_sY < m_pClientList[iClientH]->m_sY + 7)) {
        // During Crusade, check faction
        iRet = m_pClientList[i]->m_pXSock->iSendMsg(pData, dwMsgSize);
    }
    break;
```

---

## Global Broadcast

**Trigger Character**: `!`

**Requirements**:
- Level > 10
- 5 SP cost
- Not dead (HP > 0)
- Admin users bypass requirements

**Distribution**:
- Sends to all connected clients on local server
- For cross-server: Sends `GSM_CHATMSG` with type 2/10 to Gate Server

```cpp
case 2:
case 10:
    // Send to everyone
    for (i = 1; i < DEF_MAXCLIENTS; i++)
        if (m_pClientList[i] != NULL) {
            // During Crusade, check faction
            iRet = m_pClientList[i]->m_pXSock->iSendMsg(pData, dwMsgSize);
        }
    break;
```

---

## Player Profiles

### SetPlayerProfile

**File**: `Game.cpp:31343`

**Command**: `/setpf <profile text>`

```cpp
void CGame::SetPlayerProfile(int iClientH, char * pMsg, DWORD dwMsgSize);
```

- Maximum 255 characters
- Spaces are converted to underscores (`_`)
- Stored in `m_pClientList[iClientH]->m_cProfile`

### GetPlayerProfile

**File**: `Game.cpp:31366`

**Command**: `/pf <playername>`

```cpp
void CGame::GetPlayerProfile(int iClientH, char * pMsg, DWORD dwMsgSize);
```

- Returns profile via `DEF_NOTIFY_PLAYERPROFILE` notification
- Returns `DEF_NOTIFY_PLAYERNOTONGAME` if player not found

---

## Silence/Mute System

### ShutUpPlayer

**File**: `Game.cpp:32562`

**Command**: `/shutup <playername> <minutes>`

**Signature**:
```cpp
void CGame::ShutUpPlayer(int iClientH, char * pMsg, DWORD dwMsgSize);
```

**Requirements**:
- Caller must have `m_iAdminUserLevel >= m_iAdminLevelShutup`

**Flow**:
1. Parse player name and time from command
2. Search for player on local server
3. If found:
   - Set `m_iTimeLeft_ShutUp = iTime * 20` (1 minute = 20 ticks)
   - Send `DEF_NOTIFY_PLAYERSHUTUP` to both admin and target
4. If not found: Send `GSM_REQUEST_SHUTUPPLAYER` to Gate Server

**Cross-Server Muting**:
```cpp
// Request (Game.cpp:32617)
*cp = GSM_REQUEST_SHUTUPPLAYER;  // 0x10
// Server ID, Client Handle, Player Name, Time, GM Name

// Response (Game.cpp:42492)
*cp = GSM_RESPONSE_SHUTUPPLAYER;  // 0x11
// ...
m_pClientList[i]->m_iTimeLeft_ShutUp = wTime * 20;
SendNotifyMsg(NULL, i, DEF_NOTIFY_PLAYERSHUTUP, wTime, NULL, NULL, pPlayer);
```

### Automatic Silence Decrement

**File**: `Game.cpp:3574` (in timer/tick handler)

```cpp
m_pClientList[i]->m_iTimeLeft_ShutUp--;
if (m_pClientList[i]->m_iTimeLeft_ShutUp < 0)
    m_pClientList[i]->m_iTimeLeft_ShutUp = 0;
```

The silence time decrements every server tick (approximately 3 seconds per tick for 20 ticks/minute).

---

## Bad Word Filtering

### bCheckBadWord

**File**: `Game.cpp:32518`

```cpp
BOOL CGame::bCheckBadWord(char * pString);
```

**Note**: The function is defined but appears to be incomplete/disabled in this codebase. It returns `FALSE` without performing actual filtering.

### Bad Word Monitor System

**File**: `Game.cpp:36954`

```cpp
void CGame::_BWM_Init(int iClientH, char *pData);
```

Registers a client as a "Bad Word Monitor" (`m_bIsBWMonitor = TRUE`).

```cpp
void CGame::_BWM_Command_Shutup(char *pData);
```

Automatically mutes a player for 10 minutes when detected by the BWM system:
```cpp
m_pClientList[i]->m_iTimeLeft_ShutUp = 20*3*10;  // 10 minutes
```

---

## Admin/GM Chat Features

### Admin Level Constants

**File**: `Game.h:956-998`

```cpp
int m_iAdminLevelWho;           // Required level for /who command
int m_iAdminLevelShutup;        // Required level for /shutup command
int m_iAdminLevelGoto;          // Required level for /goto command
// ... many more
```

### ShowClientMsg

**File**: `Game.cpp:49065`

```cpp
void CGame::ShowClientMsg(int iClientH, char* pMsg);
```

Sends a server-generated message to a specific client. Used for system notifications, version info, etc.

**Message Format**:
- MSGID: `MSGID_COMMAND_CHATMSG` (0x03203204)
- Sender: "HGServer"
- Chat Type: 10 (admin broadcast style)
- Max Length: 50 characters

### ShowVersion

**File**: `Game.cpp:45219`

**Command**: `/version`

```cpp
void CGame::ShowVersion(int iClientH);
```

Displays server version to the player.

### CheckAndNotifyPlayerConnection

**File**: `Game.cpp:31179`

**Command**: `/fi <playername>` (find player)

```cpp
void CGame::CheckAndNotifyPlayerConnection(int iClientH, char * pMsg, DWORD dwSize);
```

- For regular players: Returns `DEF_NOTIFY_PLAYERONGAME` or `DEF_NOTIFY_PLAYERNOTONGAME`
- For GMs: Also includes map name and coordinates
- Supports cross-server lookup via Gate Server

---

## Notification Messages

### SendNotifyMsg

**File**: `Game.cpp:13762`

**Signature**:
```cpp
void CGame::SendNotifyMsg(int iFromH, int iToH, WORD wMsgType,
    DWORD sV1, DWORD sV2, DWORD sV3, char * pString,
    DWORD sV4 = NULL, DWORD sV5 = NULL, DWORD sV6 = NULL,
    DWORD sV7 = NULL, DWORD sV8 = NULL, DWORD sV9 = NULL,
    char * pString2 = NULL);
```

This is the primary function for sending notifications to clients.

**Chat-Related Notification Types**:

| Constant | Value | Description |
|----------|-------|-------------|
| `DEF_NOTIFY_WHISPERMODEON` | 0x0B35 | Whisper mode enabled |
| `DEF_NOTIFY_WHISPERMODEOFF` | 0x0B36 | Whisper mode disabled |
| `DEF_NOTIFY_PLAYERPROFILE` | 0x0B37 | Player profile response |
| `DEF_NOTIFY_PLAYERSHUTUP` | 0x0B42 | Player silenced notification |
| `DEF_NOTIFY_ADMINUSERLEVELLOW` | 0x0B43 | Admin level too low |
| `DEF_NOTIFY_NOTICEMSG` | 0x0B46 | General notice message |
| `DEF_NOTIFY_PLAYERONGAME` | 0x0B33 | Player is online |
| `DEF_NOTIFY_PLAYERNOTONGAME` | 0x0B34 | Player is offline |
| `DEF_NOTIFY_TOTALUSERS` | 0x0B29 | Total users count |

---

## Inter-Server Communication

### Gate Server Message Types

**File**: `NetMessages.h:450-474`

```cpp
#define GSM_REQUEST_FINDCHARACTER     0x01  // Find player on other servers
#define GSM_RESPONSE_FINDCHARACTER    0x02  // Response with player info
#define GSM_CHATMSG                   0x0C  // Cross-server chat
#define GSM_WHISFERMSG                0x0D  // Cross-server whisper
#define GSM_REQUEST_SHUTUPPLAYER      0x10  // Cross-server mute request
#define GSM_RESPONSE_SHUTUPPLAYER     0x11  // Cross-server mute response
```

### Gate Server Message Handling

**File**: `Game.cpp:42253` (GSM_CHATMSG)

```cpp
case GSM_CHATMSG:
    cp++;
    iV1 = *cp;           // Chat type (1=guild, 2/10=broadcast)
    cp++;
    ip = (int *)cp;
    iV2 = *ip;           // Guild GUID (for guild chat)
    cp += 4;
    memcpy(cName, cp, 10);  // Sender name
    cp += 10;
    sp = (short *)cp;
    wV1 = (WORD)*sp;     // Message length
    cp += 2;
    ChatMsgHandlerGSM(iV1, iV2, cName, cp, wV1);
    break;
```

---

## Chat Commands

All chat commands start with `/` and are processed in `ChatMsgHandler`.

### Player Commands

| Command | Description | Requirements |
|---------|-------------|--------------|
| `/to <name>` | Enable whisper to player | None |
| `/to` | Disable whisper mode | Whisper enabled |
| `/fi <name>` | Find if player is online | None |
| `/pf <name>` | View player profile | None |
| `/setpf <text>` | Set own profile | None |
| `/who` | Show online player count | Configurable admin level |
| `/version` | Show server version | None |
| `/createparty` | Create new party | Admin level > 1 |
| `/joinparty <name>` | Request to join party | None |
| `/dismissparty` | Leave current party | In party |
| `/getpartyinfo` | Get party member info | In party |
| `/deleteparty` | Disband party | Party leader |
| `/rep+ <name>` | Increase player reputation | Level 40+, no PK, location set |
| `/rep- <name>` | Decrease player reputation | Level 40+, no PK, location set |
| `/hold` | Make summoned mob stop | Has summon |
| `/free` | Release summoned mob control | Has summon |
| `/tgt <name>` | Set summon target | Has summon |
| `/ban` | Ban guild member | Guild master |
| `/dissmiss <name>` | Dismiss guild member | Guild master |
| `/reservefightzone` | Reserve fight zone | Configurable |

### Admin Commands

| Command | Admin Level | Description |
|---------|-------------|-------------|
| `/shutup <name> <min>` | `m_iAdminLevelShutup` | Silence player |
| `/goto <name>` | `m_iAdminLevelGoto` | Teleport to player |
| `/summon <npc>` | `m_iAdminLevelSummon` | Summon NPC |
| `/summonplayer <name>` | `m_iAdminLevelSummonPlayer` | Summon player |
| `/summonall` | `m_iAdminLevelSummonAll` | Summon all players |
| `/summonguild` | `m_iAdminLevelSummonGuild` | Summon guild members |
| `/kill <name>` | `m_iAdminLevelGMKill` | Kill player |
| `/revive <name>` | `m_iAdminLevelGMRevive` | Revive player |
| `/closeconn <name>` | `m_iAdminLevelGMCloseconn` | Disconnect player |
| `/teleport <map> [x] [y]` | `m_iAdminLevelTeleport` | Teleport to location |
| `/createitem <name>` | `m_iAdminLevelCreateItem` | Create item |
| `/checkip <name>` | `m_iAdminLevelCheckIP` | Check player IP |
| `/checkrep <name>` | `m_iAdminLevelGMCheckRep` | Check player reputation |
| `/checkstatus <name>` | `m_iAdminLevelCheckStatus` | Check player stats |
| `/polymorph <type>` | `m_iAdminLevelPolymorph` | Polymorph self |
| `/setinvi` | `m_iAdminLevelSetInvis` | Toggle invisibility |
| `/setzerk` | `m_iAdminLevelSetZerk` | Toggle berserk mode |
| `/setfreeze` | `m_iAdminLevelSetIce` | Freeze/unfreeze target |
| `/setstatus` | `m_iAdminLevelSetStatus` | Modify status |
| `/setattackmode` | `m_iAdminLevelSetAttackMode` | Set NPC attack mode |
| `/setobservermode` | `m_iAdminLevelObserver` | Toggle observer mode |
| `/weather <type>` | `m_iAdminLevelWeather` | Change weather |
| `/time <hour>` | `m_iAdminLevelTime` | Change game time |
| `/storm` | `m_iAdminLevelStorm` | Create storm effect |
| `/monstercount` | `m_iAdminLevelMonsterCount` | Count monsters |
| `/setforcerecalltime` | `m_iAdminLevelSetRecallTime` | Set recall timer |
| `/disconnectall` | `m_iAdminLevelDisconnectAll` | Disconnect all players |
| `/energysphere` | `m_iAdminLevelEnergySphere` | Create energy sphere |
| `/getticket` | 2+ | Get fight zone ticket |
| `/send <name>` | `m_iAdminLevelPushPlayer` | Push player location |
| `/begincrusadetotalwar` | 4+ | Start crusade |
| `/endcrusadetotalwar` | 4+ | End crusade |
| `/beginheldenian` | 3+ | Start Heldenian war |
| `/endheldenian` | 3+ | End Heldenian war |
| `/unsummonall` | `m_iAdminLevelUnsummonAll` | Remove all summons |
| `/unsummonboss` | `m_iAdminLevelUnsummonBoss` | Remove boss NPCs |
| `/clearnpc` | `m_iAdminLevelClearNpc` | Clear all NPCs |
| `/shutdownthisserverrightnow` | `m_iAdminLevelShutdown` | Shutdown server |

---

## Constants and Definitions

### Message IDs

**File**: `NetMessages.h`

```cpp
#define MSGID_COMMAND_CHATMSG           0x03203204   // Chat message packet
#define MSGID_NOTIFY                    0x0FA314D0   // Notification packet
```

### Message Buffer Indices

**File**: `MessageIndex.h`

```cpp
#define DEF_INDEX4_MSGID      0     // 4-byte message ID at offset 0
#define DEF_INDEX2_MSGTYPE    4     // 2-byte message type at offset 4
#define DEF_INDEX10_CHATNAME  10    // 10-byte chat sender name
#define DEF_INDEXX_CHATMSG    20    // Chat message content starts at offset 20
```

### Client Limits

**File**: `Game.h`

```cpp
#define DEF_MAXCLIENTS    2000    // Maximum concurrent clients
```

### Chat Packet Structure

```
Offset  Size  Field
------  ----  -----
0       4     MSGID_COMMAND_CHATMSG (0x03203204)
4       2     Client Index (sender)
6       2     X Position
8       2     Y Position
10      10    Character Name
20      1     Send Mode (chat type)
21      N     Message Content
```

---

## Message Flow Diagrams

### Local Chat Flow

```
Player types message
        |
        v
ChatMsgHandler()
        |
        v
Check silence status
        |
        v
Parse chat type prefix
        |
        v
Calculate send mode
        |
        v
Deduct SP if needed
        |
        v
Build chat packet
        |
        v
For each client in range:
  - Check distance
  - Check faction (during war)
  - Send packet
```

### Whisper Flow (Same Server)

```
/to <playername>
        |
        v
ToggleWhisperPlayer()
        |
        v
Search local clients
        |
        v
Player found?
   |         \
   Yes        No
   |           \
   v            v
Set index    GSM_REQUEST_FINDCHARACTER
   |           to Gate Server
   v            |
NOTIFY_WHISPERMODEON    v
   |           Wait for response
   v            |
Player types    v
message        GSM_RESPONSE_FINDCHARACTER
   |            |
   v            v
ChatMsgHandler() Set index = 10000
cSendMode = 20    |
   |              v
   v           NOTIFY_WHISPERMODEON
Send to self     |
   |              v
   v           Player types message
Send to target    |
(local)           v
               GSM_WHISFERMSG
               to Gate Server
                  |
                  v
               Target server
               delivers message
```

### Guild Chat Flow (Cross-Server)

```
Player types @message
        |
        v
ChatMsgHandler()
        |
        v
Check guild rank
        |
        v
Build GSM_CHATMSG packet
        |
        v
bStockMsgToGateServer()
        |
        v
Gate Server
        |
        v
Distribute to all game servers
        |
        v
Each server: ChatMsgHandlerGSM()
        |
        v
Find clients with matching
Guild GUID
        |
        v
Send to each guild member
```

### Silence/Mute Flow

```
GM: /shutup <player> <time>
        |
        v
ShutUpPlayer()
        |
        v
Search local clients
        |
        v
Player found?
   |         \
   Yes        No
   |           \
   v            v
Set ShutUp   GSM_REQUEST_SHUTUPPLAYER
timer          |
   |           v
   v        Gate Server
NOTIFY_PLAYERSHUTUP    |
to GM and player       v
              Other game servers
                  |
                  v
              GSM_RESPONSE_SHUTUPPLAYER
                  |
                  v
              Set ShutUp timer
                  |
                  v
              NOTIFY_PLAYERSHUTUP
```

---

## Related Files Summary

| File | Purpose |
|------|---------|
| `Msg.h` / `Msg.cpp` | CMsg class for message queue |
| `Client.h` / `Client.cpp` | Client chat-related fields |
| `Game.h` | CGame class declarations, constants |
| `Game.cpp` | All chat handler implementations |
| `NetMessages.h` | Message IDs and notification constants |
| `MessageIndex.h` | Packet structure offsets |

## Key Functions Reference

| Function | Line | Purpose |
|----------|------|---------|
| `ChatMsgHandler` | 8795 | Main chat message processor |
| `ChatMsgHandlerGSM` | 9659 | Cross-server chat handler |
| `ToggleWhisperPlayer` | 31258 | Enable/disable whisper mode |
| `SetPlayerProfile` | 31343 | Set player profile |
| `GetPlayerProfile` | 31366 | Get player profile |
| `ShutUpPlayer` | 32562 | Silence a player |
| `bCheckBadWord` | 32518 | Bad word filter (stub) |
| `_BWM_Init` | 36954 | Register bad word monitor |
| `_BWM_Command_Shutup` | 36963 | Auto-mute from BWM |
| `ShowClientMsg` | 49065 | Send server message to client |
| `ShowVersion` | 45219 | Show server version |
| `CheckAndNotifyPlayerConnection` | 31179 | Find player command |
| `GSM_RequestFindCharacter` | 42652 | Cross-server player lookup |
| `GSM_RequestShutupPlayer` | 45230 | Cross-server mute |
| `SendNotifyMsg` | 13762 | Send notification to client |
| `bPutMsgQuene` | 11536 | Add message to queue |
| `bGetMsgQuene` | 11562 | Get message from queue |

---

## Version History

- **v1.41**: Added ShutUp time feature
- **v1.42**: Added FirmStaminar (stamina freeze) consideration
- **v1.4334**: Added HP check for broadcast during death
- **v2.14**: Changed shutup time to minutes
- **v2.15**: Added cross-server shutup functionality
