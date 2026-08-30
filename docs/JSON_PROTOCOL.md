# Helbreath WebSocket JSON Protocol

## Overview

This document describes the JSON-based WebSocket protocol used for client-server communication in the Helbreath game server.

### Transport

- **Protocol:** WebSocket (RFC 6455)
- **Default Port:** 2848 (configurable)
- **Message Format:** JSON over WebSocket text frames

### Message Structure

All messages follow a common envelope structure:

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
- Server-initiated messages (broadcasts) use `seq: 0`

---

## Implementation Notes

### Confirm/Reject Response Pattern

**IMPORTANT:** When implementing new client request handlers, most actions require both a **confirm** (success) and **reject** (failure) response path. Before implementing any new packet handler, verify with the project lead whether the action needs:

1. **Confirm response** - Sent when action succeeds (e.g., item picked up, attack landed)
2. **Reject response** - Sent when action fails (e.g., inventory full, target out of range)
3. **Broadcast** - Sent to other nearby players to inform them of the action

**Examples of confirm/reject patterns:**

| Action | Needs Confirm | Needs Reject | Needs Broadcast |
|--------|--------------|--------------|-----------------|
| Movement | Yes | Yes (blocked) | Yes (position update) |
| Attack | Yes | Yes (out of range) | Yes (combat broadcast) |
| Pickup | Yes | Yes (inventory full) | Yes (item removed) |
| Chat | Yes | Yes (rate limited) | Yes (message broadcast) |
| Teleport | Yes | Yes (invalid dest) | Yes (despawn/spawn) |

**When to skip reject:**
- Some broadcasts don't need client-side confirmation (e.g., HP updates)
- Pure informational messages from server don't need responses

---

## Message Categories

| Category | File | Description |
|----------|------|-------------|
| System | [protocol/system.md](protocol/system.md) | ping, pong, error |
| Authentication | [protocol/auth.md](protocol/auth.md) | Login, accounts, characters, game entry |
| Player State | [protocol/player.md](protocol/player.md) | Game state objects, entity visibility, stat updates, experience/level-up updates, view mode |
| Movement | [protocol/movement.md](protocol/movement.md) | Movement, teleportation |
| Combat | [protocol/combat.md](protocol/combat.md) | Attacks, damage, death, magic, skills, combat mode, action broadcasts, ground-field dynamic objects |
| Items (v2) | [protocol/items-v2.md](protocol/items-v2.md) | Inventory, equipment, ground items, trade, shop, bank, loot |
| Items (v1, deprecated) | [protocol/items.md](protocol/items.md) | Legacy item protocol (superseded by v2) |
| NPCs | [protocol/npc.md](protocol/npc.md) | NPC spawn/movement/death, interaction, shops, banking, dialog |
| Chat | [protocol/chat.md](protocol/chat.md) | Chat messages, commands |
| Crafting | [protocol/crafting.md](protocol/crafting.md) | Manufacturing, alchemy, mining, fishing |
| Guild | [protocol/guild.md](protocol/guild.md) | Guild management, ranks, invites |
| Social | [protocol/social.md](protocol/social.md) | Friend system, party (invite/accept/leave/update) |
| War | [protocol/war.md](protocol/war.md) | Crusade, Heldenian, Apocalypse, force recall, rewards |
| Commands | [protocol/commands.md](protocol/commands.md) | Command list, availability updates |
| Admin | [ADMIN_PROTOCOL.md](ADMIN_PROTOCOL.md) | Admin web tool (32 request/response pairs) |

---

## Connection State Flow

```
Connected
    |
    v
[login_request] --> Authenticated
    |
    +----------------------------+
    |                            |
    v                            v
[get_characters_request]    [enter_admin_mode_request]
    |                            |
    v                            v
[enter_game_request]        Admin Dashboard
    |                            |
    v                            +-- [admin_*_request] --> Dashboard queries
In Game                          |
    |                            +-- [admin_subscribe_map_request] --> Spectator
    +-- [player_move_request]    |
    |                            +-- [admin_subscribe_player_request] --> Follow
    +-- [player_attack_request]  |
    |                            +-- [admin_unsubscribe_request]
    +-- [chat_message]           |
    |                            +-- [disconnect] --> (connection closed)
    +-- [command_request]
    |
    +-- [logout_request] --> Authenticated
    |
    +-- [disconnect] --> (connection closed)
```

## Visibility System

- **Visibility Radius:** Dynamic based on client screen resolution
- **Default Radius:** 20 tiles (for 640x480)
- **Calculation:** `max(screen_width, screen_height) / 32 / 2 + 5` tiles
- **When player moves:**
  - Entities entering range: `entity_spawn` or `npc_spawn` sent to player
  - Entities leaving range: `entity_despawn` or `npc_despawn` sent to player
  - Player's position: `player_position_update` broadcast to nearby
- **When player enters game:** `enter_game_response` contains all visible entities
- **When player teleports/changes map:** `player_teleport` with new area entities

## Error Handling

1. **Request Errors:** Responded with same `seq` and error field
2. **Connection Errors:** Connection closed by server
3. **Rate Limiting:** `error` message with `RATE_LIMITED` code

## Item Protocol v2

The item system has been redesigned with a new protocol. See [docs/protocol/items-v2.md](protocol/items-v2.md) for the complete v2 specification.

Key changes from v1:
- Universal item object shape used everywhere (inventory, ground, trade, bank, shop)
- Action results are acknowledgments only; state changes flow through dedicated update channels
- Three-phase trading protocol (offer → lock → confirm)
- Paginated bank with page+slot addressing
- Combined inventory_data message on login (items + equipment + gold + weight)

---

## Best Practices for Clients

1. **Track sequence numbers** for request/response matching
2. **Handle broadcasts** (seq=0) separately from responses
3. **Maintain entity cache** updated by spawn/despawn/position messages
4. **Reconnect logic:** Re-authenticate and enter game on disconnect
5. **Optimistic movement:** Show movement immediately, correct on rejection
6. **Update view range** when resolution or view mode changes using `set_view_range`
7. **Handle `set_render_mode`** to switch rendering modes as directed by the server
