#include "platform/platform.h"
#include "bridge/handlers/game_handlers.h"
#include "bridge/handlers/broadcast_util.h"
#include "bridge/handlers/entity_builders.h"
#include "network/websocket_server.h"
#include "player/player_system.h"
#include "world/world_subsystem.h"
#include "combat/combat_system.h"
#include "combat/combat_events.h"
#include "magic/magic_system.h"
#include "magic/spell.h"
#include "npc/npc_system.h"
#include "npc/npc.h"
#include "inventory/inventory_system.h"
#include "item/item_system.h"
#include "item/item_ops.h"
#include "item/item_effect.h"
#include "item/special_ability.h"
#include "registry/item_registry.h"
#include "skill/skill_system.h"
#include "quest/quest_system.h"
#include "effect/effect_system.h"
#include "social/social_system.h"
#include "social/party.h"
#include "war/crusade/crusade_system.h"
#include "audit/item_audit_system.h"
#include "core/subsystem.h"
#include "core/logger.h"
#include "perf/perf_stats.h"

#include <array>
#include <random>

namespace hb::bridge
{

// ========== Player Attack / Magic / Skill Handlers ==========

void game_handlers::handle_player_attack(connection_id conn_id, const network::json_message& msg)
{
    auto* conn = require_in_game(conn_id, msg.seq);
    if (!conn)
        return;

    if (!players_)
    {
        send_error(conn_id, msg.seq, "internal_error", "Player system unavailable");
        return;
    }

    if (!combat_)
    {
        send_error(conn_id, msg.seq, "internal_error", "Combat system unavailable");
        return;
    }

    auto data_result = network::player_attack_request_data::from_json(msg.data);
    if (data_result.is_err())
    {
        send_error(conn_id, msg.seq, "invalid_request", data_result.error());
        return;
    }

    auto& data = data_result.value();
    auto pid = conn->player();

    auto* attacker = players_->get_player(pid);
    if (!attacker)
    {
        send_error(conn_id, msg.seq, "invalid_player", "Player not found");
        return;
    }

    // Check if attacker is alive
    if (attacker->is_dead())
    {
        network::attack_result_msg result{
            .hit = false, .target_id = data.target_id, .attacker_x = attacker->pos.x, .attacker_y = attacker->pos.y};
        conn->send(network::make_player_attack_response(msg.seq, false, &result, "attacker_dead"));
        return;
    }

    // Check if attacker has movement/action blocking status
    if (attacker->has_status(player::player_status::stunned) ||
        attacker->has_status(player::player_status::paralyzed) || attacker->has_status(player::player_status::frozen))
    {
        network::attack_result_msg result{
            .hit = false, .target_id = data.target_id, .attacker_x = attacker->pos.x, .attacker_y = attacker->pos.y};
        conn->send(network::make_player_attack_response(msg.seq, false, &result, "cannot_attack"));
        return;
    }

    // Enforce attack cooldown (100ms minimum between attacks)
    auto now = std::chrono::steady_clock::now();
    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - attacker->last_attack_time);
    if (elapsed.count() < 100)
    {
        network::attack_result_msg result{
            .hit = false, .target_id = data.target_id, .attacker_x = attacker->pos.x, .attacker_y = attacker->pos.y};
        conn->send(network::make_player_attack_response(msg.seq, false, &result, "attack_too_fast"));
        return;
    }

    // Peace mode — broadcast the attack action (client renders bow animation) but no damage
    if (!attacker->combat_mode)
    {
        attacker->last_attack_time = now;

        broadcast_player_action(*attacker,
                                {.entity_id = attacker->ecs_entity.id,
                                 .action = "attack",
                                 .direction = static_cast<int16_t>(attacker->facing),
                                 .target_id = data.target_id});

        network::attack_result_msg result{
            .hit = false, .target_id = data.target_id, .attacker_x = attacker->pos.x, .attacker_y = attacker->pos.y};
        conn->send(network::make_player_attack_response(msg.seq, true, &result));
        return;
    }

    // Validate target type
    if (data.tgt_type != network::target_type::player && data.tgt_type != network::target_type::npc)
    {
        network::attack_result_msg result{
            .hit = false, .target_id = data.target_id, .attacker_x = attacker->pos.x, .attacker_y = attacker->pos.y};
        conn->send(network::make_player_attack_response(msg.seq, false, &result, "invalid_target_type"));
        return;
    }

    // Resolve target — either player or NPC
    bool target_is_npc = (data.tgt_type == network::target_type::npc);
    entity::entity target_entity{data.target_id};
    world::position target_pos{};
    map_id target_map{};
    uint32_t target_broadcast_eid = data.target_id; // Entity ID for broadcast messages
    int16_t target_hp = 0;
    int16_t target_hp_max = 0;

    // Player target pointers (only set for PvP)
    std::optional<player_id> target_pid_opt;
    player::player* target_player = nullptr;

    // NPC target pointer (only set for PvE)
    npc::npc* target_npc = nullptr;

    if (target_is_npc)
    {
        if (!npc_)
        {
            send_error(conn_id, msg.seq, "internal_error", "NPC system unavailable");
            return;
        }
        target_npc = npc_->get_npc(target_entity);
        if (!target_npc || target_npc->is_dead())
        {
            // Dead or despawned NPC — allow the swing but deal 0 damage
            attacker->last_attack_time = now;
            network::attack_result_msg result{.hit = false,
                                              .target_id = data.target_id,
                                              .attacker_x = attacker->pos.x,
                                              .attacker_y = attacker->pos.y};
            conn->send(network::make_player_attack_response(msg.seq, true, &result));
            broadcast_player_action(*attacker,
                                    {.entity_id = attacker->ecs_entity.id,
                                     .action = "attack",
                                     .direction = static_cast<int16_t>(attacker->facing),
                                     .target_id = data.target_id});
            return;
        }

        // Cannot attack friendly NPCs (merchants, guards, trainers, etc.)
        if (target_npc->is_friendly())
        {
            network::attack_result_msg result{.hit = false,
                                              .target_id = data.target_id,
                                              .attacker_x = attacker->pos.x,
                                              .attacker_y = attacker->pos.y};
            conn->send(network::make_player_attack_response(msg.seq, false, &result, "cannot_attack_friendly"));
            return;
        }

        if (attacker->current_map != target_npc->current_map)
        {
            network::attack_result_msg result{.hit = false,
                                              .target_id = data.target_id,
                                              .attacker_x = attacker->pos.x,
                                              .attacker_y = attacker->pos.y};
            conn->send(network::make_player_attack_response(msg.seq, false, &result, "target_not_in_range"));
            return;
        }

        target_pos = target_npc->pos;
        target_map = target_npc->current_map;
        target_broadcast_eid = target_npc->entity_id.id;
        target_hp = static_cast<int16_t>(target_npc->hp);
        target_hp_max = static_cast<int16_t>(target_npc->max_hp);
    }
    else
    {
        // Player target
        target_pid_opt = players_->get_player_id_by_entity(target_entity);
        target_player = target_pid_opt ? players_->get_player(*target_pid_opt) : nullptr;
        if (!target_player)
        {
            network::attack_result_msg result{.hit = false,
                                              .target_id = data.target_id,
                                              .attacker_x = attacker->pos.x,
                                              .attacker_y = attacker->pos.y};
            conn->send(network::make_player_attack_response(msg.seq, false, &result, "target_not_found"));
            return;
        }

        if (target_player->is_dead())
        {
            // Dead player — allow the swing but deal 0 damage
            attacker->last_attack_time = now;
            network::attack_result_msg result{.hit = false,
                                              .target_id = data.target_id,
                                              .attacker_x = attacker->pos.x,
                                              .attacker_y = attacker->pos.y};
            conn->send(network::make_player_attack_response(msg.seq, true, &result));
            broadcast_player_action(*attacker,
                                    {.entity_id = attacker->ecs_entity.id,
                                     .action = "attack",
                                     .direction = static_cast<int16_t>(attacker->facing),
                                     .target_id = data.target_id});
            return;
        }

        if (attacker->current_map != target_player->current_map)
        {
            network::attack_result_msg result{.hit = false,
                                              .target_id = data.target_id,
                                              .attacker_x = attacker->pos.x,
                                              .attacker_y = attacker->pos.y};
            conn->send(network::make_player_attack_response(msg.seq, false, &result, "target_not_in_range"));
            return;
        }

        target_pos = target_player->pos;
        target_map = target_player->current_map;
        target_broadcast_eid = target_player->ecs_entity.id;
        target_hp = static_cast<int16_t>(target_player->hp);
        target_hp_max = static_cast<int16_t>(target_player->computed.max_hp);
    }

    // Update facing direction from client request
    attacker->facing = static_cast<world::direction>(data.direction);

    // Determine if this is a ranged attack (bow equipped)
    bool is_ranged = false;
    network::projectile_type projectile = network::projectile_type::none;
    const item_template* weapon_tmpl = nullptr;

    auto* item_reg = subsystems().get<item_registry>();
    if (item_reg && attacker->equipment.has_equipped(player::equip_slot::weapon))
    {
        auto atk_weapon_id = attacker->equipment.get_equipped(player::equip_slot::weapon);
        weapon_tmpl = atk_weapon_id ? item_reg->get(*atk_weapon_id) : nullptr;
        // Legacy bow detection: two-hand equip + sprite == 2 (bow sprite ID)
        if (weapon_tmpl && weapon_tmpl->equip_pos == item_equip_pos::two_hand && weapon_tmpl->sprite == 2)
        {
            is_ranged = true;
        }
    }

    // For ranged attacks: check arrows and determine projectile type
    uint32_t arrow_template_id = 0;
    int32_t ammo_remaining = -1;

    if (is_ranged)
    {
        if (!inventory_)
        {
            send_error(conn_id, msg.seq, "internal_error", "Inventory system unavailable");
            return;
        }

        // Find arrows in inventory - prefer poison arrows (78), then normal (77)
        auto attacker_entity = entity_id{pid.value};
        constexpr uint32_t poison_arrow_id = 78;
        constexpr uint32_t normal_arrow_id = 77;

        if (inventory_->has_item(attacker_entity, item_id{poison_arrow_id}))
        {
            arrow_template_id = poison_arrow_id;
            projectile = network::projectile_type::poison_arrow;
        }
        else if (inventory_->has_item(attacker_entity, item_id{normal_arrow_id}))
        {
            arrow_template_id = normal_arrow_id;
            projectile = network::projectile_type::arrow;
        }
        else
        {
            network::attack_result_msg result{.hit = false,
                                              .target_id = data.target_id,
                                              .attacker_x = attacker->pos.x,
                                              .attacker_y = attacker->pos.y,
                                              .is_ranged = true};
            conn->send(network::make_player_attack_response(msg.seq, false, &result, "no_ammo"));
            return;
        }
    }

    // Calculate distance
    int distance = attacker->pos.chebyshev_distance(target_pos);

    // Validate range based on attack type and weapon
    int max_range = 1; // Melee default
    if (is_ranged)
    {
        max_range = 10; // Bow range (standard Helbreath bow range)
    }
    else if (data.type == network::attack_type::dash)
    {
        max_range = 2; // Dash attack range
    }

    if (distance > max_range)
    {
        network::attack_result_msg result{.hit = false,
                                          .target_id = data.target_id,
                                          .attacker_x = attacker->pos.x,
                                          .attacker_y = attacker->pos.y,
                                          .is_ranged = is_ranged};
        conn->send(network::make_player_attack_response(msg.seq, false, &result, "target_not_in_range"));
        return;
    }

    // Ranged attacks require minimum distance (can't fire at melee range)
    if (is_ranged && distance < 2)
    {
        network::attack_result_msg result{.hit = false,
                                          .target_id = data.target_id,
                                          .attacker_x = attacker->pos.x,
                                          .attacker_y = attacker->pos.y,
                                          .is_ranged = true};
        conn->send(network::make_player_attack_response(msg.seq, false, &result, "target_too_close"));
        return;
    }

    // Consume arrow before processing attack
    if (is_ranged && arrow_template_id > 0)
    {
        inventory_->remove_item(entity_id{pid.value}, item_id{arrow_template_id}, 1);
        ammo_remaining = inventory_->count_item(entity_id{pid.value}, item_id{arrow_template_id});
    }

    // Build attack event - defender entity depends on target type
    combat::attack_event attack;
    attack.attacker = attacker->ecs_entity;
    attack.defender = target_is_npc ? target_entity : target_player->ecs_entity;
    attack.type = combat::damage_type::physical;
    attack.base_damage = 0; // Let combat_system calculate from stats
    attack.is_skill = false;
    attack.is_ranged = is_ranged;
    attack.is_dash = (data.type == network::attack_type::dash);
    attack.distance = distance;

    // Process the attack through combat system
    auto combat_result = combat_->process_attack(attack);

    // Update last attack time
    attacker->last_attack_time = now;

    // Re-read NPC HP after damage application (may have changed)
    if (target_is_npc && target_npc)
    {
        target_hp = static_cast<int16_t>(target_npc->hp);
        target_hp_max = static_cast<int16_t>(target_npc->max_hp);
    }
    else if (target_player)
    {
        target_hp = static_cast<int16_t>(target_player->hp);
        target_hp_max = static_cast<int16_t>(target_player->computed.max_hp);
    }

    // Build response
    network::attack_result_msg result{.hit = combat_result.hit.is_hit(),
                                      .resolved = true,
                                      .dodged = combat_result.hit.is_dodged(),
                                      .critical = combat_result.hit.is_critical(),
                                      .damage = combat_result.hit.final_damage,
                                      .target_id = data.target_id,
                                      .target_hp = target_hp,
                                      .target_hp_max = target_hp_max,
                                      .attacker_x = attacker->pos.x,
                                      .attacker_y = attacker->pos.y,
                                      .is_ranged = is_ranged,
                                      .ammo_count = ammo_remaining,
                                      .ammo_template_id = arrow_template_id};

    // Send response to attacker
    conn->send(network::make_player_attack_response(msg.seq, true, &result));

    // Broadcast action animation to nearby players
    std::string action_type = (data.type == network::attack_type::dash) ? "dash_attack" : "attack";
    broadcast_player_action(*attacker,
                            {.entity_id = attacker->ecs_entity.id,
                             .action = std::move(action_type),
                             .direction = static_cast<int16_t>(attacker->facing),
                             .target_id = target_broadcast_eid});

    // Broadcast attack to players who can see the attacker
    auto broadcast_msg = network::make_combat_attack_broadcast(attacker->ecs_entity.id,
                                                               target_broadcast_eid,
                                                               attacker->pos.x,
                                                               attacker->pos.y,
                                                               target_pos.x,
                                                               target_pos.y,
                                                               static_cast<int16_t>(attacker->facing),
                                                               combat_result.hit.is_hit(),
                                                               combat_result.hit.is_critical(),
                                                               combat_result.hit.final_damage,
                                                               projectile);
    broadcast_to_visible(players_, ws_server_, attacker->current_map, attacker->pos, broadcast_msg);

    // Weapon durability loss on hit
    if (combat_result.hit.is_hit() && item_ && attacker->equipment.has_equipped(player::equip_slot::weapon))
    {
        auto weapon_item_id = *attacker->equipment.get_equipped(player::equip_slot::weapon);
        item_->damage_durability(weapon_item_id, 1);

        // Check if weapon broke
        auto* weapon = item_->get_item(weapon_item_id);
        if (weapon && weapon->is_broken())
        {
            players_->unequip_item(pid, player::equip_slot::weapon);
            players_->recalculate_equipment_modifiers(pid);
            broadcast_equipment_change(pid, player::equip_slot::weapon, item_id{});
            send_stat_update(conn_id, *attacker);
            LOG_INFO(bridge, "Player {} weapon broke during attack", pid.value);
        }
    }

    // Grant weapon skill exp on hit
    if (combat_result.hit.is_hit() && skills_)
    {
        auto weapon_skill = skill::skill_type::hand_attack;
        if (item_ && attacker->equipment.has_equipped(player::equip_slot::weapon))
        {
            auto* weapon_item = item_->get_item(*attacker->equipment.get_equipped(player::equip_slot::weapon));
            if (weapon_item)
            {
                weapon_skill = skill::weapon_type_to_skill_type(weapon_item->weapon);
            }
        }
        skills_->record_skill_use(pid, weapon_skill);
    }

    // Flush deferred NPC deaths now that attack broadcasts have been sent
    // This ensures clients receive: attack_broadcast → entity_death → loot
    if (npc_ && combat_result.target_killed)
    {
        npc_->flush_pending_deaths();
    }

    LOG_DEBUG(bridge,
              "Player {} {} {} {} (hit={}, crit={}, dmg={}, target_hp={}, ranged={})",
              pid.value,
              is_ranged ? "shot" : "attacked",
              target_is_npc ? "npc" : "player",
              target_entity.id,
              combat_result.hit.is_hit(),
              combat_result.hit.is_critical(),
              combat_result.hit.final_damage,
              target_hp,
              is_ranged);
}

void game_handlers::handle_player_magic(connection_id conn_id, const network::json_message& msg)
{
    auto* conn = require_in_game(conn_id, msg.seq);
    if (!conn)
        return;

    if (!players_)
    {
        send_error(conn_id, msg.seq, "internal_error", "Player system unavailable");
        return;
    }

    auto data_result = network::player_magic_request_data::from_json(msg.data);
    if (data_result.is_err())
    {
        send_error(conn_id, msg.seq, "invalid_request", data_result.error());
        return;
    }

    auto& data = data_result.value();
    auto pid = conn->player();

    auto* player = players_->get_player(pid);
    if (!player)
    {
        send_error(conn_id, msg.seq, "invalid_player", "Player not found");
        return;
    }

    // Dead players cannot cast spells
    if (player->is_dead())
    {
        send_error(conn_id, msg.seq, "dead", "Cannot cast while dead");
        return;
    }

    if (!magic_)
    {
        send_error(conn_id, msg.seq, "internal_error", "Magic system unavailable");
        return;
    }

    // Build cast target from request data
    magic::cast_target target{};
    if (data.tgt_type == network::target_type::player || data.tgt_type == network::target_type::npc)
    {
        target.target = entity::entity{data.target_id};
    }
    if (data.target_x != 0 || data.target_y != 0)
    {
        target.target_pos = world::position{data.target_x, data.target_y};
    }

    // Look up spell to determine cast type
    auto sid = spell_id(static_cast<int>(data.spell_id));
    auto* spell_tmpl = magic_->get_spell(sid);
    if (!spell_tmpl)
    {
        conn->send(network::make_player_magic_response(msg.seq, false, nullptr, "unknown_spell"));
        return;
    }

    if (spell_tmpl->cast_time_ms > 0)
    {
        // Channeled/cast time spell
        auto cast_result = magic_->begin_cast(player->ecs_entity, sid, target);
        if (cast_result.is_err())
        {
            conn->send(network::make_player_magic_response(msg.seq, false, nullptr, cast_result.error()));
            return;
        }

        // Cast started - result will come via callback when cast completes
        if (skills_)
            skills_->record_skill_use(pid, skill::skill_type::magic);

        network::magic_result_msg result{.success = true,
                                         .spell_id = data.spell_id,
                                         .mana_cost = spell_tmpl->mana_cost,
                                         .damage = 0,
                                         .heal = 0,
                                         .target_id = data.target_id,
                                         .caster_mp = static_cast<int16_t>(player->mp)};
        conn->send(network::make_player_magic_response(msg.seq, true, &result));
    }
    else
    {
        // Instant cast
        auto cast_result = magic_->instant_cast(player->ecs_entity, sid, target);
        if (cast_result.is_err())
        {
            conn->send(network::make_player_magic_response(msg.seq, false, nullptr, cast_result.error()));
            return;
        }

        auto& effect = cast_result.value();
        if (effect.success && skills_)
        {
            skills_->record_skill_use(pid, skill::skill_type::magic);
        }

        network::magic_result_msg result{.success = effect.success,
                                         .spell_id = data.spell_id,
                                         .mana_cost = spell_tmpl->mana_cost,
                                         .damage = effect.damage_dealt,
                                         .heal = effect.heal_applied,
                                         .target_id = data.target_id,
                                         .caster_mp = static_cast<int16_t>(player->mp)};

        conn->send(network::make_player_magic_response(
            msg.seq,
            effect.success,
            &result,
            effect.success ? std::optional<std::string_view>{} : std::optional<std::string_view>{"cast_failed"}));
    }

    // Broadcast cast animation to nearby players
    broadcast_player_action(*player,
                            {.entity_id = player->ecs_entity.id,
                             .action = "magic",
                             .direction = static_cast<int16_t>(player->facing),
                             .target_id = data.target_id,
                             .spell_id = data.spell_id});

    // Flush deferred NPC deaths now that spell broadcasts have been sent
    if (npc_ && npc_->has_pending_deaths())
    {
        npc_->flush_pending_deaths();
    }

    LOG_DEBUG(bridge, "Player {} magic request (spell={}, target={})", pid.value, data.spell_id, data.target_id);
}

void game_handlers::handle_player_skill(connection_id conn_id, const network::json_message& msg)
{
    auto* conn = require_in_game(conn_id, msg.seq);
    if (!conn)
        return;

    if (!players_)
    {
        send_error(conn_id, msg.seq, "internal_error", "Player system unavailable");
        return;
    }

    auto data_result = network::player_skill_request_data::from_json(msg.data);
    if (data_result.is_err())
    {
        send_error(conn_id, msg.seq, "invalid_request", data_result.error());
        return;
    }

    auto& data = data_result.value();
    auto pid = conn->player();

    auto* player = players_->get_player(pid);
    if (!player)
    {
        send_error(conn_id, msg.seq, "invalid_player", "Player not found");
        return;
    }

    // Dead players cannot use skills
    if (player->is_dead())
    {
        send_error(conn_id, msg.seq, "dead", "Cannot use skills while dead");
        return;
    }

    // TODO: Implement actual skill use through skill_system
    // Placeholder response
    network::skill_result_msg result{
        .success = false, .skill_id = data.skill_id, .effect_value = 0, .target_id = data.target_id};

    conn->send(network::make_player_skill_response(msg.seq, false, &result, "not_implemented"));
    LOG_DEBUG(bridge, "Player {} skill request (skill={}, target={})", pid.value, data.skill_id, data.target_id);
}

// ========== Combat Event Callbacks ==========

namespace
{

auto damage_type_to_string(combat::damage_type dt) -> std::string_view
{
    switch (dt)
    {
    case combat::damage_type::physical:
        return "physical";
    case combat::damage_type::magic:
        return "magic";
    case combat::damage_type::fire:
        return "fire";
    case combat::damage_type::ice:
        return "ice";
    case combat::damage_type::lightning:
        return "lightning";
    case combat::damage_type::poison:
        return "poison";
    case combat::damage_type::holy:
        return "holy";
    case combat::damage_type::dark:
        return "dark";
    case combat::damage_type::pure:
        return "pure";
    default:
        return "physical";
    }
}

auto spell_element_to_damage_type_string(magic::spell_element elem) -> std::string_view
{
    switch (elem)
    {
    case magic::spell_element::fire:
        return "fire";
    case magic::spell_element::ice:
        return "ice";
    case magic::spell_element::lightning:
        return "lightning";
    case magic::spell_element::holy:
        return "holy";
    case magic::spell_element::dark:
        return "dark";
    default:
        return "magic";
    }
}

// Use npc::npc::npc_category_to_string() from npc.h

} // namespace

void game_handlers::on_damage_dealt(const combat::damage_event& event)
{
    if (!players_ || !ws_server_)
        return;

    // Determine hit effect type for visual feedback
    auto& hr = event.result;
    std::string effect_type;
    if (hr.is_miss())
    {
        effect_type = "miss";
    }
    else if (hr.is_dodged())
    {
        effect_type = "dodge";
    }
    else if (hr.is_blocked())
    {
        effect_type = "block";
    }
    else if (hr.is_hit())
    {
        effect_type = "damage";
    }
    else
    {
        return; // No visual effect for this result
    }

    // Source entity_id is already an ecs_entity — use directly
    uint32_t source_eid = event.source.id;

    // Try player target first
    auto* target = players_->get_player_by_entity(event.target);
    if (target)
    {
        // Player target: interrupt movement
        if (target->connection.value != 0)
        {
            auto* conn = ws_server_->get_connection(target->connection);
            if (conn)
            {
                conn->clear_destination();
            }
        }

        // Skip HP update and combat_effect for killing blows — entity_death carries the damage
        if (target->is_dead())
            return;

        auto target_pid = players_->get_player_id_by_entity(event.target);
        if (target_pid)
            broadcast_hp_update(*target_pid, target->hp, target->computed.max_hp);

        network::combat_effect_data effect{.source_id = source_eid,
                                           .target_id = target->ecs_entity.id,
                                           .effect_type = std::move(effect_type),
                                           .value = hr.final_damage,
                                           .damage_type = std::string(damage_type_to_string(hr.type)),
                                           .spell_id = std::nullopt,
                                           .is_critical = hr.is_critical(),
                                           .target_x = target->pos.x,
                                           .target_y = target->pos.y};

        broadcast_combat_effect(target->current_map, target->pos, effect);
        return;
    }

    // Try NPC target — HP update is handled by on_damage_callback, but combat_effect
    // (floating damage numbers) needs to be broadcast here
    // Skip for killing blows — entity_death carries the damage info
    if (npc_)
    {
        auto* target_npc = npc_->get_npc(event.target);
        if (target_npc && !target_npc->is_dead())
        {
            network::combat_effect_data effect{.source_id = source_eid,
                                               .target_id = target_npc->entity_id.id,
                                               .effect_type = std::move(effect_type),
                                               .value = hr.final_damage,
                                               .damage_type = std::string(damage_type_to_string(hr.type)),
                                               .spell_id = std::nullopt,
                                               .is_critical = hr.is_critical(),
                                               .target_x = target_npc->pos.x,
                                               .target_y = target_npc->pos.y};

            broadcast_combat_effect(target_npc->current_map, target_npc->pos, effect);
        }
    }
}

void game_handlers::on_entity_death(const combat::death_event& event)
{
    if (!players_ || !ws_server_)
        return;

    // Debug log for every entity death
    {
        // Identify victim
        std::string victim_name = "unknown";
        world::position victim_pos{};
        auto v_pid = players_->get_player_id_by_entity(event.victim);
        if (v_pid)
        {
            if (auto* p = players_->get_player(*v_pid))
            {
                victim_name = "player:" + p->name;
                victim_pos = p->pos;
            }
        }
        else if (npc_)
        {
            if (auto* n = npc_->get_npc(event.victim))
            {
                victim_name = "npc:" + n->name;
                victim_pos = n->pos;
            }
        }

        // Identify killer
        std::string killer_name = "unknown";
        world::position killer_pos{};
        auto k_pid = players_->get_player_id_by_entity(event.killer);
        if (k_pid)
        {
            if (auto* p = players_->get_player(*k_pid))
            {
                killer_name = "player:" + p->name;
                killer_pos = p->pos;
            }
        }
        else if (npc_)
        {
            if (auto* n = npc_->get_npc(event.killer))
            {
                killer_name = "npc:" + n->name;
                killer_pos = n->pos;
            }
        }

        const char* method_str = "misc";
        switch (event.method)
        {
        case combat::kill_method::melee:
            method_str = "melee";
            break;
        case combat::kill_method::dash:
            method_str = "dash";
            break;
        case combat::kill_method::bow:
            method_str = "bow";
            break;
        case combat::kill_method::magic:
            method_str = "magic";
            break;
        case combat::kill_method::misc:
            method_str = "misc";
            break;
        }

        LOG_DEBUG(bridge,
                  "ENTITY_DEATH: victim={} at ({},{}) killed_by={} at ({},{}) damage={} method={}",
                  victim_name,
                  victim_pos.x,
                  victim_pos.y,
                  killer_name,
                  killer_pos.x,
                  killer_pos.y,
                  event.killing_damage,
                  method_str);
    }

    // Only handle player deaths beyond this point
    auto victim_pid_opt = players_->get_player_id_by_entity(event.victim);
    if (!victim_pid_opt)
        return;
    auto victim_pid = *victim_pid_opt;
    auto* victim = players_->get_player(victim_pid);
    if (!victim)
        return;

    // Move from live occupant slot to dead slot on tile
    if (world_)
    {
        auto* m = world_->get_map(victim->current_map);
        if (m)
        {
            m->clear_occupant(victim->pos);
            m->set_dead_entity(victim->pos, hb::entity_id{victim->ecs_entity.index()}, world::owner_type::player);
        }
    }

    auto killer_pid_opt = players_->get_player_id_by_entity(event.killer);
    player_id killer_pid = killer_pid_opt.value_or(player_id{0});

    // Broadcast death to nearby players
    broadcast_entity_death(victim_pid, killer_pid, event.killing_damage);

    // Handle death penalties and respawn
    handle_player_death(victim_pid, event);
}

void game_handlers::handle_player_death(player_id pid, const combat::death_event& event)
{
    auto* perf = subsystems().get<perf::perf_stats_system>();
    PERF_TIMER(perf, perf::metric_category::player_death);

    if (!players_ || !ws_server_ || !world_)
        return;

    auto* player = players_->get_player(pid);
    if (!player)
        return;

    // 1. Clear status effects and combat target
    player->status = player::player_status::none;
    player->target = {};

    int64_t xp_lost = 0;
    int32_t pk_points_change = 0;
    int32_t gold_reward = 0;
    std::string killer_name;

    player_id killer_pid{event.killer.id};
    auto* killer = players_->get_player(killer_pid);
    if (killer)
    {
        killer_name = killer->name;
    }

    // 2. PvP-specific penalties
    if (event.is_pvp && killer)
    {
        // XP penalty for victim
        int64_t penalty = calculate_death_xp_penalty(player->experience.level);
        xp_lost = player->experience.remove_experience(penalty);

        // If victim is innocent, killer gains PK points
        if (player->pk.is_innocent())
        {
            killer->pk.add_kill();
            pk_points_change = 50;
            LOG_INFO(bridge, "Player {} gained PK point for killing innocent {}", killer_pid.value, pid.value);
        }

        // If killer is innocent and victim is a PKer, award bounty
        if (killer->pk.is_innocent() && (player->pk.is_criminal() || player->pk.is_murderer()))
        {
            gold_reward = calculate_pk_bounty_reward(player->experience.level);
            // Cap at max reward gold (from game config default)
            gold_reward = std::min(gold_reward, static_cast<int32_t>(99999999));
            // TODO: Actually add gold to killer's inventory when economy wiring is complete
            LOG_INFO(bridge,
                     "Player {} earned {} gold bounty for killing PKer {}",
                     killer_pid.value,
                     gold_reward,
                     pid.value);
        }

        // Crusade PvP kill rewards (construction points + contribution)
        if (crusade_ && crusade_->is_active())
        {
            // Base exp reward approximation for contribution calculation
            int32_t pvp_exp_reward =
                static_cast<int32_t>(player->experience.level) * static_cast<int32_t>(player->experience.level) / 2;
            crusade_->on_player_kill(killer_pid, pid, static_cast<int32_t>(player->experience.level), pvp_exp_reward);
        }
    }

    // 3. Determine respawn location
    std::string spawn_map = get_respawn_map_name(player->faction);
    world::position spawn_pos = get_respawn_position(spawn_map);

    // 4. Save player state after applying penalties
    if (save_callback_)
    {
        save_callback_(pid);
    }

    // 5. Send death info to the dead player (client-initiated respawn, no auto-timer)
    uint32_t killer_eid = 0;
    if (killer)
    {
        killer_eid = killer->ecs_entity.id;
    }

    network::player_death_info_data death_info{.killer_id = killer_eid,
                                               .killer_name = killer_name,
                                               .is_pvp = event.is_pvp,
                                               .xp_lost = xp_lost,
                                               .pk_points_change = pk_points_change,
                                               .gold_reward = gold_reward,
                                               .respawn_delay_ms = 0,
                                               .respawn_map = spawn_map,
                                               .respawn_x = spawn_pos.x,
                                               .respawn_y = spawn_pos.y};

    auto* conn = ws_server_->get_connection(player->connection);
    if (conn && conn->is_open())
    {
        conn->send(network::make_player_death_info(death_info));
    }

    // Player stays dead in-place — respawn is client-initiated (respawn_request)
    // or triggered by another player's resurrection spell.

    LOG_INFO(bridge,
             "Player {} died (pvp={}, xp_lost={}, pk_change={}, bounty={}), awaiting respawn request",
             pid.value,
             event.is_pvp,
             xp_lost,
             pk_points_change,
             gold_reward);
}

void game_handlers::execute_respawn(player_id pid, const std::string& map_name, const world::position& pos)
{
    auto* perf = subsystems().get<perf::perf_stats_system>();
    PERF_TIMER(perf, perf::metric_category::player_death);

    if (!players_ || !ws_server_ || !combat_)
        return;

    auto* player = players_->get_player(pid);
    if (!player)
        return; // Player disconnected during respawn delay

    // Clear dead slot at death position (if still ours)
    if (world_)
    {
        auto* m = world_->get_map(player->current_map);
        if (m)
        {
            auto dead_eid = m->get_dead_entity(player->pos);
            if (dead_eid && dead_eid->value == pid.value)
            {
                m->clear_dead_entity(player->pos);
            }
        }
    }

    // Restore HP/MP to 50%
    player->hp = player->computed.max_hp / 2;
    player->mp = player->computed.max_mp / 2;

    // Set 3-second invulnerability
    combat_->set_invulnerable(player->ecs_entity, 3000);

    // Execute teleport to spawn
    execute_player_teleport(pid, player->connection, 0, map_name, pos, world::direction::south);
}

auto game_handlers::calculate_death_xp_penalty(uint8_t level) -> int64_t
{
    if (level <= 1)
        return 0;

    // Legacy Helbreath formula: random(1, level/2+1) * 50
    static thread_local std::mt19937 rng{std::random_device{}()};
    int max_roll = level / 2 + 1;
    std::uniform_int_distribution<int> dist(1, max_roll);
    return static_cast<int64_t>(dist(rng)) * 50;
}

auto game_handlers::calculate_pk_bounty_reward(uint8_t level) -> int32_t
{
    return static_cast<int32_t>(level) * 3;
}

auto game_handlers::get_respawn_map_name(hb::faction f) -> std::string
{
    switch (f)
    {
    case faction::aresden:
        return "aresden";
    case faction::elvine:
        return "elvine";
    default:
        return "default";
    }
}

auto game_handlers::get_respawn_position(const std::string& map_name) -> world::position
{
    if (!world_)
        return {18, 18};

    auto* m = world_->get_map_by_name(map_name);
    if (!m)
    {
        // Map not found - try to fall back
        return {18, 18};
    }

    auto pos = m->get_random_initial_point();
    if (pos.has_value())
    {
        return *pos;
    }

    // No initial points defined - fallback
    return {18, 18};
}

// ========== Combat Mode Change ==========

// ========== Stat Points ==========

// player_system::add_stat_point ja existia com os indices certos, mas nenhum caminho
// chegava ate ele: era codigo morto e os 3 pontos por nivel ficavam presos para sempre.
void game_handlers::handle_stat_point(connection_id conn_id, const network::json_message& msg)
{
    auto* conn = require_in_game(conn_id, msg.seq);
    if (!conn)
        return;

    auto data_result = network::stat_point_request_data::from_json(msg.data);
    if (data_result.is_err())
    {
        send_error(conn_id, msg.seq, "invalid_request", data_result.error());
        return;
    }
    const auto stat = data_result.value().stat;

    auto pid = conn->player();
    auto* plr = players_->get_player(pid);
    if (!plr)
    {
        send_error(conn_id, msg.seq, "invalid_player", "Player not found");
        return;
    }

    if (plr->stats_pts.available <= 0)
    {
        conn->send(network::make_stat_point_response(msg.seq, false, stat, 0, "no_points_available"));
        return;
    }

    players_->add_stat_point(pid, stat);

    // Re-fetch: add_stat_point recalculates derived stats.
    plr = players_->get_player(pid);
    const int16_t remaining = plr ? plr->stats_pts.available : 0;
    conn->send(network::make_stat_point_response(msg.seq, true, stat, remaining, {}));

    if (plr)
    {
        send_stat_update(conn_id, *plr);
        LOG_DEBUG(bridge, "Player {} spent a stat point on {} ({} left)", pid.value, stat, remaining);
    }
}

void game_handlers::handle_combat_mode_change(connection_id conn_id, const network::json_message& msg)
{
    auto* conn = require_in_game(conn_id, msg.seq);
    if (!conn)
        return;

    auto pid = conn->player();
    auto* plr = players_->get_player(pid);
    if (!plr)
        return;

    if (plr->is_dead())
    {
        send_error(conn_id, msg.seq, "dead", "Cannot change combat mode while dead");
        return;
    }

    // Toggle combat mode
    plr->combat_mode = !plr->combat_mode;

    // Confirm to the toggling player
    conn->send(network::make_combat_mode_change_response(msg.seq, plr->combat_mode));

    // Broadcast to nearby players
    network::combat_mode_change_broadcast_data data{.entity_id = plr->ecs_entity.id, .combat_mode = plr->combat_mode};
    auto broadcast_msg = network::make_combat_mode_change_broadcast(data);

    broadcast_to_visible(players_, ws_server_, plr->current_map, plr->pos, broadcast_msg, pid);

    // Forward to admin spectators
    for (auto admin_conn : ws_server_->get_admin_subscribers(plr->current_map))
    {
        ws_server_->send(admin_conn, broadcast_msg);
    }
}

// ========== Respawn Request ==========

void game_handlers::handle_respawn_request(connection_id conn_id, const network::json_message& msg)
{
    auto* conn = require_in_game(conn_id, msg.seq);
    if (!conn)
        return;

    if (!players_ || !ws_server_ || !combat_)
    {
        send_error(conn_id, msg.seq, "internal_error", "Required subsystems unavailable");
        return;
    }

    auto pid = conn->player();
    auto* player = players_->get_player(pid);
    if (!player)
    {
        send_error(conn_id, msg.seq, "invalid_player", "Player not found");
        return;
    }

    if (!player->is_dead())
    {
        conn->send(network::make_respawn_response(msg.seq, false, "", 0, 0, "not_dead"));
        return;
    }

    std::string spawn_map = get_respawn_map_name(player->faction);
    world::position spawn_pos = get_respawn_position(spawn_map);

    execute_respawn(pid, spawn_map, spawn_pos);

    conn->send(network::make_respawn_response(msg.seq, true, spawn_map, spawn_pos.x, spawn_pos.y));

    LOG_INFO(bridge,
             "Player {} respawned at {} ({}, {}) via client request",
             pid.value,
             spawn_map,
             spawn_pos.x,
             spawn_pos.y);
}

// ========== Combat Broadcast Methods ==========

void game_handlers::broadcast_hp_update(player_id target, int32_t hp, int32_t hp_max)
{
    if (!players_ || !ws_server_)
        return;

    auto* player = players_->get_player(target);
    if (!player)
        return;

    auto hp_msg = network::make_entity_hp_update(player->ecs_entity.id, hp, hp_max);
    broadcast_to_visible(players_, ws_server_, player->current_map, player->pos, hp_msg);

    // Forward to admin spectators
    for (auto admin_conn : ws_server_->get_admin_subscribers(player->current_map))
    {
        ws_server_->send(admin_conn, hp_msg);
    }
}

void game_handlers::broadcast_entity_death(player_id victim, player_id killer, int32_t killing_damage)
{
    if (!players_ || !ws_server_)
        return;

    auto* victim_player = players_->get_player(victim);
    if (!victim_player)
        return;

    // Resolve killer's entity_id (may be offline/disconnected)
    uint32_t killer_entity_id = 0;
    auto* killer_player = players_->get_player(killer);
    if (killer_player)
    {
        killer_entity_id = killer_player->ecs_entity.id;
    }

    auto death_msg = network::make_entity_death(
        victim_player->ecs_entity.id, killer_entity_id, victim_player->pos.x, victim_player->pos.y, killing_damage);
    broadcast_to_visible(players_, ws_server_, victim_player->current_map, victim_player->pos, death_msg);

    // Forward to admin spectators
    for (auto admin_conn : ws_server_->get_admin_subscribers(victim_player->current_map))
    {
        ws_server_->send(admin_conn, death_msg);
    }
}

// ========== Combat Effect Broadcast Methods ==========

void game_handlers::broadcast_combat_effect(map_id map,
                                            const world::position& pos,
                                            const network::combat_effect_data& data)
{
    if (!players_ || !ws_server_)
        return;

    auto msg = network::make_combat_effect(data);
    broadcast_to_visible(players_, ws_server_, map, pos, msg);

    // Forward to admin spectators
    for (auto admin_conn : ws_server_->get_admin_subscribers(map))
    {
        ws_server_->send(admin_conn, msg);
    }
}

void game_handlers::broadcast_combat_effect_to_faction(map_id map,
                                                       const world::position& pos,
                                                       hb::faction fac,
                                                       const network::combat_effect_data& data)
{
    if (!players_ || !ws_server_)
        return;

    auto msg = network::make_combat_effect(data);
    for_each_visible_connection(
        players_,
        ws_server_,
        map,
        pos,
        [&](player_id, player::player& p, network::ws_connection& conn)
        {
            if (p.faction == fac)
                conn.send(msg);
        });

    // Forward to admin spectators (admins see all factions)
    for (auto admin_conn : ws_server_->get_admin_subscribers(map))
    {
        ws_server_->send(admin_conn, msg);
    }
}

void game_handlers::on_spell_cast(entity::entity caster,
                                  const magic::spell_template& spell,
                                  const magic::spell_effect_result& result)
{
    if (!players_ || !ws_server_)
        return;
    if (!result.success)
        return; // Failed casts aren't visible

    // Find caster position - could be player or NPC (resolve via ECS entity, not player id)
    auto* caster_player = players_->get_player_by_entity(caster);
    if (!caster_player)
        return; // Only handle player casters for now

    auto caster_map = caster_player->current_map;
    auto caster_pos = caster_player->pos;
    auto caster_eid = caster_player->ecs_entity.id;
    auto dmg_type = std::string(spell_element_to_damage_type_string(spell.element));

    // Create-Food (type 10): drop a basic food item at the caster's feet
    if (spell.spell_type == magic_type::create && item_ && world_)
    {
        static constexpr std::array<uint32_t, 3> food_templates{98, 99, 100}; // Baguette, Meat, Fish
        thread_local std::mt19937 rng{std::random_device{}()};
        std::uniform_int_distribution<size_t> dist(0, food_templates.size() - 1);
        auto template_id = item_id{food_templates[dist(rng)]};

        auto create_result = item_->create_from_template(template_id, 1);
        if (create_result.is_ok())
        {
            auto dropped = create_result.value();
            item_ops::drop_loot(dropped, caster_map, caster_pos.x, caster_pos.y, item_, world_);
            broadcast_ground_item_spawn(caster_map, caster_pos, dropped);
            LOG_DEBUG(bridge, "Create-Food: player {} created item {} at ({},{})",
                      caster_player->id.value, dropped.value, caster_pos.x, caster_pos.y);
        }
    }

    // Determine broadcast based on spell category
    switch (spell.category)
    {
    case magic::spell_category::attack:
    case magic::spell_category::debuff:
    {
        for (auto target_ent : result.affected_targets)
        {
            // Determine target position for the broadcast
            int16_t tx = caster_pos.x;
            int16_t ty = caster_pos.y;
            uint32_t target_eid = target_ent.id;

            // Try to get target position from player or NPC
            if (auto* tp = players_->get_player_by_entity(target_ent))
            {
                tx = tp->pos.x;
                ty = tp->pos.y;
                target_eid = tp->ecs_entity.id;
            }
            else if (npc_)
            {
                if (auto* tn = npc_->get_npc(target_ent))
                {
                    tx = tn->pos.x;
                    ty = tn->pos.y;
                    target_eid = tn->entity_id.id;
                }
            }

            auto effect_type = spell.category == magic::spell_category::attack ? "damage" : "debuff";
            auto value = spell.category == magic::spell_category::attack ? result.damage_dealt : spell.effect_value;

            network::combat_effect_data effect{.source_id = caster_eid,
                                               .target_id = target_eid,
                                               .effect_type = effect_type,
                                               .value = value,
                                               .damage_type = dmg_type,
                                               .spell_id = spell.id.value,
                                               .is_critical = false,
                                               .target_x = tx,
                                               .target_y = ty};

            if (spell.category == magic::spell_category::debuff)
            {
                broadcast_combat_effect_to_faction(caster_map, world::position{tx, ty}, caster_player->faction, effect);
            }
            else
            {
                broadcast_combat_effect(caster_map, world::position{tx, ty}, effect);
            }
        }
        break;
    }

    case magic::spell_category::healing:
    {
        for (auto target_ent : result.affected_targets)
        {
            int16_t tx = caster_pos.x;
            int16_t ty = caster_pos.y;
            uint32_t target_eid = target_ent.id;

            if (auto* tp = players_->get_player_by_entity(target_ent))
            {
                tx = tp->pos.x;
                ty = tp->pos.y;
                target_eid = tp->ecs_entity.id;
            }

            network::combat_effect_data effect{.source_id = caster_eid,
                                               .target_id = target_eid,
                                               .effect_type = "heal",
                                               .value = result.heal_applied,
                                               .damage_type = dmg_type,
                                               .spell_id = spell.id.value,
                                               .is_critical = false,
                                               .target_x = tx,
                                               .target_y = ty};

            broadcast_combat_effect(caster_map, world::position{tx, ty}, effect);
        }
        break;
    }

    case magic::spell_category::buff:
    {
        for (auto target_ent : result.affected_targets)
        {
            int16_t tx = caster_pos.x;
            int16_t ty = caster_pos.y;
            uint32_t target_eid = target_ent.id;

            if (auto* tp = players_->get_player_by_entity(target_ent))
            {
                tx = tp->pos.x;
                ty = tp->pos.y;
                target_eid = tp->ecs_entity.id;
            }

            network::combat_effect_data effect{.source_id = caster_eid,
                                               .target_id = target_eid,
                                               .effect_type = "buff",
                                               .value = spell.effect_value,
                                               .damage_type = {},
                                               .spell_id = spell.id.value,
                                               .is_critical = false,
                                               .target_x = tx,
                                               .target_y = ty};

            broadcast_combat_effect_to_faction(caster_map, world::position{tx, ty}, caster_player->faction, effect);
        }
        break;
    }

    default:
        break;
    }
}

// ========== NPC Kill XP Distribution ==========

void game_handlers::distribute_npc_kill_exp(entity::entity killer, int32_t base_exp)
{
    if (!players_ || base_exp <= 0)
        return;

    // Resolve killer to player_id
    auto killer_pid_opt = players_->get_player_id_by_entity(killer);
    if (!killer_pid_opt)
        return;
    auto killer_pid = *killer_pid_opt;

    auto* killer_player = players_->get_player(killer_pid);
    if (!killer_player)
        return;

    // Check party membership
    social::party* pt = nullptr;
    if (social_)
    {
        auto party_id = social_->get_player_party(killer_pid);
        if (party_id.is_valid())
        {
            pt = social_->get_party(party_id);
        }
    }

    // No party, or individual mode, or low XP — award all to killer
    if (!pt || pt->experience == social::exp_mode::individual || base_exp < 10)
    {
        players_->add_experience(killer_pid, base_exp);
        LOG_DEBUG(bridge, "Awarded {} XP to player '{}' (solo kill)", base_exp, killer_player->name);
        return;
    }

    // Get eligible party members: same map, alive (hp > 0)
    auto same_map_ids = pt->members_in_map(killer_player->current_map);
    std::vector<std::pair<player_id, int16_t>> eligible; // pid, level
    int32_t total_levels = 0;
    for (auto pid : same_map_ids)
    {
        auto* p = players_->get_player(pid);
        if (p && !p->is_dead())
        {
            eligible.emplace_back(pid, p->experience.level);
            total_levels += p->experience.level;
        }
    }

    if (eligible.empty())
        return;

    // Single eligible member gets full XP (no bonus)
    if (eligible.size() == 1)
    {
        players_->add_experience(eligible[0].first, base_exp);
        LOG_DEBUG(bridge, "Awarded {} XP to player (party of 1 eligible)", base_exp);
        return;
    }

    auto eligible_count = static_cast<int>(eligible.size());

    if (pt->experience == social::exp_mode::equal_split)
    {
        auto per_member = social::calculate_party_exp_share(base_exp, eligible_count);
        for (auto& [pid, level] : eligible)
        {
            players_->add_experience(pid, per_member);
        }
        LOG_DEBUG(
            bridge, "Party equal split: {} base XP -> {} each for {} members", base_exp, per_member, eligible_count);
    }
    else if (pt->experience == social::exp_mode::level_weighted)
    {
        for (auto& [pid, level] : eligible)
        {
            auto share = social::calculate_level_weighted_exp(base_exp, eligible_count, level, total_levels);
            players_->add_experience(pid, share);
        }
        LOG_DEBUG(bridge,
                  "Party level-weighted: {} base XP for {} members (total levels {})",
                  base_exp,
                  eligible_count,
                  total_levels);
    }
}

} // namespace hb::bridge
