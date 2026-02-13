#pragma once

// skill.h
// Skill definitions and types

#include "core/types.h"
#include "item/item.h"

#include <string>
#include <array>
#include <vector>
#include <cstdint>

namespace hb::skill {

// Skill type IDs (matching original Helbreath skills.yaml)
enum class skill_type : uint8_t {
    mining = 0,
    fishing = 1,
    farming = 2,
    magic_resistance = 3,
    magic = 4,
    hand_attack = 5,
    archery = 6,
    short_sword = 7,
    long_sword = 8,
    fencing = 9,
    axe = 10,
    shield = 11,
    alchemy = 12,
    manufacturing = 13,
    hammer = 14,
    crafting = 15,
    // 16-18 are unused/unknown in original data
    pretend_corpse = 19,
    // 20 is unused/unknown in original data
    staff = 21,
    // 22 is unused/unknown in original data
    poison_resistance = 23,

    skill_count = 24
};

inline constexpr size_t max_skills = static_cast<size_t>(skill_type::skill_count);

// Skill category
enum class skill_category : uint8_t {
    combat = 0,      // Weapon skills
    defense = 1,     // Shield, resistances
    crafting = 2,    // Manufacturing, alchemy
    gathering = 3,   // Mining, fishing, farming
    special = 4,     // Magic, critical hit
};

// Get category for a skill
[[nodiscard]] inline auto get_skill_category(skill_type skill) -> skill_category {
    switch (skill) {
        case skill_type::mining:
        case skill_type::fishing:
        case skill_type::farming:
            return skill_category::gathering;
        case skill_type::manufacturing:
        case skill_type::alchemy:
        case skill_type::crafting:
            return skill_category::crafting;
        case skill_type::shield:
        case skill_type::magic_resistance:
        case skill_type::poison_resistance:
            return skill_category::defense;
        case skill_type::magic:
        case skill_type::pretend_corpse:
            return skill_category::special;
        default:
            return skill_category::combat;
    }
}

// Map weapon_type to the corresponding skill_type
[[nodiscard]] inline auto weapon_type_to_skill_type(item::weapon_type w) -> skill_type
{
    switch (w) {
        case item::weapon_type::sword:   return skill_type::short_sword;
        case item::weapon_type::axe:     return skill_type::axe;
        case item::weapon_type::hammer:  return skill_type::hammer;
        case item::weapon_type::staff:   return skill_type::staff;
        case item::weapon_type::wand:    return skill_type::staff;
        case item::weapon_type::bow:     return skill_type::archery;
        case item::weapon_type::dagger:  return skill_type::fencing;
        case item::weapon_type::fist:
        default:                         return skill_type::hand_attack;
    }
}

// Skill mastery levels
enum class mastery_level : uint8_t {
    novice = 0,      // 0-19
    beginner = 1,    // 20-39
    apprentice = 2,  // 40-59
    journeyman = 3,  // 60-79
    expert = 4,      // 80-99
    master = 5,      // 100+
    grand_master = 6 // 120+
};

// Get mastery level from skill level
[[nodiscard]] inline auto get_mastery_level(int16_t skill_level) -> mastery_level {
    if (skill_level >= 120) return mastery_level::grand_master;
    if (skill_level >= 100) return mastery_level::master;
    if (skill_level >= 80) return mastery_level::expert;
    if (skill_level >= 60) return mastery_level::journeyman;
    if (skill_level >= 40) return mastery_level::apprentice;
    if (skill_level >= 20) return mastery_level::beginner;
    return mastery_level::novice;
}

// Tier entry for skill leveling progression
struct skill_tier {
    int16_t max_level{200};   // This tier applies for levels < max_level
    int32_t multiplier{50};   // N in formula: uses_to_next = (level+1) * N
};

// Per-skill configuration (max level + tier table)
struct skill_config_entry {
    int16_t max_level{100};
    std::vector<skill_tier> tiers;
};

// Single skill state (pure data — leveling logic lives in skill_system)
struct skill_state {
    int16_t level{0};
    int32_t total_uses{0};
    int32_t uses_this_level{0};

    [[nodiscard]] auto mastery() const -> mastery_level { return get_mastery_level(level); }
};

// Player skill set - all skills for a player
struct player_skills {
    std::array<skill_state, max_skills> skills{};

    player_skills() = default;

    [[nodiscard]] auto get(skill_type type) -> skill_state& {
        return skills[static_cast<size_t>(type)];
    }

    [[nodiscard]] auto get(skill_type type) const -> const skill_state& {
        return skills[static_cast<size_t>(type)];
    }

    [[nodiscard]] auto level(skill_type type) const -> int16_t {
        return get(type).level;
    }

    void set_level(skill_type type, int16_t lvl) {
        get(type).level = lvl;
    }

    // Get combat skill for a weapon type
    [[nodiscard]] auto get_weapon_skill(skill_type weapon) const -> int16_t {
        return get(weapon).level;
    }

    // Total combat skill level (sum of all weapon skills)
    [[nodiscard]] auto total_combat_skill() const -> int32_t {
        int32_t total = 0;
        total += get(skill_type::hand_attack).level;
        total += get(skill_type::archery).level;
        total += get(skill_type::short_sword).level;
        total += get(skill_type::long_sword).level;
        total += get(skill_type::fencing).level;
        total += get(skill_type::axe).level;
        total += get(skill_type::hammer).level;
        total += get(skill_type::staff).level;
        return total;
    }
};

// Skill use result
enum class skill_use_result : uint8_t {
    success = 0,
    failure = 1,
    cooldown = 2,
    insufficient_materials = 3,
    insufficient_skill = 4,
    invalid_target = 5,
};

}  // namespace hb::skill
