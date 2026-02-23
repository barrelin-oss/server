#pragma once

// equip_mapping.h
// Maps item equip positions to player equipment slots

#include "item/item.h"
#include "player/equipment.h"

#include <optional>

namespace hb::player
{

// Map an item's equip_pos to the corresponding player equip_slot.
// For twohand weapons, maps to the twohand slot.
[[nodiscard]] inline auto
equip_pos_to_slot(item::equip_pos pos) -> std::optional<equip_slot>
{
    switch (pos)
    {
    case item::equip_pos::head:
        return equip_slot::head;
    case item::equip_pos::body:
        return equip_slot::body;
    case item::equip_pos::arms:
        return equip_slot::arms;
    case item::equip_pos::pants:
        return equip_slot::pants;
    case item::equip_pos::boots:
        return equip_slot::boots;
    case item::equip_pos::weapon:
        return equip_slot::weapon;
    case item::equip_pos::shield:
        return equip_slot::shield;
    case item::equip_pos::twohand:
        return equip_slot::twohand;
    case item::equip_pos::ring_left:
        return equip_slot::ring_left;
    case item::equip_pos::ring_right:
        return equip_slot::ring_right;
    case item::equip_pos::amulet:
        return equip_slot::amulet;
    case item::equip_pos::cape:
        return equip_slot::cape;
    case item::equip_pos::angel:
        return equip_slot::angel;
    case item::equip_pos::fullbody:
        return equip_slot::fullbody;
    default:
        return std::nullopt;
    }
}

// Validate that a client-requested equip_slot is valid for an item's equip_pos.
[[nodiscard]] inline auto is_valid_slot_for_item(item::equip_pos pos, equip_slot slot) -> bool
{
    switch (pos)
    {
    case item::equip_pos::head:
        return slot == equip_slot::head;
    case item::equip_pos::body:
        return slot == equip_slot::body;
    case item::equip_pos::arms:
        return slot == equip_slot::arms;
    case item::equip_pos::pants:
        return slot == equip_slot::pants;
    case item::equip_pos::boots:
        return slot == equip_slot::boots;
    case item::equip_pos::weapon:
        return slot == equip_slot::weapon;
    case item::equip_pos::shield:
        return slot == equip_slot::shield;
    case item::equip_pos::twohand:
        return slot == equip_slot::twohand;
    case item::equip_pos::ring_left:
        return slot == equip_slot::ring_left;
    case item::equip_pos::ring_right:
        return slot == equip_slot::ring_right;
    case item::equip_pos::amulet:
        return slot == equip_slot::amulet;
    case item::equip_pos::cape:
        return slot == equip_slot::cape;
    case item::equip_pos::angel:
        return slot == equip_slot::angel;
    case item::equip_pos::fullbody:
        return slot == equip_slot::fullbody;
    default:
        return false;
    }
}

} // namespace hb::player
