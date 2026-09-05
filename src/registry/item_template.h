#pragma once

// item_template.h
// Read-only item template data structure — raw legacy fields, 1:1 with .cfg format

#include "core/types.h"
#include "core/enums.h"

#include <string>
#include <cstdint>

namespace hb
{

// Item template - read-only data loaded from config
// Fields match the legacy .cfg format 1:1. Interpretation of effect_type + effect_value1-6
// depends on item category/type and must be done in populate_from_template or game logic.
struct item_template
{
    // Identity
    item_id id{0};
    std::string name;

    // Classification (keep as enums, cast from raw int in YAML loader)
    item_type type{item_type::none};
    item_equip_pos equip_pos{item_equip_pos::none};
    item_category category{item_category::general};

    // Effect system -- raw legacy values, interpretation depends on effect_type + category
    int16_t effect_type{0};
    int16_t effect_value1{0};
    int16_t effect_value2{0};
    int16_t effect_value3{0};
    int16_t effect_value4{0};
    int16_t effect_value5{0};
    int16_t effect_value6{0};

    // Basic properties
    int16_t durability{0};
    int16_t weight{0};
    int32_t price{0};
    int16_t level_limit{0};

    // Special effect -- raw legacy value
    int16_t special_effect{0};
    int16_t special_effect_value1{0};
    int16_t special_effect_value2{0};

    // Visual
    int16_t sprite{0};
    int16_t sprite_frame{0};
    int8_t appr_value{0};
    int8_t item_color{0};
    int8_t speed{0};
    int16_t str_speed_req{0}; // STR to swing at full speed; 0 = derive from speed (attack_timing.h)

    // Misc
    int8_t gender_limit{0};
    int16_t related_skill{0};

    // Ground item lifetime override (milliseconds). 0 = use global default.
    int32_t ground_lifetime_ms{0};
};

} // namespace hb
