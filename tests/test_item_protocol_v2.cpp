#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

#include "item/item.h"
#include "item/item_serialization.h"
#include "network/json_protocol.h"

using hb::item_id;
using namespace hb::item;
using namespace hb::network;
using json = nlohmann::json;

// --- Helper: build a basic weapon item ---

static auto make_weapon() -> item
{
    item itm;
    itm.id = item_id(12345);
    itm.template_id = item_id(100);
    itm.name = "Barbarian Sword";
    itm.type = item_type::weapon;
    itm.equip_position = equip_pos::weapon;
    itm.weapon = weapon_type::sword;
    itm.rarity = item_rarity::rare;
    itm.count = 1;
    itm.weight = 800;
    itm.price = 15000;
    itm.damage_min = 18;
    itm.damage_max = 66;
    itm.defense = 0;
    itm.magic_defense = 0;
    itm.durability = 85;
    itm.max_durability = 100;
    itm.level_requirement = 30;
    itm.color = 0;
    itm.tradeable = true;
    itm.droppable = true;
    itm.two_handed = false;
    return itm;
}

static auto make_potion() -> item
{
    item itm;
    itm.id = item_id(99999);
    itm.template_id = item_id(500);
    itm.name = "Health Potion";
    itm.type = item_type::consumable;
    itm.count = 5;
    itm.weight = 100;
    itm.durability = 0;
    itm.max_durability = 0;
    return itm;
}

// ============================================================
// 1. inventory_item_add
// ============================================================

TEST(item_protocol_v2_test, inventory_item_add_basic)
{
    auto itm = make_weapon();
    auto msg = make_inventory_item_add(itm, 30, 40, 5);

    EXPECT_EQ(msg["type"], "inventory_item_add");
    auto& data = msg["data"];
    EXPECT_EQ(data["pos_x"], 30);
    EXPECT_EQ(data["pos_y"], 40);
    EXPECT_EQ(data["z_order"], 5);

    // Item object should contain serialized item fields
    auto& item_obj = data["item"];
    EXPECT_EQ(item_obj["item_id"], 12345);
    EXPECT_EQ(item_obj["name"], "Barbarian Sword");
    EXPECT_EQ(item_obj["type"], "weapon");
}

// ============================================================
// 2. inventory_item_update (v2)
// ============================================================

TEST(item_protocol_v2_test, inventory_item_update_v2_basic)
{
    auto itm = make_weapon();
    auto msg = make_inventory_item_update_v2(itm, 10, 20, 3);

    EXPECT_EQ(msg["type"], "inventory_item_update");
    auto& data = msg["data"];
    EXPECT_EQ(data["pos_x"], 10);
    EXPECT_EQ(data["pos_y"], 20);
    EXPECT_EQ(data["z_order"], 3);
    EXPECT_EQ(data["item"]["item_id"], 12345);
}

// ============================================================
// 3. inventory_item_removed (v2)
// ============================================================

TEST(item_protocol_v2_test, inventory_item_removed_v2_basic)
{
    auto msg = make_inventory_item_removed_v2(item_id(12345));

    EXPECT_EQ(msg["type"], "inventory_item_removed");
    EXPECT_EQ(msg["data"]["item_id"], 12345);
}

// ============================================================
// 4. inventory_item_delta
// ============================================================

TEST(item_protocol_v2_test, inventory_item_delta_both_fields)
{
    auto msg = make_inventory_item_delta(item_id(12345), int16_t{47}, int16_t{82});

    EXPECT_EQ(msg["type"], "inventory_item_delta");
    EXPECT_EQ(msg["data"]["item_id"], 12345);
    EXPECT_EQ(msg["data"]["count"], 47);
    EXPECT_EQ(msg["data"]["durability"], 82);
}

TEST(item_protocol_v2_test, inventory_item_delta_count_only)
{
    auto msg = make_inventory_item_delta(item_id(100), int16_t{10}, std::nullopt);

    EXPECT_EQ(msg["type"], "inventory_item_delta");
    EXPECT_EQ(msg["data"]["item_id"], 100);
    EXPECT_EQ(msg["data"]["count"], 10);
    EXPECT_FALSE(msg["data"].contains("durability"));
}

TEST(item_protocol_v2_test, inventory_item_delta_durability_only)
{
    auto msg = make_inventory_item_delta(item_id(200), std::nullopt, int16_t{50});

    EXPECT_EQ(msg["type"], "inventory_item_delta");
    EXPECT_EQ(msg["data"]["item_id"], 200);
    EXPECT_FALSE(msg["data"].contains("count"));
    EXPECT_EQ(msg["data"]["durability"], 50);
}

TEST(item_protocol_v2_test, inventory_item_delta_neither_field)
{
    auto msg = make_inventory_item_delta(item_id(300), std::nullopt, std::nullopt);

    EXPECT_EQ(msg["type"], "inventory_item_delta");
    EXPECT_EQ(msg["data"]["item_id"], 300);
    EXPECT_FALSE(msg["data"].contains("count"));
    EXPECT_FALSE(msg["data"].contains("durability"));
}

// ============================================================
// 5. inventory_gold_update
// ============================================================

TEST(item_protocol_v2_test, inventory_gold_update_basic)
{
    auto msg = make_inventory_gold_update(48500);

    EXPECT_EQ(msg["type"], "inventory_gold_update");
    EXPECT_EQ(msg["data"]["gold"], 48500);
}

TEST(item_protocol_v2_test, inventory_gold_update_zero)
{
    auto msg = make_inventory_gold_update(0);

    EXPECT_EQ(msg["type"], "inventory_gold_update");
    EXPECT_EQ(msg["data"]["gold"], 0);
}

// ============================================================
// 6. inventory_weight_update (v2)
// ============================================================

TEST(item_protocol_v2_test, inventory_weight_update_v2_basic)
{
    auto msg = make_inventory_weight_update_v2(3200, 5500);

    EXPECT_EQ(msg["type"], "inventory_weight_update");
    EXPECT_EQ(msg["data"]["weight"], 3200);
    EXPECT_EQ(msg["data"]["max_weight"], 5500);
}

// ============================================================
// 7. force_unequip
// ============================================================

TEST(item_protocol_v2_test, force_unequip_broken)
{
    auto msg = make_force_unequip("body", "broken");

    EXPECT_EQ(msg["type"], "force_unequip");
    EXPECT_EQ(msg["data"]["slot"], "body");
    EXPECT_EQ(msg["data"]["reason"], "broken");
}

TEST(item_protocol_v2_test, force_unequip_hammer_strip)
{
    auto msg = make_force_unequip("weapon", "hammer_strip");

    EXPECT_EQ(msg["type"], "force_unequip");
    EXPECT_EQ(msg["data"]["slot"], "weapon");
    EXPECT_EQ(msg["data"]["reason"], "hammer_strip");
}

TEST(item_protocol_v2_test, force_unequip_armor_break)
{
    auto msg = make_force_unequip("shield", "armor_break");

    EXPECT_EQ(msg["type"], "force_unequip");
    EXPECT_EQ(msg["data"]["slot"], "shield");
    EXPECT_EQ(msg["data"]["reason"], "armor_break");
}

// ============================================================
// 8. equipment_change
// ============================================================

TEST(item_protocol_v2_test, equipment_change_equip)
{
    auto itm = make_weapon();
    auto item_json = serialize_item(itm);
    auto msg = make_equipment_change(1001, "weapon", item_json);

    EXPECT_EQ(msg["type"], "equipment_change");
    EXPECT_EQ(msg["data"]["entity_id"], 1001);
    EXPECT_EQ(msg["data"]["slot"], "weapon");
    EXPECT_EQ(msg["data"]["item"]["item_id"], 12345);
    EXPECT_EQ(msg["data"]["item"]["name"], "Barbarian Sword");
}

TEST(item_protocol_v2_test, equipment_change_unequip)
{
    auto msg = make_equipment_change(1001, "weapon", nlohmann::json{});

    EXPECT_EQ(msg["type"], "equipment_change");
    EXPECT_EQ(msg["data"]["entity_id"], 1001);
    EXPECT_EQ(msg["data"]["slot"], "weapon");
    EXPECT_TRUE(msg["data"]["item"].is_null());
}

// ============================================================
// 9. ground_item_spawn (v2)
// ============================================================

TEST(item_protocol_v2_test, ground_item_spawn_v2_basic)
{
    auto itm = make_potion();
    auto msg = make_ground_item_spawn_v2(itm, "default", 150, 200);

    EXPECT_EQ(msg["type"], "ground_item_spawn");
    EXPECT_EQ(msg["data"]["map"], "default");
    EXPECT_EQ(msg["data"]["x"], 150);
    EXPECT_EQ(msg["data"]["y"], 200);
    EXPECT_EQ(msg["data"]["item"]["item_id"], 99999);
    EXPECT_EQ(msg["data"]["item"]["name"], "Health Potion");
}

// ============================================================
// 10. ground_item_removed (v2)
// ============================================================

TEST(item_protocol_v2_test, ground_item_removed_v2_basic)
{
    auto msg = make_ground_item_removed_v2(item_id(12345), "default", 150, 200);

    EXPECT_EQ(msg["type"], "ground_item_removed");
    EXPECT_EQ(msg["data"]["item_id"], 12345);
    EXPECT_EQ(msg["data"]["map"], "default");
    EXPECT_EQ(msg["data"]["x"], 150);
    EXPECT_EQ(msg["data"]["y"], 200);
}

// ============================================================
// 11. bank_slot_update (v2)
// ============================================================

TEST(item_protocol_v2_test, bank_slot_update_v2_basic)
{
    auto itm = make_weapon();
    auto msg = make_bank_slot_update_v2(0, 3, itm);

    EXPECT_EQ(msg["type"], "bank_slot_update");
    EXPECT_EQ(msg["data"]["page"], 0);
    EXPECT_EQ(msg["data"]["slot"], 3);
    EXPECT_EQ(msg["data"]["item"]["item_id"], 12345);
}

// ============================================================
// 12. bank_slot_cleared
// ============================================================

TEST(item_protocol_v2_test, bank_slot_cleared_basic)
{
    auto msg = make_bank_slot_cleared(0, 3);

    EXPECT_EQ(msg["type"], "bank_slot_cleared");
    EXPECT_EQ(msg["data"]["page"], 0);
    EXPECT_EQ(msg["data"]["slot"], 3);
}

// ============================================================
// 13. ability_activated
// ============================================================

TEST(item_protocol_v2_test, ability_activated_basic)
{
    auto msg = make_ability_activated(1001, "paralyze", 20000);

    EXPECT_EQ(msg["type"], "ability_activated");
    EXPECT_EQ(msg["data"]["entity_id"], 1001);
    EXPECT_EQ(msg["data"]["ability_type"], "paralyze");
    EXPECT_EQ(msg["data"]["duration_ms"], 20000);
}

// ============================================================
// 14. ability_expired
// ============================================================

TEST(item_protocol_v2_test, ability_expired_basic)
{
    auto msg = make_ability_expired(1001, "paralyze");

    EXPECT_EQ(msg["type"], "ability_expired");
    EXPECT_EQ(msg["data"]["entity_id"], 1001);
    EXPECT_EQ(msg["data"]["ability_type"], "paralyze");
}

// ============================================================
// Enum round-trip: new types parse correctly
// ============================================================

TEST(item_protocol_v2_test, parse_new_message_types)
{
    EXPECT_EQ(parse_message_type("inventory_item_add"), json_message_type::inventory_item_add);
    EXPECT_EQ(parse_message_type("inventory_item_delta"), json_message_type::inventory_item_delta);
    EXPECT_EQ(parse_message_type("inventory_gold_update"), json_message_type::inventory_gold_update);
    EXPECT_EQ(parse_message_type("force_unequip"), json_message_type::force_unequip);
    EXPECT_EQ(parse_message_type("equipment_change"), json_message_type::equipment_change);
    EXPECT_EQ(parse_message_type("bank_slot_cleared"), json_message_type::bank_slot_cleared);
    EXPECT_EQ(parse_message_type("ability_activated"), json_message_type::ability_activated);
    EXPECT_EQ(parse_message_type("ability_expired"), json_message_type::ability_expired);
}

TEST(item_protocol_v2_test, to_string_new_message_types)
{
    EXPECT_EQ(to_string(json_message_type::inventory_item_add), "inventory_item_add");
    EXPECT_EQ(to_string(json_message_type::inventory_item_delta), "inventory_item_delta");
    EXPECT_EQ(to_string(json_message_type::inventory_gold_update), "inventory_gold_update");
    EXPECT_EQ(to_string(json_message_type::force_unequip), "force_unequip");
    EXPECT_EQ(to_string(json_message_type::equipment_change), "equipment_change");
    EXPECT_EQ(to_string(json_message_type::bank_slot_cleared), "bank_slot_cleared");
    EXPECT_EQ(to_string(json_message_type::ability_activated), "ability_activated");
    EXPECT_EQ(to_string(json_message_type::ability_expired), "ability_expired");
}

// ============================================================
// v2 action message tests
// ============================================================

// --- inventory_reposition parser ---

TEST(item_protocol_v2_test, inventory_reposition_parser)
{
    auto j = json{{"item_id", 42}, {"pos_x", 3}, {"pos_y", 7}};
    auto res = inventory_reposition_data::from_json(j);
    ASSERT_TRUE(res.is_ok());
    EXPECT_EQ(res.value().item_id, 42);
    EXPECT_EQ(res.value().pos_x, 3);
    EXPECT_EQ(res.value().pos_y, 7);
}

TEST(item_protocol_v2_test, inventory_reposition_parser_missing_field)
{
    auto j = json{{"item_id", 42}, {"pos_x", 3}};
    auto res = inventory_reposition_data::from_json(j);
    EXPECT_FALSE(res.is_ok());
}

// --- equip_request parser ---

TEST(item_protocol_v2_test, equip_request_parser)
{
    auto j = json{{"item_id", 100}, {"slot", "weapon"}};
    auto res = equip_request_data::from_json(j);
    ASSERT_TRUE(res.is_ok());
    EXPECT_EQ(res.value().item_id, 100);
    EXPECT_EQ(res.value().slot, "weapon");
}

TEST(item_protocol_v2_test, equip_request_parser_missing_slot)
{
    auto j = json{{"item_id", 100}};
    auto res = equip_request_data::from_json(j);
    EXPECT_FALSE(res.is_ok());
}

// --- equip_result builder ---

TEST(item_protocol_v2_test, equip_result_success)
{
    auto msg = make_equip_result(true, "weapon");
    EXPECT_EQ(msg["type"], "equip_result");
    EXPECT_EQ(msg["data"]["success"], true);
    EXPECT_EQ(msg["data"]["slot"], "weapon");
}

TEST(item_protocol_v2_test, equip_result_failure)
{
    auto msg = make_equip_result(false, "body");
    EXPECT_EQ(msg["type"], "equip_result");
    EXPECT_EQ(msg["data"]["success"], false);
    EXPECT_EQ(msg["data"]["slot"], "body");
}

// --- unequip_request parser ---

TEST(item_protocol_v2_test, unequip_request_parser)
{
    auto j = json{{"slot", "shield"}};
    auto res = unequip_request_data::from_json(j);
    ASSERT_TRUE(res.is_ok());
    EXPECT_EQ(res.value().slot, "shield");
}

TEST(item_protocol_v2_test, unequip_request_parser_missing_slot)
{
    auto j = json{};
    auto res = unequip_request_data::from_json(j);
    EXPECT_FALSE(res.is_ok());
}

// --- unequip_result builder ---

TEST(item_protocol_v2_test, unequip_result_success)
{
    auto msg = make_unequip_result(true, "shield");
    EXPECT_EQ(msg["type"], "unequip_result");
    EXPECT_EQ(msg["data"]["success"], true);
    EXPECT_EQ(msg["data"]["slot"], "shield");
}

// --- pickup_request parser ---

TEST(item_protocol_v2_test, pickup_request_parser)
{
    auto j = json{{"map", "default"}, {"x", 150}, {"y", 200}};
    auto res = pickup_request_data::from_json(j);
    ASSERT_TRUE(res.is_ok());
    EXPECT_EQ(res.value().map, "default");
    EXPECT_EQ(res.value().x, 150);
    EXPECT_EQ(res.value().y, 200);
}

TEST(item_protocol_v2_test, pickup_request_parser_missing_map)
{
    auto j = json{{"x", 150}, {"y", 200}};
    auto res = pickup_request_data::from_json(j);
    EXPECT_FALSE(res.is_ok());
}

// --- pickup_result builder ---

TEST(item_protocol_v2_test, pickup_result_success)
{
    auto msg = make_pickup_result(true);
    EXPECT_EQ(msg["type"], "pickup_result");
    EXPECT_EQ(msg["data"]["success"], true);
}

TEST(item_protocol_v2_test, pickup_result_failure)
{
    auto msg = make_pickup_result(false);
    EXPECT_EQ(msg["type"], "pickup_result");
    EXPECT_EQ(msg["data"]["success"], false);
}

// --- drop_request parser ---

TEST(item_protocol_v2_test, drop_request_parser)
{
    auto j = json{{"item_id", 555}};
    auto res = drop_request_data::from_json(j);
    ASSERT_TRUE(res.is_ok());
    EXPECT_EQ(res.value().item_id, 555);
}

TEST(item_protocol_v2_test, drop_request_parser_missing_item_id)
{
    auto j = json{};
    auto res = drop_request_data::from_json(j);
    EXPECT_FALSE(res.is_ok());
}

// --- drop_result builder ---

TEST(item_protocol_v2_test, drop_result_success)
{
    auto msg = make_drop_result(true);
    EXPECT_EQ(msg["type"], "drop_result");
    EXPECT_EQ(msg["data"]["success"], true);
}

// --- use_item_request (v2) parser ---

TEST(item_protocol_v2_test, use_item_request_v2_parser)
{
    auto j = json{{"item_id", 777}};
    auto res = use_item_request_data_v2::from_json(j);
    ASSERT_TRUE(res.is_ok());
    EXPECT_EQ(res.value().item_id, 777);
}

// --- use_item_result builder ---

TEST(item_protocol_v2_test, use_item_result_success)
{
    auto msg = make_use_item_result(true);
    EXPECT_EQ(msg["type"], "use_item_result");
    EXPECT_EQ(msg["data"]["success"], true);
}

TEST(item_protocol_v2_test, use_item_result_failure)
{
    auto msg = make_use_item_result(false);
    EXPECT_EQ(msg["type"], "use_item_result");
    EXPECT_EQ(msg["data"]["success"], false);
}

// --- upgrade_request parser ---

TEST(item_protocol_v2_test, upgrade_request_parser)
{
    auto j = json{{"target_id", 10}, {"stone_id", 20}};
    auto res = upgrade_request_data::from_json(j);
    ASSERT_TRUE(res.is_ok());
    EXPECT_EQ(res.value().target_id, 10);
    EXPECT_EQ(res.value().stone_id, 20);
}

TEST(item_protocol_v2_test, upgrade_request_parser_missing_stone)
{
    auto j = json{{"target_id", 10}};
    auto res = upgrade_request_data::from_json(j);
    EXPECT_FALSE(res.is_ok());
}

// --- upgrade_result builder ---

TEST(item_protocol_v2_test, upgrade_result_v2_success)
{
    auto msg = make_upgrade_result_v2(true);
    EXPECT_EQ(msg["type"], "upgrade_result");
    EXPECT_EQ(msg["data"]["success"], true);
}

// --- shop_buy_request (v2) parser ---

TEST(item_protocol_v2_test, shop_buy_request_v2_parser)
{
    auto j = json{{"template_id", 300}};
    auto res = shop_buy_request_data_v2::from_json(j);
    ASSERT_TRUE(res.is_ok());
    EXPECT_EQ(res.value().template_id, 300);
}

TEST(item_protocol_v2_test, shop_buy_request_v2_parser_missing)
{
    auto j = json{};
    auto res = shop_buy_request_data_v2::from_json(j);
    EXPECT_FALSE(res.is_ok());
}

// --- shop_buy_result builder ---

TEST(item_protocol_v2_test, shop_buy_result_success)
{
    auto msg = make_shop_buy_result(true);
    EXPECT_EQ(msg["type"], "shop_buy_result");
    EXPECT_EQ(msg["data"]["success"], true);
}

// --- shop_sell_request (v2) parser ---

TEST(item_protocol_v2_test, shop_sell_request_v2_parser)
{
    auto j = json{{"item_id", 400}};
    auto res = shop_sell_request_data_v2::from_json(j);
    ASSERT_TRUE(res.is_ok());
    EXPECT_EQ(res.value().item_id, 400);
}

// --- shop_sell_result builder ---

TEST(item_protocol_v2_test, shop_sell_result_success)
{
    auto msg = make_shop_sell_result(true);
    EXPECT_EQ(msg["type"], "shop_sell_result");
    EXPECT_EQ(msg["data"]["success"], true);
}

// --- shop_repair_request (v2) parser ---

TEST(item_protocol_v2_test, shop_repair_request_v2_parser)
{
    auto j = json{{"item_id", 500}};
    auto res = shop_repair_request_data_v2::from_json(j);
    ASSERT_TRUE(res.is_ok());
    EXPECT_EQ(res.value().item_id, 500);
}

// --- shop_repair_result builder ---

TEST(item_protocol_v2_test, shop_repair_result_success)
{
    auto msg = make_shop_repair_result(true);
    EXPECT_EQ(msg["type"], "shop_repair_result");
    EXPECT_EQ(msg["data"]["success"], true);
}

// --- bank_deposit_request (v2) parser ---

TEST(item_protocol_v2_test, bank_deposit_request_v2_auto_deposit)
{
    auto j = json{{"item_id", 600}};
    auto res = bank_deposit_request_data_v2::from_json(j);
    ASSERT_TRUE(res.is_ok());
    EXPECT_EQ(res.value().item_id, 600);
    EXPECT_FALSE(res.value().page.has_value());
    EXPECT_FALSE(res.value().slot.has_value());
}

TEST(item_protocol_v2_test, bank_deposit_request_v2_targeted)
{
    auto j = json{{"item_id", 600}, {"page", 2}, {"slot", 5}};
    auto res = bank_deposit_request_data_v2::from_json(j);
    ASSERT_TRUE(res.is_ok());
    EXPECT_EQ(res.value().item_id, 600);
    ASSERT_TRUE(res.value().page.has_value());
    EXPECT_EQ(*res.value().page, 2);
    ASSERT_TRUE(res.value().slot.has_value());
    EXPECT_EQ(*res.value().slot, 5);
}

TEST(item_protocol_v2_test, bank_deposit_request_v2_missing_item_id)
{
    auto j = json{{"page", 0}, {"slot", 0}};
    auto res = bank_deposit_request_data_v2::from_json(j);
    EXPECT_FALSE(res.is_ok());
}

// --- bank_deposit_result builder ---

TEST(item_protocol_v2_test, bank_deposit_result_success)
{
    auto msg = make_bank_deposit_result(true);
    EXPECT_EQ(msg["type"], "bank_deposit_result");
    EXPECT_EQ(msg["data"]["success"], true);
}

// --- bank_withdraw_request (v2) parser ---

TEST(item_protocol_v2_test, bank_withdraw_request_v2_parser)
{
    auto j = json{{"page", 1}, {"slot", 3}};
    auto res = bank_withdraw_request_data_v2::from_json(j);
    ASSERT_TRUE(res.is_ok());
    EXPECT_EQ(res.value().page, 1);
    EXPECT_EQ(res.value().slot, 3);
}

TEST(item_protocol_v2_test, bank_withdraw_request_v2_missing_slot)
{
    auto j = json{{"page", 1}};
    auto res = bank_withdraw_request_data_v2::from_json(j);
    EXPECT_FALSE(res.is_ok());
}

// --- bank_withdraw_result builder ---

TEST(item_protocol_v2_test, bank_withdraw_result_success)
{
    auto msg = make_bank_withdraw_result(true);
    EXPECT_EQ(msg["type"], "bank_withdraw_result");
    EXPECT_EQ(msg["data"]["success"], true);
}

// --- bank_reposition_request parser ---

TEST(item_protocol_v2_test, bank_reposition_request_parser)
{
    auto j = json{{"from_page", 0}, {"from_slot", 2}, {"to_page", 1}, {"to_slot", 5}};
    auto res = bank_reposition_request_data::from_json(j);
    ASSERT_TRUE(res.is_ok());
    EXPECT_EQ(res.value().from_page, 0);
    EXPECT_EQ(res.value().from_slot, 2);
    EXPECT_EQ(res.value().to_page, 1);
    EXPECT_EQ(res.value().to_slot, 5);
}

TEST(item_protocol_v2_test, bank_reposition_request_parser_missing_field)
{
    auto j = json{{"from_page", 0}, {"from_slot", 2}, {"to_page", 1}};
    auto res = bank_reposition_request_data::from_json(j);
    EXPECT_FALSE(res.is_ok());
}

// --- bank_reposition_result builder ---

TEST(item_protocol_v2_test, bank_reposition_result_success)
{
    auto msg = make_bank_reposition_result(true);
    EXPECT_EQ(msg["type"], "bank_reposition_result");
    EXPECT_EQ(msg["data"]["success"], true);
}

// --- activate_ability_request parser ---

TEST(item_protocol_v2_test, activate_ability_request_parser)
{
    auto j = json{{"item_id", 888}};
    auto res = activate_ability_request_data::from_json(j);
    ASSERT_TRUE(res.is_ok());
    EXPECT_EQ(res.value().item_id, 888);
}

TEST(item_protocol_v2_test, activate_ability_request_parser_missing)
{
    auto j = json{};
    auto res = activate_ability_request_data::from_json(j);
    EXPECT_FALSE(res.is_ok());
}

// --- activate_ability_failed builder ---

TEST(item_protocol_v2_test, activate_ability_failed_builder)
{
    auto msg = make_activate_ability_failed("cooldown_active");
    EXPECT_EQ(msg["type"], "activate_ability_failed");
    EXPECT_EQ(msg["data"]["error"], "cooldown_active");
}

// ============================================================
// parse_message_type round-trips for v2 action messages
// ============================================================

TEST(item_protocol_v2_test, parse_v2_action_message_types)
{
    EXPECT_EQ(parse_message_type("inventory_reposition"), json_message_type::inventory_reposition);
    EXPECT_EQ(parse_message_type("equip_request"), json_message_type::equip_request);
    EXPECT_EQ(parse_message_type("equip_result"), json_message_type::equip_result);
    EXPECT_EQ(parse_message_type("unequip_request"), json_message_type::unequip_request);
    EXPECT_EQ(parse_message_type("unequip_result"), json_message_type::unequip_result);
    EXPECT_EQ(parse_message_type("pickup_request"), json_message_type::pickup_request);
    EXPECT_EQ(parse_message_type("pickup_result"), json_message_type::pickup_result);
    EXPECT_EQ(parse_message_type("drop_request"), json_message_type::drop_request);
    EXPECT_EQ(parse_message_type("drop_result"), json_message_type::drop_result);
    EXPECT_EQ(parse_message_type("use_item_request"), json_message_type::use_item_request);
    EXPECT_EQ(parse_message_type("use_item_result"), json_message_type::use_item_result);
    EXPECT_EQ(parse_message_type("upgrade_request"), json_message_type::upgrade_request);
    EXPECT_EQ(parse_message_type("upgrade_result"), json_message_type::upgrade_result);
    EXPECT_EQ(parse_message_type("shop_buy_request_v2"), json_message_type::shop_buy_request_v2);
    EXPECT_EQ(parse_message_type("shop_buy_result"), json_message_type::shop_buy_result);
    EXPECT_EQ(parse_message_type("shop_sell_request_v2"), json_message_type::shop_sell_request_v2);
    EXPECT_EQ(parse_message_type("shop_sell_result"), json_message_type::shop_sell_result);
    EXPECT_EQ(parse_message_type("shop_repair_request_v2"), json_message_type::shop_repair_request_v2);
    EXPECT_EQ(parse_message_type("shop_repair_result"), json_message_type::shop_repair_result);
    EXPECT_EQ(parse_message_type("bank_deposit_request_v2"), json_message_type::bank_deposit_request_v2);
    EXPECT_EQ(parse_message_type("bank_deposit_result"), json_message_type::bank_deposit_result);
    EXPECT_EQ(parse_message_type("bank_withdraw_request_v2"), json_message_type::bank_withdraw_request_v2);
    EXPECT_EQ(parse_message_type("bank_withdraw_result"), json_message_type::bank_withdraw_result);
    EXPECT_EQ(parse_message_type("bank_reposition_request"), json_message_type::bank_reposition_request);
    EXPECT_EQ(parse_message_type("bank_reposition_result"), json_message_type::bank_reposition_result);
    EXPECT_EQ(parse_message_type("activate_ability_request_v2"), json_message_type::activate_ability_request_v2);
    EXPECT_EQ(parse_message_type("activate_ability_failed"), json_message_type::activate_ability_failed);
}

TEST(item_protocol_v2_test, to_string_v2_action_message_types)
{
    EXPECT_EQ(to_string(json_message_type::inventory_reposition), "inventory_reposition");
    EXPECT_EQ(to_string(json_message_type::equip_request), "equip_request");
    EXPECT_EQ(to_string(json_message_type::equip_result), "equip_result");
    EXPECT_EQ(to_string(json_message_type::unequip_request), "unequip_request");
    EXPECT_EQ(to_string(json_message_type::unequip_result), "unequip_result");
    EXPECT_EQ(to_string(json_message_type::pickup_request), "pickup_request");
    EXPECT_EQ(to_string(json_message_type::pickup_result), "pickup_result");
    EXPECT_EQ(to_string(json_message_type::drop_request), "drop_request");
    EXPECT_EQ(to_string(json_message_type::drop_result), "drop_result");
    EXPECT_EQ(to_string(json_message_type::use_item_request), "use_item_request");
    EXPECT_EQ(to_string(json_message_type::use_item_result), "use_item_result");
    EXPECT_EQ(to_string(json_message_type::upgrade_request), "upgrade_request");
    EXPECT_EQ(to_string(json_message_type::upgrade_result), "upgrade_result");
    EXPECT_EQ(to_string(json_message_type::shop_buy_request_v2), "shop_buy_request_v2");
    EXPECT_EQ(to_string(json_message_type::shop_buy_result), "shop_buy_result");
    EXPECT_EQ(to_string(json_message_type::shop_sell_request_v2), "shop_sell_request_v2");
    EXPECT_EQ(to_string(json_message_type::shop_sell_result), "shop_sell_result");
    EXPECT_EQ(to_string(json_message_type::shop_repair_request_v2), "shop_repair_request_v2");
    EXPECT_EQ(to_string(json_message_type::shop_repair_result), "shop_repair_result");
    EXPECT_EQ(to_string(json_message_type::bank_deposit_request_v2), "bank_deposit_request_v2");
    EXPECT_EQ(to_string(json_message_type::bank_deposit_result), "bank_deposit_result");
    EXPECT_EQ(to_string(json_message_type::bank_withdraw_request_v2), "bank_withdraw_request_v2");
    EXPECT_EQ(to_string(json_message_type::bank_withdraw_result), "bank_withdraw_result");
    EXPECT_EQ(to_string(json_message_type::bank_reposition_request), "bank_reposition_request");
    EXPECT_EQ(to_string(json_message_type::bank_reposition_result), "bank_reposition_result");
    EXPECT_EQ(to_string(json_message_type::activate_ability_request_v2), "activate_ability_request_v2");
    EXPECT_EQ(to_string(json_message_type::activate_ability_failed), "activate_ability_failed");
}

// ============================================================
// v2 trade protocol messages
// ============================================================

// --- Phase 0: Initiating ---

TEST(item_protocol_v2_test, trade_request_parser)
{
    json j = {{"target_entity_id", 42}};
    auto res = trade_request_data_v2::from_json(j);
    ASSERT_TRUE(res.is_ok());
    EXPECT_EQ(res.value().target_entity_id, 42);
}

TEST(item_protocol_v2_test, trade_request_parser_missing_field)
{
    json j = {{"wrong_field", 42}};
    auto res = trade_request_data_v2::from_json(j);
    ASSERT_FALSE(res.is_ok());
}

TEST(item_protocol_v2_test, trade_invite_builder)
{
    auto msg = make_trade_invite(100, "TestPlayer");
    EXPECT_EQ(msg["type"], "trade_invite");
    EXPECT_EQ(msg["data"]["from_entity_id"], 100);
    EXPECT_EQ(msg["data"]["from_name"], "TestPlayer");
}

TEST(item_protocol_v2_test, trade_accept_parser)
{
    json j = {{"from_entity_id", 55}};
    auto res = trade_accept_data::from_json(j);
    ASSERT_TRUE(res.is_ok());
    EXPECT_EQ(res.value().from_entity_id, 55);
}

TEST(item_protocol_v2_test, trade_accept_parser_missing_field)
{
    json j = json::object();
    auto res = trade_accept_data::from_json(j);
    ASSERT_FALSE(res.is_ok());
}

TEST(item_protocol_v2_test, trade_decline_parser)
{
    json j = {{"from_entity_id", 77}};
    auto res = trade_decline_data::from_json(j);
    ASSERT_TRUE(res.is_ok());
    EXPECT_EQ(res.value().from_entity_id, 77);
}

TEST(item_protocol_v2_test, trade_decline_parser_missing_field)
{
    json j = {{"from_entity_id", "not_a_number"}};
    auto res = trade_decline_data::from_json(j);
    ASSERT_FALSE(res.is_ok());
}

TEST(item_protocol_v2_test, trade_opened_builder)
{
    auto msg = make_trade_opened(200, "PartnerName");
    EXPECT_EQ(msg["type"], "trade_opened");
    EXPECT_EQ(msg["data"]["partner_entity_id"], 200);
    EXPECT_EQ(msg["data"]["partner_name"], "PartnerName");
}

// --- Phase 1: Offer ---

TEST(item_protocol_v2_test, trade_add_item_parser)
{
    json j = {{"item_id", 1001}};
    auto res = trade_add_item_data::from_json(j);
    ASSERT_TRUE(res.is_ok());
    EXPECT_EQ(res.value().item_id, 1001);
}

TEST(item_protocol_v2_test, trade_add_item_parser_missing_field)
{
    json j = json::object();
    auto res = trade_add_item_data::from_json(j);
    ASSERT_FALSE(res.is_ok());
}

TEST(item_protocol_v2_test, trade_remove_item_parser)
{
    json j = {{"item_id", 2002}};
    auto res = trade_remove_item_data::from_json(j);
    ASSERT_TRUE(res.is_ok());
    EXPECT_EQ(res.value().item_id, 2002);
}

TEST(item_protocol_v2_test, trade_remove_item_parser_missing_field)
{
    json j = {{"item_id", true}};
    auto res = trade_remove_item_data::from_json(j);
    ASSERT_FALSE(res.is_ok());
}

TEST(item_protocol_v2_test, trade_set_gold_parser)
{
    json j = {{"amount", 50000}};
    auto res = trade_set_gold_data::from_json(j);
    ASSERT_TRUE(res.is_ok());
    EXPECT_EQ(res.value().amount, 50000);
}

TEST(item_protocol_v2_test, trade_set_gold_parser_large_value)
{
    json j = {{"amount", int64_t(2000000000)}};
    auto res = trade_set_gold_data::from_json(j);
    ASSERT_TRUE(res.is_ok());
    EXPECT_EQ(res.value().amount, 2000000000);
}

TEST(item_protocol_v2_test, trade_set_gold_parser_missing_field)
{
    json j = json::object();
    auto res = trade_set_gold_data::from_json(j);
    ASSERT_FALSE(res.is_ok());
}

TEST(item_protocol_v2_test, trade_update_builder)
{
    std::vector<json> items = {
        {{"item_id", 100}, {"name", "Sword"}},
        {{"item_id", 200}, {"name", "Shield"}}};
    auto msg = make_trade_update("mine", items, 5000);
    EXPECT_EQ(msg["type"], "trade_update");
    EXPECT_EQ(msg["data"]["side"], "mine");
    EXPECT_EQ(msg["data"]["gold"], 5000);
    ASSERT_EQ(msg["data"]["items"].size(), 2u);
    EXPECT_EQ(msg["data"]["items"][0]["item_id"], 100);
    EXPECT_EQ(msg["data"]["items"][1]["name"], "Shield");
}

TEST(item_protocol_v2_test, trade_update_builder_theirs_empty)
{
    std::vector<json> items;
    auto msg = make_trade_update("theirs", items, 0);
    EXPECT_EQ(msg["data"]["side"], "theirs");
    EXPECT_TRUE(msg["data"]["items"].empty());
    EXPECT_EQ(msg["data"]["gold"], 0);
}

// --- Phase 2: Lock ---

TEST(item_protocol_v2_test, trade_lock_status_builder_both_unlocked)
{
    auto msg = make_trade_lock_status(false, false);
    EXPECT_EQ(msg["type"], "trade_lock_status");
    EXPECT_EQ(msg["data"]["my_locked"], false);
    EXPECT_EQ(msg["data"]["their_locked"], false);
}

TEST(item_protocol_v2_test, trade_lock_status_builder_both_locked)
{
    auto msg = make_trade_lock_status(true, true);
    EXPECT_EQ(msg["data"]["my_locked"], true);
    EXPECT_EQ(msg["data"]["their_locked"], true);
}

// --- Phase 3: Confirm ---

TEST(item_protocol_v2_test, trade_complete_builder_success)
{
    auto msg = make_trade_complete(true);
    EXPECT_EQ(msg["type"], "trade_complete");
    EXPECT_EQ(msg["data"]["success"], true);
}

TEST(item_protocol_v2_test, trade_complete_builder_failure)
{
    auto msg = make_trade_complete(false);
    EXPECT_EQ(msg["data"]["success"], false);
}

// --- Cancellation ---

TEST(item_protocol_v2_test, trade_canceled_builder_player)
{
    auto msg = make_trade_canceled("player_canceled");
    EXPECT_EQ(msg["type"], "trade_canceled");
    EXPECT_EQ(msg["data"]["reason"], "player_canceled");
}

TEST(item_protocol_v2_test, trade_canceled_builder_out_of_range)
{
    auto msg = make_trade_canceled("out_of_range");
    EXPECT_EQ(msg["data"]["reason"], "out_of_range");
}

TEST(item_protocol_v2_test, trade_canceled_builder_disconnected)
{
    auto msg = make_trade_canceled("disconnected");
    EXPECT_EQ(msg["data"]["reason"], "disconnected");
}

// --- parse_message_type round-trip ---

TEST(item_protocol_v2_test, parse_message_type_trade_messages)
{
    EXPECT_EQ(parse_message_type("trade_request"), json_message_type::trade_request);
    EXPECT_EQ(parse_message_type("trade_invite"), json_message_type::trade_invite);
    EXPECT_EQ(parse_message_type("trade_accept"), json_message_type::trade_accept);
    EXPECT_EQ(parse_message_type("trade_decline"), json_message_type::trade_decline);
    EXPECT_EQ(parse_message_type("trade_opened"), json_message_type::trade_opened);
    EXPECT_EQ(parse_message_type("trade_add_item"), json_message_type::trade_add_item);
    EXPECT_EQ(parse_message_type("trade_remove_item"), json_message_type::trade_remove_item);
    EXPECT_EQ(parse_message_type("trade_set_gold"), json_message_type::trade_set_gold);
    EXPECT_EQ(parse_message_type("trade_update"), json_message_type::trade_update);
    EXPECT_EQ(parse_message_type("trade_lock"), json_message_type::trade_lock);
    EXPECT_EQ(parse_message_type("trade_lock_status"), json_message_type::trade_lock_status);
    EXPECT_EQ(parse_message_type("trade_confirm"), json_message_type::trade_confirm);
    EXPECT_EQ(parse_message_type("trade_complete"), json_message_type::trade_complete);
    EXPECT_EQ(parse_message_type("trade_cancel"), json_message_type::trade_cancel);
    EXPECT_EQ(parse_message_type("trade_canceled"), json_message_type::trade_canceled);
}

// --- to_string round-trip ---

TEST(item_protocol_v2_test, to_string_trade_message_types)
{
    EXPECT_EQ(to_string(json_message_type::trade_request), "trade_request");
    EXPECT_EQ(to_string(json_message_type::trade_invite), "trade_invite");
    EXPECT_EQ(to_string(json_message_type::trade_accept), "trade_accept");
    EXPECT_EQ(to_string(json_message_type::trade_decline), "trade_decline");
    EXPECT_EQ(to_string(json_message_type::trade_opened), "trade_opened");
    EXPECT_EQ(to_string(json_message_type::trade_add_item), "trade_add_item");
    EXPECT_EQ(to_string(json_message_type::trade_remove_item), "trade_remove_item");
    EXPECT_EQ(to_string(json_message_type::trade_set_gold), "trade_set_gold");
    EXPECT_EQ(to_string(json_message_type::trade_update), "trade_update");
    EXPECT_EQ(to_string(json_message_type::trade_lock), "trade_lock");
    EXPECT_EQ(to_string(json_message_type::trade_lock_status), "trade_lock_status");
    EXPECT_EQ(to_string(json_message_type::trade_confirm), "trade_confirm");
    EXPECT_EQ(to_string(json_message_type::trade_complete), "trade_complete");
    EXPECT_EQ(to_string(json_message_type::trade_cancel), "trade_cancel");
    EXPECT_EQ(to_string(json_message_type::trade_canceled), "trade_canceled");
}

// ============================================================
// inventory_data v2 (login payload)
// ============================================================

TEST(item_protocol_v2_test, inventory_data_v2_full)
{
    // Create mock serialized item JSON objects
    auto sword_json = json{
        {"item_id", 12345},
        {"template_id", 100},
        {"name", "Barbarian Sword"},
        {"type", "weapon"},
        {"equip_pos", "weapon"},
        {"count", 1},
        {"weight", 800}};

    auto shield_json = json{
        {"item_id", 12350},
        {"template_id", 200},
        {"name", "Iron Shield"},
        {"type", "armor"},
        {"equip_pos", "shield"},
        {"count", 1},
        {"weight", 500}};

    auto potion_json = json{
        {"item_id", 99999},
        {"template_id", 500},
        {"name", "Health Potion"},
        {"type", "consumable"},
        {"count", 5},
        {"weight", 100}};

    // Build items vector: (serialized_item, pos_x, pos_y, z_order)
    std::vector<std::tuple<json, int16_t, int16_t, int32_t>> items;
    items.emplace_back(sword_json, int16_t{30}, int16_t{40}, 0);
    items.emplace_back(shield_json, int16_t{80}, int16_t{40}, 1);
    items.emplace_back(potion_json, int16_t{120}, int16_t{60}, 2);

    // Equipment slots (sword in weapon, shield in shield)
    std::map<std::string, uint32_t> equipment_slots;
    equipment_slots["weapon"] = 12345;
    equipment_slots["shield"] = 12350;

    auto msg = make_inventory_data_v2(items, equipment_slots, 50000, 3200, 5500);

    EXPECT_EQ(msg["type"], "inventory_data");
    auto& data = msg["data"];

    // Verify items array
    ASSERT_EQ(data["items"].size(), 3u);

    EXPECT_EQ(data["items"][0]["item"]["item_id"], 12345);
    EXPECT_EQ(data["items"][0]["item"]["name"], "Barbarian Sword");
    EXPECT_EQ(data["items"][0]["pos_x"], 30);
    EXPECT_EQ(data["items"][0]["pos_y"], 40);
    EXPECT_EQ(data["items"][0]["z_order"], 0);

    EXPECT_EQ(data["items"][1]["item"]["item_id"], 12350);
    EXPECT_EQ(data["items"][1]["item"]["name"], "Iron Shield");
    EXPECT_EQ(data["items"][1]["pos_x"], 80);
    EXPECT_EQ(data["items"][1]["pos_y"], 40);
    EXPECT_EQ(data["items"][1]["z_order"], 1);

    EXPECT_EQ(data["items"][2]["item"]["item_id"], 99999);
    EXPECT_EQ(data["items"][2]["item"]["name"], "Health Potion");
    EXPECT_EQ(data["items"][2]["pos_x"], 120);
    EXPECT_EQ(data["items"][2]["pos_y"], 60);
    EXPECT_EQ(data["items"][2]["z_order"], 2);

    // Verify equipment_slots
    EXPECT_EQ(data["equipment_slots"]["weapon"], 12345);
    EXPECT_EQ(data["equipment_slots"]["shield"], 12350);
    EXPECT_FALSE(data["equipment_slots"].contains("body"));

    // Verify scalars
    EXPECT_EQ(data["gold"], 50000);
    EXPECT_EQ(data["weight"], 3200);
    EXPECT_EQ(data["max_weight"], 5500);
}

TEST(item_protocol_v2_test, inventory_data_v2_empty)
{
    std::vector<std::tuple<json, int16_t, int16_t, int32_t>> items;
    std::map<std::string, uint32_t> equipment_slots;

    auto msg = make_inventory_data_v2(items, equipment_slots, 0, 0, 5000);

    EXPECT_EQ(msg["type"], "inventory_data");
    auto& data = msg["data"];
    EXPECT_TRUE(data["items"].empty());
    EXPECT_TRUE(data["equipment_slots"].empty());
    EXPECT_EQ(data["gold"], 0);
    EXPECT_EQ(data["weight"], 0);
    EXPECT_EQ(data["max_weight"], 5000);
}

TEST(item_protocol_v2_test, inventory_data_v2_with_serialize_item)
{
    // Use actual serialize_item to produce item JSON
    auto sword = make_weapon();
    auto sword_json = serialize_item(sword);

    std::vector<std::tuple<json, int16_t, int16_t, int32_t>> items;
    items.emplace_back(sword_json, int16_t{30}, int16_t{40}, 0);

    std::map<std::string, uint32_t> equipment_slots;
    equipment_slots["weapon"] = 12345;

    auto msg = make_inventory_data_v2(items, equipment_slots, 15000, 800, 4000);

    auto& data = msg["data"];
    ASSERT_EQ(data["items"].size(), 1u);
    EXPECT_EQ(data["items"][0]["item"]["item_id"], 12345);
    EXPECT_EQ(data["items"][0]["item"]["name"], "Barbarian Sword");
    EXPECT_EQ(data["items"][0]["item"]["type"], "weapon");
    EXPECT_EQ(data["items"][0]["item"]["equip_pos"], "weapon");
    EXPECT_EQ(data["items"][0]["item"]["rarity"], "rare");
    EXPECT_EQ(data["items"][0]["pos_x"], 30);
    EXPECT_EQ(data["items"][0]["pos_y"], 40);
    EXPECT_EQ(data["equipment_slots"]["weapon"], 12345);
    EXPECT_EQ(data["gold"], 15000);
}

// ============================================================
// shop_open tests
// ============================================================

TEST(item_protocol_v2_test, shop_open_basic)
{
    auto sword = make_weapon();
    auto sword_json = serialize_item(sword);

    auto potion = make_potion();
    auto potion_json = serialize_item(potion);

    std::vector<std::pair<json, int32_t>> items;
    items.emplace_back(sword_json, 15000);
    items.emplace_back(potion_json, 8000);

    auto msg = make_shop_open("William the Blacksmith", "weapon", items);

    EXPECT_EQ(msg["type"], "shop_open");
    auto& data = msg["data"];
    EXPECT_EQ(data["npc_name"], "William the Blacksmith");
    EXPECT_EQ(data["shop_type"], "weapon");
    ASSERT_EQ(data["items"].size(), 2u);
    EXPECT_EQ(data["items"][0]["buy_price"], 15000);
    EXPECT_EQ(data["items"][0]["item"]["name"], "Barbarian Sword");
    EXPECT_EQ(data["items"][1]["buy_price"], 8000);
    EXPECT_EQ(data["items"][1]["item"]["name"], "Health Potion");
}

TEST(item_protocol_v2_test, shop_open_empty_items)
{
    std::vector<std::pair<json, int32_t>> items;
    auto msg = make_shop_open("Innkeeper", "general", items);

    EXPECT_EQ(msg["type"], "shop_open");
    auto& data = msg["data"];
    EXPECT_EQ(data["npc_name"], "Innkeeper");
    EXPECT_EQ(data["shop_type"], "general");
    EXPECT_TRUE(data["items"].is_array());
    EXPECT_EQ(data["items"].size(), 0u);
}

TEST(item_protocol_v2_test, shop_open_parse_message_type)
{
    EXPECT_EQ(parse_message_type("shop_open"), json_message_type::shop_open);
    EXPECT_EQ(to_string(json_message_type::shop_open), "shop_open");
}

// ============================================================
// bank_open tests
// ============================================================

TEST(item_protocol_v2_test, bank_open_v2_basic)
{
    auto sword = make_weapon();
    auto sword_json = serialize_item(sword);

    auto potion = make_potion();
    auto potion_json = serialize_item(potion);

    // Page 0: sword in slot 0, potion in slot 2, rest null
    std::vector<json> page0(12, json{});
    page0[0] = sword_json;
    page0[2] = potion_json;

    // Page 1: all empty
    std::vector<json> page1(12, json{});

    std::vector<std::vector<json>> pages = {page0, page1};
    auto msg = make_bank_open_v2(pages, 4);

    EXPECT_EQ(msg["type"], "bank_open");
    auto& data = msg["data"];
    EXPECT_EQ(data["total_pages"], 4);
    ASSERT_EQ(data["pages"].size(), 2u);

    // Page 0
    EXPECT_EQ(data["pages"][0]["page_num"], 0);
    ASSERT_EQ(data["pages"][0]["slots"].size(), 12u);
    EXPECT_FALSE(data["pages"][0]["slots"][0].is_null());
    EXPECT_EQ(data["pages"][0]["slots"][0]["name"], "Barbarian Sword");
    EXPECT_TRUE(data["pages"][0]["slots"][1].is_null());
    EXPECT_FALSE(data["pages"][0]["slots"][2].is_null());
    EXPECT_EQ(data["pages"][0]["slots"][2]["name"], "Health Potion");
    EXPECT_TRUE(data["pages"][0]["slots"][3].is_null());

    // Page 1 — all null
    EXPECT_EQ(data["pages"][1]["page_num"], 1);
    ASSERT_EQ(data["pages"][1]["slots"].size(), 12u);
    for (size_t i = 0; i < 12; ++i)
    {
        EXPECT_TRUE(data["pages"][1]["slots"][i].is_null());
    }
}

TEST(item_protocol_v2_test, bank_open_v2_empty_pages)
{
    std::vector<std::vector<json>> pages;
    auto msg = make_bank_open_v2(pages, 0);

    EXPECT_EQ(msg["type"], "bank_open");
    auto& data = msg["data"];
    EXPECT_EQ(data["total_pages"], 0);
    EXPECT_TRUE(data["pages"].is_array());
    EXPECT_EQ(data["pages"].size(), 0u);
}

TEST(item_protocol_v2_test, bank_open_parse_message_type)
{
    EXPECT_EQ(parse_message_type("bank_open"), json_message_type::bank_open);
    EXPECT_EQ(to_string(json_message_type::bank_open), "bank_open");
}

// ============================================================
// Party loot distribution messages
// ============================================================

// --- set_loot_rule parser ---

TEST(item_protocol_v2_test, set_loot_rule_parse_valid)
{
    auto j = json{{"rule", "greed"}};
    auto result = set_loot_rule_data::from_json(j);
    ASSERT_TRUE(result.is_ok());
    EXPECT_EQ(result.value().rule, "greed");
}

TEST(item_protocol_v2_test, set_loot_rule_parse_all_rules)
{
    for (const auto& rule : {"disabled", "greed", "master"})
    {
        auto j = json{{"rule", rule}};
        auto result = set_loot_rule_data::from_json(j);
        ASSERT_TRUE(result.is_ok()) << "Failed for rule: " << rule;
        EXPECT_EQ(result.value().rule, rule);
    }
}

TEST(item_protocol_v2_test, set_loot_rule_parse_invalid_rule)
{
    auto j = json{{"rule", "ffa"}};
    auto result = set_loot_rule_data::from_json(j);
    ASSERT_FALSE(result.is_ok());
}

TEST(item_protocol_v2_test, set_loot_rule_parse_missing_rule)
{
    auto j = json{{"other", "value"}};
    auto result = set_loot_rule_data::from_json(j);
    ASSERT_FALSE(result.is_ok());
}

TEST(item_protocol_v2_test, set_loot_rule_parse_non_string_rule)
{
    auto j = json{{"rule", 42}};
    auto result = set_loot_rule_data::from_json(j);
    ASSERT_FALSE(result.is_ok());
}

// --- loot_roll parser ---

TEST(item_protocol_v2_test, loot_roll_parse_valid)
{
    auto j = json{{"loot_id", "loot-abc-123"}, {"item_id", 500}};
    auto result = loot_roll_data::from_json(j);
    ASSERT_TRUE(result.is_ok());
    EXPECT_EQ(result.value().loot_id, "loot-abc-123");
    EXPECT_EQ(result.value().item_id, 500);
}

TEST(item_protocol_v2_test, loot_roll_parse_missing_loot_id)
{
    auto j = json{{"item_id", 500}};
    auto result = loot_roll_data::from_json(j);
    ASSERT_FALSE(result.is_ok());
}

TEST(item_protocol_v2_test, loot_roll_parse_missing_item_id)
{
    auto j = json{{"loot_id", "loot-abc-123"}};
    auto result = loot_roll_data::from_json(j);
    ASSERT_FALSE(result.is_ok());
}

// --- loot_pass parser ---

TEST(item_protocol_v2_test, loot_pass_parse_valid)
{
    auto j = json{{"loot_id", "loot-xyz"}, {"item_id", 777}};
    auto result = loot_pass_data::from_json(j);
    ASSERT_TRUE(result.is_ok());
    EXPECT_EQ(result.value().loot_id, "loot-xyz");
    EXPECT_EQ(result.value().item_id, 777);
}

TEST(item_protocol_v2_test, loot_pass_parse_missing_fields)
{
    auto j = json{{"item_id", 777}};
    auto result = loot_pass_data::from_json(j);
    ASSERT_FALSE(result.is_ok());

    j = json{{"loot_id", "abc"}};
    result = loot_pass_data::from_json(j);
    ASSERT_FALSE(result.is_ok());
}

// --- loot_assign parser ---

TEST(item_protocol_v2_test, loot_assign_parse_valid)
{
    auto j = json{{"loot_id", "loot-42"}, {"item_id", 300}, {"target_entity_id", 9001}};
    auto result = loot_assign_data::from_json(j);
    ASSERT_TRUE(result.is_ok());
    EXPECT_EQ(result.value().loot_id, "loot-42");
    EXPECT_EQ(result.value().item_id, 300);
    EXPECT_EQ(result.value().target_entity_id, 9001);
}

TEST(item_protocol_v2_test, loot_assign_parse_missing_target)
{
    auto j = json{{"loot_id", "loot-42"}, {"item_id", 300}};
    auto result = loot_assign_data::from_json(j);
    ASSERT_FALSE(result.is_ok());
}

TEST(item_protocol_v2_test, loot_assign_parse_missing_loot_id)
{
    auto j = json{{"item_id", 300}, {"target_entity_id", 9001}};
    auto result = loot_assign_data::from_json(j);
    ASSERT_FALSE(result.is_ok());
}

TEST(item_protocol_v2_test, loot_assign_parse_missing_item_id)
{
    auto j = json{{"loot_id", "abc"}, {"target_entity_id", 9001}};
    auto result = loot_assign_data::from_json(j);
    ASSERT_FALSE(result.is_ok());
}

// --- make_loot_rule_changed builder ---

TEST(item_protocol_v2_test, make_loot_rule_changed_basic)
{
    auto msg = make_loot_rule_changed("master", 42);
    EXPECT_EQ(msg["type"], "loot_rule_changed");
    auto& data = msg["data"];
    EXPECT_EQ(data["rule"], "master");
    EXPECT_EQ(data["set_by"], 42);
}

// --- make_loot_available builder ---

TEST(item_protocol_v2_test, make_loot_available_basic)
{
    std::vector<json> items;
    items.push_back(json{{"item_id", 100}, {"name", "Sword"}});
    items.push_back(json{{"item_id", 200}, {"name", "Shield"}});

    auto msg = make_loot_available("loot-001", items, "aresden", 50, 75, "greed", 30000);
    EXPECT_EQ(msg["type"], "loot_available");
    auto& data = msg["data"];
    EXPECT_EQ(data["loot_id"], "loot-001");
    ASSERT_EQ(data["items"].size(), 2u);
    EXPECT_EQ(data["items"][0]["item_id"], 100);
    EXPECT_EQ(data["items"][1]["name"], "Shield");
    EXPECT_EQ(data["source_map"], "aresden");
    EXPECT_EQ(data["source_x"], 50);
    EXPECT_EQ(data["source_y"], 75);
    EXPECT_EQ(data["rule"], "greed");
    EXPECT_EQ(data["timeout_ms"], 30000);
}

TEST(item_protocol_v2_test, make_loot_available_empty_items)
{
    std::vector<json> items;
    auto msg = make_loot_available("loot-empty", items, "elvine", 10, 20, "disabled", 0);
    EXPECT_EQ(msg["type"], "loot_available");
    EXPECT_TRUE(msg["data"]["items"].is_array());
    EXPECT_EQ(msg["data"]["items"].size(), 0u);
}

// --- make_loot_roll_result builder ---

TEST(item_protocol_v2_test, make_loot_roll_result_basic)
{
    auto msg = make_loot_roll_result("loot-001", 500, 42, "TestPlayer", 87);
    EXPECT_EQ(msg["type"], "loot_roll_result");
    auto& data = msg["data"];
    EXPECT_EQ(data["loot_id"], "loot-001");
    EXPECT_EQ(data["item_id"], 500);
    EXPECT_EQ(data["entity_id"], 42);
    EXPECT_EQ(data["player_name"], "TestPlayer");
    EXPECT_EQ(data["roll"], 87);
}

// --- make_loot_awarded builder ---

TEST(item_protocol_v2_test, make_loot_awarded_basic)
{
    auto msg = make_loot_awarded("loot-001", 500, 42, "Winner");
    EXPECT_EQ(msg["type"], "loot_awarded");
    auto& data = msg["data"];
    EXPECT_EQ(data["loot_id"], "loot-001");
    EXPECT_EQ(data["item_id"], 500);
    EXPECT_EQ(data["winner_entity_id"], 42);
    EXPECT_EQ(data["winner_name"], "Winner");
}

// --- make_loot_expired builder ---

TEST(item_protocol_v2_test, make_loot_expired_basic)
{
    auto msg = make_loot_expired("loot-expired-001");
    EXPECT_EQ(msg["type"], "loot_expired");
    EXPECT_EQ(msg["data"]["loot_id"], "loot-expired-001");
}

// --- parse_message_type round-trip for all loot messages ---

TEST(item_protocol_v2_test, loot_message_type_roundtrip)
{
    struct test_case
    {
        std::string name;
        json_message_type type;
    };

    std::vector<test_case> cases = {
        {"set_loot_rule", json_message_type::set_loot_rule},
        {"loot_rule_changed", json_message_type::loot_rule_changed},
        {"loot_roll", json_message_type::loot_roll},
        {"loot_pass", json_message_type::loot_pass},
        {"loot_assign", json_message_type::loot_assign},
        {"loot_available", json_message_type::loot_available},
        {"loot_roll_result", json_message_type::loot_roll_result},
        {"loot_awarded", json_message_type::loot_awarded},
        {"loot_expired", json_message_type::loot_expired},
    };

    for (const auto& tc : cases)
    {
        EXPECT_EQ(parse_message_type(tc.name), tc.type) << "parse failed for: " << tc.name;
        EXPECT_EQ(to_string(tc.type), tc.name) << "to_string failed for: " << tc.name;
    }
}
