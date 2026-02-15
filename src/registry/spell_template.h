#pragma once

// spell_template.h
// Read-only spell template data structure

#include "core/types.h"
#include "core/enums.h"

#include <string>
#include <vector>
#include <cstdint>

namespace hb
{

// Spell target types (must be defined before spell_template)
enum class spell_target : uint8_t
{
    self = 0,
    single_enemy = 1,
    single_ally = 2,
    area_enemy = 3,
    area_ally = 4,
    area_all = 5,
    ground = 6,
    cone = 7,
    line = 8
};

// Spell effect types
enum class spell_effect_type : uint8_t
{
    none = 0,
    damage = 1,
    heal = 2,
    buff_attack = 3,
    buff_defense = 4,
    buff_speed = 5,
    debuff_slow = 6,
    debuff_blind = 7,
    stun = 8,
    poison = 9,
    burn = 10,
    freeze = 11,
    teleport = 12,
    summon = 13,
    polymorph = 14,
    invisibility = 15,
    resurrection = 16,
    mana_drain = 17,
    mana_restore = 18
};

// Spell effect entry
struct spell_effect
{
    spell_effect_type type{spell_effect_type::none};
    int16_t base_value{0};
    int16_t scaling{0}; // Per 10 magic/int
    duration_ms duration{0};
};

// Spell template - read-only data loaded from config
struct spell_template
{
    // Identity
    spell_id id{0};
    std::string name;
    std::string description;

    // Classification
    magic_type type{magic_type::damage_spot};
    spell_target target{spell_target::single_enemy};

    // Costs
    int16_t mana_cost{0};
    int16_t stamina_cost{0};
    int16_t hp_cost{0}; // For blood magic

    // Requirements
    int16_t int_req{0};
    int16_t mag_level_req{0};
    int16_t staff_required{0}; // 0 = any, >0 = specific staff type

    // Casting
    int16_t cast_time_ms{0};
    int16_t cooldown_ms{0};
    int16_t range{0};
    int16_t area_radius{0};

    // Damage/Healing
    int16_t base_damage{0};
    int16_t int_scaling{0}; // Per 10 INT
    int16_t mag_scaling{0}; // Per 10 MAG

    // Effect duration
    duration_ms effect_duration{0};

    // Effects (up to 4)
    std::vector<spell_effect> effects;

    // Flags
    bool is_offensive{true};
    bool requires_target{true};
    bool can_hit_self{false};
    bool can_hit_ally{false};
    bool can_hit_enemy{true};
    bool breaks_invisibility{true};
    bool is_channeled{false};

    // Helper methods
    [[nodiscard]] auto is_aoe() const -> bool { return area_radius > 0; }

    [[nodiscard]] auto is_instant() const -> bool { return cast_time_ms == 0; }

    [[nodiscard]] auto calculate_damage(int intelligence, int magic_level) const -> int
    {
        return base_damage + (int_scaling * intelligence / 10) + (mag_scaling * magic_level / 10);
    }
};

} // namespace hb
