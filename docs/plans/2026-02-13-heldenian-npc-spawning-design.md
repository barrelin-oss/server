# Heldenian NPC Spawning & Teleportation Design

## Problem

The heldenian system uses abstract HP-based objectives with no real NPC entities on the map. Legacy Helbreath spawns tower NPCs (types 87/89) on BtField and door NPCs (type 91) on HRampart, with teleportation and evacuation support.

## Design

### Data Changes (`heldenian_types.h`)

Add to `heldenian_objective`:
- `uint16_t npc_type{0}` — NPC template ID (87/89 for towers, 91 for doors)
- `uint8_t direction{0}` — NPC facing direction (used for doors)
- `entity::entity eid{}` — link to spawned NPC entity (runtime only, not config)

New struct:
```cpp
struct heldenian_teleport_coords {
    std::string map_name;
    int16_t x{0};
    int16_t y{0};
};
```

### System API Changes (`heldenian_system.h`)

New public methods:
- `on_npc_killed(entity::entity eid)` — NPC death callback, destroys matching objective
- `get_teleport_destination(war_faction) -> optional<heldenian_teleport_coords>` — returns war zone coords
- `evacuate_map(const string& map_name)` — teleports non-admin players off map

New callbacks:
- `set_teleport_fn(fn)` — callback for evacuation teleports

### Implementation (`heldenian_system.cpp`)

1. **Spawning in `start_heldenian()`**: After `reset_objectives()`, resolve map name → `map_id` via `world_->get_map_by_name()`, call `npcs_->spawn_npc()` for each objective, store `eid` on objective.

2. **NPC death linking**: `on_npc_killed()` scans both objective vectors for matching `eid`, marks destroyed, calls `check_victory_condition()`.

3. **Cleanup**: `cleanup_heldenian()` calls `npcs_->despawn_npc()` for surviving objectives.

4. **Evacuation**: `evacuate_map()` iterates players via `for_each_player`, teleports non-admins via callback.

5. **Teleport coords**: Returns fixed coordinates per mode/faction matching legacy values.

### Scope

- System API only — no game_handler wiring for Gail NPC interaction
- Follows crusade system's `summon_war_unit()` pattern exactly
- ~8 new tests

### Files Modified
- `src/war/heldenian/heldenian_types.h`
- `src/war/heldenian/heldenian_system.h`
- `src/war/heldenian/heldenian_system.cpp`
- `tests/test_heldenian_system.cpp`
