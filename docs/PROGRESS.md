# Helbreath Server Modernization Progress

This document tracks implementation progress for the modernized Helbreath server.

**Last Updated:** 2026-02-22

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
| Weather system | ✅ | Weather state per map, cycling, client sync |
| Day/night cycle | ✅ | Game clock broadcast, `environment_update` every 10s |

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
| Combat mode | ✅ | Toggle attack/peace stance, broadcast to nearby, included in entity spawns |
| Action broadcasts | ✅ | Unified `player_action_broadcast` for attack, dash, magic, pickup animations |
| Ranged combat | ✅ | Bow/crossbow with arrow consumption, 2-10 tile range, projectile broadcasts |
| PK system | ✅ | PK point gain on innocent kill, bounty rewards, criminal/murderer status |
| Safe zone enforcement | ✅ | Blocks PvP attacks and offensive spells in town safe zones |

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
| Visual broadcasts | ✅ | `combat_effect` broadcast for damage, heal, miss, dodge, block, buff, debuff |

---

## Phase 8: Skill System ✅

| Component | Status | Notes |
|-----------|--------|-------|
| Skill tracking | ✅ | All weapon skills tracked per player |
| Skill experience | ✅ | Use-count leveling with tiered multipliers from YAML |
| Skill mastery | ✅ | Mastery levels |
| Weapon skill bonuses | ✅ | Damage and hit rate bonuses |
| Skill training | ✅ | Training mechanics |
| Skill reset | ✅ | Reset functionality |
| Manufacturing | ✅ | YAML-driven build recipes, crafting with skill checks |
| Alchemy | ✅ | YAML-driven craft/alchemy recipes, potion/gem crafting |
| Mining | ✅ | Mineral node lifecycle, generation, mining skill, use-count leveling |
| Fishing | ✅ | Engagement-based fishing with fluctuating catch chance, YAML-driven fish types |

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
| Quest NPCs | ✅ | City hall officers (Kennedy/William) offer the legacy hunting quests via dialog and the quest_* protocol |
| Guard NPCs | ✅ | Guards target criminal/murderer players (PK >= 30), 15d20 damage, 10-tile detection |

**Documentation:** `docs/RANDOM_MOB_GENERATOR.md`

---

## Phase 10: Item System ✅

| Component | Status | Notes |
|-----------|--------|-------|
| Item definitions | ✅ | Loaded from item_registry (YAML), v2 template with raw effect_type + effect_value1-6 |
| Item instances | ✅ | Individual items with state, owner tracking, damage_min/damage_max |
| Item stacking | ✅ | Stack/split operations |
| Durability | ✅ | Wear, repair system |
| Item effects | ✅ | Stat bonuses, equipment slot mapping |
| Item properties | ✅ | Weight, price, level requirements, tradeable/droppable flags |
| Ground items | ✅ | Items on map, pickup, per-item ground lifetime (template override for expiry) |
| Loot drops | ✅ | YAML-driven loot_tables.yaml, flat percentages, on_kill + on_despawn phases |
| Use item handler | ✅ | HP/MP/SP potions, food, recall scrolls, potion speed anti-cheat |
| Item attributes | ✅ | Per-instance upgrade level, enchantments, custom-made flag (m_dwAttribute) |
| Item upgrades | ✅ | Xelima/Merien stones with legacy probability table |
| Weapon effects | ✅ | On-hit enchantments: poison, mana conversion, spell trigger, charge critical |
| Special abilities | ✅ | SPECABLTY weapon abilities: hp_halve, poison, paralyze, warrior_boost, life_drain |
| Crafting attributes | ✅ | Custom-made flag, quality, recipe enchantments on manufactured items |
| Loot attributes | ✅ | Generated enchantments on NPC loot drops, admin item creation with attributes |
| Item operations | ✅ | item_ops namespace with business logic for all item operations |
| Item serialization | ✅ | Universal serialize_item() for all contexts (inventory, ground, trade, bank, shop) |
| Registry cross-validation | ✅ | Startup validation of loot tables + shops vs item_registry |

---

## Phase 11: Inventory System ✅

| Component | Status | Notes |
|-----------|--------|-------|
| Entry-based inventory | ✅ | Item_id-keyed, free-form pixel positioning (pos_x, pos_y), z-order layering |
| Add/remove items | ✅ | Full inventory management by item_id |
| Reposition items | ✅ | Update pos_x/pos_y/z_order, no slot swapping |
| Equipment (linked model) | ✅ | equipment_state holds item_id references to inventory; no cached data duplication |
| Equip/unequip | ✅ | Via item_ops, 14 equip slots (added ring_left, ring_right, angel, fullbody) |
| Bank system | ✅ | Paginated page+slot model (default 4 pages x 12 slots) |
| Gold management | ✅ | Loaded/saved with character |
| Weight system | ✅ | `max_weight = str * 5 + level * 5`, enforced on pickup, tracked per-entity |
| Trading | ✅ | 3-phase protocol (offer → lock → confirm) via trade_window |
| Enter game integration | ✅ | v2 inventory_data message on login (items + equipment + gold + weight) |
| Save/disconnect integration | ✅ | Inventory/bank/gold saved to DB; character_equipment table for linked model |
| Item protocol v2 | ✅ | ~70 v2 protocol messages, universal item object shape, action acknowledgments + state update channels |

---

## Phase 12: Interaction System 🔄

| Component | Status | Notes |
|-----------|--------|-------|
| Interact handler | ✅ | Request parsing and NPC routing |
| NPC interaction | ✅ | Dialog trees, shop, bank |
| Object interaction | - | Dropped 2026-09-04: the original has no doors or chests (only Heldenian gate doors, owned by the war system); loot is always a ground drop |
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
| Command list | ✅ | Server pushes available commands with enabled state, auto-updates on guild changes |

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
| Guild JSON protocol | ✅ | 19 message types, entity spawn guild data, guild update broadcasts |
| Party XP share | ✅ | Party exp sharing with original bonus table, equal split + level-weighted modes |
| Guild warehouse | ❌ | Shared storage |
| Friend list | ✅ | Add, remove, block/unblock, online status, DB persistence |

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

## Phase 16: War Systems ✅

| Component | Status | Notes |
|-----------|--------|-------|
| War scheduling | ✅ | War lifecycle management |
| War types | ✅ | Crusade, Heldenian, Apocalypse defined |
| War states | ✅ | Scheduled, preparing, active, ending, ended |
| Territory control | ✅ | Faction tracking, territory state |
| War statistics | ✅ | Contribution tracking |
| Crusade mechanics | ✅ | Strike points, duty selection, mana collection, meteor strikes, construction points, war unit summoning |
| Heldenian mechanics | ✅ | Tower defense and door defense modes |
| Apocalypse mechanics | ✅ | PvE event with force recall system |
| War rewards | ✅ | Contribution-based rewards, DB persistence (war_history + war_participants) |
| Admin war management | ✅ | Start/end war, war history, war participants endpoints |

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
| Server management | ✅ | `/reloadconfig` and `/shutdown [seconds] [reason] | cancel` GM commands (same paths as the admin web API) |
| Admin web tool API | ✅ | WebSocket API for admin dashboard and spectator modes, ~100 protocol messages |
| Admin API expansion | ✅ | Broadcast, mute, template browsing, war status, parties, player search, effects |
| Admin API phase 3 | ✅ | Audit log, config management, scheduler control, DB queries, NPC/ground item inspection, guild mutations, messaging, environment, shutdown |
| Admin API phase 4 | ✅ | Skills/spells/quests/effects management, account/character CRUD, spawn/spell template browsing, maintenance mode, IP bans, enriched player/inventory/search |
| Performance profiling | ✅ | Timing/counter/gauge metrics via `admin_perf_stats_request`, scoped timers on hot paths |
| Item audit logging | ✅ | Buffered item/gold transaction logging with per-instance audit flag, admin query API |

---

## Phase 18: Persistence ✅

| Component | Status | Notes |
|-----------|--------|-------|
| Periodic save | ✅ | Configurable auto-save interval, on-demand save methods |
| Character save | ✅ | Full save on disconnect (stats, position, skills, inventory+equipment, spells, quests, appearance, PK points, EK, contribution, stat points) |
| Inventory save | ✅ | Per-item rows in `items` table with bank_page/bank_slot columns |
| Equipment save | ✅ | Linked model via `character_equipment` table (slot → item_id references) |
| Skills save | ✅ | JSON serialization to JSONB column |
| Bank save | ✅ | JSON serialization to JSONB column |
| Gold save | ✅ | Stored in characters table |
| Guild save | ✅ | Guilds and members persist to PostgreSQL across server restarts |
| World state | ✅ | Not needed — ground items, mining nodes, fish, NPCs are ephemeral by design |

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

## Phase 21: Crafting System ✅

| Component | Status | Notes |
|-----------|--------|-------|
| Recipe definitions | ✅ | `recipe_config.h` shared structs (build_recipe, craft_recipe) |
| Build recipe registry | ✅ | YAML loader for 83 manufacturing recipes |
| Craft recipe registry | ✅ | YAML loader for 80 alchemy + 38 crafting recipes |
| Manufacturing system | ✅ | Skill-gated crafting, STR*2 cap, success formula |
| Alchemy system | ✅ | Difficulty-based crafting, INT*2 cap, success formula |
| Material consumption | ✅ | Ingredients consumed on both success and failure |
| Skill integration | ✅ | Manufacturing/alchemy skill checks, use-count leveling |
| Protocol messages | ✅ | 8 messages (4 manufacturing, 4 alchemy) |
| Dialog actions | ✅ | `open_manufacturing` / `open_alchemy` dialog triggers |
| Crafting interface | ✅ | List + craft request/response flow |

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
| Deployment Guide | ✅ | docs/DEPLOYMENT.md |

---

## Immediate Next Steps

Priority order for remaining work toward a playable game:

1. ~~**Ground Items / Loot Drops**~~ - ✅ NPC loot drops, ground item spawn/despawn, YAML drop tables
2. ~~**Equip/Unequip Handlers**~~ - ✅ Full equip/unequip flow with stat updates and broadcasts
3. ~~**Combat/Spell Visual Broadcasts**~~ - ✅ Unified `combat_effect` broadcast + magic handler wired
4. ~~**NPC Interaction**~~ - ✅ Dialog trees, shop buy/sell/repair, bank deposit/withdraw
5. ~~**Death/Respawn**~~ - ✅ XP penalty, PK tracking, bounty, delayed respawn
6. ~~**Spell Effects System**~~ - ✅ Duration tracking, group slots, DoT/HoT, stat pipeline
7. ~~**Ranged Combat**~~ - ✅ Bow/crossbow with arrow consumption and min/max range
8. ~~**Crafting System**~~ - ✅ Manufacturing (83 recipes) + alchemy (80+38 recipes) with YAML configs
9. ~~**War Mechanics**~~ - ✅ Crusade, Heldenian, Apocalypse battle logic with DB persistence and admin API
10. ~~**Guild Persistence**~~ - ✅ Guilds and members persist to PostgreSQL, guild info on login
11. ~~**Dynamic ground-field spells**~~ - ✅ `dynamic_object_system` with spawn/tick/expiry, spike step triggers, freeze/poison effects, and `dynamic_object_spawn/removed` protocol
12. ~~**Missing item IDs in configs**~~ - ✅ Loot/shop references audited: remapped to real IDs where the intended item exists, removed entries for items absent from this item set

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
| Performance profiling | ✅ | Lightweight timing/counter system, exposed via admin API |
| Memory leak checking | Low | Valgrind/sanitizers |

---

## Recent Changes

### 2026-09-05: WebSocket server capped at 128 clients (ixwebsocket default), found by the 200-bot run
- `ix::WebSocketServer` was built with port and host only, so it kept the library defaults: `maxConnections = 128` and a TCP backlog of 5. `websocket.max_connections: 2000` in server.yaml never reached it, and the 129th client was dropped during the handshake (close 1006) with nothing in the server log. Bots 129-200 of the scale test all died that way
- The server now passes `config_.max_connections` and a backlog of 256 to the library and logs both at startup
- Bots reconnect after `reconnectMs` when the socket closes outside a shutdown, instead of staying dead until the process restarts

### 2026-09-05: Bots report their current action, detour around blocked tiles, drop unreachable targets
- Status lines carry `acao=` (the aiTick branch in effect: engage, shop trip, resting, loot, wander...), `stepTowards` logs when it gives up after 9 refused moves, and a shop trip that does not reach the merchant in `shopTripMaxMs` is abandoned and logged. That is what finally showed why kill rates decayed: bots stood for minutes against `blocked_terrain` (mobs that wandered outside the walkable area) and `blocked_occupied` (crowds around the two merchant tiles) without a single log line
- `stepTowards` now tries the neighbouring directions when the direct step is refused and keeps a successful detour for `detourSteps` steps instead of turning back into the same obstacle; a target abandoned for being unreachable is skipped for `avoidTargetMs`
- 50-bot run after the change: 540 and 347 kills in the first two 5-minute blocks, 0 deaths, 0 protocol errors

### 2026-09-05: Bots read hunger from the wrong message (root of "starving bots" and "no weapon")
- `hunger_update`, `skill_update` and `skill_progress` shared one `case` that did `this.hunger = d.level`, so every skill progress message (level 5, 8...) became "hunger 5". The bot then believed it was starving, ate constantly and spent its gold on food: run 4 had 187 Meat purchases against 5 swords, 10 of 30 warriors unarmed with about 20 gold each, while the server-side hunger actually drains 1 point per minute. HANDOFF items 6.2 (hunger 0) and 6.3 (rich bots without weapons) trace back to this
- `hunger_update` now has its own case; the skill messages are silent

### 2026-09-04: Bots stop looping on loot they cannot carry
- Once a bot hit its carry limit the server put the item back ("Too heavy to carry", `pickup_result` success=false) but the bot ignored that reply, deleted the ground item optimistically, saw it re-broadcast and tried again every 200 ms tick without ever fighting again. With respawn working, drops piled up and bots froze one by one: run 3 went 281 -> 151 -> 42 kills per 5 minutes with bots sitting at full HP next to 84 mobs
- `pickup_result` failures now blacklist that ground item for `lootSkipMs`; two consecutive failures switch the bot to gold-only looting for `overweightMs`. `inventory_weight_update` is tracked: above `lootWeightCap` of max weight only gold is picked up, above `sellWeightRatio` the bot goes to sell whatever junk it has

### 2026-09-04: GM server management commands (`/reloadconfig`, `/shutdown`)
- `gm_command_context` gains `config`, `broadcast_all` and `request_shutdown` hooks (injected from `application.cpp`, so the admin module still does not depend on `application.h`)
- `/reloadconfig` re-reads `server.yaml` from the boot path and reports which sections apply live; `/shutdown [seconds] [reason]` broadcasts warnings at 5 min/60 s/30 s/10 s on the `shutdown_countdown` scheduler tag shared with the admin web API, `/shutdown cancel` aborts, `/shutdown` alone stops at once
- Docs: `docs/GM_COMMANDS.md` section "Server Management Commands"; tests in `test_gm_commands.cpp`
- `tools/bot/gm-smoke.mjs`: live check against a running server with an admin account (/reloadconfig and alias, /shutdown 20 with the 20 s and 10 s system-chat warnings, cancel, usage errors, then /shutdown 0 and a clean server exit); 16 checks. It stops the server at the end unless run with --no-final-shutdown

### 2026-09-04: Spawn points reschedule per death (respawn throughput no longer capped)
- `spawn_point` keeps one due time per death (`respawn_due`) instead of a single timer. The original single timer was reset by every death and never fired under 50 bots; the first fix (serial queue, one respawn per `respawn_time_ms`) capped each spawner at 12 respawns/min, which became the kill-rate ceiling in the bot runs (285 kills in the first 5 minutes from the initial stock, then 105-110 per 5 minutes). Now N deaths come back N at once when their own delay elapses, as the legacy spot-mob-generator did
- `respawn_time_ms` is read from `mapdata/*.yaml` (`spot_mob_generator.respawn_time_ms`, default 60000); `activate_spawns` clears pending due times
- Tests: `spawn_point_respawn_test` (initial stock, burst deaths return together, late death does not delay an earlier one, clear_pending)

### 2026-09-04: City hall quests wired end to end (loader, protocol, rewards, bots)
- `quest/quest_loader`: converts the legacy `quests.yaml` rows (Quest.cfg: type 1 hunt, type 7 go-place) into `quest_template`s. Giver is the city hall officer of the quest's side (Kennedy/Aresden, William/Elvine), targets resolve through `spot_mob_type_to_name` + the NPC registry, rewards map `-1` to XP, `-2` to XP scaled by the quest minimum level, item 90 to gold, other ids to items; hunting quests are repeatable
- Protocol: `quest_list/accept/abandon/complete/journal` request+response and the `quest_update` push (docs/protocol/quest.md). Dialog actions `open_quests` and `claim_rewards` on the officers are no longer stubs
- Rewards are paid from the bridge on the quest system's completion callback (XP via `add_experience`, gold via inventory + `gold_update`, items into the inventory); the NPC death callback now feeds `quest_system::on_kill` and pushes progress to the killer
- Officers spawn from `mapdata/*.yaml` with local spot types 20/21; bots take the first offered quest at level 11+, prefer its target and turn it in at the officer
- `tools/bot/quest-smoke.mjs`: end-to-end smoke of the flow against a running server (GM account, /teleport to Kennedy, list at level 1 vs 11, accept, push, journal, wrong NPC, premature turn-in, both dialog actions, abandon); 18 checks

### 2026-09-04: Party kills paid zero XP to everyone (`distribute_npc_kill_exp`)
- `party_member::current_map` was never set: `party::add_member` takes no map and `social_system::update_party_member_map` has no callers. `members_in_map()` therefore never matched the killer's map, `eligible` came back empty, and the function returned silently — every kill by a partied player awarded nothing. Found by the 50-bot run: 595 kills, 2 level-ups, 46/50 bots still level 1 and unarmed
- Fix: eligibility now resolves each member through the live player record (`players_->get_player(member.player)->current_map`), and an empty eligible set logs a warning and pays the killer instead of dropping the XP
- Bot harness (`autoPotion`): one `use_item_request` per cooldown (`potionCooldownMs`) and only after HP/MP changed since the last one (`potionSettleMs`), separate timers for red/blue. Before: 481 of 3159 potion uses were repeats of the same HP; after: 1 of 16 in a 25-minute run

### 2026-09-04: Per-weapon attack pace (weapon speed + STR), replacing the flat 100 ms cooldown
- `combat/attack_timing.h` (header-only): `attack_interval_ms()` = `base_ms + weapon.speed * speed_step_ms`, plus `str_penalty_ms` per STR point the attacker is short of the weapon's `str_speed_req` (derived as `speed * str_per_speed` when the item declares none), clamped to `[min_ms, max_ms]`. Item.cfg field 19 ("Speed", 0..15) was loaded into `item_template::speed` but never used by the server; the legacy client paced swings from it and the server only policed speed hacks
- `str_speed_req` is a new optional `items.yaml` field and is deliberately separate from `str_requirement`: low STR slows the swing, it never blocks equipping
- `server.yaml` gains an `attack_speed:` section (`enabled`, `base_ms`, `speed_step_ms`, `str_penalty_ms`, `min_ms`, `max_ms`, `tolerance_ms`, `str_per_speed`) parsed into `server_config::attack_speed`
- `game_handlers_combat.cpp`: the equipped weapon is resolved before the pace check; swings faster than `interval - tolerance_ms` are refused with `attack_too_fast`. Every `player_attack_response` (accepted or refused) now carries `attack_interval_ms` so clients can pace themselves without knowing the formula
- Bot harness: `engage()` adopts the server-reported interval (`this.attackIntervalMs`), logs pace changes with the weapon and STR, and no longer counts `attack_too_fast` refusals as swings
- Tests: `tests/test_attack_timing.cpp` (unarmed, speed term, STR penalty, explicit vs derived requirement, clamps, disabled)

### 2026-08-30: Sweep of ECS-entity vs player_id confusion (`get_player(player_id{entity.id})`)
- Full `src/` sweep for the recurring bug class where an ECS entity id (from the shared `entity_manager`) was cast to `player_id` (or vice versa). Entity ids and player ids are NOT interchangeable — the lookup returns nullptr (or the wrong player) and `if (auto* p = ...)`-guarded blocks are silently skipped. Standard fix: `player_system::get_player_by_entity(entity)` (O(1), const overload available)
- `magic_system.cpp`: 21 sites fixed — mana/HP/SP deduction, silenced/level/stat checks, range checks, safe-zone PvP checks, damage/heal scaling, SP drain, resurrection/heal targets, debuff resist rolls. `find_aoe_targets`/`find_line_targets` no longer fabricate `entity{player_id.value, 0}` for hit players — they push the real `p.ecs_entity` (fabricated handles broke downstream `combat_system::deal_damage` and the spell broadcast, which resolve via the real ECS index)
- `application.cpp`: effect ticks (heal / mana_drain / mana_restore) resolved players by entity id cast to `player_id` — periodic heal/mana effects on players never applied
- `game_handlers_combat.cpp`: `on_spell_cast` broadcast resolved caster/targets with the bad cast (spell visuals never showed target positions for players); respawn invulnerability was keyed on `entity{pid.value}` so it never matched the real defender entity — 3s respawn protection was silently inert
- `player_system::remove_player`: active spell effects were removed for `entity{p.id.value}` instead of `p.ecs_entity` — effects leaked in the effect system on logout
- `wave4_handlers.cpp` (legacy binary path): attack + spell cast fabricated caster/target entities from `ctx.player.value`; now resolve the player's real `ecs_entity` (their spell-knowledge/cooldown lookups could never match the JSON path's keys)
- Legitimate patterns confirmed and left untouched: admin API `get_player(player_id{req.player_id})` (wire value IS a player id); inventory/trade/crafting `entity_id{pid.value}` keying (consistent at creation and access); spatial-index `entity_id{ecs_entity.index()}`
- Tests updated to the real-entity convention (`test_magic.cpp`, `test_safe_zone.cpp`, `test_melee_pve.cpp`): fixtures now resolve `ecs_entity` instead of fabricating `entity(pid.value)` — the old form only passed because test fixtures spawn no NPCs, so both counters coincided
- Note: overlaps with the helbreathx-52 fixes below (20 magic_system lookup sites, on_spell_cast caster, healing apply_heal pid) were resolved in favor of master on merge; this audit's remaining delta is the AOE/line target fabrication, calculate damage/heal scaling, SP drain, debuff resists, effect ticks in application.cpp, respawn invulnerability, logout effect cleanup, wave4 legacy handlers, and the test fixtures

### 2026-08-30: Crafting/fishing potion references fixed — zero registry warnings at boot
- recipes.yaml alchemy results remapped to this item set by function: HealthPotion→RedPotion, ManaPotion→BluePotion, RevitalizingPotion→GreenPotion, Big variants likewise (results are referenced by name)
- fishing.yaml rare catch SuperPowerGreenPotion→SuperGreenPotion (391), display name aligned
- Last 7 startup registry warnings eliminated — the boot is now fully clean (0 warnings, 0 errors)

### 2026-08-30: Final 7 legacy magic types — spell cycle closed (66/66 loadable)
- Added the last missing `magic_type` values: `sp_down_area` (5), `sp_up_area` (7), `create` (10), `possession` (15), `tremor` (22)
- Staminar-Drain drains SP directly (effect1 average, area); Celebrating-Light (type 5, zero dice) loads as a harmless visual cast
- Staminar-Recovery / Great-Staminar-Recov. restore SP (area, allies) — healing path now branches on `sp_up_spot`/`sp_up_area` to restore SP instead of HP (fixes sp_up_spot which previously "healed" HP)
- Create-Food drops a random basic food (Baguette/Meat/Fish) at the caster's feet via item_ops + ground item broadcast (bridge on_spell_cast)
- Tremor is area earthquake damage (3d4+3); legacy knockback not ported (documented)
- Possession accepted as a no-op: legacy claimed ground-item ownership, which the modern server doesn't have
- Also fixed two more entity-id-as-player-id lookups found while wiring: `on_spell_cast` caster resolution (game_handlers_combat.cpp) and the healing `apply_heal` pid (magic_system.cpp)
- New registry test; 2531 tests pass. Every spell in magic.yaml now loads (0 "invalid magic_type" warnings)

### 2026-08-30: Mage bots, magic entity-id fixes, starting spells, regen-aware potion use
- magic_system.cpp: fixed 20 occurrences of resolving players via `get_player(player_id{entity.id})` — ECS entity ids are NOT player ids, so mana deduction, range checks, INT scaling and player-target effects were silently skipped (infinite mana; Magic-Missile dealt 4 instead of 17). All now use `get_player_by_entity`. Same bug class previously fixed in loot gold; codebase-wide audit spawned as a follow-up task.
- Starting spells: on first login (magic_data defaults to `'[]'`), players are granted every spell they qualify for by INT/MAG (`auth_handlers.cpp`); persisted on next save. TODO: replace with a purchase/learning flow.
- Bot AI: mage role (2 per party, INT 20/MAG 14, class_type 1) — Magic-Missile at range, self-Heal, BluePotion for mana, melee fallback; combat awareness (recent damage or adjacent mob) gates potion use; out-of-combat "resting" state waits for natural regen (HP ~1d(VIT)/5s, MP ~1d(MAG)×0.25/5s) instead of drinking potions.

### 2026-08-30: Dynamic ground-field subsystem (Spike-Field, Ice-Storm, Cloud-Kill)
- New `world::dynamic_object_system` (src/world/dynamic_object_system.*): spawn/tick/expiry of temporary tile objects, modernized from legacy CDynamicObject (docs/legacy/16_dynamic_objects.md)
- Legacy effect semantics: fire 3x3 1d6/tick; ice storm 5x5 3d3+5/tick + freeze (20s); poison cloud 3x3 1d6 (power<20) or 1d8/tick + poison; spikes 2d4 on step (movement-triggered, never hurt their owner); 1s tick; damage attributed to the caster
- `create_dynamic` (type 14) spells now load and cast: effect3 carries {object type, radius x, radius y} (Spike-Field 9/2/2 → 25 traps in 5x5; Ice-Storm 8; Cloud-Kill 10, power 40 from effect1)
- Spawn validation: walkable tile, not safe zone, one object per tile; fields cannot exist in safe zones and their area damage skips players standing in one
- New protocol broadcasts `dynamic_object_spawn` / `dynamic_object_removed` (documented in docs/protocol/combat.md); visible fields re-sent on enter_game and teleport
- Spike trigger wired into the movement handler; spawn/remove broadcasts wired via callbacks in game_handlers
- 8 new tests (`dynamic_object_test`), registry test updated; 2530 tests pass
- Not ported (out of scope, documented): weather shortening fire duration, fire↔ice mutual duration reduction, coal fire-spreading, NPC-move spike triggers

### 2026-08-30: Bot scale phase (10 → 50), perf fixes, city habitability
- Perf: `find_aggro_target` now resolves players via O(1) `get_player_by_entity` (was a full player scan per entity in aggro range, per NPC, per 100ms); `get_players_who_can_see` skips the far-admin scan unless any player has `sees_all` (new `player_system::set_sees_all` + incremental counter; gm_commands updated — never write `player::sees_all` directly)
- Map loading: `.amd` extension check is now case-insensitive (`ARESDEN.AMD`/`ELVINE.AMD` were silently skipped — those maps never existed at runtime); map names normalized to lowercase at load so name lookups ("aresden") work
- New characters now start in their nation's town (aresden/elvine; neutral → default) — `map_name` added to the create-character INSERT
- Auth: registration rate limit configurable via `server.yaml` (`auth.max_registration_attempts`, `auth.registration_cooldown`) — the hardcoded 3/hour per IP blocked bulk bot account creation
- `mapdata/aresden.yaml` + `mapdata/elvine.yaml`: initial points, mob spawners and merchants in areas validated by the new `tools/bot/scan-map.mjs` (.amd walkability scanner); `default.yaml` densified
- `tools/bot/gen-bots.mjs` generates N bots split half Aresden / half Elvine, parties of 5 per nation; bot client retries account creation with backoff on rate_limited

### 2026-08-30: Party protocol (JSON), loot gold fixes, bot infrastructure
- New JSON party messages: `party_invite_request/response`, `party_invite_notice`, `party_accept_request/response`, `party_leave_request/response`, `party_update` — handlers in `game_handlers_chat.cpp`, documented in `docs/protocol/social.md` (legacy binary `party_operation` remains unused by the WS protocol)
- Fixed loot gold being credited to the ECS entity id instead of the resolved `player_id` (phantom inventory; `gold_update` sent to wrong connection) — `game_handlers_npc.cpp`
- `npc_registry` YAML loader now parses `gold_min`/`gold_max`; added values for tier-1/2 mobs in `npcs.yaml`; fixed `npcs.yaml` `exp_dice:` → `exp:` key mismatch (mobs gave 0 XP). Remaining known key mismatches: `defense_ratio` vs `defense`, `size` vs `body_size`
- New spot-mob codes 15 (`ShopKeeper-W`) and 19 (`Gandlf`) for map spawners; `mapdata/default.yaml` created (mob spawners + merchants + initial point); `shops.yaml` potion item ids corrected (308/309/310 → 91/93/95)
- Headless bot client (`tools/bot/`): login, hunt/combat/flee/respawn, loot, shopping (buy/sell/repair/equip), party formation; runs N bots per process (`node bot.mjs all`)

### 2026-08-30: Loot table and shop item reference audit
- Full audit of loot_tables.yaml/shops.yaml against items.yaml (the tables were authored against a different item numbering — comments named the intended items)
- Remapped 16 references whose intended item exists under another ID (BlackShadowSword→926, The_Devastator→923, BarbarianHammer→928, KlonessAxe→929, StormBringer→924, GiantSword→46, Flameberge+1→55, MagicWand(MS20)→256, KnecklaceOfStoneGolem→647, SapphireRing→336 ×3, MagicWand tiers, etc.)
- Removed 32 pool entries whose intended item does not exist in this item set (AncientTablets, CritCandy, SSS/E.S.W/I.M.C manuals, XelimaCap/Hat/Helm, NecklaceOfXelima, DragonWand MS40, HolyBlade, GiantBattleHammer) — including placeholder junk drops (Tomato/Hoe/Garlic/Carrot standing in for Ice/Merien gear in boss pools)
- Rewrote ~50 stale comments to the real item names (behavior unchanged)
- Fixed silent wrong-item bugs in shops.yaml: ShopKeeper-E/W now sell RedPotion/BluePotion/GreenPotion (91/93/95) instead of MagicNecklace(MS10) + 2 missing IDs; Gandlf/William now sell ShortSword(8)/MainGauche(12) instead of Dagger variants (2/3)
- Startup validation warnings (375+ per boot) eliminated; 2522 tests pass

### 2026-08-30: Legacy magic types restored (12 spells re-enabled)
- Extended `magic_type` enum with legacy HBX Magic.cfg values: `create_dynamic` (14), `damage_linear` (19), `damage_area_no_center` (21), `damage_area_sp_down` (25), `armor_break` (26), `ice_linear` (27)
- Spells that previously failed to load now work: Bloody-Shock-Wave, Energy-Strike, Lightning-Strike, Meteor-Strike, Mass-Magic-Missile, Earthworm-Strike, Armor-Break, Blizzard (+ corrected Cancellation, Illusion-Movement, Mass-Illusion-Movement, Resurrection)
- New line-targeting (`find_line_targets`): Bresenham trace from caster toward target, up to 12 tiles, with faction/safe-zone filters (types 19/27)
- `damage_area_sp_down` drains SP from player targets (amount from effect2 dice, parsed into `sp_drain`)
- `armor_break` deals pure damage via `deal_pure_damage` (ignores defense) — `ignores_defense` flag now honored in the attack path
- Cancellation remapped from utility stub to offensive dispel (debuff category, removes target's active effects)
- Fixed 4 placeholder rows in magic.yaml (copied from Lightning-Strike in the original Magic.cfg): Cancellation→type 28, Resurrection→32, Illusion-Movement/Mass-Illusion-Movement→16
- `create_dynamic` (Spike-Field, Ice-Storm, Cloud-Kill) accepted in the enum but spells skipped at load with an info message — pending dynamic ground-object subsystem
- New test `magic_registry_legacy_damage_types`; 2522 tests pass

### 2026-08-30: Experience Gain Notification (`experience_update`)
- New server→client message `experience_update` sent whenever a player gains XP (solo/party NPC kill XP, crusade rewards, login reward delivery)
- Carries `experience_gained`, new total `experience`, and `level`; on level-up also `levels_gained`, new `max_hp`/`max_mp`/`max_sp`, and unspent `stat_points`
- Added `experience_gain_callback` to `player_system` (fired from `add_experience` only when XP actually changes; no-op at max level), wired in `game_handlers` — covers all `add_experience` call sites automatically
- Documented in `docs/protocol/player.md` and `docs/JSON_PROTOCOL.md`; updated stale "no explicit message" note in `docs/protocol/items.md`

### 2026-02-22: Item System v2 Redesign
- Rewrote item_template struct to match legacy .cfg format 1:1 (raw effect_type + effect_value1-6)
- Rewrote item_template YAML loader for 1:1 field mapping
- Updated equip_pos/equip_slot enums to 14 slots (added ring_left, ring_right, angel, fullbody)
- Rewrote equipment_state to linked model (slots hold item_id references, not cached data)
- Removed equipped_as from inventory_entry (equipment tracked via equipment_state)
- Rewrote bank_storage with paginated page+slot model (default 4 pages x 12 slots)
- Rewrote trade_window with 3-phase protocol (offer → lock → confirm)
- Created item_ops_types.h with 17 operation result structs
- Created item_serialization.h/cpp with universal item JSON serialization (serialize_item)
- Added damage_min/damage_max fields to item struct (replacing single attack_power average)
- Added ~70 v2 protocol messages (state updates, actions, trade, shop, bank, party loot)
- Created item_ops namespace with business logic for all item operations
- Created character_equipment DB table (linked model persistence)
- Added bank_page/bank_slot columns to items table
- Rewrote auth_handlers to send v2 inventory_data on login
- Rewrote all item handlers (equip, pickup, drop, shop, bank, trade, loot) to use item_ops + v2 protocol
- Added startup registry cross-validation (loot tables + shops vs item_registry)
- Added per-item ground lifetime (template override for expiry)
- ~200 new tests, 2522 total

### 2026-02-21: Inventory Positioning Refactor
- Replaced slot-indexed inventory array with item_id-keyed `inventory_entry` collection
- Each `inventory_entry` has: `item_id`, `count`, `pos_x`, `pos_y`, `z_order`, `equipped_as`
- Z-order layering for client rendering (auto-increment on add, compact on save)
- All client-server protocol changed from slot indices to item_id references
- Replaced `inventory_slot_update` with three new messages: `inventory_item_update`, `inventory_item_removed`, `inventory_weight_update`
- Removed `slot` from `inventory_item_msg`, added `z_order`
- Removed `swapped_to_inv_slot`/`shield_to_inv_slot` from equip response, `inventory_slot` from unequip/pickup response
- All request structs (equip, drop, use_item, shop sell/repair, bank deposit, admin remove, upgrade, reposition) use `item_id` instead of slot index
- Bank storage decoupled from inventory — standalone `bank_storage` class with `container_slot`
- Weight system: `max_weight = str * 5 + level * 5`, tracked per-entity in `inventory_system`
- Weight check on pickup; weight updates sent on pickup, drop, buy, sell, deposit, withdraw
- DB migration: `z_order` column added to `items` table
- Persistence: z_order loaded/saved, next_z_order restored on login
- 2281 tests (no regressions), server binary + test binary compile clean

### 2026-02-16: Item Protocol Reconciliation
- Renamed item attribute JSON keys from compact (`mt`/`mv`/`st`/`sv`/`cm`/`cq`) to readable names (`main_type`/`main_value`/`sub_type`/`sub_value`/`custom_made`/`custom_quality`)
- Added `inventory_slot_update` to all handlers that modify inventory: equip (1-3 updates), unequip, use item, shop buy/sell/repair, bank deposit/withdraw, manufacturing, alchemy, item upgrade, admin give/remove
- Added `gold_update` protocol message (gold total + change + reason) for: shop buy/sell/repair, NPC gold loot
- Added `bank_slot_update` protocol message mirroring `inventory_slot_update` for bank deposit/withdraw
- Added `build_inventory_item_msg()` shared helper to eliminate ~30 lines of boilerplate per call site
- Refactored pickup handler to use the shared helper
- 4 new protocol tests (2281 total)

### 2026-02-16: Unified Equipment into Inventory
- Equipment is now unified with inventory, matching legacy Helbreath behavior (equipped items stay in their inventory slots with a flag)
- Added `equipped_as` field (`std::optional<uint8_t>`) to `inventory_slot` — maps to equipment slot index
- `equipment_state` is now a read-only cache rebuilt from inventory scans on equip/unequip
- `equipped_item` gains `inv_index` to track which inventory slot the cached data came from
- Equip/unequip no longer moves items between containers — just toggles the `equipped_as` flag
- Unequip can never fail due to "inventory full" (item is already in inventory)
- 2H weapon + shield conflict resolved without needing a free slot (just clears shield flag)
- Configurable inventory size via `server_config.inventory_slots` (default 50, range 20-200)
- DB migration: `equip_slot` column added to `items` table; existing equipment rows merged into inventory
- Protocol: `inventory_item_msg` gains `equipped_slot` field; `game_state_msg` no longer has separate `equipment` array
- Persistence updated: `item_row.equip_slot` saved/loaded, no separate equipment serialization loop
- 6 new tests (2278 total)

### 2026-02-16: Inventory Protocol (Reposition, Drop, Max Weight)
- Added `inventory_reposition_request` — move items between slots with free-form pixel positioning
- Added `player_drop_item_request/response` — drop item from inventory to ground with ground_item_spawn broadcast
- Added `inventory_slot_update` — lightweight single-slot change notification
- Added `pos_x`/`pos_y` fields to `inventory_slot` and `inventory_item_msg` for client layout persistence
- Added `max_weight` field to `stat_update_data` (STR * 500 + Level * 500)
- Serialization/deserialization updated for pos_x/pos_y persistence via JSONB
- 13 new tests, 2203 total

### 2026-02-14: Item Attribute System (m_dwAttribute)
- Modernized legacy 32-bit `m_dwAttribute` bitfield into clean `item_attribute` struct
- Per-instance upgrade level (0-15), main/sub enchantments, custom-made flag with quality
- 13 main enchantment types (poison, sharp, ancient, mana_conversion, etc.) and 12 sub enchantment types (physical_resist, exp_bonus, etc.)
- Stat modifiers extended: physical/magic absorption, exp/gold bonus, weapon dice, charge critical, mana conversion
- `apply_item_attribute()` wired into `recalculate_equipment_modifiers()` pipeline
- Weapon on-hit effects: poison, spell trigger, mana drain, super attack charge via `process_weapon_effect()`
- Item persistence: attributes serialized to/from JSONB in equipment/inventory columns
- Upgrade system: Xelima (weapons) / Merien (armor) stones with legacy probability table (30% at +0, 1% at +10)
- Crafting integration: manufactured items get custom_made flag, quality from skill, recipe enchantments
- Special weapon abilities: SPECABLTY items grant activatable abilities (hp_halve, poison, paralyze, warrior_boost, life_drain) with 20-minute cooldown
- Loot attribute generation: per-drop-entry config with upgrade/enchantment chance in loot_tables.yaml
- Admin item creation: optional attribute parameter for giving enchanted items
- Protocol: attribute data included in inventory, equipment, ground item, pickup, equip, and unequip messages
- Display names: "+N" suffix for upgraded items (e.g., "Iron Sword +3")
- 9 new protocol messages (upgrade, special ability, protocol sync)
- ~150 new tests across 8 test files (2147 total)

### 2026-02-14: Item Transaction Audit Logging
- New `item_audit_system` subsystem with buffered, batched writes to `item_log` table
- Extended `item_log` table with `gold_amount`, `map_name`, `pos_x`, `pos_y` columns + indexes
- Extended `item_log_type` enum with gold-specific (40-45), guild bank (50-51), mail (60-61), admin (70-71) action types
- Per-instance `audited` flag on items: equipment audited by default, consumables not, per-template YAML override
- Instrumented 12 handler sites: pickup, shop buy/sell/repair, bank deposit/withdraw, crafting, alchemy, item use, NPC gold loot, admin give/remove
- Gold transactions always logged unconditionally (no audit flag check)
- Auto-flush every 10 seconds via scheduler + flush at 50 entries + flush on shutdown
- Admin API: `admin_item_log_request/response` with player/item/action filters and pagination
- DB migration `20260214_120000_add_item_audit_columns.sql`
- 2 new protocol messages, 27 new tests (2174 total)

### 2026-02-14: Entity Appearance Data Expansion
- Expanded `visible_entity_msg` with full player appearance: gender, skin, hair, underwear, level
- Equipment visuals (per-slot `appr`, `color`, `name`, `rarity`) pre-cached on `appearance_state`
- Added `appr_value`, `item_color`, `speed` fields to `item_template` (loaded from YAML `color_b1`, `color_r2`, `unk1`)
- `recalculate_appearance()` on `player_system` updates cached appearance on equip/unequip
- Status effects serialized as string array, active buffs with type/spell_id/magnitude/remaining_ms
- Created `entity_builders.h/.cpp` with `build_player_spawn()` and `build_npc_spawn()` helpers
- Replaced 10 inline `visible_entity_msg{...}` construction sites across auth/game handlers
- Wired `effect_system` and `item_registry` to both handler classes
- Updated protocol docs with new entity_spawn fields
- 18 new tests (1995 total)

### 2026-02-14: Combat Mode & Player Action Broadcasts
- Added combat mode toggle: `combat_mode_change_request/response/broadcast` (3 protocol messages)
- Player `combat_mode` field included in `visible_entity_msg` for entity spawns
- Added unified `player_action_broadcast` message replacing legacy `SendEventToNearClient_TypeA` / `MSGID_EVENT_MOTION`
- Broadcasts attack, dash_attack, magic, and pickup animations to nearby players
- Optional `target_id` and `spell_id` fields conditionally included based on action type
- Fixed entity_id mismatch bugs in auth_handlers: spawn and despawn were using `player_id` instead of `ecs_entity.id`
- 19 new tests (1977 total)

### 2026-02-13: Chat Command List
- Server pushes available commands to client on enter_game for autocomplete/UI display
- 2 new protocol messages: `available_commands` (full list), `command_availability_update` (partial delta)
- Static command registry with visibility predicates: 3 general (online, time, pos), 7 guild commands
- Guild commands enable/disable based on guild membership, rank permissions, and pending invites
- Admin commands included for GM+ players via `admin_system::get_commands_for_level()`
- Auto-updates sent on guild state changes: create, disband, leave, kick, invite, accept, decline, promote, demote
- Post-enter-game callback wired through `auth_handlers` → `game_handlers`
- 36 new tests (1958 total)

### 2026-02-13: Use Item Handler
- Implemented consumable item usage: HP/MP/SP potions, food (hunger), recall scrolls
- Added `consumable_effect_type` enum and `consumable_effect` struct to `item_template`
- YAML item loader now parses `color_r1`..`color_b2` fields as consumable effect params
- Potion speed anti-cheat tracker on player: detects rapid potion use (avg < 180ms), consumes item but skips effect
- Map restriction flags: `potions_disabled` and `recall_impossible` parsed from map YAML config
- SP potions also cure poison status (legacy behavior)
- Recall scrolls consume first then teleport to faction home town
- 2 new protocol messages: `player_use_item_request/response`
- 39 new tests (template, registry YAML parsing, anti-cheat, dice rolls, protocol, player/map/inventory)

### 2026-02-13: Guild JSON Protocol
- Added 19 guild message types: create, disband, leave, kick, invite, promote, demote, set_motd, info (request/response pairs), plus guild_update broadcast
- Guild handlers in game_handlers dispatch guild operations through social_system and update player struct fields
- Entity spawn messages now include `guild_name` and `guild_tag` for players in a guild
- Set `guild_tag` on player struct during enter_game alongside existing `guild_name`
- Added `broadcast_guild_update()` helper to notify online guild members of state changes
- 41 new tests (protocol parsing, builders, social operations, entity spawn guild data)

### 2026-02-13: Mana System Legacy Fixes
- Fixed GMG to require 10 charges (configurable) before firing meteor (was 1)
- Fixed mana pool to reset to 0 on charge consumption, discarding remainder
- Added per-stone mana tracking with regeneration and depletion (shared between factions)
- Added MP restoration to allied players within 5 tiles of mana collectors
- Added GMG damage vulnerability: 500 accumulated damage removes one mana charge
- Added `on_npc_damage` callback to npc_system for damage event notifications
- Added `crusade_mp_restore` protocol message for client-side effect rendering
- ~12 new tests

### 2026-02-12
- **Crusade Warfare System (Phases 1-6)** - Complete war battle mechanics implementation
  - Phase 1: Crusade core with strike points, duty selection (combat/mana/construction), scheduling
  - Phase 2: Mana collection pipeline and meteor strike mechanics
  - Phase 3: Construction point system and war unit summoning
  - Phase 4: Heldenian warfare with tower defense and door defense modes
  - Phase 5: Apocalypse PvE event and force recall system
  - Phase 6: War rewards, DB persistence (`war_history` + `war_participants` tables), admin war management API
  - ~30 total war protocol messages (9 new in Phase 6)
  - ~160+ total war system tests (22 new in Phase 6)
  - New DB tables: `war_history`, `war_participants`
  - Admin endpoints: `start_war`, `end_war`, `war_history`, `war_participants`
- **Skill System Refactor: Use-Count Leveling** - Replaced abstract experience system with use-count based progression
  - Formula: `uses_to_next_level = (level + 1) * N` where N comes from per-skill tiered multiplier tables
  - Default tier table: 0-19→10, 20-39→25, 40-59→50, 60-79→75, 80-89→100, 90+→125
  - Per-skill tier overrides loaded from `skills.yaml` (JSON format)
  - `skill_state` becomes pure data: `total_uses`, `uses_this_level` replace `experience`
  - `skill_system` owns all leveling logic via `record_skill_use()` and `add_skill_uses()`
  - All callers updated: combat hits, spell casts, manufacturing, alchemy, mining, fishing
  - Removed `exp_gained` from crafting result structs and protocol responses
  - Protocol: `skill_entry_msg` now sends `total_uses`, `uses_this_level`, `uses_to_next_level`
  - Admin API: `add_exp` action → `add_uses` action in `admin_modify_skills_request`
  - Character serialization: backward-compatible (old `"exp"` key silently ignored)
  - DB migration: `20260212_120000_skill_uses_format.sql` transforms JSONB skill data
  - 25 skill tests rewritten, 1424 total tests passing

### 2026-02-11
- **Performance Profiling System** - Lightweight built-in performance monitoring via admin API
  - `perf_stats_system` subsystem: timing, counters, and gauge metrics
  - RAII `scoped_timer` for automatic timing measurement (~100ns overhead)
  - Lock-free atomic counters for message/byte tracking
  - Circular `sample_buffer` for p99 percentile calculation
  - Instrumented: tick_total, npc_ai_update, spatial queries, scheduler tasks, message handlers
  - Gauge snapshot: players, NPCs, ground items, scheduled tasks, connections
  - `admin_perf_stats_request/response` protocol messages with include_timing/counters/gauges filters
  - Enable/disable toggle, per-second rate calculation
  - 24 new tests (1398 total)
- **Fishing System** - Engagement-based fishing mechanic with YAML-driven fish types
  - Legacy engagement mechanic: player activates skill near water → engages fish within 2 tiles → catch chance fluctuates every 4s → player chooses when to attempt catch
  - `fishing_config.h`: fish_type_config, fishing_state (on player struct), catch_result enum, fish_catch_result
  - `fishing_registry.h/.cpp`: YAML loader for fish types with weighted random selection for spawning
  - `fishing_system.h/.cpp`: fish node lifecycle (spawn/despawn/timeout), engagement tracking (max 30 per node), chance calculation (skill - difficulty based), catch logic
  - `fishing.yaml`: 12 fish types (8 common fish, 4 rare items) with difficulty 1-60 and weighted spawning
  - 8 new protocol messages: `fish_skill_request/response`, `fish_engaged`, `fish_chance_update`, `fish_catch_request/response`, `fish_spawn/despawn_broadcast`
  - `fish_point` struct added to map with YAML parsing (7 maps have fish spawn points)
  - `fishing_state` on player struct tracks engagement (fish_node_index, catch_chance, last_update)
  - Fish generation: 10% chance per 4s tick per map, fish lifespan 10-30 minutes
  - DEX * 2 skill cap, chance starts at 1% and fluctuates based on skill vs difficulty dice rolls
  - Up to 30 players can fish the same node; caught fish deletes node and notifies all engaged players
  - 17 new tests (1330 total)
- **Friend List System** - Friend requests, accept/decline, remove, block/unblock with online status tracking
  - Friend request flow: send → accept/decline/cancel, auto-accept when reverse request exists
  - Blocking is unidirectional: removes existing friendship and pending requests, prevents new requests
  - Database persistence: 3 tables (friend_requests, friends with normalized ordering, friend_blocks)
  - Online/offline notifications sent to accepted friends via auth lifecycle hooks
  - `friend.h`: friend_entry, friend_request, friend_result enum (11 values)
  - 20 new protocol messages: 8 request/response pairs + 4 push notifications
  - 8 game_handlers: send/accept/decline/cancel request, remove, block, unblock, list
  - 44 new tests (1374 total)

### 2026-02-09 (b)
- **Admin Panel API Phase 3** - 14 new request/response pairs for advanced server management
  - Audit log viewer: browse GM command history with optional executor filter
  - Server config management: get (sanitized), set (dot-notation patch with sentinel skip), reload (hot-apply)
  - Scheduled task management: list all tasks with metadata, cancel by tag
  - Canned database queries: 9 predefined read-only queries (top players, recent logins, search, bans, rankings, factions)
  - Live NPC state inspection: full NPC state per map (entity_id, HP, AI state, position)
  - Ground item management: list items per map with age, remove specific items with broadcast
  - Guild mutations: admin-bypass disband/kick/set_rank (skips permission checks)
  - Player messaging: send system chat directly to a specific player
  - Environment override: set time and/or weather globally or per-map
  - Server shutdown: immediate, countdown with warning broadcasts (5m/3m/1m/30s/10s), cancel
  - Infrastructure: scheduler task_metadata + for_each_task(), ground item map enumeration + removal, guild admin-bypass methods, config to_json/sanitized/apply_dot_values
  - 38 new tests (1274 total), config serialization tests

### 2026-02-09 (c)
- **Admin Panel API Phase 4 — Complete Coverage** - 15 new request/response pairs + 4 enriched existing endpoints
  - Skill management: set/reset/add_exp/reset_all player skills via `admin_modify_skills_request`
  - Spell management: learn/forget/level_up/reset_cooldowns via `admin_modify_spells_request`
  - Quest inspection: active quests with objectives, completed quest history
  - Quest manipulation: admin accept/abandon/complete quests
  - Effect management: remove all/group/single active effects
  - Account management: create accounts, reset passwords (admin bypass), change admin levels
  - Character management: admin create/delete characters (blocks online deletion)
  - Spawn point inspection: list all spawn points with filter by map
  - Spell template browsing: list all spells, detail view by ID or name
  - Maintenance mode: toggle login blocking for non-admins with custom message
  - IP ban management: list/add/remove runtime IP bans (checked at login)
  - Enhanced get_player: skills, spells, quests, appearance, extended stats
  - Enhanced get_inventory: bank slots alongside inventory
  - Enhanced modify_player: experience, faction, hunger, stat_points, appearance fields
  - Enhanced search_players: level range, map, faction, guild filters
  - Infrastructure: for_each_spell/for_each_spawn_point, maintenance mode on auth_system, IP bans on admin_system, admin password reset
  - Wired magic_system, quest_system, skill_system to admin_web_handlers (18th-20th subsystems)
  - 28 new tests (1302 total)

### 2026-02-09 (a)
- **Admin Panel API Expansion** - 10 new request/response pairs expanding admin web tool capabilities
  - Server broadcast: system-wide announcements via `admin_broadcast_request`
  - Mute management: `admin_mute_player_request` / `admin_unmute_player_request`
  - Template browsing: item and NPC template list/detail endpoints for give/spawn commands
  - War status: all active wars with scores, phases, participant counts
  - Party inspection: all active parties with member details
  - Player search: case-insensitive substring search across online players
  - Enhanced `admin_server_stats_response`: economy stats (total_gold), scheduled tasks, admin count
  - Enhanced `admin_list_players_response`: online_seconds from connection timing
  - Enhanced `admin_get_player_response`: active buff/debuff effects array
  - Infrastructure: `connected_at` on ws_connection, `for_each_party()` on social_system
  - Wired `war_system` and `effect_system` to admin_web_handlers (15th/16th subsystems)
  - 18 new tests (1235 total)

### 2026-02-08 (e)
- **Admin Web Tool API** - Server-side WebSocket API for admin web dashboard and spectator modes
  - ~50 new protocol messages for server stats, player/NPC/map/guild/account management, and spectator subscriptions
  - `admin_dashboard` connection state with subscription tracking
  - `enter_admin_mode` auth flow requiring gamemaster+ permission level
  - Spectator system: map subscription and player-follow modes with real-time game broadcasts
  - Push notifications: player connect/disconnect events, server-wide chat logging
  - 29 new tests

### 2026-02-08 (d)
- **Safe Zone PvP Enforcement and Guard NPCs** - Town safety system
  - Safe zones block player-vs-player attacks and offensive spells; attackers receive a rejection message
  - Guard NPCs target only criminal/murderer players (PK points >= 30)
  - Guard templates updated with combat stats (15d20 damage) and 10-tile detection range
  - 14 guards spawned per town (2 per safe zone)
  - 16 new tests

### 2026-02-08 (c)
- **Character Persistence Audit & Fixes** - Comprehensive audit of all character data save/load paths
  - **Data-loss bug fixed:** Appearance fields (`hair_style`, `hair_color`, `skin_color`, `underwear_color`) were hardcoded to 0 on every save, permanently destroying character appearance after first auto-save. Added appearance fields to `player` struct and save them correctly.
  - **PK points now persisted:** Criminal/murderer status (`pk.points`) was lost on disconnect, resetting murderers to innocent. New `pk_points` DB column.
  - **Enemy kill count now persisted:** EK count reset to 0 each session. New `enemy_kill_count` DB column.
  - **Contribution now persisted:** Contribution points lost on disconnect. New `contribution` DB column.
  - **Stat points now persisted:** Unspent stat points vanished on logout. New `stat_points_available` DB column.
  - **Wired up unused DB columns:** `luck` and `reward_gold` existed in schema but were never read or written by the active code path. Now loaded and saved.
  - Database migration: `20260208_132227_add_character_persistence_fields.sql`
- **Protocol Documentation Update** - Documented all previously undocumented messages and fields
  - Documented 5 missing message types: `spell_list_update`, `mine_request`, `mine_response`, `mineral_spawn`, `mineral_despawn`
  - Documented ranged combat fields on `combat_attack_broadcast` (`attack_mode`, `projectile_type`)
  - Documented ranged fields on `player_attack_response` (`is_ranged`, `ammo_count`, `ammo_template_id`)
  - Documented position + ranged fields on `npc_attack` (`attacker_x/y`, `target_x/y`, `is_ranged`, `projectile_type`)
  - Fixed attack type enum: `super` renamed to `ranged` in docs to match code

### 2026-02-08 (b)
- **Weather/Day-Night Client Sync** - Full environment state synchronization to clients
  - `environment_update` protocol message: periodic broadcast every ~10s with game clock hour/minute, is_day flag, and weather type
  - Weather cycling logic on per-map basis: rain or snow selected based on map config, intensity ramps up/down naturally
  - Initial environment state included in `enter_game_response` as `world.environment` object
  - Environment update sent immediately on teleport so client reflects destination map's weather
  - `/settime` and `/setweather` admin commands for GM environment control
  - Weather types: clear, light_rain, rain, heavy_rain, light_snow, snow, heavy_snow, windy, stormy
  - 13 new tests

### 2026-02-08 (a)
- **Crafting System** - Full manufacturing and alchemy system with YAML-driven recipes
  - `recipe_config.h` shared data structures: `build_recipe`, `craft_recipe`, `recipe_ingredient`, `craft_result`
  - `build_recipe_registry` loads 83 manufacturing recipes from `build_recipes.yaml`, auto-assigns sequential IDs, resolves item names via `item_registry`
  - `craft_recipe_registry` loads 80 alchemy recipes from `recipes.yaml` + 38 crafting recipes from `craft_recipes.yaml`, uses YAML `id` field
  - `manufacturing_system`: skill-gated crafting, STR*2 skill cap, success formula (base + skill bonus capped at +40 + DEX/2, clamped 10-95%), ingredient aggregation and consumption on both success/failure, XP gain via `skill_system`
  - `alchemy_system`: difficulty-based crafting, INT*2 skill cap, success formula (100-difficulty + skill/2 + INT/3, clamped 5-98%), always grants XP based on difficulty/3
  - 8 new protocol messages: `manufacture_list_request/response`, `manufacture_request/response`, `alchemy_list_request/response`, `alchemy_request/response`
  - Game handlers for all 4 request types with quest integration (`on_item_crafted`)
  - `open_manufacturing` / `open_alchemy` dialog actions trigger recipe list responses
  - 46 new tests: build_recipe_registry (11), craft_recipe_registry (9), manufacturing success formula (12), alchemy success formula (14)

### 2026-02-07 (g)
- **Party XP Share** - NPC kill XP distribution with party sharing
  - `calculate_party_exp_share()` and `calculate_level_weighted_exp()` pure functions in `party.h`
  - Original Helbreath bonus table: 2→2%, 3→5%, 4→7%, 5→10%, 6→14%, 7→17%, 8→20%
  - Three modes: individual (full to killer), equal_split, level_weighted
  - Low XP threshold: kills under 10 XP go entirely to killer (matches legacy)
  - Filters eligible members: same map + alive (hp > 0)
  - `distribute_npc_kill_exp()` wired into NPC death callback in game_handlers
  - 19 new unit tests for both sharing functions

### 2026-02-07 (f)
- **Equip/Unequip Handlers** - Full client-facing equip/unequip flow with stat updates and broadcasts
  - 6 new protocol messages: `player_equip_request/response`, `player_unequip_request/response`, `equipment_change_broadcast`, `stat_update`
  - `equip_mapping.h` utility maps item `equip_pos` to player `equip_slot` with validation
  - Two-handed weapon logic: auto-unequips shield (with inventory space check), blocks shield equip
  - Swap logic: old equipped item goes to freed inventory slot (no extra slot needed)
  - Stat recalculation + `stat_update` message sent to player after every equipment change
  - `equipment_change_broadcast` sent to nearby players for visual updates
  - Requirement checks (level, STR, DEX, INT, MAG) before equipping
  - Rejects equip/unequip while dead or trading
  - **Bug fix:** Shield mapping - `left_hand` (equip_pos 7) was incorrectly mapped to `weapon` instead of `shield`
  - **Bug fix:** Two-handed flag derived from `equip_pos == twohand` instead of unreliable YAML `is_two_handed` numeric field
  - Renamed `item_template::is_two_handed` to `two_hand_modifier` (int16_t) preserving STR scaling value
  - Removed misleading `equipment_state::is_two_handed()` method

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
