# Plan 04: Crusade Reward Formula

## Problem

The crusade end-of-war reward formula in the new code is completely different from legacy. The new code uses a generic `war_system::calculate_rewards()` formula, while legacy has a specific level-based bonus and different win/lose/draw ratios. Legacy also awards no gold, handles rewards at login time (for offline players), and uses the GUID system to prevent stale rewards.

## Legacy Behavior (Reference: `Game.cpp:54464` `CheckCrusadeResultCalculation`)

### When rewards are processed:
- NOT at crusade end time
- Called per-player at **login/reconnect time** (`bSendMsgToLS` or character init)
- Only processes if `m_bIsCrusadeMode == FALSE` (crusade has ended) AND player has a non-zero `m_dwCrusadeGUID`

### GUID matching:
```cpp
if (player->m_dwCrusadeGUID != m_dwCrusadeGUID) {
    // GUID mismatch — player was in a different/older crusade
    // Send notification with -1 winner, clear data, no rewards
    return;
}
```

### Reward calculation:

**Draw (winner side == 0):**
```cpp
m_iExpStock += m_iWarContribution / 6;
```

**Winner (winner side == player's side):**
```cpp
// Level-based bonus FIRST:
if (level <= 80)      contribution += level * 100;
else if (level <= 100) contribution += level * 40;
else                   contribution += level * 1;

// Full contribution as EXP:
m_iExpStock += m_iWarContribution;  // war_contribution (the FULL value)
```

**Loser (winner side != player's side):**
```cpp
// Same level-based bonus:
if (level <= 80)      contribution += level * 100;
else if (level <= 100) contribution += level * 40;
else                   contribution += level * 1;

// Only 1/10 of contribution as EXP:
m_iExpStock += m_iWarContribution / 10;
```

### After processing:
- Sends contribution value to client (negative for losers)
- Clears: duty = 0, contribution = 0, GUID = 0
- Resets speed hack tracking

### Key differences from new code:
- **No gold rewards** — legacy awards zero gold
- **war_contribution** is the crusade-specific accumulated value, NOT kills/assists/damage from war_system
- **Level-based bonus** adds to contribution before EXP calculation
- **Deferred processing** — processed at login, not at crusade end
- **GUID matching** prevents stale rewards from old crusades

## Current New Code

In `crusade_system::end_crusade()`:
- Calls `war_->calculate_rewards()` which uses: `experience = contribution_score * 10`, `gold = contribution_score * 5`, multiplied by 1.5x (winner) / 0.5x (loser) / 1.0x (draw)
- `contribution_score` comes from war_system (kills=10, assists=5, damage/100, healing/50) — NOT from war_contribution
- Sends rewards immediately to online players only

## Implementation Plan

### Step 1: Add crusade-specific reward calculation

Don't use `war_system::calculate_rewards()` for crusade. Add a dedicated method:

```cpp
struct crusade_reward
{
    int32_t experience{0};
    int32_t war_contribution_used{0};  // the contribution value that was consumed
    bool is_winner{false};
    bool is_draw{false};
};

auto crusade_system::calculate_crusade_reward(
    const crusade_player_data& data, int32_t player_level,
    war_faction winner) -> crusade_reward;
```

Implementation:
```cpp
auto crusade_system::calculate_crusade_reward(
    const crusade_player_data& data, int32_t player_level,
    war_faction winner) -> crusade_reward
{
    crusade_reward reward;
    int32_t contribution = data.war_contribution;

    // Level-based bonus (added to contribution before EXP calc)
    if (player_level <= 80)
        contribution += player_level * 100;
    else if (player_level <= 100)
        contribution += player_level * 40;
    else
        contribution += player_level * 1;

    if (winner == war_faction::neutral)
    {
        // Draw: 1/6 of contribution as EXP
        reward.experience = contribution / 6;
        reward.is_draw = true;
    }
    else if (winner == data.faction)
    {
        // Winner: full contribution as EXP
        reward.experience = contribution;
        reward.is_winner = true;
    }
    else
    {
        // Loser: 1/10 of contribution as EXP
        reward.experience = contribution / 10;
        reward.is_winner = false;
    }

    reward.war_contribution_used = data.war_contribution;
    return reward;
}
```

### Step 2: Update `end_crusade()` to use new formula

Replace the `war_->calculate_rewards()` call:

```cpp
void crusade_system::end_crusade()
{
    // ... existing cleanup ...

    // Calculate and send rewards to online players
    for (auto& [pid, data] : player_data_)
    {
        // Get player level (need player_system access)
        int32_t level = 1;
        if (players_)
        {
            auto* plr = players_->get_player(pid);
            if (plr) level = plr->experience.level;
        }

        auto reward = calculate_crusade_reward(data, level, last_crusade_winner_);

        // Send reward summary
        nlohmann::json rdata;
        rdata["experience"] = reward.experience;
        rdata["gold"] = 0;  // Legacy awards NO gold
        rdata["war_contribution"] = reward.war_contribution_used;
        rdata["winner"] = static_cast<int>(last_crusade_winner_);
        rdata["is_winner"] = reward.is_winner;
        rdata["is_draw"] = reward.is_draw;

        network::json_message msg;
        msg.type = network::json_message_type::crusade_reward_summary;
        msg.data = std::move(rdata);
        send_to_player(pid, msg);

        // Apply EXP reward
        if (players_ && reward.experience > 0)
        {
            auto* plr = players_->get_player(pid);
            if (plr) plr->experience.pending_exp += reward.experience;
        }
    }

    // ... rest of cleanup ...
}
```

### Step 3: Handle offline player rewards (deferred processing)

Add a mechanism for offline players to receive rewards at login:

```cpp
// Store pending rewards for offline players in war_persistence or a map
struct pending_crusade_reward
{
    uint32_t crusade_guid{0};
    war_faction winner{war_faction::neutral};
    int32_t war_contribution{0};
    war_faction player_faction{war_faction::neutral};
};

// At crusade end, for players who are NOT online:
// Store their data in pending_rewards_ map (keyed by character name or account)

// At login (in auth_handlers or game_handlers):
// Check if player has pending crusade reward
// Calculate reward using their current level + stored contribution
// Send reward and clear pending data
```

This requires coordination with `auth_handlers.cpp` (enter_game flow). Add a `check_pending_crusade_reward(player_id)` method that gets called during login.

### Step 4: Remove gold from crusade rewards

Ensure no gold is awarded. If `war_system::calculate_rewards()` is still called for persistence purposes, override the gold to 0 for crusade type.

### Step 5: Update tests

1. `crusade_reward_winner_full_contribution` — level 50 winner with 1000 contribution: exp = 1000 + 50*100 = 6000
2. `crusade_reward_loser_tenth_contribution` — level 50 loser with 1000 contribution: exp = (1000 + 5000) / 10 = 600
3. `crusade_reward_draw_sixth_contribution` — level 50 draw with 1000 contribution: exp = (1000 + 5000) / 6 = 1000
4. `crusade_reward_high_level_bonus` — level 120 gets level*1 = 120 bonus (not level*100)
5. `crusade_reward_no_gold` — verify gold is always 0
6. `crusade_reward_uses_war_contribution_not_kills` — verify the contribution used is the crusade-specific value

## Files to Modify
- `src/war/crusade/crusade_system.h` — add `calculate_crusade_reward`, `pending_crusade_reward` struct
- `src/war/crusade/crusade_system.cpp` — implement reward calculation, update `end_crusade()`
- `src/war/crusade/crusade_types.h` — add `crusade_reward` struct
- `tests/test_crusade_system.cpp` — add reward formula tests

## Acceptance Criteria
- Winner gets full contribution + level bonus as EXP
- Loser gets 1/10 of (contribution + level bonus) as EXP
- Draw gets 1/6 of (contribution + level bonus) as EXP
- Level bonus: <=80 → level*100, 81-100 → level*40, 101+ → level*1
- Zero gold awarded
- EXP source is war_contribution (crusade-specific), not war_system kills
- All tests pass
