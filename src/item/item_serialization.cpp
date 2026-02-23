// item_serialization.cpp
// Universal item-to-JSON serialization implementation.

#include "item/item_serialization.h"

#include <nlohmann/json.hpp>
#include <string>

namespace hb::item
{

// --- Enum-to-string converters ---

auto item_type_to_string(item_type t) -> std::string_view
{
    switch (t)
    {
    case item_type::weapon:
        return "weapon";
    case item_type::armor:
        return "armor";
    case item_type::accessory:
        return "accessory";
    case item_type::consumable:
        return "consumable";
    case item_type::material:
        return "material";
    case item_type::quest:
        return "quest";
    case item_type::gold:
        return "gold";
    case item_type::none:
        return "none";
    }
    return "none";
}

auto equip_pos_to_string(equip_pos p) -> std::string_view
{
    switch (p)
    {
    case equip_pos::none:
        return "none";
    case equip_pos::head:
        return "head";
    case equip_pos::body:
        return "body";
    case equip_pos::arms:
        return "arms";
    case equip_pos::pants:
        return "pants";
    case equip_pos::boots:
        return "boots";
    case equip_pos::weapon:
        return "weapon";
    case equip_pos::shield:
        return "shield";
    case equip_pos::twohand:
        return "twohand";
    case equip_pos::ring_left:
        return "ring_left";
    case equip_pos::ring_right:
        return "ring_right";
    case equip_pos::amulet:
        return "amulet";
    case equip_pos::cape:
        return "cape";
    case equip_pos::angel:
        return "angel";
    case equip_pos::fullbody:
        return "fullbody";
    }
    return "none";
}

auto weapon_type_to_string(weapon_type w) -> std::string_view
{
    switch (w)
    {
    case weapon_type::none:
        return "none";
    case weapon_type::sword:
        return "sword";
    case weapon_type::axe:
        return "axe";
    case weapon_type::hammer:
        return "hammer";
    case weapon_type::staff:
        return "staff";
    case weapon_type::wand:
        return "wand";
    case weapon_type::bow:
        return "bow";
    case weapon_type::dagger:
        return "dagger";
    case weapon_type::fist:
        return "fist";
    }
    return "none";
}

auto rarity_to_string(item_rarity r) -> std::string_view
{
    switch (r)
    {
    case item_rarity::common:
        return "common";
    case item_rarity::uncommon:
        return "uncommon";
    case item_rarity::rare:
        return "rare";
    case item_rarity::epic:
        return "epic";
    case item_rarity::legendary:
        return "legendary";
    case item_rarity::ancient:
        return "ancient";
    }
    return "common";
}

auto effect_type_to_string(item_effect_type t) -> std::string_view
{
    switch (t)
    {
    case item_effect_type::none:
        return "none";
    case item_effect_type::hp_bonus:
        return "hp_bonus";
    case item_effect_type::mp_bonus:
        return "mp_bonus";
    case item_effect_type::sp_bonus:
        return "sp_bonus";
    case item_effect_type::str_bonus:
        return "str_bonus";
    case item_effect_type::dex_bonus:
        return "dex_bonus";
    case item_effect_type::vit_bonus:
        return "vit_bonus";
    case item_effect_type::int_bonus:
        return "int_bonus";
    case item_effect_type::mag_bonus:
        return "mag_bonus";
    case item_effect_type::chr_bonus:
        return "chr_bonus";
    case item_effect_type::attack_bonus:
        return "attack_bonus";
    case item_effect_type::defense_bonus:
        return "defense_bonus";
    case item_effect_type::magic_attack_bonus:
        return "magic_attack_bonus";
    case item_effect_type::magic_defense_bonus:
        return "magic_defense_bonus";
    case item_effect_type::hit_bonus:
        return "hit_bonus";
    case item_effect_type::dodge_bonus:
        return "dodge_bonus";
    case item_effect_type::critical_bonus:
        return "critical_bonus";
    case item_effect_type::hp_steal:
        return "hp_steal";
    case item_effect_type::mp_steal:
        return "mp_steal";
    case item_effect_type::damage_reduction:
        return "damage_reduction";
    case item_effect_type::exp_bonus:
        return "exp_bonus";
    case item_effect_type::gold_bonus:
        return "gold_bonus";
    case item_effect_type::poison_damage:
        return "poison_damage";
    case item_effect_type::fire_damage:
        return "fire_damage";
    case item_effect_type::ice_damage:
        return "ice_damage";
    case item_effect_type::lightning_damage:
        return "lightning_damage";
    }
    return "none";
}

auto special_ability_type_to_string(special_ability_type t) -> std::string_view
{
    switch (t)
    {
    case special_ability_type::none:
        return "none";
    case special_ability_type::hp_halve:
        return "hp_halve";
    case special_ability_type::poison:
        return "poison";
    case special_ability_type::paralyze:
        return "paralyze";
    case special_ability_type::warrior_boost:
        return "warrior_boost";
    case special_ability_type::life_drain:
        return "life_drain";
    case special_ability_type::spell_immunity:
        return "spell_immunity";
    case special_ability_type::attack_block:
        return "attack_block";
    case special_ability_type::high_spell_immunity:
        return "high_spell_immunity";
    }
    return "none";
}

auto enchantment_type_to_string(enchantment_type t) -> std::string_view
{
    // Reuse existing to_string() but return string_view
    return to_string(t);
}

auto sub_enchantment_type_to_string(sub_enchantment_type t) -> std::string_view
{
    return to_string(t);
}

// --- Serialization ---

auto serialize_item(const item& itm) -> nlohmann::json
{
    nlohmann::json j;

    // Identity
    j["item_id"] = itm.id.value;
    j["template_id"] = itm.template_id.value;

    // Name with upgrade suffix
    if (itm.attribute.upgrade_level > 0)
    {
        j["name"] = itm.name + " +" + std::to_string(itm.attribute.upgrade_level);
    }
    else
    {
        j["name"] = itm.name;
    }

    // Type classification
    j["type"] = item_type_to_string(itm.type);
    j["equip_pos"] = equip_pos_to_string(itm.equip_position);

    // weapon_type: only include for weapons
    if (itm.type == item_type::weapon)
    {
        j["weapon_type"] = weapon_type_to_string(itm.weapon);
    }

    j["rarity"] = rarity_to_string(itm.rarity);

    // Stack
    j["count"] = itm.count;

    // Physical properties
    j["weight"] = itm.weight;
    j["price"] = itm.price;

    // Combat stats
    j["damage_min"] = itm.damage_min;
    j["damage_max"] = itm.damage_max;
    j["defense"] = itm.defense;
    j["magic_defense"] = itm.magic_defense;

    // Durability
    j["durability"] = itm.durability;
    j["max_durability"] = itm.max_durability;

    // Requirements
    j["level_req"] = itm.level_requirement;
    j["str_req"] = itm.str_requirement;
    j["dex_req"] = itm.dex_requirement;
    j["int_req"] = itm.int_requirement;
    j["mag_req"] = itm.mag_requirement;

    // Effects: only include non-empty entries
    nlohmann::json effects_arr = nlohmann::json::array();
    for (const auto& eff : itm.effects)
    {
        if (!eff.is_empty())
        {
            effects_arr.push_back({
                {"type", effect_type_to_string(eff.type)},
                {"value", eff.value},
            });
        }
    }
    j["effects"] = std::move(effects_arr);

    // Attribute: omit entirely if empty (no upgrades, no enchants, not custom)
    if (!itm.attribute.is_empty())
    {
        nlohmann::json attr_j;
        attr_j["upgrade_level"] = itm.attribute.upgrade_level;

        if (itm.attribute.main_type != enchantment_type::none)
        {
            attr_j["main_enchant"] = {
                {"type", enchantment_type_to_string(itm.attribute.main_type)},
                {"value", itm.attribute.main_value},
            };
        }
        else
        {
            attr_j["main_enchant"] = nullptr;
        }

        if (itm.attribute.sub_type != sub_enchantment_type::none)
        {
            attr_j["sub_enchant"] = {
                {"type", sub_enchantment_type_to_string(itm.attribute.sub_type)},
                {"value", itm.attribute.sub_value},
            };
        }
        else
        {
            attr_j["sub_enchant"] = nullptr;
        }

        attr_j["custom_made"] = itm.attribute.custom_made;
        j["attribute"] = std::move(attr_j);
    }

    // Special ability: omit if none
    // The item struct doesn't store special_ability directly — it's on the template.
    // If caller needs it, they should set it before serializing.
    // We don't have a field on item::item for this, so we skip it here.
    // (The task spec says "Look up from template or item field" — since there's no field
    //  on the item struct, we omit by default. Callers can add it after.)

    // Visual
    j["color"] = itm.color;
    j["sprite_id"] = itm.sprite;
    j["sprite_frame"] = itm.sprite_frame;

    // Flags
    j["tradeable"] = itm.tradeable;
    j["droppable"] = itm.droppable;
    j["two_handed"] = itm.two_handed;

    // Binding: omit if not bound
    if (itm.bound_to.has_value())
    {
        j["bound_to"] = itm.bound_to->value;
    }

    return j;
}

} // namespace hb::item
