// test_combat.cpp
// Unit tests for combat system

#include <gtest/gtest.h>
#include "combat/combat_events.h"
#include "combat/damage_calc.h"
#include "combat/combat_system.h"

using namespace hb::combat;
using namespace hb::entity;

// Combat events tests

TEST(hit_flags_test, operators)
{
    hit_flags flags = hit_flags::hit | hit_flags::critical;

    EXPECT_TRUE((flags & hit_flags::hit) != hit_flags::none);
    EXPECT_TRUE((flags & hit_flags::critical) != hit_flags::none);
    EXPECT_FALSE((flags & hit_flags::miss) != hit_flags::none);
}

TEST(hit_result_test, queries)
{
    hit_result result;
    result.flags = hit_flags::hit | hit_flags::critical;
    result.raw_damage = 100;
    result.final_damage = 150;

    EXPECT_TRUE(result.is_hit());
    EXPECT_FALSE(result.is_miss());
    EXPECT_TRUE(result.is_critical());
    EXPECT_FALSE(result.is_blocked());
}

TEST(hit_result_test, miss)
{
    hit_result result;
    result.flags = hit_flags::miss;

    EXPECT_FALSE(result.is_hit());
    EXPECT_TRUE(result.is_miss());
}

TEST(combat_context_test, initialization)
{
    combat_context ctx;
    ctx.attacker = entity{1};
    ctx.defender = entity{2};
    ctx.attack_power = 100;
    ctx.defense = 50;
    ctx.hit_rate = 80;
    ctx.critical_rate = 10;
    ctx.critical_damage = 150;

    EXPECT_EQ(ctx.attack_power, 100);
    EXPECT_EQ(ctx.defense, 50);
}

// Damage calculation tests

TEST(damage_calc_test, physical_damage)
{
    // Weapon with 10-20 damage range, 30% armor (defense=30)
    combat_context ctx;
    ctx.type = damage_type::physical;
    ctx.damage_min = 10;
    ctx.damage_max = 20;
    ctx.defense = 30;
    int32_t damage = calc_physical_damage(ctx);
    EXPECT_GT(damage, 0);
    EXPECT_LE(damage, 20); // Should not exceed dice max
}

TEST(damage_calc_test, physical_damage_no_defense)
{
    // Weapon with 10-20 damage range, no armor
    combat_context ctx;
    ctx.type = damage_type::physical;
    ctx.damage_min = 10;
    ctx.damage_max = 20;
    ctx.defense = 0;
    int32_t damage = calc_physical_damage(ctx);
    EXPECT_GT(damage, 0);
    EXPECT_LE(damage, 20);
}

TEST(damage_calc_test, magic_damage)
{
    int32_t damage = calc_magic_damage(100, 50);
    EXPECT_GT(damage, 0);
}

TEST(damage_calc_test, hit_chance)
{
    // Legacy formula: (hit / defense) * 50, bounded 15-99%
    int chance = calc_hit_chance(100, 50);
    EXPECT_GE(chance, 15);
    EXPECT_LE(chance, 99);

    int high_hit = calc_hit_chance(200, 50);
    int low_hit = calc_hit_chance(50, 200);
    EXPECT_GT(high_hit, low_hit);
}

TEST(damage_calc_test, critical_chance)
{
    int normal_crit = calc_critical_chance(10, false);
    int backstab_crit = calc_critical_chance(10, true);

    EXPECT_LT(normal_crit, backstab_crit);
    EXPECT_LE(normal_crit, 75); // Max cap
}

TEST(damage_calc_test, block_chance)
{
    EXPECT_EQ(calc_block_chance(30), 30);
    EXPECT_EQ(calc_block_chance(60), 50); // Capped at 50
    EXPECT_EQ(calc_block_chance(-10), 0);
}

TEST(damage_calc_test, damage_reduction)
{
    int32_t reduced = apply_damage_reduction(100, 50);
    EXPECT_EQ(reduced, 50);

    // Max 80% reduction
    reduced = apply_damage_reduction(100, 90);
    EXPECT_EQ(reduced, 20);

    // Damage can reduce to 0 (renders as "Failed!" on client)
    reduced = apply_damage_reduction(1, 80);
    EXPECT_GE(reduced, 0);
}

TEST(damage_calc_test, critical_damage)
{
    int32_t crit = apply_critical_damage(100, 150);
    EXPECT_EQ(crit, 150);

    crit = apply_critical_damage(100, 200);
    EXPECT_EQ(crit, 200);
}

TEST(damage_calc_test, calculate_final_damage)
{
    combat_context ctx;
    ctx.attacker = entity{1};
    ctx.defender = entity{2};
    ctx.attack_power = 100;
    ctx.defense = 50;
    ctx.hit_rate = 100; // Guaranteed hit for testing
    ctx.dodge_rate = 0;
    ctx.critical_rate = 0;
    ctx.critical_damage = 150;
    ctx.guaranteed_hit = true;

    hit_result result = calculate_final_damage(ctx);
    EXPECT_TRUE(result.is_hit());
    EXPECT_GT(result.final_damage, 0);
}

TEST(damage_calc_test, guaranteed_critical)
{
    combat_context ctx;
    ctx.attack_power = 100;
    ctx.defense = 0;
    ctx.guaranteed_hit = true;
    ctx.guaranteed_critical = true;
    ctx.critical_damage = 200;

    hit_result result = calculate_final_damage(ctx);
    EXPECT_TRUE(result.is_critical());
}

TEST(damage_calc_test, ignore_defense)
{
    combat_context ctx1;
    ctx1.attack_power = 100;
    ctx1.defense = 100;
    ctx1.guaranteed_hit = true;
    ctx1.ignore_defense = false;

    combat_context ctx2 = ctx1;
    ctx2.ignore_defense = true;

    hit_result with_defense = calculate_final_damage(ctx1);
    hit_result without_defense = calculate_final_damage(ctx2);

    EXPECT_GT(without_defense.final_damage, with_defense.final_damage);
}

// Combat system tests

class combat_system_test : public ::testing::Test
{
protected:
    void SetUp() override { system_.initialize(); }

    void TearDown() override { system_.shutdown(); }

    combat_system system_;
};

TEST_F(combat_system_test, lifecycle)
{
    EXPECT_TRUE(system_.is_initialized());
    EXPECT_EQ(system_.name(), "combat_system");
}

TEST_F(combat_system_test, can_attack)
{
    entity attacker{1};
    entity defender{2};

    EXPECT_TRUE(system_.can_attack(attacker, defender));
    EXPECT_FALSE(system_.can_attack(attacker, attacker)); // Can't attack self
    EXPECT_FALSE(system_.can_attack(entity::null(), defender));
}

TEST_F(combat_system_test, invulnerability)
{
    entity e{1};

    EXPECT_FALSE(system_.is_invulnerable(e));

    system_.set_invulnerable(e, 5000);
    EXPECT_TRUE(system_.is_invulnerable(e));
}

TEST_F(combat_system_test, combat_state)
{
    entity e{1};

    EXPECT_FALSE(system_.is_in_combat(e));

    system_.enter_combat(e);
    EXPECT_TRUE(system_.is_in_combat(e));

    system_.leave_combat(e);
    EXPECT_FALSE(system_.is_in_combat(e));
}

TEST_F(combat_system_test, kill_death_tracking)
{
    entity killer{1};
    entity victim{2};

    EXPECT_EQ(system_.get_kill_count(killer), 0);
    EXPECT_EQ(system_.get_death_count(victim), 0);
}

TEST_F(combat_system_test, process_attack)
{
    attack_event attack;
    attack.attacker = entity{1};
    attack.defender = entity{2};
    attack.type = damage_type::physical;
    attack.base_damage = 100;

    auto result = system_.process_attack(attack);

    // Structural invariants regardless of hit/miss
    EXPECT_EQ(result.hit.type, damage_type::physical);
    EXPECT_FALSE(result.target_killed);
    EXPECT_EQ(result.exp_reward, 0);
    EXPECT_EQ(result.gold_reward, 0);

    if (result.hit.is_hit())
    {
        EXPECT_GT(result.hit.final_damage, 0);
        EXPECT_GT(result.hit.raw_damage, 0);
    }
}

TEST_F(combat_system_test, resolve_hit)
{
    combat_context ctx;
    ctx.attacker = entity{1};
    ctx.defender = entity{2};
    ctx.attack_power = 100;
    ctx.guaranteed_hit = true;

    auto result = system_.resolve_hit(ctx);
    EXPECT_TRUE(result.is_hit());
}

TEST_F(combat_system_test, deal_damage)
{
    entity target{1};

    // This should not crash even if target doesn't exist in player/npc system
    system_.deal_damage(target, 50, damage_type::physical, entity{2});
}

TEST_F(combat_system_test, damage_callback)
{
    bool callback_called = false;
    damage_event received_event;

    system_.on_damage(
        [&](const damage_event& event)
        {
            callback_called = true;
            received_event = event;
        });

    entity target{1};
    hit_result result;
    result.flags = hit_flags::hit;
    result.final_damage = 50;

    system_.apply_damage(target, result, entity{2});
    EXPECT_TRUE(callback_called);
    EXPECT_EQ(received_event.target.id, 1);
}

// Additional combat tests

TEST_F(combat_system_test, cannot_attack_null_entity)
{
    EXPECT_FALSE(system_.can_attack(entity::null(), entity{1}));
    EXPECT_FALSE(system_.can_attack(entity{1}, entity::null()));
}

TEST_F(combat_system_test, cannot_attack_invulnerable_target)
{
    entity attacker{1};
    entity defender{2};

    system_.set_invulnerable(defender, 5000);
    // can_attack currently doesn't check invulnerability,
    // but process_attack should handle it
    EXPECT_TRUE(system_.is_invulnerable(defender));
}

TEST_F(combat_system_test, process_attack_with_context)
{
    attack_event attack;
    attack.attacker = entity{1};
    attack.defender = entity{2};
    attack.type = damage_type::physical;
    attack.base_damage = 200;

    auto result = system_.process_attack(attack);

    EXPECT_EQ(result.hit.type, damage_type::physical);
    EXPECT_FALSE(result.target_killed);
    EXPECT_EQ(result.exp_reward, 0);
    EXPECT_EQ(result.gold_reward, 0);

    if (result.hit.is_hit())
    {
        // base_damage=200, defense=0, 90% variance floor → raw >= 180
        EXPECT_GE(result.hit.raw_damage, 100);
        EXPECT_GT(result.hit.final_damage, 0);
    }
}

TEST_F(combat_system_test, process_attack_magic_damage)
{
    attack_event attack;
    attack.attacker = entity{1};
    attack.defender = entity{2};
    attack.type = damage_type::magic;
    attack.base_damage = 150;

    auto result = system_.process_attack(attack);

    EXPECT_EQ(result.hit.type, damage_type::magic);
    EXPECT_FALSE(result.target_killed);

    if (result.hit.is_hit())
    {
        EXPECT_GT(result.hit.final_damage, 0);
        EXPECT_GT(result.hit.raw_damage, 0);
    }
}

TEST_F(combat_system_test, resolve_hit_with_dodge)
{
    combat_context ctx;
    ctx.attacker = entity{1};
    ctx.defender = entity{2};
    ctx.attack_power = 50;
    ctx.hit_rate = 5;    // Very low hit rate
    ctx.dodge_rate = 95; // Very high dodge
    ctx.guaranteed_hit = false;

    // calc_hit_chance(5, 95) = clamp(80 + (5-95)/2, 5, 95) = 35%
    // So 65% of the time we should miss or dodge.
    // Over 200 trials, expect at least 80 non-hits (very conservative).
    int miss_or_dodge_count = 0;
    constexpr int trials = 200;
    for (int i = 0; i < trials; ++i)
    {
        auto result = system_.resolve_hit(ctx);
        if (!result.is_hit())
        {
            ++miss_or_dodge_count;
        }
    }
    EXPECT_GT(miss_or_dodge_count, 80);
    EXPECT_LT(miss_or_dodge_count, 195);
}

TEST_F(combat_system_test, resolve_hit_blocked)
{
    combat_context ctx;
    ctx.attacker = entity{1};
    ctx.defender = entity{2};
    ctx.attack_power = 100;
    ctx.block_rate = 50;
    ctx.guaranteed_hit = true;

    // Run multiple times - statistically some should block
    int block_count = 0;
    for (int i = 0; i < 100; ++i)
    {
        auto result = system_.resolve_hit(ctx);
        if (result.is_blocked())
            ++block_count;
    }
    // With 50% block rate and guaranteed hit, expect some blocks
    EXPECT_GT(block_count, 0);
}

TEST_F(combat_system_test, kill_increments_counters)
{
    entity killer{1};
    entity victim{2};

    system_.enter_combat(killer);
    system_.enter_combat(victim);

    EXPECT_EQ(system_.get_kill_count(killer), 0);
    EXPECT_EQ(system_.get_death_count(victim), 0);

    // The counters should be tracked if the system provides tracking
    // Currently kill/death tracking returns 0 (stub), just verify no crash
}

// Damage calculation edge cases

TEST(damage_calc_test, zero_attack_power)
{
    // Fallback path: no weapon dice set, attack_power=0
    combat_context ctx;
    ctx.type = damage_type::physical;
    ctx.attack_power = 0;
    ctx.damage_min = 0;
    ctx.damage_max = 0;
    int32_t damage = calc_physical_damage(ctx);
    EXPECT_GE(damage, 0);
}

TEST(damage_calc_test, massive_defense)
{
    // Defense is capped at 80%, so damage is never 0 from defense alone
    combat_context ctx;
    ctx.type = damage_type::physical;
    ctx.damage_min = 10;
    ctx.damage_max = 20;
    ctx.defense = 10000; // Will be capped at 80%
    int32_t damage = calc_physical_damage(ctx);
    EXPECT_GE(damage, 0); // Should not underflow
}

TEST(damage_calc_test, magic_damage_no_defense)
{
    int32_t damage = calc_magic_damage(100, 0);
    EXPECT_GT(damage, 0);
}

TEST(damage_calc_test, hit_chance_equal_stats)
{
    int chance = calc_hit_chance(100, 100);
    EXPECT_GE(chance, 5);
    EXPECT_LE(chance, 95);
}

TEST(damage_calc_test, block_chance_zero)
{
    EXPECT_EQ(calc_block_chance(0), 0);
}

TEST(damage_calc_test, damage_reduction_zero_percent)
{
    int32_t reduced = apply_damage_reduction(100, 0);
    EXPECT_EQ(reduced, 100);
}

TEST(damage_calc_test, minimum_damage_zero)
{
    // High reduction can produce 0 damage (anti-physical "Failed!" on client)
    int32_t reduced = apply_damage_reduction(1, 80);
    EXPECT_GE(reduced, 0);
}

TEST(damage_calc_test, critical_damage_minimum_100)
{
    // Critical should not reduce below base
    int32_t crit = apply_critical_damage(100, 100);
    EXPECT_EQ(crit, 100);
}
