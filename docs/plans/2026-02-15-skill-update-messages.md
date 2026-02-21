# Skill Update & Progress Messages Implementation Plan

> **For Claude:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** Add `skill_update` (broadcast on level change) and `skill_progress` (player-only on 5% SSN threshold) protocol messages.

**Architecture:** Wire `skill_system` callbacks into `game_handlers`. `on_level_up` broadcasts to visible players. New `on_skill_progress` callback fires when uses cross a 5% boundary, sends to owning player only. Progress tracking lives in `skill_system` via per-player per-skill `last_reported_percent`.

**Tech Stack:** C++20, nlohmann-json, GTest

---

### Task 1: Add progress tracking to skill_system

Add `on_skill_progress` callback and `last_reported_percent` tracking so the system fires a callback when SSN progress crosses a 5% threshold.

**Files:**
- Modify: `src/skill/skill_system.h`
- Modify: `src/skill/skill_system.cpp`
- Test: `tests/test_skill.cpp`

**Step 1: Write failing tests for progress callback**

Add to `tests/test_skill.cpp`:

```cpp
TEST(skill_system_test, progress_callback_fires_at_5_percent_boundary)
{
    skill_system sys;
    sys.initialize();
    player_id pid(1);
    sys.register_player(pid);

    // Default tier for level 0: uses_to_next = (0+1)*10 = 10
    // But actual formula depends on config. Let's set level explicitly.
    // With default tiers: level 0 -> uses_to_next = 1 * 10 = 10
    // Actually uses_to_next_level = (level+1) * multiplier
    // Default tiers: {20,10},{40,25},{60,50},{80,75},{90,100},{200,125}
    // Level 0 falls in first tier (max_level=20, multiplier=10): (0+1)*10 = 10
    // So 5% of 10 = 0.5 -> floor to 0. 10% = 1.
    // That's too granular. Use a higher level for meaningful test.
    sys.set_skill_level(pid, skill_type::mining, 19);
    // Level 19: (19+1)*10 = 200 uses to next level. 5% = 10 uses.

    struct progress_event
    {
        player_id player{};
        skill_type skill{};
        int32_t uses_this_level{0};
        int32_t uses_to_next_level{0};
        uint8_t percent{0};
    };

    std::vector<progress_event> events;
    sys.on_skill_progress([&](const skill::skill_progress_event& e)
    {
        events.push_back({e.player, e.skill, e.uses_this_level, e.uses_to_next_level, e.percent});
    });

    // Add 10 uses (5% of 200) - should fire at 5%
    for (int i = 0; i < 10; ++i)
        sys.record_skill_use(pid, skill_type::mining);

    ASSERT_EQ(events.size(), 1u);
    EXPECT_EQ(events[0].percent, 5);
    EXPECT_EQ(events[0].uses_this_level, 10);
    EXPECT_EQ(events[0].uses_to_next_level, 200);

    // Add 10 more (10% total) - should fire again
    for (int i = 0; i < 10; ++i)
        sys.record_skill_use(pid, skill_type::mining);

    ASSERT_EQ(events.size(), 2u);
    EXPECT_EQ(events[1].percent, 10);
}

TEST(skill_system_test, progress_callback_does_not_fire_below_threshold)
{
    skill_system sys;
    sys.initialize();
    player_id pid(1);
    sys.register_player(pid);
    sys.set_skill_level(pid, skill_type::mining, 19);
    // 200 uses to next. 5% = 10 uses.

    int fire_count = 0;
    sys.on_skill_progress([&](const skill::skill_progress_event&) { ++fire_count; });

    // Add 9 uses (4.5%, below 5% threshold) - should NOT fire
    for (int i = 0; i < 9; ++i)
        sys.record_skill_use(pid, skill_type::mining);

    EXPECT_EQ(fire_count, 0);
}

TEST(skill_system_test, progress_resets_on_level_up)
{
    skill_system sys;
    sys.initialize();
    player_id pid(1);
    sys.register_player(pid);
    sys.set_skill_level(pid, skill_type::mining, 19);
    // 200 uses to next level

    std::vector<uint8_t> percents;
    sys.on_skill_progress([&](const skill::skill_progress_event& e) { percents.push_back(e.percent); });

    int level_ups = 0;
    sys.on_level_up([&](const skill::skill_level_event&) { ++level_ups; });

    // Add enough uses to level up (200) and then some into next level
    // Level 19 -> 20: 200 uses. Level 20 -> 21: (20+1)*25 = 525 (tier changes at 20).
    // We should see progress callbacks during accumulation, then reset after level-up.
    sys.add_skill_uses(pid, skill_type::mining, 200);

    EXPECT_EQ(level_ups, 1);
    // After level-up, last_reported_percent should be reset to 0.
    // Any leftover uses in new level should NOT have triggered progress
    // (0 leftover uses since exactly 200).
}

TEST(skill_system_test, add_skill_uses_fires_progress)
{
    skill_system sys;
    sys.initialize();
    player_id pid(1);
    sys.register_player(pid);
    sys.set_skill_level(pid, skill_type::mining, 19);
    // 200 uses to next level. 5% = 10 uses.

    std::vector<uint8_t> percents;
    sys.on_skill_progress([&](const skill::skill_progress_event& e) { percents.push_back(e.percent); });

    // Bulk add 50 uses (25%) - should fire for 5, 10, 15, 20, 25
    sys.add_skill_uses(pid, skill_type::mining, 50);

    ASSERT_EQ(percents.size(), 5u);
    EXPECT_EQ(percents[0], 5);
    EXPECT_EQ(percents[1], 10);
    EXPECT_EQ(percents[2], 15);
    EXPECT_EQ(percents[3], 20);
    EXPECT_EQ(percents[4], 25);
}
```

**Step 2: Run tests to verify they fail**

Run: `cmake --build build --config Debug 2>&1 | tail -20`
Expected: Compilation errors — `skill_progress_event` and `on_skill_progress` don't exist yet.

**Step 3: Add skill_progress_event and on_skill_progress to skill_system.h**

In `src/skill/skill_system.h`, after the `skill_level_event` struct (line 32), add:

```cpp
// Skill progress event (SSN threshold crossed)
struct skill_progress_event
{
    player_id player{};
    skill_type skill{};
    int32_t uses_this_level{0};
    int32_t uses_to_next_level{0};
    uint8_t percent{0};
};
```

In the class definition, after `using level_up_callback` (line 38), add:

```cpp
using progress_callback = std::function<void(const skill_progress_event&)>;
```

After `on_level_up` declaration (line 81), add:

```cpp
void on_skill_progress(progress_callback callback);
```

In private section, after `notify_level_up` (line 88), add:

```cpp
void check_progress(player_id player, skill_type skill);
```

After `level_up_callbacks_` (line 95), add:

```cpp
std::vector<progress_callback> progress_callbacks_;
struct progress_tracking
{
    std::array<uint8_t, max_skills> last_reported_percent{};
};
std::unordered_map<player_id, progress_tracking> progress_tracking_;
```

**Step 4: Implement in skill_system.cpp**

Add `on_skill_progress`:

```cpp
void skill_system::on_skill_progress(progress_callback callback)
{
    progress_callbacks_.push_back(std::move(callback));
}
```

Add `check_progress`:

```cpp
void skill_system::check_progress(player_id player, skill_type skill)
{
    if (progress_callbacks_.empty())
        return;

    auto skills_it = player_skills_.find(player);
    if (skills_it == player_skills_.end())
        return;

    const auto& ss = skills_it->second.get(skill);
    auto next = uses_to_next_level(skill, ss.level);
    if (next <= 0)
        return;

    auto raw_percent = static_cast<uint8_t>(
        (static_cast<int64_t>(ss.uses_this_level) * 100) / next);
    auto floored = static_cast<uint8_t>((raw_percent / 5) * 5);

    auto& tracking = progress_tracking_[player];
    auto skill_idx = static_cast<size_t>(skill);
    if (floored == tracking.last_reported_percent[skill_idx])
        return;

    // Fire for each 5% step crossed (handles bulk adds)
    auto old_pct = tracking.last_reported_percent[skill_idx];
    while (old_pct + 5 <= floored)
    {
        old_pct = static_cast<uint8_t>(old_pct + 5);
        skill_progress_event event;
        event.player = player;
        event.skill = skill;
        event.uses_this_level = ss.uses_this_level;
        event.uses_to_next_level = next;
        event.percent = old_pct;

        for (const auto& cb : progress_callbacks_)
        {
            cb(event);
        }
    }
    tracking.last_reported_percent[skill_idx] = floored;
}
```

Call `check_progress` at the end of `record_skill_use()` (after the level-up check but before return), and in `add_skill_uses()` similarly. On level-up, reset the tracking: in `notify_level_up()`, add:

```cpp
// Reset progress tracking on level-up
auto tracking_it = progress_tracking_.find(player);
if (tracking_it != progress_tracking_.end())
{
    tracking_it->second.last_reported_percent[static_cast<size_t>(skill)] = 0;
}
```

Also register/unregister tracking in `register_player`/`unregister_player`, and clear in `shutdown`.

**Step 5: Run tests to verify they pass**

Run: `cmake --build build --config Debug && ./bin/hgserver_tests --gtest_filter="skill_system_test.progress*:skill_system_test.add_skill_uses*"`
Expected: All 4 new tests PASS.

**Step 6: Commit**

```
feat(skill): add progress tracking with 5% threshold callbacks
```

---

### Task 2: Add protocol message types and builders

Add `skill_update` and `skill_progress` enum entries, data structs, and builder functions.

**Files:**
- Modify: `src/network/json_protocol.h` (enum + structs + builders)
- Modify: `src/network/json_protocol.cpp` (to_string + type_map + builders)

**Step 1: Add enum entries**

In `json_protocol.h`, after `player_skill_response` (line 101), add:

```cpp
skill_update,    // Server broadcast: skill level changed
skill_progress,  // Server->client: skill SSN progress update
```

**Step 2: Add to_string cases**

In `json_protocol.cpp`, in the `to_string(json_message_type)` switch, add:

```cpp
case json_message_type::skill_update: return "skill_update";
case json_message_type::skill_progress: return "skill_progress";
```

And in the `type_map` initializer:

```cpp
{"skill_update", json_message_type::skill_update},
{"skill_progress", json_message_type::skill_progress},
```

**Step 3: Add builder functions in json_protocol.h**

After `make_skills_data` declaration (line 1750):

```cpp
[[nodiscard]] auto make_skill_update(uint32_t player_id_val,
                                     const skill_entry_msg& skill,
                                     int16_t old_level) -> json_message;

[[nodiscard]] auto make_skill_progress(uint8_t skill_id,
                                       int32_t uses_this_level,
                                       int32_t uses_to_next_level,
                                       uint8_t percent) -> json_message;
```

**Step 4: Implement builders in json_protocol.cpp**

After `make_skills_data`:

```cpp
auto make_skill_update(uint32_t player_id_val,
                       const skill_entry_msg& skill,
                       int16_t old_level) -> json_message
{
    return json_message{
        .type = json_message_type::skill_update,
        .seq = 0,
        .data = nlohmann::json{
            {"player_id", player_id_val},
            {"skill_id", skill.skill_id},
            {"old_level", old_level},
            {"level", skill.level},
            {"total_uses", skill.total_uses},
            {"uses_this_level", skill.uses_this_level},
            {"uses_to_next_level", skill.uses_to_next_level}
        }
    };
}

auto make_skill_progress(uint8_t skill_id,
                         int32_t uses_this_level,
                         int32_t uses_to_next_level,
                         uint8_t percent) -> json_message
{
    return json_message{
        .type = json_message_type::skill_progress,
        .seq = 0,
        .data = nlohmann::json{
            {"skill_id", skill_id},
            {"uses_this_level", uses_this_level},
            {"uses_to_next_level", uses_to_next_level},
            {"percent", percent}
        }
    };
}
```

**Step 5: Build to verify compilation**

Run: `cmake --build build --config Debug`
Expected: Clean build.

**Step 6: Commit**

```
feat(protocol): add skill_update and skill_progress message types
```

---

### Task 3: Wire callbacks in game_handlers

Register the `on_level_up` and `on_skill_progress` callbacks so messages are actually sent.

**Files:**
- Modify: `src/bridge/handlers/game_handlers.cpp` (register callbacks in `initialize`, add send helpers)
- Modify: `src/bridge/handlers/game_handlers_movement.cpp` (remove redundant `send_skills_data` on teleport)

**Step 1: Register callbacks in game_handlers::initialize()**

After the hunger change callback registration (line 126), add:

```cpp
// Register skill level-up callback
if (skills_ && players_ && ws_server_)
{
    skills_->on_level_up(
        [this](const skill::skill_level_event& event)
        {
            auto* plr = players_->get_player(event.player);
            if (!plr)
                return;

            auto* ps = skills_->get_player_skills(event.player);
            if (!ps)
                return;

            const auto& ss = ps->get(event.skill);
            auto skill_idx = static_cast<uint8_t>(event.skill);
            network::skill_entry_msg entry{
                .skill_id = skill_idx,
                .level = event.new_level,
                .total_uses = ss.total_uses,
                .uses_this_level = ss.uses_this_level,
                .uses_to_next_level = skills_->uses_to_next_level(event.skill, event.new_level)
            };

            auto msg = network::make_skill_update(event.player.value, entry, event.old_level);
            broadcast_to_visible(players_, ws_server_, plr->current_map, plr->pos, msg);
        });

    skills_->on_skill_progress(
        [this](const skill::skill_progress_event& event)
        {
            auto* plr = players_->get_player(event.player);
            if (!plr)
                return;

            auto* conn = ws_server_->get_connection(plr->connection);
            if (!conn || !conn->is_open())
                return;

            conn->send(network::make_skill_progress(
                static_cast<uint8_t>(event.skill),
                event.uses_this_level,
                event.uses_to_next_level,
                event.percent));
        });
}
```

**Step 2: Remove redundant send_skills_data on teleport**

In `game_handlers_movement.cpp`, remove lines 505-506:

```cpp
        // Send updated skills data
        send_skills_data(conn_id, pid);
```

The `on_level_up` callback now handles incremental updates. The full `skills_data` is still sent on login (in `auth_handlers.cpp`), which is correct.

**Step 3: Build and run existing tests**

Run: `cmake --build build --config Debug && ./bin/hgserver_tests`
Expected: All tests pass.

**Step 4: Commit**

```
feat(skill): wire skill_update and skill_progress callbacks in game_handlers
```

---

### Task 4: Document protocol messages

**Files:**
- Modify: `docs/JSON_PROTOCOL.md`

**Step 1: Add skill_update and skill_progress to the protocol documentation**

Add under the Skills section:

```markdown
### skill_update

Server → Client broadcast. Sent when a player's skill level changes.

| Field | Type | Description |
|-------|------|-------------|
| player_id | uint32 | Player whose skill changed |
| skill_id | uint8 | Skill type ID |
| old_level | int16 | Previous skill level |
| level | int16 | New skill level |
| total_uses | int32 | Lifetime uses of this skill |
| uses_this_level | int32 | Uses accumulated in current level |
| uses_to_next_level | int32 | Uses required for next level |

### skill_progress

Server → Client (owning player only). Sent when skill SSN progress crosses a 5% threshold.

| Field | Type | Description |
|-------|------|-------------|
| skill_id | uint8 | Skill type ID |
| uses_this_level | int32 | Current uses in this level |
| uses_to_next_level | int32 | Uses required for next level |
| percent | uint8 | Progress percentage (multiple of 5) |
```

**Step 2: Commit**

```
docs: add skill_update and skill_progress protocol messages
```
