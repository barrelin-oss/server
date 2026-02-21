# Dead Occupant Tile System

## Problem

When an entity dies, it continues to hold the tile's live occupant slot, blocking other entities from walking over the corpse. Legacy Helbreath tiles support two occupant slots — one alive, one dead — so entities can walk over corpses. Dead entities also need to be visible to players entering range.

## Design

### Tile Occupancy Lifecycle

**On entity death:**
1. Clear the live occupant slot → tile becomes walkable
2. Set the dead occupant slot to the dying entity (last-write-wins — overwrites any previous dead occupant, matching legacy `m_cDeadOwnerClass`/`m_sDeadOwner` behavior)
3. NPC object remains in `npcs_` map with `ai_state::dead` until its own corpse timer expires

**On corpse timer expiry (NPC despawn):**
1. Fire `on_despawn_callback` → second loot phase (body parts, rares, boss multi-drops)
2. Clear the dead occupant slot only if it still matches this NPC (another corpse may have overwritten it)
3. Remove NPC from memory

**On player respawn:**
1. Clear the dead occupant slot at the death position (if it still matches this player)

### Visibility

Dead entities are sent to clients using the existing `visible_entity_msg` with an added `is_dead` field. `build_visible_entities_at()` iterates dead NPCs on the map within view range alongside live entities. The client renders corpse state based on the `is_dead` flag.

### Key Invariant

The tile dead slot is a visual marker only — "show a corpse here." It is decoupled from the NPC's in-memory lifecycle. A boss with a long corpse timer can have its tile dead slot overwritten by another entity dying on top, but the boss NPC still exists in memory and its despawn loot still fires correctly when the timer expires.

## Changes

### `npc_system.cpp` — `kill_npc()`
- Get the NPC's map and position
- Call `map->clear_occupant(pos)` to free the live slot
- Call `map->set_dead_entity(pos, entity_id, owner_type::npc)` to fill the dead slot

### `npc_system.cpp` — `despawn_npc()`
- Before removing the NPC, check if the tile's dead entity matches this NPC
- If so, call `map->clear_dead_entity(pos)`
- Existing cleanup (spatial index removal, entity destruction) remains unchanged

### `game_handlers.cpp` — `handle_player_death()`
- Get the player's map and position
- Call `map->clear_occupant(pos)` to free the live slot
- Call `map->set_dead_entity(pos, entity_id, owner_type::player)` to fill the dead slot

### `game_handlers.cpp` — respawn handler
- Clear the dead occupant slot at the old death position (if it still matches this player)

### `json_protocol.h` — `visible_entity_msg`
- Add `bool is_dead{false}` field

### `json_protocol.cpp` — `visible_entity_msg::to_json()`
- Include `"is_dead": true` when the flag is set

### `entity_builders.cpp` — `build_npc_spawn()`
- Accept a `bool is_dead` parameter and set it on the message

### `game_handlers.cpp` — `build_visible_entities_at()`
- After iterating live entities, iterate dead NPCs on the map within view range
- Build `visible_entity_msg` with `is_dead = true` for each
