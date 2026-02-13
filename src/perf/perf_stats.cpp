// perf_stats.cpp
// Performance profiling subsystem implementation

#include "perf/perf_stats.h"
#include "core/logger.h"
#include "core/subsystem.h"
#include "player/player_system.h"
#include "npc/npc_system.h"
#include "world/world_subsystem.h"
#include "scheduler/scheduler.h"
#include "network/network_subsystem.h"

#include <algorithm>
#include <cmath>

namespace hb::perf {

// timing_stats implementation

void timing_stats::add_sample(double us)
{
    total_us += us;
    ++sample_count;
    if (us < min_us) min_us = us;
    if (us > max_us) max_us = us;
}

void timing_stats::reset()
{
    sample_count = 0;
    total_us = 0.0;
    min_us = 1e18;
    max_us = 0.0;
}

auto timing_stats::avg_us() const -> double
{
    if (sample_count == 0) return 0.0;
    return total_us / static_cast<double>(sample_count);
}

// sample_buffer implementation

void sample_buffer::add(double value)
{
    samples[write_pos] = value;
    timestamps[write_pos] = std::chrono::system_clock::now();
    write_pos = (write_pos + 1) % sample_buffer_capacity;
    if (count < sample_buffer_capacity) ++count;

    // Welford's online algorithm update
    ++welford_n;
    double delta = value - welford_mean;
    welford_mean += delta / static_cast<double>(welford_n);
    double delta2 = value - welford_mean;
    welford_m2 += delta * delta2;
}

void sample_buffer::reset()
{
    write_pos = 0;
    count = 0;
    welford_n = 0;
    welford_mean = 0.0;
    welford_m2 = 0.0;
}

auto sample_buffer::welford_variance() const -> double
{
    if (welford_n < 2) return 0.0;
    return welford_m2 / static_cast<double>(welford_n - 1);
}

auto sample_buffer::welford_stddev() const -> double
{
    return std::sqrt(welford_variance());
}

auto sample_buffer::percentile(double p) const -> double
{
    if (count == 0) return 0.0;

    // Copy valid samples and sort
    std::vector<double> sorted(count);
    if (count == sample_buffer_capacity)
    {
        // Buffer is full, copy all
        std::copy(samples.begin(), samples.end(), sorted.begin());
    }
    else
    {
        // Buffer not full, copy [0, count)
        std::copy(samples.begin(), samples.begin() + count, sorted.begin());
    }
    std::sort(sorted.begin(), sorted.end());

    // Calculate percentile index
    auto index = static_cast<size_t>(std::ceil(p / 100.0 * static_cast<double>(count))) - 1;
    if (index >= count) index = count - 1;
    return sorted[index];
}

auto sample_buffer::get_windowed_stats(std::chrono::system_clock::time_point window_start) const
    -> std::tuple<double, double, double>
{
    double min_val = 1e18;
    double max_val = 0.0;
    double sum = 0.0;
    size_t windowed_count = 0;

    // Iterate through all samples in the buffer
    for (size_t i = 0; i < count; ++i)
    {
        if (timestamps[i] >= window_start)
        {
            double val = samples[i];
            if (val < min_val) min_val = val;
            if (val > max_val) max_val = val;
            sum += val;
            ++windowed_count;
        }
    }

    double avg = windowed_count > 0 ? sum / static_cast<double>(windowed_count) : 0.0;
    if (min_val == 1e18) min_val = 0.0;

    return std::make_tuple(min_val, max_val, avg);
}

// perf_stats_system implementation

perf_stats_system::perf_stats_system()
{
    for (auto& c : counters_)
    {
        c.store(0, std::memory_order_relaxed);
    }
    last_counter_values_.fill(0);
    rates_per_second_.fill(0.0);
}

perf_stats_system::~perf_stats_system() = default;

void perf_stats_system::initialize()
{
    LOG_INFO(general, "Performance stats system initialized");
    set_initialized(true);
}

void perf_stats_system::shutdown()
{
    LOG_INFO(general, "Performance stats system shut down");
    set_initialized(false);
}

void perf_stats_system::update(float delta_time)
{
    if (!enabled_.load(std::memory_order_relaxed)) return;

    // Update per-second rates every second
    rate_accumulator_ += delta_time;
    if (rate_accumulator_ >= 1.0f)
    {
        auto elapsed = static_cast<double>(rate_accumulator_);
        for (size_t i = 0; i < metric_category_count; ++i)
        {
            auto current = counters_[i].load(std::memory_order_relaxed);
            auto delta = current - last_counter_values_[i];
            auto rate = static_cast<double>(delta) / elapsed;
            rates_per_second_[i] = rate;
            last_counter_values_[i] = current;

            // Update counter rate Welford's for anomaly detection
            auto& cw = counter_welford_[i];
            ++cw.n;
            double d1 = rate - cw.mean;
            cw.mean += d1 / static_cast<double>(cw.n);
            double d2 = rate - cw.mean;
            cw.m2 += d1 * d2;
        }
        rate_accumulator_ = 0.0f;
    }

    // Update gauge snapshot
    update_gauges();
}

void perf_stats_system::record_timing(metric_category cat, double duration_us)
{
    if (!enabled_.load(std::memory_order_relaxed)) return;

    auto idx = static_cast<size_t>(cat);
    if (idx >= metric_category_count) return;

    std::lock_guard lock(timing_[idx].mutex);
    timing_[idx].stats.add_sample(duration_us);
    timing_[idx].samples.add(duration_us);
}

void perf_stats_system::increment_counter(metric_category cat, uint64_t delta)
{
    auto idx = static_cast<size_t>(cat);
    if (idx >= metric_category_count) return;
    counters_[idx].fetch_add(delta, std::memory_order_relaxed);
}

auto perf_stats_system::get_timing_snapshot(metric_category cat) -> timing_snapshot
{
    auto idx = static_cast<size_t>(cat);
    if (idx >= metric_category_count) return {};

    std::lock_guard lock(timing_[idx].mutex);
    auto& buf = timing_[idx].samples;

    // Calculate windowed statistics (last N hours)
    auto window_start = std::chrono::system_clock::now() - sample_window_;
    auto [min_us, max_us, avg_us] = buf.get_windowed_stats(window_start);

    auto avg_ms = avg_us / 1000.0;

    return timing_snapshot{
        .name = category_name(cat),
        .importance = metric_importance(cat),
        .status = compute_timing_status(cat, avg_ms, buf),
        .sample_count = buf.count,
        .avg_ms = avg_ms,
        .min_ms = min_us == 1e18 ? 0.0 : min_us / 1000.0,
        .max_ms = max_us / 1000.0,
        .p99_ms = buf.percentile(99.0) / 1000.0,
    };
}

auto perf_stats_system::get_counter_snapshot(metric_category cat) -> counter_snapshot
{
    auto idx = static_cast<size_t>(cat);
    if (idx >= metric_category_count) return {};

    auto rate = rates_per_second_[idx];

    return counter_snapshot{
        .name = category_name(cat),
        .importance = metric_importance(cat),
        .status = compute_counter_status(cat, rate),
        .total = counters_[idx].load(std::memory_order_relaxed),
        .per_second = rate,
    };
}

auto perf_stats_system::get_gauge_snapshot() const -> gauge_snapshot
{
    return cached_gauges_;
}

auto perf_stats_system::get_all_timing_snapshots() -> std::vector<timing_snapshot>
{
    std::vector<timing_snapshot> result;
    for (size_t i = 0; i < metric_category_count; ++i)
    {
        auto cat = static_cast<metric_category>(i);
        if (!is_timing_category(cat)) continue;

        result.push_back(get_timing_snapshot(cat));
    }

    // Sort by importance (critical first), then by name
    std::sort(result.begin(), result.end(), [](const auto& a, const auto& b) {
        if (a.importance != b.importance)
        {
            return a.importance < b.importance;  // Lower value = more important
        }
        return a.name < b.name;
    });

    return result;
}

auto perf_stats_system::get_all_counter_snapshots() -> std::vector<counter_snapshot>
{
    std::vector<counter_snapshot> result;
    for (size_t i = 0; i < metric_category_count; ++i)
    {
        auto cat = static_cast<metric_category>(i);
        if (is_timing_category(cat)) continue;
        if (cat == metric_category::count) continue;

        result.push_back(get_counter_snapshot(cat));
    }

    // Sort by importance (critical first), then by name
    std::sort(result.begin(), result.end(), [](const auto& a, const auto& b) {
        if (a.importance != b.importance)
        {
            return a.importance < b.importance;  // Lower value = more important
        }
        return a.name < b.name;
    });

    return result;
}

void perf_stats_system::update_gauges()
{
    auto* players = subsystems().get<player::player_system>();
    auto* npcs = subsystems().get<npc::npc_system>();
    auto* scheduler = subsystems().get<hb::scheduler>();

    cached_gauges_.players_online = players ? players->player_count() : 0;
    cached_gauges_.npcs_alive = npcs ? npcs->npc_count() : 0;
    cached_gauges_.scheduled_tasks = scheduler ? scheduler->pending_count() : 0;

    auto* world = subsystems().get<world::world_subsystem>();
    cached_gauges_.ground_items = world ? world->total_ground_item_count() : 0;

    auto* network = subsystems().get<network::network_subsystem>();
    cached_gauges_.active_connections = network ? network->connection_count() : 0;
    cached_gauges_.max_connections = network ? network->config().max_connections : 0;

    compute_gauge_statuses(cached_gauges_);
}

// Health computation helpers

auto perf_stats_system::compute_timing_status(metric_category cat, double avg_ms,
    const sample_buffer& buf) const -> health_status
{
    // Not enough samples to evaluate
    if (buf.count < 10) return health_status::good;

    auto threshold = default_timing_threshold(cat);

    // Static threshold check first
    auto result = health_status::good;
    if (avg_ms >= threshold.critical_ms)
        result = health_status::critical;
    else if (avg_ms >= threshold.warning_ms)
        result = health_status::warning;

    // Anomaly detection: check if current avg exceeds mean + N*sigma
    // Only meaningful with enough samples for stable statistics
    if (buf.welford_n >= 30)
    {
        double stddev_us = buf.welford_stddev();
        double mean_us = buf.welford_mean;
        double current_us = avg_ms * 1000.0;

        if (stddev_us > 0.0)
        {
            if (current_us > mean_us + 3.0 * stddev_us)
                result = worse_status(result, health_status::critical);
            else if (current_us > mean_us + 2.0 * stddev_us)
                result = worse_status(result, health_status::warning);
        }
    }

    return result;
}

auto perf_stats_system::compute_counter_status(metric_category cat, double per_second) const -> health_status
{
    auto idx = static_cast<size_t>(cat);
    if (idx >= metric_category_count) return health_status::good;

    const auto& cw = counter_welford_[idx];

    // Need enough rate samples for stable statistics
    if (cw.n < 30) return health_status::good;

    double variance = cw.n >= 2 ? cw.m2 / static_cast<double>(cw.n - 1) : 0.0;
    double stddev = std::sqrt(variance);

    if (stddev <= 0.0) return health_status::good;

    if (per_second > cw.mean + 3.0 * stddev)
        return health_status::critical;
    if (per_second > cw.mean + 2.0 * stddev)
        return health_status::warning;

    return health_status::good;
}

void perf_stats_system::compute_gauge_statuses(gauge_snapshot& snapshot) const
{
    snapshot.statuses.clear();

    // Connection capacity check
    if (snapshot.max_connections > 0)
    {
        double ratio = static_cast<double>(snapshot.active_connections)
                     / static_cast<double>(snapshot.max_connections);
        if (ratio >= 0.95)
            snapshot.statuses["active_connections"] = health_status::critical;
        else if (ratio >= 0.80)
            snapshot.statuses["active_connections"] = health_status::warning;
        else
            snapshot.statuses["active_connections"] = health_status::good;
    }
    else
    {
        snapshot.statuses["active_connections"] = health_status::good;
    }

    // Other gauges default to good (no capacity limit to check)
    snapshot.statuses["players_online"] = health_status::good;
    snapshot.statuses["npcs_alive"] = health_status::good;
    snapshot.statuses["ground_items"] = health_status::good;
    snapshot.statuses["active_effects"] = health_status::good;
    snapshot.statuses["scheduled_tasks"] = health_status::good;
}

auto perf_stats_system::compute_overall_health() -> health_status
{
    auto result = health_status::good;

    // Check all timing metrics
    for (size_t i = 0; i < metric_category_count; ++i)
    {
        auto cat = static_cast<metric_category>(i);
        if (!is_timing_category(cat)) continue;
        auto snap = get_timing_snapshot(cat);
        result = worse_status(result, snap.status);
        if (result == health_status::critical) return result;
    }

    // Check all counter metrics
    for (size_t i = 0; i < metric_category_count; ++i)
    {
        auto cat = static_cast<metric_category>(i);
        if (is_timing_category(cat)) continue;
        if (cat == metric_category::count) continue;
        auto snap = get_counter_snapshot(cat);
        result = worse_status(result, snap.status);
        if (result == health_status::critical) return result;
    }

    // Check gauge statuses
    for (const auto& [name, status] : cached_gauges_.statuses)
    {
        result = worse_status(result, status);
        if (result == health_status::critical) return result;
    }

    return result;
}

}  // namespace hb::perf
