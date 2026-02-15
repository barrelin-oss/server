# Helbreath Server

## Project Overview

The Helbreath game server - a classic late-1990s/early-2000s 2D MMORPG being modernized to C++20 with PostgreSQL persistence and WebSocket support.

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
- **GCC 11+** or **MSVC 19.29+**
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

### Running Tests

```bash
./bin/hgserver_tests
```

### Output

All outputs go to `bin/`:
```
bin/
├── hgserver.exe          # Main server
├── hgserver_tests.exe    # Tests
├── *.dll                 # Runtime DLLs
├── *.cfg                 # Configuration
├── game_configs/         # Game data
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

## Coding Style

### Naming Convention

**All code must follow stdlib-style snake_case naming:**

| Element | Convention | Example |
|---------|------------|---------|
| Types (classes, structs, enums) | snake_case | `player_state`, `damage_type` |
| Variables | snake_case | `health`, `player_name` |
| Functions/Methods | snake_case | `calculate_damage()`, `get_player()` |
| Constants | snake_case with constexpr | `max_inventory_slots` |
| Files | snake_case | `player_state.h`, `combat_system.cpp` |
| Namespace | `hb` | `namespace hb { }` |

**What to avoid:**
- **No Hungarian notation**: No `bVar`, `iCount`, `szString` prefixes
- **No member prefixes**: No `m_` prefix for class members
- **No C-prefix**: No `CGame`, `CClient` - just `game`, `client`
- **No SCREAMING_SNAKE**: Use `constexpr` lowercase instead

### Formatting

| Rule | Convention |
|------|------------|
| Brace style | **Allman** (opening brace on its own line) |
| Indentation | 4 spaces (no tabs) |
| Line length | 120 characters max |
| Pointer/reference | `int* ptr` not `int *ptr` |

**Note:** Some older files still use K&R braces. All new code and modified code must use Allman. Do not mix styles within a function — if you modify a K&R function, convert the entire function to Allman.

```cpp
// GOOD (Allman braces)
class player_state
{
    int32_t health;
    std::string name;
    bool is_active;
};

void calculate_damage(int raw_damage, int armor)
{
    if (raw_damage > 0)
    {
        // ...
    }
}

inline constexpr auto max_level = 180;

// BAD (K&R braces, legacy style)
class CPlayerState {              // No C-prefix, use Allman braces
    int m_iHealth;                // No m_ prefix, no Hungarian notation
    char* m_szName;               // No sz prefix
    BOOL m_bIsActive;             // No BOOL, no m_b prefix
};
#define MAX_LEVEL 180             // Use constexpr instead
```

---

## C++20 Guidelines

### Preferred Language Features

```cpp
// Use concepts to constrain callback templates
template<typename Func>
    requires std::invocable<Func, player_id, const player&>
void for_each_player(Func&& func) const;

// Use std::span instead of raw pointer + size
void process_items(std::span<const item> items);

// Use std::optional for nullable returns
auto find_player(uint32_t id) -> std::optional<player_state&>;

// Use std::expected (C++23) or result<T,E> for error handling
auto create_item(item_template tmpl) -> std::expected<item, std::string>;

// Use std::string_view for non-owning string parameters
void send_message(client_id id, std::string_view message);

// Use designated initializers
auto config = server_config
{
    .port = 2848,
    .max_clients = 2000,
};

// Use structured bindings
auto [success, player] = player_manager.authenticate(credentials);

// Use std::erase_if instead of erase-remove idiom
std::erase_if(items, [](const auto& item) { return item.expired(); });

// Use constexpr for compile-time computation
inline constexpr auto max_inventory_slots = 50;

// Use enum class with underlying type
enum class damage_type : uint8_t
{
    physical = 0,
    magic = 1,
};
```

### Memory Management

```cpp
// Use smart pointers
std::unique_ptr<T>  // Sole ownership
std::shared_ptr<T>  // Shared ownership (use sparingly)

// Use containers instead of raw arrays
std::vector<T>              // Dynamic array
std::array<T, N>            // Fixed-size array
std::unordered_map<K, V>    // Hash map

// RAII for all resources - no manual new/delete
```

---

## Memory Safety Patterns

### Container Removal Order

When an object is owned by one container (e.g., `unique_ptr` in a map) and referenced by another (e.g., raw pointers in a vector), **always remove references before deleting the owner**:

```cpp
// WRONG - use-after-free!
dialogs_.erase(type);  // Deletes the object
dialog_order_.erase(   // Dereferences deleted pointer
    std::remove_if(..., [](dialog* d) { return d->type() == type; }), ...);

// CORRECT - remove reference first, then delete
if (auto it = dialogs_.find(type); it != dialogs_.end())
{
    dialog* ptr = it->second.get();  // Get pointer while object is alive
    dialog_order_.erase(
        std::remove(dialog_order_.begin(), dialog_order_.end(), ptr),
        dialog_order_.end()
    );
    dialogs_.erase(it);  // Now safe to delete
}
```

### Thread Safety

Never modify shared containers from background threads. The ixwebsocket library runs callbacks on background threads. Use one of these patterns:

1. **Polling**: Don't set callbacks; poll for messages on the main thread:
   ```cpp
   // In update() on main thread:
   while (auto msg = connection.receive())
   {
       handle_message(*msg);  // Safe to modify state here
   }
   ```

2. **Deferred actions**: Queue events for main thread processing:
   ```cpp
   // Background thread - just set a flag
   {
       std::lock_guard<std::mutex> lock(mutex_);
       pending_event_ = event;
   }
   has_pending_event_.store(true);

   // Main thread - process the queued event
   if (has_pending_event_.exchange(false))
   {
       Event event;
       {
           std::lock_guard<std::mutex> lock(mutex_);
           event = std::move(pending_event_);
       }
       process_event(event);  // Safe to modify state
   }
   ```

---

## Error Handling

```cpp
// Prefer std::expected or Result<T,E> for recoverable errors
std::expected<result, error_code> try_operation();

// Use exceptions only for truly exceptional/unrecoverable situations
// No exceptions in hot paths

// Use assertions for programmer errors (debug only)
assert(ptr != nullptr && "Pointer must not be null");

// Log errors with structured logging (spdlog)
spdlog::error("Failed to load player {}: {}", player_id, error.message());
```

---

## File Organization

```cpp
// Header file structure
#pragma once

#include <standard_library>     // Standard library first
#include <third_party/lib.h>    // Third-party second
#include "project/header.h"     // Project headers last

namespace hb::subsystem
{

// Forward declarations
class other_class;

// Type aliases
using player_id = uint32_t;

// Class declaration
class my_class
{
public:
    my_class();
    ~my_class();

    void public_method();

private:
    int32_t member_;  // Trailing underscore for private members (optional)
};

} // namespace hb::subsystem
```

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
| [src/database/schema.sql](src/database/schema.sql) | Full database schema (for fresh installs) |
| [tools/migrate/](tools/migrate/) | Database migration tool and migration files |

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
├── database/                   # PostgreSQL connection, schema.sql
├── player/                     # Player state, queries
├── world/                      # Maps, spatial queries
├── entity/                     # Entity system
├── combat/                     # Damage calculation
├── magic/                      # Spell system
├── npc/                        # NPC management
├── item/                       # Item system
└── ...                         # Other game systems
tools/
└── migrate/                    # Database migration tool
    ├── migrate.ts              # CLI: migrate, rollback, status, create
    └── migrations/             # SQL migration files (tracked in git)
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

### Key Implementation Files

#### Core Entry Points

| File | Purpose |
|------|---------|
| `src/application.cpp` | Server startup, subsystem wiring |
| `src/main.cpp` | Entry point |

#### Network & Protocol

| File | Purpose |
|------|---------|
| `src/network/websocket_server.*` | Boost.Beast WebSocket |
| `src/network/json_protocol.*` | JSON message types |
| `src/bridge/handlers/auth_handlers.cpp` | Auth handlers |
| `src/bridge/handlers/game_handlers.cpp` | Game handlers |

#### Player & World

| File | Purpose |
|------|---------|
| `src/player/player_system.*` | Player state, queries |
| `src/world/world_subsystem.*` | Maps, zones |
| `src/world/position.h` | Position types |

#### Auth & Database

| File | Purpose |
|------|---------|
| `src/auth/auth_system.*` | Account/session/character management |
| `src/auth/account.h` | Account, character data structures |
| `src/database/database_system.*` | PostgreSQL connection pool |

#### Game Systems

| File | Purpose |
|------|---------|
| `src/combat/combat_system.*` | Damage calculation, hit resolution, kill rewards |
| `src/magic/magic_system.*` | Spell casting, cooldowns, buffs/debuffs |
| `src/npc/npc_system.*` | NPC management, AI, spawning, bosses |
| `src/item/item_system.*` | Item instances, stacking, durability |
| `src/skill/skill_system.*` | Weapon skills, training, mastery |
| `src/quest/quest_system.*` | Quest journal, objectives, rewards |
| `src/social/social_system.*` | Guilds, parties, chat |
| `src/war/war_system.*` | War scheduling, territory |
| `src/admin/admin_system.*` | GM commands, muting, audit logging |

#### Configuration

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

## Legacy Exclusions

### Equilibrium System

The legacy Helbreath codebase contains an "Equilibrium" system (also referred to as "EQ", "balance mode", or similar). **Do not implement or port equilibrium-related code by default.** When you encounter anything related to equilibrium during research or implementation — whether in legacy code references, oracle search results, or design docs — **always ask the user** whether it should be included before proceeding. In most cases the answer will be no, but occasionally it may be desired.

---

## Server-Specific Notes

When working on the server:

1. **Ask questions frequently** - Use AskUserQuestion liberally to clarify requirements, validate assumptions, and confirm implementation approaches before writing code. When in doubt, ask.
2. **Check PROGRESS.md first** - Know what's implemented before starting. Don't rebuild what already exists.
3. **Update PROGRESS.md when done** - After completing a feature or significant component, update `docs/PROGRESS.md`: mark the relevant items as done, update the phase status, and add a dated entry under `## Recent Changes` using the format:
   ```
   ### YYYY-MM-DD: Summary
   - Individual items
   - Individual items
   ...
   ```
   Keep the "Immediate Next Steps" list current. Every major feature MUST be checked off here.
4. **Document all protocol changes** - Any change to client/server WebSocket message structures MUST be documented in both `docs/JSON_PROTOCOL.md` and the `docs/protocol/` directory so the client stays in sync. This is non-negotiable — undocumented protocol changes break the client.
5. **Message routing requires two switches** - Every new client-to-server message type must be added in **two** places: the handler's `handle_message()` switch (e.g., `game_handlers.cpp`) **and** the top-level routing switch in `application.cpp`. Missing the `application.cpp` entry causes "Unknown message type" at runtime — the message is silently dropped.
6. **Protocol compatibility** - Legacy binary protocol must match original
7. **Database transactions** - Use connection pool properly
8. **Thread safety** - WebSocket callbacks run on separate threads
9. **Create migrations for DB changes** - Any change to the PostgreSQL schema (new tables, columns, indexes, constraints, functions) must include a migration file. Run `cd tools/migrate && npx tsx migrate.ts create <description>` to scaffold one, then fill in the `-- up` and `-- down` SQL. Also update `src/database/schema.sql` to match so fresh installs get the current schema. Never modify existing migration files — including the baseline schema. Always create a new migration instead.

## Notes for AI Assistants

When working on this codebase:

1. **Always use C++20 features** - Prefer modern alternatives to legacy patterns
2. **Follow the coding style strictly** - snake_case everywhere, Allman braces, no Hungarian notation
3. **Check existing code first** - Patterns may already exist
4. **Use RAII** - Never use raw `new`/`delete`
5. **Handle errors gracefully** - Use `std::expected` or result types
6. **Keep functions small** - Each function should do one thing well
7. **Log important operations** - But avoid excessive logging in hot paths
8. **Test edge cases** - Especially around combat, items, and networking
9. **Preserve game behavior** - Modernized code should behave identically to original
10. **Update documentation** - Keep CLAUDE.md and docs/ in sync with changes
11. **Translate Korean comments** - When encountered, translate to English inline
12. **Update PROGRESS.md** - After completing a feature, update `docs/PROGRESS.md`
