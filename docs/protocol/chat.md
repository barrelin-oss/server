# Chat & Commands

[← Back to Protocol Index](../JSON_PROTOCOL.md)

## Chat Messages

### `chat_message`

Client sends a chat message.

**Request:**
```json
{
  "type": "chat_message",
  "seq": 300,
  "data": {
    "content": "Hello everyone!",
    "channel": "local",
    "recipient": "PlayerName",
    "timestamp": 1706500000000
  }
}
```

| Field | Type | Required | Description |
|-------|------|----------|-------------|
| `content` | string | Yes | Message content (may include prefix like `!` for shout) |
| `channel` | string | No | Explicit channel override |
| `recipient` | string | No | Recipient name (for whispers) |
| `timestamp` | uint64 | No | Client timestamp in milliseconds |

**Chat Channels:**

| Channel | Prefix | Description |
|---------|--------|-------------|
| `local` | (none) | Nearby players (default) |
| `shout` | `!` | Server-wide |
| `guild` | `@` | Guild members |
| `party` | `$` | Party members |
| `whisper` | `#` or recipient name | Private message |
| `global` | - | Global channel |
| `trade` | - | Trade channel |
| `faction` | - | Faction channel (Aresden/Elvine) |
| `system` | - | System messages (server-generated only) |

**Response:**
```json
{
  "type": "chat_message",
  "seq": 300,
  "data": {
    "success": true
  }
}
```

---

### `chat_message_broadcast`

Server broadcasts chat message to recipients.

**Server Broadcast:**
```json
{
  "type": "chat_message_broadcast",
  "seq": 0,
  "data": {
    "channel": "local",
    "sender_id": 1001,
    "sender_name": "Warrior1",
    "content": "Hello everyone!",
    "flags": [],
    "timestamp": "2024-01-29T12:00:00Z",
    "recipient_name": null
  }
}
```

| Field | Type | Description |
|-------|------|-------------|
| `channel` | string | Chat channel |
| `sender_id` | uint32 | Sender player ID (0 for system) |
| `sender_name` | string | Sender display name |
| `content` | string | Message content |
| `flags` | array | Flags: `"emote"`, `"censored"`, `"system"`, `"gm"` |
| `timestamp` | string | ISO 8601 timestamp |
| `recipient_name` | string | Recipient name (for whisper, optional) |

---

## Command Messages

### `command_request`

Client sends a command (e.g., /help, /who).

**Request:**
```json
{
  "type": "command_request",
  "seq": 400,
  "data": {
    "command": "teleport",
    "args": ["aresden", "100", "150"],
    "params": {},
    "timestamp": 1706500000000
  }
}
```

| Field | Type | Required | Description |
|-------|------|----------|-------------|
| `command` | string | Yes | Command name (without /) |
| `args` | array | No | Command arguments |
| `params` | object | No | Named parameters |
| `timestamp` | uint64 | No | Client timestamp in milliseconds |

---

### `command_response`

Server responds to command.

**Response:**
```json
{
  "type": "command_response",
  "seq": 400,
  "data": {
    "success": true,
    "command": "teleport",
    "message": "Teleported to aresden (100, 150)",
    "result": {}
  }
}
```

| Field | Type | Description |
|-------|------|-------------|
| `success` | bool | Whether command succeeded |
| `command` | string | Echo of the command |
| `message` | string | Success/error message |
| `result` | object | Command-specific result data |

---
