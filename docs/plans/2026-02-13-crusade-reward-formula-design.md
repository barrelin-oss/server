# Crusade Reward Formula Design

**Date:** 2026-02-13
**Status:** Approved
**Reference:** `war-plans/04-crusade-reward-formula.md`

## Problem

The crusade reward formula in the new code uses `war_system::calculate_rewards()` which differs from legacy behavior. Legacy uses a level-based bonus, different win/lose/draw ratios, awards no gold, and processes rewards at login for offline players.

## Design Decisions

| Decision | Choice | Rationale |
|----------|--------|-----------|
| Formula | Match legacy exactly | Level bonus + contribution with 1x/1÷6/1÷10 split |
| Gold | Always zero | Legacy behavior |
| Deferred storage | `reward_claimed` column on `war_participants` | Reuses existing table, survives server restarts |
| Calculation timing | Pre-calculate at crusade end | Deterministic reward regardless of login timing |
| EXP application | `pending_exp` for both online and deferred | Consistent processing through normal tick system |
| GUID matching | Not needed | Pre-calculated + `reward_claimed` flag is sufficient |

## Reward Formula

Pure function with no side effects:

```cpp
struct crusade_reward
{
    int64_t experience{0};
    int32_t war_contribution_used{0};
    bool is_winner{false};
    bool is_draw{false};
};

auto calculate_crusade_reward(int32_t war_contribution, int32_t player_level,
    war_faction player_faction, war_faction winner) -> crusade_reward;
```

**Level bonus** (added to contribution before EXP calculation):
- Level 1-80: `level * 100`
- Level 81-100: `level * 40`
- Level 101+: `level * 1`

**EXP calculation** (on adjusted contribution = war_contribution + level_bonus):
- Winner: full adjusted contribution
- Draw: adjusted contribution / 6
- Loser: adjusted contribution / 10

**Gold:** always 0.

## Online Reward Delivery

At `end_crusade()`, for each participant:
1. Get player level from `player_system` (default 1 if not found)
2. Call `calculate_crusade_reward()`
3. If player is online: send `crusade_reward_summary` message, add to `pending_exp`
4. Persist to `war_participants` with pre-calculated reward values
5. Online players get `reward_claimed = true`; offline players get `reward_claimed = false`

## Deferred Reward Delivery

**DB migration:** Add `reward_claimed BOOLEAN DEFAULT FALSE` to `war_participants`.

**Login hook** in `auth_handlers.cpp` enter_game flow, after player is loaded:
1. Query `war_participants` for `character_id = X AND reward_claimed = false`
2. For each unclaimed row: add `reward_exp` to `pending_exp`, send `crusade_reward_summary`, set `reward_claimed = true`

Works for any war type since it reads from the generic `war_participants` table.

## Files to Modify

| File | Change |
|------|--------|
| `src/war/crusade/crusade_types.h` | Add `crusade_reward` struct |
| `src/war/crusade/crusade_system.h/cpp` | Add `calculate_crusade_reward()`, update `end_crusade()` |
| `src/war/war_persistence.h/cpp` | Add `get_unclaimed_rewards()`, `mark_rewards_claimed()`, update `save_participant()` |
| `src/bridge/handlers/auth_handlers.h/cpp` | Add `war_persistence_` pointer, check unclaimed rewards in enter_game |
| `src/application.cpp` | Wire `war_persistence` to auth_handlers |
| `src/database/schema.sql` | Add `reward_claimed` column |
| New migration file | `ALTER TABLE war_participants ADD COLUMN reward_claimed BOOLEAN DEFAULT FALSE` |
| `tests/test_crusade_system.cpp` | Reward formula tests |

## Test Cases

| Test | Input | Expected |
|------|-------|----------|
| Winner level 50, 1000 contribution | level=50, contrib=1000, winner | exp = 1000 + 5000 = 6000 |
| Loser level 50, 1000 contribution | level=50, contrib=1000, loser | exp = (1000 + 5000) / 10 = 600 |
| Draw level 50, 1000 contribution | level=50, contrib=1000, draw | exp = (1000 + 5000) / 6 = 1000 |
| Level 90 mid bracket | level=90, contrib=1000, winner | exp = 1000 + 3600 = 4600 |
| Level 120 high bracket | level=120, contrib=1000, winner | exp = 1000 + 120 = 1120 |
| Zero gold | any | gold = 0 |
| Uses war_contribution | contrib=500, kills irrelevant | exp based on 500, not kill count |
