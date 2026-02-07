# Helbreath Server

> **See also:** [`../CLAUDE.md`](../CLAUDE.md) for shared coding standards, C++20 guidelines, and memory safety patterns.

## Project Overview

The Helbreath game server - a classic MMO server being modernized to C++20 with PostgreSQL persistence and WebSocket support.

### Current State

| Aspect | Original | Modernized |
|--------|----------|------------|
| Language | C++98 | C++20 |
| Database | File-based | PostgreSQL |
| Auth Protocol | Custom binary | WebSocket JSON |
| Game Protocol | Custom binary | Custom binary (compatible) |
| Architecture | Monolithic (~2MB) | Subsystem-based |

---

## Building the Project

### Prerequisites

- **CMake 3.20+**
- **Visual Studio 2022** (or MSVC 19.29+)
- **vcpkg** (for dependency management)

### Build Commands

**Using CMake Presets (recommended):**

```bash
# Configure and build (Debug)
cmake --preset default
cmake --build --preset default

# Configure and build (Release)
cmake --preset release
cmake --build --preset release
```

**Manual configuration (if vcpkg path differs):**

```bash
# Configure
cmake -B build -S . -DCMAKE_TOOLCHAIN_FILE=C:/vcpkg/scripts/buildsystems/vcpkg.cmake

# Build (Debug)
cmake --build build --config Debug

# Build (Release)
cmake --build build --config Release
```

**Quick rebuild (after initial configure):**

```bash
cmake --build build --config Debug
```

### Output

All outputs go to `bin/`:
```
bin/
├── hgserver.exe          # Main server
├── hgserver_tests.exe    # Tests
├── *.dll                 # Runtime DLLs
├── *.cfg                 # Configuration
├── GameConfigs/          # Game data
├── GameData/             # Game data
└── mapdata/              # Map files
```

### Running

```bash
cd bin
./hgserver.exe
```

### Dependencies (via vcpkg)

| Package | Purpose |
|---------|---------|
| **spdlog** | Logging |
| **nlohmann-json** | JSON parsing |
| **libpqxx** | PostgreSQL client |
| **libsodium** | Password hashing |
| **ixwebsocket** | WebSocket server |
| **openssl** | TLS/SSL |
| **zlib** | Compression |
| **gtest** | Unit testing |

---

## Documentation

**Check these before starting work:**

| Document | Purpose |
|----------|---------|
| [docs/PROGRESS.md](docs/PROGRESS.md) | Implementation status |
| [docs/PACKET_PROTOCOL.md](docs/PACKET_PROTOCOL.md) | Legacy binary protocol |
| [docs/JSON_PROTOCOL.md](docs/JSON_PROTOCOL.md) | WebSocket JSON protocol |
| [docs/GAME_MESSAGES.md](docs/GAME_MESSAGES.md) | In-game message formats |
| [docs/SUBSYSTEM_INTERFACES.md](docs/SUBSYSTEM_INTERFACES.md) | Detailed subsystem interface specs |

### Current Priorities

1. **Ground Items / Loot Drops** - NPCs drop items, players can pick up
2. **Equip/Unequip Handlers** - Wire client requests to inventory equip logic
3. **Combat Broadcasts** - Visual feedback to nearby players
4. **NPC Interaction** - Dialog, shops, banks

---

## Architecture

### Directory Structure

```
src/
├── main.cpp                    # Entry point
├── application.cpp/h           # Server orchestration
├── core/                       # Result types, utilities
├── config/                     # Configuration
├── network/                    # WebSocket server
├── bridge/handlers/            # Message handlers
├── auth/                       # Authentication, sessions
├── database/                   # PostgreSQL connection
├── player/                     # Player state, queries
├── world/                      # Maps, spatial queries
├── entity/                     # Entity system
├── combat/                     # Damage calculation
├── magic/                      # Spell system
├── npc/                        # NPC management
├── item/                       # Item system
└── ...                         # Other game systems
```

### Key Subsystems

| Subsystem | Files | Purpose |
|-----------|-------|---------|
| **Network** | `src/network/websocket_server.*` | WebSocket server |
| **Auth** | `src/auth/auth_system.*` | Login, sessions, characters |
| **Database** | `src/database/database_system.*` | PostgreSQL pool |
| **Player** | `src/player/player_system.*` | Player state, movement |
| **World** | `src/world/world_subsystem.*` | Maps, spatial queries |
| **Combat** | `src/combat/combat_system.*` | Damage calculation |

---

## Key Implementation Files

### Core Entry Points

| File | Purpose |
|------|---------|
| `src/application.cpp` | Server startup, subsystem wiring |
| `src/main.cpp` | Entry point |

### Network & Protocol

| File | Purpose |
|------|---------|
| `src/network/websocket_server.*` | Boost.Beast WebSocket |
| `src/network/json_protocol.*` | JSON message types |
| `src/bridge/handlers/auth_handlers.cpp` | Auth handlers |
| `src/bridge/handlers/game_handlers.cpp` | Game handlers |

### Player & World

| File | Purpose |
|------|---------|
| `src/player/player_system.*` | Player state, queries |
| `src/world/world_subsystem.*` | Maps, zones |
| `src/world/position.h` | Position types |

### Auth & Database

| File | Purpose |
|------|---------|
| `src/auth/auth_system.*` | Account/session/character management |
| `src/auth/account.h` | Account, character data structures |
| `src/database/database_system.*` | PostgreSQL connection pool |

### Game Systems

| File | Purpose |
|------|---------|
| `src/combat/combat_system.*` | Damage calculation, hit resolution, kill rewards |
| `src/magic/magic_system.*` | Spell casting, cooldowns, buffs/debuffs |
| `src/npc/npc_system.*` | NPC management, AI, spawning, bosses |
| `src/item/item_system.*` | Item instances, stacking, durability |
| `src/skill/skill_system.*` | Weapon skills, training, mastery |
| `src/quest/quest_system.*` | Quest journal, objectives, rewards |
| `src/social/social_system.*` | Guilds, parties, chat |
| `src/war/war_system.*` | War scheduling, territory (mechanics WIP) |
| `src/admin/admin_system.*` | GM commands, muting, audit logging |

### Configuration

| File | Purpose |
|------|---------|
| `src/config/server_config.h` | All configuration structs |
| `GServer.cfg.example` | Example configuration file |

---

## Subsystem Overview

The monolithic `CGame` class is being decomposed into:

| Subsystem | Responsibility |
|-----------|---------------|
| **Network** | Socket I/O, protocol, routing |
| **Session** | Client sessions, authentication |
| **World** | Maps, tiles, spatial queries |
| **Entity** | Entity lifecycle, components |
| **Player** | Player state, stats, progression |
| **NPC** | NPC behavior, AI, spawning |
| **Combat** | Damage calculation, hit resolution |
| **Magic** | Spells, effects, mana |
| **Skill** | Skills, training, mastery |
| **Item** | Items, equipment, durability |
| **Inventory** | Inventory management, stacking |
| **Guild** | Guilds, ranks, permissions |
| **Party** | Parties, grouping, loot |
| **Trade** | Trading, shops, economy |
| **Crafting** | Recipes, materials, creation |
| **War** | Crusade, Heldenian, territory |
| **Chat** | Messaging, channels, filtering |
| **Admin** | GM commands, moderation |
| **Persistence** | Save/load, database |
| **Scheduler** | Timed events, game clock |

---

## Migration Strategy

### Completed
- [x] CMake build system with vcpkg
- [x] PostgreSQL database integration
- [x] WebSocket authentication server
- [x] Player movement and position sync
- [x] Map loading and spatial queries
- [x] Combat system (damage calc, hit resolution, crits, kill rewards)
- [x] Magic system (all spell types, cooldowns, learning)
- [x] NPC system (AI, spawning, bosses, behavior trees)
- [x] Skill system (weapon skills, training, mastery)
- [x] Item system (instances, stacking, durability)
- [x] Inventory system (slots, bank, gold, trading)
- [x] Chat system (all channels, filtering, rate limiting)
- [x] Social systems (guilds, parties)
- [x] Quest system (all objective types, rewards)
- [x] Entity ECS system
- [x] Persistence (auto-save, full character save/load)
- [x] Registry systems (NPC, magic, item from YAML)
- [x] Scheduler with game clock

### In Progress
- [x] Ground items and loot drops
- [ ] Equip/unequip handler wiring
- [ ] Combat/spell visual broadcasts
- [ ] NPC interaction (dialog, shops)
- [ ] War battle mechanics
- [ ] Admin GM commands

### Planned
- [ ] Crafting/gathering
- [ ] Ranged combat
- [ ] Death/respawn flow
- [ ] Guild persistence
- [ ] Guild warehouse
- [ ] Friend list

---

## Server-Specific Notes

When working on the server:

1. **Ask questions frequently** - Use AskUserQuestion liberally to clarify requirements, validate assumptions, and confirm implementation approaches before writing code. When in doubt, ask.
2. **Check PROGRESS.md first** - Know what's implemented before starting. Don't rebuild what already exists.
3. **Update PROGRESS.md when done** - After completing a feature or significant component, update `docs/PROGRESS.md`: mark the relevant items as ✅, update the phase status, and add a dated entry under Recent Changes. Keep the "Immediate Next Steps" list current.
4. **Protocol compatibility** - Legacy binary protocol must match original
5. **Database transactions** - Use connection pool properly
6. **Thread safety** - WebSocket callbacks run on separate threads
7. **Update JSON_PROTOCOL.md** - Any changes to WebSocket message structures must be documented in `docs/JSON_PROTOCOL.md` so the client stays in sync
