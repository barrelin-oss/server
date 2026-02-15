// scheduler.cpp
// Task scheduling implementation

#include "scheduler/scheduler.h"
#include "core/logger.h"
#include "core/subsystem.h"
#include "perf/perf_stats.h"
#include "platform/clock.h"

namespace hb
{

scheduler::scheduler() = default;
scheduler::~scheduler() = default;

void scheduler::initialize()
{
    LOG_INFO(general, "Scheduler initialized");
    set_initialized(true);
}

void scheduler::shutdown()
{
    cancel_all();
    LOG_INFO(general, "Scheduler shut down (executed: {}, cancelled: {})", tasks_executed_, tasks_cancelled_);
    set_initialized(false);
}

void scheduler::update(float delta_time)
{
    // Advance game clock
    auto real_ms = static_cast<int64_t>(delta_time * 1000.0f);
    game_clock_.advance(duration_ms{real_ms});

    // Get current time
    auto now = platform::clock::now();

    // Process due tasks
    std::vector<scheduled_task> to_execute;
    std::vector<scheduled_task> to_reschedule;

    {
        std::lock_guard lock(mutex_);

        while (!task_queue_.empty())
        {
            const auto& top = task_queue_.top();

            // Check if task is due
            if (top.execute_at > now)
            {
                break; // No more tasks due
            }

            // Pop the task
            auto task = task_queue_.top();
            task_queue_.pop();

            // Check if still active
            auto it = active_tasks_.find(task.id.value);
            if (it == active_tasks_.end() || !it->second)
            {
                continue; // Task was cancelled
            }

            // Add to execution list
            to_execute.push_back(task);

            // If repeating, schedule next execution
            if (task.interval.count() > 0)
            {
                task.execute_at = now + task.interval;
                to_reschedule.push_back(task);
                // Update metadata with next fire time
                if (auto mit = task_metadata_.find(task.id.value); mit != task_metadata_.end())
                {
                    mit->second.execute_at = task.execute_at;
                }
            }
            else
            {
                // One-shot task - mark as inactive
                active_tasks_.erase(task.id.value);
                task_metadata_.erase(task.id.value);
            }
        }

        // Re-add repeating tasks
        for (auto& task : to_reschedule)
        {
            task_queue_.push(std::move(task));
        }
    }

    // Execute tasks outside the lock
    auto* perf = subsystems().get<perf::perf_stats_system>();
    for (auto& task : to_execute)
    {
        try
        {
            if (task.callback)
            {
                PERF_TIMER(perf, perf::metric_category::scheduler_task_exec);
                task.callback();
            }
            ++tasks_executed_;
        }
        catch (const std::exception& e)
        {
            LOG_ERROR(general, "Task {} threw exception: {}", task.id.value, e.what());
        }
    }
}

auto scheduler::schedule(duration_ms delay, task_callback callback) -> task_id
{
    return schedule_tagged(delay, "", std::move(callback));
}

auto scheduler::schedule_tagged(duration_ms delay, std::string_view tag, task_callback callback) -> task_id
{
    auto id = next_id();
    auto execute_at = platform::clock::now() + delay;

    auto task = scheduled_task::one_shot(id, execute_at, std::move(callback));
    task.tag = std::string(tag);

    {
        std::lock_guard lock(mutex_);
        active_tasks_[id.value] = true;
        task_metadata_[id.value] = {std::string(tag), execute_at, duration_ms{0}};
        task_queue_.push(std::move(task));
    }

    LOG_DEBUG(general,
              "Scheduled one-shot task {} (delay: {}ms, tag: {})",
              id.value,
              delay.count(),
              tag.empty() ? "<none>" : tag);

    return id;
}

auto scheduler::schedule_repeating(duration_ms interval, task_callback callback) -> task_id
{
    return schedule_repeating(interval, interval, std::move(callback));
}

auto scheduler::schedule_repeating(duration_ms initial_delay, duration_ms interval, task_callback callback) -> task_id
{
    return schedule_repeating_tagged(interval, "", [=, cb = std::move(callback)]() mutable { cb(); });
}

auto scheduler::schedule_repeating_tagged(duration_ms interval, std::string_view tag, task_callback callback) -> task_id
{
    auto id = next_id();
    auto execute_at = platform::clock::now() + interval;

    auto task = scheduled_task::repeating(id, execute_at, interval, std::move(callback));
    task.tag = std::string(tag);

    {
        std::lock_guard lock(mutex_);
        active_tasks_[id.value] = true;
        task_metadata_[id.value] = {std::string(tag), execute_at, interval};
        task_queue_.push(std::move(task));
    }

    LOG_DEBUG(general,
              "Scheduled repeating task {} (interval: {}ms, tag: {})",
              id.value,
              interval.count(),
              tag.empty() ? "<none>" : tag);

    return id;
}

void scheduler::cancel(task_id id)
{
    if (!id.is_valid())
        return;

    std::lock_guard lock(mutex_);
    auto it = active_tasks_.find(id.value);
    if (it != active_tasks_.end())
    {
        it->second = false;
        task_metadata_.erase(id.value);
        ++tasks_cancelled_;
        LOG_DEBUG(general, "Cancelled task {}", id.value);
    }
}

void scheduler::cancel_tagged(std::string_view tag)
{
    if (tag.empty())
        return;

    std::lock_guard lock(mutex_);

    // We can't efficiently remove from priority_queue, so we mark as cancelled
    // Tasks will be skipped when they come up for execution

    // Build a new queue without the tagged tasks
    std::vector<scheduled_task> remaining;
    while (!task_queue_.empty())
    {
        auto task = task_queue_.top();
        task_queue_.pop();

        if (task.tag == tag)
        {
            active_tasks_[task.id.value] = false;
            task_metadata_.erase(task.id.value);
            ++tasks_cancelled_;
        }
        else
        {
            remaining.push_back(std::move(task));
        }
    }

    // Rebuild queue
    for (auto& task : remaining)
    {
        task_queue_.push(std::move(task));
    }

    LOG_DEBUG(general, "Cancelled all tasks with tag '{}'", tag);
}

void scheduler::cancel_all()
{
    std::lock_guard lock(mutex_);

    size_t count = 0;
    while (!task_queue_.empty())
    {
        task_queue_.pop();
        ++count;
    }
    tasks_cancelled_ += count;
    active_tasks_.clear();
    task_metadata_.clear();

    LOG_DEBUG(general, "Cancelled all {} tasks", count);
}

auto scheduler::is_pending(task_id id) const -> bool
{
    if (!id.is_valid())
        return false;

    std::lock_guard lock(mutex_);
    auto it = active_tasks_.find(id.value);
    return it != active_tasks_.end() && it->second;
}

auto scheduler::pending_count() const -> size_t
{
    std::lock_guard lock(mutex_);
    size_t count = 0;
    for (const auto& [id, active] : active_tasks_)
    {
        if (active)
            ++count;
    }
    return count;
}

void scheduler::register_task(std::string_view tag,
                              std::string_view description,
                              duration_ms default_interval,
                              bool repeating,
                              std::function<task_callback()> factory)
{
    std::lock_guard lock(mutex_);
    task_definitions_[std::string(tag)] = task_definition{.tag = std::string(tag),
                                                          .description = std::string(description),
                                                          .default_interval_ms = default_interval.count(),
                                                          .repeating = repeating,
                                                          .factory = std::move(factory)};
    LOG_DEBUG(general,
              "Registered task definition '{}': {} (interval: {}ms, repeating: {})",
              tag,
              description,
              default_interval.count(),
              repeating);
}

auto scheduler::start_task(std::string_view tag, std::optional<duration_ms> interval) -> task_id
{
    std::function<task_callback()> factory;
    duration_ms effective_interval{0};
    bool repeating = false;

    {
        std::lock_guard lock(mutex_);
        auto it = task_definitions_.find(std::string(tag));
        if (it == task_definitions_.end())
        {
            LOG_WARN(general, "Cannot start task '{}': no definition registered", tag);
            return task_id{0};
        }
        factory = it->second.factory;
        effective_interval = interval.value_or(duration_ms{it->second.default_interval_ms});
        repeating = it->second.repeating;
    }

    auto callback = factory();
    if (repeating)
    {
        return schedule_repeating_tagged(effective_interval, tag, std::move(callback));
    }
    else
    {
        return schedule_tagged(effective_interval, tag, std::move(callback));
    }
}

auto scheduler::is_task_running(std::string_view tag) const -> bool
{
    std::lock_guard lock(mutex_);
    return is_task_running_locked(tag);
}

auto scheduler::is_task_running_locked(std::string_view tag) const -> bool
{
    for (const auto& [id, meta] : task_metadata_)
    {
        if (meta.tag == tag)
        {
            auto it = active_tasks_.find(id);
            if (it != active_tasks_.end() && it->second)
            {
                return true;
            }
        }
    }
    return false;
}

auto scheduler::next_id() -> task_id
{
    return task_id{next_task_id_.fetch_add(1)};
}

// game_clock implementation

game_clock::game_clock() = default;

auto game_clock::hour() const -> int
{
    return static_cast<int>((game_seconds_ / 3600) % 24);
}

auto game_clock::minute() const -> int
{
    return static_cast<int>((game_seconds_ / 60) % 60);
}

auto game_clock::second() const -> int
{
    return static_cast<int>(game_seconds_ % 60);
}

auto game_clock::is_day() const -> bool
{
    int h = hour();
    return h >= 6 && h < 18;
}

auto game_clock::is_night() const -> bool
{
    return !is_day();
}

auto game_clock::is_dawn() const -> bool
{
    int h = hour();
    return h >= 5 && h < 7;
}

auto game_clock::is_dusk() const -> bool
{
    int h = hour();
    return h >= 17 && h < 19;
}

auto game_clock::day() const -> int
{
    return static_cast<int>(game_seconds_ / 86400); // 86400 = 24 * 60 * 60
}

auto game_clock::day_of_week() const -> int
{
    return day() % 7;
}

auto game_clock::total_minutes() const -> int64_t
{
    return game_seconds_ / 60;
}

auto game_clock::advance(duration_ms real_time) -> bool
{
    int old_hour = hour();

    // Convert real time to game time
    float real_seconds = real_time.count() / 1000.0f;
    float game_seconds_delta = real_seconds * time_scale_;

    fractional_seconds_ += game_seconds_delta;

    // Add whole seconds
    auto whole_seconds = static_cast<int64_t>(fractional_seconds_);
    game_seconds_ += whole_seconds;
    fractional_seconds_ -= static_cast<float>(whole_seconds);

    return hour() != old_hour;
}

void game_clock::set_time_scale(float scale)
{
    time_scale_ = scale > 0.0f ? scale : 1.0f;
}

auto game_clock::time_scale() const -> float
{
    return time_scale_;
}

void game_clock::set_time(int hour, int minute)
{
    int current_day = day();
    game_seconds_ = static_cast<int64_t>(current_day) * 86400 + static_cast<int64_t>(hour) * 3600 +
                    static_cast<int64_t>(minute) * 60;
}

void game_clock::reset()
{
    game_seconds_ = 0;
    fractional_seconds_ = 0.0f;
}

} // namespace hb
