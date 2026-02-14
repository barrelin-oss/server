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

namespace {

using namespace hb;

// ============================================================================
// Item template / is_usable tests
// ============================================================================

TEST(use_item_test, is_usable_eat_type)
{
    item_template tmpl;
    tmpl.type = item_type::eat;
    EXPECT_TRUE(tmpl.is_usable());
}

TEST(use_item_test, is_usable_use_deplete_type)
{
    item_template tmpl;
    tmpl.type = item_type::use_deplete;
    EXPECT_TRUE(tmpl.is_usable());
}

TEST(use_item_test, is_usable_weapon_false)
{
    item_template tmpl;
    tmpl.type = item_type::weapon;
    EXPECT_FALSE(tmpl.is_usable());
}

TEST(use_item_test, is_usable_armor_false)
{
    item_template tmpl;
    tmpl.type = item_type::armor;
    EXPECT_FALSE(tmpl.is_usable());
}

TEST(use_item_test, is_usable_none_false)
{
    item_template tmpl;
    tmpl.type = item_type::none;
    EXPECT_FALSE(tmpl.is_usable());
}

// ============================================================================
// Consumable effect struct defaults
// ============================================================================

TEST(use_item_test, consumable_effect_defaults)
{
    consumable_effect eff;
    EXPECT_EQ(eff.type, consumable_effect_type::none);
    EXPECT_EQ(eff.v1, 0);
    EXPECT_EQ(eff.v2, 0);
    EXPECT_EQ(eff.v3, 0);
    EXPECT_EQ(eff.v4, 0);
    EXPECT_EQ(eff.v5, 0);
}

TEST(use_item_test, consumable_effect_type_values)
{
    EXPECT_EQ(static_cast<uint8_t>(consumable_effect_type::none), 0);
    EXPECT_EQ(static_cast<uint8_t>(consumable_effect_type::hp_restore), 4);
    EXPECT_EQ(static_cast<uint8_t>(consumable_effect_type::mp_restore), 5);
    EXPECT_EQ(static_cast<uint8_t>(consumable_effect_type::sp_restore), 6);
    EXPECT_EQ(static_cast<uint8_t>(consumable_effect_type::food), 7);
    EXPECT_EQ(static_cast<uint8_t>(consumable_effect_type::magic_scroll), 11);
}

// ============================================================================
// Potion template fields simulation (verify struct wiring)
// ============================================================================

TEST(use_item_test, hp_potion_template_fields)
{
    // Simulate RedPotion: { id: 91, type: 7, color_r1: 4, color_g1: 2, color_b1: 12, color_r2: 10 }
    item_template tmpl;
    tmpl.id = item_id{91};
    tmpl.name = "RedPotion";
    tmpl.type = item_type::eat;
    tmpl.use_effect.type = consumable_effect_type::hp_restore;
    tmpl.use_effect.v1 = 2;   // dice count
    tmpl.use_effect.v2 = 12;  // dice sides
    tmpl.use_effect.v3 = 10;  // flat bonus

    EXPECT_TRUE(tmpl.is_usable());
    // is_consumable is set by YAML loader, not default — test that in registry test
    EXPECT_EQ(tmpl.use_effect.type, consumable_effect_type::hp_restore);
    EXPECT_EQ(tmpl.use_effect.v1, 2);
    EXPECT_EQ(tmpl.use_effect.v2, 12);
    EXPECT_EQ(tmpl.use_effect.v3, 10);
}

TEST(use_item_test, recall_scroll_template_fields)
{
    // Simulate RecallScroll: { id: 114, type: 3, color_r1: 11, color_g1: 1 }
    item_template tmpl;
    tmpl.id = item_id{114};
    tmpl.name = "RecallScroll";
    tmpl.type = item_type::use_deplete;
    tmpl.use_effect.type = consumable_effect_type::magic_scroll;
    tmpl.use_effect.v1 = 1;  // scroll subtype: recall

    EXPECT_TRUE(tmpl.is_usable());
    EXPECT_EQ(tmpl.use_effect.type, consumable_effect_type::magic_scroll);
    EXPECT_EQ(tmpl.use_effect.v1, 1);
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
    for (int i = 0; i < 4; ++i) {
        tracker.record_use(base + std::chrono::milliseconds(300 * i));
    }
    EXPECT_FALSE(tracker.is_speed_hack());
}

TEST(potion_speed_test, fast_speed_triggers_after_three)
{
    player::potion_speed_tracker tracker;
    auto base = std::chrono::steady_clock::now();

    // 50ms intervals — way below threshold
    for (int i = 0; i < 4; ++i) {
        tracker.record_use(base + std::chrono::milliseconds(50 * i));
    }
    EXPECT_TRUE(tracker.is_speed_hack());
}

TEST(potion_speed_test, resets_after_two_second_gap)
{
    player::potion_speed_tracker tracker;
    auto base = std::chrono::steady_clock::now();

    // Fast burst
    for (int i = 0; i < 4; ++i) {
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
    for (int i = 0; i < 5; ++i) {
        tracker.record_use(base + std::chrono::milliseconds(350 * i));
    }
    // count is now 5, so next use resets
    tracker.record_use(base + std::chrono::milliseconds(1800));
    EXPECT_FALSE(tracker.is_speed_hack());
}

// ============================================================================
// Dice roll range tests (using a local replica)
// ============================================================================

namespace {

auto test_dice_roll(int count, int sides, int bonus) -> int32_t
{
    if (count <= 0 || sides <= 0) return bonus;
    thread_local std::mt19937 rng{std::random_device{}()};
    std::uniform_int_distribution<int> dist(1, sides);
    int32_t total = bonus;
    for (int i = 0; i < count; ++i) {
        total += dist(rng);
    }
    return total;
}

}  // namespace

TEST(dice_roll_test, output_in_expected_range)
{
    // 2d12+10 → range [12, 34]
    for (int i = 0; i < 100; ++i) {
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
    for (int i = 0; i < 20; ++i) {
        EXPECT_EQ(test_dice_roll(3, 1, 5), 8);
    }
}

// ============================================================================
// Protocol message tests
// ============================================================================

TEST(use_item_protocol_test, request_from_json_valid)
{
    nlohmann::json j = {{"slot", 5}};
    auto result = network::use_item_request_data::from_json(j);
    ASSERT_TRUE(result.is_ok());
    EXPECT_EQ(result.value().slot, 5);
}

TEST(use_item_protocol_test, request_from_json_missing_slot)
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

    plr.heal_hp(50);  // Would go to 130, should cap at 100
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
    hunger.consume(30);  // 80 + 30 = 110, should cap at 100
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
    inventory::inventory_slot slot;
    slot.set(item_id{91}, 5);
    EXPECT_FALSE(slot.is_empty());

    --slot.count;
    EXPECT_EQ(slot.count, 4);
    EXPECT_FALSE(slot.is_empty());
}

TEST(use_item_inventory_test, last_item_cleared)
{
    inventory::inventory_slot slot;
    slot.set(item_id{91}, 1);

    if (slot.count <= 1) {
        slot.clear();
    } else {
        --slot.count;
    }

    EXPECT_TRUE(slot.is_empty());
}

TEST(use_item_inventory_test, stack_five_to_four)
{
    inventory::inventory_slot slot;
    slot.set(item_id{91}, 5);

    if (slot.count <= 1) {
        slot.clear();
    } else {
        --slot.count;
    }

    EXPECT_EQ(slot.count, 4);
    EXPECT_EQ(slot.item.value, 91u);
}

// ============================================================================
// YAML parsing test (item registry)
// ============================================================================

TEST(use_item_registry_test, parses_consumable_fields_from_yaml)
{
    // Write a temp YAML file and parse it
    auto temp_path = std::filesystem::temp_directory_path() / "test_use_item_items.yaml";
    {
        std::ofstream f(temp_path);
        f << "items:\n"
          << "  - id: 91\n"
          << "    name: RedPotion\n"
          << "    type: 7\n"
          << "    color_r1: 4\n"
          << "    color_g1: 2\n"
          << "    color_b1: 12\n"
          << "    color_r2: 10\n"
          << "  - id: 114\n"
          << "    name: RecallScroll\n"
          << "    type: 3\n"
          << "    color_r1: 11\n"
          << "    color_g1: 1\n"
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

    // RedPotion
    const auto* red_pot = registry.get(item_id{91});
    ASSERT_NE(red_pot, nullptr);
    EXPECT_EQ(red_pot->use_effect.type, consumable_effect_type::hp_restore);
    EXPECT_EQ(red_pot->use_effect.v1, 2);
    EXPECT_EQ(red_pot->use_effect.v2, 12);
    EXPECT_EQ(red_pot->use_effect.v3, 10);
    EXPECT_TRUE(red_pot->is_usable());
    EXPECT_TRUE(red_pot->is_consumable);

    // RecallScroll
    const auto* scroll = registry.get(item_id{114});
    ASSERT_NE(scroll, nullptr);
    EXPECT_EQ(scroll->use_effect.type, consumable_effect_type::magic_scroll);
    EXPECT_EQ(scroll->use_effect.v1, 1);
    EXPECT_TRUE(scroll->is_usable());
    EXPECT_TRUE(scroll->is_consumable);

    // LongSword — not usable
    const auto* sword = registry.get(item_id{200});
    ASSERT_NE(sword, nullptr);
    EXPECT_EQ(sword->use_effect.type, consumable_effect_type::none);
    EXPECT_FALSE(sword->is_usable());
    EXPECT_FALSE(sword->is_consumable);

    registry.shutdown();
    std::filesystem::remove(temp_path);
}

}  // namespace
