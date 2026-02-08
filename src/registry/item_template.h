#pragma once

// item_template.h
// Read-only item template data structure

#include "core/types.h"
#include "core/enums.h"

#include <string>
#include <array>
#include <cstdint>

namespace hb {

// Item effect applied when equipped or used
struct item_effect {
    item_effect_type type{item_effect_type::none};
    int16_t value{0};
    int16_t chance{100};  // Percentage chance to apply (100 = always)
};

// Item template - read-only data loaded from config
struct item_template {
    // Identity
    item_id id{0};
    std::string name;
    std::string sprite;  // Sprite file reference

    // Classification
    item_type type{item_type::none};
    item_equip_pos equip_pos{item_equip_pos::none};
    item_category category{item_category::general};

    // Basic properties
    int16_t weight{0};
    int32_t price{0};
    int16_t level_limit{0};

    // Combat stats (for weapons)
    int16_t attack_dice{0};
    int16_t attack_sides{0};
    int16_t attack_bonus{0};

    // Defense stats (for armor)
    int16_t defense{0};

    // Hit/Dodge modifiers
    int16_t hit_prob_bonus{0};
    int16_t dodge_prob_bonus{0};

    // Magic properties
    int16_t magic_power{0};
    int16_t mana_cost{0};

    // Attribute requirements
    int16_t str_req{0};
    int16_t dex_req{0};
    int16_t int_req{0};
    int16_t mag_req{0};
    int16_t vit_req{0};
    int16_t cha_req{0};

    // Stat bonuses when equipped
    int16_t str_bonus{0};
    int16_t dex_bonus{0};
    int16_t int_bonus{0};
    int16_t mag_bonus{0};
    int16_t vit_bonus{0};
    int16_t cha_bonus{0};

    // HP/MP/SP bonuses
    int16_t hp_bonus{0};
    int16_t mp_bonus{0};
    int16_t sp_bonus{0};

    // Durability
    int16_t max_durability{0};

    // Stacking
    int16_t max_stack{1};

    // Special effects
    std::array<item_effect, 4> effects{};

    // Visual / classification
    int16_t sprite_id{0};  // Legacy m_sItemEffectType: weapon sprite/animation type (2 = bow)

    // Flags
    bool is_stackable{false};
    bool is_tradeable{true};
    bool is_droppable{true};
    bool is_destroyable{true};
    int16_t two_hand_modifier{0};  // STR scaling modifier for two-handed weapons (-10 to +10)
    bool is_consumable{false};
    bool is_quest_item{false};

    // Helper methods
    [[nodiscard]] auto is_weapon() const -> bool {
        return type == item_type::weapon;
    }

    [[nodiscard]] auto is_armor() const -> bool {
        return type == item_type::armor;
    }

    [[nodiscard]] auto is_equippable() const -> bool {
        return equip_pos != item_equip_pos::none;
    }

    [[nodiscard]] auto average_damage() const -> float {
        if (attack_sides <= 0) return static_cast<float>(attack_bonus);
        return static_cast<float>(attack_dice) * (static_cast<float>(attack_sides + 1) / 2.0f) +
               static_cast<float>(attack_bonus);
    }

    [[nodiscard]] auto is_bow() const -> bool {
        return equip_pos == item_equip_pos::two_hand && sprite_id == 2;
    }

    [[nodiscard]] auto is_arrow() const -> bool {
        return type == item_type::arrow;
    }
};

}  // namespace hb
