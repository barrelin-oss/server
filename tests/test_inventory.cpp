// test_inventory.cpp
// Unit tests for inventory system

#include <gtest/gtest.h>
#include "core/types.h"
#include "inventory/inventory.h"
#include "inventory/inventory_system.h"

using hb::entity_id;
using hb::item_id;
using hb::player_id;
using namespace hb::inventory;

// Container slot tests (for bank/trade)

TEST(container_slot_test, default_empty)
{
    container_slot slot;
    EXPECT_TRUE(slot.is_empty());
}

TEST(container_slot_test, set_and_clear)
{
    container_slot slot;

    slot.set(item_id{100}, 5);
    EXPECT_FALSE(slot.is_empty());
    EXPECT_EQ(slot.item.value, 100);
    EXPECT_EQ(slot.count, 5);

    slot.clear();
    EXPECT_TRUE(slot.is_empty());
}

// Inventory entry tests

TEST(inventory_entry_test, default_state)
{
    inventory_entry entry{};
    EXPECT_EQ(entry.count, 0);
    EXPECT_EQ(entry.pos_x, 0);
    EXPECT_EQ(entry.pos_y, 0);
    EXPECT_EQ(entry.z_order, 0);
}

// Inventory container tests

TEST(inventory_test, default_capacity)
{
    inventory inv;
    EXPECT_EQ(inv.max_items(), max_inventory_slots);
    EXPECT_EQ(inv.count(), 0);
    EXPECT_EQ(inv.max_items() - inv.count(), max_inventory_slots);
}

TEST(inventory_test, custom_capacity)
{
    inventory inv(100);
    EXPECT_EQ(inv.max_items(), 100);
}

TEST(inventory_test, add_item)
{
    inventory inv;

    EXPECT_NE(inv.add_item(item_id{100}, 5), nullptr);
    EXPECT_EQ(inv.count(), 1);
    EXPECT_TRUE(inv.has_item(item_id{100}));
    EXPECT_EQ(inv.count_item(item_id{100}), 5);
}

TEST(inventory_test, get_item)
{
    inventory inv;

    auto* entry = inv.get_item(item_id{100});
    EXPECT_EQ(entry, nullptr);

    inv.add_item(item_id{100}, 1);
    entry = inv.get_item(item_id{100});
    ASSERT_NE(entry, nullptr);
    EXPECT_EQ(entry->item.value, 100);
    EXPECT_EQ(entry->count, 1);
}

TEST(inventory_test, add_item_when_full)
{
    inventory inv(2);

    EXPECT_NE(inv.add_item(item_id{1}, 1), nullptr);
    EXPECT_NE(inv.add_item(item_id{2}, 1), nullptr);

    // Full - add should fail
    EXPECT_EQ(inv.add_item(item_id{3}, 1), nullptr);
}

TEST(inventory_test, remove_item)
{
    inventory inv;

    inv.add_item(item_id{100}, 5);
    EXPECT_TRUE(inv.remove_item(item_id{100}));
    EXPECT_FALSE(inv.has_item(item_id{100}));
    EXPECT_EQ(inv.count(), 0);
}

TEST(inventory_test, remove_item_count)
{
    inventory inv;

    inv.add_item(item_id{100}, 10);

    EXPECT_TRUE(inv.remove_item_count(item_id{100}, 3));
    EXPECT_EQ(inv.count_item(item_id{100}), 7);

    EXPECT_FALSE(inv.remove_item_count(item_id{100}, 10)); // Not enough
}

TEST(inventory_test, is_full)
{
    inventory inv(2);

    EXPECT_FALSE(inv.is_full());

    inv.add_item(item_id{1}, 1);
    inv.add_item(item_id{2}, 1);

    EXPECT_TRUE(inv.is_full());
}

TEST(inventory_test, is_empty)
{
    inventory inv;

    EXPECT_TRUE(inv.is_empty());

    inv.add_item(item_id{1}, 1);
    EXPECT_FALSE(inv.is_empty());
}

TEST(inventory_test, clear_all)
{
    inventory inv;

    inv.add_item(item_id{1}, 1);
    inv.add_item(item_id{2}, 1);
    inv.add_item(item_id{3}, 1);

    inv.clear_all();
    EXPECT_TRUE(inv.is_empty());
    EXPECT_EQ(inv.count(), 0);
}

// Bank storage tests

TEST(bank_storage_test, default_pages_and_slots)
{
    bank_storage bank;
    EXPECT_EQ(bank.total_pages(), default_bank_pages);
    EXPECT_EQ(bank.slots_per_page(), default_bank_slots_per_page);
    EXPECT_EQ(bank.total_capacity(), default_bank_pages * default_bank_slots_per_page);
}

// Trade window tests

TEST(trade_window_test, default_state)
{
    trade_window tw;
    EXPECT_TRUE(tw.is_empty());
    EXPECT_FALSE(tw.is_confirmed());
    EXPECT_FALSE(tw.is_locked());
    EXPECT_EQ(tw.gold(), 0);
    EXPECT_TRUE(tw.items().empty());
}

TEST(trade_window_test, add_and_remove_items)
{
    trade_window tw;
    EXPECT_TRUE(tw.add_item(item_id{100}));
    EXPECT_TRUE(tw.add_item(item_id{200}));
    EXPECT_FALSE(tw.is_empty());
    EXPECT_EQ(tw.items().size(), 2u);
    EXPECT_TRUE(tw.has_item(item_id{100}));
    EXPECT_TRUE(tw.has_item(item_id{200}));

    // Duplicate rejected
    EXPECT_FALSE(tw.add_item(item_id{100}));
    EXPECT_EQ(tw.items().size(), 2u);

    // Remove
    EXPECT_TRUE(tw.remove_item(item_id{100}));
    EXPECT_FALSE(tw.has_item(item_id{100}));
    EXPECT_EQ(tw.items().size(), 1u);

    // Remove non-existent
    EXPECT_FALSE(tw.remove_item(item_id{999}));
}

TEST(trade_window_test, max_items_enforced)
{
    trade_window tw;
    for (size_t i = 0; i < trade_window::max_trade_items; ++i)
    {
        EXPECT_TRUE(tw.add_item(item_id{static_cast<uint32_t>(i + 1)}));
    }
    // 13th item rejected
    EXPECT_FALSE(tw.add_item(item_id{999}));
    EXPECT_EQ(tw.items().size(), trade_window::max_trade_items);
}

TEST(trade_window_test, lock_prevents_modification)
{
    trade_window tw;
    tw.add_item(item_id{100});
    tw.set_gold(500);
    tw.lock();

    EXPECT_TRUE(tw.is_locked());
    EXPECT_FALSE(tw.add_item(item_id{200}));
    EXPECT_FALSE(tw.remove_item(item_id{100}));

    // set_gold is no-op when locked
    tw.set_gold(9999);
    EXPECT_EQ(tw.gold(), 500);
}

TEST(trade_window_test, confirm)
{
    trade_window tw;
    EXPECT_FALSE(tw.is_confirmed());
    tw.confirm();
    EXPECT_TRUE(tw.is_confirmed());
}

TEST(trade_window_test, gold)
{
    trade_window tw;
    tw.set_gold(1000);
    EXPECT_EQ(tw.gold(), 1000);
    EXPECT_FALSE(tw.is_empty());

    // Negative clamped to zero
    tw.set_gold(-50);
    EXPECT_EQ(tw.gold(), 0);
}

TEST(trade_window_test, reset)
{
    trade_window tw;
    tw.add_item(item_id{100});
    tw.set_gold(1000);
    tw.lock();
    tw.confirm();

    tw.reset();

    EXPECT_TRUE(tw.is_empty());
    EXPECT_EQ(tw.gold(), 0);
    EXPECT_FALSE(tw.is_confirmed());
    EXPECT_FALSE(tw.is_locked());
    EXPECT_TRUE(tw.items().empty());
}

// Inventory system tests

class inventory_system_test : public ::testing::Test
{
protected:
    void SetUp() override { system_.initialize(); }

    void TearDown() override { system_.shutdown(); }

    inventory_system system_;
};

TEST_F(inventory_system_test, lifecycle)
{
    EXPECT_TRUE(system_.is_initialized());
    EXPECT_EQ(system_.name(), "inventory_system");
}

TEST_F(inventory_system_test, create_inventory)
{
    entity_id owner{1};

    system_.create_inventory(owner);

    auto* inv = system_.get_inventory(owner);
    ASSERT_NE(inv, nullptr);
    EXPECT_TRUE(inv->is_empty());
}

TEST_F(inventory_system_test, destroy_inventory)
{
    entity_id owner{1};

    system_.create_inventory(owner);
    EXPECT_NE(system_.get_inventory(owner), nullptr);

    system_.destroy_inventory(owner);
    EXPECT_EQ(system_.get_inventory(owner), nullptr);
}

TEST_F(inventory_system_test, add_item)
{
    entity_id owner{1};
    system_.create_inventory(owner);

    auto result = system_.add_item(owner, item_id{100}, 5);
    EXPECT_EQ(result, inventory_result::success);
    EXPECT_TRUE(system_.has_item(owner, item_id{100}));
    EXPECT_EQ(system_.count_item(owner, item_id{100}), 5);
}

TEST_F(inventory_system_test, remove_item)
{
    entity_id owner{1};
    system_.create_inventory(owner);

    system_.add_item(owner, item_id{100}, 10);

    auto result = system_.remove_item(owner, item_id{100}, 5);
    EXPECT_EQ(result, inventory_result::success);
    EXPECT_EQ(system_.count_item(owner, item_id{100}), 5);

    result = system_.remove_item(owner, item_id{100}, 10);
    EXPECT_EQ(result, inventory_result::insufficient_count);
}

TEST_F(inventory_system_test, free_slots)
{
    entity_id owner{1};
    system_.create_inventory(owner);

    EXPECT_EQ(system_.free_slots(owner), 50);

    system_.add_item(owner, item_id{100}, 1);
    EXPECT_EQ(system_.free_slots(owner), 49);
}

// Gold tests

TEST_F(inventory_system_test, gold_operations)
{
    entity_id owner{1};
    system_.create_inventory(owner);

    EXPECT_EQ(system_.get_gold(owner), 0);

    EXPECT_TRUE(system_.add_gold(owner, 1000));
    EXPECT_EQ(system_.get_gold(owner), 1000);

    EXPECT_TRUE(system_.has_gold(owner, 500));
    EXPECT_FALSE(system_.has_gold(owner, 2000));

    EXPECT_TRUE(system_.remove_gold(owner, 400));
    EXPECT_EQ(system_.get_gold(owner), 600);

    EXPECT_FALSE(system_.remove_gold(owner, 1000)); // Not enough
}

// Bank tests

TEST_F(inventory_system_test, bank_operations)
{
    entity_id owner{1};
    system_.create_inventory(owner);
    system_.create_bank(owner);

    system_.add_item(owner, item_id{100}, 5);

    auto result = system_.deposit_item(owner, item_id{100});
    EXPECT_EQ(result, inventory_result::success);

    auto* inv = system_.get_inventory(owner);
    auto* bank = system_.get_bank(owner);

    EXPECT_FALSE(inv->has_item(item_id{100}));
    EXPECT_FALSE(bank->is_empty());

    // Find where the item was deposited (page 0, slot 0 for first deposit)
    auto loc = bank->find_item(item_id{100});
    ASSERT_TRUE(loc.has_value());

    result = system_.withdraw_item(owner, loc->page, loc->slot);
    EXPECT_EQ(result, inventory_result::success);

    EXPECT_FALSE(inv->is_empty());
    EXPECT_TRUE(bank->is_empty());
}

// Trading tests

TEST_F(inventory_system_test, start_trade)
{
    entity_id player1{1};
    entity_id player2{2};

    system_.create_inventory(player1);
    system_.create_inventory(player2);

    system_.start_trade(player1, player2);

    EXPECT_NE(system_.get_trade_window(player1), nullptr);
    EXPECT_NE(system_.get_trade_window(player2), nullptr);
    EXPECT_EQ(system_.get_trade_partner(player1).value, player2.value);
    EXPECT_EQ(system_.get_trade_partner(player2).value, player1.value);
}

TEST_F(inventory_system_test, cancel_trade)
{
    entity_id player1{1};
    entity_id player2{2};

    system_.create_inventory(player1);
    system_.create_inventory(player2);

    system_.start_trade(player1, player2);
    system_.cancel_trade(player1);

    EXPECT_EQ(system_.get_trade_window(player1), nullptr);
    EXPECT_EQ(system_.get_trade_window(player2), nullptr);
}

TEST_F(inventory_system_test, trade_gold)
{
    entity_id player1{1};
    entity_id player2{2};

    system_.create_inventory(player1);
    system_.create_inventory(player2);
    system_.add_gold(player1, 1000);
    system_.add_gold(player2, 500);

    system_.start_trade(player1, player2);

    system_.set_trade_gold(player1, 500);
    system_.set_trade_gold(player2, 200);

    auto* window1 = system_.get_trade_window(player1);
    auto* window2 = system_.get_trade_window(player2);

    EXPECT_EQ(window1->gold(), 500);
    EXPECT_EQ(window2->gold(), 200);
}

TEST_F(inventory_system_test, complete_trade)
{
    entity_id player1{1};
    entity_id player2{2};

    system_.create_inventory(player1);
    system_.create_inventory(player2);
    system_.add_gold(player1, 1000);
    system_.add_gold(player2, 500);

    system_.start_trade(player1, player2);

    system_.set_trade_gold(player1, 300);
    system_.set_trade_gold(player2, 100);

    system_.confirm_trade(player1);
    system_.confirm_trade(player2);
    system_.lock_trade(player1);
    system_.lock_trade(player2);

    EXPECT_TRUE(system_.complete_trade(player1, player2));

    // Gold should be swapped
    EXPECT_EQ(system_.get_gold(player1), 1000 - 300 + 100); // 800
    EXPECT_EQ(system_.get_gold(player2), 500 - 100 + 300);  // 700
}
