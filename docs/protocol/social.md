# Social (Friends)

[← Back to Protocol Index](../JSON_PROTOCOL.md)

## Friend System

### `friend_request_send_request`

**Direction:** Client → Server

Send a friend request to another player by name.

```json
{
  "type": "friend_request_send_request",
  "seq": 1,
  "data": {
    "target_name": "Bob"
  }
}
```

| Field | Type | Description |
|-------|------|-------------|
| `target_name` | string | Name of the player to send a friend request to |

### `friend_request_send_response`

**Direction:** Server → Client

Result of sending a friend request.

```json
{
  "type": "friend_request_send_response",
  "seq": 1,
  "data": {
    "success": true
  }
}
```

On failure:

```json
{
  "type": "friend_request_send_response",
  "seq": 1,
  "data": {
    "success": false,
    "error": "already_friends"
  }
}
```

| Field | Type | Description |
|-------|------|-------------|
| `success` | bool | Whether the request was sent |
| `error` | string? | Error reason: `cannot_add_self`, `already_friends`, `request_already_exists`, `is_blocked`, `friend_limit_reached`, `player_not_found` |

**Note:** If the target already has a pending request to the sender, the system auto-accepts and creates a mutual friendship immediately.

### `friend_request_accept_request`

**Direction:** Client → Server

Accept a pending friend request from another player.

```json
{
  "type": "friend_request_accept_request",
  "seq": 2,
  "data": {
    "target_name": "Alice"
  }
}
```

### `friend_request_accept_response`

**Direction:** Server → Client

```json
{
  "type": "friend_request_accept_response",
  "seq": 2,
  "data": {
    "success": true
  }
}
```

| Field | Type | Description |
|-------|------|-------------|
| `success` | bool | Whether the request was accepted |
| `error` | string? | `no_pending_request`, `player_not_found` |

### `friend_request_decline_request`

**Direction:** Client → Server

Decline a pending friend request.

```json
{
  "type": "friend_request_decline_request",
  "seq": 3,
  "data": {
    "target_name": "Alice"
  }
}
```

### `friend_request_decline_response`

**Direction:** Server → Client

```json
{
  "type": "friend_request_decline_response",
  "seq": 3,
  "data": {
    "success": true
  }
}
```

### `friend_request_cancel_request`

**Direction:** Client → Server

Cancel an outgoing friend request that you sent.

```json
{
  "type": "friend_request_cancel_request",
  "seq": 4,
  "data": {
    "target_name": "Bob"
  }
}
```

### `friend_request_cancel_response`

**Direction:** Server → Client

```json
{
  "type": "friend_request_cancel_response",
  "seq": 4,
  "data": {
    "success": true
  }
}
```

### `friend_remove_request`

**Direction:** Client → Server

Remove an accepted friend.

```json
{
  "type": "friend_remove_request",
  "seq": 5,
  "data": {
    "target_name": "Bob"
  }
}
```

### `friend_remove_response`

**Direction:** Server → Client

```json
{
  "type": "friend_remove_response",
  "seq": 5,
  "data": {
    "success": true
  }
}
```

### `friend_block_request`

**Direction:** Client → Server

Block a player. If they are currently a friend, the friendship is removed. If there is a pending request, it is deleted. Blocking is unidirectional.

```json
{
  "type": "friend_block_request",
  "seq": 6,
  "data": {
    "target_name": "Bob"
  }
}
```

### `friend_block_response`

**Direction:** Server → Client

```json
{
  "type": "friend_block_response",
  "seq": 6,
  "data": {
    "success": true
  }
}
```

### `friend_unblock_request`

**Direction:** Client → Server

Unblock a previously blocked player.

```json
{
  "type": "friend_unblock_request",
  "seq": 7,
  "data": {
    "target_name": "Bob"
  }
}
```

### `friend_unblock_response`

**Direction:** Server → Client

```json
{
  "type": "friend_unblock_response",
  "seq": 7,
  "data": {
    "success": true
  }
}
```

### `friend_list_request`

**Direction:** Client → Server

Request the full friend list, pending requests, and blocked players.

```json
{
  "type": "friend_list_request",
  "seq": 8,
  "data": {}
}
```

### `friend_list_response`

**Direction:** Server → Client

```json
{
  "type": "friend_list_response",
  "seq": 8,
  "data": {
    "friends": [
      {
        "name": "Bob",
        "is_online": true,
        "last_online": "2026-02-11T12:00:00Z",
        "created_at": "2026-02-10T08:00:00Z"
      }
    ],
    "incoming_requests": [
      {
        "requester_name": "Charlie",
        "created_at": "2026-02-11T10:00:00Z"
      }
    ],
    "outgoing_requests": [
      {
        "requester_name": "Dave",
        "created_at": "2026-02-11T09:00:00Z"
      }
    ],
    "blocked": [
      {
        "name": "Eve",
        "is_online": false,
        "last_online": "",
        "created_at": ""
      }
    ]
  }
}
```

| Field | Type | Description |
|-------|------|-------------|
| `friends` | array | List of accepted friends with online status |
| `incoming_requests` | array | Friend requests sent TO this player |
| `outgoing_requests` | array | Friend requests sent BY this player |
| `blocked` | array | Players blocked by this player |

### `friend_request_notification`

**Direction:** Server → Client (push)

Another player sent you a friend request.

```json
{
  "type": "friend_request_notification",
  "seq": 0,
  "data": {
    "requester_name": "Alice"
  }
}
```

### `friend_accepted_notification`

**Direction:** Server → Client (push)

A player you sent a friend request to has accepted.

```json
{
  "type": "friend_accepted_notification",
  "seq": 0,
  "data": {
    "friend_name": "Bob"
  }
}
```

### `friend_online_notification`

**Direction:** Server → Client (push)

An accepted friend has come online.

```json
{
  "type": "friend_online_notification",
  "seq": 0,
  "data": {
    "friend_name": "Bob"
  }
}
```

### `friend_offline_notification`

**Direction:** Server → Client (push)

An accepted friend has gone offline.

```json
{
  "type": "friend_offline_notification",
  "seq": 0,
  "data": {
    "friend_name": "Bob"
  }
}
```

---
