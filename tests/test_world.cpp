// test_world.cpp
// Unit tests for world subsystem

#include <gtest/gtest.h>
#include <thread>
#include "world/position.h"
#include "world/tile.h"
#include "world/spatial_index.h"
#include "world/map.h"
#include "world/world_subsystem.h"

using namespace hb::world;
using hb::entity_id;
using hb::map_id;

namespace
{

auto find_map_file(const char* filename) -> std::filesystem::path
{
    for (auto prefix : {"bin/mapdata/", "mapdata/", "Debug/mapdata/", "../mapdata/", "../bin/mapdata/"})
    {
        auto p = std::filesystem::path(prefix) / filename;
        if (std::filesystem::exists(p))
            return p;
    }
    return {};
}

} // namespace

// Position tests

TEST(position_test, construction)
{
    position p1;
    EXPECT_EQ(p1.x, 0);
    EXPECT_EQ(p1.y, 0);

    position p2{10, 20};
    EXPECT_EQ(p2.x, 10);
    EXPECT_EQ(p2.y, 20);
}

TEST(position_test, equality)
{
    position p1{10, 20};
    position p2{10, 20};
    position p3{10, 21};

    EXPECT_EQ(p1, p2);
    EXPECT_NE(p1, p3);
}

TEST(position_test, addition)
{
    position p1{10, 20};
    position p2{5, -3};

    auto result = p1 + p2;
    EXPECT_EQ(result.x, 15);
    EXPECT_EQ(result.y, 17);
}

TEST(position_test, manhattan_distance)
{
    position p1{0, 0};
    position p2{3, 4};

    EXPECT_EQ(p1.manhattan_distance(p2), 7);
}

TEST(position_test, euclidean_distance_squared)
{
    position p1{0, 0};
    position p2{3, 4};

    EXPECT_EQ(p1.distance_squared(p2), 25); // 3^2 + 4^2
}

TEST(position_test, chebyshev_distance)
{
    position p1{0, 0};
    position p2{3, 5};

    EXPECT_EQ(p1.chebyshev_distance(p2), 5); // max(3, 5)
}

// Direction tests

TEST(direction_test, direction_offset)
{
    auto north = direction_offset(direction::north);
    EXPECT_EQ(north.x, 0);
    EXPECT_EQ(north.y, -1);

    auto south_east = direction_offset(direction::south_east);
    EXPECT_EQ(south_east.x, 1);
    EXPECT_EQ(south_east.y, 1);
}

TEST(direction_test, move_in_direction)
{
    position p{10, 10};
    auto moved = move_in_direction(p, direction::north);
    EXPECT_EQ(moved.x, 10);
    EXPECT_EQ(moved.y, 9);
}

TEST(direction_test, direction_to)
{
    position from{10, 10};

    EXPECT_EQ(direction_to(from, position{10, 5}), direction::north);
    EXPECT_EQ(direction_to(from, position{15, 10}), direction::east);
    EXPECT_EQ(direction_to(from, position{15, 15}), direction::south_east);
    EXPECT_EQ(direction_to(from, position{10, 10}), std::nullopt);
}

// Rect tests

TEST(rect_test, contains)
{
    rect r{0, 0, 10, 10};

    EXPECT_TRUE(r.contains(position{5, 5}));
    EXPECT_TRUE(r.contains(position{0, 0}));
    EXPECT_TRUE(r.contains(position{10, 10}));
    EXPECT_FALSE(r.contains(position{11, 5}));
    EXPECT_FALSE(r.contains(position{-1, 5}));
}

TEST(rect_test, dimensions)
{
    rect r{5, 10, 15, 20};

    EXPECT_EQ(r.width(), 11);
    EXPECT_EQ(r.height(), 11);
}

TEST(rect_test, intersects)
{
    rect r1{0, 0, 10, 10};
    rect r2{5, 5, 15, 15};
    rect r3{20, 20, 30, 30};

    EXPECT_TRUE(r1.intersects(r2));
    EXPECT_FALSE(r1.intersects(r3));
}

// Tile tests

TEST(tile_test, static_tile_flags)
{
    static_tile tile;
    tile.flags = tile_flags::none;

    EXPECT_TRUE(tile.is_walkable());
    EXPECT_FALSE(tile.is_teleport());

    tile.flags = tile_flags::blocks_movement;
    EXPECT_FALSE(tile.is_walkable());

    tile.flags = tile_flags::is_teleport | tile_flags::is_water;
    EXPECT_TRUE(tile.is_teleport());
    EXPECT_TRUE(tile.is_water());
}

TEST(tile_test, dynamic_tile_occupant)
{
    dynamic_tile tile;

    EXPECT_FALSE(tile.has_occupant());

    tile.set_occupant(entity_id{42}, owner_type::player);
    EXPECT_TRUE(tile.has_occupant());
    EXPECT_EQ(tile.occupant.value, 42);
    EXPECT_EQ(tile.occupant_type, owner_type::player);

    tile.clear_occupant();
    EXPECT_FALSE(tile.has_occupant());
}

TEST(tile_test, dynamic_tile_dead_entity)
{
    dynamic_tile tile;

    EXPECT_FALSE(tile.has_dead_entity());

    tile.set_dead_entity(entity_id{100}, owner_type::npc);
    EXPECT_TRUE(tile.has_dead_entity());
    EXPECT_EQ(tile.dead_entity.value, 100);

    tile.clear_dead_entity();
    EXPECT_FALSE(tile.has_dead_entity());
}

// Spatial index tests

TEST(spatial_index_test, add_and_query)
{
    spatial_index index;
    index.initialize(100, 100);

    index.add(entity_id{1}, position{10, 10});
    index.add(entity_id{2}, position{15, 10});
    index.add(entity_id{3}, position{50, 50});

    EXPECT_TRUE(index.contains(entity_id{1}));
    EXPECT_EQ(index.count(), 3);
}

TEST(spatial_index_test, get_in_range)
{
    spatial_index index;
    index.initialize(100, 100);

    index.add(entity_id{1}, position{10, 10});
    index.add(entity_id{2}, position{15, 10});
    index.add(entity_id{3}, position{50, 50});

    auto nearby = index.get_in_range(position{10, 10}, 10);
    EXPECT_EQ(nearby.size(), 2); // Entities 1 and 2

    auto far = index.get_in_range(position{50, 50}, 5);
    EXPECT_EQ(far.size(), 1); // Only entity 3
}

TEST(spatial_index_test, update_position)
{
    spatial_index index;
    index.initialize(100, 100);

    index.add(entity_id{1}, position{10, 10});

    auto pos1 = index.get_position(entity_id{1});
    EXPECT_TRUE(pos1.has_value());
    EXPECT_EQ(pos1->x, 10);

    index.update(entity_id{1}, position{20, 20});

    auto pos2 = index.get_position(entity_id{1});
    EXPECT_TRUE(pos2.has_value());
    EXPECT_EQ(pos2->x, 20);
}

TEST(spatial_index_test, remove)
{
    spatial_index index;
    index.initialize(100, 100);

    index.add(entity_id{1}, position{10, 10});
    EXPECT_TRUE(index.contains(entity_id{1}));

    index.remove(entity_id{1});
    EXPECT_FALSE(index.contains(entity_id{1}));
}

TEST(spatial_index_test, get_in_rect)
{
    spatial_index index;
    index.initialize(100, 100);

    index.add(entity_id{1}, position{10, 10});
    index.add(entity_id{2}, position{15, 15});
    index.add(entity_id{3}, position{50, 50});

    auto in_rect = index.get_in_rect(rect{5, 5, 20, 20});
    EXPECT_EQ(in_rect.size(), 2); // Entities 1 and 2
}

// Map tests

TEST(map_test, initialization)
{
    map m;
    map_config config;
    config.name = "test_map";
    config.width = 100;
    config.height = 100;

    m.initialize(map_id{1}, config);

    EXPECT_EQ(m.id().value, 1);
    EXPECT_EQ(m.name(), "test_map");
    EXPECT_EQ(m.width(), 100);
    EXPECT_EQ(m.height(), 100);
}

TEST(map_test, valid_position)
{
    map m;
    map_config config;
    config.name = "test";
    config.width = 50;
    config.height = 50;
    m.initialize(map_id{1}, config);

    EXPECT_TRUE(m.is_valid_position(position{0, 0}));
    EXPECT_TRUE(m.is_valid_position(position{49, 49}));
    EXPECT_FALSE(m.is_valid_position(position{50, 0}));
    EXPECT_FALSE(m.is_valid_position(position{-1, 0}));
}

TEST(map_test, tile_access)
{
    map m;
    map_config config;
    config.name = "test";
    config.width = 50;
    config.height = 50;
    m.initialize(map_id{1}, config);

    auto* static_tile = m.get_static_tile(position{10, 10});
    ASSERT_NE(static_tile, nullptr);

    auto* dynamic_tile = m.get_dynamic_tile(position{10, 10});
    ASSERT_NE(dynamic_tile, nullptr);
}

TEST(map_test, occupancy)
{
    map m;
    map_config config;
    config.name = "test";
    config.width = 50;
    config.height = 50;
    m.initialize(map_id{1}, config);

    position pos{25, 25};

    EXPECT_FALSE(m.get_occupant(pos).has_value());

    m.set_occupant(pos, entity_id{42}, owner_type::player);

    auto occupant = m.get_occupant(pos);
    EXPECT_TRUE(occupant.has_value());
    EXPECT_EQ(occupant->value, 42);
    EXPECT_EQ(m.get_occupant_type(pos), owner_type::player);

    m.clear_occupant(pos);
    EXPECT_FALSE(m.get_occupant(pos).has_value());
}

TEST(map_test, teleport)
{
    map m;
    map_config config;
    config.name = "test";
    config.width = 50;
    config.height = 50;
    m.initialize(map_id{1}, config);

    position pos{10, 10};
    teleport_dest dest{"other_map", 20, 30, direction::south};

    EXPECT_FALSE(m.get_teleport_dest(pos).has_value());

    m.add_teleport(pos, dest);

    auto result = m.get_teleport_dest(pos);
    EXPECT_TRUE(result.has_value());
    EXPECT_EQ(result->dest_map, "other_map");
    EXPECT_EQ(result->dest_x, 20);
    EXPECT_EQ(result->dest_y, 30);
}

// World subsystem tests

class world_subsystem_test : public ::testing::Test
{
protected:
    void SetUp() override { world_.initialize(); }

    void TearDown() override { world_.shutdown(); }

    world_subsystem world_;
};

TEST_F(world_subsystem_test, lifecycle)
{
    EXPECT_TRUE(world_.is_initialized());
    EXPECT_EQ(world_.name(), "world");
}

TEST_F(world_subsystem_test, create_map)
{
    map_config config;
    config.name = "test_map";
    config.width = 100;
    config.height = 100;

    auto result = world_.create_map(config);
    ASSERT_TRUE(result.is_ok());

    auto id = result.value();
    EXPECT_TRUE(id.is_valid());
    EXPECT_EQ(world_.map_count(), 1);

    auto* m = world_.get_map(id);
    ASSERT_NE(m, nullptr);
    EXPECT_EQ(m->name(), "test_map");
}

TEST_F(world_subsystem_test, get_by_name)
{
    map_config config;
    config.name = "named_map";
    config.width = 50;
    config.height = 50;

    world_.create_map(config);

    auto* m = world_.get_map_by_name("named_map");
    ASSERT_NE(m, nullptr);
    EXPECT_EQ(m->name(), "named_map");

    EXPECT_EQ(world_.get_map_by_name("nonexistent"), nullptr);
}

TEST_F(world_subsystem_test, unload_map)
{
    map_config config;
    config.name = "temp_map";
    config.width = 50;
    config.height = 50;

    auto result = world_.create_map(config);
    auto id = result.value();

    EXPECT_TRUE(world_.map_exists(id));
    world_.unload_map(id);
    EXPECT_FALSE(world_.map_exists(id));
}

TEST_F(world_subsystem_test, duplicate_name_fails)
{
    map_config config;
    config.name = "unique_name";
    config.width = 50;
    config.height = 50;

    auto result1 = world_.create_map(config);
    EXPECT_TRUE(result1.is_ok());

    auto result2 = world_.create_map(config);
    EXPECT_TRUE(result2.is_err());
}

TEST_F(world_subsystem_test, is_walkable)
{
    map_config config;
    config.name = "walkable_test";
    config.width = 50;
    config.height = 50;

    auto result = world_.create_map(config);
    auto id = result.value();

    // Default tiles should be walkable
    EXPECT_TRUE(world_.is_walkable(id, position{10, 10}));

    // Invalid position should not be walkable
    EXPECT_FALSE(world_.is_walkable(id, position{100, 100}));

    // Invalid map should not be walkable
    EXPECT_FALSE(world_.is_walkable(map_id{999}, position{10, 10}));
}

// Map file loading tests

TEST(map_file_test, load_from_file_not_found)
{
    map m;
    auto result = m.load_from_file("nonexistent_map.amd");
    EXPECT_TRUE(result.is_err());
    EXPECT_TRUE(result.error().find("not found") != std::string::npos);
}

TEST(map_file_test, load_from_real_file)
{
    auto map_path = find_map_file("bsmith_1.amd");
    if (map_path.empty())
    {
        GTEST_SKIP() << "Map file not found - skipping real file test";
    }

    map m;
    map_config config;
    config.name = "bsmith_1";
    m.initialize(map_id{1}, config);

    auto result = m.load_from_file(map_path);
    ASSERT_TRUE(result.is_ok()) << "Failed to load map: " << result.error();

    // Verify map dimensions are reasonable (bsmith is a small shop)
    EXPECT_GT(m.width(), 0);
    EXPECT_GT(m.height(), 0);
    EXPECT_LT(m.width(), 1000); // Reasonable upper bound
    EXPECT_LT(m.height(), 1000);
}

TEST(map_file_test, load_parses_tile_flags)
{
    auto map_path = find_map_file("aresden.amd");
    if (map_path.empty())
    {
        GTEST_SKIP() << "Map file not found - skipping tile flags test";
    }

    map m;
    map_config config;
    config.name = "aresden";
    m.initialize(map_id{1}, config);

    auto result = m.load_from_file(map_path);
    ASSERT_TRUE(result.is_ok()) << "Failed to load map: " << result.error();

    // Just verify we can query tiles
    auto* tile = m.get_static_tile(position{10, 10});
    ASSERT_NE(tile, nullptr);

    // Count blocked tiles - a city map should have buildings/walls
    int blocked_count = 0;
    for (int16_t y = 0; y < m.height() && y < 100; ++y)
    {
        for (int16_t x = 0; x < m.width() && x < 100; ++x)
        {
            auto* t = m.get_static_tile(x, y);
            if (t && !t->is_walkable())
            {
                ++blocked_count;
            }
        }
    }

    // A city map should have some blocked tiles (walls, buildings)
    EXPECT_GT(blocked_count, 0) << "Expected some blocked tiles in city map";
}

TEST_F(world_subsystem_test, load_map_from_file)
{
    auto map_path = find_map_file("bsmith_1.amd");
    if (map_path.empty())
    {
        GTEST_SKIP() << "Map file not found - skipping world subsystem load test";
    }

    auto result = world_.load_map(map_path);
    ASSERT_TRUE(result.is_ok()) << "Failed to load map: " << result.error();

    auto id = result.value();
    EXPECT_TRUE(id.is_valid());

    // Get the loaded map
    auto* m = world_.get_map(id);
    ASSERT_NE(m, nullptr);
    EXPECT_GT(m->width(), 0);
    EXPECT_GT(m->height(), 0);
}

// Ground item tests

TEST_F(world_subsystem_test, add_ground_item)
{
    map_config config;
    config.name = "ground_item_map";
    config.width = 50;
    config.height = 50;

    auto result = world_.create_map(config);
    ASSERT_TRUE(result.is_ok());
    auto id = result.value();

    position pos{10, 10};

    EXPECT_FALSE(world_.has_ground_items(id, pos));
    EXPECT_EQ(world_.ground_item_count(id, pos), 0);

    world_.add_ground_item(id, pos, hb::item_id{100});
    EXPECT_TRUE(world_.has_ground_items(id, pos));
    EXPECT_EQ(world_.ground_item_count(id, pos), 1);
}

TEST_F(world_subsystem_test, multiple_ground_items)
{
    map_config config;
    config.name = "multi_ground_map";
    config.width = 50;
    config.height = 50;

    auto result = world_.create_map(config);
    auto id = result.value();

    position pos{20, 20};

    world_.add_ground_item(id, pos, hb::item_id{1});
    world_.add_ground_item(id, pos, hb::item_id{2});
    world_.add_ground_item(id, pos, hb::item_id{3});

    EXPECT_EQ(world_.ground_item_count(id, pos), 3);

    auto items = world_.get_ground_items(id, pos);
    EXPECT_EQ(items.size(), 3);
}

TEST_F(world_subsystem_test, remove_top_ground_item)
{
    map_config config;
    config.name = "remove_ground_map";
    config.width = 50;
    config.height = 50;

    auto result = world_.create_map(config);
    auto id = result.value();

    position pos{15, 15};

    world_.add_ground_item(id, pos, hb::item_id{10});
    world_.add_ground_item(id, pos, hb::item_id{20});

    // Top item should be the last added
    auto removed = world_.remove_top_ground_item(id, pos);
    ASSERT_TRUE(removed.has_value());
    EXPECT_EQ(removed->value, 20);
    EXPECT_EQ(world_.ground_item_count(id, pos), 1);

    removed = world_.remove_top_ground_item(id, pos);
    ASSERT_TRUE(removed.has_value());
    EXPECT_EQ(removed->value, 10);
    EXPECT_EQ(world_.ground_item_count(id, pos), 0);
    EXPECT_FALSE(world_.has_ground_items(id, pos));
}

TEST_F(world_subsystem_test, remove_ground_item_empty)
{
    map_config config;
    config.name = "empty_ground_map";
    config.width = 50;
    config.height = 50;

    auto result = world_.create_map(config);
    auto id = result.value();

    // Removing from empty position
    auto removed = world_.remove_top_ground_item(id, {5, 5});
    EXPECT_FALSE(removed.has_value());
}

TEST_F(world_subsystem_test, ground_items_different_positions)
{
    map_config config;
    config.name = "diff_pos_ground_map";
    config.width = 50;
    config.height = 50;

    auto result = world_.create_map(config);
    auto id = result.value();

    world_.add_ground_item(id, {10, 10}, hb::item_id{1});
    world_.add_ground_item(id, {20, 20}, hb::item_id{2});

    EXPECT_EQ(world_.ground_item_count(id, {10, 10}), 1);
    EXPECT_EQ(world_.ground_item_count(id, {20, 20}), 1);
    EXPECT_EQ(world_.ground_item_count(id, {30, 30}), 0);
}

TEST_F(world_subsystem_test, ground_items_invalid_map)
{
    // Operations on non-existent map should not crash
    EXPECT_FALSE(world_.has_ground_items(map_id{255}, {10, 10}));
    EXPECT_EQ(world_.ground_item_count(map_id{255}, {10, 10}), 0);
    EXPECT_TRUE(world_.get_ground_items(map_id{255}, {10, 10}).empty());
    auto removed = world_.remove_top_ground_item(map_id{255}, {10, 10});
    EXPECT_FALSE(removed.has_value());
}

TEST_F(world_subsystem_test, ground_item_custom_lifetime_expires)
{
    map_config config;
    config.name = "custom_lifetime_map";
    config.width = 50;
    config.height = 50;

    auto result = world_.create_map(config);
    ASSERT_TRUE(result.is_ok());
    auto id = result.value();

    position pos{10, 10};

    // Add item with a very short custom lifetime (1ms)
    world_.add_ground_item(id, pos, hb::item_id{100}, 1);
    EXPECT_EQ(world_.ground_item_count(id, pos), 1);

    // Wait long enough for the custom lifetime to elapse
    std::this_thread::sleep_for(std::chrono::milliseconds(5));

    // Use a long global max_age (10 seconds) -- item should still expire by its custom lifetime
    auto expired = world_.remove_expired_ground_items(std::chrono::seconds(10));
    EXPECT_EQ(expired.size(), 1);
    EXPECT_EQ(std::get<2>(expired[0]).value, 100);
    EXPECT_EQ(world_.ground_item_count(id, pos), 0);
}

TEST_F(world_subsystem_test, ground_item_default_lifetime_uses_global)
{
    map_config config;
    config.name = "default_lifetime_map";
    config.width = 50;
    config.height = 50;

    auto result = world_.create_map(config);
    ASSERT_TRUE(result.is_ok());
    auto id = result.value();

    position pos{10, 10};

    // Add item with lifetime_ms = 0 (use global default)
    world_.add_ground_item(id, pos, hb::item_id{200});
    EXPECT_EQ(world_.ground_item_count(id, pos), 1);

    // Wait a tiny bit
    std::this_thread::sleep_for(std::chrono::milliseconds(5));

    // With a global max_age of 10 seconds, item should NOT expire yet
    auto expired = world_.remove_expired_ground_items(std::chrono::seconds(10));
    EXPECT_TRUE(expired.empty());
    EXPECT_EQ(world_.ground_item_count(id, pos), 1);

    // With a global max_age of 0 seconds, item SHOULD expire
    expired = world_.remove_expired_ground_items(std::chrono::seconds(0));
    EXPECT_EQ(expired.size(), 1);
    EXPECT_EQ(std::get<2>(expired[0]).value, 200);
}

TEST_F(world_subsystem_test, ground_item_mixed_lifetimes)
{
    map_config config;
    config.name = "mixed_lifetime_map";
    config.width = 50;
    config.height = 50;

    auto result = world_.create_map(config);
    ASSERT_TRUE(result.is_ok());
    auto id = result.value();

    position pos{10, 10};

    // Add one item with short custom lifetime, one with default
    world_.add_ground_item(id, pos, hb::item_id{300}, 1);   // 1ms custom
    world_.add_ground_item(id, pos, hb::item_id{400});       // global default

    EXPECT_EQ(world_.ground_item_count(id, pos), 2);

    // Wait for short-lived item to expire
    std::this_thread::sleep_for(std::chrono::milliseconds(5));

    // With a long global max_age, only the custom-lifetime item should expire
    auto expired = world_.remove_expired_ground_items(std::chrono::seconds(10));
    EXPECT_EQ(expired.size(), 1);
    EXPECT_EQ(std::get<2>(expired[0]).value, 300);
    EXPECT_EQ(world_.ground_item_count(id, pos), 1);

    // The default-lifetime item remains
    auto items = world_.get_ground_items(id, pos);
    ASSERT_EQ(items.size(), 1);
    EXPECT_EQ(items[0].value, 400);
}

// Map feature tests

TEST_F(world_subsystem_test, map_weather)
{
    map_config config;
    config.name = "weather_map";
    config.width = 50;
    config.height = 50;

    auto result = world_.create_map(config);
    auto id = result.value();
    auto* m = world_.get_map(id);

    EXPECT_EQ(m->weather(), weather_type::clear);

    m->set_weather(weather_type::rain);
    EXPECT_EQ(m->weather(), weather_type::rain);
}

TEST_F(world_subsystem_test, map_safe_zone)
{
    map_config config;
    config.name = "safe_map";
    config.width = 100;
    config.height = 100;

    auto result = world_.create_map(config);
    auto id = result.value();
    auto* m = world_.get_map(id);

    // By default no safe zones
    EXPECT_FALSE(m->is_safe_zone({50, 50}));
    EXPECT_EQ(m->safe_zone_count(), 0);
}

TEST_F(world_subsystem_test, map_dead_entity)
{
    map_config config;
    config.name = "dead_map";
    config.width = 50;
    config.height = 50;

    auto result = world_.create_map(config);
    auto id = result.value();
    auto* m = world_.get_map(id);

    position pos{25, 25};
    EXPECT_FALSE(m->get_dead_entity(pos).has_value());

    m->set_dead_entity(pos, entity_id{99}, owner_type::npc);
    auto dead = m->get_dead_entity(pos);
    EXPECT_TRUE(dead.has_value());
    EXPECT_EQ(dead->value, 99);

    m->clear_dead_entity(pos);
    EXPECT_FALSE(m->get_dead_entity(pos).has_value());
}

TEST_F(world_subsystem_test, map_teleport_management)
{
    map_config config;
    config.name = "tp_map";
    config.width = 50;
    config.height = 50;

    auto result = world_.create_map(config);
    auto id = result.value();
    auto* m = world_.get_map(id);

    EXPECT_EQ(m->teleport_count(), 0);

    m->add_teleport({5, 5}, {"dest_map", 10, 10, direction::north});
    m->add_teleport({15, 15}, {"other_map", 20, 20, direction::south});

    EXPECT_EQ(m->teleport_count(), 2);

    EXPECT_TRUE(m->remove_teleport({5, 5}));
    EXPECT_EQ(m->teleport_count(), 1);
    EXPECT_FALSE(m->get_teleport_dest({5, 5}).has_value());

    EXPECT_FALSE(m->remove_teleport({99, 99})); // Non-existent
}

TEST_F(world_subsystem_test, can_move_to)
{
    map_config config;
    config.name = "move_map";
    config.width = 50;
    config.height = 50;

    auto result = world_.create_map(config);
    auto id = result.value();
    auto* m = world_.get_map(id);

    // Unoccupied walkable tile should allow movement
    EXPECT_TRUE(world_.can_move_to(id, {10, 10}));

    // Set occupant
    m->set_occupant({10, 10}, entity_id{1}, owner_type::player);
    EXPECT_FALSE(world_.can_move_to(id, {10, 10}));

    // Clear and re-check
    m->clear_occupant({10, 10});
    EXPECT_TRUE(world_.can_move_to(id, {10, 10}));
}

TEST_F(world_subsystem_test, for_each_map)
{
    map_config c1;
    c1.name = "map_a";
    c1.width = 10;
    c1.height = 10;

    map_config c2;
    c2.name = "map_b";
    c2.width = 20;
    c2.height = 20;

    world_.create_map(c1);
    world_.create_map(c2);

    int count = 0;
    world_.for_each_map([&](map_id, const map&) { ++count; });

    EXPECT_EQ(count, 2);
}
