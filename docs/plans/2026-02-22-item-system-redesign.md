# Item System Redesign

**Date:** 2026-02-22
**Status:** Approved

## Motivation

The item system has accumulated too much iteration. The client-server protocol is inconsistent, handlers contain too much business logic, and there's no clean contract the client can implement against. This redesign defines a clean API from protocol to persistence.

## Core Principles

1. **One universal item shape** — same JSON structure everywhere an item appears (inventory, ground, trade, bank, shop)
2. **Server sends everything** — each item is fully self-contained with template + instance data. No client-side template lookup.
3. **Item + context in message** — the item shape has no location data. The wrapping message provides context (inventory position, ground coordinates, bank slot, etc.)
4. **Acknowledgment + state updates** — action messages are pure acknowledgments (`success: true`). All state changes flow through dedicated update channels (`inventory_item_add`, `inventory_item_removed`, etc.)
5. **Server `item_id` everywhere** — clients reference items by their server-assigned unique ID for all operations

## Section 1: Universal Item Shape

```json
{
  "item_id": 12345,
  "template_id": 100,
  "name": "Sword of Medusa",
  "type": "weapon",
  "equip_pos": "weapon",
  "weapon_type": "sword",

  "rarity": "rare",
  "count": 1,
  "weight": 800,
  "price": 15000,

  "damage_min": 18,
  "damage_max": 66,
  "defense": 0,
  "magic_defense": 0,

  "durability": 85,
  "max_durability": 100,

  "level_req": 30,
  "str_req": 50,
  "dex_req": 20,
  "int_req": 0,
  "mag_req": 0,

  "effects": [
    {"type": "str_bonus", "value": 3},
    {"type": "hit_bonus", "value": 5}
  ],

  "attribute": {
    "upgrade_level": 3,
    "main_enchant": {"type": "sharp", "value": 2},
    "sub_enchant": null,
    "custom_made": false
  },

  "special_ability": "paralyze_on_hit",

  "color": 0,
  "sprite_id": 14,
  "tradeable": true,
  "droppable": true,
  "two_handed": false
}
```

- `effects`: sparse array, only non-zero effects included
- `attribute`: present only if item has upgrades/enchantments, null/absent otherwise
- `special_ability`: present only on items with activatable abilities, absent otherwise
- `type` and `equip_pos` are string enums for client readability

## Section 2: Protocol Messages

### Login / Initial State

| Message | Direction | Payload |
|---------|-----------|---------|
| `inventory_data` | server → client | `items[]` (each: `item`, `pos_x`, `pos_y`, `z_order`), `equipment_slots{}` (slot → item_id), `gold`, `weight`, `max_weight` |

### State Update Channels

All state changes flow through these dedicated messages. Action results never contain state data.

| Message | Direction | Payload |
|---------|-----------|---------|
| `inventory_item_add` | server → client | `item`, `pos_x`, `pos_y`, `z_order` |
| `inventory_item_update` | server → client | `item`, `pos_x`, `pos_y`, `z_order` |
| `inventory_item_removed` | server → client | `item_id` |
| `inventory_item_delta` | server → client | `item_id`, optional `count`, optional `durability` |
| `inventory_gold_update` | server → client | `gold` |
| `inventory_weight_update` | server → client | `weight`, `max_weight` |
| `force_unequip` | server → client | `slot`, `reason` (broken / hammer_strip / armor_break) |
| `equipment_change` | server → broadcast | `entity_id`, `slot`, `item` or null |
| `ground_item_spawn` | server → broadcast | `item`, `map`, `x`, `y` |
| `ground_item_removed` | server → broadcast | `item_id`, `map`, `x`, `y` |
| `ability_activated` | server → broadcast | `entity_id`, `ability_type`, `duration_ms` |
| `ability_expired` | server → broadcast | `entity_id`, `ability_type` |
| `bank_slot_update` | server → client | `page`, `slot`, `item` |
| `bank_slot_cleared` | server → client | `page`, `slot` |

### Action Messages (acknowledgment only)

All return `success` (bool). On failure, no state update messages are sent.

| Message | Direction | Payload |
|---------|-----------|---------|
| `pickup_request` | client → server | `map`, `x`, `y` |
| `pickup_result` | server → client | `success` |
| `drop_request` | client → server | `item_id` |
| `drop_result` | server → client | `success` |
| `equip_request` | client → server | `item_id`, `slot` |
| `equip_result` | server → client | `success`, `slot` |
| `unequip_request` | client → server | `slot` |
| `unequip_result` | server → client | `success`, `slot` |
| `use_item_request` | client → server | `item_id` |
| `use_item_result` | server → client | `success` |
| `upgrade_request` | client → server | `item_id`, `stone_item_id` |
| `upgrade_result` | server → client | `success` |
| `shop_buy_request` | client → server | `template_id` |
| `shop_buy_result` | server → client | `success` |
| `shop_sell_request` | client → server | `item_id` |
| `shop_sell_result` | server → client | `success` |
| `shop_repair_request` | client → server | `item_id` |
| `shop_repair_result` | server → client | `success` |
| `bank_deposit_request` | client → server | `item_id`, optional `page`, optional `slot` |
| `bank_deposit_result` | server → client | `success` |
| `bank_withdraw_request` | client → server | `page`, `slot` |
| `bank_withdraw_result` | server → client | `success` |
| `bank_reposition_request` | client → server | `from_page`, `from_slot`, `to_page`, `to_slot` |
| `bank_reposition_result` | server → client | `success` |
| `activate_ability_request` | client → server | `item_id` |
| `activate_ability_failed` | server → client | `reason` (on_cooldown / not_equipped / no_ability) |
| `inventory_reposition` | client → server | `item_id`, `pos_x`, `pos_y` |

### Use Item Side Effects

`use_item_result` is acknowledgment only. Actual effects flow through existing stat messages:
- HP potion → `hp_update`
- MP potion → `mp_update`
- Recall scroll → teleport message
- Buff scroll → `buff_applied`

### Shop / Bank Open (lazy-loaded)

| Message | Direction | Payload |
|---------|-----------|---------|
| `shop_open` | server → client | `npc_name`, `shop_type`, `items[]` (each: `item`, `buy_price`) |
| `bank_open` | server → client | `pages[]` (each: `page_num`, `slots[]` of item or null), `total_pages` |

### Trading (3-phase: offer → lock → confirm)

| Message | Direction | Payload |
|---------|-----------|---------|
| `trade_request` | client → server | `target_entity_id` |
| `trade_invite` | server → client | `from_entity_id`, `from_name` |
| `trade_accept` | client → server | `from_entity_id` |
| `trade_decline` | client → server | `from_entity_id` |
| `trade_opened` | server → both | `partner_entity_id`, `partner_name` |
| `trade_add_item` | client → server | `item_id` |
| `trade_remove_item` | client → server | `item_id` |
| `trade_set_gold` | client → server | `amount` |
| `trade_update` | server → client | `side` (mine/theirs), `items[]`, `gold` |
| `trade_lock` | client → server | _(none)_ |
| `trade_lock_status` | server → both | `my_locked`, `their_locked` |
| `trade_confirm` | client → server | _(none)_ |
| `trade_complete` | server → both | `success` |
| `trade_cancel` | client → server | _(none)_ |
| `trade_canceled` | server → both | `reason` (player_canceled / out_of_range / disconnected) |

On `trade_complete`, state changes flow through `inventory_item_add`, `inventory_item_removed`, `inventory_gold_update`, `inventory_weight_update`.

### Party Loot

| Message | Direction | Payload |
|---------|-----------|---------|
| `set_loot_rule` | client → server | `rule` (disabled / greed / master) |
| `loot_rule_changed` | server → party | `rule`, `set_by` |
| `loot_available` | server → party | `loot_id`, `items[]`, `source_map`, `source_pos`, `rule`, `timeout_ms` |
| `loot_roll` | client → server | `loot_id`, `item_id` |
| `loot_roll_result` | server → party | `loot_id`, `item_id`, `entity_id`, `player_name`, `roll` |
| `loot_pass` | client → server | `loot_id`, `item_id` |
| `loot_assign` | client → server | `loot_id`, `item_id`, `target_entity_id` |
| `loot_awarded` | server → party | `loot_id`, `item_id`, `winner_entity_id`, `winner_name` |
| `loot_expired` | server → party | `loot_id` |

## Section 3: Server Architecture

### Layer 1: Data Subsystems (CRUD, own the data)

**`item_system`** — owns all item instances

```
create_item(item_create_info) → result<item_id, string>
restore_item(item_id, item_create_info) → result<item_id, string>
destroy_item(item_id)
get_item(item_id) → item*
item_exists(item_id) → bool
set_owner(item_id, entity_id)
damage_durability(item_id, amount)
repair_item(item_id, amount)
repair_item_full(item_id)
seed_next_id(uint32_t)
```

**`inventory`** — per-player item reference container (data structure)

```
add_item(item_id, count, pos_x, pos_y) → z_order
remove_item(item_id)
get_item(item_id) → inventory_entry*
has_item(item_id) → bool
items() → span<inventory_entry>
count() → size_t
is_full() → bool
next_z_order() → int32_t
```

**`equipment_state`** — per-player equipment slot references (data structure)

```
equip(slot, item_id)
unequip(slot) → item_id
get_equipped(slot) → optional<item_id>
find_slot_for(item_id) → optional<slot>
all_equipped() → map<slot, item_id>
```

**`bank_storage`** — per-player paginated bank slots (data structure)

```
get_slot(page, slot) → optional<item_id>
set_slot(page, slot, item_id)
clear_slot(page, slot)
find_item(item_id) → optional<{page, slot}>
find_empty_slot() → optional<{page, slot}>
```

**`trade_window`** — per-player trade state (data structure)

```
add_item(item_id)
remove_item(item_id)
set_gold(amount)
lock()
confirm()
reset()
items() → span<item_id>
is_locked() / is_confirmed()
```

### Layer 2: Container Management

**`inventory_system`** — lifecycle of per-player containers + gold + weight

```
create_player_containers(entity_id)
destroy_player_containers(entity_id)
get_inventory(entity_id) → inventory*
get_equipment(entity_id) → equipment_state*
get_bank(entity_id) → bank_storage*
get_gold(entity_id) → int64_t
add_gold(entity_id, amount)
remove_gold(entity_id, amount) → bool
get_weight(entity_id) → weight_info
set_weight(entity_id, current, max)
can_carry(entity_id, additional_weight) → bool
start_trade(entity_a, entity_b)
cancel_trade(entity_id)
get_trade_window(entity_id) → trade_window*
get_trade_partner(entity_id) → optional<entity_id>
```

### Layer 3: Operations (business logic)

**`item_ops` namespace** — high-level orchestrated actions. Each function returns a result struct. Handlers convert results to protocol messages.

```
pickup_item(player, map, pos, ...) → pickup_result
drop_item(player, item_id, ...) → drop_result
equip_item(player, item_id, slot, ...) → equip_result
unequip_item(player, slot, ...) → unequip_result
force_unequip(player, slot, reason, ...) → force_unequip_result
use_item(player, item_id, ...) → use_item_result
shop_buy(player, template_id, buy_price, ...) → shop_buy_result
shop_sell(player, item_id, ...) → shop_sell_result
shop_repair(player, item_id, repair_cost, ...) → shop_repair_result
bank_deposit(player, item_id, opt page, opt slot, ...) → bank_deposit_result
bank_withdraw(player, page, slot, ...) → bank_withdraw_result
execute_trade(player_a, player_b, ...) → trade_result
upgrade_item(player, item_id, stone_item_id, ...) → upgrade_result
activate_ability(player, item_id, ...) → activate_ability_result
drop_loot(map, pos, loot_item_result, ...) → optional<item_id>
drop_loot_multi(map, center_pos, vector<loot_item_result>, ...) → vector<item_id>
give_item(player, template_id, count, ...) → give_result
damage_equipment(player, slot, amount, ...) → damage_result
```

**Validation rules per operation:**

| Operation | Validations |
|-----------|-------------|
| pickup | Player in range, ground has items, inventory not full, can carry weight |
| drop | Item exists, player owns it, droppable, map allows drops |
| equip | Item exists, player owns it, correct equip_pos, meets requirements, not broken |
| unequip | Slot is occupied |
| force_unequip | Slot is occupied (always succeeds if so) |
| use_item | Item exists, player owns it, consumable, map allows use |
| shop_buy | Shop open, template exists, has gold, inventory not full, can carry weight |
| shop_sell | Item exists, player owns it, tradeable, shop open |
| shop_repair | Item exists, player owns it, has durability, has gold |
| bank_deposit | Item exists, player owns it, bank has space |
| bank_withdraw | Slot occupied, inventory not full, can carry weight |
| upgrade | Target exists, stone exists, correct stone type, not at max level |
| trade_add | Item exists, player owns it, tradeable, trade in offer phase |
| activate_ability | Item exists, equipped, has ability, not on cooldown |
| damage_equipment | Slot occupied, item has durability |

### Layer 4: Handlers (thin routing)

```
handler receives request
    → parse message
    → call item_ops::operation()
    → if success: send ack + state update messages
    → if failure: send ack with success=false
```

No business logic in handlers.

## Section 4: Equipment Model

### Linked Model

Equipment slots are references to items in inventory. The item lives in inventory; the equipment slot points to it.

- Equipping doesn't move the item, just sets the slot reference
- Unequipping clears the slot reference
- Item weight/position stays in inventory regardless

### Equipment Slots (14)

| Slot | Enum Value | Notes |
|------|-----------|-------|
| head | 0 | |
| body | 1 | |
| arms | 2 | |
| pants | 3 | |
| boots | 4 | |
| weapon | 5 | |
| shield | 6 | |
| twohand | 7 | Mutually exclusive with weapon + shield |
| ring_left | 8 | |
| ring_right | 9 | |
| amulet | 10 | |
| cape | 11 | |
| angel | 12 | |
| fullbody | 13 | Mutually exclusive with head + body + arms + pants + boots |

### Slot Mutual Exclusion Rules

- **twohand ↔ weapon + shield**: Equipping a two-handed weapon unequips weapon and shield. Equipping a weapon or shield unequips twohand.
- **fullbody ↔ head + body + arms + pants + boots**: Equipping a fullbody item unequips all 5 armor slots. Equipping any armor piece unequips fullbody.

### Combat-Driven Unequip

Three mechanisms cause server-initiated unequips:

1. **Durability break**: item hits 0 durability → auto-unequip, client receives `force_unequip` with reason `broken`
2. **Hammer strip**: hammer weapon combat hit can force-unequip a target's armor → `force_unequip` with reason `hammer_strip`
3. **Armor Break spell**: magic spell deals heavy durability damage → can trigger durability break → `force_unequip` with reason `armor_break`

### Aggregate Stats

Equipment stats are summed across all equipped items by the stat pipeline (not the item system). The item system provides per-item stats. The combat system uses aggregated totals for damage calculation — no per-piece RNG for damage reduction.

Durability damage targeting and hammer strip targeting are combat system decisions, not item system decisions.

## Section 5: Inventory Behavior

### Positioning

- Free-form pos_x/pos_y with z_order layering
- Default placement: (30, 40) for non-consumables
- Consumables placed at the position of the last consumable in inventory
- Player can reposition via `inventory_reposition` message
- Z-order auto-increments on add/reposition, compacts on save

### Stacking

- Template-driven via `max_stack` field
- Consumables (potions, scrolls, food) do NOT stack — each is count=1 (PvP balance)
- Arrows stack (template max_stack > 1)
- Gold is a separate counter, not an item

### Weight

- `max_weight = strength * 5 + level * 5`
- Checked on: pickup, shop buy, bank withdraw, trade receive
- Updated via `inventory_weight_update` message

## Section 6: Bank

### Model

Paginated slot-based storage. Each page has N fixed slots. Items identified by page + slot index.

### Operations

- **Auto-deposit**: client sends `item_id` only, server finds first empty slot
- **Targeted deposit**: client sends `item_id` + `page` + `slot`, server places exactly there
- **Withdraw**: client sends `page` + `slot`, server moves item to inventory
- **Reposition**: client sends from/to page+slot, server swaps/moves within bank

### Lazy Loading

Bank contents sent via `bank_open` when player interacts with bank NPC, not on login.

## Section 7: Ground Items

### Ownership

Ground items live on `world_subsystem` (spatial indexing). `item_system` owns instance data. World holds references with metadata.

### Ground Item Entry

```cpp
struct ground_item_entry
{
    item_id item;
    std::chrono::steady_clock::time_point drop_time;
    std::chrono::milliseconds lifetime;
};
```

### Lifetime

```
if template.ground_lifetime_ms has value:
    use template override
else:
    use global default (e.g., 180000ms = 3 minutes)
```

Future enhancement: rarity-based lifetime tiers.

### Expiration

Scheduled task runs periodically (e.g., every 10s):
1. `world.remove_expired_ground_items()` → list of expired items
2. For each: `item_system.destroy_item()` + broadcast `ground_item_removed`

### Multi-Drop (Boss Despawn Loot)

- Items placed in 3x3 grid centered on death position
- 1 item per tile, up to 9 items
- If a grid tile is unwalkable, overflow to nearest walkable tile
- All items always drop — no items lost to bad terrain

## Section 8: Party Loot

### Loot Rules (set by party leader, default: disabled)

| Rule | Behavior |
|------|----------|
| `disabled` | Items drop to ground as normal |
| `greed` | Items enter loot storage, players roll 1-100, highest wins |
| `master` | Items enter loot storage, leader assigns to party members |

### Rule Snapshotting

Loot rule is captured at NPC death time. Both kill loot and despawn loot from the same kill use the snapshotted rule. Changing party loot rules mid-fight only affects future kills.

### Loot Storage

Temporary per-party holding area for items awaiting distribution.

```cpp
struct pending_loot_item
{
    item_id item;
    loot_rule rule;                         // snapshot at NPC death
    map_id source_map;
    position source_pos;
    std::chrono::steady_clock::time_point created;
    loot_distribution_state state;          // rolling, assigned, awaiting_pickup
    std::unordered_map<entity_id, int16_t> rolls;
    std::optional<entity_id> assigned_to;
};
```

### Greed Flow

1. NPC dies → loot enters storage with snapshotted rule
2. `loot_available` broadcast to party
3. Players send `loot_roll` for items they want
4. `loot_roll_result` broadcast as each roll happens
5. Timer expires → non-rollers auto-pass
6. Highest roller wins → `loot_awarded` broadcast
7. `inventory_item_add` to winner
8. If winner's inventory full → item stays in storage until space made

### Master Looter Flow

1. NPC dies → loot enters storage
2. `loot_available` sent to leader
3. Leader sends `loot_assign` per item → target player
4. `loot_awarded` broadcast
5. `inventory_item_add` to target
6. If target's inventory full → item stays in storage

### Expiration

- Awarded but unclaimed (inventory full): item drops at winner's feet
- Unawarded (nobody rolled / master didn't assign): item drops at party leader's feet

### Future Considerations

- Rarity threshold (only route rare+ items through loot system)
- Edge cases around party membership changes mid-loot
- Leader disconnect during master loot distribution

## Section 9: Persistence

### Items Table

```sql
CREATE TABLE items (
    id              BIGINT PRIMARY KEY,
    character_id    BIGINT NOT NULL REFERENCES characters(id),
    template_id     INTEGER NOT NULL,

    -- Container location
    location        SMALLINT NOT NULL DEFAULT 0,  -- 0=inventory, 1=bank

    -- Inventory placement (location=0)
    pos_x           SMALLINT NOT NULL DEFAULT 30,
    pos_y           SMALLINT NOT NULL DEFAULT 40,
    z_order         INTEGER NOT NULL DEFAULT 0,

    -- Bank placement (location=1)
    bank_page       SMALLINT,
    bank_slot       SMALLINT,

    -- Instance data
    count           SMALLINT NOT NULL DEFAULT 1,
    durability      SMALLINT NOT NULL DEFAULT 0,
    max_durability  SMALLINT NOT NULL DEFAULT 0,
    color           SMALLINT NOT NULL DEFAULT 0,

    -- Attribute data
    upgrade_level   SMALLINT NOT NULL DEFAULT 0,
    main_enchant_type   SMALLINT NOT NULL DEFAULT 0,
    main_enchant_value  SMALLINT NOT NULL DEFAULT 0,
    sub_enchant_type    SMALLINT NOT NULL DEFAULT 0,
    sub_enchant_value   SMALLINT NOT NULL DEFAULT 0,
    custom_made     BOOLEAN NOT NULL DEFAULT FALSE,
    custom_quality  SMALLINT NOT NULL DEFAULT 0
);
```

### Character Equipment Table

```sql
CREATE TABLE character_equipment (
    character_id    BIGINT NOT NULL REFERENCES characters(id),
    slot            SMALLINT NOT NULL,
    item_id         BIGINT NOT NULL REFERENCES items(id),
    PRIMARY KEY (character_id, slot)
);
```

### Load Flow

1. Query items + equipment for character
2. `item_system.restore_item()` for each item row
3. Add to inventory or bank based on location
4. Set equipment slot references
5. Seed `item_system.next_id` from max item id
6. Calculate weight
7. Send `inventory_data` to client

### Save Flow

1. Compact z_order (renumber 0..N-1)
2. Build item rows from inventory + bank
3. Save equipment slots to `character_equipment`
4. Upsert items, delete removed items

### Not Persisted

- Ground items (transient, expire on timer)
- Trade window state (canceled on disconnect)
- Special ability activation state (resets on logout)
- Shop inventory (from config files)
- Party loot storage (transient)

## Section 10: Template Registry

### Cross-Validation at Startup

After all registries load, validate references:
- Loot tables: every item_id in every pool must exist in item_registry
- Shop configs: every item_id must exist in item_registry
- Crafting recipes: every input/output template_id must exist
- Log warnings for invalid references, skip bad entries

### Template Additions

- `special_ability`: optional ability type
- `ground_lifetime_ms`: optional per-template ground lifetime override

### Load Order

```
1. item_registry.load()         -- templates available
2. loot_registry.load()         -- references item_registry
3. shop_registry.load()         -- references item_registry
4. recipe_registry.load()       -- references item_registry
5. cross_validate_registries()  -- warn on broken references
6. item_system.initialize()     -- ready to create instances
```

## Section 11: Special Abilities

### Item Template Data

Items with special abilities have a `special_ability` field on the template. Types include:
- `paralyze_on_hit` (Sword of Medusa)
- `invincibility` (Merien Shield)
- `weapon_break_on_melee` (Medusa Chest Plate)
- `hp_halve` (Xelima weapons)
- Extensible for future ability types

### Player State

- One ability active at a time per player
- 20-minute global cooldown between activations
- Activation state lives on the player, not the item
- Activation/expiration broadcast to visible players via `ability_activated` / `ability_expired`
