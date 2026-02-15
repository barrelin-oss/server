// wave5_handlers.cpp
// Wave 5 Migration: Social system message handlers

#include "bridge/handlers/wave5_handlers.h"
#include "bridge/handler_registry.h"
#include "bridge/message_router.h"
#include "core/subsystem.h"
#include "core/logger.h"
#include "social/social_system.h"
#include "social/party.h"
#include "inventory/inventory_system.h"
#include "player/player_system.h"
#include "protocol/message_reader.h"
#include "protocol/message_writer.h"
#include "network/network_subsystem.h"

#include <spdlog/fmt/fmt.h>
#include <vector>

namespace hb::bridge::wave5
{

namespace
{
// Track which handlers we've registered for cleanup
std::vector<protocol::message_id> registered_handlers;

constexpr auto wave5_subsystem = "wave5_social";
} // namespace

// ========== Handler Registration ==========

auto register_wave5_handlers() -> size_t
{
    LOG_INFO(proto_bridge, "Registering Wave 5 (social system) handlers...");

    // Register party operation handler
    handlers(wave5_subsystem)
        .on_player(protocol::message_id::party_operation,
                   make_simple_handler(
                       [](const handler_context& ctx, protocol::message_reader& reader)
                       {
                           if (reader.remaining() < 1)
                           {
                               return handle_result::not_handled;
                           }

                           auto operation = reader.read_u8();

                           // Party operations:
                           // 0 = create party
                           // 1 = join party request
                           // 2 = accept invite
                           // 3 = decline invite
                           // 4 = leave party
                           // 5 = kick member
                           // 6 = change leader

                           switch (operation)
                           {
                           case 0:
                               return handle_create_party(ctx);

                           case 1:
                               return handle_join_party(ctx);

                           case 2:
                               return handle_party_response(ctx, true);

                           case 3:
                               return handle_party_response(ctx, false);

                           default:
                               // Other operations - let legacy handle for now
                               return handle_result::not_handled;
                           }
                       }));

    registered_handlers.push_back(protocol::message_id::party_operation);

    // Register command_common handler for guild and exchange operations
    handlers(wave5_subsystem)
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
                           // Guild operations
                           case protocol::common_type::join_guild_approve:
                               return handle_guild_join_response(ctx, true);

                           case protocol::common_type::join_guild_reject:
                               return handle_guild_join_response(ctx, false);

                           case protocol::common_type::dismiss_guild_approve:
                               return handle_guild_dismiss(ctx);

                           // Exchange operations
                           case protocol::common_type::exchange_item_to_char:
                               return handle_exchange_request(ctx);

                           case protocol::common_type::set_exchange_item:
                               return handle_set_exchange_item(ctx);

                           case protocol::common_type::confirm_exchange_item:
                               return handle_confirm_exchange(ctx);

                           case protocol::common_type::cancel_exchange_item:
                               return handle_cancel_exchange(ctx);

                           default:
                               // Not a social command
                               return handle_result::not_handled;
                           }
                       }));

    registered_handlers.push_back(protocol::message_id::command_common);

    // Register guild creation request
    handlers(wave5_subsystem)
        .on_player(protocol::message_id::request_create_new_guild,
                   make_simple_handler([](const handler_context& ctx, [[maybe_unused]] protocol::message_reader& reader)
                                       { return handle_create_guild(ctx); }));

    registered_handlers.push_back(protocol::message_id::request_create_new_guild);

    LOG_INFO(proto_bridge, "Wave 5 handlers registered: {} handlers", registered_handlers.size());

    return registered_handlers.size();
}

void unregister_wave5_handlers()
{
    LOG_INFO(proto_bridge, "Unregistering Wave 5 handlers...");

    for (auto msg_id : registered_handlers)
    {
        router().unregister_handler(msg_id);
    }
    registered_handlers.clear();
}

// ========== Party Handlers ==========

auto handle_create_party(const handler_context& ctx) -> handle_result
{
    LOG_DEBUG(proto_bridge, "Create party: player={}", ctx.player.value);

    auto* social = subsystems().get<social::social_system>();
    if (!social)
    {
        LOG_WARN(proto_bridge, "Social system not available for party creation");
        return handle_result::not_handled;
    }

    auto result = social->create_party(ctx.player);

    if (result.is_ok())
    {
        send_party_result(ctx, true, "Party created successfully.");
        return handle_result::handled;
    }

    auto error = result.error();
    std::string msg;
    switch (error)
    {
    case social::party_result::already_in_party:
        msg = "You are already in a party.";
        break;
    default:
        msg = "Failed to create party.";
        break;
    }
    send_party_result(ctx, false, msg);
    return handle_result::handled;
}

auto handle_join_party(const handler_context& ctx) -> handle_result
{
    protocol::message_reader reader{ctx.raw_data};
    reader.skip(4); // message_id
    reader.skip(1); // operation

    if (reader.remaining() < 4)
    {
        return handle_result::not_handled;
    }

    auto target_player = player_id{reader.read_u32()};

    LOG_DEBUG(proto_bridge, "Join party: player={} target={}", ctx.player.value, target_player.value);

    auto* social = subsystems().get<social::social_system>();
    if (!social)
    {
        return handle_result::not_handled;
    }

    // This is a request to join someone's party (sends invite)
    auto target_party = social->get_player_party(target_player);
    if (!target_party.is_valid())
    {
        send_party_result(ctx, false, "Target player is not in a party.");
        return handle_result::handled;
    }

    auto result = social->invite_to_party(ctx.player, target_player);

    if (result == social::party_result::success)
    {
        send_party_result(ctx, true, "Party invitation sent.");
    }
    else
    {
        send_party_result(ctx, false, "Failed to send party invitation.");
    }

    return handle_result::handled;
}

auto handle_party_response(const handler_context& ctx, bool accept) -> handle_result
{
    protocol::message_reader reader{ctx.raw_data};
    reader.skip(4); // message_id
    reader.skip(1); // operation

    if (reader.remaining() < 4)
    {
        return handle_result::not_handled;
    }

    auto party_id_val = social::party_id{reader.read_u32()};

    LOG_DEBUG(proto_bridge,
              "Party response: player={} party={} accept={}",
              ctx.player.value,
              party_id_val.value,
              accept ? "true" : "false");

    auto* social = subsystems().get<social::social_system>();
    if (!social)
    {
        return handle_result::not_handled;
    }

    social::party_result result;
    if (accept)
    {
        result = social->accept_party_invite(ctx.player, party_id_val);
    }
    else
    {
        result = social->decline_party_invite(ctx.player, party_id_val);
    }

    if (result == social::party_result::success)
    {
        if (accept)
        {
            send_party_result(ctx, true, "You have joined the party.");
        }
    }
    else
    {
        send_party_result(ctx, false, "Failed to process party invitation.");
    }

    return handle_result::handled;
}

// ========== Guild Handlers ==========

auto handle_create_guild(const handler_context& ctx) -> handle_result
{
    protocol::message_reader reader{ctx.raw_data};
    reader.skip(4); // message_id

    if (reader.remaining() < 2)
    {
        return handle_result::not_handled;
    }

    auto name_len = reader.read_u8();
    if (reader.remaining() < name_len)
    {
        return handle_result::not_handled;
    }

    std::string guild_name;
    guild_name.reserve(name_len);
    for (uint8_t i = 0; i < name_len; ++i)
    {
        guild_name += static_cast<char>(reader.read_u8());
    }

    std::string guild_tag = guild_name.substr(0, 3); // Default tag from first 3 chars

    LOG_DEBUG(proto_bridge, "Create guild: player={} name='{}' tag='{}'", ctx.player.value, guild_name, guild_tag);

    auto* social = subsystems().get<social::social_system>();
    if (!social)
    {
        return handle_result::not_handled;
    }

    auto result = social->create_guild(ctx.player, guild_name, guild_tag);

    if (result.is_ok())
    {
        send_guild_result(ctx, true, fmt::format("Guild '{}' created successfully.", guild_name));
        return handle_result::handled;
    }

    auto error = result.error();
    std::string msg;
    switch (error)
    {
    case social::guild_result::already_in_guild:
        msg = "You are already in a guild.";
        break;
    case social::guild_result::name_taken:
        msg = "That guild name is already taken.";
        break;
    case social::guild_result::invalid_name:
        msg = "Invalid guild name.";
        break;
    case social::guild_result::insufficient_gold:
        msg = "Not enough gold to create a guild.";
        break;
    default:
        msg = "Failed to create guild.";
        break;
    }
    send_guild_result(ctx, false, msg);
    return handle_result::handled;
}

auto handle_guild_join_response(const handler_context& ctx, bool approve) -> handle_result
{
    protocol::message_reader reader{ctx.raw_data};
    reader.skip(4); // message_id
    reader.skip(2); // subtype

    if (reader.remaining() < 4)
    {
        return handle_result::not_handled;
    }

    auto target_player = player_id{reader.read_u32()};

    LOG_DEBUG(proto_bridge,
              "Guild join response: approver={} target={} approve={}",
              ctx.player.value,
              target_player.value,
              approve ? "true" : "false");

    auto* social = subsystems().get<social::social_system>();
    if (!social)
    {
        return handle_result::not_handled;
    }

    // Get the approver's guild
    auto guild_id = social->get_player_guild(ctx.player);
    if (!guild_id.is_valid())
    {
        send_guild_result(ctx, false, "You are not in a guild.");
        return handle_result::handled;
    }

    if (approve)
    {
        // Approving: target player joins the guild
        auto result = social->join_guild(target_player, guild_id);

        if (result == social::guild_result::success)
        {
            send_guild_result(ctx, true, "Player has joined the guild.");
        }
        else
        {
            std::string msg;
            switch (result)
            {
            case social::guild_result::guild_full:
                msg = "The guild is full.";
                break;
            case social::guild_result::already_in_guild:
                msg = "Player is already in a guild.";
                break;
            default:
                msg = "Failed to add player to guild.";
                break;
            }
            send_guild_result(ctx, false, msg);
        }
    }
    else
    {
        // Rejecting: just notify the player
        send_guild_result(ctx, true, "Guild invitation declined.");
    }

    return handle_result::handled;
}

auto handle_guild_dismiss(const handler_context& ctx) -> handle_result
{
    protocol::message_reader reader{ctx.raw_data};
    reader.skip(4); // message_id
    reader.skip(2); // subtype

    if (reader.remaining() < 4)
    {
        return handle_result::not_handled;
    }

    auto target_player = player_id{reader.read_u32()};

    LOG_DEBUG(proto_bridge, "Guild dismiss: kicker={} target={}", ctx.player.value, target_player.value);

    auto* social = subsystems().get<social::social_system>();
    if (!social)
    {
        return handle_result::not_handled;
    }

    auto result = social->kick_from_guild(ctx.player, target_player);

    if (result == social::guild_result::success)
    {
        send_guild_result(ctx, true, "Member has been dismissed from the guild.");
    }
    else
    {
        std::string msg;
        switch (result)
        {
        case social::guild_result::insufficient_permissions:
            msg = "You don't have permission to dismiss members.";
            break;
        case social::guild_result::cannot_kick_self:
            msg = "You cannot dismiss yourself.";
            break;
        case social::guild_result::cannot_kick_higher_rank:
            msg = "You cannot dismiss a higher-ranked member.";
            break;
        default:
            msg = "Failed to dismiss member.";
            break;
        }
        send_guild_result(ctx, false, msg);
    }

    return handle_result::handled;
}

// ========== Exchange/Trade Handlers ==========

auto handle_exchange_request(const handler_context& ctx) -> handle_result
{
    protocol::message_reader reader{ctx.raw_data};
    reader.skip(4); // message_id
    reader.skip(2); // subtype

    if (reader.remaining() < 4)
    {
        return handle_result::not_handled;
    }

    auto target_player = player_id{reader.read_u32()};

    LOG_DEBUG(proto_bridge, "Exchange request: player={} target={}", ctx.player.value, target_player.value);

    auto* inventory = subsystems().get<inventory::inventory_system>();
    if (!inventory)
    {
        return handle_result::not_handled;
    }

    // Start trade between players
    auto entity1 = entity_id{ctx.player.value};
    auto entity2 = entity_id{target_player.value};
    inventory->start_trade(entity1, entity2);

    // Notify both players
    send_exchange_update(ctx);

    return handle_result::handled;
}

auto handle_set_exchange_item(const handler_context& ctx) -> handle_result
{
    protocol::message_reader reader{ctx.raw_data};
    reader.skip(4); // message_id
    reader.skip(2); // subtype

    if (reader.remaining() < 4)
    {
        return handle_result::not_handled;
    }

    auto item_id_val = item_id{reader.read_u32()};

    LOG_DEBUG(proto_bridge, "Set exchange item: player={} item={}", ctx.player.value, item_id_val.value);

    auto* inventory = subsystems().get<inventory::inventory_system>();
    if (!inventory)
    {
        return handle_result::not_handled;
    }

    auto entity = entity_id{ctx.player.value};
    auto result = inventory->add_to_trade(entity, item_id_val);

    if (result == inventory::inventory_result::success)
    {
        send_exchange_update(ctx);
    }

    return handle_result::handled;
}

auto handle_confirm_exchange(const handler_context& ctx) -> handle_result
{
    LOG_DEBUG(proto_bridge, "Confirm exchange: player={}", ctx.player.value);

    auto* inventory = subsystems().get<inventory::inventory_system>();
    if (!inventory)
    {
        return handle_result::not_handled;
    }

    auto entity = entity_id{ctx.player.value};
    inventory->confirm_trade(entity);

    send_exchange_update(ctx);
    return handle_result::handled;
}

auto handle_cancel_exchange(const handler_context& ctx) -> handle_result
{
    LOG_DEBUG(proto_bridge, "Cancel exchange: player={}", ctx.player.value);

    auto* inventory = subsystems().get<inventory::inventory_system>();
    if (!inventory)
    {
        return handle_result::not_handled;
    }

    auto entity = entity_id{ctx.player.value};
    inventory->cancel_trade(entity);

    // Notify with cancel
    send_response(ctx,
                  protocol::message_id::notify,
                  [](protocol::message_writer& writer)
                  { writer.write_u16(static_cast<uint16_t>(protocol::notify_type::cancel_exchange_item)); });

    return handle_result::handled;
}

// ========== Response Helpers ==========

void send_party_result(const handler_context& ctx, bool success, std::string_view message)
{
    send_response(ctx,
                  protocol::message_id::notify,
                  [&](protocol::message_writer& writer)
                  {
                      writer.write_u16(static_cast<uint16_t>(protocol::notify_type::response_create_new_party));
                      writer.write_bool(success);
                      writer.write_string_u16(message);
                  });
}

void send_guild_result(const handler_context& ctx, bool success, std::string_view message)
{
    // Use the guild creation response message
    if (success)
    {
        send_response(ctx,
                      protocol::message_id::response_create_new_guild,
                      [&](protocol::message_writer& writer)
                      {
                          writer.write_u16(static_cast<uint16_t>(protocol::msg_type::confirm));
                          writer.write_string_u16(message);
                      });
    }
    else
    {
        send_response(ctx,
                      protocol::message_id::response_create_new_guild,
                      [&](protocol::message_writer& writer)
                      {
                          writer.write_u16(static_cast<uint16_t>(protocol::msg_type::reject));
                          writer.write_string_u16(message);
                      });
    }
}

void send_exchange_update(const handler_context& ctx)
{
    send_response(ctx,
                  protocol::message_id::notify,
                  [](protocol::message_writer& writer)
                  { writer.write_u16(static_cast<uint16_t>(protocol::notify_type::set_exchange_item)); });
}

} // namespace hb::bridge::wave5
