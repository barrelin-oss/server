# Skill Update & Progress Messages

## Problem

The legacy server sends `DEF_NOTIFY_SKILL` (0x0B23) when a skill level changes, showing floating "+N%" text on the player. The modernized server has no equivalent — it only sends the full `skills_data` (all 24 skills) at login and after some level-ups. There's no incremental update message, and no progress feedback between level-ups.

## Design

Two new server-to-client messages:

### 1. `skill_update` — Level Changed

Sent when a skill levels up (or down). Broadcast to the player AND all visible players via `get_players_who_can_see()`.

```json
{
  "type": "skill_update",
  "data": {
    "player_id": 42,
    "skill_id": 8,
    "old_level": 54,
    "level": 55,
    "total_uses": 1203,
    "uses_this_level": 0,
    "uses_to_next_level": 110
  }
}
```

- `player_id`: whose skill changed (so nearby players know who to show the effect on)
- `old_level` + `level`: client computes delta for floating "+N%" text
- Progress fields: owning player updates skill UI; nearby players can ignore

Wired via the existing `skill_system::on_level_up()` callback (currently unregistered).

### 2. `skill_progress` — SSN Progress Update

Sent to the owning player only, when progress crosses a 5% threshold between level-ups. Not broadcast.

```json
{
  "type": "skill_progress",
  "data": {
    "skill_id": 8,
    "uses_this_level": 55,
    "uses_to_next_level": 110,
    "percent": 50
  }
}
```

At most 20 messages per level (every 5%).

### Throttling

`skill_system` tracks `last_reported_percent` per skill per player (uint8_t, floored to nearest 5). On every `record_skill_use()` / `add_skill_uses()`:

1. Compute `percent = (uses_this_level * 100) / uses_to_next_level`, floor to nearest 5
2. If `percent != last_reported_percent`, fire `on_skill_progress` callback, update stored value
3. Reset to 0 on level-up

### Wiring

- Register `on_level_up` callback in `game_handlers` — looks up player map/position, builds `skill_update`, broadcasts via `broadcast_to_visible`
- Register `on_skill_progress` callback in `game_handlers` — sends `skill_progress` to owning player only
- Remove redundant full `skills_data` re-sends after level-ups (e.g., `game_handlers_movement.cpp:506`)

### Files to Modify

| File | Change |
|------|--------|
| `src/network/json_protocol.h` | Add `skill_update` + `skill_progress` enum entries, data structs, builder functions |
| `src/network/json_protocol.cpp` | Implement builder functions |
| `src/skill/skill_system.h` | Add `on_skill_progress` callback type, `last_reported_percent` tracking |
| `src/skill/skill_system.cpp` | Implement progress throttling, fire progress callback |
| `src/bridge/handlers/game_handlers.h` | Declare callback registration methods |
| `src/bridge/handlers/game_handlers.cpp` | Register callbacks, build/send messages |
| `src/bridge/handlers/game_handlers_movement.cpp` | Remove redundant `skills_data` re-send |
| `tests/test_skill.cpp` | Test progress throttling, callback firing |
| `docs/JSON_PROTOCOL.md` | Document new messages |
