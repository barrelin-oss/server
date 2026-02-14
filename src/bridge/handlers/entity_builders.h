#pragma once

// entity_builders.h
// Helper functions to build visible_entity_msg for player and NPC spawns

#include "network/json_protocol.h"

namespace hb::player {
    struct player;
}

namespace hb::npc {
    struct npc;
}

namespace hb::item {
    class item_system;
}

namespace hb {
    class item_registry;
}

namespace hb::effect {
    class effect_system;
}

namespace hb::bridge {

// Build a visible_entity_msg for a player entity.
// hostility is relative to the viewing player (e.g. "friendly", "enemy", "neutral").
auto build_player_spawn(
    const player::player& plr,
    std::string_view hostility,
    const item::item_system* items,
    const item_registry* item_reg,
    const effect::effect_system* effects
) -> network::visible_entity_msg;

// Build a visible_entity_msg for an NPC entity.
auto build_npc_spawn(
    const npc::npc& n,
    std::string_view hostility,
    bool is_dead = false
) -> network::visible_entity_msg;

}  // namespace hb::bridge
