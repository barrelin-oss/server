// test_inventory.cpp
// Unit tests for inventory system

#include <gtest/gtest.h>
#include "core/types.h"
#include "inventory/inventory.h"
#include "inventory/inventory_system.h"

using hb::item_id;
using hb::entity_id;
using hb::player_id;
using namespace hb::inventory;

// Inventory slot tests

TEST(inventory_slot_test, default_empty) {
    inventory_slot slot;
    EXPECT_TRUE(slot.is_empty());
}

TEST(inventory_slot_test, set_and_clear) {
    inventory_slot slot;

    slot.set(item_id{100}, 5);
    EXPECT_FALSE(slot.is_empty());
    EXPECT_EQ(slot.item.value, 100);
    EXPECT_EQ(slot.count, 5);

    slot.clear();
    EXPECT_TRUE(slot.is_empty());
}

// Inventory container tests

TEST(inventory_test, default_capacity) {
    inventory inv;
    EXPECT_EQ(inv.capacity(), max_inventory_slots);
    EXPECT_EQ(inv.used_slots(), 0);
    EXPECT_EQ(inv.free_slots(), max_inventory_slots);
}

TEST(inventory_test, custom_capacity) {
    inventory inv(100);
    EXPECT_EQ(inv.capacity(), 100);
}

TEST(inventory_test, add_item) {
    inventory inv;

    EXPECT_TRUE(inv.add_item(item_id{100}, 5));
    EXPECT_EQ(inv.used_slots(), 1);
    EXPECT_TRUE(inv.has_item(item_id{100}));
    EXPECT_EQ(inv.count_item(item_id{100}), 5);
}

TEST(inventory_test, find_item) {
    inventory inv;

    auto found = inv.find_item(item_id{100});
    EXPECT_FALSE(found.has_value());

    inv.add_item(item_id{100}, 1);
    found = inv.find_item(item_id{100});
    EXPECT_TRUE(found.has_value());
    EXPECT_EQ(*found, 0);  // First slot
}

TEST(inventory_test, find_empty_slot) {
    inventory inv(2);

    auto slot = inv.find_empty_slot();
    EXPECT_TRUE(slot.has_value());
    EXPECT_EQ(*slot, 0);

    inv.add_item(item_id{1}, 1);
    inv.add_item(item_id{2}, 1);

    slot = inv.find_empty_slot();
    EXPECT_FALSE(slot.has_value());
}

TEST(inventory_test, remove_item) {
    inventory inv;

    inv.add_item(item_id{100}, 5);
    EXPECT_TRUE(inv.remove_item(item_id{100}));
    EXPECT_FALSE(inv.has_item(item_id{100}));
    EXPECT_EQ(inv.used_slots(), 0);
}

TEST(inventory_test, remove_item_count) {
    inventory inv;

    inv.add_item(item_id{100}, 10);

    EXPECT_TRUE(inv.remove_item_count(item_id{100}, 3));
    EXPECT_EQ(inv.count_item(item_id{100}), 7);

    EXPECT_FALSE(inv.remove_item_count(item_id{100}, 10));  // Not enough
}

TEST(inventory_test, is_full) {
    inventory inv(2);

    EXPECT_FALSE(inv.is_full());

    inv.add_item(item_id{1}, 1);
    inv.add_item(item_id{2}, 1);

    EXPECT_TRUE(inv.is_full());
}

TEST(inventory_test, is_empty) {
    inventory inv;

    EXPECT_TRUE(inv.is_empty());

    inv.add_item(item_id{1}, 1);
    EXPECT_FALSE(inv.is_empty());
}

TEST(inventory_test, swap_slots) {
    inventory inv;

    inv.add_item(item_id{100}, 5);
    inv.add_item(item_id{200}, 10);

    inv.swap_slots(0, 1);

    EXPECT_EQ(inv.get_slot(0)->item.value, 200);
    EXPECT_EQ(inv.get_slot(1)->item.value, 100);
}

TEST(inventory_test, move_item) {
    inventory inv;

    inv.add_item(item_id{100}, 5);

    EXPECT_TRUE(inv.move_item(0, 5));
    EXPECT_TRUE(inv.get_slot(0)->is_empty());
    EXPECT_EQ(inv.get_slot(5)->item.value, 100);
}

TEST(inventory_test, clear_all) {
    inventory inv;

    inv.add_item(item_id{1}, 1);
    inv.add_item(item_id{2}, 1);
    inv.add_item(item_id{3}, 1);

    inv.clear_all();
    EXPECT_TRUE(inv.is_empty());
    EXPECT_EQ(inv.used_slots(), 0);
}

// Bank storage tests

TEST(bank_storage_test, capacity) {
    bank_storage bank;
    EXPECT_EQ(bank.capacity(), max_bank_slots);
}

// Trade window tests

TEST(trade_window_test, default_state) {
    trade_window tw;
    EXPECT_TRUE(tw.is_empty());
    EXPECT_FALSE(tw.confirmed);
    EXPECT_FALSE(tw.locked);
}

TEST(trade_window_test, reset) {
    trade_window tw;
    tw.offered[0].set(item_id{100}, 1);
    tw.gold_offered = 1000;
    tw.confirmed = true;
    tw.locked = true;

    tw.reset();

    EXPECT_TRUE(tw.is_empty());
    EXPECT_EQ(tw.gold_offered, 0);
    EXPECT_FALSE(tw.confirmed);
    EXPECT_FALSE(tw.locked);
}

// Inventory system tests

class inventory_system_test : public ::testing::Test {
protected:
    void SetUp() override {
        system_.initialize();
    }

    void TearDown() override {
        system_.shutdown();
    }

    inventory_system system_;
};

TEST_F(inventory_system_test, lifecycle) {
    EXPECT_TRUE(system_.is_initialized());
    EXPECT_EQ(system_.name(), "inventory_system");
}

TEST_F(inventory_system_test, create_inventory) {
    entity_id owner{1};

    system_.create_inventory(owner);

    auto* inv = system_.get_inventory(owner);
    ASSERT_NE(inv, nullptr);
    EXPECT_TRUE(inv->is_empty());
}

TEST_F(inventory_system_test, destroy_inventory) {
    entity_id owner{1};

    system_.create_inventory(owner);
    EXPECT_NE(system_.get_inventory(owner), nullptr);

    system_.destroy_inventory(owner);
    EXPECT_EQ(system_.get_inventory(owner), nullptr);
}

TEST_F(inventory_system_test, add_item) {
    entity_id owner{1};
    system_.create_inventory(owner);

    auto result = system_.add_item(owner, item_id{100}, 5);
    EXPECT_EQ(result, inventory_result::success);
    EXPECT_TRUE(system_.has_item(owner, item_id{100}));
    EXPECT_EQ(system_.count_item(owner, item_id{100}), 5);
}

TEST_F(inventory_system_test, remove_item) {
    entity_id owner{1};
    system_.create_inventory(owner);

    system_.add_item(owner, item_id{100}, 10);

    auto result = system_.remove_item(owner, item_id{100}, 5);
    EXPECT_EQ(result, inventory_result::success);
    EXPECT_EQ(system_.count_item(owner, item_id{100}), 5);

    result = system_.remove_item(owner, item_id{100}, 10);
    EXPECT_EQ(result, inventory_result::insufficient_count);
}

TEST_F(inventory_system_test, move_item) {
    entity_id owner{1};
    system_.create_inventory(owner);

    system_.add_item(owner, item_id{100}, 5);

    auto result = system_.move_item(owner, 0, 10);
    EXPECT_EQ(result, inventory_result::success);

    auto* inv = system_.get_inventory(owner);
    EXPECT_TRUE(inv->get_slot(0)->is_empty());
    EXPECT_EQ(inv->get_slot(10)->item.value, 100);
}

TEST_F(inventory_system_test, free_slots) {
    entity_id owner{1};
    system_.create_inventory(owner);

    EXPECT_EQ(system_.free_slots(owner), 50);

    system_.add_item(owner, item_id{100}, 1);
    EXPECT_EQ(system_.free_slots(owner), 49);
}

// Gold tests

TEST_F(inventory_system_test, gold_operations) {
    entity_id owner{1};
    system_.create_inventory(owner);

    EXPECT_EQ(system_.get_gold(owner), 0);

    EXPECT_TRUE(system_.add_gold(owner, 1000));
    EXPECT_EQ(system_.get_gold(owner), 1000);

    EXPECT_TRUE(system_.has_gold(owner, 500));
    EXPECT_FALSE(system_.has_gold(owner, 2000));

    EXPECT_TRUE(system_.remove_gold(owner, 400));
    EXPECT_EQ(system_.get_gold(owner), 600);

    EXPECT_FALSE(system_.remove_gold(owner, 1000));  // Not enough
}

// Bank tests

TEST_F(inventory_system_test, bank_operations) {
    entity_id owner{1};
    system_.create_inventory(owner);
    system_.create_bank(owner);

    system_.add_item(owner, item_id{100}, 5);

    auto result = system_.deposit_item(owner, 0);
    EXPECT_EQ(result, inventory_result::success);

    auto* inv = system_.get_inventory(owner);
    auto* bank = system_.get_bank(owner);

    EXPECT_TRUE(inv->get_slot(0)->is_empty());
    EXPECT_FALSE(bank->is_empty());

    result = system_.withdraw_item(owner, 0);
    EXPECT_EQ(result, inventory_result::success);

    EXPECT_FALSE(inv->is_empty());
    EXPECT_TRUE(bank->is_empty());
}

// Trading tests

TEST_F(inventory_system_test, start_trade) {
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

TEST_F(inventory_system_test, cancel_trade) {
    entity_id player1{1};
    entity_id player2{2};

    system_.create_inventory(player1);
    system_.create_inventory(player2);

    system_.start_trade(player1, player2);
    system_.cancel_trade(player1);

    EXPECT_EQ(system_.get_trade_window(player1), nullptr);
    EXPECT_EQ(system_.get_trade_window(player2), nullptr);
}

TEST_F(inventory_system_test, trade_gold) {
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

    EXPECT_EQ(window1->gold_offered, 500);
    EXPECT_EQ(window2->gold_offered, 200);
}

TEST_F(inventory_system_test, complete_trade) {
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
    EXPECT_EQ(system_.get_gold(player1), 1000 - 300 + 100);  // 800
    EXPECT_EQ(system_.get_gold(player2), 500 - 100 + 300);   // 700
}
