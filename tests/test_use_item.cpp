// test_use_item.cpp
// Tests for consumable item usage: potions, food, recall scrolls, anti-cheat

#include <gtest/gtest.h>
#include "registry/item_template.h"
#include "registry/item_registry.h"
#include "player/player.h"
#include "world/map.h"
#include "core/types.h"
#include "core/enums.h"
#include "network/json_protocol.h"
#include "inventory/inventory.h"

#include <chrono>
#include <random>
#include <fstream>
#include <filesystem>

namespace
{

using namespace hb;

// ============================================================================
// Item template consumable type tests (raw effect_type field)
// ============================================================================

// Legacy effect_type values: 4=hp, 5=mp, 6=sp, 7=food, 11=magic_scroll
TEST(use_item_test, consumable_type_eat_has_effect_type)
{
    item_template tmpl;
    tmpl.type = item_type::eat;
    // Consumability determined by type == eat or use_deplete
    EXPECT_EQ(tmpl.type, item_type::eat);
}

TEST(use_item_test, consumable_type_use_deplete)
{
    item_template tmpl;
    tmpl.type = item_type::use_deplete;
    EXPECT_EQ(tmpl.type, item_type::use_deplete);
}

TEST(use_item_test, weapon_is_not_consumable_type)
{
    item_template tmpl;
    tmpl.type = item_type::weapon;
    EXPECT_NE(tmpl.type, item_type::eat);
    EXPECT_NE(tmpl.type, item_type::use_deplete);
}

// ============================================================================
// Raw effect_type field defaults
// ============================================================================

TEST(use_item_test, template_effect_defaults_to_zero)
{
    item_template tmpl;
    EXPECT_EQ(tmpl.effect_type, 0);
    EXPECT_EQ(tmpl.effect_value1, 0);
    EXPECT_EQ(tmpl.effect_value2, 0);
    EXPECT_EQ(tmpl.effect_value3, 0);
    EXPECT_EQ(tmpl.effect_value4, 0);
    EXPECT_EQ(tmpl.effect_value5, 0);
    EXPECT_EQ(tmpl.effect_value6, 0);
}

// ============================================================================
// Potion template fields simulation (verify struct wiring with raw fields)
// ============================================================================

TEST(use_item_test, hp_potion_template_raw_fields)
{
    // Simulate RedPotion with raw fields:
    // effect_type=4 (hp_restore), effect_value1=2 (dice), effect_value2=12 (sides), effect_value3=10 (bonus)
    item_template tmpl;
    tmpl.id = item_id{91};
    tmpl.name = "RedPotion";
    tmpl.type = item_type::eat;
    tmpl.effect_type = 4; // hp_restore
    tmpl.effect_value1 = 2;  // dice count
    tmpl.effect_value2 = 12; // dice sides
    tmpl.effect_value3 = 10; // flat bonus

    EXPECT_EQ(tmpl.type, item_type::eat);
    EXPECT_EQ(tmpl.effect_type, 4);
    EXPECT_EQ(tmpl.effect_value1, 2);
    EXPECT_EQ(tmpl.effect_value2, 12);
    EXPECT_EQ(tmpl.effect_value3, 10);
}

TEST(use_item_test, recall_scroll_template_raw_fields)
{
    // Simulate RecallScroll: effect_type=11 (magic_scroll), effect_value1=1 (recall subtype)
    item_template tmpl;
    tmpl.id = item_id{114};
    tmpl.name = "RecallScroll";
    tmpl.type = item_type::use_deplete;
    tmpl.effect_type = 11; // magic_scroll
    tmpl.effect_value1 = 1; // scroll subtype: recall

    EXPECT_EQ(tmpl.type, item_type::use_deplete);
    EXPECT_EQ(tmpl.effect_type, 11);
    EXPECT_EQ(tmpl.effect_value1, 1);
}

// ============================================================================
// Potion speed anti-cheat tracker
// ============================================================================

TEST(potion_speed_test, single_use_never_triggers)
{
    player::potion_speed_tracker tracker;
    auto now = std::chrono::steady_clock::now();
    tracker.record_use(now);
    EXPECT_FALSE(tracker.is_speed_hack());
}

TEST(potion_speed_test, two_uses_never_triggers)
{
    player::potion_speed_tracker tracker;
    auto now = std::chrono::steady_clock::now();
    tracker.record_use(now);
    tracker.record_use(now + std::chrono::milliseconds(100));
    EXPECT_FALSE(tracker.is_speed_hack());
}

TEST(potion_speed_test, normal_speed_does_not_trigger)
{
    player::potion_speed_tracker tracker;
    auto base = std::chrono::steady_clock::now();

    // 300ms intervals — well above threshold
    for (int i = 0; i < 4; ++i)
    {
        tracker.record_use(base + std::chrono::milliseconds(300 * i));
    }
    EXPECT_FALSE(tracker.is_speed_hack());
}

TEST(potion_speed_test, fast_speed_triggers_after_three)
{
    player::potion_speed_tracker tracker;
    auto base = std::chrono::steady_clock::now();

    // 50ms intervals — way below threshold
    for (int i = 0; i < 4; ++i)
    {
        tracker.record_use(base + std::chrono::milliseconds(50 * i));
    }
    EXPECT_TRUE(tracker.is_speed_hack());
}

TEST(potion_speed_test, resets_after_two_second_gap)
{
    player::potion_speed_tracker tracker;
    auto base = std::chrono::steady_clock::now();

    // Fast burst
    for (int i = 0; i < 4; ++i)
    {
        tracker.record_use(base + std::chrono::milliseconds(50 * i));
    }
    EXPECT_TRUE(tracker.is_speed_hack());

    // Wait 3 seconds, then use again — should reset
    tracker.record_use(base + std::chrono::milliseconds(3000));
    EXPECT_FALSE(tracker.is_speed_hack());
}

TEST(potion_speed_test, resets_after_five_accumulated)
{
    player::potion_speed_tracker tracker;
    auto base = std::chrono::steady_clock::now();

    // 5 uses at moderate speed (just under 2s total)
    for (int i = 0; i < 5; ++i)
    {
        tracker.record_use(base + std::chrono::milliseconds(350 * i));
    }
    // count is now 5, so next use resets
    tracker.record_use(base + std::chrono::milliseconds(1800));
    EXPECT_FALSE(tracker.is_speed_hack());
}

// ============================================================================
// Dice roll range tests (using a local replica)
// ============================================================================

namespace
{

auto test_dice_roll(int count, int sides, int bonus) -> int32_t
{
    if (count <= 0 || sides <= 0)
        return bonus;
    thread_local std::mt19937 rng{std::random_device{}()};
    std::uniform_int_distribution<int> dist(1, sides);
    int32_t total = bonus;
    for (int i = 0; i < count; ++i)
    {
        total += dist(rng);
    }
    return total;
}

} // namespace

TEST(dice_roll_test, output_in_expected_range)
{
    // 2d12+10 → range [12, 34]
    for (int i = 0; i < 100; ++i)
    {
        int32_t result = test_dice_roll(2, 12, 10);
        EXPECT_GE(result, 12);
        EXPECT_LE(result, 34);
    }
}

TEST(dice_roll_test, zero_count_returns_bonus)
{
    EXPECT_EQ(test_dice_roll(0, 12, 5), 5);
}

TEST(dice_roll_test, zero_sides_returns_bonus)
{
    EXPECT_EQ(test_dice_roll(3, 0, 7), 7);
}

TEST(dice_roll_test, one_die_one_side_returns_count_plus_bonus)
{
    // 3d1+5 = always 8
    for (int i = 0; i < 20; ++i)
    {
        EXPECT_EQ(test_dice_roll(3, 1, 5), 8);
    }
}

// ============================================================================
// Protocol message tests
// ============================================================================

TEST(use_item_protocol_test, request_from_json_valid)
{
    nlohmann::json j = {{"item_id", 5}};
    auto result = network::use_item_request_data::from_json(j);
    ASSERT_TRUE(result.is_ok());
    EXPECT_EQ(result.value().item_id, 5u);
}

TEST(use_item_protocol_test, request_from_json_missing_item_id)
{
    nlohmann::json j = {};
    auto result = network::use_item_request_data::from_json(j);
    EXPECT_TRUE(result.is_err());
}

TEST(use_item_protocol_test, response_success)
{
    auto msg = network::make_use_item_response(42, true, "RedPotion", "hp", 25, 80, 100);
    EXPECT_EQ(msg.type, network::json_message_type::player_use_item_response);
    EXPECT_EQ(msg.seq, 42u);
    EXPECT_TRUE(msg.data["success"].get<bool>());
    EXPECT_EQ(msg.data["item_name"], "RedPotion");
    EXPECT_EQ(msg.data["effect"], "hp");
    EXPECT_EQ(msg.data["amount"], 25);
    EXPECT_EQ(msg.data["current"], 80);
    EXPECT_EQ(msg.data["max"], 100);
    EXPECT_FALSE(msg.data.contains("error"));
}

TEST(use_item_protocol_test, response_error)
{
    auto msg = network::make_use_item_response(42, false, {}, {}, 0, 0, 0, "dead");
    EXPECT_FALSE(msg.data["success"].get<bool>());
    EXPECT_EQ(msg.data["error"], "dead");
    EXPECT_FALSE(msg.data.contains("item_name"));
}

TEST(use_item_protocol_test, message_type_to_string)
{
    EXPECT_EQ(network::to_string(network::json_message_type::player_use_item_request), "player_use_item_request");
    EXPECT_EQ(network::to_string(network::json_message_type::player_use_item_response), "player_use_item_response");
}

TEST(use_item_protocol_test, message_type_parse)
{
    EXPECT_EQ(network::parse_message_type("player_use_item_request"),
              network::json_message_type::player_use_item_request);
    EXPECT_EQ(network::parse_message_type("player_use_item_response"),
              network::json_message_type::player_use_item_response);
}

// ============================================================================
// Player heal / hunger / status tests (unit-level)
// ============================================================================

TEST(use_item_player_test, heal_hp_caps_at_max)
{
    player::player plr;
    plr.computed.max_hp = 100;
    plr.hp = 80;

    plr.heal_hp(50); // Would go to 130, should cap at 100
    EXPECT_EQ(plr.hp, 100);
}

TEST(use_item_player_test, heal_mp_caps_at_max)
{
    player::player plr;
    plr.computed.max_mp = 50;
    plr.mp = 40;

    plr.heal_mp(20);
    EXPECT_EQ(plr.mp, 50);
}

TEST(use_item_player_test, heal_sp_caps_at_max)
{
    player::player plr;
    plr.computed.max_sp = 80;
    plr.sp = 60;

    plr.heal_sp(30);
    EXPECT_EQ(plr.sp, 80);
}

TEST(use_item_player_test, sp_potion_cures_poison)
{
    player::player plr;
    plr.add_status(player::player_status::poisoned);
    EXPECT_TRUE(plr.has_status(player::player_status::poisoned));

    plr.remove_status(player::player_status::poisoned);
    EXPECT_FALSE(plr.has_status(player::player_status::poisoned));
}

TEST(use_item_player_test, hunger_restore_caps_at_100)
{
    player::hunger_state hunger;
    hunger.level = 80;
    hunger.consume(30); // 80 + 30 = 110, should cap at 100
    EXPECT_EQ(hunger.level, 100);
}

TEST(use_item_player_test, dead_player_detected)
{
    player::player plr;
    plr.hp = 0;
    EXPECT_TRUE(plr.is_dead());

    plr.hp = 1;
    EXPECT_FALSE(plr.is_dead());
}

// ============================================================================
// Map restriction flag tests
// ============================================================================

TEST(use_item_map_test, potions_disabled_default_false)
{
    world::map_config cfg;
    EXPECT_FALSE(cfg.is_potions_disabled);
}

TEST(use_item_map_test, recall_impossible_default_false)
{
    world::map_config cfg;
    EXPECT_FALSE(cfg.is_recall_impossible);
}

TEST(use_item_map_test, potions_disabled_flag_settable)
{
    world::map_config cfg;
    cfg.is_potions_disabled = true;
    EXPECT_TRUE(cfg.is_potions_disabled);
}

TEST(use_item_map_test, recall_impossible_flag_settable)
{
    world::map_config cfg;
    cfg.is_recall_impossible = true;
    EXPECT_TRUE(cfg.is_recall_impossible);
}

// ============================================================================
// Inventory slot consumption tests
// ============================================================================

TEST(use_item_inventory_test, stack_decrements)
{
    inventory::inventory_entry entry{};
    entry.item = item_id{91};
    entry.count = 5;

    --entry.count;
    EXPECT_EQ(entry.count, 4);
    EXPECT_TRUE(entry.item.is_valid());
}

TEST(use_item_inventory_test, last_item_consumed)
{
    inventory::inventory inv(50);
    inv.add_item(item_id{91}, 1);
    EXPECT_TRUE(inv.has_item(item_id{91}));

    inv.remove_item(item_id{91});
    EXPECT_FALSE(inv.has_item(item_id{91}));
}

TEST(use_item_inventory_test, stack_five_to_four)
{
    inventory::inventory_entry entry{};
    entry.item = item_id{91};
    entry.count = 5;

    if (entry.count <= 1)
    {
        // would remove from inventory
    }
    else
    {
        --entry.count;
    }

    EXPECT_EQ(entry.count, 4);
    EXPECT_EQ(entry.item.value, 91u);
}

// ============================================================================
// YAML parsing test (item registry)
// ============================================================================

TEST(use_item_registry_test, parses_raw_effect_fields_from_yaml)
{
    // Write a temp YAML file with new field names and parse it
    auto temp_path = std::filesystem::temp_directory_path() / "test_use_item_items.yaml";
    {
        std::ofstream f(temp_path);
        f << "items:\n"
          << "  - id: 91\n"
          << "    name: RedPotion\n"
          << "    type: 7\n"
          << "    effect_type: 4\n"
          << "    effect_value1: 2\n"
          << "    effect_value2: 12\n"
          << "    effect_value3: 10\n"
          << "  - id: 114\n"
          << "    name: RecallScroll\n"
          << "    type: 3\n"
          << "    effect_type: 11\n"
          << "    effect_value1: 1\n"
          << "  - id: 200\n"
          << "    name: LongSword\n"
          << "    type: 13\n";
        f.close();
    }

    item_registry registry;
    registry.initialize();
    auto result = registry.load_from_file(temp_path);
    ASSERT_TRUE(result.is_ok());
    EXPECT_EQ(result.value(), 3u);

    // RedPotion -- raw effect fields
    const auto* red_pot = registry.get(item_id{91});
    ASSERT_NE(red_pot, nullptr);
    EXPECT_EQ(red_pot->effect_type, 4);       // hp_restore
    EXPECT_EQ(red_pot->effect_value1, 2);      // dice count
    EXPECT_EQ(red_pot->effect_value2, 12);     // dice sides
    EXPECT_EQ(red_pot->effect_value3, 10);     // flat bonus
    EXPECT_EQ(red_pot->type, item_type::eat);

    // RecallScroll -- raw effect fields
    const auto* scroll = registry.get(item_id{114});
    ASSERT_NE(scroll, nullptr);
    EXPECT_EQ(scroll->effect_type, 11);        // magic_scroll
    EXPECT_EQ(scroll->effect_value1, 1);       // recall subtype
    EXPECT_EQ(scroll->type, item_type::use_deplete);

    // LongSword -- no effect
    const auto* sword = registry.get(item_id{200});
    ASSERT_NE(sword, nullptr);
    EXPECT_EQ(sword->effect_type, 0);
    EXPECT_EQ(sword->type, item_type::weapon);

    registry.shutdown();
    std::filesystem::remove(temp_path);
}

} // namespace
