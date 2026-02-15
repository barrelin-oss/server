#pragma once

// item_upgrade.h
// Item upgrade system (Xelima/Merien stones)

#include "item/item.h"

#include <array>
#include <cstdint>

namespace hb::item
{

// Upgrade stone template IDs
inline constexpr uint32_t xelima_stone_id = 656; // Weapon upgrade stone
inline constexpr uint32_t merien_stone_id = 657; // Armor upgrade stone

// Max upgrade level
inline constexpr uint8_t max_upgrade_level = 15;

// Legacy probability table (per-level success rate out of 10000)
inline constexpr std::array<int, 16> upgrade_base_prob = {
    3000, 2500, 2000, 1500, 1000, 1000, 800, 800, 500, 300, 100, 100, 100, 100, 100, 0};

// Upgrade result
struct upgrade_result
{
    bool success{false};
    uint8_t new_level{0};
    bool stone_consumed{true};
};

// Check if stone type matches item type
[[nodiscard]] auto is_valid_upgrade_stone(const item& target_item, uint32_t stone_template_id) -> bool;

// Attempt an upgrade on an item
// Returns: success/failure and new level
// Stone is always consumed (stone_consumed always true)
// Custom-made items with high quality get a probability boost
auto attempt_upgrade(item& target_item) -> upgrade_result;

} // namespace hb::item
