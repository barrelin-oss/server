# Helbreath Packet Protocol Documentation

This document provides a complete reference for all packets in the Helbreath server protocol, including both the legacy binary format and the modern JSON format.

---

## Table of Contents

1. [Implementation Status](#implementation-status)
2. [Protocol Overview](#protocol-overview)
3. [Legacy Binary Packet Structure](#legacy-binary-packet-structure)
4. [Modern JSON Protocol](#modern-json-protocol)
5. [Packet Categories](#packet-categories)
   - [Authentication Packets](#authentication-packets)
   - [Character Management Packets](#character-management-packets)
   - [Game Entry Packets](#game-entry-packets)
   - [Movement Packets](#movement-packets)
   - [Combat Packets](#combat-packets)
   - [Magic Packets](#magic-packets)
   - [Common Action Packets](#common-action-packets)
   - [Notification Packets](#notification-packets)
   - [Server Communication Packets](#server-communication-packets)
6. [Data Types Reference](#data-types-reference)
7. [Constants Reference](#constants-reference)

---

## Implementation Status

### Status Legend

| Symbol | Meaning |
|--------|---------|
| ✅ | Fully implemented in JSON protocol |
| 🔄 | Handler exists, logic TODO |
| 📋 | JSON type defined, handler TODO |
| ❌ | Not implemented |

### Summary by Category

| Category | Total | ✅ Done | 🔄 Partial | ❌ TODO |
|----------|-------|---------|------------|---------|
| Authentication | 6 | 6 | 0 | 0 |
| Character Management | 6 | 6 | 0 | 0 |
| Game Entry | 4 | 4 | 0 | 0 |
| Movement | 7 | 7 | 0 | 0 |
| Combat | 2 | 0 | 2 | 0 |
| Magic | 2 | 0 | 2 | 0 |
| Skills | 2 | 0 | 2 | 0 |
| Items/Pickup | 2 | 0 | 2 | 0 |
| Interaction | 2 | 0 | 2 | 0 |
| Chat | 1 | 0 | 0 | 1 |
| Common Actions | 50+ | 0 | 0 | 50+ |
| Notifications | 100+ | 0 | 0 | 100+ |

### JSON Message Types Implementation

| JSON Type | Status | Handler Location |
|-----------|--------|------------------|
| `ping` | ✅ | `application.cpp` |
| `pong` | ✅ | `application.cpp` |
| `error` | ✅ | `json_protocol.cpp` |
| `login_request` | ✅ | `auth_handlers.cpp` |
| `login_response` | ✅ | `auth_handlers.cpp` |
| `logout_request` | ✅ | `auth_handlers.cpp` |
| `logout_response` | ✅ | `auth_handlers.cpp` |
| `create_account_request` | ✅ | `auth_handlers.cpp` |
| `create_account_response` | ✅ | `auth_handlers.cpp` |
| `get_characters_request` | ✅ | `auth_handlers.cpp` |
| `get_characters_response` | ✅ | `auth_handlers.cpp` |
| `create_character_request` | ✅ | `auth_handlers.cpp` |
| `create_character_response` | ✅ | `auth_handlers.cpp` |
| `delete_character_request` | ✅ | `auth_handlers.cpp` |
| `delete_character_response` | ✅ | `auth_handlers.cpp` |
| `enter_game_request` | ✅ | `auth_handlers.cpp` |
| `enter_game_response` | ✅ | `auth_handlers.cpp` |
| `character_data` | ✅ | `json_protocol.cpp` |
| `inventory_data` | ✅ | `json_protocol.cpp` |
| `equipment_data` | ✅ | `json_protocol.cpp` |
| `skills_data` | ✅ | `json_protocol.cpp` |
| `world_init` | ✅ | `json_protocol.cpp` |
| `entity_spawn` | ✅ | `json_protocol.cpp` |
| `entity_despawn` | ✅ | `json_protocol.cpp` |
| `player_move_request` | ✅ | `game_handlers.cpp` |
| `player_move_response` | ✅ | `game_handlers.cpp` |
| `player_run_request` | ✅ | `game_handlers.cpp` |
| `player_run_response` | ✅ | `game_handlers.cpp` |
| `player_stop_request` | ✅ | `game_handlers.cpp` |
| `player_stop_response` | ✅ | `game_handlers.cpp` |
| `player_position_update` | ✅ | `game_handlers.cpp` |
| `player_attack_request` | 🔄 | `game_handlers.cpp` - parser done, combat TODO |
| `player_attack_response` | 🔄 | `json_protocol.cpp` - struct done |
| `player_magic_request` | 🔄 | `game_handlers.cpp` - parser done, magic TODO |
| `player_magic_response` | 🔄 | `json_protocol.cpp` - struct done |
| `player_skill_request` | 🔄 | `game_handlers.cpp` - parser done, skill TODO |
| `player_skill_response` | 🔄 | `json_protocol.cpp` - struct done |
| `player_pickup_request` | 🔄 | `game_handlers.cpp` - parser done, item TODO |
| `player_pickup_response` | 🔄 | `json_protocol.cpp` - struct done |
| `player_interact_request` | 🔄 | `game_handlers.cpp` - parser done, NPC TODO |
| `player_interact_response` | 🔄 | `json_protocol.cpp` - struct done |
| `chat_message` | ❌ | Not implemented |

---

## Protocol Overview

### Transport Layers

| Protocol | Transport | Format | Usage |
|----------|-----------|--------|-------|
| Legacy | TCP Socket | Binary (little-endian) | Original client-server |
| Modern | WebSocket | JSON text frames | New web-based clients |

### Byte Order

All binary values in the legacy protocol use **little-endian** byte order.

### Encoding

- Strings are fixed-length, null-padded (not null-terminated)
- Character names: 10 bytes
- Account names: 10 bytes
- Map names: 10 bytes
- Guild names: 20 bytes
- Item names: 20 bytes

---

## Legacy Binary Packet Structure

### Base Packet Header

Every legacy packet begins with a common 6-byte header:

```
Offset  Size  Type    Field        Description
------  ----  ------  -----------  ----------------------------------
0       4     DWORD   msg_id       Message identifier (MSGID_*)
4       2     WORD    msg_type     Sub-command type (DEF_*TYPE_*)
6       ...   varies  payload      Message-specific data
```

### Message ID Constants

The `msg_id` field identifies the packet category:

| Constant | Hex Value | Description | Status |
|----------|-----------|-------------|--------|
| `MSGID_REQUEST_INITPLAYER` | `0x05040205` | Player initialization request | ✅ via enter_game |
| `MSGID_RESPONSE_INITPLAYER` | `0x05040206` | Player init response | ✅ via enter_game |
| `MSGID_REQUEST_INITDATA` | `0x05080404` | Request init data | ✅ via enter_game |
| `MSGID_RESPONSE_INITDATA` | `0x05080405` | Init data response | ✅ via enter_game |
| `MSGID_COMMAND_MOTION` | `0x0FA314D5` | Movement command | ✅ |
| `MSGID_RESPONSE_MOTION` | `0x0FA314D6` | Movement response | ✅ |
| `MSGID_EVENT_MOTION` | `0x0FA314D7` | Movement event (broadcast) | ✅ |
| `MSGID_COMMAND_COMMON` | `0x0FA314DC` | Common actions (attack, magic, item use) | 🔄 |
| `MSGID_EVENT_COMMON` | `0x0FA314DB` | Common event (broadcast) | ❌ |
| `MSGID_NOTIFY` | `0x0FA314D0` | Notification to client | ❌ |
| `MSGID_COMMAND_CHATMSG` | `0x03203204` | Chat message | ❌ |
| `MSGID_COMMAND_CHECKCONNECTION` | `0x03203203` | Keep-alive ping | ✅ via ping/pong |
| `MSGID_REQUEST_LOGIN` | `0x0FC94201` | Login request | ✅ |
| `MSGID_REQUEST_CREATENEWACCOUNT` | `0x0FC94202` | Create account request | ✅ |
| `MSGID_RESPONSE_LOG` | `0x0FC94203` | Login/account response | ✅ |
| `MSGID_REQUEST_CREATENEWCHARACTER` | `0x0FC94204` | Create character request | ✅ |
| `MSGID_REQUEST_ENTERGAME` | `0x0FC94205` | Enter game request | ✅ |
| `MSGID_RESPONSE_ENTERGAME` | `0x0FC94206` | Enter game response | ✅ |
| `MSGID_REQUEST_DELETECHARACTER` | `0x0FC94207` | Delete character request | ✅ |
| `MSGID_REQUEST_CREATENEWGUILD` | `0x0FC94208` | Create guild request | ❌ |
| `MSGID_RESPONSE_CREATENEWGUILD` | `0x0FC94209` | Create guild response | ❌ |
| `MSGID_REQUEST_DISBANDGUILD` | `0x0FC9420A` | Disband guild request | ❌ |
| `MSGID_RESPONSE_DISBANDGUILD` | `0x0FC9420B` | Disband guild response | ❌ |
| `MSGID_REQUEST_TELEPORT` | `0x0EA03201` | Teleport request | ❌ |
| `MSGID_REQUEST_PLAYERDATA` | `0x0C152210` | Request player data | ❌ |
| `MSGID_RESPONSE_PLAYERDATA` | `0x0C152211` | Player data response | ❌ |
| `MSGID_REQUEST_SAVEPLAYERDATA` | `0x0DF3076F` | Save player data | ✅ internal |
| `MSGID_PARTYOPERATION` | `0x3C00123A` | Party management | ❌ |
| `MSGID_ITEMCONFIGURATIONCONTENTS` | `0x0FA314D9` | Item config data | ❌ |
| `MSGID_NPCCONFIGURATIONCONTENTS` | `0x0FA314DA` | NPC config data | ❌ |
| `MSGID_MAGICCONFIGURATIONCONTENTS` | `0x0FA314DB` | Magic config data |
| `MSGID_SKILLCONFIGURATIONCONTENTS` | `0x0FA314DC` | Skill config data |
| `MSGID_PLAYERITEMLISTCONTENTS` | `0x0FA314DD` | Player inventory |
| `MSGID_PLAYERCHARACTERCONTENTS` | `0x0FA40000` | Player character data |
| `MSGID_QUESTCONFIGURATIONCONTENTS` | `0x0FA40001` | Quest config data |
| `MSGID_BUILDITEMCONFIGURATIONCONTENTS` | `0x0FA40002` | Crafting recipes |

---

## Modern JSON Protocol

### Message Envelope

All JSON messages use a common envelope structure:

```json
{
  "type": "message_type",
  "seq": 123,
  "data": { ... }
}
```

| Field | Type | Description |
|-------|------|-------------|
| `type` | string | Message type identifier |
| `seq` | uint32 | Sequence number for request/response matching |
| `data` | object | Message-specific payload |

### Request/Response Pattern

- Client sends requests with a unique `seq` number
- Server responds with the same `seq` to enable matching
- Server-initiated broadcasts use `seq: 0`

---

## Packet Categories

---

## Authentication Packets

### Login Request

**Legacy Binary:**
```
MSGID: MSGID_REQUEST_LOGIN (0x0FC94201)

Offset  Size  Type    Field           Description
------  ----  ------  -------------   ----------------------------------
0       4     DWORD   msg_id          0x0FC94201
4       2     WORD    msg_type        0x0000
6       10    char[]  account_name    Account name (encoded)
16      10    char[]  password        Password (encoded)
26      2     WORD    world_number    Target world server
```

**JSON:**
```json
{
  "type": "login_request",
  "seq": 1,
  "data": {
    "username": "player1",
    "password": "secret123"
  }
}
```

---

### Login Response

**Legacy Binary:**
```
MSGID: MSGID_RESPONSE_LOG (0x0FC94203)

Offset  Size  Type    Field           Description
------  ----  ------  -------------   ----------------------------------
0       4     DWORD   msg_id          0x0FC94203
4       2     WORD    result_code     DEF_LOGRESMSGTYPE_* (see below)
6       1     BYTE    char_count      Number of characters (on success)
7       ...   varies  character_data  Character list (on success)
```

**Result Codes (msg_type):**

| Constant | Value | Description |
|----------|-------|-------------|
| `DEF_LOGRESMSGTYPE_CONFIRM` | `0x0F14` | Login successful |
| `DEF_LOGRESMSGTYPE_REJECT` | `0x0F15` | Login rejected |
| `DEF_LOGRESMSGTYPE_PASSWORDMISMATCH` | `0x0F16` | Wrong password |
| `DEF_LOGRESMSGTYPE_NOTEXISTINGACCOUNT` | `0x0F17` | Account doesn't exist |
| `DEF_LOGRESMSGTYPE_NEWACCOUNTCREATED` | `0x0F18` | Account created |
| `DEF_LOGRESMSGTYPE_NEWACCOUNTFAILED` | `0x0F19` | Account creation failed |
| `DEF_LOGRESMSGTYPE_ALREADYEXISTINGACCOUNT` | `0x0F1A` | Account exists |
| `DEF_LOGRESMSGTYPE_NOTEXISTINGCHARACTER` | `0x0F1B` | Character doesn't exist |
| `DEF_LOGRESMSGTYPE_NEWCHARACTERCREATED` | `0x0F1C` | Character created |
| `DEF_LOGRESMSGTYPE_NEWCHARACTERFAILED` | `0x0F1D` | Character creation failed |
| `DEF_LOGRESMSGTYPE_ALREADYEXISTINGCHARACTER` | `0x0F1E` | Character exists |
| `DEF_LOGRESMSGTYPE_CHARACTERDELETED` | `0x0F1F` | Character deleted |

**JSON:**
```json
{
  "type": "login_response",
  "seq": 1,
  "data": {
    "success": true,
    "session_token": "..."
  }
}
```

---

### Create Account Request

**Legacy Binary:**
```
MSGID: MSGID_REQUEST_CREATENEWACCOUNT (0x0FC94202)

Offset  Size  Type    Field           Description
------  ----  ------  -------------   ----------------------------------
0       4     DWORD   msg_id          0x0FC94202
4       2     WORD    msg_type        0x0000
6       10    char[]  account_name    Desired account name (encoded)
16      10    char[]  password        Password (encoded)
26      50    char[]  email           Email address
```

**JSON:**
```json
{
  "type": "create_account_request",
  "seq": 1,
  "data": {
    "username": "newplayer",
    "password": "securepass123"
  }
}
```

---

## Character Management Packets

### Get Characters Request

**Legacy Binary:**
Part of login response flow - characters included in login confirm.

**JSON:**
```json
{
  "type": "get_characters_request",
  "seq": 2,
  "data": {}
}
```

---

### Get Characters Response

**Legacy Binary:**
Included in `MSGID_RESPONSE_LOG` with `DEF_LOGRESMSGTYPE_CONFIRM`:

```
Character Info Block (repeated for each character):
Offset  Size  Type    Field           Description
------  ----  ------  -------------   ----------------------------------
0       10    char[]  char_name       Character name
10      1     BYTE    gender          0=Male, 1=Female
11      1     BYTE    skin_color      Skin color ID
12      1     BYTE    hair_style      Hair style ID
13      1     BYTE    hair_color      Hair color ID
14      2     WORD    level           Character level
15      2     WORD    exp_high        Experience (high word)
17      2     WORD    exp_low         Experience (low word)
19      2     WORD    str             Strength
21      2     WORD    vit             Vitality
23      2     WORD    dex             Dexterity
25      2     WORD    int             Intelligence
27      2     WORD    mag             Magic
29      2     WORD    chr             Charisma
31      10    char[]  map_name        Current map location
41      2     WORD    nation          Nation (1=Aresden, 2=Elvine)
```

**JSON:**
```json
{
  "type": "get_characters_response",
  "seq": 2,
  "data": {
    "success": true,
    "characters": [
      {
        "id": 1,
        "name": "Warrior1",
        "level": 45,
        "class_type": 1,
        "nation": 1,
        "gender": 0,
        "map_name": "aresden",
        "experience": 125000,
        "hair_style": 2,
        "hair_color": 3,
        "skin_color": 1
      }
    ]
  }
}
```

---

### Create Character Request

**Legacy Binary:**
```
MSGID: MSGID_REQUEST_CREATENEWCHARACTER (0x0FC94204)

Offset  Size  Type    Field           Description
------  ----  ------  -------------   ----------------------------------
0       4     DWORD   msg_id          0x0FC94204
4       2     WORD    msg_type        0x0000
6       10    char[]  char_name       Character name (encoded)
16      10    char[]  account_name    Account name (encoded)
26      10    char[]  password        Password (encoded)
36      1     BYTE    gender          0=Male, 1=Female
37      1     BYTE    skin_color      Skin color ID (0-3)
38      1     BYTE    hair_style      Hair style ID (0-7)
39      1     BYTE    hair_color      Hair color ID (0-15)
40      1     BYTE    underwear_color Underwear color ID
41      2     WORD    str             Starting strength
43      2     WORD    vit             Starting vitality
45      2     WORD    dex             Starting dexterity
47      2     WORD    int             Starting intelligence
49      2     WORD    mag             Starting magic
51      2     WORD    chr             Starting charisma
```

**JSON:**
```json
{
  "type": "create_character_request",
  "seq": 3,
  "data": {
    "name": "NewHero",
    "class_type": 0,
    "nation": 1,
    "gender": 0,
    "hair_style": 2,
    "hair_color": 4,
    "skin_color": 1,
    "underwear_color": 0,
    "strength": 14,
    "dexterity": 10,
    "vitality": 12,
    "intelligence": 10,
    "magic": 10,
    "charisma": 10
  }
}
```

---

### Delete Character Request

**Legacy Binary:**
```
MSGID: MSGID_REQUEST_DELETECHARACTER (0x0FC94207)

Offset  Size  Type    Field           Description
------  ----  ------  -------------   ----------------------------------
0       4     DWORD   msg_id          0x0FC94207
4       2     WORD    msg_type        0x0000
6       10    char[]  char_name       Character name (encoded)
16      10    char[]  account_name    Account name (encoded)
26      10    char[]  password        Password (encoded)
```

**JSON:**
```json
{
  "type": "delete_character_request",
  "seq": 4,
  "data": {
    "character_id": 5
  }
}
```

---

## Game Entry Packets

### Init Player Request

**Legacy Binary:**
```
MSGID: MSGID_REQUEST_INITPLAYER (0x05040205)

Offset  Size  Type    Field           Description
------  ----  ------  -------------   ----------------------------------
0       4     DWORD   msg_id          0x05040205
4       2     WORD    msg_type        0x0000
6       10    char[]  char_name       Character name (XOR encoded)
16      10    char[]  account_name    Account name (XOR encoded)
26      10    char[]  password        Password (XOR encoded)
36      1     BOOL    observer_mode   Observer mode flag
```

**Note:** Fields are XOR-encoded with a session key for obfuscation.

---

### Enter Game Request

**Legacy Binary:**
```
MSGID: MSGID_REQUEST_ENTERGAME (0x0FC94205)

Offset  Size  Type    Field           Description
------  ----  ------  -------------   ----------------------------------
0       4     DWORD   msg_id          0x0FC94205
4       2     WORD    enter_type      DEF_ENTERGAMEMSGTYPE_* (see below)
6       10    char[]  char_name       Character name (encoded)
16      10    char[]  account_name    Account name
26      10    char[]  password        Password
36      10    char[]  map_name        Starting map (for new characters)
46      2     WORD    pos_x           Starting X position
48      2     WORD    pos_y           Starting Y position
```

**Enter Type Values:**

| Constant | Value | Description |
|----------|-------|-------------|
| `DEF_ENTERGAMEMSGTYPE_NEW` | `0x0F1C` | New game entry |
| `DEF_ENTERGAMEMSGTYPE_NOENTER_FORCEDISCONN` | `0x0F1D` | Force disconnect |
| `DEF_ENTERGAMEMSGTYPE_CHANGINGSERVER` | `0x0F1E` | Server transfer |

**JSON:**
```json
{
  "type": "enter_game_request",
  "seq": 5,
  "data": {
    "character_id": 1
  }
}
```

---

### Enter Game Response

**Legacy Binary:**
```
MSGID: MSGID_RESPONSE_ENTERGAME (0x0FC94206)

Offset  Size  Type    Field           Description
------  ----  ------  -------------   ----------------------------------
0       4     DWORD   msg_id          0x0FC94206
4       2     WORD    result          DEF_ENTERGAMERESTYPE_* (see below)
```

On success (`DEF_ENTERGAMERESTYPE_CONFIRM`), followed by character data (see Init Data Response).

**Result Codes:**

| Constant | Value | Description |
|----------|-------|-------------|
| `DEF_ENTERGAMERESTYPE_PLAYING` | `0x0F20` | Now playing |
| `DEF_ENTERGAMERESTYPE_REJECT` | `0x0F21` | Entry rejected |
| `DEF_ENTERGAMERESTYPE_CONFIRM` | `0x0F22` | Entry confirmed |
| `DEF_ENTERGAMERESTYPE_FORCEDISCONN` | `0x0F23` | Force disconnect |

**JSON:**
```json
{
  "type": "enter_game_response",
  "seq": 5,
  "data": {
    "success": true,
    "character": { ... },
    "inventory": { ... },
    "equipment": [ ... ],
    "skills": [ ... ],
    "world": { ... }
  }
}
```

---

### Player Character Contents (Init Data)

Sent after successful game entry, contains complete character state.

**Legacy Binary:**
```
MSGID: MSGID_PLAYERCHARACTERCONTENTS (0x0FA40000)
MSG_TYPE: DEF_MSGTYPE_CONFIRM (0x0F14)

Offset  Size  Type    Field           Description
------  ----  ------  -------------   ----------------------------------
0       4     DWORD   msg_id          0x0FA40000
4       2     WORD    msg_type        0x0F14 (confirm)
6       4     int     hp              Current HP
10      4     int     mp              Current MP
14      4     int     sp              Current SP
18      4     int     defense_ratio   Defense rating
22      4     int     hit_ratio       Hit rating
26      4     int     level           Character level
30      4     int     str             Strength
34      4     int     int             Intelligence
38      4     int     vit             Vitality
42      4     int     dex             Dexterity
46      4     int     mag             Magic
50      4     int     charisma        Charisma
54      2     WORD    lu_pool         Available level-up points
56      1     char    var             Character variant
57      4     char[]  reserved        Reserved (4 bytes)
61      4     int     exp             Experience
65      4     int     enemy_kill_cnt  Enemy kill count
69      4     int     pk_count        PK count
73      4     int     reward_gold     Reward gold amount
77      10    char[]  location        Location string
87      20    char[]  guild_name      Guild name
107     4     int     guild_rank      Guild rank
111     1     char    super_atk_left  Super attack charges
112     4     int     fightzone_num   Fight zone number
```

**JSON:** Included in `enter_game_response` data as `character` object.

---

## Movement Packets

### Motion Command (Move/Attack/Stop)

**Legacy Binary:**
```
MSGID: MSGID_COMMAND_MOTION (0x0FA314D5)

Offset  Size  Type    Field           Description
------  ----  ------  -------------   ----------------------------------
0       4     DWORD   msg_id          0x0FA314D5
4       2     WORD    motion_type     Motion type (see below)
6       2     short   src_x           Source X position
8       2     short   src_y           Source Y position
10      1     char    direction       Direction (0-7)
11      2     short   dest_x          Destination X position
13      2     short   dest_y          Destination Y position
15      2     WORD    target_id       Target object ID (for attacks)
17      4     DWORD   client_time     Client timestamp
```

**Motion Types (msg_type):**

| Value | Name | Description |
|-------|------|-------------|
| 1 | MOVE | Walk one tile |
| 2 | RUN | Run two tiles |
| 3 | STOP | Stop moving |
| 4 | ATTACK | Melee attack |
| 5 | ATTACK_MOVE | Attack while moving |
| 11 | MAGIC | Cast spell |
| 20 | DASH | Dash attack |

**JSON (Walk):**
```json
{
  "type": "player_move_request",
  "seq": 10,
  "data": {
    "x": 150,
    "y": 200,
    "direction": 3,
    "timestamp": 1706620000000
  }
}
```

**JSON (Run):**
```json
{
  "type": "player_run_request",
  "seq": 11,
  "data": {
    "x": 151,
    "y": 200,
    "direction": 3,
    "timestamp": 1706620000100
  }
}
```

---

### Motion Response

**Legacy Binary:**
```
MSGID: MSGID_RESPONSE_MOTION (0x0FA314D6)

Offset  Size  Type    Field           Description
------  ----  ------  -------------   ----------------------------------
0       4     DWORD   msg_id          0x0FA314D6
4       2     WORD    result          Motion result
6       2     short   pos_x           Confirmed X position
8       2     short   pos_y           Confirmed Y position
10      1     char    direction       Confirmed direction
```

**JSON:**
```json
{
  "type": "player_move_response",
  "seq": 10,
  "data": {
    "success": true,
    "x": 151,
    "y": 200,
    "direction": 3
  }
}
```

---

### Motion Event (Broadcast)

**Legacy Binary:**
```
MSGID: MSGID_EVENT_MOTION (0x0FA314D7)

Offset  Size  Type    Field           Description
------  ----  ------  -------------   ----------------------------------
0       4     DWORD   msg_id          0x0FA314D7
4       2     WORD    motion_type     Motion type
6       2     short   pos_x           Position X
8       2     short   pos_y           Position Y
10      2     WORD    object_type     Object type (player/NPC/etc.)
12      1     char    direction       Direction facing
13      10    char[]  name            Entity name
23      2     WORD    appr1           Appearance 1 (equipment)
25      2     WORD    appr2           Appearance 2
27      2     WORD    appr3           Appearance 3
29      2     WORD    appr4           Appearance 4
31      2     WORD    status          Status flags
33      2     WORD    value1          Motion-specific value
35      2     WORD    value2          Motion-specific value
37      2     WORD    value3          Motion-specific value
```

**JSON:**
```json
{
  "type": "player_position_update",
  "seq": 0,
  "data": {
    "entity_id": 1234,
    "x": 155,
    "y": 200,
    "direction": 3,
    "is_running": false
  }
}
```

---

### Direction Values

```
    NW(7)  N(0)  NE(1)
       \   |   /
    W(6)--[P]--E(2)
       /   |   \
    SW(5)  S(4)  SE(3)
```

| Value | Direction | Delta (dx, dy) |
|-------|-----------|----------------|
| 0 | North | (0, -1) |
| 1 | Northeast | (1, -1) |
| 2 | East | (1, 0) |
| 3 | Southeast | (1, 1) |
| 4 | South | (0, 1) |
| 5 | Southwest | (-1, 1) |
| 6 | West | (-1, 0) |
| 7 | Northwest | (-1, -1) |

---

## Combat Packets

### Attack Request

Attacks are sent via `MSGID_COMMAND_MOTION` with motion type 4 (ATTACK) or 20 (DASH).

**Legacy Binary:**
```
MSGID: MSGID_COMMAND_MOTION (0x0FA314D5)
MSG_TYPE: 4 (regular) or 20 (dash)

Offset  Size  Type    Field           Description
------  ----  ------  -------------   ----------------------------------
0       4     DWORD   msg_id          0x0FA314D5
4       2     WORD    attack_type     4=regular, 20=dash
6       2     short   src_x           Attacker X position
8       2     short   src_y           Attacker Y position
10      1     char    direction       Attack direction
11      2     short   target_x        Target X position
13      2     short   target_y        Target Y position
15      2     WORD    target_id       Target entity ID
```

**JSON:**
```json
{
  "type": "player_attack_request",
  "seq": 20,
  "data": {
    "x": 150,
    "y": 200,
    "direction": 3,
    "attack_type": "regular",
    "target_type": "npc",
    "target_id": 5001,
    "timestamp": 1706620001000
  }
}
```

**Attack Types:**

| Value | Name | Requirements |
|-------|------|--------------|
| `regular` (0) | Normal attack | None |
| `dash` (1) | Dash attack | 100% weapon skill, 1 tile gap |
| `super` (2) | Super attack | 100% weapon skill, charges |

---

### Attack Response/Event

Attack results are sent via `MSGID_EVENT_MOTION` with damage information.

**Legacy Binary:**
```
MSGID: MSGID_EVENT_MOTION (0x0FA314D7)
MSG_TYPE: 4 (attack event)

Contains appearance data with attack animation and damage values:
- value1: damage dealt
- value2: damage type flags
- value3: target remaining HP
```

**JSON:**
```json
{
  "type": "player_attack_response",
  "seq": 20,
  "data": {
    "success": true,
    "result": {
      "hit": true,
      "critical": false,
      "damage": 45,
      "target_id": 5001,
      "target_hp": 255,
      "target_hp_max": 300,
      "attacker_x": 150,
      "attacker_y": 200
    }
  }
}
```

---

## Magic Packets

### Magic Request

Magic casting via `MSGID_COMMAND_COMMON` with type `DEF_COMMONTYPE_MAGIC`.

**Legacy Binary:**
```
MSGID: MSGID_COMMAND_COMMON (0x0FA314DC)
MSG_TYPE: DEF_COMMONTYPE_MAGIC (0x0A0D)

Offset  Size  Type    Field           Description
------  ----  ------  -------------   ----------------------------------
0       4     DWORD   msg_id          0x0FA314DC
4       2     WORD    cmd_type        0x0A0D (magic)
6       2     short   caster_x        Caster X position
8       2     short   caster_y        Caster Y position
10      1     char    direction       Casting direction
11      4     int     spell_id        Spell ID
15      4     int     target_id       Target entity ID (0 for ground)
19      4     int     target_x        Target X (for ground-targeted)
23      4     int     target_y        Target Y (for ground-targeted)
27      30    char[]  spell_name      Spell name string
```

**JSON:**
```json
{
  "type": "player_magic_request",
  "seq": 30,
  "data": {
    "x": 150,
    "y": 200,
    "direction": 3,
    "spell_id": 10,
    "target_type": "npc",
    "target_id": 5001,
    "target_x": 0,
    "target_y": 0,
    "timestamp": 1706620003000
  }
}
```

---

## Common Action Packets

### Common Command Structure

All common actions use `MSGID_COMMAND_COMMON`:

**Legacy Binary:**
```
MSGID: MSGID_COMMAND_COMMON (0x0FA314DC)

Offset  Size  Type    Field           Description
------  ----  ------  -------------   ----------------------------------
0       4     DWORD   msg_id          0x0FA314DC
4       2     WORD    action_type     DEF_COMMONTYPE_* (see below)
6       2     short   pos_x           Player X position
8       2     short   pos_y           Player Y position
10      1     char    direction       Direction facing
11      4     int     value1          Action-specific parameter 1
15      4     int     value2          Action-specific parameter 2
19      4     int     value3          Action-specific parameter 3
23      30    char[]  string_param    Action-specific string
53      4     int     value4          Action-specific parameter 4
```

### Common Action Types

| Constant | Value | Description | Status |
|----------|-------|-------------|--------|
| `DEF_COMMONTYPE_ITEMDROP` | `0x0A01` | Drop item | ❌ |
| `DEF_COMMONTYPE_EQUIPITEM` | `0x0A02` | Equip item | ❌ |
| `DEF_COMMONTYPE_REQ_LISTCONTENTS` | `0x0A03` | Request shop list | ❌ |
| `DEF_COMMONTYPE_REQ_PURCHASEITEM` | `0x0A04` | Buy item | ❌ |
| `DEF_COMMONTYPE_GIVEITEMTOCHAR` | `0x0A05` | Give item to player | ❌ |
| `DEF_COMMONTYPE_JOINGUILDAPPROVE` | `0x0A06` | Accept guild invite | ❌ |
| `DEF_COMMONTYPE_JOINGUILDREJECT` | `0x0A07` | Reject guild invite | ❌ |
| `DEF_COMMONTYPE_DISMISSGUILDAPPROVE` | `0x0A08` | Accept guild dismissal | ❌ |
| `DEF_COMMONTYPE_DISMISSGUILDREJECT` | `0x0A09` | Reject guild dismissal | ❌ |
| `DEF_COMMONTYPE_RELEASEITEM` | `0x0A0A` | Release/unequip item | ❌ |
| `DEF_COMMONTYPE_TOGGLECOMBATMODE` | `0x0A0B` | Toggle PvP mode | ❌ |
| `DEF_COMMONTYPE_SETITEM` | `0x0A0C` | Set item in slot | ❌ |
| `DEF_COMMONTYPE_MAGIC` | `0x0A0D` | Cast spell | 🔄 parser done |
| `DEF_COMMONTYPE_REQ_STUDYMAGIC` | `0x0A0E` | Learn spell | ❌ |
| `DEF_COMMONTYPE_REQ_TRAINSKILL` | `0x0A0F` | Train skill | ❌ |
| `DEF_COMMONTYPE_REQ_GETREWARDMONEY` | `0x0A10` | Claim gold reward | ❌ |
| `DEF_COMMONTYPE_REQ_USEITEM` | `0x0A11` | Use item | ❌ |
| `DEF_COMMONTYPE_REQ_USESKILL` | `0x0A12` | Use skill | 🔄 parser done |
| `DEF_COMMONTYPE_REQ_SELLITEM` | `0x0A13` | Sell item | ❌ |
| `DEF_COMMONTYPE_REQ_REPAIRITEM` | `0x0A14` | Repair item | ❌ |
| `DEF_COMMONTYPE_REQ_SELLITEMCONFIRM` | `0x0A15` | Confirm sell | ❌ |
| `DEF_COMMONTYPE_REQ_REPAIRITEMCONFIRM` | `0x0A16` | Confirm repair | ❌ |
| `DEF_COMMONTYPE_REQ_GETFISHTHISTIME` | `0x0A17` | Get fishing reward | ❌ |
| `DEF_COMMONTYPE_TOGGLESAFEATTACKMODE` | `0x0A18` | Toggle safe attack | ❌ |
| `DEF_COMMONTYPE_REQ_CREATEPORTION` | `0x0A19` | Create potion | ❌ |
| `DEF_COMMONTYPE_TALKTONPC` | `0x0A1A` | Talk to NPC | 🔄 parser done |
| `DEF_COMMONTYPE_REQ_SETDOWNSKILLINDEX` | `0x0A1B` | Set skill hotkey | ❌ |
| `DEF_COMMONTYPE_REQ_GETOCCUPYFLAG` | `0x0A1C` | Get territory flag | ❌ |
| `DEF_COMMONTYPE_REQ_GETHEROMANTLE` | `0x0A1D` | Get hero cape | ❌ |
| `DEF_COMMONTYPE_EXCHANGEITEMTOCHAR` | `0x0A1E` | Start trade | ❌ |
| `DEF_COMMONTYPE_SETEXCHANGEITEM` | `0x0A1F` | Set trade item | ❌ |
| `DEF_COMMONTYPE_CONFIRMEXCHANGEITEM` | `0x0A20` | Confirm trade | ❌ |
| `DEF_COMMONTYPE_CANCELEXCHANGEITEM` | `0x0A21` | Cancel trade | ❌ |
| `DEF_COMMONTYPE_QUESTACCEPTED` | `0x0A22` | Accept quest | ❌ |
| `DEF_COMMONTYPE_BUILDITEM` | `0x0A23` | Craft item | ❌ |
| `DEF_COMMONTYPE_GETMAGICABILITY` | `0x0A24` | Get magic boost | ❌ |
| `DEF_COMMONTYPE_REQ_GETOCCUPYFIGHTZONETICKET` | `0x0A25` | Get fight zone ticket | ❌ |
| `DEF_COMMONTYPE_BANGUILD` | `0x0A26` | Ban guild member | ❌ |
| `DEF_COMMONTYPE_REQUEST_ACCEPTJOINPARTY` | `0x0A30` | Accept party invite | ❌ |
| `DEF_COMMONTYPE_REQUEST_JOINPARTY` | `0x0A31` | Request party join | ❌ |
| `DEF_COMMONTYPE_RESPONSE_JOINPARTY` | `0x0A32` | Party join response | ❌ |
| `DEF_COMMONTYPE_REQUEST_ACTIVATESPECABLTY` | `0x0A40` | Activate special ability | ❌ |
| `DEF_COMMONTYPE_REQUEST_CANCELQUEST` | `0x0A50` | Cancel quest | ❌ |
| `DEF_COMMONTYPE_REQUEST_SELECTCRUSADEDUTY` | `0x0A51` | Select crusade role | ❌ |
| `DEF_COMMONTYPE_REQUEST_MAPSTATUS` | `0x0A52` | Get map status | ❌ |
| `DEF_COMMONTYPE_REQUEST_HELP` | `0x0A53` | Get help | ❌ |
| `DEF_COMMONTYPE_SETGUILDTELEPORTLOC` | `0x0A54` | Set guild teleport | ❌ |
| `DEF_COMMONTYPE_GUILDTELEPORT` | `0x0A55` | Guild teleport | ❌ |
| `DEF_COMMONTYPE_SUMMONWARUNIT` | `0x0A56` | Summon war unit | ❌ |
| `DEF_COMMONTYPE_SETGUILDCONSTRUCTLOC` | `0x0A57` | Set guild construct | ❌ |
| `DEF_COMMONTYPE_UPGRADEITEM` | `0x0A58` | Upgrade item | ❌ |
| `DEF_COMMONTYPE_REQGUILDNAME` | `0x0A59` | Request guild name | ❌ |
| `DEF_COMMONTYPE_REQ_CHANGEPLAYMODE` | `0x0A60` | Change play mode | ❌ |
| `DEF_COMMONTYPE_REQ_CREATESLATE` | `0x0A61` | Create slate | ❌ |

---

## Notification Packets

### Notification Structure

Server sends notifications via `MSGID_NOTIFY`:

**Legacy Binary:**
```
MSGID: MSGID_NOTIFY (0x0FA314D0)

Offset  Size  Type    Field           Description
------  ----  ------  -------------   ----------------------------------
0       4     DWORD   msg_id          0x0FA314D0
4       2     WORD    notify_type     DEF_NOTIFY_* (see below)
6       ...   varies  payload         Notification-specific data
```

### Common Notification Types

All notifications use `MSGID_NOTIFY` (0x0FA314D0) with the type in the `msg_type` field.

**Priority Notifications** (needed for basic gameplay):

| Constant | Value | Description | Status |
|----------|-------|-------------|--------|
| `DEF_NOTIFY_HP` | `0x0B07` | HP changed | ❌ |
| `DEF_NOTIFY_MP` | `0x0B14` | Mana changed | ❌ |
| `DEF_NOTIFY_SP` | `0x0B15` | Stamina changed | ❌ |
| `DEF_NOTIFY_EXP` | `0x0B0A` | Experience gained | ❌ |
| `DEF_NOTIFY_LEVELUP` | `0x0B16` | Level increased | ❌ |
| `DEF_NOTIFY_KILLED` | `0x0B09` | Player killed | ❌ |
| `DEF_NOTIFY_ITEMOBTAINED` | `0x0B01` | Picked up item | ❌ |
| `DEF_NOTIFY_CANNOTCARRYMOREITEM` | `0x0B05` | Inventory full | ❌ |

**Item Notifications:**

| Constant | Value | Description | Status |
|----------|-------|-------------|--------|
| `DEF_NOTIFY_ITEMPURCHASED` | `0x0B06` | Item bought | ❌ |
| `DEF_NOTIFY_ITEMLIFESPANEND` | `0x0B17` | Item expired | ❌ |
| `DEF_NOTIFY_ITEMTOBANK` | `0x0B19` | Item banked | ❌ |
| `DEF_NOTIFY_SETITEMCOUNT` | `0x0B25` | Item count changed | ❌ |
| `DEF_NOTIFY_CANNOTSELLITEM` | `0x0B2C` | Cannot sell | ❌ |
| `DEF_NOTIFY_SELLITEMPRICE` | `0x0B2D` | Sell price | ❌ |
| `DEF_NOTIFY_CANNOTREPAIRITEM` | `0x0B2E` | Cannot repair | ❌ |
| `DEF_NOTIFY_REPAIRITEMPRICE` | `0x0B2F` | Repair price | ❌ |
| `DEF_NOTIFY_ITEMREPAIRED` | `0x0B30` | Item repaired | ❌ |
| `DEF_NOTIFY_ITEMSOLD` | `0x0B31` | Item sold | ❌ |
| `DEF_NOTIFY_ITEMRELEASED` | `0x0B5C` | Item released | ❌ |
| `DEF_NOTIFY_ITEMATTRIBUTECHANGE` | `0x0BA3` | Item attribute changed | ❌ |
| `DEF_NOTIFY_ITEMUPGRADEFAIL` | `0x0BA8` | Upgrade failed | ❌ |
| `DEF_NOTIFY_NOTENOUGHGOLD` | `0x0B08` | Insufficient gold | ❌ |
| `DEF_NOTIFY_REWARDGOLD` | `0x0B4F` | Gold reward | ❌ |

**Magic/Skill Notifications:**

| Constant | Value | Description | Status |
|----------|-------|-------------|--------|
| `DEF_NOTIFY_MAGICSTUDYSUCCESS` | `0x0B10` | Spell learned | ❌ |
| `DEF_NOTIFY_MAGICSTUDYFAIL` | `0x0B11` | Spell learn failed | ❌ |
| `DEF_NOTIFY_SKILLTRAINSUCCESS` | `0x0B12` | Skill trained | ❌ |
| `DEF_NOTIFY_SKILLTRAINFAIL` | `0x0B13` | Skill train failed | ❌ |
| `DEF_NOTIFY_SKILL` | `0x0B23` | Skill info | ❌ |
| `DEF_NOTIFY_MAGICEFFECTON` | `0x0B27` | Buff applied | ❌ |
| `DEF_NOTIFY_MAGICEFFECTOFF` | `0x0B28` | Buff expired | ❌ |
| `DEF_NOTIFY_SKILLUSINGEND` | `0x0B2A` | Skill use complete | ❌ |
| `DEF_NOTIFY_SUPERATTACKLEFT` | `0x0B52` | Super attack charges | ❌ |

**Guild Notifications:**

| Constant | Value | Description | Status |
|----------|-------|-------------|--------|
| `DEF_NOTIFY_QUERY_JOINGUILDREQPERMISSION` | `0x0B02` | Guild join request | ❌ |
| `DEF_NOTIFY_QUERY_DISMISSGUILDREQPERMISSION` | `0x0B03` | Guild dismiss request | ❌ |
| `DEF_NOTIFY_WAITFORGUILDOPERATION` | `0x0B04` | Guild op in progress | ❌ |
| `DEF_NOTIFY_GUILDDISBANDED` | `0x0B0B` | Guild dissolved | ❌ |
| `DEF_NOTIFY_CANNOTJOINMOREGUILDSMAN` | `0x0B0D` | Guild full | ❌ |
| `DEF_NOTIFY_NEWGUILDSMAN` | `0x0B0E` | New guild member | ❌ |
| `DEF_NOTIFY_DISMISSGUILDSMAN` | `0x0B0F` | Guild member removed | ❌ |

**Combat/PK Notifications:**

| Constant | Value | Description | Status |
|----------|-------|-------------|--------|
| `DEF_NOTIFY_PKPENALTY` | `0x0B1A` | PK penalty | ❌ |
| `DEF_NOTIFY_PKCAPTURED` | `0x0B1B` | Captured criminal | ❌ |
| `DEF_NOTIFY_ENEMYKILLREWARD` | `0x0B1C` | Kill reward | ❌ |
| `DEF_NOTIFY_SAFEATTACKMODE` | `0x0B51` | Safe PvP mode | ❌ |
| `DEF_NOTIFY_GLOBALATTACKMODE` | `0x0B73` | Global PvP mode | ❌ |
| `DEF_NOTIFY_DAMAGEMOVE` | `0x0B74` | Damage on move | ❌ |

**Trade Notifications:**

| Constant | Value | Description | Status |
|----------|-------|-------------|--------|
| `DEF_NOTIFY_OPENEXCHANGEWINDOW` | `0x0B5E` | Trade window | ❌ |
| `DEF_NOTIFY_SETEXCHANGEITEM` | `0x0B5F` | Trade item set | ❌ |
| `DEF_NOTIFY_CANCELEXCHANGEITEM` | `0x0B60` | Trade cancelled | ❌ |
| `DEF_NOTIFY_EXCHANGEITEMCOMPLETE` | `0x0B61` | Trade complete | ❌ |

**Quest Notifications:**

| Constant | Value | Description | Status |
|----------|-------|-------------|--------|
| `DEF_NOTIFY_QUESTCONTENTS` | `0x0B66` | Quest info | ❌ |
| `DEF_NOTIFY_QUESTABORTED` | `0x0B67` | Quest cancelled | ❌ |
| `DEF_NOTIFY_QUESTCOMPLETED` | `0x0B68` | Quest finished | ❌ |
| `DEF_NOTIFY_QUESTREWARD` | `0x0B69` | Quest reward | ❌ |
| `DEF_NOTIFY_QUESTCOUNTER` | `0x0BE2` | Quest counter | ❌ |

**Party Notifications:**

| Constant | Value | Description | Status |
|----------|-------|-------------|--------|
| `DEF_NOTIFY_PARTY` | `0x0BA2` | Party info | ❌ |
| `DEF_NOTIFY_RESPONSE_CREATENEWPARTY` | `0x0B80` | Party created | ❌ |
| `DEF_NOTIFY_QUERY_JOINPARTY` | `0x0B81` | Party invite | ❌ |

**World/Object Notifications:**

| Constant | Value | Description | Status |
|----------|-------|-------------|--------|
| `DEF_NOTIFY_NEWDYNAMICOBJECT` | `0x0B21` | Object appeared | ❌ |
| `DEF_NOTIFY_DELDYNAMICOBJECT` | `0x0B22` | Object removed | ❌ |
| `DEF_NOTIFY_TIMECHANGE` | `0x0B41` | Time changed | ❌ |
| `DEF_NOTIFY_WHETHERCHANGE` | `0x0B4D` | Weather changed | ❌ |
| `DEF_NOTIFY_SHOWMAP` | `0x0B2B` | Show map info | ❌ |
| `DEF_NOTIFY_LOCKEDMAP` | `0x0B95` | Map locked | ❌ |
| `DEF_NOTIFY_SPAWNEVENT` | `0x0BAA` | Spawn event | ❌ |

**System Notifications:**

| Constant | Value | Description | Status |
|----------|-------|-------------|--------|
| `DEF_NOTIFY_TOTALUSERS` | `0x0B29` | Server population | ❌ |
| `DEF_NOTIFY_SERVERCHANGE` | `0x0B24` | Server changed | ❌ |
| `DEF_NOTIFY_SERVERSHUTDOWN` | `0x0B4E` | Server shutdown | ❌ |
| `DEF_NOTIFY_FORCEDISCONN` | `0x0B75` | Force disconnect | ❌ |
| `DEF_NOTIFY_NOTICEMSG` | `0x0B46` | Notice message | ❌ |
| `DEF_NOTIFY_EVENTMSGSTRING` | `0x0B0C` | Event message | ❌ |
| `DEF_NOTIFY_HELP` | `0x0B99` | Help message | ❌ |

**Player Status Notifications:**

| Constant | Value | Description | Status |
|----------|-------|-------------|--------|
| `DEF_NOTIFY_CHARISMA` | `0x0B32` | Charisma info | ❌ |
| `DEF_NOTIFY_HUNGER` | `0x0B39` | Hunger status | ❌ |
| `DEF_NOTIFY_PLAYERPROFILE` | `0x0B37` | Player profile | ❌ |
| `DEF_NOTIFY_PLAYERONGAME` | `0x0B33` | Player online | ❌ |
| `DEF_NOTIFY_PLAYERNOTONGAME` | `0x0B34` | Player offline | ❌ |
| `DEF_NOTIFY_WHISPERMODEON` | `0x0B35` | Whisper enabled | ❌ |
| `DEF_NOTIFY_WHISPERMODEOFF` | `0x0B36` | Whisper disabled | ❌ |
| `DEF_NOTIFY_PLAYERSHUTUP` | `0x0B42` | Player muted | ❌ |
| `DEF_NOTIFY_OBSERVERMODE` | `0x0B72` | Observer mode | ❌ |
| `DEF_NOTIFY_LIMITEDLEVEL` | `0x0B18` | Level restricted | ❌ |
| `DEF_NOTIFY_TRAVELERLIMITEDLEVEL` | `0x0B38` | Traveler level limit | ❌ |
| `DEF_NOTIFY_CHANGEPLAYMODE` | `0x0BA9` | Play mode changed | ❌ |

**NPC/Dialog Notifications:**

| Constant | Value | Description | Status |
|----------|-------|-------------|--------|
| `DEF_NOTIFY_NPCTALK` | `0x0B57` | NPC dialog | ❌ |

**Crafting Notifications:**

| Constant | Value | Description | Status |
|----------|-------|-------------|--------|
| `DEF_NOTIFY_PORTIONSUCCESS` | `0x0B56` | Potion created | ❌ |
| `DEF_NOTIFY_BUILDITEMSUCCESS` | `0x0B70` | Crafting succeeded | ❌ |
| `DEF_NOTIFY_BUILDITEMFAIL` | `0x0B71` | Crafting failed | ❌ |

**War/Event Notifications:**

| Constant | Value | Description | Status |
|----------|-------|-------------|--------|
| `DEF_NOTIFY_CRUSADE` | `0x0B94` | Crusade event | ❌ |
| `DEF_NOTIFY_METEORSTRIKECOMING` | `0x0B9B` | Meteor incoming | ❌ |
| `DEF_NOTIFY_METEORSTRIKEHIT` | `0x0B9C` | Meteor hit | ❌ |
| `DEF_NOTIFY_GRANDMAGICRESULT` | `0x0B9D` | Grand magic result | ❌ |
| `DEF_NOTIFY_TOBERECALLED` | `0x0B40` | Will be recalled | ❌ |
| `DEF_NOTIFY_NORECALL` | `0x0BD1` | Cannot recall | ❌ |
| `DEF_NOTIFY_APOCGATEOPEN` | `0x0BD4` | Apocalypse gate open | ❌ |
| `DEF_NOTIFY_APOCGATECLOSE` | `0x0BD5` | Apocalypse gate closed | ❌ |
| `DEF_NOTIFY_RESURRECTPLAYER` | `0x0BE9` | Player resurrection | ❌ |
| `DEF_NOTIFY_HELDENIANSTART` | `0x0BEA` | Heldenian started | ❌ |
| `DEF_NOTIFY_HELDENIANCOUNT` | `0x0BEC` | Heldenian countdown | ❌ |
| `DEF_NOTIFY_HELDENIANTELEPORT` | `0x0BE6` | Heldenian teleport | ❌ |

---

### Item Obtained Notification

**Legacy Binary:**
```
MSGID: MSGID_NOTIFY (0x0FA314D0)
MSG_TYPE: DEF_NOTIFY_ITEMOBTAINED (0x0B01)

Offset  Size  Type    Field           Description
------  ----  ------  -------------   ----------------------------------
0       4     DWORD   msg_id          0x0FA314D0
4       2     WORD    notify_type     0x0B01
6       1     BYTE    pickup_type     1=normal, 2=bank
7       20    char[]  item_name       Item name
27      4     DWORD   item_count      Stack count
31      1     BYTE    item_type       Item type
32      1     BYTE    equip_pos       Equipment position
33      2     WORD    sprite_id       Item sprite
35      2     WORD    sprite_frame    Sprite frame
37      1     BYTE    item_color      Item color
38      4     int     effect1         Effect 1
42      4     int     effect2         Effect 2
46      4     int     effect3         Effect 3
50      2     WORD    lifespan        Item durability
52      1     BYTE    attribute       Item attribute

Total: 53 bytes
```

---

### HP/MP/SP Notification

**Legacy Binary:**
```
MSGID: MSGID_NOTIFY (0x0FA314D0)
MSG_TYPE: DEF_NOTIFY_HP/MP/SP (0x0B07/0x0B14/0x0B15)

Offset  Size  Type    Field           Description
------  ----  ------  -------------   ----------------------------------
0       4     DWORD   msg_id          0x0FA314D0
4       2     WORD    notify_type     0x0B07 (HP), 0x0B14 (MP), 0x0B15 (SP)
6       4     int     current_value   Current HP/MP/SP
10      4     int     max_value       Maximum HP/MP/SP
```

---

### Experience Notification

**Legacy Binary:**
```
MSGID: MSGID_NOTIFY (0x0FA314D0)
MSG_TYPE: DEF_NOTIFY_EXP (0x0B0A)

Offset  Size  Type    Field           Description
------  ----  ------  -------------   ----------------------------------
0       4     DWORD   msg_id          0x0FA314D0
4       2     WORD    notify_type     0x0B0A
6       4     DWORD   exp_gained      Experience points gained
10      4     DWORD   total_exp_low   Total experience (low DWORD)
14      4     DWORD   total_exp_high  Total experience (high DWORD)
```

---

### Level Up Notification

**Legacy Binary:**
```
MSGID: MSGID_NOTIFY (0x0FA314D0)
MSG_TYPE: DEF_NOTIFY_LEVELUP (0x0B16)

Offset  Size  Type    Field           Description
------  ----  ------  -------------   ----------------------------------
0       4     DWORD   msg_id          0x0FA314D0
4       2     WORD    notify_type     0x0B16
6       2     WORD    new_level       New character level
8       4     int     new_hp          New max HP
12      4     int     new_mp          New max MP
16      4     int     new_sp          New max SP
```

---

## Server Communication Packets

### Chat Message

**Legacy Binary:**
```
MSGID: MSGID_COMMAND_CHATMSG (0x03203204)

Offset  Size  Type    Field           Description
------  ----  ------  -------------   ----------------------------------
0       4     DWORD   msg_id          0x03203204
4       2     WORD    chat_type       Chat channel type
6       10    char[]  sender_name     Sender name
16      ...   char[]  message         Chat message (null-terminated)
```

**Chat Types:**
- 0: Local chat
- 1: Global/all chat
- 2: Party chat
- 3: Guild chat
- 4: Whisper
- 5: GM broadcast

**JSON:**
```json
{
  "type": "chat_message",
  "seq": 0,
  "data": {
    "channel": "local",
    "sender": "Player1",
    "message": "Hello world!"
  }
}
```

---

### Connection Check (Ping)

**Legacy Binary:**
```
MSGID: MSGID_COMMAND_CHECKCONNECTION (0x03203203)

Offset  Size  Type    Field           Description
------  ----  ------  -------------   ----------------------------------
0       4     DWORD   msg_id          0x03203203
4       2     WORD    msg_type        0x0000
```

**JSON:**
```json
{
  "type": "ping",
  "seq": 1,
  "data": {}
}
```

---

## Data Types Reference

### Integer Types

| Type | Size | Range | Description |
|------|------|-------|-------------|
| BYTE | 1 | 0-255 | Unsigned 8-bit |
| char | 1 | -128 to 127 | Signed 8-bit |
| WORD | 2 | 0-65535 | Unsigned 16-bit |
| short | 2 | -32768 to 32767 | Signed 16-bit |
| DWORD | 4 | 0-4294967295 | Unsigned 32-bit |
| int | 4 | -2147483648 to 2147483647 | Signed 32-bit |
| BOOL | 4 | 0 or 1 | Boolean (0=false) |

### String Types

| Type | Size | Description |
|------|------|-------------|
| char[10] | 10 | Fixed-length name (null-padded) |
| char[20] | 20 | Fixed-length item/guild name |
| char[30] | 30 | Fixed-length command string |
| char[50] | 50 | Fixed-length email |

---

## Constants Reference

### Object Types

| Value | Name | Description |
|-------|------|-------------|
| 1 | `DEF_OWNERTYPE_PLAYER` | Player character |
| 2 | `DEF_OWNERTYPE_NPC` | NPC/Monster |
| 3 | `DEF_OWNERTYPE_DYNAMIC` | Dynamic object |

### Nations

| Value | Name |
|-------|------|
| 0 | None/Neutral |
| 1 | Aresden |
| 2 | Elvine |

### Gender

| Value | Name |
|-------|------|
| 0 | Male |
| 1 | Female |

### Equipment Slots

| Slot | Name |
|------|------|
| 0 | Head |
| 1 | Body |
| 2 | Arms |
| 3 | Pants |
| 4 | Boots |
| 5 | Weapon |
| 6 | Shield |
| 7 | Ring 1 |
| 8 | Ring 2 |
| 9 | Amulet |
| 10 | Cape |
| 11 | Accessory |

---

## Appendix: Gate Server Messages

Messages between game servers and gate/log servers:

| Constant | Value | Description |
|----------|-------|-------------|
| `GSM_REQUEST_FINDCHARACTER` | `0x01` | Find character |
| `GSM_RESPONSE_FINDCHARACTER` | `0x02` | Character location |
| `GSM_GRANDMAGICRESULT` | `0x03` | Grand magic result |
| `GSM_GRANDMAGICLAUNCH` | `0x04` | Launch grand magic |
| `GSM_COLLECTEDMANA` | `0x05` | Mana collected |
| `GSM_BEGINCRUSADE` | `0x06` | Start crusade |
| `GSM_ENDCRUSADE` | `0x07` | End crusade |
| `GSM_MIDDLEMAPSTATUS` | `0x08` | Crusade map status |
| `GSM_SETGUILDTELEPORTLOC` | `0x09` | Guild teleport |
| `GSM_CONSTRUCTIONPOINT` | `0x0A` | Construction points |
| `GSM_SETGUILDCONSTRUCTLOC` | `0x0B` | Guild construct |
| `GSM_CHATMSG` | `0x0C` | Cross-server chat |
| `GSM_WHISFERMSG` | `0x0D` | Cross-server whisper |
| `GSM_DISCONNECT` | `0x0E` | Player disconnect |
| `GSM_REQUEST_SUMMONPLAYER` | `0x0F` | Summon player |
| `GSM_REQUEST_SHUTUPPLAYER` | `0x10` | Mute player |
| `GSM_RESPONSE_SHUTUPPLAYER` | `0x11` | Mute response |
| `GSM_REQUEST_SETFORCERECALLTIME` | `0x12` | Set recall timer |
| `GSM_BEGINAPOCALYPSE` | `0x13` | Apocalypse start |
| `GSM_ENDAPOCALYPSE` | `0x14` | Apocalypse end |
| `GSM_REQUEST_SUMMONGUILD` | `0x15` | Summon guild |
| `GSM_REQUEST_SUMMONALL` | `0x16` | Summon all |
| `GSM_ENDHELDENIAN` | `0x17` | Heldenian end |
| `GSM_UPDATECONFIGS` | `0x18` | Update configs |
| `GSM_STARTHELDENIAN` | `0x19` | Heldenian start |

---

## Version History

| Version | Date | Changes |
|---------|------|---------|
| 1.0 | 2026-01 | Initial documentation from legacy code analysis |
