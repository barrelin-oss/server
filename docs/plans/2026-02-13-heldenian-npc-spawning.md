# Heldenian NPC Spawning & Teleportation Implementation Plan

> **For Claude:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** Bridge heldenian objectives to real NPC entities, add teleportation coords API, and evacuation support.

**Architecture:** Extend existing `heldenian_objective` with `npc_type`, `direction`, and `eid` fields. Spawn NPCs via `npc_system::spawn_npc()` during `start_heldenian()`, link NPC death to objective destruction, despawn on cleanup. Teleportation returns fixed coords per mode/faction. Evacuation uses a callback function.

**Tech Stack:** C++20, GTest, existing npc_system/world_subsystem/player_system APIs.

---

### Task 1: Add fields to heldenian_objective and new teleport struct

**Files:**
- Modify: `src/war/heldenian/heldenian_types.h:6-40`
- Test: `tests/test_heldenian_system.cpp`

**Step 1: Add entity include and new fields to heldenian_objective**

In `heldenian_types.h`, add include for entity.h and add three new fields to `heldenian_objective`:

```cpp
// After the existing includes (line 7):
#include "entity/entity.h"
```

Add these fields to `heldenian_objective` (after line 37, before `is_destroyed()`):
```cpp
    uint16_t npc_type{0};       // NPC template ID (87/89 for towers, 91 for doors)
    uint8_t direction{0};       // NPC facing direction (doors only)
    entity::entity eid{};       // Runtime: link to spawned NPC entity
```

**Step 2: Add heldenian_teleport_coords struct**

After `heldenian_objective` (after line 40), add:
```cpp
// Teleportation destination for entering the war zone
struct heldenian_teleport_coords {
    std::string map_name;
    int16_t x{0};
    int16_t y{0};
};
```

**Step 3: Add teleport config to heldenian_config**

Add teleport coordinate config fields to `heldenian_config` (after line 74, before charisma fields):
```cpp
    // Teleport coordinates — tower mode (BtField)
    heldenian_teleport_coords tower_aresden_spawn{"BtField", 68, 225};
    heldenian_teleport_coords tower_elvine_spawn{"BtField", 202, 70};

    // Teleport coordinates — door mode (HRampart)
    heldenian_teleport_coords door_defender_spawn{"HRampart", 81, 42};
    heldenian_teleport_coords door_attacker_spawn{"HRampart", 156, 153};
```

**Step 4: Write failing test for new fields**

At end of `tests/test_heldenian_system.cpp`, add:
```cpp
// ========== NPC Spawning: Type Tests ==========

TEST(heldenian_types_test, objective_has_npc_fields) {
    heldenian_objective obj;
    EXPECT_EQ(obj.npc_type, 0);
    EXPECT_EQ(obj.direction, 0);
    EXPECT_FALSE(obj.eid.is_valid());
}

TEST(heldenian_types_test, teleport_coords_default) {
    heldenian_teleport_coords tc;
    EXPECT_TRUE(tc.map_name.empty());
    EXPECT_EQ(tc.x, 0);
    EXPECT_EQ(tc.y, 0);
}
```

**Step 5: Build and run tests**

Run: `cmake --build build --config Debug 2>&1 | tail -20 && ./bin/hgserver_tests --gtest_filter="heldenian_types_test.*"`
Expected: All heldenian_types_test tests PASS (including 2 new ones).

**Step 6: Commit**

```bash
git add src/war/heldenian/heldenian_types.h tests/test_heldenian_system.cpp
git commit -m "feat(heldenian): add npc_type, direction, eid fields to objective and teleport coords struct"
```

---

### Task 2: Add teleport destination API

**Files:**
- Modify: `src/war/heldenian/heldenian_system.h:94-99`
- Modify: `src/war/heldenian/heldenian_system.cpp`
- Test: `tests/test_heldenian_system.cpp`

**Step 1: Write failing tests for get_teleport_destination**

At end of `tests/test_heldenian_system.cpp`, add:
```cpp
// ========== NPC Spawning: Teleport Tests ==========

TEST_F(heldenian_tower_test, teleport_tower_mode_aresden) {
    heldenian_.start_heldenian(heldenian_mode::tower_defense);

    auto dest = heldenian_.get_teleport_destination(war_faction::aresden);
    ASSERT_TRUE(dest.has_value());
    EXPECT_EQ(dest->map_name, "BtField");
    EXPECT_EQ(dest->x, 68);
    EXPECT_EQ(dest->y, 225);
}

TEST_F(heldenian_tower_test, teleport_tower_mode_elvine) {
    heldenian_.start_heldenian(heldenian_mode::tower_defense);

    auto dest = heldenian_.get_teleport_destination(war_faction::elvine);
    ASSERT_TRUE(dest.has_value());
    EXPECT_EQ(dest->map_name, "BtField");
    EXPECT_EQ(dest->x, 202);
    EXPECT_EQ(dest->y, 70);
}

TEST_F(heldenian_door_test, teleport_door_mode_defender) {
    heldenian_.start_heldenian(heldenian_mode::door_defense);

    auto dest = heldenian_.get_teleport_destination(war_faction::aresden);  // default defender
    ASSERT_TRUE(dest.has_value());
    EXPECT_EQ(dest->map_name, "HRampart");
    EXPECT_EQ(dest->x, 81);
    EXPECT_EQ(dest->y, 42);
}

TEST_F(heldenian_door_test, teleport_door_mode_attacker) {
    heldenian_.start_heldenian(heldenian_mode::door_defense);

    auto dest = heldenian_.get_teleport_destination(war_faction::elvine);  // attacker
    ASSERT_TRUE(dest.has_value());
    EXPECT_EQ(dest->map_name, "HRampart");
    EXPECT_EQ(dest->x, 156);
    EXPECT_EQ(dest->y, 153);
}

TEST_F(heldenian_tower_test, teleport_inactive_returns_nullopt) {
    auto dest = heldenian_.get_teleport_destination(war_faction::aresden);
    EXPECT_FALSE(dest.has_value());
}

TEST_F(heldenian_tower_test, teleport_neutral_returns_nullopt) {
    heldenian_.start_heldenian(heldenian_mode::tower_defense);
    auto dest = heldenian_.get_teleport_destination(war_faction::neutral);
    EXPECT_FALSE(dest.has_value());
}
```

**Step 2: Build — expect compile error (method doesn't exist)**

Run: `cmake --build build --config Debug 2>&1 | tail -20`
Expected: FAIL — `get_teleport_destination` not declared.

**Step 3: Add declaration to header**

In `heldenian_system.h`, after `count_surviving` (line 99), add:
```cpp
    // ========== Teleportation ==========

    [[nodiscard]] auto get_teleport_destination(war_faction player_faction) const
        -> std::optional<heldenian_teleport_coords>;
```

Also add `#include <optional>` to the includes (after line 15).

**Step 4: Implement get_teleport_destination**

In `heldenian_system.cpp`, after `count_surviving` (line 288), add:
```cpp
// ========== Teleportation ==========

auto heldenian_system::get_teleport_destination(war_faction player_faction) const
    -> std::optional<heldenian_teleport_coords>
{
    if (!active_ || player_faction == war_faction::neutral)
        return std::nullopt;

    if (current_mode_ == heldenian_mode::tower_defense)
    {
        if (player_faction == war_faction::aresden)
            return config_.tower_aresden_spawn;
        if (player_faction == war_faction::elvine)
            return config_.tower_elvine_spawn;
    }
    else if (current_mode_ == heldenian_mode::door_defense)
    {
        if (player_faction == defending_faction_)
            return config_.door_defender_spawn;
        else
            return config_.door_attacker_spawn;
    }

    return std::nullopt;
}
```

**Step 5: Build and run tests**

Run: `cmake --build build --config Debug 2>&1 | tail -20 && ./bin/hgserver_tests --gtest_filter="*heldenian*teleport*"`
Expected: All 6 teleport tests PASS.

**Step 6: Commit**

```bash
git add src/war/heldenian/heldenian_system.h src/war/heldenian/heldenian_system.cpp tests/test_heldenian_system.cpp
git commit -m "feat(heldenian): add get_teleport_destination API with per-mode/faction coords"
```

---

### Task 3: Add NPC spawning in start_heldenian and despawning in cleanup

**Files:**
- Modify: `src/war/heldenian/heldenian_system.h`
- Modify: `src/war/heldenian/heldenian_system.cpp:140-206,407-449,552-561`
- Test: `tests/test_heldenian_system.cpp`

**Step 1: Write failing tests for NPC spawning**

At end of `tests/test_heldenian_system.cpp`, add a new test fixture and tests:
```cpp
// ========== NPC Spawning: Spawn/Despawn Tests ==========

#include "npc/npc_system.h"
#include "registry/npc_registry.h"
#include "world/world_subsystem.h"
#include "world/map.h"

class heldenian_npc_test : public ::testing::Test {
protected:
    void SetUp() override {
        // Set up NPC registry with tower/door templates
        hb::npc_template tower87;
        tower87.id = hb::npc_id(87);
        tower87.name = "Tower";
        tower87.hp = 500;
        npc_reg_.register_template(tower87);

        hb::npc_template tower89;
        tower89.id = hb::npc_id(89);
        tower89.name = "Tower2";
        tower89.hp = 500;
        npc_reg_.register_template(tower89);

        hb::npc_template door91;
        door91.id = hb::npc_id(91);
        door91.name = "Door";
        door91.hp = 300;
        npc_reg_.register_template(door91);

        npc_sys_.set_registry(&npc_reg_);
        npc_sys_.initialize();
        war_sys_.initialize();
        heldenian_.initialize();
        heldenian_.set_dependencies(&war_sys_, nullptr, nullptr, &npc_sys_, nullptr);
    }

    void TearDown() override {
        heldenian_.shutdown();
        npc_sys_.shutdown();
        war_sys_.shutdown();
    }

    hb::npc_registry npc_reg_;
    hb::npc::npc_system npc_sys_;
    war_system war_sys_;
    heldenian_system heldenian_;
};

TEST_F(heldenian_npc_test, tower_mode_spawns_npcs) {
    heldenian_config cfg;
    cfg.enabled = true;

    heldenian_objective t1{.id = 1, .x = 10, .y = 20, .max_hp = 500, .npc_type = 87};
    heldenian_objective t2{.id = 2, .x = 30, .y = 40, .max_hp = 500, .npc_type = 89};
    cfg.aresden_towers = {t1};
    cfg.elvine_towers = {t2};

    heldenian_.set_config(cfg);
    auto result = heldenian_.start_heldenian(heldenian_mode::tower_defense);
    EXPECT_TRUE(result.is_ok());

    // Objectives should have valid entity IDs
    const auto& ares = heldenian_.get_objectives(war_faction::aresden);
    ASSERT_EQ(ares.size(), 1);
    EXPECT_TRUE(ares[0].eid.is_valid());
    EXPECT_TRUE(npc_sys_.npc_exists(ares[0].eid));

    const auto& elv = heldenian_.get_objectives(war_faction::elvine);
    ASSERT_EQ(elv.size(), 1);
    EXPECT_TRUE(elv[0].eid.is_valid());
    EXPECT_TRUE(npc_sys_.npc_exists(elv[0].eid));
}

TEST_F(heldenian_npc_test, door_mode_spawns_defender_npcs) {
    heldenian_config cfg;
    cfg.enabled = true;

    heldenian_objective d1{.id = 1, .x = 50, .y = 60, .max_hp = 300, .npc_type = 91, .direction = 3};
    heldenian_objective d2{.id = 2, .x = 70, .y = 80, .max_hp = 300, .npc_type = 91, .direction = 1};
    cfg.defender_doors = {d1, d2};

    heldenian_.set_config(cfg);
    auto result = heldenian_.start_heldenian(heldenian_mode::door_defense);
    EXPECT_TRUE(result.is_ok());

    const auto& doors = heldenian_.get_objectives(war_faction::aresden);
    ASSERT_EQ(doors.size(), 2);
    EXPECT_TRUE(doors[0].eid.is_valid());
    EXPECT_TRUE(doors[1].eid.is_valid());
    EXPECT_TRUE(npc_sys_.npc_exists(doors[0].eid));
    EXPECT_TRUE(npc_sys_.npc_exists(doors[1].eid));
}

TEST_F(heldenian_npc_test, cleanup_despawns_surviving_npcs) {
    heldenian_config cfg;
    cfg.enabled = true;

    heldenian_objective t1{.id = 1, .x = 10, .y = 20, .max_hp = 500, .npc_type = 87};
    cfg.aresden_towers = {t1};

    heldenian_objective t2{.id = 1, .x = 30, .y = 40, .max_hp = 500, .npc_type = 89};
    cfg.elvine_towers = {t2};

    heldenian_.set_config(cfg);
    heldenian_.start_heldenian(heldenian_mode::tower_defense);

    const auto& ares = heldenian_.get_objectives(war_faction::aresden);
    auto eid = ares[0].eid;
    EXPECT_TRUE(npc_sys_.npc_exists(eid));

    heldenian_.end_heldenian(war_faction::neutral);
    EXPECT_FALSE(npc_sys_.npc_exists(eid));  // Despawned
}

TEST_F(heldenian_npc_test, start_without_npc_system_still_works) {
    // Like the existing tests — nullptr npc system
    heldenian_system h2;
    h2.initialize();
    h2.set_dependencies(&war_sys_, nullptr, nullptr, nullptr, nullptr);

    heldenian_config cfg;
    cfg.enabled = true;
    heldenian_objective t1{.id = 1, .max_hp = 200, .npc_type = 87};
    cfg.aresden_towers = {t1};
    heldenian_objective t2{.id = 1, .max_hp = 200, .npc_type = 89};
    cfg.elvine_towers = {t2};
    h2.set_config(cfg);

    auto result = h2.start_heldenian(heldenian_mode::tower_defense);
    EXPECT_TRUE(result.is_ok());
    EXPECT_TRUE(h2.is_active());

    // Objectives exist but without entity IDs
    const auto& ares = h2.get_objectives(war_faction::aresden);
    EXPECT_FALSE(ares[0].eid.is_valid());

    h2.shutdown();
}
```

**Step 2: Build — expect compile error**

Run: `cmake --build build --config Debug 2>&1 | tail -20`
Expected: FAIL — tests reference new `npc_type`/`direction` fields in designated initializers, but fields may be in wrong order. Fix any ordering issues.

**Step 3: Add spawn_objectives helper and modify start_heldenian**

In `heldenian_system.h`, add private method declaration (after line 128):
```cpp
    void spawn_objective_npcs();
    void despawn_objective_npcs();
```

In `heldenian_system.cpp`, add the spawning logic. After `reset_objectives()` call in `start_heldenian()` (line 175), add:
```cpp
    spawn_objective_npcs();
```

Add the implementation (before `cleanup_heldenian()`):
```cpp
void heldenian_system::spawn_objective_npcs()
{
    if (!npcs_) return;

    auto spawn_for_objectives = [&](std::vector<heldenian_objective>& objs, const std::string& map_name)
    {
        map_id mid{};
        if (world_)
        {
            auto* m = world_->get_map_by_name(map_name);
            if (m) mid = m->id();
        }

        for (auto& obj : objs)
        {
            if (obj.npc_type == 0) continue;

            auto spawn_result = npcs_->spawn_npc(
                npc_id(obj.npc_type), mid, {obj.x, obj.y});
            if (spawn_result.is_ok())
            {
                obj.eid = spawn_result.value();
                LOG_DEBUG(general, "Spawned heldenian NPC type={} at ({},{}) eid={}",
                    obj.npc_type, obj.x, obj.y, obj.eid.id);
            }
        }
    };

    if (current_mode_ == heldenian_mode::tower_defense)
    {
        spawn_for_objectives(aresden_objectives_, config_.tower_map);
        spawn_for_objectives(elvine_objectives_, config_.tower_map);
    }
    else
    {
        auto& objs = (defending_faction_ == war_faction::aresden) ?
            aresden_objectives_ : elvine_objectives_;
        spawn_for_objectives(objs, config_.door_map);
    }
}

void heldenian_system::despawn_objective_npcs()
{
    if (!npcs_) return;

    auto despawn = [&](std::vector<heldenian_objective>& objs)
    {
        for (auto& obj : objs)
        {
            if (obj.eid.is_valid() && !obj.is_destroyed())
            {
                npcs_->despawn_npc(obj.eid);
            }
        }
    };

    despawn(aresden_objectives_);
    despawn(elvine_objectives_);
}
```

In `cleanup_heldenian()`, add `despawn_objective_npcs()` **before** clearing the objective vectors (before line 559):
```cpp
    despawn_objective_npcs();
```

**Step 4: Build and run tests**

Run: `cmake --build build --config Debug 2>&1 | tail -20 && ./bin/hgserver_tests --gtest_filter="*heldenian_npc*"`
Expected: All 4 NPC tests PASS. Also run existing tests to ensure no regressions:
Run: `./bin/hgserver_tests --gtest_filter="*heldenian*"`
Expected: All heldenian tests PASS.

**Step 5: Commit**

```bash
git add src/war/heldenian/heldenian_system.h src/war/heldenian/heldenian_system.cpp tests/test_heldenian_system.cpp
git commit -m "feat(heldenian): spawn NPC entities for tower/door objectives at war start, despawn on cleanup"
```

---

### Task 4: Add on_npc_killed callback for NPC death → objective destruction

**Files:**
- Modify: `src/war/heldenian/heldenian_system.h`
- Modify: `src/war/heldenian/heldenian_system.cpp`
- Test: `tests/test_heldenian_system.cpp`

**Step 1: Write failing tests for on_npc_killed**

At end of `tests/test_heldenian_system.cpp`:
```cpp
// ========== NPC Spawning: NPC Death → Objective Destruction ==========

TEST_F(heldenian_npc_test, npc_death_destroys_objective) {
    heldenian_config cfg;
    cfg.enabled = true;

    heldenian_objective t1{.id = 1, .x = 10, .y = 20, .max_hp = 500, .npc_type = 87};
    heldenian_objective t2{.id = 2, .x = 30, .y = 40, .max_hp = 500, .npc_type = 89};
    cfg.aresden_towers = {t1, t2};

    heldenian_objective e1{.id = 1, .x = 50, .y = 60, .max_hp = 500, .npc_type = 87};
    cfg.elvine_towers = {e1};

    heldenian_.set_config(cfg);
    heldenian_.start_heldenian(heldenian_mode::tower_defense);

    const auto& ares = heldenian_.get_objectives(war_faction::aresden);
    auto eid1 = ares[0].eid;

    heldenian_.on_npc_killed(eid1);

    EXPECT_EQ(heldenian_.count_surviving(war_faction::aresden), 1);
    EXPECT_TRUE(heldenian_.is_active());  // Still has 1 tower left
}

TEST_F(heldenian_npc_test, all_towers_killed_triggers_victory) {
    heldenian_config cfg;
    cfg.enabled = true;

    heldenian_objective t1{.id = 1, .x = 10, .y = 20, .max_hp = 500, .npc_type = 87};
    cfg.aresden_towers = {t1};

    heldenian_objective e1{.id = 1, .x = 50, .y = 60, .max_hp = 500, .npc_type = 87};
    cfg.elvine_towers = {e1};

    heldenian_.set_config(cfg);
    heldenian_.start_heldenian(heldenian_mode::tower_defense);

    const auto& ares = heldenian_.get_objectives(war_faction::aresden);
    heldenian_.on_npc_killed(ares[0].eid);

    EXPECT_FALSE(heldenian_.is_active());  // Elvine wins — all Aresden towers dead
}

TEST_F(heldenian_npc_test, npc_killed_with_invalid_eid_ignored) {
    heldenian_config cfg;
    cfg.enabled = true;

    heldenian_objective t1{.id = 1, .x = 10, .y = 20, .max_hp = 500, .npc_type = 87};
    cfg.aresden_towers = {t1};
    heldenian_objective e1{.id = 1, .x = 50, .y = 60, .max_hp = 500, .npc_type = 87};
    cfg.elvine_towers = {e1};

    heldenian_.set_config(cfg);
    heldenian_.start_heldenian(heldenian_mode::tower_defense);

    heldenian_.on_npc_killed(hb::entity::entity(99999));  // Unknown NPC
    EXPECT_TRUE(heldenian_.is_active());
    EXPECT_EQ(heldenian_.count_surviving(war_faction::aresden), 1);
}

TEST_F(heldenian_npc_test, door_npc_killed_triggers_victory) {
    heldenian_config cfg;
    cfg.enabled = true;

    heldenian_objective d1{.id = 1, .x = 50, .y = 60, .max_hp = 300, .npc_type = 91};
    cfg.defender_doors = {d1};

    heldenian_.set_config(cfg);
    heldenian_.start_heldenian(heldenian_mode::door_defense);

    const auto& doors = heldenian_.get_objectives(war_faction::aresden);
    heldenian_.on_npc_killed(doors[0].eid);

    EXPECT_FALSE(heldenian_.is_active());  // Attacker wins — all doors dead
}
```

**Step 2: Build — expect compile error**

Run: `cmake --build build --config Debug 2>&1 | tail -20`
Expected: FAIL — `on_npc_killed` not declared.

**Step 3: Add on_npc_killed declaration and implementation**

In `heldenian_system.h`, in the Objectives section (after line 99), add:
```cpp
    // NPC death callback — links NPC entity death to objective destruction
    void on_npc_killed(entity::entity eid);
```

Also add `#include "entity/entity.h"` to the header includes if not already present (it's included transitively via heldenian_types.h after Task 1).

In `heldenian_system.cpp`, after `count_surviving` (line 288):
```cpp
void heldenian_system::on_npc_killed(entity::entity eid)
{
    if (!active_ || !eid.is_valid()) return;

    auto check_objectives = [&](std::vector<heldenian_objective>& objs) -> bool
    {
        for (auto& obj : objs)
        {
            if (obj.eid == eid && !obj.is_destroyed())
            {
                obj.hp = 0;
                LOG_INFO(general, "Heldenian objective {} destroyed via NPC death (eid={})",
                    obj.id, eid.id);
                broadcast_objective_update(obj.faction);
                return true;
            }
        }
        return false;
    };

    if (check_objectives(aresden_objectives_) || check_objectives(elvine_objectives_))
    {
        check_victory_condition();
    }
}
```

**Step 4: Build and run tests**

Run: `cmake --build build --config Debug 2>&1 | tail -20 && ./bin/hgserver_tests --gtest_filter="*heldenian_npc*"`
Expected: All NPC tests PASS.
Run: `./bin/hgserver_tests --gtest_filter="*heldenian*"`
Expected: All heldenian tests PASS.

**Step 5: Commit**

```bash
git add src/war/heldenian/heldenian_system.h src/war/heldenian/heldenian_system.cpp tests/test_heldenian_system.cpp
git commit -m "feat(heldenian): add on_npc_killed callback linking NPC death to objective destruction"
```

---

### Task 5: Add evacuation callback and evacuate_map

**Files:**
- Modify: `src/war/heldenian/heldenian_system.h`
- Modify: `src/war/heldenian/heldenian_system.cpp`
- Test: `tests/test_heldenian_system.cpp`

**Step 1: Write failing tests for evacuation**

At end of `tests/test_heldenian_system.cpp`:
```cpp
// ========== NPC Spawning: Evacuation Tests ==========

TEST(heldenian_evacuate_test, evacuate_calls_callback_for_map) {
    war_system war_sys;
    heldenian_system heldenian;
    war_sys.initialize();
    heldenian.initialize();
    heldenian.set_dependencies(&war_sys, nullptr, nullptr, nullptr, nullptr);

    std::vector<player_id> evacuated;
    heldenian.set_evacuate_fn([&](player_id pid, const std::string& map) {
        evacuated.push_back(pid);
    });

    heldenian_config cfg;
    cfg.enabled = true;
    heldenian_objective t1{.id = 1, .max_hp = 200, .npc_type = 87};
    cfg.aresden_towers = {t1};
    heldenian_objective t2{.id = 1, .max_hp = 200, .npc_type = 89};
    cfg.elvine_towers = {t2};
    heldenian.set_config(cfg);

    // Evacuate a map — since no player_system, callback should not fire
    heldenian.evacuate_map("BtField");
    EXPECT_TRUE(evacuated.empty());

    heldenian.shutdown();
    war_sys.shutdown();
}
```

**Step 2: Build — expect compile error**

Run: `cmake --build build --config Debug 2>&1 | tail -20`
Expected: FAIL — `set_evacuate_fn` and `evacuate_map` not declared.

**Step 3: Add evacuate_fn callback and evacuate_map declaration**

In `heldenian_system.h`, after the broadcast callback typedefs (line 45), add:
```cpp
using heldenian_evacuate_fn = std::function<void(player_id, const std::string&)>;
```

After `set_broadcast_all_fn` (line 80), add:
```cpp
    void set_evacuate_fn(heldenian_evacuate_fn fn) { evacuate_fn_ = std::move(fn); }
```

In the public Lifecycle section (after `cancel_heldenian`, line 87), add:
```cpp
    // Evacuation
    void evacuate_map(const std::string& map_name);
```

Add private member (after `broadcast_all_fn_`, line 161):
```cpp
    heldenian_evacuate_fn evacuate_fn_;
```

**Step 4: Implement evacuate_map**

In `heldenian_system.cpp`, after the teleport implementation:
```cpp
void heldenian_system::evacuate_map(const std::string& map_name)
{
    if (!players_ || !evacuate_fn_) return;

    std::vector<player_id> to_evacuate;
    players_->for_each_player([&](player_id pid, player::player& plr) {
        if (plr.current_map == map_name && plr.admin == player::admin_level::player)
        {
            to_evacuate.push_back(pid);
        }
    });

    for (auto pid : to_evacuate)
    {
        evacuate_fn_(pid, map_name);
    }

    if (!to_evacuate.empty())
    {
        LOG_INFO(general, "Evacuated {} players from {} for Heldenian",
            to_evacuate.size(), map_name);
    }
}
```

**Step 5: Build and run tests**

Run: `cmake --build build --config Debug 2>&1 | tail -20 && ./bin/hgserver_tests --gtest_filter="*heldenian*"`
Expected: All heldenian tests PASS.

**Step 6: Commit**

```bash
git add src/war/heldenian/heldenian_system.h src/war/heldenian/heldenian_system.cpp tests/test_heldenian_system.cpp
git commit -m "feat(heldenian): add evacuate_map with callback for pre-war player evacuation"
```

---

### Task 6: Run full test suite and verify

**Step 1: Run all tests**

Run: `./bin/hgserver_tests`
Expected: All tests PASS, no regressions.

**Step 2: Run just heldenian tests and count**

Run: `./bin/hgserver_tests --gtest_filter="*heldenian*" --gtest_print_time=0 2>&1 | tail -5`
Expected: ~12 new tests added (2 type + 6 teleport + 4 NPC spawn + 4 NPC death + 1 evacuate = ~17 new, totaling ~50+ heldenian tests).

**Step 3: Commit if any fixups needed**

Only commit if there were fixups required. Otherwise, skip.
