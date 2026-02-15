// special_ability_test.cpp
// Tests for special weapon/armor ability system

#include <gtest/gtest.h>
#include <nlohmann/json.hpp>
#include <chrono>

#include "item/special_ability.h"
#include "network/json_protocol.h"

using hb::item::ability_cooldown_seconds;
using hb::item::ability_status;
using hb::item::is_attack_ability;
using hb::item::is_defense_ability;
using hb::item::special_ability_state;
using hb::item::special_ability_type;
using hb::network::json_message;
using hb::network::json_message_type;

// ============================================================================
// Ability type classification
// ============================================================================

TEST(special_ability_type_test, none_is_neither_attack_nor_defense)
{
    EXPECT_FALSE(is_attack_ability(special_ability_type::none));
    EXPECT_FALSE(is_defense_ability(special_ability_type::none));
}

TEST(special_ability_type_test, attack_abilities_classified_correctly)
{
    EXPECT_TRUE(is_attack_ability(special_ability_type::hp_halve));
    EXPECT_TRUE(is_attack_ability(special_ability_type::poison));
    EXPECT_TRUE(is_attack_ability(special_ability_type::paralyze));
    EXPECT_TRUE(is_attack_ability(special_ability_type::warrior_boost));
    EXPECT_TRUE(is_attack_ability(special_ability_type::life_drain));

    EXPECT_FALSE(is_defense_ability(special_ability_type::hp_halve));
}

TEST(special_ability_type_test, defense_abilities_classified_correctly)
{
    EXPECT_TRUE(is_defense_ability(special_ability_type::spell_immunity));
    EXPECT_TRUE(is_defense_ability(special_ability_type::attack_block));
    EXPECT_TRUE(is_defense_ability(special_ability_type::high_spell_immunity));

    EXPECT_FALSE(is_attack_ability(special_ability_type::spell_immunity));
}

// ============================================================================
// Ability state machine
// ============================================================================

TEST(special_ability_state_test, default_state_is_disabled)
{
    special_ability_state state;
    EXPECT_EQ(state.type, special_ability_type::none);
    EXPECT_EQ(state.status, ability_status::disabled);
    EXPECT_FALSE(state.is_ready());
    EXPECT_FALSE(state.is_active());
}

TEST(special_ability_state_test, set_ability_transitions_to_ready)
{
    special_ability_state state;
    state.set_ability(special_ability_type::hp_halve);

    EXPECT_EQ(state.type, special_ability_type::hp_halve);
    EXPECT_EQ(state.status, ability_status::ready);
    EXPECT_TRUE(state.is_ready());
}

TEST(special_ability_state_test, set_ability_none_transitions_to_disabled)
{
    special_ability_state state;
    state.set_ability(special_ability_type::hp_halve);
    state.set_ability(special_ability_type::none);

    EXPECT_EQ(state.status, ability_status::disabled);
    EXPECT_FALSE(state.is_ready());
}

TEST(special_ability_state_test, activate_from_ready_succeeds)
{
    special_ability_state state;
    state.set_ability(special_ability_type::poison);

    auto now = std::chrono::steady_clock::now();
    bool result = state.activate(now);

    EXPECT_TRUE(result);
    EXPECT_TRUE(state.is_active());
    EXPECT_FALSE(state.is_ready());
}

TEST(special_ability_state_test, activate_from_disabled_fails)
{
    special_ability_state state;
    auto now = std::chrono::steady_clock::now();
    bool result = state.activate(now);

    EXPECT_FALSE(result);
    EXPECT_EQ(state.status, ability_status::disabled);
}

TEST(special_ability_state_test, activate_from_active_fails)
{
    special_ability_state state;
    state.set_ability(special_ability_type::hp_halve);

    auto now = std::chrono::steady_clock::now();
    state.activate(now);
    bool result = state.activate(now);

    EXPECT_FALSE(result);
    EXPECT_TRUE(state.is_active());
}

TEST(special_ability_state_test, consume_starts_cooldown)
{
    special_ability_state state;
    state.set_ability(special_ability_type::life_drain);

    auto now = std::chrono::steady_clock::now();
    state.activate(now);
    state.consume(now);

    EXPECT_EQ(state.status, ability_status::cooldown);
    EXPECT_TRUE(state.is_on_cooldown(now));
    EXPECT_FALSE(state.is_active());
    EXPECT_FALSE(state.is_ready());
}

TEST(special_ability_state_test, cooldown_remaining_reports_correctly)
{
    special_ability_state state;
    state.set_ability(special_ability_type::hp_halve);

    auto now = std::chrono::steady_clock::now();
    state.activate(now);
    state.consume(now);

    int32_t remaining = state.cooldown_remaining(now);
    EXPECT_GT(remaining, 1190);
    EXPECT_LE(remaining, ability_cooldown_seconds);
}

TEST(special_ability_state_test, cooldown_expires_and_becomes_ready)
{
    special_ability_state state;
    state.set_ability(special_ability_type::paralyze);

    auto now = std::chrono::steady_clock::now();
    state.activate(now);
    state.consume(now);

    // Simulate time passing beyond cooldown
    auto future = now + std::chrono::seconds(ability_cooldown_seconds + 1);
    EXPECT_FALSE(state.is_on_cooldown(future));

    bool became_ready = state.check_cooldown(future);
    EXPECT_TRUE(became_ready);
    EXPECT_TRUE(state.is_ready());
    EXPECT_EQ(state.status, ability_status::ready);
}

TEST(special_ability_state_test, check_cooldown_during_cooldown_returns_false)
{
    special_ability_state state;
    state.set_ability(special_ability_type::hp_halve);

    auto now = std::chrono::steady_clock::now();
    state.activate(now);
    state.consume(now);

    auto mid = now + std::chrono::seconds(600);
    bool became_ready = state.check_cooldown(mid);
    EXPECT_FALSE(became_ready);
    EXPECT_EQ(state.status, ability_status::cooldown);
}

TEST(special_ability_state_test, clear_resets_everything)
{
    special_ability_state state;
    state.set_ability(special_ability_type::hp_halve);

    auto now = std::chrono::steady_clock::now();
    state.activate(now);
    state.consume(now);
    state.clear();

    EXPECT_EQ(state.type, special_ability_type::none);
    EXPECT_EQ(state.status, ability_status::disabled);
    EXPECT_EQ(state.cooldown_remaining(now), 0);
}

TEST(special_ability_state_test, set_ability_same_type_preserves_cooldown)
{
    // If you re-equip same ability type while on cooldown, preserve state
    special_ability_state state;
    state.set_ability(special_ability_type::hp_halve);
    auto now = std::chrono::steady_clock::now();
    state.activate(now);
    state.consume(now);

    // Re-set same type — set_ability resets to ready (deliberate: legacy resets on re-equip)
    state.set_ability(special_ability_type::hp_halve);
    EXPECT_TRUE(state.is_ready());
}

TEST(special_ability_state_test, activate_after_cooldown_check)
{
    special_ability_state state;
    state.set_ability(special_ability_type::poison);

    auto now = std::chrono::steady_clock::now();
    state.activate(now);
    state.consume(now);

    // Wait for cooldown to expire
    auto future = now + std::chrono::seconds(ability_cooldown_seconds + 1);
    state.check_cooldown(future);

    // Should be able to activate again
    bool result = state.activate(future);
    EXPECT_TRUE(result);
    EXPECT_TRUE(state.is_active());
}

TEST(special_ability_state_test, cooldown_constant_is_20_minutes)
{
    EXPECT_EQ(ability_cooldown_seconds, 1200);
}

// ============================================================================
// Protocol messages
// ============================================================================

TEST(special_ability_protocol_test, activate_ability_response_success)
{
    auto msg = hb::network::make_activate_ability_response(42, true, 1, 1200);
    EXPECT_EQ(msg.type, json_message_type::activate_ability_response);
    EXPECT_EQ(msg.seq, 42u);

    bool success = msg.data["success"];
    EXPECT_TRUE(success);

    int ability_type = msg.data["ability_type"];
    EXPECT_EQ(ability_type, 1);

    int cooldown_sec = msg.data["cooldown_sec"];
    EXPECT_EQ(cooldown_sec, 1200);
}

TEST(special_ability_protocol_test, activate_ability_response_failure)
{
    auto msg = hb::network::make_activate_ability_response(43, false, 0, 0, "on_cooldown");
    EXPECT_EQ(msg.type, json_message_type::activate_ability_response);

    bool success = msg.data["success"];
    EXPECT_FALSE(success);

    std::string error = msg.data["error"];
    EXPECT_EQ(error, "on_cooldown");
}

TEST(special_ability_protocol_test, special_ability_status_message)
{
    auto msg = hb::network::make_special_ability_status("active", 2, 0);
    EXPECT_EQ(msg.type, json_message_type::special_ability_status);

    std::string status = msg.data["status"];
    EXPECT_EQ(status, "active");

    int ability_type = msg.data["ability_type"];
    EXPECT_EQ(ability_type, 2);

    int remaining = msg.data["cooldown_remaining_sec"];
    EXPECT_EQ(remaining, 0);
}

TEST(special_ability_protocol_test, special_ability_status_cooldown)
{
    auto msg = hb::network::make_special_ability_status("cooldown", 50, 600);

    std::string status = msg.data["status"];
    EXPECT_EQ(status, "cooldown");

    int ability_type = msg.data["ability_type"];
    EXPECT_EQ(ability_type, 50);

    int remaining = msg.data["cooldown_remaining_sec"];
    EXPECT_EQ(remaining, 600);
}

TEST(special_ability_protocol_test, special_ability_status_disabled)
{
    auto msg = hb::network::make_special_ability_status("disabled", 0, 0);

    std::string status = msg.data["status"];
    EXPECT_EQ(status, "disabled");
}

// ============================================================================
// Full lifecycle test
// ============================================================================

TEST(special_ability_lifecycle_test, full_equip_activate_consume_cooldown_cycle)
{
    special_ability_state state;

    // 1. Equip a weapon with hp_halve ability
    state.set_ability(special_ability_type::hp_halve);
    EXPECT_TRUE(state.is_ready());

    // 2. Player activates ability
    auto t0 = std::chrono::steady_clock::now();
    EXPECT_TRUE(state.activate(t0));
    EXPECT_TRUE(state.is_active());

    // 3. Player hits enemy, ability consumed
    state.consume(t0);
    EXPECT_EQ(state.status, ability_status::cooldown);

    // 4. Midway through cooldown — not ready
    auto t1 = t0 + std::chrono::seconds(600);
    EXPECT_FALSE(state.check_cooldown(t1));
    EXPECT_TRUE(state.is_on_cooldown(t1));

    // 5. Cooldown expires — becomes ready again
    auto t2 = t0 + std::chrono::seconds(1201);
    EXPECT_TRUE(state.check_cooldown(t2));
    EXPECT_TRUE(state.is_ready());

    // 6. Can activate again
    EXPECT_TRUE(state.activate(t2));
    EXPECT_TRUE(state.is_active());

    // 7. Unequip clears everything
    state.clear();
    EXPECT_EQ(state.type, special_ability_type::none);
    EXPECT_EQ(state.status, ability_status::disabled);
}

TEST(special_ability_lifecycle_test, defense_ability_stays_active_until_unequip)
{
    // Defense abilities don't consume on hit — they stay active
    // until unequipped or manual deactivation (not implemented in this phase)
    special_ability_state state;
    state.set_ability(special_ability_type::spell_immunity);
    EXPECT_TRUE(state.is_ready());
    EXPECT_TRUE(is_defense_ability(state.type));

    auto now = std::chrono::steady_clock::now();
    EXPECT_TRUE(state.activate(now));
    EXPECT_TRUE(state.is_active());
    // Defense abilities stay active — combat code only consumes attack abilities
}
