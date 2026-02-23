#pragma once

// equipment.h
// Equipment slots and equipped items

#include "core/types.h"
#include <array>
#include <optional>
#include <utility>
#include <vector>

namespace hb::player
{

// Equipment slot positions
enum class equip_slot : uint8_t
{
    head = 0,
    body = 1,
    arms = 2,
    pants = 3,
    boots = 4,
    weapon = 5,
    shield = 6,
    twohand = 7,
    ring_left = 8,
    ring_right = 9,
    amulet = 10,
    cape = 11,
    angel = 12,
    fullbody = 13,

    count = 14
};

inline constexpr size_t equip_slot_count = static_cast<size_t>(equip_slot::count);

// Get slot name for debugging/display
[[nodiscard]] inline auto equip_slot_name(equip_slot slot) -> const char*
{
    switch (slot)
    {
    case equip_slot::head:
        return "Head";
    case equip_slot::body:
        return "Body";
    case equip_slot::arms:
        return "Arms";
    case equip_slot::pants:
        return "Pants";
    case equip_slot::boots:
        return "Boots";
    case equip_slot::weapon:
        return "Weapon";
    case equip_slot::shield:
        return "Shield";
    case equip_slot::twohand:
        return "Two-Hand";
    case equip_slot::ring_left:
        return "Left Ring";
    case equip_slot::ring_right:
        return "Right Ring";
    case equip_slot::amulet:
        return "Amulet";
    case equip_slot::cape:
        return "Cape";
    case equip_slot::angel:
        return "Angel";
    case equip_slot::fullbody:
        return "Full Body";
    default:
        return "Unknown";
    }
}

// Player equipment state — linked model.
// Slots hold item_id references into the inventory/item_system.
// Actual item data (durability, stats, etc.) is looked up from item_system when needed.
struct equipment_state
{
    void equip(equip_slot slot, item_id id)
    {
        slots_[static_cast<size_t>(slot)] = id;
    }

    auto unequip(equip_slot slot) -> std::optional<item_id>
    {
        auto& s = slots_[static_cast<size_t>(slot)];
        if (!s.is_valid())
            return std::nullopt;
        auto id = s;
        s = item_id{};
        return id;
    }

    [[nodiscard]] auto get_equipped(equip_slot slot) const -> std::optional<item_id>
    {
        auto& s = slots_[static_cast<size_t>(slot)];
        return s.is_valid() ? std::optional{s} : std::nullopt;
    }

    [[nodiscard]] auto has_equipped(equip_slot slot) const -> bool
    {
        return slots_[static_cast<size_t>(slot)].is_valid();
    }

    [[nodiscard]] auto find_slot_for(item_id id) const -> std::optional<equip_slot>
    {
        for (size_t i = 0; i < equip_slot_count; ++i)
        {
            if (slots_[i] == id)
                return static_cast<equip_slot>(i);
        }
        return std::nullopt;
    }

    [[nodiscard]] auto all_equipped() const -> std::vector<std::pair<equip_slot, item_id>>
    {
        std::vector<std::pair<equip_slot, item_id>> result;
        for (size_t i = 0; i < equip_slot_count; ++i)
        {
            if (slots_[i].is_valid())
                result.emplace_back(static_cast<equip_slot>(i), slots_[i]);
        }
        return result;
    }

    void clear_all()
    {
        for (auto& s : slots_)
            s = item_id{};
    }

private:
    std::array<item_id, equip_slot_count> slots_{};
};

} // namespace hb::player
