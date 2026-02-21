# Dead Occupant Tile System Implementation Plan

> **For Claude:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** Make dead entities occupy the tile dead slot instead of blocking the live slot, and make dead entities visible to players entering range.

**Architecture:** Tiles already have `dead_entity`/`dead_type` fields and `set_dead_entity()`/`clear_dead_entity()` methods — they're just not wired up. On death, move entity from live→dead slot. On despawn/respawn, clear dead slot (if still ours). Add `is_dead` to `visible_entity_msg` and include dead NPCs in visibility builds.

**Tech Stack:** C++20, gtest, nlohmann-json

---

### Task 1: Add `is_dead` to `visible_entity_msg` and serialization

**Files:**
- Modify: `src/network/json_protocol.h:1165` (after `category` field)
- Modify: `src/network/json_protocol.cpp:1195` (before `return j;`)

**Step 1: Add `is_dead` field to struct**

In `src/network/json_protocol.h`, after line 1165 (`std::string category;`), add:

```cpp
    bool is_dead{false};         // Entity is a corpse
```

**Step 2: Add `is_dead` to `to_json()`**

In `src/network/json_protocol.cpp`, after the NPC block at line 1195 (the closing `}`), before `return j;`, add:

```cpp
    if (is_dead) {
        j["is_dead"] = true;
    }
```

**Step 3: Build and verify**

Run: `cmake --build build --config Debug`
Expected: Clean compile

**Step 4: Run tests**

Run: `./bin/hgserver_tests`
Expected: All tests pass (no behavior change yet)

---

### Task 2: Add `is_dead` parameter to `build_npc_spawn()`

**Files:**
- Modify: `src/bridge/handlers/entity_builders.h:41-44`
- Modify: `src/bridge/handlers/entity_builders.cpp:235-258`

**Step 1: Update function signature in header**

In `src/bridge/handlers/entity_builders.h`, change `build_npc_spawn` signature from:

```cpp
auto build_npc_spawn(
    const npc::npc& n,
    std::string_view hostility
) -> network::visible_entity_msg;
```

to:

```cpp
auto build_npc_spawn(
    const npc::npc& n,
    std::string_view hostility,
    bool is_dead = false
) -> network::visible_entity_msg;
```

**Step 2: Update implementation**

In `src/bridge/handlers/entity_builders.cpp`, change `build_npc_spawn` signature to match, and before the `return msg;` at line 257, add:

```cpp
    msg.is_dead = is_dead;
```

**Step 3: Build and verify**

Run: `cmake --build build --config Debug`
Expected: Clean compile (default parameter means no callers need updating)

---

### Task 3: Wire `kill_npc()` to move NPC from live→dead tile slot

**Files:**
- Modify: `src/npc/npc_system.cpp:433-457` (`kill_npc`)
- Test: `tests/test_npc.cpp`

**Step 1: Write failing test**

Add to `tests/test_npc.cpp`, after the existing `kill_npc` test. This test needs a world subsystem wired to npc_system to verify tile state. Since the existing `npc_system_test` fixture doesn't wire world, create a new test that directly verifies the tile API:

```cpp
TEST(tile_dead_occupant_test, set_and_clear_dead_entity) {
    world::dynamic_tile tile;

    EXPECT_FALSE(tile.has_dead_entity());
    EXPECT_FALSE(tile.has_occupant());

    // Simulate entity alive on tile
    tile.set_occupant(entity_id{42}, world::owner_type::npc);
    EXPECT_TRUE(tile.has_occupant());

    // Simulate death: clear live, set dead
    tile.clear_occupant();
    tile.set_dead_entity(entity_id{42}, world::owner_type::npc);

    EXPECT_FALSE(tile.has_occupant());
    EXPECT_TRUE(tile.has_dead_entity());
    EXPECT_EQ(tile.dead_entity.value, 42u);
    EXPECT_EQ(tile.dead_type, world::owner_type::npc);
}

TEST(tile_dead_occupant_test, dead_slot_is_last_write_wins) {
    world::dynamic_tile tile;

    tile.set_dead_entity(entity_id{1}, world::owner_type::npc);
    EXPECT_EQ(tile.dead_entity.value, 1u);

    // Second death overwrites
    tile.set_dead_entity(entity_id{2}, world::owner_type::npc);
    EXPECT_EQ(tile.dead_entity.value, 2u);
}

TEST(tile_dead_occupant_test, dead_entity_does_not_block_movement) {
    world::dynamic_tile tile;
    tile.set_dead_entity(entity_id{42}, world::owner_type::npc);

    // Dead entity should not count as occupant
    EXPECT_FALSE(tile.has_occupant());
    EXPECT_FALSE(tile.is_temp_blocked());
}

TEST(tile_dead_occupant_test, clear_dead_entity_removes_corpse_flag) {
    world::dynamic_tile tile;
    tile.set_dead_entity(entity_id{42}, world::owner_type::npc);
    EXPECT_TRUE(has_flag(tile.flags, world::dynamic_tile_flags::has_corpse));

    tile.clear_dead_entity();
    EXPECT_FALSE(tile.has_dead_entity());
    EXPECT_FALSE(has_flag(tile.flags, world::dynamic_tile_flags::has_corpse));
}
```

**Step 2: Run tests to verify they pass**

These tests exercise existing tile code that already works — they should pass immediately since the tile methods already exist.

Run: `./bin/hgserver_tests --gtest_filter="tile_dead_occupant*"`
Expected: PASS (4 tests)

**Step 3: Modify `kill_npc()` to update tile**

In `src/npc/npc_system.cpp`, in `kill_npc()` after line 438 (`npc_ptr->ai_state.death_time = ...`), add:

```cpp
    // Move from live occupant slot to dead slot on tile
    auto* world = subsystems().get<world::world_subsystem>();
    if (world) {
        auto* m = world->get_map(npc_ptr->current_map);
        if (m) {
            m->clear_occupant(npc_ptr->pos);
            m->set_dead_entity(npc_ptr->pos, hb::entity_id{id.index()}, world::owner_type::npc);
        }
    }
```

You'll need to add the world include if not already present. Check the top of `npc_system.cpp` for:
```cpp
#include "world/world_subsystem.h"
```

**Step 4: Build and run all tests**

Run: `cmake --build build --config Debug && ./bin/hgserver_tests`
Expected: All pass

---

### Task 4: Wire `despawn_npc()` to clear dead slot only if still ours

**Files:**
- Modify: `src/npc/npc_system.cpp:379-428` (`despawn_npc`)

**Step 1: Modify `despawn_npc()` to conditionally clear dead slot**

In `despawn_npc()`, replace the existing tile cleanup block (lines 410-418):

```cpp
    // Remove from spatial index and clear occupant
    auto* world = subsystems().get<world::world_subsystem>();
    if (world) {
        auto* m = world->get_map(npc_ref.current_map);
        if (m) {
            m->spatial().remove(hb::entity_id{id.index()});
            m->clear_occupant(npc_ref.pos);
        }
    }
```

with:

```cpp
    // Remove from spatial index and clear tile slots
    auto* world = subsystems().get<world::world_subsystem>();
    if (world) {
        auto* m = world->get_map(npc_ref.current_map);
        if (m) {
            m->spatial().remove(hb::entity_id{id.index()});
            m->clear_occupant(npc_ref.pos);

            // Clear dead slot only if it still belongs to this NPC
            auto dead_eid = m->get_dead_entity(npc_ref.pos);
            if (dead_eid && dead_eid->value == id.index()) {
                m->clear_dead_entity(npc_ref.pos);
            }
        }
    }
```

**Step 2: Build and run all tests**

Run: `cmake --build build --config Debug && ./bin/hgserver_tests`
Expected: All pass

---

### Task 5: Wire player death to move from live→dead tile slot

**Files:**
- Modify: `src/bridge/handlers/game_handlers.cpp:3784-3799` (`on_entity_death`)

**Step 1: Add tile slot swap in `on_entity_death()`**

In `on_entity_death()`, after getting the victim player (line 3790), before `broadcast_entity_death`, add:

```cpp
    // Move from live occupant slot to dead slot on tile
    if (world_) {
        auto* m = world_->get_map(victim->current_map);
        if (m) {
            m->clear_occupant(victim->pos);
            m->set_dead_entity(victim->pos, hb::entity_id{victim_pid.value}, world::owner_type::player);
        }
    }
```

**Step 2: Build and run all tests**

Run: `cmake --build build --config Debug && ./bin/hgserver_tests`
Expected: All pass

---

### Task 6: Wire respawn to clear dead tile slot

**Files:**
- Modify: `src/bridge/handlers/game_handlers.cpp:3900-3922` (`execute_respawn`)

**Step 1: Clear dead slot at old position before teleporting**

In `execute_respawn()`, after getting the player (line 3909), before restoring HP, add:

```cpp
    // Clear dead slot at death position (if still ours)
    if (world_) {
        auto* m = world_->get_map(player->current_map);
        if (m) {
            auto dead_eid = m->get_dead_entity(player->pos);
            if (dead_eid && dead_eid->value == pid.value) {
                m->clear_dead_entity(player->pos);
            }
        }
    }
```

**Step 2: Build and run all tests**

Run: `cmake --build build --config Debug && ./bin/hgserver_tests`
Expected: All pass

---

### Task 7: Include dead NPCs in `build_visible_entities_at()`

**Files:**
- Modify: `src/bridge/handlers/game_handlers.cpp:3626-3670` (`build_visible_entities_at`)

**Step 1: Add dead NPC iteration**

In `build_visible_entities_at()`, after the existing NPC block (line 3667, closing `}`), add:

```cpp
    // Include dead NPCs (corpses visible within range)
    if (npc_) {
        npc_->for_each_npc_on_map(map, [&](auto /*id*/, const hb::npc::npc& n) {
            if (!n.is_dead()) return;  // Only dead NPCs in this pass

            // Rect-filter: skip NPCs outside the rectangular viewport
            if (std::abs(n.pos.x - pos.x) > visibility_radius_x
                || std::abs(n.pos.y - pos.y) > visibility_radius_y) return;

            entities.push_back(build_npc_spawn(n, "neutral", true));
        });
    }
```

Note: The existing NPC loop at line 3660 iterates ALL NPCs (dead included). Change line 3660 to skip dead NPCs so they aren't sent twice. Inside the existing lambda, add at the top:

```cpp
            if (n.is_dead()) return;  // Dead NPCs handled separately below
```

**Step 2: Build and run all tests**

Run: `cmake --build build --config Debug && ./bin/hgserver_tests`
Expected: All pass

---

### Task 8: Write integration tests for dead entity visibility

**Files:**
- Modify: `tests/test_npc.cpp`

**Step 1: Write tests for `visible_entity_msg` with `is_dead`**

```cpp
TEST(visible_entity_msg_test, is_dead_serialized_when_true) {
    network::visible_entity_msg msg;
    msg.entity_id = 42;
    msg.type = "npc";
    msg.name = "Slime";
    msg.x = 10;
    msg.y = 20;
    msg.hp_percent = 0;
    msg.direction = 0;
    msg.template_id = 1;
    msg.sprite_id = 10;
    msg.level = 1;
    msg.category = "monster";
    msg.hostility = "neutral";
    msg.is_dead = true;

    auto j = msg.to_json();
    EXPECT_TRUE(j.contains("is_dead"));
    EXPECT_TRUE(j["is_dead"].get<bool>());
}

TEST(visible_entity_msg_test, is_dead_omitted_when_false) {
    network::visible_entity_msg msg;
    msg.entity_id = 42;
    msg.type = "npc";
    msg.name = "Slime";
    msg.x = 10;
    msg.y = 20;
    msg.hp_percent = 100;
    msg.direction = 0;
    msg.template_id = 1;
    msg.sprite_id = 10;
    msg.level = 1;
    msg.category = "monster";
    msg.hostility = "neutral";
    msg.is_dead = false;

    auto j = msg.to_json();
    EXPECT_FALSE(j.contains("is_dead"));
}
```

Add includes at top of test file if needed:
```cpp
#include "network/json_protocol.h"
```

**Step 2: Run the new tests**

Run: `./bin/hgserver_tests --gtest_filter="visible_entity_msg*"`
Expected: PASS

**Step 3: Run full test suite**

Run: `./bin/hgserver_tests`
Expected: All pass

---

### Task 9: Notify claude-a about protocol changes

**Step 1: Send broker message to claude-a**

Use the broker system to notify claude-a about the `is_dead` field addition to `visible_entity_msg`:
- `visible_entity_msg` now includes `"is_dead": true` for dead entities (omitted when false)
- Dead NPCs are now included in entity visibility builds when entering range
- Client should render corpse animation for entities with `is_dead: true`
