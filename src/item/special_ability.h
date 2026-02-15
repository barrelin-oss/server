#pragma once

// special_ability.h
// Special weapon/armor ability system (SPECABLTY items)

#include <chrono>
#include <cstdint>

namespace hb::item
{

// Special ability types (from legacy ActivateSpecialAbilityHandler)
enum class special_ability_type : uint8_t
{
    none = 0,
    hp_halve = 1,      // Halve target HP on next hit
    poison = 2,        // Apply poison on next hit
    paralyze = 3,      // Freeze target on next hit
    warrior_boost = 4, // Temporary stat boost
    life_drain = 5,    // Drain HP on next hit
    // 50+ = defensive/wizard abilities
    spell_immunity = 50,      // Immune to magic while active
    attack_block = 51,        // Block player attacks while active
    high_spell_immunity = 52, // Immune to level 5+ spells
};

// Whether an ability is an attack ability (1-5) or defense ability (50+)
[[nodiscard]] inline auto is_attack_ability(special_ability_type type) -> bool
{
    return static_cast<uint8_t>(type) >= 1 && static_cast<uint8_t>(type) <= 5;
}

[[nodiscard]] inline auto is_defense_ability(special_ability_type type) -> bool
{
    return static_cast<uint8_t>(type) >= 50;
}

// Cooldown duration (legacy: DEF_SPECABLTYTIMESEC = 1200 seconds = 20 minutes)
inline constexpr int32_t ability_cooldown_seconds = 1200;

// Ability status for protocol communication
enum class ability_status : uint8_t
{
    disabled = 0, // No ability equipped
    ready = 1,    // Ability ready to activate
    active = 2,   // Ability currently active
    cooldown = 3, // Ability on cooldown
};

// Per-player special ability state
struct special_ability_state
{
    special_ability_type type{special_ability_type::none};
    ability_status status{ability_status::disabled};
    std::chrono::steady_clock::time_point cooldown_end{};

    [[nodiscard]] auto is_ready() const -> bool { return status == ability_status::ready; }

    [[nodiscard]] auto is_active() const -> bool { return status == ability_status::active; }

    [[nodiscard]] auto is_on_cooldown(std::chrono::steady_clock::time_point now) const -> bool
    {
        return status == ability_status::cooldown && now < cooldown_end;
    }

    [[nodiscard]] auto cooldown_remaining(std::chrono::steady_clock::time_point now) const -> int32_t
    {
        if (status != ability_status::cooldown || now >= cooldown_end)
            return 0;
        auto remaining = std::chrono::duration_cast<std::chrono::seconds>(cooldown_end - now);
        return static_cast<int32_t>(remaining.count());
    }

    // Set ability type (when equipping a SPECABLTY item)
    void set_ability(special_ability_type new_type)
    {
        type = new_type;
        status = (new_type != special_ability_type::none) ? ability_status::ready : ability_status::disabled;
        cooldown_end = {};
    }

    // Activate the ability
    auto activate(std::chrono::steady_clock::time_point now) -> bool
    {
        if (status != ability_status::ready)
            return false;
        status = ability_status::active;
        return true;
    }

    // Consume the ability (after use in combat) and start cooldown
    void consume(std::chrono::steady_clock::time_point now)
    {
        status = ability_status::cooldown;
        cooldown_end = now + std::chrono::seconds(ability_cooldown_seconds);
    }

    // Check and update cooldown expiration
    auto check_cooldown(std::chrono::steady_clock::time_point now) -> bool
    {
        if (status == ability_status::cooldown && now >= cooldown_end)
        {
            status = ability_status::ready;
            cooldown_end = {};
            return true; // Just became ready
        }
        return false;
    }

    // Clear (when unequipping)
    void clear()
    {
        type = special_ability_type::none;
        status = ability_status::disabled;
        cooldown_end = {};
    }
};

} // namespace hb::item
