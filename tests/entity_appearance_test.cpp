// entity_appearance_test.cpp
// Tests for entity appearance data expansion

#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

#include "network/json_protocol.h"
#include "player/player.h"
#include "player/player_system.h"
#include "item/item_system.h"
#include "item/item.h"
#include "registry/item_registry.h"
#include "registry/item_template.h"
#include "effect/effect_system.h"
#include "bridge/handlers/entity_builders.h"
#include "npc/npc.h"
#include "core/subsystem.h"

namespace net = hb::network;
using namespace hb;

// ========== visible_entity_msg to_json Tests ==========

TEST(entity_appearance_test, player_spawn_includes_base_appearance)
{
    net::visible_entity_msg msg;
    msg.entity_id = 42;
    msg.type = "player";
    msg.name = "TestPlayer";
    msg.x = 100;
    msg.y = 200;
    msg.hp_percent = 85;
    msg.direction = 3;
    msg.faction = "aresden";
    msg.hostility = "friendly";
    msg.pk_status = "innocent";
    msg.combat_mode = true;
    msg.gender = 1;
    msg.skin_color = 2;
    msg.hair_style = 3;
    msg.hair_color = 7;
    msg.underwear_color = 4;
    msg.player_level = 50;

    auto j = msg.to_json();
    EXPECT_EQ(j["gender"], 1);
    EXPECT_EQ(j["skin_color"], 2);
    EXPECT_EQ(j["hair_style"], 3);
    EXPECT_EQ(j["hair_color"], 7);
    EXPECT_EQ(j["underwear_color"], 4);
    EXPECT_EQ(j["level"], 50);
}

TEST(entity_appearance_test, player_spawn_includes_equipment_visuals)
{
    net::visible_entity_msg msg;
    msg.entity_id = 42;
    msg.type = "player";
    msg.name = "TestPlayer";
    msg.x = 100;
    msg.y = 200;
    msg.hp_percent = 85;
    msg.direction = 3;
    msg.faction = "aresden";
    msg.hostility = "friendly";
    msg.pk_status = "innocent";

    msg.weapon_visual = {.appr = 9, .color = 2, .name = "LongSword+2", .rarity = "rare"};
    msg.body_visual = {.appr = 5, .color = 1, .name = "PlateArmor", .rarity = "uncommon"};
    msg.weapon_glow = 1;
    msg.weapon_speed = 7;

    auto j = msg.to_json();
    ASSERT_TRUE(j.contains("equipment"));
    EXPECT_EQ(j["equipment"]["weapon"]["appr"], 9);
    EXPECT_EQ(j["equipment"]["weapon"]["color"], 2);
    EXPECT_EQ(j["equipment"]["weapon"]["name"], "LongSword+2");
    EXPECT_EQ(j["equipment"]["weapon"]["rarity"], "rare");
    EXPECT_EQ(j["equipment"]["body"]["appr"], 5);
    EXPECT_EQ(j["equipment"]["body"]["color"], 1);
    EXPECT_EQ(j["weapon_glow"], 1);
    EXPECT_EQ(j["weapon_speed"], 7);
}

TEST(entity_appearance_test, empty_equipment_produces_zero_values)
{
    net::visible_entity_msg msg;
    msg.entity_id = 1;
    msg.type = "player";
    msg.name = "Naked";
    msg.x = 0;
    msg.y = 0;
    msg.hp_percent = 100;
    msg.direction = 0;
    msg.faction = "neutral";
    msg.hostility = "neutral";
    msg.pk_status = "innocent";

    auto j = msg.to_json();
    ASSERT_TRUE(j.contains("equipment"));
    EXPECT_EQ(j["equipment"]["weapon"]["appr"], 0);
    EXPECT_EQ(j["equipment"]["weapon"]["color"], 0);
    EXPECT_FALSE(j["equipment"]["weapon"].contains("name"));
    EXPECT_FALSE(j["equipment"]["weapon"].contains("rarity"));
    EXPECT_EQ(j["weapon_glow"], 0);
    EXPECT_EQ(j["shield_glow"], 0);
    EXPECT_EQ(j["weapon_speed"], 0);
}

TEST(entity_appearance_test, status_effects_array)
{
    net::visible_entity_msg msg;
    msg.entity_id = 1;
    msg.type = "player";
    msg.name = "Buffed";
    msg.x = 0;
    msg.y = 0;
    msg.hp_percent = 100;
    msg.direction = 0;
    msg.faction = "elvine";
    msg.hostility = "friendly";
    msg.pk_status = "innocent";
    msg.status_effects = {"poisoned", "berserk"};

    auto j = msg.to_json();
    ASSERT_TRUE(j.contains("status_effects"));
    EXPECT_EQ(j["status_effects"].size(), 2);
    EXPECT_EQ(j["status_effects"][0], "poisoned");
    EXPECT_EQ(j["status_effects"][1], "berserk");
}

TEST(entity_appearance_test, empty_status_effects_omitted)
{
    net::visible_entity_msg msg;
    msg.entity_id = 1;
    msg.type = "player";
    msg.name = "Clean";
    msg.x = 0;
    msg.y = 0;
    msg.hp_percent = 100;
    msg.direction = 0;
    msg.faction = "neutral";
    msg.hostility = "neutral";
    msg.pk_status = "innocent";

    auto j = msg.to_json();
    EXPECT_FALSE(j.contains("status_effects"));
}

TEST(entity_appearance_test, active_buffs_serialized)
{
    net::visible_entity_msg msg;
    msg.entity_id = 1;
    msg.type = "player";
    msg.name = "Mage";
    msg.x = 0;
    msg.y = 0;
    msg.hp_percent = 100;
    msg.direction = 0;
    msg.faction = "elvine";
    msg.hostility = "friendly";
    msg.pk_status = "innocent";
    msg.active_buffs = {{.type = "buff_defense", .spell_id = 42, .magnitude = 20, .remaining_ms = 30000},
                        {.type = "poison", .spell_id = 15, .magnitude = 5, .remaining_ms = 8000}};

    auto j = msg.to_json();
    ASSERT_TRUE(j.contains("active_buffs"));
    EXPECT_EQ(j["active_buffs"].size(), 2);
    EXPECT_EQ(j["active_buffs"][0]["type"], "buff_defense");
    EXPECT_EQ(j["active_buffs"][0]["spell_id"], 42);
    EXPECT_EQ(j["active_buffs"][0]["magnitude"], 20);
    EXPECT_EQ(j["active_buffs"][0]["remaining_ms"], 30000);
    EXPECT_EQ(j["active_buffs"][1]["type"], "poison");
}

TEST(entity_appearance_test, empty_buffs_omitted)
{
    net::visible_entity_msg msg;
    msg.entity_id = 1;
    msg.type = "player";
    msg.name = "NoBuff";
    msg.x = 0;
    msg.y = 0;
    msg.hp_percent = 100;
    msg.direction = 0;
    msg.faction = "neutral";
    msg.hostility = "neutral";
    msg.pk_status = "innocent";

    auto j = msg.to_json();
    EXPECT_FALSE(j.contains("active_buffs"));
}

TEST(entity_appearance_test, npc_spawn_has_no_appearance_fields)
{
    net::visible_entity_msg msg;
    msg.entity_id = 5000;
    msg.type = "npc";
    msg.name = "Slime";
    msg.x = 50;
    msg.y = 60;
    msg.hp_percent = 100;
    msg.direction = 2;
    msg.hostility = "hostile";
    msg.template_id = 10;
    msg.sprite_id = 10;
    msg.level = 5;
    msg.category = "monster";

    auto j = msg.to_json();
    EXPECT_FALSE(j.contains("gender"));
    EXPECT_FALSE(j.contains("equipment"));
    EXPECT_FALSE(j.contains("weapon_glow"));
    EXPECT_FALSE(j.contains("status_effects"));
    EXPECT_FALSE(j.contains("active_buffs"));
    EXPECT_EQ(j["template_id"], 10);
    EXPECT_EQ(j["category"], "monster");
}

// ========== build_player_spawn Tests ==========

TEST(entity_appearance_test, build_player_spawn_base_fields)
{
    player::player plr;
    plr.ecs_entity = entity::entity{42};
    plr.name = "TestPlayer";
    plr.pos = world::position{100, 200};
    plr.hp = 85;
    plr.computed.max_hp = 100;
    plr.facing = world::direction::east;
    plr.faction = faction::aresden;
    plr.pk = {};
    plr.combat_mode = true;
    plr.sex = player::gender::male;
    plr.skin_color = 2;
    plr.hair_style = 3;
    plr.hair_color = 7;
    plr.underwear_color = 4;
    plr.experience.level = 50;

    auto msg = bridge::build_player_spawn(plr, "friendly", nullptr, nullptr, nullptr);

    EXPECT_EQ(msg.entity_id, 42u);
    EXPECT_EQ(msg.type, "player");
    EXPECT_EQ(msg.name, "TestPlayer");
    EXPECT_EQ(msg.x, 100);
    EXPECT_EQ(msg.y, 200);
    EXPECT_EQ(msg.hp_percent, 85);
    EXPECT_EQ(msg.direction, static_cast<int16_t>(world::direction::east));
    EXPECT_EQ(msg.faction, "aresden");
    EXPECT_EQ(msg.hostility, "friendly");
    EXPECT_EQ(msg.pk_status, "innocent");
    EXPECT_EQ(msg.combat_mode, true);
    EXPECT_EQ(msg.gender, 1);
    EXPECT_EQ(msg.skin_color, 2);
    EXPECT_EQ(msg.hair_style, 3);
    EXPECT_EQ(msg.hair_color, 7);
    EXPECT_EQ(msg.underwear_color, 4);
    EXPECT_EQ(msg.player_level, 50);
}

TEST(entity_appearance_test, build_player_spawn_cached_appearance)
{
    player::player plr;
    plr.ecs_entity = entity::entity{1};
    plr.name = "Equipped";
    plr.pos = world::position{10, 20};
    plr.hp = 100;
    plr.computed.max_hp = 100;
    plr.facing = world::direction::south;
    plr.faction = faction::elvine;

    plr.appearance.weapon = {.appr = 9, .color = 2};
    plr.appearance.shield = {.appr = 3, .color = 0};
    plr.appearance.body = {.appr = 5, .color = 1};
    plr.appearance.weapon_glow = 1;
    plr.appearance.weapon_speed = 7;

    auto msg = bridge::build_player_spawn(plr, "neutral", nullptr, nullptr, nullptr);

    EXPECT_EQ(msg.weapon_visual.appr, 9);
    EXPECT_EQ(msg.weapon_visual.color, 2);
    EXPECT_EQ(msg.shield_visual.appr, 3);
    EXPECT_EQ(msg.body_visual.appr, 5);
    EXPECT_EQ(msg.body_visual.color, 1);
    EXPECT_EQ(msg.weapon_glow, 1);
    EXPECT_EQ(msg.weapon_speed, 7);
}

TEST(entity_appearance_test, build_player_spawn_status_flags)
{
    player::player plr;
    plr.ecs_entity = entity::entity{1};
    plr.name = "StatusTest";
    plr.pos = world::position{10, 20};
    plr.hp = 100;
    plr.computed.max_hp = 100;
    plr.facing = world::direction::south;
    plr.faction = faction::neutral;
    plr.status = player::player_status::poisoned | player::player_status::berserk;

    auto msg = bridge::build_player_spawn(plr, "neutral", nullptr, nullptr, nullptr);

    ASSERT_EQ(msg.status_effects.size(), 2);
    EXPECT_EQ(msg.status_effects[0], "poisoned");
    EXPECT_EQ(msg.status_effects[1], "berserk");
}

TEST(entity_appearance_test, build_player_spawn_no_status)
{
    player::player plr;
    plr.ecs_entity = entity::entity{1};
    plr.name = "Clean";
    plr.pos = world::position{10, 20};
    plr.hp = 100;
    plr.computed.max_hp = 100;
    plr.facing = world::direction::south;
    plr.faction = faction::neutral;
    plr.status = player::player_status::none;

    auto msg = bridge::build_player_spawn(plr, "neutral", nullptr, nullptr, nullptr);
    EXPECT_TRUE(msg.status_effects.empty());
}

// ========== build_npc_spawn Tests ==========

TEST(entity_appearance_test, build_npc_spawn_all_fields)
{
    hb::npc::npc n;
    n.entity_id = entity::entity{5000};
    n.name = "Slime";
    n.pos = world::position{50, 60};
    n.hp = 80;
    n.max_hp = 100;
    n.facing = world::direction::north;
    n.template_id = npc_id(static_cast<uint16_t>(10));
    n.sprite_id = 10;
    n.level = 5;
    n.category = hb::npc::npc_category::monster;

    auto msg = bridge::build_npc_spawn(n, "hostile");

    EXPECT_EQ(msg.entity_id, 5000u);
    EXPECT_EQ(msg.type, "npc");
    EXPECT_EQ(msg.name, "Slime");
    EXPECT_EQ(msg.x, 50);
    EXPECT_EQ(msg.y, 60);
    EXPECT_EQ(msg.hp_percent, 80);
    EXPECT_EQ(msg.direction, static_cast<int16_t>(world::direction::north));
    EXPECT_EQ(msg.hostility, "hostile");
    EXPECT_EQ(msg.template_id, 10u);
    EXPECT_EQ(msg.sprite_id, 10);
    EXPECT_EQ(msg.level, 5);
    EXPECT_EQ(msg.category, "monster");
}

TEST(entity_appearance_test, build_npc_spawn_zero_max_hp)
{
    hb::npc::npc n;
    n.entity_id = entity::entity{100};
    n.name = "ZeroHP";
    n.pos = world::position{1, 2};
    n.hp = 0;
    n.max_hp = 0;
    n.facing = world::direction::south;
    n.template_id = npc_id(static_cast<uint16_t>(1));
    n.sprite_id = 1;
    n.level = 1;
    n.category = hb::npc::npc_category::merchant;

    auto msg = bridge::build_npc_spawn(n, "friendly");
    EXPECT_EQ(msg.hp_percent, 100); // Fallback when max_hp is 0
}

// ========== item_template appearance fields ==========

TEST(entity_appearance_test, item_template_has_appearance_fields)
{
    item_template tmpl;
    tmpl.appr_value = 5;
    tmpl.item_color = 3;
    tmpl.speed = 7;

    EXPECT_EQ(tmpl.appr_value, 5);
    EXPECT_EQ(tmpl.item_color, 3);
    EXPECT_EQ(tmpl.speed, 7);
}

TEST(entity_appearance_test, item_template_appearance_defaults)
{
    item_template tmpl;
    EXPECT_EQ(tmpl.appr_value, 0);
    EXPECT_EQ(tmpl.item_color, 0);
    EXPECT_EQ(tmpl.speed, 0);
}

// ========== appearance_state ==========

TEST(entity_appearance_test, appearance_state_defaults_to_zero)
{
    player::appearance_state appr;
    EXPECT_EQ(appr.weapon.appr, 0);
    EXPECT_EQ(appr.weapon.color, 0);
    EXPECT_EQ(appr.shield.appr, 0);
    EXPECT_EQ(appr.body.appr, 0);
    EXPECT_EQ(appr.pants.appr, 0);
    EXPECT_EQ(appr.head.appr, 0);
    EXPECT_EQ(appr.arms.appr, 0);
    EXPECT_EQ(appr.boots.appr, 0);
    EXPECT_EQ(appr.cape.appr, 0);
    EXPECT_EQ(appr.weapon_glow, 0);
    EXPECT_EQ(appr.shield_glow, 0);
    EXPECT_EQ(appr.weapon_speed, 0);
}

// ========== Full round-trip: to_json with all player fields ==========

TEST(entity_appearance_test, full_player_spawn_json_round_trip)
{
    net::visible_entity_msg msg;
    msg.entity_id = 42;
    msg.type = "player";
    msg.name = "FullTest";
    msg.x = 100;
    msg.y = 200;
    msg.hp_percent = 85;
    msg.direction = 3;
    msg.faction = "aresden";
    msg.hostility = "friendly";
    msg.pk_status = "innocent";
    msg.guild_name = "Warriors";
    msg.guild_tag = "WAR";
    msg.combat_mode = true;
    msg.gender = 1;
    msg.skin_color = 1;
    msg.hair_style = 3;
    msg.hair_color = 7;
    msg.underwear_color = 2;
    msg.player_level = 50;
    msg.weapon_visual = {.appr = 9, .color = 2, .name = "LongSword+2", .rarity = "rare"};
    msg.shield_visual = {.appr = 3, .color = 0, .name = "TowerShield", .rarity = "common"};
    msg.body_visual = {.appr = 5, .color = 1, .name = "PlateArmor", .rarity = "uncommon"};
    msg.weapon_glow = 1;
    msg.weapon_speed = 7;
    msg.status_effects = {"poisoned", "protection"};
    msg.active_buffs = {{.type = "buff_defense", .spell_id = 42, .magnitude = 20, .remaining_ms = 30000}};

    auto j = msg.to_json();

    // Verify all top-level fields
    EXPECT_EQ(j["entity_id"], 42);
    EXPECT_EQ(j["type"], "player");
    EXPECT_EQ(j["name"], "FullTest");
    EXPECT_EQ(j["faction"], "aresden");
    EXPECT_EQ(j["hostility"], "friendly");
    EXPECT_EQ(j["guild_name"], "Warriors");
    EXPECT_EQ(j["guild_tag"], "WAR");
    EXPECT_EQ(j["combat_mode"], true);
    EXPECT_EQ(j["gender"], 1);
    EXPECT_EQ(j["level"], 50);

    // Equipment
    EXPECT_EQ(j["equipment"]["weapon"]["appr"], 9);
    EXPECT_EQ(j["equipment"]["weapon"]["name"], "LongSword+2");
    EXPECT_EQ(j["equipment"]["shield"]["appr"], 3);
    EXPECT_EQ(j["equipment"]["body"]["rarity"], "uncommon");

    // Glows
    EXPECT_EQ(j["weapon_glow"], 1);
    EXPECT_EQ(j["weapon_speed"], 7);

    // Status
    EXPECT_EQ(j["status_effects"].size(), 2);

    // Buffs
    EXPECT_EQ(j["active_buffs"].size(), 1);
    EXPECT_EQ(j["active_buffs"][0]["type"], "buff_defense");
}
