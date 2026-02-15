// item_upgrade.cpp
// Item upgrade system (Xelima/Merien stones)

#include "item/item_upgrade.h"

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

    // Custom-made bonus: items with custom_made and quality > 100 get a boost
    // Legacy: sItemSpecEffectValue2 maps to custom_quality
    if (target_item.attribute.custom_made && target_item.attribute.custom_quality > 100)
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
        return {.success = true, .new_level = new_level, .stone_consumed = true};
    }

    return {.success = false, .new_level = current_level, .stone_consumed = true};
}

} // namespace hb::item
