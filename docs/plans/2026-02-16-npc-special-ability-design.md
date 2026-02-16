# NPC Special Ability System

> **For Claude:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** Implement the legacy NPC special ability system where NPCs randomly receive special abilities at spawn time from weighted pools.

**Architecture:** Each NPC template has `sa_prob` (probability %) and `sa_pool` (pool ID). Pools are defined in `special_abilities.yaml` — each pool lists which ability IDs can be rolled, with equal probability. At spawn, roll the probability; if it succeeds, pick a random ability from the pool. Apply stat modifications and send the ability to the client instead of the static `attribute` field.

**Tech Stack:** C++20, YAML config, Google Test

---

## Problem

The modern server conflates two separate legacy systems:

1. **`m_cAttribute`** (Npc.cfg `Attr` column) — fixed internal property per NPC type
2. **`m_cSpecialAbility`** (MobGenerator random roll) — randomly assigned at spawn, displayed to client, modifies stats

Currently, `attribute` from npcs.yaml is copied to every NPC instance and sent to the client as the display status. In the legacy system, only 5-35% of NPCs received a special ability, randomly rolled at spawn time from weighted ability pools.

## Special Ability Types (1-8)

| ID | Name | Stat Modifications |
|----|------|--------------------|
| 1 | Clairvoyant | +25% exp |
| 2 | Destructive Magic Protection | +30% exp |
| 3 | Anti-Physical | abs_damage -= rand(20,80), capped at -90; +proportional exp |
| 4 | Anti-Magic | abs_damage += rand(20,80), capped at 90; +proportional exp |
| 5 | Poisonous | +15% exp, poison resistance = 110 |
| 6 | Critical Poisonous | poison resistance = 110 |
| 7 | Explosive | +20% exp |
| 8 | Hi-Explosive | +25% exp |

**Mutual exclusion (from legacy `_bInitNpcAttr`):**
- SA 3 (Anti-Physical): if NPC already has positive abs_damage → SA cleared to 0
- SA 4 (Anti-Magic): if NPC already has negative abs_damage → SA cleared to 0

**Special case:** Beholder (sprite_id 53) always gets SA=1 (Clairvoyant) regardless of roll.

## Config File: `special_abilities.yaml`

Pools and ability names are defined in YAML, not hardcoded. Default config matches legacy:

```yaml
abilities:
  - { id: 1, name: "Clairvoyant" }
  - { id: 2, name: "Destructive Magic Protection" }
  - { id: 3, name: "Anti-Physical" }
  - { id: 4, name: "Anti-Magic" }
  - { id: 5, name: "Poisonous" }
  - { id: 6, name: "Critical Poisonous" }
  - { id: 7, name: "Explosive" }
  - { id: 8, name: "Hi-Explosive" }

pools:
  - { id: 1, abilities: [3, 4] }
  - { id: 2, abilities: [3, 4, 5] }
  - { id: 3, abilities: [3, 4, 5, 6] }
  - { id: 4, abilities: [3, 4, 7] }
  - { id: 5, abilities: [3, 4, 7, 8] }
  - { id: 6, abilities: [3, 4, 5] }
  - { id: 7, abilities: [1, 2, 4] }
  - { id: 8, abilities: [1, 2, 3, 4, 8] }
  - { id: 9, abilities: [1, 2, 3, 4, 5, 6, 7, 8] }
```

## Special Ability Pools (from true legacy `_cGetSpecialAbility`)

Each pool randomly selects one ability with equal probability:

| Pool | Abilities | Used By |
|------|-----------|---------|
| 1 | 3, 4 | Slime, Orc, Orge, WereWolf, Mountain-Giant, Stalker, Hellclaw, Wyvern, Fire-Wyvern, Barlog, Tentocle, Centaurus, etc. |
| 2 | 3, 4, 5 | Giant-Ant, Cat, Giant-Frog |
| 3 | 3, 4, 5, 6 | Zombie, Scorpion, Amphis, Troll, Dark-Elf |
| 4 | 3, 4, 7 | (unused in MobGenerator table) |
| 5 | 3, 4, 7, 8 | Stone-Golem, Clay-Golem, Beholder, Cannibal-Plant, Rudolph, DireBoar |
| 6 | 3, 4, 5 | Cat (spot-mob-gen context only) |
| 7 | 1, 2, 4 | Orc-Mage, Unicorn |
| 8 | 1, 2, 3, 4, 8 | Skeleton, Cyclops, Hellbound, Liche, Demon, Gagoyle, Ettin, Frost, Ice-Golem |
| 9 | uniform 1-8 | (fallback/special) |

## NPC SA Table (from true legacy MobGenerator)

| NPC | sa_prob | sa_pool |
|-----|---------|---------|
| Slime | 5 | 1 |
| Rabbit | 5 | 1 |
| Giant-Ant | 10 | 2 |
| Cat | 10 | 2 |
| Giant-Frog | 10 | 2 |
| Orc | 15 | 1 |
| Zombie | 15 | 3 |
| Scorpion | 15 | 3 |
| Amphis | 20 | 3 |
| Dark-Elf | 20 | 3 |
| Clay-Golem | 20 | 5 |
| Beholder | 20 | 5 |
| Cannibal-Plant | 20 | 5 |
| Rudolph | 20 | 5 |
| DireBoar | 20 | 5 |
| Demon | 20 | 8 |
| Gagoyle | 20 | 8 |
| Ettin | 20 | 8 |
| Stalker | 20 | 1 |
| Hellclaw | 20 | 1 |
| Wyvern | 20 | 1 |
| Fire-Wyvern | 20 | 1 |
| Barlog | 20 | 1 |
| Tentocle | 20 | 1 |
| Centaurus | 20 | 1 |
| Troll | 25 | 3 |
| Orge | 25 | 1 |
| Mountain-Giant | 25 | 1 |
| Stone-Golem | 25 | 5 |
| WereWolf | 25 | 1 |
| Hellbound | 25 | 8 |
| Orc-Mage | 30 | 7 |
| Liche | 30 | 8 |
| Frost | 30 | 8 |
| Skeleton | 35 | 8 |
| Cyclops | 35 | 8 |
| Unicorn | 35 | 7 |
| Ice-Golem | 35 | 8 |

NPCs not in this table get `sa_prob: 0, sa_pool: 0` (never get a special ability).

---

## Implementation Plan

### Task 1: Add special_ability field to NPC instance and string conversion

**Files:**
- Modify: `src/npc/npc.h:39-148` (npc struct) and lines 233-262 (attribute strings function)
- Test: `tests/test_npc.cpp`

**Step 1: Write failing tests**

```cpp
TEST(npc_special_ability_test, strings_returns_empty_for_zero)
{
    EXPECT_TRUE(npc::npc_special_ability_strings(0).empty());
}

TEST(npc_special_ability_test, strings_returns_correct_names)
{
    EXPECT_EQ(npc::npc_special_ability_strings(1), std::vector<std::string>{"Clairvoyant"});
    EXPECT_EQ(npc::npc_special_ability_strings(2), std::vector<std::string>{"Destructive Magic Protection"});
    EXPECT_EQ(npc::npc_special_ability_strings(3), std::vector<std::string>{"Anti-Physical"});
    EXPECT_EQ(npc::npc_special_ability_strings(4), std::vector<std::string>{"Anti-Magic"});
    EXPECT_EQ(npc::npc_special_ability_strings(5), std::vector<std::string>{"Poisonous"});
    EXPECT_EQ(npc::npc_special_ability_strings(6), std::vector<std::string>{"Critical Poisonous"});
    EXPECT_EQ(npc::npc_special_ability_strings(7), std::vector<std::string>{"Explosive"});
    EXPECT_EQ(npc::npc_special_ability_strings(8), std::vector<std::string>{"Hi-Explosive"});
}

TEST(npc_special_ability_test, strings_returns_empty_for_out_of_range)
{
    EXPECT_TRUE(npc::npc_special_ability_strings(9).empty());
    EXPECT_TRUE(npc::npc_special_ability_strings(-1).empty());
}
```

**Step 2: Run tests to verify they fail**

Run: `cmake --build build --config Debug && ./bin/hgserver_tests --gtest_filter="npc_special_ability*"`
Expected: compile error — `npc_special_ability_strings` not defined

**Step 3: Implement**

In `src/npc/npc.h`:
- Add `int8_t special_ability{0};` to `npc` struct (after `attribute` field, line ~73)
- Add `npc_special_ability_strings()` function (reusing same logic as `npc_attribute_strings` but renamed)

**Step 4: Run tests to verify they pass**

Run: `cmake --build build --config Debug && ./bin/hgserver_tests --gtest_filter="npc_special_ability*"`
Expected: PASS

**Step 5: Commit**

---

### Task 2: Add sa_prob/sa_pool to npc_template and YAML loader

**Files:**
- Modify: `src/registry/npc_template.h:47-136` (npc_template struct)
- Modify: `src/registry/npc_registry.cpp:238-245` (YAML parsing, after attribute parsing)
- Modify: `bin/game_configs/npcs.yaml` (add sa_prob/sa_pool to all NPCs)
- Test: `tests/test_npc.cpp`

**Step 1: Write failing test**

```cpp
TEST(npc_registry_test, yaml_parses_sa_fields)
{
    // Write a minimal YAML with sa_prob and sa_pool
    // Load it via registry
    // Verify template has correct sa_prob and sa_pool values
}
```

**Step 2: Implement**

In `src/registry/npc_template.h`, add after `attribute` field (line 70):
```cpp
int16_t sa_prob{0};   // Special ability probability % (0-100)
int16_t sa_pool{0};   // Special ability pool (1-9) for _cGetSpecialAbility
```

In `src/registry/npc_registry.cpp`, after attribute parsing (line ~239):
```cpp
if (node["sa_prob"])
    npc.sa_prob = static_cast<int16_t>(node["sa_prob"].as<int>());
if (node["sa_pool"])
    npc.sa_pool = static_cast<int16_t>(node["sa_pool"].as<int>());
```

In `bin/game_configs/npcs.yaml`, add `sa_prob` and `sa_pool` to every NPC entry using the legacy MobGenerator table above. NPCs not in the table get `sa_prob: 0, sa_pool: 0`.

**Step 3: Run tests**

**Step 4: Commit**

---

### Task 3: Implement roll_special_ability pool function

**Files:**
- Create: `src/npc/special_ability.h`
- Test: `tests/test_npc.cpp`

**Step 1: Write failing tests**

```cpp
TEST(special_ability_pool_test, pool_1_returns_3_or_4)
{
    std::set<int8_t> seen;
    for (int i = 0; i < 200; ++i)
    {
        auto sa = npc::roll_special_ability(1);
        EXPECT_TRUE(sa == 3 || sa == 4) << "pool 1 returned " << (int)sa;
        seen.insert(sa);
    }
    EXPECT_EQ(seen.size(), 2u); // Both values should appear
}

TEST(special_ability_pool_test, pool_2_returns_3_4_or_5)
{
    std::set<int8_t> seen;
    for (int i = 0; i < 300; ++i)
    {
        auto sa = npc::roll_special_ability(2);
        EXPECT_TRUE(sa >= 3 && sa <= 5) << "pool 2 returned " << (int)sa;
        seen.insert(sa);
    }
    EXPECT_EQ(seen.size(), 3u);
}

// Similar tests for pools 3-9...

TEST(special_ability_pool_test, pool_0_returns_0)
{
    EXPECT_EQ(npc::roll_special_ability(0), 0);
}

TEST(special_ability_pool_test, invalid_pool_returns_0)
{
    EXPECT_EQ(npc::roll_special_ability(10), 0);
    EXPECT_EQ(npc::roll_special_ability(-1), 0);
}
```

**Step 2: Implement**

`src/npc/special_ability.h`:
```cpp
#pragma once
#include <cstdint>
#include <random>

namespace hb::npc
{

// Roll a random special ability from the given pool (1-9).
// Returns 0 for invalid pool. Matches legacy _cGetSpecialAbility().
[[nodiscard]] inline auto roll_special_ability(int pool) -> int8_t
{
    thread_local std::mt19937 rng{std::random_device{}()};

    switch (pool)
    {
    case 1: // Anti-Physical, Anti-Magic
    {
        std::uniform_int_distribution<int> dist(1, 2);
        switch (dist(rng))
        {
        case 1: return 3;
        case 2: return 4;
        }
        break;
    }
    case 2: // Anti-Physical, Anti-Magic, Poisonous
    {
        std::uniform_int_distribution<int> dist(1, 3);
        switch (dist(rng))
        {
        case 1: return 3;
        case 2: return 4;
        case 3: return 5;
        }
        break;
    }
    case 3: // Anti-Physical, Anti-Magic, Poisonous, Critical Poisonous
    {
        std::uniform_int_distribution<int> dist(1, 4);
        switch (dist(rng))
        {
        case 1: return 3;
        case 2: return 4;
        case 3: return 5;
        case 4: return 6;
        }
        break;
    }
    case 4: // Anti-Physical, Anti-Magic, Explosive
    {
        std::uniform_int_distribution<int> dist(1, 3);
        switch (dist(rng))
        {
        case 1: return 3;
        case 2: return 4;
        case 3: return 7;
        }
        break;
    }
    case 5: // Anti-Physical, Anti-Magic, Explosive, Hi-Explosive
    {
        std::uniform_int_distribution<int> dist(1, 4);
        switch (dist(rng))
        {
        case 1: return 3;
        case 2: return 4;
        case 3: return 7;
        case 4: return 8;
        }
        break;
    }
    case 6: // Anti-Physical, Anti-Magic, Poisonous (same as pool 2)
    {
        std::uniform_int_distribution<int> dist(1, 3);
        switch (dist(rng))
        {
        case 1: return 3;
        case 2: return 4;
        case 3: return 5;
        }
        break;
    }
    case 7: // Clairvoyant, DMP, Anti-Magic
    {
        std::uniform_int_distribution<int> dist(1, 3);
        switch (dist(rng))
        {
        case 1: return 1;
        case 2: return 2;
        case 3: return 4;
        }
        break;
    }
    case 8: // Clairvoyant, DMP, Anti-Physical, Anti-Magic, Hi-Explosive
    {
        std::uniform_int_distribution<int> dist(1, 5);
        switch (dist(rng))
        {
        case 1: return 1;
        case 2: return 2;
        case 3: return 4;
        case 4: return 3;
        case 5: return 8;
        }
        break;
    }
    case 9: // All abilities 1-8
    {
        std::uniform_int_distribution<int> dist(1, 8);
        return static_cast<int8_t>(dist(rng));
    }
    default:
        return 0;
    }
    return 0;
}

} // namespace hb::npc
```

**Step 3: Run tests**

**Step 4: Commit**

---

### Task 4: Apply special ability at spawn with stat modifications

**Files:**
- Modify: `src/npc/npc_system.cpp:160-280` (spawn_npc function)
- Test: `tests/test_npc.cpp`

**Step 1: Write failing tests**

```cpp
// Test that spawning with sa_prob=100 always gets an ability
TEST_F(npc_system_test, spawn_with_sa_prob_100_always_gets_ability)
{
    // Set up registry with template that has sa_prob=100, sa_pool=9
    // Spawn 50 NPCs
    // All should have special_ability != 0
}

// Test stat modifications
TEST(special_ability_stat_test, clairvoyant_adds_25_percent_exp)
{
    npc::npc n;
    n.exp_reward = 100;
    npc::apply_special_ability(n, 1);
    EXPECT_EQ(n.special_ability, 1);
    EXPECT_EQ(n.exp_reward, 125);
}

TEST(special_ability_stat_test, anti_physical_sets_negative_abs_damage)
{
    npc::npc n;
    n.abs_damage = 0;
    n.exp_reward = 100;
    npc::apply_special_ability(n, 3);
    EXPECT_EQ(n.special_ability, 3);
    EXPECT_LT(n.abs_damage, 0);
    EXPECT_GE(n.abs_damage, -90);
    EXPECT_GT(n.exp_reward, 100);
}

TEST(special_ability_stat_test, anti_physical_cleared_if_positive_abs_damage)
{
    npc::npc n;
    n.abs_damage = 30; // positive = already has magic absorption
    n.exp_reward = 100;
    npc::apply_special_ability(n, 3);
    EXPECT_EQ(n.special_ability, 0); // Cleared
    EXPECT_EQ(n.abs_damage, 30);     // Unchanged
}

TEST(special_ability_stat_test, anti_magic_cleared_if_negative_abs_damage)
{
    npc::npc n;
    n.abs_damage = -30; // negative = already has physical absorption
    n.exp_reward = 100;
    npc::apply_special_ability(n, 4);
    EXPECT_EQ(n.special_ability, 0); // Cleared
}

TEST(special_ability_stat_test, beholder_always_clairvoyant)
{
    npc::npc n;
    n.sprite_id = 53; // Beholder
    n.exp_reward = 100;
    npc::apply_special_ability(n, 5); // Tried to assign poisonous
    EXPECT_EQ(n.special_ability, 1);  // Overridden to clairvoyant
    EXPECT_EQ(n.exp_reward, 125);     // +25% for clairvoyant
}
```

**Step 2: Implement**

Add `apply_special_ability()` function in `src/npc/special_ability.h`:
```cpp
inline void apply_special_ability(npc& n, int8_t sa)
{
    // Beholder override
    if (n.sprite_id == 53)
        sa = 1;

    thread_local std::mt19937 rng{std::random_device{}()};

    switch (sa)
    {
    case 1: // Clairvoyant
        n.exp_reward += n.exp_reward / 4; // +25%
        break;
    case 2: // DMP
        n.exp_reward += n.exp_reward * 30 / 100; // +30%
        break;
    case 3: // Anti-Physical
        if (n.abs_damage > 0) { sa = 0; break; } // Mutual exclusion
        {
            std::uniform_int_distribution<int> dist(1, 60);
            int amount = 20 + dist(rng);
            n.abs_damage -= static_cast<int16_t>(amount);
            if (n.abs_damage < -90) n.abs_damage = -90;
            n.exp_reward += static_cast<int32_t>(
                static_cast<double>(n.exp_reward) * static_cast<double>(std::abs(n.abs_damage)) / 100.0);
        }
        break;
    case 4: // Anti-Magic
        if (n.abs_damage < 0) { sa = 0; break; } // Mutual exclusion
        {
            std::uniform_int_distribution<int> dist(1, 60);
            int amount = 20 + dist(rng);
            n.abs_damage += static_cast<int16_t>(amount);
            if (n.abs_damage > 90) n.abs_damage = 90;
            n.exp_reward += static_cast<int32_t>(
                static_cast<double>(n.exp_reward) * static_cast<double>(n.abs_damage) / 100.0);
        }
        break;
    case 5: // Poisonous
        n.exp_reward += n.exp_reward * 15 / 100; // +15%
        // poison_resistance = 110 (add field if needed, or skip for now)
        break;
    case 6: // Critical Poisonous
        // poison_resistance = 110
        break;
    case 7: // Explosive
        n.exp_reward += n.exp_reward / 5; // +20%
        break;
    case 8: // Hi-Explosive
        n.exp_reward += n.exp_reward / 4; // +25%
        break;
    default:
        sa = 0;
        break;
    }
    n.special_ability = sa;
}
```

In `src/npc/npc_system.cpp::spawn_npc()`, after line ~222 (`new_npc->area = tmpl->area;`):
```cpp
// Roll special ability
if (tmpl->sa_prob > 0 && tmpl->sa_pool > 0 && random_int(1, 100) <= tmpl->sa_prob)
{
    auto sa = roll_special_ability(tmpl->sa_pool);
    apply_special_ability(*new_npc, sa);
}
else if (new_npc->sprite_id == 53) // Beholder always clairvoyant
{
    apply_special_ability(*new_npc, 1);
}
```

**Step 3: Run tests**

**Step 4: Commit**

---

### Task 5: Update client display — send special_ability instead of attribute

**Files:**
- Modify: `src/bridge/handlers/entity_builders.cpp:415` and `:444`
- Modify: `src/bridge/handlers/game_handlers_npc.cpp:31`
- Test: `tests/test_entity_appearance.cpp`

**Step 1: Write failing test**

```cpp
TEST(entity_appearance_test, npc_spawn_uses_special_ability_not_attribute)
{
    // Create NPC with attribute=2 but special_ability=5
    // Build spawn message
    // Verify attributes list contains "Poisonous" not "Destructive Magic Protection"
}
```

**Step 2: Implement**

Replace all 3 call sites of `npc_attribute_strings(n.attribute)` with `npc_special_ability_strings(n.special_ability)`.

Add `#include "npc/special_ability.h"` where needed.

**Step 3: Run ALL tests to check for regressions**

Run: `cmake --build build --config Debug && ./bin/hgserver_tests`

**Step 4: Commit**

---

### Task 6: Update npcs.yaml with SA data from legacy MobGenerator

**Files:**
- Modify: `bin/game_configs/npcs.yaml`

Add `sa_prob` and `sa_pool` fields to every NPC entry. Use the legacy MobGenerator table above. NPCs not in the table get `sa_prob: 0, sa_pool: 0`.

**Commit**

---
