# Plan 02: Meteor Player Damage Fixes

## Problem

The meteor player damage formula in the new code implements **dead code** from the legacy rather than the actual executed formula. Additionally, the legacy 255 damage cap is missing, and several secondary effects (hold/paralyze break, playing dead interaction) are absent.

## Legacy Behavior (Reference: `Game.cpp:42697` `DoMeteorStrikeDamageHandler`)

### Actual damage formula:
```cpp
// Lines 42704-42706 are DEAD CODE (overwritten by line 42707):
// if (level < 80) iDamage = level + iDice(1,10);
// else            iDamage = level*2 + iDice(1,10);

// Line 42707 — THIS is what actually executes:
iDamage = iDice(1, level) + level;
// Range: [level+1, level*2]
// For level 100: 101-200
```

### Damage cap:
```cpp
if (iDamage > 255) iDamage = 255;
```

### Protection from Magic:
```cpp
// Magnitude >= 5 (Absolute Magic Protection): damage = 0
// Magnitude >= 2 (Protection From Magic): damage = damage/2 - 2
```

### Admin immunity:
```cpp
if (m_pClientList[i]->m_iAdminUserLevel > 0) iDamage = 0;
```

### Secondary effects on surviving players with damage > 0:
1. Send HP update notification
2. Send damage motion event to nearby clients (visual feedback)
3. If player is playing dead (skill 19): refresh owner position on map
4. **Break hold/paralyze effects** (`DEF_MAGICTYPE_HOLDOBJECT`)

### Player death:
- If HP <= 0: calls `ClientKilledHandler`, increments casualties counter

## Current New Code

File: `src/war/crusade/meteor_handler.h` / `meteor_handler.cpp`

### Current (wrong) formula at `calculate_player_damage`:
```cpp
if (player_level < 80)
    return player_level + d10(rng);  // WRONG — this is the dead code branch
return player_level * 2 + d10(rng);  // WRONG — this is the dead code branch
```

### Missing:
- No 255 damage cap
- No hold/paralyze break
- No playing dead interaction
- No damage motion broadcast

## Implementation Plan

### Step 1: Fix `calculate_player_damage` in `meteor_handler.cpp`

Change the formula to match legacy line 42707:

```cpp
int32_t meteor_handler::calculate_player_damage(int32_t player_level)
{
    if (player_level <= 0) return 0;

    thread_local std::mt19937 rng{std::random_device{}()};
    std::uniform_int_distribution<int32_t> dist(1, player_level);

    int32_t damage = dist(rng) + player_level;
    // Range: [level+1, level*2]

    // Legacy cap: 255 maximum
    if (damage > 255) damage = 255;

    return damage;
}
```

### Step 2: Update the player damage callback in `crusade_system.cpp`

In `setup_meteor_callbacks()`, the player damage lambda already handles magic protection and admin immunity. Add hold/paralyze break after applying damage:

```cpp
// After applying damage to a surviving player:
if (plr.hp > 0 && damage > 0)
{
    // Break hold/paralyze effects (DEF_MAGICTYPE_HOLDOBJECT)
    if (effects_ && plr.ecs_entity.id != 0)
    {
        effects_->remove_effects_by_group(plr.ecs_entity, magic_type::hold);
    }
}
```

Note: Check if `effect_system` has a `remove_effects_by_group` method or equivalent. If not, iterate effects and remove those with `group == magic_type::hold` (or the equivalent enum value for hold/paralyze).

### Step 3: Update tests

In the crusade system tests, update the damage formula expectations:

```cpp
// Old test expectation (WRONG):
// level 50: 50 + d10 = 51-60
// level 100: 200 + d10 = 201-210

// New test expectation (CORRECT):
// level 50: iDice(1,50) + 50 = 51-100
// level 100: iDice(1,100) + 100 = 101-200
// level 150: iDice(1,150) + 150 = 151-300 → capped at 255
```

Add test cases:
1. `meteor_player_damage_formula_matches_legacy` — verify range [level+1, min(level*2, 255)]
2. `meteor_player_damage_capped_at_255` — level 200 should still cap at 255
3. `meteor_damage_breaks_hold_effects` — verify hold/paralyze removed on surviving player
4. `meteor_damage_zero_for_admins` — verify admin immunity
5. `meteor_damage_reduced_by_protection` — verify magnitude 2 and 5 protection

### Step 4: Verify magic protection is correct

The current protection logic in the callback:
```cpp
if (eff.magnitude >= 5) damage = 0;                    // Absolute Magic Protection
else if (eff.magnitude >= 2) damage = max(0, damage/2 - 2);  // Protection From Magic
```

This matches legacy. Confirm this is still present after the formula fix.

## Files to Modify
- `src/war/crusade/meteor_handler.cpp` — fix `calculate_player_damage`
- `src/war/crusade/crusade_system.cpp` — add hold/paralyze break in meteor callback
- `tests/test_crusade_system.cpp` — update damage formula tests

## Acceptance Criteria
- `calculate_player_damage(100)` returns values in range [101, 200]
- `calculate_player_damage(200)` returns values capped at 255
- Hold/paralyze effects are removed from surviving players hit by meteor
- Magic protection still reduces damage correctly
- Admin players take 0 damage
- All tests pass, build succeeds
