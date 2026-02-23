// test_item_ops.cpp
// Unit tests for item_ops pickup and drop operations

#include <gtest/gtest.h>

#include "core/types.h"
#include "core/subsystem.h"
#include "item/item.h"
#include "item/item_ops.h"
#include "item/item_system.h"
#include "item/item_attribute.h"
#include "item/special_ability.h"
#include "inventory/inventory.h"
#include "inventory/inventory_system.h"
#include "world/world_subsystem.h"
#include "player/equipment.h"
#include "registry/item_registry.h"
#include "registry/item_template.h"

using hb::entity_id;
using hb::item_id;
using hb::map_id;
using hb::item_registry;
using hb::item_template;

class item_ops_test : public ::testing::Test
{
protected:
    void SetUp() override
    {
        // Set up subsystem registry with item_registry so item_system can resolve templates
        auto& registry = hb::subsystems().create_subsystem<item_registry>();

        // Sword template (equipment, droppable)
        item_template sword{};
        sword.id = item_id{100};
        sword.name = "Test Sword";
        sword.type = hb::item_type::weapon;
        sword.equip_pos = hb::item_equip_pos::right_hand;
        sword.weight = 10;
        sword.price = 100;
        sword.durability = 200;
        registry.register_template(sword);

        // Potion template (consumable)
        item_template potion{};
        potion.id = item_id{200};
        potion.name = "HP Potion";
        potion.type = hb::item_type::consume;
        potion.weight = 2;
        potion.price = 10;
        potion.durability = 0;
        registry.register_template(potion);

        // Quest item template (not droppable)
        item_template quest_item{};
        quest_item.id = item_id{300};
        quest_item.name = "Quest Scroll";
        quest_item.type = hb::item_type::use_perm;
        quest_item.weight = 1;
        quest_item.price = 0;
        quest_item.durability = 0;
        registry.register_template(quest_item);

        // Upgrade stone template (consumable, used for upgrading)
        item_template stone{};
        stone.id = item_id{400};
        stone.name = "Xelima Stone";
        stone.type = hb::item_type::consume;
        stone.weight = 1;
        stone.price = 500;
        stone.durability = 0;
        registry.register_template(stone);

        // Special ability sword template (equipment with special_effect)
        item_template ability_sword{};
        ability_sword.id = item_id{500};
        ability_sword.name = "Sword of Life Drain";
        ability_sword.type = hb::item_type::weapon;
        ability_sword.equip_pos = hb::item_equip_pos::right_hand;
        ability_sword.weight = 15;
        ability_sword.price = 1000;
        ability_sword.durability = 300;
        ability_sword.special_effect = static_cast<int16_t>(hb::item::special_ability_type::life_drain);
        registry.register_template(ability_sword);

        // Create subsystems
        hb::subsystems().create_subsystem<hb::item::item_system>();
        hb::subsystems().create_subsystem<hb::inventory::inventory_system>();
        hb::subsystems().create_subsystem<hb::world::world_subsystem>();

        hb::subsystems().initialize_all();

        items_ = hb::subsystems().get<hb::item::item_system>();
        inv_ = hb::subsystems().get<hb::inventory::inventory_system>();
        world_ = hb::subsystems().get<hb::world::world_subsystem>();

        // Create a test map
        hb::world::map_config map_cfg;
        map_cfg.name = "test_map";
        map_cfg.width = 100;
        map_cfg.height = 100;
        auto map_result = world_->create_map(map_cfg);
        ASSERT_TRUE(map_result.is_ok());
        test_map_ = map_result.value();

        // Create player inventory
        inv_->create_inventory(player_);
    }

    void TearDown() override
    {
        hb::subsystems().clear_all();
    }

    // Helper: create an item from a template and return its instance id
    auto create_item(item_id template_id, entity_id owner = entity_id{}) -> item_id
    {
        hb::item::item_create_info info;
        info.template_id = template_id;
        info.owner = owner;
        auto result = items_->create_item(info);
        EXPECT_TRUE(result.is_ok());
        return result.value();
    }

    // Helper: place an item on the ground
    void place_on_ground(item_id id, map_id map, hb::world::position pos)
    {
        world_->add_ground_item(map, pos, id);
    }

    // Helper: add item to player inventory
    void add_to_inventory(entity_id owner, item_id id, int16_t count = 1)
    {
        auto result = inv_->add_item(owner, id, count);
        EXPECT_EQ(result, hb::inventory::inventory_result::success);
    }

    hb::item::item_system* items_{nullptr};
    hb::inventory::inventory_system* inv_{nullptr};
    hb::world::world_subsystem* world_{nullptr};

    entity_id player_{entity_id(1u)};
    map_id test_map_{};
    hb::world::position player_pos_{10, 20};

    hb::player::equipment_state equipment_;
};

// ========== Pickup Tests ==========

TEST_F(item_ops_test, pickup_success)
{
    // Create an item and place on ground
    auto sword_id = create_item(item_id{100});
    place_on_ground(sword_id, test_map_, player_pos_);

    auto result = hb::item_ops::pickup_item(
        player_, test_map_, player_pos_, items_, inv_, world_);

    EXPECT_TRUE(result.success);
    EXPECT_EQ(result.picked_up, sword_id);
    EXPECT_TRUE(result.error.empty());

    // Item should be in inventory now
    auto* inv = inv_->get_inventory(player_);
    ASSERT_NE(inv, nullptr);
    EXPECT_TRUE(inv->has_item(sword_id));

    // Item should no longer be on ground
    EXPECT_FALSE(world_->has_ground_items(test_map_, player_pos_));

    // Item owner should be set
    auto* itm = items_->get_item(sword_id);
    ASSERT_NE(itm, nullptr);
    EXPECT_EQ(itm->owner, player_);
}

TEST_F(item_ops_test, pickup_returns_placement_info)
{
    auto sword_id = create_item(item_id{100});
    place_on_ground(sword_id, test_map_, player_pos_);

    auto result = hb::item_ops::pickup_item(
        player_, test_map_, player_pos_, items_, inv_, world_);

    EXPECT_TRUE(result.success);
    // Default placement is (30, 40)
    EXPECT_EQ(result.pos_x, 30);
    EXPECT_EQ(result.pos_y, 40);
    // z_order should be non-negative (first item = 0)
    EXPECT_GE(result.z_order, 0);
}

TEST_F(item_ops_test, pickup_empty_tile)
{
    // No items on ground at this position
    auto result = hb::item_ops::pickup_item(
        player_, test_map_, player_pos_, items_, inv_, world_);

    EXPECT_FALSE(result.success);
    EXPECT_FALSE(result.error.empty());
}

TEST_F(item_ops_test, pickup_full_inventory)
{
    // Fill inventory to capacity (50 slots)
    for (int i = 0; i < 50; ++i)
    {
        auto id = create_item(item_id{200});
        add_to_inventory(player_, id);
    }
    EXPECT_TRUE(inv_->is_full(player_));

    // Place an item on the ground
    auto sword_id = create_item(item_id{100});
    place_on_ground(sword_id, test_map_, player_pos_);

    auto result = hb::item_ops::pickup_item(
        player_, test_map_, player_pos_, items_, inv_, world_);

    EXPECT_FALSE(result.success);
    EXPECT_FALSE(result.error.empty());

    // Item should still be on ground
    EXPECT_TRUE(world_->has_ground_items(test_map_, player_pos_));
}

TEST_F(item_ops_test, pickup_weight_limit)
{
    // Set weight limit: player can carry 5 weight max
    inv_->set_weight(player_, 0, 5);

    // Sword weighs 10 — exceeds limit
    auto sword_id = create_item(item_id{100});
    place_on_ground(sword_id, test_map_, player_pos_);

    auto result = hb::item_ops::pickup_item(
        player_, test_map_, player_pos_, items_, inv_, world_);

    EXPECT_FALSE(result.success);
    EXPECT_NE(result.error.find("heavy"), std::string::npos);

    // Item should still be on ground (put back after weight check)
    EXPECT_TRUE(world_->has_ground_items(test_map_, player_pos_));
}

TEST_F(item_ops_test, pickup_null_subsystems)
{
    auto result = hb::item_ops::pickup_item(
        player_, test_map_, player_pos_, nullptr, inv_, world_);

    EXPECT_FALSE(result.success);
    EXPECT_FALSE(result.error.empty());
}

TEST_F(item_ops_test, pickup_multiple_items_takes_top)
{
    // Place two items on ground — top item should be picked up
    auto sword1 = create_item(item_id{100});
    auto sword2 = create_item(item_id{100});
    place_on_ground(sword1, test_map_, player_pos_);
    place_on_ground(sword2, test_map_, player_pos_);

    auto result = hb::item_ops::pickup_item(
        player_, test_map_, player_pos_, items_, inv_, world_);

    EXPECT_TRUE(result.success);
    // Top item (last added) should be picked up
    EXPECT_EQ(result.picked_up, sword2);

    // One item should remain on ground
    EXPECT_TRUE(world_->has_ground_items(test_map_, player_pos_));
    auto remaining = world_->get_ground_items(test_map_, player_pos_);
    EXPECT_EQ(remaining.size(), 1u);
    EXPECT_EQ(remaining[0], sword1);
}

// ========== Drop Tests ==========

TEST_F(item_ops_test, drop_success)
{
    // Create item owned by player in inventory
    auto sword_id = create_item(item_id{100}, player_);
    add_to_inventory(player_, sword_id);

    auto result = hb::item_ops::drop_item(
        player_, sword_id, test_map_, player_pos_, items_, inv_, world_, &equipment_);

    EXPECT_TRUE(result.success);
    EXPECT_EQ(result.dropped, sword_id);
    EXPECT_FALSE(result.was_equipped);
    EXPECT_FALSE(result.unequipped_slot.has_value());

    // Item should be on ground
    EXPECT_TRUE(world_->has_ground_items(test_map_, player_pos_));
    auto ground = world_->get_ground_items(test_map_, player_pos_);
    EXPECT_EQ(ground.size(), 1u);
    EXPECT_EQ(ground[0], sword_id);

    // Item should not be in inventory
    auto* inv = inv_->get_inventory(player_);
    ASSERT_NE(inv, nullptr);
    EXPECT_FALSE(inv->has_item(sword_id));

    // Owner should be cleared
    auto* itm = items_->get_item(sword_id);
    ASSERT_NE(itm, nullptr);
    EXPECT_FALSE(itm->owner.is_valid());
}

TEST_F(item_ops_test, drop_not_found)
{
    auto result = hb::item_ops::drop_item(
        player_, item_id{9999}, test_map_, player_pos_, items_, inv_, world_, &equipment_);

    EXPECT_FALSE(result.success);
    EXPECT_FALSE(result.error.empty());
}

TEST_F(item_ops_test, drop_not_owned_by_player)
{
    // Create item owned by someone else
    entity_id other_player{entity_id(2u)};
    auto sword_id = create_item(item_id{100}, other_player);

    auto result = hb::item_ops::drop_item(
        player_, sword_id, test_map_, player_pos_, items_, inv_, world_, &equipment_);

    EXPECT_FALSE(result.success);
    EXPECT_NE(result.error.find("not owned"), std::string::npos);
}

TEST_F(item_ops_test, drop_not_droppable)
{
    // Create a quest item and mark it as not droppable
    auto quest_id = create_item(item_id{300}, player_);
    auto* itm = items_->get_item(quest_id);
    ASSERT_NE(itm, nullptr);
    itm->droppable = false;

    add_to_inventory(player_, quest_id);

    auto result = hb::item_ops::drop_item(
        player_, quest_id, test_map_, player_pos_, items_, inv_, world_, &equipment_);

    EXPECT_FALSE(result.success);
    EXPECT_NE(result.error.find("cannot be dropped"), std::string::npos);

    // Item should still be in inventory
    auto* inv = inv_->get_inventory(player_);
    EXPECT_TRUE(inv->has_item(quest_id));
}

TEST_F(item_ops_test, drop_equipped_item)
{
    // Create item, add to inventory, equip it
    auto sword_id = create_item(item_id{100}, player_);
    add_to_inventory(player_, sword_id);
    equipment_.equip(hb::player::equip_slot::weapon, sword_id);

    auto result = hb::item_ops::drop_item(
        player_, sword_id, test_map_, player_pos_, items_, inv_, world_, &equipment_);

    EXPECT_TRUE(result.success);
    EXPECT_EQ(result.dropped, sword_id);
    EXPECT_TRUE(result.was_equipped);
    ASSERT_TRUE(result.unequipped_slot.has_value());
    EXPECT_EQ(result.unequipped_slot.value(), hb::player::equip_slot::weapon);

    // Equipment slot should be empty
    EXPECT_FALSE(equipment_.has_equipped(hb::player::equip_slot::weapon));

    // Item should be on ground, not in inventory
    EXPECT_TRUE(world_->has_ground_items(test_map_, player_pos_));
    auto* inv = inv_->get_inventory(player_);
    EXPECT_FALSE(inv->has_item(sword_id));
}

TEST_F(item_ops_test, drop_null_subsystems)
{
    auto result = hb::item_ops::drop_item(
        player_, item_id{1}, test_map_, player_pos_, nullptr, inv_, world_, &equipment_);

    EXPECT_FALSE(result.success);
    EXPECT_FALSE(result.error.empty());
}

TEST_F(item_ops_test, drop_null_equipment_skips_unequip)
{
    // Drop should work even without equipment_state pointer
    auto sword_id = create_item(item_id{100}, player_);
    add_to_inventory(player_, sword_id);

    auto result = hb::item_ops::drop_item(
        player_, sword_id, test_map_, player_pos_, items_, inv_, world_, nullptr);

    EXPECT_TRUE(result.success);
    EXPECT_FALSE(result.was_equipped);
    EXPECT_FALSE(result.unequipped_slot.has_value());
}

// ========== Equip Tests ==========

TEST_F(item_ops_test, equip_success)
{
    // Create a sword owned by player, add to inventory
    auto sword_id = create_item(item_id{100}, player_);
    add_to_inventory(player_, sword_id);

    auto result = hb::item_ops::equip_item(
        player_, sword_id, hb::player::equip_slot::weapon, items_, inv_, &equipment_);

    EXPECT_TRUE(result.success);
    EXPECT_TRUE(result.error.empty());
    EXPECT_EQ(result.slot, hb::player::equip_slot::weapon);
    EXPECT_EQ(result.equipped, sword_id);
    EXPECT_FALSE(result.swapped_out.has_value());

    // Equipment slot should now have the item
    EXPECT_TRUE(equipment_.has_equipped(hb::player::equip_slot::weapon));
    EXPECT_EQ(equipment_.get_equipped(hb::player::equip_slot::weapon).value(), sword_id);
}

TEST_F(item_ops_test, equip_with_swap)
{
    // Create two swords, put both in inventory, equip first
    auto sword1 = create_item(item_id{100}, player_);
    auto sword2 = create_item(item_id{100}, player_);
    add_to_inventory(player_, sword1);
    add_to_inventory(player_, sword2);
    equipment_.equip(hb::player::equip_slot::weapon, sword1);

    // Equip second sword into same slot — should swap out first
    auto result = hb::item_ops::equip_item(
        player_, sword2, hb::player::equip_slot::weapon, items_, inv_, &equipment_);

    EXPECT_TRUE(result.success);
    EXPECT_EQ(result.equipped, sword2);
    ASSERT_TRUE(result.swapped_out.has_value());
    EXPECT_EQ(result.swapped_out.value(), sword1);

    // Equipment slot should have the new item
    EXPECT_EQ(equipment_.get_equipped(hb::player::equip_slot::weapon).value(), sword2);
}

TEST_F(item_ops_test, equip_broken_item)
{
    // Create a sword, break it (set durability to 0)
    auto sword_id = create_item(item_id{100}, player_);
    auto* itm = items_->get_item(sword_id);
    ASSERT_NE(itm, nullptr);
    itm->durability = 0;
    itm->owner = player_;
    add_to_inventory(player_, sword_id);

    auto result = hb::item_ops::equip_item(
        player_, sword_id, hb::player::equip_slot::weapon, items_, inv_, &equipment_);

    EXPECT_FALSE(result.success);
    EXPECT_NE(result.error.find("broken"), std::string::npos);

    // Slot should remain empty
    EXPECT_FALSE(equipment_.has_equipped(hb::player::equip_slot::weapon));
}

TEST_F(item_ops_test, equip_non_equippable)
{
    // Potion (template 200) has equip_pos=none
    auto potion_id = create_item(item_id{200}, player_);
    add_to_inventory(player_, potion_id);

    auto result = hb::item_ops::equip_item(
        player_, potion_id, hb::player::equip_slot::weapon, items_, inv_, &equipment_);

    EXPECT_FALSE(result.success);
    EXPECT_NE(result.error.find("not equippable"), std::string::npos);
}

TEST_F(item_ops_test, equip_item_not_in_inventory)
{
    // Create item owned by player but don't add to inventory
    auto sword_id = create_item(item_id{100}, player_);

    auto result = hb::item_ops::equip_item(
        player_, sword_id, hb::player::equip_slot::weapon, items_, inv_, &equipment_);

    EXPECT_FALSE(result.success);
    EXPECT_NE(result.error.find("not in inventory"), std::string::npos);
}

TEST_F(item_ops_test, equip_item_not_owned)
{
    // Create item owned by someone else, add to their inventory
    entity_id other{entity_id(2u)};
    auto sword_id = create_item(item_id{100}, other);

    auto result = hb::item_ops::equip_item(
        player_, sword_id, hb::player::equip_slot::weapon, items_, inv_, &equipment_);

    EXPECT_FALSE(result.success);
    EXPECT_NE(result.error.find("not owned"), std::string::npos);
}

// ========== Unequip Tests ==========

TEST_F(item_ops_test, unequip_success)
{
    // Create sword, add to inventory, equip it
    auto sword_id = create_item(item_id{100}, player_);
    add_to_inventory(player_, sword_id);
    equipment_.equip(hb::player::equip_slot::weapon, sword_id);

    auto result = hb::item_ops::unequip_item(
        player_, hb::player::equip_slot::weapon, &equipment_);

    EXPECT_TRUE(result.success);
    EXPECT_TRUE(result.error.empty());
    EXPECT_EQ(result.slot, hb::player::equip_slot::weapon);
    EXPECT_EQ(result.unequipped, sword_id);

    // Slot should be empty
    EXPECT_FALSE(equipment_.has_equipped(hb::player::equip_slot::weapon));

    // Item should still be in inventory (linked model)
    auto* inv = inv_->get_inventory(player_);
    ASSERT_NE(inv, nullptr);
    EXPECT_TRUE(inv->has_item(sword_id));
}

TEST_F(item_ops_test, unequip_empty_slot)
{
    auto result = hb::item_ops::unequip_item(
        player_, hb::player::equip_slot::weapon, &equipment_);

    EXPECT_FALSE(result.success);
    EXPECT_NE(result.error.find("empty"), std::string::npos);
}

// ========== Force Unequip Tests ==========

TEST_F(item_ops_test, force_unequip_broken)
{
    // Create sword, equip it
    auto sword_id = create_item(item_id{100}, player_);
    add_to_inventory(player_, sword_id);
    equipment_.equip(hb::player::equip_slot::weapon, sword_id);

    auto result = hb::item_ops::force_unequip(
        player_, hb::player::equip_slot::weapon,
        hb::item_ops::force_unequip_reason::broken, &equipment_);

    EXPECT_TRUE(result.success);
    EXPECT_EQ(result.slot, hb::player::equip_slot::weapon);
    EXPECT_EQ(result.unequipped, sword_id);
    EXPECT_EQ(result.reason, hb::item_ops::force_unequip_reason::broken);

    // Slot should be empty
    EXPECT_FALSE(equipment_.has_equipped(hb::player::equip_slot::weapon));
}

TEST_F(item_ops_test, force_unequip_empty_slot)
{
    auto result = hb::item_ops::force_unequip(
        player_, hb::player::equip_slot::weapon,
        hb::item_ops::force_unequip_reason::broken, &equipment_);

    EXPECT_FALSE(result.success);
}

TEST_F(item_ops_test, force_unequip_reason_propagated)
{
    auto sword_id = create_item(item_id{100}, player_);
    add_to_inventory(player_, sword_id);
    equipment_.equip(hb::player::equip_slot::weapon, sword_id);

    // Test with hammer_strip reason
    auto result = hb::item_ops::force_unequip(
        player_, hb::player::equip_slot::weapon,
        hb::item_ops::force_unequip_reason::hammer_strip, &equipment_);

    EXPECT_TRUE(result.success);
    EXPECT_EQ(result.reason, hb::item_ops::force_unequip_reason::hammer_strip);
}

// ========== Shop Buy Tests ==========

TEST_F(item_ops_test, shop_buy_success)
{
    // Give player some gold
    inv_->add_gold(player_, 500);

    auto result = hb::item_ops::shop_buy(
        player_, item_id{100}, 100, items_, inv_);

    EXPECT_TRUE(result.success);
    EXPECT_TRUE(result.error.empty());
    EXPECT_TRUE(result.created.is_valid());

    // Gold should be deducted
    EXPECT_EQ(result.new_gold, 400);
    EXPECT_EQ(inv_->get_gold(player_), 400);

    // Item should be in inventory
    auto* inv = inv_->get_inventory(player_);
    ASSERT_NE(inv, nullptr);
    EXPECT_TRUE(inv->has_item(result.created));

    // Item should be owned by player
    auto* itm = items_->get_item(result.created);
    ASSERT_NE(itm, nullptr);
    EXPECT_EQ(itm->owner, player_);

    // Default placement (30, 40)
    EXPECT_EQ(result.pos_x, 30);
    EXPECT_EQ(result.pos_y, 40);
}

TEST_F(item_ops_test, shop_buy_not_enough_gold)
{
    // Give player only 50 gold, sword costs 100
    inv_->add_gold(player_, 50);

    auto result = hb::item_ops::shop_buy(
        player_, item_id{100}, 100, items_, inv_);

    EXPECT_FALSE(result.success);
    EXPECT_NE(result.error.find("gold"), std::string::npos);

    // Gold should be unchanged
    EXPECT_EQ(inv_->get_gold(player_), 50);
}

TEST_F(item_ops_test, shop_buy_inventory_full)
{
    inv_->add_gold(player_, 500);

    // Fill inventory to capacity (50 slots)
    for (int i = 0; i < 50; ++i)
    {
        auto id = create_item(item_id{200});
        add_to_inventory(player_, id);
    }
    EXPECT_TRUE(inv_->is_full(player_));

    auto result = hb::item_ops::shop_buy(
        player_, item_id{100}, 100, items_, inv_);

    EXPECT_FALSE(result.success);
    EXPECT_NE(result.error.find("full"), std::string::npos);

    // Gold should be unchanged
    EXPECT_EQ(inv_->get_gold(player_), 500);
}

// ========== Shop Sell Tests ==========

TEST_F(item_ops_test, shop_sell_success)
{
    // Create a sword owned by player in inventory
    auto sword_id = create_item(item_id{100}, player_);
    add_to_inventory(player_, sword_id);

    auto result = hb::item_ops::shop_sell(
        player_, sword_id, items_, inv_, &equipment_);

    EXPECT_TRUE(result.success);
    EXPECT_TRUE(result.error.empty());
    EXPECT_EQ(result.sold, sword_id);

    // Gold should increase by item price (100)
    EXPECT_EQ(result.new_gold, 100);
    EXPECT_EQ(inv_->get_gold(player_), 100);

    // Item should no longer be in inventory
    auto* inv = inv_->get_inventory(player_);
    ASSERT_NE(inv, nullptr);
    EXPECT_FALSE(inv->has_item(sword_id));

    // Item instance should be destroyed
    EXPECT_EQ(items_->get_item(sword_id), nullptr);
}

TEST_F(item_ops_test, shop_sell_non_tradeable)
{
    // Create item and mark as non-tradeable
    auto sword_id = create_item(item_id{100}, player_);
    auto* itm = items_->get_item(sword_id);
    ASSERT_NE(itm, nullptr);
    itm->tradeable = false;
    add_to_inventory(player_, sword_id);

    auto result = hb::item_ops::shop_sell(
        player_, sword_id, items_, inv_, &equipment_);

    EXPECT_FALSE(result.success);
    EXPECT_NE(result.error.find("tradeable"), std::string::npos);

    // Item should still be in inventory
    auto* inv = inv_->get_inventory(player_);
    EXPECT_TRUE(inv->has_item(sword_id));

    // No gold gained
    EXPECT_EQ(inv_->get_gold(player_), 0);
}

TEST_F(item_ops_test, shop_sell_equipped_item)
{
    // Create sword, add to inventory, equip it
    auto sword_id = create_item(item_id{100}, player_);
    add_to_inventory(player_, sword_id);
    equipment_.equip(hb::player::equip_slot::weapon, sword_id);

    auto result = hb::item_ops::shop_sell(
        player_, sword_id, items_, inv_, &equipment_);

    EXPECT_TRUE(result.success);
    EXPECT_EQ(result.sold, sword_id);
    EXPECT_EQ(result.new_gold, 100);

    // Equipment slot should be empty (auto-unequipped)
    EXPECT_FALSE(equipment_.has_equipped(hb::player::equip_slot::weapon));

    // Item should be destroyed
    EXPECT_EQ(items_->get_item(sword_id), nullptr);
}

// ========== Shop Repair Tests ==========

TEST_F(item_ops_test, shop_repair_success)
{
    inv_->add_gold(player_, 500);

    // Create a sword and damage it
    auto sword_id = create_item(item_id{100}, player_);
    auto* itm = items_->get_item(sword_id);
    ASSERT_NE(itm, nullptr);
    itm->durability = 100; // Half damaged (max is 200)
    add_to_inventory(player_, sword_id);

    auto result = hb::item_ops::shop_repair(
        player_, sword_id, items_, inv_);

    EXPECT_TRUE(result.success);
    EXPECT_TRUE(result.error.empty());
    EXPECT_EQ(result.repaired, sword_id);

    // Durability should be restored to max
    itm = items_->get_item(sword_id);
    ASSERT_NE(itm, nullptr);
    EXPECT_EQ(itm->durability, itm->max_durability);

    // Gold should be deducted (repair cost = price * damage/max = 100 * 100/200 = 50)
    EXPECT_EQ(result.new_gold, 450);
    EXPECT_EQ(inv_->get_gold(player_), 450);
}

TEST_F(item_ops_test, shop_repair_undamaged_item)
{
    inv_->add_gold(player_, 500);

    // Create a sword at full durability
    auto sword_id = create_item(item_id{100}, player_);
    add_to_inventory(player_, sword_id);

    auto result = hb::item_ops::shop_repair(
        player_, sword_id, items_, inv_);

    EXPECT_FALSE(result.success);
    EXPECT_NE(result.error.find("not damaged"), std::string::npos);

    // Gold should be unchanged
    EXPECT_EQ(inv_->get_gold(player_), 500);
}

TEST_F(item_ops_test, shop_repair_not_enough_gold)
{
    // Give just 1 gold — not enough for a repair
    inv_->add_gold(player_, 1);

    // Create a sword and damage it heavily
    auto sword_id = create_item(item_id{100}, player_);
    auto* itm = items_->get_item(sword_id);
    ASSERT_NE(itm, nullptr);
    itm->durability = 0; // Fully broken (repair cost = 100 * 200/200 = 100)
    add_to_inventory(player_, sword_id);

    auto result = hb::item_ops::shop_repair(
        player_, sword_id, items_, inv_);

    EXPECT_FALSE(result.success);
    EXPECT_NE(result.error.find("gold"), std::string::npos);

    // Durability should be unchanged
    itm = items_->get_item(sword_id);
    ASSERT_NE(itm, nullptr);
    EXPECT_EQ(itm->durability, 0);

    // Gold should be unchanged
    EXPECT_EQ(inv_->get_gold(player_), 1);
}

// ========== Bank Deposit Tests ==========

TEST_F(item_ops_test, bank_deposit_auto_success)
{
    // Create bank for the player
    inv_->create_bank(player_);

    // Create item owned by player in inventory
    auto sword_id = create_item(item_id{100}, player_);
    add_to_inventory(player_, sword_id);

    auto result = hb::item_ops::bank_deposit(
        player_, sword_id, std::nullopt, std::nullopt, items_, inv_, &equipment_);

    EXPECT_TRUE(result.success);
    EXPECT_TRUE(result.error.empty());
    EXPECT_EQ(result.deposited, sword_id);
    // First empty slot should be page 0, slot 0
    EXPECT_EQ(result.page, 0);
    EXPECT_EQ(result.slot, 0);

    // Item should be in bank
    auto* bank = inv_->get_bank(player_);
    ASSERT_NE(bank, nullptr);
    auto bank_item = bank->get_slot(0, 0);
    ASSERT_TRUE(bank_item.has_value());
    EXPECT_EQ(bank_item.value(), sword_id);

    // Item should NOT be in inventory
    auto* inv = inv_->get_inventory(player_);
    ASSERT_NE(inv, nullptr);
    EXPECT_FALSE(inv->has_item(sword_id));
}

TEST_F(item_ops_test, bank_deposit_targeted_success)
{
    inv_->create_bank(player_);

    auto sword_id = create_item(item_id{100}, player_);
    add_to_inventory(player_, sword_id);

    // Deposit to page 2, slot 5
    auto result = hb::item_ops::bank_deposit(
        player_, sword_id, int16_t{2}, int16_t{5}, items_, inv_, &equipment_);

    EXPECT_TRUE(result.success);
    EXPECT_EQ(result.deposited, sword_id);
    EXPECT_EQ(result.page, 2);
    EXPECT_EQ(result.slot, 5);

    // Item should be in the specified bank slot
    auto* bank = inv_->get_bank(player_);
    ASSERT_NE(bank, nullptr);
    auto bank_item = bank->get_slot(2, 5);
    ASSERT_TRUE(bank_item.has_value());
    EXPECT_EQ(bank_item.value(), sword_id);
}

TEST_F(item_ops_test, bank_deposit_full_bank)
{
    inv_->create_bank(player_);

    auto* bank = inv_->get_bank(player_);
    ASSERT_NE(bank, nullptr);

    // Fill every bank slot
    auto total = bank->total_capacity();
    for (int16_t i = 0; i < total; ++i)
    {
        auto filler = create_item(item_id{200}, player_);
        int16_t page = i / bank->slots_per_page();
        int16_t slot = i % bank->slots_per_page();
        bank->set_slot(page, slot, filler);
    }
    EXPECT_TRUE(bank->is_full());

    // Try to deposit another item
    auto sword_id = create_item(item_id{100}, player_);
    add_to_inventory(player_, sword_id);

    auto result = hb::item_ops::bank_deposit(
        player_, sword_id, std::nullopt, std::nullopt, items_, inv_, &equipment_);

    EXPECT_FALSE(result.success);
    EXPECT_NE(result.error.find("full"), std::string::npos);

    // Item should still be in inventory
    auto* inv = inv_->get_inventory(player_);
    EXPECT_TRUE(inv->has_item(sword_id));
}

TEST_F(item_ops_test, bank_deposit_equipped_item)
{
    inv_->create_bank(player_);

    // Create sword, add to inventory, equip it
    auto sword_id = create_item(item_id{100}, player_);
    add_to_inventory(player_, sword_id);
    equipment_.equip(hb::player::equip_slot::weapon, sword_id);

    auto result = hb::item_ops::bank_deposit(
        player_, sword_id, std::nullopt, std::nullopt, items_, inv_, &equipment_);

    EXPECT_TRUE(result.success);
    EXPECT_EQ(result.deposited, sword_id);

    // Equipment slot should be empty (auto-unequipped)
    EXPECT_FALSE(equipment_.has_equipped(hb::player::equip_slot::weapon));

    // Item should be in bank, not inventory
    auto* bank = inv_->get_bank(player_);
    ASSERT_NE(bank, nullptr);
    EXPECT_TRUE(bank->has_item(sword_id));

    auto* inv = inv_->get_inventory(player_);
    EXPECT_FALSE(inv->has_item(sword_id));
}

// ========== Bank Withdraw Tests ==========

TEST_F(item_ops_test, bank_withdraw_success)
{
    inv_->create_bank(player_);

    // Create item and put it directly in the bank
    auto sword_id = create_item(item_id{100}, player_);
    auto* bank = inv_->get_bank(player_);
    ASSERT_NE(bank, nullptr);
    bank->set_slot(1, 3, sword_id);

    auto result = hb::item_ops::bank_withdraw(
        player_, 1, 3, items_, inv_);

    EXPECT_TRUE(result.success);
    EXPECT_TRUE(result.error.empty());
    EXPECT_EQ(result.withdrawn, sword_id);
    // Default placement (30, 40)
    EXPECT_EQ(result.pos_x, 30);
    EXPECT_EQ(result.pos_y, 40);
    EXPECT_GE(result.z_order, 0);

    // Item should be in inventory
    auto* inv = inv_->get_inventory(player_);
    ASSERT_NE(inv, nullptr);
    EXPECT_TRUE(inv->has_item(sword_id));

    // Bank slot should be empty
    auto bank_item = bank->get_slot(1, 3);
    EXPECT_FALSE(bank_item.has_value());
}

TEST_F(item_ops_test, bank_withdraw_empty_slot)
{
    inv_->create_bank(player_);

    auto result = hb::item_ops::bank_withdraw(
        player_, 0, 0, items_, inv_);

    EXPECT_FALSE(result.success);
    EXPECT_NE(result.error.find("empty"), std::string::npos);
}

TEST_F(item_ops_test, bank_withdraw_inventory_full)
{
    inv_->create_bank(player_);

    // Fill inventory to capacity (50 slots)
    for (int i = 0; i < 50; ++i)
    {
        auto id = create_item(item_id{200});
        add_to_inventory(player_, id);
    }
    EXPECT_TRUE(inv_->is_full(player_));

    // Put item in bank
    auto sword_id = create_item(item_id{100}, player_);
    auto* bank = inv_->get_bank(player_);
    ASSERT_NE(bank, nullptr);
    bank->set_slot(0, 0, sword_id);

    auto result = hb::item_ops::bank_withdraw(
        player_, 0, 0, items_, inv_);

    EXPECT_FALSE(result.success);
    EXPECT_NE(result.error.find("full"), std::string::npos);

    // Item should still be in bank
    auto bank_item = bank->get_slot(0, 0);
    ASSERT_TRUE(bank_item.has_value());
    EXPECT_EQ(bank_item.value(), sword_id);
}

// ========== Bank Reposition Tests ==========

TEST_F(item_ops_test, bank_reposition_move_to_empty)
{
    inv_->create_bank(player_);

    auto sword_id = create_item(item_id{100}, player_);
    auto* bank = inv_->get_bank(player_);
    ASSERT_NE(bank, nullptr);
    bank->set_slot(0, 0, sword_id);

    // Move from (0,0) to (1,5)
    auto result = hb::item_ops::bank_reposition(
        player_, 0, 0, 1, 5, inv_);

    EXPECT_TRUE(result.success);
    EXPECT_TRUE(result.error.empty());

    // Source should be empty
    auto source = bank->get_slot(0, 0);
    EXPECT_FALSE(source.has_value());

    // Dest should have the item
    auto dest = bank->get_slot(1, 5);
    ASSERT_TRUE(dest.has_value());
    EXPECT_EQ(dest.value(), sword_id);
}

TEST_F(item_ops_test, bank_reposition_swap)
{
    inv_->create_bank(player_);

    auto sword_id = create_item(item_id{100}, player_);
    auto potion_id = create_item(item_id{200}, player_);
    auto* bank = inv_->get_bank(player_);
    ASSERT_NE(bank, nullptr);
    bank->set_slot(0, 0, sword_id);
    bank->set_slot(2, 3, potion_id);

    // Swap (0,0) and (2,3)
    auto result = hb::item_ops::bank_reposition(
        player_, 0, 0, 2, 3, inv_);

    EXPECT_TRUE(result.success);

    // Items should be swapped
    auto slot_a = bank->get_slot(0, 0);
    ASSERT_TRUE(slot_a.has_value());
    EXPECT_EQ(slot_a.value(), potion_id);

    auto slot_b = bank->get_slot(2, 3);
    ASSERT_TRUE(slot_b.has_value());
    EXPECT_EQ(slot_b.value(), sword_id);
}

// ========== Trade Tests ==========

TEST_F(item_ops_test, trade_successful_items_and_gold)
{
    // Set up second player
    entity_id player_b{entity_id(2u)};
    inv_->create_inventory(player_b);
    inv_->add_gold(player_, 10000);
    inv_->add_gold(player_b, 10000);

    // Create items: player A has sword, player B has potion
    auto sword_id = create_item(item_id{100}, player_);
    add_to_inventory(player_, sword_id);

    auto potion_id = create_item(item_id{200}, player_b);
    auto potion_add = inv_->add_item(player_b, potion_id, 1);
    EXPECT_EQ(potion_add, hb::inventory::inventory_result::success);

    // Start trade, offer items and gold
    inv_->start_trade(player_, player_b);

    auto* window_a = inv_->get_trade_window(player_);
    ASSERT_NE(window_a, nullptr);
    window_a->add_item(sword_id);
    window_a->set_gold(100);

    auto* window_b = inv_->get_trade_window(player_b);
    ASSERT_NE(window_b, nullptr);
    window_b->add_item(potion_id);

    // Lock and confirm both
    window_a->lock();
    window_b->lock();
    window_a->confirm();
    window_b->confirm();

    // Execute trade
    auto result = hb::item_ops::execute_trade(player_, player_b, items_, inv_);

    EXPECT_TRUE(result.success);
    EXPECT_TRUE(result.error.empty());

    // Player A: lost sword, gained potion, lost 100 gold
    EXPECT_EQ(result.player_a.lost_items.size(), 1u);
    EXPECT_EQ(result.player_a.lost_items[0], sword_id);
    EXPECT_EQ(result.player_a.received_items.size(), 1u);
    EXPECT_EQ(result.player_a.received_items[0], potion_id);
    EXPECT_EQ(result.player_a.gold_change, -100);

    // Player B: lost potion, gained sword, gained 100 gold
    EXPECT_EQ(result.player_b.lost_items.size(), 1u);
    EXPECT_EQ(result.player_b.lost_items[0], potion_id);
    EXPECT_EQ(result.player_b.received_items.size(), 1u);
    EXPECT_EQ(result.player_b.received_items[0], sword_id);
    EXPECT_EQ(result.player_b.gold_change, 100);

    // Verify inventories
    auto* inv_a = inv_->get_inventory(player_);
    ASSERT_NE(inv_a, nullptr);
    EXPECT_FALSE(inv_a->has_item(sword_id));
    EXPECT_TRUE(inv_a->has_item(potion_id));

    auto* inv_b = inv_->get_inventory(player_b);
    ASSERT_NE(inv_b, nullptr);
    EXPECT_TRUE(inv_b->has_item(sword_id));
    EXPECT_FALSE(inv_b->has_item(potion_id));

    // Verify gold
    EXPECT_EQ(inv_->get_gold(player_), 9900);
    EXPECT_EQ(inv_->get_gold(player_b), 10100);

    // Verify ownership transferred
    auto* sword_item = items_->get_item(sword_id);
    ASSERT_NE(sword_item, nullptr);
    EXPECT_EQ(sword_item->owner, player_b);

    auto* potion_item = items_->get_item(potion_id);
    ASSERT_NE(potion_item, nullptr);
    EXPECT_EQ(potion_item->owner, player_);

    // Verify placements
    EXPECT_EQ(result.player_a.placements.size(), 1u);
    EXPECT_EQ(result.player_a.placements[0].item, potion_id);
    EXPECT_EQ(result.player_a.placements[0].pos_x, 30);
    EXPECT_EQ(result.player_a.placements[0].pos_y, 40);

    EXPECT_EQ(result.player_b.placements.size(), 1u);
    EXPECT_EQ(result.player_b.placements[0].item, sword_id);
    EXPECT_EQ(result.player_b.placements[0].pos_x, 30);
    EXPECT_EQ(result.player_b.placements[0].pos_y, 40);

    // Trade windows should be cleaned up
    EXPECT_EQ(inv_->get_trade_window(player_), nullptr);
    EXPECT_EQ(inv_->get_trade_window(player_b), nullptr);
}

TEST_F(item_ops_test, trade_not_confirmed_fails)
{
    entity_id player_b{entity_id(2u)};
    inv_->create_inventory(player_b);
    inv_->add_gold(player_, 10000);
    inv_->add_gold(player_b, 10000);

    auto sword_id = create_item(item_id{100}, player_);
    add_to_inventory(player_, sword_id);

    inv_->start_trade(player_, player_b);

    auto* window_a = inv_->get_trade_window(player_);
    auto* window_b = inv_->get_trade_window(player_b);
    ASSERT_NE(window_a, nullptr);
    ASSERT_NE(window_b, nullptr);

    window_a->add_item(sword_id);

    // Lock both but only confirm A
    window_a->lock();
    window_b->lock();
    window_a->confirm();
    // window_b NOT confirmed

    auto result = hb::item_ops::execute_trade(player_, player_b, items_, inv_);

    EXPECT_FALSE(result.success);
    EXPECT_NE(result.error.find("not confirmed"), std::string::npos);

    // Sword should still be in A's inventory
    auto* inv_a = inv_->get_inventory(player_);
    ASSERT_NE(inv_a, nullptr);
    EXPECT_TRUE(inv_a->has_item(sword_id));
}

TEST_F(item_ops_test, trade_not_locked_fails)
{
    entity_id player_b{entity_id(2u)};
    inv_->create_inventory(player_b);
    inv_->add_gold(player_, 10000);
    inv_->add_gold(player_b, 10000);

    auto sword_id = create_item(item_id{100}, player_);
    add_to_inventory(player_, sword_id);

    inv_->start_trade(player_, player_b);

    auto* window_a = inv_->get_trade_window(player_);
    auto* window_b = inv_->get_trade_window(player_b);
    ASSERT_NE(window_a, nullptr);
    ASSERT_NE(window_b, nullptr);

    window_a->add_item(sword_id);

    // Confirm both but don't lock
    window_a->confirm();
    window_b->confirm();

    auto result = hb::item_ops::execute_trade(player_, player_b, items_, inv_);

    EXPECT_FALSE(result.success);
    EXPECT_NE(result.error.find("not locked"), std::string::npos);

    // Sword should still be in A's inventory
    auto* inv_a = inv_->get_inventory(player_);
    ASSERT_NE(inv_a, nullptr);
    EXPECT_TRUE(inv_a->has_item(sword_id));
}

TEST_F(item_ops_test, trade_gold_only)
{
    entity_id player_b{entity_id(2u)};
    inv_->create_inventory(player_b);
    inv_->add_gold(player_, 10000);
    inv_->add_gold(player_b, 10000);

    inv_->start_trade(player_, player_b);

    auto* window_a = inv_->get_trade_window(player_);
    auto* window_b = inv_->get_trade_window(player_b);
    ASSERT_NE(window_a, nullptr);
    ASSERT_NE(window_b, nullptr);

    // A offers 500 gold, B offers nothing
    window_a->set_gold(500);

    window_a->lock();
    window_b->lock();
    window_a->confirm();
    window_b->confirm();

    auto result = hb::item_ops::execute_trade(player_, player_b, items_, inv_);

    EXPECT_TRUE(result.success);

    // No items transferred
    EXPECT_TRUE(result.player_a.lost_items.empty());
    EXPECT_TRUE(result.player_a.received_items.empty());
    EXPECT_TRUE(result.player_b.lost_items.empty());
    EXPECT_TRUE(result.player_b.received_items.empty());
    EXPECT_TRUE(result.player_a.placements.empty());
    EXPECT_TRUE(result.player_b.placements.empty());

    // Gold transferred
    EXPECT_EQ(result.player_a.gold_change, -500);
    EXPECT_EQ(result.player_b.gold_change, 500);

    EXPECT_EQ(inv_->get_gold(player_), 9500);
    EXPECT_EQ(inv_->get_gold(player_b), 10500);
}

TEST_F(item_ops_test, trade_multi_item)
{
    entity_id player_b{entity_id(2u)};
    inv_->create_inventory(player_b);
    inv_->add_gold(player_, 10000);
    inv_->add_gold(player_b, 10000);

    // A offers 2 items, B offers 1 item
    auto sword1 = create_item(item_id{100}, player_);
    auto sword2 = create_item(item_id{100}, player_);
    add_to_inventory(player_, sword1);
    add_to_inventory(player_, sword2);

    auto potion_id = create_item(item_id{200}, player_b);
    auto potion_add = inv_->add_item(player_b, potion_id, 1);
    EXPECT_EQ(potion_add, hb::inventory::inventory_result::success);

    inv_->start_trade(player_, player_b);

    auto* window_a = inv_->get_trade_window(player_);
    auto* window_b = inv_->get_trade_window(player_b);
    ASSERT_NE(window_a, nullptr);
    ASSERT_NE(window_b, nullptr);

    window_a->add_item(sword1);
    window_a->add_item(sword2);
    window_b->add_item(potion_id);

    window_a->lock();
    window_b->lock();
    window_a->confirm();
    window_b->confirm();

    auto result = hb::item_ops::execute_trade(player_, player_b, items_, inv_);

    EXPECT_TRUE(result.success);

    // A lost 2 items, received 1
    EXPECT_EQ(result.player_a.lost_items.size(), 2u);
    EXPECT_EQ(result.player_a.received_items.size(), 1u);
    EXPECT_EQ(result.player_a.received_items[0], potion_id);

    // B lost 1, received 2
    EXPECT_EQ(result.player_b.lost_items.size(), 1u);
    EXPECT_EQ(result.player_b.lost_items[0], potion_id);
    EXPECT_EQ(result.player_b.received_items.size(), 2u);

    // Verify all items in correct inventories
    auto* inv_a = inv_->get_inventory(player_);
    auto* inv_b = inv_->get_inventory(player_b);
    ASSERT_NE(inv_a, nullptr);
    ASSERT_NE(inv_b, nullptr);

    EXPECT_FALSE(inv_a->has_item(sword1));
    EXPECT_FALSE(inv_a->has_item(sword2));
    EXPECT_TRUE(inv_a->has_item(potion_id));

    EXPECT_TRUE(inv_b->has_item(sword1));
    EXPECT_TRUE(inv_b->has_item(sword2));
    EXPECT_FALSE(inv_b->has_item(potion_id));

    // Verify ownership
    EXPECT_EQ(items_->get_item(sword1)->owner, player_b);
    EXPECT_EQ(items_->get_item(sword2)->owner, player_b);
    EXPECT_EQ(items_->get_item(potion_id)->owner, player_);

    // Placements: A got 1 item, B got 2
    EXPECT_EQ(result.player_a.placements.size(), 1u);
    EXPECT_EQ(result.player_b.placements.size(), 2u);
}

// ========== Upgrade Item Tests ==========

TEST_F(item_ops_test, upgrade_item_success)
{
    // Create a sword and an upgrade stone, both owned by the player
    auto sword_id = create_item(item_id{100}, player_);
    auto stone_id = create_item(item_id{400}, player_);
    add_to_inventory(player_, sword_id);
    add_to_inventory(player_, stone_id);

    // Verify initial upgrade level is 0
    auto* itm = items_->get_item(sword_id);
    ASSERT_NE(itm, nullptr);
    EXPECT_EQ(itm->attribute.upgrade_level, 0);

    auto result = hb::item_ops::upgrade_item(
        player_, sword_id, stone_id, items_, inv_);

    EXPECT_TRUE(result.success);
    EXPECT_TRUE(result.error.empty());
    EXPECT_EQ(result.target, sword_id);
    EXPECT_EQ(result.stone, stone_id);

    // Upgrade level should be incremented
    itm = items_->get_item(sword_id);
    ASSERT_NE(itm, nullptr);
    EXPECT_EQ(itm->attribute.upgrade_level, 1);

    // Stone should be destroyed
    EXPECT_EQ(items_->get_item(stone_id), nullptr);

    // Stone should be removed from inventory
    auto* inv = inv_->get_inventory(player_);
    ASSERT_NE(inv, nullptr);
    EXPECT_FALSE(inv->has_item(stone_id));

    // Sword should still be in inventory
    EXPECT_TRUE(inv->has_item(sword_id));
}

TEST_F(item_ops_test, upgrade_item_target_not_equipment)
{
    // Potion (template 200) is consumable, not equipment
    auto potion_id = create_item(item_id{200}, player_);
    auto stone_id = create_item(item_id{400}, player_);
    add_to_inventory(player_, potion_id);
    add_to_inventory(player_, stone_id);

    auto result = hb::item_ops::upgrade_item(
        player_, potion_id, stone_id, items_, inv_);

    EXPECT_FALSE(result.success);
    EXPECT_NE(result.error.find("not equipment"), std::string::npos);

    // Stone should NOT be consumed on failure
    EXPECT_NE(items_->get_item(stone_id), nullptr);
}

TEST_F(item_ops_test, upgrade_item_stone_not_found)
{
    auto sword_id = create_item(item_id{100}, player_);
    add_to_inventory(player_, sword_id);

    auto result = hb::item_ops::upgrade_item(
        player_, sword_id, item_id{9999}, items_, inv_);

    EXPECT_FALSE(result.success);
    EXPECT_NE(result.error.find("Stone not found"), std::string::npos);

    // Sword upgrade level should be unchanged
    auto* itm = items_->get_item(sword_id);
    ASSERT_NE(itm, nullptr);
    EXPECT_EQ(itm->attribute.upgrade_level, 0);
}

// ========== Activate Ability Tests ==========

TEST_F(item_ops_test, activate_ability_success)
{
    // Create a special ability sword, add to inventory, equip it
    auto sword_id = create_item(item_id{500}, player_);
    add_to_inventory(player_, sword_id);
    equipment_.equip(hb::player::equip_slot::weapon, sword_id);

    auto result = hb::item_ops::activate_ability(
        player_, sword_id, items_, &equipment_);

    EXPECT_TRUE(result.success);
    EXPECT_TRUE(result.error.empty());
    EXPECT_EQ(result.ability, hb::item::special_ability_type::life_drain);
    EXPECT_EQ(result.duration_ms, hb::item::ability_cooldown_seconds * 1000);
}

TEST_F(item_ops_test, activate_ability_not_equipped)
{
    // Create a special ability sword but don't equip it
    auto sword_id = create_item(item_id{500}, player_);
    add_to_inventory(player_, sword_id);

    auto result = hb::item_ops::activate_ability(
        player_, sword_id, items_, &equipment_);

    EXPECT_FALSE(result.success);
    EXPECT_EQ(result.error, "not_equipped");
}

TEST_F(item_ops_test, activate_ability_no_ability)
{
    // Create a regular sword (template 100, no special_effect), equip it
    auto sword_id = create_item(item_id{100}, player_);
    add_to_inventory(player_, sword_id);
    equipment_.equip(hb::player::equip_slot::weapon, sword_id);

    auto result = hb::item_ops::activate_ability(
        player_, sword_id, items_, &equipment_);

    EXPECT_FALSE(result.success);
    EXPECT_EQ(result.error, "no_ability");
}

// ========== Drop Loot Tests ==========

TEST_F(item_ops_test, drop_loot_single_item)
{
    // Create an item (owner set to some NPC entity initially)
    auto sword_id = create_item(item_id{100}, entity_id(99u));

    auto result = hb::item_ops::drop_loot(
        sword_id, test_map_, 15, 25, items_, world_);

    // Should report correct position
    EXPECT_EQ(result.dropped, sword_id);
    EXPECT_EQ(result.ground_x, 15);
    EXPECT_EQ(result.ground_y, 25);

    // Item should be on ground
    hb::world::position pos{15, 25};
    EXPECT_TRUE(world_->has_ground_items(test_map_, pos));
    auto ground = world_->get_ground_items(test_map_, pos);
    EXPECT_EQ(ground.size(), 1u);
    EXPECT_EQ(ground[0], sword_id);

    // Owner should be cleared
    auto* itm = items_->get_item(sword_id);
    ASSERT_NE(itm, nullptr);
    EXPECT_FALSE(itm->owner.is_valid());
}

// ========== Drop Loot Multi Tests ==========

TEST_F(item_ops_test, drop_loot_multi_3_items_spread)
{
    // Create 3 items
    auto id1 = create_item(item_id{100});
    auto id2 = create_item(item_id{200});
    auto id3 = create_item(item_id{100});

    std::vector<item_id> ids = {id1, id2, id3};

    auto results = hb::item_ops::drop_loot_multi(
        ids, test_map_, 50, 50, items_, world_);

    ASSERT_EQ(results.size(), 3u);

    // First 3 grid positions: (-1,-1), (0,-1), (1,-1) relative to center (50,50)
    EXPECT_EQ(results[0].dropped, id1);
    EXPECT_EQ(results[0].ground_x, 49);
    EXPECT_EQ(results[0].ground_y, 49);

    EXPECT_EQ(results[1].dropped, id2);
    EXPECT_EQ(results[1].ground_x, 50);
    EXPECT_EQ(results[1].ground_y, 49);

    EXPECT_EQ(results[2].dropped, id3);
    EXPECT_EQ(results[2].ground_x, 51);
    EXPECT_EQ(results[2].ground_y, 49);

    // Each item should be on ground at its position
    for (const auto& r : results)
    {
        hb::world::position pos{r.ground_x, r.ground_y};
        EXPECT_TRUE(world_->has_ground_items(test_map_, pos));
    }

    // All owners should be cleared
    for (auto id : ids)
    {
        auto* itm = items_->get_item(id);
        ASSERT_NE(itm, nullptr);
        EXPECT_FALSE(itm->owner.is_valid());
    }
}

TEST_F(item_ops_test, drop_loot_multi_overflow_at_center)
{
    // Create 11 items (9 grid slots + 2 overflow)
    std::vector<item_id> ids;
    for (int i = 0; i < 11; ++i)
    {
        ids.push_back(create_item(item_id{200}));
    }

    auto results = hb::item_ops::drop_loot_multi(
        ids, test_map_, 50, 50, items_, world_);

    ASSERT_EQ(results.size(), 11u);

    // First 9 items should be at distinct grid positions
    // Items 0-8 use the 3x3 grid offsets
    EXPECT_EQ(results[0].ground_x, 49); // (-1,-1)
    EXPECT_EQ(results[0].ground_y, 49);
    EXPECT_EQ(results[4].ground_x, 50); // (0,0) = center
    EXPECT_EQ(results[4].ground_y, 50);
    EXPECT_EQ(results[8].ground_x, 51); // (1,1)
    EXPECT_EQ(results[8].ground_y, 51);

    // Items 9 and 10 should overflow to center
    EXPECT_EQ(results[9].ground_x, 50);
    EXPECT_EQ(results[9].ground_y, 50);
    EXPECT_EQ(results[10].ground_x, 50);
    EXPECT_EQ(results[10].ground_y, 50);
}

// ========== Give Item Tests ==========

TEST_F(item_ops_test, give_item_success)
{
    auto result = hb::item_ops::give_item(
        player_, item_id{100}, 1, items_, inv_);

    EXPECT_TRUE(result.success);
    EXPECT_TRUE(result.error.empty());
    EXPECT_TRUE(result.created.is_valid());

    // Item should be in inventory
    auto* inv = inv_->get_inventory(player_);
    ASSERT_NE(inv, nullptr);
    EXPECT_TRUE(inv->has_item(result.created));

    // Item should be owned by player
    auto* itm = items_->get_item(result.created);
    ASSERT_NE(itm, nullptr);
    EXPECT_EQ(itm->owner, player_);

    // Default placement (30, 40)
    EXPECT_EQ(result.pos_x, 30);
    EXPECT_EQ(result.pos_y, 40);
}

TEST_F(item_ops_test, give_item_full_inventory)
{
    // Fill inventory to capacity (50 slots)
    for (int i = 0; i < 50; ++i)
    {
        auto id = create_item(item_id{200});
        add_to_inventory(player_, id);
    }
    EXPECT_TRUE(inv_->is_full(player_));

    auto result = hb::item_ops::give_item(
        player_, item_id{100}, 1, items_, inv_);

    EXPECT_FALSE(result.success);
    EXPECT_NE(result.error.find("full"), std::string::npos);

    // The created item should have been destroyed (cleaned up)
    // We can't check directly since we don't know the ID, but
    // inventory should still be at 50 items
    auto* inv = inv_->get_inventory(player_);
    ASSERT_NE(inv, nullptr);
    EXPECT_EQ(inv->count(), 50);
}

// ========== Damage Equipment Tests ==========

TEST_F(item_ops_test, damage_equipment_reduces_durability)
{
    // Create sword with 200 durability, equip it
    auto sword_id = create_item(item_id{100}, player_);
    add_to_inventory(player_, sword_id);
    equipment_.equip(hb::player::equip_slot::weapon, sword_id);

    auto* itm = items_->get_item(sword_id);
    ASSERT_NE(itm, nullptr);
    EXPECT_EQ(itm->durability, 200);

    auto result = hb::item_ops::damage_equipment(
        player_, hb::player::equip_slot::weapon, 50, items_, &equipment_);

    EXPECT_TRUE(result.success);
    EXPECT_EQ(result.damaged, sword_id);
    EXPECT_EQ(result.new_durability, 150);
    EXPECT_FALSE(result.broke);
    EXPECT_EQ(result.slot, hb::player::equip_slot::weapon);

    // Verify item durability actually changed
    itm = items_->get_item(sword_id);
    ASSERT_NE(itm, nullptr);
    EXPECT_EQ(itm->durability, 150);
}

TEST_F(item_ops_test, damage_equipment_to_zero_breaks)
{
    // Create sword with 200 durability, equip it
    auto sword_id = create_item(item_id{100}, player_);
    add_to_inventory(player_, sword_id);
    equipment_.equip(hb::player::equip_slot::weapon, sword_id);

    // Damage by 200 to bring to exactly 0
    auto result = hb::item_ops::damage_equipment(
        player_, hb::player::equip_slot::weapon, 200, items_, &equipment_);

    EXPECT_TRUE(result.success);
    EXPECT_EQ(result.damaged, sword_id);
    EXPECT_EQ(result.new_durability, 0);
    EXPECT_TRUE(result.broke);
    EXPECT_EQ(result.slot, hb::player::equip_slot::weapon);

    // Item is still equipped (caller handles unequip on break)
    EXPECT_TRUE(equipment_.has_equipped(hb::player::equip_slot::weapon));
}

TEST_F(item_ops_test, damage_equipment_indestructible)
{
    // Create sword, make it indestructible, equip it
    auto sword_id = create_item(item_id{100}, player_);
    auto* itm = items_->get_item(sword_id);
    ASSERT_NE(itm, nullptr);
    itm->indestructible = true;
    int16_t original_durability = itm->durability;

    add_to_inventory(player_, sword_id);
    equipment_.equip(hb::player::equip_slot::weapon, sword_id);

    auto result = hb::item_ops::damage_equipment(
        player_, hb::player::equip_slot::weapon, 100, items_, &equipment_);

    EXPECT_TRUE(result.success);
    EXPECT_EQ(result.damaged, sword_id);
    EXPECT_EQ(result.new_durability, original_durability);
    EXPECT_FALSE(result.broke);
    EXPECT_EQ(result.slot, hb::player::equip_slot::weapon);

    // Durability should be unchanged
    itm = items_->get_item(sword_id);
    ASSERT_NE(itm, nullptr);
    EXPECT_EQ(itm->durability, original_durability);
}

TEST_F(item_ops_test, damage_equipment_empty_slot)
{
    // No item equipped in weapon slot
    auto result = hb::item_ops::damage_equipment(
        player_, hb::player::equip_slot::weapon, 50, items_, &equipment_);

    EXPECT_FALSE(result.success);
}
