#pragma once

// item_serialization.h
// Universal item-to-JSON serialization, independent of the protocol layer.

#include "item/item.h"
#include "item/item_attribute.h"
#include "item/special_ability.h"

#include <nlohmann/json_fwd.hpp>
#include <string_view>

namespace hb::item
{

// --- Enum-to-string converters ---

[[nodiscard]] auto item_type_to_string(item_type t) -> std::string_view;
[[nodiscard]] auto equip_pos_to_string(equip_pos p) -> std::string_view;
[[nodiscard]] auto weapon_type_to_string(weapon_type w) -> std::string_view;
[[nodiscard]] auto rarity_to_string(item_rarity r) -> std::string_view;
[[nodiscard]] auto effect_type_to_string(item_effect_type t) -> std::string_view;
[[nodiscard]] auto special_ability_type_to_string(special_ability_type t) -> std::string_view;
[[nodiscard]] auto enchantment_type_to_string(enchantment_type t) -> std::string_view;
[[nodiscard]] auto sub_enchantment_type_to_string(sub_enchantment_type t) -> std::string_view;

// --- Serialization ---

// Serialize an item instance to the universal JSON shape.
// See docs/protocol/items.md for field descriptions.
[[nodiscard]] auto serialize_item(const item& itm) -> nlohmann::json;

} // namespace hb::item
