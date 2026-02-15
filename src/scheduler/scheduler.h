#pragma once

// scheduler.h
// Task scheduling subsystem
// Provides clean interface for delayed and repeating tasks

#include "core/subsystem.h"
#include "core/types.h"
#include "scheduler/scheduled_task.h"
#include "scheduler/game_clock.h"

#include "platform/clock.h"

#include <vector>
#include <queue>
#include <unordered_map>
#include <mutex>
#include <atomic>
#include <functional>
#include <optional>

namespace hb
{

// Scheduler subsystem for managing timed tasks
class scheduler : public subsystem
{
public:
    scheduler();
    ~scheduler() override;

    // Subsystem interface
    [[nodiscard]] auto name() const -> std::string_view override { return "scheduler"; }
    void initialize() override;
    void shutdown() override;
    void update(float delta_time) override;

    // Schedule a one-shot task to run after a delay
    auto schedule(duration_ms delay, task_callback callback) -> task_id;

    // Schedule a one-shot task with a tag (for bulk cancellation)
    auto schedule_tagged(duration_ms delay, std::string_view tag, task_callback callback) -> task_id;

    // Schedule a repeating task
    auto schedule_repeating(duration_ms interval, task_callback callback) -> task_id;

    // Schedule a repeating task with initial delay different from interval
    auto schedule_repeating(duration_ms initial_delay, duration_ms interval, task_callback callback) -> task_id;

    // Schedule a repeating task with a tag
    auto schedule_repeating_tagged(duration_ms interval, std::string_view tag, task_callback callback) -> task_id;

    // Cancel a specific task
    void cancel(task_id id);

    // Cancel all tasks with a specific tag
    void cancel_tagged(std::string_view tag);

    // Cancel all tasks
    void cancel_all();

    // Check if a task is still pending
    [[nodiscard]] auto is_pending(task_id id) const -> bool;

    // Get count of pending tasks
    [[nodiscard]] auto pending_count() const -> size_t;

    // Task enumeration for admin inspection
    struct task_info
    {
        uint64_t id{0};
        std::string tag;
        int64_t next_fire_ms{0}; // ms from now until next execution
        int64_t interval_ms{0};  // 0 = one-shot
        bool repeating{false};
    };

    template<typename Func> void for_each_task(Func&& func) const
    {
        // Copy metadata under lock, iterate outside
        std::vector<task_info> snapshot;
        {
            std::lock_guard lock(mutex_);
            snapshot.reserve(task_metadata_.size());
            auto now = platform::clock::now();
            for (const auto& [id, meta] : task_metadata_)
            {
                auto it = active_tasks_.find(id);
                if (it == active_tasks_.end() || !it->second)
                    continue;
                auto ms_until = std::chrono::duration_cast<std::chrono::milliseconds>(meta.execute_at - now).count();
                snapshot.push_back({.id = id,
                                    .tag = meta.tag,
                                    .next_fire_ms = ms_until,
                                    .interval_ms = meta.interval.count(),
                                    .repeating = meta.interval.count() > 0});
            }
        }
        for (const auto& info : snapshot)
        {
            func(info);
        }
    }

    // Task definition registry
    struct task_definition
    {
        std::string tag;
        std::string description;
        int64_t default_interval_ms{0};
        bool repeating{false};
        std::function<task_callback()> factory;
    };

    void register_task(std::string_view tag,
                       std::string_view description,
                       duration_ms default_interval,
                       bool repeating,
                       std::function<task_callback()> factory);

    auto start_task(std::string_view tag, std::optional<duration_ms> interval = std::nullopt) -> task_id;

    [[nodiscard]] auto is_task_running(std::string_view tag) const -> bool;

    template<typename Func> void for_each_definition(Func&& func) const
    {
        std::lock_guard lock(mutex_);
        for (const auto& [tag, def] : task_definitions_)
        {
            bool running = is_task_running_locked(tag);
            func(def, running);
        }
    }

    // Game clock access
    [[nodiscard]] auto game_time() -> game_clock& { return game_clock_; }
    [[nodiscard]] auto game_time() const -> const game_clock& { return game_clock_; }

private:
    // Check if a task with the given tag is running (caller must hold mutex_)
    [[nodiscard]] auto is_task_running_locked(std::string_view tag) const -> bool;

    // Generate next task ID
    auto next_id() -> task_id;

    // Priority queue comparator (earliest first)
    struct task_comparator
    {
        auto operator()(const scheduled_task& a, const scheduled_task& b) const -> bool
        {
            return a.execute_at > b.execute_at; // Min-heap
        }
    };

    // Task storage
    std::priority_queue<scheduled_task, std::vector<scheduled_task>, task_comparator> task_queue_;
    std::unordered_map<uint64_t, bool> active_tasks_; // id -> is_active

    // Task metadata for enumeration (mirrors active_tasks_)
    struct task_metadata
    {
        std::string tag;
        time_point execute_at;
        duration_ms interval{0};
    };
    std::unordered_map<uint64_t, task_metadata> task_metadata_;

    // Thread safety
    mutable std::mutex mutex_;

    // ID generation
    std::atomic<uint64_t> next_task_id_{1};

    // Game clock
    game_clock game_clock_;

    // Task definition registry
    std::unordered_map<std::string, task_definition> task_definitions_;

    // Stats
    uint64_t tasks_executed_{0};
    uint64_t tasks_cancelled_{0};
};

} // namespace hb
