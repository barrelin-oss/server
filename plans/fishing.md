# Fishing System Implementation Plan

## Overview
Fishing allows players to catch fish from water tiles using a fishing rod. Two-phase mechanic: cast line (wait for bite), then reel in (reaction-based). Uses `tile_flags::is_water` for valid locations and the scheduler for timing.

## Architecture

### 1. Fishing Config (`src/crafting/fishing_config.h`)

```cpp
struct fish_entry
{
    std::string item_name;
    int32_t template_id{};  // resolved at load
    int16_t min_skill{};    // minimum skill to catch
    int32_t weight{100};    // relative catch weight
    bool night_only{false}; // only available at night
};

struct fishing_zone
{
    std::string name;                   // e.g., "Shallow Waters", "Deep Lake"
    int16_t skill_limit{};              // exp cap for this zone
    duration_ms min_bite_time{5000};    // minimum wait for bite
    duration_ms max_bite_time{30000};   // maximum wait for bite
    duration_ms reel_window{3000};      // time to react after bite
    std::vector<fish_entry> fish;
};

// Maps map_name -> zone name (default zone if not specified)
struct fishing_zone_assignment
{
    std::string map_name;
    std::string zone_name;
};
```

### 2. Fishing Registry (`src/registry/fishing_registry.h/.cpp`)
Follow `loot_registry` pattern:
- `load_from_file(path)` parses new `fishing.yaml`
- Two sections: `zones` and `zone_assignments`
- Resolves item names to template IDs via `item_registry`
- Getters: `get_zone(name)`, `get_zone_for_map(map_name)`, `count()`
- Default zone fallback if map has no specific assignment

### 3. Fishing YAML Config (`bin/game_configs/fishing.yaml`)

```yaml
zones:
  shallow:
    name: "Shallow Waters"
    skill_limit: 30
    min_bite_time: 3000
    max_bite_time: 15000
    reel_window: 4000
    fish:
      - item: "Small Fish"
        min_skill: 0
        weight: 100
      - item: "Trout"
        min_skill: 10
        weight: 60
      - item: "Salmon"
        min_skill: 20
        weight: 30
        night_only: true

  deep:
    name: "Deep Waters"
    skill_limit: 80
    min_bite_time: 5000
    max_bite_time: 30000
    reel_window: 2500
    fish:
      - item: "Large Fish"
        min_skill: 30
        weight: 80
      - item: "Rare Fish"
        min_skill: 50
        weight: 20

zone_assignments:
  - map: "default"
    zone: "shallow"
```

Actual fish items and zones populated from legacy data.

### 4. Fishing System (`src/crafting/fishing_system.h/.cpp`)

```cpp
enum class fishing_state : uint8_t
{
    idle = 0,
    casting = 1,       // line in water, waiting for bite
    bite = 2,          // fish biting, waiting for player to reel
};

struct active_fishing_session
{
    entity_id player{};
    fishing_state state{fishing_state::idle};
    std::string map_name;
    int16_t x{};
    int16_t y{};
    const fishing_zone* zone{};
    scheduler::task_id bite_timer{};
    scheduler::task_id reel_timeout{};
};

class fishing_system : public subsystem
{
public:
    void set_dependencies(skill_system*, inventory_system*, item_system*,
                          fishing_registry*, scheduler*, player_system*, world_subsystem*);

    auto cast_line(entity_id player, int16_t dir_x, int16_t dir_y,
                   const std::string& map_name) -> cast_result;
    auto reel_in(entity_id player) -> reel_result;
    void cancel_fishing(entity_id player);

    auto is_fishing(entity_id player) -> bool;
    auto get_state(entity_id player) -> fishing_state;

private:
    void on_fish_bite(entity_id player);
    void on_reel_timeout(entity_id player);
    auto roll_catch(const fishing_zone& zone, int16_t skill_level, bool is_night) -> const fish_entry*;
    auto calculate_catch_chance(int16_t skill_level, const fish_entry& fish) -> int32_t;

    std::unordered_map<uint32_t, active_fishing_session> sessions_;
};
```

Result structs:
```cpp
struct cast_result
{
    bool started{};
    skill_use_result reason{};  // invalid_target (not water), insufficient_materials (no rod)
};

struct reel_result
{
    bool caught{};
    std::string item_name;
    item_id caught_item{};
    int32_t exp_gained{};
    skill_use_result reason{};  // success, failure (fish got away)
};
```

Fishing flow:
1. Player sends `cast_line_request` with direction
2. Server validates: target tile is water, player has fishing rod (template check), not already fishing
3. Determine fishing zone for current map
4. `schedule_tagged(random_bite_time, player_tag, on_fish_bite)` — random between min/max
5. Fish bites: send `fish_bite_notification`, start `reel_timeout` timer
6. Player sends `reel_request` within window:
   - Roll catch chance based on skill + fish difficulty
   - Success: create fish item, grant exp, send `fish_result(caught=true)`
   - Fail: send `fish_result(caught=false, reason="The fish got away")`
7. Timeout expires: fish escapes, send `fish_result(caught=false)`
8. Player moves: cancel fishing session

Catch chance:
- Base: 60%
- Skill bonus: `+(skill - fish.min_skill) * 1.5` (capped at +30)
- DEX bonus: `+dex / 4`
- Final clamped to 20-95%

### 5. Protocol Messages (5 messages)

| Message | Direction | Fields |
|---------|-----------|--------|
| `cast_line_request` | C→S | `dir_x`, `dir_y` (direction to cast) |
| `cast_line_response` | S→C | `success`, `reason` |
| `fish_bite` | S→C | (none — notification that fish is biting) |
| `reel_request` | C→S | (none — player reacts to bite) |
| `fish_result` | S→C | `caught` (bool), `item_name`, `item_id`, `exp_gained` |

### 6. Handler Integration
Add to `game_handlers.cpp`:
- `handle_cast_line_request` — validates and starts fishing
- `handle_reel_request` — processes reel attempt
- Movement handler should cancel active fishing sessions

### 7. Fishing Rod Check
Requires fishing rod item in inventory or equipped. Define rod template IDs as constants. Check via `inventory_system`.

### 8. Game Clock Integration
Use `game_clock::is_night()` to filter `night_only` fish entries from the drop pool.

### 9. Wiring (`application.cpp`)
- Create `fishing_registry` subsystem, load `fishing.yaml`
- Create `fishing_system`, wire dependencies (including `world_subsystem` for tile checks)
- Add `fishing_system*` to `game_handlers`
- Register handler callbacks

### 10. Tests (`tests/fishing_test.cpp`)
- Fishing registry: YAML loading, zone parsing, map assignments
- Cast line: valid water tile, no water, no rod, already fishing
- Fish bite: timer fires, notification sent
- Reel in: within window (success/fail roll), after timeout (auto-fail)
- Cancel: movement cancels session, timer cleanup
- Catch chance: skill scaling, DEX bonus, clamping
- Night fish: filtered by game clock
- Drop rolling: weight-based selection, min_skill gating

## Files to Create
1. `src/crafting/fishing_config.h`
2. `src/registry/fishing_registry.h`
3. `src/registry/fishing_registry.cpp`
4. `src/crafting/fishing_system.h`
5. `src/crafting/fishing_system.cpp`
6. `bin/game_configs/fishing.yaml`
7. `tests/fishing_test.cpp`

## Files to Modify
1. `src/network/json_protocol.h` — 5 new message types
2. `src/network/json_protocol.cpp` — type_map + to_string
3. `src/bridge/handlers/game_handlers.h` — fishing_system* member, 2 handler methods
4. `src/bridge/handlers/game_handlers.cpp` — handler implementations + registrations
5. `src/application.cpp` — subsystem creation and wiring
