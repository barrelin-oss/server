# Plan 12: War Unit Construction Cost Corrections

## Problem

The current war unit construction costs are fabricated and don't match legacy. The legacy system has a fundamentally different model:
- **Structures (types 36-39)**: Zero construction cost, restricted by guild construct location
- **Mobile units (types 43-47, 51)**: Explicit construction costs
- **ESG (type 40)**: Pre-placed only, not player-summonable

Additionally, several mobile unit types are missing entirely.

## Legacy Values (Reference: `Game.cpp` line 491-497)

### Mobile unit costs (from `m_iNpcConstructionPoint[]`):
```
Type 43 (LWB):      1000
Type 44 (GHK):      2000
Type 45 (GHKABS):   3000
Type 46 (TK):       2000
Type 47 (BG):       3000
Type 51 (Catapult): 1500
```

### Structures (types 36-39): NOT in the cost array
- Cost is effectively 0
- Placement controlled by guild construct location + proximity rules
- See Plan 05 for the full build system

### ESG (type 40): NOT summonable by players
- Only placed via `CreateCrusadeStructures()` (pre-placed from config)
- The `RequestSummonWarUnitHandler` does not assign ESG to either side's NPC name

## Current New Code

```cpp
// In crusade_system.cpp, get_construction_cost():
case war_unit_type::agt:            return 2000;  // WRONG — should be 0
case war_unit_type::cgt:            return 3000;  // WRONG — should be 0
case war_unit_type::mana_collector: return 1500;  // WRONG — should be 0
case war_unit_type::detector:       return 1000;  // WRONG — should be 0
case war_unit_type::esg:            return 2500;  // WRONG — not summonable
case war_unit_type::lwb:            return 500;   // WRONG — should be 1000
case war_unit_type::catapult:       return 4000;  // WRONG — should be 1500
```

## Implementation Plan

This is a subset of Plan 05 (Structures & Build System) focused only on the cost corrections. If Plan 05 is being done, skip this plan. Otherwise, this is a quick standalone fix.

### Step 1: Fix `get_construction_cost()`

```cpp
auto crusade_system::get_construction_cost(war_unit_type type) const -> int32_t
{
    switch (type)
    {
    // Structures: zero cost (restricted by location in Plan 05)
    case war_unit_type::agt:            return 0;
    case war_unit_type::cgt:            return 0;
    case war_unit_type::mana_collector: return 0;
    case war_unit_type::detector:       return 0;

    // ESG: not player-summonable
    case war_unit_type::esg:            return -1;  // Invalid

    // Mobile units: legacy costs
    case war_unit_type::lwb:       return 1000;
    case war_unit_type::catapult:  return 1500;

    // If new types are added (see Plan 05):
    // case war_unit_type::ghk:    return 2000;
    // case war_unit_type::ghkabs: return 3000;
    // case war_unit_type::tk:     return 2000;
    // case war_unit_type::bg:     return 3000;

    default: return -1;
    }
}
```

### Step 2: Handle zero-cost structures in summon_war_unit()

```cpp
int32_t cost = get_construction_cost(type);
if (cost < 0) return crusade_result::invalid_unit;  // Not summonable

// Only check/deduct cost if > 0
if (cost > 0)
{
    if (data->construction_points < cost)
        return crusade_result::insufficient_points;
    data->construction_points -= cost;
}
// Structures (cost 0) skip the cost check entirely
```

### Step 3: Block ESG summoning

Since `get_construction_cost` returns -1 for ESG, it will be caught by the `invalid_unit` check. Alternatively, add an explicit check:

```cpp
if (type == war_unit_type::esg)
    return crusade_result::invalid_unit;  // Pre-placed only
```

### Step 4: Update tests

1. `structure_costs_zero` — AGT, CGT, Mana Collector, Detector cost 0
2. `mobile_unit_costs_match_legacy` — LWB=1000, Catapult=1500
3. `esg_not_summonable` — ESG returns invalid_unit
4. `zero_cost_skips_point_check` — structure summoning doesn't require any construction points

## Files to Modify
- `src/war/crusade/crusade_system.cpp` — fix `get_construction_cost()`, update `summon_war_unit()`
- `tests/test_crusade_system.cpp` — update cost tests

## Acceptance Criteria
- Structures cost 0 construction points
- LWB costs 1000, Catapult costs 1500
- ESG cannot be summoned by players
- Zero-cost structures can be summoned with 0 construction points
- All tests pass
