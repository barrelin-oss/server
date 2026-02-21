# Plan 09: Force Recall Crusade Integration

## Problems

1. **Force recall doesn't pause during crusade** — legacy pauses the countdown entirely during crusade mode; new system has no crusade awareness
2. **`ForceRecallProcess` instant-recall is missing** — legacy immediately teleports enemies out of faction buildings during crusade (separate from timer-based recall)
3. **No crusade-end civilian timer clear** — `RemoveCrusadeRecallTime` clears timers for civilian players at crusade end
4. **No "inside own town" pause** — legacy pauses countdown when player is in their own faction's territory during crusade
5. **Building debuff removal missing** — legacy strips Confuse/Illusion from players in buildings during crusade

## Legacy Behavior (Reference: `Game.cpp`)

### Force recall countdown pause during crusade (line 3702-3705):
```cpp
// In CheckClientResponseTime (called every 3 seconds):
if (m_bIsCrusadeMode == FALSE)          // Only decrement if NO crusade
    if (m_pClientList[i]->m_bIsInsideOwnTown == FALSE)  // And not in own town
        m_pClientList[i]->m_iTimeLeft_ForceRecall--;
```
During crusade, the timer NEVER decrements. Crusade has its own recall rules via `ForceRecallProcess`.

### `ForceRecallProcess` instant building recall (line 54600):
Called **every game tick** (not every 3 seconds). Separate from timer-based recall:
```cpp
for each connected, initialized client:
    iMapSide = iGetMapLocationSide(client->m_cMapName)

    // During crusade: teleport enemies out of faction buildings
    if (m_bIsCrusadeMode == TRUE):
        if (client->m_cSide == ARESDEN && iMapSide == 2):
            // Aresden player in Elvine zone during crusade
            RequestTeleportHandler(i, "2   ", "aresden", -1, -1)
        if (client->m_cSide == ELVINE && iMapSide == 1):
            // Elvine player in Aresden zone during crusade
            RequestTeleportHandler(i, "2   ", "elvine", -1, -1)

    // Building clearance (always, not just crusade):
    if client is in building maps (wrhus, gshop_1, bsmith_1, cath_1,
       CmdHall_1, cityhall_1, gshop_2, bsmith_2, cath_2, CmdHall_2,
       cityhall_2, wzdtwr, gldhall):
        // Remove Confuse/Illusion buff
        if (client->m_iStatus & 0x00200000):
            remove_status_effect(CONFUSE/ILLUSION)
```

### `RemoveCrusadeRecallTime` (line 44747):
Called at crusade end:
```cpp
for each connected, initialized client:
    if (client->m_bIsWarLocation == TRUE &&
        client->m_bIsPlayerCivil == TRUE):  // Only civilians
        client->m_iTimeLeft_ForceRecall = 0;
```

### `m_bIsInsideOwnTown` (line 1991-1998):
Set when player enters a map:
```cpp
m_bIsInsideOwnTown = FALSE;
if (player_side != map_side && map_side != 0):
    if (map_side <= 2 && admin_level < 1):
        if (bIsCrusadeMode == TRUE):
            m_bIsWarLocation = TRUE;
            m_iTimeLeft_ForceRecall = 1;
            m_bIsInsideOwnTown = TRUE;  // Pauses countdown
```
This marks players who are in their own faction's territory during crusade as "inside own town" — their timer doesn't decrement.

## Current New Code

File: `src/war/force_recall/force_recall_system.h/.cpp`

The current system has:
- Per-player trackers with `time_remaining_seconds`
- `tick_trackers()` decrements unconditionally (no crusade check)
- No building recall
- No civilian concept
- No own-town pause

## Implementation Plan

### Step 1: Add crusade awareness to force_recall_system

Add a way for the system to know if crusade is active:

```cpp
// In force_recall_system.h:
using is_crusade_active_fn = std::function<bool()>;
is_crusade_active_fn is_crusade_active_;

void set_crusade_check(is_crusade_active_fn fn) { is_crusade_active_ = std::move(fn); }
```

Wire in `application.cpp`:
```cpp
force_recall->set_crusade_check([crusade]() { return crusade->is_active(); });
```

### Step 2: Pause timer during crusade

In `tick_trackers()`:
```cpp
void force_recall_system::tick_trackers(float delta_time)
{
    bool crusade_active = is_crusade_active_ ? is_crusade_active_() : false;

    for (auto it = trackers_.begin(); it != trackers_.end(); )
    {
        auto& [pid, tracker] = *it;

        // Don't decrement during crusade
        if (crusade_active)
        {
            ++it;
            continue;
        }

        // Don't decrement if in own town
        if (tracker.in_own_town)
        {
            ++it;
            continue;
        }

        tracker.time_remaining_seconds -= delta_time;

        if (tracker.time_remaining_seconds <= 0)
        {
            execute_recall(pid);
            it = trackers_.erase(it);
        }
        else
        {
            // Periodic timer update to client
            maybe_send_timer_update(pid, tracker);
            ++it;
        }
    }
}
```

### Step 3: Add "in own town" flag

```cpp
struct recall_tracker
{
    float time_remaining_seconds{0};
    float last_update_sent{0};
    bool in_own_town{false};  // NEW — pauses countdown
};
```

When setting up a tracker, determine if the player is in their own town:
```cpp
void force_recall_system::check_player_territory(player_id pid,
    war_faction player_faction, war_faction map_faction)
{
    // ... existing logic ...

    // If in enemy territory during crusade, set own_town based on
    // whether the player is actually in the enemy's zone
    // (This is complex — in legacy, m_bIsInsideOwnTown is set based
    // on the map's faction matching the player's faction)
    // Simplification: if player_faction == map_faction, they're in own town
    tracker.in_own_town = (player_faction == map_faction);
}
```

### Step 4: Add instant building recall during crusade

Add a new method for the per-tick building check:

```cpp
void force_recall_system::check_building_recall(
    player_id pid, const std::string& map_name, war_faction player_faction)
{
    if (!is_crusade_active_ || !is_crusade_active_()) return;

    // Check if player is in enemy faction's building
    auto map_faction = get_map_faction(map_name);

    if (player_faction == war_faction::aresden && map_faction == war_faction::elvine)
    {
        // Aresden player in Elvine building → teleport to aresden
        if (execute_fn_) execute_fn_(pid);
    }
    else if (player_faction == war_faction::elvine && map_faction == war_faction::aresden)
    {
        // Elvine player in Aresden building → teleport to elvine
        if (execute_fn_) execute_fn_(pid);
    }
}

bool force_recall_system::is_building_map(const std::string& map_name) const
{
    static const std::unordered_set<std::string> building_maps = {
        "wrhus", "gshop_1", "bsmith_1", "cath_1", "CmdHall_1", "cityhall_1",
        "gshop_2", "bsmith_2", "cath_2", "CmdHall_2", "cityhall_2",
        "wzdtwr", "gldhall"
    };
    return building_maps.count(map_name) > 0;
}
```

This should be called from the game tick for each player, or from the map-enter handler.

### Step 5: Add crusade-end civilian timer clear

```cpp
void force_recall_system::clear_civilian_timers()
{
    // Called at crusade end
    // Remove trackers for civilian players (players with hunter location)
    // Since we may not know who's civilian, clear all war-location trackers
    // OR accept a predicate function:
    for (auto it = trackers_.begin(); it != trackers_.end(); )
    {
        // If the tracker was set during crusade, clear it
        if (it->second.set_during_crusade)
        {
            it = trackers_.erase(it);
        }
        else
        {
            ++it;
        }
    }
}
```

Add `set_during_crusade` flag to tracker:
```cpp
struct recall_tracker
{
    float time_remaining_seconds{0};
    float last_update_sent{0};
    bool in_own_town{false};
    bool set_during_crusade{false};  // NEW
};
```

Wire in crusade end:
```cpp
// In crusade_system::end_crusade() or cleanup:
if (force_recall_)
    force_recall_->clear_civilian_timers();
```

### Step 6: Add building debuff removal

```cpp
void force_recall_system::check_building_debuffs(
    player_id pid, const std::string& map_name, entity::entity eid)
{
    if (!is_building_map(map_name)) return;

    // Remove Confuse/Illusion effects
    if (effects_)
    {
        auto* effs = effects_->get_effects(eid);
        if (effs)
        {
            effects_->remove_effects_by_group(eid, magic_type::confuse);
            effects_->remove_effects_by_group(eid, magic_type::illusion);
        }
    }
}
```

Need to add `effect_system` pointer to `force_recall_system`.

### Step 7: Wire everything in application.cpp

```cpp
// Wire crusade check
force_recall_system->set_crusade_check([crusade]() { return crusade->is_active(); });

// Wire crusade end callback
crusade_system->set_on_end_callback([force_recall]() {
    force_recall->clear_civilian_timers();
});

// Wire effect system for debuff removal (if needed)
force_recall_system->set_effects(effects);
```

### Step 8: Update tests

1. `timer_paused_during_crusade` — set crusade active, verify timer doesn't decrement
2. `timer_resumes_after_crusade` — deactivate crusade, verify timer decrements again
3. `own_town_pauses_timer` — player in own faction territory doesn't count down
4. `building_recall_during_crusade` — enemy in faction building gets teleported
5. `building_recall_only_during_crusade` — no building recall outside crusade
6. `civilian_timers_cleared_at_crusade_end` — verify trackers removed
7. `building_debuff_removal` — confuse/illusion stripped in buildings

## Files to Modify
- `src/war/force_recall/force_recall_system.h` — add crusade awareness, building checks, tracker fields
- `src/war/force_recall/force_recall_system.cpp` — implement all changes
- `src/application.cpp` — wire crusade check and callbacks
- `tests/test_force_recall_system.cpp` (or test_apocalypse_system.cpp if tests are combined) — add tests

## Acceptance Criteria
- Force recall timer does not decrement during active crusade
- Players in their own faction's territory have paused timers
- Enemies in faction buildings are instantly recalled during crusade
- Civilian timers cleared at crusade end
- Confuse/Illusion stripped from players in buildings
- All tests pass
