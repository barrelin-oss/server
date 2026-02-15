#pragma once

// force_recall_system.h
// Force recall — tracks players in enemy territory and teleports them home after a timer

#include "core/types.h"
#include "core/subsystem.h"
#include "war/war_types.h"

#include <functional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace hb::player
{
class player_system;
}

namespace hb::world
{
class world_subsystem;
}

namespace hb::network
{
struct json_message;
}

namespace hb::war
{

// Per-day raid time configuration
struct raid_time_entry
{
    uint8_t day_of_week{0};        // 0=Sunday, 6=Saturday
    int32_t duration_seconds{180}; // How long players can stay in enemy territory
};

// Force recall configuration
struct force_recall_config
{
    bool enabled{true};
    std::vector<raid_time_entry> raid_times; // Per-day settings
    int32_t default_duration_seconds{180};   // 3 minutes default
    int32_t warning_interval_seconds{30};    // How often to send timer warnings
    int32_t override_duration_seconds{0};    // >0 overrides all per-day values
    bool fight_zone_recall_enabled{true};    // Whether fight zone players get recalled
    int32_t fight_zone_duration_seconds{0};  // >0 overrides dynamic even-hour calculation
    int32_t jail_duration_seconds{300};      // 5 minutes for jail maps
};

// Flags describing the territory a player is in
struct territory_flags
{
    bool is_fight_zone{false};
    bool is_jail{false};
};

// Default legacy raid times
inline auto get_default_raid_times() -> std::vector<raid_time_entry>
{
    return {
        {0, 3600}, // Sunday: 60 min
        {1, 180},  // Monday: 3 min
        {2, 180},  // Tuesday: 3 min
        {3, 180},  // Wednesday: 3 min
        {4, 180},  // Thursday: 3 min
        {5, 600},  // Friday: 10 min (legacy code uses 600s despite comments saying 15)
        {6, 2700}, // Saturday: 45 min
    };
}

// Tracked player in enemy territory
struct recall_tracker
{
    player_id pid{};
    war_faction player_faction{war_faction::neutral};
    int32_t time_remaining_seconds{0};
    int32_t max_time_seconds{0};
    bool warned{false};
    bool in_own_town{false};        // Pauses countdown (player in own faction territory during crusade)
    bool set_during_crusade{false}; // Cleared at crusade end for civilians
};

// Callback types
using recall_broadcast_fn = std::function<void(player_id, const network::json_message&)>;
using recall_execute_fn = std::function<void(player_id)>; // Teleport player home
using is_crusade_active_fn = std::function<bool()>;       // Query crusade state
using debuff_removal_fn = std::function<void(player_id)>; // Strip building debuffs

class force_recall_system : public subsystem
{
public:
    force_recall_system() = default;
    ~force_recall_system() override;

    [[nodiscard]] auto name() const -> std::string_view override { return "force_recall_system"; }
    void initialize() override;
    void shutdown() override;
    void update(float delta_time) override;

    void set_config(const force_recall_config& config) { config_ = config; }
    void set_broadcast_fn(recall_broadcast_fn fn) { broadcast_fn_ = std::move(fn); }
    void set_execute_fn(recall_execute_fn fn) { execute_fn_ = std::move(fn); }
    void set_players(player::player_system* players) { players_ = players; }
    void set_world(world::world_subsystem* world) { world_ = world; }
    void set_crusade_check(is_crusade_active_fn fn) { is_crusade_active_ = std::move(fn); }
    void set_debuff_removal_fn(debuff_removal_fn fn) { debuff_removal_fn_ = std::move(fn); }

    // Check if a player is in enemy territory and start tracking them
    void check_player_territory(player_id pid,
                                war_faction player_faction,
                                war_faction map_faction,
                                territory_flags flags = {});

    // Stop tracking a player (left enemy territory or logged out)
    void remove_player(player_id pid);

    // Get the raid duration for the current day of week
    [[nodiscard]] auto get_current_raid_duration() const -> int32_t;

    // Query
    [[nodiscard]] auto is_tracked(player_id pid) const -> bool;
    [[nodiscard]] auto get_tracker(player_id pid) const -> const recall_tracker*;
    [[nodiscard]] auto tracked_count() const -> size_t { return trackers_.size(); }

    // Crusade integration: instant building recall for enemies during crusade
    void check_building_recall(player_id pid, const std::string& map_name, war_faction player_faction);

    // Crusade integration: strip debuffs in building maps
    void check_building_debuffs(player_id pid, const std::string& map_name);

    // Crusade integration: clear timers for players tracked during crusade
    void clear_crusade_trackers();

    // Admin toggle for fight zone recall
    void set_fight_zone_recall(bool enabled);

    // Calculate fight zone duration: time until next even hour minus 2 minutes
    // Testable overload takes explicit hour/minute
    [[nodiscard]] static auto calculate_fight_zone_duration(int hour, int minute) -> int32_t;

    // Building map check
    [[nodiscard]] static auto is_building_map(const std::string& map_name) -> bool;

    // Map faction from building name (buildings ending in _1 = aresden, _2 = elvine)
    [[nodiscard]] static auto get_building_faction(const std::string& map_name) -> war_faction;

private:
    void tick_trackers(float delta_time);
    void check_day_transitions();
    void send_timer_update(player_id pid, int32_t remaining);
    void execute_recall(player_id pid);

    force_recall_config config_;
    float tick_accumulator_{0.0f};
    uint8_t last_checked_day_{255}; // Sentinel: 255 = not yet checked

    std::unordered_map<player_id, recall_tracker> trackers_;

    player::player_system* players_{nullptr};
    world::world_subsystem* world_{nullptr};
    recall_broadcast_fn broadcast_fn_;
    recall_execute_fn execute_fn_;
    is_crusade_active_fn is_crusade_active_;
    debuff_removal_fn debuff_removal_fn_;
};

} // namespace hb::war
