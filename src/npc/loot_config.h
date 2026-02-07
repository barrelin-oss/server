#pragma once

// loot_config.h
// Data structures for YAML-driven loot system

#include "core/types.h"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace hb::npc {

// A single weighted item in a pool
struct weighted_item
{
    item_id item{};
    int32_t weight{1};
};

// A named pool of weighted items
struct item_pool
{
    std::string name;
    std::vector<weighted_item> items;
    int32_t total_weight{0};
};

// A reference to a named pool with a drop chance
struct loot_drop_entry
{
    std::string pool_name;
    int16_t chance{0};  // per 10000
};

// Multi-drop config for bosses
struct multi_drop_config
{
    int16_t min_count{1};
    int16_t max_count{1};
};

// Config for one drop phase (on_kill or on_despawn)
struct loot_phase_config
{
    int16_t gold_chance{0};  // per 10000; gold amounts come from npc template
    std::optional<multi_drop_config> multi_drop;
    std::vector<loot_drop_entry> drops;
};

// Complete loot config for an NPC sprite_id
struct npc_loot_config
{
    loot_phase_config on_kill;
    loot_phase_config on_despawn;
};

}  // namespace hb::npc
