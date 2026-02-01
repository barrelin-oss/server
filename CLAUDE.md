# Helbreath Server Modernization Guide

## Project Overview

**Project:** Helbreath Game Server
**Current Version:** 3.0.0 (Modernization in progress)
**Target Standard:** C++20
**Platform:** Cross-platform (Windows, Linux)

This is a classic late-1990s/early-2000s MMO game server being modernized to C++20. The original codebase is monolithic (~2MB single file for game logic) with manual memory management, fixed-size arrays, and Windows-specific APIs.

---

## Building the Project

### Prerequisites

- **CMake** 3.20 or later
- **Visual Studio 2022** (or another C++20-capable compiler)
- **vcpkg** (for dependency management)

### Quick Build (Windows)

```bash
# Configure (first time or after CMakeLists.txt changes)
cmake -B build -S . -DCMAKE_TOOLCHAIN_FILE=C:/vcpkg/scripts/buildsystems/vcpkg.cmake

# Build (Debug)
cmake --build build --config Debug

# Build (Release)
cmake --build build --config Release
```

### Using MSBuild Directly

After configuring with CMake:

```bash
# Build from the build directory
msbuild build/hgserver.sln /p:Configuration=Debug /m

# Or use the /build skill which does this automatically
```

### Output Location

All binaries, DLLs, and config files are output to the `bin/` directory:

```
bin/
├── hgserver.exe          # Main server executable
├── hgserver_tests.exe    # Test executable
├── *.dll                 # Required runtime DLLs
├── *.cfg                 # Configuration files
├── GameConfigs/          # Game configuration data
├── GameData/             # Game data files
└── mapdata/              # Map files
```

### Visual Studio Debugging

The VS solution is configured to use `bin/` as the working directory for debugging. Press F5 to debug directly - DLLs and assets are already in place.

### Running the Server

```bash
cd bin
./hgserver.exe
```

### Running Tests

```bash
cd bin
./hgserver_tests.exe
```

### Dependencies (via vcpkg)

The following packages are installed automatically via vcpkg.json:
- **spdlog** - Structured logging
- **nlohmann-json** - JSON parsing
- **libpqxx** - PostgreSQL client
- **libsodium** - Cryptography (password hashing)
- **ixwebsocket** - WebSocket server
- **openssl** - TLS/SSL
- **zlib** - Compression
- **gtest** - Unit testing

---

## Project Documentation

**Always check these documents before starting work:**

| Document | Purpose |
|----------|---------|
| [docs/PROGRESS.md](docs/PROGRESS.md) | **Implementation progress tracker** - what's done, in progress, and planned |
| [docs/PACKET_PROTOCOL.md](docs/PACKET_PROTOCOL.md) | **Complete packet reference** - Legacy binary and modern JSON formats |
| [docs/JSON_PROTOCOL.md](docs/JSON_PROTOCOL.md) | WebSocket JSON protocol specification (auth, characters, game entry) |
| [docs/GAME_MESSAGES.md](docs/GAME_MESSAGES.md) | In-game message formats (movement, combat, actions) |

### Current Focus

See `docs/PROGRESS.md` for detailed status. Immediate priorities:

1. **Combat System** - Wire attack handlers to actual damage calculation
2. **NPC System** - Basic NPCs with AI and combat
3. **Item/Loot System** - Ground items, pickup, drops
4. **Inventory System** - Full item management

---

## Coding Style

**IMPORTANT: All new code must follow stdlib-style snake_case naming:**

- **Everything snake_case**: Types, variables, functions, methods, files (like std::string, std::vector)
- **No Hungarian notation**: No `bVar`, `iCount`, `szString` prefixes
- **No member prefixes**: No `m_` prefix for class members
- **No C-prefix**: No `CGame`, `CClient` - just `game`, `client`
- **Constants**: `snake_case` with constexpr (not SCREAMING_SNAKE)
- **Files**: `snake_case.h`, `snake_case.cpp`
- **Namespace**: `hb` for all new code

```cpp
// GOOD (stdlib-like style)
class player_state {
    int health;
    std::string name;
    bool is_active;
};

struct client_id { uint16_t value; };
struct position { int16_t x; int16_t y; };

void calculate_damage(int raw_damage, int armor);
auto get_player(client_id id) -> std::optional<player_state&>;

// BAD (legacy style to avoid)
class CPlayerState {
    int m_iHealth;      // No m_ prefix, no Hungarian notation
    char* m_szName;     // No sz prefix
    BOOL m_bIsActive;   // No BOOL, no m_b prefix
};
```

---

## Modernization Goals

1. **C++20 Standard Compliance** - Use modern language features throughout
2. **Subsystem Architecture** - Break monolithic CGame into focused, testable subsystems
3. **Memory Safety** - Replace raw pointers with smart pointers and RAII
4. **Type Safety** - Use strong typing, enums, and concepts
5. **Testability** - Design for unit testing with dependency injection
6. **Cross-Platform** - Console application, no Windows GUI dependencies
7. **Performance** - Maintain or improve current performance characteristics

---

## C++20 Guidelines

### Preferred Language Features

```cpp
// Use concepts for generic constraints
template<typename T>
concept entity = requires(T t) {
    { t.get_id() } -> std::convertible_to<uint32_t>;
    { t.get_position() } -> std::same_as<position>;
};

// Use std::span instead of raw pointer + size
void process_items(std::span<const item> items);

// Use std::optional for nullable returns
auto find_player(uint32_t id) -> std::optional<player_state&>;

// Use result<T,E> pattern for error handling (see src/core/result.h)
auto create_item(item_template tmpl) -> result<item, std::string>;

// Use std::string_view for non-owning string parameters
void send_message(client_id id, std::string_view message);

// Use designated initializers
auto config = server_config{
    .port = 2848,
    .max_clients = 2000,
    .tick_rate = 100
};

// Use structured bindings
auto [success, player] = player_manager.authenticate(credentials);

// Use ranges for collection operations
auto active_clients = clients
    | std::views::filter([](auto& c) { return c.is_active(); })
    | std::views::transform([](auto& c) { return c.get_id(); });

// Use constexpr for compile-time computation
inline constexpr auto max_inventory_slots = 50;
constexpr auto calculate_damage_modifier(int level) -> int { /* ... */ }

// Use enum class with underlying type
enum class damage_type : uint8_t {
    physical = 0,
    magic = 1,
    fire = 2,
    ice = 3,
    lightning = 4
};

// Use spdlog for logging (see src/core/logger.h)
LOG_INFO(combat, "Player {} dealt {} damage", player_name, damage);
```

### Memory Management

```cpp
// Use smart pointers
std::unique_ptr<T>  // Sole ownership
std::shared_ptr<T>  // Shared ownership (use sparingly)
std::weak_ptr<T>    // Non-owning observer

// Use containers instead of raw arrays
std::vector<T>              // Dynamic array
std::array<T, N>            // Fixed-size array
std::unordered_map<K, V>    // Hash map
std::flat_map<K, V>         // Sorted contiguous map (C++23, or use boost)

// Object pools for frequently allocated types
ObjectPool<NPC> npcPool{5000};
ObjectPool<DynamicObject> objectPool{60000};
```

### Error Handling

```cpp
// No exceptions in hot paths - use Result types
template<typename T, typename E = std::error_code>
class Result {
public:
    bool isOk() const;
    T& value();
    E& error();
};

// Use std::expected when available (C++23)
std::expected<Player, PlayerError> loadPlayer(uint32_t id);

// Exceptions only for truly exceptional/unrecoverable situations
// Log errors with structured logging
```

### Threading Model

```cpp
// Use std::jthread for automatic joining
std::jthread networkThread([this](std::stop_token st) {
    while (!st.stop_requested()) {
        processNetworkEvents();
    }
});

// Use std::atomic for shared state
std::atomic<uint32_t> activeConnections{0};

// Use std::mutex with std::scoped_lock
std::scoped_lock lock{mutex1, mutex2};

// Prefer message passing over shared state
```

### Memory Safety Patterns

#### Container Removal Order

When an object is owned by one container (e.g., `unique_ptr` in a map) and referenced by another (e.g., raw pointers in a vector), **always remove references before deleting the owner**:

```cpp
// WRONG - use-after-free!
dialogs_.erase(type);  // Deletes the object
dialog_order_.erase(   // Dereferences deleted pointer
    std::remove_if(..., [](dialog* d) { return d->type() == type; }), ...);

// CORRECT - remove reference first, then delete
if (auto it = dialogs_.find(type); it != dialogs_.end()) {
    dialog* ptr = it->second.get();  // Get pointer while object is alive
    dialog_order_.erase(
        std::remove(dialog_order_.begin(), dialog_order_.end(), ptr),
        dialog_order_.end()
    );
    dialogs_.erase(it);  // Now safe to delete
}
```

#### Thread Safety

Never modify UI containers (`dialog_order_`, etc.) from background threads. The ixwebsocket library runs callbacks on a background thread. Use one of these patterns:

1. **Polling**: Don't set callbacks; poll for messages on the main thread:
   ```cpp
   // In update() on main thread:
   while (auto msg = ws_connection_.receive()) {
       handle_ws_message(*msg);  // Safe to modify UI here
   }
   ```

2. **Deferred actions**: Queue events for main thread processing:
   ```cpp
   // Background thread - just set a flag
   {
       std::lock_guard<std::mutex> lock(mutex_);
       pending_error_ = reason;
   }
   has_pending_error_.store(true);

   // Main thread - process the queued event
   if (has_pending_error_.exchange(false)) {
       std::string error;
       {
           std::lock_guard<std::mutex> lock(mutex_);
           error = std::move(pending_error_);
       }
       show_error(error);  // Safe to modify UI
   }
   ```

---

## Subsystem Architecture

The monolithic `CGame` class must be decomposed into the following focused subsystems:

### Core Infrastructure

```
┌─────────────────────────────────────────────────────────────┐
│                     GameServer                               │
│  (Orchestrates subsystems, manages lifecycle)               │
└──────────────────────────┬──────────────────────────────────┘
                           │
    ┌──────────────────────┼──────────────────────────────────┐
    │                      │                                  │
┌───▼────┐          ┌──────▼──────┐                    ┌──────▼──────┐
│ Config │          │  EventBus   │                    │   Logger    │
│ System │          │  (pub/sub)  │                    │             │
└────────┘          └─────────────┘                    └─────────────┘
```

### Subsystem Overview

| Subsystem | Responsibility | Key Types |
|-----------|---------------|-----------|
| **Network** | Socket I/O, protocol, message routing | `NetworkManager`, `Connection`, `Protocol` |
| **Session** | Client sessions, authentication | `SessionManager`, `Session`, `Credentials` |
| **World** | Maps, tiles, spatial queries | `WorldManager`, `Map`, `Tile`, `Zone` |
| **Entity** | Entity lifecycle, components | `EntityManager`, `Entity`, `Component` |
| **Player** | Player state, stats, progression | `PlayerSystem`, `PlayerState`, `Stats` |
| **NPC** | NPC behavior, AI, spawning | `NPCSystem`, `NPCBehavior`, `Spawner` |
| **Combat** | Damage calculation, hit resolution | `CombatSystem`, `DamageEvent`, `HitResult` |
| **Magic** | Spells, effects, mana | `MagicSystem`, `Spell`, `SpellEffect` |
| **Skill** | Skills, training, mastery | `SkillSystem`, `Skill`, `SkillProgress` |
| **Item** | Items, equipment, durability | `ItemSystem`, `Item`, `Equipment` |
| **Inventory** | Inventory management, stacking | `InventorySystem`, `Inventory`, `Slot` |
| **Quest** | Quests, objectives, rewards | `QuestSystem`, `Quest`, `Objective` |
| **Guild** | Guilds, ranks, permissions | `GuildSystem`, `Guild`, `GuildMember` |
| **Party** | Parties, grouping, loot | `PartySystem`, `Party`, `PartyMember` |
| **Trade** | Trading, shops, economy | `TradeSystem`, `Trade`, `Shop` |
| **Crafting** | Recipes, materials, creation | `CraftingSystem`, `Recipe`, `Material` |
| **Gathering** | Fishing, mining, farming | `GatheringSystem`, `Resource`, `Node` |
| **War** | Crusade, Heldenian, territory | `WarSystem`, `War`, `Territory` |
| **Chat** | Messaging, channels, filtering | `ChatSystem`, `Message`, `Channel` |
| **Admin** | GM commands, moderation | `AdminSystem`, `Command`, `AdminLevel` |
| **Persistence** | Save/load, database | `PersistenceSystem`, `Repository<T>` |
| **Scheduler** | Timed events, game clock | `Scheduler`, `ScheduledEvent`, `GameTime` |

### Subsystem Communication

Subsystems communicate through:

1. **EventBus** - Decoupled publish/subscribe for game events
2. **Direct Calls** - For synchronous, performance-critical operations
3. **Message Queues** - For async operations and cross-thread communication

```cpp
// Event-based communication
eventBus.publish(PlayerDamagedEvent{playerId, damage, source});

// Direct dependency injection
class CombatSystem {
public:
    CombatSystem(PlayerSystem& players, NPCSystem& npcs, ItemSystem& items);
};
```

---

## Detailed Subsystem Specifications

### 1. Network Subsystem

**Purpose:** Handle all network I/O, protocol encoding/decoding, connection management

**Files:** `src/network/`
- `NetworkManager.h/cpp` - Main network orchestrator
- `Connection.h/cpp` - Individual connection state
- `Protocol.h/cpp` - Message serialization/deserialization
- `MessageHandler.h` - Message handler interface
- `Messages.h` - Message type definitions

**Key Interfaces:**
```cpp
class INetworkManager {
public:
    virtual void start(uint16_t port) = 0;
    virtual void stop() = 0;
    virtual void send(ConnectionId id, const Message& msg) = 0;
    virtual void broadcast(const Message& msg) = 0;
    virtual void disconnect(ConnectionId id) = 0;
    virtual void registerHandler(MessageType type, IMessageHandler* handler) = 0;
};

class IMessageHandler {
public:
    virtual void handle(ConnectionId sender, const Message& msg) = 0;
};
```

**Replace:** `XSocket`, raw Winsock calls, message buffers

### 2. Session Subsystem

**Purpose:** Manage client sessions, authentication state, connection-to-player mapping

**Files:** `src/session/`
- `SessionManager.h/cpp`
- `Session.h/cpp`
- `Authenticator.h/cpp`

**Key Interfaces:**
```cpp
class ISessionManager {
public:
    virtual Session& createSession(ConnectionId connId) = 0;
    virtual void destroySession(SessionId id) = 0;
    virtual std::optional<Session&> getSession(SessionId id) = 0;
    virtual std::optional<Session&> getSessionByConnection(ConnectionId connId) = 0;
    virtual std::optional<Session&> getSessionByPlayer(PlayerId playerId) = 0;
};

enum class SessionState : uint8_t {
    Connected,
    Authenticating,
    CharacterSelect,
    InGame,
    Disconnecting
};
```

**Replace:** Client state management scattered in CGame

### 3. World Subsystem

**Purpose:** Manage game world structure, maps, tiles, spatial queries

**Files:** `src/world/`
- `WorldManager.h/cpp`
- `Map.h/cpp`
- `Tile.h/cpp`
- `Zone.h/cpp`
- `SpatialIndex.h/cpp`

**Key Interfaces:**
```cpp
class IWorldManager {
public:
    virtual Map* getMap(MapId id) = 0;
    virtual std::vector<EntityId> getEntitiesInRange(Position pos, float radius) = 0;
    virtual std::vector<EntityId> getEntitiesInRect(Rect area) = 0;
    virtual bool isWalkable(MapId map, Position pos) = 0;
    virtual std::optional<Position> findPath(MapId map, Position from, Position to) = 0;
};

struct Position {
    int16_t x;
    int16_t y;

    auto operator<=>(const Position&) const = default;
};
```

**Replace:** `CMap`, `CTile`, map management in CGame

### 4. Entity Subsystem

**Purpose:** Core entity-component system for all game objects

**Files:** `src/entity/`
- `EntityManager.h/cpp`
- `Entity.h/cpp`
- `Component.h`
- `Components/*.h` - Specific component types

**Key Interfaces:**
```cpp
class EntityManager {
public:
    EntityId create();
    void destroy(EntityId id);
    bool exists(EntityId id) const;

    template<typename T>
    T& addComponent(EntityId id);

    template<typename T>
    T* getComponent(EntityId id);

    template<typename T>
    bool hasComponent(EntityId id) const;

    template<typename... Ts>
    auto view() -> EntityView<Ts...>;
};

// Example components
struct TransformComponent {
    MapId mapId;
    Position position;
    Direction facing;
};

struct HealthComponent {
    int32_t current;
    int32_t maximum;
};

struct CombatComponent {
    int32_t attackPower;
    int32_t defense;
    int32_t hitRate;
    int32_t dodgeRate;
};
```

### 5. Player Subsystem

**Purpose:** Player-specific logic, stats, progression, status effects

**Files:** `src/player/`
- `PlayerSystem.h/cpp`
- `PlayerState.h/cpp`
- `Stats.h/cpp`
- `StatusEffect.h/cpp`

**Key Interfaces:**
```cpp
class IPlayerSystem {
public:
    virtual PlayerId createPlayer(SessionId session, const CharacterData& data) = 0;
    virtual void removePlayer(PlayerId id) = 0;
    virtual PlayerState* getPlayer(PlayerId id) = 0;

    virtual void addExperience(PlayerId id, int64_t amount) = 0;
    virtual void modifyStats(PlayerId id, const StatModifier& mod) = 0;
    virtual void applyStatusEffect(PlayerId id, StatusEffect effect) = 0;
};

struct PlayerStats {
    int16_t strength;
    int16_t dexterity;
    int16_t vitality;
    int16_t intelligence;
    int16_t magic;
    int16_t charisma;
    int16_t luck;
};
```

**Replace:** `CClient` player-specific fields, stat calculations in CGame

### 6. NPC Subsystem

**Purpose:** NPC behavior, AI state machines, spawning, pathing

**Files:** `src/npc/`
- `NPCSystem.h/cpp`
- `NPCBehavior.h/cpp`
- `AIStateMachine.h/cpp`
- `Spawner.h/cpp`

**Key Interfaces:**
```cpp
class INPCSystem {
public:
    virtual NPCId spawn(NPCTemplateId templateId, MapId map, Position pos) = 0;
    virtual void despawn(NPCId id) = 0;
    virtual void update(float deltaTime) = 0;
    virtual void setTarget(NPCId npc, EntityId target) = 0;
};

enum class AIState : uint8_t {
    Idle,
    Patrol,
    Chase,
    Attack,
    Flee,
    Return,
    Dead
};
```

**Replace:** `CNpc`, NPC processing loops in CGame

### 7. Combat Subsystem

**Purpose:** Damage calculation, hit/miss resolution, combat events

**Files:** `src/combat/`
- `CombatSystem.h/cpp`
- `DamageCalculator.h/cpp`
- `HitResolver.h/cpp`
- `CombatEvents.h`

**Key Interfaces:**
```cpp
class ICombatSystem {
public:
    virtual HitResult resolveAttack(EntityId attacker, EntityId defender) = 0;
    virtual DamageResult calculateDamage(const AttackContext& ctx) = 0;
    virtual void applyDamage(EntityId target, const DamageResult& damage) = 0;
};

struct HitResult {
    bool hit;
    bool critical;
    bool blocked;
    DamageType damageType;
    int32_t rawDamage;
    int32_t finalDamage;
    int32_t absorbed;
};

// Events published
struct EntityDamagedEvent {
    EntityId target;
    EntityId source;
    DamageResult damage;
};

struct EntityKilledEvent {
    EntityId killed;
    EntityId killer;
};
```

**Replace:** Damage calculation functions scattered in CGame

### 8. Magic Subsystem

**Purpose:** Spell definitions, casting, effects, mana management

**Files:** `src/magic/`
- `MagicSystem.h/cpp`
- `Spell.h/cpp`
- `SpellEffect.h/cpp`
- `SpellRegistry.h/cpp`

**Key Interfaces:**
```cpp
class IMagicSystem {
public:
    virtual CastResult castSpell(EntityId caster, SpellId spell, const CastTarget& target) = 0;
    virtual bool canCast(EntityId caster, SpellId spell) const = 0;
    virtual int32_t getManaCost(EntityId caster, SpellId spell) const = 0;
    virtual float getCooldown(EntityId caster, SpellId spell) const = 0;
};

enum class SpellTargetType : uint8_t {
    Self,
    SingleTarget,
    AreaOfEffect,
    Cone,
    Line
};
```

**Replace:** `CMagic`, magic handling in CGame

### 9. Skill Subsystem

**Purpose:** Skill definitions, training, mastery progression

**Files:** `src/skill/`
- `SkillSystem.h/cpp`
- `Skill.h/cpp`
- `SkillProgress.h/cpp`

**Key Interfaces:**
```cpp
class ISkillSystem {
public:
    virtual int32_t getSkillLevel(PlayerId player, SkillId skill) const = 0;
    virtual void addSkillExperience(PlayerId player, SkillId skill, int32_t amount) = 0;
    virtual bool canUseSkill(PlayerId player, SkillId skill) const = 0;
    virtual SkillResult useSkill(PlayerId player, SkillId skill, const SkillTarget& target) = 0;
};
```

**Replace:** `CSkill`, skill handling in CGame

### 10. Item Subsystem

**Purpose:** Item definitions, properties, effects, durability

**Files:** `src/item/`
- `ItemSystem.h/cpp`
- `Item.h/cpp`
- `ItemTemplate.h/cpp`
- `ItemRegistry.h/cpp`

**Key Interfaces:**
```cpp
class IItemSystem {
public:
    virtual ItemId createItem(ItemTemplateId templateId) = 0;
    virtual void destroyItem(ItemId id) = 0;
    virtual Item* getItem(ItemId id) = 0;
    virtual void modifyDurability(ItemId id, int32_t delta) = 0;
    virtual ItemStats calculateStats(ItemId id) const = 0;
};

struct Item {
    ItemId id;
    ItemTemplateId templateId;
    int16_t quantity;
    int16_t durability;
    int16_t maxDurability;
    std::array<int16_t, 6> attributes;
    // ...
};
```

**Replace:** `CItem`, item creation/management in CGame

### 11. Inventory Subsystem

**Purpose:** Inventory management, equipment slots, item stacking

**Files:** `src/inventory/`
- `InventorySystem.h/cpp`
- `Inventory.h/cpp`
- `EquipmentSlot.h`

**Key Interfaces:**
```cpp
class IInventorySystem {
public:
    virtual bool addItem(PlayerId player, ItemId item) = 0;
    virtual bool removeItem(PlayerId player, ItemId item) = 0;
    virtual bool moveItem(PlayerId player, SlotId from, SlotId to) = 0;
    virtual bool equipItem(PlayerId player, ItemId item, EquipSlot slot) = 0;
    virtual bool unequipItem(PlayerId player, EquipSlot slot) = 0;
    virtual std::span<const ItemId> getInventory(PlayerId player) const = 0;
};

enum class EquipSlot : uint8_t {
    Head = 0,
    Body = 1,
    Arms = 2,
    Pants = 3,
    Boots = 4,
    Weapon = 5,
    Shield = 6,
    Ring1 = 7,
    Ring2 = 8,
    Amulet = 9,
    Cape = 10,
    // ... etc
};
```

**Replace:** Inventory handling in CClient and CGame

### 12. Quest Subsystem

**Purpose:** Quest definitions, progress tracking, objectives, rewards

**Files:** `src/quest/`
- `QuestSystem.h/cpp`
- `Quest.h/cpp`
- `Objective.h/cpp`
- `QuestRegistry.h/cpp`

**Key Interfaces:**
```cpp
class IQuestSystem {
public:
    virtual bool acceptQuest(PlayerId player, QuestId quest) = 0;
    virtual bool abandonQuest(PlayerId player, QuestId quest) = 0;
    virtual void updateProgress(PlayerId player, const QuestEvent& event) = 0;
    virtual bool completeQuest(PlayerId player, QuestId quest) = 0;
    virtual std::vector<QuestId> getActiveQuests(PlayerId player) const = 0;
};

enum class ObjectiveType : uint8_t {
    Kill,
    Collect,
    Deliver,
    Escort,
    Visit,
    Craft,
    // ...
};
```

**Replace:** `CQuest`, quest handling in CGame

### 13. Guild Subsystem

**Purpose:** Guild management, ranks, permissions, guild wars

**Files:** `src/guild/`
- `GuildSystem.h/cpp`
- `Guild.h/cpp`
- `GuildMember.h/cpp`
- `GuildRank.h/cpp`

**Key Interfaces:**
```cpp
class IGuildSystem {
public:
    virtual GuildId createGuild(PlayerId founder, std::string_view name) = 0;
    virtual void disbandGuild(GuildId id) = 0;
    virtual bool invitePlayer(GuildId guild, PlayerId inviter, PlayerId invitee) = 0;
    virtual bool kickMember(GuildId guild, PlayerId kicker, PlayerId target) = 0;
    virtual bool setRank(GuildId guild, PlayerId setter, PlayerId target, GuildRank rank) = 0;
    virtual Guild* getGuild(GuildId id) = 0;
    virtual std::optional<GuildId> getPlayerGuild(PlayerId player) const = 0;
};
```

**Replace:** `GuildsMan`, guild handling in CGame

### 14. Party Subsystem

**Purpose:** Party creation, member management, loot distribution

**Files:** `src/party/`
- `PartySystem.h/cpp`
- `Party.h/cpp`
- `LootDistribution.h/cpp`

**Replace:** Party handling in CGame

### 15. Trade Subsystem

**Purpose:** Player trading, NPC shops, economy

**Files:** `src/trade/`
- `TradeSystem.h/cpp`
- `Trade.h/cpp`
- `Shop.h/cpp`
- `Economy.h/cpp`

**Replace:** Trade/exchange handling in CGame

### 16. Crafting Subsystem

**Purpose:** Crafting recipes, material requirements, item creation

**Files:** `src/crafting/`
- `CraftingSystem.h/cpp`
- `Recipe.h/cpp`
- `RecipeRegistry.h/cpp`

**Replace:** `CBuildItem`, `CPortion`, crafting in CGame

### 17. Gathering Subsystem

**Purpose:** Fishing, mining, farming resource nodes

**Files:** `src/gathering/`
- `GatheringSystem.h/cpp`
- `FishingSystem.h/cpp`
- `MiningSystem.h/cpp`
- `FarmingSystem.h/cpp`
- `ResourceNode.h/cpp`

**Replace:** `CFish`, `CMineral`, gathering in CGame

### 18. War Subsystem

**Purpose:** Crusade, Heldenian, Apocalypse war systems

**Files:** `src/war/`
- `WarSystem.h/cpp`
- `CrusadeWar.h/cpp`
- `HeldenianWar.h/cpp`
- `ApocalypseEvent.h/cpp`
- `Territory.h/cpp`

**Replace:** War handling in CGame, `CrusadeCore`

### 19. Chat Subsystem

**Purpose:** Chat messaging, channels, whispers, filtering

**Files:** `src/chat/`
- `ChatSystem.h/cpp`
- `Channel.h/cpp`
- `ChatFilter.h/cpp`

**Replace:** Chat handling scattered in CGame

### 20. Admin Subsystem

**Purpose:** GM commands, moderation tools, admin levels

**Files:** `src/admin/`
- `AdminSystem.h/cpp`
- `Command.h/cpp`
- `CommandRegistry.h/cpp`

**Replace:** `AdminOrder_*` functions in CGame

### 21. Persistence Subsystem

**Purpose:** Save/load game data, database abstraction

**Files:** `src/persistence/`
- `PersistenceSystem.h/cpp`
- `Repository.h` - Generic repository interface
- `PlayerRepository.h/cpp`
- `GuildRepository.h/cpp`
- `Serialization.h/cpp`

**Key Interfaces:**
```cpp
template<typename T, typename Id>
class IRepository {
public:
    virtual std::optional<T> load(Id id) = 0;
    virtual bool save(const T& entity) = 0;
    virtual bool remove(Id id) = 0;
    virtual std::vector<T> loadAll() = 0;
};
```

**Replace:** Save/load functions in CGame, log server communication

### 22. Scheduler Subsystem

**Purpose:** Timed events, game clock, scheduled tasks

**Files:** `src/scheduler/`
- `Scheduler.h/cpp`
- `ScheduledTask.h/cpp`
- `GameClock.h/cpp`

**Key Interfaces:**
```cpp
class IScheduler {
public:
    virtual TaskId schedule(Duration delay, std::function<void()> task) = 0;
    virtual TaskId scheduleRepeating(Duration interval, std::function<void()> task) = 0;
    virtual void cancel(TaskId id) = 0;
    virtual void update(Duration deltaTime) = 0;
    virtual GameTime getGameTime() const = 0;
};
```

**Replace:** Timer handling, `DelayEvent`, scheduled events in CGame

---

## Directory Structure

```
src/
├── core/
│   ├── GameServer.h/cpp        # Main server orchestrator
│   ├── EventBus.h/cpp          # Pub/sub event system
│   ├── Result.h                # Result<T,E> type
│   ├── Types.h                 # Common type aliases
│   └── Constants.h             # Game constants
├── config/
│   ├── ConfigSystem.h/cpp      # Configuration loading
│   ├── ServerConfig.h          # Server configuration
│   └── GameConfig.h            # Game balance config
├── network/
│   └── ...
├── session/
│   └── ...
├── world/
│   └── ...
├── entity/
│   └── ...
├── player/
│   └── ...
├── npc/
│   └── ...
├── combat/
│   └── ...
├── magic/
│   └── ...
├── skill/
│   └── ...
├── item/
│   └── ...
├── inventory/
│   └── ...
├── quest/
│   └── ...
├── guild/
│   └── ...
├── party/
│   └── ...
├── trade/
│   └── ...
├── crafting/
│   └── ...
├── gathering/
│   └── ...
├── war/
│   └── ...
├── chat/
│   └── ...
├── admin/
│   └── ...
├── persistence/
│   └── ...
├── scheduler/
│   └── ...
├── util/
│   ├── Logger.h/cpp
│   ├── StringUtils.h/cpp
│   ├── MathUtils.h/cpp
│   └── Random.h/cpp
└── platform/
    ├── Platform.h              # Platform abstraction
    ├── windows/                # Windows-specific
    └── linux/                  # Future Linux support
```

---

## File Organization

```cpp
// Header file structure
#pragma once

#include <standard_library>     // Standard library first
#include <third_party/lib.h>   // Third-party second
#include "project/header.h"     // Project headers last

namespace hb::subsystem {

// Forward declarations
class other_class;

// Type aliases
using player_id = uint32_t;

// Class declaration
class my_class {
public:
    my_class();
    ~my_class();

    void public_method();

private:
    int32_t member;  // No m_ prefix
};

} // namespace hb::subsystem
```

### Documentation

```cpp
/// @brief Brief description of the function
/// @param param1 Description of first parameter
/// @param param2 Description of second parameter
/// @return Description of return value
auto create_item(item_template_id template_id, int32_t quantity) -> result<item, std::string>;
```

---

## Migration Strategy

### Phase 1: Infrastructure (Foundation)
1. Set up CMake build system
2. Create core types and utilities (`Result`, `Types.h`, `Logger`)
3. Implement EventBus
4. Create platform abstraction layer

### Phase 2: Network Layer
1. Implement new NetworkManager with async I/O
2. Create Protocol serialization
3. Build Session management
4. Maintain wire protocol compatibility

### Phase 3: Entity System
1. Implement EntityManager with component storage
2. Define core components
3. Create entity views for queries

### Phase 4: World System
1. Port Map and Tile classes
2. Implement spatial indexing
3. Create WorldManager

### Phase 5: Game Systems (iterative)
1. Combat System
2. Magic System
3. Skill System
4. Item/Inventory System
5. NPC System
6. Quest System
7. Guild System
8. Party System
9. Trade System
10. Crafting System
11. Gathering System
12. War System
13. Chat System
14. Admin System

### Phase 6: Persistence
1. Create repository interfaces
2. Implement save/load functionality
3. Port database communication

### Phase 7: Testing & Polish
1. Unit tests for all subsystems
2. Integration tests
3. Performance optimization
4. Documentation

---

## Build System (CMake)

The project uses CMake with vcpkg for dependency management. Key files:

| File | Purpose |
|------|---------|
| `CMakeLists.txt` | Main build configuration |
| `cmake/compiler_settings.cmake` | C++20 compiler flags |
| `cmake/dependencies.cmake` | vcpkg package discovery |
| `vcpkg.json` | Dependency manifest |

### Build Targets

| Target | Description |
|--------|-------------|
| `hgserver` | Main server executable |
| `hgserver_core` | Static library with all game logic |
| `hgserver_tests` | Google Test executable |

### Output Directories

All outputs go to `bin/` for easy deployment:
- Executables (Debug and Release)
- Runtime DLLs (copied from vcpkg)
- Configuration files

---

## Key Constants Migration

Replace DEF_* macros with constexpr:

```cpp
namespace hb::constants {

// Server limits
inline constexpr auto max_clients = 2000;
inline constexpr auto max_npcs = 5000;
inline constexpr auto max_maps = 100;
inline constexpr auto max_items = 6000;
inline constexpr auto max_guilds = 1000;

// Player limits
inline constexpr auto max_level = 180;
inline constexpr auto max_inventory_slots = 50;
inline constexpr auto max_bank_slots = 200;
inline constexpr auto max_equipment_slots = 15;

// Game balance
inline constexpr auto base_tick_rate_ms = 100;
inline constexpr auto save_interval_ms = 300000;  // 5 minutes

} // namespace hb::constants
```

---

## Testing Strategy

### Unit Tests
- Test each subsystem in isolation
- Mock dependencies using interfaces
- Focus on game logic correctness

### Integration Tests
- Test subsystem interactions
- Verify event flow
- Test persistence round-trips

### Performance Tests
- Benchmark critical paths (combat, movement)
- Profile memory usage
- Test with simulated load

---

## Notes for AI Assistants

When working on this codebase:

1. **Always check existing code** before implementing new features - patterns may already exist
2. **Maintain backwards compatibility** with save files and network protocol where possible
3. **Prefer composition over inheritance** for game systems
4. **Use strong types** - don't pass raw ints for IDs, wrap them in type-safe wrappers
5. **Log important operations** but avoid excessive logging in hot paths
6. **Test edge cases** - especially around combat and item manipulation
7. **Keep Korean comments** as historical reference but add English documentation
8. **Profile before optimizing** - don't assume bottlenecks
9. **Check docs/PROGRESS.md** before starting work to understand current state
10. **Update docs/PROGRESS.md** when completing features

---

## Key Implementation Files

Understanding these files is essential for working on the codebase:

### Core Entry Points
| File | Purpose |
|------|---------|
| `src/application.cpp` | Server startup, subsystem wiring, message routing |
| `src/main.cpp` | Entry point, runs application |

### Network & Protocol
| File | Purpose |
|------|---------|
| `src/network/websocket_server.h/cpp` | Boost.Beast WebSocket server |
| `src/network/json_protocol.h/cpp` | All JSON message types, parsing, building |
| `src/bridge/handlers/auth_handlers.cpp` | Login, character management handlers |
| `src/bridge/handlers/game_handlers.cpp` | Movement, combat, action handlers |

### Player & World
| File | Purpose |
|------|---------|
| `src/player/player_system.h/cpp` | Player state, movement, queries |
| `src/world/world_subsystem.h/cpp` | Maps, spatial queries |
| `src/world/position.h` | Position, direction, rect types |

### Auth & Database
| File | Purpose |
|------|---------|
| `src/auth/auth_system.h/cpp` | Account/session/character management |
| `src/auth/account.h` | Account, character data structures |
| `src/database/database_system.h/cpp` | PostgreSQL connection pool |

### Game Systems (need implementation)
| File | Purpose |
|------|---------|
| `src/combat/combat_system.h/cpp` | Damage calculation (stub) |
| `src/magic/magic_system.h/cpp` | Spell casting (stub) |
| `src/npc/npc_system.h/cpp` | NPC management (stub) |
| `src/item/item_system.h/cpp` | Item management (stub) |

### Configuration
| File | Purpose |
|------|---------|
| `src/config/server_config.h` | All configuration structs |
| `GServer.cfg.example` | Example configuration file |
