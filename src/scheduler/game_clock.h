#pragma once

// game_clock.h
// In-game time management with day/night cycle

#include "core/types.h"

#include <chrono>

namespace hb
{

// Game clock manages in-game time separate from real time
// Default: 1 real minute = 1 game hour (24 real minutes = 1 game day)
class game_clock
{
public:
    game_clock();

    // Current game time
    [[nodiscard]] auto hour() const -> int;   // 0-23
    [[nodiscard]] auto minute() const -> int; // 0-59
    [[nodiscard]] auto second() const -> int; // 0-59

    // Day/night checks (day = 6:00 to 17:59, night = 18:00 to 5:59)
    [[nodiscard]] auto is_day() const -> bool;
    [[nodiscard]] auto is_night() const -> bool;
    [[nodiscard]] auto is_dawn() const -> bool; // 5:00 to 6:59
    [[nodiscard]] auto is_dusk() const -> bool; // 17:00 to 18:59

    // Day tracking
    [[nodiscard]] auto day() const -> int;         // Days since server start
    [[nodiscard]] auto day_of_week() const -> int; // 0-6 (0 = Sunday)

    // Total game time in minutes since start
    [[nodiscard]] auto total_minutes() const -> int64_t;

    // Advance time by real-world duration
    // Returns true if the hour changed
    auto advance(duration_ms real_time) -> bool;

    // Set time scale (1.0 = default, 2.0 = 2x speed, 0.5 = half speed)
    void set_time_scale(float scale);
    [[nodiscard]] auto time_scale() const -> float;

    // Set specific game time (for debugging/admin commands)
    void set_time(int hour, int minute = 0);

    // Reset to midnight of day 0
    void reset();

private:
    // Internal time stored as total game seconds since epoch
    int64_t game_seconds_{0};

    // How many game seconds pass per real second
    // Default: 60 (1 real minute = 1 game hour)
    float time_scale_{60.0f};

    // Accumulated fractional seconds
    float fractional_seconds_{0.0f};
};

} // namespace hb
