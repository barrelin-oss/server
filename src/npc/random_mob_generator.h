#pragma once

// random_mob_generator.h
// Level-based random NPC spawning tables
//
// Based on legacy CGame::MobGenerator() logic
// Maps random_mob_generator level to weighted NPC spawn tables

#include "core/types.h"
#include "npc/npc.h"

#include <string_view>
#include <optional>

namespace hb::npc
{

// Result of random NPC selection
struct random_npc_choice
{
    std::string_view npc_name;
    npc_id template_id;
};

// Random mob generator - selects NPCs based on map level
class random_mob_generator
{
public:
    // Get a random NPC for the given generator level
    // Returns nullopt if level is invalid or disabled
    static auto get_random_npc(int level) -> std::optional<random_npc_choice>;

private:
    // Level-specific spawn tables
    static auto select_level_1() -> random_npc_choice; // arefarm, elvfarm, towns
    static auto select_level_2() -> random_npc_choice; // Basic dungeons
    static auto select_level_3() -> random_npc_choice; // Mid-level areas
    static auto select_level_4() -> random_npc_choice; // dungeons
    static auto select_level_5() -> random_npc_choice; // Advanced areas
    static auto select_level_6() -> random_npc_choice; // High-level zones
    static auto select_level_7() -> random_npc_choice; // Elite zones
};

} // namespace hb::npc
