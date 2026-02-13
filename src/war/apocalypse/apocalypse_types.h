#pragma once

// apocalypse_types.h
// Type definitions for the Apocalypse gate-toggle event

#include "world/position.h"

#include <string>
#include <vector>
#include <utility>
#include <cstdint>

namespace hb::war {

// Schedule entry for automatic start/end
struct apocalypse_schedule_entry
{
    uint8_t day_of_week{0};
    uint8_t hour{0};
    uint8_t minute{0};
};

// A gate that opens during an apocalypse event
struct apocalypse_gate
{
    std::string map_name;                                       // map where the gate exists
    int16_t gate_x{0};
    int16_t gate_y{0};
    std::string destination_map;                                // empty = notification only, no teleport
    world::position destination_pos{18, 18};
    std::vector<std::pair<int16_t, int16_t>> teleport_tiles;    // tiles that trigger teleport
};

// Apocalypse configuration
struct apocalypse_config
{
    std::vector<apocalypse_schedule_entry> start_schedule;
    std::vector<apocalypse_schedule_entry> end_schedule;
    std::vector<apocalypse_gate> gates;
    std::vector<std::string> apocalypse_maps;   // eject players from these on end
    float gate_check_interval_seconds{3.0f};
};

}  // namespace hb::war
