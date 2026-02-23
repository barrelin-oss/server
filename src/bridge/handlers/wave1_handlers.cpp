// wave1_handlers.cpp
// Wave 1 Migration: Read-only / Stateless message handlers

#include "bridge/handlers/wave1_handlers.h"
#include "bridge/handler_registry.h"
#include "bridge/message_router.h"
#include "core/subsystem.h"
#include "core/logger.h"
#include "registry/item_registry.h"
#include "registry/npc_registry.h"
#include "registry/magic_registry.h"
#include "admin/admin_system.h"
#include "admin/command.h"
#include "protocol/message_reader.h"
#include "protocol/message_writer.h"
#include "network/network_subsystem.h"

#include <spdlog/fmt/fmt.h>
#include <vector>

namespace hb::bridge::wave1
{

namespace
{
// Track which handlers we've registered for cleanup
std::vector<protocol::message_id> registered_handlers;

// Message IDs for Wave 1 handlers
// Note: These handlers intercept command_common messages based on subtype
// For now we register against the main message_id and check subtype in handler

constexpr auto wave1_subsystem = "wave1_readonly";
} // namespace

// ========== Handler Registration ==========

auto register_wave1_handlers() -> size_t
{
    LOG_INFO(proto_bridge, "Registering Wave 1 (read-only) handlers...");

    // Use the handler builder for clean registration
    handlers(wave1_subsystem)
        // Help request uses command_common with request_help subtype
        .on(protocol::message_id::command_common,
            make_simple_handler(
                [](const handler_context& ctx, protocol::message_reader& reader)
                {
                    // Read the subtype (2 bytes after message ID)
                    if (reader.remaining() < 2)
                    {
                        return handle_result::not_handled;
                    }

                    auto subtype = reader.read_u16();
                    auto common_type = static_cast<protocol::common_type>(subtype);

                    // Only handle request_help, let others fall through
                    if (common_type == protocol::common_type::request_help)
                    {
                        return handle_help_request(ctx);
                    }

                    // Not a message we handle - let legacy code process it
                    return handle_result::not_handled;
                }));

    registered_handlers.push_back(protocol::message_id::command_common);

    // Mark as migrated for tracking
    mark_fully_migrated(protocol::message_id::command_common);

    LOG_INFO(proto_bridge, "Wave 1 handlers registered: {} handlers", registered_handlers.size());

    return registered_handlers.size();
}

void unregister_wave1_handlers()
{
    LOG_INFO(proto_bridge, "Unregistering Wave 1 handlers...");

    for (auto msg_id : registered_handlers)
    {
        router().unregister_handler(msg_id);
    }
    registered_handlers.clear();
}

// ========== Help Request Handler ==========

auto handle_help_request(const handler_context& ctx) -> handle_result
{
    LOG_DEBUG(proto_bridge, "Handling help request (conn={})", ctx.connection.value);

    // Get admin system for help text generation
    auto* admin = subsystems().get<admin::admin_system>();
    if (!admin)
    {
        LOG_WARN(proto_bridge, "Admin system not available for help request");
        send_help_response(ctx, "Help system unavailable.");
        return handle_result::handled;
    }

    // Determine the player's admin level for appropriate help
    auto level = admin::admin_level::player;
    if (ctx.player.is_valid())
    {
        level = admin->get_admin_level(ctx.player);
    }

    // Generate help text
    std::string help_text = admin->get_help_all(level);
    if (help_text.empty())
    {
        help_text = "No commands available. Type /help <command> for specific help.";
    }

    send_help_response(ctx, help_text);
    return handle_result::handled;
}

// ========== Item Lookup Handler ==========

auto handle_item_lookup(const handler_context& ctx) -> handle_result
{
    protocol::message_reader reader{ctx.raw_data};
    reader.skip(4); // Skip message ID
    reader.skip(2); // Skip subtype

    if (reader.remaining() < 2)
    {
        LOG_WARN(proto_bridge, "Item lookup: insufficient data");
        return handle_result::error;
    }

    auto item_id_val = reader.read_u16();
    LOG_DEBUG(proto_bridge, "Item lookup for ID {} (conn={})", item_id_val, ctx.connection.value);

    send_item_info(ctx, item_id_val);
    return handle_result::handled;
}

// ========== NPC Lookup Handler ==========

auto handle_npc_lookup(const handler_context& ctx) -> handle_result
{
    protocol::message_reader reader{ctx.raw_data};
    reader.skip(4); // Skip message ID
    reader.skip(2); // Skip subtype

    if (reader.remaining() < 2)
    {
        LOG_WARN(proto_bridge, "NPC lookup: insufficient data");
        return handle_result::error;
    }

    auto npc_id_val = reader.read_u16();
    LOG_DEBUG(proto_bridge, "NPC lookup for ID {} (conn={})", npc_id_val, ctx.connection.value);

    send_npc_info(ctx, npc_id_val);
    return handle_result::handled;
}

// ========== Magic Lookup Handler ==========

auto handle_magic_lookup(const handler_context& ctx) -> handle_result
{
    protocol::message_reader reader{ctx.raw_data};
    reader.skip(4); // Skip message ID
    reader.skip(2); // Skip subtype

    if (reader.remaining() < 2)
    {
        LOG_WARN(proto_bridge, "Magic lookup: insufficient data");
        return handle_result::error;
    }

    auto spell_id_val = reader.read_u16();
    LOG_DEBUG(proto_bridge, "Magic lookup for ID {} (conn={})", spell_id_val, ctx.connection.value);

    send_magic_info(ctx, spell_id_val);
    return handle_result::handled;
}

// ========== Response Builders ==========

void send_help_response(const handler_context& ctx, std::string_view help_text)
{
    // Build notify message with help type
    send_response(ctx,
                  protocol::message_id::notify,
                  [help_text](protocol::message_writer& writer)
                  {
                      writer.write_u16(static_cast<uint16_t>(protocol::notify_type::help));
                      // Write help text with length prefix
                      writer.write_string_u16(help_text);
                  });
}

void send_item_info(const handler_context& ctx, uint16_t item_id_val)
{
    auto* registry = subsystems().get<item_registry>();
    if (!registry)
    {
        LOG_WARN(proto_bridge, "Item registry not available for lookup");
        return;
    }

    auto* item = registry->get(item_id{item_id_val});
    if (!item)
    {
        LOG_DEBUG(proto_bridge, "Item {} not found in registry", item_id_val);
        // Send empty/not found response
        send_response(ctx,
                      protocol::message_id::notify,
                      [](protocol::message_writer& writer)
                      {
                          writer.write_u16(static_cast<uint16_t>(protocol::notify_type::event_msg_string));
                          writer.write_string_u16("Item not found.");
                      });
        return;
    }

    // Build response with item info
    send_response(ctx,
                  protocol::message_id::notify,
                  [item](protocol::message_writer& writer)
                  {
                      writer.write_u16(static_cast<uint16_t>(protocol::notify_type::event_msg_string));

                      // Format item info string with raw template fields
                      std::string info = fmt::format("Item: {} (ID: {})\n"
                                                     "Type: {} | EquipPos: {} | Weight: {} | Price: {}\n"
                                                     "EffectType: {} | EV1-6: {} {} {} {} {} {}\n"
                                                     "Durability: {} | Level: {} | Special: {}",
                                                     item->name,
                                                     item->id.value,
                                                     static_cast<int>(item->type),
                                                     static_cast<int>(item->equip_pos),
                                                     item->weight,
                                                     item->price,
                                                     item->effect_type,
                                                     item->effect_value1,
                                                     item->effect_value2,
                                                     item->effect_value3,
                                                     item->effect_value4,
                                                     item->effect_value5,
                                                     item->effect_value6,
                                                     item->durability,
                                                     item->level_limit,
                                                     item->special_effect);
                      writer.write_string_u16(info);
                  });
}

void send_npc_info(const handler_context& ctx, uint16_t npc_id_val)
{
    auto* registry = subsystems().get<npc_registry>();
    if (!registry)
    {
        LOG_WARN(proto_bridge, "NPC registry not available for lookup");
        return;
    }

    auto* npc = registry->get(npc_id{npc_id_val});
    if (!npc)
    {
        LOG_DEBUG(proto_bridge, "NPC {} not found in registry", npc_id_val);
        send_response(ctx,
                      protocol::message_id::notify,
                      [](protocol::message_writer& writer)
                      {
                          writer.write_u16(static_cast<uint16_t>(protocol::notify_type::event_msg_string));
                          writer.write_string_u16("NPC not found.");
                      });
        return;
    }

    // Build response with NPC info
    send_response(ctx,
                  protocol::message_id::notify,
                  [npc](protocol::message_writer& writer)
                  {
                      writer.write_u16(static_cast<uint16_t>(protocol::notify_type::event_msg_string));

                      std::string info = fmt::format("NPC: {} (ID: {})\n"
                                                     "Level: {} | HP: {} | MP: {}\n"
                                                     "Attack: {}d{}{:+d} | Defense: {}\n"
                                                     "EXP: {} | Gold: {}-{}",
                                                     npc->name,
                                                     npc->id.value,
                                                     npc->level,
                                                     npc->hp,
                                                     npc->mp,
                                                     npc->attack_dice,
                                                     npc->attack_sides,
                                                     npc->attack_bonus,
                                                     npc->defense,
                                                     npc->exp_reward,
                                                     npc->gold_min,
                                                     npc->gold_max);
                      writer.write_string_u16(info);
                  });
}

void send_magic_info(const handler_context& ctx, uint16_t spell_id_val)
{
    auto* registry = subsystems().get<magic_registry>();
    if (!registry)
    {
        LOG_WARN(proto_bridge, "Magic registry not available for lookup");
        return;
    }

    auto* spell = registry->get(spell_id{spell_id_val});
    if (!spell)
    {
        LOG_DEBUG(proto_bridge, "Spell {} not found in registry", spell_id_val);
        send_response(ctx,
                      protocol::message_id::notify,
                      [](protocol::message_writer& writer)
                      {
                          writer.write_u16(static_cast<uint16_t>(protocol::notify_type::event_msg_string));
                          writer.write_string_u16("Spell not found.");
                      });
        return;
    }

    // Build response with spell info
    send_response(ctx,
                  protocol::message_id::notify,
                  [spell](protocol::message_writer& writer)
                  {
                      writer.write_u16(static_cast<uint16_t>(protocol::notify_type::event_msg_string));

                      std::string info = fmt::format("Spell: {} (ID: {})\n"
                                                     "Type: {} | Target: {}\n"
                                                     "Mana: {} | Cast: {}ms | Cooldown: {}ms\n"
                                                     "Damage: {} + INT*{} + MAG*{}\n"
                                                     "Requirements: INT {} | MAG Level {}",
                                                     spell->name,
                                                     spell->id.value,
                                                     static_cast<int>(spell->type),
                                                     static_cast<int>(spell->target),
                                                     spell->mana_cost,
                                                     spell->cast_time_ms,
                                                     spell->cooldown_ms,
                                                     spell->base_damage,
                                                     spell->int_scaling,
                                                     spell->mag_scaling,
                                                     spell->int_req,
                                                     spell->mag_level_req);
                      writer.write_string_u16(info);
                  });
}

} // namespace hb::bridge::wave1
