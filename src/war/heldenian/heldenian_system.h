#pragma once

// heldenian_system.h
// Heldenian warfare subsystem — tower defense and door defense modes

#include "core/types.h"
#include "core/result.h"
#include "core/subsystem.h"
#include "war/heldenian/heldenian_types.h"

#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace hb
{
class scheduler;
}

namespace hb::war
{
class war_system;
class war_persistence;
} // namespace hb::war

namespace hb::player
{
class player_system;
}

namespace hb::world
{
class world_subsystem;
}

namespace hb::npc
{
class npc_system;
}

namespace hb::network
{
struct json_message;
}

namespace hb::war
{

// Broadcast callbacks
using heldenian_broadcast_fn = std::function<void(player_id, const network::json_message&)>;
using heldenian_broadcast_all_fn = std::function<void(const network::json_message&)>;
using heldenian_evacuate_fn = std::function<void(player_id, const std::string&)>;

// Result codes
enum class heldenian_result : uint8_t
{
    success = 0,
    not_active = 1,
    already_active = 2,
    not_in_faction = 3,
    config_error = 4,
};

class heldenian_system : public subsystem
{
public:
    heldenian_system();
    ~heldenian_system() override;

    // Subsystem interface
    [[nodiscard]] auto name() const -> std::string_view override { return "heldenian_system"; }
    void initialize() override;
    void shutdown() override;
    void update(float delta_time) override;

    // Dependencies
    void set_dependencies(war_system* war,
                          player::player_system* players,
                          world::world_subsystem* world,
                          npc::npc_system* npcs,
                          scheduler* sched);

    // Configuration
    void set_config(const heldenian_config& config);
    [[nodiscard]] auto config() const -> const heldenian_config& { return config_; }

    // War persistence (for last-winner tracking across restarts)
    void set_persistence(war_persistence* persistence) { persistence_ = persistence; }

    // Broadcast callbacks
    void set_broadcast_fn(heldenian_broadcast_fn fn) { broadcast_fn_ = std::move(fn); }
    void set_broadcast_all_fn(heldenian_broadcast_all_fn fn) { broadcast_all_fn_ = std::move(fn); }
    void set_evacuate_fn(heldenian_evacuate_fn fn) { evacuate_fn_ = std::move(fn); }

    // ========== Lifecycle ==========

    auto start_heldenian(heldenian_mode mode,
                         war_faction defender = war_faction::neutral) -> result<war_id, heldenian_result>;
    auto end_heldenian(war_faction winner) -> heldenian_result;
    void cancel_heldenian();

    // Evacuation
    void evacuate_map(const std::string& map_name);

    [[nodiscard]] auto is_active() const -> bool { return active_; }
    [[nodiscard]] auto current_war_id() const -> war_id { return current_war_id_; }
    [[nodiscard]] auto current_mode() const -> heldenian_mode { return current_mode_; }
    [[nodiscard]] auto elapsed_seconds() const -> int32_t { return elapsed_seconds_; }

    // ========== Objectives (Towers/Doors) ==========

    [[nodiscard]] auto get_objectives(war_faction faction) const -> const std::vector<heldenian_objective>&;
    auto damage_objective(war_faction faction, uint16_t obj_id, int32_t damage) -> bool;
    [[nodiscard]] auto all_objectives_destroyed(war_faction faction) const -> bool;
    [[nodiscard]] auto count_surviving(war_faction faction) const -> int32_t;

    // NPC death callback — links NPC entity death to objective destruction
    void on_npc_killed(entity::entity eid);

    // ========== Teleportation ==========

    [[nodiscard]] auto
    get_teleport_destination(war_faction player_faction) const -> std::optional<heldenian_teleport_coords>;

    // ========== Player State ==========

    auto join_heldenian(player_id pid, war_faction faction) -> heldenian_result;
    void leave_heldenian(player_id pid);

    [[nodiscard]] auto get_player_data(player_id pid) -> heldenian_player_data*;
    [[nodiscard]] auto get_player_data(player_id pid) const -> const heldenian_player_data*;
    [[nodiscard]] auto is_in_heldenian(player_id pid) const -> bool;
    [[nodiscard]] auto participant_count() const -> size_t { return player_data_.size(); }

    // Kill/death tracking
    void record_kill(player_id killer, player_id victim, war_faction victim_faction);

    // Faction-level death counters
    [[nodiscard]] auto aresden_deaths() const -> int32_t { return aresden_deaths_; }
    [[nodiscard]] auto elvine_deaths() const -> int32_t { return elvine_deaths_; }

    // Previous winner (for tiebreaker)
    void set_last_winner(war_faction faction) { last_winner_ = faction; }
    [[nodiscard]] auto last_winner() const -> war_faction { return last_winner_; }

    // Defending faction (for door defense mode)
    [[nodiscard]] auto defending_faction() const -> war_faction { return defending_faction_; }

    // ========== Schedule ==========

    [[nodiscard]] auto check_schedule() const -> std::optional<heldenian_mode>;

private:
    void reset_objectives();
    void broadcast_heldenian_started();
    void broadcast_heldenian_ended(war_faction winner);
    void broadcast_status_update();
    void broadcast_objective_update(war_faction faction);
    void check_victory_condition();
    void cleanup_heldenian();
    void spawn_objective_npcs();
    void despawn_objective_npcs();

    void send_to_player(player_id pid, const network::json_message& msg);
    void send_to_all(const network::json_message& msg);
    void send_to_war_map(const network::json_message& msg);

    // Configuration
    heldenian_config config_;

    // State
    bool active_{false};
    heldenian_mode current_mode_{heldenian_mode::tower_defense};
    war_faction defending_faction_{war_faction::neutral};
    war_id current_war_id_{};
    int32_t elapsed_seconds_{0};
    float status_broadcast_accumulator_{0.0f};
    float schedule_check_accumulator_{0.0f};

    // Faction-level death counters (for tiebreaker)
    int32_t aresden_deaths_{0};
    int32_t elvine_deaths_{0};

    // Previous winner (for complete-tie tiebreaker)
    war_faction last_winner_{war_faction::neutral};

    // Runtime objective state
    std::vector<heldenian_objective> aresden_objectives_;
    std::vector<heldenian_objective> elvine_objectives_;

    // Per-player data
    std::unordered_map<player_id, heldenian_player_data> player_data_;

    // Dependencies
    war_system* war_{nullptr};
    player::player_system* players_{nullptr};
    world::world_subsystem* world_{nullptr};
    npc::npc_system* npcs_{nullptr};
    scheduler* scheduler_{nullptr};
    war_persistence* persistence_{nullptr};

    // Broadcast callbacks
    heldenian_broadcast_fn broadcast_fn_;
    heldenian_broadcast_all_fn broadcast_all_fn_;
    heldenian_evacuate_fn evacuate_fn_;
};

} // namespace hb::war
