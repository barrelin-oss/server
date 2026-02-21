# Crusade Reward Formula Implementation Plan

> **For Claude:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** Replace the generic war reward formula with the legacy crusade-specific formula and add deferred reward delivery for offline players.

**Architecture:** Pure reward calculation function in crusade_types.h, wired into end_crusade(). Deferred rewards use existing war_participants table with a new `reward_claimed` column. Login hook in auth_handlers queries unclaimed rewards and applies them via player_system::add_experience().

**Tech Stack:** C++20, PostgreSQL, GTest

---

### Task 1: Add `crusade_reward` struct and `calculate_crusade_reward()` function

**Files:**
- Modify: `src/war/crusade/crusade_types.h:129` (after `max_war_contribution`)
- Test: `tests/test_crusade_system.cpp`

**Step 1: Write the failing tests**

Add to `tests/test_crusade_system.cpp` at the end of the file:

```cpp
// ========== Crusade Reward Formula Tests ==========

TEST(crusade_reward_test, winner_level_50_full_contribution) {
    // Level 50 (bracket <=80): bonus = 50 * 100 = 5000
    // Winner gets full adjusted contribution: 1000 + 5000 = 6000
    auto reward = calculate_crusade_reward(1000, 50, war_faction::aresden, war_faction::aresden);
    EXPECT_EQ(reward.experience, 6000);
    EXPECT_TRUE(reward.is_winner);
    EXPECT_FALSE(reward.is_draw);
    EXPECT_EQ(reward.war_contribution_used, 1000);
}

TEST(crusade_reward_test, loser_level_50_tenth_contribution) {
    // Level 50: bonus = 5000, adjusted = 6000
    // Loser gets 1/10: 6000 / 10 = 600
    auto reward = calculate_crusade_reward(1000, 50, war_faction::elvine, war_faction::aresden);
    EXPECT_EQ(reward.experience, 600);
    EXPECT_FALSE(reward.is_winner);
    EXPECT_FALSE(reward.is_draw);
}

TEST(crusade_reward_test, draw_level_50_sixth_contribution) {
    // Level 50: bonus = 5000, adjusted = 6000
    // Draw gets 1/6: 6000 / 6 = 1000
    auto reward = calculate_crusade_reward(1000, 50, war_faction::aresden, war_faction::neutral);
    EXPECT_EQ(reward.experience, 1000);
    EXPECT_FALSE(reward.is_winner);
    EXPECT_TRUE(reward.is_draw);
}

TEST(crusade_reward_test, level_90_mid_bracket) {
    // Level 90 (bracket 81-100): bonus = 90 * 40 = 3600
    // Winner: 1000 + 3600 = 4600
    auto reward = calculate_crusade_reward(1000, 90, war_faction::aresden, war_faction::aresden);
    EXPECT_EQ(reward.experience, 4600);
    EXPECT_TRUE(reward.is_winner);
}

TEST(crusade_reward_test, level_120_high_bracket) {
    // Level 120 (bracket 101+): bonus = 120 * 1 = 120
    // Winner: 1000 + 120 = 1120
    auto reward = calculate_crusade_reward(1000, 120, war_faction::aresden, war_faction::aresden);
    EXPECT_EQ(reward.experience, 1120);
    EXPECT_TRUE(reward.is_winner);
}

TEST(crusade_reward_test, level_80_boundary) {
    // Level 80 is still in first bracket: bonus = 80 * 100 = 8000
    auto reward = calculate_crusade_reward(500, 80, war_faction::aresden, war_faction::aresden);
    EXPECT_EQ(reward.experience, 8500);
}

TEST(crusade_reward_test, level_81_boundary) {
    // Level 81 enters second bracket: bonus = 81 * 40 = 3240
    auto reward = calculate_crusade_reward(500, 81, war_faction::aresden, war_faction::aresden);
    EXPECT_EQ(reward.experience, 3740);
}

TEST(crusade_reward_test, level_100_boundary) {
    // Level 100 is still in second bracket: bonus = 100 * 40 = 4000
    auto reward = calculate_crusade_reward(500, 100, war_faction::aresden, war_faction::aresden);
    EXPECT_EQ(reward.experience, 4500);
}

TEST(crusade_reward_test, level_101_boundary) {
    // Level 101 enters third bracket: bonus = 101 * 1 = 101
    auto reward = calculate_crusade_reward(500, 101, war_faction::aresden, war_faction::aresden);
    EXPECT_EQ(reward.experience, 601);
}

TEST(crusade_reward_test, zero_contribution) {
    // Level 50: bonus = 5000, adjusted = 5000
    // Winner: 5000
    auto reward = calculate_crusade_reward(0, 50, war_faction::aresden, war_faction::aresden);
    EXPECT_EQ(reward.experience, 5000);
}

TEST(crusade_reward_test, zero_level) {
    // Level 0 (edge case): bonus = 0, adjusted = 1000
    auto reward = calculate_crusade_reward(1000, 0, war_faction::aresden, war_faction::aresden);
    EXPECT_EQ(reward.experience, 1000);
}

TEST(crusade_reward_test, no_gold_ever) {
    // The crusade_reward struct has no gold field — gold is always 0 in the protocol message
    auto reward = calculate_crusade_reward(10000, 80, war_faction::aresden, war_faction::aresden);
    // Just verify it doesn't crash and gives EXP only
    EXPECT_GT(reward.experience, 0);
}
```

**Step 2: Run tests to verify they fail**

Run: `cmake --build build --config Debug && ./bin/hgserver_tests --gtest_filter="crusade_reward_test.*"`
Expected: FAIL — `calculate_crusade_reward` not defined

**Step 3: Write the implementation**

Add to `src/war/crusade/crusade_types.h` after line 129 (after `max_war_contribution`):

```cpp
// Crusade reward calculation result
struct crusade_reward
{
    int64_t experience{0};
    int32_t war_contribution_used{0};
    bool is_winner{false};
    bool is_draw{false};
};

// Calculate crusade reward using legacy formula.
// Level bonus is added to contribution before EXP calculation.
// Winner: full adjusted contribution. Draw: 1/6. Loser: 1/10.
inline auto calculate_crusade_reward(int32_t war_contribution, int32_t player_level,
    war_faction player_faction, war_faction winner) -> crusade_reward
{
    crusade_reward reward;
    reward.war_contribution_used = war_contribution;

    int64_t contribution = war_contribution;

    // Level-based bonus (legacy formula)
    if (player_level <= 80)
        contribution += static_cast<int64_t>(player_level) * 100;
    else if (player_level <= 100)
        contribution += static_cast<int64_t>(player_level) * 40;
    else
        contribution += static_cast<int64_t>(player_level) * 1;

    if (winner == war_faction::neutral)
    {
        // Draw: 1/6 of adjusted contribution
        reward.experience = contribution / 6;
        reward.is_draw = true;
    }
    else if (winner == player_faction)
    {
        // Winner: full adjusted contribution
        reward.experience = contribution;
        reward.is_winner = true;
    }
    else
    {
        // Loser: 1/10 of adjusted contribution
        reward.experience = contribution / 10;
    }

    return reward;
}
```

**Step 4: Run tests to verify they pass**

Run: `cmake --build build --config Debug && ./bin/hgserver_tests --gtest_filter="crusade_reward_test.*"`
Expected: All 12 tests PASS

**Step 5: Commit**

```bash
git add src/war/crusade/crusade_types.h tests/test_crusade_system.cpp
git commit -m "Add crusade reward formula with legacy level-based bonus"
```

---

### Task 2: Update `end_crusade()` to use new formula and apply EXP

**Files:**
- Modify: `src/war/crusade/crusade_system.cpp:194-234` (end_crusade function)
- Test: `tests/test_crusade_system.cpp`

**Step 1: Write the failing test**

Add to `tests/test_crusade_system.cpp`:

```cpp
// Test that end_crusade sends correct reward data via broadcast
TEST_F(crusade_system_test, end_crusade_sends_legacy_reward) {
    // Set up player system for level lookup
    hb::player::player_system player_sys;
    player_sys.initialize();
    crusade_.set_dependencies(&war_sys_, &player_sys, nullptr, nullptr, nullptr);

    auto result = crusade_.start_crusade();
    ASSERT_TRUE(result.is_ok());

    // Create a player with level 50
    auto pid = player_sys.create_player();
    auto* plr = player_sys.get_player(pid);
    ASSERT_NE(plr, nullptr);
    plr->experience.level = 50;
    plr->name = "TestPlayer";

    // Join crusade and accumulate contribution
    crusade_.join_crusade(pid, war_faction::aresden);
    crusade_.award_contribution(pid, 1000);

    // Capture broadcast
    network::json_message captured_msg{};
    bool got_reward = false;
    crusade_.set_broadcast_fn([&](player_id target_pid, const network::json_message& msg) {
        if (msg.type == network::json_message_type::crusade_reward_summary) {
            captured_msg = msg;
            got_reward = true;
        }
    });

    // End crusade with aresden winning
    crusade_.end_crusade(war_faction::aresden);

    ASSERT_TRUE(got_reward);
    // Level 50, 1000 contribution: exp = 1000 + 5000 = 6000
    EXPECT_EQ(captured_msg.data["reward_exp"].get<int64_t>(), 6000);
    EXPECT_EQ(captured_msg.data["reward_gold"].get<int64_t>(), 0);
    EXPECT_EQ(captured_msg.data["contribution"].get<int32_t>(), 1000);

    player_sys.shutdown();
}

TEST_F(crusade_system_test, end_crusade_applies_exp_to_online_player) {
    hb::player::player_system player_sys;
    player_sys.initialize();
    crusade_.set_dependencies(&war_sys_, &player_sys, nullptr, nullptr, nullptr);

    auto result = crusade_.start_crusade();
    ASSERT_TRUE(result.is_ok());

    auto pid = player_sys.create_player();
    auto* plr = player_sys.get_player(pid);
    ASSERT_NE(plr, nullptr);
    plr->experience.level = 50;
    plr->name = "TestPlayer";

    int64_t exp_before = plr->experience.experience;

    crusade_.join_crusade(pid, war_faction::aresden);
    crusade_.award_contribution(pid, 1000);

    // Suppress broadcast errors
    crusade_.set_broadcast_fn([](player_id, const network::json_message&) {});

    crusade_.end_crusade(war_faction::aresden);

    // Check EXP was added (6000 for level 50 winner with 1000 contribution)
    EXPECT_EQ(plr->experience.experience, exp_before + 6000);

    player_sys.shutdown();
}
```

**Step 2: Run tests to verify they fail**

Run: `cmake --build build --config Debug && ./bin/hgserver_tests --gtest_filter="crusade_system_test.end_crusade_sends_legacy_reward:crusade_system_test.end_crusade_applies_exp_to_online_player"`
Expected: FAIL — still uses old formula

**Step 3: Update `end_crusade()` implementation**

In `src/war/crusade/crusade_system.cpp`, replace lines 212-224 (the reward loop inside `end_crusade`):

Replace:
```cpp
    // Send reward summaries to participants
    if (war_ && current_war_id_.is_valid())
    {
        for (const auto& [pid, pdata] : player_data_)
        {
            auto rewards = war_->calculate_rewards(current_war_id_, pid);
            auto msg = network::make_crusade_reward_summary(
                0, static_cast<uint8_t>(winner),
                pdata.war_contribution, rewards.experience,
                rewards.gold, rewards.contribution_points);
            send_to_player(pid, msg);
        }
    }
```

With:
```cpp
    // Calculate and send crusade-specific rewards to participants
    for (const auto& [pid, pdata] : player_data_)
    {
        // Get player level for formula
        int32_t level = 1;
        if (players_)
        {
            auto* plr = players_->get_player(pid);
            if (plr) level = plr->experience.level;
        }

        auto reward = calculate_crusade_reward(
            pdata.war_contribution, level, pdata.faction, winner);

        // Send reward summary (gold is always 0 for crusade)
        auto msg = network::make_crusade_reward_summary(
            0, static_cast<uint8_t>(winner),
            pdata.war_contribution, reward.experience,
            0, 0);
        send_to_player(pid, msg);

        // Apply EXP to online players
        if (players_ && reward.experience > 0)
        {
            players_->add_experience(pid, reward.experience);
        }
    }
```

**Step 4: Run tests to verify they pass**

Run: `cmake --build build --config Debug && ./bin/hgserver_tests --gtest_filter="crusade_reward_test.*:crusade_system_test.end_crusade_sends_legacy_reward:crusade_system_test.end_crusade_applies_exp_to_online_player"`
Expected: All PASS

**Step 5: Commit**

```bash
git add src/war/crusade/crusade_system.cpp tests/test_crusade_system.cpp
git commit -m "Wire legacy crusade reward formula into end_crusade"
```

---

### Task 3: Update `persist_war_result()` to use crusade formula for reward values

**Files:**
- Modify: `src/war/crusade/crusade_system.cpp:580-634` (persist_war_result function)

The persistence function currently calls `war_->calculate_rewards()` to get reward values for the DB. It needs to use the new formula instead.

**Step 1: Write the failing test**

This is a wiring change — tested via existing persistence tests. No new test needed, but verify the persist function compiles with updated rewards.

**Step 2: Update `persist_war_result()` implementation**

In `src/war/crusade/crusade_system.cpp`, in `persist_war_result()`, replace lines 625-629:

Replace:
```cpp
        auto rewards = war_->calculate_rewards(current_war_id_, pid);

        persistence_->save_participant(
            war_db_id, char_id, *participant,
            static_cast<uint8_t>(pdata.duty), rewards);
```

With:
```cpp
        // Calculate crusade-specific rewards for persistence
        int32_t level = 1;
        if (players_)
        {
            auto* plr = players_->get_player(pid);
            if (plr) level = plr->experience.level;
        }

        auto crusade_rwd = calculate_crusade_reward(
            pdata.war_contribution, level, pdata.faction, winner);

        war_rewards rewards;
        rewards.experience = crusade_rwd.experience;
        rewards.gold = 0;
        rewards.contribution_points = pdata.war_contribution;

        persistence_->save_participant(
            war_db_id, char_id, *participant,
            static_cast<uint8_t>(pdata.duty), rewards);
```

**Step 3: Run tests to verify compilation and existing tests pass**

Run: `cmake --build build --config Debug && ./bin/hgserver_tests --gtest_filter="crusade_*"`
Expected: All PASS

**Step 4: Commit**

```bash
git add src/war/crusade/crusade_system.cpp
git commit -m "Use crusade reward formula for persistence instead of generic war rewards"
```

---

### Task 4: Add `reward_claimed` column — DB migration and schema

**Files:**
- Create: `tools/migrate/migrations/20260213_160000_add_war_reward_claimed.sql`
- Modify: `src/database/schema.sql:272-291`

**Step 1: Create the migration file**

Run: `ls tools/migrate/migrations/` to confirm directory exists.

Create `tools/migrate/migrations/20260213_160000_add_war_reward_claimed.sql`:

```sql
-- up
ALTER TABLE war_participants ADD COLUMN reward_claimed BOOLEAN NOT NULL DEFAULT FALSE;
CREATE INDEX IF NOT EXISTS idx_war_participants_unclaimed ON war_participants(character_id) WHERE reward_claimed = FALSE;

-- down
DROP INDEX IF EXISTS idx_war_participants_unclaimed;
ALTER TABLE war_participants DROP COLUMN IF EXISTS reward_claimed;
```

**Step 2: Update `src/database/schema.sql`**

Add `reward_claimed` column to `war_participants` table definition. After the `reward_contribution` line (line 286):

Add:
```sql
    reward_claimed  BOOLEAN NOT NULL DEFAULT FALSE
```

Also add the partial index after the existing indexes (after line 291):

```sql
CREATE INDEX IF NOT EXISTS idx_war_participants_unclaimed ON war_participants(character_id) WHERE reward_claimed = FALSE;
```

**Step 3: Commit**

```bash
git add tools/migrate/migrations/20260213_160000_add_war_reward_claimed.sql src/database/schema.sql
git commit -m "Add reward_claimed column to war_participants table"
```

---

### Task 5: Add `reward_claimed` to `save_participant()` and add unclaimed reward queries

**Files:**
- Modify: `src/war/war_persistence.h:35-50` (war_participant_row struct)
- Modify: `src/war/war_persistence.h:53-87` (class interface)
- Modify: `src/war/war_persistence.cpp:73-110` (save_participant)
- Modify: `src/war/war_persistence.cpp:186-227` (load_war_participants)

**Step 1: Update `war_participant_row` struct**

In `src/war/war_persistence.h`, add to `war_participant_row` after line 49 (`reward_contribution`):

```cpp
    bool reward_claimed{false};
```

**Step 2: Add new methods to `war_persistence` class**

In `src/war/war_persistence.h`, add before `private:` (line 85):

```cpp
    // Get unclaimed rewards for a character (for deferred reward delivery at login)
    auto get_unclaimed_rewards(int32_t character_id)
        -> hb::result<std::vector<war_participant_row>, std::string>;

    // Mark specific participant rewards as claimed
    auto mark_rewards_claimed(int32_t participant_id)
        -> hb::result<void, std::string>;

    // Save participant with reward_claimed flag
    auto save_participant_with_claimed(int32_t war_db_id,
                                       int32_t character_id,
                                       const war_participant& participant,
                                       uint8_t duty,
                                       const war_rewards& rewards,
                                       bool claimed) -> hb::result<void, std::string>;
```

**Step 3: Update `save_participant()` SQL to include `reward_claimed`**

In `src/war/war_persistence.cpp`, update `save_participant()` (line 82-101) to pass `false` for `reward_claimed`:

Replace the SQL in save_participant:
```cpp
    auto query_result = db_->execute_params(
        "INSERT INTO war_participants (war_id, character_id, faction, duty, "
        "kills, deaths, assists, damage_dealt, healing_done, contribution, "
        "reward_exp, reward_gold, reward_contribution) "
        "VALUES ($1, $2, $3, $4, $5, $6, $7, $8, $9, $10, $11, $12, $13) "
        "ON CONFLICT (war_id, character_id) DO NOTHING",
```

With:
```cpp
    auto query_result = db_->execute_params(
        "INSERT INTO war_participants (war_id, character_id, faction, duty, "
        "kills, deaths, assists, damage_dealt, healing_done, contribution, "
        "reward_exp, reward_gold, reward_contribution, reward_claimed) "
        "VALUES ($1, $2, $3, $4, $5, $6, $7, $8, $9, $10, $11, $12, $13, $14) "
        "ON CONFLICT (war_id, character_id) DO NOTHING",
        war_db_id,
        character_id,
        static_cast<int16_t>(participant.faction),
        static_cast<int16_t>(duty),
        participant.kills,
        participant.deaths,
        participant.assists,
        participant.damage_dealt,
        participant.healing_done,
        participant.contribution_score,
        rewards.experience,
        static_cast<int32_t>(rewards.gold),
        rewards.contribution_points,
        false
    );
```

**Step 4: Implement new methods**

Add to `src/war/war_persistence.cpp`:

```cpp
auto war_persistence::save_participant_with_claimed(int32_t war_db_id,
                                                     int32_t character_id,
                                                     const war_participant& participant,
                                                     uint8_t duty,
                                                     const war_rewards& rewards,
                                                     bool claimed)
    -> hb::result<void, std::string>
{
    if (!db_) return hb::result<void, std::string>::err("No database system");

    auto query_result = db_->execute_params(
        "INSERT INTO war_participants (war_id, character_id, faction, duty, "
        "kills, deaths, assists, damage_dealt, healing_done, contribution, "
        "reward_exp, reward_gold, reward_contribution, reward_claimed) "
        "VALUES ($1, $2, $3, $4, $5, $6, $7, $8, $9, $10, $11, $12, $13, $14) "
        "ON CONFLICT (war_id, character_id) DO NOTHING",
        war_db_id,
        character_id,
        static_cast<int16_t>(participant.faction),
        static_cast<int16_t>(duty),
        participant.kills,
        participant.deaths,
        participant.assists,
        participant.damage_dealt,
        participant.healing_done,
        participant.contribution_score,
        rewards.experience,
        static_cast<int32_t>(rewards.gold),
        rewards.contribution_points,
        claimed
    );

    if (query_result.is_err())
    {
        LOG_ERROR(general, "Failed to save war participant: {}", query_result.error());
        return hb::result<void, std::string>::err(query_result.error());
    }

    return hb::result<void, std::string>::ok();
}

auto war_persistence::get_unclaimed_rewards(int32_t character_id)
    -> hb::result<std::vector<war_participant_row>, std::string>
{
    if (!db_) return hb::result<std::vector<war_participant_row>, std::string>::err("No database system");

    auto query_result = db_->execute_params(
        "SELECT wp.id, wp.war_id, wp.character_id, wp.faction, wp.duty, "
        "wp.kills, wp.deaths, wp.assists, wp.damage_dealt, wp.healing_done, "
        "wp.contribution, wp.reward_exp, wp.reward_gold, wp.reward_contribution, "
        "wp.reward_claimed "
        "FROM war_participants wp "
        "WHERE wp.character_id = $1 AND wp.reward_claimed = FALSE "
        "ORDER BY wp.id ASC",
        character_id
    );

    if (query_result.is_err())
    {
        return hb::result<std::vector<war_participant_row>, std::string>::err(query_result.error());
    }

    std::vector<war_participant_row> rows;
    for (const auto& row : query_result.value())
    {
        war_participant_row r;
        r.id = row["id"].as<int32_t>();
        r.war_id = row["war_id"].as<int32_t>();
        r.character_id = row["character_id"].as<int32_t>();
        r.faction = static_cast<war_faction>(row["faction"].as<int16_t>());
        r.duty = static_cast<uint8_t>(row["duty"].as<int16_t>());
        r.kills = row["kills"].as<int32_t>();
        r.deaths = row["deaths"].as<int32_t>();
        r.assists = row["assists"].as<int32_t>();
        r.damage_dealt = row["damage_dealt"].as<int32_t>();
        r.healing_done = row["healing_done"].as<int32_t>();
        r.contribution = row["contribution"].as<int32_t>();
        r.reward_exp = row["reward_exp"].as<int64_t>();
        r.reward_gold = row["reward_gold"].as<int32_t>();
        r.reward_contribution = row["reward_contribution"].as<int32_t>();
        r.reward_claimed = row["reward_claimed"].as<bool>();
        rows.push_back(std::move(r));
    }

    return hb::result<std::vector<war_participant_row>, std::string>::ok(std::move(rows));
}

auto war_persistence::mark_rewards_claimed(int32_t participant_id)
    -> hb::result<void, std::string>
{
    if (!db_) return hb::result<void, std::string>::err("No database system");

    auto query_result = db_->execute_params(
        "UPDATE war_participants SET reward_claimed = TRUE WHERE id = $1",
        participant_id
    );

    if (query_result.is_err())
    {
        return hb::result<void, std::string>::err(query_result.error());
    }

    return hb::result<void, std::string>::ok();
}
```

**Step 5: Update `load_war_participants` to include `reward_claimed`**

In `src/war/war_persistence.cpp`, in `load_war_participants()`, add `reward_claimed` to the SELECT and row parsing:

Update the SQL query (line 192) to include `wp.reward_claimed` in the SELECT.

Add after line 222 (`r.reward_contribution = ...`):
```cpp
        r.reward_claimed = row["reward_claimed"].as<bool>();
```

**Step 6: Run tests to verify compilation**

Run: `cmake --build build --config Debug && ./bin/hgserver_tests --gtest_filter="crusade_*:war_persistence*"`
Expected: PASS (compilation succeeds, existing tests still pass)

**Step 7: Commit**

```bash
git add src/war/war_persistence.h src/war/war_persistence.cpp
git commit -m "Add reward_claimed flag and unclaimed reward queries to war_persistence"
```

---

### Task 6: Update `persist_war_result()` to track claimed status per participant

**Files:**
- Modify: `src/war/crusade/crusade_system.cpp:580-634` (persist_war_result)

Online players who received their reward should be persisted with `reward_claimed = true`. Offline players get `reward_claimed = false`.

**Step 1: Update persist_war_result to use `save_participant_with_claimed`**

In `src/war/crusade/crusade_system.cpp`, in `persist_war_result()`, change the `save_participant` call to use `save_participant_with_claimed`:

Replace (the block we already modified in Task 3):
```cpp
        persistence_->save_participant(
            war_db_id, char_id, *participant,
            static_cast<uint8_t>(pdata.duty), rewards);
```

With:
```cpp
        // Determine if player is online (reward was already delivered)
        bool is_online = false;
        if (players_)
        {
            auto* plr = players_->get_player(pid);
            is_online = (plr != nullptr);
        }

        persistence_->save_participant_with_claimed(
            war_db_id, char_id, *participant,
            static_cast<uint8_t>(pdata.duty), rewards, is_online);
```

**Step 2: Run tests**

Run: `cmake --build build --config Debug && ./bin/hgserver_tests --gtest_filter="crusade_*"`
Expected: All PASS

**Step 3: Commit**

```bash
git add src/war/crusade/crusade_system.cpp
git commit -m "Track reward_claimed status when persisting crusade participants"
```

---

### Task 7: Add deferred reward hook in `auth_handlers` enter_game flow

**Files:**
- Modify: `src/bridge/handlers/auth_handlers.h:10-130`
- Modify: `src/bridge/handlers/auth_handlers.cpp:1-60` (includes and initialize)
- Modify: `src/bridge/handlers/auth_handlers.cpp:1075-1180` (end of handle_enter_game)
- Modify: `src/application.cpp:370-383` (auth_handlers initialize call)

**Step 1: Add `war_persistence` pointer to `auth_handlers`**

In `src/bridge/handlers/auth_handlers.h`:

Add forward declaration after the existing ones (around line 47):
```cpp
namespace hb::war {
    class war_persistence;
}
```

Add to `auth_handlers` class — update `initialize()` signature (line 60-69) to add `war_persistence*`:
```cpp
    void initialize(network::websocket_server* ws_server,
                    auth::auth_system* auth,
                    player::player_system* players = nullptr,
                    world::world_subsystem* world = nullptr,
                    inventory::inventory_system* inventory = nullptr,
                    admin::admin_system* admin = nullptr,
                    npc::npc_system* npc = nullptr,
                    item::item_system* item = nullptr,
                    social::social_system* social = nullptr,
                    scheduler* sched = nullptr,
                    war::war_persistence* war_persistence = nullptr);
```

Add member after `scheduler_` (line 128):
```cpp
    war::war_persistence* war_persistence_{nullptr};
```

**Step 2: Update `auth_handlers.cpp` initialize**

In `src/bridge/handlers/auth_handlers.cpp`:

Add include:
```cpp
#include "war/war_persistence.h"
#include "network/json_protocol.h"
```

Update `initialize()` function signature (line 34-43) and body to accept and store `war_persistence`:

```cpp
void auth_handlers::initialize(network::websocket_server* ws_server,
                                auth::auth_system* auth,
                                player::player_system* players,
                                world::world_subsystem* world,
                                inventory::inventory_system* inventory,
                                admin::admin_system* admin,
                                npc::npc_system* npc,
                                item::item_system* item,
                                social::social_system* social,
                                scheduler* sched,
                                war::war_persistence* war_persistence) {
    ws_server_ = ws_server;
    auth_ = auth;
    players_ = players;
    world_ = world;
    inventory_ = inventory;
    admin_ = admin;
    npc_ = npc;
    item_ = item;
    social_ = social;
    scheduler_ = sched;
    war_persistence_ = war_persistence;
```

**Step 3: Add deferred reward check at end of handle_enter_game**

In `src/bridge/handlers/auth_handlers.cpp`, at the end of `handle_enter_game()` — just before the closing `}` on line 1180, after the nearby player notification block:

```cpp
    // Check for unclaimed war rewards (deferred from crusade end while offline)
    if (war_persistence_ && players_) {
        auto* player = players_->get_player(live_player_id);
        if (player) {
            int32_t db_char_id = static_cast<int32_t>(player->character_id.value);
            auto unclaimed_result = war_persistence_->get_unclaimed_rewards(db_char_id);
            if (unclaimed_result.is_ok()) {
                for (const auto& row : unclaimed_result.value()) {
                    // Apply deferred EXP reward
                    if (row.reward_exp > 0) {
                        players_->add_experience(live_player_id, row.reward_exp);
                    }

                    // Send reward summary to client
                    auto reward_msg = network::make_crusade_reward_summary(
                        0, static_cast<uint8_t>(row.faction),
                        row.contribution, row.reward_exp,
                        row.reward_gold, row.reward_contribution);
                    conn->send(reward_msg);

                    // Mark as claimed
                    war_persistence_->mark_rewards_claimed(row.id);

                    LOG_INFO(bridge, "Applied deferred war reward to player {} (char_id={}): {} exp",
                        live_player_id.value, db_char_id, row.reward_exp);
                }
            }
        }
    }
```

**Step 4: Wire `war_persistence` in `application.cpp`**

In `src/application.cpp`, find the `auth_handlers_->initialize(` call (line 372-383) and add the war_persistence pointer:

Replace:
```cpp
        auth_handlers_->initialize(
            ws_server_.get(),
            &auth_sys,
            subsystems().get<player::player_system>(),
            subsystems().get<world::world_subsystem>(),
            subsystems().get<inventory::inventory_system>(),
            subsystems().get<admin::admin_system>(),
            subsystems().get<npc::npc_system>(),
            subsystems().get<item::item_system>(),
            subsystems().get<social::social_system>(),
            subsystems().get<scheduler>()
        );
```

With:
```cpp
        auth_handlers_->initialize(
            ws_server_.get(),
            &auth_sys,
            subsystems().get<player::player_system>(),
            subsystems().get<world::world_subsystem>(),
            subsystems().get<inventory::inventory_system>(),
            subsystems().get<admin::admin_system>(),
            subsystems().get<npc::npc_system>(),
            subsystems().get<item::item_system>(),
            subsystems().get<social::social_system>(),
            subsystems().get<scheduler>(),
            war_persistence_.get()
        );
```

Note: `war_persistence_` should already exist as a member of the application class. Verify with `grep war_persistence_ src/application.h`.

**Step 5: Build and run all tests**

Run: `cmake --build build --config Debug && ./bin/hgserver_tests`
Expected: All tests PASS

**Step 6: Commit**

```bash
git add src/bridge/handlers/auth_handlers.h src/bridge/handlers/auth_handlers.cpp src/application.cpp
git commit -m "Add deferred war reward delivery at login via auth_handlers"
```

---

### Task 8: Final integration test and verification

**Files:**
- Test: `tests/test_crusade_system.cpp`

**Step 1: Run full test suite**

Run: `cmake --build build --config Debug && ./bin/hgserver_tests`
Expected: All tests PASS, no regressions

**Step 2: Run crusade-specific tests**

Run: `./bin/hgserver_tests --gtest_filter="crusade_*" --gtest_print_time=1`
Expected: All crusade tests PASS

**Step 3: Verify no compilation warnings**

Run: `cmake --build build --config Debug 2>&1 | grep -i "warning" | grep -i "crusade\|war_persist\|auth_handler" | head -20`
Expected: No relevant warnings

**Step 4: Final commit with all changes**

If any loose changes remain:

```bash
git add -A
git status
```

Review and commit if needed.
