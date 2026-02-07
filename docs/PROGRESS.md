# Helbreath Server Modernization Progress

This document tracks implementation progress for the modernized Helbreath server.

**Last Updated:** 2026-02-07

---

## Status Legend

- ✅ Complete - Fully implemented and tested
- 🔄 In Progress - Partially implemented
- 📋 Planned - Designed but not started
- ❌ Not Started - No work done yet

---

## Phase 1: Core Infrastructure ✅

| Component | Status | Notes |
|-----------|--------|-------|
| CMake build system | ✅ | vcpkg integration, Windows/Linux support |
| Subsystem architecture | ✅ | Base class, lifecycle management |
| Event bus | ✅ | Pub/sub for decoupled communication |
| Logging (spdlog) | ✅ | Category-based logging, file + console |
| Configuration system | ✅ | TOML config files, server_config struct |
| Platform abstraction | ✅ | Windows/Linux, timers, clock |
| Result type | ✅ | Error handling without exceptions |
| Strong types | ✅ | player_id, connection_id, map_id, etc. |

---

## Phase 2: Network Layer ✅

| Component | Status | Notes |
|-----------|--------|-------|
| WebSocket server | ✅ | Boost.Beast, async I/O |
| Connection management | ✅ | Connection state, player binding |
| JSON protocol | ✅ | nlohmann/json, message envelope |
| Message routing | ✅ | Type-based dispatch to handlers |
| Protocol documentation | ✅ | docs/JSON_PROTOCOL.md, docs/GAME_MESSAGES.md |

---

## Phase 3: Authentication & Database ✅

| Component | Status | Notes |
|-----------|--------|-------|
| PostgreSQL integration | ✅ | libpqxx, connection pooling |
| Account management | ✅ | Create, authenticate, ban |
| Password hashing | ✅ | Argon2id via libsodium |
| Session tokens | ✅ | Secure generation, caching |
| Login attempt tracking | ✅ | Rate limiting by IP |
| Character CRUD | ✅ | Create, list, delete, load full data |
| Auth handlers | ✅ | Login, logout, create account, enter game |
| Enter game flow | ✅ | Full character loading, player creation, spawn, notifications |
| Player disconnect | ✅ | State saving, despawn notification |

**Database Schema:** `src/database/schema.sql` (complete)

---

## Phase 4: World & Entity System ✅

| Component | Status | Notes |
|-----------|--------|-------|
| Position/direction types | ✅ | position, direction, rect |
| Map structure | ✅ | Map class with tiles, spatial index |
| Tile system | ✅ | Static/dynamic tiles, flags, walkability |
| Spatial indexing | ✅ | Grid-based entity queries |
| Entity manager | ✅ | ECS architecture, component storage, type erasure, entity recycling |
| Entity visibility | ✅ | 20-tile Chebyshev distance, spawn/despawn |
| Map loading (.amd) | ✅ | Binary tile data from legacy format |
| Map config loading (.txt) | ✅ | Teleports, spawn points, safe zones, spawners |
| Teleport system | ✅ | Gate definitions loaded from config |
| Weather system | ✅ | Weather state per map |

---

## Phase 5: Player System ✅

| Component | Status | Notes |
|-----------|--------|-------|
| Player state | ✅ | Stats, position, HP/MP/SP |
| Movement (walk) | ✅ | Direction-based, validation |
| Movement (run) | ✅ | 2-tile movement |
| Position sync | ✅ | Client validation, desync detection |
| Stats calculation | ✅ | Base + equipment + effect modifiers → computed stats |
| Level/experience | ✅ | XP gain, level up |
| Equipment management | ✅ | Equipment slots, stat application |
| Hunger system | ✅ | Hunger decay, regen penalties, client notifications |
| Status effects | ✅ | Poison, paralyze, freeze, curse, etc. (managed by effect_system) |
| Regeneration | ✅ | HP/MP/SP regen with modifiers |
| PK decay | ✅ | Criminal status decay over time |
| Save/load | ✅ | Skills, equipment, inventory saved/loaded on enter/disconnect |
| Death/respawn | ✅ | XP penalty, PK tracking, bounty rewards, delayed respawn, map initial_points |

---

## Phase 6: Combat System ✅

| Component | Status | Notes |
|-----------|--------|-------|
| Attack handler | ✅ | Full attack processing pipeline |
| Damage calculation | ✅ | Physical/magic formulas from original game |
| Hit/miss resolution | ✅ | Accuracy, dodge, block mechanics |
| Critical hits | ✅ | Crit chance, multiplier |
| Attack types | ✅ | Regular, dash, super |
| Melee combat | ✅ | Range checking, weapon type bonuses |
| PvP/PvE modifiers | ✅ | Separate damage scaling |
| Combat state | ✅ | In-combat tracking, invulnerability |
| Kill rewards | ✅ | Experience and gold on kill |
| Death detection | ✅ | Death events, kill/death counters |
| Combat broadcasts | ✅ | Unified `combat_effect` broadcast for melee damage, spell effects, buffs/debuffs |
| Ranged combat | 📋 | Bow/crossbow, projectiles |
| PK system | ✅ | PK point gain on innocent kill, bounty rewards, criminal/murderer status |

---

## Phase 7: Magic System ✅

| Component | Status | Notes |
|-----------|--------|-------|
| Magic handler | ✅ | Full spell casting pipeline |
| Spell definitions | ✅ | Loaded from magic_registry (YAML) |
| Mana cost/regen | ✅ | MP/HP/SP cost deduction |
| Spell targeting | ✅ | Single, AoE, self, ground |
| Cast times | ✅ | Instant and channeled spells |
| Spell cooldowns | ✅ | Per-spell cooldown tracking |
| Spell learning | ✅ | Learn/forget spells, level progression |
| Damage spells | ✅ | Element-based damage with stat scaling |
| Heal spells | ✅ | Healing calculation |
| Buff spells | ✅ | Protection, haste, invisibility, etc. |
| Debuff spells | ✅ | Poison, paralyze, etc. |
| AoE targeting | ✅ | Ally/enemy/all target finding |
| Spell effects | ✅ | Duration tracking, periodic ticks, stat modifiers via effect_system |
| Effect groups | ✅ | magic_type-based slots, one-per-group rule |
| DoT/HoT ticking | ✅ | Poison, burn, heal, mana drain/restore with 2s tick |
| Effect-stat pipeline | ✅ | Equipment + effect modifiers combined, auto-revert on expiry |
| Visual broadcasts | 📋 | Client broadcast for effect applied/removed TODO |

---

## Phase 8: Skill System ✅

| Component | Status | Notes |
|-----------|--------|-------|
| Skill tracking | ✅ | All weapon skills tracked per player |
| Skill experience | ✅ | XP gain through use, leveling |
| Skill mastery | ✅ | Mastery levels |
| Weapon skill bonuses | ✅ | Damage and hit rate bonuses |
| Skill training | ✅ | Training mechanics |
| Skill reset | ✅ | Reset functionality |
| Manufacturing | ❌ | Item creation |
| Alchemy | ❌ | Potion creation |
| Mining | ❌ | Resource gathering |
| Fishing | ❌ | Fish catching |

---

## Phase 9: NPC System ✅

| Component | Status | Notes |
|-----------|--------|-------|
| NPC data structure | ✅ | Stats, position, AI state |
| NPC registry | ✅ | Load from npcs.yaml |
| Spawn point system | ✅ | Rectangular areas, max count, respawn timers |
| Random mob generator | ✅ | Level-based spawn tables (see docs/RANDOM_MOB_GENERATOR.md) |
| NPC spawning | ✅ | Manual, spawn point, and random mob spawn |
| NPC despawning | ✅ | Death handling, cleanup |
| NPC AI | ✅ | State machine (idle, wander, chase, attack, flee, return home, scripted) |
| Aggro detection | ✅ | Range-based, sight checks |
| Pathfinding | ✅ | Move-towards-target |
| NPC combat | ✅ | Attack execution, damage calculation integration |
| Pack AI | ✅ | Social behaviors, calls for help |
| Boss mechanics | ✅ | Phase system, boss controller |
| Spawn rules | ✅ | Time, weather, event-based spawning |
| Behavior trees | ✅ | Custom NPC behavior scripting |
| NPC loot | ✅ | YAML-driven loot tables, on_kill + on_despawn phases, boss multi-drops |
| Corpse cleanup | ✅ | 15s linger timer, despawn callback fires body part/rare/boss drops |
| NPC dialog | ✅ | YAML-driven dialog trees with action routing |
| Shop NPCs | ✅ | Buy/sell/repair with charisma pricing |
| Quest NPCs | ❌ | Quest givers |
| Guard NPCs | ❌ | Town protection |

**Documentation:** `docs/RANDOM_MOB_GENERATOR.md`

---

## Phase 10: Item System 🔄

| Component | Status | Notes |
|-----------|--------|-------|
| Item definitions | ✅ | Loaded from item_registry (YAML) |
| Item instances | ✅ | Individual items with state, owner tracking |
| Item stacking | ✅ | Stack/split operations |
| Durability | ✅ | Wear, repair system |
| Item effects | ✅ | Stat bonuses, equipment slot mapping |
| Item properties | ✅ | Weight, price, level requirements, tradeable/droppable flags |
| Ground items | ✅ | Items on map, pickup, despawn timer |
| Loot drops | ✅ | YAML-driven loot_tables.yaml, flat percentages, on_kill + on_despawn phases |

---

## Phase 11: Inventory System ✅

| Component | Status | Notes |
|-----------|--------|-------|
| Inventory slots | ✅ | 50 slots defined, serialized to JSONB |
| Add/remove items | ✅ | Full inventory management |
| Move items | ✅ | Slot swap/move |
| Equipment slots | ✅ | 12+ slots defined, serialized to JSONB |
| Equip/unequip | 🔄 | Slot management works, handler wiring TODO |
| Bank system | ✅ | 200 slots, deposit/withdraw |
| Gold management | ✅ | Loaded/saved with character |
| Trading | ✅ | Trade window, item/gold offering, confirm/lock, completion |
| Enter game integration | ✅ | Inventory/bank/gold loaded from DB |
| Save/disconnect integration | ✅ | Inventory/bank/gold saved to DB |

---

## Phase 12: Interaction System 🔄

| Component | Status | Notes |
|-----------|--------|-------|
| Interact handler | ✅ | Request parsing and NPC routing |
| NPC interaction | ✅ | Dialog trees, shop, bank |
| Object interaction | ❌ | Doors, chests, etc. |
| Shop interface | ✅ | Buy/sell/repair with charisma discount, territory restrictions |
| Bank interface | ✅ | Deposit/withdraw via NPC dialog action |

---

## Phase 13: Chat System ✅

| Component | Status | Notes |
|-----------|--------|-------|
| Chat message type | ✅ | chat_message, chat_message_broadcast |
| Local chat | ✅ | 15-tile range, default channel |
| Global/Shout | ✅ | Server-wide with `!` prefix |
| Whisper | ✅ | Private messages with recipient |
| Guild chat | ✅ | `@` prefix, guild members only |
| Party chat | ✅ | `$` prefix, party members only |
| Trade chat | ✅ | `~` prefix |
| Chat filter | ✅ | Profanity filter with asterisks |
| Rate limiting | ✅ | 3/sec, 30/min limits |
| Channel settings | ✅ | Per-player enable/disable |
| Player blocking | ✅ | Block player messages |
| Command system | ✅ | Separate command_request packet, built-in commands |

---

## Phase 14: Social Systems ✅

| Component | Status | Notes |
|-----------|--------|-------|
| Party system | ✅ | Create, invite, accept, leave, kick, disband |
| Party settings | ✅ | Loot mode, XP mode, leader transfer |
| Party member tracking | ✅ | HP, MP, map stats |
| Party invite expiration | ✅ | Timed invites |
| Guild system | ✅ | Create, join, leave, kick, disband |
| Guild ranks | ✅ | Master, officer, veteran, member, recruit |
| Guild permissions | ✅ | Invite, kick, promote, demote, etc. |
| Guild MOTD | ✅ | Message of the day |
| Party XP share | 📋 | Experience distribution logic TODO |
| Guild warehouse | ❌ | Shared storage |
| Friend list | ❌ | Add, remove, online status |

---

## Phase 15: Quest System ✅

| Component | Status | Notes |
|-----------|--------|-------|
| Quest definitions | ✅ | Quest templates with prerequisites |
| Quest tracking | ✅ | Quest journal per player |
| Quest acceptance | ✅ | Accept with prerequisite checks |
| Quest abandonment | ✅ | Abandon active quests |
| Quest completion | ✅ | Completion with rewards |
| Kill objectives | ✅ | Kill monsters/players |
| Collect objectives | ✅ | Collect/deliver items |
| Visit objectives | ✅ | Visit locations |
| Talk objectives | ✅ | Talk to NPCs |
| Craft objectives | ✅ | Craft items |
| Gather objectives | ✅ | Gather resources |
| Level objectives | ✅ | Reach level/skill level |
| Timed quests | ✅ | Expiration tracking |
| Repeatable quests | ✅ | Repeat mechanics |
| Auto-complete quests | ✅ | Auto-complete on objective fulfillment |
| Quest rewards | ✅ | XP, items, gold |

---

## Phase 16: War Systems 🔄

| Component | Status | Notes |
|-----------|--------|-------|
| War scheduling | ✅ | War lifecycle management |
| War types | ✅ | Crusade, Heldenian, Apocalypse defined |
| War states | ✅ | Scheduled, preparing, active, ending, ended |
| Territory control | ✅ | Faction tracking, territory state |
| War statistics | ✅ | Contribution tracking |
| Crusade mechanics | ❌ | Actual battle objectives, flag capture |
| Heldenian mechanics | ❌ | Castle siege logic |
| Apocalypse mechanics | ❌ | World boss event |
| War rewards | ❌ | Contribution point redemption |

---

## Phase 17: Admin System 🔄

| Component | Status | Notes |
|-----------|--------|-------|
| Admin levels | ✅ | Permission levels defined |
| Command framework | ✅ | Registration, aliases, permission checks |
| Player muting | ✅ | Mute with expiration |
| Command logging | ✅ | Audit trail |
| GM commands | 🔄 | Framework done, specific commands partially implemented |
| Player management | 🔄 | Kick/ban via auth, mute via admin, more TODO |
| Server management | ❌ | Reload, shutdown commands |

---

## Phase 18: Persistence ✅

| Component | Status | Notes |
|-----------|--------|-------|
| Periodic save | ✅ | Configurable auto-save interval, on-demand save methods |
| Character save | ✅ | Full save on disconnect (stats, position, skills, equipment, inventory) |
| Inventory save | ✅ | JSON serialization to JSONB column |
| Equipment save | ✅ | JSON serialization to JSONB column |
| Skills save | ✅ | JSON serialization to JSONB column |
| Bank save | ✅ | JSON serialization to JSONB column |
| Gold save | ✅ | Stored in characters table |
| Guild save | ✅ | Guilds and members persist to PostgreSQL across server restarts |
| World state | ❌ | Dynamic objects |

---

## Phase 19: Registry Systems ✅

| Component | Status | Notes |
|-----------|--------|-------|
| NPC registry | ✅ | Template loading from YAML |
| Magic registry | ✅ | Spell data loading from YAML |
| Item registry | ✅ | Item template loading from YAML |

---

## Phase 20: Scheduler ✅

| Component | Status | Notes |
|-----------|--------|-------|
| Task scheduling | ✅ | One-shot and repeating tasks |
| Game clock | ✅ | Time acceleration |
| Task cancellation | ✅ | Cancel by ID or tag |
| Priority queue | ✅ | Ordered task execution |

---

## Phase 21: Crafting System ❌

| Component | Status | Notes |
|-----------|--------|-------|
| Recipe definitions | ❌ | Recipe data structure |
| Crafting interface | ❌ | Crafting UI flow |
| Material consumption | ❌ | Use materials, produce items |
| Skill integration | ❌ | Manufacturing/alchemy skill checks |

---

## Documentation

| Document | Status | Location |
|----------|--------|----------|
| Packet Protocol | ✅ | docs/PACKET_PROTOCOL.md |
| JSON Protocol | ✅ | docs/JSON_PROTOCOL.md |
| Game Messages | ✅ | docs/GAME_MESSAGES.md |
| Progress Tracking | ✅ | docs/PROGRESS.md |
| Subsystem Interfaces | ✅ | docs/SUBSYSTEM_INTERFACES.md |
| Random Mob Generator | ✅ | docs/RANDOM_MOB_GENERATOR.md |
| Database Schema | ✅ | src/database/schema.sql |
| API Reference | ❌ | docs/API.md |
| Deployment Guide | ❌ | docs/DEPLOYMENT.md |

---

## Immediate Next Steps

Priority order for remaining work toward a playable game:

1. ~~**Ground Items / Loot Drops**~~ - ✅ NPC loot drops, ground item spawn/despawn, YAML drop tables
2. **Equip/Unequip Handlers** - Wire client requests to inventory equip logic
3. ~~**Combat/Spell Visual Broadcasts**~~ - ✅ Unified `combat_effect` broadcast + magic handler wired
4. ~~**NPC Interaction**~~ - ✅ Dialog trees, shop buy/sell/repair, bank deposit/withdraw
5. ~~**Death/Respawn**~~ - ✅ XP penalty, PK tracking, bounty, delayed respawn
6. ~~**Spell Effects System**~~ - ✅ Duration tracking, group slots, DoT/HoT, stat pipeline
7. **Ranged Combat** - Bow/crossbow projectiles
8. **Crafting System** - Manufacturing, alchemy
9. **War Mechanics** - Crusade, Heldenian, Apocalypse battle logic
10. ~~**Guild Persistence**~~ - ✅ Guilds and members persist to PostgreSQL, guild info on login

---

## Technical Debt

| Issue | Priority | Notes |
|-------|----------|-------|
| ~~Map loading from files~~ | ~~High~~ | ✅ Completed - .amd and .txt loading |
| ~~Item data loading~~ | ~~High~~ | ✅ Item registry from YAML |
| ~~NPC data loading~~ | ~~High~~ | ✅ NPC registry from YAML |
| ~~Magic data loading~~ | ~~High~~ | ✅ Magic registry from YAML |
| Unit tests | Medium | Limited test coverage |
| Integration tests | Medium | End-to-end testing |
| Performance profiling | Low | Not yet needed |
| Memory leak checking | Low | Valgrind/sanitizers |

---

## Recent Changes

### 2026-02-07 (e)
- **Combat/Spell Visual Broadcasts** - Unified `combat_effect` message for all combat/spell visual feedback
  - New `combat_effect` protocol message with effect_type discriminator (damage/heal/miss/dodge/block/resist/buff/debuff)
  - Wired `magic_system` to `game_handlers` - replaced TODO stub with actual spell casting via `instant_cast`/`begin_cast`
  - Spell cast callback broadcasts damage/heal/buff/debuff effects to nearby players
  - `on_damage_dealt` enriched with `combat_effect` broadcast (derives effect_type from hit_result flags)
  - Faction-scoped broadcasts for buff/debuff effects (only same-faction players see them)

### 2026-02-07 (d)
- **NPC Interaction System** - Dialog trees, shop buy/sell/repair, bank deposit/withdraw
  - YAML-driven shop registry (`shops.yaml`) and dialog registry (`dialogs.yaml`)
  - Shop pricing with charisma discount, territory restrictions, neutral map penalties
  - Two-step sell/repair flow (quote then confirm)
  - Dialog tree navigation with action routing (close, open_shop, open_bank, stub actions for citizenship/crusade/rewards)
  - 48 new unit tests for pricing, shop registry, and dialog registry

### 2026-02-07 (c)
- **Guild Persistence** - Guilds and members persist to PostgreSQL across server restarts, players see guild info on login

### 2026-02-07 (b)
- **YAML-Driven Loot System Refactor** - Complete rewrite of loot generation:
  - New `loot_registry` subsystem loads `loot_tables.yaml` with named item pools and per-NPC configs
  - Flat percentage system replaces nested dice chains (gold + items roll independently)
  - Two drop phases: `on_kill` (immediate gold + item drops) and `on_despawn` (body parts, rares, boss multi-drops)
  - Corpse cleanup timer in `npc_system` (5s check interval, 15s linger) fires `on_despawn_callback`
  - All legacy loot data ported: 10 melee tiers, wand tiers, 10 armor tiers, potions, body parts, rare pools
  - Boss multi-drops: Wyvern (5-15 items), Fire-Wyvern (5-15), Abaddon (12-20) on corpse despawn
  - Hellclaw and Tigerworm: guaranteed rare drops on despawn (100% chance from unique pools)
  - Deleted `loot_table.h`, removed unused `std::optional<loot_table>` from `npc.h`
  - New files: `loot_config.h`, `loot_registry.h/.cpp`, `loot_tables.yaml`
  - Rewritten: `loot_generator.h` (config-driven via `loot_registry`)

### 2026-02-07 (a)
- **Ground Items & Loot Drops** - Full NPC loot pipeline:
  - Real RNG in `loot_table.h` using `thread_local std::mt19937` (replaces placeholder stubs)
  - Loot tables built during NPC spawn from template gold range + drop entries
  - YAML `drops` array parsing in `npc_registry.cpp` for NPC drop table configuration
  - Sample drops added to Slime, Orc, Skeleton in `npcs.yaml`
  - Gold awarded directly to killer on NPC death via `inventory_->add_gold()`
  - Item drops: `create_from_template()` → `add_ground_item()` → broadcast `ground_item_spawn`
  - New `ground_item_spawn` protocol message (type + data struct + builder)
  - `broadcast_ground_item_spawn()` sends to all players who can see the drop position
  - `send_visible_ground_items()` sends existing ground items on teleport and enter game
  - Ground item despawn timer: 30s sweep, 3-minute lifetime, broadcasts `ground_item_removed` with `picker_id: 0`
  - `ground_item_entry` struct with `steady_clock::time_point` tracks drop timestamps
  - `remove_expired_ground_items()` in `world_subsystem` returns expired items for cleanup
  - `auth_handlers` receives `item_system*` to send ground items on enter game
  - Updated `JSON_PROTOCOL.md` with `ground_item_spawn` message documentation

### 2026-02-05
- **Spell Effects System** - Full implementation (`src/effect/`):
  - `effect_system` subsystem tracking active spell effects (buffs, debuffs, DoTs, HoTs)
  - Effect group slots via `magic_type` - only one effect per group per target (first-wins)
  - Groups: protection(11), hold/paralyze(12), invisibility(13), confusion(16), poison(17), berserk(18), cancellation(28), inhibition(29)
  - Duration tracking with auto-expiry and periodic tick processing (2s intervals)
  - DoT/HoT ticks: poison/burn deal damage, heal restores HP, mana drain/restore affects MP
  - Split player stat modifiers: `equipment_modifiers` + `effect_modifiers` → combined `modifiers`
  - Effect-managed status flag mask preserves non-effect flags (e.g. `gm_invisible`)
  - `compute_effect_modifiers()` pure function maps effects → stat_modifiers + player_status
  - `magic_system` buff/debuff rewritten to use effect_system (replaces hardcoded switches)
  - Cancellation spell removes all effects; Cure removes poison group
  - NPC combat context queries effect modifiers for attack/defense
  - NPC AI skips processing when stunned/frozen/paralyzed
  - Death/despawn/disconnect cleanup removes all effects
  - Callbacks: on_effect_applied, on_effect_removed, on_effect_tick
  - 38 new unit tests (997 total, all passing)
  - New files: `active_effect.h`, `effect_modifiers.h/.cpp`, `effect_system.h/.cpp`
  - Modified: `player.h`, `player_system.h/.cpp`, `spell.h`, `magic_system.h/.cpp`, `combat_system.cpp`, `npc_system.cpp`, `application.h/.cpp`, `enums.h`, `CMakeLists.txt`
- **Death/Respawn system** - Full implementation:
  - XP penalty on PvP death with level-floor clamping (no de-leveling)
  - PK point tracking: killers gain PK points for killing innocents
  - PK bounty rewards: gold for killing PKers
  - Status effects cleared on death
  - Respawn uses map `initial_points` instead of hardcoded {18,18}
  - Configurable respawn delay via scheduler (`respawn_delay_ms`)
  - `player_death_info` message sent to dead player with full penalty details
  - Player state saved after death penalties
  - Post-respawn: 50% HP/MP restore, 3s invulnerability
  - 16 new unit tests for XP penalty clamping and PK state transitions
- **Audit and update of PROGRESS.md** - Marked many systems as complete that were previously listed as stubs/planned:
  - Combat system: full damage calc, hit resolution, crits, PvP/PvE, kill rewards
  - Magic system: full spell casting, all spell types, cooldowns, learning
  - Skill system: weapon skills, experience, mastery, training
  - NPC system: pack AI, boss mechanics, spawn rules, behavior trees
  - Quest system: full implementation with all objective types
  - Social system: guilds and parties fully functional with trade system
  - Entity system: full ECS with component storage
  - Player system: stats calc, leveling, status effects, regen
  - Item system: instances, stacking, durability, effects
  - Admin system: command framework, muting, logging
  - War system: scheduling, states, territory (mechanics still TODO)
  - Registry systems: NPC, magic, item registries all loading from YAML
  - Scheduler: complete with game clock
  - Added phases 19-21 (Registries, Scheduler, Crafting)

### 2026-02-02
- **Implemented periodic auto-save system:**
  - Added `auto_save_config` with `enabled` and `interval_seconds` options
  - YAML config parsing for `auto_save:` section
  - `save_player(player_id)` - on-demand single player save for important interactions
  - `save_all_players()` - save all online players, returns count saved
  - Scheduler integration for periodic saves at configurable interval (default 5 minutes)
  - Final save of all players on server shutdown
  - Updated server.yaml with auto_save configuration section

### 2026-01-31
- **Implemented complete chat system:**
  - Prefix-based channel routing: `!` shout, `@` guild, `$` party, `#` whisper, `~` trade
  - Explicit channel override via JSON field
  - Local chat (15-tile range) as default
  - Global/shout chat (server-wide)
  - Guild chat (guild members only)
  - Party chat (party members only)
  - Whisper (private messages)
  - Trade channel
  - Profanity filtering with asterisk replacement
  - Rate limiting (3 msg/sec, 30 msg/min)
  - Per-player channel settings (enable/disable)
  - Player blocking (ignore messages from blocked players)
  - Chat callbacks for message distribution
- **Implemented command system:**
  - Separate `command_request` packet type (not parsed from chat)
  - Structured command with name, args array, named params
  - Built-in commands: `/who`, `/time`, `/pos`
  - Extensible for admin/GM commands
- **Added protocol types:**
  - `chat_message_request_data` - client chat request
  - `chat_message_broadcast_data` - server chat broadcast
  - `command_request_data` - client command request
  - `command_response_data` - server command response
  - Response builders for chat and commands
- **Wired chat to game handlers:**
  - `handle_chat_message()` - process incoming chat
  - `handle_command()` - process incoming commands
  - `on_chat_message()` callback for distribution
  - Integration with social_system for filtering and routing
- **Updated documentation:**
  - Added chat and command sections to GAME_MESSAGES.md
  - Complete examples for all chat channels
  - Command request/response formats

### 2026-01-30
- **Created comprehensive PACKET_PROTOCOL.md documentation:**
  - Complete legacy binary packet structures with byte offsets
  - All message IDs (MSGID_*) with hex values
  - Common action types (DEF_COMMONTYPE_*) - 50+ actions
  - Notification types (DEF_NOTIFY_*) - 100+ notifications
  - Motion packet structure (movement, attack)
  - Player character contents structure
  - Corresponding JSON format for each packet type
  - Data types reference (BYTE, WORD, DWORD, etc.)
  - Constants reference (nations, equipment slots, directions)
- Added motion message types (walk, run, stop)
- Added combat message type (attack with regular/dash/super)
- Added action message types (magic, skill, pickup, interact)
- Added position validation with desync detection
- Added is_running flag to position broadcasts
- Created GAME_MESSAGES.md documentation
- Created PROGRESS.md tracking document
- **Implemented map data loading from files:**
  - Binary .amd file loading (256-byte header + tile data)
  - Config .txt file parsing (teleports, spawn points, safe zones, mob spawners, waypoints)
  - Tile flags: blocked, teleport, farm, water, safe zone
  - Auto-detection of map properties from name (fightzone, icebound, etc.)
  - Integration with world_subsystem for automatic config loading
- **Implemented full enter_game flow:**
  - Full character loading from database with ownership verification
  - Player instance creation in player_system
  - Position setting in world at saved location
  - Connection binding to player
  - Full game state sent: character data, skills, equipment, visible entities
  - Spawn notification broadcast to nearby players
  - Logout/disconnect handling with state saving
  - Despawn notification to nearby players on logout
- **Implemented character data serialization:**
  - Added `player_skills` to player struct
  - JSON serialization for skills (type, level, experience)
  - JSON serialization for equipment (slot, item_id, durability)
  - Migrated from BYTEA to JSONB columns for better queryability
  - Skills/equipment properly loaded on enter_game, saved on disconnect
- **Integrated inventory_system with enter_game and save flows:**
  - Inventory created and populated from DB on enter_game
  - Bank storage created and populated from DB on enter_game
  - Gold loaded from character data into inventory_system
  - Full inventory serialized and saved to DB on disconnect
  - Bank storage serialized and saved to DB on disconnect
  - Gold saved to DB on disconnect
  - Inventory data now included in game_state_msg sent to client
  - Inventory/bank destroyed on logout/disconnect cleanup
  - GIN indexes added for JSONB columns for fast containment queries
