# Plan 08: Heldenian Kill/Death Tiebreaker & Status Tracking

## Problems

1. **Kill/death tiebreaker missing** — legacy tower mode uses faction death counts as tiebreaker when tower counts are equal; new code returns draw
2. **No faction-level death counters** — new code tracks per-player kills/deaths but doesn't aggregate into faction totals
3. **Death counts not included in status broadcasts** — legacy sends tower counts AND death counts; new only sends tower counts
4. **Status broadcasts go to all clients** — legacy sends only to clients on the war map (BtField)
5. **No last-winner persistence across restarts** — legacy saves to GUID file, new has no mechanism to restore

## Legacy Behavior (Reference: `Game.cpp`)

### Faction death counters:
```cpp
// At line ~15478-15486, when a player dies on a Heldenian map:
if (m_pMapList[...]->m_bIsHeldenianMap == TRUE) {
    if (m_pClientList[iClientH]->m_cSide == 1)
        m_iHeldenianAresdenDead++;
    else if (m_pClientList[iClientH]->m_cSide == 2)
        m_iHeldenianElvineDead++;
    UpdateHeldenianStatus();
}
```

### Tiebreaker in tower defense (`LocalEndHeldenianMode` line 53724):
```cpp
// Mode 1 (tower defense) winner determination:
if (m_iHeldenianAresdenLeftTower > m_iHeldenianElvineLeftTower)
    winner = ARESDEN;
else if (m_iHeldenianElvineLeftTower > m_iHeldenianAresdenLeftTower)
    winner = ELVINE;
else {
    // Equal towers — use death count tiebreaker
    if (m_iHeldenianAresdenDead < m_iHeldenianElvineDead)
        winner = ARESDEN;  // Fewer Aresden deaths → Aresden wins
    else if (m_iHeldenianElvineDead < m_iHeldenianAresdenDead)
        winner = ELVINE;   // Fewer Elvine deaths → Elvine wins
    else
        winner = m_sLastHeldenianWinner;  // Complete tie → previous winner stays
}
```

### Status broadcast (`UpdateHeldenianStatus` line 54548):
```cpp
// Only sends to clients on Heldenian maps (BtField specifically)
for each map where m_bIsHeldenianMap == TRUE:
    for each NPC on that map (repurposing NPC list for client search):
        // Actually iterates m_pClientList checking m_cMapIndex
        SendNotifyMsg(NULL, i, DEF_NOTIFY_HELDENIANCOUNT,
            m_iHeldenianAresdenLeftTower,
            m_iHeldenianElvineLeftTower,
            m_iHeldenianAresdenDead,
            m_iHeldenianElvineDead);
```

### When status updates are triggered:
1. After tower/door destruction
2. After player death on Heldenian map
3. After a player enters the war zone
4. Periodic (UpdateHeldenianStatus called from timer)

### Last winner persistence (`_CreateHeldenianGUID` / `bReadHeldenianGUIDFile`):
```
File: GameData/HeldenianGUID.Txt
Contents:
    HeldenianGUID = <value>
    winner-side = <value>

Read on server startup → m_sLastHeldenianWinner
Written at war end with winner side
```

## Current New Code

- `heldenian_system::record_kill()` — tracks per-player kills/deaths in `heldenian_player_data`
- `broadcast_status_update()` — sends to ALL clients, only includes surviving tower counts
- Tower defense time-limit: equal towers → `war_faction::neutral` (draw)

## Implementation Plan

### Step 1: Add faction-level death counters

In `heldenian_system`:
```cpp
int32_t aresden_deaths_{0};
int32_t elvine_deaths_{0};
```

Update `record_kill()` to also increment faction counters:
```cpp
void heldenian_system::record_kill(player_id killer, player_id victim,
                                    war_faction victim_faction)
{
    // Existing per-player tracking...
    auto* victim_data = get_player_data(victim);
    if (victim_data) victim_data->deaths++;

    auto* killer_data = get_player_data(killer);
    if (killer_data) killer_data->kills++;

    // NEW: faction-level death counters
    if (victim_faction == war_faction::aresden)
        aresden_deaths_++;
    else if (victim_faction == war_faction::elvine)
        elvine_deaths_++;

    // Broadcast status update after death
    broadcast_status_update();
}
```

Reset counters in `start_heldenian()`:
```cpp
aresden_deaths_ = 0;
elvine_deaths_ = 0;
```

### Step 2: Implement kill/death tiebreaker

In the `update()` timer-expiry handler for tower defense:

```cpp
// BEFORE:
if (aresden_surviving > elvine_surviving)
    winner = war_faction::aresden;
else if (elvine_surviving > aresden_surviving)
    winner = war_faction::elvine;
else
    winner = war_faction::neutral;  // draw

// AFTER (with tiebreaker):
if (aresden_surviving > elvine_surviving)
    winner = war_faction::aresden;
else if (elvine_surviving > aresden_surviving)
    winner = war_faction::elvine;
else
{
    // Equal towers — use death count tiebreaker
    if (aresden_deaths_ < elvine_deaths_)
        winner = war_faction::aresden;   // Fewer deaths wins
    else if (elvine_deaths_ < aresden_deaths_)
        winner = war_faction::elvine;
    else
        winner = last_winner_;  // Complete tie → previous winner stays
}
```

### Step 3: Include death counts in status broadcasts

Update `broadcast_status_update()`:
```cpp
void heldenian_system::broadcast_status_update()
{
    nlohmann::json data;
    data["active"] = active_;
    data["mode"] = static_cast<int>(mode_);
    data["elapsed_s"] = elapsed_seconds_;
    data["aresden_surviving"] = count_surviving(war_faction::aresden);
    data["elvine_surviving"] = count_surviving(war_faction::elvine);
    data["aresden_deaths"] = aresden_deaths_;   // NEW
    data["elvine_deaths"] = elvine_deaths_;       // NEW

    // ...send message...
}
```

### Step 4: Restrict status broadcasts to war-zone players

Instead of broadcasting to ALL clients, send only to players on the Heldenian map:

```cpp
void heldenian_system::broadcast_status_update()
{
    // ... build message ...

    std::string war_map = (mode_ == heldenian_mode::tower_defense)
        ? config_.tower_map : config_.door_map;

    // Send to players on the war map only
    if (players_)
    {
        players_->for_each_player([&](player_id pid, player::player& plr) {
            if (plr.current_map == war_map)
            {
                send_to_player(pid, msg);
            }
        });
    }
}
```

Keep `broadcast_objective_update()` going to all participants (or also restrict to war map).

### Step 5: Add last-winner persistence

Option A: Use `war_persistence` to save/load last winner:

```cpp
// At war end, after determining winner:
if (persistence_)
{
    persistence_->save_heldenian_last_winner(last_winner_);
}

// At system initialization:
if (persistence_)
{
    last_winner_ = persistence_->load_heldenian_last_winner();
}
```

Add methods to `war_persistence`:
```cpp
void save_heldenian_last_winner(war_faction winner);
auto load_heldenian_last_winner() -> war_faction;
```

This can use the existing `war_history` table — query the most recent heldenian war result to determine the last winner. Or add a simple `server_state` table/key-value store.

Option B: Simpler approach — use a config file:
```cpp
// Save to a file (like legacy GUID file):
void save_last_winner(war_faction winner)
{
    std::ofstream f("GameData/heldenian_state.txt");
    f << "last_winner=" << static_cast<int>(winner) << "\n";
}

auto load_last_winner() -> war_faction
{
    std::ifstream f("GameData/heldenian_state.txt");
    // Parse and return
}
```

Option A is preferred since the project uses PostgreSQL.

### Step 6: Trigger status update on key events

Add `broadcast_status_update()` calls to:
1. `on_npc_killed()` / `damage_objective()` — when a tower/door is destroyed
2. `record_kill()` — when a player dies (already added in Step 1)
3. When a player enters the war zone (if there's a join/enter handler)

### Step 7: Provide public accessors for death counts

```cpp
int32_t aresden_deaths() const { return aresden_deaths_; }
int32_t elvine_deaths() const { return elvine_deaths_; }
```

### Step 8: Update tests

1. `tiebreaker_fewer_deaths_wins` — equal towers, aresden has 5 deaths, elvine has 8 → aresden wins
2. `tiebreaker_complete_tie_previous_winner` — equal towers, equal deaths → previous winner stays
3. `faction_deaths_increment_on_kill` — verify `record_kill` increments faction counters
4. `status_includes_death_counts` — verify JSON includes aresden_deaths/elvine_deaths
5. `status_only_sent_to_war_map_players` — players on other maps don't receive status
6. `last_winner_persists_across_wars` — start war, end with aresden winning, start new war → defending faction is aresden
7. `death_counters_reset_on_start` — new war starts with 0 deaths

## Files to Modify
- `src/war/heldenian/heldenian_system.h` — add death counters, persistence methods
- `src/war/heldenian/heldenian_system.cpp` — implement tiebreaker, death tracking, broadcast changes
- `src/war/heldenian/heldenian_types.h` — (if needed for config/state)
- `src/war/war_persistence.h` — add heldenian last winner methods
- `src/war/war_persistence.cpp` — implement persistence
- `tests/test_heldenian_system.cpp` — add tiebreaker and tracking tests
- `docs/JSON_PROTOCOL.md` — update heldenian_status_update fields

## Acceptance Criteria
- Equal towers → fewer deaths wins; equal deaths → previous winner stays
- Faction death counters increment on player death
- Status broadcasts include death counts
- Status broadcasts only go to war-zone players
- Last winner persists across server restarts (via DB)
- Death counters reset at war start
- All tests pass
