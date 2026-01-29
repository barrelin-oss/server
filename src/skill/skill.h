#pragma once

// skill.h
// Skill definitions and types

#include "core/types.h"

#include <string>
#include <array>
#include <cstdint>

namespace hb::skill {

// Skill type IDs (matching original Helbreath)
enum class skill_type : uint8_t {
    none = 0,
    mining = 1,
    manufacturing = 2,
    alchemy = 3,
    magic = 4,
    sword = 5,
    axe = 6,
    hammer = 7,
    long_sword = 8,
    dagger = 9,
    staff = 10,
    bow = 11,
    shield = 12,
    pretend_corpse = 13,
    fishing = 14,
    farming = 15,
    critical_hit = 16,
    archery = 17,
    magic_resistance = 18,
    physical_resistance = 19,
    poison_resistance = 20,
    fist = 21,
    wand = 22,
    throwing = 23,

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
            return skill_category::crafting;
        case skill_type::shield:
        case skill_type::magic_resistance:
        case skill_type::physical_resistance:
        case skill_type::poison_resistance:
            return skill_category::defense;
        case skill_type::magic:
        case skill_type::critical_hit:
        case skill_type::pretend_corpse:
            return skill_category::special;
        default:
            return skill_category::combat;
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

// Single skill state
struct skill_state {
    skill_type type{skill_type::none};
    int16_t level{0};
    int32_t experience{0};

    [[nodiscard]] auto is_valid() const -> bool { return type != skill_type::none; }
    [[nodiscard]] auto mastery() const -> mastery_level { return get_mastery_level(level); }

    // Experience needed for next level (simplified formula)
    [[nodiscard]] auto exp_for_next_level() const -> int32_t {
        return (level + 1) * 100;
    }

    // Check if we have enough exp to level up
    [[nodiscard]] auto can_level_up() const -> bool {
        return experience >= exp_for_next_level() && level < 200;
    }

    // Add experience and return levels gained
    auto add_experience(int32_t amount) -> int16_t {
        if (level >= 200) return 0;  // Max level

        experience += amount;
        int16_t levels_gained = 0;

        while (can_level_up()) {
            experience -= exp_for_next_level();
            ++level;
            ++levels_gained;
        }

        return levels_gained;
    }
};

// Player skill set - all skills for a player
struct player_skills {
    std::array<skill_state, max_skills> skills{};

    player_skills() {
        // Initialize all skill types
        for (size_t i = 0; i < max_skills; ++i) {
            skills[i].type = static_cast<skill_type>(i);
        }
    }

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

    auto add_experience(skill_type type, int32_t amount) -> int16_t {
        return get(type).add_experience(amount);
    }

    // Get combat skill for a weapon type
    [[nodiscard]] auto get_weapon_skill(skill_type weapon) const -> int16_t {
        return get(weapon).level;
    }

    // Total combat skill level (sum of all weapon skills)
    [[nodiscard]] auto total_combat_skill() const -> int32_t {
        int32_t total = 0;
        total += get(skill_type::sword).level;
        total += get(skill_type::axe).level;
        total += get(skill_type::hammer).level;
        total += get(skill_type::long_sword).level;
        total += get(skill_type::dagger).level;
        total += get(skill_type::staff).level;
        total += get(skill_type::bow).level;
        total += get(skill_type::fist).level;
        total += get(skill_type::wand).level;
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
