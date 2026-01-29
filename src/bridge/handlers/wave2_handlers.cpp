// wave2_handlers.cpp
// Wave 2 Migration: Admin command message handlers

#include "bridge/handlers/wave2_handlers.h"
#include "bridge/handler_registry.h"
#include "bridge/message_router.h"
#include "core/subsystem.h"
#include "core/logger.h"
#include "admin/admin_system.h"
#include "admin/command.h"
#include "protocol/message_reader.h"
#include "protocol/message_writer.h"
#include "network/network_subsystem.h"

#include <spdlog/fmt/fmt.h>
#include <vector>

namespace hb::bridge::wave2 {

namespace {
    // Track which handlers we've registered for cleanup
    std::vector<protocol::message_id> registered_handlers;

    constexpr auto wave2_subsystem = "wave2_admin";

    // Check if a chat message is an admin command
    auto is_admin_command(std::string_view message, char prefix = '/') -> bool {
        return !message.empty() && message.front() == prefix;
    }

    // Send a notification message to a connection
    void send_notify_message(const handler_context& ctx, std::string_view message) {
        send_response(ctx, protocol::message_id::notify, [message](protocol::message_writer& writer) {
            writer.write_u16(static_cast<uint16_t>(protocol::notify_type::event_msg_string));
            writer.write_string_u16(message);
        });
    }

    // Send admin-specific info notification
    void send_admin_info(const handler_context& ctx, std::string_view message) {
        send_response(ctx, protocol::message_id::notify, [message](protocol::message_writer& writer) {
            writer.write_u16(static_cast<uint16_t>(protocol::notify_type::admin_info));
            writer.write_string_u16(message);
        });
    }
}

// ========== Handler Registration ==========

auto register_wave2_handlers() -> size_t {
    LOG_INFO(proto_bridge, "Registering Wave 2 (admin command) handlers...");

    // Register handler for chat messages that may contain admin commands
    handlers(wave2_subsystem)
        .on(protocol::message_id::command_chat_msg,
            make_simple_handler([](const handler_context& ctx, protocol::message_reader& reader) {
                // Chat message format:
                // - message_id (4 bytes) - already parsed
                // - message type (2 bytes)
                // - sender name (varies)
                // - message content (varies)

                if (reader.remaining() < 2) {
                    return handle_result::not_handled;
                }

                // Skip message subtype
                auto subtype = reader.read_u16();
                (void)subtype;  // May use later for different chat types

                // Read sender name length and name
                if (reader.remaining() < 1) {
                    return handle_result::not_handled;
                }

                auto name_len = reader.read_u8();
                if (reader.remaining() < name_len) {
                    return handle_result::not_handled;
                }

                std::string sender_name;
                for (uint8_t i = 0; i < name_len; ++i) {
                    sender_name += static_cast<char>(reader.read_u8());
                }

                // Read message content
                if (reader.remaining() < 2) {
                    return handle_result::not_handled;
                }

                auto msg_len = reader.read_u16();
                if (reader.remaining() < msg_len) {
                    return handle_result::not_handled;
                }

                std::string message;
                message.reserve(msg_len);
                for (uint16_t i = 0; i < msg_len; ++i) {
                    message += static_cast<char>(reader.read_u8());
                }

                // Check if this is an admin command
                if (!is_admin_command(message)) {
                    // Not a command, let legacy handle regular chat
                    return handle_result::not_handled;
                }

                // Route to admin system
                return handle_chat_command(ctx);
            }));

    registered_handlers.push_back(protocol::message_id::command_chat_msg);

    LOG_INFO(proto_bridge, "Wave 2 handlers registered: {} handlers", registered_handlers.size());

    return registered_handlers.size();
}

void unregister_wave2_handlers() {
    LOG_INFO(proto_bridge, "Unregistering Wave 2 handlers...");

    for (auto msg_id : registered_handlers) {
        router().unregister_handler(msg_id);
    }
    registered_handlers.clear();
}

// ========== Chat Command Handler ==========

auto handle_chat_command(const handler_context& ctx) -> handle_result {
    LOG_DEBUG(proto_bridge, "Processing admin command (conn={}, player={})",
              ctx.connection.value, ctx.player.value);

    // Get admin system
    auto* admin = subsystems().get<admin::admin_system>();
    if (!admin) {
        LOG_WARN(proto_bridge, "Admin system not available for command processing");
        return handle_result::not_handled;
    }

    // Player must be valid to execute commands
    if (!ctx.player.is_valid()) {
        LOG_DEBUG(proto_bridge, "Command rejected: no valid player for connection {}",
                  ctx.connection.value);
        return handle_result::not_handled;
    }

    // Re-parse the message to get the command string
    protocol::message_reader reader{ctx.raw_data};
    reader.skip(4);  // message_id
    reader.skip(2);  // subtype

    auto name_len = reader.read_u8();
    reader.skip(name_len);  // sender name

    auto msg_len = reader.read_u16();
    std::string command_str;
    command_str.reserve(msg_len);
    for (uint16_t i = 0; i < msg_len; ++i) {
        command_str += static_cast<char>(reader.read_u8());
    }

    // Execute the command through admin system
    auto result = admin->execute(ctx.player, command_str);

    // Send result back to player
    if (!result.message.empty()) {
        if (result.success) {
            send_admin_info(ctx, result.message);
        } else {
            send_notify_message(ctx, fmt::format("Command failed: {}", result.message));
        }
    }

    return handle_result::handled;
}

// ========== Admin Action Handlers ==========

auto admin_kill(const handler_context& ctx, player_id target) -> handle_result {
    LOG_INFO(proto_bridge, "Admin kill: executor={} target={}",
             ctx.player.value, target.value);

    // This would integrate with combat_system to kill the target
    // For now, we just log and report success
    send_admin_info(ctx, fmt::format("Kill command executed on player {}", target.value));

    return handle_result::handled;
}

auto admin_summon(const handler_context& ctx, player_id target) -> handle_result {
    LOG_INFO(proto_bridge, "Admin summon: executor={} target={}",
             ctx.player.value, target.value);

    // This would integrate with player_system to teleport target to executor
    send_admin_info(ctx, fmt::format("Summon command executed for player {}", target.value));

    return handle_result::handled;
}

auto admin_teleport(const handler_context& ctx, int16_t x, int16_t y, map_id map) -> handle_result {
    LOG_INFO(proto_bridge, "Admin teleport: executor={} to ({},{}) on map {}",
             ctx.player.value, x, y, map.value);

    // This would integrate with world_subsystem and player_system
    send_admin_info(ctx, fmt::format("Teleport to ({},{}) on map {}", x, y, map.value));

    return handle_result::handled;
}

auto admin_invisible(const handler_context& ctx, bool invisible) -> handle_result {
    LOG_INFO(proto_bridge, "Admin invisible: executor={} invisible={}",
             ctx.player.value, invisible);

    auto* admin = subsystems().get<admin::admin_system>();
    if (admin) {
        admin->set_invisible(ctx.player, invisible);
        send_admin_info(ctx, invisible ? "You are now invisible" : "You are now visible");
    }

    return handle_result::handled;
}

auto admin_ban(const handler_context& ctx, player_id target, std::string_view reason,
               int64_t duration_seconds) -> handle_result {
    LOG_INFO(proto_bridge, "Admin ban: executor={} target={} reason='{}' duration={}",
             ctx.player.value, target.value, reason, duration_seconds);

    auto* admin = subsystems().get<admin::admin_system>();
    if (admin) {
        auto result = admin->ban_player(target, ctx.player, reason, duration_seconds);
        if (result.success) {
            send_admin_info(ctx, result.message);
        } else {
            send_notify_message(ctx, result.message);
        }
    }

    return handle_result::handled;
}

}  // namespace hb::bridge::wave2
