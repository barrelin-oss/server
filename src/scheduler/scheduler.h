#pragma once

// scheduler.h
// Task scheduling subsystem
// Provides clean interface for delayed and repeating tasks

#include "core/subsystem.h"
#include "core/types.h"
#include "scheduler/scheduled_task.h"
#include "scheduler/game_clock.h"

#include <vector>
#include <queue>
#include <unordered_map>
#include <mutex>
#include <atomic>

namespace hb {

// Scheduler subsystem for managing timed tasks
class scheduler : public subsystem {
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

    // Game clock access
    [[nodiscard]] auto game_time() -> game_clock& { return game_clock_; }
    [[nodiscard]] auto game_time() const -> const game_clock& { return game_clock_; }

private:
    // Generate next task ID
    auto next_id() -> task_id;

    // Priority queue comparator (earliest first)
    struct task_comparator {
        auto operator()(const scheduled_task& a, const scheduled_task& b) const -> bool {
            return a.execute_at > b.execute_at;  // Min-heap
        }
    };

    // Task storage
    std::priority_queue<scheduled_task, std::vector<scheduled_task>, task_comparator> task_queue_;
    std::unordered_map<uint64_t, bool> active_tasks_;  // id -> is_active

    // Thread safety
    mutable std::mutex mutex_;

    // ID generation
    std::atomic<uint64_t> next_task_id_{1};

    // Game clock
    game_clock game_clock_;

    // Stats
    uint64_t tasks_executed_{0};
    uint64_t tasks_cancelled_{0};
};

}  // namespace hb
