#pragma once

// perf_stats.h
// Lightweight performance profiling subsystem
// Tracks timing, counters, and gauge metrics for server health monitoring

#include "core/subsystem.h"

#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <mutex>
#include <string>
#include <string_view>
#include <unordered_map>

namespace hb::perf
{

// Health status for metric evaluation
enum class health_status : uint8_t
{
    good = 0,
    warning = 1,
    critical = 2
};

[[nodiscard]] constexpr auto health_status_string(health_status s) -> std::string_view
{
    switch (s)
    {
    case health_status::good:
        return "good";
    case health_status::warning:
        return "warning";
    case health_status::critical:
        return "critical";
    default:
        return "good";
    }
}

[[nodiscard]] constexpr auto worse_status(health_status a, health_status b) -> health_status
{
    return a > b ? a : b;
}

// Importance level for metric categorization
enum class importance_level : uint8_t
{
    critical = 0, // Game-breaking if slow
    high = 1,     // Noticeable impact on gameplay
    medium = 2,   // Important but tolerable delays
    low = 3,      // Less critical operations
    info = 4      // Monitoring/debugging only
};

// Metric categories for instrumentation points
enum class metric_category : uint8_t
{
    // Tick timing
    tick_total = 0,

    // Hot path timing
    npc_ai_update,
    spatial_query_visibility,
    spatial_query_range,
    db_query,
    db_pool_acquire,
    scheduler_task_exec,
    message_handler,

    // Instrumentation points
    broadcast,
    combat_attack,
    inventory_op,
    trade_complete,
    npc_pathfinding,
    chat_send,
    chat_filter,
    skill_training,
    magic_cast,
    magic_learn,
    entity_lifecycle,
    player_save,
    loot_generation,
    effect_tick,
    quest_update,
    crafting,
    player_death,
    visibility_update,

    // Counters
    messages_received,
    messages_sent,
    bytes_received,
    bytes_sent,
    db_queries,

    count // Must be last
};

inline constexpr auto metric_category_count = static_cast<size_t>(metric_category::count);

// Human-readable names for each category
[[nodiscard]] constexpr auto category_name(metric_category cat) -> std::string_view
{
    switch (cat)
    {
    case metric_category::tick_total:
        return "tick_total";
    case metric_category::npc_ai_update:
        return "npc_ai_update";
    case metric_category::spatial_query_visibility:
        return "spatial_query_visibility";
    case metric_category::spatial_query_range:
        return "spatial_query_range";
    case metric_category::db_query:
        return "db_query";
    case metric_category::db_pool_acquire:
        return "db_pool_acquire";
    case metric_category::scheduler_task_exec:
        return "scheduler_task_exec";
    case metric_category::message_handler:
        return "message_handler";
    case metric_category::broadcast:
        return "broadcast";
    case metric_category::combat_attack:
        return "combat_attack";
    case metric_category::inventory_op:
        return "inventory_op";
    case metric_category::trade_complete:
        return "trade_complete";
    case metric_category::npc_pathfinding:
        return "npc_pathfinding";
    case metric_category::chat_send:
        return "chat_send";
    case metric_category::chat_filter:
        return "chat_filter";
    case metric_category::skill_training:
        return "skill_training";
    case metric_category::magic_cast:
        return "magic_cast";
    case metric_category::magic_learn:
        return "magic_learn";
    case metric_category::entity_lifecycle:
        return "entity_lifecycle";
    case metric_category::player_save:
        return "player_save";
    case metric_category::loot_generation:
        return "loot_generation";
    case metric_category::effect_tick:
        return "effect_tick";
    case metric_category::quest_update:
        return "quest_update";
    case metric_category::crafting:
        return "crafting";
    case metric_category::player_death:
        return "player_death";
    case metric_category::visibility_update:
        return "visibility_update";
    case metric_category::messages_received:
        return "messages_received";
    case metric_category::messages_sent:
        return "messages_sent";
    case metric_category::bytes_received:
        return "bytes_received";
    case metric_category::bytes_sent:
        return "bytes_sent";
    case metric_category::db_queries:
        return "db_queries";
    default:
        return "unknown";
    }
}

// Returns true if this category is a timing metric (vs counter)
[[nodiscard]] constexpr auto is_timing_category(metric_category cat) -> bool
{
    return cat <= metric_category::visibility_update;
}

// Static thresholds for timing metrics (in milliseconds, applied to avg_ms)
struct timing_threshold
{
    double warning_ms{5.0};
    double critical_ms{15.0};
};

[[nodiscard]] constexpr auto default_timing_threshold(metric_category cat) -> timing_threshold
{
    switch (cat)
    {
    case metric_category::tick_total:
        return {12.0, 16.0};
    case metric_category::npc_ai_update:
        return {5.0, 10.0};
    case metric_category::db_query:
        return {5.0, 10.0};
    case metric_category::db_pool_acquire:
        return {2.0, 5.0};
    case metric_category::message_handler:
        return {3.0, 8.0};
    case metric_category::player_save:
        return {10.0, 20.0};
    default:
        return {5.0, 15.0};
    }
}

// Maps a metric category to its importance level
[[nodiscard]] constexpr auto metric_importance(metric_category cat) -> importance_level
{
    switch (cat)
    {
    // Critical - game-breaking if slow
    case metric_category::tick_total:
    case metric_category::combat_attack:
    case metric_category::entity_lifecycle:
    case metric_category::player_save:
        return importance_level::critical;

    // High - noticeable impact on gameplay
    case metric_category::npc_ai_update:
    case metric_category::spatial_query_visibility:
    case metric_category::spatial_query_range:
    case metric_category::broadcast:
    case metric_category::inventory_op:
    case metric_category::loot_generation:
    case metric_category::effect_tick:
    case metric_category::visibility_update:
        return importance_level::high;

    // Medium - important but tolerable delays
    case metric_category::message_handler:
    case metric_category::scheduler_task_exec:
    case metric_category::skill_training:
    case metric_category::magic_cast:
    case metric_category::npc_pathfinding:
    case metric_category::quest_update:
    case metric_category::player_death:
        return importance_level::medium;

    // Low - less critical operations
    case metric_category::db_query:
    case metric_category::db_pool_acquire:
    case metric_category::chat_send:
    case metric_category::chat_filter:
    case metric_category::magic_learn:
    case metric_category::trade_complete:
    case metric_category::crafting:
        return importance_level::low;

    // Info - monitoring/debugging only (all counters)
    default:
        return importance_level::info;
    }
}

// Accumulated timing statistics for a single category
struct timing_stats
{
    uint64_t sample_count{0};
    double total_us{0.0};
    double min_us{1e18};
    double max_us{0.0};

    void add_sample(double us);
    void reset();
    [[nodiscard]] auto avg_us() const -> double;
};

// Circular buffer for percentile calculation with time-based rolling window
inline constexpr size_t sample_buffer_capacity = 1024;

struct sample_buffer
{
    std::array<double, sample_buffer_capacity> samples{};
    std::array<std::chrono::system_clock::time_point, sample_buffer_capacity> timestamps{};
    size_t write_pos{0};
    size_t count{0};

    // Welford's online algorithm for mean/variance
    uint64_t welford_n{0};
    double welford_mean{0.0};
    double welford_m2{0.0}; // Sum of squared deviations

    void add(double value);
    void reset();
    [[nodiscard]] auto percentile(double p) const -> double;
    [[nodiscard]] auto get_windowed_stats(std::chrono::system_clock::time_point window_start) const
        -> std::tuple<double, double, double>; // min, max, avg
    [[nodiscard]] auto welford_variance() const -> double;
    [[nodiscard]] auto welford_stddev() const -> double;
};

// Snapshot of timing data for a single category (returned to callers)
struct timing_snapshot
{
    std::string_view name;
    importance_level importance{importance_level::info};
    health_status status{health_status::good};
    uint64_t sample_count{0};
    double avg_ms{0.0};
    double min_ms{0.0};
    double max_ms{0.0};
    double p99_ms{0.0};
};

// Snapshot of counter data for a single category
struct counter_snapshot
{
    std::string_view name;
    importance_level importance{importance_level::info};
    health_status status{health_status::good};
    uint64_t total{0};
    double per_second{0.0};
};

// Snapshot of entity/resource gauges
struct gauge_snapshot
{
    size_t players_online{0};
    size_t npcs_alive{0};
    size_t ground_items{0};
    size_t active_effects{0};
    size_t scheduled_tasks{0};
    size_t active_connections{0};
    size_t max_connections{0}; // For capacity-based health check
    std::unordered_map<std::string, health_status> statuses;
};

// Performance statistics subsystem
class perf_stats_system : public subsystem
{
public:
    perf_stats_system();
    ~perf_stats_system() override;

    [[nodiscard]] auto name() const -> std::string_view override { return "perf_stats"; }
    void initialize() override;
    void shutdown() override;
    void update(float delta_time) override;

    // Record a timing sample (thread-safe)
    void record_timing(metric_category cat, double duration_us);

    // Increment a counter (thread-safe, lock-free)
    void increment_counter(metric_category cat, uint64_t delta = 1);

    // Query snapshots
    [[nodiscard]] auto get_timing_snapshot(metric_category cat) -> timing_snapshot;
    [[nodiscard]] auto get_counter_snapshot(metric_category cat) -> counter_snapshot;
    [[nodiscard]] auto get_gauge_snapshot() const -> gauge_snapshot;

    // Batch queries
    [[nodiscard]] auto get_all_timing_snapshots() -> std::vector<timing_snapshot>;
    [[nodiscard]] auto get_all_counter_snapshots() -> std::vector<counter_snapshot>;

    // Overall server health (worst status across all metrics)
    [[nodiscard]] auto compute_overall_health() -> health_status;

    // Enable/disable
    void set_enabled(bool enabled) { enabled_.store(enabled, std::memory_order_relaxed); }
    [[nodiscard]] auto is_enabled() const -> bool { return enabled_.load(std::memory_order_relaxed); }

    // Sample window configuration (for rolling min/max/avg)
    void set_sample_window(std::chrono::hours window) { sample_window_ = window; }
    [[nodiscard]] auto sample_window() const -> std::chrono::hours { return sample_window_; }

private:
    std::atomic<bool> enabled_{true};

    // Sample window for rolling stats (1 hour default)
    std::chrono::hours sample_window_{1};

    // Timing data (mutex-protected per category)
    struct timing_data
    {
        std::mutex mutex;
        timing_stats stats;
        sample_buffer samples;
    };
    std::array<timing_data, metric_category_count> timing_;

    // Counter data (lock-free atomics)
    std::array<std::atomic<uint64_t>, metric_category_count> counters_{};

    // Per-second rate tracking
    float rate_accumulator_{0.0f};
    std::array<uint64_t, metric_category_count> last_counter_values_{};
    std::array<double, metric_category_count> rates_per_second_{};

    // Counter rate history for anomaly detection (Welford's)
    struct counter_welford
    {
        uint64_t n{0};
        double mean{0.0};
        double m2{0.0};
    };
    std::array<counter_welford, metric_category_count> counter_welford_{};

    // Cached gauge snapshot (updated each tick on main thread)
    gauge_snapshot cached_gauges_;
    void update_gauges();

    // Health computation helpers
    [[nodiscard]] auto
    compute_timing_status(metric_category cat, double avg_ms, const sample_buffer& buf) const -> health_status;
    [[nodiscard]] auto compute_counter_status(metric_category cat, double per_second) const -> health_status;
    void compute_gauge_statuses(gauge_snapshot& snapshot) const;
};

// RAII scoped timer that auto-records on destruction
class scoped_timer
{
public:
    scoped_timer(perf_stats_system* stats, metric_category cat)
        : stats_(stats)
        , category_(cat)
        , start_(std::chrono::steady_clock::now())
    {
    }

    ~scoped_timer()
    {
        if (stats_ && stats_->is_enabled())
        {
            auto elapsed = std::chrono::steady_clock::now() - start_;
            auto us = std::chrono::duration<double, std::micro>(elapsed).count();
            stats_->record_timing(category_, us);
        }
    }

    // Non-copyable, non-movable
    scoped_timer(const scoped_timer&) = delete;
    auto operator=(const scoped_timer&) -> scoped_timer& = delete;
    scoped_timer(scoped_timer&&) = delete;
    auto operator=(scoped_timer&&) -> scoped_timer& = delete;

private:
    perf_stats_system* stats_;
    metric_category category_;
    std::chrono::steady_clock::time_point start_;
};

// Convenience macro - creates a scoped timer with unique variable name
#define PERF_TIMER(stats_ptr, category) ::hb::perf::scoped_timer perf_timer_##__LINE__((stats_ptr), (category))

} // namespace hb::perf
