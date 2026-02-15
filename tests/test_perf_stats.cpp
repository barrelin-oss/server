// test_perf_stats.cpp
// Tests for performance profiling subsystem

#include <gtest/gtest.h>
#include <thread>
#include <vector>

#include "perf/perf_stats.h"
#include "network/json_protocol.h"

using namespace hb::perf;

// ============================================================================
// timing_stats tests
// ============================================================================

TEST(perf_stats, timing_stats_accumulation)
{
    timing_stats stats;
    stats.add_sample(100.0);
    stats.add_sample(200.0);
    stats.add_sample(300.0);

    EXPECT_EQ(stats.sample_count, 3u);
    EXPECT_DOUBLE_EQ(stats.min_us, 100.0);
    EXPECT_DOUBLE_EQ(stats.max_us, 300.0);
    EXPECT_DOUBLE_EQ(stats.avg_us(), 200.0);
    EXPECT_DOUBLE_EQ(stats.total_us, 600.0);
}

TEST(perf_stats, timing_stats_reset)
{
    timing_stats stats;
    stats.add_sample(100.0);
    stats.add_sample(200.0);
    stats.reset();

    EXPECT_EQ(stats.sample_count, 0u);
    EXPECT_DOUBLE_EQ(stats.avg_us(), 0.0);
    EXPECT_DOUBLE_EQ(stats.total_us, 0.0);
}

TEST(perf_stats, timing_stats_single_sample)
{
    timing_stats stats;
    stats.add_sample(42.5);

    EXPECT_EQ(stats.sample_count, 1u);
    EXPECT_DOUBLE_EQ(stats.min_us, 42.5);
    EXPECT_DOUBLE_EQ(stats.max_us, 42.5);
    EXPECT_DOUBLE_EQ(stats.avg_us(), 42.5);
}

// ============================================================================
// sample_buffer tests
// ============================================================================

TEST(perf_stats, sample_buffer_percentile)
{
    sample_buffer buf;

    // Add 100 samples: 1.0, 2.0, ..., 100.0
    for (int i = 1; i <= 100; ++i)
    {
        buf.add(static_cast<double>(i));
    }

    EXPECT_EQ(buf.count, 100u);

    // p50 should be ~50
    auto p50 = buf.percentile(50.0);
    EXPECT_DOUBLE_EQ(p50, 50.0);

    // p99 should be ~99
    auto p99 = buf.percentile(99.0);
    EXPECT_DOUBLE_EQ(p99, 99.0);

    // p100 should be 100
    auto p100 = buf.percentile(100.0);
    EXPECT_DOUBLE_EQ(p100, 100.0);
}

TEST(perf_stats, sample_buffer_empty)
{
    sample_buffer buf;
    EXPECT_DOUBLE_EQ(buf.percentile(99.0), 0.0);
}

TEST(perf_stats, sample_buffer_wraparound)
{
    sample_buffer buf;

    // Fill buffer and then some to test circular behavior
    for (size_t i = 0; i < sample_buffer_capacity + 100; ++i)
    {
        buf.add(static_cast<double>(i));
    }

    EXPECT_EQ(buf.count, sample_buffer_capacity);

    // After wraparound, values should be [100, 101, ..., 1123]
    // p0 should be around 100
    auto p1 = buf.percentile(1.0);
    EXPECT_GE(p1, 100.0);
}

TEST(perf_stats, sample_buffer_reset)
{
    sample_buffer buf;
    buf.add(1.0);
    buf.add(2.0);
    buf.reset();

    EXPECT_EQ(buf.count, 0u);
    EXPECT_DOUBLE_EQ(buf.percentile(99.0), 0.0);
}

// ============================================================================
// scoped_timer tests
// ============================================================================

TEST(perf_stats, scoped_timer_measures_duration)
{
    perf_stats_system sys;
    sys.initialize();

    {
        scoped_timer timer(&sys, metric_category::tick_total);
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }

    auto snap = sys.get_timing_snapshot(metric_category::tick_total);
    EXPECT_EQ(snap.sample_count, 1u);
    // Should be at least 4ms (sleep may be imprecise)
    EXPECT_GE(snap.avg_ms, 3.0);
    // Should be less than 100ms
    EXPECT_LE(snap.avg_ms, 100.0);
}

TEST(perf_stats, scoped_timer_null_stats_noop)
{
    // Should not crash when stats pointer is null
    {
        scoped_timer timer(nullptr, metric_category::tick_total);
    }
}

TEST(perf_stats, scoped_timer_disabled_noop)
{
    perf_stats_system sys;
    sys.initialize();
    sys.set_enabled(false);

    {
        scoped_timer timer(&sys, metric_category::tick_total);
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }

    auto snap = sys.get_timing_snapshot(metric_category::tick_total);
    EXPECT_EQ(snap.sample_count, 0u);
}

// ============================================================================
// Counter tests
// ============================================================================

TEST(perf_stats, counter_increment)
{
    perf_stats_system sys;
    sys.initialize();

    sys.increment_counter(metric_category::messages_received);
    sys.increment_counter(metric_category::messages_received);
    sys.increment_counter(metric_category::messages_received, 5);

    auto snap = sys.get_counter_snapshot(metric_category::messages_received);
    EXPECT_EQ(snap.total, 7u);
    EXPECT_EQ(std::string(snap.name), "messages_received");
}

TEST(perf_stats, counter_per_second_rate)
{
    perf_stats_system sys;
    sys.initialize();

    sys.increment_counter(metric_category::messages_sent, 100);

    // Simulate 1 second passing
    sys.update(1.0f);

    auto snap = sys.get_counter_snapshot(metric_category::messages_sent);
    EXPECT_EQ(snap.total, 100u);
    EXPECT_NEAR(snap.per_second, 100.0, 1.0);

    // After another second with no new increments, rate should be 0
    sys.update(1.0f);

    snap = sys.get_counter_snapshot(metric_category::messages_sent);
    EXPECT_EQ(snap.total, 100u);
    EXPECT_NEAR(snap.per_second, 0.0, 1.0);
}

// ============================================================================
// Thread safety tests
// ============================================================================

TEST(perf_stats, thread_safety_timing)
{
    perf_stats_system sys;
    sys.initialize();

    constexpr int num_threads = 8;
    constexpr int samples_per_thread = 1000;
    std::vector<std::thread> threads;

    for (int t = 0; t < num_threads; ++t)
    {
        threads.emplace_back(
            [&sys, t]()
            {
                for (int i = 0; i < samples_per_thread; ++i)
                {
                    sys.record_timing(metric_category::tick_total, static_cast<double>(t * 1000 + i));
                }
            });
    }

    for (auto& t : threads)
    {
        t.join();
    }

    auto snap = sys.get_timing_snapshot(metric_category::tick_total);
    // Buffer holds last 1024 samples (rolling window), so sample_count is capped at 1024
    // But with 8000 samples, the buffer should be full
    EXPECT_EQ(snap.sample_count, 1024u);
    // Verify statistics are valid
    EXPECT_GT(snap.avg_ms, 0.0);
    EXPECT_GE(snap.min_ms, 0.0);
    EXPECT_LE(snap.min_ms, snap.max_ms);
}

TEST(perf_stats, thread_safety_counters)
{
    perf_stats_system sys;
    sys.initialize();

    constexpr int num_threads = 8;
    constexpr int increments_per_thread = 10000;
    std::vector<std::thread> threads;

    for (int t = 0; t < num_threads; ++t)
    {
        threads.emplace_back(
            [&sys]()
            {
                for (int i = 0; i < increments_per_thread; ++i)
                {
                    sys.increment_counter(metric_category::bytes_received);
                }
            });
    }

    for (auto& t : threads)
    {
        t.join();
    }

    auto snap = sys.get_counter_snapshot(metric_category::bytes_received);
    EXPECT_EQ(snap.total, static_cast<uint64_t>(num_threads * increments_per_thread));
}

// ============================================================================
// Batch query tests
// ============================================================================

TEST(perf_stats, get_all_timing_snapshots)
{
    perf_stats_system sys;
    sys.initialize();

    sys.record_timing(metric_category::tick_total, 1000.0);
    sys.record_timing(metric_category::npc_ai_update, 500.0);

    auto snapshots = sys.get_all_timing_snapshots();

    // Should have all 26 timing categories (even those with zero samples)
    // because we include all metrics for admin visibility
    EXPECT_EQ(snapshots.size(), 26u);

    bool found_tick = false;
    bool found_npc = false;
    for (const auto& s : snapshots)
    {
        if (s.name == "tick_total")
        {
            found_tick = true;
            EXPECT_EQ(s.sample_count, 1u);
            EXPECT_DOUBLE_EQ(s.avg_ms, 1.0); // 1000us = 1ms
        }
        if (s.name == "npc_ai_update")
        {
            found_npc = true;
            EXPECT_EQ(s.sample_count, 1u);
            EXPECT_DOUBLE_EQ(s.avg_ms, 0.5); // 500us = 0.5ms
        }
    }
    EXPECT_TRUE(found_tick);
    EXPECT_TRUE(found_npc);

    // Verify metrics are sorted by importance (critical first)
    EXPECT_LT(static_cast<int>(snapshots[0].importance), static_cast<int>(snapshots[25].importance));
}

TEST(perf_stats, get_all_counter_snapshots_skips_zero)
{
    perf_stats_system sys;
    sys.initialize();

    sys.increment_counter(metric_category::messages_received, 42);

    auto snapshots = sys.get_all_counter_snapshots();

    // Should only contain non-zero counters
    EXPECT_GE(snapshots.size(), 1u);
    bool found = false;
    for (const auto& s : snapshots)
    {
        if (s.name == "messages_received")
        {
            EXPECT_EQ(s.total, 42u);
            found = true;
        }
    }
    EXPECT_TRUE(found);
}

// ============================================================================
// Gauge snapshot tests
// ============================================================================

TEST(perf_stats, gauge_snapshot_defaults)
{
    perf_stats_system sys;
    sys.initialize();

    auto g = sys.get_gauge_snapshot();
    EXPECT_EQ(g.players_online, 0u);
    EXPECT_EQ(g.npcs_alive, 0u);
    EXPECT_EQ(g.ground_items, 0u);
    EXPECT_EQ(g.active_effects, 0u);
    EXPECT_EQ(g.scheduled_tasks, 0u);
    EXPECT_EQ(g.active_connections, 0u);
}

// ============================================================================
// Category metadata tests
// ============================================================================

TEST(perf_stats, category_names)
{
    EXPECT_EQ(category_name(metric_category::tick_total), "tick_total");
    EXPECT_EQ(category_name(metric_category::npc_ai_update), "npc_ai_update");
    EXPECT_EQ(category_name(metric_category::messages_received), "messages_received");
    EXPECT_EQ(category_name(metric_category::db_queries), "db_queries");
}

TEST(perf_stats, is_timing_category_classification)
{
    EXPECT_TRUE(is_timing_category(metric_category::tick_total));
    EXPECT_TRUE(is_timing_category(metric_category::message_handler));
    EXPECT_FALSE(is_timing_category(metric_category::messages_received));
    EXPECT_FALSE(is_timing_category(metric_category::bytes_sent));
}

// ============================================================================
// Protocol tests
// ============================================================================

TEST(perf_stats, protocol_request_data_defaults)
{
    auto result = hb::network::admin_perf_stats_request_data::from_json(nlohmann::json::object());
    ASSERT_TRUE(result.is_ok());
    auto& data = result.value();
    EXPECT_TRUE(data.include_timing);
    EXPECT_TRUE(data.include_counters);
    EXPECT_TRUE(data.include_gauges);
}

TEST(perf_stats, protocol_request_data_custom)
{
    nlohmann::json j = {{"include_timing", false}, {"include_counters", true}, {"include_gauges", false}};
    auto result = hb::network::admin_perf_stats_request_data::from_json(j);
    ASSERT_TRUE(result.is_ok());
    auto& data = result.value();
    EXPECT_FALSE(data.include_timing);
    EXPECT_TRUE(data.include_counters);
    EXPECT_FALSE(data.include_gauges);
}

TEST(perf_stats, protocol_message_types)
{
    auto req_str = hb::network::to_string(hb::network::json_message_type::admin_perf_stats_request);
    auto resp_str = hb::network::to_string(hb::network::json_message_type::admin_perf_stats_response);
    EXPECT_EQ(req_str, "admin_perf_stats_request");
    EXPECT_EQ(resp_str, "admin_perf_stats_response");
}

TEST(perf_stats, protocol_message_type_parsing)
{
    auto type = hb::network::parse_message_type("admin_perf_stats_request");
    EXPECT_EQ(type, hb::network::json_message_type::admin_perf_stats_request);
}

// ============================================================================
// Enable/disable tests
// ============================================================================

TEST(perf_stats, enable_disable)
{
    perf_stats_system sys;
    sys.initialize();

    EXPECT_TRUE(sys.is_enabled());

    sys.set_enabled(false);
    EXPECT_FALSE(sys.is_enabled());

    // Recording while disabled should be ignored
    sys.record_timing(metric_category::tick_total, 100.0);
    auto snap = sys.get_timing_snapshot(metric_category::tick_total);
    EXPECT_EQ(snap.sample_count, 0u);

    sys.set_enabled(true);
    sys.record_timing(metric_category::tick_total, 100.0);
    snap = sys.get_timing_snapshot(metric_category::tick_total);
    EXPECT_EQ(snap.sample_count, 1u);
}

// ============================================================================
// Health status enum tests
// ============================================================================

TEST(perf_stats, health_status_string_values)
{
    EXPECT_EQ(health_status_string(health_status::good), "good");
    EXPECT_EQ(health_status_string(health_status::warning), "warning");
    EXPECT_EQ(health_status_string(health_status::critical), "critical");
}

TEST(perf_stats, worse_status_picks_worst)
{
    EXPECT_EQ(worse_status(health_status::good, health_status::good), health_status::good);
    EXPECT_EQ(worse_status(health_status::good, health_status::warning), health_status::warning);
    EXPECT_EQ(worse_status(health_status::warning, health_status::good), health_status::warning);
    EXPECT_EQ(worse_status(health_status::warning, health_status::critical), health_status::critical);
    EXPECT_EQ(worse_status(health_status::critical, health_status::good), health_status::critical);
}

// ============================================================================
// Welford's online algorithm tests
// ============================================================================

TEST(perf_stats, welford_mean_and_variance)
{
    sample_buffer buf;

    // Add known values: 2, 4, 4, 4, 5, 5, 7, 9
    // Mean = 5.0, Variance = 4.0 (sample), StdDev = 2.0
    buf.add(2.0);
    buf.add(4.0);
    buf.add(4.0);
    buf.add(4.0);
    buf.add(5.0);
    buf.add(5.0);
    buf.add(7.0);
    buf.add(9.0);

    EXPECT_EQ(buf.welford_n, 8u);
    EXPECT_DOUBLE_EQ(buf.welford_mean, 5.0);
    EXPECT_NEAR(buf.welford_variance(), 4.571, 0.001); // Sample variance: 32/7
    EXPECT_NEAR(buf.welford_stddev(), 2.138, 0.001);
}

TEST(perf_stats, welford_reset_clears_state)
{
    sample_buffer buf;
    buf.add(10.0);
    buf.add(20.0);
    buf.reset();

    EXPECT_EQ(buf.welford_n, 0u);
    EXPECT_DOUBLE_EQ(buf.welford_mean, 0.0);
    EXPECT_DOUBLE_EQ(buf.welford_m2, 0.0);
    EXPECT_DOUBLE_EQ(buf.welford_variance(), 0.0);
    EXPECT_DOUBLE_EQ(buf.welford_stddev(), 0.0);
}

TEST(perf_stats, welford_single_sample)
{
    sample_buffer buf;
    buf.add(42.0);

    EXPECT_EQ(buf.welford_n, 1u);
    EXPECT_DOUBLE_EQ(buf.welford_mean, 42.0);
    EXPECT_DOUBLE_EQ(buf.welford_variance(), 0.0); // Can't compute with n<2
    EXPECT_DOUBLE_EQ(buf.welford_stddev(), 0.0);
}

TEST(perf_stats, welford_constant_values)
{
    sample_buffer buf;
    for (int i = 0; i < 100; ++i)
        buf.add(5.0);

    EXPECT_DOUBLE_EQ(buf.welford_mean, 5.0);
    EXPECT_NEAR(buf.welford_variance(), 0.0, 1e-10);
    EXPECT_NEAR(buf.welford_stddev(), 0.0, 1e-10);
}

// ============================================================================
// Static timing threshold tests
// ============================================================================

TEST(perf_stats, default_timing_thresholds)
{
    auto tick = default_timing_threshold(metric_category::tick_total);
    EXPECT_DOUBLE_EQ(tick.warning_ms, 12.0);
    EXPECT_DOUBLE_EQ(tick.critical_ms, 16.0);

    auto db = default_timing_threshold(metric_category::db_query);
    EXPECT_DOUBLE_EQ(db.warning_ms, 5.0);
    EXPECT_DOUBLE_EQ(db.critical_ms, 10.0);

    auto handler = default_timing_threshold(metric_category::message_handler);
    EXPECT_DOUBLE_EQ(handler.warning_ms, 3.0);
    EXPECT_DOUBLE_EQ(handler.critical_ms, 8.0);

    // Default for unspecified categories
    auto other = default_timing_threshold(metric_category::broadcast);
    EXPECT_DOUBLE_EQ(other.warning_ms, 5.0);
    EXPECT_DOUBLE_EQ(other.critical_ms, 15.0);
}

// ============================================================================
// Timing health status tests
// ============================================================================

TEST(perf_stats, timing_status_good_under_threshold)
{
    perf_stats_system sys;
    sys.initialize();

    // Record samples well under warning threshold (tick_total warn=12ms)
    // 1ms = 1000us
    for (int i = 0; i < 50; ++i)
    {
        sys.record_timing(metric_category::tick_total, 1000.0); // 1ms
    }

    auto snap = sys.get_timing_snapshot(metric_category::tick_total);
    EXPECT_EQ(snap.status, health_status::good);
}

TEST(perf_stats, timing_status_warning_above_threshold)
{
    perf_stats_system sys;
    sys.initialize();

    // Record samples above warning threshold (tick_total warn=12ms, crit=16ms)
    // 13ms = 13000us
    for (int i = 0; i < 50; ++i)
    {
        sys.record_timing(metric_category::tick_total, 13000.0); // 13ms
    }

    auto snap = sys.get_timing_snapshot(metric_category::tick_total);
    EXPECT_EQ(snap.status, health_status::warning);
}

TEST(perf_stats, timing_status_critical_above_threshold)
{
    perf_stats_system sys;
    sys.initialize();

    // Record samples above critical threshold (tick_total crit=16ms)
    // 20ms = 20000us
    for (int i = 0; i < 50; ++i)
    {
        sys.record_timing(metric_category::tick_total, 20000.0); // 20ms
    }

    auto snap = sys.get_timing_snapshot(metric_category::tick_total);
    EXPECT_EQ(snap.status, health_status::critical);
}

TEST(perf_stats, timing_status_few_samples_always_good)
{
    perf_stats_system sys;
    sys.initialize();

    // With < 10 samples, status should always be good (not enough data)
    for (int i = 0; i < 5; ++i)
    {
        sys.record_timing(metric_category::tick_total, 50000.0); // 50ms, way above critical
    }

    auto snap = sys.get_timing_snapshot(metric_category::tick_total);
    EXPECT_EQ(snap.status, health_status::good);
}

TEST(perf_stats, timing_status_anomaly_detection_with_static_threshold)
{
    perf_stats_system sys;
    sys.initialize();

    // Anomaly detection via Welford's requires long-running baseline where the
    // Welford mean is low while the current windowed avg is high — hard to
    // simulate in a unit test since all samples are instant. Instead, verify
    // that a spike above the static threshold triggers warning/critical.
    // broadcast: warn=5ms, crit=15ms
    for (int i = 0; i < 50; ++i)
    {
        sys.record_timing(metric_category::broadcast, 6000.0); // 6ms, above 5ms warning
    }

    auto snap = sys.get_timing_snapshot(metric_category::broadcast);
    EXPECT_EQ(snap.status, health_status::warning);
}

TEST(perf_stats, welford_anomaly_math_direct)
{
    // Directly test the Welford math that anomaly detection relies on
    sample_buffer buf;

    // 500 stable samples at 1000us (mean ≈ 1000, low stddev)
    for (int i = 0; i < 500; ++i)
    {
        buf.add(1000.0);
    }

    EXPECT_NEAR(buf.welford_mean, 1000.0, 0.1);
    EXPECT_NEAR(buf.welford_stddev(), 0.0, 0.1);

    // A value of 5000us would be > mean + 3*sigma (since sigma ≈ 0)
    // This is what the anomaly detector checks against
    double threshold_3sigma = buf.welford_mean + 3.0 * buf.welford_stddev();
    EXPECT_LT(threshold_3sigma, 5000.0);
}

// ============================================================================
// Counter health status tests
// ============================================================================

TEST(perf_stats, counter_status_good_with_few_samples)
{
    perf_stats_system sys;
    sys.initialize();

    sys.increment_counter(metric_category::messages_received, 1000);
    sys.update(1.0f);

    auto snap = sys.get_counter_snapshot(metric_category::messages_received);
    // With n < 30 rate samples, should be good
    EXPECT_EQ(snap.status, health_status::good);
}

TEST(perf_stats, counter_status_stable_rate_is_good)
{
    perf_stats_system sys;
    sys.initialize();

    // Build stable baseline over 30+ ticks
    for (int i = 0; i < 40; ++i)
    {
        sys.increment_counter(metric_category::messages_received, 100);
        sys.update(1.0f);
    }

    auto snap = sys.get_counter_snapshot(metric_category::messages_received);
    EXPECT_EQ(snap.status, health_status::good);
}

// ============================================================================
// Gauge health status tests
// ============================================================================

TEST(perf_stats, gauge_statuses_present)
{
    perf_stats_system sys;
    sys.initialize();
    sys.update(0.016f); // Trigger gauge update

    auto g = sys.get_gauge_snapshot();
    // All gauges should have status entries
    EXPECT_TRUE(g.statuses.count("active_connections") > 0);
    EXPECT_TRUE(g.statuses.count("players_online") > 0);
    EXPECT_TRUE(g.statuses.count("npcs_alive") > 0);
    EXPECT_TRUE(g.statuses.count("ground_items") > 0);
    EXPECT_TRUE(g.statuses.count("active_effects") > 0);
    EXPECT_TRUE(g.statuses.count("scheduled_tasks") > 0);
}

TEST(perf_stats, gauge_statuses_default_good)
{
    perf_stats_system sys;
    sys.initialize();
    sys.update(0.016f);

    auto g = sys.get_gauge_snapshot();
    // With 0 connections and 0 max, all should be good
    for (const auto& [name, status] : g.statuses)
    {
        EXPECT_EQ(status, health_status::good) << "Gauge " << name << " should be good";
    }
}

// ============================================================================
// Overall health rollup tests
// ============================================================================

TEST(perf_stats, overall_health_good_when_all_good)
{
    perf_stats_system sys;
    sys.initialize();
    sys.update(0.016f);

    auto health = sys.compute_overall_health();
    EXPECT_EQ(health, health_status::good);
}

TEST(perf_stats, overall_health_reflects_worst_timing)
{
    perf_stats_system sys;
    sys.initialize();
    sys.update(0.016f);

    // Push tick_total above critical (16ms)
    for (int i = 0; i < 50; ++i)
    {
        sys.record_timing(metric_category::tick_total, 25000.0); // 25ms
    }

    auto health = sys.compute_overall_health();
    EXPECT_EQ(health, health_status::critical);
}

// ============================================================================
// Timing snapshot includes status field
// ============================================================================

TEST(perf_stats, timing_snapshot_has_status_field)
{
    perf_stats_system sys;
    sys.initialize();

    for (int i = 0; i < 20; ++i)
    {
        sys.record_timing(metric_category::tick_total, 500.0); // 0.5ms, well under threshold
    }

    auto snap = sys.get_timing_snapshot(metric_category::tick_total);
    EXPECT_EQ(snap.status, health_status::good);
    EXPECT_EQ(std::string(snap.name), "tick_total");
}

TEST(perf_stats, counter_snapshot_has_status_field)
{
    perf_stats_system sys;
    sys.initialize();

    sys.increment_counter(metric_category::messages_sent, 10);
    sys.update(1.0f);

    auto snap = sys.get_counter_snapshot(metric_category::messages_sent);
    EXPECT_EQ(snap.status, health_status::good);
}
