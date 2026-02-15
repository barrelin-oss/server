#pragma once

// item_attribute.h
// Per-instance item attribute system (modernized m_dwAttribute)

#include <cstdint>
#include <algorithm>
#include <nlohmann/json_fwd.hpp>

namespace hb::item
{

// Main weapon/armor enchantment type (bits 23-20 of legacy m_dwAttribute)
enum class enchantment_type : uint8_t
{
    none = 0,
    critical_bonus = 1, // Super attack charge chance
    poison = 2,         // Poison on hit
    righteous = 3,      // Rep-based bonus damage
    spell_on_hit = 4,   // Triggers spell (value maps to spell ID)
    damage_reduction = 5,
    light = 6,
    sharp = 7, // +1 weapon dice range
    fire = 8,
    ancient = 9,          // +2 weapon dice range
    magic_damage = 10,    // Bonus magic damage / construction points
    mana_conversion = 11, // Damage-to-mana conversion (cap 20)
    charge_critical = 12, // Chance to gain super attack charge (cap 20)
};

// Sub enchantment type — stat bonus (bits 15-12 of legacy m_dwAttribute)
enum class sub_enchantment_type : uint8_t
{
    none = 0,
    physical_resist = 1,     // +value*7
    attack_rating = 2,       // +value*7
    defense_rating = 3,      // +value*7
    hp_recovery = 4,         // +value*7
    sp_recovery = 5,         // +value*7
    mp_recovery = 6,         // +value*7
    magic_resist = 7,        // +value*7
    physical_absorption = 8, // +value*3
    magic_absorption = 9,    // +value*3 (cap 80)
    critical_damage = 10,    // +value
    exp_bonus = 11,          // +value*10%
    gold_bonus = 12,         // +value*10%
};

// Per-instance item attribute data (replaces legacy m_dwAttribute bitfield)
//
// Legacy bit layout:
//   Bits 31-28: Upgrade level (+0 to +15)
//   Bits 23-20: Main enchantment type
//   Bits 19-16: Main enchantment value (0-15)
//   Bits 15-12: Sub enchantment type
//   Bits 11-8:  Sub enchantment value (0-15)
//   Bit 0:      Custom-made item flag
struct item_attribute
{
    uint8_t upgrade_level{0};        // 0-15
    enchantment_type main_type{};    // Weapon/armor enchantment
    uint8_t main_value{0};           // 0-15
    sub_enchantment_type sub_type{}; // Stat bonus enchantment
    uint8_t sub_value{0};            // 0-15
    bool custom_made{false};         // Crafted by player
    int8_t custom_quality{0};        // -100 to +100

    [[nodiscard]] auto is_empty() const -> bool
    {
        return upgrade_level == 0 && main_type == enchantment_type::none && sub_type == sub_enchantment_type::none &&
               !custom_made;
    }

    // Convert to legacy 32-bit packed format
    [[nodiscard]] auto to_legacy_dword() const -> uint32_t;

    // Parse from legacy 32-bit packed format
    static auto from_legacy_dword(uint32_t dw) -> item_attribute;

    // JSON serialization
    [[nodiscard]] auto to_json() const -> nlohmann::json;
    static auto from_json(const nlohmann::json& j) -> item_attribute;
};

// String conversion helpers
auto to_string(enchantment_type type) -> const char*;
auto to_string(sub_enchantment_type type) -> const char*;

} // namespace hb::item
