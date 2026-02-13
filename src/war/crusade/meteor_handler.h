#pragma once

// meteor_handler.h
// Multi-stage meteor strike sequence for crusade warfare
// Announce → Strike damage → Player damage waves → Victory check

#include "core/types.h"
#include "war/war_types.h"
#include "war/crusade/crusade_types.h"

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace hb {
    class scheduler;
}

namespace hb::war {

// Meteor strike configuration
struct meteor_config {
    int32_t warning_time_ms{5000};          // Time between announce and impact
    int32_t player_wave1_delay_ms{6000};    // Delay after impact for wave 1
    int32_t player_wave2_delay_ms{9000};    // Delay after impact for wave 2
    int32_t result_delay_ms{11000};         // Delay for final results
    int32_t esg_protection_radius{10};      // Tiles around strike point to check for ESGs
    int32_t base_strike_damage{2};          // Base damage to strike point per meteor
};

// Result of a meteor strike on a single strike point
struct meteor_strike_result {
    uint16_t strike_point_id{0};
    war_faction target_faction{war_faction::neutral};
    int32_t esg_count{0};              // ESG shields near this point
    int32_t damage_applied{0};         // Actual damage dealt
    bool point_destroyed{false};
};

// Full result of a meteor event
struct meteor_event_result {
    war_faction attacking_faction{war_faction::neutral};
    war_faction target_faction{war_faction::neutral};
    std::vector<meteor_strike_result> strike_results;
    int32_t player_casualties{0};
    bool triggered_victory{false};
};

// Callbacks the meteor handler needs
struct meteor_callbacks {
    // Get ESG count near a strike point (within esg_protection_radius tiles)
    std::function<int32_t(war_faction, uint16_t strike_point_id)> get_esg_count;

    // Apply damage to a strike point
    std::function<bool(war_faction, uint16_t strike_point_id, int32_t damage)> damage_strike_point;

    // Get active strike points for a faction
    std::function<std::vector<strike_point>(war_faction)> get_strike_points;

    // Broadcast meteor warning to all players
    std::function<void(war_faction target, int32_t time_ms)> broadcast_warning;

    // Broadcast meteor hit results
    std::function<void(const meteor_event_result&)> broadcast_result;

    // Apply damage to players in a map area (returns count of casualties)
    std::function<int32_t(war_faction target, int32_t damage)> damage_players;
};

// Meteor strike handler
// Manages the multi-stage meteor sequence using the scheduler
class meteor_handler {
public:
    meteor_handler() = default;

    void set_config(const meteor_config& config) { config_ = config; }
    [[nodiscard]] auto get_config() const -> const meteor_config& { return config_; }
    void set_scheduler(scheduler* sched) { scheduler_ = sched; }
    void set_callbacks(meteor_callbacks cbs) { callbacks_ = std::move(cbs); }

    // Trigger a meteor strike by the attacking faction
    // The target is the opposing faction's strike points
    void fire_meteor(war_faction attacking_faction);

    // Cancel all pending meteor sequences (on crusade end)
    void cancel_all();

    [[nodiscard]] auto pending_count() const -> int32_t { return pending_meteors_; }
    [[nodiscard]] auto calculate_player_damage(int32_t player_level) const -> int32_t;

private:
    void execute_meteor_impact(war_faction attacking_faction);
    void execute_player_damage(war_faction target_faction, int32_t wave);
    void execute_result(war_faction attacking_faction, meteor_event_result result);

    [[nodiscard]] auto get_target_faction(war_faction attacker) const -> war_faction;

    meteor_config config_;
    scheduler* scheduler_{nullptr};
    meteor_callbacks callbacks_;
    int32_t pending_meteors_{0};
};

}  // namespace hb::war
