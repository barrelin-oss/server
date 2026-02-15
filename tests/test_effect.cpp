// test_effect.cpp
// Unit tests for effect system

#include <gtest/gtest.h>
#include <thread>
#include <chrono>
#include "effect/active_effect.h"
#include "effect/effect_modifiers.h"
#include "effect/effect_system.h"
#include "player/player.h"
#include "player/stats.h"
#include "core/enums.h"
#include "registry/spell_template.h"

using namespace hb::effect;

using hb::magic_type;
using hb::spell_effect_type;
using hb::spell_id;
using hb::player::player_status;
using hb::player::stat_modifiers;

// ============================================================
// active_effect tests
// ============================================================

TEST(active_effect_test, effect_id_validity)
{
    effect_id invalid{};
    EXPECT_FALSE(invalid.is_valid());
    EXPECT_EQ(invalid.value, 0u);

    effect_id valid{42};
    EXPECT_TRUE(valid.is_valid());
    EXPECT_EQ(valid.value, 42u);
}

TEST(active_effect_test, effect_id_comparison)
{
    effect_id a{1};
    effect_id b{1};
    effect_id c{2};

    EXPECT_EQ(a, b);
    EXPECT_NE(a, c);
}

TEST(active_effect_test, default_construction)
{
    active_effect eff{};
    EXPECT_FALSE(eff.id.is_valid());
    EXPECT_FALSE(eff.source.is_valid());
    EXPECT_FALSE(eff.source_spell.has_value());
    EXPECT_EQ(eff.type, spell_effect_type::none);
    EXPECT_EQ(eff.magnitude, 0);
    EXPECT_EQ(eff.expires_at_ms, 0);
    EXPECT_EQ(eff.tick_interval_ms, 0);
    EXPECT_EQ(eff.status_flag, player_status::none);
}

// ============================================================
// compute_effect_modifiers tests
// ============================================================

TEST(effect_modifiers_test, empty_effects)
{
    std::vector<active_effect> effects;
    auto result = compute_effect_modifiers(effects);

    EXPECT_EQ(result.status, player_status::none);
    EXPECT_EQ(result.modifiers.attack_power, 0);
    EXPECT_EQ(result.modifiers.defense, 0);
    EXPECT_EQ(result.modifiers.move_speed, 0);
}

TEST(effect_modifiers_test, protection_adds_defense)
{
    active_effect eff{};
    eff.group = magic_type::protection;
    eff.magnitude = 25;
    eff.status_flag = player_status::protection;

    std::vector<active_effect> effects = {eff};
    auto result = compute_effect_modifiers(effects);

    EXPECT_EQ(result.modifiers.defense, 25);
    EXPECT_NE(result.status & player_status::protection, player_status::none);
}

TEST(effect_modifiers_test, berserk_modifies_attack_and_defense)
{
    active_effect eff{};
    eff.group = magic_type::berserk;
    eff.magnitude = 30;
    eff.status_flag = player_status::berserk;

    std::vector<active_effect> effects = {eff};
    auto result = compute_effect_modifiers(effects);

    EXPECT_EQ(result.modifiers.attack_power, 30);
    EXPECT_EQ(result.modifiers.defense, -30);
    EXPECT_NE(result.status & player_status::berserk, player_status::none);
}

TEST(effect_modifiers_test, buff_attack_adds_attack_power)
{
    active_effect eff{};
    eff.type = spell_effect_type::buff_attack;
    eff.magnitude = 20;
    eff.status_flag = player_status::attack_up;

    std::vector<active_effect> effects = {eff};
    auto result = compute_effect_modifiers(effects);

    EXPECT_EQ(result.modifiers.attack_power, 20);
    EXPECT_NE(result.status & player_status::attack_up, player_status::none);
}

TEST(effect_modifiers_test, buff_defense_adds_defense)
{
    active_effect eff{};
    eff.type = spell_effect_type::buff_defense;
    eff.magnitude = 15;
    eff.status_flag = player_status::defense_up;

    std::vector<active_effect> effects = {eff};
    auto result = compute_effect_modifiers(effects);

    EXPECT_EQ(result.modifiers.defense, 15);
    EXPECT_NE(result.status & player_status::defense_up, player_status::none);
}

TEST(effect_modifiers_test, buff_speed_adds_move_speed)
{
    active_effect eff{};
    eff.type = spell_effect_type::buff_speed;
    eff.magnitude = 10;
    eff.status_flag = player_status::haste;

    std::vector<active_effect> effects = {eff};
    auto result = compute_effect_modifiers(effects);

    EXPECT_EQ(result.modifiers.move_speed, 10);
    EXPECT_NE(result.status & player_status::haste, player_status::none);
}

TEST(effect_modifiers_test, debuff_slow_reduces_move_speed)
{
    active_effect eff{};
    eff.type = spell_effect_type::debuff_slow;
    eff.magnitude = 5;
    eff.status_flag = player_status::slow;

    std::vector<active_effect> effects = {eff};
    auto result = compute_effect_modifiers(effects);

    EXPECT_EQ(result.modifiers.move_speed, -5);
    EXPECT_NE(result.status & player_status::slow, player_status::none);
}

TEST(effect_modifiers_test, poison_only_sets_status)
{
    // Poison doesn't modify stats - it acts through periodic ticks
    active_effect eff{};
    eff.group = magic_type::poison;
    eff.type = spell_effect_type::poison;
    eff.magnitude = 10;
    eff.status_flag = player_status::poisoned;

    std::vector<active_effect> effects = {eff};
    auto result = compute_effect_modifiers(effects);

    EXPECT_NE(result.status & player_status::poisoned, player_status::none);
    // No stat changes from poison
    EXPECT_EQ(result.modifiers.attack_power, 0);
    EXPECT_EQ(result.modifiers.defense, 0);
}

TEST(effect_modifiers_test, stun_sets_status)
{
    active_effect eff{};
    eff.type = spell_effect_type::stun;
    eff.status_flag = player_status::stunned;

    std::vector<active_effect> effects = {eff};
    auto result = compute_effect_modifiers(effects);

    EXPECT_NE(result.status & player_status::stunned, player_status::none);
}

TEST(effect_modifiers_test, freeze_sets_status)
{
    active_effect eff{};
    eff.type = spell_effect_type::freeze;
    eff.status_flag = player_status::frozen;

    std::vector<active_effect> effects = {eff};
    auto result = compute_effect_modifiers(effects);

    EXPECT_NE(result.status & player_status::frozen, player_status::none);
}

TEST(effect_modifiers_test, multiple_effects_combine)
{
    active_effect prot{};
    prot.group = magic_type::protection;
    prot.magnitude = 20;
    prot.status_flag = player_status::protection;

    active_effect atk{};
    atk.type = spell_effect_type::buff_attack;
    atk.magnitude = 15;
    atk.status_flag = player_status::attack_up;

    std::vector<active_effect> effects = {prot, atk};
    auto result = compute_effect_modifiers(effects);

    EXPECT_EQ(result.modifiers.defense, 20);
    EXPECT_EQ(result.modifiers.attack_power, 15);
    EXPECT_NE(result.status & player_status::protection, player_status::none);
    EXPECT_NE(result.status & player_status::attack_up, player_status::none);
}

TEST(effect_modifiers_test, paralyzed_status_from_group)
{
    active_effect eff{};
    eff.group = magic_type::hold_paralyze;
    eff.status_flag = player_status::paralyzed;

    std::vector<active_effect> effects = {eff};
    auto result = compute_effect_modifiers(effects);

    EXPECT_NE(result.status & player_status::paralyzed, player_status::none);
}

TEST(effect_modifiers_test, silenced_status_from_group)
{
    active_effect eff{};
    eff.group = magic_type::inhibition;
    eff.status_flag = player_status::silenced;

    std::vector<active_effect> effects = {eff};
    auto result = compute_effect_modifiers(effects);

    EXPECT_NE(result.status & player_status::silenced, player_status::none);
}

TEST(effect_modifiers_test, invisible_status_from_group)
{
    active_effect eff{};
    eff.group = magic_type::invisibility;
    eff.status_flag = player_status::invisible;

    std::vector<active_effect> effects = {eff};
    auto result = compute_effect_modifiers(effects);

    EXPECT_NE(result.status & player_status::invisible, player_status::none);
}

TEST(effect_modifiers_test, cursed_status_from_confusion)
{
    active_effect eff{};
    eff.group = magic_type::confusion;
    eff.status_flag = player_status::cursed;

    std::vector<active_effect> effects = {eff};
    auto result = compute_effect_modifiers(effects);

    EXPECT_NE(result.status & player_status::cursed, player_status::none);
}

// ============================================================
// effect_system tests
// ============================================================

class effect_system_test : public ::testing::Test
{
protected:
    void SetUp() override { sys_.initialize(); }

    void TearDown() override { sys_.shutdown(); }

    effect_system sys_;
    hb::entity::entity caster{1};
    hb::entity::entity target{2};
};

TEST_F(effect_system_test, apply_effect_returns_valid_id)
{
    apply_effect_params params{};
    params.source = caster;
    params.target = target;
    params.source_spell = spell_id{10};
    params.group = magic_type::protection;
    params.type = spell_effect_type::buff_defense;
    params.magnitude = 25;
    params.duration_ms = 10000;

    auto eid = sys_.apply_effect(params);
    EXPECT_TRUE(eid.is_valid());
}

TEST_F(effect_system_test, group_slot_blocks_second_effect)
{
    apply_effect_params params1{};
    params1.source = caster;
    params1.target = target;
    params1.group = magic_type::protection;
    params1.type = spell_effect_type::buff_defense;
    params1.magnitude = 25;
    params1.duration_ms = 10000;

    auto eid1 = sys_.apply_effect(params1);
    EXPECT_TRUE(eid1.is_valid());

    // Second effect in same group should be blocked
    apply_effect_params params2{};
    params2.source = caster;
    params2.target = target;
    params2.group = magic_type::protection; // Same group!
    params2.type = spell_effect_type::buff_defense;
    params2.magnitude = 50;
    params2.duration_ms = 20000;

    auto eid2 = sys_.apply_effect(params2);
    EXPECT_FALSE(eid2.is_valid()); // Blocked

    // Original effect should still be there
    EXPECT_TRUE(sys_.has_effect_in_group(target, magic_type::protection));
    EXPECT_EQ(sys_.effect_count(target), 1u);
}

TEST_F(effect_system_test, different_groups_coexist)
{
    apply_effect_params prot{};
    prot.source = caster;
    prot.target = target;
    prot.group = magic_type::protection;
    prot.magnitude = 25;
    prot.duration_ms = 10000;

    apply_effect_params berserk{};
    berserk.source = caster;
    berserk.target = target;
    berserk.group = magic_type::berserk;
    berserk.magnitude = 20;
    berserk.duration_ms = 10000;

    auto eid1 = sys_.apply_effect(prot);
    auto eid2 = sys_.apply_effect(berserk);

    EXPECT_TRUE(eid1.is_valid());
    EXPECT_TRUE(eid2.is_valid());
    EXPECT_EQ(sys_.effect_count(target), 2u);
}

TEST_F(effect_system_test, remove_effect_by_id)
{
    apply_effect_params params{};
    params.source = caster;
    params.target = target;
    params.group = magic_type::protection;
    params.magnitude = 25;
    params.duration_ms = 10000;

    auto eid = sys_.apply_effect(params);
    EXPECT_EQ(sys_.effect_count(target), 1u);

    sys_.remove_effect(target, eid);
    EXPECT_EQ(sys_.effect_count(target), 0u);
    EXPECT_FALSE(sys_.has_effect_in_group(target, magic_type::protection));
}

TEST_F(effect_system_test, remove_effects_by_group)
{
    apply_effect_params prot{};
    prot.source = caster;
    prot.target = target;
    prot.group = magic_type::protection;
    prot.magnitude = 25;
    prot.duration_ms = 10000;

    apply_effect_params berserk{};
    berserk.source = caster;
    berserk.target = target;
    berserk.group = magic_type::berserk;
    berserk.magnitude = 20;
    berserk.duration_ms = 10000;

    sys_.apply_effect(prot);
    sys_.apply_effect(berserk);
    EXPECT_EQ(sys_.effect_count(target), 2u);

    sys_.remove_effects_by_group(target, magic_type::protection);
    EXPECT_EQ(sys_.effect_count(target), 1u);
    EXPECT_FALSE(sys_.has_effect_in_group(target, magic_type::protection));
    EXPECT_TRUE(sys_.has_effect_in_group(target, magic_type::berserk));
}

TEST_F(effect_system_test, remove_all_effects)
{
    apply_effect_params prot{};
    prot.source = caster;
    prot.target = target;
    prot.group = magic_type::protection;
    prot.magnitude = 25;
    prot.duration_ms = 10000;

    apply_effect_params berserk{};
    berserk.source = caster;
    berserk.target = target;
    berserk.group = magic_type::berserk;
    berserk.magnitude = 20;
    berserk.duration_ms = 10000;

    sys_.apply_effect(prot);
    sys_.apply_effect(berserk);
    EXPECT_EQ(sys_.effect_count(target), 2u);

    sys_.remove_all_effects(target);
    EXPECT_EQ(sys_.effect_count(target), 0u);
}

TEST_F(effect_system_test, has_effect_by_type)
{
    apply_effect_params params{};
    params.source = caster;
    params.target = target;
    params.group = magic_type::poison;
    params.type = spell_effect_type::poison;
    params.magnitude = 10;
    params.duration_ms = 10000;

    sys_.apply_effect(params);
    EXPECT_TRUE(sys_.has_effect(target, spell_effect_type::poison));
    EXPECT_FALSE(sys_.has_effect(target, spell_effect_type::stun));
}

TEST_F(effect_system_test, has_status)
{
    apply_effect_params params{};
    params.source = caster;
    params.target = target;
    params.group = magic_type::poison;
    params.type = spell_effect_type::poison;
    params.magnitude = 10;
    params.duration_ms = 10000;

    sys_.apply_effect(params);
    EXPECT_TRUE(sys_.has_status(target, player_status::poisoned));
    EXPECT_FALSE(sys_.has_status(target, player_status::stunned));
}

TEST_F(effect_system_test, get_effect_modifiers)
{
    apply_effect_params params{};
    params.source = caster;
    params.target = target;
    params.group = magic_type::protection;
    params.type = spell_effect_type::none;
    params.magnitude = 30;
    params.duration_ms = 10000;

    sys_.apply_effect(params);

    auto* mods = sys_.get_effect_modifiers(target);
    ASSERT_NE(mods, nullptr);
    EXPECT_EQ(mods->modifiers.defense, 30);
    EXPECT_NE(mods->status & player_status::protection, player_status::none);
}

TEST_F(effect_system_test, effect_applied_callback_fires)
{
    bool called = false;
    hb::entity::entity callback_target{};
    effect_id callback_eid{};

    sys_.on_effect_applied(
        [&](hb::entity::entity t, const active_effect& eff)
        {
            called = true;
            callback_target = t;
            callback_eid = eff.id;
        });

    apply_effect_params params{};
    params.source = caster;
    params.target = target;
    params.group = magic_type::protection;
    params.magnitude = 25;
    params.duration_ms = 10000;

    auto eid = sys_.apply_effect(params);

    EXPECT_TRUE(called);
    EXPECT_EQ(callback_target, target);
    EXPECT_EQ(callback_eid, eid);
}

TEST_F(effect_system_test, effect_removed_callback_fires)
{
    bool called = false;
    hb::entity::entity callback_target{};

    sys_.on_effect_removed(
        [&](hb::entity::entity t, const active_effect&)
        {
            called = true;
            callback_target = t;
        });

    apply_effect_params params{};
    params.source = caster;
    params.target = target;
    params.group = magic_type::protection;
    params.magnitude = 25;
    params.duration_ms = 10000;

    auto eid = sys_.apply_effect(params);
    EXPECT_FALSE(called);

    sys_.remove_effect(target, eid);
    EXPECT_TRUE(called);
    EXPECT_EQ(callback_target, target);
}

TEST_F(effect_system_test, no_effect_on_nonexistent_target)
{
    hb::entity::entity nonexistent{999};
    EXPECT_FALSE(sys_.has_effect_in_group(nonexistent, magic_type::protection));
    EXPECT_FALSE(sys_.has_effect(nonexistent, spell_effect_type::poison));
    EXPECT_FALSE(sys_.has_status(nonexistent, player_status::poisoned));
    EXPECT_EQ(sys_.get_effects(nonexistent), nullptr);
    EXPECT_EQ(sys_.get_effect_modifiers(nonexistent), nullptr);
    EXPECT_EQ(sys_.effect_count(nonexistent), 0u);
}

TEST_F(effect_system_test, permanent_effect_no_expiry)
{
    apply_effect_params params{};
    params.source = caster;
    params.target = target;
    params.group = magic_type::protection;
    params.magnitude = 25;
    params.duration_ms = 0; // Permanent

    sys_.apply_effect(params);

    auto* effects = sys_.get_effects(target);
    ASSERT_NE(effects, nullptr);
    ASSERT_EQ(effects->size(), 1u);
    EXPECT_EQ((*effects)[0].expires_at_ms, 0); // No expiry
}

TEST_F(effect_system_test, multiple_targets_independent)
{
    hb::entity::entity target2{3};

    apply_effect_params p1{};
    p1.source = caster;
    p1.target = target;
    p1.group = magic_type::protection;
    p1.magnitude = 25;
    p1.duration_ms = 10000;

    apply_effect_params p2{};
    p2.source = caster;
    p2.target = target2;
    p2.group = magic_type::protection; // Same group, different target = OK
    p2.magnitude = 50;
    p2.duration_ms = 10000;

    auto eid1 = sys_.apply_effect(p1);
    auto eid2 = sys_.apply_effect(p2);

    EXPECT_TRUE(eid1.is_valid());
    EXPECT_TRUE(eid2.is_valid());
    EXPECT_EQ(sys_.effect_count(target), 1u);
    EXPECT_EQ(sys_.effect_count(target2), 1u);

    // Removing from one target doesn't affect the other
    sys_.remove_all_effects(target);
    EXPECT_EQ(sys_.effect_count(target), 0u);
    EXPECT_EQ(sys_.effect_count(target2), 1u);
}

TEST_F(effect_system_test, tick_callback_fires_on_update)
{
    int tick_count = 0;
    hb::entity::entity tick_target{};

    sys_.on_effect_tick(
        [&](hb::entity::entity t, const active_effect&)
        {
            ++tick_count;
            tick_target = t;
        });

    apply_effect_params params{};
    params.source = caster;
    params.target = target;
    params.group = magic_type::poison;
    params.type = spell_effect_type::poison;
    params.magnitude = 10;
    params.duration_ms = 100000; // Long duration
    params.tick_interval_ms = 1; // Very short tick for testing

    sys_.apply_effect(params);

    // Immediate first tick won't fire since last_tick_ms = now
    sys_.update(0.0f);
    // After a tiny sleep the tick interval (1ms) will have elapsed
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
    sys_.update(0.005f);

    EXPECT_GT(tick_count, 0);
    EXPECT_EQ(tick_target, target);
}

// ============================================================
// Player modifier split tests
// ============================================================

TEST(player_modifier_split_test, equipment_and_effect_combine)
{
    hb::player::player p{};
    p.base.strength = 20;
    p.base.dexterity = 15;
    p.base.vitality = 18;
    p.base.intelligence = 10;
    p.base.magic = 10;
    p.base.charisma = 10;
    p.experience.level = 10;

    p.equipment_modifiers.defense = 50;
    p.equipment_modifiers.attack_power = 30;

    p.effect_modifiers.defense = 25;
    p.effect_modifiers.attack_power = 15;

    p.recalculate_stats();

    // Combined modifiers should have both
    EXPECT_EQ(p.modifiers.defense, 75);
    EXPECT_EQ(p.modifiers.attack_power, 45);
}

TEST(player_modifier_split_test, equipment_change_preserves_effects)
{
    hb::player::player p{};
    p.base.strength = 20;
    p.base.dexterity = 15;
    p.base.vitality = 18;
    p.base.intelligence = 10;
    p.base.magic = 10;
    p.base.charisma = 10;
    p.experience.level = 10;

    p.equipment_modifiers.defense = 50;
    p.effect_modifiers.defense = 25;

    p.recalculate_stats();
    EXPECT_EQ(p.modifiers.defense, 75);

    // Change equipment modifiers (simulating equip/unequip)
    p.equipment_modifiers.defense = 30;
    p.recalculate_stats();

    // Effect modifier should be preserved
    EXPECT_EQ(p.modifiers.defense, 55);
    EXPECT_EQ(p.effect_modifiers.defense, 25); // Unchanged
}

TEST(player_modifier_split_test, effect_change_preserves_equipment)
{
    hb::player::player p{};
    p.base.strength = 20;
    p.base.dexterity = 15;
    p.base.vitality = 18;
    p.base.intelligence = 10;
    p.base.magic = 10;
    p.base.charisma = 10;
    p.experience.level = 10;

    p.equipment_modifiers.defense = 50;
    p.effect_modifiers.defense = 25;

    p.recalculate_stats();
    EXPECT_EQ(p.modifiers.defense, 75);

    // Change effect modifiers (simulating buff expiry)
    p.effect_modifiers = stat_modifiers{};
    p.recalculate_stats();

    // Equipment modifier should be preserved
    EXPECT_EQ(p.modifiers.defense, 50);
    EXPECT_EQ(p.equipment_modifiers.defense, 50); // Unchanged
}

// ============================================================
// Status flag management tests
// ============================================================

TEST(player_status_test, effect_managed_mask_preserves_gm_flags)
{
    hb::player::player p{};

    // Set a non-effect-managed flag
    p.status = player_status::gm_invisible;

    // Simulate set_effect_status - clear effect flags, set new ones
    constexpr auto effect_managed_mask =
        player_status::poisoned | player_status::paralyzed | player_status::invisible | player_status::frozen |
        player_status::berserk | player_status::protection | player_status::defense_up | player_status::attack_up |
        player_status::magic_up | player_status::haste | player_status::slow | player_status::stunned |
        player_status::silenced | player_status::invincible | player_status::cursed;

    auto effect_flags = player_status::poisoned | player_status::protection;
    p.status = (p.status & ~effect_managed_mask) | effect_flags;

    EXPECT_TRUE(p.has_status(player_status::gm_invisible));
    EXPECT_TRUE(p.has_status(player_status::poisoned));
    EXPECT_TRUE(p.has_status(player_status::protection));
    EXPECT_FALSE(p.has_status(player_status::stunned));
}

TEST(player_status_test, clearing_effects_preserves_gm_flags)
{
    hb::player::player p{};
    p.status = player_status::gm_invisible | player_status::poisoned | player_status::berserk;

    constexpr auto effect_managed_mask =
        player_status::poisoned | player_status::paralyzed | player_status::invisible | player_status::frozen |
        player_status::berserk | player_status::protection | player_status::defense_up | player_status::attack_up |
        player_status::magic_up | player_status::haste | player_status::slow | player_status::stunned |
        player_status::silenced | player_status::invincible | player_status::cursed;

    // Clear all effect-managed flags
    p.status = (p.status & ~effect_managed_mask) | player_status::none;

    EXPECT_TRUE(p.has_status(player_status::gm_invisible));
    EXPECT_FALSE(p.has_status(player_status::poisoned));
    EXPECT_FALSE(p.has_status(player_status::berserk));
}
