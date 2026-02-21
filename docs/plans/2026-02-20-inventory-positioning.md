# Inventory Positioning & Z-Order Implementation Plan

> **For Claude:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** Refactor inventory from slot-indexed array to item_id-keyed collection with free-form pixel positioning (x, y), z-order layering, and weight capacity enforcement.

**Architecture:** Replace the fixed `inventory_slot[50]` array with a `vector<inventory_entry>` keyed by `item_id`. All client-server messages switch from slot indices to item IDs. Add z_order field for layering (auto-increment on drag, compact on save). Add weight capacity using legacy formula `str * 5 + level * 5`. Bank storage keeps its existing slot-based model unchanged.

**Tech Stack:** C++20, PostgreSQL (migration for z_order column), JSON protocol

**Design doc:** `docs/plans/2026-02-20-inventory-positioning-design.md`

---

### Task 1: Core Data Model — inventory_entry and inventory class

Rewrite the `inventory` class from slot-based to entry-based. This is the foundation everything else builds on.

**Files:**
- Modify: `src/inventory/inventory.h` (full rewrite of `inventory_slot` → `inventory_entry` and `inventory` class)
- Modify: `tests/test_inventory.cpp` (rewrite all slot-based tests)

**Step 1: Rewrite `inventory.h`**

Replace `inventory_slot` with `inventory_entry`:

```cpp
struct inventory_entry
{
    item_id item{};
    int16_t count{0};
    int16_t pos_x{0};
    int16_t pos_y{0};
    int32_t z_order{0};
    std::optional<uint8_t> equipped_as{};

    [[nodiscard]] auto is_equipped() const -> bool { return equipped_as.has_value(); }
};
```

Replace the `inventory` class internals:
- Private: `std::vector<inventory_entry> entries_` + `int16_t max_items_` + `int32_t next_z_order_{0}`
- `get_item(item_id)` → `inventory_entry*` — linear scan of `entries_`
- `get_item(item_id) const` → `const inventory_entry*`
- `add_item(item_id, int16_t count, int16_t pos_x = 0, int16_t pos_y = 0)` → `inventory_entry*` — returns nullptr if full, else pushes back and assigns next z_order
- `remove_item(item_id)` → `bool` — erase from vector
- `find_equipped(uint8_t equip_slot)` → `inventory_entry*` — scan for `equipped_as == equip_slot`
- `find_by_template(item_id template_id, item_system*)` — needed by upgrade handler to find stones
- `items()` → `std::span<inventory_entry>` for iteration
- `items() const` → `std::span<const inventory_entry>`
- `count()` → `int16_t` — current number of entries
- `max_items()` → `int16_t`
- `is_full()` → `bool`
- `is_empty()` → `bool`
- `has_item(item_id)` → `bool`
- `count_item(item_id)` → `int32_t` — sum of counts for matching item
- `has_item(item_id, int16_t count)` → `bool`
- `next_z_order()` → `int32_t` — returns `next_z_order_++`
- `compact_z_order()` — sort entries by z_order, renumber 0..N-1, set `next_z_order_` = N
- `clear_all()` — clear vector and reset counter
- Keep `remove_item_count(item_id, int16_t)` — same logic but over entries_ instead of slots_

Remove all slot-index based methods: `get_slot()`, `find_empty_slot()`, `find_empty_unequipped_slot()`, `move_item(from, to)`, `swap_slots()`, `clear_slot()`, `capacity()`, `free_slots()`, `used_slots()`, `find_item()` (returned slot index).

Keep `bank_storage` as-is — it still inherits from the old slot-based inventory. To do this cleanly, **decouple**: make `bank_storage` NOT inherit from `inventory`. Give it its own simple implementation (it's just a slot array). Copy the old `inventory` class code into `bank_storage` as a standalone class. The old `inventory` was simple enough that duplicating for bank is fine — bank doesn't need z_order, weight, or item_id keying.

Similarly, `trade_window` keeps using `inventory_slot` — rename that struct to `bank_slot` or just inline it in `bank_storage`/`trade_window`.

**Step 2: Rewrite `tests/test_inventory.cpp`**

Replace all slot-based tests with entry-based equivalents:
- `inventory_entry` default state test
- `add_item` / `remove_item` / `get_item` by item_id
- `is_full` at capacity
- `z_order` auto-increment on add
- `compact_z_order` renumbers correctly
- `find_equipped` returns correct entry
- `has_item` / `count_item` work correctly
- Bank tests stay slot-based (bank_storage is unchanged)

**Step 3: Build and run tests**

Run: `cmake --build build --config Debug && ./bin/hgserver_tests --gtest_filter=inventory*`

Expected: All new inventory tests pass. Other tests will fail (they still use old API) — that's expected and fixed in later tasks.

**Step 4: Commit**

```
feat: rewrite inventory from slot-indexed to item_id-keyed with z_order
```

---

### Task 2: Equipment Cache — Remove inv_index dependency

The `equipped_item` struct in `equipment.h` stores `inv_index` (slot index) which no longer exists. The equipment cache needs to reference items by `item_id` instead.

**Files:**
- Modify: `src/player/equipment.h` — remove `inv_index` from `equipped_item`
- Modify: `src/player/player_system.h` — change `equip_item` signature
- Modify: `src/player/player_system.cpp` — update `equip_item`, `unequip_item`, `rebuild_equipment_cache`

**Step 1: Update `equipment.h`**

In `equipped_item`:
- Remove `int16_t inv_index{-1}` field
- Remove `inv_index = -1` from `clear()`
- No other changes needed — `id`, `template_id`, `durability`, `max_durability` stay

**Step 2: Update `player_system.h`**

Change signature:
```cpp
void equip_item(player_id id, item_id item, equip_slot slot);
```
(was: `void equip_item(player_id id, int16_t inv_slot_index, equip_slot slot)`)

**Step 3: Update `player_system.cpp`**

`equip_item()`: Find entry by item_id via `inv->get_item(item)`, set `entry->equipped_as`.

`unequip_item()`: Find entry via `inv->find_equipped(slot)`, clear `entry->equipped_as`.

`rebuild_equipment_cache()`: Iterate `inv->items()` instead of slot index loop. No more `eq.inv_index = i`.

**Step 4: Build**

Run: `cmake --build build --config Debug`

Expected: Compilation errors in handlers that call `equip_item` with old signature — fixed in Task 5.

**Step 5: Commit**

```
refactor: remove inv_index from equipment cache, use item_id lookup
```

---

### Task 3: Protocol Structs — Switch slot fields to item_id

Update all protocol request/response structs to use `item_id` instead of slot indices.

**Files:**
- Modify: `src/network/json_protocol.h` — update structs, add z_order, add new messages
- Modify: `src/network/json_protocol.cpp` — update `from_json`/`to_json` implementations
- Modify: `tests/test_json_protocol.cpp` — update protocol tests

**Step 1: Update `json_protocol.h` structs**

1. `inventory_item_msg` (line ~1470):
   - Remove: `uint8_t slot`
   - Add: `int32_t z_order{0}`

2. `player_equip_request_data` (line ~2123):
   - Change: `int16_t inventory_slot{-1}` → `uint32_t item_id{0}`

3. `equip_result_msg` (line ~2140):
   - Remove: `std::optional<uint8_t> swapped_to_inv_slot`
   - Remove: `std::optional<uint8_t> shield_to_inv_slot`

4. `unequip_result_msg` (line ~2159):
   - Remove: `uint8_t inventory_slot{0}`

5. `inventory_reposition_request_data` (line ~2224):
   - Replace `from_slot`/`to_slot` with `uint32_t item_id{0}`
   - Keep `pos_x`, `pos_y`

6. `drop_item_request_data` (line ~2236):
   - Change: `int16_t slot{0}` → `uint32_t item_id{0}`

7. `use_item_request_data` (line ~2712):
   - Change: `int16_t slot{0}` → `uint32_t item_id{0}`

8. Shop/bank request structs (lines ~2272-2326):
   - `shop_sell_request_data`: `int16_t inventory_slot{-1}` → `uint32_t item_id{0}`
   - `shop_sell_confirm_request_data`: same
   - `shop_repair_request_data`: same
   - `shop_repair_confirm_request_data`: same
   - `bank_deposit_request_data`: same

9. `admin_remove_item_request_data` (line ~2882):
   - Change: `int16_t inventory_slot{-1}` → `uint32_t item_id{0}`

10. Message builders:
    - Rename: `make_inventory_slot_update(int16_t slot, const inventory_item_msg*)` → `make_inventory_item_update(const inventory_item_msg& item)`
    - Add: `make_inventory_item_removed(uint32_t item_id)` → `json_message`
    - Add: `make_inventory_weight_update(int32_t current_weight, int32_t max_weight)` → `json_message`
    - Add enum entries: `inventory_item_update`, `inventory_item_removed`, `inventory_weight_update`

**Step 2: Update `json_protocol.cpp`**

Update all `from_json`/`to_json` implementations to match new field names:
- `"slot"` → `"item_id"` in request parsing
- `"inventory_slot"` → `"item_id"` in request parsing
- `"from_slot"`/`"to_slot"` → `"item_id"` in reposition parsing
- Add `"z_order"` to `inventory_item_msg::to_json()`
- Remove `"slot"` from `inventory_item_msg::to_json()`
- Remove `"swapped_to_inv_slot"`, `"shield_to_inv_slot"` from `equip_result_msg::to_json()`
- Remove `"inventory_slot"` from `unequip_result_msg::to_json()`
- Implement new builders: `make_inventory_item_update`, `make_inventory_item_removed`, `make_inventory_weight_update`
- Add new message types to `type_map` and `to_string()`

**Step 3: Update `tests/test_json_protocol.cpp`**

Update any tests that reference `slot`, `inventory_slot`, `from_slot`, `to_slot` fields.

**Step 4: Build and run protocol tests**

Run: `cmake --build build --config Debug && ./bin/hgserver_tests --gtest_filter=*json_protocol*`

Expected: Protocol tests pass.

**Step 5: Commit**

```
refactor: switch protocol structs from slot indices to item_id
```

---

### Task 4: inventory_system — Update subsystem API

Update `inventory_system` methods to match new `inventory` API.

**Files:**
- Modify: `src/inventory/inventory_system.h` — update method signatures
- Modify: `src/inventory/inventory_system.cpp` — update implementations

**Step 1: Update `inventory_system.h`**

- `add_item(entity_id, item_id, int16_t count, int16_t pos_x = 0, int16_t pos_y = 0)` → `inventory_result`
- Remove: `move_item(entity_id, int16_t from, int16_t to)`
- Remove: `swap_items(entity_id, int16_t a, int16_t b)`
- `deposit_item(entity_id, item_id item)` → `inventory_result` (was slot-based)
- Keep: `remove_item`, `has_item`, `count_item`, `is_full`
- `free_slots` → rename to `free_capacity` or keep name (returns `max_items - count`)

**Step 2: Update `inventory_system.cpp`**

- `add_item()`: Call `inv->add_item(item, count, pos_x, pos_y)` instead of finding empty slot
- Remove `move_item()` and `swap_items()` implementations
- `deposit_item()`: Use `inv->get_item(item)` instead of `inv->get_slot(slot)`
- `withdraw_item()`: Find empty inventory capacity check, add to inventory with `add_item`

**Step 3: Build**

Run: `cmake --build build --config Debug`

Expected: Compilation errors in handlers that call removed methods — fixed in Task 5.

**Step 4: Commit**

```
refactor: update inventory_system API for item_id-keyed inventory
```

---

### Task 5: Handler Refactor — Equipment, Use Item, Drop

Update the equipment, use-item, and drop-item handlers.

**Files:**
- Modify: `src/bridge/handlers/game_handlers_equipment.cpp` — equip, unequip, use_item, upgrade handlers
- Modify: `src/bridge/handlers/game_handlers_shop.cpp` — drop_item handler
- Modify: `tests/test_player.cpp` — update equipment tests
- Modify: `tests/test_use_item.cpp` — update use item tests

**Step 1: Update `handle_player_equip()`**

Change from:
```cpp
auto* slot = inv->get_slot(data.inventory_slot);
```
To:
```cpp
auto* entry = inv->get_item(item_id{data.item_id});
```

Update the equip_item call:
```cpp
players_->equip_item(pid, item_id{data.item_id}, target_slot);
```

Update response building — use `make_inventory_item_update` instead of `make_inventory_slot_update`.

**Step 2: Update `handle_player_unequip()`**

Update response to not include `inventory_slot`. The item stays in inventory — just clear `equipped_as` flag. Send `make_inventory_item_update` with updated entry.

**Step 3: Update `handle_player_use_item()`**

Change `inv->get_slot(data.slot)` to `inv->get_item(item_id{data.item_id})`.
Update slot update broadcasts to use `make_inventory_item_update` / `make_inventory_item_removed`.

**Step 4: Update `handle_player_upgrade_item()`**

The upgrade handler loops through inventory to find upgrade stones. Change from slot index loop to iterating `inv->items()`. Use `entry.item` to look up item instances.

**Step 5: Update `handle_player_drop_item()`** (in game_handlers_shop.cpp)

Change `inv->get_slot(data.slot)` to `inv->get_item(item_id{data.item_id})`.

**Step 6: Update tests**

Update `tests/test_player.cpp` and `tests/test_use_item.cpp` to use item_id instead of slot indices in test setup and assertions.

**Step 7: Build and run tests**

Run: `cmake --build build --config Debug && ./bin/hgserver_tests --gtest_filter=*player*:*use_item*`

**Step 8: Commit**

```
refactor: update equipment, use, drop handlers for item_id inventory
```

---

### Task 6: Handler Refactor — Shop, Bank, Reposition

Update shop, bank, and reposition handlers.

**Files:**
- Modify: `src/bridge/handlers/game_handlers_shop.cpp` — all shop/bank/reposition handlers

**Step 1: Update shop handlers**

For each of `handle_shop_sell`, `handle_shop_sell_confirm`, `handle_shop_repair`, `handle_shop_repair_confirm`:
- Change `inv->get_slot(data.inventory_slot)` to `inv->get_item(item_id{data.item_id})`
- Update slot update broadcasts

**Step 2: Update `handle_shop_buy()`**

This one adds items to inventory. Update to use `inventory_system::add_item()` with the new signature. Send `make_inventory_item_update` instead of `make_inventory_slot_update`.

**Step 3: Update `handle_bank_deposit()`**

Change from slot-based to item_id-based lookup.

**Step 4: Update `handle_bank_withdraw()`**

Bank withdrawal still uses `bank_slot` (bank stays slot-based). The inventory side uses the new `add_item`.

**Step 5: Rewrite `handle_inventory_reposition()`**

Complete rewrite — no more from_slot/to_slot/swap:
```cpp
auto* entry = inv->get_item(item_id{data.item_id});
if (!entry) return;
entry->pos_x = data.pos_x;
entry->pos_y = data.pos_y;
entry->z_order = inv->next_z_order();
```

**Step 6: Build and run tests**

Run: `cmake --build build --config Debug && ./bin/hgserver_tests --gtest_filter=*shop*:*bank*`

**Step 7: Commit**

```
refactor: update shop, bank, reposition handlers for item_id inventory
```

---

### Task 7: Handler Refactor — Crafting and Admin

Update crafting snapshot logic and admin handlers.

**Files:**
- Modify: `src/bridge/handlers/game_handlers_crafting.cpp` — crafting snapshot logic
- Modify: `src/bridge/handlers/admin_web_handlers.cpp` — give/remove item handlers

**Step 1: Update crafting snapshot**

Replace slot-index loops with item_id snapshot:
```cpp
// Before crafting: snapshot current item_ids
std::unordered_set<uint32_t> before_ids;
for (const auto& entry : inv->items())
    before_ids.insert(entry.item.value);

// ... crafting happens ...

// After: find added/removed/changed items
for (const auto& entry : inv->items())
{
    if (!before_ids.contains(entry.item.value))
        // New item — send inventory_item_update
}
for (auto id : before_ids)
{
    if (!inv->get_item(item_id{id}))
        // Removed item — send inventory_item_removed
}
```

**Step 2: Update admin `handle_give_item()`**

Update to use `make_inventory_item_update` with the new `inventory_item_msg` (no slot field).

**Step 3: Update admin `handle_remove_item()`**

Change from `inv->get_slot(req.inventory_slot)` to `inv->get_item(item_id{req.item_id})`.

**Step 4: Build and run tests**

Run: `cmake --build build --config Debug && ./bin/hgserver_tests --gtest_filter=*craft*:*admin*`

**Step 5: Commit**

```
refactor: update crafting and admin handlers for item_id inventory
```

---

### Task 8: Persistence — Load and Save

Update the auth_handlers persistence code to work with the new inventory model.

**Files:**
- Modify: `src/bridge/handlers/auth_handlers.cpp` — load_items restore, save_player_state, enter_game inventory build
- Modify: `src/database/schema.sql` — add z_order column
- Create: `tools/migrate/migrations/YYYYMMDD_HHMMSS_add_item_z_order.sql` — migration

**Step 1: Create migration**

Run: `cd tools/migrate && npx tsx migrate.ts create add_item_z_order`

Fill in:
```sql
-- up
ALTER TABLE items ADD COLUMN z_order SMALLINT NOT NULL DEFAULT 0;

-- down
ALTER TABLE items DROP COLUMN z_order;
```

**Step 2: Update `schema.sql`**

Add `z_order SMALLINT NOT NULL DEFAULT 0` to the `items` table after `pos_y`.

**Step 3: Update load logic in `handle_enter_game()`**

When restoring inventory items (location = inventory or equipment):
- Use `inv->add_item(restored_id, count, pos_x, pos_y)` to create entry
- Set `entry->z_order` from DB row
- Set `entry->equipped_as` from `equip_slot` if present
- After all items loaded, set `inv->set_next_z_order(max_z + 1)`

When building `inventory_data` message:
- Iterate `inv->items()` instead of slot index loop
- Populate `inventory_item_msg` with `z_order` field (no `slot` field)

**Step 4: Update save logic in `save_player_state()`**

- Call `inv->compact_z_order()` before collecting items
- Iterate `inv->items()` instead of slot index loop
- Write `slot=0` for inventory items, `z_order` from entry
- Include `z_order` in INSERT query

**Step 5: Update `destroy_player_items()` on logout**

Change slot index loop to iterate `inv->items()`.

**Step 6: Update SQL queries**

Add `z_order` to:
- SELECT in `load_items()`
- INSERT in `save_player_state()`

**Step 7: Build and run**

Run: `cmake --build build --config Debug && ./bin/hgserver_tests`

**Step 8: Commit**

```
feat: update item persistence for z_order and slot-free inventory
```

---

### Task 9: Weight System

Add weight capacity tracking and enforcement.

**Files:**
- Modify: `src/inventory/inventory.h` — add weight tracking to inventory class
- Modify: `src/inventory/inventory_system.h` — add weight query methods
- Modify: `src/inventory/inventory_system.cpp` — implement weight enforcement
- Modify: `src/network/json_protocol.h` — add weight update message (if not done in Task 3)
- Modify: `src/network/json_protocol.cpp` — implement weight message builder
- Modify: `src/bridge/handlers/game_handlers_shop.cpp` — enforce weight on buy/withdraw
- Modify: `src/bridge/handlers/auth_handlers.cpp` — send weight on login
- Create: `tests/test_inventory_weight.cpp` — weight-specific tests

**Step 1: Write weight tests**

```cpp
TEST(inventory_weight, max_weight_formula)
{
    // Legacy: str * 5 + level * 5
    EXPECT_EQ(inventory::max_weight(10, 1), 55);   // 50 + 5
    EXPECT_EQ(inventory::max_weight(50, 100), 750); // 250 + 500
}

TEST(inventory_weight, total_weight_calculated)
{
    // Items with known weights, verify total
}

TEST(inventory_weight, reject_when_over_weight)
{
    // Try to add item that would exceed weight limit
}
```

**Step 2: Run tests to verify they fail**

**Step 3: Implement weight tracking**

Add to `inventory` class:
- `static auto max_weight(int32_t strength, int32_t level) -> int32_t` — returns `str * 5 + level * 5`

Add to `inventory_system`:
- `auto total_weight(entity_id owner) -> int32_t` — sum of `item_template.weight * entry.count` for all entries
- Weight check in `add_item()` — requires access to item_system for template weights
- Add `item_system*` pointer to inventory_system (wired in application.cpp)

**Step 4: Implement weight enforcement**

Add weight check before adding items in:
- `inventory_system::add_item()` — return `inventory_result::weight_limit` if over
- Shop buy handler — check before purchase
- Bank withdraw handler — check before withdrawal
- Loot pickup — check before granting

**Step 5: Implement weight protocol message**

`make_inventory_weight_update(current_weight, max_weight)` — sends `{ "type": "inventory_weight_update", "data": { ... } }`

Send on:
- Login (in enter_game response)
- After any inventory change that affects weight

**Step 6: Run all tests**

Run: `cmake --build build --config Debug && ./bin/hgserver_tests`

**Step 7: Commit**

```
feat: add inventory weight capacity system with legacy formula
```

---

### Task 10: Build helper — `build_inventory_item_msg` refactor

There's a `build_inventory_item_msg` helper used in multiple places to construct `inventory_item_msg` from an inventory entry + item instance + template. This needs updating for the new struct.

**Files:**
- Modify: `src/bridge/handlers/auth_handlers.cpp` or wherever `build_inventory_item_msg` is defined
- Potentially create a shared utility if it's duplicated

**Step 1: Find and update `build_inventory_item_msg`**

Search for all call sites. Update to:
- Accept `inventory_entry&` instead of `int16_t slot`
- Populate `z_order` from entry
- Remove `slot` field population

**Step 2: Build and run full test suite**

Run: `cmake --build build --config Debug && ./bin/hgserver_tests`

Expected: All tests pass.

**Step 3: Commit**

```
refactor: update build_inventory_item_msg for slot-free inventory
```

---

### Task 11: Database Migration and Schema

Finalize the migration and ensure schema.sql is consistent.

**Files:**
- Verify: `src/database/schema.sql` has z_order column
- Verify: Migration file is correct
- Run migration against dev database

**Step 1: Verify schema.sql**

Confirm `items` table has `z_order SMALLINT NOT NULL DEFAULT 0` after `pos_y`.

**Step 2: Run migration**

```bash
cd tools/migrate && npx tsx migrate.ts migrate
```

**Step 3: Verify migration status**

```bash
cd tools/migrate && npx tsx migrate.ts status
```

**Step 4: Commit** (if any fixes needed)

```
chore: finalize z_order migration and schema
```

---

### Task 12: Full Integration Test and Cleanup

Run the complete test suite, fix any remaining compilation errors or test failures.

**Files:**
- All test files
- Any remaining files with compilation errors

**Step 1: Full build**

Run: `cmake --build build --config Debug 2>&1`

Fix any remaining compilation errors.

**Step 2: Run all tests**

Run: `./bin/hgserver_tests`

Expected: All tests pass (same count as before, minus removed tests, plus new tests).

**Step 3: Grep for any remaining slot references**

Search for `get_slot(`, `find_empty_slot()`, `from_slot`, `to_slot`, `inv_slot`, `inventory_slot` in handler and player system code to verify no stale references remain.

Slot references in bank/trade code are expected and OK.

**Step 4: Update protocol docs**

Update `docs/protocol/items.md` and `docs/protocol/player.md` to reflect the new field names (item_id instead of slot, z_order addition).

**Step 5: Update PROGRESS.md**

Add entry under Recent Changes documenting the inventory refactor.

**Step 6: Final commit**

```
chore: cleanup and docs for inventory positioning refactor
```
