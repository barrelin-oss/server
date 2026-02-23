// test_equipment_state.cpp
// Unit tests for the linked equipment_state model

#include <gtest/gtest.h>
#include "player/equipment.h"

using namespace hb::player;
using hb::item_id;

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

TEST(equipment_state_test, has_equipped_false_for_empty_slot)
{
    equipment_state eq;
    EXPECT_FALSE(eq.has_equipped(equip_slot::weapon));
    EXPECT_FALSE(eq.has_equipped(equip_slot::head));
    EXPECT_FALSE(eq.has_equipped(equip_slot::body));
}

TEST(equipment_state_test, has_equipped_true_after_equip)
{
    equipment_state eq;
    eq.equip(equip_slot::weapon, item_id{42});
    EXPECT_TRUE(eq.has_equipped(equip_slot::weapon));
}

TEST(equipment_state_test, has_equipped_false_after_unequip)
{
    equipment_state eq;
    eq.equip(equip_slot::weapon, item_id{42});
    eq.unequip(equip_slot::weapon);
    EXPECT_FALSE(eq.has_equipped(equip_slot::weapon));
}

TEST(equipment_state_test, equip_replaces_existing)
{
    equipment_state eq;
    eq.equip(equip_slot::weapon, item_id{1});
    eq.equip(equip_slot::weapon, item_id{2});
    auto equipped = eq.get_equipped(equip_slot::weapon);
    ASSERT_TRUE(equipped.has_value());
    EXPECT_EQ(equipped->value, 2u);
}

TEST(equipment_state_test, all_equipped_includes_correct_slots)
{
    equipment_state eq;
    eq.equip(equip_slot::weapon, item_id{10});
    eq.equip(equip_slot::shield, item_id{20});
    eq.equip(equip_slot::head, item_id{30});

    auto all = eq.all_equipped();
    ASSERT_EQ(all.size(), 3u);

    // Check that the returned pairs have the correct slot-id mappings
    bool found_weapon = false, found_shield = false, found_head = false;
    for (auto& [slot, id] : all)
    {
        if (slot == equip_slot::weapon)
        {
            EXPECT_EQ(id.value, 10u);
            found_weapon = true;
        }
        else if (slot == equip_slot::shield)
        {
            EXPECT_EQ(id.value, 20u);
            found_shield = true;
        }
        else if (slot == equip_slot::head)
        {
            EXPECT_EQ(id.value, 30u);
            found_head = true;
        }
    }
    EXPECT_TRUE(found_weapon);
    EXPECT_TRUE(found_shield);
    EXPECT_TRUE(found_head);
}

TEST(equipment_state_test, find_slot_for_with_multiple_items)
{
    equipment_state eq;
    eq.equip(equip_slot::weapon, item_id{10});
    eq.equip(equip_slot::body, item_id{20});
    eq.equip(equip_slot::boots, item_id{30});

    auto slot1 = eq.find_slot_for(item_id{20});
    ASSERT_TRUE(slot1.has_value());
    EXPECT_EQ(*slot1, equip_slot::body);

    auto slot2 = eq.find_slot_for(item_id{30});
    ASSERT_TRUE(slot2.has_value());
    EXPECT_EQ(*slot2, equip_slot::boots);
}

TEST(equipment_state_test, all_slots_can_be_equipped)
{
    equipment_state eq;
    for (size_t i = 0; i < equip_slot_count; ++i)
    {
        eq.equip(static_cast<equip_slot>(i), item_id{static_cast<uint32_t>(i + 1)});
    }
    auto all = eq.all_equipped();
    EXPECT_EQ(all.size(), equip_slot_count);
}
