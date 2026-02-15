#include "spawn_context.h"
#include "core/subsystem.h"
#include "player/player_system.h"
#include "scheduler/game_clock.h"

namespace hb::npc
{

// Terrain queries
auto spawn_context::is_water() const -> bool
{
    if (!tile)
        return false;

    // Check if tile is water type
    return tile->is_water();
}

auto spawn_context::is_farm() const -> bool
{
    if (!tile)
        return false;

    // Check if tile is farm type
    return tile->is_farm();
}

auto spawn_context::is_safe_zone() const -> bool
{
    if (!map_ptr)
        return false;

    return !map_ptr->config().is_fight_zone;
}

auto spawn_context::is_fight_zone() const -> bool
{
    if (!map_ptr)
        return false;

    return map_ptr->config().is_fight_zone;
}

// Time queries
auto spawn_context::is_day() const -> bool
{
    if (!clock)
        return true; // Default to day if no clock

    int hour = clock->hour();
    return hour >= 6 && hour < 18; // 6 AM to 6 PM
}

auto spawn_context::is_night() const -> bool
{
    if (!clock)
        return false;

    int hour = clock->hour();
    return hour < 6 || hour >= 18; // 6 PM to 6 AM
}

auto spawn_context::is_dawn() const -> bool
{
    if (!clock)
        return false;

    int hour = clock->hour();
    return hour >= 5 && hour < 7; // 5-7 AM
}

auto spawn_context::is_dusk() const -> bool
{
    if (!clock)
        return false;

    int hour = clock->hour();
    return hour >= 17 && hour < 19; // 5-7 PM
}

auto spawn_context::get_hour() const -> int
{
    if (!clock)
        return 12; // Default to noon

    return clock->hour();
}

// Player proximity queries (lazy-evaluated and cached)
auto spawn_context::get_nearest_player_distance() const -> int
{
    if (!nearest_player_distance_.has_value())
    {
        // Query player system for nearest player
        auto* player_sys = subsystems().get<player::player_system>();
        if (!player_sys)
        {
            nearest_player_distance_ = 999; // No player system, return large distance
            return 999;
        }

        int min_dist = 999;
        player_sys->for_each_player(
            [&](player_id pid, const player::player& p)
            {
                (void)pid; // Unused
                // Only consider players on the same map
                // Compare map names if available
                if (map_ptr && p.current_map == map_ptr->id())
                {
                    int dist = pos.chebyshev_distance(p.pos);
                    min_dist = std::min(min_dist, dist);
                }
            });

        nearest_player_distance_ = min_dist;
    }

    return *nearest_player_distance_;
}

auto spawn_context::get_player_count_in_range(int radius) const -> int
{
    // For now, compute each time (not cached since radius varies)
    auto* player_sys = subsystems().get<player::player_system>();
    if (!player_sys)
        return 0;

    int count = 0;
    player_sys->for_each_player(
        [&](player_id pid, const player::player& p)
        {
            (void)pid; // Unused
            // Only consider players on the same map
            if (map_ptr && p.current_map == map_ptr->id())
            {
                int dist = pos.chebyshev_distance(p.pos);
                if (dist <= radius)
                    ++count;
            }
        });

    return count;
}

} // namespace hb::npc
