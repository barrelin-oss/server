// attack_timing.h
// Per-swing attack interval (HANDOFF 6.4). The legacy game paced attacks on the
// client from Item.cfg field 19 ("Speed", 0 = fastest) and only policed speed hacks
// on the server. Here the server owns the pace:
//
//   interval = base_ms + weapon.speed * speed_step_ms
//            + max(0, str_for_full_speed - attacker_str) * str_penalty_ms
//
// clamped to [min_ms, max_ms]. A weapon may declare str_speed_req explicitly;
// otherwise it derives from speed * str_per_speed, so heavier (slower) weapons ask
// for more STR before they swing at full pace. This never blocks equipping — that
// is str_requirement's job — it only slows the swing.
#pragma once

#include "config/server_config.h"

#include <algorithm>
#include <cstdint>

namespace hb::combat
{

// STR needed to swing this weapon at full speed. 0 means "any STR".
[[nodiscard]] inline auto str_for_full_speed(int8_t weapon_speed,
                                             int16_t explicit_req,
                                             const attack_speed_config& cfg) -> int32_t
{
    if (explicit_req > 0)
        return explicit_req;
    if (weapon_speed <= 0)
        return 0;
    return static_cast<int32_t>(weapon_speed) * cfg.str_per_speed;
}

// Milliseconds the attacker must wait between swings. weapon_speed < 0 or a
// null weapon means unarmed (base pace, no STR penalty).
[[nodiscard]] inline auto attack_interval_ms(int8_t weapon_speed,
                                             int16_t weapon_str_speed_req,
                                             int32_t attacker_str,
                                             const attack_speed_config& cfg) -> int32_t
{
    if (!cfg.enabled)
        return cfg.min_ms;

    int32_t interval = cfg.base_ms;
    if (weapon_speed > 0)
    {
        interval += static_cast<int32_t>(weapon_speed) * cfg.speed_step_ms;
        const int32_t need = str_for_full_speed(weapon_speed, weapon_str_speed_req, cfg);
        if (attacker_str < need)
            interval += (need - attacker_str) * cfg.str_penalty_ms;
    }
    return std::clamp(interval, cfg.min_ms, cfg.max_ms);
}

} // namespace hb::combat
