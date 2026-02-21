# Plan 10: Force Recall System Completeness

## Problems

1. **Friday raid time mismatch** — legacy default 10 min (600s), new has 15 min (900s)
2. **No day-transition timer adjustment** — legacy resets timer to 1 tick if a new shorter day starts
3. **No fight zone handling** — legacy has special fight zone recall timing
4. **No jail timer special case** — legacy jails have fixed 5-minute timer
5. **No `CheckForceRecallTime` equivalent** — separate function that clamps to new day time

## Legacy Behavior (Reference: `Game.cpp`)

### Friday raid time (`SetForceRecallTime` line 46400):
```cpp
// Default if config value is 0:
case 5: iTL_ = 20 * 10; break;  // Friday: 10 minutes = 200 ticks = 600 seconds
// Comments say "15 min" but actual code is 10
```

### Day-transition timer adjustment (`SetForceRecallTime` line 46400):
```cpp
if (m_iTimeLeft_ForceRecall != 0) {
    // Player already has a timer set
    // Calculate what the current day's time would be
    int iTL_ = 20 * get_day_raid_time(current_day);

    if (m_sForceRecallTime > 0)
        iTL_ = 20 * m_sForceRecallTime;

    if (m_iTimeLeft_ForceRecall > iTL_) {
        // Player's existing timer exceeds current day's limit
        // Reset to 1 tick (will recall next tick)
        m_iTimeLeft_ForceRecall = 1;
    }
}
```

This handles the case where a player entered on Friday (10 min timer) and is still there on Saturday (45 min) — no adjustment needed since they have less time. But if they entered on Sunday (60 min) and it's now Monday (3 min), their 60-min timer gets reset to 1 tick.

### Fight zone handling (`SetForceRecallTime` line ~46430):
```cpp
if (m_iFightzoneNoForceRecall == 0) {
    // Fight zone players DO get recall timer
    // Special calculation: time until next even hour minus 2 minutes
    GetLocalTime(&SysTime);
    iTL_ = 2*60*20 - ((SysTime.wHour%2)*20*60 + SysTime.wMinute*20) - 2*20;
    // This = (120 min - current_minute_in_2hr_cycle - 2 min) * 20 ticks/min
    // Effectively: recall at the next even hour minus 2 minutes
    m_iTimeLeft_ForceRecall = iTL_;
}
// m_iFightzoneNoForceRecall == 1: exempt from recall entirely
```

### Jail timer (in `IsEnemyZone` equivalent logic):
```cpp
// Maps "arejail" and "elvjail" get a fixed 5-minute timer:
if (map == jail):
    m_iTimeLeft_ForceRecall = 20 * 5;  // 100 ticks = 300 seconds
```

### `CheckForceRecallTime` (line 46457):
Separate from `SetForceRecallTime`. Called in different context (possibly on map transition):
```cpp
if (m_iTimeLeft_ForceRecall > iTL_) {
    m_iTimeLeft_ForceRecall = iTL_;  // Clamp to current day (not reset to 1)
}
```
Difference: `SetForceRecallTime` resets to 1 (immediate recall), `CheckForceRecallTime` clamps to new max.

## Current New Code

File: `src/war/force_recall/force_recall_system.h/.cpp`

```cpp
// get_default_raid_times():
{0, 3600},   // Sunday: 3600s (60 min) — correct
{1, 180},    // Monday: 180s (3 min) — correct
{2, 180},    // Tuesday — correct
{3, 180},    // Wednesday — correct
{4, 180},    // Thursday — correct
{5, 900},    // Friday: 900s (15 min) — WRONG, should be 600s (10 min)
{6, 2700},   // Saturday: 2700s (45 min) — correct
```

## Implementation Plan

### Step 1: Fix Friday raid time

```cpp
// In get_default_raid_times() or wherever defaults are set:
{5, 600},    // Friday: 600s (10 min) — matches legacy code (not comments)
```

### Step 2: Add day-transition timer adjustment

Add a method called periodically (e.g., every 30 seconds) or on day change:

```cpp
void force_recall_system::check_day_transitions()
{
    auto current_duration = get_current_raid_duration();

    for (auto& [pid, tracker] : trackers_)
    {
        if (tracker.time_remaining_seconds > current_duration)
        {
            // Player's timer exceeds current day's limit
            // Reset to near-zero (will recall soon)
            tracker.time_remaining_seconds = 3.0f;  // ~1 tick in legacy (3 seconds)
            LOG_DEBUG(general, "Force recall day transition: player {} timer reset", pid.value);
        }
    }
}
```

Call this from `update()` or on a periodic timer. Can also store `last_checked_day` and only run when the day changes:

```cpp
void force_recall_system::update(float delta_time)
{
    // Check day transitions
    auto now = std::chrono::system_clock::now();
    auto time_t = std::chrono::system_clock::to_time_t(now);
    auto* tm = std::localtime(&time_t);
    uint8_t current_day = tm->tm_wday;

    if (current_day != last_checked_day_)
    {
        last_checked_day_ = current_day;
        check_day_transitions();
    }

    tick_trackers(delta_time);
}
```

### Step 3: Add fight zone handling

```cpp
struct force_recall_config
{
    // ... existing fields ...
    bool fight_zone_recall_enabled{true};  // m_iFightzoneNoForceRecall == 0
};

// Special timer for fight zones:
float force_recall_system::calculate_fight_zone_duration() const
{
    auto now = std::chrono::system_clock::now();
    auto time_t = std::chrono::system_clock::to_time_t(now);
    auto* tm = std::localtime(&time_t);

    // Time until next even hour, minus 2 minutes
    int minutes_in_cycle = (tm->tm_hour % 2) * 60 + tm->tm_min;
    int total_cycle = 120;  // 2 hours
    int remaining = total_cycle - minutes_in_cycle - 2;  // minus 2 minutes

    if (remaining <= 0) remaining = 1;  // minimum 1 minute

    return static_cast<float>(remaining * 60);  // convert to seconds
}
```

In `check_player_territory()`, add a `is_fight_zone` parameter:
```cpp
void force_recall_system::check_player_territory(
    player_id pid, war_faction player_faction, war_faction map_faction,
    bool is_fight_zone = false)
{
    // ... existing logic ...

    if (is_fight_zone)
    {
        if (!config_.fight_zone_recall_enabled)
            return;  // Exempt

        float duration = calculate_fight_zone_duration();
        trackers_[pid] = recall_tracker{duration};
        return;
    }

    // ... normal territory logic ...
}
```

### Step 4: Add jail timer special case

```cpp
void force_recall_system::check_player_territory(
    player_id pid, war_faction player_faction, war_faction map_faction,
    bool is_fight_zone = false, bool is_jail = false)
{
    // ... existing logic ...

    if (is_jail)
    {
        // Fixed 5-minute timer for jails
        trackers_[pid] = recall_tracker{300.0f};  // 5 * 60 = 300 seconds
        return;
    }

    // ... rest of logic ...
}
```

### Step 5: Add admin toggle for fight zone recall

```cpp
void force_recall_system::set_fight_zone_recall(bool enabled)
{
    config_.fight_zone_recall_enabled = enabled;
    LOG_INFO(general, "Fight zone recall {}", enabled ? "enabled" : "disabled");
}
```

### Step 6: Update tests

1. `friday_raid_time_600_seconds` — verify Friday default is 600s, not 900s
2. `day_transition_resets_long_timer` — Sunday timer (3600s) → Monday: reset to ~3s
3. `day_transition_keeps_short_timer` — Monday timer (180s) → Sunday: no change (shorter)
4. `fight_zone_timer_until_even_hour` — at 13:30, timer should be ~28 minutes
5. `fight_zone_exempt_when_disabled` — no timer set when `fight_zone_recall_enabled = false`
6. `jail_fixed_5_minute_timer` — jail maps always get 300s regardless of day
7. `check_day_transition_only_runs_on_day_change` — verify not running every tick

## Files to Modify
- `src/war/force_recall/force_recall_system.h` — add new methods, config fields
- `src/war/force_recall/force_recall_system.cpp` — implement all changes
- `tests/test_force_recall_system.cpp` (or equivalent) — add tests

## Acceptance Criteria
- Friday default raid time is 600 seconds (10 minutes)
- Day transitions properly clamp or reset timers
- Fight zone has special even-hour-based timer calculation
- Fight zone recall can be toggled by admin
- Jail maps use fixed 5-minute timer
- All tests pass
