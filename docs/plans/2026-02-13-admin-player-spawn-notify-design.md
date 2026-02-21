# Admin Spectator: Player Spawn/Despawn Notifications

## Problem

When a player enters or leaves a map, admin spectators watching that map are not notified. NPC spawn/despawn already forwards to admin subscribers correctly. This causes stale state on admin panels — players appear only on initial subscription snapshot but never update as they enter/leave.

## Solution

Add `get_admin_subscribers()` forwarding at the 3 existing player spawn/despawn sites, using the same pattern as NPC broadcasts. No new protocol messages or data structures needed.

## Changes

### 1. Player enters game (`auth_handlers.cpp:~1183`)

After the existing "notify nearby players" loop, forward an `entity_spawn` message to admin subscribers on that map. Use neutral hostility (admins see objective view, same as NPC spawns).

### 2. Player disconnects (`auth_handlers.cpp:~1578`)

After the existing "notify nearby players of despawn" loop, forward the `entity_despawn` message to admin subscribers on that map.

### 3. Player teleports (`game_handlers.cpp:2885-2978`)

Two forwarding points:
- **Despawn from old map**: Forward `entity_despawn` to admin subscribers on the old map (after line ~2898)
- **Spawn on new map**: Forward `entity_spawn` with neutral hostility to admin subscribers on the new map (after line ~2978)

## Pattern (from existing NPC spawn broadcast)

```cpp
// Forward to admin spectators
for (auto admin_conn : ws_server_->get_admin_subscribers(map_id)) {
    ws_server_->send(admin_conn, msg);
}
```

## Scope

- 3 files touched: `auth_handlers.cpp`, `game_handlers.cpp`
- Actually just 2 files — all changes are in handler code
- No new protocol messages
- No new tests needed beyond verifying the wiring works (existing admin subscriber infrastructure is already tested)
