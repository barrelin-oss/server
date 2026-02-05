#pragma once

// effect_modifiers.h
// Pure function: compute stat modifiers and status flags from active effects

#include "effect/active_effect.h"
#include "player/stats.h"
#include "player/player.h"

#include <span>

namespace hb::effect {

// Result of computing modifiers from active effects
struct effect_modifier_result
{
    player::stat_modifiers modifiers{};
    player::player_status status{player::player_status::none};
};

// Compute the combined stat modifiers and status flags from a set of active effects.
// This is a pure function with no side effects.
auto compute_effect_modifiers(std::span<const active_effect> effects) -> effect_modifier_result;

}  // namespace hb::effect
