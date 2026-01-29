#pragma once

// war_system.h
// War management subsystem

#include "core/types.h"
#include "core/result.h"
#include "core/subsystem.h"
#include "war/war_types.h"
#include "war/territory.h"

#include <unordered_map>
#include <string_view>
#include <functional>
#include <vector>
#include <optional>

namespace hb::war {

// War events
struct war_started_event {
    war_id war{};
    war_type type{};
};

struct war_ended_event {
    war_id war{};
    war_type type{};
    war_faction winner{war_faction::neutral};
    bool draw{false};
};

struct war_phase_changed_event {
    war_id war{};
    war_phase old_phase{};
    war_phase new_phase{};
};

struct participant_joined_event {
    war_id war{};
    player_id player{};
    war_faction faction{};
};

struct participant_killed_event {
    war_id war{};
    player_id victim{};
    player_id killer{};
};

struct objective_captured_event {
    war_id war{};
    uint16_t objective_id{};
    war_faction old_faction{};
    war_faction new_faction{};
};

struct territory_captured_event {
    territory_id territory{};
    war_faction old_faction{};
    war_faction new_faction{};
};

// War configuration
struct war_config {
    // Timing
    int32_t preparation_seconds{300};       // 5 minutes prep
    int32_t duration_seconds{3600};         // 1 hour combat
    int32_t ending_seconds{60};             // 1 minute ending

    // Respawning
    int32_t base_respawn_seconds{30};
    int32_t max_respawn_seconds{120};
    int32_t respawn_increment_per_death{5};

    // Scoring
    int32_t kill_score{1};
    int32_t objective_score{10};
    int32_t main_objective_score{50};

    // Rewards
    float winner_reward_mult{1.5f};
    float loser_reward_mult{0.5f};
    float participation_reward_mult{1.0f};
};

// War system configuration
struct war_system_config {
    bool enable_crusade{true};
    bool enable_heldenian{true};
    bool enable_apocalypse{false};
    int32_t max_concurrent_wars{3};
    war_config default_war_config{};
};

// War state container
struct war_state {
    war_id id{};
    war_type type{war_type::crusade};
    war_status state{war_status::inactive};
    war_phase phase{war_phase::none};
    war_config config{};

    // Timing
    std::chrono::system_clock::time_point scheduled_start{};
    std::chrono::system_clock::time_point started_at{};
    std::chrono::system_clock::time_point ends_at{};
    int32_t elapsed_seconds{0};

    // Scores
    faction_score aresden_score{war_faction::aresden};
    faction_score elvine_score{war_faction::elvine};

    // Participants
    std::unordered_map<player_id, war_participant> participants;

    // Objectives
    std::vector<war_objective> objectives;

    // Spawn points
    std::vector<war_spawn_point> aresden_spawns;
    std::vector<war_spawn_point> elvine_spawns;

    // Maps involved
    std::vector<map_id> war_maps;

    [[nodiscard]] auto is_active() const -> bool {
        return state == war_status::active;
    }

    [[nodiscard]] auto is_running() const -> bool {
        return state == war_status::preparing ||
               state == war_status::active ||
               state == war_status::ending;
    }

    [[nodiscard]] auto get_faction_score(war_faction faction) -> faction_score& {
        return faction == war_faction::aresden ? aresden_score : elvine_score;
    }

    [[nodiscard]] auto get_faction_score(war_faction faction) const -> const faction_score& {
        return faction == war_faction::aresden ? aresden_score : elvine_score;
    }

    [[nodiscard]] auto participant_count() const -> size_t {
        return participants.size();
    }

    [[nodiscard]] auto get_participant(player_id player) -> war_participant* {
        auto it = participants.find(player);
        return it != participants.end() ? &it->second : nullptr;
    }

    [[nodiscard]] auto get_participant(player_id player) const -> const war_participant* {
        auto it = participants.find(player);
        return it != participants.end() ? &it->second : nullptr;
    }

    [[nodiscard]] auto is_participant(player_id player) const -> bool {
        return participants.contains(player);
    }

    [[nodiscard]] auto get_leading_faction() const -> war_faction {
        if (aresden_score.total_score > elvine_score.total_score) {
            return war_faction::aresden;
        } else if (elvine_score.total_score > aresden_score.total_score) {
            return war_faction::elvine;
        }
        return war_faction::neutral;  // Tie
    }
};

// War operation results
enum class war_result_code : uint8_t {
    success = 0,
    war_not_found = 1,
    already_participant = 2,
    not_participant = 3,
    war_not_active = 4,
    wrong_faction = 5,
    max_wars_reached = 6,
    invalid_war_type = 7,
    war_already_running = 8,
};

// War system - manages faction wars and territory
class war_system : public subsystem {
public:
    using war_callback = std::function<void(const war_started_event&)>;
    using war_end_callback = std::function<void(const war_ended_event&)>;
    using territory_callback = std::function<void(const territory_captured_event&)>;

    war_system();
    ~war_system() override;

    // Subsystem interface
    [[nodiscard]] auto name() const -> std::string_view override { return "war_system"; }
    void initialize() override;
    void shutdown() override;
    void update(float delta_time) override;

    // Configuration
    void set_config(const war_system_config& config);

    // ========== War Lifecycle ==========

    auto schedule_war(war_type type, std::chrono::system_clock::time_point start_time) -> result<war_id, war_result_code>;
    auto start_war(war_type type) -> result<war_id, war_result_code>;
    auto end_war(war_id id) -> war_result_code;
    auto cancel_war(war_id id) -> war_result_code;

    // ========== War Queries ==========

    [[nodiscard]] auto get_war(war_id id) -> war_state*;
    [[nodiscard]] auto get_war(war_id id) const -> const war_state*;
    [[nodiscard]] auto get_active_war(war_type type) const -> std::optional<war_id>;
    [[nodiscard]] auto get_all_active_wars() const -> std::vector<war_id>;
    [[nodiscard]] auto is_war_active(war_type type) const -> bool;
    [[nodiscard]] auto active_war_count() const -> size_t;

    // ========== Participation ==========

    auto join_war(war_id war, player_id player, const std::string& player_name, war_faction faction)
        -> war_result_code;
    auto leave_war(war_id war, player_id player) -> war_result_code;
    auto respawn_participant(war_id war, player_id player) -> bool;

    // ========== Combat Events ==========

    void record_kill(war_id war, player_id killer, player_id victim);
    void record_assist(war_id war, player_id assister, player_id victim);
    void record_damage(war_id war, player_id dealer, int32_t damage);
    void record_healing(war_id war, player_id healer, int32_t amount);

    // ========== Objectives ==========

    void update_objective_capture(war_id war, uint16_t objective_id, war_faction capturing, int32_t progress);
    auto get_objective(war_id war, uint16_t objective_id) -> war_objective*;

    // ========== Territory ==========

    void register_territory(territory t);
    [[nodiscard]] auto get_territory(territory_id id) -> territory*;
    [[nodiscard]] auto get_territory(territory_id id) const -> const territory*;
    [[nodiscard]] auto get_territory_at(map_id map, int16_t x, int16_t y) const -> territory_id;
    [[nodiscard]] auto get_faction_territories(war_faction faction) const -> std::vector<territory_id>;
    [[nodiscard]] auto get_faction_summary(war_faction faction) const -> faction_territory_summary;

    auto capture_territory(territory_id id, war_faction new_faction) -> bool;
    [[nodiscard]] auto territory_count() const -> size_t;

    // ========== Rewards ==========

    [[nodiscard]] auto calculate_rewards(war_id war, player_id player) const -> war_rewards;

    // ========== Callbacks ==========

    void on_war_started(war_callback callback);
    void on_war_ended(war_end_callback callback);
    void on_territory_captured(territory_callback callback);

    // ========== Player Queries ==========

    [[nodiscard]] auto get_player_faction(player_id player) const -> war_faction;
    void set_player_faction(player_id player, war_faction faction);
    [[nodiscard]] auto is_player_in_war(player_id player) const -> bool;
    [[nodiscard]] auto get_player_war(player_id player) const -> war_id;

private:
    void update_war(war_state& war, float delta_time);
    void update_war_timers(war_state& war, float delta_time);
    void update_objectives(war_state& war, float delta_time);
    void transition_war_state(war_state& war, war_status new_state);
    void finalize_war(war_state& war);
    auto determine_winner(const war_state& war) const -> war_faction;
    auto generate_war_id() -> war_id;

    war_system_config config_;

    // Wars
    std::unordered_map<war_id, war_state> wars_;
    std::unordered_map<war_type, war_id> active_war_by_type_;
    uint32_t next_war_id_{1};

    // Territories
    std::unordered_map<territory_id, territory> territories_;

    // Player tracking
    std::unordered_map<player_id, war_faction> player_factions_;
    std::unordered_map<player_id, war_id> player_wars_;

    // Callbacks
    std::vector<war_callback> war_started_callbacks_;
    std::vector<war_end_callback> war_ended_callbacks_;
    std::vector<territory_callback> territory_captured_callbacks_;
};

}  // namespace hb::war
