// test_player.cpp
// Unit tests for player system

#include <gtest/gtest.h>
#include "core/types.h"
#include "player/stats.h"
#include "player/experience.h"
#include "player/equipment.h"
#include "player/player.h"
#include "player/player_system.h"
#include "world/position.h"

using hb::item_id;
using namespace hb::player;

// Stats tests

TEST(base_stats_test, default_values)
{
    base_stats stats;
    EXPECT_EQ(stats.strength, 10);
    EXPECT_EQ(stats.dexterity, 10);
    EXPECT_EQ(stats.vitality, 10);
    EXPECT_EQ(stats.intelligence, 10);
    EXPECT_EQ(stats.magic, 10);
    EXPECT_EQ(stats.charisma, 10);
}

TEST(base_stats_test, derived_values)
{
    base_stats stats;
    stats.strength = 20;
    stats.vitality = 30;
    stats.dexterity = 15;
    stats.level_bonus = 10;

    EXPECT_GT(stats.max_hp(), 0);
    EXPECT_GT(stats.physical_attack(), 0);
    EXPECT_GT(stats.hit_rate(), 0);
}

TEST(stat_modifiers_test, addition)
{
    stat_modifiers a;
    a.strength = 5;
    a.attack_power = 10;

    stat_modifiers b;
    b.strength = 3;
    b.defense = 15;

    auto result = a + b;
    EXPECT_EQ(result.strength, 8);
    EXPECT_EQ(result.attack_power, 10);
    EXPECT_EQ(result.defense, 15);
}

TEST(computed_stats_test, compute)
{
    base_stats base;
    base.strength = 20;
    base.vitality = 25;

    stat_modifiers mods;
    mods.attack_power = 50;
    mods.defense = 100;

    computed_stats computed;
    computed.compute(base, mods);

    EXPECT_EQ(computed.strength, 20);
    EXPECT_EQ(computed.vitality, 25);
    EXPECT_GT(computed.max_hp, 0);
    EXPECT_GT(computed.attack_power, 0);
    EXPECT_EQ(computed.defense, 100);
}

// Experience tests

TEST(experience_table_test, level_requirements)
{
    EXPECT_EQ(exp_table.exp_for_level(1), 0);
    EXPECT_GT(exp_table.exp_for_level(2), 0);
    EXPECT_GT(exp_table.exp_for_level(10), exp_table.exp_for_level(5));
    EXPECT_GT(exp_table.exp_for_level(100), exp_table.exp_for_level(50));
}

TEST(experience_state_test, add_experience)
{
    experience_state exp;
    exp.level = 1;
    exp.experience = 0;

    int levels_gained = exp.add_experience(exp_table.exp_for_level(3));
    EXPECT_GE(levels_gained, 1);
    EXPECT_GE(exp.level, 2);
}

TEST(experience_state_test, max_level_cap)
{
    experience_state exp;
    exp.level = max_level;
    exp.experience = 999999999;

    int levels_gained = exp.add_experience(999999999);
    EXPECT_EQ(levels_gained, 0);
    EXPECT_EQ(exp.level, max_level);
}

TEST(stat_points_test, allocation)
{
    stat_points points;
    points.available = 10;

    EXPECT_TRUE(points.allocate(5));
    EXPECT_EQ(points.available, 5);
    EXPECT_EQ(points.used, 5);

    EXPECT_FALSE(points.allocate(10)); // Not enough
    EXPECT_EQ(points.available, 5);
}

// Equipment tests

TEST(equipment_state_test, equip_unequip)
{
    equipment_state equip;

    EXPECT_FALSE(equip.has_equipped(equip_slot::weapon));

    equip.equip(equip_slot::weapon, item_id{100}, 50, 100);
    EXPECT_TRUE(equip.has_equipped(equip_slot::weapon));
    EXPECT_EQ(equip.weapon().id.value, 100);
    EXPECT_EQ(equip.weapon().durability, 50);

    auto item = equip.unequip(equip_slot::weapon);
    EXPECT_FALSE(equip.has_equipped(equip_slot::weapon));
    EXPECT_EQ(item.id.value, 100);
}

TEST(equipped_item_test, durability)
{
    equipped_item item;
    item.id = item_id{1};
    item.durability = 50;
    item.max_durability = 100;

    EXPECT_FLOAT_EQ(item.durability_percent(), 0.5f);
}

// Player component tests

TEST(player_test, status_flags)
{
    player p;
    EXPECT_FALSE(p.has_status(player_status::poisoned));

    p.add_status(player_status::poisoned);
    EXPECT_TRUE(p.has_status(player_status::poisoned));

    p.remove_status(player_status::poisoned);
    EXPECT_FALSE(p.has_status(player_status::poisoned));
}

TEST(player_test, resources)
{
    player p;
    p.computed.max_hp = 100;
    p.computed.max_mp = 50;
    p.hp = 100;
    p.mp = 50;

    p.damage_hp(30);
    EXPECT_EQ(p.hp, 70);
    EXPECT_TRUE(p.is_alive());

    p.heal_hp(50);
    EXPECT_EQ(p.hp, 100); // Capped at max

    p.damage_hp(200);
    EXPECT_EQ(p.hp, 0);
    EXPECT_TRUE(p.is_dead());
}

TEST(player_test, mana_spending)
{
    player p;
    p.computed.max_mp = 100;
    p.mp = 50;

    EXPECT_TRUE(p.spend_mp(30));
    EXPECT_EQ(p.mp, 20);

    EXPECT_FALSE(p.spend_mp(30)); // Not enough
    EXPECT_EQ(p.mp, 20);
}

TEST(hunger_state_test, decay_and_consume)
{
    hunger_state hunger;
    hunger.level = 100;

    EXPECT_TRUE(hunger.is_full());

    hunger.decay(30);
    EXPECT_EQ(hunger.level, 70);
    EXPECT_FALSE(hunger.is_hungry());

    hunger.decay(50);
    EXPECT_TRUE(hunger.is_hungry());

    hunger.consume(100);
    EXPECT_EQ(hunger.level, 100);
}

TEST(pk_state_test, kill_tracking)
{
    pk_state pk;
    EXPECT_TRUE(pk.is_innocent());

    pk.add_kill();
    EXPECT_EQ(pk.count, 1);
    EXPECT_EQ(pk.points, 50);
    EXPECT_TRUE(pk.is_criminal());

    pk.add_kill();
    EXPECT_TRUE(pk.is_murderer());

    pk.decay_points(80);
    // 100 - 80 = 20 points, which is innocent (< 30)
    EXPECT_TRUE(pk.is_innocent());
}

// Player system tests

class player_system_test : public ::testing::Test
{
protected:
    void SetUp() override { system_.initialize(); }

    void TearDown() override { system_.shutdown(); }

    player_system system_;
};

TEST_F(player_system_test, lifecycle)
{
    EXPECT_TRUE(system_.is_initialized());
    EXPECT_EQ(system_.name(), "player_system");
}

TEST_F(player_system_test, create_player)
{
    player_create_info info;
    info.name = "TestPlayer";
    info.account_name = "testaccount";
    info.sex = gender::male;
    info.profession = player_class::warrior;

    auto result = system_.create_player(info);
    ASSERT_TRUE(result.is_ok());

    auto id = result.value();
    EXPECT_TRUE(id.is_valid());
    EXPECT_EQ(system_.player_count(), 1);

    auto* p = system_.get_player(id);
    ASSERT_NE(p, nullptr);
    EXPECT_EQ(p->name, "TestPlayer");
}

TEST_F(player_system_test, get_by_name)
{
    player_create_info info;
    info.name = "UniquePlayer";

    auto result = system_.create_player(info);
    ASSERT_TRUE(result.is_ok());

    auto* p = system_.get_player_by_name("UniquePlayer");
    ASSERT_NE(p, nullptr);
    EXPECT_EQ(p->name, "UniquePlayer");

    EXPECT_EQ(system_.get_player_by_name("Nonexistent"), nullptr);
}

TEST_F(player_system_test, remove_player)
{
    player_create_info info;
    info.name = "ToRemove";

    auto result = system_.create_player(info);
    auto id = result.value();

    EXPECT_EQ(system_.player_count(), 1);
    system_.remove_player(id);
    EXPECT_EQ(system_.player_count(), 0);
    EXPECT_EQ(system_.get_player(id), nullptr);
}

TEST_F(player_system_test, duplicate_name_fails)
{
    player_create_info info;
    info.name = "DuplicateName";

    auto result1 = system_.create_player(info);
    EXPECT_TRUE(result1.is_ok());

    auto result2 = system_.create_player(info);
    EXPECT_TRUE(result2.is_err());
}

TEST_F(player_system_test, add_experience)
{
    player_create_info info;
    info.name = "ExpPlayer";

    auto result = system_.create_player(info);
    auto id = result.value();

    auto* p = system_.get_player(id);
    EXPECT_EQ(p->experience.level, 1);

    system_.add_experience(id, exp_table.exp_for_level(5));
    EXPECT_GT(p->experience.level, 1);
}

TEST_F(player_system_test, status_effects)
{
    player_create_info info;
    info.name = "StatusPlayer";

    auto result = system_.create_player(info);
    auto id = result.value();

    auto* p = system_.get_player(id);
    EXPECT_FALSE(p->has_status(player_status::poisoned));

    system_.add_status(id, player_status::poisoned);
    EXPECT_TRUE(p->has_status(player_status::poisoned));

    system_.remove_status(id, player_status::poisoned);
    EXPECT_FALSE(p->has_status(player_status::poisoned));
}

// Movement tests

TEST_F(player_system_test, can_move_to_invalid_player)
{
    // Non-existent player should return invalid_player
    using move_result = player_system::move_result;
    auto result = system_.can_move_to(hb::player_id{9999}, hb::world::position{10, 10});
    EXPECT_EQ(result, move_result::invalid_player);
}

TEST_F(player_system_test, can_move_to_dead_player)
{
    player_create_info info;
    info.name = "DeadPlayer";
    auto result = system_.create_player(info);
    auto id = result.value();

    auto* p = system_.get_player(id);
    p->hp = 0; // Make player dead

    using move_result = player_system::move_result;
    auto move_check = system_.can_move_to(id, hb::world::position{10, 10});
    EXPECT_EQ(move_check, move_result::blocked_dead);
}

TEST_F(player_system_test, can_move_to_paralyzed_player)
{
    player_create_info info;
    info.name = "ParalyzedPlayer";
    auto result = system_.create_player(info);
    auto id = result.value();

    system_.add_status(id, player_status::paralyzed);

    using move_result = player_system::move_result;
    auto move_check = system_.can_move_to(id, hb::world::position{10, 10});
    EXPECT_EQ(move_check, move_result::blocked_status);
}

TEST_F(player_system_test, can_move_to_frozen_player)
{
    player_create_info info;
    info.name = "FrozenPlayer";
    auto result = system_.create_player(info);
    auto id = result.value();

    system_.add_status(id, player_status::frozen);

    using move_result = player_system::move_result;
    auto move_check = system_.can_move_to(id, hb::world::position{10, 10});
    EXPECT_EQ(move_check, move_result::blocked_status);
}

TEST_F(player_system_test, can_move_to_stunned_player)
{
    player_create_info info;
    info.name = "StunnedPlayer";
    auto result = system_.create_player(info);
    auto id = result.value();

    system_.add_status(id, player_status::stunned);

    using move_result = player_system::move_result;
    auto move_check = system_.can_move_to(id, hb::world::position{10, 10});
    EXPECT_EQ(move_check, move_result::blocked_status);
}

TEST_F(player_system_test, try_move_without_world_succeeds)
{
    // Without world system registered, movement should succeed (basic mode)
    player_create_info info;
    info.name = "MovePlayer";
    auto result = system_.create_player(info);
    auto id = result.value();

    auto* p = system_.get_player(id);
    p->hp = 100;
    p->computed.max_hp = 100;

    // Try to move without world system
    auto move_info = system_.try_move(id, hb::world::position{50, 50}, hb::world::direction::east);

    using move_result = player_system::move_result;
    EXPECT_EQ(move_info.result, move_result::success);
    EXPECT_EQ(p->pos.x, 50);
    EXPECT_EQ(p->pos.y, 50);
    EXPECT_EQ(p->facing, hb::world::direction::east);
}

TEST_F(player_system_test, try_move_dead_player_fails)
{
    player_create_info info;
    info.name = "DeadMovePlayer";
    auto result = system_.create_player(info);
    auto id = result.value();

    auto* p = system_.get_player(id);
    p->hp = 0; // Dead

    auto move_info = system_.try_move(id, hb::world::position{50, 50}, hb::world::direction::east);

    using move_result = player_system::move_result;
    EXPECT_EQ(move_info.result, move_result::blocked_dead);
}

TEST_F(player_system_test, get_player_at_returns_nullopt_without_world)
{
    // Without world system, should return nullopt
    auto result = system_.get_player_at(hb::map_id{1}, hb::world::position{10, 10});
    EXPECT_FALSE(result.has_value());
}

TEST_F(player_system_test, get_players_in_range_returns_empty_without_world)
{
    player_create_info info;
    info.name = "RangePlayer";
    auto result = system_.create_player(info);
    auto id = result.value();

    auto players = system_.get_players_in_range(id, 10);
    EXPECT_TRUE(players.empty());
}

// Damage and heal tests

TEST_F(player_system_test, apply_damage)
{
    player_create_info info;
    info.name = "DamagePlayer";
    auto result = system_.create_player(info);
    auto id = result.value();

    auto* p = system_.get_player(id);
    int32_t initial_hp = p->hp;
    ASSERT_GT(initial_hp, 0);

    system_.apply_damage(id, 10);
    EXPECT_EQ(p->hp, initial_hp - 10);
    EXPECT_TRUE(p->is_alive());
}

TEST_F(player_system_test, apply_damage_kills)
{
    player_create_info info;
    info.name = "KillPlayer";
    auto result = system_.create_player(info);
    auto id = result.value();

    auto* p = system_.get_player(id);
    system_.apply_damage(id, p->hp + 100);
    EXPECT_EQ(p->hp, 0);
    EXPECT_TRUE(p->is_dead());
}

TEST_F(player_system_test, apply_damage_to_dead_player)
{
    player_create_info info;
    info.name = "AlreadyDead";
    auto result = system_.create_player(info);
    auto id = result.value();

    auto* p = system_.get_player(id);
    p->hp = 0; // Already dead

    // Should not crash or change state
    system_.apply_damage(id, 50);
    EXPECT_EQ(p->hp, 0);
}

TEST_F(player_system_test, apply_heal)
{
    player_create_info info;
    info.name = "HealPlayer";
    auto result = system_.create_player(info);
    auto id = result.value();

    auto* p = system_.get_player(id);
    int32_t max_hp = p->computed.max_hp;
    p->hp = max_hp / 2;

    system_.apply_heal(id, 10);
    EXPECT_EQ(p->hp, max_hp / 2 + 10);
}

TEST_F(player_system_test, apply_heal_capped_at_max)
{
    player_create_info info;
    info.name = "MaxHealPlayer";
    auto result = system_.create_player(info);
    auto id = result.value();

    auto* p = system_.get_player(id);
    p->hp = p->computed.max_hp - 5;

    system_.apply_heal(id, 100);
    EXPECT_EQ(p->hp, p->computed.max_hp);
}

TEST_F(player_system_test, apply_heal_dead_player_noop)
{
    player_create_info info;
    info.name = "DeadHealPlayer";
    auto result = system_.create_player(info);
    auto id = result.value();

    auto* p = system_.get_player(id);
    p->hp = 0;

    system_.apply_heal(id, 50);
    EXPECT_EQ(p->hp, 0); // Dead players can't be healed
}

// Equipment tests

TEST_F(player_system_test, equip_and_unequip_item)
{
    player_create_info info;
    info.name = "EquipPlayer";
    auto result = system_.create_player(info);
    auto id = result.value();

    auto* p = system_.get_player(id);
    EXPECT_FALSE(p->equipment.has_equipped(equip_slot::weapon));

    system_.equip_item(id, equip_slot::weapon, hb::item_id{42}, 80, 100);
    EXPECT_TRUE(p->equipment.has_equipped(equip_slot::weapon));
    EXPECT_EQ(p->equipment.weapon().id.value, 42);

    auto unequipped = system_.unequip_item(id, equip_slot::weapon);
    EXPECT_EQ(unequipped.id.value, 42);
    EXPECT_EQ(unequipped.durability, 80);
    EXPECT_FALSE(p->equipment.has_equipped(equip_slot::weapon));
}

TEST_F(player_system_test, unequip_empty_slot)
{
    player_create_info info;
    info.name = "EmptySlotPlayer";
    auto result = system_.create_player(info);
    auto id = result.value();

    auto unequipped = system_.unequip_item(id, equip_slot::shield);
    EXPECT_FALSE(unequipped.id.is_valid());
}

// Binding tests

TEST_F(player_system_test, bind_connection)
{
    player_create_info info;
    info.name = "BindConnPlayer";
    auto result = system_.create_player(info);
    auto id = result.value();

    auto conn = hb::connection_id{100};
    system_.bind_connection(id, conn);

    auto* p = system_.get_player_by_connection(conn);
    ASSERT_NE(p, nullptr);
    EXPECT_EQ(p->name, "BindConnPlayer");
}

TEST_F(player_system_test, unbind_connection)
{
    player_create_info info;
    info.name = "UnbindConnPlayer";
    auto result = system_.create_player(info);
    auto id = result.value();

    auto conn = hb::connection_id{101};
    system_.bind_connection(id, conn);
    EXPECT_NE(system_.get_player_by_connection(conn), nullptr);

    system_.unbind_connection(id);
    EXPECT_EQ(system_.get_player_by_connection(conn), nullptr);
}

TEST_F(player_system_test, bind_session)
{
    player_create_info info;
    info.name = "BindSessPlayer";
    auto result = system_.create_player(info);
    auto id = result.value();

    auto sess = hb::session_id{200};
    system_.bind_session(id, sess);

    auto* p = system_.get_player_by_session(sess);
    ASSERT_NE(p, nullptr);
    EXPECT_EQ(p->name, "BindSessPlayer");
}

TEST_F(player_system_test, unbind_session)
{
    player_create_info info;
    info.name = "UnbindSessPlayer";
    auto result = system_.create_player(info);
    auto id = result.value();

    auto sess = hb::session_id{201};
    system_.bind_session(id, sess);

    system_.unbind_session(id);
    EXPECT_EQ(system_.get_player_by_session(sess), nullptr);
}

TEST_F(player_system_test, rebind_connection)
{
    player_create_info info;
    info.name = "RebindPlayer";
    auto result = system_.create_player(info);
    auto id = result.value();

    system_.bind_connection(id, hb::connection_id{10});
    system_.bind_connection(id, hb::connection_id{20});

    // Old connection should no longer resolve
    EXPECT_EQ(system_.get_player_by_connection(hb::connection_id{10}), nullptr);
    EXPECT_NE(system_.get_player_by_connection(hb::connection_id{20}), nullptr);
}

// Stat allocation tests

TEST_F(player_system_test, add_stat_point)
{
    player_create_info info;
    info.name = "StatPlayer";
    auto result = system_.create_player(info);
    auto id = result.value();

    auto* p = system_.get_player(id);
    p->stats_pts.available = 5;
    int16_t initial_str = p->base.strength;

    system_.add_stat_point(id, 0); // 0 = strength
    EXPECT_EQ(p->base.strength, initial_str + 1);
    EXPECT_EQ(p->stats_pts.available, 4);
}

TEST_F(player_system_test, add_stat_point_no_points)
{
    player_create_info info;
    info.name = "NoStatPlayer";
    auto result = system_.create_player(info);
    auto id = result.value();

    auto* p = system_.get_player(id);
    p->stats_pts.available = 0;
    int16_t initial_str = p->base.strength;

    system_.add_stat_point(id, 0);
    EXPECT_EQ(p->base.strength, initial_str); // Unchanged
}

TEST_F(player_system_test, add_stat_point_invalid_index)
{
    player_create_info info;
    info.name = "BadStatPlayer";
    auto result = system_.create_player(info);
    auto id = result.value();

    auto* p = system_.get_player(id);
    p->stats_pts.available = 5;

    system_.add_stat_point(id, 10);       // Invalid index
    EXPECT_EQ(p->stats_pts.available, 5); // Unchanged
}

// Status effect tests

TEST_F(player_system_test, clear_all_status)
{
    player_create_info info;
    info.name = "ClearStatusPlayer";
    auto result = system_.create_player(info);
    auto id = result.value();

    system_.add_status(id, player_status::poisoned);
    system_.add_status(id, player_status::frozen);

    auto* p = system_.get_player(id);
    EXPECT_TRUE(p->has_status(player_status::poisoned));
    EXPECT_TRUE(p->has_status(player_status::frozen));

    system_.clear_all_status(id);
    EXPECT_FALSE(p->has_status(player_status::poisoned));
    EXPECT_FALSE(p->has_status(player_status::frozen));
}

// Max player limit test

TEST_F(player_system_test, max_players_limit)
{
    player_system_config config;
    config.max_players = 2;
    system_.set_config(config);

    player_create_info info1;
    info1.name = "Player1";
    auto r1 = system_.create_player(info1);
    EXPECT_TRUE(r1.is_ok());

    player_create_info info2;
    info2.name = "Player2";
    auto r2 = system_.create_player(info2);
    EXPECT_TRUE(r2.is_ok());

    player_create_info info3;
    info3.name = "Player3";
    auto r3 = system_.create_player(info3);
    EXPECT_TRUE(r3.is_err());
}

// Hunger callback tests

TEST_F(player_system_test, restore_hunger)
{
    player_create_info info;
    info.name = "HungryPlayer";
    auto result = system_.create_player(info);
    auto id = result.value();

    auto* p = system_.get_player(id);
    p->hunger.level = 50;

    system_.restore_hunger(id, 20);
    EXPECT_EQ(p->hunger.level, 70);
}

TEST_F(player_system_test, hunger_callback_on_restore)
{
    player_create_info info;
    info.name = "CallbackHunger";
    auto result = system_.create_player(info);
    auto id = result.value();

    auto* p = system_.get_player(id);
    p->hunger.level = 50;

    bool callback_fired = false;
    int8_t cb_old = 0, cb_new = 0;

    system_.on_hunger_change(
        [&](hb::player_id, int8_t old_level, int8_t new_level)
        {
            callback_fired = true;
            cb_old = old_level;
            cb_new = new_level;
        });

    system_.restore_hunger(id, 30);
    EXPECT_TRUE(callback_fired);
    EXPECT_EQ(cb_old, 50);
    EXPECT_EQ(cb_new, 80);
}

// For_each and find tests

TEST_F(player_system_test, for_each_player)
{
    player_create_info info1;
    info1.name = "ForEach1";
    system_.create_player(info1);

    player_create_info info2;
    info2.name = "ForEach2";
    system_.create_player(info2);

    int count = 0;
    system_.for_each_player([&](hb::player_id, const player&) { ++count; });

    EXPECT_EQ(count, 2);
}

TEST_F(player_system_test, find_players_if)
{
    player_create_info info1;
    info1.name = "Warrior1";
    info1.profession = player_class::warrior;
    system_.create_player(info1);

    player_create_info info2;
    info2.name = "Mage1";
    info2.profession = player_class::mage;
    system_.create_player(info2);

    auto warriors = system_.find_players_if([](const player& p) { return p.profession == player_class::warrior; });

    EXPECT_EQ(warriors.size(), 1);
}

// Set position and facing tests

TEST_F(player_system_test, set_position)
{
    player_create_info info;
    info.name = "PosPlayer";
    auto result = system_.create_player(info);
    auto id = result.value();

    system_.set_position(id, hb::map_id{5}, hb::world::position{100, 200}, hb::world::direction::north);

    auto* p = system_.get_player(id);
    EXPECT_EQ(p->current_map.value, 5);
    EXPECT_EQ(p->pos.x, 100);
    EXPECT_EQ(p->pos.y, 200);
    EXPECT_EQ(p->facing, hb::world::direction::north);
}

TEST_F(player_system_test, set_facing)
{
    player_create_info info;
    info.name = "FacingPlayer";
    auto result = system_.create_player(info);
    auto id = result.value();

    system_.set_facing(id, hb::world::direction::west);
    EXPECT_EQ(system_.get_player(id)->facing, hb::world::direction::west);
}

// Combat target tests

TEST_F(player_system_test, set_and_clear_target)
{
    player_create_info info;
    info.name = "TargetPlayer";
    auto result = system_.create_player(info);
    auto id = result.value();

    auto* p = system_.get_player(id);
    system_.set_target(id, hb::entity::entity{42});
    EXPECT_EQ(p->target.id, 42);

    system_.clear_target(id);
    EXPECT_FALSE(p->target.is_valid());
}

// Get all players test

TEST_F(player_system_test, get_all_players)
{
    player_create_info info1;
    info1.name = "All1";
    system_.create_player(info1);

    player_create_info info2;
    info2.name = "All2";
    system_.create_player(info2);

    auto ids = system_.get_all_players();
    EXPECT_EQ(ids.size(), 2);
}

// Player exists test

TEST_F(player_system_test, player_exists)
{
    player_create_info info;
    info.name = "ExistsPlayer";
    auto result = system_.create_player(info);
    auto id = result.value();

    EXPECT_TRUE(system_.player_exists(id));
    EXPECT_FALSE(system_.player_exists(hb::player_id{9999}));
}
