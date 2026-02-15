#pragma once

// loot_config.h
// Data structures for YAML-driven loot system

#include "core/types.h"
#include "item/item_attribute.h"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace hb::npc
{

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

// Attribute generation config for loot drops
struct loot_attribute_config
{
    uint8_t max_upgrade_level{0};      // Random upgrade 0..N
    uint8_t enchantment_chance{0};     // % chance for main enchantment (0-100)
    uint8_t sub_enchantment_chance{0}; // % chance for sub enchantment (0-100)
    uint8_t max_enchantment_value{15}; // Max value for enchantments (0-15)
};

// A reference to a named pool with a drop chance
struct loot_drop_entry
{
    std::string pool_name;
    int16_t chance{0}; // per 10000
    std::optional<loot_attribute_config> attribute;
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
    int16_t gold_chance{0}; // per 10000; gold amounts come from npc template
    std::optional<multi_drop_config> multi_drop;
    std::vector<loot_drop_entry> drops;
};

// Complete loot config for an NPC sprite_id
struct npc_loot_config
{
    loot_phase_config on_kill;
    loot_phase_config on_despawn;
};

} // namespace hb::npc
