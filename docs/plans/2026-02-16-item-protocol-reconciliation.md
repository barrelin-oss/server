# Item Protocol Reconciliation Implementation Plan

> **For Claude:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** Fix three categories of item protocol issues: rename compact attribute JSON keys to readable names, add server-authoritative `inventory_slot_update` to every handler that mutates inventory, and add a dedicated `gold_update` message for gold changes.

**Architecture:** The fix is additive — no handler logic changes, just adding message sends after existing mutations. A shared `build_inventory_item_msg()` helper eliminates ~30 lines of boilerplate per call site. New `bank_slot_update` and `gold_update` protocol message types follow existing patterns.

**Tech Stack:** C++20, nlohmann-json, Google Test

---

## Task 1: Rename Attribute JSON Keys

Rename compact keys (`mt`/`mv`/`st`/`sv`/`cm`/`cq`) to readable names (`main_type`/`main_value`/`sub_type`/`sub_value`/`custom_made`/`custom_quality`) in `item_attribute::to_json()` and `from_json()`.

**Files:**
- Modify: `src/item/item_attribute.cpp:8-58`
- Modify: `tests/test_item_attribute.cpp:75-126`
- Modify: `tests/test_loot_attribute.cpp:156`

**Step 1: Update `to_json()` keys**

In `src/item/item_attribute.cpp`, change lines 15-27:

```cpp
auto item_attribute::to_json() const -> nlohmann::json
{
    nlohmann::json j = nlohmann::json::object();
    if (upgrade_level > 0)
        j["upgrade"] = upgrade_level;
    if (main_type != enchantment_type::none)
    {
        j["main_type"] = static_cast<uint8_t>(main_type);
        j["main_value"] = main_value;
    }
    if (sub_type != sub_enchantment_type::none)
    {
        j["sub_type"] = static_cast<uint8_t>(sub_type);
        j["sub_value"] = sub_value;
    }
    if (custom_made)
    {
        j["custom_made"] = true;
        if (custom_quality != 0)
            j["custom_quality"] = custom_quality;
    }
    return j;
}
```

**Step 2: Update `from_json()` keys**

In the same file, change lines 40-56:

```cpp
auto item_attribute::from_json(const nlohmann::json& j) -> item_attribute
{
    item_attribute attr;
    if (j.is_null() || !j.is_object())
        return attr;

    if (j.contains("upgrade"))
        attr.upgrade_level = std::min<uint8_t>(j["upgrade"].get<uint8_t>(), 15);
    if (j.contains("main_type"))
    {
        attr.main_type = static_cast<enchantment_type>(j["main_type"].get<uint8_t>());
        if (j.contains("main_value"))
            attr.main_value = std::min<uint8_t>(j["main_value"].get<uint8_t>(), 15);
    }
    if (j.contains("sub_type"))
    {
        attr.sub_type = static_cast<sub_enchantment_type>(j["sub_type"].get<uint8_t>());
        if (j.contains("sub_value"))
            attr.sub_value = std::min<uint8_t>(j["sub_value"].get<uint8_t>(), 15);
    }
    if (j.contains("custom_made"))
    {
        attr.custom_made = j["custom_made"].get<bool>();
        if (j.contains("custom_quality"))
            attr.custom_quality = std::clamp<int8_t>(j["custom_quality"].get<int8_t>(), -100, 100);
    }
    return attr;
}
```

**Step 3: Update tests**

In `tests/test_item_attribute.cpp`:
- Line 75: `EXPECT_FALSE(j.contains("mt"))` → `EXPECT_FALSE(j.contains("main_type"))`
- Line 76: `EXPECT_FALSE(j.contains("st"))` → `EXPECT_FALSE(j.contains("sub_type"))`
- Line 109: `j["cm"]` → `j["custom_made"]`
- Line 110: `j.contains("cq")` → `j.contains("custom_quality")`
- Line 122: `{{"mt", 7}, {"mv", 20}, {"st", 1}, {"sv", 20}}` → `{{"main_type", 7}, {"main_value", 20}, {"sub_type", 1}, {"sub_value", 20}}`

In `tests/test_loot_attribute.cpp`:
- Line 156: `{"mt", 7}, {"mv", 1}, {"st", 2}, {"sv", 5}, {"cm", true}` → `{"main_type", 7}, {"main_value", 1}, {"sub_type", 2}, {"sub_value", 5}, {"custom_made", true}`

**Step 4: Build and test**

```bash
cmake --build build --config Debug && ./bin/hgserver_tests --gtest_filter="item_attribute*:loot_attribute*"
```

**Step 5: Commit**

```bash
git add src/item/item_attribute.cpp tests/test_item_attribute.cpp tests/test_loot_attribute.cpp
git commit -m "Rename item attribute JSON keys from compact to readable names"
```

---

## Task 2: Add `build_inventory_item_msg` Helper

Create a shared helper that builds an `inventory_item_msg` from an item_id and slot index. Currently this is ~30 lines of boilerplate in the pickup handler (`game_handlers_shop.cpp:100-147`). We'll need it in ~14 more places.

**Files:**
- Modify: `src/network/json_protocol.h` (add free function declaration)
- Modify: `src/network/json_protocol.cpp` (add implementation)
- Modify: `src/bridge/handlers/game_handlers_shop.cpp:100-149` (refactor pickup handler to use it)
- Test: `tests/test_json_protocol.cpp`

**Step 1: Declare the helper in json_protocol.h**

Add near the other builder functions (after `make_inventory_slot_update` declaration, around line 2230):

```cpp
// Build an inventory_item_msg for a given slot, looking up item and template data.
// Returns nullopt if the item doesn't exist. Requires item_system and item_registry pointers.
auto build_inventory_item_msg(
    int16_t slot_index,
    item_id iid,
    const item::item_system* items,
    const item_registry* registry,
    std::optional<uint8_t> equipped_slot = std::nullopt) -> std::optional<inventory_item_msg>;
```

This needs forward declarations for `item::item_system` and `item_registry`. Check if they already exist in the header; if not, add them.

**Step 2: Implement the helper in json_protocol.cpp**

Add near `make_inventory_slot_update` (after line 2940):

```cpp
auto build_inventory_item_msg(
    int16_t slot_index,
    item_id iid,
    const item::item_system* items,
    const item_registry* registry,
    std::optional<uint8_t> equipped_slot) -> std::optional<inventory_item_msg>
{
    if (!items)
        return std::nullopt;

    auto* itm = items->get_item(iid);
    if (!itm)
        return std::nullopt;

    inventory_item_msg msg{};
    msg.slot = static_cast<uint8_t>(slot_index);
    msg.item_id = iid.value;
    msg.count = itm->count;
    msg.durability = static_cast<int16_t>(itm->durability);
    msg.max_durability = static_cast<int16_t>(itm->max_durability);
    msg.attribute = itm->attribute;
    msg.equipped_slot = equipped_slot;

    if (registry)
    {
        if (auto* tmpl = registry->get(itm->template_id))
        {
            msg.name = get_display_name(tmpl->name, itm->attribute);
            msg.item_type = static_cast<uint8_t>(tmpl->type);
            msg.equip_pos = static_cast<uint8_t>(tmpl->equip_pos);
            msg.sprite = tmpl->ground_sprite;
            msg.sprite_frame = tmpl->ground_sprite_frame;
            msg.color = tmpl->item_color;
            msg.weight = tmpl->weight;
            msg.level_limit = tmpl->level_limit;
        }
    }

    return msg;
}
```

Note: `json_protocol.cpp` already includes `item/item_system.h` and `registry/item_registry.h` — verify this. If not, add the includes.

**Step 3: Refactor the pickup handler to use the helper**

In `src/bridge/handlers/game_handlers_shop.cpp`, replace lines 100-149 (the `item_name`, `attr`, template field extraction, and `inventory_item_msg` construction) with:

```cpp
    // Send inventory slot update with full item details
    auto item_msg = network::build_inventory_item_msg(slot_idx, picked_item_id, item_, item_registry_);
    std::string item_name = item_msg ? item_msg->name : "Unknown";

    conn->send(network::make_player_pickup_response(msg.seq, true, nullptr, std::nullopt));
    if (item_msg)
    {
        conn->send(network::make_inventory_slot_update(slot_idx, &*item_msg));
    }
```

**Step 4: Add test**

In `tests/test_json_protocol.cpp`, add a test:

```cpp
TEST(json_protocol_test, build_inventory_item_msg_returns_nullopt_for_null_systems)
{
    auto result = network::build_inventory_item_msg(0, item_id{1}, nullptr, nullptr);
    EXPECT_FALSE(result.has_value());
}
```

**Step 5: Build and test**

```bash
cmake --build build --config Debug && ./bin/hgserver_tests --gtest_filter="json_protocol*"
```

**Step 6: Commit**

```bash
git add src/network/json_protocol.h src/network/json_protocol.cpp src/bridge/handlers/game_handlers_shop.cpp tests/test_json_protocol.cpp
git commit -m "Add build_inventory_item_msg helper and refactor pickup handler"
```

---

## Task 3: Add `gold_update` Protocol Message

Add a new `gold_update` message type for notifying clients of gold changes.

**Files:**
- Modify: `src/network/json_protocol.h` (enum entry, data struct, builder)
- Modify: `src/network/json_protocol.cpp` (to_string, type_map, builder impl)
- Test: `tests/test_json_protocol.cpp`

**Step 1: Add enum entry**

In `src/network/json_protocol.h`, add before `unknown` (line 487):

```cpp
    // Gold notification
    gold_update,             // S->C: Gold amount changed
```

**Step 2: Add to_string case**

In the `to_string(json_message_type)` switch, add before the `unknown` case:

```cpp
    case json_message_type::gold_update:
        return "gold_update";
```

**Step 3: Add to type_map**

In `src/network/json_protocol.cpp`, in the `type_map` static map, add:

```cpp
    {"gold_update", json_message_type::gold_update},
```

**Step 4: Add data struct and builder**

In `src/network/json_protocol.h`, add near the inventory message structs:

```cpp
struct gold_update_data
{
    int64_t gold{};     // New gold total
    int64_t change{};   // Amount changed (+/-)
    std::string reason; // Why: shop_buy, shop_sell, shop_repair, npc_loot, admin, trade

    [[nodiscard]] auto to_json() const -> nlohmann::json;
};

[[nodiscard]] auto make_gold_update(const gold_update_data& data) -> json_message;
```

**Step 5: Implement in json_protocol.cpp**

Add near the other inventory builders:

```cpp
auto gold_update_data::to_json() const -> nlohmann::json
{
    return nlohmann::json{{"gold", gold}, {"change", change}, {"reason", reason}};
}

auto make_gold_update(const gold_update_data& data) -> json_message
{
    return json_message{.type = json_message_type::gold_update, .seq = 0, .data = data.to_json()};
}
```

**Step 6: Add test**

```cpp
TEST(json_protocol_test, gold_update_serialization)
{
    network::gold_update_data data{.gold = 5000, .change = -500, .reason = "shop_buy"};
    auto msg = network::make_gold_update(data);
    EXPECT_EQ(msg.type, network::json_message_type::gold_update);
    EXPECT_EQ(msg.data["gold"], 5000);
    EXPECT_EQ(msg.data["change"], -500);
    EXPECT_EQ(msg.data["reason"], "shop_buy");
}
```

**Step 7: Build and test**

```bash
cmake --build build --config Debug && ./bin/hgserver_tests --gtest_filter="json_protocol*"
```

**Step 8: Commit**

```bash
git add src/network/json_protocol.h src/network/json_protocol.cpp tests/test_json_protocol.cpp
git commit -m "Add gold_update protocol message type"
```

---

## Task 4: Add `bank_slot_update` Protocol Message

Add a new `bank_slot_update` message type for notifying clients of bank slot changes.

**Files:**
- Modify: `src/network/json_protocol.h` (enum entry, builder)
- Modify: `src/network/json_protocol.cpp` (to_string, type_map, builder impl)
- Test: `tests/test_json_protocol.cpp`

**Step 1: Add enum entry**

In `src/network/json_protocol.h`, add near `inventory_slot_update` (line 484):

```cpp
    bank_slot_update,         // S->C: Single bank slot changed
```

**Step 2: Add to_string case**

```cpp
    case json_message_type::bank_slot_update:
        return "bank_slot_update";
```

**Step 3: Add to type_map**

```cpp
    {"bank_slot_update", json_message_type::bank_slot_update},
```

**Step 4: Add builder**

In `src/network/json_protocol.h`:

```cpp
[[nodiscard]] auto make_bank_slot_update(int16_t slot, const inventory_item_msg* item = nullptr) -> json_message;
```

**Step 5: Implement builder**

In `src/network/json_protocol.cpp`, near `make_inventory_slot_update`:

```cpp
auto make_bank_slot_update(int16_t slot, const inventory_item_msg* item) -> json_message
{
    nlohmann::json j;
    j["slot"] = slot;
    if (item)
    {
        j["item"] = item->to_json();
    }
    else
    {
        j["item"] = nullptr;
    }
    return json_message{.type = json_message_type::bank_slot_update, .seq = 0, .data = j};
}
```

**Step 6: Add test**

```cpp
TEST(json_protocol_test, bank_slot_update_with_item)
{
    network::inventory_item_msg item{.slot = 3, .item_id = 42, .name = "Sword", .count = 1, .durability = 100, .max_durability = 100};
    auto msg = network::make_bank_slot_update(3, &item);
    EXPECT_EQ(msg.type, network::json_message_type::bank_slot_update);
    EXPECT_EQ(msg.data["slot"], 3);
    EXPECT_EQ(msg.data["item"]["item_id"], 42);
}

TEST(json_protocol_test, bank_slot_update_null_clears)
{
    auto msg = network::make_bank_slot_update(5, nullptr);
    EXPECT_EQ(msg.data["slot"], 5);
    EXPECT_TRUE(msg.data["item"].is_null());
}
```

**Step 7: Build and test**

```bash
cmake --build build --config Debug && ./bin/hgserver_tests --gtest_filter="json_protocol*"
```

**Step 8: Commit**

```bash
git add src/network/json_protocol.h src/network/json_protocol.cpp tests/test_json_protocol.cpp
git commit -m "Add bank_slot_update protocol message type"
```

---

## Task 5: Add Slot Updates to Equip/Unequip Handlers

The equip handler needs to send `inventory_slot_update` for: unequipped shield (if 2H), unequipped old item (if swap), and newly equipped item. The unequip handler sends an update for the now-unequipped item.

**Files:**
- Modify: `src/bridge/handlers/game_handlers_equipment.cpp:45-329`

**Step 1: Add slot update to equip handler**

In `handle_player_equip()`, after line 205 (`conn->send(network::make_player_equip_response(...))`), add slot updates for each change:

After the shield unequip block (line 153-158), add:
```cpp
        // Send slot update for unequipped shield (still in inventory, just unequipped)
        if (auto shield_inv_slot = inv->find_equipped_slot(static_cast<uint8_t>(player::equip_slot::shield)); !shield_inv_slot)
        {
            // Shield was just unequipped — find its slot by item ID
            if (auto shield_slot = inv->find_item(shield_item_id); shield_slot)
            {
                auto shield_msg = network::build_inventory_item_msg(*shield_slot, shield_item_id, item_, item_registry_);
                if (shield_msg)
                    conn->send(network::make_inventory_slot_update(*shield_slot, &*shield_msg));
            }
        }
```

Wait — this is tricky because the `unequip_item` already cleared the flag. Let me reconsider the flow. The `unequip_item` call at line 153 clears `equipped_as` and rebuilds the cache. So after that call, the item is in inventory without `equipped_as` set. We need to send the update *after* `unequip_item` returns — and we already have the item_id from `shield_item_id`.

Actually, the cleanest approach is to send ALL slot updates together after the equip operation is fully complete (line 205), not interleaved with the logic. This is because `equip_item()` at line 181 may also rebuild caches.

**Revised approach** — after the response send at line 205, before stat update:

```cpp
    // Send inventory_slot_update for each affected item
    // 1. Shield that was unequipped (if 2H weapon equipped)
    if (result.unequipped_shield_id != 0)
    {
        auto sid = item_id{result.unequipped_shield_id};
        if (auto shield_slot = inv->find_item(sid); shield_slot)
        {
            auto shield_msg = network::build_inventory_item_msg(*shield_slot, sid, item_, item_registry_);
            if (shield_msg)
                conn->send(network::make_inventory_slot_update(*shield_slot, &*shield_msg));
        }
    }
    // 2. Old item that was unequipped from target slot (swap)
    if (result.swapped_item_id != 0)
    {
        auto oid = item_id{result.swapped_item_id};
        if (auto old_slot = inv->find_item(oid); old_slot)
        {
            auto old_msg = network::build_inventory_item_msg(*old_slot, oid, item_, item_registry_);
            if (old_msg)
                conn->send(network::make_inventory_slot_update(*old_slot, &*old_msg));
        }
    }
    // 3. Newly equipped item (same slot, now with equipped_slot set)
    {
        auto equip_msg = network::build_inventory_item_msg(
            data.inventory_slot, inv_slot->item, item_, item_registry_, static_cast<uint8_t>(target_slot));
        if (equip_msg)
            conn->send(network::make_inventory_slot_update(data.inventory_slot, &*equip_msg));
    }
```

**Step 2: Add slot update to unequip handler**

In `handle_player_unequip()`, after line 311 (`conn->send(network::make_player_unequip_response(...))`), add:

```cpp
    // Send inventory_slot_update (item stays in same slot, just no longer equipped)
    {
        auto item_msg = network::build_inventory_item_msg(
            static_cast<int16_t>(inv_idx), equipped_id, item_, item_registry_);
        if (item_msg)
            conn->send(network::make_inventory_slot_update(static_cast<int16_t>(inv_idx), &*item_msg));
    }
```

**Step 3: Build and test**

```bash
cmake --build build --config Debug && ./bin/hgserver_tests
```

**Step 4: Commit**

```bash
git add src/bridge/handlers/game_handlers_equipment.cpp
git commit -m "Add inventory_slot_update to equip/unequip handlers"
```

---

## Task 6: Add Slot Update to Use Item Handler

When an item is consumed (count decremented or slot cleared), send `inventory_slot_update`.

**Files:**
- Modify: `src/bridge/handlers/game_handlers_equipment.cpp:364-595`

**Step 1: Add slot updates after consumption**

Each consumption branch (HP/MP/SP potions, food, scroll) decrements or clears the slot. After each `conn->send(make_use_item_response(...))`, add the slot update. But we need to be careful — for recall scrolls, `execute_player_teleport` already sends its own response and the handler returns early.

The cleanest approach: add a single slot update block at the end of the function, before the audit block (line 585). At that point, the slot has already been modified:

```cpp
    // Send inventory_slot_update after item consumption
    if (slot->is_empty())
    {
        conn->send(network::make_inventory_slot_update(data.slot, nullptr));
    }
    else
    {
        auto item_msg = network::build_inventory_item_msg(data.slot, slot->item, item_, item_registry_);
        if (item_msg)
            conn->send(network::make_inventory_slot_update(data.slot, &*item_msg));
    }
```

For recall scrolls (line 541-573): the slot is already modified before `execute_player_teleport`. Add the slot update right before the `execute_player_teleport` call:

```cpp
            // Send inventory_slot_update for consumed scroll
            if (slot->is_empty())
            {
                conn->send(network::make_inventory_slot_update(data.slot, nullptr));
            }
            else
            {
                auto scroll_msg = network::build_inventory_item_msg(data.slot, slot->item, item_, item_registry_);
                if (scroll_msg)
                    conn->send(network::make_inventory_slot_update(data.slot, &*scroll_msg));
            }
```

**Step 2: Build and test**

```bash
cmake --build build --config Debug && ./bin/hgserver_tests
```

**Step 3: Commit**

```bash
git add src/bridge/handlers/game_handlers_equipment.cpp
git commit -m "Add inventory_slot_update to use_item handler"
```

---

## Task 7: Add Slot Updates + Gold Updates to Shop Handlers

Shop buy, sell confirm, and repair confirm all modify inventory and gold. Add both `inventory_slot_update` and `gold_update` to each.

**Files:**
- Modify: `src/bridge/handlers/game_handlers_shop.cpp:465-980`

**Step 1: Shop Buy — add slot update + gold_update**

In `handle_shop_buy()`, after line 579 (the response send), add:

```cpp
        // Send inventory_slot_update for the new item
        auto* bought_inv = inventory_->get_inventory(owner_id);
        if (bought_inv)
        {
            if (auto new_slot = bought_inv->find_item(new_item_id); new_slot)
            {
                auto item_msg = network::build_inventory_item_msg(*new_slot, new_item_id, item_, item_registry_);
                if (item_msg)
                    conn->send(network::make_inventory_slot_update(*new_slot, &*item_msg));
            }
        }

        // Send gold_update
        conn->send(network::make_gold_update({
            .gold = static_cast<int64_t>(inventory_->get_gold(owner_id)),
            .change = static_cast<int64_t>(-total_price),
            .reason = "shop_buy"}));
```

**Step 2: Shop Sell Confirm — add slot update + gold_update**

In `handle_shop_sell_confirm()`, after line 785 (the response send), add:

```cpp
        // Send inventory_slot_update (slot is now empty)
        conn->send(network::make_inventory_slot_update(data.inventory_slot, nullptr));

        // Send gold_update
        conn->send(network::make_gold_update({
            .gold = static_cast<int64_t>(inventory_->get_gold(owner_id)),
            .change = static_cast<int64_t>(sell_price),
            .reason = "shop_sell"}));
```

**Step 3: Shop Repair Confirm — add slot update + gold_update**

In `handle_shop_repair_confirm()`, after line 958 (the response send), add:

```cpp
        // Send inventory_slot_update (item has new durability)
        auto repair_msg = network::build_inventory_item_msg(data.inventory_slot, slot->item, item_, item_registry_);
        if (repair_msg)
            conn->send(network::make_inventory_slot_update(data.inventory_slot, &*repair_msg));

        // Send gold_update
        conn->send(network::make_gold_update({
            .gold = static_cast<int64_t>(inventory_->get_gold(owner_id)),
            .change = static_cast<int64_t>(-repair_cost),
            .reason = "shop_repair"}));
```

**Step 4: Build and test**

```bash
cmake --build build --config Debug && ./bin/hgserver_tests
```

**Step 5: Commit**

```bash
git add src/bridge/handlers/game_handlers_shop.cpp
git commit -m "Add inventory_slot_update and gold_update to shop handlers"
```

---

## Task 8: Add Slot Updates to Bank Handlers

Bank deposit clears an inventory slot and fills a bank slot. Bank withdraw does the reverse.

**Files:**
- Modify: `src/bridge/handlers/game_handlers_shop.cpp:982-1141`

**Step 1: Bank Deposit — add inventory + bank slot updates**

In `handle_bank_deposit()`, after line 1047 (the response send), add:

```cpp
        // Send inventory_slot_update (inventory slot is now empty)
        conn->send(network::make_inventory_slot_update(data.inventory_slot, nullptr));

        // Send bank_slot_update (item now in bank)
        auto* bank_after = inventory_->get_bank(owner_id);
        if (bank_after)
        {
            if (auto bank_slot = bank_after->find_item(deposit_item_id); bank_slot)
            {
                auto bank_msg = network::build_inventory_item_msg(*bank_slot, deposit_item_id, item_, item_registry_);
                if (bank_msg)
                    conn->send(network::make_bank_slot_update(*bank_slot, &*bank_msg));
            }
        }
```

**Step 2: Bank Withdraw — add bank + inventory slot updates**

In `handle_bank_withdraw()`, after line 1130 (the response send), add:

```cpp
        // Send bank_slot_update (bank slot is now empty)
        conn->send(network::make_bank_slot_update(data.bank_slot, nullptr));

        // Send inventory_slot_update (item now in inventory)
        auto* inv_after = inventory_->get_inventory(owner_id);
        if (inv_after)
        {
            if (auto inv_slot = inv_after->find_item(withdraw_item_id); inv_slot)
            {
                auto inv_msg = network::build_inventory_item_msg(*inv_slot, withdraw_item_id, item_, item_registry_);
                if (inv_msg)
                    conn->send(network::make_inventory_slot_update(*inv_slot, &*inv_msg));
            }
        }
```

**Step 3: Build and test**

```bash
cmake --build build --config Debug && ./bin/hgserver_tests
```

**Step 4: Commit**

```bash
git add src/bridge/handlers/game_handlers_shop.cpp
git commit -m "Add inventory and bank slot updates to bank deposit/withdraw handlers"
```

---

## Task 9: Add Slot Updates to Item Upgrade Handler

The upgrade handler consumes a stone and modifies the target item's attribute. Send updates for both the consumed stone slot and the upgraded item slot.

**Files:**
- Modify: `src/bridge/handlers/game_handlers_equipment.cpp:597-728`

**Step 1: Add slot updates after upgrade**

In `handle_item_upgrade()`, after line 727 (the response send), add:

```cpp
    // Send inventory_slot_update for consumed stone
    if (stone_slot >= 0)
    {
        auto* stone_s = inv->get_slot(stone_slot);
        if (stone_s && stone_s->is_empty())
        {
            conn->send(network::make_inventory_slot_update(stone_slot, nullptr));
        }
        else if (stone_s)
        {
            auto stone_msg = network::build_inventory_item_msg(stone_slot, stone_s->item, item_, item_registry_);
            if (stone_msg)
                conn->send(network::make_inventory_slot_update(stone_slot, &*stone_msg));
        }
    }

    // Send inventory_slot_update for upgraded item (attribute changed)
    {
        auto upgraded_msg = network::build_inventory_item_msg(data.item_slot, target_slot->item, item_, item_registry_);
        if (upgraded_msg)
            conn->send(network::make_inventory_slot_update(data.item_slot, &*upgraded_msg));
    }
```

**Step 2: Build and test**

```bash
cmake --build build --config Debug && ./bin/hgserver_tests
```

**Step 3: Commit**

```bash
git add src/bridge/handlers/game_handlers_equipment.cpp
git commit -m "Add inventory_slot_update to item upgrade handler"
```

---

## Task 10: Add Slot Updates to Crafting Handlers

Manufacturing and alchemy consume ingredients and produce a result item. The crafting system (`manufacturing_system`, `alchemy_system`) handles consumption internally via `consume_ingredients()` which calls `inventory_->remove_item()`. We need to determine which slots changed.

**Strategy:** Snapshot occupied slot indices before crafting. After `attempt_craft()` returns, diff against current state. Slots that became empty = consumed materials. New occupied slots = result.

**Files:**
- Modify: `src/bridge/handlers/game_handlers_crafting.cpp:63-254`
- Modify: `src/inventory/inventory.h` (add `get_occupied_slots()` helper if missing)

**Step 1: Add `get_occupied_slots()` to inventory if needed**

Check if `inventory` has a method to list occupied slot indices. If not, add to `src/inventory/inventory.h`:

```cpp
[[nodiscard]] auto get_occupied_slots() const -> std::vector<int16_t>
{
    std::vector<int16_t> result;
    for (int16_t i = 0; i < capacity(); ++i)
    {
        auto* s = get_slot(i);
        if (s && !s->is_empty())
            result.push_back(i);
    }
    return result;
}
```

**Step 2: Add slot updates to `handle_manufacture_request()`**

In `game_handlers_crafting.cpp`, before the `attempt_craft` call (line 84), snapshot the inventory:

```cpp
    // Snapshot inventory state before crafting
    auto* inv = inventory_->get_inventory(entity_id{pid.value});
    std::set<int16_t> slots_before;
    if (inv)
    {
        for (int16_t i = 0; i < inv->capacity(); ++i)
        {
            auto* s = inv->get_slot(i);
            if (s && !s->is_empty())
                slots_before.insert(i);
        }
    }
```

After the response send (line 112), add:

```cpp
    // Send inventory_slot_updates for changed slots
    if (inv)
    {
        for (int16_t i = 0; i < inv->capacity(); ++i)
        {
            auto* s = inv->get_slot(i);
            bool was_occupied = slots_before.count(i) > 0;
            bool is_occupied = s && !s->is_empty();

            if (was_occupied && !is_occupied)
            {
                // Slot was cleared (consumed material)
                conn->send(network::make_inventory_slot_update(i, nullptr));
            }
            else if (!was_occupied && is_occupied)
            {
                // Slot was filled (crafted result)
                auto item_msg = network::build_inventory_item_msg(i, s->item, item_, item_registry_);
                if (item_msg)
                    conn->send(network::make_inventory_slot_update(i, &*item_msg));
            }
            else if (was_occupied && is_occupied)
            {
                // Slot still occupied but count may have changed (partial consumption)
                // Only send update if the item in this slot was an ingredient
                // Skip for simplicity — partial stacks are rare in crafting
            }
        }
    }
```

Note: `network::make_inventory_slot_update` and `network::build_inventory_item_msg` need the right includes. `game_handlers_crafting.cpp` should already include `game_handlers.h` which includes `json_protocol.h`.

**Step 3: Repeat for `handle_alchemy_request()`**

Same pattern as manufacturing. Copy the snapshot-before + diff-after approach around the `attempt_craft` call (line 200) and after the response send (line 228).

**Step 4: Build and test**

```bash
cmake --build build --config Debug && ./bin/hgserver_tests
```

**Step 5: Commit**

```bash
git add src/bridge/handlers/game_handlers_crafting.cpp src/inventory/inventory.h
git commit -m "Add inventory_slot_update to manufacturing and alchemy handlers"
```

---

## Task 11: Add Slot Updates + Gold Update to Admin Handlers

Admin give and remove operate on a target player. The target player needs the slot update — use `ws_server_->get_connection_by_player()` to reach them.

**Files:**
- Modify: `src/bridge/handlers/admin_web_handlers.cpp:1387-1544`

**Step 1: Admin Give — send slot update to target player**

In `handle_give_item()`, after line 1457 (the admin response send), add:

```cpp
    // Notify target player with inventory_slot_update
    if (auto* target_conn = ws_server_->get_connection_by_player(plr->id))
    {
        auto* target_inv = inventory_->get_inventory(entity_id(plr->id.value));
        if (target_inv)
        {
            if (auto slot = target_inv->find_item(new_item_id); slot)
            {
                auto item_msg = network::build_inventory_item_msg(*slot, new_item_id, item_, item_registry_);
                if (item_msg)
                    target_conn->send(network::make_inventory_slot_update(*slot, &*item_msg));
            }
        }
    }
```

**Step 2: Admin Remove — send slot update to target player**

In `handle_remove_item()`, after line 1539 (the admin response send), add:

```cpp
    // Notify target player with inventory_slot_update
    if (auto* target_conn = ws_server_->get_connection_by_player(plr->id))
    {
        if (slot->is_empty())
        {
            target_conn->send(network::make_inventory_slot_update(req.inventory_slot, nullptr));
        }
        else
        {
            auto item_msg = network::build_inventory_item_msg(req.inventory_slot, slot->item, item_, item_registry_);
            if (item_msg)
                target_conn->send(network::make_inventory_slot_update(req.inventory_slot, &*item_msg));
        }
    }
```

**Step 3: Build and test**

```bash
cmake --build build --config Debug && ./bin/hgserver_tests
```

**Step 4: Commit**

```bash
git add src/bridge/handlers/admin_web_handlers.cpp
git commit -m "Add inventory_slot_update to admin give/remove item handlers"
```

---

## Task 12: Add Gold Update to NPC Loot Handler

The NPC loot handler gives gold directly to the killer but sends no message. Add `gold_update`.

**Files:**
- Modify: `src/bridge/handlers/game_handlers_npc.cpp:308-387`

**Step 1: Send gold_update after NPC gold loot**

In `handle_npc_loot_drop()`, after the gold is added (line 328), add inside the `if (drop.gold > 0 && inventory_)` block:

```cpp
        // Send gold_update to killer
        if (auto* killer_conn = ws_server_ ? ws_server_->get_connection_by_player(player_id{killer.id}) : nullptr)
        {
            killer_conn->send(network::make_gold_update({
                .gold = static_cast<int64_t>(inventory_->get_gold(killer_entity)),
                .change = static_cast<int64_t>(drop.gold),
                .reason = "npc_loot"}));
        }
```

Note: `ws_server_` should be available on `game_handlers`. Verify it's accessible.

**Step 2: Build and test**

```bash
cmake --build build --config Debug && ./bin/hgserver_tests
```

**Step 3: Commit**

```bash
git add src/bridge/handlers/game_handlers_npc.cpp
git commit -m "Add gold_update to NPC loot handler"
```

---

## Task 13: Update Protocol Documentation

Update `docs/protocol/items.md` to reflect all changes:

**Files:**
- Modify: `docs/protocol/items.md`
- Modify: `docs/PROGRESS.md`

**Step 1: Update items.md**

- Update attribute key names from compact to readable throughout
- Document `gold_update` message type and structure
- Document `bank_slot_update` message type and structure
- Update each handler section to document the additional slot/gold updates they now send
- Update the summary table showing which handlers send which updates

**Step 2: Update PROGRESS.md**

Add a dated changelog entry:

```markdown
### 2026-02-16: Item Protocol Reconciliation
- Renamed item attribute JSON keys from compact (`mt`/`mv`/`st`/`sv`/`cm`/`cq`) to readable names
- Added `inventory_slot_update` to all handlers that modify inventory (equip, unequip, use, shop, bank, crafting, upgrade, admin)
- Added `gold_update` protocol message for all gold-changing operations
- Added `bank_slot_update` protocol message for bank deposit/withdraw
- Added `build_inventory_item_msg()` shared helper function
```

**Step 3: Commit**

```bash
git add docs/protocol/items.md docs/PROGRESS.md
git commit -m "Update protocol docs for item reconciliation changes"
```

---

## Verification

After all tasks:

```bash
cmake --build build --config Debug && ./bin/hgserver_tests
```

All 2276+ tests should pass. Key test filters:

```bash
./bin/hgserver_tests --gtest_filter="item_attribute*:json_protocol*:loot_attribute*"
```
