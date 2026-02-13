#pragma once

// heldenian_types.h
// Type definitions for the Heldenian warfare system

#include "core/types.h"
#include "entity/entity.h"
#include "war/war_types.h"

#include <string>
#include <vector>
#include <cstdint>

namespace hb::war {

// Heldenian battle mode
enum class heldenian_mode : uint8_t {
    tower_defense = 1,  // BtField: faction with more surviving towers wins
    door_defense = 2,   // HRampart: attacker wins if all doors destroyed
};

// Schedule entry for heldenian wars
struct heldenian_schedule_entry {
    uint8_t day_of_week{0};
    uint8_t hour{0};
    uint8_t minute{0};
    heldenian_mode mode{heldenian_mode::tower_defense};
};

// A tower or door — destructible war objective
struct heldenian_objective {
    uint16_t id{0};
    std::string map_name;
    int16_t x{0};
    int16_t y{0};
    int32_t hp{0};
    int32_t max_hp{500};
    war_faction faction{war_faction::neutral};
    uint16_t npc_type{0};       // NPC template ID (87/89 for towers, 91 for doors)
    uint8_t direction{0};       // NPC facing direction (doors only)
    entity::entity eid{};       // Runtime: link to spawned NPC entity

    [[nodiscard]] auto is_destroyed() const -> bool { return hp <= 0; }
};

// Teleportation destination for entering the war zone
struct heldenian_teleport_coords {
    std::string map_name;
    int16_t x{0};
    int16_t y{0};
};

// Per-player heldenian state
struct heldenian_player_data {
    player_id pid{};
    war_faction faction{war_faction::neutral};
    int32_t kills{0};
    int32_t deaths{0};
    int32_t construction_points{0};
};

// Timing configuration
struct heldenian_timing_config {
    int32_t preparation_seconds{120};     // 2 minutes prep
    int32_t duration_seconds{2700};       // 45 minutes max
    int32_t status_broadcast_seconds{15}; // Status update interval
};

// Heldenian configuration
struct heldenian_config {
    bool enabled{true};
    heldenian_timing_config timing;
    std::vector<heldenian_schedule_entry> schedule;

    // Tower defense mode (BtField)
    std::vector<heldenian_objective> aresden_towers;
    std::vector<heldenian_objective> elvine_towers;
    std::string tower_map{"BtField"};

    // Door defense mode (HRampart)
    std::vector<heldenian_objective> defender_doors;
    std::string door_map{"HRampart"};

    // Default defending faction for door defense mode
    war_faction default_defender{war_faction::aresden};

    // Teleport coordinates — tower mode (BtField)
    heldenian_teleport_coords tower_aresden_spawn{"BtField", 68, 225};
    heldenian_teleport_coords tower_elvine_spawn{"BtField", 202, 70};

    // Teleport coordinates — door mode (HRampart)
    heldenian_teleport_coords door_defender_spawn{"HRampart", 81, 42};
    heldenian_teleport_coords door_attacker_spawn{"HRampart", 156, 153};

    // Construction points = min(charisma * multiplier, max_points)
    int32_t charisma_multiplier{300};
    int32_t max_construction_points{12000};
};

}  // namespace hb::war
