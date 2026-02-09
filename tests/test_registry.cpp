// test_registry.cpp
// Unit tests for item, npc, and magic registries

#include <gtest/gtest.h>
#include "registry/item_registry.h"
#include "registry/npc_registry.h"
#include "registry/magic_registry.h"

#include <filesystem>
#include <fstream>

using namespace hb;

class registry_test : public ::testing::Test {
protected:
    void SetUp() override {
        // Create temp directory for test files
        test_dir_ = std::filesystem::temp_directory_path() / "hgserver_registry_test";
        std::filesystem::create_directories(test_dir_);
    }

    void TearDown() override {
        std::filesystem::remove_all(test_dir_);
    }

    auto create_test_file(const std::string& name, const std::string& content)
        -> std::filesystem::path
    {
        auto path = test_dir_ / name;
        std::ofstream file(path);
        file << content;
        return path;
    }

    std::filesystem::path test_dir_;
};

// Item Registry Tests

TEST_F(registry_test, item_registry_lifecycle) {
    item_registry registry;

    EXPECT_FALSE(registry.is_initialized());
    EXPECT_EQ(registry.name(), "item_registry");

    registry.initialize();
    EXPECT_TRUE(registry.is_initialized());

    registry.shutdown();
    EXPECT_FALSE(registry.is_initialized());
}

TEST_F(registry_test, item_registry_load) {
    auto path = create_test_file("items.cfg",
        "; Item config\n"
        "1\tSword\t13\t8\t100\t500\t10\t1\t6\t2\t0\t10\t5\t0\t0\n"
        "2\tShield\t14\t7\t50\t300\t5\t0\t0\t0\t15\t8\t0\t0\t0\n"
        "3\tPotion\t5\t0\t5\t25\t0\t0\t0\t0\t0\t0\t0\t0\t0\n"
    );

    item_registry registry;
    registry.initialize();

    auto result = registry.load_from_file(path);
    ASSERT_TRUE(result.is_ok()) << result.error();
    EXPECT_EQ(result.value(), 3);
    EXPECT_EQ(registry.count(), 3);
}

TEST_F(registry_test, item_registry_get_by_id) {
    auto path = create_test_file("items.cfg",
        "100\tTestSword\t13\t8\t100\t500\t10\t2\t8\t3\t0\t15\t10\t5\t0\n"
    );

    item_registry registry;
    registry.initialize();
    registry.load_from_file(path);

    auto* item = registry.get(item_id{100});
    ASSERT_NE(item, nullptr);
    EXPECT_EQ(item->name, "TestSword");
    EXPECT_EQ(item->id.value, 100);
    EXPECT_EQ(item->weight, 100);
    EXPECT_EQ(item->price, 500);

    // Non-existent item
    EXPECT_EQ(registry.get(item_id{999}), nullptr);
}

TEST_F(registry_test, item_registry_find_by_name) {
    auto path = create_test_file("items.cfg",
        "1\tMagicStaff\t13\t8\t50\t1000\t20\t1\t4\t1\t0\t0\t0\t15\t10\n"
    );

    item_registry registry;
    registry.initialize();
    registry.load_from_file(path);

    // Case-insensitive search
    auto* item1 = registry.find_by_name("MagicStaff");
    auto* item2 = registry.find_by_name("magicstaff");
    auto* item3 = registry.find_by_name("MAGICSTAFF");

    ASSERT_NE(item1, nullptr);
    EXPECT_EQ(item1, item2);
    EXPECT_EQ(item2, item3);

    // Non-existent
    EXPECT_EQ(registry.find_by_name("NotAnItem"), nullptr);
}

TEST_F(registry_test, item_registry_duplicate_id) {
    auto path = create_test_file("items.cfg",
        "1\tItem1\t13\t8\t10\t100\t0\n"
        "1\tItem2\t13\t8\t20\t200\t0\n"  // Duplicate ID
        "2\tItem3\t13\t8\t30\t300\t0\n"
    );

    item_registry registry;
    registry.initialize();

    auto result = registry.load_from_file(path);
    EXPECT_TRUE(result.is_ok());
    EXPECT_EQ(registry.count(), 2);  // Only 2 loaded, duplicate skipped
}

// NPC Registry Tests

TEST_F(registry_test, npc_registry_lifecycle) {
    npc_registry registry;

    EXPECT_FALSE(registry.is_initialized());
    EXPECT_EQ(registry.name(), "npc_registry");

    registry.initialize();
    EXPECT_TRUE(registry.is_initialized());

    registry.shutdown();
    EXPECT_FALSE(registry.is_initialized());
}

TEST_F(registry_test, npc_registry_load) {
    auto path = create_test_file("npcs.cfg",
        "1\tGoblin\t0\t100\t20\t5\t1\t4\t2\t5\t50\t10\t50\t100\t150\n"
        "2\tOrc\t0\t200\t40\t10\t2\t6\t4\t10\t100\t20\t100\t120\t180\n"
    );

    npc_registry registry;
    registry.initialize();

    auto result = registry.load_from_file(path);
    ASSERT_TRUE(result.is_ok()) << result.error();
    EXPECT_EQ(result.value(), 2);
}

TEST_F(registry_test, npc_registry_get_by_id) {
    auto path = create_test_file("npcs.cfg",
        "10\tDragon\t5\t5000\t1000\t50\t3\t10\t20\t100\t10000\t500\t2000\t80\t60\n"
    );

    npc_registry registry;
    registry.initialize();
    registry.load_from_file(path);

    auto* npc = registry.get(npc_id{10});
    ASSERT_NE(npc, nullptr);
    EXPECT_EQ(npc->name, "Dragon");
    EXPECT_EQ(npc->hp, 5000);
    EXPECT_EQ(npc->level, 50);
    EXPECT_EQ(npc->exp_reward, 10000);
}

TEST_F(registry_test, npc_registry_by_level_range) {
    auto path = create_test_file("npcs.cfg",
        "1\tRat\t0\t10\t5\t1\t1\t2\t0\t1\t5\t0\t5\n"
        "2\tWolf\t0\t50\t20\t5\t1\t4\t2\t5\t25\t5\t20\n"
        "3\tBear\t0\t150\t60\t15\t2\t6\t4\t10\t100\t20\t80\n"
        "4\tOgre\t0\t300\t100\t25\t2\t8\t6\t15\t200\t40\t150\n"
    );

    npc_registry registry;
    registry.initialize();
    registry.load_from_file(path);

    auto level_1_10 = registry.by_level_range(1, 10);
    EXPECT_EQ(level_1_10.size(), 2);  // Rat (1) and Wolf (5)

    auto level_10_20 = registry.by_level_range(10, 20);
    EXPECT_EQ(level_10_20.size(), 1);  // Bear (15) only - Wolf is level 5, not in range

    auto level_20_30 = registry.by_level_range(20, 30);
    EXPECT_EQ(level_20_30.size(), 1);  // Ogre (25)
}

// Magic Registry Tests

TEST_F(registry_test, magic_registry_lifecycle) {
    magic_registry registry;

    EXPECT_FALSE(registry.is_initialized());
    EXPECT_EQ(registry.name(), "magic_registry");

    registry.initialize();
    EXPECT_TRUE(registry.is_initialized());

    registry.shutdown();
    EXPECT_FALSE(registry.is_initialized());
}

TEST_F(registry_test, magic_registry_load) {
    auto path = create_test_file("magic.yaml",
        "magic:\n"
        "  - id: 1\n"
        "    name: Fireball\n"
        "    type: 1\n"
        "    mana_cost: 10\n"
        "    delay: 500\n"
        "    range1: 8\n"
        "    effect1: { dice: 2, sides: 8, bonus: 5 }\n"
        "    int_req: 10\n"
        "  - id: 2\n"
        "    name: Heal\n"
        "    type: 2\n"
        "    mana_cost: 15\n"
        "    delay: 300\n"
        "    range1: 5\n"
        "    effect1: { dice: 2, sides: 6, bonus: 3 }\n"
        "    int_req: 8\n"
    );

    magic_registry registry;
    registry.initialize();

    auto result = registry.load_from_file(path);
    ASSERT_TRUE(result.is_ok()) << result.error();
    EXPECT_EQ(result.value(), 2);
}

TEST_F(registry_test, magic_registry_get_by_id) {
    auto path = create_test_file("magic.yaml",
        "magic:\n"
        "  - id: 5\n"
        "    name: IceStorm\n"
        "    type: 3\n"
        "    mana_cost: 30\n"
        "    delay: 800\n"
        "    range1: 10\n"
        "    effect1: { dice: 10, sides: 15, bonus: 0 }\n"
        "    int_req: 20\n"
    );

    magic_registry registry;
    registry.initialize();
    registry.load_from_file(path);

    auto* spell = registry.get(spell_id{5});
    ASSERT_NE(spell, nullptr);
    EXPECT_EQ(spell->name, "IceStorm");
    EXPECT_EQ(spell->mana_cost, 30);
    EXPECT_EQ(spell->base_damage, 80);  // 10 * (15+1)/2 + 0 = 80
}

TEST_F(registry_test, magic_registry_by_type) {
    auto path = create_test_file("magic.yaml",
        "magic:\n"
        "  - id: 1\n"
        "    name: Flame\n"
        "    type: 1\n"
        "    mana_cost: 10\n"
        "    int_req: 5\n"
        "  - id: 2\n"
        "    name: Blaze\n"
        "    type: 1\n"
        "    mana_cost: 20\n"
        "    int_req: 10\n"
        "  - id: 3\n"
        "    name: Cure\n"
        "    type: 2\n"
        "    mana_cost: 15\n"
        "    int_req: 5\n"
    );

    magic_registry registry;
    registry.initialize();
    registry.load_from_file(path);

    auto damage_spells = registry.by_type(magic_type::damage_spot);
    EXPECT_EQ(damage_spells.size(), 2);

    auto heal_spells = registry.by_type(magic_type::hp_up_spot);
    EXPECT_EQ(heal_spells.size(), 1);
}

TEST_F(registry_test, magic_registry_damage_calculation) {
    auto path = create_test_file("magic.yaml",
        "magic:\n"
        "  - id: 1\n"
        "    name: TestSpell\n"
        "    type: 1\n"
        "    mana_cost: 10\n"
        "    delay: 500\n"
        "    range1: 8\n"
        "    effect1: { dice: 10, sides: 19, bonus: 0 }\n"
        "    int_req: 15\n"
    );

    magic_registry registry;
    registry.initialize();
    registry.load_from_file(path);

    auto* spell = registry.get(spell_id{1});
    ASSERT_NE(spell, nullptr);

    // base_damage = dice * (sides+1)/2 + bonus = 10 * (19+1)/2 + 0 = 100
    EXPECT_EQ(spell->base_damage, 100);
}

TEST_F(registry_test, magic_registry_parses_range2_as_area_radius) {
    auto path = create_test_file("magic.yaml",
        "magic:\n"
        "  - id: 1\n"
        "    name: FireBall\n"
        "    type: 3\n"
        "    mana_cost: 27\n"
        "    range1: 2\n"
        "    range2: 4\n"
        "    effect1: { dice: 2, sides: 6, bonus: 2 }\n"
        "    int_req: 26\n"
    );

    magic_registry registry;
    registry.initialize();
    registry.load_from_file(path);

    auto* spell = registry.get(spell_id{1});
    ASSERT_NE(spell, nullptr);
    EXPECT_EQ(spell->range, 2);
    EXPECT_EQ(spell->area_radius, 4);
}

TEST_F(registry_test, magic_registry_area_radius_defaults_to_zero) {
    auto path = create_test_file("magic.yaml",
        "magic:\n"
        "  - id: 1\n"
        "    name: MagicMissile\n"
        "    type: 1\n"
        "    mana_cost: 8\n"
        "    range1: 1\n"
        "    effect1: { dice: 1, sides: 8, bonus: 0 }\n"
        "    int_req: 18\n"
    );

    magic_registry registry;
    registry.initialize();
    registry.load_from_file(path);

    auto* spell = registry.get(spell_id{1});
    ASSERT_NE(spell, nullptr);
    EXPECT_EQ(spell->area_radius, 0);
}
