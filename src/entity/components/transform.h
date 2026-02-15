#pragma once

// transform.h
// Transform component - position, facing, and map location

#include "core/types.h"
#include "world/position.h"

namespace hb::entity
{

// Transform component - where is this entity?
struct transform
{
    map_id map{0};                                    // Which map
    world::position pos{0, 0};                        // Position on map
    world::direction facing{world::direction::south}; // Direction facing

    transform() = default;
    transform(map_id m, world::position p, world::direction f = world::direction::south) : map(m), pos(p), facing(f) {}
    transform(map_id m, int16_t x, int16_t y, world::direction f = world::direction::south)
        : map(m)
        , pos{x, y}
        , facing(f)
    {
    }
};

// Velocity component - for moving entities
struct velocity
{
    float dx{0.0f}; // Tiles per second
    float dy{0.0f};

    [[nodiscard]] auto speed() const -> float { return std::sqrt(dx * dx + dy * dy); }

    [[nodiscard]] auto is_moving() const -> bool { return dx != 0.0f || dy != 0.0f; }
};

} // namespace hb::entity
