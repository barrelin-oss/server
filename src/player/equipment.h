#pragma once

// equipment.h
// Equipment slots and equipped items

#include "core/types.h"
#include <array>
#include <optional>

namespace hb::player {

// Equipment slot positions
enum class equip_slot : uint8_t {
    head = 0,
    body = 1,
    arms = 2,
    pants = 3,
    boots = 4,
    weapon = 5,
    shield = 6,
    ring_left = 7,
    ring_right = 8,
    amulet = 9,
    cape = 10,
    held_item = 11,  // Potion, scroll, etc in hand

    count = 12
};

inline constexpr size_t equip_slot_count = static_cast<size_t>(equip_slot::count);

// Get slot name for debugging/display
[[nodiscard]] inline auto equip_slot_name(equip_slot slot) -> const char* {
    switch (slot) {
        case equip_slot::head: return "Head";
        case equip_slot::body: return "Body";
        case equip_slot::arms: return "Arms";
        case equip_slot::pants: return "Pants";
        case equip_slot::boots: return "Boots";
        case equip_slot::weapon: return "Weapon";
        case equip_slot::shield: return "Shield";
        case equip_slot::ring_left: return "Left Ring";
        case equip_slot::ring_right: return "Right Ring";
        case equip_slot::amulet: return "Amulet";
        case equip_slot::cape: return "Cape";
        case equip_slot::held_item: return "Held Item";
        default: return "Unknown";
    }
}

// Equipped item reference
struct equipped_item {
    item_id id{};
    uint16_t durability{0};
    uint16_t max_durability{0};

    [[nodiscard]] auto is_empty() const -> bool { return !id.is_valid(); }
    [[nodiscard]] auto durability_percent() const -> float {
        return max_durability > 0 ? static_cast<float>(durability) / max_durability : 0.0f;
    }

    void clear() {
        id = item_id{};
        durability = 0;
        max_durability = 0;
    }
};

// Player equipment state
struct equipment_state {
    std::array<equipped_item, equip_slot_count> slots{};

    [[nodiscard]] auto get(equip_slot slot) -> equipped_item& {
        return slots[static_cast<size_t>(slot)];
    }

    [[nodiscard]] auto get(equip_slot slot) const -> const equipped_item& {
        return slots[static_cast<size_t>(slot)];
    }

    [[nodiscard]] auto has_equipped(equip_slot slot) const -> bool {
        return !slots[static_cast<size_t>(slot)].is_empty();
    }

    [[nodiscard]] auto weapon() const -> const equipped_item& {
        return get(equip_slot::weapon);
    }

    [[nodiscard]] auto shield() const -> const equipped_item& {
        return get(equip_slot::shield);
    }

    void equip(equip_slot slot, item_id id, uint16_t dur, uint16_t max_dur) {
        auto& item = slots[static_cast<size_t>(slot)];
        item.id = id;
        item.durability = dur;
        item.max_durability = max_dur;
    }

    auto unequip(equip_slot slot) -> equipped_item {
        auto item = slots[static_cast<size_t>(slot)];
        slots[static_cast<size_t>(slot)].clear();
        return item;
    }

    void clear_all() {
        for (auto& slot : slots) {
            slot.clear();
        }
    }
};

}  // namespace hb::player
