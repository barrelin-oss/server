// test_build_recipe_registry.cpp
// Unit tests for build recipe registry YAML loading

#include "registry/build_recipe_registry.h"
#include "registry/item_registry.h"

#include <gtest/gtest.h>
#include <fstream>
#include <filesystem>

namespace hb {

class build_recipe_registry_test : public ::testing::Test {
protected:
    void SetUp() override
    {
        test_dir_ = std::filesystem::temp_directory_path() / "hgserver_build_recipe_test";
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

    build_recipe_registry registry_;
    item_registry items_;
    std::filesystem::path test_dir_;
};

TEST_F(build_recipe_registry_test, load_valid_yaml)
{
    // Load item so name resolution works
    load_items("1\tSword\t13\t8\t100\t500\t10\t1\t6\t2\t0\t10\t5\t0\t0\n");

    auto path = write_file("recipes.yaml", R"(
build_recipes:
  - result: Sword
    skill_req: 10
    skill_limit: 50
    success_rate: 60
    ingredients:
      - { item_id: 500, count: 1 }
      - { item_id: 501, count: 2 }
)");

    auto result = registry_.load_from_file(path, items_);
    ASSERT_TRUE(result.is_ok());
    EXPECT_EQ(result.value(), 1u);
    EXPECT_EQ(registry_.count(), 1u);
}

TEST_F(build_recipe_registry_test, auto_assigns_sequential_ids)
{
    load_items(
        "1\tSword\t13\t8\t100\t500\t10\t1\t6\t2\t0\t10\t5\t0\t0\n"
        "2\tShield\t14\t7\t50\t300\t5\t0\t0\t0\t15\t8\t0\t0\t0\n"
    );

    auto path = write_file("recipes.yaml", R"(
build_recipes:
  - result: Sword
    skill_req: 0
    success_rate: 50
    ingredients: []
  - result: Shield
    skill_req: 10
    success_rate: 40
    ingredients: []
)");

    registry_.load_from_file(path, items_);
    ASSERT_EQ(registry_.count(), 2u);

    auto* r0 = registry_.get(0);
    auto* r1 = registry_.get(1);
    ASSERT_NE(r0, nullptr);
    ASSERT_NE(r1, nullptr);
    EXPECT_EQ(r0->id, 0);
    EXPECT_EQ(r1->id, 1);
    EXPECT_EQ(r0->result, "Sword");
    EXPECT_EQ(r1->result, "Shield");
}

TEST_F(build_recipe_registry_test, parses_all_fields)
{
    load_items("42\tTestBlade\t13\t8\t100\t500\t10\t1\t6\t2\t0\t10\t5\t0\t0\n");

    auto path = write_file("recipes.yaml", R"(
build_recipes:
  - result: TestBlade
    skill_req: 20
    skill_limit: 80
    success_rate: 45
    ingredients:
      - { item_id: 100, count: 3 }
      - { item_id: 200, count: 1 }
)");

    registry_.load_from_file(path, items_);
    auto* recipe = registry_.get(0);
    ASSERT_NE(recipe, nullptr);
    EXPECT_EQ(recipe->result, "TestBlade");
    EXPECT_EQ(recipe->result_template_id, 42);
    EXPECT_EQ(recipe->skill_req, 20);
    EXPECT_EQ(recipe->skill_limit, 80);
    EXPECT_EQ(recipe->success_rate, 45);
    ASSERT_EQ(recipe->ingredients.size(), 2u);
    EXPECT_EQ(recipe->ingredients[0].item_id, 100);
    EXPECT_EQ(recipe->ingredients[0].count, 3);
    EXPECT_EQ(recipe->ingredients[1].item_id, 200);
    EXPECT_EQ(recipe->ingredients[1].count, 1);
}

TEST_F(build_recipe_registry_test, resolves_item_name_to_template_id)
{
    load_items("99\tMagicAxe\t13\t8\t100\t500\t10\t1\t6\t2\t0\t10\t5\t0\t0\n");

    auto path = write_file("recipes.yaml", R"(
build_recipes:
  - result: MagicAxe
    skill_req: 0
    success_rate: 50
    ingredients: []
)");

    registry_.load_from_file(path, items_);
    auto* recipe = registry_.get(0);
    ASSERT_NE(recipe, nullptr);
    EXPECT_EQ(recipe->result_template_id, 99);
}

TEST_F(build_recipe_registry_test, unresolved_item_gets_zero_template_id)
{
    // No items loaded - name won't resolve
    auto path = write_file("recipes.yaml", R"(
build_recipes:
  - result: NonExistentItem
    skill_req: 0
    success_rate: 50
    ingredients: []
)");

    auto result = registry_.load_from_file(path, items_);
    ASSERT_TRUE(result.is_ok());  // Loading succeeds with warning
    auto* recipe = registry_.get(0);
    ASSERT_NE(recipe, nullptr);
    EXPECT_EQ(recipe->result_template_id, 0);
}

TEST_F(build_recipe_registry_test, find_by_result_name)
{
    load_items(
        "1\tSword\t13\t8\t100\t500\t10\t1\t6\t2\t0\t10\t5\t0\t0\n"
        "2\tShield\t14\t7\t50\t300\t5\t0\t0\t0\t15\t8\t0\t0\t0\n"
    );

    auto path = write_file("recipes.yaml", R"(
build_recipes:
  - result: Sword
    skill_req: 0
    success_rate: 50
    ingredients: []
  - result: Shield
    skill_req: 10
    success_rate: 40
    ingredients: []
)");

    registry_.load_from_file(path, items_);
    auto* recipe = registry_.find_by_result("Shield");
    ASSERT_NE(recipe, nullptr);
    EXPECT_EQ(recipe->result, "Shield");
    EXPECT_EQ(recipe->id, 1);
}

TEST_F(build_recipe_registry_test, find_missing_returns_nullptr)
{
    auto path = write_file("recipes.yaml", R"(
build_recipes:
  - result: Sword
    skill_req: 0
    success_rate: 50
    ingredients: []
)");

    registry_.load_from_file(path, items_);
    EXPECT_EQ(registry_.find_by_result("NonExistent"), nullptr);
}

TEST_F(build_recipe_registry_test, get_out_of_range_returns_nullptr)
{
    auto path = write_file("recipes.yaml", R"(
build_recipes:
  - result: Sword
    skill_req: 0
    success_rate: 50
    ingredients: []
)");

    registry_.load_from_file(path, items_);
    EXPECT_EQ(registry_.get(-1), nullptr);
    EXPECT_EQ(registry_.get(1), nullptr);
    EXPECT_EQ(registry_.get(100), nullptr);
}

TEST_F(build_recipe_registry_test, invalid_yaml_returns_error)
{
    auto path = write_file("recipes.yaml", "not: valid: yaml: [[[");
    auto result = registry_.load_from_file(path, items_);
    EXPECT_TRUE(result.is_err());
}

TEST_F(build_recipe_registry_test, missing_section_returns_error)
{
    auto path = write_file("recipes.yaml", "something_else: true");
    auto result = registry_.load_from_file(path, items_);
    EXPECT_TRUE(result.is_err());
}

TEST_F(build_recipe_registry_test, ingredient_default_count)
{
    auto path = write_file("recipes.yaml", R"(
build_recipes:
  - result: Test
    skill_req: 0
    success_rate: 50
    ingredients:
      - { item_id: 100 }
)");

    registry_.load_from_file(path, items_);
    auto* recipe = registry_.get(0);
    ASSERT_NE(recipe, nullptr);
    ASSERT_EQ(recipe->ingredients.size(), 1u);
    EXPECT_EQ(recipe->ingredients[0].count, 1);  // default count
}

}  // namespace hb
