#pragma once

// position.h
// Position and coordinate types for the game world

#include <cstdint>
#include <cmath>
#include <functional>
#include <algorithm>
#include <optional>

namespace hb::world {

// Forward declare direction enum for position::move()
enum class direction : uint8_t;

// Constexpr-compatible abs function
template<typename T>
[[nodiscard]] constexpr auto constexpr_abs(T x) -> T {
    return x < 0 ? -x : x;
}

// 2D position in tile coordinates
struct position {
    int16_t x{0};
    int16_t y{0};

    constexpr position() = default;
    constexpr position(int16_t x_, int16_t y_) : x(x_), y(y_) {}

    [[nodiscard]] constexpr auto operator==(const position& other) const -> bool = default;
    [[nodiscard]] constexpr auto operator!=(const position& other) const -> bool = default;

    // Addition/subtraction
    [[nodiscard]] constexpr auto operator+(const position& other) const -> position {
        return {static_cast<int16_t>(x + other.x), static_cast<int16_t>(y + other.y)};
    }

    [[nodiscard]] constexpr auto operator-(const position& other) const -> position {
        return {static_cast<int16_t>(x - other.x), static_cast<int16_t>(y - other.y)};
    }

    constexpr auto operator+=(const position& other) -> position& {
        x = static_cast<int16_t>(x + other.x);
        y = static_cast<int16_t>(y + other.y);
        return *this;
    }

    constexpr auto operator-=(const position& other) -> position& {
        x = static_cast<int16_t>(x - other.x);
        y = static_cast<int16_t>(y - other.y);
        return *this;
    }

    // Manhattan distance
    [[nodiscard]] constexpr auto manhattan_distance(const position& other) const -> int {
        return constexpr_abs(x - other.x) + constexpr_abs(y - other.y);
    }

    // Euclidean distance squared (avoids sqrt)
    [[nodiscard]] constexpr auto distance_squared(const position& other) const -> int {
        int dx = x - other.x;
        int dy = y - other.y;
        return dx * dx + dy * dy;
    }

    // Euclidean distance
    [[nodiscard]] auto distance(const position& other) const -> float {
        return std::sqrt(static_cast<float>(distance_squared(other)));
    }

    // Chebyshev distance (max of dx, dy) - useful for tile-based movement
    [[nodiscard]] constexpr auto chebyshev_distance(const position& other) const -> int {
        return std::max(constexpr_abs(x - other.x), constexpr_abs(y - other.y));
    }

    // Check if position is within a rectangle
    [[nodiscard]] constexpr auto is_in_rect(int16_t min_x, int16_t min_y, int16_t max_x, int16_t max_y) const -> bool {
        return x >= min_x && x <= max_x && y >= min_y && y <= max_y;
    }

    // Move in a direction (forward declaration - implemented after direction enum)
    [[nodiscard]] constexpr auto move(direction dir) const -> position;
};

// Direction enum (8 directions)
// Values follow standard convention: 0-7 clockwise from north
// Use std::optional<direction> when "no direction" is needed
enum class direction : uint8_t {
    north = 0,
    north_east = 1,
    east = 2,
    south_east = 3,
    south = 4,
    south_west = 5,
    west = 6,
    north_west = 7,
};

// Direction offsets (indexed by direction value 0-7)
constexpr position direction_offset(direction dir) {
    // Lookup table for direction offsets
    constexpr position offsets[] = {
        {0, -1},   // north (0)
        {1, -1},   // north_east (1)
        {1, 0},    // east (2)
        {1, 1},    // south_east (3)
        {0, 1},    // south (4)
        {-1, 1},   // south_west (5)
        {-1, 0},   // west (6)
        {-1, -1},  // north_west (7)
    };
    auto idx = static_cast<uint8_t>(dir);
    if (idx < 8) {
        return offsets[idx];
    }
    return {0, 0};
}

// Move position in direction
[[nodiscard]] constexpr auto move_in_direction(position pos, direction dir) -> position {
    return pos + direction_offset(dir);
}

// Implementation of position::move (after direction_offset is defined)
constexpr auto position::move(direction dir) const -> position {
    return *this + direction_offset(dir);
}

// Get direction from one position to another
// Returns std::nullopt if positions are the same
[[nodiscard]] inline auto direction_to(const position& from, const position& to) -> std::optional<direction> {
    int dx = to.x - from.x;
    int dy = to.y - from.y;

    if (dx == 0 && dy == 0) return std::nullopt;

    // Normalize to -1, 0, 1
    int sx = (dx > 0) ? 1 : (dx < 0) ? -1 : 0;
    int sy = (dy > 0) ? 1 : (dy < 0) ? -1 : 0;

    if (sx == 0 && sy == -1) return direction::north;
    if (sx == 1 && sy == -1) return direction::north_east;
    if (sx == 1 && sy == 0) return direction::east;
    if (sx == 1 && sy == 1) return direction::south_east;
    if (sx == 0 && sy == 1) return direction::south;
    if (sx == -1 && sy == 1) return direction::south_west;
    if (sx == -1 && sy == 0) return direction::west;
    if (sx == -1 && sy == -1) return direction::north_west;

    return std::nullopt;  // Should not reach here
}

// Axis-aligned bounding box
struct rect {
    int16_t min_x{0};
    int16_t min_y{0};
    int16_t max_x{0};
    int16_t max_y{0};

    constexpr rect() = default;
    constexpr rect(int16_t min_x_, int16_t min_y_, int16_t max_x_, int16_t max_y_)
        : min_x(min_x_), min_y(min_y_), max_x(max_x_), max_y(max_y_) {}

    [[nodiscard]] constexpr auto contains(const position& pos) const -> bool {
        return pos.x >= min_x && pos.x <= max_x && pos.y >= min_y && pos.y <= max_y;
    }

    [[nodiscard]] constexpr auto width() const -> int16_t {
        return static_cast<int16_t>(max_x - min_x + 1);
    }

    [[nodiscard]] constexpr auto height() const -> int16_t {
        return static_cast<int16_t>(max_y - min_y + 1);
    }

    [[nodiscard]] constexpr auto intersects(const rect& other) const -> bool {
        return !(max_x < other.min_x || min_x > other.max_x ||
                 max_y < other.min_y || min_y > other.max_y);
    }
};

// Create rect centered on position with given radius
[[nodiscard]] constexpr auto rect_around(const position& center, int16_t radius) -> rect {
    return rect{
        static_cast<int16_t>(center.x - radius),
        static_cast<int16_t>(center.y - radius),
        static_cast<int16_t>(center.x + radius),
        static_cast<int16_t>(center.y + radius)
    };
}

}  // namespace hb::world

// Hash specialization for position
template<>
struct std::hash<hb::world::position> {
    auto operator()(const hb::world::position& pos) const noexcept -> size_t {
        // Combine x and y into a single hash
        return std::hash<uint32_t>{}(
            (static_cast<uint32_t>(static_cast<uint16_t>(pos.x)) << 16) |
            static_cast<uint32_t>(static_cast<uint16_t>(pos.y))
        );
    }
};
