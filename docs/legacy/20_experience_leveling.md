# Legacy Experience & Leveling System Documentation

**System:** Experience & Leveling
**Primary Files:** Level/exp handling in `Game.cpp`, `Client.h`
**Estimated Lines:** ~600 across codebase
**Complexity:** Medium

---

## Table of Contents

1. [Overview](#overview)
2. [Data Structures](#data-structures)
3. [Experience Table Calculation](#experience-table-calculation)
4. [Level-Up Mechanics](#level-up-mechanics)
5. [Stat Point Allocation](#stat-point-allocation)
6. [Experience Sources](#experience-sources)
7. [Experience Modifiers](#experience-modifiers)
8. [Party Experience Sharing](#party-experience-sharing)
9. [Experience Slate Buff](#experience-slate-buff)
10. [Auto-Exp System](#auto-exp-system)
11. [Death Penalties](#death-penalties)
12. [Majestic Points System](#majestic-points-system)
13. [Early Level Rewards](#early-level-rewards)
14. [Limited User Restrictions](#limited-user-restrictions)
15. [Key Functions Reference](#key-functions-reference)
16. [Constants Reference](#constants-reference)
17. [Interactions with Other Systems](#interactions-with-other-systems)
18. [Persistence Format](#persistence-format)
19. [Modernization Notes](#modernization-notes)

---

## Overview

The experience and leveling system manages character progression from level 1 to the maximum level of 180, with post-cap progression through Majestic Points. Players earn experience from multiple activities and allocate stat points as they level up.

### Core Concepts

- **Experience Points (EXP):** Accumulated value determining level
- **Level:** Character progression tier (1-180)
- **Stat Points (LU_Pool):** Allocatable points gained on level-up (3 per level)
- **Experience Stock:** Pending experience for deferred distribution
- **Auto-Exp:** Passive experience gain in safe zones
- **Majestic Points:** Post-180 progression currency for item upgrades

---

## Data Structures

### Per-Player Experience Data (CClient)

**File:** `Client.h`

```cpp
class CClient {
    // Core progression
    int m_iLevel;           // Current level (1-180)
    int m_iExp;             // Current experience points
    int m_iNextLevelExp;    // Experience threshold for next level

    // Stat point allocation
    int m_iLU_Pool;         // Unallocated stat points

    // Deferred experience
    int m_iExpStock;        // Pending experience to distribute
    int m_iAutoExpAmount;   // Auto-exp accumulated amount
    DWORD m_dwExpStockTime; // Last exp stock distribution time
    DWORD m_dwAutoExpTime;  // Last auto-exp tick time

    // Experience modifiers
    int m_iAddExp;          // Equipment-based exp bonus percentage
};
```

### Global Experience Table (CGame)

**File:** `Game.h`

```cpp
class CGame {
    // Level thresholds - calculated at startup
    int m_iLevelExpTable[200];  // Experience required for each level
};
```

---

## Experience Table Calculation

### Initialization

The experience table is calculated at server startup using a recursive formula:

```cpp
// In CGame initialization
for (int i = 1; i < 200; i++) {
    m_iLevelExpTable[i] = iGetLevelExp(i);
}
```

### Recursive Formula

```cpp
int CGame::iGetLevelExp(int iLevel) {
    int iRet;

    if (iLevel == 0) return 0;

    iRet = iGetLevelExp(iLevel - 1) + iLevel * (50 + (iLevel * (iLevel / 17) * (iLevel / 17)));

    return iRet;
}
```

### Formula Breakdown

```
EXP(level) = EXP(level-1) + level * (50 + (level * (level/17)^2))

Where:
- Base component: level * 50
- Scaling component: level * (level * (level/17)^2)
- Integer division used throughout (truncation)
```

### Sample Experience Thresholds

| Level | EXP Required | EXP to Next Level |
|-------|--------------|-------------------|
| 1 | 0 | 51 |
| 5 | 260 | 256 |
| 10 | 1,094 | 560 |
| 20 | 6,960 | 1,560 |
| 50 | 84,525 | 6,950 |
| 100 | 737,450 | 35,400 |
| 150 | 2,887,125 | 96,250 |
| 180 | 5,847,870 | N/A (max) |

*Note: Values are approximate due to integer truncation in formula.*

### Reverse Lookup

```cpp
int CGame::iGetExpLevel(int iExp) {
    // Get level from total experience value
    for (int i = 1; i < 200; i++) {
        if (iExp < m_iLevelExpTable[i]) {
            return i - 1;
        }
    }
    return 180;  // Max level
}
```

---

## Level-Up Mechanics

### Level Check Function

```cpp
BOOL CGame::bCheckLevelUp(int iClientH) {
    // Validate client
    if (m_pClientList[iClientH] == NULL) return FALSE;

    // Check max level cap
    if (m_pClientList[iClientH]->m_iLevel >= DEF_PLAYERMAXLEVEL) {
        return FALSE;  // Already at max level
    }

    // Check if experience meets threshold
    if (m_pClientList[iClientH]->m_iExp >= m_pClientList[iClientH]->m_iNextLevelExp) {
        // Level up!
        m_pClientList[iClientH]->m_iLevel++;

        // Award stat points
        m_pClientList[iClientH]->m_iLU_Pool += DEF_TOTALLEVELUPPOINT;  // +3

        // Early level bonus (private server feature)
        if (m_pClientList[iClientH]->m_iLevel <= 5) {
            // Create gold item reward
            CreateGoldItem(iClientH, 100000);
        }

        // Update next level threshold
        m_pClientList[iClientH]->m_iNextLevelExp =
            m_iLevelExpTable[m_pClientList[iClientH]->m_iLevel + 1];

        // Notify client
        SendNotifyMsg(NULL, iClientH, DEF_NOTIFY_LEVELUP,
                      m_pClientList[iClientH]->m_iLevel, 0, 0, NULL);

        // Recursively check for multiple level-ups
        bCheckLevelUp(iClientH);

        return TRUE;
    }

    return FALSE;
}
```

### Key Behaviors

1. **Recursive Checking:** Allows multiple level-ups from large experience gains
2. **Immediate Notification:** Client is notified of each level-up
3. **Threshold Update:** Next level requirement updated after each level
4. **Point Accumulation:** Stat points are pooled, not forced allocation

---

## Stat Point Allocation

### Points Per Level

```cpp
#define DEF_TOTALLEVELUPPOINT  3  // Stat points awarded per level
```

### Total Points Formula

```
Total_Available_Points = Base_Points + ((Level - 1) * Points_Per_Level)
Total_Available_Points = 70 + ((Level - 1) * 3)
```

### Points by Level

| Level | Total Stat Points | Cumulative from Leveling |
|-------|-------------------|--------------------------|
| 1 | 70 | 0 |
| 10 | 97 | 27 |
| 50 | 217 | 147 |
| 100 | 367 | 297 |
| 150 | 517 | 447 |
| 180 | 607 | 537 |

### Stat Point Limits

```cpp
#define DEF_CHARPOINTLIMIT  1000  // Maximum points per individual stat
```

### Allocation

Players can allocate points to any of the 7 primary stats:
- **STR** (Strength)
- **DEX** (Dexterity)
- **VIT** (Vitality)
- **INT** (Intelligence)
- **MAG** (Magic)
- **CHR** (Charisma)
- **LCK** (Luck)

All stats follow the same allocation rules with no class-based restrictions.

---

## Experience Sources

Players can earn experience from multiple activities:

### Primary Sources

| Source | Description |
|--------|-------------|
| **Monster Kills** | Primary source; experience based on NPC type |
| **Player Kills (PvP)** | Experience from killing enemy faction players |
| **Quest Completion** | Rewards specified in quest definitions |

### Secondary Sources

| Source | Description |
|--------|-------------|
| **War/Crusade** | Contribution-based rewards during faction wars |
| **Crafting** | Experience from successful item creation |
| **Gathering** | Experience from mining, fishing activities |

### Experience Award Function

```cpp
void CGame::GetExp(int iClientH, int iExp, BOOL bIsAttackerOwn) {
    // Check for limited user restrictions
    if (bCheckLimitedUser(iClientH)) {
        // Apply trial account restrictions
    }

    // Add base experience
    m_pClientList[iClientH]->m_iExp += iExp;

    // Apply equipment bonus
    if (m_pClientList[iClientH]->m_iAddExp > 0) {
        int iBonus = (iExp * m_pClientList[iClientH]->m_iAddExp) / 100;
        m_pClientList[iClientH]->m_iExp += iBonus;
    }

    // Notify client
    SendNotifyMsg(NULL, iClientH, DEF_NOTIFY_EXP,
                  m_pClientList[iClientH]->m_iExp, iExp, 0, NULL);

    // Check for level up
    bCheckLevelUp(iClientH);
}
```

---

## Experience Modifiers

### Equipment Bonus

```cpp
int m_iAddExp;  // Percentage bonus from equipment
```

**Calculation:**
```
Final_EXP = Base_EXP + (Base_EXP * m_iAddExp / 100)
```

**Example:** With `m_iAddExp = 15` (15% bonus):
- Base kill: 100 EXP
- Bonus: 100 * 15 / 100 = 15 EXP
- Total: 115 EXP

### Zone-Based Modifiers (Planned Enhancement)

The modernized server should support zone-based experience modifiers:

| Zone Type | Modifier | Rationale |
|-----------|----------|-----------|
| Safe zones | -50% to -75% | Discourage farming in town |
| Normal zones | 100% (base) | Standard experience |
| Dangerous dungeons | +25% to +50% | Reward risk-taking |
| Event zones | Configurable | Special event bonuses |

*Note: This is a planned enhancement, not legacy behavior.*

---

## Party Experience Sharing

### Distribution Mechanics

When a party member kills an enemy, experience is shared among all eligible members:

```cpp
// Count eligible party members (alive, same map, not invisible)
int iPartyCount = 0;
for (int i = 0; i < DEF_MAXPARTYMEMBERS; i++) {
    if (PartyMember[i] != NULL &&
        PartyMember[i]->m_bIsAlive &&
        PartyMember[i]->m_cMapIndex == cKillerMapIndex &&
        !PartyMember[i]->m_bIsInvisible) {
        iPartyCount++;
    }
}

// Clamp to maximum
if (iPartyCount > 8) iPartyCount = 8;
```

### Party Bonus Calculation

```cpp
double dBaseExp = (double)iExp;
double dModifiedExp;

switch (iPartyCount) {
    case 1: dModifiedExp = dBaseExp; break;                           // 100%
    case 2: dModifiedExp = (dBaseExp + (dBaseExp * 0.02)) / 2.0; break;  // 102% / 2
    case 3: dModifiedExp = (dBaseExp + (dBaseExp * 0.05)) / 3.0; break;  // 105% / 3
    case 4: dModifiedExp = (dBaseExp + (dBaseExp * 0.07)) / 4.0; break;  // 107% / 4
    case 5: dModifiedExp = (dBaseExp + (dBaseExp * 0.10)) / 5.0; break;  // 110% / 5
    case 6: dModifiedExp = (dBaseExp + (dBaseExp * 0.14)) / 6.0; break;  // 114% / 6
    case 7: dModifiedExp = (dBaseExp + (dBaseExp * 0.17)) / 7.0; break;  // 117% / 7
    case 8: dModifiedExp = (dBaseExp + (dBaseExp * 0.20)) / 8.0; break;  // 120% / 8
}
```

### Party Bonus Summary

| Party Size | Pool Bonus | Per-Member Share | Effective Rate |
|------------|------------|------------------|----------------|
| 1 (solo) | 0% | 100% | 100% |
| 2 | +2% | 51% | 102% total |
| 3 | +5% | 35% | 105% total |
| 4 | +7% | 26.75% | 107% total |
| 5 | +10% | 22% | 110% total |
| 6 | +14% | 19% | 114% total |
| 7 | +17% | 16.7% | 117% total |
| 8 | +20% | 15% | 120% total |

### Eligibility Requirements

- Must be alive
- Must be on the same map as the killer
- Must not be invisible
- Maximum 8 members counted (clamped)

### Distribution Method

Legacy code uses equal split regardless of level differences. Each eligible member receives the same share.

---

## Experience Slate Buff

### Overview

The Experience Slate is a consumable item that grants a significant experience multiplier when activated.

### Activation

```cpp
// Status flag for Experience Slate buff
#define DEF_STATUS_EXPSLATE  0x10000

// Check for active buff
if (m_pClientList[iClientH]->m_iStatus & DEF_STATUS_EXPSLATE) {
    // Apply 3x multiplier
    iExp *= 3;
}
```

### Properties

| Property | Value |
|----------|-------|
| Multiplier | 3x experience |
| Duration | Time-based (configurable) |
| Source | Consumable item |
| Stacking | Does not stack with itself |

### Application

The slate buff multiplies experience after base calculation but before party distribution:

```
Final_EXP = Base_EXP * Equipment_Bonus * Slate_Multiplier
```

---

## Auto-Exp System

### Overview

Players passively accumulate experience while logged in and located in safe zones (towns).

### Timing

```cpp
#define DEF_AUTOEXPTIME  360000  // 6 minutes in milliseconds
```

### Mechanics

```cpp
void CGame::ProcessAutoExp(int iClientH) {
    DWORD dwCurrentTime = timeGetTime();

    // Check timer
    if ((dwCurrentTime - m_pClientList[iClientH]->m_dwAutoExpTime) < DEF_AUTOEXPTIME) {
        return;
    }

    // Update timer
    m_pClientList[iClientH]->m_dwAutoExpTime = dwCurrentTime;

    // Check if in safe zone
    if (!IsInSafeZone(iClientH)) {
        return;
    }

    // Award auto-exp
    int iAutoExp = CalculateAutoExp(iClientH);
    m_pClientList[iClientH]->m_iAutoExpAmount += iAutoExp;
    m_pClientList[iClientH]->m_iExpStock += iAutoExp;
}
```

### Zone Restrictions

| Zone Type | Auto-Exp Allowed |
|-----------|------------------|
| Aresden Town | Yes |
| Elvine Town | Yes |
| Starting Areas | Yes |
| Hunting Grounds | No |
| Dungeons | No |
| War Zones | No |

### Experience Stock Distribution

```cpp
#define DEF_EXPSTOCKTIME  10000  // 10 seconds in milliseconds

void CGame::CalcExpStock(int iClientH) {
    // Distribute pending experience
    if (m_pClientList[iClientH]->m_iExpStock > 0) {
        m_pClientList[iClientH]->m_iExp += m_pClientList[iClientH]->m_iExpStock;
        m_pClientList[iClientH]->m_iExpStock = 0;

        // Notify client
        SendNotifyMsg(NULL, iClientH, DEF_NOTIFY_EXP,
                      m_pClientList[iClientH]->m_iExp, 0, 0, NULL);

        // Check for level up
        bCheckLevelUp(iClientH);
    }
}
```

---

## Death Penalties

### Legacy Behavior

The legacy code applies experience penalties for PvP deaths only. The penalty amount is trivial.

### PvP Death Penalty Formula

```cpp
// When killed by another player
int iExpLoss = iDice((iVictimLevel / 2 + 1), 50);
m_pClientList[iVictimH]->m_iExp -= iExpLoss;

// Ensure experience doesn't go negative
if (m_pClientList[iVictimH]->m_iExp < 0) {
    m_pClientList[iVictimH]->m_iExp = 0;
}
```

### Penalty Calculation

```
EXP_Loss = 1d((Level/2 + 1)) * 50

Where:
- 1d(N) = Random roll from 1 to N
- Result: 1 to ((Level/2 + 1) * 50) experience lost
```

### Example Penalties

| Victim Level | Max Loss | Average Loss |
|--------------|----------|--------------|
| 10 | 300 | ~150 |
| 50 | 1,300 | ~650 |
| 100 | 2,550 | ~1,275 |
| 180 | 4,550 | ~2,275 |

### De-Leveling

If configured to allow de-leveling:

```cpp
// Check if experience dropped below current level threshold
if (m_pClientList[iClientH]->m_iExp < m_iLevelExpTable[m_pClientList[iClientH]->m_iLevel]) {
    // De-level
    m_pClientList[iClientH]->m_iLevel--;

    // Remove stat points
    m_pClientList[iClientH]->m_iLU_Pool -= DEF_TOTALLEVELUPPOINT;  // -3 points

    // Handle negative pool (force stat reduction)
    if (m_pClientList[iClientH]->m_iLU_Pool < 0) {
        // Reduce allocated stats
        ForceStatReduction(iClientH, -m_pClientList[iClientH]->m_iLU_Pool);
        m_pClientList[iClientH]->m_iLU_Pool = 0;
    }

    // Update next level threshold
    m_pClientList[iClientH]->m_iNextLevelExp =
        m_iLevelExpTable[m_pClientList[iClientH]->m_iLevel + 1];

    // Notify client
    SendNotifyMsg(NULL, iClientH, DEF_NOTIFY_LEVELDOWN,
                  m_pClientList[iClientH]->m_iLevel, 0, 0, NULL);
}
```

---

## Majestic Points System

### Overview

Players who reach level 180 can continue earning experience. Experience that would have caused level-ups is converted to Majestic Points instead.

### Conversion Mechanics

```cpp
void CGame::ProcessPostCapExperience(int iClientH) {
    // Only for max level players
    if (m_pClientList[iClientH]->m_iLevel < DEF_PLAYERMAXLEVEL) {
        return;
    }

    // Check if would-be level-up occurred
    while (m_pClientList[iClientH]->m_iExp >= m_iLevelExpTable[181]) {
        // Convert to Majestic Point
        m_pClientList[iClientH]->m_iMajesticPoint++;

        // Subtract the experience cost
        m_pClientList[iClientH]->m_iExp -= m_iLevelExpTable[181];

        // Notify client
        SendNotifyMsg(NULL, iClientH, DEF_NOTIFY_MAJESTICPOINT,
                      m_pClientList[iClientH]->m_iMajesticPoint, 0, 0, NULL);
    }
}
```

### Conversion Rate

```
1 Majestic Point = 1 "would-be level" worth of experience
                 = m_iLevelExpTable[181] experience points
```

### Usage

Majestic Points are used to upgrade items:
- Enhance equipment stats
- Apply special bonuses
- Unlock item potential

*Note: Specific upgrade mechanics are part of the Item System.*

---

## Early Level Rewards

### Legacy Behavior (Private Server)

The legacy private server code awards gold to new characters:

```cpp
if (m_pClientList[iClientH]->m_iLevel <= 5) {
    CreateGoldItem(iClientH, 100000);  // 100,000 gold per level
}
```

### Modernization Recommendation

Make this feature configurable:

```yaml
# server_config.yaml
early_level_rewards:
  enabled: true
  max_level: 10           # Stop rewards after this level
  gold_per_level: 1000    # Gold awarded per level-up
```

### Rationale

- Original amount (100k/level) is excessive
- Configurable allows server operators to tune economy
- Lower default (1,000/level) keeps gold valuable

---

## Limited User Restrictions

### Overview

"Limited users" are trial or free accounts with restricted experience gain.

### Check Function

```cpp
BOOL CGame::bCheckLimitedUser(int iClientH) {
    // Check account flags
    if (m_pClientList[iClientH]->m_bIsLimitedUser) {
        return TRUE;
    }
    return FALSE;
}
```

### Restrictions Applied

| Restriction | Description |
|-------------|-------------|
| EXP Reduction | Reduced experience gain percentage |
| Level Cap | Maximum level limit for trial accounts |
| Feature Lock | Some progression features disabled |

*Note: Exact restrictions vary by server configuration.*

---

## Key Functions Reference

### Experience Management

| Function | Purpose |
|----------|---------|
| `GetExp(int, int, BOOL)` | Award experience to player |
| `bCheckLevelUp(int)` | Check and process level-up |
| `CalcExpStock(int)` | Distribute pending experience |
| `ProcessAutoExp(int)` | Handle passive experience gain |

### Level Calculation

| Function | Purpose |
|----------|---------|
| `iGetLevelExp(int)` | Get experience threshold for level |
| `iGetExpLevel(int)` | Get level from experience amount |

### Experience Table

| Function | Purpose |
|----------|---------|
| `InitLevelExpTable()` | Calculate all level thresholds |

### Death Handling

| Function | Purpose |
|----------|---------|
| `ApplyDeathPenalty(int)` | Apply experience loss on death |
| `CheckDeLevelUp(int)` | Check for de-leveling |

### Majestic Points

| Function | Purpose |
|----------|---------|
| `ProcessPostCapExperience(int)` | Convert excess EXP to MP |

---

## Constants Reference

### Limits

| Constant | Value | Description |
|----------|-------|-------------|
| `DEF_PLAYERMAXLEVEL` | 180 | Maximum player level |
| `DEF_TOTALLEVELUPPOINT` | 3 | Stat points per level |
| `DEF_CHARPOINTLIMIT` | 1000 | Maximum per stat |

### Timing

| Constant | Value | Description |
|----------|-------|-------------|
| `DEF_EXPSTOCKTIME` | 10000 | Exp stock interval (ms) |
| `DEF_AUTOEXPTIME` | 360000 | Auto-exp interval (ms) |

### Status Flags

| Constant | Value | Description |
|----------|-------|-------------|
| `DEF_STATUS_EXPSLATE` | 0x10000 | Experience Slate buff active |

---

## Interactions with Other Systems

### Combat System

- Monster kills award base experience
- PvP kills may award or penalize experience
- Damage contribution may affect party distribution

### Party System

- Experience shared among party members
- Bonus scaling based on party size
- Eligibility checks (alive, same map)

### Quest System

- Quest completion awards experience
- Quest definitions specify rewards
- Some quests have repeatable exp rewards

### Item System

- Equipment can provide `m_iAddExp` bonus
- Experience Slate consumable grants 3x buff
- Majestic Points used for item upgrades

### NPC System

- NPC type determines base experience
- Quest NPCs may trigger exp rewards
- Summoned creatures don't share exp

### War/Crusade System

- War contribution grants experience
- Special war objectives may award bonus exp
- Victory bonuses for winning faction

### World System

- Zone determines auto-exp eligibility
- Safe zones allow passive exp gain
- Zone modifiers affect exp rates (planned)

---

## Persistence Format

### Character Save Data

```ini
[STATUS]
character-LEVEL = 50
character-EXP = 84000
character-LU_Pool = 12
character-MajesticPoint = 5

[STATS]
character-STR = 50
character-DEX = 60
character-VIT = 40
character-INT = 30
character-MAG = 45
character-CHR = 20
character-LCK = 25
```

### Database Schema (Modernized)

```sql
CREATE TABLE characters (
    id SERIAL PRIMARY KEY,
    account_id INTEGER REFERENCES accounts(id),
    name VARCHAR(20) NOT NULL,
    level INTEGER DEFAULT 1,
    experience BIGINT DEFAULT 0,
    stat_points INTEGER DEFAULT 70,
    majestic_points INTEGER DEFAULT 0,
    -- Stats
    str INTEGER DEFAULT 10,
    dex INTEGER DEFAULT 10,
    vit INTEGER DEFAULT 10,
    int INTEGER DEFAULT 10,
    mag INTEGER DEFAULT 10,
    chr INTEGER DEFAULT 10,
    lck INTEGER DEFAULT 10
);
```

---

## Modernization Notes

### Configurable Features

The modernized server should make these settings configurable:

```yaml
experience:
  # Level cap
  max_level: 180

  # Stat points
  base_stat_points: 70
  points_per_level: 3
  max_stat_value: 1000

  # Early rewards
  early_level_rewards:
    enabled: true
    max_level: 10
    gold_per_level: 1000

  # Death penalties
  death_penalty:
    enabled: true
    pvp_only: true      # Only PvP deaths cause loss
    allow_delevel: false # Can lose levels from death
    penalty_formula: "level * 10"  # Simplified formula

  # Auto-exp
  auto_exp:
    enabled: true
    interval_seconds: 360
    safe_zones_only: true
    base_amount: 10

  # Party sharing
  party:
    max_share_members: 8
    level_weighted: false  # Equal split vs level-based

  # Zone modifiers (planned)
  zone_modifiers:
    safe_zone_multiplier: 0.5
    dungeon_multiplier: 1.25
```

### Key Differences from Legacy

| Aspect | Legacy | Modernized |
|--------|--------|------------|
| Config | Hardcoded | YAML configurable |
| Death Penalty | Trivial amounts | Configurable formula |
| De-leveling | Not clear | Explicit toggle |
| Zone modifiers | None | Planned feature |
| Party split | Equal only | Optionally weighted |
| Early rewards | 100k gold | Configurable amount |

### Planned Enhancements

1. **Zone-Based Modifiers:** Different areas grant different exp rates
2. **Level-Weighted Party Split:** Higher levels contribute more, get more
3. **Rested Experience:** Bonus exp after being offline
4. **Experience Events:** Server-wide multiplier events
5. **Achievement Bonuses:** One-time exp for accomplishments

---

## Known Issues and Edge Cases

1. **Integer Overflow:** Very high experience values may overflow int32
2. **Recursive Level-Up:** Large exp gains trigger multiple notifications
3. **Party Edge Cases:** Members joining/leaving mid-combat
4. **De-level Stat Loss:** Negative LU_Pool requires forced stat reduction
5. **Auto-Exp Abuse:** Players may AFK in safe zones indefinitely

---

## Formula Reference

### Experience for Level N

```
EXP(0) = 0
EXP(N) = EXP(N-1) + N * (50 + (N * (N/17)^2))

Simplified approximation for high levels:
EXP(N) ≈ N^4 / 289 + 25 * N^2
```

### Total Stat Points at Level N

```
Points(N) = 70 + (N - 1) * 3
```

### Party Experience Share

```
Share(members) = (BaseEXP * (1 + Bonus[members])) / members

Where Bonus = {1:0%, 2:2%, 3:5%, 4:7%, 5:10%, 6:14%, 7:17%, 8:20%}
```

### Death Penalty

```
Loss = random(1, (Level/2 + 1)) * 50
```
