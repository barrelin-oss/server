// effect_modifiers.cpp
// Compute stat modifiers and status flags from active effects

#include "effect/effect_modifiers.h"

namespace hb::effect
{

auto compute_effect_modifiers(std::span<const active_effect> effects) -> effect_modifier_result
{
    effect_modifier_result result{};

    for (const auto& eff : effects)
    {
        // Apply status flag if set
        if (eff.status_flag != player::player_status::none)
        {
            result.status = result.status | eff.status_flag;
        }

        // Apply stat modifiers based on magic_type group
        switch (eff.group)
        {
        case magic_type::protection:
            // Protection spells add defense
            result.modifiers.defense += static_cast<int16_t>(eff.magnitude);
            break;

        case magic_type::berserk:
            // Berserk: boost attack, reduce defense
            result.modifiers.attack_power += static_cast<int16_t>(eff.magnitude);
            result.modifiers.defense -= static_cast<int16_t>(eff.magnitude);
            break;

        default:
            break;
        }

        // Apply stat modifiers based on spell_effect_type (from registry effects vector)
        switch (eff.type)
        {
        case spell_effect_type::buff_attack:
            result.modifiers.attack_power += static_cast<int16_t>(eff.magnitude);
            break;

        case spell_effect_type::buff_defense:
            result.modifiers.defense += static_cast<int16_t>(eff.magnitude);
            break;

        case spell_effect_type::buff_speed:
            result.modifiers.move_speed += static_cast<int16_t>(eff.magnitude);
            break;

        case spell_effect_type::debuff_slow:
            result.modifiers.move_speed -= static_cast<int16_t>(eff.magnitude);
            break;

        default:
            // DoT/HoT types (poison, burn, heal, mana_drain, mana_restore)
            // act through periodic ticks, not stat modifiers.
            // Stun/freeze/etc. are handled via status_flag above.
            break;
        }
    }

    return result;
}

} // namespace hb::effect
