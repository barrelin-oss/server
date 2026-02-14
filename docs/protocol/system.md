# System Messages

[← Back to Protocol Index](../JSON_PROTOCOL.md)

## `ping`

Client sends to check connection and measure latency.

**Request:**
```json
{
  "type": "ping",
  "seq": 1,
  "data": {}
}
```

**Response:** See `pong`

---

## `pong`

Server response to `ping`.

**Response:**
```json
{
  "type": "pong",
  "seq": 1,
  "data": {
    "timestamp": 1706500000000
  }
}
```

| Field | Type | Description |
|-------|------|-------------|
| `timestamp` | int64 | Server timestamp in milliseconds since epoch |

---

## `error`

Generic error response for any failed request.

**Response:**
```json
{
  "type": "error",
  "seq": 5,
  "data": {
    "error_code": "INVALID_REQUEST",
    "message": "Description of what went wrong"
  }
}
```

| Field | Type | Description |
|-------|------|-------------|
| `error_code` | string | Machine-readable error code |
| `message` | string | Human-readable error description |

**Common Error Codes:**

| Code | Description |
|------|-------------|
| `NOT_AUTHENTICATED` | Action requires authentication |
| `NOT_IN_GAME` | Action requires being in-game |
| `INVALID_REQUEST` | Malformed request data |
| `INTERNAL_ERROR` | Server-side error |
| `RATE_LIMITED` | Too many requests |

---
