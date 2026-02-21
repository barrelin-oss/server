# Plan 01: Apocalypse System Redesign

## Problem

The current `apocalypse_system` implements a **wave-based PvE event** with progressive NPC spawning. The legacy apocalypse is fundamentally different — it's a **server-wide mode toggle** that opens gates to special maps. This is the single largest discrepancy across all war subsystems.

## Legacy Behavior (Reference: `Game.cpp`)

### What the apocalypse IS:
- A boolean mode flag (`m_bIsApocalypseMode`) toggled on/off
- When ON: sends `DEF_NOTIFY_APOCGATESTARTMSG` to all clients (client renders a gate)
- When OFF: sends `DEF_NOTIFY_APOCGATEENDMSG` to all clients
- **Zero NPC spawning** — no waves, no bosses, no PvE

### Gate mechanic (in `CheckClientResponseTime`, called every 3 seconds):
- Maps have `m_bIsApocalypseMap` flag (set in map config)
- During apocalypse: sends `DEF_NOTIFY_APOCGATEOPEN` to players on "abaddon" map (gate coords 167,169) and "icebound" map (gate coords 89,31)
- Players standing on icebound gate tiles (89,31 / 89,32 / 90,31 / 90,32) are teleported to "druncncity"
- When apocalypse ends: players on `m_bIsApocalypseMap == TRUE` maps are teleported home

### Scheduling:
- **Separate start and end schedules**: `m_stApocalypseScheduleStart[]` and `m_stApocalypseScheduleEnd[]`
- Each entry: `{iDay, iHour, iMinute}` (day-of-week, hour, minute)
- `ApocalypseStarter()` is **commented out** in legacy — apocalypse can only auto-end, not auto-start (started via admin or gate server)
- `ApocalypseEnder()` checks the end schedule and calls `GlobalEndApocalypseMode()`

### GUID persistence:
- Reads/writes `GameData/ApocalypseGUID.Txt` with a single DWORD identifier
- Used for multi-server event coordination (not needed for single-server)

### Start flow (`LocalStartApocalypse`):
1. Guard: return if already active
2. Set `m_bIsApocalypseMode = TRUE`
3. Write GUID file
4. Send `DEF_NOTIFY_APOCGATESTARTMSG` to ALL connected clients
5. Log "Apocalypse Mode ON"

### End flow (`GlobalEndApocalypseMode` / `LocalEndApocalypse`):
1. Guard: return if not active
2. Set `m_bIsApocalypseMode = FALSE`
3. Send `DEF_NOTIFY_APOCGATEENDMSG` to ALL connected clients
4. Log "Apocalypse Mode OFF"
5. (Periodic tick handles teleporting players off apocalypse maps)

### What the legacy apocalypse does NOT have:
- No waves
- No NPC spawning
- No boss fights
- No per-wave EXP bonuses
- No wave kill tracking
- No max duration timer (uses explicit end schedule)
- No rewards or score tracking

## Current New Code Location

- `src/war/apocalypse/apocalypse_system.h`
- `src/war/apocalypse/apocalypse_system.cpp`
- `src/war/apocalypse/apocalypse_types.h`
- `tests/test_apocalypse_system.cpp`

## Implementation Plan

### Step 1: Rewrite `apocalypse_types.h`

Replace the wave-based types with the legacy gate-toggle model:

```cpp
// Remove: apocalypse_wave, wave-based config fields
// Keep: apocalypse_schedule_entry (but add end schedule support)

struct apocalypse_schedule_entry
{
    uint8_t day_of_week{0};
    uint8_t hour{0};
    uint8_t minute{0};
};

struct apocalypse_gate
{
    std::string map_name;         // e.g., "abaddon", "icebound"
    int16_t gate_x{0};
    int16_t gate_y{0};
    std::string destination_map;  // e.g., "druncncity" (empty = no teleport, just notification)
    // Gate teleport tiles (players standing on these get teleported)
    std::vector<std::pair<int16_t, int16_t>> teleport_tiles;
};

struct apocalypse_config
{
    std::vector<apocalypse_schedule_entry> start_schedule;
    std::vector<apocalypse_schedule_entry> end_schedule;  // SEPARATE end schedule
    std::vector<apocalypse_gate> gates;
    // Maps flagged as apocalypse maps — players teleported out when event ends
    std::vector<std::string> apocalypse_maps;
    float gate_check_interval_seconds{3.0f};  // How often to check gate tiles
};
```

### Step 2: Rewrite `apocalypse_system.h/.cpp`

The system becomes much simpler:

**State:**
- `bool active_` — is apocalypse mode on
- `float gate_check_accumulator_` — timer for periodic gate tile checks

**Public API:**
- `start_event()` — set active, broadcast gate start message
- `end_event()` — set inactive, broadcast gate end message
- `update(float delta_time)` — check end schedule, periodic gate tile checks
- `is_active() const` — query state
- `check_start_schedule()` — match current time to start schedule (can be called by admin system or scheduler)
- `check_end_schedule()` — match current time to end schedule

**Callbacks (set by application wiring):**
- `broadcast_fn_` — send JSON message to all players
- `get_players_on_map_fn_` — get players on a specific map (for gate notifications)
- `teleport_player_fn_` — teleport a player to a destination map
- `get_player_map_fn_` — get player's current map name

**Periodic gate check logic (every 3 seconds):**
```
for each gate in config.gates:
    for each player on gate.map_name:
        send gate_open notification with gate coords
        if player position is on any teleport_tile:
            teleport to gate.destination_map
```

**End event cleanup:**
```
for each map_name in config.apocalypse_maps:
    for each player on that map:
        teleport player home (to their faction town)
```

### Step 3: Update protocol messages

Replace wave-based messages with gate messages:

- `apocalypse_started` → keep, but change data: `{active: true}`
- `apocalypse_ended` → keep, but change data: `{active: false}`
- `apocalypse_gate_open` → NEW: `{map_name, gate_x, gate_y}` (sent periodically to players near gates)
- Remove: `apocalypse_wave_started`, `apocalypse_wave_completed`

### Step 4: Rewrite tests

Tests should verify:
1. `start_event()` sets active and broadcasts
2. `end_event()` sets inactive and broadcasts
3. Schedule matching works for both start and end schedules
4. Gate tile check detects players at teleport tiles and teleports them
5. End event teleports players off apocalypse maps
6. Cannot start when already active
7. Cannot end when not active
8. Gate notifications sent to players on gate maps only

### Step 5: Update application wiring

In `application.cpp` and `game_handlers.cpp`, wire the callbacks appropriately. The apocalypse system needs access to:
- `player_system` (for player iteration by map)
- `world_subsystem` (for map lookups)
- Network send function (for broadcasts)

### Step 6: Update `crusade.yaml` or create `apocalypse.yaml`

Config should define:
```yaml
apocalypse:
  gates:
    - map: abaddon
      gate_x: 167
      gate_y: 169
      destination: ""  # no teleport, just notification
    - map: icebound
      gate_x: 89
      gate_y: 31
      destination: druncncity
      teleport_tiles:
        - [89, 31]
        - [89, 32]
        - [90, 31]
        - [90, 32]
  apocalypse_maps:
    - abaddon
    - icebound
    - druncncity
  end_schedule:
    - day: 0
      hour: 22
      minute: 0
```

## Files to Modify
- `src/war/apocalypse/apocalypse_types.h` — rewrite
- `src/war/apocalypse/apocalypse_system.h` — rewrite
- `src/war/apocalypse/apocalypse_system.cpp` — rewrite
- `tests/test_apocalypse_system.cpp` — rewrite
- `src/network/json_protocol.h` — update message types
- `src/network/json_protocol.cpp` — update message types
- `src/application.cpp` — update wiring
- `src/bridge/handlers/game_handlers.cpp` — update handler if needed

## Acceptance Criteria
- Apocalypse is a mode toggle, not a wave system
- Gates open on specific maps and teleport players through them
- Separate start and end schedules
- Players ejected from apocalypse maps when event ends
- All existing tests replaced with new tests covering the gate mechanic
- Builds and all tests pass
