#pragma once

// shop_pricing.h
// Pure functions for shop buy/sell/repair pricing and territory checks

#include "npc/shop_config.h"
#include "core/enums.h"

#include <algorithm>
#include <cstdint>
#include <string_view>

namespace hb::npc {

// Calculate buy price with charisma discount.
// Discount: (charisma - 10) / 4 %, capped at 50%.
[[nodiscard]] inline auto calculate_buy_price(int32_t base_price, int16_t count, int16_t charisma) -> int32_t
{
    if (base_price <= 0 || count <= 0) return 0;

    int32_t discount_pct = std::clamp((charisma - 10) / 4, 0, 50);
    int32_t unit_price = base_price - (base_price * discount_pct / 100);
    if (unit_price < 1) unit_price = 1;
    return unit_price * count;
}

// Calculate sell price for equipment based on durability.
// Formula: (cur_dur / max_dur) * 0.5 * base_price. Neutral maps halve again.
// Zero durability = unsellable.
[[nodiscard]] inline auto calculate_sell_price_equipment(int32_t base_price, int16_t cur_dur, int16_t max_dur,
                                                         bool is_neutral_map) -> int32_t
{
    if (base_price <= 0 || max_dur <= 0 || cur_dur <= 0) return 0;

    int32_t price = static_cast<int32_t>(
        static_cast<double>(cur_dur) / max_dur * 0.5 * base_price
    );
    if (is_neutral_map) price /= 2;
    return std::max(price, 1);
}

// Calculate sell price for consumables/stackables.
// Formula: base/2 * count. Neutral maps halve again.
[[nodiscard]] inline auto calculate_sell_price_consumable(int32_t base_price, int16_t count,
                                                          bool is_neutral_map) -> int32_t
{
    if (base_price <= 0 || count <= 0) return 0;

    int32_t price = (base_price / 2) * count;
    if (is_neutral_map) price /= 2;
    return std::max(price, 1);
}

// Calculate repair cost based on missing durability.
// Formula: base/2 - (cur/max * 0.5 * base), min 1.
[[nodiscard]] inline auto calculate_repair_cost(int32_t base_price, int16_t cur_dur, int16_t max_dur) -> int32_t
{
    if (base_price <= 0 || max_dur <= 0) return 0;
    if (cur_dur >= max_dur) return 0;  // Already fully repaired

    double full_repair_value = base_price * 0.5;
    double current_value = static_cast<double>(cur_dur) / max_dur * full_repair_value;
    int32_t cost = static_cast<int32_t>(full_repair_value - current_value);
    return std::max(cost, 1);
}

// Check if a shop accepts items of a given category for buying
[[nodiscard]] inline auto is_category_accepted(const shop_config& shop, uint8_t category) -> bool
{
    return std::find(shop.buy_categories.begin(), shop.buy_categories.end(), category) !=
           shop.buy_categories.end();
}

// Check if a shop can repair items of a given category
[[nodiscard]] inline auto is_category_repairable(const shop_config& shop, uint8_t category) -> bool
{
    return std::find(shop.repair_categories.begin(), shop.repair_categories.end(), category) !=
           shop.repair_categories.end();
}

// Check if a player can buy in the given territory.
// Players cannot buy in hostile territory (e.g., Aresden player in Elvine maps).
// Neutral maps allow all factions. Neutral players can buy everywhere.
[[nodiscard]] inline auto can_buy_in_territory(faction player_faction,
                                                std::string_view map_location) -> bool
{
    if (player_faction == faction::neutral) return true;
    if (map_location.empty()) return true;

    // Check if location belongs to a faction
    bool is_aresden_territory = map_location.find("aresden") != std::string_view::npos ||
                                 map_location.find("Aresden") != std::string_view::npos;
    bool is_elvine_territory = map_location.find("elvine") != std::string_view::npos ||
                                map_location.find("Elvine") != std::string_view::npos;

    if (is_aresden_territory && player_faction != faction::aresden) return false;
    if (is_elvine_territory && player_faction != faction::elvine) return false;

    return true;
}

// Check if a map is considered neutral territory (not belonging to either faction)
[[nodiscard]] inline auto is_neutral_territory(std::string_view map_location) -> bool
{
    if (map_location.empty()) return true;

    bool is_aresden = map_location.find("aresden") != std::string_view::npos ||
                       map_location.find("Aresden") != std::string_view::npos;
    bool is_elvine = map_location.find("elvine") != std::string_view::npos ||
                      map_location.find("Elvine") != std::string_view::npos;

    return !is_aresden && !is_elvine;
}

}  // namespace hb::npc
