// entity_builders.cpp
// Helper functions to build visible_entity_msg for player and NPC spawns,
// and shared functions for sending entity data on login/teleport.

// Include platform header first to define NOMINMAX before Windows headers
#include "platform/platform.h"

#include "bridge/handlers/entity_builders.h"
#include "player/player.h"
#include "player/player_system.h"
#include "npc/npc.h"
#include "npc/npc_system.h"
#include "item/item_system.h"
#include "item/item.h"
#include "registry/item_registry.h"
#include "registry/item_template.h"
#include "effect/effect_system.h"
#include "effect/active_effect.h"
#include "world/world_subsystem.h"
#include "world/map.h"
#include "network/websocket_server.h"
#include "core/enums.h"
#include "core/logger.h"

namespace hb::bridge
{

namespace
{

auto rarity_to_string(item::item_rarity r) -> std::string_view
{
    switch (r)
    {
    case item::item_rarity::common:
        return "common";
    case item::item_rarity::uncommon:
        return "uncommon";
    case item::item_rarity::rare:
        return "rare";
    case item::item_rarity::epic:
        return "epic";
    case item::item_rarity::legendary:
        return "legendary";
    case item::item_rarity::ancient:
        return "ancient";
    default:
        return "common";
    }
}

auto spell_effect_type_to_string(spell_effect_type t) -> std::string_view
{
    switch (t)
    {
    case spell_effect_type::none:
        return "none";
    case spell_effect_type::damage:
        return "damage";
    case spell_effect_type::heal:
        return "heal";
    case spell_effect_type::buff_attack:
        return "buff_attack";
    case spell_effect_type::buff_defense:
        return "buff_defense";
    case spell_effect_type::buff_speed:
        return "buff_speed";
    case spell_effect_type::debuff_slow:
        return "debuff_slow";
    case spell_effect_type::debuff_blind:
        return "debuff_blind";
    case spell_effect_type::stun:
        return "stun";
    case spell_effect_type::poison:
        return "poison";
    case spell_effect_type::burn:
        return "burn";
    case spell_effect_type::freeze:
        return "freeze";
    case spell_effect_type::teleport:
        return "teleport";
    case spell_effect_type::summon:
        return "summon";
    case spell_effect_type::polymorph:
        return "polymorph";
    case spell_effect_type::invisibility:
        return "invisibility";
    case spell_effect_type::resurrection:
        return "resurrection";
    case spell_effect_type::mana_drain:
        return "mana_drain";
    case spell_effect_type::mana_restore:
        return "mana_restore";
    default:
        return "unknown";
    }
}

auto status_flag_to_string(player::player_status flag) -> std::string_view
{
    switch (flag)
    {
    case player::player_status::poisoned:
        return "poisoned";
    case player::player_status::paralyzed:
        return "paralyzed";
    case player::player_status::invisible:
        return "invisible";
    case player::player_status::frozen:
        return "frozen";
    case player::player_status::berserk:
        return "berserk";
    case player::player_status::protection:
        return "protection";
    case player::player_status::defense_up:
        return "defense_up";
    case player::player_status::attack_up:
        return "attack_up";
    case player::player_status::magic_up:
        return "magic_up";
    case player::player_status::haste:
        return "haste";
    case player::player_status::slow:
        return "slow";
    case player::player_status::cursed:
        return "cursed";
    case player::player_status::stunned:
        return "stunned";
    case player::player_status::silenced:
        return "silenced";
    case player::player_status::invincible:
        return "invincible";
    default:
        return "";
    }
}

auto build_equip_visual(player::equip_slot slot,
                        const player::equipment_state& equip,
                        const player::appearance_state& appr,
                        const item::item_system* items,
                        const item_registry* item_reg) -> network::equip_visual_msg
{
    network::equip_visual_msg v;

    // Copy cached appearance data
    switch (slot)
    {
    case player::equip_slot::weapon:
        v.appr = appr.weapon.appr;
        v.color = appr.weapon.color;
        break;
    case player::equip_slot::shield:
        v.appr = appr.shield.appr;
        v.color = appr.shield.color;
        break;
    case player::equip_slot::body:
        v.appr = appr.body.appr;
        v.color = appr.body.color;
        break;
    case player::equip_slot::pants:
        v.appr = appr.pants.appr;
        v.color = appr.pants.color;
        break;
    case player::equip_slot::head:
        v.appr = appr.head.appr;
        v.color = appr.head.color;
        break;
    case player::equip_slot::arms:
        v.appr = appr.arms.appr;
        v.color = appr.arms.color;
        break;
    case player::equip_slot::boots:
        v.appr = appr.boots.appr;
        v.color = appr.boots.color;
        break;
    case player::equip_slot::cape:
        v.appr = appr.cape.appr;
        v.color = appr.cape.color;
        break;
    default:
        break;
    }

    // Look up item name and rarity
    auto equipped = equip.get_equipped(slot);
    if (equipped.has_value() && items && item_reg)
    {
        if (auto* inst = items->get_item(*equipped))
        {
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

    for (auto flag : flags)
    {
        if ((status & flag) != player::player_status::none)
        {
            auto name = status_flag_to_string(flag);
            if (!name.empty())
            {
                result.emplace_back(name);
            }
        }
    }

    return result;
}

auto collect_active_buffs(entity::entity eid,
                          const effect::effect_system* effects) -> std::vector<network::buff_info_msg>
{
    std::vector<network::buff_info_msg> result;
    if (!effects)
        return result;

    auto* effect_list = effects->get_effects(eid);
    if (!effect_list)
        return result;

    for (const auto& eff : *effect_list)
    {
        network::buff_info_msg b;
        b.type = std::string(spell_effect_type_to_string(eff.type));
        b.spell_id = eff.source_spell ? static_cast<uint32_t>(eff.source_spell->value) : 0;
        b.magnitude = eff.magnitude;
        b.remaining_ms = eff.duration_ms > 0 ? std::max<int64_t>(0, eff.expires_at_ms - eff.applied_at_ms) : 0;
        result.push_back(std::move(b));
    }

    return result;
}

} // namespace

auto build_player_spawn(const player::player& plr,
                        std::string_view hostility,
                        const item::item_system* items,
                        const item_registry* item_reg,
                        const effect::effect_system* effects) -> network::visible_entity_msg
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
    msg.body_visual = build_equip_visual(player::equip_slot::body, plr.equipment, plr.appearance, items, item_reg);
    msg.pants_visual = build_equip_visual(player::equip_slot::pants, plr.equipment, plr.appearance, items, item_reg);
    msg.head_visual = build_equip_visual(player::equip_slot::head, plr.equipment, plr.appearance, items, item_reg);
    msg.arms_visual = build_equip_visual(player::equip_slot::arms, plr.equipment, plr.appearance, items, item_reg);
    msg.boots_visual = build_equip_visual(player::equip_slot::boots, plr.equipment, plr.appearance, items, item_reg);
    msg.cape_visual = build_equip_visual(player::equip_slot::cape, plr.equipment, plr.appearance, items, item_reg);

    msg.weapon_glow = plr.appearance.weapon_glow;
    msg.shield_glow = plr.appearance.shield_glow;
    msg.weapon_speed = plr.appearance.weapon_speed;

    // Death state
    msg.is_dead = plr.is_dead();

    // Status effects
    msg.status_effects = collect_status_effects(plr.status);

    // Active buffs
    msg.active_buffs = collect_active_buffs(plr.ecs_entity, effects);

    return msg;
}

auto build_npc_spawn(const npc::npc& n, std::string_view hostility, bool is_dead) -> network::visible_entity_msg
{
    network::visible_entity_msg msg;

    msg.entity_id = n.entity_id.id;
    msg.type = "npc";
    msg.name = n.name;
    msg.x = n.pos.x;
    msg.y = n.pos.y;
    msg.hp_percent = n.max_hp > 0 ? static_cast<int16_t>((n.hp * 100) / n.max_hp) : static_cast<int16_t>(100);
    msg.direction = static_cast<int16_t>(n.facing);
    msg.hostility = std::string(hostility);
    msg.template_id = n.template_id.value;
    msg.sprite_id = n.sprite_id;
    msg.level = n.level;
    msg.category = std::string(npc::npc_category_to_string(n.category));
    msg.is_dead = is_dead;

    return msg;
}

// ========== Shared send functions (login + teleport) ==========

void send_visible_entity_spawns(network::ws_connection* conn,
                                player_id viewer,
                                map_id map,
                                const world::position& pos,
                                player::player_system* players,
                                npc::npc_system* npc_sys,
                                const world::world_subsystem* world,
                                const item::item_system* items,
                                const item_registry* item_reg,
                                const effect::effect_system* effects)
{
    if (!conn || !conn->is_open() || !players || !world)
        return;

    auto* player = players->get_player(viewer);
    if (!player)
        return;

    int rx = player->visibility_radius_x;
    int ry = player->visibility_radius_y;

    auto* m = world->get_map(map);
    if (!m)
        return;

    // Send entity_spawn for nearby players via spatial index
    auto coarse_radius = std::max(rx, ry);
    auto nearby_entities = m->get_entities_in_range(pos, coarse_radius);

    for (auto eid : nearby_entities)
    {
        auto* p = players->get_player_by_entity(entity::entity{eid.value});
        if (!p)
            continue;
        if (p == player)
            continue; // Skip self

        if (std::abs(p->pos.x - pos.x) > rx || std::abs(p->pos.y - pos.y) > ry)
            continue;

        conn->send(network::make_entity_spawn(
            0,
            build_player_spawn(
                *p, player_hostility(player->faction, p->faction), items, item_reg, effects)));
    }

    // Send npc_spawn for nearby alive NPCs
    if (npc_sys)
    {
        npc_sys->for_each_npc_on_map(
            map,
            [&](auto /*id*/, const hb::npc::npc& n)
            {
                if (n.is_dead())
                    return;

                if (std::abs(n.pos.x - pos.x) > rx || std::abs(n.pos.y - pos.y) > ry)
                    return;

                network::npc_spawn_data data{
                    .entity_id = n.entity_id.id,
                    .template_id = n.template_id.value,
                    .sprite_id = n.sprite_id,
                    .name = n.name,
                    .x = n.pos.x,
                    .y = n.pos.y,
                    .direction = static_cast<uint8_t>(n.facing),
                    .hp = n.hp,
                    .max_hp = n.max_hp,
                    .level = n.level,
                    .category = std::string(npc::npc_category_to_string(n.category)),
                    .hostility = std::string(npc::npc_hostility_for_player(
                        n, player->faction, player->pk.is_criminal(), player->pk.is_murderer())),
                    .attributes = npc::npc_special_ability_strings(n.special_ability)};
                conn->send(network::make_npc_spawn_message(data));
            });

        // Send npc_spawn for dead NPC corpses
        npc_sys->for_each_npc_on_map(
            map,
            [&](auto /*id*/, const hb::npc::npc& n)
            {
                if (!n.is_dead())
                    return;

                if (std::abs(n.pos.x - pos.x) > rx || std::abs(n.pos.y - pos.y) > ry)
                    return;

                network::npc_spawn_data data{
                    .entity_id = n.entity_id.id,
                    .template_id = n.template_id.value,
                    .sprite_id = n.sprite_id,
                    .name = n.name,
                    .x = n.pos.x,
                    .y = n.pos.y,
                    .direction = static_cast<uint8_t>(n.facing),
                    .hp = n.hp,
                    .max_hp = n.max_hp,
                    .level = n.level,
                    .category = std::string(npc::npc_category_to_string(n.category)),
                    .hostility = std::string(npc::npc_hostility_for_player(
                        n, player->faction, player->pk.is_criminal(), player->pk.is_murderer())),
                    .attributes = npc::npc_special_ability_strings(n.special_ability),
                    .is_dead = true};
                conn->send(network::make_npc_spawn_message(data));
            });
    }
}

void send_visible_ground_items(network::ws_connection* conn,
                               map_id map,
                               const world::position& pos,
                               int radius_x,
                               int radius_y,
                               const world::world_subsystem* world,
                               const item::item_system* items,
                               const item_registry* item_reg)
{
    if (!conn || !conn->is_open() || !world || !items)
        return;

    for (int16_t dx = static_cast<int16_t>(-radius_x); dx <= radius_x; ++dx)
    {
        for (int16_t dy = static_cast<int16_t>(-radius_y); dy <= radius_y; ++dy)
        {
            world::position tile_pos{static_cast<int16_t>(pos.x + dx), static_cast<int16_t>(pos.y + dy)};

            auto ground_items = world->get_ground_items(map, tile_pos);
            if (ground_items.empty())
                continue;

            // Only send the top-most item (last in FILO stack)
            auto top_item = ground_items.back();
            auto* itm = items->get_item(top_item);
            if (!itm)
                continue;

            std::string display_name = itm->name;
            int16_t gi_sprite = 0;
            int16_t gi_frame = 0;
            int8_t gi_color = 0;
            if (item_reg)
            {
                if (auto* tmpl = item_reg->get(itm->template_id))
                {
                    display_name = network::get_display_name(tmpl->name, itm->attribute);
                    gi_sprite = tmpl->sprite;
                    gi_frame = tmpl->sprite_frame;
                    gi_color = tmpl->item_color;
                }
            }

            network::ground_item_spawn_data data{.item_id = top_item.value,
                                                 .template_id = itm->template_id.value,
                                                 .item_name = std::move(display_name),
                                                 .count = itm->count,
                                                 .x = tile_pos.x,
                                                 .y = tile_pos.y,
                                                 .ground_sprite = gi_sprite,
                                                 .ground_sprite_frame = gi_frame,
                                                 .item_color = gi_color,
                                                 .attribute = itm->attribute};

            conn->send(network::make_ground_item_spawn(data));
        }
    }
}

void send_map_teleporters(network::ws_connection* conn, const world::map& map)
{
    if (!conn || !conn->is_open())
        return;

    const auto& teleports = map.get_all_teleports();

    network::map_teleporters_msg teleporters_msg;
    teleporters_msg.map_name = std::string(map.name());

    for (const auto& [pos, dest] : teleports)
    {
        network::teleporter_info_msg tp_info{
            .id = (static_cast<uint32_t>(pos.x) << 16) | static_cast<uint32_t>(static_cast<uint16_t>(pos.y)),
            .x = pos.x,
            .y = pos.y,
            .dest_map = dest.dest_map,
            .dest_x = dest.dest_x,
            .dest_y = dest.dest_y,
            .dest_dir = static_cast<int16_t>(dest.dest_dir)};
        teleporters_msg.teleporters.push_back(tp_info);
    }

    conn->send(network::make_map_teleporters(teleporters_msg));

    LOG_DEBUG(bridge, "Sent {} teleporters for map {}", teleporters_msg.teleporters.size(), map.name());
}

} // namespace hb::bridge
