#pragma once

// active_effect.h
// Core data structures for active spell effects (buffs, debuffs, DoTs, HoTs)

#include "core/types.h"
#include "core/enums.h"
#include "entity/entity.h"
#include "player/player.h"
#include "registry/spell_template.h"

#include <cstdint>
#include <optional>

namespace hb::effect {

// Unique identifier for an active effect instance
struct effect_id
{
    uint32_t value{0};

    constexpr effect_id() = default;
    constexpr explicit effect_id(uint32_t v) : value(v) {}

    constexpr auto operator<=>(const effect_id&) const = default;
    [[nodiscard]] constexpr auto is_valid() const -> bool { return value != 0; }
};

// Default tick interval for periodic effects (2 seconds, matches Helbreath convention)
inline constexpr int64_t default_tick_interval_ms = 2000;

// A running effect instance on an entity
struct active_effect
{
    effect_id id{};                                         // Unique monotonic ID
    entity::entity source{};                                // Who applied it
    std::optional<spell_id> source_spell;                    // Which spell (nullopt if non-spell)
    magic_type group{};                                     // Effect group/slot (from spell's magic_type)
    spell_effect_type type{spell_effect_type::none};        // Specific effect type
    int32_t magnitude{0};                                   // Pre-computed value
    int64_t applied_at_ms{0};
    int64_t expires_at_ms{0};                               // 0 = permanent
    int64_t duration_ms{0};
    int64_t tick_interval_ms{0};                            // 0 = not periodic
    int64_t last_tick_ms{0};
    player::player_status status_flag{player::player_status::none};  // Mapped status flag
};

// Parameters for applying a new effect
struct apply_effect_params
{
    entity::entity source{};
    entity::entity target{};
    std::optional<spell_id> source_spell;
    magic_type group{};                                     // Effect slot group
    spell_effect_type type{spell_effect_type::none};        // Specific effect type
    int32_t magnitude{0};                                   // Pre-computed
    int64_t duration_ms{0};                                 // 0 = permanent
    int64_t tick_interval_ms{0};                            // 0 = not periodic
};

}  // namespace hb::effect
