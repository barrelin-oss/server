#include "combat/weapon_effect.h"
#include "combat/damage_calc.h"

#include <algorithm>

namespace hb::combat {

auto process_weapon_effect(item::enchantment_type type, uint8_t value,
                           int32_t damage_dealt, int32_t attacker_mp,
                           int32_t attacker_max_mp) -> weapon_on_hit_result
{
    weapon_on_hit_result result;

    switch (type) {
        case item::enchantment_type::none:
            break;

        case item::enchantment_type::critical_bonus:
            // Adds to super attack charge chance
            result.apply_critical_bonus = true;
            result.critical_bonus = value;
            break;

        case item::enchantment_type::poison:
            // Poison on hit — value determines severity
            result.apply_poison = true;
            result.poison_level = std::max(1, static_cast<int32_t>(value));
            break;

        case item::enchantment_type::righteous:
            // Rep-based bonus damage
            result.apply_righteous = true;
            result.righteous_bonus = value;
            break;

        case item::enchantment_type::spell_on_hit:
            // Trigger spell effect — value maps to effect
            result.trigger_spell = true;
            result.spell_value = value;
            break;

        case item::enchantment_type::damage_reduction:
            // Percentage damage reduction
            result.apply_damage_reduction = true;
            result.damage_reduction = value;
            break;

        case item::enchantment_type::light:
            // Light effect — visual only, no combat effect
            break;

        case item::enchantment_type::sharp:
            // +1 dice range handled in stat application (Phase 2)
            break;

        case item::enchantment_type::fire:
            // Fire attribute damage bonus
            result.apply_fire = true;
            result.fire_bonus = value;
            break;

        case item::enchantment_type::ancient:
            // +2 dice range handled in stat application (Phase 2)
            break;

        case item::enchantment_type::magic_damage:
            // Bonus magic damage
            result.apply_magic_damage = true;
            result.magic_damage_bonus = value;
            break;

        case item::enchantment_type::mana_conversion: {
            // Convert portion of damage dealt to mana (cap 20%)
            int32_t conversion_rate = std::min(static_cast<int32_t>(value), 20);
            int32_t mana_gain = damage_dealt * conversion_rate / 100;
            int32_t mana_space = attacker_max_mp - attacker_mp;
            result.mana_gained = std::min(mana_gain, std::max(0, mana_space));
            break;
        }

        case item::enchantment_type::charge_critical: {
            // Chance to gain super attack charge (value = % chance, cap 20)
            int32_t chance = std::min(static_cast<int32_t>(value), 20);
            if (chance > 0 && random_percent() <= chance) {
                result.gained_super_charge = true;
            }
            break;
        }
    }

    return result;
}

}  // namespace hb::combat
