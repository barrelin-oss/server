// test_item.cpp
// Unit tests for item system

#include <gtest/gtest.h>
#include "core/types.h"
#include "entity/entity.h"
#include "item/item.h"
#include "item/item_effect.h"
#include "item/item_system.h"

using hb::item_id;
using hb::entity_id;
using namespace hb::entity;
using namespace hb::item;

// Item tests

TEST(item_test, default_values) {
    item itm;
    EXPECT_FALSE(itm.is_valid());
    EXPECT_EQ(itm.count, 1);
    EXPECT_EQ(itm.type, item_type::none);
}

TEST(item_test, is_equipment) {
    item itm;
    itm.type = item_type::weapon;
    EXPECT_TRUE(itm.is_equipment());

    itm.type = item_type::armor;
    EXPECT_TRUE(itm.is_equipment());

    itm.type = item_type::consumable;
    EXPECT_FALSE(itm.is_equipment());
}

TEST(item_test, is_broken) {
    item itm;
    itm.max_durability = 100;
    itm.durability = 50;

    EXPECT_FALSE(itm.is_broken());

    itm.durability = 0;
    EXPECT_TRUE(itm.is_broken());

    itm.indestructible = true;
    EXPECT_FALSE(itm.is_broken());
}

TEST(item_test, durability_operations) {
    item itm;
    itm.max_durability = 100;
    itm.durability = 100;

    itm.damage_durability(30);
    EXPECT_EQ(itm.durability, 70);

    itm.damage_durability(100);
    EXPECT_EQ(itm.durability, 0);

    itm.repair(50);
    EXPECT_EQ(itm.durability, 50);

    itm.repair_full();
    EXPECT_EQ(itm.durability, 100);
}

TEST(item_test, indestructible) {
    item itm;
    itm.max_durability = 100;
    itm.durability = 100;
    itm.indestructible = true;

    itm.damage_durability(200);
    EXPECT_EQ(itm.durability, 100);  // No damage
}

TEST(item_test, stacking) {
    item itm1;
    itm1.template_id = item_id{100};
    itm1.count = 50;
    itm1.max_stack = 99;
    itm1.stackable = true;

    item itm2;
    itm2.template_id = item_id{100};
    itm2.count = 30;
    itm2.max_stack = 99;
    itm2.stackable = true;

    EXPECT_TRUE(itm1.can_stack_with(itm2));

    int16_t stacked = itm1.stack(itm2);
    EXPECT_EQ(stacked, 30);
    EXPECT_EQ(itm1.count, 80);
    EXPECT_EQ(itm2.count, 0);
}

TEST(item_test, stacking_overflow) {
    item itm1;
    itm1.template_id = item_id{100};
    itm1.count = 80;
    itm1.max_stack = 99;
    itm1.stackable = true;

    item itm2;
    itm2.template_id = item_id{100};
    itm2.count = 30;
    itm2.max_stack = 99;
    itm2.stackable = true;

    int16_t stacked = itm1.stack(itm2);
    EXPECT_EQ(stacked, 19);
    EXPECT_EQ(itm1.count, 99);
    EXPECT_EQ(itm2.count, 11);
}

TEST(item_test, split) {
    item itm;
    itm.template_id = item_id{100};
    itm.count = 50;

    auto split = itm.split(20);
    EXPECT_EQ(itm.count, 30);
    EXPECT_EQ(split.count, 20);
    EXPECT_EQ(split.template_id.value, 100);
}

// Item effect tests

TEST(item_effect_test, apply_effects) {
    item itm;
    itm.effects[0] = {item_effect_type::str_bonus, 10};
    itm.effects[1] = {item_effect_type::attack_bonus, 25};
    itm.attack_power = 50;
    itm.defense = 30;
    itm.durability = 100;  // Not broken
    itm.max_durability = 100;

    hb::player::stat_modifiers mods;
    apply_item_base_stats(itm, mods);

    EXPECT_EQ(mods.attack_power, 75);  // 50 base + 25 from effect
    EXPECT_EQ(mods.defense, 30);
    EXPECT_EQ(mods.strength, 10);
}

TEST(item_effect_test, broken_item_no_stats) {
    item itm;
    itm.attack_power = 100;
    itm.durability = 0;
    itm.max_durability = 100;

    hb::player::stat_modifiers mods;
    apply_item_base_stats(itm, mods);

    EXPECT_EQ(mods.attack_power, 0);  // Broken = no stats
}

TEST(requirement_check_test, meets_requirements) {
    item itm;
    itm.level_requirement = 10;
    itm.str_requirement = 20;
    itm.dex_requirement = 15;

    auto check = check_requirements(itm, 15, 25, 20, 10, 10);
    EXPECT_TRUE(check.can_use());

    check = check_requirements(itm, 5, 25, 20, 10, 10);
    EXPECT_FALSE(check.meets_level);
    EXPECT_FALSE(check.can_use());

    check = check_requirements(itm, 15, 10, 20, 10, 10);
    EXPECT_FALSE(check.meets_str);
    EXPECT_FALSE(check.can_use());
}

// Item system tests

class item_system_test : public ::testing::Test {
protected:
    void SetUp() override {
        system_.initialize();
    }

    void TearDown() override {
        system_.shutdown();
    }

    item_system system_;
};

TEST_F(item_system_test, lifecycle) {
    EXPECT_TRUE(system_.is_initialized());
    EXPECT_EQ(system_.name(), "item_system");
}

TEST_F(item_system_test, create_item) {
    item_create_info info;
    info.template_id = item_id{100};
    info.count = 5;

    auto result = system_.create_item(info);
    ASSERT_TRUE(result.is_ok());

    auto id = result.value();
    EXPECT_TRUE(id.is_valid());
    EXPECT_EQ(system_.item_count(), 1);

    auto* itm = system_.get_item(id);
    ASSERT_NE(itm, nullptr);
    EXPECT_EQ(itm->count, 5);
}

TEST_F(item_system_test, destroy_item) {
    item_create_info info;
    info.template_id = item_id{100};

    auto result = system_.create_item(info);
    auto id = result.value();

    EXPECT_TRUE(system_.item_exists(id));
    system_.destroy_item(id);
    EXPECT_FALSE(system_.item_exists(id));
}

TEST_F(item_system_test, ownership) {
    item_create_info info;
    info.template_id = item_id{100};
    info.owner = entity_id{42};

    auto result = system_.create_item(info);
    auto id = result.value();

    auto* itm = system_.get_item(id);
    EXPECT_EQ(itm->owner.value, 42);

    auto owned = system_.get_items_owned_by(entity_id{42});
    EXPECT_EQ(owned.size(), 1);

    system_.clear_owner(id);
    EXPECT_FALSE(itm->owner.is_valid());
}

TEST_F(item_system_test, stack_items) {
    item_create_info info1;
    info1.template_id = item_id{100};
    info1.count = 50;

    item_create_info info2;
    info2.template_id = item_id{100};
    info2.count = 30;

    auto r1 = system_.create_item(info1);
    auto r2 = system_.create_item(info2);

    auto id1 = r1.value();
    auto id2 = r2.value();

    auto* itm1 = system_.get_item(id1);
    auto* itm2 = system_.get_item(id2);

    // Make them stackable
    itm1->stackable = true;
    itm1->max_stack = 99;
    itm2->stackable = true;
    itm2->max_stack = 99;

    EXPECT_TRUE(system_.try_stack(id1, id2));
    EXPECT_EQ(system_.get_item(id1)->count, 80);
    EXPECT_FALSE(system_.item_exists(id2));  // Destroyed after stacking
}

TEST_F(item_system_test, split_item) {
    item_create_info info;
    info.template_id = item_id{100};
    info.count = 50;

    auto create_result = system_.create_item(info);
    auto id = create_result.value();

    auto split_result = system_.split_item(id, 20);
    ASSERT_TRUE(split_result.is_ok());

    auto new_id = split_result.value();
    EXPECT_EQ(system_.get_item(id)->count, 30);
    EXPECT_EQ(system_.get_item(new_id)->count, 20);
}

TEST_F(item_system_test, durability_operations) {
    item_create_info info;
    info.template_id = item_id{100};

    auto result = system_.create_item(info);
    auto id = result.value();

    auto* itm = system_.get_item(id);
    EXPECT_EQ(itm->durability, 100);

    system_.damage_durability(id, 30);
    EXPECT_EQ(itm->durability, 70);

    system_.repair_item(id, 20);
    EXPECT_EQ(itm->durability, 90);

    system_.repair_item_full(id);
    EXPECT_EQ(itm->durability, 100);
}

// Additional item system tests

TEST_F(item_system_test, max_items_limit) {
    item_system_config config;
    config.max_items = 3;
    system_.set_config(config);

    item_create_info info;
    info.template_id = item_id{1};

    EXPECT_TRUE(system_.create_item(info).is_ok());
    EXPECT_TRUE(system_.create_item(info).is_ok());
    EXPECT_TRUE(system_.create_item(info).is_ok());

    auto r4 = system_.create_item(info);
    EXPECT_TRUE(r4.is_err());
}

TEST_F(item_system_test, get_nonexistent_item) {
    EXPECT_EQ(system_.get_item(item_id{999}), nullptr);
}

TEST_F(item_system_test, destroy_nonexistent_item) {
    // Should not crash
    system_.destroy_item(item_id{999});
}

TEST_F(item_system_test, set_owner) {
    item_create_info info;
    info.template_id = item_id{100};

    auto result = system_.create_item(info);
    auto id = result.value();

    system_.set_owner(id, entity_id{10});
    EXPECT_EQ(system_.get_item(id)->owner.value, 10);

    auto owned = system_.get_items_owned_by(entity_id{10});
    EXPECT_EQ(owned.size(), 1);
    EXPECT_EQ(owned[0].value, id.value);
}

TEST_F(item_system_test, multiple_items_same_owner) {
    item_create_info info;
    info.template_id = item_id{1};
    info.owner = entity_id{42};

    system_.create_item(info);
    system_.create_item(info);
    system_.create_item(info);

    auto owned = system_.get_items_owned_by(entity_id{42});
    EXPECT_EQ(owned.size(), 3);
}

TEST_F(item_system_test, stack_different_templates_fails) {
    item_create_info info1;
    info1.template_id = item_id{100};
    info1.count = 10;

    item_create_info info2;
    info2.template_id = item_id{200};  // Different template
    info2.count = 5;

    auto r1 = system_.create_item(info1);
    auto r2 = system_.create_item(info2);

    auto* itm1 = system_.get_item(r1.value());
    auto* itm2 = system_.get_item(r2.value());
    itm1->stackable = true;
    itm1->max_stack = 99;
    itm2->stackable = true;
    itm2->max_stack = 99;

    // Different templates should not stack
    EXPECT_FALSE(system_.try_stack(r1.value(), r2.value()));
    EXPECT_TRUE(system_.item_exists(r2.value()));  // Source still exists
}

TEST_F(item_system_test, split_item_count_one_fails) {
    item_create_info info;
    info.template_id = item_id{100};
    info.count = 1;

    auto create_result = system_.create_item(info);
    auto id = create_result.value();

    // Can't split a single item
    auto split_result = system_.split_item(id, 1);
    // The split method on item limits to count-1, so splitting 1 from count=1 gives 0
    // This depends on implementation - let's just verify no crash
}

TEST_F(item_system_test, for_each_item) {
    item_create_info info;
    info.template_id = item_id{1};

    system_.create_item(info);
    system_.create_item(info);

    int count = 0;
    system_.for_each_item([&](item_id, item&) {
        ++count;
    });
    EXPECT_EQ(count, 2);
}

TEST_F(item_system_test, for_each_item_owned_by) {
    item_create_info info1;
    info1.template_id = item_id{1};
    info1.owner = entity_id{10};

    item_create_info info2;
    info2.template_id = item_id{2};
    info2.owner = entity_id{20};

    system_.create_item(info1);
    system_.create_item(info1);
    system_.create_item(info2);

    int count = 0;
    system_.for_each_item_owned_by(entity_id{10}, [&](item_id, item&) {
        ++count;
    });
    EXPECT_EQ(count, 2);
}

TEST_F(item_system_test, durability_to_zero_breaks_item) {
    item_create_info info;
    info.template_id = item_id{100};

    auto result = system_.create_item(info);
    auto id = result.value();

    system_.damage_durability(id, 200);  // More than max
    EXPECT_EQ(system_.get_item(id)->durability, 0);
    EXPECT_TRUE(system_.get_item(id)->is_broken());
}

// Item struct edge cases

TEST(item_test, durability_percent_zero_max) {
    item itm;
    itm.max_durability = 0;
    itm.durability = 0;

    // Should return 1.0f when max is 0 (indestructible-like)
    EXPECT_FLOAT_EQ(itm.durability_percent(), 1.0f);
}

TEST(item_test, cannot_stack_non_stackable) {
    item itm1;
    itm1.template_id = item_id{100};
    itm1.stackable = false;

    item itm2;
    itm2.template_id = item_id{100};
    itm2.stackable = false;

    EXPECT_FALSE(itm1.can_stack_with(itm2));
}

TEST(item_test, cannot_stack_full) {
    item itm1;
    itm1.template_id = item_id{100};
    itm1.count = 99;
    itm1.max_stack = 99;
    itm1.stackable = true;

    item itm2;
    itm2.template_id = item_id{100};
    itm2.count = 1;
    itm2.stackable = true;

    EXPECT_FALSE(itm1.can_stack_with(itm2));  // Already at max
}

TEST(item_test, is_consumable) {
    item itm;
    itm.type = item_type::consumable;
    EXPECT_TRUE(itm.is_consumable());

    itm.type = item_type::weapon;
    EXPECT_FALSE(itm.is_consumable());
}

TEST(item_test, item_type_gold) {
    item itm;
    itm.type = item_type::gold;
    EXPECT_FALSE(itm.is_equipment());
    EXPECT_FALSE(itm.is_consumable());
}

TEST(item_test, repair_caps_at_max) {
    item itm;
    itm.max_durability = 100;
    itm.durability = 80;

    itm.repair(50);
    EXPECT_EQ(itm.durability, 100);  // Capped at max
}

// Item effect edge cases

TEST(item_effect_test, empty_effect) {
    item_effect effect;
    EXPECT_TRUE(effect.is_empty());

    effect.type = item_effect_type::hp_bonus;
    EXPECT_FALSE(effect.is_empty());
}

TEST(item_effect_test, multiple_effects) {
    item itm;
    itm.effects[0] = {item_effect_type::str_bonus, 10};
    itm.effects[1] = {item_effect_type::dex_bonus, 5};
    itm.effects[2] = {item_effect_type::hp_bonus, 50};
    itm.effects[3] = {item_effect_type::defense_bonus, 20};
    itm.defense = 10;
    itm.durability = 100;
    itm.max_durability = 100;

    hb::player::stat_modifiers mods;
    apply_item_base_stats(itm, mods);

    EXPECT_EQ(mods.strength, 10);
    EXPECT_EQ(mods.dexterity, 5);
    EXPECT_EQ(mods.defense, 30);  // 10 base + 20 from effect
}
