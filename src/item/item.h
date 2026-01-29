#pragma once

// item.h
// Item instance and properties

#include "core/types.h"

#include <string>
#include <array>
#include <cstdint>

namespace hb::item {

// Item type categories
enum class item_type : uint8_t {
    none = 0,
    weapon = 1,
    armor = 2,
    accessory = 3,
    consumable = 4,
    material = 5,
    quest = 6,
    gold = 7,
};

// Equipment position
enum class equip_pos : uint8_t {
    none = 0,
    head = 1,
    body = 2,
    arms = 3,
    pants = 4,
    boots = 5,
    weapon = 6,
    shield = 7,
    ring = 8,
    amulet = 9,
    cape = 10,
    twohand = 11,
};

// Weapon type
enum class weapon_type : uint8_t {
    none = 0,
    sword = 1,
    axe = 2,
    hammer = 3,
    staff = 4,
    wand = 5,
    bow = 6,
    dagger = 7,
    fist = 8,
};

// Item rarity
enum class item_rarity : uint8_t {
    common = 0,
    uncommon = 1,
    rare = 2,
    epic = 3,
    legendary = 4,
    ancient = 5,
};

// Item effect types
enum class item_effect_type : uint8_t {
    none = 0,
    hp_bonus = 1,
    mp_bonus = 2,
    sp_bonus = 3,
    str_bonus = 4,
    dex_bonus = 5,
    vit_bonus = 6,
    int_bonus = 7,
    mag_bonus = 8,
    chr_bonus = 9,
    attack_bonus = 10,
    defense_bonus = 11,
    magic_attack_bonus = 12,
    magic_defense_bonus = 13,
    hit_bonus = 14,
    dodge_bonus = 15,
    critical_bonus = 16,
    hp_steal = 17,
    mp_steal = 18,
    damage_reduction = 19,
    exp_bonus = 20,
    gold_bonus = 21,
    poison_damage = 22,
    fire_damage = 23,
    ice_damage = 24,
    lightning_damage = 25,
};

// Single item effect
struct item_effect {
    item_effect_type type{item_effect_type::none};
    int16_t value{0};

    [[nodiscard]] auto is_empty() const -> bool {
        return type == item_effect_type::none;
    }
};

inline constexpr size_t max_item_effects = 6;

// Item instance - a specific item in the world
struct item {
    // Identity
    item_id id{};            // Unique instance ID
    item_id template_id{};   // Template this is based on
    std::string name;

    // Type
    item_type type{item_type::none};
    equip_pos equip_position{equip_pos::none};
    weapon_type weapon{weapon_type::none};
    item_rarity rarity{item_rarity::common};

    // Stack
    int16_t count{1};
    int16_t max_stack{1};
    bool stackable{false};

    // Physical properties
    int16_t weight{1};
    int32_t price{0};
    int16_t level_requirement{0};

    // Combat stats
    int16_t attack_power{0};
    int16_t magic_power{0};
    int16_t defense{0};
    int16_t magic_defense{0};

    // Requirements
    int16_t str_requirement{0};
    int16_t dex_requirement{0};
    int16_t int_requirement{0};
    int16_t mag_requirement{0};

    // Durability
    int16_t durability{0};
    int16_t max_durability{0};
    bool indestructible{false};

    // Effects
    std::array<item_effect, max_item_effects> effects{};

    // Flags
    bool bound{false};           // Soulbound
    bool tradeable{true};        // Can be traded
    bool droppable{true};        // Can be dropped
    bool two_handed{false};      // Two-handed weapon

    // Owner
    entity_id owner{};           // Current owner entity

    // Helper methods
    [[nodiscard]] auto is_valid() const -> bool { return id.is_valid(); }
    [[nodiscard]] auto is_equipment() const -> bool {
        return type == item_type::weapon || type == item_type::armor || type == item_type::accessory;
    }
    [[nodiscard]] auto is_consumable() const -> bool { return type == item_type::consumable; }
    [[nodiscard]] auto is_broken() const -> bool { return !indestructible && durability <= 0; }

    [[nodiscard]] auto durability_percent() const -> float {
        return max_durability > 0 ? static_cast<float>(durability) / max_durability : 1.0f;
    }

    [[nodiscard]] auto can_stack_with(const item& other) const -> bool {
        if (!stackable) return false;
        if (template_id != other.template_id) return false;
        return count < max_stack;  // Has room for at least 1 item
    }

    auto stack(item& other) -> int16_t {
        if (!can_stack_with(other)) return 0;
        int16_t space = max_stack - count;
        int16_t to_add = std::min(space, other.count);
        count += to_add;
        other.count -= to_add;
        return to_add;
    }

    auto split(int16_t amount) -> item {
        item split_item = *this;
        split_item.id = item_id{};  // New instance
        amount = std::min(amount, static_cast<int16_t>(count - 1));
        split_item.count = amount;
        count -= amount;
        return split_item;
    }

    void damage_durability(int16_t amount) {
        if (indestructible) return;
        durability = std::max<int16_t>(0, durability - amount);
    }

    void repair(int16_t amount) {
        durability = std::min(max_durability, static_cast<int16_t>(durability + amount));
    }

    void repair_full() {
        durability = max_durability;
    }
};

}  // namespace hb::item
