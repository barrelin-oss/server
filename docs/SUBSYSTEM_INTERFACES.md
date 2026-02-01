# Subsystem Interface Specifications

This document contains detailed interface specifications for the planned subsystems. These serve as design references for implementation.

> **Note:** These interfaces are aspirational designs. Actual implementations may vary based on practical needs.

---

## 1. Network Subsystem

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

---

## 2. Session Subsystem

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

---

## 3. World Subsystem

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

---

## 4. Entity Subsystem

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

---

## 5. Player Subsystem

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

---

## 6. NPC Subsystem

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

---

## 7. Combat Subsystem

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

---

## 8. Magic Subsystem

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

---

## 9. Skill Subsystem

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

---

## 10. Item Subsystem

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

---

## 11. Inventory Subsystem

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

---

## 12. Quest Subsystem

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

---

## 13. Guild Subsystem

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

---

## 14. Party Subsystem

**Purpose:** Party creation, member management, loot distribution

**Files:** `src/party/`
- `PartySystem.h/cpp`
- `Party.h/cpp`
- `LootDistribution.h/cpp`

**Replace:** Party handling in CGame

---

## 15. Trade Subsystem

**Purpose:** Player trading, NPC shops, economy

**Files:** `src/trade/`
- `TradeSystem.h/cpp`
- `Trade.h/cpp`
- `Shop.h/cpp`
- `Economy.h/cpp`

**Replace:** Trade/exchange handling in CGame

---

## 16. Crafting Subsystem

**Purpose:** Crafting recipes, material requirements, item creation

**Files:** `src/crafting/`
- `CraftingSystem.h/cpp`
- `Recipe.h/cpp`
- `RecipeRegistry.h/cpp`

**Replace:** `CBuildItem`, `CPortion`, crafting in CGame

---

## 17. Gathering Subsystem

**Purpose:** Fishing, mining, farming resource nodes

**Files:** `src/gathering/`
- `GatheringSystem.h/cpp`
- `FishingSystem.h/cpp`
- `MiningSystem.h/cpp`
- `FarmingSystem.h/cpp`
- `ResourceNode.h/cpp`

**Replace:** `CFish`, `CMineral`, gathering in CGame

---

## 18. War Subsystem

**Purpose:** Crusade, Heldenian, Apocalypse war systems

**Files:** `src/war/`
- `WarSystem.h/cpp`
- `CrusadeWar.h/cpp`
- `HeldenianWar.h/cpp`
- `ApocalypseEvent.h/cpp`
- `Territory.h/cpp`

**Replace:** War handling in CGame, `CrusadeCore`

---

## 19. Chat Subsystem

**Purpose:** Chat messaging, channels, whispers, filtering

**Files:** `src/chat/`
- `ChatSystem.h/cpp`
- `Channel.h/cpp`
- `ChatFilter.h/cpp`

**Replace:** Chat handling scattered in CGame

---

## 20. Admin Subsystem

**Purpose:** GM commands, moderation tools, admin levels

**Files:** `src/admin/`
- `AdminSystem.h/cpp`
- `Command.h/cpp`
- `CommandRegistry.h/cpp`

**Replace:** `AdminOrder_*` functions in CGame

---

## 21. Persistence Subsystem

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

---

## 22. Scheduler Subsystem

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

## Key Constants

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
