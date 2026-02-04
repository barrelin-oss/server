#pragma once

#include "world/position.h"
#include "world/world_subsystem.h"
#include <optional>
#include <string_view>

namespace hb
{
class game_clock;
}

namespace hb::npc
{

// Bundles all available spawn context into a single struct
struct spawn_context
{
    // Core context
    std::string_view map_name;
    world::position pos;
    const world::static_tile* tile = nullptr;
    const world::dynamic_tile* dyn_tile = nullptr;
    const world::map* map_ptr = nullptr;
    const game_clock* clock = nullptr;
    world::weather_type weather = world::weather_type::clear;
    std::string_view active_event;

    // Lazy-evaluated queries (cached)
    mutable std::optional<int> nearest_player_distance_;
    mutable std::optional<int> player_count_nearby_;

    // Helper methods - terrain queries
    auto is_water() const -> bool;
    auto is_farm() const -> bool;
    auto is_safe_zone() const -> bool;
    auto is_fight_zone() const -> bool;

    // Helper methods - time queries
    auto is_day() const -> bool;
    auto is_night() const -> bool;
    auto is_dawn() const -> bool;
    auto is_dusk() const -> bool;
    auto get_hour() const -> int;

    // Helper methods - player proximity (lazy-evaluated, cached)
    auto get_nearest_player_distance() const -> int;
    auto get_player_count_in_range(int radius) const -> int;
};

} // namespace hb::npc
