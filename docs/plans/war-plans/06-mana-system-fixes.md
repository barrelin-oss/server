# Plan 06: Mana System Fixes

## Problems

1. **GMG charges default to 1** — legacy requires `m_iMaxMana` charges (NPC-configured, typically > 1) before firing meteor
2. **Mana pool reset discards remainder in legacy, new keeps it** — new is more generous
3. **Mana stones are not individual entities** — legacy has per-stone depletion and regeneration; new treats them as infinite sources
4. **Mana collector MP restoration to allies missing** — legacy restores player MP within 5 tiles
5. **GMG attackability missing** — legacy allows attacking GMG to reduce mana stock
6. **Collector MP restore config field exists but is unused**

## Legacy Behavior (Reference: `Game.cpp`)

### Mana Stone (NPC type 42, line 27027-27031):
```cpp
// Each behavior tick:
m_iV1 += 5;           // Regenerate 5 mana
if (m_iV1 > 5) m_iV1 = 5;  // Cap at 5
// So stones always have exactly 5 mana available (instant refill)
```

### Mana Collector (NPC type 38, `_bNpcBehavior_ManaCollector` line 53012-53069):
```cpp
// Scan rectangular area: dX-5 to dX+5, dY-5 to dY+5 (11x11 tiles)
for each tile in range:
    if tile has NPC type 42 (ManaStone):
        if stone->m_iV1 >= 3:
            stone->m_iV1 -= 3;
            m_iCollectedMana[side] += 3;
        else:
            m_iCollectedMana[side] += stone->m_iV1;
            stone->m_iV1 = 0;

    if tile has player of same side:
        // MP restoration to allies
        iTotal = iDice(1, player.m_iMag);  // Random 1 to magic stat
        // Apply AddMP% bonus from equipment
        if (AddMP% > 0) iTotal += (iTotal * AddMP%) / 100;
        player.m_iMP += iTotal;
        if (player.m_iMP > player.m_iMaxMP) player.m_iMP = player.m_iMaxMP;
```

### Grand Magic Generator (NPC type 41, `_NpcBehavior_GrandMagicGenerator` line 53122-53156):
```cpp
// Called every 3 behavior turns
if (side == 1 && m_iAresdenMana > DEF_GMGMANACONSUMEUNIT) { // 15
    m_iAresdenMana = 0;  // RESET TO ZERO (discards remainder)
    m_iManaStock++;
    if (m_iManaStock > m_iMaxMana) {  // m_iMaxMana from NPC config
        m_iManaStock = 0;
        // FIRE METEOR
        MeteorStrikeMsgHandler(side);
    }
}
// Same for side 2 with m_iElvineMana
```

### GMG damage vulnerability (around line 10940-10950):
```cpp
// When GMG (type 41) takes damage:
m_pNpcList[iNpcH]->m_iV1 += iDamage;
if (m_pNpcList[iNpcH]->m_iV1 > 500) {
    m_pNpcList[iNpcH]->m_iV1 = 0;
    m_iManaStock--;  // Reduce mana charge
    if (m_iManaStock < 0) m_iManaStock = 0;
}
```

### Key constants:
- `DEF_GMGMANACONSUMEUNIT = 15` — mana threshold per charge
- `m_iMaxMana` — from NPC config, determines charges needed for meteor (typically > 1)
- Collector scan radius: 5 tiles (rectangular)
- Drain per stone per tick: 3
- Stone regen: 5 per tick, cap 5
- GMG damage threshold for charge reduction: 500 accumulated damage

## Current New Code

File: `src/war/crusade/mana_system.h` / `mana_system.cpp`

Current config:
```cpp
struct mana_config {
    int32_t collector_scan_radius{5};
    int32_t collector_harvest_rate{3};
    int32_t gmg_mana_threshold{15};
    int32_t gmg_charges_for_meteor{1};  // WRONG — should be > 1
    float tick_interval_seconds{5.0f};
    int32_t collector_mp_restore{5};    // Exists but UNUSED
};
```

## Implementation Plan

### Step 1: Fix GMG charges for meteor

Change default `gmg_charges_for_meteor` from 1 to a value matching legacy. The legacy value comes from `m_iMaxMana` in the NPC config for GMG (type 41). Check the NPC config files (`npcs.yaml` or equivalent) to find the GMG's MaxMana value. If not available, use a reasonable default (e.g., 10).

```cpp
int32_t gmg_charges_for_meteor{10};  // Update from 1 to match legacy NPC config
```

### Step 2: Fix mana pool reset to discard remainder

In `mana_system::check_gmg()`:

```cpp
// BEFORE (keeps remainder):
state.mana_pool -= config_.gmg_mana_threshold;

// AFTER (discards remainder, matching legacy):
state.mana_pool = 0;  // Legacy resets to 0, discarding any excess
state.gmg_charges++;
```

### Step 3: Add per-stone mana tracking (optional simplification)

The legacy has individual stone entities with mana state. For the new system, we can simulate this without actual NPC entities:

```cpp
struct mana_stone_state
{
    int32_t current_mana{5};
    static constexpr int32_t max_mana = 5;
    static constexpr int32_t regen_rate = 5;
};

// In mana_system:
std::vector<mana_stone_state> stones_;  // Per-faction stone states

void tick_faction_mana(war_faction faction, int32_t collector_count, int32_t stone_count)
{
    // Regenerate stones first
    for (auto& stone : stones_)
    {
        stone.current_mana = std::min(stone.current_mana + mana_stone_state::regen_rate,
                                       mana_stone_state::max_mana);
    }

    // Each collector harvests from stones
    int32_t total_harvested = 0;
    for (int c = 0; c < collector_count; c++)
    {
        for (auto& stone : stones_)
        {
            if (stone.current_mana >= config_.collector_harvest_rate)
            {
                stone.current_mana -= config_.collector_harvest_rate;
                total_harvested += config_.collector_harvest_rate;
            }
            else if (stone.current_mana > 0)
            {
                total_harvested += stone.current_mana;
                stone.current_mana = 0;
            }
        }
    }

    auto& state = get_faction_state(faction);
    state.mana_pool += total_harvested;
}
```

**Alternative**: If the stone-level simulation adds too much complexity, keep the current simplified approach but document the difference. The key behavioral impact is that with infinite sources, mana accumulates faster than legacy.

### Step 4: Add MP restoration to allies

Add a callback for MP restoration:

```cpp
// In mana_config:
int32_t collector_mp_restore_enabled{1};  // Enable/disable

// Callback type:
using restore_mp_fn = std::function<void(player_id, int32_t amount)>;

// In crusade_system, during mana tick:
if (config_.mana.collector_mp_restore_enabled)
{
    // For each collector, find nearby allied players and restore MP
    // This requires knowing collector positions (from war_structures_) and
    // getting players within 5 tiles of each collector
    for (const auto& ws : war_structures_)
    {
        if (ws.type != war_unit_type::mana_collector) continue;

        // Get players within 5 tiles of this collector
        auto nearby = players_->get_players_who_can_see(ws.map_name, {ws.x, ws.y});
        for (auto pid : nearby)
        {
            auto* plr = players_->get_player(pid);
            if (!plr) continue;
            if (get_player_faction(pid) != ws.faction) continue;

            // Legacy: iDice(1, player.m_iMag)
            int32_t mag = plr->stats.magic;
            if (mag <= 0) continue;

            thread_local std::mt19937 rng{std::random_device{}()};
            std::uniform_int_distribution<int32_t> dist(1, mag);
            int32_t restore = dist(rng);

            plr->mp = std::min(plr->mp + restore, plr->max_mp);
        }
    }
}
```

### Step 5: Add GMG damage vulnerability

Add a method for when a GMG NPC takes damage:

```cpp
void crusade_system::on_gmg_damage(war_faction gmg_faction, int32_t damage)
{
    auto& state = mana_system_.get_faction_state(gmg_faction);
    state.gmg_accumulated_damage += damage;

    if (state.gmg_accumulated_damage > 500)
    {
        state.gmg_accumulated_damage = 0;
        if (state.gmg_charges > 0)
        {
            state.gmg_charges--;
            LOG_INFO(general, "GMG ({}) lost a charge from damage. Charges: {}",
                static_cast<int>(gmg_faction), state.gmg_charges);
        }
    }
}
```

Add `gmg_accumulated_damage` to `faction_mana_state`:
```cpp
struct faction_mana_state
{
    int32_t mana_pool{0};
    int32_t gmg_charges{0};
    int32_t gmg_accumulated_damage{0};  // NEW
};
```

Wire this in the NPC damage handler so that when a type 41 NPC takes damage, `on_gmg_damage()` is called.

### Step 6: Update tests

1. `gmg_requires_multiple_charges` — verify meteor doesn't fire after 1 charge (needs N charges)
2. `mana_pool_reset_discards_remainder` — 20 mana with threshold 15 → pool becomes 0, not 5
3. `gmg_damage_reduces_charges` — 500+ damage removes a charge
4. `mp_restoration_to_allies` — players near collectors get MP restored
5. `stone_depletion_limits_harvest` — if implemented: multiple collectors deplete stones
6. `collector_only_restores_same_faction` — different faction players don't get MP

## Files to Modify
- `src/war/crusade/mana_system.h` — add state fields, update config defaults
- `src/war/crusade/mana_system.cpp` — fix pool reset, update charge logic
- `src/war/crusade/crusade_system.h` — add `on_gmg_damage`
- `src/war/crusade/crusade_system.cpp` — implement GMG damage, MP restoration
- `tests/test_crusade_system.cpp` — add/update mana tests

## Acceptance Criteria
- GMG requires multiple charges (configurable, default > 1) before firing meteor
- Mana pool resets to 0 on charge consumption (discards remainder)
- GMG can be attacked: 500+ accumulated damage removes a charge
- MP restoration to nearby allied players during mana tick
- All tests pass
