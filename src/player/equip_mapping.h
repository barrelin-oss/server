#pragma once

// equip_mapping.h
// Maps item equip positions to player equipment slots

#include "item/item.h"
#include "player/equipment.h"

#include <optional>

namespace hb::player
{

// Map an item's equip_pos to the corresponding player equip_slot.
// For rings, preferred_ring selects which ring slot (defaults to ring_left).
// For twohand weapons, maps to the weapon slot.
[[nodiscard]] inline auto
equip_pos_to_slot(item::equip_pos pos,
                  std::optional<equip_slot> preferred_ring = std::nullopt) -> std::optional<equip_slot>
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
        return equip_slot::weapon;
    case item::equip_pos::ring:
        if (preferred_ring.has_value() &&
            (*preferred_ring == equip_slot::ring_left || *preferred_ring == equip_slot::ring_right))
        {
            return *preferred_ring;
        }
        return equip_slot::ring_left;
    case item::equip_pos::amulet:
        return equip_slot::amulet;
    case item::equip_pos::cape:
        return equip_slot::cape;
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
        return slot == equip_slot::weapon;
    case item::equip_pos::ring:
        return slot == equip_slot::ring_left || slot == equip_slot::ring_right;
    case item::equip_pos::amulet:
        return slot == equip_slot::amulet;
    case item::equip_pos::cape:
        return slot == equip_slot::cape;
    default:
        return false;
    }
}

} // namespace hb::player
