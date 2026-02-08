# Mining System Implementation Plan

## Overview
Mining allows players to gather ores and gems from mineral nodes placed on maps. Uses timed interaction via the scheduler. `dynamic_object_type::mineral1/mineral2` already exist in enums.

## Architecture

### 1. Mining Config (`src/crafting/mining_config.h`)

```cpp
struct mineral_drop
{
    std::string item_name;
    int32_t template_id{};  // resolved at load
    int16_t min_skill{};    // minimum skill to find this
    int32_t weight{100};    // relative drop weight
    int32_t min_count{1};
    int32_t max_count{1};
};

struct mineral_node_config
{
    std::string name;                   // e.g., "Iron Vein", "Gold Deposit"
    int16_t min_skill{};                // minimum skill to mine
    int16_t skill_limit{};              // exp cap
    duration_ms mine_time{3000};        // time per swing
    int32_t max_hits{5};                // hits before node depletes
    duration_ms respawn_time{60000};    // respawn timer
    std::vector<mineral_drop> drops;
};

struct mineral_node_spawn
{
    std::string node_type;  // references mineral_node_config
    std::string map_name;
    int16_t x{};
    int16_t y{};
};
```

### 2. Mining Registry (`src/registry/mining_registry.h/.cpp`)
Follow `loot_registry` pattern:
- `load_from_file(path)` parses new `mining_nodes.yaml`
- Two sections: `node_types` (configs) and `spawns` (placements)
- Resolves item names to template IDs via `item_registry`
- Getters: `get_node_type(name)`, `get_spawns_for_map(map_name)`, `count()`

### 3. Mining YAML Config (`bin/game_configs/mining_nodes.yaml`)

```yaml
node_types:
  iron_vein:
    name: "Iron Vein"
    min_skill: 0
    skill_limit: 30
    mine_time: 3000
    max_hits: 5
    respawn_time: 60000
    drops:
      - item: "Iron Ore"
        min_skill: 0
        weight: 100
      - item: "Coal"
        min_skill: 0
        weight: 50
  # ... more node types

spawns:
  - node_type: iron_vein
    map: "default"
    x: 100
    y: 200
  # ... more spawns
```

Actual node types and spawn positions will be populated based on legacy game data.

### 4. Mining System (`src/crafting/mining_system.h/.cpp`)

```cpp
struct active_mining_session
{
    entity_id player{};
    std::string node_id;        // map:x:y key
    int32_t hits_remaining{};
    scheduler::task_id timer{};
};

struct mineral_node_state
{
    const mineral_node_config* config{};
    int32_t hits_remaining{};
    bool depleted{false};
    scheduler::task_id respawn_timer{};
};

class mining_system : public subsystem
{
public:
    void set_dependencies(skill_system*, inventory_system*, item_system*,
                          mining_registry*, scheduler*, player_system*);

    void spawn_nodes_for_map(const std::string& map_name);

    auto start_mining(entity_id player, int16_t target_x, int16_t target_y,
                      const std::string& map_name) -> mine_start_result;
    void cancel_mining(entity_id player);

    auto is_mining(entity_id player) -> bool;

private:
    void on_mine_tick(entity_id player);
    void on_node_depleted(const std::string& node_id);
    void respawn_node(const std::string& node_id);
    auto roll_drop(const mineral_node_config& config, int16_t skill_level) -> const mineral_drop*;

    std::unordered_map<std::string, mineral_node_state> nodes_;        // "map:x:y" -> state
    std::unordered_map<uint32_t, active_mining_session> sessions_;     // player entity -> session
};
```

`mine_start_result`:
```cpp
struct mine_start_result
{
    bool started{};
    skill_use_result reason{};  // insufficient_skill, invalid_target
};
```

Mining flow:
1. Player sends `mine_start_request` near a mineral node
2. Server validates: adjacent to node, not depleted, skill >= min_skill, has pickaxe (template check)
3. `schedule_repeating_tagged(mine_time, player_tag, on_mine_tick)`
4. Each tick: roll for ore drop, grant item + exp, decrement node hits
5. Node depleted: cancel session, schedule respawn
6. Player moves/cancels: cancel timer

### 5. Protocol Messages (4 messages)

| Message | Direction | Fields |
|---------|-----------|--------|
| `mine_start_request` | C→S | `target_x`, `target_y` |
| `mine_start_response` | S→C | `success`, `reason`, `node_name` |
| `mine_result` | S→C | `success` (bool), `item_name`, `item_id`, `count`, `exp_gained`, `node_depleted` |
| `mine_cancel` | C→S | (none — player moves or explicitly cancels) |

### 6. Handler Integration
Add to `game_handlers.cpp`:
- `handle_mine_start_request` — calls `start_mining()`
- `handle_mine_cancel` — calls `cancel_mining()`
- Movement handler should also cancel active mining sessions

### 7. Pickaxe Check
Mining requires a pickaxe item (by template_id or item category). Check via `inventory_system::has_item()` or equipment check. Define pickaxe template IDs as constants.

### 8. Wiring (`application.cpp`)
- Create `mining_registry` subsystem, load `mining_nodes.yaml`
- Create `mining_system`, wire dependencies
- On map load, call `spawn_nodes_for_map()` for each loaded map
- Add `mining_system*` to `game_handlers`
- Register handler callbacks

### 9. Tests (`tests/mining_test.cpp`)
- Mining registry: YAML loading, node type parsing, spawn parsing
- Start mining: valid node, depleted node, too far, insufficient skill
- Mine tick: drop rolling, exp gain, node depletion countdown
- Cancel mining: explicit cancel, movement cancel
- Node respawn: depleted node respawns after timer
- Drop weight calculation: skill-gated drops

## Files to Create
1. `src/crafting/mining_config.h`
2. `src/registry/mining_registry.h`
3. `src/registry/mining_registry.cpp`
4. `src/crafting/mining_system.h`
5. `src/crafting/mining_system.cpp`
6. `bin/game_configs/mining_nodes.yaml`
7. `tests/mining_test.cpp`

## Files to Modify
1. `src/network/json_protocol.h` — 4 new message types
2. `src/network/json_protocol.cpp` — type_map + to_string
3. `src/bridge/handlers/game_handlers.h` — mining_system* member, 2 handler methods
4. `src/bridge/handlers/game_handlers.cpp` — handler implementations + registrations
5. `src/application.cpp` — subsystem creation and wiring
