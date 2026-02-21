# Plan 03: Crusade Construction & Economy Fixes

## Problems

Multiple economy-related discrepancies between legacy and new crusade code:

1. **Construction point transfer awards contribution to the wrong player** — legacy gives contribution to the commander, new code gives it to the fighter
2. **Player kill construction formula is 4x too high** — legacy uses `victim_level / 2`, new uses `100 * victim_level / 50`
3. **NPC kill construction/contribution rewards are missing entirely**
4. **Only fighters earn from kills** — legacy allows all duties to earn
5. **No friendly NPC kill penalty**
6. **Direct contribution from player kills is missing** — legacy awards `(reward_exp - reward_exp/3) * 12` contribution on player kills

## Legacy Behavior (Reference: `Game.cpp`)

### Construction point transfer (`CheckCommanderConstructionPoint`, line 42880):
```cpp
// Fighter/constructor transfers ALL points to guild master commander
commander->m_iConstructionPoint += fighter->m_iConstructionPoint;
// COMMANDER gets contribution (not the fighter):
commander->m_iWarContribution += fighter->m_iConstructionPoint / 10;
// Cap both:
if (commander->m_iConstructionPoint > DEF_MAXCONSTRUCTIONPOINT) // 30000
    commander->m_iConstructionPoint = DEF_MAXCONSTRUCTIONPOINT;
if (commander->m_iWarContribution > DEF_MAXWARCONTRIBUTION) // 200000
    commander->m_iWarContribution = DEF_MAXWARCONTRIBUTION;
// Fighter's points reset to 0
fighter->m_iConstructionPoint = 0;
```

### Player kill construction points (line 10818-10853):
```cpp
// Construction points from killing enemy PLAYERS during crusade:
iConstructionPoint = m_pNpcList[victim]->... // This is actually for NPC kills
// For player kills (line ~24488):
m_pClientList[iAttackerH]->m_iConstructionPoint += m_pNpcList[iNpcH]->m_sType; // NPC type/2 for NPCs

// Actually, player kill construction is at line ~10840:
iConstructionPoint = iTargetLevel / 2;  // victim_level / 2
```

### NPC kill rewards (line 10818-10839):
Per NPC type, hardcoded construction point and contribution values:
```
Type 1-6 (basic mobs):     50 construction,  100 contribution
Type 36 (AGT):             700 construction, 4000 contribution
Type 37 (CGT):             700 construction, 4000 contribution
Type 38 (Mana Collector):  500 construction, 2000 contribution
Type 39 (Detector):        500 construction, 2000 contribution
Type 40 (ESG):            1500 construction, 5000 contribution
Type 41 (GMG):            5000 construction, 10000 contribution
Type 43 (LWB):             500 construction, 1000 contribution
Type 44 (GHK):            1000 construction, 2000 contribution
Type 45 (GHKABS):         1500 construction, 3000 contribution
Type 46 (TK):             1000 construction, 2000 contribution
Type 47 (BG):             1500 construction, 3000 contribution
Type 51 (Catapult):        500 construction, 1500 contribution
```

### Player kill contribution (line ~24488-24491):
During crusade mode, killing a player awards:
```cpp
// EXP: (reward_exp/3)*4
m_pClientList[iAttackerH]->m_iExp += (iRewardExp/3)*4;
// Contribution: (reward_exp - reward_exp/3)*12
m_pClientList[iAttackerH]->m_iWarContribution += (iRewardExp - (iRewardExp/3))*12;
```
This is SEPARATE from construction points. Contribution comes from EXP-based calculation.

### Friendly NPC kill penalty:
```cpp
// Killing a friendly war NPC:
m_pClientList[iAttackerH]->m_iWarContribution -= m_pClientList[iAttackerH]->m_iWarContribution * 2;
```
This effectively sets contribution to negative (double the current value subtracted).

### Duty restriction:
Legacy does NOT restrict construction point earning to fighters only. Any player who kills an enemy gets construction points during crusade.

## Current New Code Location

- `src/war/crusade/crusade_system.cpp` — `on_player_kill()`, `transfer_construction_points()`
- `src/war/crusade/crusade_types.h` — config structs

## Implementation Plan

### Step 1: Fix construction point transfer direction

In `crusade_system.cpp`, `transfer_construction_points()`:

```cpp
// BEFORE (wrong — gives contribution to fighter):
data.war_contribution = std::min(data.war_contribution + contribution, max_war_contribution);

// AFTER (correct — gives contribution to commander):
commander_data->war_contribution = std::min(
    commander_data->war_contribution + contribution, max_war_contribution);
// Fighter gets nothing extra (points zeroed, no contribution)
data.construction_points = 0;
```

### Step 2: Fix player kill construction formula

In `on_player_kill()`:

```cpp
// BEFORE (wrong — 4x too high):
int32_t points = config_.construction.points_per_kill_base;
if (victim_level > 0) {
    points = points * victim_level / 50;
    points = std::max(points, 10);
}

// AFTER (correct — matches legacy):
int32_t points = victim_level / 2;
points = std::max(points, 1);  // minimum 1 point
```

Remove `points_per_kill_base` from config (or repurpose it). The formula is just `victim_level / 2`.

### Step 3: Remove fighter-only restriction on kill rewards

In `on_player_kill()`:

```cpp
// BEFORE:
if (killer_data->duty != crusade_duty::fighter) return;

// AFTER: Remove this check entirely. All duties earn from kills.
```

### Step 4: Add player kill contribution

In `on_player_kill()`, after awarding construction points, also award contribution:

```cpp
// Award contribution from player kill (legacy formula)
// This requires knowing the EXP reward. Since we may not have that context,
// use a simplified formula based on victim level:
// Legacy: (reward_exp - reward_exp/3) * 12
// reward_exp is level-dependent. Approximate: victim_level * 10 as base exp
int32_t base_exp = victim_level * 10;  // simplified approximation
int32_t contribution = (base_exp - base_exp / 3) * 12;
killer_data->war_contribution = std::min(
    killer_data->war_contribution + contribution, max_war_contribution);
```

NOTE: The exact `iRewardExp` depends on the combat system's kill reward calculation. If `combat_system` can provide the actual exp reward, use that instead. Check how `on_player_kill` is called and whether the exp reward value is available. If not, add a parameter.

### Step 5: Add NPC kill handler

Add a new method `on_npc_kill(player_id killer, uint16_t npc_type)`:

```cpp
void crusade_system::on_npc_kill(player_id killer, uint16_t npc_type)
{
    if (!active_) return;
    auto* data = get_player_data(killer);
    if (!data) return;

    auto [construction, contribution] = get_npc_kill_rewards(npc_type);
    if (construction > 0)
    {
        data->construction_points = std::min(
            data->construction_points + construction, max_construction_points);
    }
    if (contribution > 0)
    {
        data->war_contribution = std::min(
            data->war_contribution + contribution, max_war_contribution);
    }

    broadcast_construction_point_update(killer);
}
```

Add the reward lookup:
```cpp
auto crusade_system::get_npc_kill_rewards(uint16_t npc_type)
    -> std::pair<int32_t, int32_t>
{
    switch (npc_type)
    {
    case 36: return {700, 4000};   // AGT
    case 37: return {700, 4000};   // CGT
    case 38: return {500, 2000};   // Mana Collector
    case 39: return {500, 2000};   // Detector
    case 40: return {1500, 5000};  // ESG
    case 41: return {5000, 10000}; // GMG
    case 43: return {500, 1000};   // LWB
    case 44: return {1000, 2000};  // GHK
    case 45: return {1500, 3000};  // GHKABS
    case 46: return {1000, 2000};  // TK
    case 47: return {1500, 3000};  // BG
    case 51: return {500, 1500};   // Catapult
    default:
        if (npc_type >= 1 && npc_type <= 6) return {50, 100};
        return {0, 0};
    }
}
```

### Step 6: Add friendly NPC kill penalty

Add `on_friendly_npc_kill(player_id killer)`:
```cpp
void crusade_system::on_friendly_npc_kill(player_id killer)
{
    if (!active_) return;
    auto* data = get_player_data(killer);
    if (!data) return;

    // Legacy: subtracts 2x current contribution (effectively sets to negative equivalent)
    data->war_contribution = -(data->war_contribution);
    // Clamp to 0 minimum (don't allow negative in new system)
    data->war_contribution = std::max(data->war_contribution, 0);
}
```

### Step 7: Wire NPC kill callbacks

In `game_handlers.cpp` or wherever NPC death is handled during crusade, call `crusade->on_npc_kill()` when a player kills an enemy NPC, and `crusade->on_friendly_npc_kill()` when killing a friendly NPC.

### Step 8: Update config

Remove `points_per_kill_base` from `construction_point_config` since the formula is now hardcoded to `victim_level / 2`. Keep `transfer_contribution_ratio` (0.1) and `transfer_interval_seconds` (30).

### Step 9: Update tests

1. `transfer_awards_contribution_to_commander` — verify commander gets contribution, fighter gets zero
2. `player_kill_construction_formula` — level 100 victim = 50 points, level 50 = 25 points
3. `all_duties_earn_from_kills` — commanders and constructors also earn
4. `npc_kill_rewards_match_legacy` — test each NPC type's reward values
5. `friendly_npc_kill_penalty` — verify contribution penalty
6. `player_kill_awards_contribution` — verify EXP-based contribution on kill

## Files to Modify
- `src/war/crusade/crusade_system.h` — add `on_npc_kill`, `on_friendly_npc_kill`, `get_npc_kill_rewards`
- `src/war/crusade/crusade_system.cpp` — fix transfer, fix kill formula, add new methods
- `src/war/crusade/crusade_types.h` — remove/update `points_per_kill_base`
- `tests/test_crusade_system.cpp` — update and add tests
- `src/bridge/handlers/game_handlers.cpp` — wire NPC kill callbacks (if applicable)

## Acceptance Criteria
- Transfer gives contribution to commander, not fighter
- Player kill awards `victim_level / 2` construction points (not 4x that)
- All duty types earn from kills
- NPC kills during crusade award type-specific construction and contribution
- Friendly NPC kills penalize contribution
- All tests pass
