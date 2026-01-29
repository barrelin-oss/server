#pragma once

// combat_events.h
// Combat system events

#include "entity/entity.h"
#include "world/position.h"

#include <cstdint>

namespace hb::combat {

// Damage type categories
enum class damage_type : uint8_t {
    physical = 0,
    magic = 1,
    fire = 2,
    ice = 3,
    lightning = 4,
    poison = 5,
    holy = 6,
    dark = 7,
    pure = 8,  // Ignores all defenses
};

// Attack result flags
enum class hit_flags : uint16_t {
    none = 0,
    hit = 1 << 0,
    miss = 1 << 1,
    critical = 1 << 2,
    blocked = 1 << 3,
    dodged = 1 << 4,
    absorbed = 1 << 5,
    reflected = 1 << 6,
    killed = 1 << 7,
    backstab = 1 << 8,
    counter = 1 << 9,
};

inline auto operator|(hit_flags a, hit_flags b) -> hit_flags {
    return static_cast<hit_flags>(static_cast<uint16_t>(a) | static_cast<uint16_t>(b));
}

inline auto operator&(hit_flags a, hit_flags b) -> hit_flags {
    return static_cast<hit_flags>(static_cast<uint16_t>(a) & static_cast<uint16_t>(b));
}

// Result of a hit resolution
struct hit_result {
    hit_flags flags{hit_flags::none};
    damage_type type{damage_type::physical};
    int32_t raw_damage{0};       // Before defenses
    int32_t final_damage{0};     // After defenses
    int32_t absorbed{0};         // Absorbed by shields/effects
    int32_t reflected{0};        // Reflected back to attacker

    [[nodiscard]] auto is_hit() const -> bool {
        return (flags & hit_flags::hit) != hit_flags::none;
    }

    [[nodiscard]] auto is_miss() const -> bool {
        return (flags & hit_flags::miss) != hit_flags::none;
    }

    [[nodiscard]] auto is_critical() const -> bool {
        return (flags & hit_flags::critical) != hit_flags::none;
    }

    [[nodiscard]] auto is_blocked() const -> bool {
        return (flags & hit_flags::blocked) != hit_flags::none;
    }

    [[nodiscard]] auto is_dodged() const -> bool {
        return (flags & hit_flags::dodged) != hit_flags::none;
    }

    [[nodiscard]] auto caused_death() const -> bool {
        return (flags & hit_flags::killed) != hit_flags::none;
    }
};

// Attack event - published when an attack is attempted
struct attack_event {
    hb::entity::entity attacker{};
    hb::entity::entity defender{};
    damage_type type{damage_type::physical};
    int32_t base_damage{0};
    bool is_skill{false};
    uint16_t skill_id{0};
};

// Damage event - published when damage is applied
struct damage_event {
    hb::entity::entity target{};
    hb::entity::entity source{};
    hit_result result{};
    hb::world::position location{};
};

// Death event - published when an entity dies
struct death_event {
    hb::entity::entity victim{};
    hb::entity::entity killer{};
    hb::world::position location{};
    bool is_pvp{false};
};

// Combat context for damage calculations
struct combat_context {
    hb::entity::entity attacker{};
    hb::entity::entity defender{};
    damage_type type{damage_type::physical};

    // Attacker stats
    int32_t attack_power{0};
    int32_t magic_power{0};
    int32_t hit_rate{0};
    int32_t critical_rate{0};
    int32_t critical_damage{0};  // Percentage (150 = 150%)

    // Defender stats
    int32_t defense{0};
    int32_t magic_defense{0};
    int32_t dodge_rate{0};
    int32_t block_rate{0};
    int32_t damage_reduction{0};  // Percentage

    // Positional
    bool is_backstab{false};
    int32_t distance{0};

    // Modifiers
    float damage_multiplier{1.0f};
    bool ignore_defense{false};
    bool guaranteed_hit{false};
    bool guaranteed_critical{false};
};

}  // namespace hb::combat
