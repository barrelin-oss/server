#pragma once

// scheduled_task.h
// Task types for the scheduler

#include "core/types.h"

#include <functional>
#include <string>
#include <chrono>

namespace hb
{

// Unique identifier for a scheduled task
struct task_id
{
    uint64_t value{0};

    constexpr task_id() = default;
    constexpr explicit task_id(uint64_t v) : value(v) {}

    constexpr auto operator<=>(const task_id&) const = default;
    [[nodiscard]] constexpr auto is_valid() const -> bool { return value != 0; }
};

// Task callback type
using task_callback = std::function<void()>;

// Internal task representation
struct scheduled_task
{
    task_id id;
    task_callback callback;

    // When to execute (absolute time point)
    time_point execute_at;

    // For repeating tasks: interval between executions
    // Zero means one-shot task
    duration_ms interval{0};

    // Optional tag for bulk cancellation
    std::string tag;

    // Is this task still active?
    bool active{true};

    // Was this task cancelled?
    bool cancelled{false};

    // Create a one-shot task
    static auto one_shot(task_id id, time_point when, task_callback cb) -> scheduled_task
    {
        return scheduled_task{.id = id,
                              .callback = std::move(cb),
                              .execute_at = when,
                              .interval = duration_ms{0},
                              .tag = "",
                              .active = true,
                              .cancelled = false};
    }

    // Create a repeating task
    static auto repeating(task_id id, time_point first_run, duration_ms interval, task_callback cb) -> scheduled_task
    {
        return scheduled_task{.id = id,
                              .callback = std::move(cb),
                              .execute_at = first_run,
                              .interval = interval,
                              .tag = "",
                              .active = true,
                              .cancelled = false};
    }
};

} // namespace hb

// Hash specialization for task_id
namespace std
{

template<> struct hash<hb::task_id>
{
    auto operator()(const hb::task_id& id) const noexcept -> size_t { return hash<uint64_t>{}(id.value); }
};

} // namespace std
