// test_craft_recipe_registry.cpp
// Unit tests for craft recipe registry YAML loading

#include "registry/craft_recipe_registry.h"
#include "registry/item_registry.h"

#include <gtest/gtest.h>
#include <fstream>
#include <filesystem>

namespace hb
{

class craft_recipe_registry_test : public ::testing::Test
{
protected:
    void SetUp() override
    {
        test_dir_ = std::filesystem::temp_directory_path() / "hgserver_craft_recipe_test";
        std::filesystem::create_directories(test_dir_);
        registry_.initialize();
        items_.initialize();
    }

    void TearDown() override
    {
        registry_.shutdown();
        items_.shutdown();
        std::filesystem::remove_all(test_dir_);
    }

    auto write_file(const std::string& name, const std::string& content) -> std::filesystem::path
    {
        auto path = test_dir_ / name;
        std::ofstream f(path);
        f << content;
        return path;
    }

    void load_items(const std::string& content)
    {
        auto path = write_file("items.cfg", content);
        items_.load_from_file(path);
    }

    craft_recipe_registry registry_;
    item_registry items_;
    std::filesystem::path test_dir_;
};

TEST_F(craft_recipe_registry_test, load_alchemy_recipes)
{
    load_items("10\tHealthPotion\t5\t0\t5\t25\t0\t0\t0\t0\t0\t0\t0\t0\t0\n");

    auto path = write_file("recipes.yaml", R"(
alchemy_recipes:
  - id: 1
    result: HealthPotion
    skill_limit: 0
    difficulty: 20
    ingredients:
      - { item_id: 500, count: 1 }
  - id: 2
    result: HealthPotion
    skill_limit: 10
    difficulty: 40
    ingredients:
      - { item_id: 500, count: 2 }
      - { item_id: 501, count: 1 }
)");

    auto result = registry_.load_alchemy(path, items_);
    ASSERT_TRUE(result.is_ok());
    EXPECT_EQ(result.value(), 2u);
    EXPECT_EQ(registry_.alchemy_count(), 2u);
}

TEST_F(craft_recipe_registry_test, load_crafting_recipes)
{
    load_items("20\tGem\t5\t0\t5\t100\t0\t0\t0\t0\t0\t0\t0\t0\t0\n");

    auto path = write_file("craft.yaml", R"(
crafting_recipes:
  - id: 1
    result: Gem
    skill_limit: 5
    difficulty: 30
    ingredients:
      - { item_id: 600, count: 3 }
)");

    auto result = registry_.load_crafting(path, items_);
    ASSERT_TRUE(result.is_ok());
    EXPECT_EQ(result.value(), 1u);
    EXPECT_EQ(registry_.crafting_count(), 1u);
}

TEST_F(craft_recipe_registry_test, parses_all_fields)
{
    load_items("42\tManaPotion\t5\t0\t5\t50\t0\t0\t0\t0\t0\t0\t0\t0\t0\n");

    auto path = write_file("recipes.yaml", R"(
alchemy_recipes:
  - id: 7
    result: ManaPotion
    skill_limit: 15
    difficulty: 35
    ingredients:
      - { item_id: 100, count: 2 }
      - { item_id: 200, count: 1 }
)");

    registry_.load_alchemy(path, items_);
    auto* recipe = registry_.get(7);
    ASSERT_NE(recipe, nullptr);
    EXPECT_EQ(recipe->id, 7);
    EXPECT_EQ(recipe->result, "ManaPotion");
    EXPECT_EQ(recipe->result_template_id, 42);
    EXPECT_EQ(recipe->skill_limit, 15);
    EXPECT_EQ(recipe->difficulty, 35);
    ASSERT_EQ(recipe->ingredients.size(), 2u);
    EXPECT_EQ(recipe->ingredients[0].item_id, 100);
    EXPECT_EQ(recipe->ingredients[0].count, 2);
}

TEST_F(craft_recipe_registry_test, lookup_by_id_across_categories)
{
    load_items("1\tPotion\t5\t0\t5\t25\t0\t0\t0\t0\t0\t0\t0\t0\t0\n"
               "2\tGem\t5\t0\t5\t100\t0\t0\t0\t0\t0\t0\t0\t0\t0\n");

    auto alchemy_path = write_file("recipes.yaml", R"(
alchemy_recipes:
  - id: 10
    result: Potion
    skill_limit: 0
    difficulty: 20
    ingredients: []
)");

    auto craft_path = write_file("craft.yaml", R"(
crafting_recipes:
  - id: 20
    result: Gem
    skill_limit: 5
    difficulty: 30
    ingredients: []
)");

    registry_.load_alchemy(alchemy_path, items_);
    registry_.load_crafting(craft_path, items_);

    auto* r10 = registry_.get(10);
    auto* r20 = registry_.get(20);
    ASSERT_NE(r10, nullptr);
    ASSERT_NE(r20, nullptr);
    EXPECT_EQ(r10->result, "Potion");
    EXPECT_EQ(r20->result, "Gem");
}

TEST_F(craft_recipe_registry_test, find_by_result_name)
{
    load_items("1\tSuperGem\t5\t0\t5\t100\t0\t0\t0\t0\t0\t0\t0\t0\t0\n");

    auto path = write_file("craft.yaml", R"(
crafting_recipes:
  - id: 5
    result: SuperGem
    skill_limit: 10
    difficulty: 50
    ingredients: []
)");

    registry_.load_crafting(path, items_);
    auto* recipe = registry_.find_by_result("SuperGem");
    ASSERT_NE(recipe, nullptr);
    EXPECT_EQ(recipe->id, 5);
}

TEST_F(craft_recipe_registry_test, find_missing_returns_nullptr)
{
    auto path = write_file("recipes.yaml", R"(
alchemy_recipes:
  - id: 1
    result: Potion
    skill_limit: 0
    difficulty: 10
    ingredients: []
)");

    registry_.load_alchemy(path, items_);
    EXPECT_EQ(registry_.get(999), nullptr);
    EXPECT_EQ(registry_.find_by_result("NonExistent"), nullptr);
}

TEST_F(craft_recipe_registry_test, invalid_yaml_returns_error)
{
    auto path = write_file("recipes.yaml", "not: valid: yaml: [[[");
    auto result = registry_.load_alchemy(path, items_);
    EXPECT_TRUE(result.is_err());
}

TEST_F(craft_recipe_registry_test, missing_section_returns_error)
{
    auto path = write_file("recipes.yaml", "something_else: true");
    auto result = registry_.load_alchemy(path, items_);
    EXPECT_TRUE(result.is_err());
}

TEST_F(craft_recipe_registry_test, unresolved_item_gets_zero_template_id)
{
    auto path = write_file("recipes.yaml", R"(
alchemy_recipes:
  - id: 1
    result: MissingItem
    skill_limit: 0
    difficulty: 10
    ingredients: []
)");

    auto result = registry_.load_alchemy(path, items_);
    ASSERT_TRUE(result.is_ok());
    auto* recipe = registry_.get(1);
    ASSERT_NE(recipe, nullptr);
    EXPECT_EQ(recipe->result_template_id, 0);
}

} // namespace hb
