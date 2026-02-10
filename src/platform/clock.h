#pragma once

// clock.h
// High-resolution clock and time utilities
// Replaces GetTickCount() and other Windows-specific timing

#include <chrono>
#include <cstdint>
#include <thread>

namespace hb::platform {

class clock {
public:
    using time_point = std::chrono::steady_clock::time_point;
    using duration = std::chrono::steady_clock::duration;

    // Get current time point
    [[nodiscard]] static auto now() -> time_point {
        return std::chrono::steady_clock::now();
    }

    // Get milliseconds since clock epoch (like GetTickCount)
    // Note: This uses steady_clock for monotonic time
    [[nodiscard]] static auto tick_count() -> uint32_t {
        static const auto start_time = std::chrono::steady_clock::now();
        auto elapsed = std::chrono::steady_clock::now() - start_time;
        return static_cast<uint32_t>(
            std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count()
        );
    }

    // Get milliseconds since clock epoch as 64-bit (for longer running times)
    [[nodiscard]] static auto tick_count_64() -> uint64_t {
        static const auto start_time = std::chrono::steady_clock::now();
        auto elapsed = std::chrono::steady_clock::now() - start_time;
        return static_cast<uint64_t>(
            std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count()
        );
    }

    // Get elapsed time between two time points in milliseconds
    [[nodiscard]] static auto elapsed_ms(time_point start, time_point end) -> int64_t {
        return std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();
    }

    // Get elapsed time since a time point in milliseconds
    [[nodiscard]] static auto elapsed_since_ms(time_point start) -> int64_t {
        return elapsed_ms(start, now());
    }

    // Check if a duration has passed since a time point
    [[nodiscard]] static auto has_elapsed(time_point start, std::chrono::milliseconds duration) -> bool {
        return (now() - start) >= duration;
    }

    // Sleep for a duration (use sparingly - prefer timer callbacks)
    static void sleep_for(std::chrono::milliseconds duration) {
        std::this_thread::sleep_for(duration);
    }

    // Sleep until a time point
    static void sleep_until(time_point tp) {
        std::this_thread::sleep_until(tp);
    }
};

// Convenience type aliases
using time_point = clock::time_point;
using duration_ms = std::chrono::milliseconds;
using duration_us = std::chrono::microseconds;
using duration_s = std::chrono::seconds;

}  // namespace hb::platform
