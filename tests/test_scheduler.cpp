// test_scheduler.cpp
// Unit tests for scheduler and game_clock

#include <gtest/gtest.h>
#include "scheduler/scheduler.h"
#include "scheduler/game_clock.h"

#include <thread>
#include <chrono>
#include <atomic>

using namespace hb;

class scheduler_test : public ::testing::Test
{
protected:
    void SetUp() override { sched_.initialize(); }

    void TearDown() override { sched_.shutdown(); }

    scheduler sched_;
};

// Game clock tests

TEST(game_clock_test, initial_state)
{
    game_clock clock;
    EXPECT_EQ(clock.hour(), 0);
    EXPECT_EQ(clock.minute(), 0);
    EXPECT_EQ(clock.second(), 0);
    EXPECT_EQ(clock.day(), 0);
}

TEST(game_clock_test, advance_time)
{
    game_clock clock;

    // Advance 1 real second at default time scale (60x)
    // Should advance 60 game seconds = 1 game minute
    clock.advance(duration_ms{1000});

    EXPECT_EQ(clock.minute(), 1);
    EXPECT_EQ(clock.second(), 0);
}

TEST(game_clock_test, hour_rollover)
{
    game_clock clock;

    // Advance 60 real seconds = 60 game minutes = 1 game hour
    clock.advance(duration_ms{60000});

    EXPECT_EQ(clock.hour(), 1);
    EXPECT_EQ(clock.minute(), 0);
}

TEST(game_clock_test, day_night_cycle)
{
    game_clock clock;

    // At midnight (hour 0), it's night
    EXPECT_TRUE(clock.is_night());
    EXPECT_FALSE(clock.is_day());

    // Set to 6 AM - should be day
    clock.set_time(6, 0);
    EXPECT_TRUE(clock.is_day());
    EXPECT_FALSE(clock.is_night());

    // Set to noon - still day
    clock.set_time(12, 0);
    EXPECT_TRUE(clock.is_day());

    // Set to 6 PM - night begins
    clock.set_time(18, 0);
    EXPECT_TRUE(clock.is_night());
    EXPECT_FALSE(clock.is_day());
}

TEST(game_clock_test, dawn_and_dusk)
{
    game_clock clock;

    // Dawn is 5-7
    clock.set_time(5, 30);
    EXPECT_TRUE(clock.is_dawn());

    clock.set_time(6, 30);
    EXPECT_TRUE(clock.is_dawn());

    clock.set_time(7, 30);
    EXPECT_FALSE(clock.is_dawn());

    // Dusk is 17-19
    clock.set_time(17, 30);
    EXPECT_TRUE(clock.is_dusk());

    clock.set_time(18, 30);
    EXPECT_TRUE(clock.is_dusk());

    clock.set_time(19, 30);
    EXPECT_FALSE(clock.is_dusk());
}

TEST(game_clock_test, time_scale)
{
    game_clock clock;

    // Double speed
    clock.set_time_scale(120.0f);
    EXPECT_FLOAT_EQ(clock.time_scale(), 120.0f);

    // Advance 1 real second at 120x = 2 game minutes
    clock.advance(duration_ms{1000});
    EXPECT_EQ(clock.minute(), 2);
}

TEST(game_clock_test, day_of_week)
{
    game_clock clock;

    EXPECT_EQ(clock.day_of_week(), 0); // Day 0 = Sunday

    // Advance multiple days
    clock.set_time(0, 0);
    for (int i = 0; i < 24; ++i)
    {
        clock.advance(duration_ms{60000}); // 1 real minute = 1 game hour
    }

    EXPECT_EQ(clock.day(), 1);
    EXPECT_EQ(clock.day_of_week(), 1); // Monday
}

// Scheduler tests

TEST_F(scheduler_test, lifecycle)
{
    EXPECT_TRUE(sched_.is_initialized());
    EXPECT_EQ(sched_.name(), "scheduler");
}

TEST_F(scheduler_test, schedule_one_shot)
{
    std::atomic<int> counter{0};

    auto id = sched_.schedule(duration_ms{10}, [&counter]() { ++counter; });

    EXPECT_TRUE(id.is_valid());
    EXPECT_TRUE(sched_.is_pending(id));
    EXPECT_EQ(sched_.pending_count(), 1);

    // Wait and update
    std::this_thread::sleep_for(std::chrono::milliseconds{50});
    sched_.update(0.05f);

    EXPECT_EQ(counter.load(), 1);
    EXPECT_FALSE(sched_.is_pending(id));
}

TEST_F(scheduler_test, schedule_repeating)
{
    std::atomic<int> counter{0};

    auto id = sched_.schedule_repeating(duration_ms{10}, [&counter]() { ++counter; });

    EXPECT_TRUE(id.is_valid());

    // Wait and update multiple times
    for (int i = 0; i < 5; ++i)
    {
        std::this_thread::sleep_for(std::chrono::milliseconds{15});
        sched_.update(0.015f);
    }

    // Should have executed multiple times
    EXPECT_GE(counter.load(), 3);
    EXPECT_TRUE(sched_.is_pending(id)); // Still active
}

TEST_F(scheduler_test, cancel_task)
{
    std::atomic<int> counter{0};

    auto id = sched_.schedule(duration_ms{100}, [&counter]() { ++counter; });

    EXPECT_TRUE(sched_.is_pending(id));

    sched_.cancel(id);

    EXPECT_FALSE(sched_.is_pending(id));

    // Wait and update - task should not execute
    std::this_thread::sleep_for(std::chrono::milliseconds{150});
    sched_.update(0.15f);

    EXPECT_EQ(counter.load(), 0);
}

TEST_F(scheduler_test, cancel_all)
{
    std::atomic<int> counter{0};

    sched_.schedule(duration_ms{10}, [&counter]() { ++counter; });
    sched_.schedule(duration_ms{10}, [&counter]() { ++counter; });
    sched_.schedule(duration_ms{10}, [&counter]() { ++counter; });

    EXPECT_EQ(sched_.pending_count(), 3);

    sched_.cancel_all();

    EXPECT_EQ(sched_.pending_count(), 0);

    std::this_thread::sleep_for(std::chrono::milliseconds{50});
    sched_.update(0.05f);

    EXPECT_EQ(counter.load(), 0);
}

TEST_F(scheduler_test, tagged_tasks)
{
    std::atomic<int> counter_a{0};
    std::atomic<int> counter_b{0};

    sched_.schedule_tagged(duration_ms{10}, "group_a", [&counter_a]() { ++counter_a; });
    sched_.schedule_tagged(duration_ms{10}, "group_a", [&counter_a]() { ++counter_a; });
    sched_.schedule_tagged(duration_ms{10}, "group_b", [&counter_b]() { ++counter_b; });

    EXPECT_EQ(sched_.pending_count(), 3);

    // Cancel only group_a
    sched_.cancel_tagged("group_a");

    EXPECT_EQ(sched_.pending_count(), 1);

    std::this_thread::sleep_for(std::chrono::milliseconds{50});
    sched_.update(0.05f);

    EXPECT_EQ(counter_a.load(), 0);
    EXPECT_EQ(counter_b.load(), 1);
}

TEST_F(scheduler_test, game_clock_integration)
{
    auto& clock = sched_.game_time();

    EXPECT_EQ(clock.hour(), 0);

    // Set time through scheduler
    clock.set_time(12, 30);

    EXPECT_EQ(clock.hour(), 12);
    EXPECT_EQ(clock.minute(), 30);
}
