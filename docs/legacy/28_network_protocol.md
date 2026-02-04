# 28. Network Protocol System

**Status:** Complete
**Complexity:** High (~2,000+ lines)
**Primary Files:** `XSocket.cpp/h`, `Msg.cpp/h`, `NetMessages.h`, message handling in `Game.cpp`

---

## Table of Contents

1. [Overview](#overview)
2. [Architecture](#architecture)
3. [XSocket Class](#xsocket-class)
4. [Message Queue System](#message-queue-system)
5. [Packet Framing](#packet-framing)
6. [XOR Encryption](#xor-encryption)
7. [Message Routing](#message-routing)
8. [Motion Protocol](#motion-protocol)
9. [Common Action Protocol](#common-action-protocol)
10. [Notification Protocol](#notification-protocol)
11. [Server Communication](#server-communication)
12. [Constants Reference](#constants-reference)
13. [Modernization Notes](#modernization-notes)

---

## Overview

The legacy Helbreath server uses a custom binary protocol over TCP sockets. The network layer consists of:

- **XSocket**: Winsock wrapper providing async I/O with message framing
- **CMsg**: Message queue entry for deferred processing
- **Message Queue**: 100,000-entry circular buffer for processing messages on main thread
- **Message Handlers**: Switch-based dispatch to handler functions in `CGame`

### Key Characteristics

| Aspect | Value |
|--------|-------|
| Transport | TCP (Winsock2) |
| Byte Order | Little-endian |
| String Encoding | Fixed-length, null-padded |
| Encryption | Per-message XOR with random key |
| Max Message Size | 30,000 bytes (`DEF_MSGBUFFERSIZE`) |
| Max Clients | 2,000 (`DEF_MAXCLIENTS`) |
| Message Queue | 100,000 entries (`DEF_MSGQUENESIZE`) |

---

## Architecture

```
┌─────────────────────────────────────────────────────────────────┐
│                        Game Server                               │
├─────────────────────────────────────────────────────────────────┤
│  ┌─────────┐    ┌─────────┐    ┌─────────┐    ┌─────────┐      │
│  │ Client  │    │ Client  │    │  Log    │    │  Sub    │      │
│  │ Socket  │    │ Socket  │    │ Server  │    │  Log    │      │
│  │  [0]    │    │  [N]    │    │ Socket  │    │ Sockets │      │
│  └────┬────┘    └────┬────┘    └────┬────┘    └────┬────┘      │
│       │              │              │              │            │
│       └──────────────┴──────────────┴──────────────┘            │
│                              │                                   │
│                    ┌─────────▼─────────┐                        │
│                    │   Message Queue    │                        │
│                    │  (100,000 slots)   │                        │
│                    │   Circular Buffer  │                        │
│                    └─────────┬─────────┘                        │
│                              │                                   │
│                    ┌─────────▼─────────┐                        │
│                    │  Message Router   │                        │
│                    │  (ProcessMsgs)    │                        │
│                    └─────────┬─────────┘                        │
│                              │                                   │
│       ┌──────────────────────┼──────────────────────┐           │
│       ▼                      ▼                      ▼           │
│  ┌─────────┐          ┌─────────┐          ┌─────────┐         │
│  │ Motion  │          │ Common  │          │  Chat   │         │
│  │ Handler │          │ Handler │          │ Handler │         │
│  └─────────┘          └─────────┘          └─────────┘         │
└─────────────────────────────────────────────────────────────────┘
```

### Message Sources

Messages are tagged with their source for routing:

| Constant | Value | Description |
|----------|-------|-------------|
| `DEF_MSGFROM_CLIENT` | 1 | Game client connection |
| `DEF_MSGFROM_LOGSERVER` | 2 | Log/database server |
| `DEF_MSGFROM_GATESERVER` | 3 | Gate/lobby server |
| `DEF_MSGFROM_BOT` | 4 | Admin tools (deprecated) |

---

## XSocket Class

`XSocket` is a Winsock2 wrapper providing:

- Async/non-blocking I/O via `WSAAsyncSelect`
- Message framing with header + body
- XOR encryption/decryption
- Unsent data queue for handling WOULDBLOCK

### Class Definition

```cpp
class XSocket
{
public:
    XSocket(HWND hWnd, int iBlockLimit);
    ~XSocket();

    // Initialization
    BOOL bInitBufferSize(DWORD dwBufferSize);

    // Connection
    BOOL bListen(char* pAddr, int iPort, unsigned int uiMsg);
    BOOL bAccept(XSocket* pXSock, unsigned int uiMsg);
    BOOL bConnect(char* pAddr, int iPort, unsigned int uiMsg);

    // I/O
    int iSendMsg(char* cData, DWORD dwSize, char cKey = NULL);
    char* pGetRcvDataPointer(DWORD* pMsgSize, char* pKey = NULL);
    int iOnSocketEvent(WPARAM wParam, LPARAM lParam);

    // Utility
    SOCKET iGetSocket();
    int iGetPeerAddress(char* pAddrString);

    int m_WSAErr;           // Last WSA error code
    BOOL m_bIsAvailable;    // Connection established

private:
    char m_cType;           // DEF_XSOCK_LISTENSOCK, NORMALSOCK, SHUTDOWNEDSOCK
    char* m_pRcvBuffer;     // Receive buffer
    char* m_pSndBuffer;     // Send buffer
    DWORD m_dwBufferSize;   // Buffer size

    SOCKET m_Sock;
    char m_cStatus;         // DEF_XSOCKSTATUS_READINGHEADER or READINGBODY
    DWORD m_dwReadSize;     // Bytes remaining to read
    DWORD m_dwTotalReadSize;// Bytes read so far

    // Unsent data queue (circular buffer)
    char* m_pUnsentDataList[DEF_XSOCKBLOCKLIMIT];
    int m_iUnsentDataSize[DEF_XSOCKBLOCKLIMIT];
    short m_sHead, m_sTail;

    int m_iBlockLimit;      // Max queued unsent messages
};
```

### Socket Types

| Constant | Value | Description |
|----------|-------|-------------|
| `DEF_XSOCK_LISTENSOCK` | 1 | Listening socket for accepts |
| `DEF_XSOCK_NORMALSOCK` | 2 | Connected client/server socket |
| `DEF_XSOCK_SHUTDOWNEDSOCK` | 3 | Closed/shutdown socket |

### Socket Events

| Constant | Value | Description |
|----------|-------|-------------|
| `DEF_XSOCKEVENT_CONNECTIONESTABLISH` | -122 | Connection successful |
| `DEF_XSOCKEVENT_RETRYINGCONNECTION` | -123 | Reconnection attempt |
| `DEF_XSOCKEVENT_ONREAD` | -124 | Partial read in progress |
| `DEF_XSOCKEVENT_READCOMPLETE` | -125 | Full message received |
| `DEF_XSOCKEVENT_SOCKETCLOSED` | -127 | Connection closed |
| `DEF_XSOCKEVENT_BLOCK` | -128 | WOULDBLOCK condition |
| `DEF_XSOCKEVENT_SOCKETERROR` | -129 | Socket error occurred |
| `DEF_XSOCKEVENT_CRITICALERROR` | -130 | Fatal error |
| `DEF_XSOCKEVENT_MSGSIZETOOLARGE` | -132 | Message exceeds buffer |
| `DEF_XSOCKEVENT_QUENEFULL` | -134 | Unsent queue full |

### Block Limits

Different socket types have different queue limits for unsent data:

| Constant | Value | Purpose |
|----------|-------|---------|
| `DEF_CLIENTSOCKETBLOCKLIMIT` | 15 | Client sockets (smaller - slow clients get disconnected) |
| `DEF_SERVERSOCKETBLOCKLIMIT` | 300 | Server-to-server sockets (larger - more reliable) |

---

## Message Queue System

The message queue decouples socket I/O from game logic processing.

### CMsg Class

```cpp
class CMsg
{
public:
    CMsg();
    ~CMsg();

    BOOL bPut(char cFrom, char* pData, DWORD dwSize, int iIndex, char cKey);
    void Get(char* pFrom, char* pData, DWORD* pSize, int* pIndex, char* pKey);

    char m_cFrom;      // Message source (DEF_MSGFROM_*)
    char* m_pData;     // Message payload (heap allocated)
    DWORD m_dwSize;    // Payload size
    int m_iIndex;      // Client/socket index
    char m_cKey;       // XOR encryption key
};
```

### Queue Operations

```cpp
// In CGame class
class CMsg* m_pMsgQuene[DEF_MSGQUENESIZE];  // 100,000 slots
int m_iQueneHead;  // Read position
int m_iQueneTail;  // Write position

// Add message to queue
BOOL bPutMsgQuene(char cFrom, char* pData, DWORD dwMsgSize, int iIndex, char cKey)
{
    if (m_pMsgQuene[m_iQueneTail] != NULL) return FALSE;  // Queue full

    m_pMsgQuene[m_iQueneTail] = new CMsg;
    m_pMsgQuene[m_iQueneTail]->bPut(cFrom, pData, dwMsgSize, iIndex, cKey);

    m_iQueneTail++;
    if (m_iQueneTail >= DEF_MSGQUENESIZE) m_iQueneTail = 0;
    return TRUE;
}

// Remove message from queue
BOOL bGetMsgQuene(char* pFrom, char* pData, DWORD* pMsgSize, int* pIndex, char* pKey)
{
    if (m_pMsgQuene[m_iQueneHead] == NULL) return FALSE;  // Queue empty

    m_pMsgQuene[m_iQueneHead]->Get(pFrom, pData, pMsgSize, pIndex, pKey);
    delete m_pMsgQuene[m_iQueneHead];
    m_pMsgQuene[m_iQueneHead] = NULL;

    m_iQueneHead++;
    if (m_iQueneHead >= DEF_MSGQUENESIZE) m_iQueneHead = 0;
    return TRUE;
}
```

---

## Packet Framing

All messages use a 3-byte header followed by the payload.

### Wire Format

```
┌─────────────────────────────────────────────────────────────┐
│  Byte 0   │  Bytes 1-2  │  Bytes 3+                         │
├───────────┼─────────────┼───────────────────────────────────┤
│  XOR Key  │  Total Size │  Encrypted Payload                │
│  (1 byte) │  (2 bytes)  │  (Size - 3 bytes)                 │
└───────────┴─────────────┴───────────────────────────────────┘
```

| Field | Size | Description |
|-------|------|-------------|
| Key | 1 byte | XOR encryption key (0 = no encryption) |
| Size | 2 bytes | Total message size including header (little-endian) |
| Payload | Variable | Message data (encrypted if key != 0) |

### Read State Machine

```cpp
// Reading states
#define DEF_XSOCKSTATUS_READINGHEADER  11
#define DEF_XSOCKSTATUS_READINGBODY    12

int XSocket::_iOnRead()
{
    if (m_cStatus == DEF_XSOCKSTATUS_READINGHEADER) {
        // Read 3-byte header
        recv(m_Sock, m_pRcvBuffer + m_dwTotalReadSize, m_dwReadSize, 0);

        if (header_complete) {
            // Parse size from header
            WORD* wp = (WORD*)(m_pRcvBuffer + 1);
            m_dwReadSize = (*wp) - 3;  // Body size
            m_cStatus = DEF_XSOCKSTATUS_READINGBODY;
        }
    }
    else if (m_cStatus == DEF_XSOCKSTATUS_READINGBODY) {
        // Read payload
        recv(m_Sock, m_pRcvBuffer + m_dwTotalReadSize, m_dwReadSize, 0);

        if (body_complete) {
            m_cStatus = DEF_XSOCKSTATUS_READINGHEADER;
            m_dwReadSize = 3;
            return DEF_XSOCKEVENT_READCOMPLETE;
        }
    }
}
```

---

## XOR Encryption

Messages are encrypted with a per-message XOR key. The key is randomly generated for each message.

### Encryption Algorithm

```cpp
// Encrypt payload before sending
if (cKey != NULL) {
    for (int i = 0; i < dwSize; i++) {
        m_pSndBuffer[3+i] += (i ^ cKey);
        m_pSndBuffer[3+i]  = m_pSndBuffer[3+i] ^ (cKey ^ (dwSize - i));
    }
}

// Decrypt payload after receiving
if (cKey != NULL) {
    for (int i = 0; i < dwSize; i++) {
        m_pRcvBuffer[3+i]  = m_pRcvBuffer[3+i] ^ (cKey ^ (dwSize - i));
        m_pRcvBuffer[3+i] -= (i ^ cKey);
    }
}
```

### Security Notes

- Key is transmitted with each message (byte 0 of header)
- Provides obfuscation, not cryptographic security
- Key=0 means no encryption (used for some internal messages)
- Modern implementation should use TLS instead

---

## Message Routing

Messages are dispatched based on source and message ID.

### Payload Structure

All messages start with a common header after the framing:

```
┌─────────────────────────────────────────────────────────────┐
│  Bytes 0-3   │  Bytes 4-5  │  Bytes 6+                      │
├──────────────┼─────────────┼────────────────────────────────┤
│  Message ID  │  Msg Type   │  Message-specific payload      │
│  (DWORD)     │  (WORD)     │                                │
└──────────────┴─────────────┴────────────────────────────────┘
```

### Index Constants

```cpp
#define DEF_INDEX4_MSGID    0   // Offset for 4-byte message ID
#define DEF_INDEX2_MSGTYPE  4   // Offset for 2-byte message type
```

### Client Message Dispatch

```cpp
void CGame::ProcessClientMessage(int iClientH, char* pData, DWORD dwMsgSize)
{
    DWORD* dwpMsgID = (DWORD*)(pData + DEF_INDEX4_MSGID);

    switch (*dwpMsgID) {
    case MSGID_REQUEST_INITPLAYER:
        RequestInitPlayerHandler(iClientH, pData, cKey);
        break;

    case MSGID_REQUEST_INITDATA:
        RequestInitDataHandler(iClientH, pData, cKey);
        break;

    case MSGID_COMMAND_COMMON:
        ClientCommonHandler(iClientH, pData);
        break;

    case MSGID_COMMAND_MOTION:
        ClientMotionHandler(iClientH, pData);
        break;

    case MSGID_COMMAND_CHECKCONNECTION:
        CheckConnectionHandler(iClientH, pData);
        break;

    case MSGID_COMMAND_CHATMSG:
        ChatMsgHandler(iClientH, pData, dwMsgSize);
        break;

    // ... more handlers

    default:
        // Unknown message - disconnect client
        DeleteClient(iClientH, TRUE, TRUE);
        break;
    }
}
```

---

## Motion Protocol

Motion messages handle all movement and combat actions.

### Message ID

```cpp
#define MSGID_COMMAND_MOTION   0x0FA314D5  // Client -> Server
#define MSGID_RESPONSE_MOTION  0x0FA314D6  // Server -> Client
#define MSGID_EVENT_MOTION     0x0FA314D7  // Server -> Nearby Clients (broadcast)
```

### Motion Types

| Constant | Value | Description |
|----------|-------|-------------|
| `DEF_OBJECTSTOP` | 0 | Stop movement |
| `DEF_OBJECTMOVE` | 1 | Walk one tile |
| `DEF_OBJECTRUN` | 2 | Run (faster) |
| `DEF_OBJECTATTACK` | 3 | Melee attack |
| `DEF_OBJECTMAGIC` | 4 | Cast spell |
| `DEF_OBJECTGETITEM` | 5 | Pick up item |
| `DEF_OBJECTDAMAGE` | 6 | Receiving damage |
| `DEF_OBJECTDAMAGEMOVE` | 7 | Knockback movement |
| `DEF_OBJECTATTACKMOVE` | 8 | Attack while moving |
| `DEF_OBJECTDYING` | 10 | Death animation |
| `DEF_OBJECTNULLACTION` | 100 | No action |

### Motion Command Payload

```
Offset  Size   Type    Field              Description
------  -----  ------  -----------------  ---------------------------------
0       4      DWORD   msg_id             MSGID_COMMAND_MOTION
4       2      WORD    motion_type        DEF_OBJECT* motion type
6       2      short   src_x              Source X position
8       2      short   src_y              Source Y position
10      1      char    direction          Direction (1-8)
11      2      short   dest_x             Destination/target X
13      2      short   dest_y             Destination/target Y
15      2      short   attack_type        Attack type (for attacks)
17      2      WORD    target_id          Target object ID (attacks only)
19      4      DWORD   client_time        Client timestamp (anti-cheat)
```

### Direction Values

```
    NW(8)  N(1)  NE(2)
       \   |   /
    W(7)--[P]--E(3)
       /   |   \
    SW(6)  S(5)  SE(4)
```

Note: Directions are 1-8 (not 0-7) in the motion protocol.

### Motion Response Codes

| Constant | Value | Description |
|----------|-------|-------------|
| `DEF_OBJECTMOVE_CONFIRM` | 1001 | Movement approved |
| `DEF_OBJECTMOVE_REJECT` | 1010 | Movement rejected |
| `DEF_OBJECTMOTION_CONFIRM` | 1020 | General motion approved |
| `DEF_OBJECTMOTION_ATTACK_CONFIRM` | 1030 | Attack motion approved |
| `DEF_OBJECTMOTION_REJECT` | 1040 | Motion rejected |

---

## Common Action Protocol

Common actions cover most gameplay interactions.

### Message ID

```cpp
#define MSGID_COMMAND_COMMON  0x0FA314DC  // Client -> Server
#define MSGID_EVENT_COMMON    0x0FA314DB  // Server -> Nearby Clients
```

### Common Command Payload

```
Offset  Size   Type    Field              Description
------  -----  ------  -----------------  ---------------------------------
0       4      DWORD   msg_id             MSGID_COMMAND_COMMON
4       2      WORD    action_type        DEF_COMMONTYPE_*
6       2      short   pos_x              Player X position
8       2      short   pos_y              Player Y position
10      1      char    direction          Direction facing
11      4      int     value1             Action-specific parameter 1
15      4      int     value2             Action-specific parameter 2
19      4      int     value3             Action-specific parameter 3
23      30     char[]  string_param       Action-specific string
53      4      int     value4             Action-specific parameter 4
```

### Common Action Types

#### Item Actions

| Constant | Value | Description |
|----------|-------|-------------|
| `DEF_COMMONTYPE_ITEMDROP` | 0x0A01 | Drop item |
| `DEF_COMMONTYPE_EQUIPITEM` | 0x0A02 | Equip item |
| `DEF_COMMONTYPE_RELEASEITEM` | 0x0A0A | Unequip item |
| `DEF_COMMONTYPE_SETITEM` | 0x0A0C | Set item in slot |
| `DEF_COMMONTYPE_REQ_USEITEM` | 0x0A11 | Use item |

#### Shop/Trading Actions

| Constant | Value | Description |
|----------|-------|-------------|
| `DEF_COMMONTYPE_REQ_LISTCONTENTS` | 0x0A03 | Request shop list |
| `DEF_COMMONTYPE_REQ_PURCHASEITEM` | 0x0A04 | Buy item |
| `DEF_COMMONTYPE_REQ_SELLITEM` | 0x0A13 | Sell item |
| `DEF_COMMONTYPE_REQ_SELLITEMCONFIRM` | 0x0A15 | Confirm sell |
| `DEF_COMMONTYPE_REQ_REPAIRITEM` | 0x0A14 | Repair item |
| `DEF_COMMONTYPE_REQ_REPAIRITEMCONFIRM` | 0x0A16 | Confirm repair |

#### Player-to-Player Trading

| Constant | Value | Description |
|----------|-------|-------------|
| `DEF_COMMONTYPE_GIVEITEMTOCHAR` | 0x0A05 | Give item to player |
| `DEF_COMMONTYPE_EXCHANGEITEMTOCHAR` | 0x0A1E | Start trade |
| `DEF_COMMONTYPE_SETEXCHANGEITEM` | 0x0A1F | Set trade item |
| `DEF_COMMONTYPE_CONFIRMEXCHANGEITEM` | 0x0A20 | Confirm trade |
| `DEF_COMMONTYPE_CANCELEXCHANGEITEM` | 0x0A21 | Cancel trade |

#### Combat/Magic Actions

| Constant | Value | Description |
|----------|-------|-------------|
| `DEF_COMMONTYPE_TOGGLECOMBATMODE` | 0x0A0B | Toggle PvP mode |
| `DEF_COMMONTYPE_MAGIC` | 0x0A0D | Cast spell |
| `DEF_COMMONTYPE_REQ_STUDYMAGIC` | 0x0A0E | Learn spell |
| `DEF_COMMONTYPE_REQ_USESKILL` | 0x0A12 | Use skill |
| `DEF_COMMONTYPE_REQ_TRAINSKILL` | 0x0A0F | Train skill |
| `DEF_COMMONTYPE_TOGGLESAFEATTACKMODE` | 0x0A18 | Toggle safe attack |
| `DEF_COMMONTYPE_REQ_SETDOWNSKILLINDEX` | 0x0A1B | Set skill hotkey |

#### Guild Actions

| Constant | Value | Description |
|----------|-------|-------------|
| `DEF_COMMONTYPE_JOINGUILDAPPROVE` | 0x0A06 | Accept guild invite |
| `DEF_COMMONTYPE_JOINGUILDREJECT` | 0x0A07 | Reject guild invite |
| `DEF_COMMONTYPE_DISMISSGUILDAPPROVE` | 0x0A08 | Accept guild dismissal |
| `DEF_COMMONTYPE_DISMISSGUILDREJECT` | 0x0A09 | Reject guild dismissal |
| `DEF_COMMONTYPE_BANGUILD` | 0x0A26 | Ban guild member |
| `DEF_COMMONTYPE_SETGUILDTELEPORTLOC` | 0x0A54 | Set guild teleport |
| `DEF_COMMONTYPE_GUILDTELEPORT` | 0x0A55 | Guild teleport |
| `DEF_COMMONTYPE_SETGUILDCONSTRUCTLOC` | 0x0A57 | Set guild construct |
| `DEF_COMMONTYPE_REQGUILDNAME` | 0x0A59 | Request guild name |

#### Party Actions

| Constant | Value | Description |
|----------|-------|-------------|
| `DEF_COMMONTYPE_REQUEST_ACCEPTJOINPARTY` | 0x0A30 | Accept party invite |
| `DEF_COMMONTYPE_REQUEST_JOINPARTY` | 0x0A31 | Request party join |
| `DEF_COMMONTYPE_RESPONSE_JOINPARTY` | 0x0A32 | Party join response |

#### Quest Actions

| Constant | Value | Description |
|----------|-------|-------------|
| `DEF_COMMONTYPE_QUESTACCEPTED` | 0x0A22 | Accept quest |
| `DEF_COMMONTYPE_REQUEST_CANCELQUEST` | 0x0A50 | Cancel quest |

#### Crafting/Gathering Actions

| Constant | Value | Description |
|----------|-------|-------------|
| `DEF_COMMONTYPE_REQ_CREATEPORTION` | 0x0A19 | Create potion |
| `DEF_COMMONTYPE_BUILDITEM` | 0x0A23 | Craft item |
| `DEF_COMMONTYPE_REQ_GETFISHTHISTIME` | 0x0A17 | Fishing reward |
| `DEF_COMMONTYPE_REQ_CREATESLATE` | 0x0A61 | Create slate |

#### NPC Interaction

| Constant | Value | Description |
|----------|-------|-------------|
| `DEF_COMMONTYPE_TALKTONPC` | 0x0A1A | Talk to NPC |
| `DEF_COMMONTYPE_REQ_GETREWARDMONEY` | 0x0A10 | Claim gold reward |

#### War/Crusade Actions

| Constant | Value | Description |
|----------|-------|-------------|
| `DEF_COMMONTYPE_REQ_GETOCCUPYFLAG` | 0x0A1C | Get territory flag |
| `DEF_COMMONTYPE_REQ_GETHEROMANTLE` | 0x0A1D | Get hero cape |
| `DEF_COMMONTYPE_REQ_GETOCCUPYFIGHTZONETICKET` | 0x0A25 | Get fight zone ticket |
| `DEF_COMMONTYPE_REQUEST_SELECTCRUSADEDUTY` | 0x0A51 | Select crusade role |
| `DEF_COMMONTYPE_REQUEST_MAPSTATUS` | 0x0A52 | Get map status |
| `DEF_COMMONTYPE_SUMMONWARUNIT` | 0x0A56 | Summon war unit |

#### Misc Actions

| Constant | Value | Description |
|----------|-------|-------------|
| `DEF_COMMONTYPE_REQUEST_HELP` | 0x0A53 | Get help |
| `DEF_COMMONTYPE_GETMAGICABILITY` | 0x0A24 | Get magic boost |
| `DEF_COMMONTYPE_REQUEST_ACTIVATESPECABLTY` | 0x0A40 | Activate special ability |
| `DEF_COMMONTYPE_UPGRADEITEM` | 0x0A58 | Upgrade item |
| `DEF_COMMONTYPE_REQ_CHANGEPLAYMODE` | 0x0A60 | Change play mode |

---

## Notification Protocol

Server-to-client notifications for state changes and events.

### Message ID

```cpp
#define MSGID_NOTIFY  0x0FA314D0
```

### Notification Payload

```
Offset  Size   Type    Field              Description
------  -----  ------  -----------------  ---------------------------------
0       4      DWORD   msg_id             MSGID_NOTIFY
4       2      WORD    notify_type        DEF_NOTIFY_*
6       ...    varies  payload            Notification-specific data
```

### Key Notification Types

See `NetMessages.h` for complete list. Major categories:

| Category | Example Constants |
|----------|-------------------|
| Stats | `DEF_NOTIFY_HP`, `DEF_NOTIFY_MP`, `DEF_NOTIFY_SP`, `DEF_NOTIFY_EXP` |
| Items | `DEF_NOTIFY_ITEMOBTAINED`, `DEF_NOTIFY_ITEMRELEASED`, `DEF_NOTIFY_SETITEMCOUNT` |
| Magic | `DEF_NOTIFY_MAGICEFFECTON`, `DEF_NOTIFY_MAGICEFFECTOFF` |
| Skills | `DEF_NOTIFY_SKILL`, `DEF_NOTIFY_SKILLTRAINSUCCESS` |
| Guild | `DEF_NOTIFY_NEWGUILDSMAN`, `DEF_NOTIFY_GUILDDISBANDED` |
| Party | `DEF_NOTIFY_PARTY`, `DEF_NOTIFY_QUERY_JOINPARTY` |
| Trading | `DEF_NOTIFY_OPENEXCHANGEWINDOW`, `DEF_NOTIFY_EXCHANGEITEMCOMPLETE` |
| Quest | `DEF_NOTIFY_QUESTCONTENTS`, `DEF_NOTIFY_QUESTCOMPLETED` |
| Combat | `DEF_NOTIFY_KILLED`, `DEF_NOTIFY_PKPENALTY` |
| World | `DEF_NOTIFY_NEWDYNAMICOBJECT`, `DEF_NOTIFY_TIMECHANGE` |
| System | `DEF_NOTIFY_SERVERSHUTDOWN`, `DEF_NOTIFY_FORCEDISCONN` |

---

## Server Communication

### Log Server Communication

The game server communicates with a central log/database server for:

- Player data persistence
- Account management
- Guild operations
- Logging/auditing

Multiple sub-log sockets provide load balancing for high-traffic operations.

```cpp
class XSocket* m_pMainLogSock;                      // Primary log server connection
class XSocket* m_pSubLogSock[DEF_MAXSUBLOGSOCK];   // Sub-log sockets for load balancing
int m_iCurSubLogSockIndex;                          // Current sub-log socket index
```

### Key Log Server Messages

| Constant | Description |
|----------|-------------|
| `MSGID_REQUEST_REGISTERGAMESERVER` | Register game server |
| `MSGID_REQUEST_PLAYERDATA` | Request player data |
| `MSGID_REQUEST_SAVEPLAYERDATA` | Save player data |
| `MSGID_REQUEST_CREATENEWGUILD` | Create guild |
| `MSGID_REQUEST_DISBANDGUILD` | Disband guild |
| `MSGID_GAMEITEMLOG` | Log item transaction |
| `MSGID_GAMEMASTERLOG` | Log GM action |

---

## Constants Reference

### Buffer Sizes

| Constant | Value | Description |
|----------|-------|-------------|
| `DEF_MSGBUFFERSIZE` | 30,000 | Max message payload |
| `DEF_MSGQUENESIZE` | 100,000 | Message queue slots |

### Capacity Limits

| Constant | Value | Description |
|----------|-------|-------------|
| `DEF_MAXCLIENTS` | 2,000 | Max client connections |
| `DEF_MAXNPCS` | 5,000 | Max NPCs |

### Message Type Constants

| Constant | Value | Description |
|----------|-------|-------------|
| `DEF_MSGTYPE_CONFIRM` | 0x0F14 | Success response |
| `DEF_MSGTYPE_REJECT` | 0x0F15 | Failure response |

### Login Response Codes

| Constant | Value | Description |
|----------|-------|-------------|
| `DEF_LOGRESMSGTYPE_CONFIRM` | 0x0F14 | Login successful |
| `DEF_LOGRESMSGTYPE_REJECT` | 0x0F15 | Login rejected |
| `DEF_LOGRESMSGTYPE_PASSWORDMISMATCH` | 0x0F16 | Wrong password |
| `DEF_LOGRESMSGTYPE_NOTEXISTINGACCOUNT` | 0x0F17 | Account doesn't exist |
| `DEF_LOGRESMSGTYPE_NEWACCOUNTCREATED` | 0x0F18 | Account created |
| `DEF_LOGRESMSGTYPE_NEWACCOUNTFAILED` | 0x0F19 | Account creation failed |
| `DEF_LOGRESMSGTYPE_ALREADYEXISTINGACCOUNT` | 0x0F1A | Account already exists |
| `DEF_LOGRESMSGTYPE_NOTEXISTINGCHARACTER` | 0x0F1B | Character doesn't exist |
| `DEF_LOGRESMSGTYPE_NEWCHARACTERCREATED` | 0x0F1C | Character created |
| `DEF_LOGRESMSGTYPE_NEWCHARACTERFAILED` | 0x0F1D | Character creation failed |
| `DEF_LOGRESMSGTYPE_ALREADYEXISTINGCHARACTER` | 0x0F1E | Character already exists |
| `DEF_LOGRESMSGTYPE_CHARACTERDELETED` | 0x0F1F | Character deleted |

### Enter Game Codes

| Constant | Value | Description |
|----------|-------|-------------|
| `DEF_ENTERGAMEMSGTYPE_NEW` | 0x0F1C | Normal game entry |
| `DEF_ENTERGAMEMSGTYPE_NOENTER_FORCEDISCONN` | 0x0F1D | Force disconnect |
| `DEF_ENTERGAMEMSGTYPE_CHANGINGSERVER` | 0x0F1E | Server transfer |
| `DEF_ENTERGAMERESTYPE_PLAYING` | 0x0F20 | Now playing |
| `DEF_ENTERGAMERESTYPE_REJECT` | 0x0F21 | Entry rejected |
| `DEF_ENTERGAMERESTYPE_CONFIRM` | 0x0F22 | Entry confirmed |
| `DEF_ENTERGAMERESTYPE_FORCEDISCONN` | 0x0F23 | Force disconnect |

---

## Modernization Notes

### Current Implementation

The modernized server uses WebSocket with JSON for the authentication/character flow, while preserving binary protocol compatibility for game data:

- **WebSocket Server**: `src/network/websocket_server.*` (Boost.Beast)
- **JSON Protocol**: `src/network/json_protocol.*`
- **Handlers**: `src/bridge/handlers/`

### Key Differences

| Aspect | Legacy | Modern |
|--------|--------|--------|
| Transport | Raw TCP | WebSocket |
| Auth Format | Binary | JSON |
| Game Format | Binary | Binary (compatible) |
| Encryption | XOR | TLS |
| Threading | Single + async | Thread-safe queues |

### Migration Strategy

1. Auth flow fully migrated to WebSocket/JSON
2. Game protocol handlers being ported
3. Binary format preserved for game data (client compatibility)
4. Gate server being removed (single-server deployment)
5. Log server replaced by PostgreSQL direct access

### References

- See `docs/PACKET_PROTOCOL.md` for detailed packet formats
- See `docs/JSON_PROTOCOL.md` for modern JSON protocol
- See `src/network/json_protocol.h` for message type definitions
