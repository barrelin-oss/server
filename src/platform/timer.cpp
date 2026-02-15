// timer.cpp
// Cross-platform periodic timer implementation

#include "platform/timer.h"

namespace hb::platform
{

timer::timer(duration interval, callback_fn callback) : interval_(interval), callback_(std::move(callback)) {}

timer::~timer()
{
    stop();
}

timer::timer(timer&& other) noexcept
    : interval_(other.interval_)
    , callback_(std::move(other.callback_))
    , running_(other.running_.load())
{
    other.stop();
}

auto timer::operator=(timer&& other) noexcept -> timer&
{
    if (this != &other)
    {
        stop();
        interval_ = other.interval_;
        callback_ = std::move(other.callback_);
        running_ = other.running_.load();
        other.stop();
    }
    return *this;
}

void timer::start()
{
    if (running_.exchange(true))
    {
        return; // Already running
    }

    thread_ = std::jthread([this](std::stop_token stop_token) { thread_loop(stop_token); });
}

void timer::stop()
{
    if (!running_.exchange(false))
    {
        return; // Not running
    }

    // Signal the condition variable to wake up the thread
    cv_.notify_all();

    // Request stop and wait for thread to finish
    if (thread_.joinable())
    {
        thread_.request_stop();
        thread_.join();
    }
}

auto timer::is_running() const -> bool
{
    return running_.load();
}

auto timer::interval() const -> duration
{
    std::lock_guard lock(mutex_);
    return interval_;
}

void timer::set_interval(duration new_interval)
{
    std::lock_guard lock(mutex_);
    interval_ = new_interval;
}

void timer::set_callback(callback_fn new_callback)
{
    std::lock_guard lock(mutex_);
    callback_ = std::move(new_callback);
}

void timer::trigger()
{
    triggered_.store(true);
    cv_.notify_all();
}

void timer::thread_loop(std::stop_token stop_token)
{
    while (!stop_token.stop_requested() && running_.load())
    {
        // Wait for interval or trigger
        {
            std::unique_lock lock(mutex_);
            cv_.wait_for(lock,
                         stop_token,
                         interval_,
                         [this, &stop_token]()
                         { return stop_token.stop_requested() || !running_.load() || triggered_.load(); });
        }

        // Check if we should exit
        if (stop_token.stop_requested() || !running_.load())
        {
            break;
        }

        // Reset trigger flag
        triggered_.store(false);

        // Call the callback
        callback_fn callback_copy;
        {
            std::lock_guard lock(mutex_);
            callback_copy = callback_;
        }

        if (callback_copy)
        {
            callback_copy();
        }
    }
}

// one_shot_timer implementation

one_shot_timer::one_shot_timer(duration delay, callback_fn callback)
    : thread_(
          [this, delay, cb = std::move(callback)](std::stop_token stop_token)
          {
              std::this_thread::sleep_for(delay);

              if (!stop_token.stop_requested() && !cancelled_.load())
              {
                  fired_.store(true);
                  if (cb)
                  {
                      cb();
                  }
              }
          })
{
}

one_shot_timer::~one_shot_timer()
{
    cancel();
}

auto one_shot_timer::has_fired() const -> bool
{
    return fired_.load();
}

void one_shot_timer::cancel()
{
    cancelled_.store(true);
    if (thread_.joinable())
    {
        thread_.request_stop();
        thread_.join();
    }
}

} // namespace hb::platform
