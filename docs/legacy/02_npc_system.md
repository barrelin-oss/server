# Legacy NPC System Documentation

**System:** NPC (Non-Player Character) Management
**Primary Files:** `Npc.cpp/h`, `Game.cpp/h`
**Estimated Lines:** ~3,000+ lines across Game.cpp
**Complexity:** High

---

## Table of Contents

1. [Overview](#overview)
2. [Constants & Limits](#constants--limits)
3. [CNpc Class Definition](#cnpc-class-definition)
4. [NPC Types & Categories](#npc-types--categories)
5. [Movement System](#movement-system)
6. [Behavior State Machine](#behavior-state-machine)
7. [Combat & AI](#combat--ai)
8. [Magic System Integration](#magic-system-integration)
9. [Summoning & Follow System](#summoning--follow-system)
10. [Loot & Item Drops](#loot--item-drops)
11. [Crusade Structures](#crusade-structures)
12. [NPC Processing Loop](#npc-processing-loop)
13. [Configuration & Loading](#configuration--loading)
14. [Key Algorithms](#key-algorithms)
15. [Function Reference](#function-reference)
16. [Modernization Notes](#modernization-notes)

---

## Overview

The NPC system manages all non-player characters in the game, including:

- **Combat NPCs**: Monsters, bosses, and hostile creatures
- **Shop NPCs**: Merchants, blacksmiths, and service providers
- **Guard NPCs**: City guards and protectors
- **Quest NPCs**: Quest givers and story characters
- **Summoned NPCs**: Player-summoned creatures
- **Crusade Structures**: Towers, collectors, and war machines

The system handles spawning, AI behavior, pathfinding, combat, death, and loot generation for all NPCs.

---

## Constants & Limits

### Core Limits

```cpp
DEF_MAXNPCS          = 5000    // Maximum active NPCs on server
DEF_MAXNPCTYPES      = 200     // Maximum NPC template definitions
DEF_MAXNPCITEMS      = 1000    // Maximum simultaneous NPC item drops
DEF_MAXWAYPOINTS     = 10      // Maximum waypoints per NPC
```

### Movement Types

```cpp
DEF_MOVETYPE_STOP           = 0  // NPC stands still at position
DEF_MOVETYPE_SEQWAYPOINT    = 1  // Follows waypoints in sequence (1->2->3->1...)
DEF_MOVETYPE_RANDOMWAYPOINT = 2  // Randomly selects next waypoint
DEF_MOVETYPE_FOLLOW         = 3  // Follows another entity
DEF_MOVETYPE_RANDOMAREA     = 4  // Moves randomly within defined RECT
DEF_MOVETYPE_RANDOM         = 5  // Moves randomly anywhere on map
DEF_MOVETYPE_GUARD          = 6  // Returns to home position after combat
```

### Behavior States

```cpp
DEF_BEHAVIOR_STOP   = 0  // Stationary/idle
DEF_BEHAVIOR_MOVE   = 1  // Moving to destination
DEF_BEHAVIOR_ATTACK = 2  // Engaging target in combat
DEF_BEHAVIOR_FLEE   = 3  // Running from danger
DEF_BEHAVIOR_DEAD   = 4  // Dead, awaiting cleanup
```

### Action Limits

```cpp
m_cActionLimit values:
  0 = Normal (full behavior)
  1 = Move-only (cannot attack)
  2 = Stop-only (cannot move or attack)
  3 = Dummy (decoration, no behavior)
  4 = Building (crusade structure)
  5 = Special Structure (unique behavior)
```

### Attack Strategies

```cpp
DEF_ATTACKAI_NORMAL         = 1  // Standard attack behavior
DEF_ATTACKAI_EXCHANGEATTACK = 2  // Attack once then flee
DEF_ATTACKAI_TWOBYONEATTACK = 3  // Attack twice then flee
```

### Side Affiliations

```cpp
m_cSide values:
  0     = Neutral (attacks PKers only)
  1     = Aresden (attacks Elvine and PKers)
  2     = Elvine (attacks Aresden and PKers)
  >= 10 = Special (attacks opposite side and neutral NPCs)
```

### Special Abilities

```cpp
m_cSpecialAbility values:
  0 = None
  1 = Penetrating Invisibility (can see invisible players)
  2 = Breaking Magic Protection (ignores magic barriers)
  3 = Absorbing Physical Damage (reduces physical damage)
  4 = Absorbing Magical Damage (reduces magic damage)
  5 = Poisonous (attacks inflict poison)
  6 = Extremely Poisonous (attacks inflict severe poison)
  7 = Explosive (explodes on death)
  8 = Hi-Explosive (larger explosion on death)
```

---

## CNpc Class Definition

### Source Files

- `Npc.h` - Class declaration
- `Npc.cpp` - Constructor/destructor implementation

### Member Variables

#### Identification & Basic Info

| Variable | Type | Description |
|----------|------|-------------|
| `m_cNpcName[21]` | char[] | Display name shown to players |
| `m_cName[6]` | char[] | Internal 5-character name code |
| `m_cMapIndex` | char | Map index (0-99) where NPC exists |
| `m_sType` | short | NPC type ID from template |
| `m_sOriginalType` | short | Original type (preserved across transformations) |

#### Position & Movement

| Variable | Type | Description |
|----------|------|-------------|
| `m_sX, m_sY` | short | Current tile position |
| `m_dX, m_dY` | short | Destination tile position |
| `m_vX, m_vY` | short | Previous visited position |
| `m_cDir` | char | Facing direction (1-8, compass) |
| `m_cTurn` | char | Turn preference (0=right, 1=left) |
| `m_tmp_iError` | int | Pathfinding error code |

#### Movement Configuration

| Variable | Type | Description |
|----------|------|-------------|
| `m_cMoveType` | char | Movement behavior type (DEF_MOVETYPE_*) |
| `m_cCurWaypoint` | char | Current waypoint index (0-10) |
| `m_cTotalWaypoint` | char | Total waypoints defined |
| `m_iWayPointIndex[11]` | int[] | Array of waypoint IDs (-1 = unused) |
| `m_rcRandomArea` | RECT | Bounding rectangle for RANDOMAREA mode |

#### Combat Statistics

| Variable | Type | Description |
|----------|------|-------------|
| `m_iHP` | int | Current hit points |
| `m_iHitDice` | int | HP dice for regeneration/max HP calculation |
| `m_iDefenseRatio` | int | Defense rating (reduces damage) |
| `m_iHitRatio` | int | Physical attack accuracy |
| `m_iMagicHitRatio` | int | Magical attack accuracy |
| `m_cAttackDiceThrow` | char | Attack damage: number of dice |
| `m_cAttackDiceRange` | char | Attack damage: dice sides |
| `m_cAttackBonus` | char | Attack damage: flat bonus |
| `m_iLastDamage` | int | Last damage taken (for animation) |

#### Bravery & Morale

| Variable | Type | Description |
|----------|------|-------------|
| `m_cBravery` | char | Current bravery (0-10) |
| `m_iMinBravery` | int | Minimum bravery threshold |

> **Bravery System**: When danger exceeds bravery, NPC may flee. Low HP combined with low bravery triggers flee behavior.

#### Magic & Mana

| Variable | Type | Description |
|----------|------|-------------|
| `m_cMagicLevel` | char | Magic ability level (0-13) |
| `m_iMana` | int | Current mana points |
| `m_iMaxMana` | int | Maximum mana points |
| `m_cResistMagic` | char | Magic resistance level |

#### Appearance & Size

| Variable | Type | Description |
|----------|------|-------------|
| `m_cSize` | char | 0=Small/Medium, 1=Large |
| `m_sAppr2` | short | Appearance encoding (equipment/effects) |

##### Appearance Encoding (m_sAppr2)

```
Bits 12-15: Equipment slot 1 (13 types)
Bits 0-7:   Armor type (9 types)
0xF000:     Aura/special effect indicator
```

#### Status & Effects

| Variable | Type | Description |
|----------|------|-------------|
| `m_iStatus` | int | Status flags (berserk, etc.) |
| `m_cMagicEffectStatus[100]` | char[] | Active magic effects array |
| `m_cAttribute` | char | Element type (1=fire, 2=water, 3=wind, 4=earth) |
| `m_iAbsDamage` | int | Absolute/bypass damage amount |

#### AI & Behavior

| Variable | Type | Description |
|----------|------|-------------|
| `m_cBehavior` | char | Current behavior state (DEF_BEHAVIOR_*) |
| `m_sBehaviorTurnCount` | short | Behavior action counter |
| `m_cActionLimit` | char | Behavior restriction level (0-5) |
| `m_cTargetSearchRange` | char | Detection radius for enemies |
| `m_iTargetIndex` | int | Current target's index |
| `m_cTargetType` | char | Target type (NPC or Player) |
| `m_iAILevel` | int | Intelligence (1=basic, 2=tactical, 3=smart) |
| `m_iAttackStrategy` | int | Attack pattern (DEF_ATTACKAI_*) |
| `m_iAttackRange` | int | Extended attack range (ranged NPCs) |
| `m_iAttackCount` | int | Attack counter for strategies |
| `m_bIsPermAttackMode` | BOOL | Permanent attack mode flag |
| `m_iNoDieRemainExp` | int | Reserved experience value |

#### Follow & Summon System

| Variable | Type | Description |
|----------|------|-------------|
| `m_iFollowOwnerIndex` | int | Index of entity being followed |
| `m_cFollowOwnerType` | char | Type of follow owner |
| `m_bIsSummoned` | BOOL | Is this a summoned NPC |
| `m_dwSummonedTime` | DWORD | Timestamp when summoned |
| `m_iSummonControlMode` | int | 0=Free, 1=Hold, 2=Target |
| `m_bIsMaster` | BOOL | Is master summoned creature |
| `m_iGuildGUID` | int | Guild ownership for war units |

#### Experience & Loot

| Variable | Type | Description |
|----------|------|-------------|
| `m_iExp` | int | Base experience reward |
| `m_iExpDiceMin, m_iExpDiceMax` | int | Experience dice roll range |
| `m_iGoldDiceMin, m_iGoldDiceMax` | int | Gold drop dice roll range |
| `m_iItemRatio` | int | Item drop rate percentage |
| `m_iAssignedItem` | int | Forced item drop ID (if any) |

#### State & Lifecycle

| Variable | Type | Description |
|----------|------|-------------|
| `m_bIsKilled` | BOOL | NPC is dead |
| `m_bIsUnsummoned` | BOOL | NPC was unsummoned |
| `m_dwDeadTime` | DWORD | Timestamp of death |

#### Timing

| Variable | Type | Description |
|----------|------|-------------|
| `m_dwTime` | DWORD | General action timer |
| `m_dwActionTime` | DWORD | Action speed interval (ms) |
| `m_dwHPupTime` | DWORD | HP regeneration timer |
| `m_dwMPupTime` | DWORD | MP regeneration timer |
| `m_dwRegenTime` | DWORD | Regeneration reference time |

#### Special Systems

| Variable | Type | Description |
|----------|------|-------------|
| `m_iBuildCount` | int | Construction progress (crusade) |
| `m_iManaStock` | int | Mana pool for special systems |
| `m_cDayOfWeekLimit` | char | Day restriction (>=10 = no limit) |
| `m_cChatMsgPresence` | char | Has chat message flag |
| `m_cSide` | char | Faction affiliation |
| `m_cCropType` | char | Crop type (farming NPCs) |
| `m_cCropSkill` | char | Crop skill requirement |
| `m_cArea` | char | Zone/area assignment |
| `m_iSpotMobIndex` | int | Spot mob generator reference |

### Constructor

```cpp
CNpc::CNpc(char * pName5)
```

- Copies 5-character name to `m_cName`
- Initializes all waypoints to -1 (unused)
- Clears magic effect status array
- Sets all counters and flags to default values

### Destructor

```cpp
CNpc::~CNpc()
```

- Currently empty (no dynamic allocations to free)

---

## NPC Types & Categories

### Monster Types

| Type | Name | Notes |
|------|------|-------|
| 10 | Slime | Basic monster |
| 11 | Skeleton | Undead |
| 12 | Stone-Golem | High defense |
| 13 | Cyclops | Large creature |
| 14 | Orc/Orc-Mage | Magic level 3 |
| 16 | Giant-Ant | Swarm type |
| 17 | Scorpion | Poison ability |
| 18 | Zombie | Undead |
| 22 | Amphis | Amphibian |
| 23 | Clay-Golem | Construct |
| 27 | Hellbound | Demon type |
| 28 | Troll | Regenerating |
| 29 | Ogre | Large humanoid |
| 30 | Liche | High magic |
| 31 | Demon | Magic level 7 |
| 32 | Unicorn | Guard type |
| 33 | WereWolf | Transform type |
| 48-62 | Various | Mid-tier monsters |
| 63-65 | Ice Monsters | Ice element |
| 66 | Wyvern | Multi-drop boss |
| 70 | Barlog | Magic level 7 |
| 73 | Fire-Wyvern | Multi-drop boss |
| 79 | Nizie | Magic level 10 |
| 81 | Abaddon | Final boss, multi-drop |
| 87, 89 | Special | Unique attack patterns |

### Crusade Structure Types

| Type | Name | Appr2 | Function |
|------|------|-------|----------|
| 36 | Crossbow Guard Tower | 3 | Ranged defense |
| 37 | Cannon Guard Tower | 3 | Heavy ranged defense |
| 38 | Mana Collector | 3 | Collects mana from kills |
| 39 | Detector | 3 | Detects enemy presence |
| 40 | Energy Shield Generator | - | Creates protective barrier |
| 41 | Grand Magic Generator | - | Enables meteor strikes |
| 42 | Mana Stone | 3 | Accumulates mana passively |

### Special Types

| Type | Name | Function |
|------|------|----------|
| 51 | Catapult | Siege weapon |
| 52 | Gargoyle | Magic level 7 |
| 53 | Beholder | Magic type |
| 54 | Dark Elf | Humanoid caster |
| 64 | Crop | Farming system (Appr2 = growth stage) |

---

## Movement System

### Movement Type Behaviors

#### STOP (Type 0)
- NPC remains at spawn position
- Used for: Shop NPCs, quest givers, guards

#### SEQWAYPOINT (Type 1)
- Follows waypoints in order: 1 → 2 → 3 → 1 → ...
- `m_cCurWaypoint` tracks current waypoint
- `m_cTotalWaypoint` defines sequence length

#### RANDOMWAYPOINT (Type 2)
- Randomly selects next waypoint from list
- Uses `iDice()` to pick waypoint index
- Can revisit same waypoint

#### FOLLOW (Type 3)
- Follows `m_iFollowOwnerIndex` entity
- Maintains close distance to owner
- Inherits side affiliation from owner
- Used for: Summoned creatures, pets

#### RANDOMAREA (Type 4)
- Moves randomly within `m_rcRandomArea` bounds
- Common for patrol zones
- Respects walkability constraints

#### RANDOM (Type 5)
- Moves to any walkable tile on map
- Maximum freedom of movement

#### GUARD (Type 6)
- Returns to home position after combat
- Defends assigned area
- Used for: City guards, structure guards

### Waypoint System

```cpp
// Waypoint array
int m_iWayPointIndex[DEF_MAXWAYPOINTS + 1];  // 11 slots, -1 = unused

// Processing
void CalcNextWayPointDestination(int iNpcH)
{
    // Based on m_cMoveType:
    // SEQWAYPOINT: m_cCurWaypoint = (m_cCurWaypoint + 1) % m_cTotalWaypoint
    // RANDOMWAYPOINT: m_cCurWaypoint = iDice(1, m_cTotalWaypoint) - 1
    // Sets m_dX, m_dY from waypoint position
}
```

### Pathfinding

```cpp
char cGetNextMoveDir(short sX, short sY, short dstX, short dstY,
                     char cMapIndex, char cTurn, int * pError)
```

- Uses `Misc.GetPoint()` for A* pathfinding
- Returns direction 1-8 (compass directions)
- Considers turn preference (`m_cTurn`)
- Validates tile walkability
- Sets error code on failure

---

## Behavior State Machine

### State Diagram

```
                    ┌──────────────────────────────────────┐
                    │                                      │
                    ▼                                      │
┌──────┐  target   ┌────────┐  target lost   ┌──────┐     │
│ STOP │ ────────► │ ATTACK │ ─────────────► │ MOVE │ ────┘
└──────┘  found    └────────┘                └──────┘
    │                  │                         │
    │                  │ danger > bravery        │ target
    │                  │ or low HP               │ found
    │                  ▼                         │
    │              ┌──────┐                      │
    │              │ FLEE │ ◄────────────────────┤
    │              └──────┘                      │
    │                  │                         │
    │                  │ safe                    │
    │                  ▼                         │
    │              ┌──────┐                      │
    │              │ MOVE │ ─────────────────────┘
    │              └──────┘
    │
    │ killed
    ▼
┌──────┐  timeout   ┌───────────┐
│ DEAD │ ─────────► │ DeleteNpc │
└──────┘            └───────────┘
```

### State Handlers

#### NpcBehavior_Stop()

```cpp
void CGame::NpcBehavior_Stop(int iNpcH)
```

- Used for stationary NPCs
- Can still search for targets (if action limit allows)
- Special handling for crusade structures:
  - Type 38: Calls `_bNpcBehavior_ManaCollector()`
  - Type 39: Calls `_bNpcBehavior_Detector()`
  - Type 41: Calls `_NpcBehavior_GrandMagicGenerator()`
  - Type 42: Accumulates mana passively

#### NpcBehavior_Move()

```cpp
void CGame::NpcBehavior_Move(int iNpcH)
```

1. Check action limit (types 2,3,5 cannot move)
2. Increment behavior turn counter
3. Call `TargetSearch()` for enemies
4. If target found → transition to ATTACK
5. Handle FOLLOW mode (track owner position)
6. Get next direction via `cGetNextMoveDir()`
7. Validate new position walkability
8. Update map system with new position
9. Send movement event to nearby clients

#### NpcBehavior_Attack()

```cpp
void CGame::NpcBehavior_Attack(int iNpcH)
```

1. Check action limit (types 1-4 cannot attack)
2. Evaluate danger vs bravery
3. Check HP threshold for flee
4. If target adjacent:
   - Execute melee attack
   - Apply damage and effects
5. If target distant and has range:
   - Execute ranged/magic attack
6. Handle attack strategies:
   - NORMAL: Continue attacking
   - EXCHANGEATTACK: Attack once, then flee
   - TWOBYONEATTACK: Attack twice, then flee
7. Increment attack counter
8. Check for target loss → transition to MOVE

#### NpcBehavior_Flee()

```cpp
void CGame::NpcBehavior_Flee(int iNpcH)
```

1. Evaluate all 8 directions
2. Use `iGetDangerValue()` for each direction
3. Select safest direction
4. Move away from threats
5. When safe → transition to MOVE

#### NpcBehavior_Dead()

```cpp
void CGame::NpcBehavior_Dead(int iNpcH)
```

1. Track dead time counter
2. Wait for cleanup timeout
3. Call `DeleteNpc()` when ready

---

## Combat & AI

### Target Acquisition

```cpp
void TargetSearch(int iNpcH, short * pTarget, char * pTargetType)
```

**Search Process:**
1. Scan tiles within `m_cTargetSearchRange` radius
2. Use map `GetOwner()` to identify entities
3. Filter targets:
   - Exclude self
   - Exclude allies (same side)
   - Exclude invisible entities
   - Exclude admins (observer mode)
4. Select closest valid target
5. Smart targeting based on AI level:
   - Level 1: Closest target
   - Level 2: Weakest nearby target
   - Level 3: Most strategic target

**Side Logic:**
```cpp
switch (m_cSide) {
    case 0:  // Neutral - attacks PKers only
    case 1:  // Aresden - attacks Elvine + PKers
    case 2:  // Elvine - attacks Aresden + PKers
    default: // >= 10 Special - attacks opposite + neutrals
}
```

### Danger Evaluation

```cpp
int iGetDangerValue(int iNpcH, short dX, short dY)
```

- Counts hostile entities near target position
- Returns danger score
- Used for:
  - Flee direction selection
  - Attack/flee decision making
  - Bravery comparison

### Attack Execution

**Melee Attack Flow:**
1. Validate target is adjacent (distance = 1)
2. Calculate hit chance: `m_iHitRatio` vs target defense
3. Roll for hit/miss
4. Calculate damage: `iDice(m_cAttackDiceThrow, m_cAttackDiceRange) + m_cAttackBonus`
5. Apply armor reduction
6. Apply special effects (poison, etc.)
7. Send damage event to clients

**Ranged Attack Flow:**
1. Validate target within `m_iAttackRange`
2. Line-of-sight check
3. Execute ranged attack calculation
4. Apply distance modifiers
5. Process hit/damage

### Attack Strategies

```cpp
// DEF_ATTACKAI_NORMAL (1)
// - Continues attacking until target dead or lost

// DEF_ATTACKAI_EXCHANGEATTACK (2)
// - Attack once, then flee
// - Returns after cooldown

// DEF_ATTACKAI_TWOBYONEATTACK (3)
// - Attack twice, then flee
// - Uses m_iAttackCount to track hits
```

### Assistance Calls

```cpp
void NpcRequestAssistance(int iNpcH)
```

- Notifies nearby allied NPCs of attack
- May trigger reinforcement spawns
- Extends combat radius

---

## Magic System Integration

### Magic Levels

| Level | Description | NPCs |
|-------|-------------|------|
| 0 | No magic | Most basic monsters |
| 1 | Basic (FireBolt) | Low-tier casters |
| 2 | Intermediate (Lightning) | Mid-tier casters |
| 3 | Orc-Mage level | Orc Mages |
| 4 | Advanced | Higher casters |
| 5 | Rudolph/Cyclops level | Special monsters |
| 6 | Tentacle/Liche level | Boss-tier |
| 7 | Barlog/Demon level | High bosses |
| 8 | Unicorn/Centaurus | Guard types |
| 9 | Tigerworm | Rare |
| 10 | Frost/Nizie | Ice element |
| 11 | Ice-Golem | Ice boss |
| 12 | Wyvern | Dragon type |
| 13 | Abaddon | Final boss |
| Negative | Buff spells | Support types |

### Magic Handler

```cpp
void NpcMagicHandler(int iNpcH, short dX, short dY, short sType)
```

1. Check mana availability
2. Deduct mana cost
3. Calculate magic hit chance using `m_iMagicHitRatio`
4. Apply spell effects
5. Send magic event to clients

### Spell Selection

NPCs select spells based on:
- `m_cMagicLevel` determines available spells
- Current mana availability
- Target distance and type
- Random selection from valid spells

---

## Summoning & Follow System

### Summoned NPCs

```cpp
// Key variables
m_bIsSummoned      // TRUE if summoned by player
m_dwSummonedTime   // When summoned
m_iSummonControlMode // Control behavior

// Control modes
0 = Free       // Attacks nearby enemies
1 = Hold       // Stays in position
2 = Target     // Attacks specific target
```

### Summoning Timeout

```cpp
// In NpcProcess():
if (m_bIsSummoned) {
    if (GetTickCount() - m_dwSummonedTime > DEF_SUMMONTIME) {
        // Unsummon the NPC
        m_bIsUnsummoned = TRUE;
    }
}

// DEF_SUMMONTIME = 300000 (5 minutes)
```

### Follow Mode

```cpp
BOOL bSetNpcFollowMode(char * pName, char * pFollowName, char cFollowOwnerType)
```

1. Find NPC by name
2. Find follow target (NPC or Player)
3. Validate both on same map
4. Set `m_cMoveType = DEF_MOVETYPE_FOLLOW`
5. Set follow owner info
6. Copy side affiliation from target

### Summoned NPC Restrictions

- Do not regenerate HP
- Have fixed lifetime
- Disappear when owner disconnects
- Can be controlled by owner

---

## Loot & Item Drops

### Drop Generation

```cpp
void NpcDeadItemGenerator(int iNpcH, short sAttackerH, char cAttackerType)
```

1. Check `m_iItemRatio` for drop chance
2. If assigned item: drop specific item
3. Otherwise: roll from loot table

### Single Item Drops

```cpp
BOOL bGetItemNameWhenDeleteNpc(int & iItemID, short sNpcType)
```

- NPC type-specific loot tables
- Uses `iDice()` for probability
- Returns item ID if drop succeeds

### Multi-Item Drops

```cpp
BOOL bGetMultipleItemNamesWhenDeleteNpc(
    short sNpcType,
    int iProbability,
    int iMin, int iMax,          // Item count range
    short sBaseX, short sBaseY,   // Drop location
    int iSpreadType,              // FIXED or RANDOM
    int iSpreadRange,             // Spread distance
    int iItemIDs[],               // Output array
    POINT ItemPositions[],        // Output positions
    int * piNumItem               // Output count
)
```

Used for bosses:
- Type 66 (Wyvern)
- Type 73 (Fire-Wyvern)
- Type 81 (Abaddon)

### Rare Drops

```cpp
// Very rare slate drop (1 in 100,000 chance)
if (iDice(1, 100000) == 1) {
    // Drop rare slate item
}
```

### Item Creation Flow

1. Generate item ID from loot table
2. Create CItem instance
3. Set item properties (count, effects, durability)
4. Find valid ground position
5. Place item on map
6. Set touch effect for pickup
7. Log item drop

---

## Crusade Structures

### Structure Types

| Type | Name | Behavior |
|------|------|----------|
| 36 | Crossbow Guard Tower | Attacks enemies in range |
| 37 | Cannon Guard Tower | Heavy attacks, slow rate |
| 38 | Mana Collector | Collects mana from nearby kills |
| 39 | Detector | Reveals enemy positions |
| 40 | Energy Shield | Creates protective barrier |
| 41 | Grand Magic Generator | Enables faction meteor strikes |
| 42 | Mana Stone | Passively accumulates mana |

### Mana Collector Behavior

```cpp
BOOL _bNpcBehavior_ManaCollector(int iNpcH)
```

- Tracks kills in area
- Accumulates mana in `m_iManaStock`
- Transfers mana to faction pool

### Detector Behavior

```cpp
BOOL _bNpcBehavior_Detector(int iNpcH)
```

- Scans for enemy presence
- Reports detected enemies to faction
- Reveals invisible enemies

### Grand Magic Generator

```cpp
void _NpcBehavior_GrandMagicGenerator(int iNpcH)
```

- Accumulates faction mana
- When threshold reached: enables meteor strike
- Triggers war event notifications

### Construction System

```cpp
m_iBuildCount  // Progress toward completion
```

- Players contribute to construction
- Progress tracked per structure
- Completed structures activate behavior

---

## NPC Processing Loop

### Main Loop

```cpp
void NpcProcess()
```

Called every game tick:

```cpp
for each NPC in m_pNpcList:
    if NPC is null: continue

    // Check action timer
    if (GetTickCount() - m_dwTime < m_dwActionTime): continue

    // Update timer
    m_dwTime = GetTickCount()

    // MP regeneration (every DEF_MPUPTIME)
    if (GetTickCount() - m_dwMPupTime > DEF_MPUPTIME):
        m_iMana += iDice(1, m_iMaxMana / 5)
        if (m_iMana > m_iMaxMana): m_iMana = m_iMaxMana
        m_dwMPupTime = GetTickCount()

    // HP regeneration (every DEF_HPUPTIME)
    // Summoned NPCs do not regenerate HP
    if (!m_bIsSummoned):
        if (GetTickCount() - m_dwHPupTime > DEF_HPUPTIME):
            m_iHP += iDice(1, m_iHitDice)
            maxHP = iDice(m_iHitDice, 8) + m_iHitDice
            if (m_iHP > maxHP): m_iHP = maxHP
            m_dwHPupTime = GetTickCount()

    // Summon timeout check
    if (m_bIsSummoned):
        if (GetTickCount() - m_dwSummonedTime > DEF_SUMMONTIME):
            // Unsummon

    // Behavior dispatch
    switch (m_cBehavior):
        case DEF_BEHAVIOR_DEAD:  NpcBehavior_Dead(); break
        case DEF_BEHAVIOR_STOP:  NpcBehavior_Stop(); break
        case DEF_BEHAVIOR_MOVE:  NpcBehavior_Move(); break
        case DEF_BEHAVIOR_ATTACK: NpcBehavior_Attack(); break
        case DEF_BEHAVIOR_FLEE:  NpcBehavior_Flee(); break
```

### Timing Constants

| Constant | Value | Description |
|----------|-------|-------------|
| `DEF_HPUPTIME` | 15000ms | HP regeneration interval |
| `DEF_MPUPTIME` | 20000ms | MP regeneration interval |
| `DEF_SUMMONTIME` | 300000ms | Summon duration (5 min) |

### Action Time Modifiers

```cpp
// Base action time from template
m_dwActionTime

// Ice effect: +50% action time (slower)
if (hasIceEffect):
    effectiveActionTime = m_dwActionTime * 1.5

// Attack mode: Random reduction (600-700ms faster)
if (m_cBehavior == DEF_BEHAVIOR_ATTACK):
    effectiveActionTime -= iDice(600, 100)
```

---

## Configuration & Loading

### NPC Template Loading

```cpp
BOOL _bDecodeNpcConfigFileContents(char * pData, int iSize)
```

Parses `Npc.cfg` configuration file:

```
// Example format (reconstructed)
[NPC-NAME]
Type = 10
HitDice = 5
DefenseRatio = 20
HitRatio = 50
MagicHitRatio = 0
AttackDiceThrow = 2
AttackDiceRange = 6
AttackBonus = 3
Size = 0
MoveType = 4
ActionTime = 1000
SearchRange = 10
Bravery = 5
MagicLevel = 0
MaxMana = 0
Exp = 100
ExpDiceMin = 80
ExpDiceMax = 120
GoldDiceMin = 10
GoldDiceMax = 50
ItemRatio = 30
Side = 0
SpecialAbility = 0
```

### Template Storage

```cpp
class CNpc * m_pNpcConfigList[DEF_MAXNPCTYPES];
```

- Indexed by NPC type
- Templates cloned for each instance
- Shared configuration data

---

## Key Algorithms

### HP Regeneration Formula

```cpp
// Amount
HP_gain = iDice(1, m_iHitDice)

// Maximum HP calculation
maxHP = iDice(m_iHitDice, 8) + m_iHitDice

// Regeneration
m_iHP += HP_gain
if (m_iHP > maxHP) m_iHP = maxHP

// Note: Summoned NPCs do NOT regenerate HP
```

### MP Regeneration Formula

```cpp
// Amount
MP_gain = iDice(1, m_iMaxMana / 5)

// Regeneration
m_iMana += MP_gain
if (m_iMana > m_iMaxMana) m_iMana = m_iMaxMana
```

### Experience Distribution

```cpp
// Base experience
exp = m_iExp / 3

// Quest bonus
if (playerHasQuestForNpc):
    questProgress += 1

// Exp modifier from items
exp *= playerExpBonus

// Crusade penalty
if (crusadeActive):
    exp /= 3
```

### Direction Calculation

```cpp
// 8 compass directions
// 1 = North, 2 = NE, 3 = East, 4 = SE
// 5 = South, 6 = SW, 7 = West, 8 = NW

// Direction from (sX,sY) to (dX,dY)
direction = GetPoint(sX, sY, dX, dY, ...)
```

---

## Function Reference

### Creation Functions

| Function | Purpose |
|----------|---------|
| `bCreateNewNpc()` | Create NPC instance from template |
| `_bInitNpcAttr()` | Initialize NPC attributes from template |
| `bSetNpcFollowMode()` | Set NPC to follow entity |

### Processing Functions

| Function | Purpose |
|----------|---------|
| `NpcProcess()` | Main NPC update loop |
| `NpcBehavior_Stop()` | Handle STOP behavior |
| `NpcBehavior_Move()` | Handle MOVE behavior |
| `NpcBehavior_Attack()` | Handle ATTACK behavior |
| `NpcBehavior_Flee()` | Handle FLEE behavior |
| `NpcBehavior_Dead()` | Handle DEAD behavior |

### Combat Functions

| Function | Purpose |
|----------|---------|
| `TargetSearch()` | Find valid attack targets |
| `NpcMagicHandler()` | Execute NPC magic attack |
| `iGetDangerValue()` | Evaluate position danger |
| `NpcRequestAssistance()` | Call for help |
| `bSetNpcAttackMode()` | Force attack specific target |

### Death Functions

| Function | Purpose |
|----------|---------|
| `NpcKilledHandler()` | Process NPC death |
| `DeleteNpc()` | Remove NPC from game |
| `NpcDeadItemGenerator()` | Generate loot drops |
| `bGetItemNameWhenDeleteNpc()` | Single item drop |
| `bGetMultipleItemNamesWhenDeleteNpc()` | Multi-item drops |

### Movement Functions

| Function | Purpose |
|----------|---------|
| `CalcNextWayPointDestination()` | Calculate next waypoint |
| `cGetNextMoveDir()` | Get pathfinding direction |

### Special Behaviors

| Function | Purpose |
|----------|---------|
| `_bNpcBehavior_ManaCollector()` | Mana collector structure |
| `_bNpcBehavior_Detector()` | Detector structure |
| `_NpcBehavior_GrandMagicGenerator()` | Grand magic generator |

### Query Functions

| Function | Purpose |
|----------|---------|
| `iGetNpcRelationship()` | Get NPC attitude |
| `iGetNpcRelationship_SendEvent()` | Query and notify client |

### Admin Functions

| Function | Purpose |
|----------|---------|
| `AdminOrder_ClearNpc()` | Clear all NPCs |
| `AdminOrder_GetNpcStatus()` | Query NPC status |
| `RemoveCrusadeNpcs()` | Remove war NPCs |
| `RemoveHeldenianNpc()` | Remove event NPC |

### Interaction Functions

| Function | Purpose |
|----------|---------|
| `NpcTalkHandler()` | Handle player-NPC interaction |

---

## Modernization Notes

### Type Safety Improvements

```cpp
// Legacy
int m_iTargetIndex;
char m_cTargetType;

// Modern
struct npc_id { uint16_t value; };
struct entity_ref {
    entity_id id;
    entity_type type;
};
std::optional<entity_ref> target;
```

### Memory Management

```cpp
// Legacy
class CNpc * m_pNpcList[DEF_MAXNPCS];

// Modern
std::vector<std::unique_ptr<npc>> npc_list;
// or
object_pool<npc> npc_pool{max_npcs};
```

### Component Decomposition

```cpp
// Break CNpc into components:
struct transform_component {
    position pos;
    position destination;
    direction facing;
    int8_t map_index;
};

struct combat_stats_component {
    int32_t hp;
    int32_t max_hp;
    int32_t defense;
    int32_t hit_ratio;
    damage_dice attack;
};

struct behavior_component {
    behavior_state state;
    move_type movement;
    int16_t behavior_turns;
    std::optional<entity_ref> target;
};

struct magic_component {
    int32_t mana;
    int32_t max_mana;
    int8_t magic_level;
    int8_t resist_magic;
};
```

### Event-Based Communication

```cpp
// Legacy (direct calls)
NpcKilledHandler(attackerH, attackerType, npcH, damage);

// Modern (event bus)
event_bus.publish(npc_killed_event{
    .npc_id = npc_id,
    .killer = killer_ref,
    .damage = damage,
    .position = npc.position
});
```

### Naming Conventions

| Legacy | Modern |
|--------|--------|
| `m_cNpcName` | `name` |
| `m_sX, m_sY` | `position.x, position.y` |
| `m_cBehavior` | `behavior_state` |
| `m_iHP` | `health.current` |
| `m_bIsKilled` | `is_dead` |
| `m_dwActionTime` | `action_interval` |
| `DEF_BEHAVIOR_ATTACK` | `behavior_state::attack` |

### Behavior State Machine

```cpp
// Modern state machine
class npc_behavior_fsm {
public:
    void update(npc& npc, duration delta);
    void transition_to(behavior_state new_state);

private:
    behavior_state current_state = behavior_state::idle;

    void handle_idle(npc& npc);
    void handle_move(npc& npc);
    void handle_attack(npc& npc);
    void handle_flee(npc& npc);
    void handle_dead(npc& npc);
};
```

### Async Processing

```cpp
// Consider task-based processing for heavy operations
auto result = co_await pathfinder.find_path(
    npc.position,
    npc.destination,
    npc.map_index
);
```

---

## Related Systems

- **Combat System** (`03_combat_system.md`) - Damage calculation, hit resolution
- **Magic System** (`04_magic_system.md`) - Spell casting, effects
- **Item System** (`06_item_system.md`) - Loot drops, item creation
- **World & Map System** (`15_world_map_system.md`) - Position management, pathfinding
- **War & Crusade System** (`14_war_crusade_system.md`) - Crusade structures
- **Quest System** (`08_quest_system.md`) - Kill objectives, rewards

---

## Appendix: NPC Message Types

| Message | Direction | Purpose |
|---------|-----------|---------|
| `CYCLENPC` | Server→Client | NPC position update |
| `NPCACTION` | Server→Client | NPC action (attack, cast) |
| `NPCDEAD` | Server→Client | NPC death notification |
| `NPCSPAWN` | Server→Client | New NPC appeared |
| `NPCTALK` | Client→Server | Player interacts with NPC |
| `NPCINFO` | Server→Client | NPC details for targeting |
