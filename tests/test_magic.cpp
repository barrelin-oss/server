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
