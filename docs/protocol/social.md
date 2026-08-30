# Social (Friends & Party)

[← Back to Protocol Index](../JSON_PROTOCOL.md)

## Party System

Parties share kill experience among eligible members on the same map (default mode: `equal_split`,
see `social::exp_mode`). Max 8 members; invites expire after 60 seconds.

### `party_invite_request`

**Direction:** Client → Server

Invite a player (by character name) to the sender's party. If the sender is not in a party, one is
created implicitly and the sender becomes its leader.

```json
{
  "type": "party_invite_request",
  "seq": 300,
  "data": { "target_name": "OtherPlayer" }
}
```

### `party_invite_response`

**Direction:** Server → Client (same `seq`)

```json
{ "type": "party_invite_response", "seq": 300, "data": { "success": true, "party_id": 1 } }
```

On failure, `data` is `{ "success": false, "error": "<code>" }` with codes:
`player_not_found`, `already_in_party`, `party_full`, `not_leader`, `party_not_found`.

### `party_invite_notice`

**Direction:** Server → Client (broadcast to invitee, `seq: 0`)

Sent to the invited player when an invite is issued.

```json
{ "type": "party_invite_notice", "seq": 0, "data": { "party_id": 1, "inviter_name": "Leader" } }
```

### `party_accept_request`

**Direction:** Client → Server

Accept (or decline, with `"accept": false`) a pending invite.

```json
{
  "type": "party_accept_request",
  "seq": 301,
  "data": { "party_id": 1, "accept": true }
}
```

### `party_accept_response`

**Direction:** Server → Client (same `seq`)

```json
{ "type": "party_accept_response", "seq": 301, "data": { "success": true, "party_id": 1 } }
```

Failure codes: `no_pending_invite`, `invite_expired`, `party_full`, `already_in_party`,
`party_not_found`.

### `party_leave_request`

**Direction:** Client → Server

```json
{ "type": "party_leave_request", "seq": 302, "data": {} }
```

Response: `party_leave_response` with `{ "success": bool }`.

### `party_update`

**Direction:** Server → Client (broadcast to all party members, `seq: 0`)

Sent whenever party membership changes (member joined or left).

```json
{
  "type": "party_update",
  "seq": 0,
  "data": {
    "party_id": 1,
    "leader_name": "Leader",
    "members": ["Leader", "Member2", "Member3"]
  }
}
```

---

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
