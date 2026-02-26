// item_upgrade.cpp
// Item upgrade system (Xelima/Merien stones)

#include "item/item_upgrade.h"

#include <limits>
#include <random>

namespace hb::item
{

auto is_valid_upgrade_stone(const item& target_item, uint32_t stone_template_id) -> bool
{
    if (stone_template_id == xelima_stone_id)
    {
        // Xelima stones upgrade weapons
        return target_item.type == item_type::weapon;
    }
    if (stone_template_id == merien_stone_id)
    {
        // Merien stones upgrade armor, shields, and accessories
        return target_item.type == item_type::armor || target_item.type == item_type::accessory;
    }
    return false;
}

auto attempt_upgrade(item& target_item) -> upgrade_result
{
    auto current_level = target_item.attribute.upgrade_level;

    // Already at max
    if (current_level >= max_upgrade_level)
    {
        return {.success = false, .new_level = current_level, .stone_consumed = true};
    }

    // Get base probability (out of 10000)
    int prob = upgrade_base_prob[current_level];

    // Custom-made bonus: positive quality crafted items get upgrade probability boost
    // Legacy bug: original checked > 100 but sItemSpecEffectValue2 maxes at 100, so never triggered
    if (target_item.attribute.custom_made && target_item.attribute.custom_quality > 0)
    {
        int quality = target_item.attribute.custom_quality;
        // Legacy tiered bonus based on current probability
        if (prob > 2000)
            prob += quality * 10; // quality/10 * 100 (already scaled to 10000)
        else if (prob > 700)
            prob += quality * 5; // quality/20 * 100
        else
            prob += quality * 2; // quality/40 * 100 (approx)
    }

    // Roll
    thread_local std::mt19937 rng{std::random_device{}()};
    std::uniform_int_distribution<int> dist(1, 10000);
    int roll = dist(rng);

    if (prob >= roll)
    {
        uint8_t new_level = static_cast<uint8_t>(std::min<int>(current_level + 1, max_upgrade_level));
        target_item.attribute.upgrade_level = new_level;

        // Increase max durability on successful upgrade (legacy behavior)
        // Custom-made items get +20%, non-custom get +15%
        if (target_item.max_durability > 0)
        {
            double rate = target_item.attribute.custom_made ? 0.20 : 0.15;
            double new_max = target_item.max_durability * (1.0 + rate);
            // Clamp to int16_t max to prevent overflow
            if (new_max > static_cast<double>(std::numeric_limits<int16_t>::max()))
                new_max = static_cast<double>(target_item.max_durability);
            target_item.max_durability = static_cast<int16_t>(new_max);
        }

        return {.success = true, .new_level = new_level, .stone_consumed = true};
    }

    return {.success = false, .new_level = current_level, .stone_consumed = true};
}

} // namespace hb::item
