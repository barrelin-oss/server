// meteor_handler.cpp
// Multi-stage meteor strike sequence implementation

#include "war/crusade/meteor_handler.h"
#include "scheduler/scheduler.h"
#include "core/logger.h"

#include <algorithm>
#include <random>

namespace hb::war {

void meteor_handler::fire_meteor(war_faction attacking_faction)
{
    auto target = get_target_faction(attacking_faction);
    if (target == war_faction::neutral) return;

    ++pending_meteors_;

    LOG_INFO(general, "Meteor incoming! Faction {} attacking faction {} (warning: {}ms)",
        static_cast<int>(attacking_faction), static_cast<int>(target), config_.warning_time_ms);

    // Stage 1: Broadcast warning
    if (callbacks_.broadcast_warning)
    {
        callbacks_.broadcast_warning(target, config_.warning_time_ms);
    }

    // Stage 2: Schedule impact after warning period
    if (scheduler_)
    {
        scheduler_->schedule_tagged(
            duration_ms{config_.warning_time_ms},
            "crusade_meteor",
            [this, attacking_faction]() {
                execute_meteor_impact(attacking_faction);
            }
        );
    }
    else
    {
        // No scheduler — execute immediately (for testing)
        execute_meteor_impact(attacking_faction);
    }
}

void meteor_handler::cancel_all()
{
    if (scheduler_)
    {
        scheduler_->cancel_tagged("crusade_meteor");
    }
    pending_meteors_ = 0;
}

void meteor_handler::execute_meteor_impact(war_faction attacking_faction)
{
    auto target = get_target_faction(attacking_faction);

    meteor_event_result result;
    result.attacking_faction = attacking_faction;
    result.target_faction = target;

    // Get current strike points for the target faction
    std::vector<strike_point> points;
    if (callbacks_.get_strike_points)
    {
        points = callbacks_.get_strike_points(target);
    }

    // Calculate damage to each strike point
    for (const auto& sp : points)
    {
        if (sp.is_destroyed()) continue;

        meteor_strike_result sr;
        sr.strike_point_id = sp.id;
        sr.target_faction = target;

        // Check ESG protection
        if (callbacks_.get_esg_count)
        {
            sr.esg_count = callbacks_.get_esg_count(target, sp.id);
        }

        // Damage = max(0, base_damage - esg_count)
        sr.damage_applied = std::max(0, config_.base_strike_damage - sr.esg_count);

        if (sr.damage_applied > 0 && callbacks_.damage_strike_point)
        {
            callbacks_.damage_strike_point(target, sp.id, sr.damage_applied);
            sr.point_destroyed = (sp.hp - sr.damage_applied) <= 0;
        }

        result.strike_results.push_back(sr);
    }

    LOG_INFO(general, "Meteor impact on faction {}! {} strike points hit",
        static_cast<int>(target), result.strike_results.size());

    // Schedule player damage waves
    if (scheduler_)
    {
        auto wave1_delay = duration_ms{config_.player_wave1_delay_ms - config_.warning_time_ms};
        scheduler_->schedule_tagged(
            wave1_delay,
            "crusade_meteor",
            [this, target]() {
                execute_player_damage(target, 1);
            }
        );

        auto wave2_delay = duration_ms{config_.player_wave2_delay_ms - config_.warning_time_ms};
        scheduler_->schedule_tagged(
            wave2_delay,
            "crusade_meteor",
            [this, target]() {
                execute_player_damage(target, 2);
            }
        );

        // Schedule result broadcast
        auto result_delay = duration_ms{config_.result_delay_ms - config_.warning_time_ms};
        auto result_copy = result;
        scheduler_->schedule_tagged(
            result_delay,
            "crusade_meteor",
            [this, attacking_faction, r = std::move(result_copy)]() mutable {
                execute_result(attacking_faction, std::move(r));
            }
        );
    }
    else
    {
        // No scheduler — execute immediately (for testing)
        execute_player_damage(target, 1);
        execute_player_damage(target, 2);
        execute_result(attacking_faction, std::move(result));
    }
}

void meteor_handler::execute_player_damage(war_faction target_faction, int32_t wave)
{
    LOG_DEBUG(general, "Meteor player damage wave {} on faction {}", wave, static_cast<int>(target_faction));

    // Calculate base damage (level-based, applied through callback)
    if (callbacks_.damage_players)
    {
        // The callback handles per-player damage calculation and application
        int32_t casualties = callbacks_.damage_players(target_faction, wave);
        LOG_DEBUG(general, "Meteor wave {} caused {} casualties", wave, casualties);
    }
}

void meteor_handler::execute_result(war_faction attacking_faction, meteor_event_result result)
{
    --pending_meteors_;

    LOG_INFO(general, "Meteor sequence complete for faction {} attack. {} strike points damaged, {} casualties",
        static_cast<int>(attacking_faction), result.strike_results.size(), result.player_casualties);

    if (callbacks_.broadcast_result)
    {
        callbacks_.broadcast_result(result);
    }
}

auto meteor_handler::get_target_faction(war_faction attacker) const -> war_faction
{
    switch (attacker)
    {
        case war_faction::aresden: return war_faction::elvine;
        case war_faction::elvine: return war_faction::aresden;
        default: return war_faction::neutral;
    }
}

auto meteor_handler::calculate_player_damage(int32_t player_level) const -> int32_t
{
    if (player_level <= 0) return 0;

    thread_local std::mt19937 rng{std::random_device{}()};
    std::uniform_int_distribution<int32_t> dist(1, player_level);

    // Legacy formula (Game.cpp:42707): iDamage = iDice(1, level) + level
    // Range: [level+1, level*2]
    int32_t damage = dist(rng) + player_level;

    // Legacy cap: 255 maximum
    if (damage > 255) damage = 255;

    return damage;
}

}  // namespace hb::war
