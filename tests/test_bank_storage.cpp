// test_bank_storage.cpp
// Unit tests for paginated bank_storage

#include <gtest/gtest.h>
#include "inventory/inventory.h"

using namespace hb::inventory;
using hb::item_id;

TEST(bank_storage_test, empty_bank_has_pages)
{
    bank_storage bank(4, 12); // 4 pages, 12 slots each
    EXPECT_EQ(bank.total_pages(), 4);
    EXPECT_EQ(bank.slots_per_page(), 12);
    EXPECT_EQ(bank.total_capacity(), 48);
}

TEST(bank_storage_test, set_and_get_slot)
{
    bank_storage bank(4, 12);
    bank.set_slot(0, 3, item_id{100});
    auto id = bank.get_slot(0, 3);
    ASSERT_TRUE(id.has_value());
    EXPECT_EQ(id->value, 100u);
}

TEST(bank_storage_test, get_empty_slot_returns_nullopt)
{
    bank_storage bank(4, 12);
    auto id = bank.get_slot(0, 0);
    EXPECT_FALSE(id.has_value());
}

TEST(bank_storage_test, get_out_of_range_returns_nullopt)
{
    bank_storage bank(2, 3);
    EXPECT_FALSE(bank.get_slot(-1, 0).has_value());
    EXPECT_FALSE(bank.get_slot(0, -1).has_value());
    EXPECT_FALSE(bank.get_slot(2, 0).has_value());
    EXPECT_FALSE(bank.get_slot(0, 3).has_value());
    EXPECT_FALSE(bank.get_slot(99, 99).has_value());
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

TEST(bank_storage_test, find_empty_slot_wraps_to_next_page)
{
    bank_storage bank(2, 2); // 2 pages, 2 slots each
    bank.set_slot(0, 0, item_id{1});
    bank.set_slot(0, 1, item_id{2});
    auto empty = bank.find_empty_slot();
    ASSERT_TRUE(empty.has_value());
    EXPECT_EQ(empty->page, 1);
    EXPECT_EQ(empty->slot, 0);
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

TEST(bank_storage_test, find_item_not_present)
{
    bank_storage bank(4, 12);
    bank.set_slot(0, 0, item_id{100});
    auto loc = bank.find_item(item_id{999});
    EXPECT_FALSE(loc.has_value());
}

TEST(bank_storage_test, full_bank)
{
    bank_storage bank(1, 2);
    bank.set_slot(0, 0, item_id{1});
    bank.set_slot(0, 1, item_id{2});
    EXPECT_TRUE(bank.is_full());
    EXPECT_FALSE(bank.find_empty_slot().has_value());
}

TEST(bank_storage_test, empty_bank)
{
    bank_storage bank(2, 3);
    EXPECT_TRUE(bank.is_empty());
    EXPECT_EQ(bank.used_slots(), 0);
    EXPECT_EQ(bank.free_slots(), 6);
}

TEST(bank_storage_test, used_and_free_slots)
{
    bank_storage bank(2, 3);
    bank.set_slot(0, 0, item_id{1});
    bank.set_slot(1, 2, item_id{2});
    EXPECT_EQ(bank.used_slots(), 2);
    EXPECT_EQ(bank.free_slots(), 4);
    EXPECT_FALSE(bank.is_full());
    EXPECT_FALSE(bank.is_empty());
}

TEST(bank_storage_test, has_item)
{
    bank_storage bank(2, 3);
    bank.set_slot(0, 1, item_id{42});
    EXPECT_TRUE(bank.has_item(item_id{42}));
    EXPECT_FALSE(bank.has_item(item_id{99}));
}

TEST(bank_storage_test, clear_all)
{
    bank_storage bank(2, 3);
    bank.set_slot(0, 0, item_id{1});
    bank.set_slot(0, 1, item_id{2});
    bank.set_slot(1, 0, item_id{3});
    EXPECT_EQ(bank.used_slots(), 3);

    bank.clear_all();
    EXPECT_TRUE(bank.is_empty());
    EXPECT_EQ(bank.used_slots(), 0);
}

TEST(bank_storage_test, set_out_of_range_ignored)
{
    bank_storage bank(1, 2);
    bank.set_slot(5, 0, item_id{1}); // out of range page
    bank.set_slot(0, 5, item_id{2}); // out of range slot
    EXPECT_TRUE(bank.is_empty());
}

TEST(bank_storage_test, clear_out_of_range_ignored)
{
    bank_storage bank(1, 2);
    bank.set_slot(0, 0, item_id{1});
    bank.clear_slot(5, 0); // out of range — should not crash
    EXPECT_EQ(bank.used_slots(), 1);
}

TEST(bank_storage_test, overwrite_slot)
{
    bank_storage bank(2, 3);
    bank.set_slot(0, 0, item_id{100});
    bank.set_slot(0, 0, item_id{200}); // overwrite

    auto id = bank.get_slot(0, 0);
    ASSERT_TRUE(id.has_value());
    EXPECT_EQ(id->value, 200u);
    EXPECT_EQ(bank.used_slots(), 1);
}

TEST(bank_storage_test, items_on_different_pages_independent)
{
    bank_storage bank(3, 2);
    bank.set_slot(0, 0, item_id{10});
    bank.set_slot(1, 0, item_id{20});
    bank.set_slot(2, 0, item_id{30});

    EXPECT_EQ(bank.get_slot(0, 0)->value, 10u);
    EXPECT_EQ(bank.get_slot(1, 0)->value, 20u);
    EXPECT_EQ(bank.get_slot(2, 0)->value, 30u);

    bank.clear_slot(1, 0);
    EXPECT_TRUE(bank.get_slot(0, 0).has_value());
    EXPECT_FALSE(bank.get_slot(1, 0).has_value());
    EXPECT_TRUE(bank.get_slot(2, 0).has_value());
}

TEST(bank_storage_test, default_constructor_values)
{
    bank_storage bank;
    EXPECT_EQ(bank.total_pages(), default_bank_pages);
    EXPECT_EQ(bank.slots_per_page(), default_bank_slots_per_page);
    EXPECT_EQ(bank.total_capacity(), default_bank_pages * default_bank_slots_per_page);
    EXPECT_TRUE(bank.is_empty());
}
