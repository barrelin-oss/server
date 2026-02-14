#pragma once

// damage_calc.h
// Damage calculation formulas

#include "combat/combat_events.h"

#include <cmath>
#include <algorithm>
#include <random>

namespace hb::combat {

// Random number generator for combat
inline auto& combat_rng() {
    static thread_local std::mt19937 rng{std::random_device{}()};
    return rng;
}

inline auto random_int(int min, int max) -> int {
    std::uniform_int_distribution<int> dist(min, max);
    return dist(combat_rng());
}

inline auto random_float(float min, float max) -> float {
    std::uniform_real_distribution<float> dist(min, max);
    return dist(combat_rng());
}

inline auto random_percent() -> int {
    return random_int(1, 100);
}

// Calculate base physical damage
inline auto calc_physical_damage(int32_t attack_power, int32_t defense) -> int32_t {
    // Basic formula: damage = attack * (100 / (100 + defense))
    // This gives diminishing returns on defense
    if (defense < 0) defense = 0;

    float damage_reduction = 100.0f / (100.0f + static_cast<float>(defense));
    int32_t damage = static_cast<int32_t>(static_cast<float>(attack_power) * damage_reduction);

    // Add some variance (90-110%)
    float variance = random_float(0.9f, 1.1f);
    damage = static_cast<int32_t>(static_cast<float>(damage) * variance);

    return std::max(0, damage);
}

// Calculate magic damage
inline auto calc_magic_damage(int32_t magic_power, int32_t magic_defense) -> int32_t {
    // Similar to physical but typically less affected by defense
    if (magic_defense < 0) magic_defense = 0;

    float damage_reduction = 100.0f / (100.0f + static_cast<float>(magic_defense) * 0.8f);
    int32_t damage = static_cast<int32_t>(static_cast<float>(magic_power) * damage_reduction);

    float variance = random_float(0.95f, 1.05f);
    damage = static_cast<int32_t>(static_cast<float>(damage) * variance);

    return std::max(0, damage);
}

// Calculate hit chance
inline auto calc_hit_chance(int32_t hit_rate, int32_t dodge_rate) -> int {
    // Base 80% hit chance, modified by hit/dodge difference
    int base_chance = 80;
    int modifier = (hit_rate - dodge_rate) / 2;
    return std::clamp(base_chance + modifier, 5, 95);  // 5-95% range
}

// Calculate critical hit chance
inline auto calc_critical_chance(int32_t base_crit_rate, bool is_backstab) -> int {
    int chance = base_crit_rate;
    if (is_backstab) {
        chance += 25;  // Backstab bonus
    }
    return std::clamp(chance, 0, 75);  // Max 75% crit
}

// Calculate block chance (for shields)
inline auto calc_block_chance(int32_t block_rate) -> int {
    return std::clamp(block_rate, 0, 50);  // Max 50% block
}

// Apply damage reduction (from armor, buffs, etc.)
inline auto apply_damage_reduction(int32_t damage, int32_t reduction_percent) -> int32_t {
    reduction_percent = std::clamp(reduction_percent, 0, 80);  // Max 80% reduction
    // Use integer math to avoid floating-point precision issues
    int32_t reduced = damage * (100 - reduction_percent) / 100;
    return std::max(0, reduced);
}

// Apply critical damage multiplier
inline auto apply_critical_damage(int32_t damage, int32_t crit_damage_percent) -> int32_t {
    float multiplier = static_cast<float>(crit_damage_percent) / 100.0f;
    return static_cast<int32_t>(static_cast<float>(damage) * multiplier);
}

// Calculate final damage with all modifiers
inline auto calculate_final_damage(const combat_context& ctx) -> hit_result {
    hit_result result;
    result.type = ctx.type;

    // Check for hit
    if (!ctx.guaranteed_hit) {
        int hit_chance = calc_hit_chance(ctx.hit_rate, ctx.dodge_rate);
        if (random_percent() > hit_chance) {
            if (random_percent() <= 50) {
                result.flags = hit_flags::miss;
            } else {
                result.flags = hit_flags::dodged;
            }
            return result;
        }
    }

    result.flags = hit_flags::hit;

    // Calculate base damage
    int32_t damage = 0;
    if (ctx.type == damage_type::physical) {
        int32_t effective_defense = ctx.ignore_defense ? 0 : ctx.defense;
        damage = calc_physical_damage(ctx.attack_power, effective_defense);
    } else if (ctx.type == damage_type::magic) {
        int32_t effective_defense = ctx.ignore_defense ? 0 : ctx.magic_defense;
        damage = calc_magic_damage(ctx.magic_power, effective_defense);
    } else if (ctx.type == damage_type::pure) {
        // Pure damage ignores all defenses
        damage = ctx.attack_power;
    } else {
        // Elemental damage - uses magic formula
        damage = calc_magic_damage(ctx.magic_power, ctx.magic_defense / 2);
    }

    result.raw_damage = damage;

    // Check for block
    if (ctx.block_rate > 0 && random_percent() <= calc_block_chance(ctx.block_rate)) {
        result.flags = result.flags | hit_flags::blocked;
        damage = damage / 2;  // Blocked attacks deal half damage
    }

    // Check for critical
    bool is_crit = ctx.guaranteed_critical ||
        (random_percent() <= calc_critical_chance(ctx.critical_rate, ctx.is_backstab));

    if (is_crit) {
        result.flags = result.flags | hit_flags::critical;
        damage = apply_critical_damage(damage, ctx.critical_damage);

        if (ctx.is_backstab) {
            result.flags = result.flags | hit_flags::backstab;
        }
    }

    // Apply damage reduction
    if (ctx.damage_reduction > 0) {
        damage = apply_damage_reduction(damage, ctx.damage_reduction);
    }

    // Apply final multiplier
    damage = static_cast<int32_t>(static_cast<float>(damage) * ctx.damage_multiplier);

    result.final_damage = std::max(0, damage);
    return result;
}

}  // namespace hb::combat
