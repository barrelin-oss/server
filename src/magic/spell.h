#pragma once

// spell.h
// Spell definitions and types

#include "core/types.h"
#include "core/enums.h"
#include "entity/entity.h"
#include "world/position.h"
#include "registry/spell_template.h"

#include <optional>
#include <string>
#include <vector>
#include <cstdint>

namespace hb::magic
{

// Use explicit types to avoid namespace ambiguity

// Spell categories
enum class spell_category : uint8_t
{
    attack = 0,  // Damage spells
    defense = 1, // Protective spells
    healing = 2, // Healing spells
    buff = 3,    // Stat enhancement
    debuff = 4,  // Stat reduction
    summon = 5,  // Summon creatures
    utility = 6, // Teleport, create food, etc.
    special = 7, // Unique spells
};

// Spell targeting types
enum class spell_target : uint8_t
{
    self = 0,
    single_enemy = 1,
    single_ally = 2,
    single_any = 3,
    aoe_enemy = 4,
    aoe_ally = 5,
    aoe_all = 6,
    ground = 7,
    cone = 8,
    line = 9,
};

// Spell element
enum class spell_element : uint8_t
{
    none = 0,
    fire = 1,
    ice = 2,
    lightning = 3,
    earth = 4,
    wind = 5,
    holy = 6,
    dark = 7,
};

// Cast result
enum class cast_result : uint8_t
{
    success = 0,
    not_enough_mana = 1,
    on_cooldown = 2,
    invalid_target = 3,
    out_of_range = 4,
    silenced = 5,
    interrupted = 6,
    level_too_low = 7,
    spell_not_learned = 8,
    target_immune = 9,
    failed = 10,
    safe_zone_blocked = 11,
};

// Spell template/definition
struct spell_template
{
    spell_id id{};
    std::string name;
    spell_category category{spell_category::attack};
    spell_target target_type{spell_target::single_enemy};
    spell_element element{spell_element::none};

    // Costs
    int16_t mana_cost{0};
    int16_t hp_cost{0};
    int16_t sp_cost{0};

    // Timing
    int32_t cast_time_ms{0}; // Cast delay
    int32_t cooldown_ms{0};  // Cooldown after cast
    int32_t duration_ms{0};  // Effect duration (for buffs/debuffs)

    // Range
    int16_t range{1};
    int16_t aoe_radius{0};

    // Effect values
    int32_t base_damage{0};
    int32_t base_heal{0};
    int16_t effect_value{0}; // Generic effect magnitude

    // Scaling
    int16_t int_scaling{0}; // % of INT added to damage/heal
    int16_t mag_scaling{0}; // % of MAG added to effect

    // Requirements
    int16_t level_requirement{0};
    int16_t int_requirement{0};
    int16_t mag_requirement{0};

    // Effect system integration
    magic_type spell_type{};               // Magic type from YAML (effect group key)
    std::vector<hb::spell_effect> effects; // Effect entries from registry

    // Flags
    bool can_critical{true};
    bool ignores_defense{false};
    bool channeled{false};

    [[nodiscard]] auto is_offensive() const -> bool
    {
        return category == spell_category::attack || category == spell_category::debuff;
    }

    [[nodiscard]] auto is_defensive() const -> bool
    {
        return category == spell_category::defense || category == spell_category::healing ||
               category == spell_category::buff;
    }

    [[nodiscard]] auto is_aoe() const -> bool
    {
        return target_type >= spell_target::aoe_enemy && target_type <= spell_target::aoe_all;
    }
};

// Cast target - where the spell is directed
struct cast_target
{
    hb::entity::entity target{};      // For single target spells
    hb::world::position target_pos{}; // For ground-targeted spells

    [[nodiscard]] auto has_entity() const -> bool { return target.is_valid(); }
    [[nodiscard]] auto has_position() const -> bool { return target_pos.x != 0 || target_pos.y != 0; }
};

// Spell cast state (for tracking channeled/cast time spells)
struct spell_cast_state
{
    std::optional<spell_id> spell;
    cast_target target{};
    int64_t start_time_ms{0};
    int64_t end_time_ms{0};
    bool cancelled{false};

    [[nodiscard]] auto is_active() const -> bool { return spell.has_value() && !cancelled; }

    [[nodiscard]] auto progress(int64_t current_time_ms) const -> float
    {
        if (!is_active())
            return 0.0f;
        int64_t duration = end_time_ms - start_time_ms;
        if (duration <= 0)
            return 1.0f;
        int64_t elapsed = current_time_ms - start_time_ms;
        return std::clamp(static_cast<float>(elapsed) / static_cast<float>(duration), 0.0f, 1.0f);
    }

    [[nodiscard]] auto is_complete(int64_t current_time_ms) const -> bool
    {
        return is_active() && current_time_ms >= end_time_ms;
    }

    void cancel() { cancelled = true; }
};

// Player spell knowledge
struct spell_knowledge
{
    spell_id spell{};
    int16_t level{1}; // Spell level/mastery
    int64_t last_cast_time_ms{0};
    int32_t total_casts{0};

    [[nodiscard]] auto cooldown_remaining(int64_t current_time_ms, int32_t cooldown_ms) const -> int32_t
    {
        int64_t elapsed = current_time_ms - last_cast_time_ms;
        if (elapsed >= cooldown_ms) return 0;
        return static_cast<int32_t>(cooldown_ms - elapsed);
    }

    [[nodiscard]] auto is_on_cooldown(int64_t current_time_ms, int32_t cooldown_ms) const -> bool
    {
        return cooldown_remaining(current_time_ms, cooldown_ms) > 0;
    }
};

} // namespace hb::magic
