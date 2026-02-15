// test_shop_registry.cpp
// Unit tests for shop registry YAML loading

#include "registry/shop_registry.h"

#include <gtest/gtest.h>
#include <fstream>
#include <filesystem>

namespace hb
{

class shop_registry_test : public ::testing::Test
{
protected:
    void SetUp() override { registry_.initialize(); }

    void TearDown() override
    {
        registry_.shutdown();
        // Clean up temp file
        if (std::filesystem::exists(temp_path_))
        {
            std::filesystem::remove(temp_path_);
        }
    }

    void write_yaml(const std::string& content)
    {
        std::ofstream f(temp_path_);
        f << content;
        f.close();
    }

    shop_registry registry_;
    std::filesystem::path temp_path_{"test_shops_tmp.yaml"};
};

TEST_F(shop_registry_test, load_valid_yaml)
{
    write_yaml(R"(
shops:
  TestShop:
    type: general
    buy_categories: [10, 11, 30]
    repair_categories: [10, 11]
    items:
      - { item_id: 1, count: 5 }
      - { item_id: 2, count: 1 }
  TestBlacksmith:
    type: blacksmith
    buy_categories: [1, 2, 3]
    repair_categories: [1, 2, 3]
    items:
      - { item_id: 100 }
)");

    auto result = registry_.load_from_file(temp_path_);
    ASSERT_TRUE(result.is_ok());
    EXPECT_EQ(result.value(), 2u);
    EXPECT_EQ(registry_.count(), 2u);
}

TEST_F(shop_registry_test, get_shop_by_name)
{
    write_yaml(R"(
shops:
  MyShop:
    type: general
    buy_categories: [30]
    repair_categories: []
    items:
      - { item_id: 308, count: 1 }
)");

    auto result = registry_.load_from_file(temp_path_);
    ASSERT_TRUE(result.is_ok());

    auto* shop = registry_.get_shop("MyShop");
    ASSERT_NE(shop, nullptr);
    EXPECT_EQ(shop->npc_name, "MyShop");
    EXPECT_EQ(shop->type, npc::shop_type::general);
    EXPECT_EQ(shop->buy_categories.size(), 1u);
    EXPECT_EQ(shop->buy_categories[0], 30);
    EXPECT_EQ(shop->items.size(), 1u);
    EXPECT_EQ(shop->items[0].item.value, 308u);
    EXPECT_EQ(shop->items[0].default_count, 1);
}

TEST_F(shop_registry_test, get_missing_shop_returns_nullptr)
{
    write_yaml(R"(
shops:
  ExistingShop:
    type: general
    buy_categories: []
    repair_categories: []
    items: []
)");

    registry_.load_from_file(temp_path_);
    EXPECT_EQ(registry_.get_shop("NonExistent"), nullptr);
}

TEST_F(shop_registry_test, blacksmith_type)
{
    write_yaml(R"(
shops:
  Gandlf:
    type: blacksmith
    buy_categories: [1, 2]
    repair_categories: [1, 2]
    items:
      - { item_id: 1 }
)");

    registry_.load_from_file(temp_path_);
    auto* shop = registry_.get_shop("Gandlf");
    ASSERT_NE(shop, nullptr);
    EXPECT_EQ(shop->type, npc::shop_type::blacksmith);
}

TEST_F(shop_registry_test, invalid_yaml_returns_error)
{
    write_yaml("not: valid: yaml: [[[");
    auto result = registry_.load_from_file(temp_path_);
    EXPECT_TRUE(result.is_err());
}

TEST_F(shop_registry_test, missing_shops_section_returns_error)
{
    write_yaml("something_else: true");
    auto result = registry_.load_from_file(temp_path_);
    EXPECT_TRUE(result.is_err());
}

TEST_F(shop_registry_test, item_default_count)
{
    write_yaml(R"(
shops:
  TestShop:
    type: general
    buy_categories: []
    repair_categories: []
    items:
      - { item_id: 1 }
      - { item_id: 2, count: 10 }
)");

    registry_.load_from_file(temp_path_);
    auto* shop = registry_.get_shop("TestShop");
    ASSERT_NE(shop, nullptr);
    ASSERT_EQ(shop->items.size(), 2u);
    EXPECT_EQ(shop->items[0].default_count, 1); // default
    EXPECT_EQ(shop->items[1].default_count, 10);
}

} // namespace hb
