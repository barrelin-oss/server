#pragma once

// timer.h
// Cross-platform periodic timer using std::jthread
// Replaces Windows multimedia timers (timeSetEvent)

#include <chrono>
#include <functional>
#include <thread>
#include <atomic>
#include <mutex>
#include <condition_variable>

namespace hb::platform
{

class timer
{
public:
    using callback_fn = std::function<void()>;
    using duration = std::chrono::milliseconds;

    // Construct a timer with interval and callback
    // Timer is not started until start() is called
    timer(duration interval, callback_fn callback);

    // Destructor stops the timer if running
    ~timer();

    // Non-copyable
    timer(const timer&) = delete;
    auto operator=(const timer&) -> timer& = delete;

    // Movable
    timer(timer&& other) noexcept;
    auto operator=(timer&& other) noexcept -> timer&;

    // Start the timer
    void start();

    // Stop the timer (waits for current callback to complete)
    void stop();

    // Check if timer is running
    [[nodiscard]] auto is_running() const -> bool;

    // Get the interval
    [[nodiscard]] auto interval() const -> duration;

    // Set a new interval (takes effect after current interval completes)
    void set_interval(duration new_interval);

    // Set a new callback
    void set_callback(callback_fn new_callback);

    // Trigger callback immediately (in addition to regular interval)
    void trigger();

private:
    void thread_loop(std::stop_token stop_token);

    duration interval_;
    callback_fn callback_;
    std::jthread thread_;
    std::atomic<bool> running_{false};
    std::atomic<bool> triggered_{false};
    mutable std::mutex mutex_;
    std::condition_variable_any cv_;
};

// One-shot timer that fires once after a delay
class one_shot_timer
{
public:
    using callback_fn = std::function<void()>;
    using duration = std::chrono::milliseconds;

    // Schedule a callback to run after delay
    one_shot_timer(duration delay, callback_fn callback);

    // Cancel the timer (callback won't be called)
    ~one_shot_timer();

    // Non-copyable, non-movable
    one_shot_timer(const one_shot_timer&) = delete;
    auto operator=(const one_shot_timer&) -> one_shot_timer& = delete;

    // Check if timer has fired
    [[nodiscard]] auto has_fired() const -> bool;

    // Cancel the timer
    void cancel();

private:
    std::jthread thread_;
    std::atomic<bool> fired_{false};
    std::atomic<bool> cancelled_{false};
};

} // namespace hb::platform
