// test_mining_registry.cpp
// Unit tests for mining registry YAML loading

#include "registry/mining_registry.h"
#include "registry/item_registry.h"

#include <gtest/gtest.h>
#include <fstream>
#include <filesystem>

namespace hb
{

class mining_registry_test : public ::testing::Test
{
protected:
    void SetUp() override
    {
        test_dir_ = std::filesystem::temp_directory_path() / "hgserver_mining_registry_test";
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

    void load_items(const std::string& yaml_content)
    {
        auto path = write_file("items.yaml", yaml_content);
        items_.load_from_file(path);
    }

    mining_registry registry_;
    item_registry items_;
    std::filesystem::path test_dir_;
};

TEST_F(mining_registry_test, load_valid_yaml)
{
    auto path = write_file("mining.yaml", R"(
mineral_types:
  - type_id: 1
    name: "Iron Vein"
    difficulty: 10
    max_hits: 20
    visual_type: 1
    drops:
      - { item: Coal, weight: 60 }
)");

    auto result = registry_.load_from_file(path, items_);
    ASSERT_TRUE(result.is_ok());
    EXPECT_EQ(result.value(), 1u);
    EXPECT_EQ(registry_.count(), 1u);
}

TEST_F(mining_registry_test, parses_all_fields)
{
    load_items("items:\n  - {id: 42, name: Coal, type: 13, equip_pos: 8, weight: 100, price: 500}\n");

    auto path = write_file("mining.yaml", R"(
mineral_types:
  - type_id: 3
    name: "Rich Iron Vein"
    difficulty: 20
    max_hits: 10
    visual_type: 2
    drops:
      - { item: Coal, weight: 80, min_skill: 5 }
)");

    registry_.load_from_file(path, items_);
    auto* config = registry_.get_type(3);
    ASSERT_NE(config, nullptr);
    EXPECT_EQ(config->type_id, 3);
    EXPECT_EQ(config->name, "Rich Iron Vein");
    EXPECT_EQ(config->difficulty, 20);
    EXPECT_EQ(config->max_hits, 10);
    EXPECT_EQ(config->visual_type, 2);
    ASSERT_EQ(config->drops.size(), 1u);
    EXPECT_EQ(config->drops[0].item_name, "Coal");
    EXPECT_EQ(config->drops[0].template_id, 42);
    EXPECT_EQ(config->drops[0].weight, 80);
    EXPECT_EQ(config->drops[0].min_skill, 5);
}

TEST_F(mining_registry_test, resolves_item_name_to_template_id)
{
    load_items("items:\n  - {id: 99, name: IronOre, type: 13, equip_pos: 8, weight: 100, price: 500}\n");

    auto path = write_file("mining.yaml", R"(
mineral_types:
  - type_id: 1
    name: "Test"
    difficulty: 10
    max_hits: 5
    visual_type: 1
    drops:
      - { item: IronOre, weight: 100 }
)");

    registry_.load_from_file(path, items_);
    auto* config = registry_.get_type(1);
    ASSERT_NE(config, nullptr);
    ASSERT_EQ(config->drops.size(), 1u);
    EXPECT_EQ(config->drops[0].template_id, 99);
}

TEST_F(mining_registry_test, unresolved_item_gets_zero_template_id)
{
    auto path = write_file("mining.yaml", R"(
mineral_types:
  - type_id: 1
    name: "Test"
    difficulty: 10
    max_hits: 5
    visual_type: 1
    drops:
      - { item: NonExistentItem, weight: 100 }
)");

    auto result = registry_.load_from_file(path, items_);
    ASSERT_TRUE(result.is_ok());
    auto* config = registry_.get_type(1);
    ASSERT_NE(config, nullptr);
    ASSERT_EQ(config->drops.size(), 1u);
    EXPECT_EQ(config->drops[0].template_id, 0);
}

TEST_F(mining_registry_test, get_type_not_found_returns_nullptr)
{
    auto path = write_file("mining.yaml", R"(
mineral_types:
  - type_id: 1
    name: "Test"
    difficulty: 10
    max_hits: 5
    visual_type: 1
    drops: []
)");

    registry_.load_from_file(path, items_);
    EXPECT_EQ(registry_.get_type(99), nullptr);
}

TEST_F(mining_registry_test, get_all_returns_all_types)
{
    auto path = write_file("mining.yaml", R"(
mineral_types:
  - type_id: 1
    name: "Type A"
    difficulty: 10
    max_hits: 5
    visual_type: 1
    drops: []
  - type_id: 2
    name: "Type B"
    difficulty: 20
    max_hits: 10
    visual_type: 2
    drops: []
)");

    registry_.load_from_file(path, items_);
    EXPECT_EQ(registry_.count(), 2u);
    EXPECT_EQ(registry_.get_all().size(), 2u);
    EXPECT_EQ(registry_.get_all()[0].name, "Type A");
    EXPECT_EQ(registry_.get_all()[1].name, "Type B");
}

TEST_F(mining_registry_test, invalid_yaml_returns_error)
{
    auto path = write_file("mining.yaml", "not: valid: yaml: [[[");
    auto result = registry_.load_from_file(path, items_);
    EXPECT_TRUE(result.is_err());
}

TEST_F(mining_registry_test, missing_section_returns_error)
{
    auto path = write_file("mining.yaml", "something_else: true");
    auto result = registry_.load_from_file(path, items_);
    EXPECT_TRUE(result.is_err());
}

TEST_F(mining_registry_test, drop_default_values)
{
    auto path = write_file("mining.yaml", R"(
mineral_types:
  - type_id: 1
    name: "Test"
    difficulty: 10
    max_hits: 5
    visual_type: 1
    drops:
      - { item: Coal }
)");

    registry_.load_from_file(path, items_);
    auto* config = registry_.get_type(1);
    ASSERT_NE(config, nullptr);
    ASSERT_EQ(config->drops.size(), 1u);
    EXPECT_EQ(config->drops[0].min_skill, 0);
    EXPECT_EQ(config->drops[0].weight, 100);
}

} // namespace hb
