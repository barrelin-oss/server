# Random Mob Generator System

## Overview

The Random Mob Generator system controls which NPCs can spawn on maps outside of defined spawner areas. Each map has a `random_mob_generator` level (1-7) that determines which NPC groups are allowed to spawn.

This system prevents arbitrary NPCs from spawning on inappropriate maps - for example, you won't find high-level Demons spawning in the starter farm areas.

## Configuration

### Map YAML Configuration

Each map's YAML file can define random mob spawning:

```yaml
name: arefarm
random_mob_generator:
  enabled: true
  level: 1
```

- **enabled**: Whether random mob spawning is active on this map
- **level**: Determines which NPC groups can spawn (1-7)

### Spawn Levels

The level determines the difficulty and type of NPCs that can spawn:

| Level | Map Types | Example Maps |
|-------|-----------|--------------|
| 1 | Starter areas, towns, farms | arefarm, elvfarm, aresden, elvine |
| 2 | Basic dungeons | Simple dungeon areas |
| 3 | Mid-level areas | Intermediate zones |
| 4 | Dungeons | Standard dungeons |
| 5 | Advanced areas | Higher-level zones |
| 6 | High-level zones | huntzone3, huntzone4 |
| 7+ | Elite zones | End-game areas |

## NPC Spawn Tables

### Level 1 (Starter Areas)

| NPC | Probability | Notes |
|-----|-------------|-------|
| Rabbit | 45% | Non-aggressive |
| Slime | 19% | Weak monster |
| Giant-Ant | 20% | Weak monster |
| Cat | 10% | Non-aggressive |
| Orc | 6% | Basic aggressor |

**Example:** arefarm, elvfarm - safe training areas

### Level 2 (Basic Dungeons)

| NPC | Probability |
|-----|-------------|
| Slime | 40% |
| Giant-Ant | 40% |
| Amphis | 20% |

### Level 3 (Mid-Level Areas)

Spawn groups include:
- **Low tier (20%)**: Orc, Zombie
- **Special (5%)**: Rudolph
- **Mid tier (25%)**: Skeleton, Orc-Mage, Scorpion
- **Higher tier (25%)**: Stone-Golem, Clay-Golem, Troll, WereWolf, Giant-Frog, Ettin
- **Top tier (25%)**: Cyclops, Orge, Hellbound, Mountain-Giant

### Level 4 (Dungeons)

| Tier | NPCs | Probability |
|------|------|-------------|
| Basic | Giant-Ant, Amphis | 50% |
| Mid | Stone-Golem, Clay-Golem | 30% |
| High | Hellbound, Cyclops | 20% |

### Level 5 (Advanced Areas)

Progressive difficulty with weighted spawns from Giant-Ant up to Cyclops.

### Level 6 (High-Level Zones)

| Tier | NPCs | Probability |
|------|------|-------------|
| Mid | Skeleton, Orc-Mage, Cyclops, Troll | 60% |
| High | Stone-Golem, Troll, Cyclops, Tentocle | 30% |
| Elite | Giant-Frog, Orge, Hellbound, WereWolf, Ettin, Mountain-Giant, Cannibal-Plant | 10% |

**Example:** huntzone3, huntzone4

### Level 7+ (Elite Zones)

| Tier | NPCs | Probability |
|------|------|-------------|
| Elite | Orge, Hellbound, Liche | 50% |
| Boss-tier | Liche, Demon, Unicorn | 30% |
| Ultimate | Gagoyle, Beholder, Dark-Elf, Ice-Golem, DireBoar, Frost, Wyvern | 20% |

## Usage

### Code Example

```cpp
// Spawn a random mob on a map (will use map's level)
auto result = npc_system->spawn_random_mob(map_id, position);

if (result.is_ok()) {
    auto entity_id = result.value();
    // NPC spawned successfully
} else {
    // Error: map might not have random_mob_generator enabled
    LOG_ERROR(general, "Random spawn failed: {}", result.error());
}
```

### Legacy Compatibility

This system preserves the exact spawn probabilities and NPC groups from the original Helbreath server (`CGame::MobGenerator()` in `Game.cpp`).

## Implementation Details

### Files

- **src/npc/random_mob_generator.h** - Public interface
- **src/npc/random_mob_generator.cpp** - Spawn tables and logic
- **src/npc/npc_system.h/cpp** - Integration with NPC system
- **src/world/map.h/cpp** - Map configuration storage

### How It Works

1. Map loads `random_mob_generator` config from YAML
2. When spawning is needed, `npc_system::spawn_random_mob()` is called
3. System checks if map has random mob generator enabled
4. Gets the map's level (1-7)
5. Selects a random NPC from that level's spawn table
6. Uses weighted probabilities to pick specific NPC
7. Spawns the NPC using normal spawn logic

### Spawn Point Types

There are two types of NPC spawning:

1. **Spot Mob Generators** (Spawners)
   - Defined in YAML with specific locations and NPC types
   - Spawn specific NPC types at specific locations
   - Example: `{ id: 1, type: 1, x1: 117, y1: 87, x2: 137, y2: 103, npc_type: 10, max_count: 30 }`

2. **Random Mob Generator** (This System)
   - Spawns random NPCs across the map (outside spawner areas)
   - Uses level-based spawn tables
   - Provides variety and exploration

## Notes

- Only maps with `random_mob_generator.enabled: true` will spawn random mobs
- The level value **must** be 1-7 (other values return error)
- Spawners always take precedence - they spawn specific NPCs in specific areas
- Random mob generator fills in the "wilderness" areas of maps
- Original probabilities and NPC groups are preserved for authenticity
