#pragma once

// combat_stats.h
// Combat-related stats component

#include <cstdint>
#include <algorithm>

namespace hb::entity
{

// Health component
struct health
{
    int32_t current{0};
    int32_t maximum{0};

    health() = default;
    health(int32_t max) : current(max), maximum(max) {}
    health(int32_t cur, int32_t max) : current(cur), maximum(max) {}

    [[nodiscard]] auto is_alive() const -> bool { return current > 0; }
    [[nodiscard]] auto is_full() const -> bool { return current >= maximum; }
    [[nodiscard]] auto percent() const -> float { return maximum > 0 ? static_cast<float>(current) / static_cast<float>(maximum) : 0.0f; }

    void damage(int32_t amount) { current = std::max(0, current - amount); }

    void heal(int32_t amount) { current = std::min(maximum, current + amount); }

    void set_max(int32_t max, bool heal_to_full = false)
    {
        maximum = max;
        if (heal_to_full || current > maximum)
        {
            current = maximum;
        }
    }
};

// Mana component
struct mana
{
    int32_t current{0};
    int32_t maximum{0};

    mana() = default;
    mana(int32_t max) : current(max), maximum(max) {}
    mana(int32_t cur, int32_t max) : current(cur), maximum(max) {}

    [[nodiscard]] auto has_mana(int32_t amount) const -> bool { return current >= amount; }
    [[nodiscard]] auto is_full() const -> bool { return current >= maximum; }
    [[nodiscard]] auto percent() const -> float { return maximum > 0 ? static_cast<float>(current) / static_cast<float>(maximum) : 0.0f; }

    auto spend(int32_t amount) -> bool
    {
        if (current < amount)
            return false;
        current -= amount;
        return true;
    }

    void restore(int32_t amount) { current = std::min(maximum, current + amount); }
};

// Stamina component
struct stamina
{
    int32_t current{0};
    int32_t maximum{0};

    stamina() = default;
    stamina(int32_t max) : current(max), maximum(max) {}

    [[nodiscard]] auto has_stamina(int32_t amount) const -> bool { return current >= amount; }

    auto spend(int32_t amount) -> bool
    {
        if (current < amount)
            return false;
        current -= amount;
        return true;
    }

    void restore(int32_t amount) { current = std::min(maximum, current + amount); }
};

// Combat stats component
struct combat_stats
{
    // Offensive
    int16_t attack_power{0}; // Base physical attack
    int16_t magic_power{0};  // Base magic attack
    int16_t hit_rate{0};     // Accuracy bonus

    // Defensive
    int16_t defense{0};       // Physical defense
    int16_t magic_defense{0}; // Magic defense
    int16_t dodge_rate{0};    // Evasion bonus

    // Speed
    int16_t attack_speed{0}; // Attack delay reduction
    int16_t move_speed{0};   // Movement speed bonus
    int16_t cast_speed{0};   // Spell cast speed bonus

    // Critical
    int16_t critical_rate{0};   // Crit chance (percentage)
    int16_t critical_damage{0}; // Crit damage bonus (percentage)
};

// Level component
struct level
{
    uint8_t current{1};
    int64_t experience{0};
    int64_t next_level_exp{0};

    [[nodiscard]] auto exp_progress() const -> float
    {
        return next_level_exp > 0 ? static_cast<float>(experience) / static_cast<float>(next_level_exp) : 0.0f;
    }
};

} // namespace hb::entity
