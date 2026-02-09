// test_magic.cpp
// Unit tests for magic system

#include <gtest/gtest.h>
#include "core/types.h"
#include "magic/spell.h"
#include "magic/magic_system.h"

using hb::spell_id;
using namespace hb::magic;
using namespace hb::entity;

// Spell tests

TEST(spell_template_test, default_values) {
    spell_template spell;
    EXPECT_EQ(spell.category, spell_category::attack);
    EXPECT_EQ(spell.target_type, spell_target::single_enemy);
    EXPECT_EQ(spell.mana_cost, 0);
}

TEST(spell_template_test, is_offensive) {
    spell_template spell;
    spell.category = spell_category::attack;
    EXPECT_TRUE(spell.is_offensive());
    EXPECT_FALSE(spell.is_defensive());

    spell.category = spell_category::debuff;
    EXPECT_TRUE(spell.is_offensive());
}

TEST(spell_template_test, is_defensive) {
    spell_template spell;
    spell.category = spell_category::healing;
    EXPECT_TRUE(spell.is_defensive());
    EXPECT_FALSE(spell.is_offensive());

    spell.category = spell_category::buff;
    EXPECT_TRUE(spell.is_defensive());
}

TEST(spell_template_test, is_aoe) {
    spell_template spell;
    spell.target_type = spell_target::single_enemy;
    EXPECT_FALSE(spell.is_aoe());

    spell.target_type = spell_target::aoe_enemy;
    EXPECT_TRUE(spell.is_aoe());

    spell.target_type = spell_target::aoe_all;
    EXPECT_TRUE(spell.is_aoe());
}

TEST(cast_target_test, entity_and_position) {
    cast_target target;
    EXPECT_FALSE(target.has_entity());

    target.target = entity{42};
    EXPECT_TRUE(target.has_entity());

    target.target_pos = hb::world::position{10, 20};
    EXPECT_TRUE(target.has_position());
}

TEST(spell_cast_state_test, active) {
    spell_cast_state state;
    EXPECT_FALSE(state.is_active());

    state.spell = spell_id{1};
    EXPECT_TRUE(state.is_active());

    state.cancel();
    EXPECT_FALSE(state.is_active());
}

TEST(spell_cast_state_test, progress) {
    spell_cast_state state;
    state.spell = spell_id{1};
    state.start_time_ms = 0;
    state.end_time_ms = 1000;

    EXPECT_FLOAT_EQ(state.progress(0), 0.0f);
    EXPECT_FLOAT_EQ(state.progress(500), 0.5f);
    EXPECT_FLOAT_EQ(state.progress(1000), 1.0f);
}

TEST(spell_cast_state_test, is_complete) {
    spell_cast_state state;
    state.spell = spell_id{1};
    state.start_time_ms = 0;
    state.end_time_ms = 1000;

    EXPECT_FALSE(state.is_complete(500));
    EXPECT_TRUE(state.is_complete(1000));
    EXPECT_TRUE(state.is_complete(1500));
}

TEST(spell_knowledge_test, cooldown) {
    spell_knowledge knowledge;
    knowledge.spell = spell_id{1};
    knowledge.last_cast_time_ms = 0;

    int32_t remaining = knowledge.cooldown_remaining(500, 1000);
    EXPECT_EQ(remaining, 500);

    remaining = knowledge.cooldown_remaining(1500, 1000);
    EXPECT_EQ(remaining, 0);

    EXPECT_TRUE(knowledge.is_on_cooldown(500, 1000));
    EXPECT_FALSE(knowledge.is_on_cooldown(1500, 1000));
}

// Magic system tests

class magic_system_test : public ::testing::Test {
protected:
    void SetUp() override {
        system_.initialize();

        // Register a test spell
        test_spell_.id = spell_id{1};
        test_spell_.name = "Test Spell";
        test_spell_.category = spell_category::attack;
        test_spell_.target_type = spell_target::single_enemy;
        test_spell_.mana_cost = 10;
        test_spell_.cast_time_ms = 0;  // Instant
        test_spell_.cooldown_ms = 1000;
        test_spell_.base_damage = 50;
        system_.register_spell(test_spell_);
    }

    void TearDown() override {
        system_.shutdown();
    }

    magic_system system_;
    spell_template test_spell_;
};

TEST_F(magic_system_test, lifecycle) {
    EXPECT_TRUE(system_.is_initialized());
    EXPECT_EQ(system_.name(), "magic_system");
}

TEST_F(magic_system_test, register_and_get_spell) {
    const auto* spell = system_.get_spell(spell_id{1});
    ASSERT_NE(spell, nullptr);
    EXPECT_EQ(spell->name, "Test Spell");
    EXPECT_EQ(spell->base_damage, 50);
}

TEST_F(magic_system_test, learn_spell) {
    entity caster{1};

    EXPECT_FALSE(system_.knows_spell(caster, spell_id{1}));

    system_.learn_spell(caster, spell_id{1});
    EXPECT_TRUE(system_.knows_spell(caster, spell_id{1}));
    EXPECT_EQ(system_.get_spell_level(caster, spell_id{1}), 1);
}

TEST_F(magic_system_test, forget_spell) {
    entity caster{1};

    system_.learn_spell(caster, spell_id{1});
    EXPECT_TRUE(system_.knows_spell(caster, spell_id{1}));

    system_.forget_spell(caster, spell_id{1});
    EXPECT_FALSE(system_.knows_spell(caster, spell_id{1}));
}

TEST_F(magic_system_test, level_up_spell) {
    entity caster{1};

    system_.learn_spell(caster, spell_id{1});
    EXPECT_EQ(system_.get_spell_level(caster, spell_id{1}), 1);

    system_.level_up_spell(caster, spell_id{1});
    EXPECT_EQ(system_.get_spell_level(caster, spell_id{1}), 2);
}

TEST_F(magic_system_test, can_cast) {
    entity caster{1};

    // Not learned yet
    auto result = system_.can_cast(caster, spell_id{1}, cast_target{});
    EXPECT_EQ(result, cast_result::spell_not_learned);

    system_.learn_spell(caster, spell_id{1});
    result = system_.can_cast(caster, spell_id{1}, cast_target{});
    EXPECT_EQ(result, cast_result::success);
}

TEST_F(magic_system_test, instant_cast) {
    entity caster{1};
    system_.learn_spell(caster, spell_id{1});

    cast_target target;
    target.target = entity{2};

    auto result = system_.instant_cast(caster, spell_id{1}, target);
    ASSERT_TRUE(result.is_ok());

    auto effect = result.value();
    EXPECT_TRUE(effect.success);
    EXPECT_GT(effect.damage_dealt, 0);
}

TEST_F(magic_system_test, cooldown) {
    entity caster{1};
    system_.learn_spell(caster, spell_id{1});

    cast_target target;
    target.target = entity{2};

    // First cast should work
    auto result = system_.instant_cast(caster, spell_id{1}, target);
    EXPECT_TRUE(result.is_ok());

    // Should be on cooldown now
    EXPECT_GT(system_.get_cooldown_remaining(caster, spell_id{1}), 0);

    // Reset cooldown
    system_.reset_cooldown(caster, spell_id{1});
    EXPECT_EQ(system_.get_cooldown_remaining(caster, spell_id{1}), 0);
}

TEST_F(magic_system_test, reset_all_cooldowns) {
    entity caster{1};
    system_.learn_spell(caster, spell_id{1});

    cast_target target;
    target.target = entity{2};

    system_.instant_cast(caster, spell_id{1}, target);
    EXPECT_GT(system_.get_cooldown_remaining(caster, spell_id{1}), 0);

    system_.reset_all_cooldowns(caster);
    EXPECT_EQ(system_.get_cooldown_remaining(caster, spell_id{1}), 0);
}

TEST_F(magic_system_test, is_casting) {
    entity caster{1};
    system_.learn_spell(caster, spell_id{1});

    EXPECT_FALSE(system_.is_casting(caster));

    // For instant spells, should not be casting
    cast_target target;
    target.target = entity{2};
    system_.begin_cast(caster, spell_id{1}, target);

    // Instant cast completes immediately
    EXPECT_FALSE(system_.is_casting(caster));
}

TEST_F(magic_system_test, cancel_cast) {
    // Register a channeled spell
    spell_template channel_spell;
    channel_spell.id = spell_id{2};
    channel_spell.name = "Channeled Spell";
    channel_spell.cast_time_ms = 2000;
    channel_spell.mana_cost = 20;
    system_.register_spell(channel_spell);

    entity caster{1};
    system_.learn_spell(caster, spell_id{2});

    cast_target target;
    target.target = entity{2};

    system_.begin_cast(caster, spell_id{2}, target);
    EXPECT_TRUE(system_.is_casting(caster));

    system_.cancel_cast(caster);
    EXPECT_FALSE(system_.is_casting(caster));
}

TEST_F(magic_system_test, calculate_mana_cost) {
    entity caster{1};

    int32_t cost = system_.calculate_mana_cost(caster, spell_id{1});
    EXPECT_EQ(cost, 10);  // Base cost from test_spell_
}

TEST_F(magic_system_test, spell_callback) {
    entity caster{1};
    system_.learn_spell(caster, spell_id{1});

    bool callback_called = false;
    spell_template received_spell;

    system_.on_spell_cast([&](entity e, const spell_template& spell, const spell_effect_result&) {
        callback_called = true;
        received_spell = spell;
    });

    cast_target target;
    target.target = entity{2};
    system_.instant_cast(caster, spell_id{1}, target);

    EXPECT_TRUE(callback_called);
    EXPECT_EQ(received_spell.name, "Test Spell");
}

// Self-targeting prevention

TEST_F(magic_system_test, offensive_spell_blocks_self_target) {
    entity caster{1};
    system_.learn_spell(caster, spell_id{1});

    cast_target target;
    target.target = caster;  // Target self

    auto result = system_.can_cast(caster, spell_id{1}, target);
    EXPECT_EQ(result, cast_result::invalid_target);
}

TEST_F(magic_system_test, healing_spell_allows_self_target) {
    spell_template heal;
    heal.id = spell_id{50};
    heal.name = "Self Heal";
    heal.category = spell_category::healing;
    heal.target_type = spell_target::single_ally;
    heal.mana_cost = 10;
    heal.base_heal = 50;
    system_.register_spell(heal);

    entity caster{1};
    system_.learn_spell(caster, spell_id{50});

    cast_target target;
    target.target = caster;  // Target self

    auto result = system_.can_cast(caster, spell_id{50}, target);
    EXPECT_EQ(result, cast_result::success);
}

// Range validation

TEST_F(magic_system_test, range_check_skipped_without_player_system) {
    // Without player_system wired, range check is skipped
    entity caster{1};
    system_.learn_spell(caster, spell_id{1});

    cast_target target;
    target.target = entity{2};

    // Should succeed since we can't look up positions without player_system
    auto result = system_.can_cast(caster, spell_id{1}, target);
    EXPECT_EQ(result, cast_result::success);
}

// Additional magic system tests

TEST_F(magic_system_test, get_nonexistent_spell) {
    const auto* spell = system_.get_spell(spell_id{999});
    EXPECT_EQ(spell, nullptr);
}

TEST_F(magic_system_test, learn_same_spell_twice) {
    entity caster{1};
    system_.learn_spell(caster, spell_id{1});
    system_.learn_spell(caster, spell_id{1});

    // Should still know the spell at level 1 (no duplicate)
    EXPECT_TRUE(system_.knows_spell(caster, spell_id{1}));
    EXPECT_EQ(system_.get_spell_level(caster, spell_id{1}), 1);
}

TEST_F(magic_system_test, forget_unlearned_spell) {
    entity caster{1};
    // Should not crash
    system_.forget_spell(caster, spell_id{1});
    EXPECT_FALSE(system_.knows_spell(caster, spell_id{1}));
}

TEST_F(magic_system_test, level_up_unlearned_spell) {
    entity caster{1};
    // Should not crash or create knowledge
    system_.level_up_spell(caster, spell_id{1});
    EXPECT_FALSE(system_.knows_spell(caster, spell_id{1}));
}

TEST_F(magic_system_test, cast_unknown_spell) {
    entity caster{1};
    system_.learn_spell(caster, spell_id{1});

    cast_target target;
    target.target = entity{2};

    auto result = system_.instant_cast(caster, spell_id{999}, target);
    EXPECT_TRUE(result.is_err());
}

TEST_F(magic_system_test, cast_unlearned_spell) {
    entity caster{1};
    // Don't learn the spell

    cast_target target;
    target.target = entity{2};

    auto result = system_.instant_cast(caster, spell_id{1}, target);
    EXPECT_TRUE(result.is_err());
}

TEST_F(magic_system_test, begin_cast_channeled_spell) {
    spell_template channel_spell;
    channel_spell.id = spell_id{10};
    channel_spell.name = "Big Fireball";
    channel_spell.category = spell_category::attack;
    channel_spell.target_type = spell_target::single_enemy;
    channel_spell.mana_cost = 30;
    channel_spell.cast_time_ms = 3000;
    channel_spell.base_damage = 200;
    system_.register_spell(channel_spell);

    entity caster{1};
    system_.learn_spell(caster, spell_id{10});

    cast_target target;
    target.target = entity{2};

    auto result = system_.begin_cast(caster, spell_id{10}, target);
    EXPECT_TRUE(result.is_ok());
    EXPECT_TRUE(system_.is_casting(caster));

    auto* state = system_.get_cast_state(caster);
    ASSERT_NE(state, nullptr);
    EXPECT_TRUE(state->is_active());
    ASSERT_TRUE(state->spell.has_value());
    EXPECT_EQ(state->spell->value, 10);
}

TEST_F(magic_system_test, get_cast_state_not_casting) {
    entity caster{1};
    EXPECT_EQ(system_.get_cast_state(caster), nullptr);
}

TEST_F(magic_system_test, healing_spell) {
    spell_template heal_spell;
    heal_spell.id = spell_id{20};
    heal_spell.name = "Heal";
    heal_spell.category = spell_category::healing;
    heal_spell.target_type = spell_target::single_ally;
    heal_spell.mana_cost = 15;
    heal_spell.base_heal = 100;
    system_.register_spell(heal_spell);

    entity caster{1};
    system_.learn_spell(caster, spell_id{20});

    cast_target target;
    target.target = entity{2};

    auto result = system_.instant_cast(caster, spell_id{20}, target);
    ASSERT_TRUE(result.is_ok());
    EXPECT_TRUE(result.value().success);
    EXPECT_GT(result.value().heal_applied, 0);
}

TEST_F(magic_system_test, calculate_damage) {
    entity caster{1};
    int32_t damage = system_.calculate_damage(caster, spell_id{1});
    EXPECT_GT(damage, 0);
}

TEST_F(magic_system_test, calculate_heal) {
    spell_template heal_spell;
    heal_spell.id = spell_id{30};
    heal_spell.name = "Minor Heal";
    heal_spell.category = spell_category::healing;
    heal_spell.base_heal = 50;
    system_.register_spell(heal_spell);

    entity caster{1};
    int32_t heal = system_.calculate_heal(caster, spell_id{30});
    EXPECT_GT(heal, 0);
}

TEST_F(magic_system_test, mana_cost_with_modifier) {
    magic_system_config config;
    config.global_mana_cost_modifier = 2.0f;
    system_.set_config(config);

    entity caster{1};
    int32_t cost = system_.calculate_mana_cost(caster, spell_id{1});
    EXPECT_EQ(cost, 20);  // 10 base * 2.0 modifier
}

TEST_F(magic_system_test, set_player_spells) {
    entity caster{1};

    std::vector<spell_knowledge> spells;
    spell_knowledge sk;
    sk.spell = spell_id{1};
    sk.level = 5;
    spells.push_back(sk);

    system_.set_player_spells(caster, std::move(spells));

    EXPECT_TRUE(system_.knows_spell(caster, spell_id{1}));
    EXPECT_EQ(system_.get_spell_level(caster, spell_id{1}), 5);

    const auto* player_spells = system_.get_player_spells(caster);
    ASSERT_NE(player_spells, nullptr);
    EXPECT_EQ(player_spells->size(), 1);
}

TEST_F(magic_system_test, get_player_spells_unknown_caster) {
    const auto* spells = system_.get_player_spells(entity{999});
    EXPECT_EQ(spells, nullptr);
}

// Spell template feature tests

TEST(spell_template_test, utility_spell) {
    spell_template spell;
    spell.category = spell_category::utility;
    EXPECT_FALSE(spell.is_offensive());
    EXPECT_FALSE(spell.is_defensive());
}

TEST(spell_template_test, summon_spell) {
    spell_template spell;
    spell.category = spell_category::summon;
    EXPECT_FALSE(spell.is_offensive());
    EXPECT_FALSE(spell.is_defensive());
}

TEST(spell_template_test, ground_target) {
    spell_template spell;
    spell.target_type = spell_target::ground;
    EXPECT_FALSE(spell.is_aoe());

    spell.target_type = spell_target::cone;
    EXPECT_FALSE(spell.is_aoe());
}

TEST(cast_target_test, default_no_position) {
    cast_target target;
    EXPECT_FALSE(target.has_entity());
    EXPECT_FALSE(target.has_position());
}

TEST(spell_cast_state_test, cancel_stops_activity) {
    spell_cast_state state;
    state.spell = spell_id{1};
    EXPECT_TRUE(state.is_active());

    state.cancel();
    EXPECT_FALSE(state.is_active());
    EXPECT_FLOAT_EQ(state.progress(500), 0.0f);  // Progress returns 0 when inactive
}

TEST(spell_knowledge_test, total_casts_tracking) {
    spell_knowledge knowledge;
    knowledge.total_casts = 5;
    EXPECT_EQ(knowledge.total_casts, 5);

    ++knowledge.total_casts;
    EXPECT_EQ(knowledge.total_casts, 6);
}

// ============================================================================
// Integration tests: AOE targeting and range validation with player_system
// ============================================================================

#include "player/player_system.h"
#include "world/world_subsystem.h"
#include "entity/entity_manager.h"
#include "combat/combat_system.h"
#include "core/subsystem.h"

using hb::player_id;
using hb::map_id;

class magic_aoe_test : public ::testing::Test {
protected:
    void SetUp() override
    {
        hb::subsystems().create_subsystem<hb::player::player_system>();
        hb::subsystems().create_subsystem<hb::world::world_subsystem>();
        hb::subsystems().create_subsystem<hb::magic::magic_system>();
        hb::subsystems().create_subsystem<hb::entity::entity_manager>();
        hb::subsystems().create_subsystem<hb::combat::combat_system>();
        hb::subsystems().initialize_all();

        player_sys_ = hb::subsystems().get<hb::player::player_system>();
        world_ = hb::subsystems().get<hb::world::world_subsystem>();
        magic_ = hb::subsystems().get<hb::magic::magic_system>();

        // Create a map
        hb::world::map_config cfg;
        cfg.name = "test_map";
        cfg.width = 100;
        cfg.height = 100;
        auto result = world_->create_map(cfg);
        ASSERT_TRUE(result.is_ok());
        map_id_ = result.value();

        // Register AOE attack spell
        spell_template aoe_attack;
        aoe_attack.id = hb::spell_id(1);
        aoe_attack.name = "Fire Ball";
        aoe_attack.category = spell_category::attack;
        aoe_attack.target_type = spell_target::aoe_enemy;
        aoe_attack.mana_cost = 20;
        aoe_attack.aoe_radius = 3;
        aoe_attack.base_damage = 50;
        aoe_attack.range = 2;  // 24 tiles
        magic_->register_spell(aoe_attack);

        // Register single-target attack spell with short range
        spell_template single_attack;
        single_attack.id = hb::spell_id(2);
        single_attack.name = "Magic Missile";
        single_attack.category = spell_category::attack;
        single_attack.target_type = spell_target::single_enemy;
        single_attack.mana_cost = 8;
        single_attack.base_damage = 30;
        single_attack.range = 1;  // 12 tiles
        magic_->register_spell(single_attack);
    }

    void TearDown() override
    {
        hb::subsystems().clear_all();
    }

    auto create_player_at(hb::world::position pos, hb::faction faction = hb::faction::neutral) -> player_id
    {
        hb::player::player_create_info info;
        info.name = "player_" + std::to_string(next_name_++);
        auto result = player_sys_->create_player(info);
        auto pid = result.value();
        player_sys_->set_position(pid, map_id_, pos, hb::world::direction::south);

        auto* p = player_sys_->get_player(pid);
        if (p) {
            p->mp = 1000;
            p->experience.level = 100;
            p->computed.intelligence = 100;
            p->computed.magic = 100;
            p->faction = faction;
        }
        return pid;
    }

    hb::player::player_system* player_sys_{};
    hb::world::world_subsystem* world_{};
    hb::magic::magic_system* magic_{};
    map_id map_id_{};
    int next_name_{1};
};

TEST_F(magic_aoe_test, aoe_spell_excludes_caster)
{
    auto caster_pid = create_player_at({50, 50}, hb::faction::aresden);
    auto enemy_pid = create_player_at({52, 50}, hb::faction::elvine);

    entity caster_e(caster_pid.value);
    entity enemy_e(enemy_pid.value);

    magic_->learn_spell(caster_e, hb::spell_id(1));

    // Cast AOE centered on enemy position
    cast_target target;
    target.target = enemy_e;

    auto result = magic_->instant_cast(caster_e, hb::spell_id(1), target);
    ASSERT_TRUE(result.is_ok());

    auto& effect = result.value();
    EXPECT_TRUE(effect.success);

    // Enemy should be hit
    bool enemy_hit = false;
    bool caster_hit = false;
    for (auto& t : effect.affected_targets) {
        if (t.id == enemy_pid.value) enemy_hit = true;
        if (t.id == caster_pid.value) caster_hit = true;
    }
    EXPECT_TRUE(enemy_hit);
    EXPECT_FALSE(caster_hit) << "Caster should not be hit by their own AOE";
}

TEST_F(magic_aoe_test, aoe_spell_skips_same_faction)
{
    auto caster_pid = create_player_at({50, 50}, hb::faction::aresden);
    auto ally_pid = create_player_at({51, 50}, hb::faction::aresden);
    auto enemy_pid = create_player_at({52, 50}, hb::faction::elvine);

    entity caster_e(caster_pid.value);

    magic_->learn_spell(caster_e, hb::spell_id(1));

    cast_target target;
    target.target_pos = hb::world::position{51, 50};  // Center between all three

    auto result = magic_->instant_cast(caster_e, hb::spell_id(1), target);
    ASSERT_TRUE(result.is_ok());

    auto& effect = result.value();
    bool ally_hit = false;
    bool enemy_hit = false;
    for (auto& t : effect.affected_targets) {
        if (t.id == ally_pid.value) ally_hit = true;
        if (t.id == enemy_pid.value) enemy_hit = true;
    }
    EXPECT_FALSE(ally_hit) << "Same-faction ally should not be hit by enemy AOE";
    EXPECT_TRUE(enemy_hit);
}

TEST_F(magic_aoe_test, aoe_spell_center_from_position)
{
    auto caster_pid = create_player_at({10, 10}, hb::faction::aresden);
    auto far_enemy = create_player_at({50, 50}, hb::faction::elvine);
    auto near_enemy = create_player_at({22, 10}, hb::faction::elvine);

    entity caster_e(caster_pid.value);

    magic_->learn_spell(caster_e, hb::spell_id(1));

    // Target position near the near_enemy, far from far_enemy
    cast_target target;
    target.target_pos = hb::world::position{22, 10};

    auto result = magic_->instant_cast(caster_e, hb::spell_id(1), target);
    ASSERT_TRUE(result.is_ok());

    auto& effect = result.value();
    bool near_hit = false;
    bool far_hit = false;
    for (auto& t : effect.affected_targets) {
        if (t.id == near_enemy.value) near_hit = true;
        if (t.id == far_enemy.value) far_hit = true;
    }
    EXPECT_TRUE(near_hit) << "Enemy within AOE radius should be hit";
    EXPECT_FALSE(far_hit) << "Enemy outside AOE radius should not be hit";
}

TEST_F(magic_aoe_test, range_check_blocks_distant_target)
{
    auto caster_pid = create_player_at({10, 10});
    auto enemy_pid = create_player_at({80, 80});  // ~140 manhattan distance

    entity caster_e(caster_pid.value);
    entity enemy_e(enemy_pid.value);

    magic_->learn_spell(caster_e, hb::spell_id(2));  // range=1 (12 tiles)

    cast_target target;
    target.target = enemy_e;

    auto result = magic_->can_cast(caster_e, hb::spell_id(2), target);
    EXPECT_EQ(result, cast_result::out_of_range);
}

TEST_F(magic_aoe_test, range_check_allows_close_target)
{
    auto caster_pid = create_player_at({50, 50});
    auto enemy_pid = create_player_at({55, 50});  // 5 tiles manhattan

    entity caster_e(caster_pid.value);
    entity enemy_e(enemy_pid.value);

    magic_->learn_spell(caster_e, hb::spell_id(2));  // range=1 (12 tiles)

    cast_target target;
    target.target = enemy_e;

    auto result = magic_->can_cast(caster_e, hb::spell_id(2), target);
    EXPECT_EQ(result, cast_result::success);
}

TEST_F(magic_aoe_test, range_check_with_position_target)
{
    auto caster_pid = create_player_at({50, 50});

    entity caster_e(caster_pid.value);

    magic_->learn_spell(caster_e, hb::spell_id(1));  // range=2 (24 tiles)

    // Position within range
    cast_target close_target;
    close_target.target_pos = hb::world::position{60, 50};  // 10 tiles
    EXPECT_EQ(magic_->can_cast(caster_e, hb::spell_id(1), close_target), cast_result::success);

    // Position out of range
    cast_target far_target;
    far_target.target_pos = hb::world::position{90, 90};  // 80 tiles
    EXPECT_EQ(magic_->can_cast(caster_e, hb::spell_id(1), far_target), cast_result::out_of_range);
}

TEST_F(magic_aoe_test, self_target_blocked_for_offensive_spell)
{
    auto caster_pid = create_player_at({50, 50});
    entity caster_e(caster_pid.value);

    magic_->learn_spell(caster_e, hb::spell_id(2));

    cast_target target;
    target.target = caster_e;

    auto result = magic_->can_cast(caster_e, hb::spell_id(2), target);
    EXPECT_EQ(result, cast_result::invalid_target);
}

TEST_F(magic_aoe_test, mana_deducted_on_instant_cast)
{
    auto caster_pid = create_player_at({50, 50}, hb::faction::aresden);
    auto enemy_pid = create_player_at({52, 50}, hb::faction::elvine);

    entity caster_e(caster_pid.value);
    entity enemy_e(enemy_pid.value);

    magic_->learn_spell(caster_e, hb::spell_id(1));  // mana_cost = 20

    auto* caster = player_sys_->get_player(caster_pid);
    ASSERT_NE(caster, nullptr);
    int32_t mp_before = caster->mp;

    cast_target target;
    target.target = enemy_e;

    auto result = magic_->instant_cast(caster_e, hb::spell_id(1), target);
    ASSERT_TRUE(result.is_ok());
    EXPECT_TRUE(result.value().success);

    EXPECT_EQ(caster->mp, mp_before - 20) << "Mana should be deducted after cast";
}
