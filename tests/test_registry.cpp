// test_registry.cpp
// Unit tests for item, npc, and magic registries

#include <gtest/gtest.h>
#include "registry/item_registry.h"
#include "registry/npc_registry.h"
#include "registry/magic_registry.h"

#include <filesystem>
#include <fstream>

using namespace hb;

class registry_test : public ::testing::Test
{
protected:
    void SetUp() override
    {
        // Create temp directory for test files
        test_dir_ = std::filesystem::temp_directory_path() / "hgserver_registry_test";
        std::filesystem::create_directories(test_dir_);
    }

    void TearDown() override { std::filesystem::remove_all(test_dir_); }

    auto create_test_file(const std::string& name, const std::string& content) -> std::filesystem::path
    {
        auto path = test_dir_ / name;
        std::ofstream file(path);
        file << content;
        return path;
    }

    std::filesystem::path test_dir_;
};

// Item Registry Tests

TEST_F(registry_test, item_registry_lifecycle)
{
    item_registry registry;

    EXPECT_FALSE(registry.is_initialized());
    EXPECT_EQ(registry.name(), "item_registry");

    registry.initialize();
    EXPECT_TRUE(registry.is_initialized());

    registry.shutdown();
    EXPECT_FALSE(registry.is_initialized());
}

TEST_F(registry_test, item_registry_load)
{
    auto path = create_test_file("items.yaml",
                                 "items:\n"
                                 "  - {id: 1, name: Sword, type: 13, equip_pos: 8, weight: 100, price: 500}\n"
                                 "  - {id: 2, name: Shield, type: 14, equip_pos: 7, weight: 50, price: 300}\n"
                                 "  - {id: 3, name: Potion, type: 5, equip_pos: 0, weight: 5, price: 25}\n");

    item_registry registry;
    registry.initialize();

    auto result = registry.load_from_file(path);
    ASSERT_TRUE(result.is_ok()) << result.error();
    EXPECT_EQ(result.value(), 3);
    EXPECT_EQ(registry.count(), 3);
}

TEST_F(registry_test, item_registry_get_by_id)
{
    auto path = create_test_file("items.yaml",
                                 "items:\n"
                                 "  - {id: 100, name: TestSword, type: 13, equip_pos: 8, weight: 100, price: 500}\n");

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

TEST_F(registry_test, item_registry_find_by_name)
{
    auto path = create_test_file("items.yaml",
                                 "items:\n"
                                 "  - {id: 1, name: MagicStaff, type: 13, equip_pos: 8, weight: 50, price: 1000}\n");

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

TEST_F(registry_test, item_registry_duplicate_id)
{
    auto path = create_test_file("items.yaml",
                                 "items:\n"
                                 "  - {id: 1, name: Item1, type: 13, equip_pos: 8, weight: 10, price: 100}\n"
                                 "  - {id: 1, name: Item2, type: 13, equip_pos: 8, weight: 20, price: 200}\n"
                                 "  - {id: 2, name: Item3, type: 13, equip_pos: 8, weight: 30, price: 300}\n");

    item_registry registry;
    registry.initialize();

    auto result = registry.load_from_file(path);
    EXPECT_TRUE(result.is_ok());
    EXPECT_EQ(registry.count(), 2); // Only 2 loaded, duplicate skipped
}

// NPC Registry Tests

TEST_F(registry_test, npc_registry_lifecycle)
{
    npc_registry registry;

    EXPECT_FALSE(registry.is_initialized());
    EXPECT_EQ(registry.name(), "npc_registry");

    registry.initialize();
    EXPECT_TRUE(registry.is_initialized());

    registry.shutdown();
    EXPECT_FALSE(registry.is_initialized());
}

TEST_F(registry_test, npc_registry_load)
{
    auto path = create_test_file("npcs.cfg",
                                 "1\tGoblin\t0\t100\t20\t5\t1\t4\t2\t5\t50\t10\t50\t100\t150\n"
                                 "2\tOrc\t0\t200\t40\t10\t2\t6\t4\t10\t100\t20\t100\t120\t180\n");

    npc_registry registry;
    registry.initialize();

    auto result = registry.load_from_file(path);
    ASSERT_TRUE(result.is_ok()) << result.error();
    EXPECT_EQ(result.value(), 2);
}

TEST_F(registry_test, npc_registry_get_by_id)
{
    auto path =
        create_test_file("npcs.cfg", "10\tDragon\t5\t5000\t1000\t50\t3\t10\t20\t100\t10000\t500\t2000\t80\t60\n");

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

TEST_F(registry_test, npc_registry_by_level_range)
{
    auto path = create_test_file("npcs.cfg",
                                 "1\tRat\t0\t10\t5\t1\t1\t2\t0\t1\t5\t0\t5\n"
                                 "2\tWolf\t0\t50\t20\t5\t1\t4\t2\t5\t25\t5\t20\n"
                                 "3\tBear\t0\t150\t60\t15\t2\t6\t4\t10\t100\t20\t80\n"
                                 "4\tOgre\t0\t300\t100\t25\t2\t8\t6\t15\t200\t40\t150\n");

    npc_registry registry;
    registry.initialize();
    registry.load_from_file(path);

    auto level_1_10 = registry.by_level_range(1, 10);
    EXPECT_EQ(level_1_10.size(), 2); // Rat (1) and Wolf (5)

    auto level_10_20 = registry.by_level_range(10, 20);
    EXPECT_EQ(level_10_20.size(), 1); // Bear (15) only - Wolf is level 5, not in range

    auto level_20_30 = registry.by_level_range(20, 30);
    EXPECT_EQ(level_20_30.size(), 1); // Ogre (25)
}

// NPC YAML loader tests

TEST_F(registry_test, npc_yaml_loads_correct_field_mapping)
{
    auto path = create_test_file("npcs.yaml",
                                 "npcs:\n"
                                 "  - { name: Slime, sprite_id: 10, hit_dice: 2, defense_ratio: 20, hit_ratio: 30, "
                                 "min_bravery: 1, exp: 4, attack_dice: 1, attack_sides: 4, body_size: 0, "
                                 "side: 10, action_limit: 0, action_time: 2100, resist_magic: 5, magic_level: 0, "
                                 "day_of_week: 10, chat_msg: 0, detection_range: 2, regen_time: 5000, attribute: 1, "
                                 "abs_damage: 0, max_mana: 0, magic_hit_ratio: 0, attack_range: 1, area: 0 }\n");

    npc_registry registry;
    registry.initialize();

    auto result = registry.load_from_file(path);
    ASSERT_TRUE(result.is_ok()) << result.error();
    EXPECT_EQ(result.value(), 1);

    auto* npc = registry.get(npc_id{1});
    ASSERT_NE(npc, nullptr);
    EXPECT_EQ(npc->name, "Slime");
    EXPECT_EQ(npc->sprite_id, 10);
    EXPECT_EQ(npc->hit_dice, 2);
    // defense_ratio do .cfg legado e o denominador da chance de acerto (dodge_rate),
    // nao absorcao; npc.defense fica 0 de proposito (ver npc_registry.cpp).
    EXPECT_EQ(npc->dodge_rate, 20);
    EXPECT_EQ(npc->defense, 0);
    EXPECT_EQ(npc->hit_ratio, 30);
    EXPECT_EQ(npc->min_bravery, 1);
    EXPECT_EQ(npc->exp_reward, 4);
    EXPECT_EQ(npc->attack_dice, 1);
    EXPECT_EQ(npc->attack_sides, 4);
    EXPECT_EQ(npc->body_size, 0);
    EXPECT_EQ(npc->action_limit, 0);
    EXPECT_EQ(npc->action_time, 2100);
    EXPECT_EQ(npc->magic_resist, 5); // RestM -> magic_resist
    EXPECT_EQ(npc->magic_level, 0);
    EXPECT_EQ(npc->day_of_week, 10);
    EXPECT_EQ(npc->chat_msg, 0);
    EXPECT_EQ(npc->sight_range, 2); // detection_range -> sight_range
    EXPECT_EQ(npc->regen_time, 5000);
    EXPECT_EQ(npc->attribute, 1);
    EXPECT_EQ(npc->abs_damage, 0);
    EXPECT_EQ(npc->mp, 0); // max_mana -> mp
    EXPECT_EQ(npc->magic_hit_ratio, 0);
    EXPECT_EQ(npc->attack_range, 1); // Raw tile value
    EXPECT_EQ(npc->area, 0);
}

TEST_F(registry_test, npc_yaml_derives_level_from_hit_ratio)
{
    auto path = create_test_file("npcs.yaml",
                                 "npcs:\n"
                                 "  - { name: Cyclops, sprite_id: 13, hit_dice: 60, defense: 100, hit_ratio: 180, "
                                 "min_bravery: 100, exp: 625, attack_dice: 8, attack_sides: 6, body_size: 1, "
                                 "side: 10, action_limit: 0, action_time: 1200, resist_magic: 65, magic_level: 5, "
                                 "day_of_week: 10, chat_msg: 0, detection_range: 7, regen_time: 5000, attribute: 3, "
                                 "abs_damage: 0, max_mana: 450, magic_hit_ratio: 50, attack_range: 3, area: 0 }\n");

    npc_registry registry;
    registry.initialize();
    registry.load_from_file(path);

    auto* npc = registry.get(npc_id{1});
    ASSERT_NE(npc, nullptr);
    EXPECT_EQ(npc->level, 180);      // level = hit_ratio
    EXPECT_EQ(npc->hit_rate, 180);   // hit_rate = hit_ratio
    EXPECT_EQ(npc->exp_reward, 625); // exp -> exp_reward
    EXPECT_EQ(npc->mp, 450);         // max_mana -> mp
}

TEST_F(registry_test, npc_yaml_aggression_from_side_10)
{
    auto path = create_test_file("npcs.yaml",
                                 "npcs:\n"
                                 // Side 10 + action_limit 0 = aggressive monster
                                 "  - { name: Orc, sprite_id: 14, hit_dice: 4, defense: 75, hit_ratio: 70, "
                                 "min_bravery: 2, exp: 20, attack_dice: 3, attack_sides: 3, body_size: 0, "
                                 "side: 10, action_limit: 0, action_time: 1200, resist_magic: 25, magic_level: 0, "
                                 "day_of_week: 10, chat_msg: 0, detection_range: 5, regen_time: 5000, attribute: 1, "
                                 "abs_damage: 0, max_mana: 0, magic_hit_ratio: 0, attack_range: 1, area: 0 }\n"
                                 // Side 0 + action_limit 0 = passive creature (Rabbit)
                                 "  - { name: Rabbit, sprite_id: 55, hit_dice: 4, defense: 20, hit_ratio: 35, "
                                 "min_bravery: 2, exp: 150, attack_dice: 1, attack_sides: 5, body_size: 0, "
                                 "side: 0, action_limit: 0, action_time: 1500, resist_magic: 5, magic_level: 0, "
                                 "day_of_week: 10, chat_msg: 0, detection_range: 2, regen_time: 5000, attribute: 1, "
                                 "abs_damage: 0, max_mana: 0, magic_hit_ratio: 0, attack_range: 1, area: 0 }\n");

    npc_registry registry;
    registry.initialize();
    registry.load_from_file(path);

    auto* orc = registry.get(npc_id{1});
    ASSERT_NE(orc, nullptr);
    EXPECT_TRUE(orc->is_aggressive);
    EXPECT_EQ(orc->type, npc_type::monster);

    auto* rabbit = registry.get(npc_id{2});
    ASSERT_NE(rabbit, nullptr);
    EXPECT_FALSE(rabbit->is_aggressive);
    EXPECT_EQ(rabbit->type, npc_type::monster); // Passive creature: huntable, never starts a fight
}

TEST_F(registry_test, npc_yaml_dummy_not_aggressive)
{
    auto path = create_test_file("npcs.yaml",
                                 "npcs:\n"
                                 "  - { name: Dummy, sprite_id: 34, hit_dice: 10, defense: 30, hit_ratio: 300, "
                                 "min_bravery: 100, exp: 35, attack_dice: 0, attack_sides: 0, body_size: 1, "
                                 "side: 10, action_limit: 3, action_time: 2100, resist_magic: 5, magic_level: 0, "
                                 "day_of_week: 10, chat_msg: 0, detection_range: 7, regen_time: 3000, attribute: 2, "
                                 "abs_damage: 0, max_mana: 0, magic_hit_ratio: 0, attack_range: 1, area: 0 }\n");

    npc_registry registry;
    registry.initialize();
    registry.load_from_file(path);

    auto* dummy = registry.get(npc_id{1});
    ASSERT_NE(dummy, nullptr);
    EXPECT_FALSE(dummy->is_aggressive);        // action_limit=3 => not aggressive
    EXPECT_EQ(dummy->type, npc_type::monster); // Still targetable
}

TEST_F(registry_test, npc_yaml_town_npc_from_action_limit)
{
    auto path = create_test_file("npcs.yaml",
                                 "npcs:\n"
                                 "  - { name: ShopKeeper-W, sprite_id: 15, hit_dice: 10, defense: 10, hit_ratio: 20, "
                                 "min_bravery: 5, exp: 1, attack_dice: 1, attack_sides: 1, body_size: 0, "
                                 "side: 0, action_limit: 2, action_time: 15000, resist_magic: 0, magic_level: 0, "
                                 "day_of_week: 10, chat_msg: 1, detection_range: 0, regen_time: 10000, attribute: 0, "
                                 "abs_damage: 0, max_mana: 0, magic_hit_ratio: 0, attack_range: 1, area: 0 }\n");

    npc_registry registry;
    registry.initialize();
    registry.load_from_file(path);

    auto* shop = registry.get(npc_id{1});
    ASSERT_NE(shop, nullptr);
    EXPECT_FALSE(shop->is_aggressive);
    EXPECT_EQ(shop->type, npc_type::npc);
    EXPECT_TRUE(shop->can_talk);
}

TEST_F(registry_test, npc_yaml_guard_detection)
{
    auto path =
        create_test_file("npcs.yaml",
                         "npcs:\n"
                         "  - { name: Guard-Aresden, sprite_id: 21, hit_dice: 1350, defense: 150, hit_ratio: 230, "
                         "min_bravery: 3, exp: 0, attack_dice: 12, attack_sides: 18, body_size: 0, "
                         "side: 1, action_limit: 0, action_time: 1000, resist_magic: 100, magic_level: -10, "
                         "day_of_week: 10, chat_msg: 1, detection_range: 8, regen_time: 10000, attribute: 2, "
                         "abs_damage: 0, max_mana: 1000, magic_hit_ratio: 130, attack_range: 5, area: 0 }\n");

    npc_registry registry;
    registry.initialize();
    registry.load_from_file(path);

    auto* guard = registry.get(npc_id{1});
    ASSERT_NE(guard, nullptr);
    EXPECT_EQ(guard->type, npc_type::guard);
    EXPECT_TRUE(guard->is_aggressive);
    EXPECT_EQ(guard->level, 230); // hit_ratio
}

TEST_F(registry_test, npc_yaml_undead_by_name)
{
    auto path = create_test_file("npcs.yaml",
                                 "npcs:\n"
                                 "  - { name: Zombie, sprite_id: 18, hit_dice: 10, defense: 80, hit_ratio: 90, "
                                 "min_bravery: 100, exp: 40, attack_dice: 4, attack_sides: 4, body_size: 0, "
                                 "side: 10, action_limit: 0, action_time: 1500, resist_magic: 30, magic_level: 0, "
                                 "day_of_week: 10, chat_msg: 0, detection_range: 6, regen_time: 5000, attribute: 1, "
                                 "abs_damage: 0, max_mana: 0, magic_hit_ratio: 0, attack_range: 1, area: 0 }\n"
                                 "  - { name: Orc, sprite_id: 14, hit_dice: 4, defense: 75, hit_ratio: 70, "
                                 "min_bravery: 2, exp: 20, attack_dice: 3, attack_sides: 3, body_size: 0, "
                                 "side: 10, action_limit: 0, action_time: 1200, resist_magic: 25, magic_level: 0, "
                                 "day_of_week: 10, chat_msg: 0, detection_range: 5, regen_time: 5000, attribute: 1, "
                                 "abs_damage: 0, max_mana: 0, magic_hit_ratio: 0, attack_range: 1, area: 0 }\n");

    npc_registry registry;
    registry.initialize();
    registry.load_from_file(path);

    auto* zombie = registry.get(npc_id{1});
    ASSERT_NE(zombie, nullptr);
    EXPECT_TRUE(zombie->is_undead);

    auto* orc = registry.get(npc_id{2});
    ASSERT_NE(orc, nullptr);
    EXPECT_FALSE(orc->is_undead);
}

TEST_F(registry_test, npc_yaml_hp_computed_from_hit_dice)
{
    auto path = create_test_file("npcs.yaml",
                                 "npcs:\n"
                                 // hit_dice=2 (<=5): hp = 2*3 + 2 = 8 (approximate)
                                 "  - { name: SmallMob, sprite_id: 10, hit_dice: 2, defense: 20, hit_ratio: 30, "
                                 "min_bravery: 0, exp: 4, attack_dice: 1, attack_sides: 4, body_size: 0, "
                                 "side: 10, action_limit: 0, action_time: 2100, resist_magic: 5, magic_level: 0, "
                                 "day_of_week: 10, chat_msg: 0, detection_range: 2, regen_time: 5000, attribute: 1, "
                                 "abs_damage: 0, max_mana: 0, magic_hit_ratio: 0, attack_range: 1, area: 0 }\n"
                                 // hit_dice=60 (>5): hp = 60*5 + 60 = 360 (approximate)
                                 "  - { name: BigMob, sprite_id: 13, hit_dice: 60, defense: 100, hit_ratio: 180, "
                                 "min_bravery: 100, exp: 625, attack_dice: 8, attack_sides: 6, body_size: 1, "
                                 "side: 10, action_limit: 0, action_time: 1200, resist_magic: 65, magic_level: 5, "
                                 "day_of_week: 10, chat_msg: 0, detection_range: 7, regen_time: 5000, attribute: 3, "
                                 "abs_damage: 0, max_mana: 450, magic_hit_ratio: 50, attack_range: 3, area: 0 }\n");

    npc_registry registry;
    registry.initialize();
    registry.load_from_file(path);

    auto* small_mob = registry.get(npc_id{1});
    ASSERT_NE(small_mob, nullptr);
    // hit_dice=2, <=5: hp = 2*3 + 2 = 8
    EXPECT_EQ(small_mob->hp, 8);
    EXPECT_EQ(small_mob->hit_dice, 2);

    auto* big_mob = registry.get(npc_id{2});
    ASSERT_NE(big_mob, nullptr);
    // hit_dice=60, >5: hp = 60*5 + 60 = 360
    EXPECT_EQ(big_mob->hp, 360);
    EXPECT_EQ(big_mob->hit_dice, 60);
}

TEST_F(registry_test, npc_yaml_gate_not_aggressive)
{
    auto path = create_test_file("npcs.yaml",
                                 "npcs:\n"
                                 "  - { name: gate-a, sprite_id: 91, hit_dice: 1500, defense: 750, hit_ratio: 100, "
                                 "min_bravery: 100, exp: 40000, attack_dice: 0, attack_sides: 0, body_size: 1, "
                                 "side: 1, action_limit: 8, action_time: 1600, resist_magic: 100, magic_level: 0, "
                                 "day_of_week: 10, chat_msg: 0, detection_range: 1, regen_time: 3000, attribute: 2, "
                                 "abs_damage: 0, max_mana: 0, magic_hit_ratio: 0, attack_range: 1, area: 3 }\n");

    npc_registry registry;
    registry.initialize();
    registry.load_from_file(path);

    auto* gate = registry.get(npc_id{1});
    ASSERT_NE(gate, nullptr);
    EXPECT_FALSE(gate->is_aggressive); // action_limit=8 => gate, not aggressive
    EXPECT_EQ(gate->action_limit, 8);
    EXPECT_EQ(gate->area, 3);
}

// Magic Registry Tests

TEST_F(registry_test, magic_registry_lifecycle)
{
    magic_registry registry;

    EXPECT_FALSE(registry.is_initialized());
    EXPECT_EQ(registry.name(), "magic_registry");

    registry.initialize();
    EXPECT_TRUE(registry.is_initialized());

    registry.shutdown();
    EXPECT_FALSE(registry.is_initialized());
}

TEST_F(registry_test, magic_registry_load)
{
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
                                 "    int_req: 8\n");

    magic_registry registry;
    registry.initialize();

    auto result = registry.load_from_file(path);
    ASSERT_TRUE(result.is_ok()) << result.error();
    EXPECT_EQ(result.value(), 2);
}

TEST_F(registry_test, magic_registry_get_by_id)
{
    auto path = create_test_file("magic.yaml",
                                 "magic:\n"
                                 "  - id: 5\n"
                                 "    name: IceStorm\n"
                                 "    type: 3\n"
                                 "    mana_cost: 30\n"
                                 "    delay: 800\n"
                                 "    range1: 10\n"
                                 "    effect1: { dice: 10, sides: 15, bonus: 0 }\n"
                                 "    int_req: 20\n");

    magic_registry registry;
    registry.initialize();
    registry.load_from_file(path);

    auto* spell = registry.get(spell_id{5});
    ASSERT_NE(spell, nullptr);
    EXPECT_EQ(spell->name, "IceStorm");
    EXPECT_EQ(spell->mana_cost, 30);
    EXPECT_EQ(spell->base_damage, 80); // 10 * (15+1)/2 + 0 = 80
}

TEST_F(registry_test, magic_registry_by_type)
{
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
                                 "    int_req: 5\n");

    magic_registry registry;
    registry.initialize();
    registry.load_from_file(path);

    auto damage_spells = registry.by_type(magic_type::damage_spot);
    EXPECT_EQ(damage_spells.size(), 2);

    auto heal_spells = registry.by_type(magic_type::hp_up_spot);
    EXPECT_EQ(heal_spells.size(), 1);
}

TEST_F(registry_test, magic_registry_legacy_damage_types)
{
    auto path = create_test_file("magic.yaml",
                                 "magic:\n"
                                 "  - id: 70\n"
                                 "    name: Bloody-Shock-Wave\n"
                                 "    type: 19\n"
                                 "    mana_cost: 120\n"
                                 "    effect1: { dice: 8, sides: 8, bonus: 50 }\n"
                                 "    int_req: 105\n"
                                 "  - id: 60\n"
                                 "    name: Energy-Strike\n"
                                 "    type: 21\n"
                                 "    mana_cost: 65\n"
                                 "    range2: 2\n"
                                 "    effect1: { dice: 7, sides: 6, bonus: 17 }\n"
                                 "    int_req: 67\n"
                                 "  - id: 64\n"
                                 "    name: Earthworm-Strike\n"
                                 "    type: 25\n"
                                 "    mana_cost: 80\n"
                                 "    range2: 2\n"
                                 "    effect1: { dice: 7, sides: 6, bonus: 17 }\n"
                                 "    effect2: { dice: 7, sides: 6, bonus: 17 }\n"
                                 "    int_req: 97\n"
                                 "  - id: 66\n"
                                 "    name: Armor-Break\n"
                                 "    type: 26\n"
                                 "    mana_cost: 90\n"
                                 "    effect1: { dice: 7, sides: 6, bonus: 17 }\n"
                                 "    int_req: 97\n"
                                 "  - id: 91\n"
                                 "    name: Blizzard\n"
                                 "    type: 27\n"
                                 "    mana_cost: 170\n"
                                 "    effect1: { dice: 10, sides: 10, bonus: 16 }\n"
                                 "    int_req: 195\n"
                                 "  - id: 54\n"
                                 "    name: Spike-Field\n"
                                 "    type: 14\n"
                                 "    mana_cost: 56\n"
                                 "    duration: 30\n"
                                 "    effect1: { dice: 2, sides: 8, bonus: 0 }\n"
                                 "    effect3: { dice: 9, sides: 2, bonus: 2 }\n"
                                 "    int_req: 56\n"
                                 "  - id: 99\n"
                                 "    name: BrokenField\n"
                                 "    type: 14\n"
                                 "    mana_cost: 10\n"
                                 "    int_req: 10\n");

    magic_registry registry;
    registry.initialize();

    auto result = registry.load_from_file(path);
    ASSERT_TRUE(result.is_ok()) << result.error();
    // Type 14 (create_dynamic) loads when effect3 provides the object type;
    // BrokenField (no effect3) is skipped
    EXPECT_EQ(result.value(), 6);
    EXPECT_EQ(registry.get(spell_id{99}), nullptr);

    const auto* field = registry.get(spell_id{54});
    ASSERT_NE(field, nullptr);
    EXPECT_EQ(field->type, magic_type::create_dynamic);
    EXPECT_TRUE(field->is_offensive);
    EXPECT_EQ(field->dynamic_type, 9); // spike
    EXPECT_EQ(field->dynamic_rx, 2);
    EXPECT_EQ(field->dynamic_ry, 2);
    EXPECT_EQ(field->effect_duration.count(), 30000);

    const auto* linear = registry.get(spell_id{70});
    ASSERT_NE(linear, nullptr);
    EXPECT_EQ(linear->type, magic_type::damage_linear);
    EXPECT_TRUE(linear->is_offensive);

    const auto* nospot = registry.get(spell_id{60});
    ASSERT_NE(nospot, nullptr);
    EXPECT_EQ(nospot->type, magic_type::damage_area_no_center);
    EXPECT_TRUE(nospot->is_offensive);

    const auto* spdown = registry.get(spell_id{64});
    ASSERT_NE(spdown, nullptr);
    EXPECT_EQ(spdown->type, magic_type::damage_area_sp_down);
    EXPECT_TRUE(spdown->is_offensive);
    EXPECT_EQ(spdown->sp_drain, 7 * 7 / 2 + 17); // 7d6+17 average = 41

    const auto* armor = registry.get(spell_id{66});
    ASSERT_NE(armor, nullptr);
    EXPECT_EQ(armor->type, magic_type::armor_break);
    EXPECT_TRUE(armor->is_offensive);

    const auto* ice_line = registry.get(spell_id{91});
    ASSERT_NE(ice_line, nullptr);
    EXPECT_EQ(ice_line->type, magic_type::ice_linear);
    EXPECT_TRUE(ice_line->is_offensive);
}

TEST_F(registry_test, magic_registry_utility_and_stamina_types)
{
    auto path = create_test_file("magic.yaml",
                                 "magic:\n"
                                 "  - id: 2\n"
                                 "    name: Create-Food\n"
                                 "    type: 10\n"
                                 "    mana_cost: 18\n"
                                 "    int_req: 18\n"
                                 "  - id: 11\n"
                                 "    name: Staminar-Drain\n"
                                 "    type: 5\n"
                                 "    mana_cost: 14\n"
                                 "    range2: 3\n"
                                 "    effect1: { dice: 4, sides: 6, bonus: 10 }\n"
                                 "    int_req: 22\n"
                                 "  - id: 23\n"
                                 "    name: Staminar-Recovery\n"
                                 "    type: 7\n"
                                 "    mana_cost: 20\n"
                                 "    range2: 3\n"
                                 "    effect1: { dice: 4, sides: 8, bonus: 8 }\n"
                                 "    int_req: 20\n"
                                 "  - id: 26\n"
                                 "    name: Possession\n"
                                 "    type: 15\n"
                                 "    mana_cost: 25\n"
                                 "    int_req: 26\n"
                                 "  - id: 38\n"
                                 "    name: Tremor\n"
                                 "    type: 22\n"
                                 "    mana_cost: 34\n"
                                 "    range2: 2\n"
                                 "    effect1: { dice: 3, sides: 4, bonus: 3 }\n"
                                 "    int_req: 33\n");

    magic_registry registry;
    registry.initialize();

    auto result = registry.load_from_file(path);
    ASSERT_TRUE(result.is_ok()) << result.error();
    EXPECT_EQ(result.value(), 5);

    const auto* create = registry.get(spell_id{2});
    ASSERT_NE(create, nullptr);
    EXPECT_EQ(create->type, magic_type::create);
    EXPECT_FALSE(create->is_offensive);

    const auto* drain = registry.get(spell_id{11});
    ASSERT_NE(drain, nullptr);
    EXPECT_EQ(drain->type, magic_type::sp_down_area);
    EXPECT_TRUE(drain->is_offensive);
    EXPECT_EQ(drain->base_damage, 4 * 7 / 2 + 10); // 4d6+10 average = 24

    const auto* recov = registry.get(spell_id{23});
    ASSERT_NE(recov, nullptr);
    EXPECT_EQ(recov->type, magic_type::sp_up_area);
    EXPECT_FALSE(recov->is_offensive);
    EXPECT_TRUE(recov->can_hit_ally);

    const auto* possession = registry.get(spell_id{26});
    ASSERT_NE(possession, nullptr);
    EXPECT_EQ(possession->type, magic_type::possession);
    EXPECT_FALSE(possession->is_offensive);

    const auto* tremor = registry.get(spell_id{38});
    ASSERT_NE(tremor, nullptr);
    EXPECT_EQ(tremor->type, magic_type::tremor);
    EXPECT_TRUE(tremor->is_offensive);
}

TEST_F(registry_test, magic_registry_damage_calculation)
{
    auto path = create_test_file("magic.yaml",
                                 "magic:\n"
                                 "  - id: 1\n"
                                 "    name: TestSpell\n"
                                 "    type: 1\n"
                                 "    mana_cost: 10\n"
                                 "    delay: 500\n"
                                 "    range1: 8\n"
                                 "    effect1: { dice: 10, sides: 19, bonus: 0 }\n"
                                 "    int_req: 15\n");

    magic_registry registry;
    registry.initialize();
    registry.load_from_file(path);

    auto* spell = registry.get(spell_id{1});
    ASSERT_NE(spell, nullptr);

    // base_damage = dice * (sides+1)/2 + bonus = 10 * (19+1)/2 + 0 = 100
    EXPECT_EQ(spell->base_damage, 100);
}

TEST_F(registry_test, magic_registry_parses_range2_as_area_radius)
{
    auto path = create_test_file("magic.yaml",
                                 "magic:\n"
                                 "  - id: 1\n"
                                 "    name: FireBall\n"
                                 "    type: 3\n"
                                 "    mana_cost: 27\n"
                                 "    range1: 2\n"
                                 "    range2: 4\n"
                                 "    effect1: { dice: 2, sides: 6, bonus: 2 }\n"
                                 "    int_req: 26\n");

    magic_registry registry;
    registry.initialize();
    registry.load_from_file(path);

    auto* spell = registry.get(spell_id{1});
    ASSERT_NE(spell, nullptr);
    EXPECT_EQ(spell->range, 2);
    EXPECT_EQ(spell->area_radius, 4);
}

TEST_F(registry_test, magic_registry_area_radius_defaults_to_zero)
{
    auto path = create_test_file("magic.yaml",
                                 "magic:\n"
                                 "  - id: 1\n"
                                 "    name: MagicMissile\n"
                                 "    type: 1\n"
                                 "    mana_cost: 8\n"
                                 "    range1: 1\n"
                                 "    effect1: { dice: 1, sides: 8, bonus: 0 }\n"
                                 "    int_req: 18\n");

    magic_registry registry;
    registry.initialize();
    registry.load_from_file(path);

    auto* spell = registry.get(spell_id{1});
    ASSERT_NE(spell, nullptr);
    EXPECT_EQ(spell->area_radius, 0);
}
