#include "item/item_attribute.h"

#include <nlohmann/json.hpp>

namespace hb::item
{

auto item_attribute::to_json() const -> nlohmann::json
{
    nlohmann::json j = nlohmann::json::object();
    if (upgrade_level > 0)
        j["upgrade"] = upgrade_level;
    if (main_type != enchantment_type::none)
    {
        j["main_type"] = static_cast<uint8_t>(main_type);
        j["main_value"] = main_value;
    }
    if (sub_type != sub_enchantment_type::none)
    {
        j["sub_type"] = static_cast<uint8_t>(sub_type);
        j["sub_value"] = sub_value;
    }
    if (custom_made)
    {
        j["custom_made"] = true;
        if (custom_quality != 0)
            j["custom_quality"] = custom_quality;
    }
    return j;
}

auto item_attribute::from_json(const nlohmann::json& j) -> item_attribute
{
    item_attribute attr;
    if (j.is_null() || !j.is_object())
        return attr;

    if (j.contains("upgrade"))
        attr.upgrade_level = std::min<uint8_t>(j["upgrade"].get<uint8_t>(), 15);
    if (j.contains("main_type"))
    {
        attr.main_type = static_cast<enchantment_type>(j["main_type"].get<uint8_t>());
        if (j.contains("main_value"))
            attr.main_value = std::min<uint8_t>(j["main_value"].get<uint8_t>(), 15);
    }
    if (j.contains("sub_type"))
    {
        attr.sub_type = static_cast<sub_enchantment_type>(j["sub_type"].get<uint8_t>());
        if (j.contains("sub_value"))
            attr.sub_value = std::min<uint8_t>(j["sub_value"].get<uint8_t>(), 15);
    }
    if (j.contains("custom_made"))
    {
        attr.custom_made = j["custom_made"].get<bool>();
        if (j.contains("custom_quality"))
            attr.custom_quality = std::clamp<int8_t>(j["custom_quality"].get<int8_t>(), -100, 100);
    }
    return attr;
}

auto to_string(enchantment_type type) -> const char*
{
    switch (type)
    {
    case enchantment_type::none:
        return "none";
    case enchantment_type::critical_bonus:
        return "critical_bonus";
    case enchantment_type::poison:
        return "poison";
    case enchantment_type::righteous:
        return "righteous";
    case enchantment_type::spell_on_hit:
        return "spell_on_hit";
    case enchantment_type::damage_reduction:
        return "damage_reduction";
    case enchantment_type::light:
        return "light";
    case enchantment_type::sharp:
        return "sharp";
    case enchantment_type::fire:
        return "fire";
    case enchantment_type::ancient:
        return "ancient";
    case enchantment_type::magic_damage:
        return "magic_damage";
    case enchantment_type::mana_conversion:
        return "mana_conversion";
    case enchantment_type::charge_critical:
        return "charge_critical";
    }
    return "unknown";
}

auto to_string(sub_enchantment_type type) -> const char*
{
    switch (type)
    {
    case sub_enchantment_type::none:
        return "none";
    case sub_enchantment_type::physical_resist:
        return "physical_resist";
    case sub_enchantment_type::attack_rating:
        return "attack_rating";
    case sub_enchantment_type::defense_rating:
        return "defense_rating";
    case sub_enchantment_type::hp_recovery:
        return "hp_recovery";
    case sub_enchantment_type::sp_recovery:
        return "sp_recovery";
    case sub_enchantment_type::mp_recovery:
        return "mp_recovery";
    case sub_enchantment_type::magic_resist:
        return "magic_resist";
    case sub_enchantment_type::physical_absorption:
        return "physical_absorption";
    case sub_enchantment_type::magic_absorption:
        return "magic_absorption";
    case sub_enchantment_type::critical_damage:
        return "critical_damage";
    case sub_enchantment_type::exp_bonus:
        return "exp_bonus";
    case sub_enchantment_type::gold_bonus:
        return "gold_bonus";
    }
    return "unknown";
}

} // namespace hb::item
