#pragma once

// war_types.h
// War system type definitions

#include "core/types.h"

#include <string>
#include <vector>
#include <chrono>
#include <cstdint>

namespace hb::war {

// War ID type
struct war_id {
    uint32_t value{0};

    constexpr war_id() = default;
    constexpr explicit war_id(uint32_t v) : value(v) {}
    constexpr war_id(int v) : value(static_cast<uint32_t>(v)) {}

    constexpr auto operator<=>(const war_id&) const = default;
    [[nodiscard]] constexpr auto is_valid() const -> bool { return value != 0; }
};

// Territory ID type
struct territory_id {
    uint16_t value{0};

    constexpr territory_id() = default;
    constexpr explicit territory_id(uint16_t v) : value(v) {}
    constexpr territory_id(int v) : value(static_cast<uint16_t>(v)) {}

    constexpr auto operator<=>(const territory_id&) const = default;
    [[nodiscard]] constexpr auto is_valid() const -> bool { return value != 0; }
};

// Faction type
enum class war_faction : uint8_t {
    neutral = 0,
    aresden = 1,
    elvine = 2,
};

// War type
enum class war_type : uint8_t {
    crusade = 0,        // Timed faction war for territory
    heldenian = 1,      // Special battle event
    apocalypse = 2,     // PvE/PvP world event
    siege = 3,          // Castle siege
    guild_war = 4,      // Guild vs guild
};

// War status
enum class war_status : uint8_t {
    inactive = 0,       // War not running
    scheduled = 1,      // War scheduled to start
    preparing = 2,      // Preparation phase
    active = 3,         // Combat phase
    ending = 4,         // Ending/cleanup phase
    completed = 5,      // War finished
};

// War phase (for multi-phase wars)
enum class war_phase : uint8_t {
    none = 0,
    phase_1 = 1,        // Initial phase
    phase_2 = 2,        // Mid phase
    phase_3 = 3,        // Final phase
    overtime = 4,       // Tiebreaker
};

// Participant status
enum class participant_status : uint8_t {
    alive = 0,
    dead = 1,
    fled = 2,
    disconnected = 3,
};

// War contribution type
enum class contribution_type : uint8_t {
    kill = 0,
    death = 1,
    objective_capture = 2,
    objective_defend = 3,
    healing = 4,
    damage_dealt = 5,
    flag_capture = 6,
    assist = 7,
};

// War contribution record
struct war_contribution {
    player_id player{};
    contribution_type type{};
    int32_t amount{1};
    std::chrono::system_clock::time_point timestamp{};
};

// War participant
struct war_participant {
    player_id player{};
    std::string name;
    war_faction faction{war_faction::neutral};
    participant_status status{participant_status::alive};

    // Statistics
    int32_t kills{0};
    int32_t deaths{0};
    int32_t assists{0};
    int32_t damage_dealt{0};
    int32_t damage_taken{0};
    int32_t healing_done{0};
    int32_t objectives_captured{0};
    int32_t objectives_defended{0};
    int32_t flags_captured{0};

    // Contribution score
    int32_t contribution_score{0};

    // Timing
    std::chrono::system_clock::time_point joined_at{};
    std::chrono::system_clock::time_point last_death{};
    int32_t respawn_timer_seconds{0};

    [[nodiscard]] auto kd_ratio() const -> float {
        if (deaths == 0) return static_cast<float>(kills);
        return static_cast<float>(kills) / static_cast<float>(deaths);
    }

    void record_kill() {
        ++kills;
        contribution_score += 10;
    }

    void record_death() {
        ++deaths;
        last_death = std::chrono::system_clock::now();
    }

    void record_assist() {
        ++assists;
        contribution_score += 5;
    }
};

// Faction score tracking
struct faction_score {
    war_faction faction{war_faction::neutral};
    int32_t kills{0};
    int32_t deaths{0};
    int32_t objectives{0};
    int32_t total_score{0};
    int32_t participant_count{0};

    void add_kill() { ++kills; total_score += 1; }
    void add_death() { ++deaths; }
    void add_objective() { ++objectives; total_score += 10; }
};

// War objective (capture point, flag, etc.)
struct war_objective {
    uint16_t id{0};
    std::string name;
    map_id location_map{};
    int16_t location_x{0};
    int16_t location_y{0};
    int16_t capture_radius{5};

    war_faction controlling_faction{war_faction::neutral};
    int32_t capture_progress{0};        // 0-100
    war_faction capturing_faction{war_faction::neutral};

    int32_t score_per_tick{1};          // Score per tick while owned
    bool can_be_captured{true};
    bool is_main_objective{false};

    [[nodiscard]] auto is_captured() const -> bool {
        return capture_progress >= 100;
    }

    [[nodiscard]] auto is_contested() const -> bool {
        return capturing_faction != war_faction::neutral &&
               capturing_faction != controlling_faction;
    }
};

// Spawn point for war respawns
struct war_spawn_point {
    map_id map{};
    int16_t x{0};
    int16_t y{0};
    int16_t radius{5};
    war_faction faction{war_faction::neutral};  // neutral = any faction can use
};

// War rewards
struct war_rewards {
    int64_t experience{0};
    int64_t gold{0};
    int32_t contribution_points{0};
    int32_t honor_points{0};
    std::vector<item_id> items;

    [[nodiscard]] auto has_rewards() const -> bool {
        return experience > 0 || gold > 0 || contribution_points > 0 ||
               honor_points > 0 || !items.empty();
    }
};

// War result
struct war_result {
    war_id id{};
    war_type type{};
    war_faction winner{war_faction::neutral};
    bool draw{false};

    faction_score aresden_score{};
    faction_score elvine_score{};

    std::chrono::system_clock::time_point started_at{};
    std::chrono::system_clock::time_point ended_at{};
    int32_t duration_seconds{0};

    std::vector<war_participant> participants;
    std::vector<war_contribution> contributions;

    [[nodiscard]] auto winning_score() const -> const faction_score& {
        return winner == war_faction::aresden ? aresden_score : elvine_score;
    }

    [[nodiscard]] auto losing_score() const -> const faction_score& {
        return winner == war_faction::aresden ? elvine_score : aresden_score;
    }
};

}  // namespace hb::war

// Hash specializations
namespace std {
template<>
struct hash<hb::war::war_id> {
    auto operator()(const hb::war::war_id& id) const noexcept -> size_t {
        return hash<uint32_t>{}(id.value);
    }
};

template<>
struct hash<hb::war::territory_id> {
    auto operator()(const hb::war::territory_id& id) const noexcept -> size_t {
        return hash<uint16_t>{}(id.value);
    }
};
}  // namespace std
