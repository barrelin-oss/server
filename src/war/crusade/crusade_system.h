#pragma once

// crusade_system.h
// Crusade warfare subsystem — manages crusade lifecycle, strike points, and player war state

#include "core/types.h"
#include "core/result.h"
#include "core/subsystem.h"
#include "war/crusade/crusade_types.h"
#include "war/crusade/mana_system.h"
#include "war/crusade/meteor_handler.h"

#include <algorithm>
#include <functional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

namespace hb {
    class scheduler;
}

namespace hb::war {
    class war_system;
    class war_persistence;
    class force_recall_system;
}

namespace hb::player {
    class player_system;
    struct player;
}

namespace hb::world {
    class world_subsystem;
}

namespace hb::npc {
    class npc_system;
}

namespace hb::social {
    class social_system;
}

namespace hb::effect {
    class effect_system;
}

namespace hb::network {
    class websocket_server;
    struct json_message;
}

namespace hb::war {

// Broadcast callback — crusade_system calls this to send messages to players
using crusade_broadcast_fn = std::function<void(player_id, const network::json_message&)>;
using crusade_broadcast_all_fn = std::function<void(const network::json_message&)>;

// Result codes
enum class crusade_result : uint8_t {
    success = 0,
    not_active = 1,
    already_active = 2,
    invalid_duty = 3,
    already_has_duty = 4,
    not_in_crusade = 5,
    not_in_faction = 6,
    config_error = 7,
    not_constructor = 8,
    insufficient_points = 9,
    invalid_unit = 10,
    not_guild_master = 11,
    no_construct_location = 12,
    too_far_from_construct_location = 13,
    guild_build_limit = 14,
    too_close_to_tower = 15,
    invalid_position = 16,
    restricted_map = 17,
    duplicate_today = 18,
    wrong_map = 19,
};

class crusade_system : public subsystem {
public:
    crusade_system();
    ~crusade_system() override;

    // Subsystem interface
    [[nodiscard]] auto name() const -> std::string_view override { return "crusade_system"; }
    void initialize() override;
    void shutdown() override;
    void update(float delta_time) override;

    // Dependencies
    void set_dependencies(war_system* war,
                          player::player_system* players,
                          world::world_subsystem* world,
                          npc::npc_system* npcs,
                          scheduler* sched);
    void set_social(social::social_system* social) { social_ = social; }
    void set_effects(effect::effect_system* effects) { effects_ = effects; }
    void set_persistence(war_persistence* persistence) { persistence_ = persistence; }
    void set_force_recall(force_recall_system* fr) { force_recall_ = fr; }

    // Configuration
    void set_config(const crusade_config& config);
    [[nodiscard]] auto config() const -> const crusade_config& { return config_; }

    // Broadcast callbacks
    void set_broadcast_fn(crusade_broadcast_fn fn) { broadcast_fn_ = std::move(fn); }
    void set_broadcast_all_fn(crusade_broadcast_all_fn fn) { broadcast_all_fn_ = std::move(fn); }

    // ========== Lifecycle ==========

    auto start_crusade() -> result<war_id, crusade_result>;
    auto end_crusade(war_faction winner) -> crusade_result;
    void cancel_crusade();

    [[nodiscard]] auto is_active() const -> bool { return active_; }
    [[nodiscard]] auto current_war_id() const -> war_id { return current_war_id_; }
    [[nodiscard]] auto crusade_guid() const -> uint64_t { return crusade_guid_; }
    [[nodiscard]] auto elapsed_seconds() const -> int32_t { return elapsed_seconds_; }

    // ========== Strike Points ==========

    [[nodiscard]] auto get_strike_points(war_faction faction) const -> const std::vector<strike_point>&;
    auto damage_strike_point(war_faction faction, uint16_t point_id, int32_t damage) -> bool;
    [[nodiscard]] auto all_strike_points_destroyed(war_faction faction) const -> bool;

    // ========== Player War State ==========

    auto join_crusade(player_id pid, war_faction faction) -> crusade_result;
    void leave_crusade(player_id pid);
    auto select_duty(player_id pid, crusade_duty duty) -> crusade_result;

    [[nodiscard]] auto get_player_data(player_id pid) -> crusade_player_data*;
    [[nodiscard]] auto get_player_data(player_id pid) const -> const crusade_player_data*;
    [[nodiscard]] auto is_in_crusade(player_id pid) const -> bool;
    [[nodiscard]] auto participant_count() const -> size_t { return player_data_.size(); }

    // ========== Construction Points & Combat ==========

    void award_construction_points(player_id pid, int32_t points);
    void award_contribution(player_id pid, int32_t amount);

    // Called when a player kills an enemy player during crusade
    void on_player_kill(player_id killer, player_id victim, int32_t victim_level, int32_t exp_reward);

    // Called when a player kills an enemy NPC during crusade
    void on_npc_kill(player_id killer, int16_t npc_sprite_id);

    // Called when a player kills a friendly war NPC (penalty)
    void on_friendly_npc_kill(player_id killer);

    // Transfer accumulated construction points from fighters to their guild commander
    void transfer_construction_points();

    // NPC kill reward lookup (construction, contribution) by sprite ID
    [[nodiscard]] static auto get_npc_kill_rewards(int16_t npc_sprite_id) -> std::pair<int32_t, int32_t>;

    // ========== Guild Construct Location ==========

    auto set_guild_construct_location(player_id commander, const std::string& map, int16_t x, int16_t y) -> crusade_result;
    [[nodiscard]] auto get_guild_construct_location(uint32_t guild_id) const -> const guild_construct_location*;

    // ========== Guild Teleport ==========

    auto set_guild_teleport_location(player_id commander, const std::string& map, int16_t x, int16_t y) -> crusade_result;
    auto use_guild_teleport(player_id pid) -> crusade_result;
    [[nodiscard]] auto get_guild_teleport_location(uint32_t guild_id) const -> const guild_teleport_location*;
    [[nodiscard]] auto get_guild_teleport_dest(player_id pid) const -> const guild_teleport_location*;

    // ========== War Unit Summoning ==========

    auto summon_war_unit(player_id pid, war_unit_type type,
                        const std::string& map_name = {}, int16_t x = 0, int16_t y = 0) -> crusade_result;
    [[nodiscard]] auto get_war_structures() const -> const std::vector<war_structure_instance>& { return war_structures_; }
    [[nodiscard]] auto get_war_structures(war_faction faction) const -> std::vector<war_structure_instance>;
    [[nodiscard]] auto count_structures_by_type(war_faction faction, war_unit_type type) const -> int32_t;

    // Add a structure directly (for pre-placed structures and testing)
    void add_war_structure(const war_structure_instance& ws) { war_structures_.push_back(ws); }

    // ========== Mana System ==========

    // Called when a GMG NPC takes damage — reduces mana charges
    void on_gmg_damage(entity::entity eid, int32_t damage);

    [[nodiscard]] auto mana() -> mana_system& { return mana_system_; }
    [[nodiscard]] auto mana() const -> const mana_system& { return mana_system_; }
    void set_mana_config(const mana_config& config) { mana_system_.set_config(config); }

    // ========== Meteor Handler ==========

    [[nodiscard]] auto meteor() -> meteor_handler& { return meteor_handler_; }
    [[nodiscard]] auto meteor() const -> const meteor_handler& { return meteor_handler_; }
    void set_meteor_config(const meteor_config& config) { meteor_handler_.set_config(config); }

    // ========== Previous Winner ==========

    void set_last_winner(war_faction faction) { last_crusade_winner_ = faction; }
    [[nodiscard]] auto last_winner() const -> war_faction { return last_crusade_winner_; }

    // Day tracking for duplicate-in-day prevention
    void set_last_crusade_day(int8_t day) { last_crusade_day_ = day; }
    [[nodiscard]] auto last_crusade_day() const -> int8_t { return last_crusade_day_; }

    // Crusade advantage (-5 to +5, positive = Aresden dominant)
    void set_crusade_advantage(int8_t advantage) { crusade_advantage_ = std::clamp(advantage, int8_t{-5}, int8_t{5}); }
    [[nodiscard]] auto crusade_advantage() const -> int8_t { return crusade_advantage_; }

    // ========== Schedule ==========

    [[nodiscard]] auto check_schedule() const -> bool;

private:
    void reset_strike_points();
    void broadcast_crusade_started();
    void broadcast_crusade_ended(war_faction winner);
    void broadcast_status_update();
    void broadcast_strike_point_update(war_faction faction);
    void check_victory_condition();
    void cleanup_crusade();

    void persist_war_result(war_faction winner);
    void send_to_player(player_id pid, const network::json_message& msg);
    void send_to_all(const network::json_message& msg);
    void setup_mana_and_meteor();
    void tick_mana(float delta_time);
    void tick_construction_transfer(float delta_time);
    void broadcast_mana_update();
    void broadcast_meteor_warning(war_faction target, int32_t time_ms);
    void broadcast_meteor_result(const meteor_event_result& result);
    void broadcast_construction_point_update(player_id pid);
    [[nodiscard]] auto find_guild_commander(player_id fighter) -> player_id;

    // Configuration
    crusade_config config_;

    // State
    bool active_{false};
    war_id current_war_id_{};
    uint64_t crusade_guid_{0};
    uint64_t next_guid_{1};
    int32_t elapsed_seconds_{0};
    float status_broadcast_accumulator_{0.0f};
    float schedule_check_accumulator_{0.0f};
    float mana_tick_accumulator_{0.0f};
    float mana_broadcast_accumulator_{0.0f};

    // Runtime strike point state (copies from config, modified during war)
    std::vector<strike_point> aresden_strike_points_;
    std::vector<strike_point> elvine_strike_points_;

    // Per-player war data
    std::unordered_map<player_id, crusade_player_data> player_data_;

    // War structures spawned during this crusade
    std::vector<war_structure_instance> war_structures_;
    float construction_transfer_accumulator_{0.0f};

    // Guild construct locations (guild_id → location)
    std::unordered_map<uint32_t, guild_construct_location> guild_construct_locations_;

    // Guild teleport locations (guild_id → location), set by commanders
    std::unordered_map<uint32_t, guild_teleport_location> guild_teleport_locations_;

    // Previous winner tracking
    war_faction last_crusade_winner_{war_faction::neutral};

    // Duplicate-in-day prevention (legacy: m_iLatestCrusadeDayOfWeek)
    int8_t last_crusade_day_{-1};  // Day of week of last crusade start (-1 = none)

    // Crusade advantage: tracks consecutive wins (-5 to +5, positive = Aresden dominant)
    int8_t crusade_advantage_{0};

    // Dependencies
    war_system* war_{nullptr};
    player::player_system* players_{nullptr};
    world::world_subsystem* world_{nullptr};
    npc::npc_system* npcs_{nullptr};
    scheduler* scheduler_{nullptr};
    social::social_system* social_{nullptr};
    effect::effect_system* effects_{nullptr};
    war_persistence* persistence_{nullptr};
    force_recall_system* force_recall_{nullptr};

    // Mana & Meteor subsystems
    mana_system mana_system_;
    meteor_handler meteor_handler_;

    // Broadcast callbacks
    crusade_broadcast_fn broadcast_fn_;
    crusade_broadcast_all_fn broadcast_all_fn_;
};

}  // namespace hb::war
