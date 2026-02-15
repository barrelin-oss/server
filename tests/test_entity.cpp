// test_entity.cpp
// Unit tests for entity system

#include <gtest/gtest.h>
#include "entity/entity.h"
#include "entity/component_storage.h"
#include "entity/entity_manager.h"
#include "entity/components/transform.h"
#include "entity/components/combat_stats.h"
#include "entity/components/name.h"

using namespace hb::entity;
using namespace hb::world;
using hb::map_id;

// Entity ID tests

TEST(entity_test, null_entity)
{
    entity e;
    EXPECT_FALSE(e.is_valid());
    EXPECT_EQ(e.id, 0);

    entity null = entity::null();
    EXPECT_FALSE(null.is_valid());
}

TEST(entity_test, valid_entity)
{
    entity e{42};
    EXPECT_TRUE(e.is_valid());
    EXPECT_EQ(e.index(), 42);
}

TEST(entity_test, index_and_generation)
{
    // Create entity with index 100 and generation 5
    entity e{100, 5};

    EXPECT_EQ(e.index(), 100);
    EXPECT_EQ(e.generation(), 5);
}

TEST(entity_test, equality)
{
    entity e1{42, 1};
    entity e2{42, 1};
    entity e3{42, 2}; // Same index, different generation

    EXPECT_EQ(e1, e2);
    EXPECT_NE(e1, e3);
}

// Component storage tests

struct test_component
{
    int value{0};
    std::string name;

    test_component() = default;
    test_component(int v, std::string n) : value(v), name(std::move(n)) {}
};

TEST(component_storage_test, emplace_and_get)
{
    component_storage<test_component> storage;

    entity e{1};
    auto& comp = storage.emplace(e, 42, "test");

    EXPECT_EQ(comp.value, 42);
    EXPECT_EQ(comp.name, "test");

    auto* ptr = storage.get(e);
    ASSERT_NE(ptr, nullptr);
    EXPECT_EQ(ptr->value, 42);
}

TEST(component_storage_test, contains)
{
    component_storage<test_component> storage;

    entity e{1};
    EXPECT_FALSE(storage.contains(e));

    storage.emplace(e);
    EXPECT_TRUE(storage.contains(e));
}

TEST(component_storage_test, remove)
{
    component_storage<test_component> storage;

    entity e1{1};
    entity e2{2};

    storage.emplace(e1, 1, "first");
    storage.emplace(e2, 2, "second");

    EXPECT_EQ(storage.size(), 2);

    storage.remove(e1);

    EXPECT_FALSE(storage.contains(e1));
    EXPECT_TRUE(storage.contains(e2));
    EXPECT_EQ(storage.size(), 1);

    // Verify remaining entity is still accessible
    auto* ptr = storage.get(e2);
    ASSERT_NE(ptr, nullptr);
    EXPECT_EQ(ptr->value, 2);
}

TEST(component_storage_test, iteration)
{
    component_storage<test_component> storage;

    storage.emplace(entity{1}, 10, "a");
    storage.emplace(entity{2}, 20, "b");
    storage.emplace(entity{3}, 30, "c");

    int sum = 0;
    for (const auto& comp : storage)
    {
        sum += comp.value;
    }
    EXPECT_EQ(sum, 60);
}

TEST(component_storage_test, for_each)
{
    component_storage<test_component> storage;

    storage.emplace(entity{1}, 10, "a");
    storage.emplace(entity{2}, 20, "b");

    int sum = 0;
    storage.for_each([&sum]([[maybe_unused]] entity e, const test_component& comp) { sum += comp.value; });
    EXPECT_EQ(sum, 30);
}

TEST(component_storage_test, replace_existing)
{
    component_storage<test_component> storage;

    entity e{1};
    storage.emplace(e, 10, "old");
    storage.emplace(e, 20, "new"); // Should replace

    EXPECT_EQ(storage.size(), 1);
    auto* ptr = storage.get(e);
    EXPECT_EQ(ptr->value, 20);
    EXPECT_EQ(ptr->name, "new");
}

// Entity manager tests

class entity_manager_test : public ::testing::Test
{
protected:
    void SetUp() override { manager_.initialize(); }

    void TearDown() override { manager_.shutdown(); }

    entity_manager manager_;
};

TEST_F(entity_manager_test, lifecycle)
{
    EXPECT_TRUE(manager_.is_initialized());
    EXPECT_EQ(manager_.name(), "entity_manager");
}

TEST_F(entity_manager_test, create_entity)
{
    auto e = manager_.create();
    EXPECT_TRUE(e.is_valid());
    EXPECT_TRUE(manager_.is_alive(e));
    EXPECT_EQ(manager_.entity_count(), 1);
}

TEST_F(entity_manager_test, create_with_type)
{
    auto e = manager_.create(entity_type::player);
    EXPECT_TRUE(e.is_valid());
    EXPECT_EQ(manager_.get_type(e), entity_type::player);
}

TEST_F(entity_manager_test, destroy_entity)
{
    auto e = manager_.create();
    EXPECT_TRUE(manager_.is_alive(e));

    manager_.destroy(e);
    EXPECT_FALSE(manager_.is_alive(e));
    EXPECT_EQ(manager_.entity_count(), 0);
}

TEST_F(entity_manager_test, generation_increment)
{
    auto e1 = manager_.create();
    uint32_t index = e1.index();
    uint8_t gen1 = e1.generation();

    manager_.destroy(e1);

    // Create another entity - should recycle the index with incremented generation
    auto e2 = manager_.create();

    // If index was recycled, generation should be incremented
    if (e2.index() == index)
    {
        EXPECT_EQ(e2.generation(), static_cast<uint8_t>((gen1 + 1) & 0xFF));
    }

    // Old entity reference should be invalid
    EXPECT_FALSE(manager_.is_alive(e1));
    EXPECT_TRUE(manager_.is_alive(e2));
}

TEST_F(entity_manager_test, add_component)
{
    auto e = manager_.create();

    auto& health_comp = manager_.add_component<health>(e, 100);
    EXPECT_EQ(health_comp.current, 100);
    EXPECT_EQ(health_comp.maximum, 100);

    EXPECT_TRUE(manager_.has_component<health>(e));
}

TEST_F(entity_manager_test, get_component)
{
    auto e = manager_.create();
    manager_.add_component<health>(e, 50, 100);

    auto* comp = manager_.get_component<health>(e);
    ASSERT_NE(comp, nullptr);
    EXPECT_EQ(comp->current, 50);
    EXPECT_EQ(comp->maximum, 100);
}

TEST_F(entity_manager_test, remove_component)
{
    auto e = manager_.create();
    manager_.add_component<health>(e, 100);

    EXPECT_TRUE(manager_.has_component<health>(e));

    manager_.remove_component<health>(e);

    EXPECT_FALSE(manager_.has_component<health>(e));
}

TEST_F(entity_manager_test, destroy_removes_components)
{
    auto e = manager_.create();
    manager_.add_component<health>(e, 100);
    manager_.add_component<name>(e, "Test");

    manager_.destroy(e);

    EXPECT_FALSE(manager_.has_component<health>(e));
    EXPECT_FALSE(manager_.has_component<name>(e));
}

TEST_F(entity_manager_test, multiple_components)
{
    auto e = manager_.create();

    manager_.add_component<health>(e, 100);
    manager_.add_component<mana>(e, 50);
    manager_.add_component<name>(e, "Player");
    manager_.add_component<transform>(e, map_id{1}, position{10, 20});

    EXPECT_TRUE(manager_.has_component<health>(e));
    EXPECT_TRUE(manager_.has_component<mana>(e));
    EXPECT_TRUE(manager_.has_component<name>(e));
    EXPECT_TRUE(manager_.has_component<transform>(e));

    auto* trans = manager_.get_component<transform>(e);
    EXPECT_EQ(trans->map.value, 1);
    EXPECT_EQ(trans->pos.x, 10);
    EXPECT_EQ(trans->pos.y, 20);
}

TEST_F(entity_manager_test, for_each_single_component)
{
    auto e1 = manager_.create();
    auto e2 = manager_.create();

    manager_.add_component<health>(e1, 100);
    manager_.add_component<health>(e2, 200);

    int sum = 0;
    manager_.for_each<health>([&sum]([[maybe_unused]] entity e, health& h) { sum += h.current; });

    EXPECT_EQ(sum, 300);
}

TEST_F(entity_manager_test, for_each_two_components)
{
    auto e1 = manager_.create();
    auto e2 = manager_.create();
    auto e3 = manager_.create();

    // e1 has both
    manager_.add_component<health>(e1, 100);
    manager_.add_component<name>(e1, "Both");

    // e2 has only health
    manager_.add_component<health>(e2, 200);

    // e3 has only name
    manager_.add_component<name>(e3, "NameOnly");

    int count = 0;
    manager_.for_each<health, name>(
        [&count]([[maybe_unused]] entity e, [[maybe_unused]] health& h, name& n)
        {
            ++count;
            EXPECT_EQ(n.value, "Both");
        });

    EXPECT_EQ(count, 1); // Only e1 has both
}

// Component tests

TEST(health_component_test, damage_and_heal)
{
    health h{100};

    h.damage(30);
    EXPECT_EQ(h.current, 70);
    EXPECT_TRUE(h.is_alive());

    h.heal(50);
    EXPECT_EQ(h.current, 100); // Capped at max
    EXPECT_TRUE(h.is_full());

    h.damage(150);
    EXPECT_EQ(h.current, 0);
    EXPECT_FALSE(h.is_alive());
}

TEST(mana_component_test, spend_and_restore)
{
    mana m{50};

    EXPECT_TRUE(m.has_mana(30));
    EXPECT_TRUE(m.spend(30));
    EXPECT_EQ(m.current, 20);

    EXPECT_FALSE(m.has_mana(30));
    EXPECT_FALSE(m.spend(30)); // Not enough

    m.restore(100);
    EXPECT_EQ(m.current, 50); // Capped at max
}

TEST(transform_component_test, construction)
{
    transform t{map_id{1}, position{100, 200}, direction::east};

    EXPECT_EQ(t.map.value, 1);
    EXPECT_EQ(t.pos.x, 100);
    EXPECT_EQ(t.pos.y, 200);
    EXPECT_EQ(t.facing, direction::east);
}

TEST(faction_component_test, hostile)
{
    faction_component f1{faction::aresden};
    [[maybe_unused]] faction_component f2{faction::elvine};
    [[maybe_unused]] faction_component f3{faction::aresden};
    faction_component f4{faction::none};

    EXPECT_TRUE(f1.is_hostile_to(faction::elvine));
    EXPECT_FALSE(f1.is_hostile_to(faction::aresden));
    EXPECT_FALSE(f1.is_hostile_to(faction::none));
    EXPECT_FALSE(f4.is_hostile_to(faction::aresden));
}

TEST(level_component_test, exp_progress)
{
    level l;
    l.current = 10;
    l.experience = 500;
    l.next_level_exp = 1000;

    EXPECT_FLOAT_EQ(l.exp_progress(), 0.5f);
}
