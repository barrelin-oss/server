# Helbreath Server Modernization Progress

This document tracks implementation progress for the modernized Helbreath server.

**Last Updated:** 2026-02-02

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

## Phase 4: World & Entity System 🔄

| Component | Status | Notes |
|-----------|--------|-------|
| Position/direction types | ✅ | position, direction, rect |
| Map structure | ✅ | Map class with tiles, spatial index |
| Tile system | ✅ | Static/dynamic tiles, flags, walkability |
| Spatial indexing | ✅ | Grid-based entity queries |
| Entity manager | 🔄 | Basic structure, needs components |
| Entity visibility | ✅ | 20-tile Chebyshev distance, spawn/despawn |
| Map loading (.amd) | ✅ | Binary tile data from legacy format |
| Map config loading (.txt) | ✅ | Teleports, spawn points, safe zones, spawners |
| Teleport system | ✅ | Gate definitions loaded from config |

---

## Phase 5: Player System 🔄

| Component | Status | Notes |
|-----------|--------|-------|
| Player state | ✅ | Stats, position, HP/MP/SP |
| Movement (walk) | ✅ | Direction-based, validation |
| Movement (run) | ✅ | 2-tile movement |
| Position sync | ✅ | Client validation, desync detection |
| Stats calculation | 📋 | Derived stats from base + equipment |
| Level/experience | 📋 | XP gain, level up |
| Death/respawn | ❌ | Death handling, respawn location |
| Hunger system | ❌ | Hunger decay, effects |
| Status effects | 📋 | Buffs, debuffs, duration tracking |
| Save/load | ✅ | Skills, equipment saved/loaded on enter/disconnect |

---

## Phase 6: Combat System 🔄

| Component | Status | Notes |
|-----------|--------|-------|
| Attack handler | 🔄 | Request parsing done, combat logic TODO |
| Damage calculation | 📋 | Formula from original game |
| Hit/miss resolution | 📋 | Accuracy, dodge, block |
| Critical hits | 📋 | Crit chance, multiplier |
| Attack types | 📋 | Regular, dash, super defined |
| Melee combat | ❌ | Range checking, weapon types |
| Ranged combat | ❌ | Bow/crossbow, projectiles |
| Combat broadcasts | ❌ | Notify nearby players of combat |
| PK system | ❌ | Criminal status, penalties |

---

## Phase 7: Magic System 🔄

| Component | Status | Notes |
|-----------|--------|-------|
| Magic handler | 🔄 | Request parsing done, cast logic TODO |
| Spell definitions | 📋 | Load from data files |
| Mana cost/regen | 📋 | MP consumption, regeneration |
| Spell targeting | 📋 | Single, AoE, self, ground |
| Damage spells | ❌ | Energy bolt, fire, ice, etc. |
| Heal spells | ❌ | Heal, mass heal |
| Buff spells | ❌ | Protection, haste, etc. |
| Debuff spells | ❌ | Poison, paralyze, etc. |
| Spell effects | ❌ | Visual effects, broadcasts |

---

## Phase 8: Skill System 🔄

| Component | Status | Notes |
|-----------|--------|-------|
| Skill handler | 🔄 | Request parsing done, skill logic TODO |
| Skill definitions | 📋 | 24 skills from original |
| Skill experience | ❌ | Gain XP through use |
| Skill effects | ❌ | Per-skill implementations |
| Manufacturing | ❌ | Item creation |
| Alchemy | ❌ | Potion creation |
| Mining | ❌ | Resource gathering |
| Fishing | ❌ | Fish catching |

---

## Phase 9: NPC System 🔄

| Component | Status | Notes |
|-----------|--------|-------|
| NPC data structure | ✅ | Stats, position, AI state |
| NPC registry | ✅ | Load from npcs.yaml |
| Spawn point system | ✅ | Rectangular areas, max count, respawn timers |
| Random mob generator | ✅ | Level-based spawn tables (see docs/RANDOM_MOB_GENERATOR.md) |
| NPC spawning | ✅ | Manual spawn, spawn point spawn, random mob spawn |
| NPC despawning | ✅ | Death handling, cleanup |
| NPC AI | ✅ | State machine (idle, wander, chase, attack, flee, return home) |
| Aggro detection | ✅ | Range-based, sight checks |
| Pathfinding | ✅ | Basic move-towards-target |
| NPC combat | ✅ | Attack execution, damage calculation integration |
| NPC loot | ❌ | Drop tables, gold |
| NPC dialog | ❌ | Conversation trees |
| Shop NPCs | ❌ | Buy/sell interface |
| Quest NPCs | ❌ | Quest givers |
| Guard NPCs | ❌ | Town protection |

**Documentation:** `docs/RANDOM_MOB_GENERATOR.md`

---

## Phase 10: Item System 🔄

| Component | Status | Notes |
|-----------|--------|-------|
| Item definitions | 📋 | Load from Item.cfg |
| Item instances | 📋 | Individual items with state |
| Ground items | ❌ | Items on map, pickup |
| Pickup handler | 🔄 | Request parsing done, logic TODO |
| Item properties | ❌ | Stats, requirements, effects |
| Durability | ❌ | Wear, repair |
| Item stacking | ❌ | Stackable items, splitting |

---

## Phase 11: Inventory System 🔄

| Component | Status | Notes |
|-----------|--------|-------|
| Inventory slots | ✅ | 50 slots defined, serialized to JSONB |
| Add/remove items | ✅ | Inventory management working |
| Move items | ✅ | Slot swap/move implemented |
| Equipment slots | ✅ | 12+ slots defined, serialized to JSONB |
| Equip/unequip | 📋 | Handler needed, stat application TODO |
| Equipment effects | ❌ | Set bonuses, special effects |
| Bank system | ✅ | 200 slots, serialized to JSONB |
| Gold management | ✅ | Loaded/saved with character |
| Enter game integration | ✅ | Inventory/bank/gold loaded from DB |
| Save/disconnect integration | ✅ | Inventory/bank/gold saved to DB |

---

## Phase 12: Interaction System 🔄

| Component | Status | Notes |
|-----------|--------|-------|
| Interact handler | 🔄 | Request parsing done, logic TODO |
| NPC interaction | ❌ | Dialog, shop, bank |
| Object interaction | ❌ | Doors, chests, etc. |
| Shop interface | ❌ | Buy/sell transactions |
| Bank interface | ❌ | Deposit/withdraw |

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
| Chat filter | ✅ | Profanity filter with asterisks |
| Rate limiting | ✅ | 3/sec, 30/min limits |
| Channel settings | ✅ | Per-player enable/disable |
| Player blocking | ✅ | Block player messages |
| Command system | ✅ | Separate command_request packet |

---

## Phase 14: Social Systems 🔄

| Component | Status | Notes |
|-----------|--------|-------|
| Party system | ✅ | Create, invite, accept, leave, kick |
| Party settings | ✅ | Loot mode, XP mode, leader transfer |
| Party XP share | 📋 | Experience distribution logic TODO |
| Guild system | ✅ | Create, join, leave, kick, ranks |
| Guild permissions | ✅ | Invite, kick, promote, demote, etc. |
| Guild warehouse | ❌ | Shared storage |
| Friend list | ❌ | Add, remove, online status |
| Trade system | ❌ | Player-to-player trading |

---

## Phase 15: Quest System ❌

| Component | Status | Notes |
|-----------|--------|-------|
| Quest definitions | ❌ | Quest data structure |
| Quest tracking | ❌ | Active quests, progress |
| Quest objectives | ❌ | Kill, collect, deliver |
| Quest rewards | ❌ | XP, items, gold |
| Quest NPCs | ❌ | Accept, complete |

---

## Phase 16: War Systems ❌

| Component | Status | Notes |
|-----------|--------|-------|
| Nation system | 📋 | Aresden vs Elvine |
| Crusade | ❌ | Scheduled warfare |
| Heldenian | ❌ | Castle siege |
| Apocalypse | ❌ | World boss event |
| Territory control | ❌ | Map ownership |
| War rewards | ❌ | Contribution points |

---

## Phase 17: Admin System ❌

| Component | Status | Notes |
|-----------|--------|-------|
| Admin levels | 📋 | Defined in auth |
| GM commands | ❌ | Teleport, spawn, etc. |
| Player management | ❌ | Kick, ban, mute |
| Server management | ❌ | Reload, shutdown |
| Logging/audit | ❌ | Action logging |

---

## Phase 18: Persistence 🔄

| Component | Status | Notes |
|-----------|--------|-------|
| Periodic save | ✅ | Configurable auto-save interval, on-demand save methods |
| Character save | ✅ | Full save on disconnect (stats, position, skills, equipment, inventory) |
| Inventory save | ✅ | JSON serialization to JSONB column |
| Equipment save | ✅ | JSON serialization to JSONB column |
| Skills save | ✅ | JSON serialization to JSONB column |
| Bank save | ✅ | JSON serialization to JSONB column |
| Gold save | ✅ | Stored in characters table |
| Guild save | ❌ | Guild data |
| World state | ❌ | Dynamic objects |

---

## Documentation

| Document | Status | Location |
|----------|--------|----------|
| Packet Protocol | ✅ | docs/PACKET_PROTOCOL.md |
| JSON Protocol | ✅ | docs/JSON_PROTOCOL.md |
| Game Messages | ✅ | docs/GAME_MESSAGES.md |
| Progress Tracking | ✅ | docs/PROGRESS.md |
| Database Schema | ✅ | src/database/schema.sql |
| API Reference | ❌ | docs/API.md |
| Deployment Guide | ❌ | docs/DEPLOYMENT.md |

---

## Immediate Next Steps

Priority order for a minimally playable game:

1. **Combat System** - Wire attack handlers to damage calculation
2. **NPC System** - Basic NPCs that can be attacked and killed
3. **Item/Loot System** - NPCs drop items, players can pick up
4. ~~**Inventory System**~~ - ✅ Inventory/bank/gold integrated with enter_game and save
5. **Equip/Unequip Handlers** - Client requests to equip items
6. **Magic System** - Basic spells working
7. **Death/Respawn** - Handle player death
8. ~~**Chat System**~~ - ✅ All channels implemented with prefix routing
9. ~~**Persistence**~~ - ✅ Periodic auto-save with configurable interval

---

## Technical Debt

| Issue | Priority | Notes |
|-------|----------|-------|
| ~~Map loading from files~~ | ~~High~~ | ✅ Completed - .amd and .txt loading |
| Item data loading | High | Need Item.cfg parser |
| NPC data loading | High | Need NPC.cfg parser |
| Magic data loading | High | Need Magic.cfg parser |
| Unit tests | Medium | Limited test coverage |
| Integration tests | Medium | End-to-end testing |
| Performance profiling | Low | Not yet needed |
| Memory leak checking | Low | Valgrind/sanitizers |

---

## Recent Changes

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
