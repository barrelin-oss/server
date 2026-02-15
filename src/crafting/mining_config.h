#pragma once

// mining_config.h
// Data structures for the mining system

#include "core/types.h"
#include "skill/skill.h"

#include <string>
#include <vector>
#include <cstdint>

namespace hb::crafting
{

// A single drop entry within a mineral type
struct mineral_drop
{
    std::string item_name;
    int32_t template_id{}; // resolved via item_registry at load
    int16_t min_skill{};
    int32_t weight{100}; // relative drop weight
};

// Configuration for a mineral type (loaded from mining.yaml)
struct mineral_type_config
{
    int32_t type_id{};     // 1-6
    std::string name;      // "Iron Vein"
    int16_t difficulty{};  // subtracted from skill for success roll
    int32_t max_hits{};    // remaining extractions before depletion
    uint8_t visual_type{}; // 1 = mineral1, 2 = mineral2 (dynamic_object_type)
    std::vector<mineral_drop> drops;
};

// Template ID for the pickaxe item
inline constexpr int32_t pickaxe_template_id = 231;

// Result from a single mining attempt
struct mine_result
{
    bool success{};
    skill::skill_use_result reason{};
    std::string item_name;
    int32_t template_id{};
    int32_t count{1};
    bool node_depleted{};
};

} // namespace hb::crafting
