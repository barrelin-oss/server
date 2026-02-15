#pragma once

// bt_context.h
// Shared context for behavior tree execution

#include "npc/bt/bt_node.h"

#include <chrono>
#include <unordered_map>

namespace hb::npc
{
class npc_system;
}

namespace hb::npc::bt
{

struct bt_context
{
    npc_system* npc_sys{nullptr};
    float delta_time{0.0f};

    // Per-node persistent state (keyed by node pointer)
    std::unordered_map<const bt_node*, int> run_counts;
    std::unordered_map<const bt_node*, std::chrono::steady_clock::time_point> cooldowns;
};

} // namespace hb::npc::bt
