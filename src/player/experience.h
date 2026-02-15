#pragma once

// experience.h
// Experience and leveling system

#include <cstdint>
#include <array>

namespace hb::player
{

inline constexpr uint8_t max_level = 180;
inline constexpr int32_t contribution_per_ek = 30; // EK contribution to level

// Experience table (pre-computed for each level)
struct experience_table
{
    std::array<int64_t, max_level + 1> required{};

    constexpr experience_table()
    {
        required[0] = 0;
        required[1] = 0;
        for (int i = 2; i <= max_level; ++i)
        {
            // Helbreath-style exp curve
            int64_t base = static_cast<int64_t>(i) * i * i * 10;
            required[i] = required[i - 1] + base;
        }
    }

    [[nodiscard]] constexpr auto exp_for_level(uint8_t level) const -> int64_t
    {
        return level <= max_level ? required[level] : required[max_level];
    }

    [[nodiscard]] constexpr auto exp_to_next(uint8_t current_level, int64_t current_exp) const -> int64_t
    {
        if (current_level >= max_level)
            return 0;
        return required[current_level + 1] - current_exp;
    }
};

inline constexpr experience_table exp_table{};

// Player experience state
struct experience_state
{
    uint8_t level{1};
    int64_t experience{0};
    int32_t enemy_kill_count{0}; // EK system
    int32_t contribution{0};     // Contribution points

    [[nodiscard]] auto exp_to_next_level() const -> int64_t { return exp_table.exp_to_next(level, experience); }

    [[nodiscard]] auto exp_progress() const -> float
    {
        if (level >= max_level)
            return 1.0f;
        int64_t current_level_exp = exp_table.exp_for_level(level);
        int64_t next_level_exp = exp_table.exp_for_level(level + 1);
        int64_t level_range = next_level_exp - current_level_exp;
        if (level_range <= 0)
            return 1.0f;
        return static_cast<float>(experience - current_level_exp) / level_range;
    }

    [[nodiscard]] auto ek_contribution() const -> int32_t { return enemy_kill_count * contribution_per_ek; }

    // Add experience and return number of levels gained
    auto add_experience(int64_t amount) -> int
    {
        if (level >= max_level)
            return 0;

        experience += amount;
        int levels_gained = 0;

        while (level < max_level && experience >= exp_table.exp_for_level(level + 1))
        {
            ++level;
            ++levels_gained;
        }

        return levels_gained;
    }

    void add_enemy_kill()
    {
        ++enemy_kill_count;
        contribution += contribution_per_ek;
    }

    // Remove experience as death penalty. Clamps to level floor (no de-leveling).
    // Level 1 players are immune. Returns amount actually removed.
    auto remove_experience(int64_t amount) -> int64_t
    {
        if (level <= 1 || amount <= 0)
            return 0;

        int64_t floor = exp_table.exp_for_level(level);
        int64_t removable = experience - floor;
        if (removable <= 0)
            return 0;

        int64_t actual = std::min(amount, removable);
        experience -= actual;
        return actual;
    }
};

// Stat points awarded per level
struct stat_points
{
    int16_t available{0};
    int16_t used{0};

    [[nodiscard]] auto total() const -> int16_t { return available + used; }

    auto allocate(int16_t amount) -> bool
    {
        if (amount > available)
            return false;
        available -= amount;
        used += amount;
        return true;
    }

    void award(int16_t amount) { available += amount; }
};

} // namespace hb::player
