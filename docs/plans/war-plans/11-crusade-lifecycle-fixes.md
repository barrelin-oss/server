# Plan 11: Crusade Lifecycle & Scheduling Fixes

## Problems

1. **Duplicate-in-day prevention missing** — legacy prevents two crusades on the same calendar day
2. **Victory check is immediate** — legacy delays until 6s post-meteor (full sequence completion); new checks instantly on strike point destruction
3. **No offline player reward processing** — legacy processes rewards at login time; new only processes online players
4. **No last winner persistence across restarts** — legacy saves to GUID file; new stores in memory only
5. **Map disabling on strike point destruction missing** — legacy disables the linked map

## Legacy Behavior (Reference: `Game.cpp`)

### Duplicate-in-day prevention (`GlobalStartCrusadeMode` line 45858):
```cpp
if (m_iLatestCrusadeDayOfWeek == SysTime.wDayOfWeek) {
    // Already had a crusade today — don't start another
    return;
}
if (m_iLatestCrusadeDayOfWeek == -1) {
    m_iLatestCrusadeDayOfWeek = SysTime.wDayOfWeek;
}
```

### Delayed victory check (`CalcMeteorStrikeEffectHandler` line 48540):
Called 6 seconds after meteor impact (the "result" phase):
```cpp
// Count active structures
int iActiveStructure = 0;
for (i = 1; i <= m_iTotalStrikePoints; i++) {
    if (strikePoint[i].iHP > 0) iActiveStructure++;
}

if (iActiveStructure == 0) {
    // ALL structures destroyed — determine winner and end crusade
    if (target_was_aresden) winner = ELVINE;
    else winner = ARESDEN;
    LocalEndCrusadeMode(winner);
}
```

This means victory is ONLY checked after the full meteor sequence completes (warning + damage waves + result = ~11 seconds). In the new code, victory is checked immediately in `damage_strike_point()`, which could cut off subsequent player damage waves.

### GUID file for last winner (`_CreateCrusadeGUID` / `bReadCrusadeGUIDFile`):
```
File: GameData/CrusadeGUID.Txt
Contents:
    crusade-GUID = <value>
    winner-side = <value>

Read on server startup → m_iLastCrusadeWinner
Written at crusade end with winner side
```
`m_iLastCrusadeWinner` is used by `SelectCrusadeDutyHandler` for the commander bonus and by login flow for deferred rewards.

### Map disabling on strike point destruction (`MeteorStrikeHandler` line 46189):
```cpp
if (strikePoint.iHP <= 0) {
    strikePoint.iHP = 0;
    m_pMapList[strikePoint.iMapIndex]->m_bIsDisabled = TRUE;
    // Disabled maps: no spawning, no entry, no combat
}
```

### Deferred reward processing:
See Plan 04 for the full reward formula. The key point is:
- At crusade end: player data (duty, contribution, GUID) is preserved on the player
- At login: `CheckCrusadeResultCalculation` processes rewards and clears the data
- This ensures offline players get their rewards when they log back in

## Current New Code

- No duplicate-in-day check
- `damage_strike_point()` calls `check_victory_condition()` immediately
- `last_crusade_winner_` stored in memory only, lost on restart
- No map disabling
- All rewards processed at `end_crusade()` time — offline players get nothing

## Implementation Plan

### Step 1: Add duplicate-in-day prevention

```cpp
// In crusade_system:
int8_t last_crusade_day_{-1};  // Day of week of last crusade start (-1 = none)

auto crusade_system::check_schedule() -> bool
{
    // ... existing time matching ...

    if (matched)
    {
        // Prevent two crusades on the same day
        auto now = std::chrono::system_clock::now();
        auto time_t = std::chrono::system_clock::to_time_t(now);
        auto* tm = std::localtime(&time_t);

        if (last_crusade_day_ == tm->tm_wday)
        {
            LOG_DEBUG(general, "Skipping crusade — already had one today (day {})", tm->tm_wday);
            return false;
        }

        last_crusade_day_ = tm->tm_wday;
        return true;
    }
    return false;
}
```

### Step 2: Defer victory check to result phase

The meteor sequence has 3 phases:
1. Warning (t=0)
2. Impact + player damage waves (t=5000ms, +1s, +4s)
3. Result (t=5000ms + 6000ms = 11000ms)

Victory should only be checked in the result phase. Modify `damage_strike_point()`:

```cpp
void crusade_system::damage_strike_point(uint16_t id, int32_t damage)
{
    // Apply damage
    auto& sp = find_strike_point(id);
    sp.hp = std::max(0, sp.hp - damage);

    // Do NOT check victory here — defer to meteor result phase
    // check_victory_condition();  // REMOVE THIS
}
```

Move the victory check to the meteor result callback:

```cpp
// In setup_meteor_callbacks(), the result callback:
cbs.broadcast_result = [this](const meteor_event_result& result) {
    broadcast_meteor_result(result);

    // Check victory AFTER the full meteor sequence completes
    check_victory_condition();
};
```

### Step 3: Add last winner persistence

Option A (preferred — use DB):
```cpp
// In war_persistence:
void save_crusade_last_winner(war_faction winner);
auto load_crusade_last_winner() -> war_faction;

// Implementation uses the war_history table:
// SELECT winner_faction FROM war_history
// WHERE war_type = 'crusade' ORDER BY ended_at DESC LIMIT 1
```

Option B (file-based, like legacy):
```cpp
void crusade_system::save_last_winner()
{
    std::ofstream f("GameData/crusade_state.txt");
    f << "last_winner=" << static_cast<int>(last_crusade_winner_) << "\n";
}

void crusade_system::load_last_winner()
{
    std::ifstream f("GameData/crusade_state.txt");
    // Parse and set last_crusade_winner_
}
```

Call `load_last_winner()` during system initialization, `save_last_winner()` at crusade end.

### Step 4: Add map disabling on strike point destruction

This requires the `world_subsystem` to support map disabling:

```cpp
// Check if world_subsystem has a disable mechanism:
// world_->disable_map(map_name);

// In damage_strike_point(), when a point is destroyed:
if (sp.hp <= 0 && !sp.linked_map.empty() && world_)
{
    world_->set_map_disabled(sp.linked_map, true);
}
```

Add `linked_map` to strike point config:
```cpp
struct strike_point
{
    // ... existing fields ...
    std::string linked_map;  // Map disabled when this point is destroyed
};
```

At crusade end, re-enable all maps:
```cpp
void crusade_system::cleanup_crusade()
{
    // Re-enable disabled maps
    for (const auto& sp : aresden_strike_points_)
    {
        if (!sp.linked_map.empty() && world_)
            world_->set_map_disabled(sp.linked_map, false);
    }
    for (const auto& sp : elvine_strike_points_)
    {
        if (!sp.linked_map.empty() && world_)
            world_->set_map_disabled(sp.linked_map, false);
    }
    // ... rest of cleanup ...
}
```

Check if `world_subsystem` has `set_map_disabled()`. If not, it needs to be added — a boolean flag on the map that prevents entry and spawning.

### Step 5: Store offline player reward data

At `end_crusade()`, for participants who are NOT online, store their reward data:

```cpp
struct pending_crusade_reward
{
    uint32_t crusade_guid{0};
    war_faction winner{war_faction::neutral};
    war_faction player_faction{war_faction::neutral};
    int32_t war_contribution{0};
    int32_t crusade_duty{0};
};

// In crusade_system:
std::unordered_map<std::string, pending_crusade_reward> pending_rewards_;
// Keyed by character name

void crusade_system::end_crusade()
{
    // For each participant:
    for (auto& [pid, data] : player_data_)
    {
        auto* plr = players_ ? players_->get_player(pid) : nullptr;
        if (plr)
        {
            // Online — process immediately (existing logic)
            auto reward = calculate_crusade_reward(data, plr->experience.level, last_crusade_winner_);
            // ... send and apply ...
        }
        else
        {
            // Offline — store for later
            // Need character name from the player data or a mapping
            pending_rewards_[data.character_name] = {
                current_guid_,
                last_crusade_winner_,
                data.faction,
                data.war_contribution,
                static_cast<int32_t>(data.duty)
            };
        }
    }
}

// Called during login (from auth_handlers):
auto crusade_system::check_pending_reward(const std::string& char_name, int32_t level)
    -> std::optional<crusade_reward>
{
    auto it = pending_rewards_.find(char_name);
    if (it == pending_rewards_.end()) return std::nullopt;

    crusade_player_data fake_data;
    fake_data.war_contribution = it->second.war_contribution;
    fake_data.faction = it->second.player_faction;

    auto reward = calculate_crusade_reward(fake_data, level, it->second.winner);
    pending_rewards_.erase(it);
    return reward;
}
```

Optionally persist `pending_rewards_` to DB so they survive server restarts.

### Step 6: Update tests

1. `duplicate_in_day_prevention` — start and end crusade, try to start another on same day → rejected
2. `victory_deferred_to_result_phase` — destroy all strike points during meteor → crusade doesn't end until result callback
3. `last_winner_persisted` — end crusade with winner, load → `last_crusade_winner_` restored
4. `map_disabled_on_strike_point_destruction` — verify map flagged as disabled
5. `maps_reenabled_on_cleanup` — verify all maps re-enabled after crusade
6. `offline_player_reward_stored` — offline participant has pending reward
7. `offline_player_reward_on_login` — `check_pending_reward` returns correct reward and clears it

## Files to Modify
- `src/war/crusade/crusade_system.h` — add day tracking, pending rewards, map disable
- `src/war/crusade/crusade_system.cpp` — implement all changes
- `src/war/crusade/crusade_types.h` — add `linked_map` to strike_point, pending reward struct
- `src/war/war_persistence.h/.cpp` — add last winner load/save
- `src/world/world_subsystem.h/.cpp` — add `set_map_disabled()` if missing
- `tests/test_crusade_system.cpp` — add lifecycle tests
- `bin/game_configs/crusade.yaml` — add linked_map to strike points

## Acceptance Criteria
- Cannot start two crusades on the same calendar day
- Victory is determined 6s after meteor impact, not immediately
- Last crusade winner persists across restarts
- Strike point destruction disables linked map
- Disabled maps re-enabled at crusade end
- Offline players can collect rewards at login
- All tests pass
