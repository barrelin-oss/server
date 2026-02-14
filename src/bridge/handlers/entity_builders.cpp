// entity_builders.cpp
// Helper functions to build visible_entity_msg for player and NPC spawns

#include "bridge/handlers/entity_builders.h"
#include "player/player.h"
#include "npc/npc.h"
#include "item/item_system.h"
#include "item/item.h"
#include "registry/item_registry.h"
#include "registry/item_template.h"
#include "effect/effect_system.h"
#include "effect/active_effect.h"
#include "core/enums.h"

namespace hb::bridge {

namespace {

auto rarity_to_string(item::item_rarity r) -> std::string_view
{
    switch (r) {
        case item::item_rarity::common:    return "common";
        case item::item_rarity::uncommon:  return "uncommon";
        case item::item_rarity::rare:      return "rare";
        case item::item_rarity::epic:      return "epic";
        case item::item_rarity::legendary: return "legendary";
        case item::item_rarity::ancient:   return "ancient";
        default: return "common";
    }
}

auto spell_effect_type_to_string(spell_effect_type t) -> std::string_view
{
    switch (t) {
        case spell_effect_type::none:           return "none";
        case spell_effect_type::damage:         return "damage";
        case spell_effect_type::heal:           return "heal";
        case spell_effect_type::buff_attack:    return "buff_attack";
        case spell_effect_type::buff_defense:   return "buff_defense";
        case spell_effect_type::buff_speed:     return "buff_speed";
        case spell_effect_type::debuff_slow:    return "debuff_slow";
        case spell_effect_type::debuff_blind:   return "debuff_blind";
        case spell_effect_type::stun:           return "stun";
        case spell_effect_type::poison:         return "poison";
        case spell_effect_type::burn:           return "burn";
        case spell_effect_type::freeze:         return "freeze";
        case spell_effect_type::teleport:       return "teleport";
        case spell_effect_type::summon:         return "summon";
        case spell_effect_type::polymorph:      return "polymorph";
        case spell_effect_type::invisibility:   return "invisibility";
        case spell_effect_type::resurrection:   return "resurrection";
        case spell_effect_type::mana_drain:     return "mana_drain";
        case spell_effect_type::mana_restore:   return "mana_restore";
        default: return "unknown";
    }
}

auto status_flag_to_string(player::player_status flag) -> std::string_view
{
    switch (flag) {
        case player::player_status::poisoned:    return "poisoned";
        case player::player_status::paralyzed:   return "paralyzed";
        case player::player_status::invisible:   return "invisible";
        case player::player_status::frozen:      return "frozen";
        case player::player_status::berserk:     return "berserk";
        case player::player_status::protection:  return "protection";
        case player::player_status::defense_up:  return "defense_up";
        case player::player_status::attack_up:   return "attack_up";
        case player::player_status::magic_up:    return "magic_up";
        case player::player_status::haste:       return "haste";
        case player::player_status::slow:        return "slow";
        case player::player_status::cursed:      return "cursed";
        case player::player_status::stunned:     return "stunned";
        case player::player_status::silenced:    return "silenced";
        case player::player_status::invincible:  return "invincible";
        default: return "";
    }
}

auto build_equip_visual(
    player::equip_slot slot,
    const player::equipment_state& equip,
    const player::appearance_state& appr,
    const item::item_system* items,
    const item_registry* item_reg
) -> network::equip_visual_msg
{
    network::equip_visual_msg v;

    // Copy cached appearance data
    switch (slot) {
        case player::equip_slot::weapon: v.appr = appr.weapon.appr; v.color = appr.weapon.color; break;
        case player::equip_slot::shield: v.appr = appr.shield.appr; v.color = appr.shield.color; break;
        case player::equip_slot::body:   v.appr = appr.body.appr;   v.color = appr.body.color;   break;
        case player::equip_slot::pants:  v.appr = appr.pants.appr;  v.color = appr.pants.color;  break;
        case player::equip_slot::head:   v.appr = appr.head.appr;   v.color = appr.head.color;   break;
        case player::equip_slot::arms:   v.appr = appr.arms.appr;   v.color = appr.arms.color;   break;
        case player::equip_slot::boots:  v.appr = appr.boots.appr;  v.color = appr.boots.color;  break;
        case player::equip_slot::cape:   v.appr = appr.cape.appr;   v.color = appr.cape.color;   break;
        default: break;
    }

    // Look up item name and rarity
    const auto& equipped = equip.get(slot);
    if (!equipped.is_empty() && items && item_reg) {
        if (auto* inst = items->get_item(equipped.id)) {
            v.name = inst->name;
            v.rarity = std::string(rarity_to_string(inst->rarity));
        }
    }

    return v;
}

auto collect_status_effects(player::player_status status) -> std::vector<std::string>
{
    std::vector<std::string> result;

    // Check each flag bit
    constexpr player::player_status flags[] = {
        player::player_status::poisoned,
        player::player_status::paralyzed,
        player::player_status::invisible,
        player::player_status::frozen,
        player::player_status::berserk,
        player::player_status::protection,
        player::player_status::defense_up,
        player::player_status::attack_up,
        player::player_status::magic_up,
        player::player_status::haste,
        player::player_status::slow,
        player::player_status::cursed,
        player::player_status::stunned,
        player::player_status::silenced,
        player::player_status::invincible,
    };

    for (auto flag : flags) {
        if ((status & flag) != player::player_status::none) {
            auto name = status_flag_to_string(flag);
            if (!name.empty()) {
                result.emplace_back(name);
            }
        }
    }

    return result;
}

auto collect_active_buffs(
    entity::entity eid,
    const effect::effect_system* effects
) -> std::vector<network::buff_info_msg>
{
    std::vector<network::buff_info_msg> result;
    if (!effects) return result;

    auto* effect_list = effects->get_effects(eid);
    if (!effect_list) return result;

    for (const auto& eff : *effect_list) {
        network::buff_info_msg b;
        b.type = std::string(spell_effect_type_to_string(eff.type));
        b.spell_id = eff.source_spell ? static_cast<uint32_t>(eff.source_spell->value) : 0;
        b.magnitude = eff.magnitude;
        b.remaining_ms = eff.duration_ms > 0
            ? std::max<int64_t>(0, eff.expires_at_ms - eff.applied_at_ms)
            : 0;
        result.push_back(std::move(b));
    }

    return result;
}

}  // namespace

auto build_player_spawn(
    const player::player& plr,
    std::string_view hostility,
    const item::item_system* items,
    const item_registry* item_reg,
    const effect::effect_system* effects
) -> network::visible_entity_msg
{
    network::visible_entity_msg msg;

    // Base fields
    msg.entity_id = plr.ecs_entity.id;
    msg.type = "player";
    msg.name = plr.name;
    msg.x = plr.pos.x;
    msg.y = plr.pos.y;
    msg.hp_percent = static_cast<int16_t>(plr.hp_percent() * 100);
    msg.direction = static_cast<int16_t>(plr.facing);

    // Faction / hostility / PK
    msg.faction = std::string(faction_to_string(plr.faction));
    msg.hostility = std::string(hostility);
    msg.pk_status = std::string(plr.pk.status_string());
    msg.guild_name = plr.guild_name;
    msg.guild_tag = plr.guild_tag;
    msg.combat_mode = plr.combat_mode;

    // Base appearance
    msg.gender = static_cast<int8_t>(plr.sex);
    msg.skin_color = static_cast<int8_t>(plr.skin_color);
    msg.hair_style = static_cast<int8_t>(plr.hair_style);
    msg.hair_color = static_cast<int8_t>(plr.hair_color);
    msg.underwear_color = static_cast<int8_t>(plr.underwear_color);
    msg.player_level = plr.experience.level;

    // Equipment visuals
    msg.weapon_visual = build_equip_visual(player::equip_slot::weapon, plr.equipment, plr.appearance, items, item_reg);
    msg.shield_visual = build_equip_visual(player::equip_slot::shield, plr.equipment, plr.appearance, items, item_reg);
    msg.body_visual   = build_equip_visual(player::equip_slot::body,   plr.equipment, plr.appearance, items, item_reg);
    msg.pants_visual  = build_equip_visual(player::equip_slot::pants,  plr.equipment, plr.appearance, items, item_reg);
    msg.head_visual   = build_equip_visual(player::equip_slot::head,   plr.equipment, plr.appearance, items, item_reg);
    msg.arms_visual   = build_equip_visual(player::equip_slot::arms,   plr.equipment, plr.appearance, items, item_reg);
    msg.boots_visual  = build_equip_visual(player::equip_slot::boots,  plr.equipment, plr.appearance, items, item_reg);
    msg.cape_visual   = build_equip_visual(player::equip_slot::cape,   plr.equipment, plr.appearance, items, item_reg);

    msg.weapon_glow = plr.appearance.weapon_glow;
    msg.shield_glow = plr.appearance.shield_glow;
    msg.weapon_speed = plr.appearance.weapon_speed;

    // Status effects
    msg.status_effects = collect_status_effects(plr.status);

    // Active buffs
    msg.active_buffs = collect_active_buffs(plr.ecs_entity, effects);

    return msg;
}

auto build_npc_spawn(
    const npc::npc& n,
    std::string_view hostility,
    bool is_dead
) -> network::visible_entity_msg
{
    network::visible_entity_msg msg;

    msg.entity_id = n.entity_id.id;
    msg.type = "npc";
    msg.name = n.name;
    msg.x = n.pos.x;
    msg.y = n.pos.y;
    msg.hp_percent = n.max_hp > 0
        ? static_cast<int16_t>((n.hp * 100) / n.max_hp)
        : static_cast<int16_t>(100);
    msg.direction = static_cast<int16_t>(n.facing);
    msg.hostility = std::string(hostility);
    msg.template_id = n.template_id.value;
    msg.sprite_id = n.sprite_id;
    msg.level = n.level;
    msg.category = std::string(npc::npc_category_to_string(n.category));
    msg.is_dead = is_dead;

    return msg;
}

}  // namespace hb::bridge
