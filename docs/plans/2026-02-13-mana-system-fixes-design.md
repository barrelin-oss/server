# Mana System Fixes Design

**Date:** 2026-02-13

## Problems

1. GMG charges default to 1 — legacy requires multiple charges (from NPC config, typically 10) before firing meteor
2. Mana pool reset keeps remainder — legacy discards it (resets to 0)
3. Mana stones are infinite sources — legacy has per-stone depletion and regeneration per tick
4. Mana collector MP restoration to nearby allied players is missing
5. GMG attackability missing — legacy allows attacking GMG to reduce mana charges
6. `collector_mp_restore` config field exists but is unused

## Design

### 1. GMG Charge Threshold Fix

- Change `gmg_charges_for_meteor` default from `1` to `10`
- Change `check_gmg()`: replace `while (pool >= threshold) { pool -= threshold; ... }` with `if (pool >= threshold) { pool = 0; ... }`
- Single charge per threshold crossing, remainder discarded (matching legacy)

### 2. Virtual Stone State

Stones are shared between factions. Both sides' collectors drain from the same pool.

New struct:
```cpp
struct mana_stone_state
{
    int32_t current_mana{5};
    static constexpr int32_t max_mana = 5;
    static constexpr int32_t regen_rate = 5;
};
```

Changes to `mana_system`:
- Add `std::vector<mana_stone_state> stones_` member
- New `initialize_stones(int32_t count)` called on reset
- New public `tick(int32_t aresden_collectors, int32_t elvine_collectors)` replaces two `tick_faction_mana` calls

Tick sequence:
1. Regenerate all stones (`current_mana = min(current_mana + regen_rate, max_mana)`)
2. For aresden collectors: drain from stones (up to `harvest_rate` per stone, limited by `current_mana`)
3. For elvine collectors: drain from stones (same, competing for remaining mana)
4. Add harvested mana to each faction's pool
5. Check GMG for both factions

Aresden processes first (matches legacy side-1-first ordering).

### 3. MP Restoration to Allied Players

During each mana tick (every 5 seconds), for each mana_collector structure:
- Get all players who can see the collector (`get_players_who_can_see`)
- For allied players within 5 tiles: restore MP = random(1, player.stats.magic), clamped to max_mp
- Send `crusade_mp_restore` message to ALL viewers (not just healed players)

Protocol message:
```
crusade_mp_restore
{
    "source_x": int,        // Collector position
    "source_y": int,
    "radius": 5,            // Restore radius
    "your_restore": int     // 0 if viewer is out of range or wrong faction
}
```

Broadcast to all players who can see the collector so the client can render the area effect. Players outside the radius or wrong faction receive `your_restore: 0`.

### 4. GMG Damage Vulnerability

New NPC callback:
```cpp
using on_npc_damage_callback = std::function<void(const npc&, int32_t damage, entity::entity source)>;
```

Fires in `npc_system::apply_damage()` after HP reduction, before death check.

New state on `faction_mana_state`:
```cpp
int32_t gmg_accumulated_damage{0};
```

New config on `mana_config`:
```cpp
int32_t gmg_damage_threshold{500};
```

New method `mana_system::apply_gmg_damage(war_faction, int32_t damage)`:
- Accumulates damage on faction state
- At threshold (500): reset accumulator, decrement `gmg_charges` (min 0)

`crusade_system::on_gmg_damage(entity::entity eid, int32_t damage)`:
- Looks up faction from `war_structures_` by entity ID
- Forwards to `mana_system_.apply_gmg_damage()`

Wired via `npc_->set_on_damage_callback()` in game_handlers, filtered to `sprite_id == 41`.

## Files to Modify

- `src/npc/npc_system.h` — add `on_npc_damage_callback` type, setter, member
- `src/npc/npc_system.cpp` — fire callback in `apply_damage()`
- `src/war/crusade/mana_system.h` — `mana_stone_state`, `gmg_accumulated_damage`, `gmg_damage_threshold`, `stones_` vector, new public API
- `src/war/crusade/mana_system.cpp` — stone tracking, pool reset fix, `apply_gmg_damage()`
- `src/war/crusade/crusade_system.h` — add `on_gmg_damage()` method
- `src/war/crusade/crusade_system.cpp` — MP restoration in `tick_mana()`, GMG damage forwarding
- `src/network/json_protocol.h` — add `crusade_mp_restore` message type
- `src/network/json_protocol.cpp` — wire `to_string`/`type_map`/`to_json`/builder
- `src/bridge/handlers/game_handlers.cpp` — wire `on_damage_callback` for GMG
- `src/bridge/handlers/game_handlers.h` — forward declare if needed
- `tests/test_crusade_system.cpp` — ~12 new tests

## Test Plan

**Mana system unit tests:**
1. `gmg_requires_multiple_charges` — charges_for_meteor=3, verify 1-2 charges don't fire, 3rd does
2. `mana_pool_reset_discards_remainder` — 20 mana with threshold 15 → pool = 0
3. `stone_depletion_limits_harvest` — 2 collectors, 1 stone: total harvest = 5 (not 6)
4. `stones_regenerate_each_tick` — drain to 0, next tick full again
5. `gmg_damage_reduces_charges` — 500+ damage decrements charge
6. `gmg_damage_below_threshold_no_effect` — 499 damage, no change
7. `gmg_damage_resets_accumulator` — accumulator resets after threshold

**Crusade integration tests:**
8. `mp_restoration_to_nearby_allies` — player within 5 tiles gets MP
9. `mp_restoration_skips_enemy_faction` — enemy player gets nothing
10. `mp_restoration_skips_out_of_range` — allied player >5 tiles gets nothing
11. `mp_restore_broadcast_to_viewers` — all viewers get `crusade_mp_restore`, correct `your_restore`
12. `gmg_damage_wiring_through_crusade` — `on_gmg_damage(eid, damage)` resolves faction correctly
