// test_npc_special_ability.cpp
// Tests for NPC special ability pool system

#include <gtest/gtest.h>
#include <fstream>
#include <set>
#include <filesystem>
#include <cstdlib>

#include "npc/special_ability.h"
#include "npc/npc.h"

namespace sa = hb::npc;

// ============================================================================
// SA config loading
// ============================================================================

namespace
{

auto write_temp_yaml(const std::string& content) -> std::filesystem::path
{
    auto path = std::filesystem::temp_directory_path() / "test_sa_config.yaml";
    std::ofstream f(path);
    f << content;
    return path;
}

} // namespace

TEST(sa_config_test, load_valid_config)
{
    auto path = write_temp_yaml(R"(
abilities:
  - { id: 1, name: "Clairvoyant" }
  - { id: 2, name: "DMP" }
  - { id: 3, name: "Anti-Physical" }

pools:
  - { id: 1, abilities: [1, 2] }
  - { id: 2, abilities: [1, 2, 3] }
)");

    auto result = sa::load_sa_config(path);
    ASSERT_TRUE(result.is_ok());

    const auto& config = result.value();
    EXPECT_EQ(config.abilities.size(), 3u);
    EXPECT_EQ(config.pools.size(), 2u);

    EXPECT_EQ(config.abilities[0].id, 1);
    EXPECT_EQ(config.abilities[0].name, "Clairvoyant");

    auto* pool = config.get_pool(1);
    ASSERT_NE(pool, nullptr);
    EXPECT_EQ(pool->abilities.size(), 2u);
    EXPECT_EQ(pool->abilities[0], 1);
    EXPECT_EQ(pool->abilities[1], 2);

    auto* pool2 = config.get_pool(2);
    ASSERT_NE(pool2, nullptr);
    EXPECT_EQ(pool2->abilities.size(), 3u);

    std::filesystem::remove(path);
}

TEST(sa_config_test, load_missing_file)
{
    auto result = sa::load_sa_config("/nonexistent/path.yaml");
    ASSERT_TRUE(result.is_err());
}

TEST(sa_config_test, get_pool_returns_null_for_missing)
{
    sa::sa_config config;
    EXPECT_EQ(config.get_pool(99), nullptr);
}

TEST(sa_config_test, get_ability_name_returns_empty_for_missing)
{
    sa::sa_config config;
    EXPECT_TRUE(config.get_ability_name(99).empty());
}

TEST(sa_config_test, get_ability_name_returns_correct_name)
{
    sa::sa_config config;
    config.abilities.push_back({1, "Clairvoyant"});
    config.abilities.push_back({5, "Poisonous"});

    EXPECT_EQ(config.get_ability_name(1), "Clairvoyant");
    EXPECT_EQ(config.get_ability_name(5), "Poisonous");
    EXPECT_TRUE(config.get_ability_name(3).empty());
}

// ============================================================================
// Pool rolling
// ============================================================================

TEST(sa_pool_roll_test, roll_from_empty_config_returns_0)
{
    sa::sa_config config;
    EXPECT_EQ(sa::roll_from_pool(config, 1), 0);
}

TEST(sa_pool_roll_test, roll_from_missing_pool_returns_0)
{
    sa::sa_config config;
    sa::sa_pool_def pool{1, {3, 4}};
    config.pools[1] = pool;

    EXPECT_EQ(sa::roll_from_pool(config, 99), 0);
}

TEST(sa_pool_roll_test, roll_from_empty_pool_returns_0)
{
    sa::sa_config config;
    sa::sa_pool_def pool{1, {}};
    config.pools[1] = pool;

    EXPECT_EQ(sa::roll_from_pool(config, 1), 0);
}

TEST(sa_pool_roll_test, roll_from_single_element_pool)
{
    sa::sa_config config;
    sa::sa_pool_def pool{1, {5}};
    config.pools[1] = pool;

    for (int i = 0; i < 10; ++i)
    {
        EXPECT_EQ(sa::roll_from_pool(config, 1), 5);
    }
}

TEST(sa_pool_roll_test, pool_1_returns_3_or_4)
{
    sa::sa_config config;
    config.pools[1] = sa::sa_pool_def{1, {3, 4}};

    std::set<int8_t> seen;
    for (int i = 0; i < 200; ++i)
    {
        auto sa = sa::roll_from_pool(config, 1);
        EXPECT_TRUE(sa == 3 || sa == 4) << "pool 1 returned " << static_cast<int>(sa);
        seen.insert(sa);
    }
    EXPECT_EQ(seen.size(), 2u);
}

TEST(sa_pool_roll_test, pool_with_5_elements_returns_all)
{
    sa::sa_config config;
    config.pools[8] = sa::sa_pool_def{8, {1, 2, 3, 4, 8}};

    std::set<int8_t> seen;
    for (int i = 0; i < 500; ++i)
    {
        auto sa = sa::roll_from_pool(config, 8);
        EXPECT_TRUE(sa == 1 || sa == 2 || sa == 3 || sa == 4 || sa == 8)
            << "pool 8 returned " << static_cast<int>(sa);
        seen.insert(sa);
    }
    EXPECT_EQ(seen.size(), 5u);
}

// ============================================================================
// apply_special_ability stat modifications
// ============================================================================

TEST(sa_apply_test, zero_sa_does_nothing)
{
    sa::npc n;
    n.exp_reward = 100;
    n.abs_damage = 0;
    sa::apply_special_ability(n, 0);
    EXPECT_EQ(n.special_ability, 0);
    EXPECT_EQ(n.exp_reward, 100);
}

TEST(sa_apply_test, clairvoyant_adds_25_percent_exp)
{
    sa::npc n;
    n.exp_reward = 100;
    sa::apply_special_ability(n, 1);
    EXPECT_EQ(n.special_ability, 1);
    EXPECT_EQ(n.exp_reward, 125);
}

TEST(sa_apply_test, dmp_adds_30_percent_exp)
{
    sa::npc n;
    n.exp_reward = 100;
    sa::apply_special_ability(n, 2);
    EXPECT_EQ(n.special_ability, 2);
    EXPECT_EQ(n.exp_reward, 130);
}

TEST(sa_apply_test, anti_physical_sets_negative_abs_damage)
{
    sa::npc n;
    n.abs_damage = 0;
    n.exp_reward = 100;
    sa::apply_special_ability(n, 3);
    EXPECT_EQ(n.special_ability, 3);
    EXPECT_LT(n.abs_damage, 0);
    EXPECT_GE(n.abs_damage, -90);
    EXPECT_GT(n.exp_reward, 100);
}

TEST(sa_apply_test, anti_physical_cleared_if_positive_abs_damage)
{
    sa::npc n;
    n.abs_damage = 30;
    n.exp_reward = 100;
    sa::apply_special_ability(n, 3);
    EXPECT_EQ(n.special_ability, 0);
    EXPECT_EQ(n.abs_damage, 30);
    EXPECT_EQ(n.exp_reward, 100);
}

TEST(sa_apply_test, anti_magic_sets_positive_abs_damage)
{
    sa::npc n;
    n.abs_damage = 0;
    n.exp_reward = 100;
    sa::apply_special_ability(n, 4);
    EXPECT_EQ(n.special_ability, 4);
    EXPECT_GT(n.abs_damage, 0);
    EXPECT_LE(n.abs_damage, 90);
    EXPECT_GT(n.exp_reward, 100);
}

TEST(sa_apply_test, anti_magic_cleared_if_negative_abs_damage)
{
    sa::npc n;
    n.abs_damage = -30;
    n.exp_reward = 100;
    sa::apply_special_ability(n, 4);
    EXPECT_EQ(n.special_ability, 0);
    EXPECT_EQ(n.abs_damage, -30);
    EXPECT_EQ(n.exp_reward, 100);
}

TEST(sa_apply_test, poisonous_adds_15_percent_exp)
{
    sa::npc n;
    n.exp_reward = 100;
    sa::apply_special_ability(n, 5);
    EXPECT_EQ(n.special_ability, 5);
    EXPECT_EQ(n.exp_reward, 115);
}

TEST(sa_apply_test, critical_poisonous_no_exp_change)
{
    sa::npc n;
    n.exp_reward = 100;
    sa::apply_special_ability(n, 6);
    EXPECT_EQ(n.special_ability, 6);
    EXPECT_EQ(n.exp_reward, 100);
}

TEST(sa_apply_test, explosive_adds_20_percent_exp)
{
    sa::npc n;
    n.exp_reward = 100;
    sa::apply_special_ability(n, 7);
    EXPECT_EQ(n.special_ability, 7);
    EXPECT_EQ(n.exp_reward, 120);
}

TEST(sa_apply_test, hi_explosive_adds_25_percent_exp)
{
    sa::npc n;
    n.exp_reward = 100;
    sa::apply_special_ability(n, 8);
    EXPECT_EQ(n.special_ability, 8);
    EXPECT_EQ(n.exp_reward, 125);
}

TEST(sa_apply_test, beholder_always_clairvoyant)
{
    sa::npc n;
    n.sprite_id = 53;
    n.exp_reward = 100;
    sa::apply_special_ability(n, 5); // Tried to assign poisonous
    EXPECT_EQ(n.special_ability, 1); // Overridden to clairvoyant
    EXPECT_EQ(n.exp_reward, 125);    // +25% for clairvoyant
}

TEST(sa_apply_test, invalid_sa_sets_zero)
{
    sa::npc n;
    n.exp_reward = 100;
    sa::apply_special_ability(n, 99);
    EXPECT_EQ(n.special_ability, 0);
    EXPECT_EQ(n.exp_reward, 100);
}

TEST(sa_apply_test, anti_physical_caps_at_negative_90)
{
    sa::npc n;
    n.abs_damage = -70; // Already has some physical absorption
    n.exp_reward = 100;
    sa::apply_special_ability(n, 3);
    EXPECT_EQ(n.special_ability, 3);
    EXPECT_GE(n.abs_damage, -90); // Should not go below -90
}

TEST(sa_apply_test, anti_magic_caps_at_positive_90)
{
    sa::npc n;
    n.abs_damage = 70; // Already has some magic absorption
    n.exp_reward = 100;

    // abs_damage is positive, so Anti-Physical would be cleared (mutual exclusion)
    // But Anti-Magic should work since abs_damage >= 0
    sa::apply_special_ability(n, 4);
    EXPECT_EQ(n.special_ability, 4);
    EXPECT_LE(n.abs_damage, 90);
}

// ============================================================================
// Full config file loading (actual YAML)
// ============================================================================

TEST(sa_config_test, load_full_legacy_config)
{
    auto path = write_temp_yaml(R"(
abilities:
  - { id: 1, name: "Clairvoyant" }
  - { id: 2, name: "Destructive Magic Protection" }
  - { id: 3, name: "Anti-Physical" }
  - { id: 4, name: "Anti-Magic" }
  - { id: 5, name: "Poisonous" }
  - { id: 6, name: "Critical Poisonous" }
  - { id: 7, name: "Explosive" }
  - { id: 8, name: "Hi-Explosive" }

pools:
  - { id: 1, abilities: [3, 4] }
  - { id: 2, abilities: [3, 4, 5] }
  - { id: 3, abilities: [3, 4, 5, 6] }
  - { id: 4, abilities: [3, 4, 7] }
  - { id: 5, abilities: [3, 4, 7, 8] }
  - { id: 6, abilities: [3, 4, 5] }
  - { id: 7, abilities: [1, 2, 4] }
  - { id: 8, abilities: [1, 2, 3, 4, 8] }
  - { id: 9, abilities: [1, 2, 3, 4, 5, 6, 7, 8] }
)");

    auto result = sa::load_sa_config(path);
    ASSERT_TRUE(result.is_ok());

    const auto& config = result.value();
    EXPECT_EQ(config.abilities.size(), 8u);
    EXPECT_EQ(config.pools.size(), 9u);

    // Verify pool 8 (most complex)
    auto* pool8 = config.get_pool(8);
    ASSERT_NE(pool8, nullptr);
    EXPECT_EQ(pool8->abilities.size(), 5u);

    // Verify pool 9 (all abilities)
    auto* pool9 = config.get_pool(9);
    ASSERT_NE(pool9, nullptr);
    EXPECT_EQ(pool9->abilities.size(), 8u);

    // Verify ability names
    EXPECT_EQ(config.get_ability_name(1), "Clairvoyant");
    EXPECT_EQ(config.get_ability_name(8), "Hi-Explosive");

    std::filesystem::remove(path);
}
