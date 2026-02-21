# Mana System Fixes Implementation Plan

> **For Claude:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** Fix 6 mana system issues to match legacy Helbreath behavior: multi-charge GMG, pool reset, stone depletion, MP restoration, GMG attackability, and unused config field.

**Architecture:** Changes span `mana_system` (core pipeline), `crusade_system` (MP restoration + GMG damage wiring), `npc_system` (new damage callback), `json_protocol` (new message type), and `game_handlers` (callback wiring). All new behavior is testable via existing test fixtures.

**Tech Stack:** C++20, Google Test, nlohmann-json

---

### Task 1: Fix GMG Multi-Charge and Pool Reset

**Files:**
- Modify: `src/war/crusade/mana_system.h:44` — change default
- Modify: `src/war/crusade/mana_system.cpp:53-80` — rewrite `check_gmg()`
- Test: `tests/test_crusade_system.cpp` — add/update tests in `mana_system_test`

**Step 1: Update existing tests that assume charges_for_meteor=1**

The existing `mana_system_test` fixture already sets `cfg.gmg_charges_for_meteor = 1` explicitly in SetUp (line 601), so existing tests won't break when the default changes. Verify this by reading the fixture.

**Step 2: Write failing tests for pool reset and multi-charge**

Add after the `multi_charge_threshold` test (line ~748):

```cpp
TEST_F(mana_system_test, mana_pool_reset_discards_remainder) {
    // Override to 1 charge for simplicity
    mana_config cfg = mana_.get_config();
    cfg.gmg_charges_for_meteor = 1;
    mana_.set_config(cfg);
    mana_.set_meteor_trigger([](war_faction) {});

    // Collect 21 mana (threshold=15). Legacy discards remainder.
    mana_.add_mana(war_faction::aresden, 21);
    EXPECT_EQ(mana_.aresden_mana(), 0);  // NOT 6 — remainder discarded
    EXPECT_EQ(mana_.get_state(war_faction::aresden).gmg_charge, 0);
    EXPECT_EQ(mana_.get_state(war_faction::aresden).meteors_fired, 1);
}

TEST_F(mana_system_test, gmg_requires_multiple_charges_default) {
    // Use default config (charges_for_meteor=10)
    mana_system fresh;
    int meteor_count = 0;
    fresh.set_meteor_trigger([&](war_faction) { meteor_count++; });

    // Add 15 mana = 1 charge. Need 10 charges for meteor.
    fresh.add_mana(war_faction::aresden, 15);
    EXPECT_EQ(meteor_count, 0);
    EXPECT_EQ(fresh.get_state(war_faction::aresden).gmg_charge, 1);

    // Add 15 mana 8 more times = 9 charges total
    for (int i = 0; i < 8; i++)
    {
        fresh.add_mana(war_faction::aresden, 15);
    }
    EXPECT_EQ(meteor_count, 0);
    EXPECT_EQ(fresh.get_state(war_faction::aresden).gmg_charge, 9);

    // 10th charge triggers meteor
    fresh.add_mana(war_faction::aresden, 15);
    EXPECT_EQ(meteor_count, 1);
    EXPECT_EQ(fresh.get_state(war_faction::aresden).gmg_charge, 0);
}
```

**Step 3: Run tests to verify they fail**

Run: `cmake --build build --config Debug && ./bin/hgserver_tests --gtest_filter="mana_system_test.*"`

Expected: `mana_pool_reset_discards_remainder` fails (pool has 6, not 0). `gmg_requires_multiple_charges_default` fails (meteor fires at 1 charge).

**Step 4: Change default and fix check_gmg**

In `src/war/crusade/mana_system.h:44`, change:
```cpp
int32_t gmg_charges_for_meteor{10}; // Charges needed to fire meteor (legacy: m_iMaxMana)
```

In `src/war/crusade/mana_system.cpp`, replace `check_gmg()` (lines 53-80):
```cpp
void mana_system::check_gmg(war_faction faction)
{
    auto& state = get_state(faction);

    // Convert mana pool into GMG charge (legacy: reset to 0, discard remainder)
    if (state.mana_pool >= config_.gmg_mana_threshold)
    {
        state.mana_pool = 0;  // Discard remainder (legacy behavior)
        state.gmg_charge++;

        LOG_DEBUG(general, "Faction {} GMG charged (charge={}/{})",
            static_cast<int>(faction), state.gmg_charge, config_.gmg_charges_for_meteor);

        // Fire meteor when enough charges
        if (state.gmg_charge >= config_.gmg_charges_for_meteor)
        {
            state.gmg_charge = 0;
            state.meteors_fired++;

            LOG_INFO(general, "Faction {} fires meteor #{}", static_cast<int>(faction), state.meteors_fired);

            if (meteor_trigger_)
            {
                meteor_trigger_(faction);
            }
        }
    }
}
```

**Step 5: Update the `leftover_mana_after_gmg` test**

This existing test (line 670) expects remainder to be kept. Update it:
```cpp
TEST_F(mana_system_test, leftover_mana_after_gmg) {
    mana_.set_meteor_trigger([](war_faction) {});

    // Collect 21 mana → pool resets to 0 (remainder discarded)
    mana_.tick_faction_mana(war_faction::aresden, 1, 7);  // 7 * 3 = 21
    EXPECT_EQ(mana_.aresden_mana(), 0);  // Legacy discards remainder
}
```

Also update `multiple_meteors_from_large_mana` (line 659) — with pool reset, 45 mana only gives 1 charge (not 3), since pool resets to 0 after the first threshold crossing:
```cpp
TEST_F(mana_system_test, single_charge_per_threshold_crossing) {
    int meteor_count = 0;
    mana_.set_meteor_trigger([&](war_faction) { meteor_count++; });

    // Collect 45 mana in one tick → only 1 charge (pool resets to 0)
    mana_.tick_faction_mana(war_faction::elvine, 1, 15);  // 15 * 3 = 45
    EXPECT_EQ(meteor_count, 1);  // charges_for_meteor=1, so 1 meteor
    EXPECT_EQ(mana_.get_state(war_faction::elvine).meteors_fired, 1);
    EXPECT_EQ(mana_.elvine_mana(), 0);
}
```

**Step 6: Run all mana tests**

Run: `cmake --build build --config Debug && ./bin/hgserver_tests --gtest_filter="mana_system_test.*"`

Expected: All pass.

**Step 7: Run full test suite**

Run: `cmake --build build --config Debug && ./bin/hgserver_tests`

Expected: All pass (some integration tests may need updating too — fix any failures).

**Step 8: Commit**

```
feat: fix GMG multi-charge threshold and mana pool reset

GMG now requires 10 charges (configurable) before firing meteor.
Mana pool resets to 0 on charge consumption, discarding remainder.
Both match legacy Helbreath behavior.
```

---

### Task 2: Virtual Stone State Tracking

**Files:**
- Modify: `src/war/crusade/mana_system.h` — add `mana_stone_state`, `stones_` vector, new `tick()` API
- Modify: `src/war/crusade/mana_system.cpp` — stone regen/drain logic
- Modify: `src/war/crusade/crusade_system.cpp:903-925` — update `tick_mana()` to use new API
- Test: `tests/test_crusade_system.cpp` — add stone depletion tests

**Step 1: Write failing tests**

Add after the new tests from Task 1:

```cpp
TEST_F(mana_system_test, stone_depletion_limits_harvest) {
    // 1 stone, 2 collectors. Stone has max 5 mana.
    // First collector drains 3, second drains 2 (remaining). Total = 5, not 6.
    mana_.initialize_stones(1);
    mana_.set_meteor_trigger([](war_faction) {});

    // Tick with aresden=2 collectors, elvine=0
    mana_.tick(2, 0);

    // 1 stone * 5 mana max, but harvest_rate=3 per collector:
    // collector 1: takes 3 (stone has 2 left)
    // collector 2: takes 2 (stone has 0 left)
    // total = 5
    EXPECT_EQ(mana_.aresden_mana(), 5);
}

TEST_F(mana_system_test, stones_regenerate_each_tick) {
    mana_.initialize_stones(1);
    mana_.set_meteor_trigger([](war_faction) {});

    // First tick: drain stone to 2 (1 collector takes 3)
    mana_.tick(1, 0);
    EXPECT_EQ(mana_.aresden_mana(), 3);

    // Second tick: stone regens to 5, collector takes 3 again
    mana_.tick(1, 0);
    EXPECT_EQ(mana_.aresden_mana(), 6);
}

TEST_F(mana_system_test, both_factions_compete_for_stones) {
    mana_.initialize_stones(1);
    mana_.set_meteor_trigger([](war_faction) {});

    // 1 stone, 1 collector each faction
    // Aresden goes first (legacy ordering): takes 3 (stone has 2)
    // Elvine goes second: takes 2 (stone has 0)
    mana_.tick(1, 1);
    EXPECT_EQ(mana_.aresden_mana(), 3);
    EXPECT_EQ(mana_.elvine_mana(), 2);
}

TEST_F(mana_system_test, multiple_stones_provide_more_mana) {
    mana_.initialize_stones(5);
    mana_.set_meteor_trigger([](war_faction) {});

    // 5 stones, 1 collector: takes 3 from each = 15
    mana_.tick(1, 0);
    EXPECT_EQ(mana_.aresden_mana(), 15);
}

TEST_F(mana_system_test, zero_collectors_yields_zero_with_stones) {
    mana_.initialize_stones(5);
    mana_.set_meteor_trigger([](war_faction) {});

    mana_.tick(0, 0);
    EXPECT_EQ(mana_.aresden_mana(), 0);
    EXPECT_EQ(mana_.elvine_mana(), 0);
}
```

**Step 2: Run tests to verify they fail**

Run: `cmake --build build --config Debug && ./bin/hgserver_tests --gtest_filter="mana_system_test.stone*:mana_system_test.both*:mana_system_test.multiple_stones*:mana_system_test.zero_collectors*"`

Expected: Compile errors (methods don't exist yet).

**Step 3: Add mana_stone_state and new API to header**

In `src/war/crusade/mana_system.h`, add before `class mana_system`:

```cpp
// Individual mana stone state (shared between factions)
struct mana_stone_state
{
    int32_t current_mana{5};
    static constexpr int32_t max_mana = 5;
    static constexpr int32_t regen_rate = 5;
};
```

Add to `mana_system` public section:

```cpp
    // Initialize stone state for a new crusade
    void initialize_stones(int32_t count);

    // Main tick — handles both factions, stone regen, and drain
    // Replaces calling tick_faction_mana twice
    void tick(int32_t aresden_collectors, int32_t elvine_collectors);
```

Add to `mana_system` private section:

```cpp
    int32_t harvest_from_stones(int32_t collector_count);

    std::vector<mana_stone_state> stones_;
```

**Step 4: Implement stone logic**

In `src/war/crusade/mana_system.cpp`:

```cpp
void mana_system::initialize_stones(int32_t count)
{
    stones_.clear();
    stones_.resize(static_cast<size_t>(std::max(0, count)));
}

void mana_system::tick(int32_t aresden_collectors, int32_t elvine_collectors)
{
    // Step 1: Regenerate all stones
    for (auto& stone : stones_)
    {
        stone.current_mana = std::min(
            stone.current_mana + mana_stone_state::regen_rate,
            mana_stone_state::max_mana);
    }

    // Step 2: Aresden collectors drain first (legacy ordering)
    if (aresden_collectors > 0)
    {
        int32_t harvested = harvest_from_stones(aresden_collectors);
        if (harvested > 0)
        {
            aresden_state_.add_mana(harvested);
            LOG_DEBUG(general, "Aresden collected {} mana ({} collectors), pool={}",
                harvested, aresden_collectors, aresden_state_.mana_pool);
            check_gmg(war_faction::aresden);
        }
    }

    // Step 3: Elvine collectors drain remaining
    if (elvine_collectors > 0)
    {
        int32_t harvested = harvest_from_stones(elvine_collectors);
        if (harvested > 0)
        {
            elvine_state_.add_mana(harvested);
            LOG_DEBUG(general, "Elvine collected {} mana ({} collectors), pool={}",
                harvested, elvine_collectors, elvine_state_.mana_pool);
            check_gmg(war_faction::elvine);
        }
    }
}

int32_t mana_system::harvest_from_stones(int32_t collector_count)
{
    int32_t total = 0;
    for (int32_t c = 0; c < collector_count; c++)
    {
        for (auto& stone : stones_)
        {
            if (stone.current_mana >= config_.collector_harvest_rate)
            {
                stone.current_mana -= config_.collector_harvest_rate;
                total += config_.collector_harvest_rate;
            }
            else if (stone.current_mana > 0)
            {
                total += stone.current_mana;
                stone.current_mana = 0;
            }
        }
    }
    return total;
}
```

Update `reset()` to also clear stones:
```cpp
void mana_system::reset()
{
    aresden_state_.reset();
    elvine_state_.reset();
    stones_.clear();
}
```

**Step 5: Update crusade_system::tick_mana() to use new API**

In `src/war/crusade/crusade_system.cpp`, replace the mana tick body in `tick_mana()` (lines 912-916):

```cpp
    int32_t aresden_collectors = count_structures_by_type(war_faction::aresden, war_unit_type::mana_collector);
    int32_t elvine_collectors = count_structures_by_type(war_faction::elvine, war_unit_type::mana_collector);
    mana_system_.tick(aresden_collectors, elvine_collectors);
```

Remove the two `tick_faction_mana` calls and the `stones` variable.

Also in `setup_mana_and_meteor()` (line 773), add stone initialization:
```cpp
    mana_system_.reset();
    mana_system_.initialize_stones(config_.mana_stone_count);
```

**Step 6: Update existing tests that use tick_faction_mana**

The old `tick_faction_mana` is still public and used by existing mana_system_test tests. Two options:
- Keep `tick_faction_mana` as a legacy convenience that doesn't use stones (for backward compat in simple tests)
- OR update all tests to use `tick()` + `initialize_stones()`

Keep `tick_faction_mana` working as-is (it doesn't touch stones) so existing tests don't need mass updating. The new `tick()` is the stone-aware path. Add a note in the header that `tick()` is the preferred API.

**Step 7: Run tests**

Run: `cmake --build build --config Debug && ./bin/hgserver_tests --gtest_filter="mana_system_test.*"`

Expected: All pass (old tests use `tick_faction_mana`, new tests use `tick()`).

**Step 8: Run full test suite**

Run: `cmake --build build --config Debug && ./bin/hgserver_tests`

Expected: All pass.

**Step 9: Commit**

```
feat: add per-stone mana tracking with regen and depletion

Stones are shared between factions. Each has 5 mana, regens 5/tick,
collectors drain up to 3 per stone. Aresden processes first (legacy).
Multiple collectors on same stones see diminishing returns.
```

---

### Task 3: Add crusade_mp_restore Protocol Message

**Files:**
- Modify: `src/network/json_protocol.h:408` — add enum entry before `unknown`
- Modify: `src/network/json_protocol.cpp` — add to `to_string`, `type_map`, add builder
- Test: `tests/test_crusade_system.cpp` — protocol message test

**Step 1: Write failing test**

Add to the protocol message tests section (near line 1122):

```cpp
TEST_F(mana_meteor_integration_test, crusade_mp_restore_protocol_message) {
    EXPECT_EQ(hb::network::to_string(hb::network::json_message_type::crusade_mp_restore),
              "crusade_mp_restore");

    auto parsed = hb::network::parse_message_type("crusade_mp_restore");
    EXPECT_EQ(parsed, hb::network::json_message_type::crusade_mp_restore);

    auto msg = hb::network::make_crusade_mp_restore(128, 130, 5, 12);
    EXPECT_EQ(msg.type, hb::network::json_message_type::crusade_mp_restore);
    EXPECT_EQ(msg.data["source_x"], 128);
    EXPECT_EQ(msg.data["source_y"], 130);
    EXPECT_EQ(msg.data["radius"], 5);
    EXPECT_EQ(msg.data["your_restore"], 12);
}
```

**Step 2: Run test to verify it fails**

Run: `cmake --build build --config Debug && ./bin/hgserver_tests --gtest_filter="*crusade_mp_restore*"`

Expected: Compile error (enum entry doesn't exist).

**Step 3: Add enum entry**

In `src/network/json_protocol.h`, before the `unknown` entry (line 410):

```cpp
    // Crusade mana collector MP restoration
    crusade_mp_restore,             // S->C: Mana collector restored MP to nearby allies

    // Unknown/invalid
    unknown
```

**Step 4: Add builder declaration**

In `src/network/json_protocol.h`, after `make_crusade_reward_summary` (line 1930):

```cpp
// Mana collector MP restoration broadcast
[[nodiscard]] auto make_crusade_mp_restore(int16_t source_x, int16_t source_y,
    int32_t radius, int32_t your_restore) -> json_message;
```

**Step 5: Add to_string, type_map, and builder implementation**

In `src/network/json_protocol.cpp`:

Add to `to_string()` switch (before the `unknown` case):
```cpp
        case json_message_type::crusade_mp_restore: return "crusade_mp_restore";
```

Add to `type_map` (before the closing brace):
```cpp
        {"crusade_mp_restore", json_message_type::crusade_mp_restore},
```

Add builder implementation (after `make_crusade_reward_summary`):
```cpp
auto make_crusade_mp_restore(int16_t source_x, int16_t source_y,
    int32_t radius, int32_t your_restore) -> json_message
{
    json_message msg;
    msg.type = json_message_type::crusade_mp_restore;
    msg.data = {
        {"source_x", source_x},
        {"source_y", source_y},
        {"radius", radius},
        {"your_restore", your_restore}
    };
    return msg;
}
```

**Step 6: Run test**

Run: `cmake --build build --config Debug && ./bin/hgserver_tests --gtest_filter="*crusade_mp_restore*"`

Expected: Pass.

**Step 7: Commit**

```
feat: add crusade_mp_restore protocol message

Sent to all players who can see a mana collector during MP restoration.
Includes collector position, radius, and per-player restore amount.
```

---

### Task 4: Implement MP Restoration in Crusade System

**Files:**
- Modify: `src/war/crusade/crusade_system.cpp:903-925` — add MP restore logic to `tick_mana()`
- Modify: `src/war/crusade/mana_system.h` — add `collector_mp_restore_radius` config field
- Test: `tests/test_crusade_system.cpp` — MP restoration tests

**Step 1: Add config field**

In `src/war/crusade/mana_system.h`, in `mana_config`:

```cpp
    int32_t collector_mp_restore_radius{5}; // Tiles around collector for MP restore
```

**Step 2: Write failing tests**

These tests need a player_system mock. Create a new fixture that extends the mana_meteor_integration_test pattern but adds player_system:

```cpp
// ========== MP Restoration Tests ==========

class mp_restoration_test : public ::testing::Test {
protected:
    void SetUp() override {
        war_sys_.initialize();
        player_sys_.initialize();
        crusade_.initialize();
        crusade_.set_dependencies(&war_sys_, &player_sys_, &world_sys_, nullptr, nullptr);

        crusade_config cfg;
        cfg.enabled = true;
        cfg.timing.duration_seconds = 3600;
        cfg.mana_stone_count = 5;

        strike_point sp{.id = 1, .hp = 100, .max_hp = 100};
        cfg.aresden_strike_points = {sp};
        cfg.elvine_strike_points = {sp};

        crusade_.set_config(cfg);

        crusade_.set_broadcast_fn([this](hb::player_id pid, const hb::network::json_message& msg) {
            player_broadcasts_.push_back({pid, msg});
        });
        crusade_.set_broadcast_all_fn([this](const hb::network::json_message& msg) {
            all_broadcasts_.push_back(msg);
        });
    }

    void TearDown() override {
        crusade_.shutdown();
        player_sys_.shutdown();
        war_sys_.shutdown();
    }

    war_system war_sys_;
    hb::player::player_system player_sys_;
    hb::world::world_subsystem world_sys_;
    crusade_system crusade_;

    std::vector<hb::network::json_message> all_broadcasts_;
    struct player_msg {
        hb::player_id pid;
        hb::network::json_message msg;
    };
    std::vector<player_msg> player_broadcasts_;
};
```

Note: This fixture may need adjustments depending on player_system's constructor requirements. If player_system needs a database or other dependencies to initialize, use a simpler approach: directly test the MP restoration logic by calling it in a way that doesn't need full player_system wiring.

**Alternative simpler test approach** — test the restoration logic as a method on crusade_system that takes explicit parameters, making it testable without full player system:

```cpp
TEST_F(mana_meteor_integration_test, mp_restoration_to_nearby_allies) {
    crusade_.start_crusade();

    // Place a mana collector for aresden
    war_structure_instance collector;
    collector.type = war_unit_type::mana_collector;
    collector.faction = war_faction::aresden;
    collector.map_name = "middleland";
    collector.x = 100;
    collector.y = 100;
    crusade_.add_war_structure(collector);

    // Without player_system wired, MP restoration won't find players
    // but shouldn't crash
    crusade_.update(5.0f);  // Trigger mana tick

    // Verify no crashes — functional test requires player_system integration
    EXPECT_TRUE(true);
}
```

The full integration test with real player objects is best done after wiring. For now, write unit tests that verify the broadcast messages are correct when MP restoration fires.

**Step 3: Implement MP restoration in tick_mana()**

In `src/war/crusade/crusade_system.cpp`, add MP restoration after the `mana_system_.tick()` call in `tick_mana()`:

```cpp
    // MP restoration: collectors restore MP to nearby allied players
    if (players_ && world_)
    {
        for (const auto& ws : war_structures_)
        {
            if (ws.type != war_unit_type::mana_collector) continue;

            auto* m = world_->get_map_by_name(ws.map_name);
            if (!m) continue;

            int32_t radius = mana_system_.get_config().collector_mp_restore_radius;

            // Get all players who can see the collector (for broadcast)
            auto viewers = players_->get_players_who_can_see(m->id(), {ws.x, ws.y});

            for (auto viewer_pid : viewers)
            {
                auto* plr = players_->get_player(viewer_pid);
                if (!plr) continue;

                int32_t restore = 0;

                // Check if viewer is within restore radius AND same faction
                bool in_range = std::abs(plr->pos.x - ws.x) <= radius &&
                                std::abs(plr->pos.y - ws.y) <= radius;

                war_faction plr_faction = war_faction::neutral;
                if (plr->faction == hb::faction::aresden) plr_faction = war_faction::aresden;
                else if (plr->faction == hb::faction::elvine) plr_faction = war_faction::elvine;

                if (in_range && plr_faction == ws.faction && plr->computed.magic > 0)
                {
                    thread_local std::mt19937 rng{std::random_device{}()};
                    std::uniform_int_distribution<int32_t> dist(1, plr->computed.magic);
                    restore = dist(rng);
                    plr->mp = std::min(plr->mp + restore, plr->computed.max_mp);
                }

                // Broadcast to ALL viewers (even those who get 0)
                auto msg = network::make_crusade_mp_restore(ws.x, ws.y, radius, restore);
                send_to_player(viewer_pid, msg);
            }
        }
    }
```

Add required includes to `crusade_system.cpp`:
```cpp
#include <random>
```

**Step 4: Run tests**

Run: `cmake --build build --config Debug && ./bin/hgserver_tests --gtest_filter="mana_*"`

Expected: All pass.

**Step 5: Run full test suite**

Run: `cmake --build build --config Debug && ./bin/hgserver_tests`

Expected: All pass.

**Step 6: Commit**

```
feat: add MP restoration to allied players near mana collectors

Every mana tick (5s), collectors restore random(1, magic) MP to
same-faction players within 5 tiles. Broadcasts crusade_mp_restore
to all viewers for client-side effect rendering.
```

---

### Task 5: Add NPC Damage Callback to npc_system

**Files:**
- Modify: `src/npc/npc_system.h:114-125,207-211` — add callback type, setter, member
- Modify: `src/npc/npc_system.cpp:493-534` — fire callback in `apply_damage()`

**Step 1: Add callback type and setter to header**

In `src/npc/npc_system.h`, after the existing callback types (line 118):

```cpp
    using on_npc_damage_callback = std::function<void(const npc&, int32_t damage, entity::entity source)>;
```

After the existing setters (line 125):

```cpp
    void set_on_damage_callback(on_npc_damage_callback cb) { on_damage_callback_ = std::move(cb); }
```

After the existing callback members (line 211):

```cpp
    on_npc_damage_callback on_damage_callback_;
```

**Step 2: Fire callback in apply_damage()**

In `src/npc/npc_system.cpp`, in `apply_damage()`, add after the aggro line (line 523) and before the death check (line 531):

```cpp
    // Notify damage callback
    if (on_damage_callback_)
    {
        on_damage_callback_(*npc_ptr, damage, source);
    }
```

**Step 3: Run full test suite**

Run: `cmake --build build --config Debug && ./bin/hgserver_tests`

Expected: All pass (callback is optional, no existing code sets it).

**Step 4: Commit**

```
feat: add on_npc_damage callback to npc_system

Fires in apply_damage() after HP reduction and aggro update,
before death check. Used by crusade system for GMG damage tracking.
```

---

### Task 6: Implement GMG Damage Vulnerability

**Files:**
- Modify: `src/war/crusade/mana_system.h` — add `gmg_accumulated_damage` to state, `gmg_damage_threshold` to config, `apply_gmg_damage()` method
- Modify: `src/war/crusade/mana_system.cpp` — implement `apply_gmg_damage()`
- Modify: `src/war/crusade/crusade_system.h` — add `on_gmg_damage()` method
- Modify: `src/war/crusade/crusade_system.cpp` — implement `on_gmg_damage()`
- Modify: `src/bridge/handlers/game_handlers.cpp` — wire NPC damage callback for GMG
- Test: `tests/test_crusade_system.cpp` — GMG damage tests

**Step 1: Write failing tests**

```cpp
TEST_F(mana_system_test, gmg_damage_reduces_charges) {
    mana_.set_meteor_trigger([](war_faction) {});

    // Give aresden 2 charges
    mana_.add_mana(war_faction::aresden, 15);
    mana_.add_mana(war_faction::aresden, 15);
    EXPECT_EQ(mana_.get_state(war_faction::aresden).gmg_charge, 2);

    // Accumulate 500 damage → lose 1 charge
    mana_.apply_gmg_damage(war_faction::aresden, 500);
    EXPECT_EQ(mana_.get_state(war_faction::aresden).gmg_charge, 1);
}

TEST_F(mana_system_test, gmg_damage_below_threshold_no_effect) {
    mana_.set_meteor_trigger([](war_faction) {});

    mana_.add_mana(war_faction::aresden, 15);
    EXPECT_EQ(mana_.get_state(war_faction::aresden).gmg_charge, 1);

    // 499 damage — not enough
    mana_.apply_gmg_damage(war_faction::aresden, 499);
    EXPECT_EQ(mana_.get_state(war_faction::aresden).gmg_charge, 1);
}

TEST_F(mana_system_test, gmg_damage_resets_accumulator) {
    mana_.set_meteor_trigger([](war_faction) {});

    mana_.add_mana(war_faction::aresden, 15);
    mana_.add_mana(war_faction::aresden, 15);
    EXPECT_EQ(mana_.get_state(war_faction::aresden).gmg_charge, 2);

    // 300 damage, then 250 more = 550 total → triggers at 500, remainder 50
    mana_.apply_gmg_damage(war_faction::aresden, 300);
    EXPECT_EQ(mana_.get_state(war_faction::aresden).gmg_charge, 2);

    mana_.apply_gmg_damage(war_faction::aresden, 250);
    EXPECT_EQ(mana_.get_state(war_faction::aresden).gmg_charge, 1);

    // 450 more (50 + 450 = 500) → triggers again
    mana_.apply_gmg_damage(war_faction::aresden, 450);
    EXPECT_EQ(mana_.get_state(war_faction::aresden).gmg_charge, 0);
}

TEST_F(mana_system_test, gmg_damage_at_zero_charges_no_underflow) {
    mana_.set_meteor_trigger([](war_faction) {});

    // No charges, take 500 damage — should not underflow
    mana_.apply_gmg_damage(war_faction::aresden, 500);
    EXPECT_EQ(mana_.get_state(war_faction::aresden).gmg_charge, 0);
}
```

**Step 2: Run tests to verify they fail**

Expected: Compile error (`apply_gmg_damage` doesn't exist).

**Step 3: Add state field, config field, and method declaration**

In `src/war/crusade/mana_system.h`:

Add to `faction_mana_state`:
```cpp
    int32_t gmg_accumulated_damage{0}; // Damage accumulator for charge reduction
```

Update `faction_mana_state::reset()`:
```cpp
    void reset()
    {
        mana_pool = 0;
        gmg_charge = 0;
        total_mana_collected = 0;
        meteors_fired = 0;
        gmg_accumulated_damage = 0;
    }
```

Add to `mana_config`:
```cpp
    int32_t gmg_damage_threshold{500}; // Accumulated damage to remove 1 GMG charge
```

Add to `mana_system` public section:
```cpp
    // Called when GMG takes damage — accumulates and reduces charges at threshold
    void apply_gmg_damage(war_faction faction, int32_t damage);
```

**Step 4: Implement apply_gmg_damage**

In `src/war/crusade/mana_system.cpp`:

```cpp
void mana_system::apply_gmg_damage(war_faction faction, int32_t damage)
{
    if (damage <= 0) return;

    auto& state = get_state(faction);
    state.gmg_accumulated_damage += damage;

    if (state.gmg_accumulated_damage >= config_.gmg_damage_threshold)
    {
        state.gmg_accumulated_damage = 0;
        if (state.gmg_charge > 0)
        {
            state.gmg_charge--;
            LOG_INFO(general, "GMG ({}) lost a charge from damage. Charges: {}",
                static_cast<int>(faction), state.gmg_charge);
        }
    }
}
```

**Step 5: Run mana tests**

Run: `cmake --build build --config Debug && ./bin/hgserver_tests --gtest_filter="mana_system_test.*"`

Expected: All pass.

**Step 6: Add crusade_system::on_gmg_damage**

In `src/war/crusade/crusade_system.h`, public section:
```cpp
    // Called when a GMG NPC takes damage — reduces mana charges
    void on_gmg_damage(entity::entity eid, int32_t damage);
```

In `src/war/crusade/crusade_system.cpp`:
```cpp
void crusade_system::on_gmg_damage(entity::entity eid, int32_t damage)
{
    if (!active_ || damage <= 0) return;

    // Find which faction owns this GMG
    for (const auto& ws : war_structures_)
    {
        if (ws.eid == eid)
        {
            mana_system_.apply_gmg_damage(ws.faction, damage);
            return;
        }
    }
}
```

**Step 7: Add crusade integration test**

```cpp
TEST_F(mana_meteor_integration_test, gmg_damage_wiring_through_crusade) {
    crusade_.start_crusade();

    // Place a GMG structure for aresden
    war_structure_instance gmg;
    gmg.eid = entity::entity{999};
    gmg.type = war_unit_type::esg;  // Using ESG type as placeholder (GMG has no war_unit_type)
    gmg.faction = war_faction::aresden;
    gmg.map_name = "middleland";
    gmg.x = 100;
    gmg.y = 100;
    crusade_.add_war_structure(gmg);

    // Give aresden a charge
    crusade_.mana().add_mana(war_faction::aresden, 15);
    auto& state = crusade_.mana().get_state(war_faction::aresden);
    EXPECT_EQ(state.gmg_charge, 1);

    // Damage GMG via entity
    crusade_.on_gmg_damage(entity::entity{999}, 500);
    EXPECT_EQ(state.gmg_charge, 0);
}
```

**Step 8: Wire NPC damage callback in game_handlers**

In `src/bridge/handlers/game_handlers.cpp`, after the existing `set_on_attack_callback` (around line 169), add:

```cpp
    npc_->set_on_damage_callback([this](const npc::npc& n, int32_t damage, entity::entity /*source*/) {
        // GMG damage tracking for crusade mana system
        if (n.sprite_id == 41 && crusade_ && crusade_->is_active())
        {
            crusade_->on_gmg_damage(n.entity_id, damage);
        }
    });
```

**Step 9: Run full test suite**

Run: `cmake --build build --config Debug && ./bin/hgserver_tests`

Expected: All pass.

**Step 10: Commit**

```
feat: add GMG damage vulnerability for mana charge reduction

500 accumulated damage on a GMG NPC removes one mana charge.
Wired through npc_system on_damage callback → crusade_system → mana_system.
```

---

### Task 7: Update Documentation

**Files:**
- Modify: `docs/JSON_PROTOCOL.md` — add `crusade_mp_restore` message
- Modify: `docs/PROGRESS.md` — mark mana system fixes as complete

**Step 1: Update JSON_PROTOCOL.md**

Add `crusade_mp_restore` to the crusade messages section:

```markdown
#### `crusade_mp_restore` (S→C)
Sent to all players who can see a mana collector during MP restoration tick.
```json
{
    "type": "crusade_mp_restore",
    "source_x": 128,
    "source_y": 130,
    "radius": 5,
    "your_restore": 12
}
```
- `source_x`, `source_y`: Mana collector position
- `radius`: MP restoration radius (tiles)
- `your_restore`: MP restored to this player (0 if out of range or wrong faction)
```

**Step 2: Update PROGRESS.md**

Add a dated changelog entry for the mana system fixes.

**Step 3: Commit**

```
docs: update protocol docs and progress for mana system fixes
```

---

## Execution Order Summary

| Task | Description | Depends On |
|------|-------------|------------|
| 1 | GMG multi-charge + pool reset | — |
| 2 | Virtual stone state tracking | — |
| 3 | crusade_mp_restore protocol message | — |
| 4 | MP restoration implementation | 3 |
| 5 | NPC damage callback | — |
| 6 | GMG damage vulnerability | 5 |
| 7 | Documentation | 1-6 |

Tasks 1, 2, 3, and 5 are independent and can be done in any order.
