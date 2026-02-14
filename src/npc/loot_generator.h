#pragma once

// loot_generator.h
// Config-driven loot generation using loot_registry
//
// Two drop phases:
//   on_kill   — gold (awarded to killer) + item drops (placed on ground)
//   on_despawn — body parts, rare items, boss multi-drops

#include "core/types.h"
#include "npc/loot_config.h"
#include "item/item_attribute.h"
#include "registry/loot_registry.h"

#include <cstdint>
#include <random>
#include <vector>

namespace hb::npc {

namespace detail {

inline auto loot_rng() -> std::mt19937&
{
    thread_local std::mt19937 rng{std::random_device{}()};
    return rng;
}

// Roll a uniform int in [min, max]
inline auto roll(int min, int max) -> int
{
    if (min >= max) return min;
    std::uniform_int_distribution<int> dist(min, max);
    return dist(loot_rng());
}

// Pick a weighted random item from a pool
inline auto pick_from_pool(const item_pool& pool) -> item_id
{
    if (pool.items.empty() || pool.total_weight <= 0) return item_id{0};

    int r = roll(1, pool.total_weight);
    int cumulative = 0;
    for (const auto& wi : pool.items)
    {
        cumulative += wi.weight;
        if (r <= cumulative)
        {
            return wi.item;
        }
    }
    // Fallback (shouldn't happen)
    return pool.items.back().item;
}

// Generate random item attribute from config
inline auto generate_attribute(const loot_attribute_config& cfg) -> item::item_attribute
{
    item::item_attribute attr;

    // Random upgrade level
    if (cfg.max_upgrade_level > 0)
    {
        attr.upgrade_level = static_cast<uint8_t>(roll(0, cfg.max_upgrade_level));
    }

    // Main enchantment
    if (cfg.enchantment_chance > 0 && roll(1, 100) <= cfg.enchantment_chance)
    {
        // Pick a random enchantment type (1-12)
        attr.main_type = static_cast<item::enchantment_type>(roll(1, 12));
        attr.main_value = static_cast<uint8_t>(roll(1, std::max<int>(1, cfg.max_enchantment_value)));
    }

    // Sub enchantment
    if (cfg.sub_enchantment_chance > 0 && roll(1, 100) <= cfg.sub_enchantment_chance)
    {
        // Pick a random sub enchantment type (1-12)
        attr.sub_type = static_cast<item::sub_enchantment_type>(roll(1, 12));
        attr.sub_value = static_cast<uint8_t>(roll(1, std::max<int>(1, cfg.max_enchantment_value)));
    }

    return attr;
}

}  // namespace detail

// A single dropped item with optional attribute
struct loot_item_result
{
    item_id template_id{};
    item::item_attribute attribute{};  // Empty unless attribute config was present
};

// Result of loot generation
struct loot_result
{
    int32_t gold{0};
    std::vector<loot_item_result> items;
};

// NPC info needed for loot generation
struct loot_npc_info
{
    int16_t sprite_id{0};
    int32_t gold_min{0};
    int32_t gold_max{0};
    bool is_summoned{false};
};

// Generate loot for on_kill phase
inline auto generate_kill_loot(const loot_registry& reg,
                                int16_t sprite_id,
                                int32_t gold_min,
                                int32_t gold_max,
                                bool is_summoned) -> loot_result
{
    loot_result result;

    // Summoned NPCs never drop
    if (is_summoned) return result;

    auto* config = reg.get_config(sprite_id);
    if (!config) return result;

    const auto& phase = config->on_kill;

    // Gold roll (independent)
    if (phase.gold_chance > 0 && detail::roll(1, 10000) <= phase.gold_chance)
    {
        if (gold_max > gold_min)
        {
            result.gold = detail::roll(gold_min, gold_max);
        }
        else
        {
            result.gold = gold_min;
        }
    }

    // Item drops (each pool rolls independently)
    for (const auto& drop : phase.drops)
    {
        if (detail::roll(1, 10000) <= drop.chance)
        {
            auto* pool = reg.get_pool(drop.pool_name);
            if (pool)
            {
                auto tmpl_id = detail::pick_from_pool(*pool);
                if (tmpl_id.is_valid())
                {
                    loot_item_result item_result;
                    item_result.template_id = tmpl_id;
                    if (drop.attribute.has_value())
                    {
                        item_result.attribute = detail::generate_attribute(*drop.attribute);
                    }
                    result.items.push_back(std::move(item_result));
                }
            }
        }
    }

    return result;
}

// Generate loot for on_despawn phase
inline auto generate_despawn_loot(const loot_registry& reg,
                                   int16_t sprite_id) -> loot_result
{
    loot_result result;

    auto* config = reg.get_config(sprite_id);
    if (!config) return result;

    const auto& phase = config->on_despawn;

    // Determine number of roll iterations
    int iterations = 1;
    if (phase.multi_drop.has_value())
    {
        iterations = detail::roll(phase.multi_drop->min_count, phase.multi_drop->max_count);
    }

    for (int i = 0; i < iterations; ++i)
    {
        for (const auto& drop : phase.drops)
        {
            if (detail::roll(1, 10000) <= drop.chance)
            {
                auto* pool = reg.get_pool(drop.pool_name);
                if (pool)
                {
                    auto tmpl_id = detail::pick_from_pool(*pool);
                    if (tmpl_id.is_valid())
                    {
                        loot_item_result item_result;
                        item_result.template_id = tmpl_id;
                        if (drop.attribute.has_value())
                        {
                            item_result.attribute = detail::generate_attribute(*drop.attribute);
                        }
                        result.items.push_back(std::move(item_result));
                    }
                }
            }
        }
    }

    return result;
}

}  // namespace hb::npc
