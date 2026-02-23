# Item System Redesign — Implementation Plan

> **For Claude:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** Rewrite the item system to match the architecture in `docs/plans/2026-02-22-item-system-redesign.md` and the protocol spec in `docs/protocol/items-v2.md`.

**Architecture:** Four-layer system — (1) data structures own data, (2) inventory_system manages container lifecycle, (3) item_ops namespace orchestrates business logic, (4) handlers are thin routing. All action messages are acknowledgments; state changes flow through dedicated update channels.

**Tech Stack:** C++20, PostgreSQL, WebSocket JSON protocol, GTest

**Build:** `cmake --build build --config Debug -j8` from project root
**Test:** `./bin/hgserver_tests` from project root

---

## Phase 1: Core Data Structures

Update the foundational types that everything else builds on. These changes are additive — existing code continues to compile.

---

### Task 1: Update equip_pos enum and add angel slot

**Files:**
- Modify: `src/item/item.h` (equip_pos enum, ~line 32-45)
- Modify: `src/player/equipment.h` (equip_slot enum, ~line 14-30)

**Step 1: Update `item::equip_pos` to 14 slots**

In `src/item/item.h`, replace the equip_pos enum with the full 14-slot design:

```cpp
enum class equip_pos : uint8_t
{
    none = 0,
    head = 1,
    body = 2,
    arms = 3,
    pants = 4,
    boots = 5,
    weapon = 6,
    shield = 7,
    twohand = 8,
    ring_left = 9,
    ring_right = 10,
    amulet = 11,
    cape = 12,
    angel = 13,
    fullbody = 14,
};
```

**Step 2: Update `player::equip_slot` to match design**

In `src/player/equipment.h`, replace enum with the 14 slots from the design. Remove `held_item`, add `twohand`, `angel`, `fullbody`, split `ring` into `ring_left`/`ring_right`:

```cpp
enum class equip_slot : uint8_t
{
    head = 0,
    body = 1,
    arms = 2,
    pants = 3,
    boots = 4,
    weapon = 5,
    shield = 6,
    twohand = 7,
    ring_left = 8,
    ring_right = 9,
    amulet = 10,
    cape = 11,
    angel = 12,
    fullbody = 13,

    count = 14
};
```

Update `equip_slot_name()` to match.

**Step 3: Build and fix any compilation errors from enum changes**

Run: `cmake --build build --config Debug -j8`

Grep for `held_item` references and remove them. Update any single `ring` references to `ring_left`/`ring_right` as appropriate.

**Step 4: Run tests**

Run: `./bin/hgserver_tests`
Expected: All existing tests pass.

---

### Task 2: New equipment_state (linked model)

The design says equipment slots are just `item_id` references into inventory. The current `equipment_state` caches template_id/durability. Rewrite to the linked model.

**Files:**
- Modify: `src/player/equipment.h`
- Create: `tests/test_equipment_state.cpp`

**Step 1: Write tests for new equipment_state**

```cpp
#include <gtest/gtest.h>
#include "player/equipment.h"

using namespace hb::player;

TEST(equipment_state_test, initially_empty)
{
    equipment_state eq;
    EXPECT_FALSE(eq.get_equipped(equip_slot::weapon).has_value());
    EXPECT_TRUE(eq.all_equipped().empty());
}

TEST(equipment_state_test, equip_and_get)
{
    equipment_state eq;
    eq.equip(equip_slot::weapon, item_id{100});
    auto equipped = eq.get_equipped(equip_slot::weapon);
    ASSERT_TRUE(equipped.has_value());
    EXPECT_EQ(equipped->value, 100u);
}

TEST(equipment_state_test, unequip_returns_item_id)
{
    equipment_state eq;
    eq.equip(equip_slot::body, item_id{200});
    auto removed = eq.unequip(equip_slot::body);
    ASSERT_TRUE(removed.has_value());
    EXPECT_EQ(removed->value, 200u);
    EXPECT_FALSE(eq.get_equipped(equip_slot::body).has_value());
}

TEST(equipment_state_test, unequip_empty_returns_nullopt)
{
    equipment_state eq;
    EXPECT_FALSE(eq.unequip(equip_slot::weapon).has_value());
}

TEST(equipment_state_test, find_slot_for_item)
{
    equipment_state eq;
    eq.equip(equip_slot::head, item_id{300});
    auto slot = eq.find_slot_for(item_id{300});
    ASSERT_TRUE(slot.has_value());
    EXPECT_EQ(*slot, equip_slot::head);
}

TEST(equipment_state_test, find_slot_for_missing_returns_nullopt)
{
    equipment_state eq;
    EXPECT_FALSE(eq.find_slot_for(item_id{999}).has_value());
}

TEST(equipment_state_test, all_equipped_returns_occupied_slots)
{
    equipment_state eq;
    eq.equip(equip_slot::weapon, item_id{1});
    eq.equip(equip_slot::body, item_id{2});
    auto all = eq.all_equipped();
    EXPECT_EQ(all.size(), 2u);
}

TEST(equipment_state_test, clear_all)
{
    equipment_state eq;
    eq.equip(equip_slot::weapon, item_id{1});
    eq.equip(equip_slot::body, item_id{2});
    eq.clear_all();
    EXPECT_TRUE(eq.all_equipped().empty());
}
```

**Step 2: Run tests to verify they fail**

Run: `./bin/hgserver_tests --gtest_filter="equipment_state_test.*"`
Expected: Compilation or test failures (API doesn't match yet).

**Step 3: Rewrite equipment_state to linked model**

In `src/player/equipment.h`, replace the current `equipped_item` struct and `equipment_state` struct:

```cpp
// Equipment state — linked model (slots reference items in inventory)
struct equipment_state
{
    // Equip an item in a slot
    void equip(equip_slot slot, item_id id)
    {
        slots_[static_cast<size_t>(slot)] = id;
    }

    // Unequip a slot, returns the item_id that was there
    auto unequip(equip_slot slot) -> std::optional<item_id>
    {
        auto& s = slots_[static_cast<size_t>(slot)];
        if (!s.is_valid())
            return std::nullopt;
        auto id = s;
        s = item_id{};
        return id;
    }

    // Get what's equipped in a slot
    [[nodiscard]] auto get_equipped(equip_slot slot) const -> std::optional<item_id>
    {
        auto& s = slots_[static_cast<size_t>(slot)];
        return s.is_valid() ? std::optional{s} : std::nullopt;
    }

    // Find which slot an item is in
    [[nodiscard]] auto find_slot_for(item_id id) const -> std::optional<equip_slot>
    {
        for (size_t i = 0; i < equip_slot_count; ++i)
        {
            if (slots_[i] == id)
                return static_cast<equip_slot>(i);
        }
        return std::nullopt;
    }

    // Get all occupied slots
    [[nodiscard]] auto all_equipped() const -> std::vector<std::pair<equip_slot, item_id>>
    {
        std::vector<std::pair<equip_slot, item_id>> result;
        for (size_t i = 0; i < equip_slot_count; ++i)
        {
            if (slots_[i].is_valid())
                result.emplace_back(static_cast<equip_slot>(i), slots_[i]);
        }
        return result;
    }

    void clear_all()
    {
        for (auto& s : slots_)
            s = item_id{};
    }

private:
    std::array<item_id, equip_slot_count> slots_{};
};
```

Remove the old `equipped_item` struct. Code that references `equipped_item` (combat system, entity builders, stat pipeline) will need updating — search for all `equipped_item` usages and update to use `item_system->get_item(equipment->get_equipped(slot))` pattern instead.

**Step 4: Build, fix compilation errors**

Run: `cmake --build build --config Debug -j8`

All code referencing `equipped_item` must be updated. Key locations:
- `src/bridge/handlers/game_handlers_equipment.cpp`
- `src/bridge/handlers/entity_builders.cpp`
- `src/combat/combat_system.cpp`
- `src/item/item_effect.h` (stat application pipeline)

For each, replace `equipment.get(slot).id` → `equipment.get_equipped(slot)` and look up full item via `item_system->get_item(id)`.

**Step 5: Run tests**

Run: `./bin/hgserver_tests`
Expected: All tests pass including new equipment_state tests.

---

### Task 3: Update inventory_entry (remove equipped_as)

The design separates equipment state from inventory. Remove `equipped_as` from `inventory_entry`.

**Files:**
- Modify: `src/inventory/inventory.h` (inventory_entry struct, ~line 24-34)

**Step 1: Remove equipped_as from inventory_entry**

```cpp
struct inventory_entry
{
    item_id item{};
    int16_t count{0};
    int16_t pos_x{0};
    int16_t pos_y{0};
    int32_t z_order{0};
};
```

Remove `find_equipped()` methods from the `inventory` class.

**Step 2: Build and fix compilation errors**

Run: `cmake --build build --config Debug -j8`

Grep for `equipped_as` and `find_equipped` — update all references to use the new `equipment_state` API instead.

**Step 3: Run tests**

Run: `./bin/hgserver_tests`
Expected: All tests pass.

---

### Task 4: Paginated bank_storage

Replace the flat slot array with paginated pages.

**Files:**
- Modify: `src/inventory/inventory.h` (bank_storage class)
- Create: `tests/test_bank_storage.cpp`

**Step 1: Write tests for paginated bank**

```cpp
#include <gtest/gtest.h>
#include "inventory/inventory.h"

using namespace hb::inventory;

TEST(bank_storage_test, empty_bank_has_pages)
{
    bank_storage bank(4, 12); // 4 pages, 12 slots each
    EXPECT_EQ(bank.total_pages(), 4);
    EXPECT_EQ(bank.slots_per_page(), 12);
}

TEST(bank_storage_test, set_and_get_slot)
{
    bank_storage bank(4, 12);
    bank.set_slot(0, 3, item_id{100});
    auto id = bank.get_slot(0, 3);
    ASSERT_TRUE(id.has_value());
    EXPECT_EQ(id->value, 100u);
}

TEST(bank_storage_test, clear_slot)
{
    bank_storage bank(4, 12);
    bank.set_slot(1, 5, item_id{200});
    bank.clear_slot(1, 5);
    EXPECT_FALSE(bank.get_slot(1, 5).has_value());
}

TEST(bank_storage_test, find_empty_slot)
{
    bank_storage bank(2, 3); // 2 pages, 3 slots
    bank.set_slot(0, 0, item_id{1});
    bank.set_slot(0, 1, item_id{2});
    auto empty = bank.find_empty_slot();
    ASSERT_TRUE(empty.has_value());
    EXPECT_EQ(empty->page, 0);
    EXPECT_EQ(empty->slot, 2);
}

TEST(bank_storage_test, find_item)
{
    bank_storage bank(4, 12);
    bank.set_slot(2, 7, item_id{500});
    auto loc = bank.find_item(item_id{500});
    ASSERT_TRUE(loc.has_value());
    EXPECT_EQ(loc->page, 2);
    EXPECT_EQ(loc->slot, 7);
}

TEST(bank_storage_test, full_bank)
{
    bank_storage bank(1, 2);
    bank.set_slot(0, 0, item_id{1});
    bank.set_slot(0, 1, item_id{2});
    EXPECT_TRUE(bank.is_full());
    EXPECT_FALSE(bank.find_empty_slot().has_value());
}
```

**Step 2: Implement paginated bank_storage**

Replace the existing `bank_storage` in `inventory.h`:

```cpp
class bank_storage
{
public:
    struct bank_location
    {
        int16_t page{0};
        int16_t slot{0};
    };

    explicit bank_storage(int16_t pages = 4, int16_t slots_per_page = 12)
        : pages_(pages), slots_per_page_(slots_per_page)
    {
        data_.resize(static_cast<size_t>(pages * slots_per_page));
    }

    [[nodiscard]] auto get_slot(int16_t page, int16_t slot) const -> std::optional<item_id>
    {
        auto idx = index(page, slot);
        if (!idx.has_value() || !data_[*idx].is_valid())
            return std::nullopt;
        return data_[*idx];
    }

    void set_slot(int16_t page, int16_t slot, item_id id)
    {
        auto idx = index(page, slot);
        if (idx.has_value())
            data_[*idx] = id;
    }

    void clear_slot(int16_t page, int16_t slot)
    {
        auto idx = index(page, slot);
        if (idx.has_value())
            data_[*idx] = item_id{};
    }

    [[nodiscard]] auto find_item(item_id id) const -> std::optional<bank_location>
    {
        for (int16_t p = 0; p < pages_; ++p)
        {
            for (int16_t s = 0; s < slots_per_page_; ++s)
            {
                auto idx = static_cast<size_t>(p * slots_per_page_ + s);
                if (data_[idx] == id)
                    return bank_location{p, s};
            }
        }
        return std::nullopt;
    }

    [[nodiscard]] auto find_empty_slot() const -> std::optional<bank_location>
    {
        for (int16_t p = 0; p < pages_; ++p)
        {
            for (int16_t s = 0; s < slots_per_page_; ++s)
            {
                auto idx = static_cast<size_t>(p * slots_per_page_ + s);
                if (!data_[idx].is_valid())
                    return bank_location{p, s};
            }
        }
        return std::nullopt;
    }

    [[nodiscard]] auto is_full() const -> bool { return !find_empty_slot().has_value(); }
    [[nodiscard]] auto total_pages() const -> int16_t { return pages_; }
    [[nodiscard]] auto slots_per_page() const -> int16_t { return slots_per_page_; }

    void clear_all()
    {
        for (auto& id : data_)
            id = item_id{};
    }

private:
    [[nodiscard]] auto index(int16_t page, int16_t slot) const -> std::optional<size_t>
    {
        if (page < 0 || page >= pages_ || slot < 0 || slot >= slots_per_page_)
            return std::nullopt;
        return static_cast<size_t>(page * slots_per_page_ + slot);
    }

    int16_t pages_;
    int16_t slots_per_page_;
    std::vector<item_id> data_;
};
```

**Step 3: Build, fix bank_storage API changes**

Run: `cmake --build build --config Debug -j8`

Update all `bank_storage` callers (inventory_system, auth_handlers, admin_web_handlers) from flat index to page+slot API.

**Step 4: Run tests**

Run: `./bin/hgserver_tests`
Expected: All tests pass.

---

### Task 5: Update trade_window (3-phase with lock)

Add the lock phase to trading.

**Files:**
- Modify: `src/inventory/inventory.h` (trade_window class)

**Step 1: Rewrite trade_window**

Replace the current slot-based trade_window with item_id list + lock phase:

```cpp
class trade_window
{
public:
    auto add_item(item_id id) -> bool
    {
        if (locked_ || items_.size() >= max_trade_items)
            return false;
        if (has_item(id))
            return false;
        items_.push_back(id);
        return true;
    }

    auto remove_item(item_id id) -> bool
    {
        if (locked_)
            return false;
        auto it = std::find(items_.begin(), items_.end(), id);
        if (it == items_.end())
            return false;
        items_.erase(it);
        return true;
    }

    void set_gold(int64_t amount)
    {
        if (!locked_)
            gold_ = std::max<int64_t>(0, amount);
    }

    void lock() { locked_ = true; }
    void confirm() { confirmed_ = true; }

    void reset()
    {
        items_.clear();
        gold_ = 0;
        locked_ = false;
        confirmed_ = false;
    }

    [[nodiscard]] auto items() const -> std::span<const item_id> { return items_; }
    [[nodiscard]] auto gold() const -> int64_t { return gold_; }
    [[nodiscard]] auto is_locked() const -> bool { return locked_; }
    [[nodiscard]] auto is_confirmed() const -> bool { return confirmed_; }
    [[nodiscard]] auto has_item(item_id id) const -> bool
    {
        return std::find(items_.begin(), items_.end(), id) != items_.end();
    }

    static constexpr size_t max_trade_items = 12;

private:
    std::vector<item_id> items_;
    int64_t gold_{0};
    bool locked_{false};
    bool confirmed_{false};
};
```

**Step 2: Build and fix compilation errors**

Run: `cmake --build build --config Debug -j8`

**Step 3: Run tests**

Run: `./bin/hgserver_tests`
Expected: All tests pass.

---

### Task 6: item_ops result types

Create the operation result structs that the ops layer and handlers will use.

**Files:**
- Create: `src/item/item_ops_types.h`

**Step 1: Create result types header**

```cpp
#pragma once

// item_ops_types.h
// Result types for item operations layer

#include "core/types.h"
#include "item/item.h"
#include "player/equipment.h"

#include <optional>
#include <string>
#include <vector>

namespace hb::item_ops
{

// Force unequip reasons
enum class force_unequip_reason : uint8_t
{
    broken = 0,
    hammer_strip = 1,
    armor_break = 2,
};

struct pickup_result
{
    bool success{false};
    std::string error;
    item_id picked_up{};
    int16_t pos_x{0};
    int16_t pos_y{0};
    int32_t z_order{0};
    int32_t new_weight{0};
    int32_t max_weight{0};
};

struct drop_result
{
    bool success{false};
    std::string error;
    item_id dropped{};
    bool was_equipped{false};
    std::optional<player::equip_slot> unequipped_slot{};
};

struct equip_result
{
    bool success{false};
    std::string error;
    player::equip_slot slot{};
    item_id equipped{};
    std::optional<item_id> swapped_out{};
};

struct unequip_result
{
    bool success{false};
    std::string error;
    player::equip_slot slot{};
    item_id unequipped{};
};

struct force_unequip_result
{
    bool success{false};
    player::equip_slot slot{};
    item_id unequipped{};
    force_unequip_reason reason{};
};

struct use_item_result
{
    bool success{false};
    std::string error;
    item_id used{};
    bool item_consumed{false};
};

struct shop_buy_result
{
    bool success{false};
    std::string error;
    item_id created{};
    int16_t pos_x{0};
    int16_t pos_y{0};
    int32_t z_order{0};
    int64_t new_gold{0};
    int32_t new_weight{0};
    int32_t max_weight{0};
};

struct shop_sell_result
{
    bool success{false};
    std::string error;
    item_id sold{};
    int64_t new_gold{0};
    int32_t new_weight{0};
    int32_t max_weight{0};
};

struct shop_repair_result
{
    bool success{false};
    std::string error;
    item_id repaired{};
    int64_t new_gold{0};
};

struct bank_deposit_result
{
    bool success{false};
    std::string error;
    item_id deposited{};
    int16_t page{0};
    int16_t slot{0};
    int32_t new_weight{0};
    int32_t max_weight{0};
};

struct bank_withdraw_result
{
    bool success{false};
    std::string error;
    item_id withdrawn{};
    int16_t pos_x{0};
    int16_t pos_y{0};
    int32_t z_order{0};
    int32_t new_weight{0};
    int32_t max_weight{0};
};

struct upgrade_result
{
    bool success{false};
    std::string error;
    item_id target{};
    item_id stone{};
};

struct activate_ability_result
{
    bool success{false};
    std::string error;
    item::special_ability_type ability{};
    int32_t duration_ms{0};
};

struct damage_equipment_result
{
    bool success{false};
    item_id damaged{};
    int16_t new_durability{0};
    bool broke{false};
    player::equip_slot slot{};
};

struct inventory_placement
{
    item_id item{};
    int16_t pos_x{0};
    int16_t pos_y{0};
    int32_t z_order{0};
};

struct trade_result
{
    bool success{false};
    std::string error;

    struct player_side
    {
        std::vector<item_id> received_items;
        std::vector<item_id> lost_items;
        int64_t gold_change{0};
        int32_t new_weight{0};
        int32_t max_weight{0};
        std::vector<inventory_placement> placements;
    };

    player_side player_a;
    player_side player_b;
};

struct give_result
{
    bool success{false};
    std::string error;
    item_id created{};
    int16_t pos_x{0};
    int16_t pos_y{0};
    int32_t z_order{0};
};

struct loot_drop_result
{
    item_id dropped{};
    int16_t ground_x{0};
    int16_t ground_y{0};
};

} // namespace hb::item_ops
```

**Step 2: Build to verify it compiles**

Run: `cmake --build build --config Debug -j8`
Expected: Clean compile (header-only, no callers yet).

---

## Phase 2: Protocol Messages

Define all the new v2 protocol messages. Add them alongside existing messages — don't remove old ones yet.

---

### Task 7: Universal item serialization

Create a function that serializes an `item` instance to the universal JSON shape from the protocol spec.

**Files:**
- Modify: `src/network/json_protocol.h` (add new message type enums + data structs)
- Modify: `src/network/json_protocol.cpp` (add serialization)
- Create: `tests/test_item_protocol_v2.cpp`

**Step 1: Write test for item serialization**

```cpp
TEST(item_protocol_v2, serialize_universal_item)
{
    item::item itm;
    itm.id = item_id{100};
    itm.template_id = item_id{50};
    itm.name = "Test Sword";
    itm.type = item::item_type::weapon;
    itm.equip_position = item::equip_pos::weapon;
    itm.weapon = item::weapon_type::sword;
    itm.count = 1;
    itm.weight = 800;
    itm.price = 15000;
    itm.attack_power = 42;
    itm.durability = 85;
    itm.max_durability = 100;
    itm.tradeable = true;
    itm.droppable = true;

    auto json = network::serialize_item(itm);

    EXPECT_EQ(json["item_id"], 100);
    EXPECT_EQ(json["template_id"], 50);
    EXPECT_EQ(json["name"], "Test Sword");
    EXPECT_EQ(json["type"], "weapon");
    EXPECT_EQ(json["equip_pos"], "weapon");
    EXPECT_EQ(json["weapon_type"], "sword");
    EXPECT_EQ(json["count"], 1);
    EXPECT_EQ(json["weight"], 800);
    EXPECT_EQ(json["attack_power"], 42);
    EXPECT_EQ(json["durability"], 85);
    EXPECT_EQ(json["max_durability"], 100);
    EXPECT_TRUE(json["tradeable"].get<bool>());
}
```

**Step 2: Implement `serialize_item()` and string enum converters**

Add `item_type_to_string()`, `equip_pos_to_string()`, `weapon_type_to_string()` converters. Add `serialize_item(const item::item&) -> nlohmann::json`.

Refer to `docs/protocol/items-v2.md` Section 1 for the exact field list.

**Step 3: Build and run tests**

Run: `cmake --build build --config Debug -j8 && ./bin/hgserver_tests --gtest_filter="item_protocol_v2.*"`

---

### Task 8: State update messages

Add the v2 state update channel messages.

**Files:**
- Modify: `src/network/json_protocol.h`
- Modify: `src/network/json_protocol.cpp`
- Extend: `tests/test_item_protocol_v2.cpp`

Add enum entries and builder functions for:
- `inventory_item_add` — item + pos_x + pos_y + z_order
- `inventory_item_update` — same shape as add (full item resend)
- `inventory_item_removed` — item_id only
- `inventory_item_delta` — item_id + optional count + optional durability
- `inventory_gold_update` — gold amount
- `inventory_weight_update` — weight + max_weight (already exists, verify shape matches spec)
- `force_unequip` — slot (string) + reason (string)
- `equipment_change` — entity_id + slot + item (or null)
- `ground_item_spawn` — item + map + x + y
- `ground_item_removed` — item_id + map + x + y
- `bank_slot_update` — page + slot + item
- `bank_slot_cleared` — page + slot
- `ability_activated` — entity_id + ability_type + duration_ms
- `ability_expired` — entity_id + ability_type

Each message needs:
1. Enum entry in `json_message_type`
2. `to_string()` case
3. Type map entry
4. Data struct
5. `to_json()` function
6. Builder function (`make_*`)

Refer to `docs/protocol/items-v2.md` Section 3 for exact JSON shapes.

**Write tests for each builder function** verifying the JSON output matches the spec.

---

### Task 9: Action messages (acknowledgment-only)

Add all the v2 action request/result message pairs.

**Files:**
- Modify: `src/network/json_protocol.h`
- Modify: `src/network/json_protocol.cpp`

For each action pair, the result is just `{success: bool}` (some include a `slot` field). Add:
- `pickup_request` / `pickup_result`
- `drop_request` / `drop_result`
- `equip_request` / `equip_result` (includes slot)
- `unequip_request` / `unequip_result` (includes slot)
- `use_item_request` / `use_item_result`
- `upgrade_request` / `upgrade_result`
- `shop_buy_request` / `shop_buy_result`
- `shop_sell_request` / `shop_sell_result`
- `shop_repair_request` / `shop_repair_result`
- `bank_deposit_request` / `bank_deposit_result`
- `bank_withdraw_request` / `bank_withdraw_result`
- `bank_reposition_request` / `bank_reposition_result`
- `activate_ability_request` / `activate_ability_failed`
- `inventory_reposition` (no response)

Refer to `docs/protocol/items-v2.md` Sections 4-12 for exact JSON shapes of each request.

**Note:** Many of these replace existing messages with the same name. Use `_v2` suffix internally if needed during transition, or replace in-place if the old handlers are being rewritten simultaneously.

---

### Task 10: Trade messages

Add the v2 trading protocol messages (3-phase).

**Files:**
- Modify: `src/network/json_protocol.h`
- Modify: `src/network/json_protocol.cpp`

Messages:
- `trade_request`, `trade_invite`, `trade_accept`, `trade_decline`
- `trade_opened`
- `trade_add_item`, `trade_remove_item`, `trade_set_gold`
- `trade_update` (side + items[] + gold)
- `trade_lock`, `trade_lock_status`
- `trade_confirm`, `trade_complete`
- `trade_cancel`, `trade_canceled`

Refer to `docs/protocol/items-v2.md` Section 9 for exact JSON shapes.

---

### Task 11: Inventory data message (login payload)

Add the combined `inventory_data` message for login.

**Files:**
- Modify: `src/network/json_protocol.h`
- Modify: `src/network/json_protocol.cpp`

```json
{
  "type": "inventory_data",
  "data": {
    "items": [{"item": {...}, "pos_x": 30, "pos_y": 40, "z_order": 0}],
    "equipment_slots": {"weapon": 12345, "body": 12350},
    "gold": 50000,
    "weight": 3200,
    "max_weight": 5500
  }
}
```

This replaces the current separate `inventory_data` + `equipment_data` messages with a single combined message using the universal item shape.

---

### Task 12: Shop and bank open messages

Add `shop_open` and `bank_open` messages.

**Files:**
- Modify: `src/network/json_protocol.h`
- Modify: `src/network/json_protocol.cpp`

`shop_open`: npc_name + shop_type + items[] (each: universal item + buy_price)
`bank_open`: pages[] (each: page_num + slots[] of item or null) + total_pages

Refer to `docs/protocol/items-v2.md` Sections 7-8.

---

### Task 13: Party loot messages

Add party loot distribution messages.

**Files:**
- Modify: `src/network/json_protocol.h`
- Modify: `src/network/json_protocol.cpp`

Messages:
- `set_loot_rule`, `loot_rule_changed`
- `loot_available` (loot_id + items[] + source location + rule + timeout)
- `loot_roll`, `loot_roll_result`, `loot_pass`
- `loot_assign`, `loot_awarded`
- `loot_expired`

Refer to `docs/protocol/items-v2.md` Section 13.

---

## Phase 3: Item Operations Layer

The business logic layer. Each operation validates, mutates state, and returns a result struct.

---

### Task 14: Create item_ops namespace with pickup and drop

**Files:**
- Create: `src/item/item_ops.h`
- Create: `src/item/item_ops.cpp`
- Create: `tests/test_item_ops.cpp`

**Step 1: Write tests for pickup_item**

Test cases:
- Successful pickup: item removed from ground, added to inventory, weight updated
- Pickup with full inventory: returns failure
- Pickup with weight limit: returns failure
- Pickup consumable: placed at last consumable position
- Pickup non-consumable: placed at (30, 40)

**Step 2: Write tests for drop_item**

Test cases:
- Successful drop: item removed from inventory, placed on ground
- Drop equipped item: auto-unequips first, then drops
- Drop non-droppable item: returns failure
- Drop item not owned: returns failure

**Step 3: Implement pickup_item and drop_item**

Each function takes the subsystem pointers it needs:

```cpp
namespace hb::item_ops
{

auto pickup_item(
    entity_id player,
    map_id map,
    const world::position& pos,
    item::item_system* items,
    inventory::inventory_system* inv,
    world::world_subsystem* world
) -> pickup_result;

auto drop_item(
    entity_id player,
    item_id id,
    map_id map,
    const world::position& player_pos,
    item::item_system* items,
    inventory::inventory_system* inv,
    world::world_subsystem* world
) -> drop_result;

}
```

**Step 4: Build and run tests**

---

### Task 15: Equip, unequip, force_unequip operations

**Files:**
- Modify: `src/item/item_ops.h`
- Modify: `src/item/item_ops.cpp`
- Extend: `tests/test_item_ops.cpp`

Test cases for equip:
- Successful equip: slot reference set, stat recalc needed
- Equip with swap: old item unequipped, new item equipped
- Equip broken item: returns failure
- Equip wrong slot: returns failure
- Equip without meeting requirements: returns failure

Test cases for force_unequip:
- Successful force unequip: slot cleared
- Force unequip empty slot: returns failure
- Correct reason propagated (broken, hammer_strip, armor_break)

---

### Task 16: Shop operations (buy, sell, repair)

**Files:**
- Modify: `src/item/item_ops.h`
- Modify: `src/item/item_ops.cpp`
- Extend: `tests/test_item_ops.cpp`

Test cases for shop_buy:
- Successful buy: item created, gold deducted, weight updated
- Buy without gold: returns failure
- Buy with full inventory: returns failure
- Buy over weight limit: returns failure

Test cases for shop_sell:
- Successful sell: item removed, gold added
- Sell non-tradeable: returns failure

Test cases for shop_repair:
- Successful repair: durability restored, gold deducted
- Repair undamaged item: returns failure (or no-op success)

---

### Task 17: Bank operations (deposit, withdraw, reposition)

**Files:**
- Modify: `src/item/item_ops.h`
- Modify: `src/item/item_ops.cpp`
- Extend: `tests/test_item_ops.cpp`

Test cases for deposit:
- Auto-deposit (no page/slot): server picks first empty
- Targeted deposit: places in specified slot
- Deposit equipped item: auto-unequips first
- Deposit to full bank: returns failure

Test cases for withdraw:
- Successful withdraw: removed from bank, added to inventory
- Withdraw with full inventory: returns failure
- Withdraw over weight limit: returns failure

Test cases for bank reposition:
- Move item to empty slot
- Swap two items

---

### Task 18: Trade operations

**Files:**
- Modify: `src/item/item_ops.h`
- Modify: `src/item/item_ops.cpp`
- Extend: `tests/test_item_ops.cpp`

Test cases for execute_trade:
- Both players exchange items and gold successfully
- Items transferred correctly (A's items go to B, B's to A)
- Gold transferred correctly
- Weight updated for both players
- Inventory placement calculated for received items

---

### Task 19: Upgrade and ability operations

**Files:**
- Modify: `src/item/item_ops.h`
- Modify: `src/item/item_ops.cpp`
- Extend: `tests/test_item_ops.cpp`

Test cases for upgrade:
- Successful upgrade: stone consumed, item level increased
- Failed upgrade: stone consumed, item unchanged
- Wrong stone type: returns failure

Test cases for activate_ability:
- Successful activation: ability started, duration returned
- On cooldown: returns failure
- Item not equipped: returns failure
- Item has no ability: returns failure

---

### Task 20: Loot and give operations

**Files:**
- Modify: `src/item/item_ops.h`
- Modify: `src/item/item_ops.cpp`
- Extend: `tests/test_item_ops.cpp`

Test cases for drop_loot:
- Item created and placed on ground at position
- Returns item_id for broadcasting

Test cases for drop_loot_multi:
- Multiple items placed in 3x3 grid
- Unwalkable tiles overflow to nearest walkable
- All items always placed (none lost)

Test cases for give_item:
- Item created and added to player inventory
- Placement at default position

Test cases for damage_equipment:
- Durability reduced, not broken
- Durability hits 0: broke=true, triggers force unequip

---

## Phase 4: Persistence

Update the database schema and load/save flow.

---

### Task 21: Database migration

**Files:**
- Create: `tools/migrate/migrations/YYYYMMDD_HHMMSS_item_system_v2.sql`
- Modify: `src/database/schema.sql`

**Step 1: Create migration**

Run: `cd tools/migrate && npx tsx migrate.ts create item_system_v2`

**Step 2: Write migration SQL**

```sql
-- up
-- Add bank pagination columns
ALTER TABLE items ADD COLUMN IF NOT EXISTS bank_page SMALLINT;
ALTER TABLE items ADD COLUMN IF NOT EXISTS bank_slot SMALLINT;

-- Create character_equipment table
CREATE TABLE IF NOT EXISTS character_equipment (
    character_id BIGINT NOT NULL REFERENCES characters(id) ON DELETE CASCADE,
    slot SMALLINT NOT NULL,
    item_id BIGINT NOT NULL REFERENCES items(id) ON DELETE CASCADE,
    PRIMARY KEY (character_id, slot)
);

-- Migrate existing equipment data:
-- Items with location=1 (equipment) and equip_slot set need to be
-- inserted into character_equipment and their location changed to 0 (inventory)
INSERT INTO character_equipment (character_id, slot, item_id)
SELECT character_id, equip_slot, id
FROM items
WHERE location = 1 AND equip_slot IS NOT NULL
ON CONFLICT DO NOTHING;

-- Move equipment items back to inventory location
UPDATE items SET location = 0 WHERE location = 1;

-- Migrate bank items: set bank_page/bank_slot from slot column
UPDATE items SET bank_page = 0, bank_slot = slot WHERE location = 2;

-- down
DROP TABLE IF EXISTS character_equipment;
ALTER TABLE items DROP COLUMN IF EXISTS bank_page;
ALTER TABLE items DROP COLUMN IF EXISTS bank_slot;
```

**Step 3: Update schema.sql to match**

Add the `character_equipment` table and `bank_page`/`bank_slot` columns to the canonical schema.

**Step 4: Run migration**

Run: `cd tools/migrate && npx tsx migrate.ts migrate`

---

### Task 22: Update item_row and load/save

**Files:**
- Modify: `src/auth/account.h` (item_row struct)
- Modify: `src/auth/auth_system.cpp` (load_items, save_items queries)

Update `item_row` to match new schema:
- Remove `name` field (derived from template)
- Remove `equip_slot` field (now in character_equipment table)
- Add `bank_page`, `bank_slot` fields
- Simplify `item_location` enum to just `inventory = 0, bank = 1`

Add `load_equipment(player_id) -> vector<pair<equip_slot, item_id>>` query.
Add `save_equipment(player_id, equipment_state)` query.

Update `load_items` SELECT to include `bank_page`, `bank_slot`.
Update `save_items` INSERT/UPSERT to include `bank_page`, `bank_slot`.

---

### Task 23: Update auth_handlers load/save flow

**Files:**
- Modify: `src/bridge/handlers/auth_handlers.cpp`

Update the enter-game flow:
1. Load items from DB
2. For each item: `item_system.restore_item()`, add to inventory or bank based on location
3. Load equipment from DB → set `equipment_state` slot references
4. Calculate weight
5. Send single `inventory_data` message (replaces separate inventory + equipment messages)

Update the logout/save flow:
1. Compact z_order
2. Build item_rows from inventory + bank
3. Save equipment to `character_equipment` table
4. Upsert items

---

## Phase 5: Handler Rewrite

Replace existing handlers with thin routing that calls item_ops.

---

### Task 24: Rewrite equipment handlers

**Files:**
- Modify: `src/bridge/handlers/game_handlers_equipment.cpp`

Replace equip/unequip handler logic with:
```
parse request → call item_ops::equip_item() → send equip_result ack
    → if success: send equipment_change broadcast, stat updates
```

Replace use_item handler:
```
parse request → call item_ops::use_item() → send use_item_result ack
    → if consumed: send inventory_item_removed
    → apply effects via existing stat messages
```

---

### Task 25: Rewrite pickup/drop handlers

**Files:**
- Modify: `src/bridge/handlers/game_handlers_shop.cpp` (pickup/drop live here)

Replace with:
```
pickup: parse → item_ops::pickup_item() → pickup_result ack
    → inventory_item_add + weight_update + ground_item_removed broadcast

drop: parse → item_ops::drop_item() → drop_result ack
    → inventory_item_removed + weight_update + ground_item_spawn broadcast
    → if was equipped: force_unequip + equipment_change broadcast
```

---

### Task 26: Rewrite shop handlers

**Files:**
- Modify: `src/bridge/handlers/game_handlers_shop.cpp`

Replace buy/sell/repair with thin ops calls. Each follows the pattern:
```
parse → item_ops::shop_*() → *_result ack
    → state update messages (item_add/removed, gold_update, weight_update)
```

---

### Task 27: Rewrite bank handlers

**Files:**
- Modify: `src/bridge/handlers/game_handlers_shop.cpp` (bank handlers live here)

Replace with:
```
deposit: parse → item_ops::bank_deposit() → ack
    → inventory_item_removed + weight_update + bank_slot_update

withdraw: parse → item_ops::bank_withdraw() → ack
    → bank_slot_cleared + inventory_item_add + weight_update

reposition: parse → item_ops::bank_reposition() → ack
    → bank_slot_update(s) + bank_slot_cleared (if not swap)
```

Add `bank_open` handler: when player interacts with bank NPC, serialize full bank contents and send `bank_open` message.

---

### Task 28: Rewrite trade handlers

**Files:**
- Modify: `src/bridge/handlers/game_handlers.cpp` (trade handlers)

Implement the 3-phase flow:
- `trade_request` → `trade_invite` to target
- `trade_accept` → `trade_opened` to both
- `trade_add_item` / `trade_remove_item` / `trade_set_gold` → `trade_update` to both
- `trade_lock` → `trade_lock_status` to both
- `trade_confirm` → if both confirmed: `item_ops::execute_trade()` → `trade_complete` + state updates
- `trade_cancel` → `trade_canceled` to both

---

### Task 29: Loot drop handlers

**Files:**
- Modify: `src/bridge/handlers/game_handlers_npc.cpp`

Update NPC death handler to call `item_ops::drop_loot()` / `drop_loot_multi()` instead of inline loot logic. Broadcast `ground_item_spawn` for each dropped item.

---

## Phase 6: Integration & Cleanup

---

### Task 30: Update application.cpp wiring

**Files:**
- Modify: `src/application.cpp`

Ensure:
- `inventory_system` manages `equipment_state` per player
- New message types routed to handlers
- `item_ops` functions can access all needed subsystems

---

### Task 31: Cross-validation at startup

**Files:**
- Modify: `src/application.cpp` (or create `src/registry/registry_validator.cpp`)

After all registries load, validate:
- Every item_id in loot_tables.yaml exists in item_registry
- Every item_id in shop configs exists in item_registry
- Log warnings for invalid references, skip bad entries

---

### Task 32: Ground item lifetime enhancement

**Files:**
- Modify: `src/world/world_subsystem.h` (ground_item_entry)
- Modify: `src/world/world_subsystem.cpp`
- Modify: `src/registry/item_template.h` (add ground_lifetime_ms field)

Update `ground_item_entry` to include per-item lifetime (from template or default). Update `remove_expired_ground_items()` to check per-item lifetime instead of global max_age.

---

### Task 33: Remove old protocol messages and dead code

**Files:**
- Modify: `src/network/json_protocol.h`
- Modify: `src/network/json_protocol.cpp`

Remove old v1 item messages that are fully replaced:
- Old `inventory_data` / `equipment_data` (replaced by combined `inventory_data`)
- Old `inventory_slot_update` (replaced by `inventory_item_add` / `inventory_item_update`)
- Old equip/unequip messages with slot-based payloads
- Old shop messages with embedded state data
- Other deprecated message types

Remove the `equipped_item` struct from equipment.h if not already removed.
Remove `equipped_as` handling from persistence code.

---

### Task 34: Update tests

**Files:**
- Modify: `tests/test_inventory.cpp`
- Modify: `tests/test_item.cpp`
- Modify: `tests/test_json_protocol.cpp`
- Modify: `tests/test_item_persistence.cpp`
- Modify: all other item-related test files

Update existing tests to work with new APIs:
- Inventory tests: remove `equipped_as` references
- Protocol tests: update to new message shapes
- Persistence tests: update to new load/save flow with `character_equipment` table
- Equipment tests: update to linked model

---

### Task 35: Update documentation

**Files:**
- Modify: `docs/PROGRESS.md`
- Modify: `docs/JSON_PROTOCOL.md`
- Modify: `docs/protocol/items.md` (point to items-v2.md or merge)

Update PROGRESS.md with item system v2 completion status.
Update JSON_PROTOCOL.md to reference the v2 messages.
Archive or remove the old items protocol doc.

---

## Execution Notes

### Build & Test Commands

```bash
# Build
cmake --build build --config Debug -j8

# Run all tests
./bin/hgserver_tests

# Run specific test suite
./bin/hgserver_tests --gtest_filter="equipment_state_test.*"
./bin/hgserver_tests --gtest_filter="bank_storage_test.*"
./bin/hgserver_tests --gtest_filter="item_ops_test.*"
./bin/hgserver_tests --gtest_filter="item_protocol_v2.*"
```

### Key Files Reference

| File | Purpose |
|------|---------|
| `docs/plans/2026-02-22-item-system-redesign.md` | Architecture decisions |
| `docs/protocol/items-v2.md` | Protocol spec (client contract) |
| `src/item/item.h` | Item instance struct |
| `src/item/item_system.h` | Item lifecycle subsystem |
| `src/item/item_ops.h` | Operations layer (NEW) |
| `src/item/item_ops_types.h` | Operation result types (NEW) |
| `src/inventory/inventory.h` | Inventory, bank, trade data structures |
| `src/inventory/inventory_system.h` | Container lifecycle management |
| `src/player/equipment.h` | Equipment slot references |
| `src/network/json_protocol.h` | Protocol message definitions |
| `src/bridge/handlers/` | Message handlers |
| `src/auth/account.h` | DB row structs |
| `src/auth/auth_system.cpp` | Item persistence queries |

### Dependencies Between Tasks

```
Phase 1 (Tasks 1-6): No dependencies, can be done in any order
Phase 2 (Tasks 7-13): Depends on Phase 1 (needs updated enums and types)
Phase 3 (Tasks 14-20): Depends on Phase 1 (needs result types and data structures)
Phase 4 (Tasks 21-23): Depends on Phase 1 (needs updated data structures)
Phase 5 (Tasks 24-29): Depends on Phases 2+3 (needs protocol messages and ops layer)
Phase 6 (Tasks 30-35): Depends on everything
```

Phases 2, 3, and 4 can be worked in parallel after Phase 1 completes.
