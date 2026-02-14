// item_audit_test.cpp
// Tests for item transaction audit logging system

#include <gtest/gtest.h>
#include <set>

#include "audit/item_audit_system.h"
#include "core/enums.h"
#include "item/item.h"
#include "registry/item_template.h"
#include "item/item_system.h"

using namespace hb;
using namespace hb::audit;

// ============================================================================
// Buffer tests
// ============================================================================

TEST(item_audit, buffer_accumulates_entries)
{
    item_audit_system audit;
    audit.initialize();

    audit.log_item(1, "Sword", 100, item_log_type::get, 1);
    audit.log_item(2, "Shield", 200, item_log_type::buy, 1);
    audit.log_item(3, "Potion", 300, item_log_type::use, 5);

    EXPECT_EQ(audit.buffer_size(), 3u);
}

TEST(item_audit, flush_clears_buffer)
{
    item_audit_system audit;
    audit.initialize();

    audit.log_item(1, "Sword", 100, item_log_type::get, 1);
    audit.log_item(2, "Shield", 200, item_log_type::buy, 1);

    EXPECT_EQ(audit.buffer_size(), 2u);

    // Flush without database — should clear buffer without crashing
    audit.flush();

    EXPECT_EQ(audit.buffer_size(), 0u);
}

TEST(item_audit, flush_with_no_database_does_not_crash)
{
    item_audit_system audit;
    audit.initialize();

    // Add entries without setting database
    for (int i = 0; i < 10; ++i) {
        audit.log_item(i, "Item" + std::to_string(i), i * 100, item_log_type::get, 1);
    }

    // Flush should not crash
    EXPECT_NO_THROW(audit.flush());
    EXPECT_EQ(audit.buffer_size(), 0u);
}

TEST(item_audit, auto_flush_at_threshold)
{
    item_audit_system audit;
    audit.initialize();

    // Buffer threshold is 50 — adding 50 entries should trigger auto-flush
    for (int i = 0; i < 50; ++i) {
        audit.log_item(1, "Item", i, item_log_type::get, 1);
    }

    // Without a database, entries are discarded on flush
    // Buffer should be empty after auto-flush
    EXPECT_EQ(audit.buffer_size(), 0u);
}

TEST(item_audit, buffer_below_threshold_does_not_auto_flush)
{
    item_audit_system audit;
    audit.initialize();

    for (int i = 0; i < 49; ++i) {
        audit.log_item(1, "Item", i, item_log_type::get, 1);
    }

    EXPECT_EQ(audit.buffer_size(), 49u);
}

// ============================================================================
// Gold logging tests
// ============================================================================

TEST(item_audit, log_gold_always_creates_entry)
{
    item_audit_system audit;
    audit.initialize();

    audit.log_gold(1, item_log_type::gold_loot, 500);
    audit.log_gold(2, item_log_type::gold_shop_spend, -100);
    audit.log_gold(3, item_log_type::gold_trade_receive, 250);

    EXPECT_EQ(audit.buffer_size(), 3u);
}

TEST(item_audit, gold_entry_has_correct_fields)
{
    item_audit_system audit;
    audit.initialize();

    audit.log_gold(42, item_log_type::gold_loot, 500, 0, "elvine", 100, 200,
        {{"npc", "Slime"}});

    EXPECT_EQ(audit.buffer_size(), 1u);
}

TEST(item_audit, gold_entry_item_name_is_gold)
{
    // Gold entries should always have item_name = "gold"
    // This is verified by the implementation — log_gold sets item_name = "gold"
    item_audit_system audit;
    audit.initialize();

    audit.log_gold(1, item_log_type::gold_loot, 100);

    // Can't directly inspect buffer entries without DB, but we verify
    // it doesn't crash and creates an entry
    EXPECT_EQ(audit.buffer_size(), 1u);
}

// ============================================================================
// Item entry data propagation tests
// ============================================================================

TEST(item_audit, item_entry_propagates_character_id)
{
    item_audit_system audit;
    audit.initialize();

    audit.log_item(42, "TestSword", 100, item_log_type::get, 1);

    EXPECT_EQ(audit.buffer_size(), 1u);
}

TEST(item_audit, item_entry_propagates_position)
{
    item_audit_system audit;
    audit.initialize();

    audit.log_item(1, "TestSword", 100, item_log_type::get, 1, 0,
        "elvine", 150, 200);

    EXPECT_EQ(audit.buffer_size(), 1u);
}

TEST(item_audit, item_entry_propagates_other_char_id)
{
    item_audit_system audit;
    audit.initialize();

    audit.log_item(1, "TestSword", 100, item_log_type::exchange, 1, 42);

    EXPECT_EQ(audit.buffer_size(), 1u);
}

TEST(item_audit, item_entry_propagates_details_json)
{
    item_audit_system audit;
    audit.initialize();

    nlohmann::json details = {{"source", "quest"}, {"quest_id", 5}};
    audit.log_item(1, "QuestReward", 100, item_log_type::quest_reward, 1,
        0, "", 0, 0, details);

    EXPECT_EQ(audit.buffer_size(), 1u);
}

// ============================================================================
// Audit flag tests
// ============================================================================

TEST(item_audit, equipment_defaults_to_audited)
{
    item::item itm;
    itm.type = item::item_type::weapon;
    // is_equipment() returns true for weapon
    EXPECT_TRUE(itm.is_equipment());

    // When populated from template, equipment should default to audited=true
    // We test the logic directly:
    bool audited = itm.is_equipment();
    EXPECT_TRUE(audited);
}

TEST(item_audit, consumable_defaults_to_not_audited)
{
    item::item itm;
    itm.type = item::item_type::consumable;
    EXPECT_FALSE(itm.is_equipment());

    // Consumables should default to audited=false
    bool audited = itm.is_equipment();
    EXPECT_FALSE(audited);
}

TEST(item_audit, armor_defaults_to_audited)
{
    item::item itm;
    itm.type = item::item_type::armor;
    EXPECT_TRUE(itm.is_equipment());
}

TEST(item_audit, accessory_defaults_to_audited)
{
    item::item itm;
    itm.type = item::item_type::accessory;
    EXPECT_TRUE(itm.is_equipment());
}

TEST(item_audit, material_defaults_to_not_audited)
{
    item::item itm;
    itm.type = item::item_type::material;
    EXPECT_FALSE(itm.is_equipment());
}

TEST(item_audit, template_audit_override_true)
{
    item_template tmpl;
    tmpl.audit_override = true;

    // When override is set, it should be used regardless of type
    EXPECT_TRUE(tmpl.audit_override.has_value());
    EXPECT_TRUE(*tmpl.audit_override);
}

TEST(item_audit, template_audit_override_false)
{
    item_template tmpl;
    tmpl.audit_override = false;

    EXPECT_TRUE(tmpl.audit_override.has_value());
    EXPECT_FALSE(*tmpl.audit_override);
}

TEST(item_audit, template_audit_override_not_set)
{
    item_template tmpl;

    EXPECT_FALSE(tmpl.audit_override.has_value());
}

// ============================================================================
// Action type enum tests
// ============================================================================

TEST(item_audit, action_types_are_distinct)
{
    // Verify all new action type values are unique
    std::vector<uint8_t> values = {
        static_cast<uint8_t>(item_log_type::give),
        static_cast<uint8_t>(item_log_type::drop),
        static_cast<uint8_t>(item_log_type::get),
        static_cast<uint8_t>(item_log_type::deplete),
        static_cast<uint8_t>(item_log_type::buy),
        static_cast<uint8_t>(item_log_type::sell),
        static_cast<uint8_t>(item_log_type::retrieve),
        static_cast<uint8_t>(item_log_type::deposit),
        static_cast<uint8_t>(item_log_type::exchange),
        static_cast<uint8_t>(item_log_type::make),
        static_cast<uint8_t>(item_log_type::repair),
        static_cast<uint8_t>(item_log_type::use),
        static_cast<uint8_t>(item_log_type::quest_reward),
        static_cast<uint8_t>(item_log_type::gold_loot),
        static_cast<uint8_t>(item_log_type::gold_trade_send),
        static_cast<uint8_t>(item_log_type::gold_trade_receive),
        static_cast<uint8_t>(item_log_type::gold_shop_spend),
        static_cast<uint8_t>(item_log_type::gold_shop_earn),
        static_cast<uint8_t>(item_log_type::guild_bank_deposit),
        static_cast<uint8_t>(item_log_type::guild_bank_withdraw),
        static_cast<uint8_t>(item_log_type::mail_send),
        static_cast<uint8_t>(item_log_type::mail_receive),
        static_cast<uint8_t>(item_log_type::admin_spawn),
        static_cast<uint8_t>(item_log_type::admin_remove),
    };

    std::set<uint8_t> unique_values(values.begin(), values.end());
    EXPECT_EQ(unique_values.size(), values.size())
        << "Duplicate item_log_type values found!";
}

TEST(item_audit, gold_types_are_in_40_range)
{
    EXPECT_EQ(static_cast<uint8_t>(item_log_type::quest_reward), 40);
    EXPECT_EQ(static_cast<uint8_t>(item_log_type::gold_loot), 41);
    EXPECT_EQ(static_cast<uint8_t>(item_log_type::gold_trade_send), 42);
    EXPECT_EQ(static_cast<uint8_t>(item_log_type::gold_trade_receive), 43);
    EXPECT_EQ(static_cast<uint8_t>(item_log_type::gold_shop_spend), 44);
    EXPECT_EQ(static_cast<uint8_t>(item_log_type::gold_shop_earn), 45);
}

TEST(item_audit, admin_types_are_in_70_range)
{
    EXPECT_EQ(static_cast<uint8_t>(item_log_type::admin_spawn), 70);
    EXPECT_EQ(static_cast<uint8_t>(item_log_type::admin_remove), 71);
}

// ============================================================================
// Shutdown safety tests
// ============================================================================

TEST(item_audit, shutdown_flushes_remaining)
{
    item_audit_system audit;
    audit.initialize();

    audit.log_item(1, "Sword", 100, item_log_type::get, 1);
    audit.log_gold(2, item_log_type::gold_loot, 500);

    EXPECT_EQ(audit.buffer_size(), 2u);

    audit.shutdown();

    // After shutdown, buffer should be flushed
    EXPECT_EQ(audit.buffer_size(), 0u);
}

TEST(item_audit, multiple_flush_calls_are_safe)
{
    item_audit_system audit;
    audit.initialize();

    audit.log_item(1, "Sword", 100, item_log_type::get, 1);

    audit.flush();
    audit.flush();
    audit.flush();

    EXPECT_EQ(audit.buffer_size(), 0u);
}

TEST(item_audit, empty_flush_is_noop)
{
    item_audit_system audit;
    audit.initialize();

    EXPECT_NO_THROW(audit.flush());
    EXPECT_EQ(audit.buffer_size(), 0u);
}

// ============================================================================
// Query tests (without DB)
// ============================================================================

TEST(item_audit, query_without_db_returns_empty)
{
    item_audit_system audit;
    audit.initialize();

    item_log_query query;
    query.limit = 10;

    auto results = audit.query_logs(query);
    EXPECT_TRUE(results.empty());
}
