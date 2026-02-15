// test_event_bus.cpp
// Unit tests for the event_bus

#include <gtest/gtest.h>
#include "core/event_bus.h"

#include <string>
#include <atomic>

using namespace hb;

// Test events
struct test_event_a
{
    int value;
};

struct test_event_b
{
    std::string message;
};

struct test_event_c
{
    int x;
    int y;
};

TEST(event_bus_test, subscribe_and_publish)
{
    event_bus bus;
    int received_value = 0;

    bus.subscribe<test_event_a>([&](const test_event_a& e) { received_value = e.value; });

    bus.publish(test_event_a{42});

    EXPECT_EQ(received_value, 42);
}

TEST(event_bus_test, multiple_subscribers)
{
    event_bus bus;
    int count = 0;

    bus.subscribe<test_event_a>([&](const test_event_a&) { count++; });
    bus.subscribe<test_event_a>([&](const test_event_a&) { count++; });
    bus.subscribe<test_event_a>([&](const test_event_a&) { count++; });

    bus.publish(test_event_a{1});

    EXPECT_EQ(count, 3);
}

TEST(event_bus_test, different_event_types)
{
    event_bus bus;
    int a_received = 0;
    std::string b_received;

    bus.subscribe<test_event_a>([&](const test_event_a& e) { a_received = e.value; });

    bus.subscribe<test_event_b>([&](const test_event_b& e) { b_received = e.message; });

    bus.publish(test_event_a{100});
    bus.publish(test_event_b{"hello"});

    EXPECT_EQ(a_received, 100);
    EXPECT_EQ(b_received, "hello");
}

TEST(event_bus_test, unsubscribe)
{
    event_bus bus;
    int count = 0;

    auto id = bus.subscribe<test_event_a>([&](const test_event_a&) { count++; });

    bus.publish(test_event_a{1});
    EXPECT_EQ(count, 1);

    bus.unsubscribe(id);

    bus.publish(test_event_a{1});
    EXPECT_EQ(count, 1); // Should not increment
}

TEST(event_bus_test, subscription_id_validity)
{
    event_bus bus;

    auto id = bus.subscribe<test_event_a>([](const test_event_a&) {});

    EXPECT_TRUE(id.is_valid());

    subscription_id invalid_id;
    EXPECT_FALSE(invalid_id.is_valid());
}

TEST(event_bus_test, has_subscribers)
{
    event_bus bus;

    EXPECT_FALSE(bus.has_subscribers<test_event_a>());

    auto id = bus.subscribe<test_event_a>([](const test_event_a&) {});

    EXPECT_TRUE(bus.has_subscribers<test_event_a>());
    EXPECT_FALSE(bus.has_subscribers<test_event_b>());

    bus.unsubscribe(id);

    EXPECT_FALSE(bus.has_subscribers<test_event_a>());
}

TEST(event_bus_test, subscriber_count)
{
    event_bus bus;

    EXPECT_EQ(bus.subscriber_count<test_event_a>(), 0u);

    bus.subscribe<test_event_a>([](const test_event_a&) {});
    EXPECT_EQ(bus.subscriber_count<test_event_a>(), 1u);

    bus.subscribe<test_event_a>([](const test_event_a&) {});
    EXPECT_EQ(bus.subscriber_count<test_event_a>(), 2u);
}

TEST(event_bus_test, clear)
{
    event_bus bus;
    int count = 0;

    bus.subscribe<test_event_a>([&](const test_event_a&) { count++; });
    bus.subscribe<test_event_b>([&](const test_event_b&) { count++; });

    bus.clear();

    bus.publish(test_event_a{1});
    bus.publish(test_event_b{"test"});

    EXPECT_EQ(count, 0);
}

TEST(event_bus_test, publish_to_no_subscribers)
{
    event_bus bus;

    // Should not crash
    EXPECT_NO_THROW(bus.publish(test_event_a{42}));
}

TEST(event_bus_test, subscription_guard)
{
    event_bus bus;
    int count = 0;

    {
        subscription_guard guard = subscribe_scoped<test_event_a>(bus, [&](const test_event_a&) { count++; });

        bus.publish(test_event_a{1});
        EXPECT_EQ(count, 1);
    }

    // Guard destroyed, should be unsubscribed
    bus.publish(test_event_a{1});
    EXPECT_EQ(count, 1); // Should not increment
}

TEST(event_bus_test, subscription_guard_release)
{
    event_bus bus;
    int count = 0;
    subscription_id released_id;

    {
        subscription_guard guard = subscribe_scoped<test_event_a>(bus, [&](const test_event_a&) { count++; });

        released_id = guard.release();
    }

    // Guard released, subscription should still be active
    bus.publish(test_event_a{1});
    EXPECT_EQ(count, 1);

    // Manual cleanup
    bus.unsubscribe(released_id);
}

TEST(event_bus_test, subscription_guard_move)
{
    event_bus bus;
    int count = 0;

    subscription_guard guard1 = subscribe_scoped<test_event_a>(bus, [&](const test_event_a&) { count++; });

    subscription_guard guard2 = std::move(guard1);

    // guard1 should be invalid now
    EXPECT_FALSE(guard1.id().is_valid());
    EXPECT_TRUE(guard2.id().is_valid());

    bus.publish(test_event_a{1});
    EXPECT_EQ(count, 1);
}

TEST(event_bus_test, global_event_bus)
{
    int count = 0;

    auto id = global_event_bus().subscribe<test_event_a>([&](const test_event_a&) { count++; });

    global_event_bus().publish(test_event_a{1});
    EXPECT_EQ(count, 1);

    global_event_bus().unsubscribe(id);
}

// Thread safety test (basic)
TEST(event_bus_test, thread_safety)
{
    event_bus bus;
    std::atomic<int> count{0};

    // Subscribe from main thread
    bus.subscribe<test_event_a>([&](const test_event_a&) { count++; });

    // Publish from multiple threads
    std::vector<std::thread> threads;
    for (int i = 0; i < 10; ++i)
    {
        threads.emplace_back(
            [&bus]()
            {
                for (int j = 0; j < 100; ++j)
                {
                    bus.publish(test_event_a{j});
                }
            });
    }

    for (auto& t : threads)
    {
        t.join();
    }

    EXPECT_EQ(count.load(), 1000);
}
