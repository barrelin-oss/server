# Legacy Helbreath Server Systems Overview

**Document Version:** 1.0
**Source Code Version:** 2.03 (leaked sources)
**Total Legacy Codebase:** ~45,500 lines of code

This document provides a comprehensive index of all major systems in the legacy Helbreath game server. Each system will have its own detailed documentation file.

---

## Table of Contents

| # | System | File | Status | Complexity |
|---|--------|------|--------|------------|
| 01 | [Player System](#01-player-system) | `01_player_system.md` | **Complete** | High |
| 02 | [NPC System](#02-npc-system) | `02_npc_system.md` | **Complete** | High |
| 03 | [Combat System](#03-combat-system) | `03_combat_system.md` | **Complete** | High |
| 04 | [Magic System](#04-magic_system.md) | `04_magic_system.md` | **Complete** | High |
| 05 | [Skill System](#05-skill-system) | `05_skill_system.md` | **Complete** | Medium |
| 06 | [Item System](#06-item-system) | `06_item_system.md` | **Complete** | High |
| 07 | [Inventory System](#07-inventory-system) | `07_inventory_system.md` | **Complete** | Medium |
| 08 | [Quest System](#08-quest-system) | `08_quest_system.md` | **Complete** | High |
| 09 | [Guild System](#09-guild-system) | `09_guild_system.md` | **Complete** | Medium |
| 10 | [Party System](#10-party-system) | `10_party_system.md` | **Complete** | Low |
| 11 | [Crafting System](#11-crafting-system) | `11_crafting_system.md` | **Complete** | Medium |
| 12 | [Gathering System](#12-gathering-system) | `12_gathering_system.md` | **Complete** | Medium |
| 13 | [Trading & Shops](#13-trading-shops) | `13_trading_shops.md` | **Complete** | Medium |
| 14 | [War & Crusade System](#14-war-crusade-system) | `14_war_crusade_system.md` | Pending | Very High |
| 15 | [World & Map System](#15-world-map-system) | `15_world_map_system.md` | **Complete** | High |
| 16 | [Dynamic Objects](#16-dynamic-objects) | `16_dynamic_objects.md` | Pending | Medium |
| 17 | [Delayed Events](#17-delayed-events) | `17_delayed_events.md` | Pending | Medium |
| 18 | [Teleportation System](#18-teleportation-system) | `18_teleportation_system.md` | **Complete** | Low |
| 19 | [Status Effects](#19-status-effects) | `19_status_effects.md` | **Complete** | Medium |
| 20 | [Experience & Leveling](#20-experience-leveling) | `20_experience_leveling.md` | Pending | Medium |
| 21 | [Chat & Messaging](#21-chat-messaging) | `21_chat_messaging.md` | **Complete** | Low |
| 22 | [Administrative System](#22-administrative-system) | `22_administrative_system.md` | Pending | Medium |
| 23 | [Persistence & Logging](#23-persistence-logging) | `23_persistence_logging.md` | Pending | Medium |
| 24 | [Anti-Cheat System](#24-anti-cheat-system) | `24_anti_cheat_system.md` | Pending | Medium |
| 25 | [Special Events](#25-special-events) | `25_special_events.md` | Pending | Medium |
| 26 | [Character Customization](#26-character-customization) | `26_character_customization.md` | **Complete** | Low |
| 27 | [Economy & Gold](#27-economy-gold) | `27_economy_gold.md` | Pending | Low |
| 28 | [Network Protocol](#28-network-protocol) | `28_network_protocol.md` | Pending | High |

---

## Primary Source Files

### Monolithic Core

| File | Lines | Description |
|------|-------|-------------|
| `Game.cpp` | 42,704 | Central game engine - contains ALL game logic |
| `Game.h` | 673 | CGame class definition with 500+ methods |

### Supporting Classes

| File | Lines | Description |
|------|-------|-------------|
| `Client.cpp/h` | 298 | Player character state (CClient) |
| `Npc.cpp/h` | 54 | NPC entity state (CNpc) |
| `Item.cpp/h` | 51 | Item instance data (CItem) |
| `Map.cpp/h` | 739 | Map and tile management (CMap) |
| `Tile.cpp/h` | ~50 | Individual tile data (CTile) |
| `Magic.cpp/h` | 20 | Magic spell definitions (CMagic) |
| `Skill.cpp/h` | 19 | Skill definitions (CSkill) |
| `Quest.cpp/h` | 20 | Quest definitions (CQuest) |
| `GuildsMan.cpp/h` | 20 | Guild member data (CGuildsMan) |

### Crafting & Gathering

| File | Lines | Description |
|------|-------|-------------|
| `BuildItem.cpp/h` | 36 | Crafting recipes (CBuildItem) |
| `Portion.cpp/h` | 26 | Potion recipes (CPortion) |
| `Fish.cpp/h` | 29 | Fishing node data (CFish) |
| `Mineral.cpp/h` | 24 | Mining node data (CMineral) |

### World Objects

| File | Lines | Description |
|------|-------|-------------|
| `DynamicObject.cpp/h` | 32 | Temporary world objects (CDynamicObject) |
| `DelayEvent.cpp/h` | 19 | Timed event queue (CDelayEvent) |
| `TeleportLoc.cpp/h` | 33 | Teleport destinations (CTeleportLoc) |
| `OccupyFlag.cpp/h` | ~30 | War territory markers |
| `StrategicPoint.cpp/h` | ~30 | Crusade control points |

### Network & Utilities

| File | Lines | Description |
|------|-------|-------------|
| `XSocket.cpp/h` | 599 | Winsock wrapper (XSocket) |
| `Msg.cpp/h` | 44 | Message queue structure (CMsg) |
| `Misc.cpp/h` | 368 | Utility functions |
| `StrTok.cpp/h` | 83 | String tokenizer |

### Crusade System

| File | Lines | Description |
|------|-------|-------------|
| `CrusadeCore.cpp/h` | 24 | Crusade war data |

---

## System Summaries

### 01. Player System
**Primary Files:** `Client.cpp/h`, `Player.h`, portions of `Game.cpp`
**Complexity:** High (~5,000+ lines across Game.cpp)

Manages all player character state including:
- Core attributes (STR, INT, VIT, DEX, MAG, CHR, LCK)
- Vital stats (HP, MP, SP, EXP, Level)
- Inventory and equipment (50 slots + 15 equipment positions)
- Bank storage (120 slots)
- Skill mastery (60 skill types)
- Magic mastery (100 spell types)
- Status effects and buffs
- Guild membership and rank
- Party membership
- Quest progress
- Combat statistics
- Session data and anti-cheat tracking

---

### 02. NPC System
**Primary Files:** `Npc.cpp/h`, portions of `Game.cpp`
**Complexity:** High (~3,000+ lines across Game.cpp)

Manages all non-player characters:
- NPC spawning and despawning
- 6 movement types (STOP, WAYPOINT, RANDOM, FOLLOW, etc.)
- 5 behavior states (IDLE, MOVE, ATTACK, FLEE, DEAD)
- Combat AI with target acquisition
- Waypoint pathing (up to 10 waypoints per NPC)
- Summoned creature management
- Shop NPCs for trading
- Quest-giver NPCs
- Faction allegiance (Aresden/Elvine/Neutral)
- Special NPC types (Guards, Detectors, Mana Collectors)
- Maximum 5,000 simultaneous NPCs

---

### 03. Combat System
**Primary Files:** Damage/hit functions in `Game.cpp`
**Complexity:** High (~2,500+ lines)

Handles all combat resolution:
- Physical damage calculation (attack dice + bonuses)
- Armor absorption per equipment piece
- Shield parrying and blocking
- Hit/miss resolution (10-95% effective range)
- Critical hit mechanics
- Weapon skill modifiers
- PvP combat with crime penalties
- Safe zones and peaceful areas
- Battle zone mechanics
- War contribution tracking
- Knockback/damage move effects

---

### 04. Magic System
**Primary Files:** `Magic.cpp/h`, magic handling in `Game.cpp`
**Complexity:** High (~3,000+ lines)

Manages all magical abilities:
- 23 spell effect types
- Damage spells (single target, AoE, linear)
- Healing spells (HP/SP recovery)
- Buff/debuff spells
- Summoning spells
- Teleportation spells
- Polymorph and transformation
- Mana cost calculation
- Casting delays and cooldowns
- Magic mastery progression
- Elemental attributes (Fire, Water, Air, Earth)
- Maximum 100 concurrent magic effects per player

---

### 05. Skill System
**Primary Files:** `Skill.cpp/h`, skill handling in `Game.cpp`
**Complexity:** Medium (~1,500 lines)

Manages player skills:
- 60 skill types
- Skill mastery levels (0-255)
- 700 total skill points allocation
- Weapon-specific skills
- Super attack mechanics
- Skill training from NPCs
- Skill-based special abilities
- Combo attack dependencies
- Down skill (recovery moves)

---

### 06. Item System
**Primary Files:** `Item.cpp/h`, item handling in `Game.cpp`
**Complexity:** High (~3,000+ lines)

Manages all items in the game:
- Item type definitions (5,000 max types)
- Equipment items with stats
- Consumables (potions, food)
- Crafting materials
- Quest items
- 27 special effect types
- Durability tracking
- Weight calculations
- Item attributes and bonuses
- Elemental resistances
- Unique item binding
- Drop rate mechanics
- Item transformation/upgrade

---

### 07. Inventory System
**Primary Files:** Inventory handling in `Game.cpp`, `Client.h`
**Complexity:** Medium (~1,000 lines)

Manages player inventories:
- 50 inventory slots
- 15 equipment positions
- Item pickup and drop
- Equip/unequip mechanics
- Weight/burden limits
- Stack management
- Bank storage (120 slots)
- Item positioning within grid

---

### 08. Quest System
**Primary Files:** `Quest.cpp/h`, quest handling in `Game.cpp`
**Complexity:** High (~2,000+ lines)

Manages all quests:
- 11 quest types (hunting, delivery, escort, etc.)
- 200 maximum quest definitions
- Level requirements
- Time limits
- Kill counters and objectives
- Reward distribution
- Crusade-specific quests
- Quest NPC interactions
- Quest progress persistence

---

### 09. Guild System
**Primary Files:** `GuildsMan.cpp/h`, guild handling in `Game.cpp`
**Complexity:** Medium (~1,500 lines)

Manages guilds:
- Guild creation/dissolution
- 128 members per guild maximum
- 12 rank levels
- Member management (invite, kick, promote)
- Guild contribution tracking
- Guild teleport locations
- Guild-exclusive features
- 1,000 maximum guilds

---

### 10. Party System
**Primary Files:** Party handling in `Game.cpp`, `Client.h`
**Complexity:** Low (~500 lines)

Manages temporary groups:
- 6 members maximum (1 leader + 5)
- Party creation and invitation
- Member tracking
- Experience distribution
- Loot distribution
- Party dissolution

---

### 11. Crafting System
**Primary Files:** `BuildItem.cpp/h`, `Portion.cpp/h`, crafting in `Game.cpp`
**Complexity:** Medium (~1,200 lines)

Manages item creation:
- 300 crafting recipes (BuildItem)
- 500 potion recipes (Portion)
- Material requirements (up to 6 per recipe)
- Skill level requirements
- Success/failure mechanics
- Item enhancement
- Dye/color application

---

### 12. Gathering System
**Primary Files:** `Fish.cpp/h`, `Mineral.cpp/h`, gathering in `Game.cpp`
**Complexity:** Medium (~800 lines)

Manages resource collection:
- Fishing mechanics
- Mining mechanics
- 200 nodes per map maximum
- Resource depletion and regeneration
- Engagement limits
- Difficulty ratings
- Success calculations

---

### 13. Trading & Shops
**Primary Files:** Trading/shop handling in `Game.cpp`
**Complexity:** Medium (~1,000 lines)

Manages commerce:
- NPC shop inventories
- Item purchase/sale
- Price determination
- Item repair services
- Player-to-player trading
- Exchange validation

---

### 14. War & Crusade System
**Primary Files:** `CrusadeCore.cpp/h`, `OccupyFlag.cpp/h`, `StrategicPoint.cpp/h`, war handling in `Game.cpp`
**Complexity:** Very High (~4,000+ lines)

Manages faction warfare:
- Aresden vs Elvine warfare
- Territory occupation (20,001 flags)
- 200 strategic points
- Crusade mode activation
- War structures (barracks, towers)
- Mana pool management
- Grand Magic/Meteor strikes
- Special crusade NPCs
- War contribution and rewards
- Victory conditions
- City status tracking

---

### 15. World & Map System
**Primary Files:** `Map.cpp/h`, `Tile.cpp/h`, map handling in `Game.cpp`
**Complexity:** High (~2,500 lines)

Manages the game world:
- 100 maximum maps
- Tile-based collision
- Sector organization (60 sectors per map)
- Spawn points (20 per map)
- Teleport locations (200 per map)
- Waypoints (200 per map)
- Mob generator zones (100 per map)
- Map attribute flags
- Walkability validation
- Water tiles
- No-attack zones

---

### 16. Dynamic Objects
**Primary Files:** `DynamicObject.cpp/h`, object handling in `Game.cpp`
**Complexity:** Medium (~600 lines)

Manages temporary world objects:
- 60,000 maximum objects
- Dropped items
- Summoned creatures
- Temporary effects
- Quest objects
- Lifespan management
- Ownership tracking

---

### 17. Delayed Events
**Primary Files:** `DelayEvent.cpp/h`, event handling in `Game.cpp`
**Complexity:** Medium (~500 lines)

Manages timed events:
- 60,000 maximum events
- Damage application delays
- Magic effect timing
- Meteor strike sequences
- Skill usage delays
- Event scheduling

---

### 18. Teleportation System
**Primary Files:** `TeleportLoc.cpp/h`, teleport handling in `Game.cpp`
**Complexity:** Low (~400 lines)

Manages map transitions:
- 200 teleport points per map
- Source/destination pairs
- Guild teleport locations
- Force recall mechanics
- Direction facing
- Validation rules

---

### 19. Status Effects
**Primary Files:** Status effect handling in `Game.cpp`, `Client.h`
**Complexity:** Medium (~800 lines)

Manages buffs and debuffs:
- Poison (levels and duration)
- Berserk (attack boost)
- Ice (movement restriction)
- Invisibility
- Polymorph
- Hunger/stamina
- Stun/knockdown
- 100 concurrent effects maximum
- Duration decay

---

### 20. Experience & Leveling
**Primary Files:** Level/exp handling in `Game.cpp`
**Complexity:** Medium (~600 lines)

Manages character progression:
- Experience accumulation
- Level thresholds
- 180 maximum level
- 3 stat points per level
- 200 stat point cap
- Experience penalties
- Experience bonuses
- Auto-exp mechanics

---

### 21. Chat & Messaging
**Primary Files:** Chat handling in `Game.cpp`
**Complexity:** Low (~500 lines)

Manages communication:
- Whisper/private messages
- Guild chat
- Party chat
- Area chat
- Broadcast messages
- Chat filtering
- Notice system
- Rate limiting

---

### 22. Administrative System
**Primary Files:** `AdminOrder_*` functions in `Game.cpp`
**Complexity:** Medium (~1,500 lines)

Manages GM commands:
- Item creation/deletion
- Creature spawning
- Player management
- Server control
- Ban/mute functions
- Invisibility/observer modes
- Rating system
- Admin permission levels

---

### 23. Persistence & Logging
**Primary Files:** Save/load functions in `Game.cpp`
**Complexity:** Medium (~1,200 lines)

Manages data persistence:
- Character file saving
- Guild data persistence
- Quest progress saving
- Item logging
- Action logging
- Log server communication
- 10 sub-log sockets

---

### 24. Anti-Cheat System
**Primary Files:** Anti-cheat logic in `Game.cpp`
**Complexity:** Medium (~600 lines)

Protects game integrity:
- Speed hack detection
- Message frequency validation
- Latency analysis
- Session validation
- IP tracking
- Movement verification

---

### 25. Special Events
**Primary Files:** Event handling in `Game.cpp`
**Complexity:** Medium (~700 lines)

Manages timed events:
- 10-minute event cycle
- Energy sphere spawning
- Special NPC spawning
- Weather changes
- Season effects
- Fishing/mining events

---

### 26. Character Customization
**Primary Files:** Character data in `Client.h`, creation in `Game.cpp`
**Complexity:** Low (~300 lines)

Manages appearance:
- Sex selection
- Skin color
- Hair style/color
- Underwear type
- Appearance values (4 slots)
- Polymorph forms
- Special effects

---

### 27. Economy & Gold
**Primary Files:** Gold handling in `Game.cpp`
**Complexity:** Low (~400 lines)

Manages currency:
- Gold currency
- Maximum 99,999,999 gold
- Transaction validation
- Shop pricing
- Repair costs
- Quest rewards
- City economic tracking

---

### 28. Network Protocol
**Primary Files:** `XSocket.cpp/h`, `Msg.cpp/h`, message handling in `Game.cpp`
**Complexity:** High (~2,000+ lines)

Manages network communication:
- Custom binary protocol
- Message queue (100,000 capacity)
- Client socket management
- Gate server communication
- Log server communication
- Message routing
- Async I/O

---

## Server Capacity Limits

| Resource | Maximum | Constant |
|----------|---------|----------|
| Clients | 2,000 | `DEF_MAXCLIENTS` |
| NPCs | 5,000 | `DEF_MAXNPCS` |
| Maps | 100 | `DEF_MAXMAPS` |
| Dynamic Objects | 60,000 | `DEF_MAXDYNAMICOBJECTS` |
| Delayed Events | 60,000 | `DEF_MAXDELAYEVENTS` |
| Item Types | 5,000 | `DEF_MAXITEMTYPES` |
| Guilds | 1,000 | `DEF_MAXGUILDS` |
| Message Queue | 100,000 | `DEF_MSGQUENESIZE` |
| Quest Types | 200 | `DEF_MAXQUESTTYPE` |
| NPC Types | 100 | `DEF_MAXNPCTYPES` |
| Teleport Locations/Map | 200 | Per-map limit |
| Waypoints/Map | 200 | Per-map limit |
| Spawn Points/Map | 20 | Per-map limit |
| Spot Mob Generators/Map | 100 | Per-map limit |

---

## Per-Player Limits

| Resource | Maximum | Constant |
|----------|---------|----------|
| Inventory Slots | 50 | `DEF_MAXITEMS` |
| Bank Slots | 120 | `DEF_MAXBANKITEMS` |
| Equipment Slots | 15 | `DEF_MAXITEMEQUIPPOS` |
| Magic Types | 100 | `DEF_MAXMAGICTYPE` |
| Skill Types | 60 | `DEF_MAXSKILLTYPE` |
| Skill Points | 700 | `DEF_MAXSKILLPOINTS` |
| Party Members | 6 | `DEF_MAXPARTYMEMBERS` |
| Status Effects | 100 | `DEF_MAXMAGICEFFECTS` |
| Stat Points | 200 | `DEF_CHARPOINTLIMIT` |
| Level | 180 | `DEF_PLAYERMAXLEVEL` |
| Guild Rank | 12 | `DEF_GUILDSTARTRANK` |

---

## Timing Constants

| Event | Interval | Constant |
|-------|----------|----------|
| Auto-save | 30 minutes | `DEF_AUTOSAVETIME` |
| HP Regeneration | 15 seconds | `DEF_HPUPTIME` |
| MP Regeneration | 20 seconds | `DEF_MPUPTIME` |
| SP Regeneration | 10 seconds | `DEF_SPUPTIME` |
| Hunger Tick | 60 seconds | `DEF_HUNGERTIME` |
| Poison Duration | 12 seconds | `DEF_POISONTIME` |
| Summon Timeout | 5 minutes | `DEF_SUMMONTIME` |
| Notice Display | 80 seconds | `DEF_NOTICETIME` |
| Experience Stock | 10 seconds | `DEF_EXPSTOCKTIME` |
| Auto-Experience | 6 minutes | `DEF_AUTOEXPTIME` |
| Client Timeout | 10 seconds | `DEF_CLIENTTIMEOUT` |
| Special Event Cycle | 10 minutes | `DEF_SPECIALEVENTTIME` |
| Night Time Start | 40 units | `DEF_NIGHTTIME` |

---

## Documentation Progress

- [x] 01_player_system.md
- [ ] 02_npc_system.md
- [x] 03_combat_system.md
- [x] 04_magic_system.md
- [x] 05_skill_system.md
- [x] 06_item_system.md
- [x] 07_inventory_system.md
- [x] 08_quest_system.md
- [x] 09_guild_system.md
- [x] 10_party_system.md
- [x] 11_crafting_system.md
- [x] 12_gathering_system.md
- [x] 13_trading_shops.md
- [ ] 14_war_crusade_system.md
- [x] 15_world_map_system.md
- [ ] 16_dynamic_objects.md
- [ ] 17_delayed_events.md
- [x] 18_teleportation_system.md
- [x] 19_status_effects.md
- [ ] 20_experience_leveling.md
- [x] 21_chat_messaging.md
- [ ] 22_administrative_system.md
- [ ] 23_persistence_logging.md
- [ ] 24_anti_cheat_system.md
- [ ] 25_special_events.md
- [x] 26_character_customization.md
- [ ] 27_economy_gold.md
- [ ] 28_network_protocol.md

---

## Notes

- The legacy codebase uses Hungarian notation (prefixes like `m_`, `i`, `sz`, `b`)
- All code is in a single `CGame` class in Game.cpp (monolithic architecture)
- Heavy use of `#define` macros for constants
- Windows-specific APIs (Winsock, CRITICAL_SECTION)
- Korean comments throughout (historical reference)
- No modern error handling (return codes, no exceptions)
- Manual memory management (new/delete, raw pointers)
- Fixed-size arrays throughout
