// wave4_handlers.cpp
// Wave 4 Migration: Combat message handlers

#include "bridge/handlers/wave4_handlers.h"
#include "bridge/handlers/wave3_handlers.h" // For motion_type
#include "bridge/handler_registry.h"
#include "bridge/message_router.h"
#include "core/subsystem.h"
#include "core/logger.h"
#include "combat/combat_system.h"
#include "magic/magic_system.h"
#include "skill/skill_system.h"
#include "player/player_system.h"
#include "protocol/message_reader.h"
#include "protocol/message_writer.h"
#include "network/network_subsystem.h"

#include <spdlog/fmt/fmt.h>
#include <vector>

namespace hb::bridge::wave4
{

namespace
{
// Track which handlers we've registered for cleanup
std::vector<protocol::message_id> registered_handlers;

constexpr auto wave4_subsystem = "wave4_combat";

// Combat-related common types
constexpr auto magic_subtype = protocol::common_type::magic;
constexpr auto skill_subtype = protocol::common_type::req_use_skill;
} // namespace

// ========== Handler Registration ==========

auto register_wave4_handlers() -> size_t
{
    LOG_INFO(proto_bridge, "Registering Wave 4 (combat) handlers...");

    // Register handler for command_common magic/skill usage
    handlers(wave4_subsystem)
        .on_player(protocol::message_id::command_common,
                   make_simple_handler(
                       [](const handler_context& ctx, protocol::message_reader& reader)
                       {
                           if (reader.remaining() < 2)
                           {
                               return handle_result::not_handled;
                           }

                           auto subtype = static_cast<protocol::common_type>(reader.read_u16());

                           switch (subtype)
                           {
                           case protocol::common_type::magic:
                               return handle_magic(ctx);

                           case protocol::common_type::req_use_skill:
                               return handle_use_skill(ctx);

                           default:
                               // Not a combat command we handle
                               return handle_result::not_handled;
                           }
                       }));

    registered_handlers.push_back(protocol::message_id::command_common);

    LOG_INFO(proto_bridge, "Wave 4 handlers registered: {} handlers", registered_handlers.size());

    return registered_handlers.size();
}

void unregister_wave4_handlers()
{
    LOG_INFO(proto_bridge, "Unregistering Wave 4 handlers...");

    for (auto msg_id : registered_handlers)
    {
        router().unregister_handler(msg_id);
    }
    registered_handlers.clear();
}

// ========== Attack Handler ==========

auto handle_attack(const handler_context& ctx) -> handle_result
{
    protocol::message_reader reader{ctx.raw_data};
    reader.skip(4); // message_id
    reader.skip(1); // motion type (attack)

    if (reader.remaining() < 6)
    {
        LOG_WARN(proto_bridge, "Attack: insufficient data (conn={})", ctx.connection.value);
        return handle_result::not_handled;
    }

    auto x = reader.read_i16();
    auto y = reader.read_i16();
    auto dir = reader.read_i8();

    // Read target ID if present
    entity_id target{};
    if (reader.remaining() >= 4)
    {
        target = entity_id{reader.read_u32()};
    }

    LOG_DEBUG(proto_bridge,
              "Attack: player={} at ({},{}) dir={} target={}",
              ctx.player.value,
              x,
              y,
              static_cast<int>(dir),
              target.value);

    return handle_attack_target(ctx, target);
}

auto handle_attack_target(const handler_context& ctx, entity_id target_id) -> handle_result
{
    auto* combat = subsystems().get<combat::combat_system>();
    if (!combat)
    {
        LOG_WARN(proto_bridge, "Combat system not available for attack");
        return handle_result::not_handled;
    }

    auto* player_sys = subsystems().get<player::player_system>();
    if (!player_sys)
    {
        return handle_result::not_handled;
    }

    auto* attacker = player_sys->get_player(ctx.player);
    if (!attacker)
    {
        return handle_result::not_handled;
    }

    // Check if we can attack — use the real ECS entity handles (ids on the wire
    // are full entity ids; player_id values are NOT entity ids)
    auto attacker_entity = attacker->ecs_entity;
    auto target_entity = entity::entity{target_id.value};

    if (!combat->can_attack(attacker_entity, target_entity))
    {
        LOG_DEBUG(proto_bridge, "Attack blocked: cannot attack target {}", target_id.value);
        return handle_result::handled; // Handled, but attack was invalid
    }

    // Build attack event
    combat::attack_event attack{.attacker = attacker_entity,
                                .defender = target_entity,
                                .type = combat::damage_type::physical,
                                .base_damage = attacker->computed.attack_power,
                                .is_skill = false,
                                .skill_id = 0};

    // Process the attack
    auto result = combat->process_attack(attack);

    // Send damage event to client
    send_damage_event(ctx, target_id, result.hit.final_damage, result.hit.is_critical(), result.target_killed);

    return handle_result::handled;
}

// ========== Magic Handler ==========

auto handle_magic(const handler_context& ctx) -> handle_result
{
    protocol::message_reader reader{ctx.raw_data};
    reader.skip(4); // message_id
    reader.skip(2); // subtype (magic)

    if (reader.remaining() < 4)
    {
        LOG_WARN(proto_bridge, "Magic: insufficient data (conn={})", ctx.connection.value);
        return handle_result::not_handled;
    }

    auto spell_id_val = reader.read_u16();
    auto target_x = reader.read_i16();
    auto target_y = reader.read_i16();

    // Read target entity if present
    entity_id target{};
    if (reader.remaining() >= 4)
    {
        target = entity_id{reader.read_u32()};
    }

    LOG_DEBUG(proto_bridge,
              "Magic: player={} spell={} target=({},{}) entity={}",
              ctx.player.value,
              spell_id_val,
              target_x,
              target_y,
              target.value);

    return handle_cast_spell(ctx, spell_id{spell_id_val}, target);
}

auto handle_cast_spell(const handler_context& ctx, spell_id spell, entity_id target) -> handle_result
{
    auto* magic = subsystems().get<magic::magic_system>();
    if (!magic)
    {
        LOG_WARN(proto_bridge, "Magic system not available for spell cast");
        return handle_result::not_handled;
    }

    auto* player_sys = subsystems().get<player::player_system>();
    if (!player_sys)
    {
        return handle_result::not_handled;
    }

    auto* caster_player = player_sys->get_player(ctx.player);
    if (!caster_player)
    {
        return handle_result::not_handled;
    }

    // Use the real ECS entity handle — player_id values are NOT entity ids
    auto caster = caster_player->ecs_entity;

    // Build cast target
    magic::cast_target cast_target{.target = entity::entity{target.value}, .target_pos = world::position{0, 0}};

    // Attempt instant cast
    auto result = magic->instant_cast(caster, spell, cast_target);

    if (result.is_ok())
    {
        auto& effect = result.value();
        int32_t damage = effect.damage_dealt;
        bool resisted = !effect.success && damage == 0;

        send_spell_effect(ctx, spell, target, damage, resisted);
        return handle_result::handled;
    }

    // Cast failed
    LOG_DEBUG(proto_bridge, "Spell cast failed: {}", result.error());
    return handle_result::handled;
}

// ========== Skill Handler ==========

auto handle_use_skill(const handler_context& ctx) -> handle_result
{
    protocol::message_reader reader{ctx.raw_data};
    reader.skip(4); // message_id
    reader.skip(2); // subtype

    if (reader.remaining() < 2)
    {
        LOG_WARN(proto_bridge, "Skill: insufficient data (conn={})", ctx.connection.value);
        return handle_result::not_handled;
    }

    auto skill_id_val = reader.read_u16();

    entity_id target{};
    if (reader.remaining() >= 4)
    {
        target = entity_id{reader.read_u32()};
    }

    LOG_DEBUG(proto_bridge, "Skill: player={} skill={} target={}", ctx.player.value, skill_id_val, target.value);

    return handle_skill_use(ctx, skill_id{skill_id_val}, target);
}

auto handle_skill_use(const handler_context& ctx, skill_id skill, [[maybe_unused]] entity_id target) -> handle_result
{
    auto* skill_sys = subsystems().get<skill::skill_system>();
    if (!skill_sys)
    {
        LOG_WARN(proto_bridge, "Skill system not available for skill use");
        return handle_result::not_handled;
    }

    // Convert skill_id to skill_type
    auto skill_type = static_cast<skill::skill_type>(skill.value);

    // Check if this is a valid skill type
    if (skill.value >= static_cast<uint16_t>(skill::skill_type::skill_count))
    {
        LOG_WARN(proto_bridge, "Invalid skill ID: {}", skill.value);
        send_skill_result(ctx, skill, false);
        return handle_result::handled;
    }

    // Attempt to train/use the skill
    auto result = skill_sys->train_skill(ctx.player, skill_type);

    switch (result)
    {
    case skill::skill_use_result::success:
        LOG_DEBUG(proto_bridge, "Skill use success: player={} skill={}", ctx.player.value, skill.value);
        send_skill_result(ctx, skill, true);
        break;

    case skill::skill_use_result::cooldown:
        LOG_DEBUG(proto_bridge, "Skill on cooldown: player={} skill={}", ctx.player.value, skill.value);
        send_skill_result(ctx, skill, false);
        break;

    case skill::skill_use_result::insufficient_skill:
        LOG_DEBUG(proto_bridge, "Insufficient skill level: player={} skill={}", ctx.player.value, skill.value);
        send_skill_result(ctx, skill, false);
        break;

    default:
        send_skill_result(ctx, skill, false);
        break;
    }

    return handle_result::handled;
}

// ========== Combat Event Responses ==========

void send_damage_event(const handler_context& ctx, entity_id target, int32_t damage, bool critical, bool killed)
{
    send_response(ctx,
                  protocol::message_id::notify,
                  [&](protocol::message_writer& writer)
                  {
                      if (killed)
                      {
                          writer.write_u16(static_cast<uint16_t>(protocol::notify_type::killed));
                          writer.write_u32(target.value);
                          writer.write_i32(damage);
                      }
                      else
                      {
                          writer.write_u16(static_cast<uint16_t>(protocol::notify_type::damage_move));
                          writer.write_u32(target.value);
                          writer.write_i32(damage);
                          writer.write_bool(critical);
                      }
                  });
}

void send_spell_effect(const handler_context& ctx, spell_id spell, entity_id target, int32_t damage, bool resisted)
{
    send_response(ctx,
                  protocol::message_id::notify,
                  [&](protocol::message_writer& writer)
                  {
                      if (resisted)
                      {
                          writer.write_u16(static_cast<uint16_t>(protocol::notify_type::event_msg_string));
                          writer.write_string_u16("Your spell was resisted!");
                      }
                      else
                      {
                          writer.write_u16(static_cast<uint16_t>(protocol::notify_type::magic_effect_on));
                          writer.write_u16(spell.value);
                          writer.write_u32(target.value);
                          writer.write_i32(damage);
                      }
                  });
}

void send_skill_result(const handler_context& ctx, skill_id skill, bool success)
{
    send_response(ctx,
                  protocol::message_id::notify,
                  [&](protocol::message_writer& writer)
                  {
                      if (success)
                      {
                          writer.write_u16(static_cast<uint16_t>(protocol::notify_type::skill));
                          writer.write_u16(skill.value);
                      }
                      else
                      {
                          writer.write_u16(static_cast<uint16_t>(protocol::notify_type::skill_using_end));
                          writer.write_u16(skill.value);
                      }
                  });
}

} // namespace hb::bridge::wave4
