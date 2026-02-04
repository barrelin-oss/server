# Legacy Persistence and Logging System Documentation

## Table of Contents

1. [Overview](#overview)
2. [Log Server Architecture](#log-server-architecture)
   - [Socket Connections](#socket-connections)
   - [Connection Management](#connection-management)
   - [Message Routing](#message-routing)
3. [Character Save System](#character-save-system)
   - [Auto-Save Mechanism](#auto-save-mechanism)
   - [Save on Logout](#save-on-logout)
   - [Local Fallback Save](#local-fallback-save)
4. [Character Data File Format](#character-data-file-format)
   - [File Header](#file-header)
   - [Status Section](#status-section)
   - [Item List Section](#item-list-section)
   - [Bank Item List Section](#bank-item-list-section)
   - [Magic and Skill Mastery](#magic-and-skill-mastery)
   - [Equipment Status](#equipment-status)
5. [Guild Data Persistence](#guild-data-persistence)
6. [War and Crusade Data](#war-and-crusade-data)
7. [Local Log Files](#local-log-files)
   - [Log File Types](#log-file-types)
   - [Log Functions](#log-functions)
   - [Log Format](#log-format)
8. [Log Server Messages](#log-server-messages)
9. [Key Functions](#key-functions)
10. [Constants and Definitions](#constants-and-definitions)
11. [Data Flow Diagrams](#data-flow-diagrams)

---

## Overview

The Helbreath legacy persistence system handles all data storage and logging operations:

- **Character Data**: Saving and loading player characters via the Log Server
- **Guild Data**: Creating, disbanding, and updating guild membership
- **War Data**: Crusade occupation flag persistence
- **Action Logging**: Recording server events, admin actions, hack attempts, PvP kills, and item transactions
- **Log Server Communication**: Multi-socket load-balanced connection to centralized logging service

The system uses a hybrid approach:
1. **Primary**: Network-based persistence via Log Server (handles database operations)
2. **Fallback**: Local file saves when Log Server is unavailable
3. **Local Logs**: Text files for event tracking and debugging

---

## Log Server Architecture

### Socket Connections

The game server maintains multiple connections to the Log Server for load balancing and redundancy.

**File**: `Game.h`

```cpp
class CGame {
    // Main log socket - for critical operations
    class XSocket* m_pMainLogSock;
    BOOL m_bIsLogSockAvailable;

    // Sub-log sockets - for load-balanced operations
    class XSocket* m_pSubLogSock[DEF_MAXSUBLOGSOCK];  // 10 sockets
    BOOL m_bIsSubLogSockAvailable[DEF_MAXSUBLOGSOCK];

    // Connection tracking
    int m_iSubLogSockInitIndex;      // Next socket to initialize
    int m_iCurSubLogSockIndex;       // Current socket for round-robin
    int m_iSubLogSockFailCount;      // Failed connection attempts
    int m_iSubLogSockActiveCount;    // Currently active connections

    // Server configuration
    char m_cLogServerAddr[16];       // Log server IP address
    int m_iLogServerPort;            // Log server port
};
```

| Socket Type | Count | Purpose |
|-------------|-------|---------|
| Main Log Socket | 1 | Server registration, shutdown notifications |
| Sub Log Sockets | 10 | Player saves, guild operations, action logging |

### Connection Management

**Main Socket Event Handler** (`Game.cpp:3884`)

```cpp
void CGame::OnMainLogSocketEvent(UINT message, WPARAM wParam, LPARAM lParam)
{
    int iRet;

    if (m_pMainLogSock == NULL) return;

    iRet = m_pMainLogSock->iOnSocketEvent(wParam, lParam);

    switch (iRet) {
    case DEF_XSOCKEVENT_CONNECTIONESTABLISH:
        PutLogList("(!!!) Main-log-socket connected!");
        bSendMsgToLS(MSGID_REQUEST_REGISTERGAMESERVER, NULL);
        break;

    case DEF_XSOCKEVENT_READCOMPLETE:
        OnMainLogRead();
        break;

    case DEF_XSOCKEVENT_SOCKETCLOSED:
        // Critical! Triggers server shutdown
        delete m_pMainLogSock;
        m_pMainLogSock = NULL;
        m_bIsLogSockAvailable = FALSE;

        if (m_bOnExitProcess == FALSE) {
            m_cShutDownCode = 3;
            m_bOnExitProcess = TRUE;
            m_dwExitProcessTime = timeGetTime();
            PutLogList("(!!!) GAME SERVER SHUTDOWN PROCESS BEGIN!!!");
        }
        break;
    }
}
```

**Sub Socket Event Handler** (`Game.cpp:38513`)

```cpp
void CGame::OnSubLogSocketEvent(UINT message, WPARAM wParam, LPARAM lParam)
{
    int iLogSockH = (message - WM_ONLOGSOCKETEVENT) - 1;
    int iRet;

    if (m_pSubLogSock[iLogSockH] == NULL) return;

    iRet = m_pSubLogSock[iLogSockH]->iOnSocketEvent(wParam, lParam);

    switch (iRet) {
    case DEF_XSOCKEVENT_CONNECTIONESTABLISH:
        m_bIsSubLogSockAvailable[iLogSockH] = TRUE;
        m_iSubLogSockActiveCount++;
        break;

    case DEF_XSOCKEVENT_READCOMPLETE:
        OnSubLogRead(iLogSockH);
        break;

    case DEF_XSOCKEVENT_SOCKETCLOSED:
        // Attempt reconnection
        delete m_pSubLogSock[iLogSockH];
        m_pSubLogSock[iLogSockH] = NULL;
        m_bIsSubLogSockAvailable[iLogSockH] = FALSE;
        m_iSubLogSockFailCount++;
        m_iSubLogSockActiveCount--;

        // Create new connection
        m_pSubLogSock[iLogSockH] = new class XSocket(m_hWnd, DEF_SERVERSOCKETBLOCKLIMIT);
        m_pSubLogSock[iLogSockH]->bConnect(m_cLogServerAddr, m_iLogServerPort,
                                          (WM_ONLOGSOCKETEVENT + iLogSockH + 1));
        m_pSubLogSock[iLogSockH]->bInitBufferSize(DEF_MSGBUFFERSIZE);
        break;
    }
}
```

### Message Routing

Sub-log sockets use round-robin distribution for load balancing.

**Socket Index Selection** (`Game.cpp:38603`)

```cpp
BOOL CGame::_bCheckSubLogSocketIndex()
{
    int iCnt = 0;
    BOOL bLoopFlag = FALSE;

    m_iCurSubLogSockIndex++;
    if (m_iCurSubLogSockIndex >= DEF_MAXSUBLOGSOCK)
        m_iCurSubLogSockIndex = 0;

    // Find next available socket
    while (bLoopFlag == FALSE) {
        if ((m_pSubLogSock[m_iCurSubLogSockIndex] != NULL) &&
            (m_bIsSubLogSockAvailable[m_iCurSubLogSockIndex] == TRUE))
            bLoopFlag = TRUE;
        else
            m_iCurSubLogSockIndex++;

        iCnt++;
        if (iCnt >= DEF_MAXSUBLOGSOCK) {
            // No available sockets - trigger shutdown
            if (m_bOnExitProcess == FALSE) {
                m_cShutDownCode = 3;
                m_bOnExitProcess = TRUE;
                m_dwExitProcessTime = timeGetTime();
                PutLogList("(!) GAME SERVER SHUTDOWN PROCESS STARTED(by Log-server connection Lost)!!!");
            }
            return FALSE;
        }
    }
    return TRUE;
}
```

---

## Character Save System

### Auto-Save Mechanism

Characters are automatically saved every 30 minutes while logged in (unless in Fight Zone).

**Auto-Save Logic** (`Game.cpp:3627`)

```cpp
// In main game loop timer processing
if ((m_pMapList[m_pClientList[i]->m_cMapIndex]->m_bIsFightZone == FALSE) &&
    ((dwTime - m_pClientList[i]->m_dwAutoSaveTime) > (DWORD)DEF_AUTOSAVETIME)) {

    bSendMsgToLS(MSGID_REQUEST_SAVEPLAYERDATA, i);
    m_pClientList[i]->m_dwAutoSaveTime = dwTime;
}
```

| Constant | Value | Description |
|----------|-------|-------------|
| `DEF_AUTOSAVETIME` | 30 minutes | Auto-save interval |

### Save on Logout

When a player disconnects, their data is saved before cleanup.

**Logout Save Flow** (`Game.cpp:2439`)

```cpp
void CGame::DeleteClient(int iClientH, BOOL bSave, BOOL bNotify,
                         BOOL bCountLogout, BOOL bForceCloseConn)
{
    // ... cleanup code ...

    if ((bSave == TRUE) && (m_pClientList[iClientH]->m_bIsOnServerChange == FALSE)) {
        // Handle death respawn location
        if (m_pClientList[iClientH]->m_bIsKilled == TRUE) {
            m_pClientList[iClientH]->m_sX = -1;
            m_pClientList[iClientH]->m_sY = -1;
            // Set respawn map based on faction
            // ...
        }

        if (m_pClientList[iClientH]->m_bIsInitComplete == TRUE) {
            // Try to save via Log Server, fallback to local
            if (bSendMsgToLS(MSGID_REQUEST_SAVEPLAYERDATALOGOUT, iClientH, bCountLogout) == FALSE) {
                LocalSavePlayerData(iClientH);
            }
        }
        else {
            // Character not fully loaded - notify logout without save
            bSendMsgToLS(MSGID_REQUEST_NOSAVELOGOUT, iClientH, bCountLogout);
        }
    }

    // ... delete client object ...
}
```

**Save Message Types**:

| Message ID | Purpose |
|------------|---------|
| `MSGID_REQUEST_SAVEPLAYERDATA` | Periodic auto-save |
| `MSGID_REQUEST_SAVEPLAYERDATALOGOUT` | Save on logout |
| `MSGID_REQUEST_SAVEPLAYERDATA_REPLY` | Server change save |
| `MSGID_REQUEST_NOSAVELOGOUT` | Logout without save (partial load) |

### Local Fallback Save

When Log Server is unavailable, character data is saved locally.

**Local Save Function** (`Game.cpp:35242`)

```cpp
void CGame::LocalSavePlayerData(int iClientH)
{
    char *pData, *cp, cFn[256], cDir[256], cTxt[256], cCharDir[256];
    int iSize;
    FILE *pFile;
    SYSTEMTIME SysTime;

    if (m_pClientList[iClientH] == NULL) return;

    // Allocate buffer for character data
    pData = new char[30000];
    if (pData == NULL) return;
    ZeroMemory(pData, 30000);

    cp = (char*)(pData);
    iSize = _iComposePlayerDataFileContents(iClientH, cp);

    // Create timestamped directory
    GetLocalTime(&SysTime);
    wsprintf(cCharDir, "Character_%d_%d_%d_%d",
             SysTime.wMonth, SysTime.wDay, SysTime.wHour, SysTime.wMinute);

    // Build file path: CharDir/MeC77X/CharacterName.txt
    // (X is first character of name as hex)
    wsprintf(cTxt, "MeC77%d", (unsigned char)m_pClientList[iClientH]->m_cCharName[0]);
    // ...

    // Create directories
    _mkdir(cCharDir);
    _mkdir(cDir);

    // Write file
    if (iSize == 0) {
        PutLogList("(!) Character data body empty: Cannot save.");
        delete pData;
        return;
    }

    pFile = fopen(cFn, "wt");
    if (pFile != NULL) {
        wsprintf(cTxt, "(!) temporal player data file saved: Name(%s)", cFn);
        PutLogList(cTxt);
        fwrite(cp, iSize, 1, pFile);
        fclose(pFile);
    }

    delete pData;
}
```

---

## Character Data File Format

Character data is stored in a structured text format with sections.

### File Header

```
[FILE-DATE]

file-saved-date: 2004 11 22 15 30

[NAME-ACCOUNT]

character-name     = PlayerName
account-name       = AccountName
```

### Status Section

```
[STATUS]

character-profile   = Player profile text here
character-location  = aresden
character-guild-name = GuildName
character-guild-GUID = 12345
character-guild-rank = 5
character-loc-map = aresden
character-loc-x   = 150
character-loc-y   = 200

character-HP       = 500
character-MP       = 300
character-SP       = 100
character-LEVEL    = 50
character-RATING   = 1000
character-STR      = 50
character-INT      = 40
character-VIT      = 45
character-DEX      = 55
character-MAG      = 35
character-CHARISMA = 30
character-LUCK     = 20
character-EXP      = 1500000
character-LU_Pool  = 10
character-EK-Count = 100
character-PK-Count = 5
character-reward-gold = 50000
character-downskillindex = 0
character-IDnum1 = 12345
character-IDnum2 = 67890
character-IDnum3 = 11111
sex-status       = 1
skin-status      = 2
hairstyle-status = 3
haircolor-status = 1
underwear-status = 0
hunger-status    = 100
timeleft-shutup  = 0
timeleft-rating  = 0
timeleft-force-recall  = 0
timeleft-firm-staminar = 0
admin-user-level = 0
penalty-block-date = 0 0 0
character-quest-number = 1
character-quest-ID     = 5
current-quest-count    = 3
quest-reward-type      = 1
quest-reward-amount    = 1000
character-contribution = 500
character-war-contribution = 200
character-quest-completed = 1
special-event-id = 0
super-attack-left = 3
reserved-fightzone-id = 0 0 0
special-ability-time = 0
locked-map-name = NONE
locked-map-time = 0
crusade-job = 0
crusade-GUID = 0
construct-point = 0
dead-penalty-time = 0
party-id = 0
gizon-item-upgade-left = 3

appr1 = 0
appr2 = 256
appr3 = 0
appr4 = 0
appr-color = 0
```

### Item List Section

```
[ITEMLIST]

character-item = SwordOfIce          1 0 0 0 0 0 1 0 0 0 500 0
character-item = HealthPotion        10 0 0 0 0 0 0 0 0 0 100 0
```

**Item Format**: `name count touchType touchVal1 touchVal2 touchVal3 color specVal1 specVal2 specVal3 lifespan attribute`

| Field | Description |
|-------|-------------|
| name | Item name (20 chars, space-padded) |
| count | Stack count |
| touchType | Touch effect type |
| touchVal1-3 | Touch effect values |
| color | Item color |
| specVal1-3 | Special effect values |
| lifespan | Current durability |
| attribute | Item attributes bitfield |

### Bank Item List Section

```
[BANKITEMLIST]

character-bank-item = GoldBar           5 0 0 0 0 0 0 0 0 1000 0
```

Same format as inventory items but stored in bank (120 slot limit).

### Magic and Skill Mastery

```
[MAGIC-SKILL-MASTERY]

//------------------012345678901234567890123456789012345678901234567890
magic-mastery     = 111111111100000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000
skill-mastery     = 100 80 60 50 40 30 20 10 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0
skill-SSN     = 1000 500 300 200 100 50 20 10 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0
```

| Field | Description |
|-------|-------------|
| magic-mastery | 100 binary digits (0/1) for each spell learned |
| skill-mastery | 60 space-separated values (0-255) for skill levels |
| skill-SSN | 60 space-separated values for skill experience |

### Equipment Status

```
[ITEM-EQUIP-STATUS]

item-equip-status = 11100000000000000000000000000000000000000000000000
item-position-x = 10 50 90 130 170 40 40 40 40 40 40 40 40 40 40 40 40 40 40 40 40 40 40 40 40 40 40 40 40 40 40 40 40 40 40 40 40 40 40 40 40 40 40 40 40 40 40 40 40 40
item-position-y = 10 10 10 10 10 30 30 30 30 30 30 30 30 30 30 30 30 30 30 30 30 30 30 30 30 30 30 30 30 30 30 30 30 30 30 30 30 30 30 30 30 30 30 30 30 30 30 30 30 30

[EOF]
```

| Field | Description |
|-------|-------------|
| item-equip-status | 50 binary digits (1 = equipped) |
| item-position-x | 50 space-separated X coordinates in inventory grid |
| item-position-y | 50 space-separated Y coordinates in inventory grid |

---

## Guild Data Persistence

Guild operations are sent to the Log Server for database persistence.

**Guild Messages via `bSendMsgToLS`** (`Game.cpp:4279-4428`)

### Create New Guild

```cpp
case MSGID_REQUEST_CREATENEWGUILD:
    // Sends: CharName(10) + AccountName(10) + AccountPass(10) +
    //        GuildName(20) + GuildLocation(10) + GuildGUID(4)
    // Total: 75 bytes
    break;
```

### Disband Guild

```cpp
case MSGID_REQUEST_DISBANDGUILD:
    // Sends: CharName(10) + AccountName(10) + AccountPass(10) + GuildName(20)
    // Total: 56 bytes
    break;
```

### Update Guild Membership

```cpp
case MSGID_REQUEST_UPDATEGUILDINFO_NEWGUILDSMAN:
case MSGID_REQUEST_UPDATEGUILDINFO_DELGUILDSMAN:
    // Sends: CharName(10) + GuildName(20)
    // Total: 36 bytes
    break;
```

| Message ID | Purpose |
|------------|---------|
| `MSGID_REQUEST_CREATENEWGUILD` | Create a new guild |
| `MSGID_REQUEST_DISBANDGUILD` | Disband existing guild |
| `MSGID_REQUEST_UPDATEGUILDINFO_NEWGUILDSMAN` | Add member to guild |
| `MSGID_REQUEST_UPDATEGUILDINFO_DELGUILDSMAN` | Remove member from guild |

---

## War and Crusade Data

Occupation flag data is saved locally to track territory control.

**Save Occupation Flags** (`Game.cpp:41365`)

```cpp
void CGame::SaveOccupyFlagData()
{
    char *pData;
    int iSize;
    FILE *pFile;

    PutLogList("(!) Middleland OccupyFlag data saved.");

    // Allocate large buffer for flag data
    pData = new char[1000000+1];
    if (pData == NULL) return;
    ZeroMemory(pData, 1000000);

    // Compose flag status (up to 20,001 flags)
    iSize = _iComposeFlagStatusContents(pData);

    // Ensure directory exists
    _mkdir("GameData");

    // Write to file
    pFile = fopen("GameData\\OccupyFlag.txt", "wt");
    if (pFile == NULL) return;

    fwrite(pData, 1, iSize, pFile);

    delete pData;
    fclose(pFile);
}
```

**File Location**: `GameData\OccupyFlag.txt`

---

## Local Log Files

### Log File Types

All logs are stored in the `GameLogs\` directory.

| Log File | Function | Purpose |
|----------|----------|---------|
| `Events.log` | `PutLogFileList` | General server events |
| `AdminEvents.log` | `PutAdminLogFileList` | GM/Admin actions |
| `HackEvents.log` | `PutHackLogFileList` | Hack detection and suspicious activity |
| `PvPEvents.log` | `PutPvPLogFileList` | Player vs Player kills |
| `ItemEvents.log` | `PutItemLogFileList` | Item transactions (trades, drops, pickups) |
| `XSocket.log` | `PutXSocketLogFileList` | Network socket events |
| `LogEvents.log` | `PutLogEventFileList` | Login/logout events |

### Log Functions

**File**: `Wmain.cpp`

All log functions follow the same pattern:

```cpp
void PutLogFileList(char *cStr)
{
    FILE *pFile;
    char cBuffer[512];
    SYSTEMTIME SysTime;

    // Open in append mode
    pFile = fopen("GameLogs\\Events.log", "at");
    if (pFile == NULL) return;

    ZeroMemory(cBuffer, sizeof(cBuffer));

    // Add timestamp
    GetLocalTime(&SysTime);
    wsprintf(cBuffer, "(%4d:%2d:%2d:%2d:%2d) - ",
             SysTime.wYear, SysTime.wMonth, SysTime.wDay,
             SysTime.wHour, SysTime.wMinute);
    strcat(cBuffer, cStr);
    strcat(cBuffer, "\n");

    // Write and close
    fwrite(cBuffer, 1, strlen(cBuffer), pFile);
    fclose(pFile);
}
```

### Log Format

```
(2004:11:22:15:30) - Message text here
(2004:11:22:15:31) - Another message
```

**Format**: `(YYYY:MM:DD:HH:MM) - message\n`

### Log Usage Examples

**Events.log** - General events:
```cpp
PutLogFileList("Server started successfully");
PutLogFileList("Map 'aresden' loaded with 500 NPCs");
```

**AdminEvents.log** - GM actions:
```cpp
wsprintf(G_cTxt, "GM(%s) created item(%s) for player(%s)",
         gmName, itemName, targetPlayer);
PutAdminLogFileList(G_cTxt);
```

**HackEvents.log** - Suspicious activity:
```cpp
wsprintf(G_cTxt, "(HACK?) Speed hack detected: Player(%s) IP(%s)",
         playerName, ipAddr);
PutHackLogFileList(G_cTxt);
```

**PvPEvents.log** - Player kills:
```cpp
wsprintf(cTxt, "PK: %s killed %s at %s (%d,%d)",
         killerName, victimName, mapName, x, y);
PutPvPLogFileList(cTxt);
```

**ItemEvents.log** - Item transactions:
```cpp
wsprintf(cTxt, "Trade: %s gave %s(%d) to %s",
         giverName, itemName, count, receiverName);
PutItemLogFileList(cTxt);
```

---

## Log Server Messages

The game server sends various messages to the Log Server for centralized logging.

### Item Transaction Log

```cpp
case MSGID_GAMEITEMLOG:
    // Sends log string to Log Server for database storage
    // Used for important item events (rare drops, trades, etc.)
    if (_bCheckSubLogSocketIndex() == FALSE) return FALSE;

    dwp = (DWORD*)(G_cData50000 + DEF_INDEX4_MSGID);
    *dwp = MSGID_GAMEITEMLOG;
    wp = (WORD*)(G_cData50000 + DEF_INDEX2_MSGTYPE);
    *wp = DEF_MSGTYPE_CONFIRM;

    cp = (char*)(G_cData50000 + DEF_INDEX2_MSGTYPE + 2);
    iSize = strlen(pData);
    memcpy(cp, pData, iSize);

    iRet = m_pSubLogSock[m_iCurSubLogSockIndex]->iSendMsg(G_cData50000, 6 + iSize);
    break;
```

### GM Action Log

```cpp
case MSGID_GAMEMASTERLOG:
    // Logs GM commands and actions to Log Server
    // Same structure as MSGID_GAMEITEMLOG
    break;
```

**Usage Examples**:

```cpp
// Log GM item creation
wsprintf(cTemp, "GM(%s) created item(%s) x%d for player(%s)",
         gmName, itemName, count, targetName);
bSendMsgToLS(MSGID_GAMEMASTERLOG, iClientH, FALSE, cTemp);

// Log rare item drop
wsprintf(cTxt, "RareItem: %s dropped %s from NPC(%s)",
         playerName, itemName, npcName);
bSendMsgToLS(MSGID_GAMEITEMLOG, iClientH, NULL, cTxt);
```

---

## Key Functions

### Core Persistence Functions

| Function | File:Line | Purpose |
|----------|-----------|---------|
| `_iComposePlayerDataFileContents` | `Game.cpp:7350` | Creates character data text format |
| `bSendMsgToLS` | `Game.cpp:3938` | Sends message to Log Server |
| `LocalSavePlayerData` | `Game.cpp:35242` | Fallback local file save |
| `DeleteClient` | `Game.cpp:2439` | Handles logout with save |
| `ResponseSavePlayerDataReplyHandler` | `Game.cpp:32812` | Handles save confirmations |
| `SaveOccupyFlagData` | `Game.cpp:41365` | Saves war territory data |

### Socket Management Functions

| Function | File:Line | Purpose |
|----------|-----------|---------|
| `OnMainLogSocketEvent` | `Game.cpp:3884` | Main socket event handler |
| `OnMainLogRead` | `Game.cpp:3925` | Process main socket data |
| `OnSubLogSocketEvent` | `Game.cpp:38513` | Sub socket event handler |
| `OnSubLogRead` | `Game.cpp:38571` | Process sub socket data |
| `_bCheckSubLogSocketIndex` | `Game.cpp:38603` | Round-robin socket selection |

### Log File Functions

| Function | File:Line | Purpose |
|----------|-----------|---------|
| `PutLogFileList` | `Wmain.cpp:666` | Write to Events.log |
| `PutAdminLogFileList` | `Wmain.cpp:685` | Write to AdminEvents.log |
| `PutHackLogFileList` | `Wmain.cpp:705` | Write to HackEvents.log |
| `PutPvPLogFileList` | `Wmain.cpp:725` | Write to PvPEvents.log |
| `PutItemLogFileList` | `Wmain.cpp:765` | Write to ItemEvents.log |
| `PutXSocketLogFileList` | `Wmain.cpp:745` | Write to XSocket.log |
| `PutLogEventFileList` | `Wmain.cpp:785` | Write to LogEvents.log |

---

## Constants and Definitions

### Timing Constants

| Constant | Value | Description |
|----------|-------|-------------|
| `DEF_AUTOSAVETIME` | 30 minutes | Auto-save interval |

### Socket Constants

| Constant | Value | Description |
|----------|-------|-------------|
| `DEF_MAXSUBLOGSOCK` | 10 | Maximum sub-log socket connections |
| `DEF_SERVERSOCKETBLOCKLIMIT` | - | Socket blocking limit |
| `DEF_MSGBUFFERSIZE` | - | Message buffer size |

### Message IDs

**File**: `NetMessages.h`

| Constant | Value | Description |
|----------|-------|-------------|
| `MSGID_REQUEST_REGISTERGAMESERVER` | - | Register with Log Server |
| `MSGID_REQUEST_SAVEPLAYERDATA` | - | Save character (auto-save) |
| `MSGID_REQUEST_SAVEPLAYERDATALOGOUT` | - | Save character (logout) |
| `MSGID_REQUEST_SAVEPLAYERDATA_REPLY` | - | Save for server change |
| `MSGID_REQUEST_NOSAVELOGOUT` | - | Logout without save |
| `MSGID_GAMEMASTERLOG` | `0x210A914E` | Log GM action |
| `MSGID_GAMEITEMLOG` | `0x210A914F` | Log item transaction |
| `MSGID_REQUEST_CREATENEWGUILD` | - | Create guild |
| `MSGID_REQUEST_DISBANDGUILD` | - | Disband guild |
| `MSGID_REQUEST_UPDATEGUILDINFO_NEWGUILDSMAN` | - | Add guild member |
| `MSGID_REQUEST_UPDATEGUILDINFO_DELGUILDSMAN` | - | Remove guild member |
| `MSGID_SENDSERVERSHUTDOWNMSG` | - | Server shutdown notification |
| `MSGID_GAMESERVERSHUTDOWNED` | - | Server shutdown complete |

### Message Sources

| Constant | Value | Description |
|----------|-------|-------------|
| `DEF_MSGFROM_CLIENT` | 1 | From game client |
| `DEF_MSGFROM_LOGSERVER` | 2 | From Log Server |
| `DEF_MSGFROM_GATESERVER` | 3 | From Gate Server |
| `DEF_MSGFROM_BOT` | 4 | From automated system |

---

## Data Flow Diagrams

### Character Save Flow

```
┌─────────────────────────────────────────────────────────────────────┐
│                      CHARACTER SAVE FLOW                            │
├─────────────────────────────────────────────────────────────────────┤
│                                                                     │
│  ┌──────────────┐     ┌──────────────┐     ┌──────────────────┐   │
│  │  Auto-Save   │     │   Logout     │     │  Server Change   │   │
│  │   Timer      │     │   Request    │     │    Request       │   │
│  └──────┬───────┘     └──────┬───────┘     └────────┬─────────┘   │
│         │                    │                      │              │
│         └────────────┬───────┴──────────────────────┘              │
│                      │                                              │
│                      ▼                                              │
│         ┌────────────────────────────┐                             │
│         │ _iComposePlayerDataFile    │                             │
│         │       Contents()           │                             │
│         └────────────┬───────────────┘                             │
│                      │                                              │
│                      ▼                                              │
│         ┌────────────────────────────┐                             │
│         │    bSendMsgToLS()          │                             │
│         │ MSGID_REQUEST_SAVEPLAYERDATA                             │
│         └────────────┬───────────────┘                             │
│                      │                                              │
│              ┌───────┴───────┐                                     │
│              │               │                                     │
│        Success?         Failed?                                    │
│              │               │                                     │
│              ▼               ▼                                     │
│    ┌─────────────┐   ┌───────────────────┐                        │
│    │ Log Server  │   │ LocalSavePlayer   │                        │
│    │  Database   │   │     Data()        │                        │
│    └─────────────┘   └───────────────────┘                        │
│                              │                                     │
│                              ▼                                     │
│                     ┌─────────────────┐                           │
│                     │ Local .txt file │                           │
│                     │  Character_M_D_  │                           │
│                     │    H_M/...      │                           │
│                     └─────────────────┘                           │
│                                                                    │
└────────────────────────────────────────────────────────────────────┘
```

### Log Server Connection Architecture

```
┌─────────────────────────────────────────────────────────────────────┐
│                   LOG SERVER ARCHITECTURE                           │
├─────────────────────────────────────────────────────────────────────┤
│                                                                     │
│      Game Server                              Log Server            │
│  ┌──────────────────────┐              ┌──────────────────────┐   │
│  │                      │              │                      │   │
│  │  ┌────────────────┐  │              │                      │   │
│  │  │ m_pMainLogSock │──┼──────────────┼─► Registration       │   │
│  │  │   (Critical)   │  │              │   Shutdown msgs      │   │
│  │  └────────────────┘  │              │                      │   │
│  │                      │              │                      │   │
│  │  ┌────────────────┐  │              │                      │   │
│  │  │m_pSubLogSock[0]│──┼──────────────┼─► Player saves       │   │
│  │  └────────────────┘  │              │   Guild ops          │   │
│  │  ┌────────────────┐  │              │   Item logs          │   │
│  │  │m_pSubLogSock[1]│──┼──────────────┼─► GM action logs     │   │
│  │  └────────────────┘  │              │                      │   │
│  │         ...          │              │                      │   │
│  │  ┌────────────────┐  │              │                      │   │
│  │  │m_pSubLogSock[9]│──┼──────────────┼─►                    │   │
│  │  └────────────────┘  │              │                      │   │
│  │                      │              │    ┌──────────────┐  │   │
│  │  Round-robin         │              │    │   Database   │  │   │
│  │  distribution        │              │    │  PostgreSQL  │  │   │
│  │                      │              │    └──────────────┘  │   │
│  └──────────────────────┘              └──────────────────────┘   │
│                                                                     │
└─────────────────────────────────────────────────────────────────────┘
```

### Local Log File Flow

```
┌─────────────────────────────────────────────────────────────────────┐
│                     LOCAL LOGGING FLOW                              │
├─────────────────────────────────────────────────────────────────────┤
│                                                                     │
│  Game Event                                                         │
│      │                                                              │
│      ▼                                                              │
│  ┌─────────────────┐                                               │
│  │ wsprintf(cTxt,  │                                               │
│  │  "Event: %s",   │                                               │
│  │   details);     │                                               │
│  └────────┬────────┘                                               │
│           │                                                         │
│           ▼                                                         │
│  ┌───────────────────────────────────────────────────────┐         │
│  │              Select Appropriate Log Function           │         │
│  ├───────────────────────────────────────────────────────┤         │
│  │ PutLogFileList()      → GameLogs\Events.log           │         │
│  │ PutAdminLogFileList() → GameLogs\AdminEvents.log      │         │
│  │ PutHackLogFileList()  → GameLogs\HackEvents.log       │         │
│  │ PutPvPLogFileList()   → GameLogs\PvPEvents.log        │         │
│  │ PutItemLogFileList()  → GameLogs\ItemEvents.log       │         │
│  └───────────────────────────────────────────────────────┘         │
│           │                                                         │
│           ▼                                                         │
│  ┌─────────────────────────────────────────────────────┐           │
│  │ FILE* pFile = fopen("GameLogs\\X.log", "at");       │           │
│  │ GetLocalTime(&SysTime);                              │           │
│  │ wsprintf(cBuffer, "(%04d:%02d:%02d:%02d:%02d) - %s", │           │
│  │          Year, Month, Day, Hour, Minute, cTxt);      │           │
│  │ fwrite(cBuffer, 1, strlen(cBuffer), pFile);          │           │
│  │ fclose(pFile);                                       │           │
│  └─────────────────────────────────────────────────────┘           │
│                                                                     │
└─────────────────────────────────────────────────────────────────────┘
```

---

## Notes

1. **Critical Dependency**: The game server requires Log Server connectivity. If the main log socket disconnects, the server initiates shutdown.

2. **Data Integrity**: Character saves use the Log Server for atomic database operations. Local saves are only used as emergency fallback.

3. **Load Balancing**: The 10 sub-log sockets distribute save operations using round-robin to prevent bottlenecks.

4. **Fight Zone Exception**: Characters in Fight Zones are not auto-saved to prevent exploitation.

5. **Log File Management**: Log files grow unbounded and should be rotated externally. Each log entry opens and closes the file, which is inefficient but ensures data is flushed.

6. **Character Data Size**: The character data buffer is 30KB, accommodating full inventory, bank, skills, and quest state.

7. **Guild Persistence**: Guild operations are immediately sent to the Log Server rather than batched, ensuring consistency across multiple game servers.
