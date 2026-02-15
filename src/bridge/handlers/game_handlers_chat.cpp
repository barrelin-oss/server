// game_handlers_chat.cpp
// Chat and command handling methods extracted from game_handlers.cpp

// Include platform header first to define NOMINMAX before Windows headers
#include "platform/platform.h"

#include "bridge/handlers/game_handlers.h"
#include "bridge/handlers/broadcast_util.h"
#include "network/websocket_server.h"
#include "player/player_system.h"
#include "world/world_subsystem.h"
#include "social/social_system.h"
#include "admin/admin_system.h"
#include "admin/command.h"
#include "war/crusade/crusade_system.h"
#include "core/logger.h"
#include "perf/perf_stats.h"
#include "core/subsystem.h"

#include <chrono>
#include <iomanip>
#include <sstream>

namespace hb::bridge
{

// ========== Chat Handling ==========

namespace
{

// Parse chat channel from prefix or explicit channel name
auto parse_chat_channel(std::string_view content, const std::optional<std::string>& explicit_channel)
    -> std::pair<social::chat_channel, std::string>
{
    // If explicit channel provided, use that
    if (explicit_channel.has_value())
    {
        const auto& ch = *explicit_channel;
        if (ch == "local")
            return {social::chat_channel::local, std::string(content)};
        if (ch == "shout")
            return {social::chat_channel::shout, std::string(content)};
        if (ch == "guild")
            return {social::chat_channel::guild, std::string(content)};
        if (ch == "party")
            return {social::chat_channel::party, std::string(content)};
        if (ch == "whisper")
            return {social::chat_channel::whisper, std::string(content)};
        if (ch == "global")
            return {social::chat_channel::global, std::string(content)};
        if (ch == "trade")
            return {social::chat_channel::trade, std::string(content)};
        if (ch == "faction")
            return {social::chat_channel::faction, std::string(content)};
        // Default to local for unknown channels
        return {social::chat_channel::local, std::string(content)};
    }

    // Check for prefix-based channel
    if (!content.empty())
    {
        char prefix = content[0];
        std::string msg_content = std::string(content.substr(1));

        switch (prefix)
        {
        case '!': // Shout - server-wide
            return {social::chat_channel::shout, msg_content};
        case '@': // Guild
            return {social::chat_channel::guild, msg_content};
        case '$': // Party
            return {social::chat_channel::party, msg_content};
        case '#': // Say override (local) during whisper mode
            return {social::chat_channel::local, msg_content};
        case '%': // Trade channel
            return {social::chat_channel::trade, msg_content};
        case '~': // Faction chat
            return {social::chat_channel::faction, msg_content};
        case '^': // GM chat
            return {social::chat_channel::gm, msg_content};
        default:
            break;
        }
    }

    // Default to local chat
    return {social::chat_channel::local, std::string(content)};
}

auto channel_to_string(social::chat_channel channel) -> std::string
{
    switch (channel)
    {
    case social::chat_channel::local:
        return "local";
    case social::chat_channel::global:
        return "global";
    case social::chat_channel::guild:
        return "guild";
    case social::chat_channel::party:
        return "party";
    case social::chat_channel::whisper:
        return "whisper";
    case social::chat_channel::trade:
        return "trade";
    case social::chat_channel::shout:
        return "shout";
    case social::chat_channel::alliance:
        return "alliance";
    case social::chat_channel::faction:
        return "faction";
    case social::chat_channel::gm:
        return "gm";
    case social::chat_channel::system:
        return "system";
    default:
        return "unknown";
    }
}

auto format_timestamp(std::chrono::system_clock::time_point tp) -> std::string
{
    auto time_t = std::chrono::system_clock::to_time_t(tp);
    std::tm tm_buf{};
#ifdef _WIN32
    gmtime_s(&tm_buf, &time_t);
#else
    gmtime_r(&time_t, &tm_buf);
#endif
    std::ostringstream oss;
    oss << std::put_time(&tm_buf, "%Y-%m-%dT%H:%M:%SZ");
    return oss.str();
}

auto filter_result_to_string(social::filter_result result) -> std::string_view
{
    switch (result)
    {
    case social::filter_result::allowed:
        return "allowed";
    case social::filter_result::censored:
        return "censored";
    case social::filter_result::blocked:
        return "blocked";
    case social::filter_result::rate_limited:
        return "rate_limited";
    default:
        return "unknown";
    }
}

} // namespace

void game_handlers::handle_chat_message(connection_id conn_id, const network::json_message& msg)
{
    auto* conn = require_in_game(conn_id, msg.seq);
    if (!conn)
        return;

    if (!social_ || !players_)
    {
        send_error(conn_id, msg.seq, "internal_error", "Chat system unavailable");
        return;
    }

    // Parse request
    auto data_result = network::chat_message_request_data::from_json(msg.data);
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

    // Parse channel and extract message content
    auto [channel, content] = parse_chat_channel(data.content, data.channel);

    // Validate content is not empty after prefix removal
    if (content.empty())
    {
        conn->send(network::make_chat_message_response(msg.seq, false, "empty_message"));
        return;
    }

    // Intercept /commands from chat and route to command handler
    if (!content.empty() && content[0] == '/')
    {
        auto parsed = admin::command_parser::parse(content, '/');
        if (parsed.valid)
        {
            // Build a synthetic command_request message and route it
            network::json_message cmd_msg;
            cmd_msg.type = network::json_message_type::command_request;
            cmd_msg.seq = msg.seq;
            cmd_msg.data = nlohmann::json{{"command", parsed.command_name}, {"args", parsed.raw_args}};
            handle_command(conn_id, cmd_msg);
            return;
        }
    }

    // Handle based on channel type
    social::filter_result result;

    switch (channel)
    {
    case social::chat_channel::local:
        result = social_->send_local_chat(pid, content, player->current_map, player->pos.x, player->pos.y);
        break;

    case social::chat_channel::shout:
        // Shout is server-wide
        result = social_->send_global_chat(pid, content);
        break;

    case social::chat_channel::guild:
        result = social_->send_guild_chat(pid, content);
        break;

    case social::chat_channel::party:
        result = social_->send_party_chat(pid, content);
        break;

    case social::chat_channel::whisper:
    {
        // Need recipient for whisper
        if (!data.recipient_name.has_value() || data.recipient_name->empty())
        {
            conn->send(network::make_chat_message_response(msg.seq, false, "no_recipient"));
            return;
        }

        // Find recipient by name
        auto* recipient = players_->get_player_by_name(*data.recipient_name);
        if (!recipient)
        {
            conn->send(network::make_chat_message_response(msg.seq, false, "recipient_not_found"));
            return;
        }

        result = social_->send_whisper(pid, recipient->id, content);
        break;
    }

    case social::chat_channel::global:
        result = social_->send_global_chat(pid, content);
        break;

    case social::chat_channel::trade:
        // Trade channel is global for now
        result = social_->send_global_chat(pid, content);
        break;

    case social::chat_channel::gm:
        if (!player->is_gm())
        {
            conn->send(network::make_chat_message_response(msg.seq, false, "not_authorized"));
            return;
        }
        result = social_->send_global_chat(pid, content);
        break;

    default:
        result = social_->send_local_chat(pid, content, player->current_map, player->pos.x, player->pos.y);
        break;
    }

    // Send response to sender
    if (result == social::filter_result::allowed || result == social::filter_result::censored)
    {
        conn->send(network::make_chat_message_response(msg.seq, true));
    }
    else
    {
        conn->send(network::make_chat_message_response(msg.seq, false, filter_result_to_string(result)));
    }

    LOG_DEBUG(bridge, "Player {} sent {} chat: {}", pid.value, channel_to_string(channel), content.substr(0, 50));
}

namespace
{

auto guild_result_string(social::guild_result r) -> std::string
{
    switch (r)
    {
    case social::guild_result::success:
        return "success";
    case social::guild_result::guild_not_found:
        return "guild_not_found";
    case social::guild_result::player_not_member:
        return "not_member";
    case social::guild_result::insufficient_permissions:
        return "insufficient_permissions";
    case social::guild_result::guild_full:
        return "guild_full";
    case social::guild_result::already_in_guild:
        return "already_in_guild";
    case social::guild_result::name_taken:
        return "name_taken";
    case social::guild_result::invalid_name:
        return "invalid_name";
    case social::guild_result::cannot_kick_self:
        return "cannot_kick_self";
    case social::guild_result::cannot_kick_higher_rank:
        return "cannot_kick_higher_rank";
    case social::guild_result::cannot_promote_higher:
        return "cannot_promote_higher";
    case social::guild_result::insufficient_gold:
        return "insufficient_gold";
    default:
        return "unknown_error";
    }
}

} // namespace

void game_handlers::handle_command(connection_id conn_id, const network::json_message& msg)
{
    auto* conn = require_in_game(conn_id, msg.seq);
    if (!conn)
        return;

    if (!players_)
    {
        send_error(conn_id, msg.seq, "internal_error", "Player system unavailable");
        return;
    }

    // Parse request
    auto data_result = network::command_request_data::from_json(msg.data);
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

    LOG_DEBUG(bridge, "Player {} command: {} args={}", pid.value, data.command, data.args.size());

    // Simple built-in commands (available to all players)
    if (data.command == "online")
    {
        // Return online player count
        auto count = players_->active_player_count();
        conn->send(network::make_command_response(
            msg.seq, true, data.command, std::to_string(count) + " players online", nlohmann::json{{"count", count}}));
        return;
    }

    if (data.command == "time")
    {
        // Return server time
        auto now = std::chrono::system_clock::now();
        conn->send(network::make_command_response(
            msg.seq, true, data.command, format_timestamp(now), nlohmann::json{{"timestamp", format_timestamp(now)}}));
        return;
    }

    if (data.command == "pos" || data.command == "position")
    {
        // Return player position
        std::string map_name = "unknown";
        if (world_)
        {
            auto* current_map = world_->get_map(player->current_map);
            if (current_map)
            {
                map_name = std::string(current_map->name());
            }
        }
        conn->send(network::make_command_response(msg.seq,
                                                  true,
                                                  data.command,
                                                  "Position: " + map_name + " (" + std::to_string(player->pos.x) +
                                                      ", " + std::to_string(player->pos.y) + ")",
                                                  nlohmann::json{{"x", player->pos.x},
                                                                 {"y", player->pos.y},
                                                                 {"map", player->current_map.value},
                                                                 {"map_name", map_name}}));
        return;
    }

    // Guild commands (available to all players)
    if (data.command == "gcreate" && social_)
    {
        if (data.args.empty())
        {
            conn->send(network::make_command_response(msg.seq, false, data.command, "Usage: /gcreate <guild_name>"));
            return;
        }
        std::string guild_name = data.args[0];
        // Use first 3 chars of name as default tag
        std::string guild_tag = guild_name.substr(0, std::min(guild_name.size(), size_t{3}));
        if (data.args.size() > 1)
        {
            guild_tag = data.args[1];
        }

        auto result = social_->create_guild(pid, guild_name, guild_tag);
        if (result.is_err())
        {
            conn->send(network::make_command_response(
                msg.seq, false, data.command, "Failed to create guild: " + guild_result_string(result.error())));
            return;
        }
        auto gid = result.value();
        auto* g = social_->get_guild(gid);
        if (g)
        {
            player->guild_name = g->name;
            player->guild_tag = g->tag;
            player->guild_rank = static_cast<uint8_t>(social::guild_rank::guild_master);
        }
        conn->send(network::make_command_response(
            msg.seq, true, data.command, "Guild '" + guild_name + "' [" + guild_tag + "] created!"));
        return;
    }

    if (data.command == "gdisband" && social_)
    {
        auto gid = social_->get_player_guild(pid);
        if (!gid.is_valid())
        {
            conn->send(network::make_command_response(msg.seq, false, data.command, "You are not in a guild"));
            return;
        }
        auto* g = social_->get_guild(gid);
        if (!g)
        {
            conn->send(network::make_command_response(msg.seq, false, data.command, "Guild not found"));
            return;
        }
        std::string guild_name = g->name;

        // Collect member PIDs before disband
        std::vector<player_id> member_pids;
        for (const auto& m : g->members)
        {
            member_pids.push_back(m.player);
        }

        auto result = social_->disband_guild(pid, gid);
        if (result != social::guild_result::success)
        {
            conn->send(
                network::make_command_response(msg.seq, false, data.command, "Failed: " + guild_result_string(result)));
            return;
        }

        // Clear guild fields for all members
        for (auto mid : member_pids)
        {
            auto* member_plr = players_->get_player(mid);
            if (member_plr)
            {
                member_plr->guild_name.clear();
                member_plr->guild_tag.clear();
                member_plr->guild_rank = 0;
            }
        }

        // Notify all online members
        auto disbanded_msg = network::make_guild_update("guild_disbanded", guild_name);
        for (auto mid : member_pids)
        {
            auto* member_conn = ws_server_->get_connection_by_player(mid);
            if (member_conn)
            {
                member_conn->send(disbanded_msg);
            }
        }

        conn->send(network::make_command_response(
            msg.seq, true, data.command, "Guild '" + guild_name + "' has been disbanded"));
        return;
    }

    if (data.command == "ginvite" && social_)
    {
        if (data.args.empty())
        {
            conn->send(network::make_command_response(msg.seq, false, data.command, "Usage: /ginvite <player_name>"));
            return;
        }

        auto* target = players_->get_player_by_name(data.args[0]);
        if (!target)
        {
            conn->send(network::make_command_response(msg.seq, false, data.command, "Player not found"));
            return;
        }

        auto gid = social_->get_player_guild(pid);
        if (!gid.is_valid())
        {
            conn->send(network::make_command_response(msg.seq, false, data.command, "You are not in a guild"));
            return;
        }

        auto result = social_->invite_to_guild(pid, gid, target->id);
        if (result != social::guild_result::success)
        {
            conn->send(
                network::make_command_response(msg.seq, false, data.command, "Failed: " + guild_result_string(result)));
            return;
        }

        // Push invite notification to target
        auto* g = social_->get_guild(gid);
        auto* target_conn = ws_server_->get_connection_by_player(target->id);
        if (target_conn && g)
        {
            target_conn->send(network::make_guild_invite_received(g->name, g->tag, player->name));
        }

        conn->send(network::make_command_response(
            msg.seq, true, data.command, "Invited " + data.args[0] + " to your guild. They must /gaccept to join."));
        return;
    }

    if (data.command == "gkick" && social_)
    {
        if (data.args.empty())
        {
            conn->send(network::make_command_response(msg.seq, false, data.command, "Usage: /gkick <player_name>"));
            return;
        }

        auto* target = players_->get_player_by_name(data.args[0]);
        if (!target)
        {
            conn->send(network::make_command_response(msg.seq, false, data.command, "Player not found"));
            return;
        }

        auto gid = social_->get_player_guild(pid);
        if (!gid.is_valid())
        {
            conn->send(network::make_command_response(msg.seq, false, data.command, "You are not in a guild"));
            return;
        }

        auto* g = social_->get_guild(gid);
        std::string guild_name = g ? g->name : "";

        auto result = social_->kick_from_guild(pid, target->id);
        if (result != social::guild_result::success)
        {
            conn->send(
                network::make_command_response(msg.seq, false, data.command, "Failed: " + guild_result_string(result)));
            return;
        }

        // Clear target's guild fields
        target->guild_name.clear();
        target->guild_tag.clear();
        target->guild_rank = 0;

        // Notify kicked player
        auto* target_conn = ws_server_->get_connection_by_player(target->id);
        if (target_conn)
        {
            target_conn->send(network::make_guild_update("you_were_kicked", guild_name, player->name));
        }

        // Broadcast to guild
        broadcast_guild_update(gid, "member_kicked", guild_name, target->name);

        conn->send(
            network::make_command_response(msg.seq, true, data.command, "Kicked " + data.args[0] + " from the guild"));
        return;
    }

    if (data.command == "gaccept" && social_)
    {
        auto accept_result = social_->accept_guild_invite(pid);
        if (accept_result.is_err())
        {
            std::string err = (accept_result.error() == social::guild_result::guild_not_found)
                                  ? "No pending guild invite"
                                  : guild_result_string(accept_result.error());
            conn->send(network::make_command_response(msg.seq, false, data.command, err));
            return;
        }

        auto gid = accept_result.value();
        auto* g = social_->get_guild(gid);
        if (g)
        {
            player->guild_name = g->name;
            player->guild_tag = g->tag;
            player->guild_rank = static_cast<uint8_t>(social::guild_rank::recruit);

            broadcast_guild_update(gid, "member_joined", g->name, player->name);

            conn->send(network::make_command_response(
                msg.seq, true, data.command, "You joined guild '" + g->name + "' [" + g->tag + "]!"));
        }
        else
        {
            conn->send(network::make_command_response(msg.seq, true, data.command, "You joined the guild!"));
        }
        return;
    }

    if (data.command == "gdecline" && social_)
    {
        auto result = social_->decline_guild_invite(pid);
        if (result != social::guild_result::success)
        {
            conn->send(network::make_command_response(msg.seq, false, data.command, "No pending guild invite"));
            return;
        }
        conn->send(network::make_command_response(msg.seq, true, data.command, "Guild invite declined"));
        return;
    }

    if (data.command == "gquit" && social_)
    {
        auto gid = social_->get_player_guild(pid);
        if (!gid.is_valid())
        {
            conn->send(network::make_command_response(msg.seq, false, data.command, "You are not in a guild"));
            return;
        }

        // Check if guild master
        auto* g = social_->get_guild(gid);
        if (g && g->master == pid)
        {
            conn->send(network::make_command_response(
                msg.seq,
                false,
                data.command,
                "Guild masters cannot quit. Use /gdisband or transfer leadership first."));
            return;
        }

        std::string guild_name = g ? g->name : "";

        auto result = social_->leave_guild(pid);
        if (result != social::guild_result::success)
        {
            conn->send(
                network::make_command_response(msg.seq, false, data.command, "Failed: " + guild_result_string(result)));
            return;
        }

        player->guild_name.clear();
        player->guild_tag.clear();
        player->guild_rank = 0;

        broadcast_guild_update(gid, "member_left", guild_name, player->name);

        conn->send(network::make_command_response(msg.seq, true, data.command, "You left the guild"));
        return;
    }

    // Route to admin system for admin commands
    if (admin_)
    {
        // Build command string: /<command> [args...]
        std::string cmd_string = "/" + data.command;
        for (const auto& arg : data.args)
        {
            cmd_string += " " + arg;
        }

        auto result = admin_->execute(pid, cmd_string);

        // Send response
        conn->send(network::make_command_response(msg.seq, result.success, data.command, result.message));
        return;
    }

    // Unknown command (no admin system available)
    conn->send(network::make_command_response(msg.seq, false, data.command, "Unknown command: " + data.command));
}

void game_handlers::on_chat_message(const social::chat_message_event& event)
{
    if (!ws_server_ || !players_)
        return;

    const auto& msg = event.message;

    // Build broadcast data
    network::chat_message_broadcast_data broadcast;
    broadcast.channel = channel_to_string(msg.channel);
    broadcast.sender_id = msg.sender.value;
    broadcast.sender_name = msg.sender_name;
    broadcast.content = msg.content;
    broadcast.timestamp = format_timestamp(msg.timestamp);

    // Add flags
    if (social::has_flag(msg.flags, social::chat_flags::emote))
    {
        broadcast.flags.push_back("emote");
    }
    if (social::has_flag(msg.flags, social::chat_flags::censored))
    {
        broadcast.flags.push_back("censored");
    }
    if (social::has_flag(msg.flags, social::chat_flags::system))
    {
        broadcast.flags.push_back("system");
    }
    if (social::has_flag(msg.flags, social::chat_flags::gm))
    {
        broadcast.flags.push_back("gm");
    }

    // Route based on channel
    switch (msg.channel)
    {
    case social::chat_channel::local:
        // Send to nearby players
        send_chat_to_nearby(msg.sender, 15, broadcast); // 15 tile range
        break;

    case social::chat_channel::shout:
    case social::chat_channel::global:
    {
        // Send to all online players
        auto all_players = players_->get_all_players();
        for (auto pid : all_players)
        {
            send_chat_to_player(pid, broadcast);
        }
        break;
    }

    case social::chat_channel::guild:
    {
        // Send to guild members
        if (!social_)
            break;

        auto guild_id = social_->get_player_guild(msg.sender);
        if (!guild_id.is_valid())
            break;

        auto* guild = social_->get_guild(guild_id);
        if (!guild)
            break;

        for (const auto& member : guild->members)
        {
            send_chat_to_player(member.player, broadcast);
        }
        break;
    }

    case social::chat_channel::party:
    {
        // Send to party members
        if (!social_)
            break;

        auto party_id = social_->get_player_party(msg.sender);
        if (!party_id.is_valid())
            break;

        auto* party = social_->get_party(party_id);
        if (!party)
            break;

        for (const auto& member : party->members)
        {
            send_chat_to_player(member.player, broadcast);
        }
        break;
    }

    case social::chat_channel::whisper:
    {
        // Send to sender (confirmation) and recipient
        broadcast.recipient_name = msg.recipient_name;
        send_chat_to_player(msg.sender, broadcast);    // Echo to sender
        send_chat_to_player(msg.recipient, broadcast); // Send to recipient
        break;
    }

    case social::chat_channel::system:
    {
        // System messages to specific recipient
        send_chat_to_player(msg.recipient, broadcast);
        break;
    }

    case social::chat_channel::trade:
    case social::chat_channel::faction:
    case social::chat_channel::alliance:
    {
        // Broadcast to all for now
        auto all_players = players_->get_all_players();
        for (auto pid : all_players)
        {
            send_chat_to_player(pid, broadcast);
        }
        break;
    }

    case social::chat_channel::gm:
    {
        // Only deliver to players with GM permissions
        auto all_players = players_->get_all_players();
        for (auto pid : all_players)
        {
            auto* p = players_->get_player(pid);
            if (p && p->is_gm())
            {
                send_chat_to_player(pid, broadcast);
            }
        }
        break;
    }

    default:
        LOG_WARN(bridge, "Unknown chat channel: {}", static_cast<int>(msg.channel));
        break;
    }
}

void game_handlers::send_chat_to_player(player_id target, const network::chat_message_broadcast_data& data)
{
    if (!ws_server_ || !players_)
        return;

    auto* player = players_->get_player(target);
    if (!player || player->connection.value == 0)
        return;

    auto* conn = ws_server_->get_connection(player->connection);
    if (!conn || !conn->is_open())
        return;

    // Check if player has this channel enabled
    if (social_)
    {
        auto* settings = social_->get_chat_settings(target);
        if (settings)
        {
            // Convert string channel back to enum for settings check
            social::chat_channel ch = social::chat_channel::local;
            if (data.channel == "local")
                ch = social::chat_channel::local;
            else if (data.channel == "global")
                ch = social::chat_channel::global;
            else if (data.channel == "guild")
                ch = social::chat_channel::guild;
            else if (data.channel == "party")
                ch = social::chat_channel::party;
            else if (data.channel == "whisper")
                ch = social::chat_channel::whisper;
            else if (data.channel == "trade")
                ch = social::chat_channel::trade;
            else if (data.channel == "shout")
                ch = social::chat_channel::shout;
            else if (data.channel == "system")
                ch = social::chat_channel::system;

            if (!settings->is_channel_enabled(ch) && ch != social::chat_channel::system)
            {
                return; // Player has this channel disabled
            }

            // Check if sender is blocked
            if (settings->is_player_blocked(player_id{data.sender_id}))
            {
                return; // Player has blocked the sender
            }
        }
    }

    conn->send(network::make_chat_message_broadcast(data));
}

void game_handlers::send_chat_to_nearby(player_id sender,
                                        int16_t range,
                                        const network::chat_message_broadcast_data& data)
{
    if (!players_)
        return;

    auto nearby = players_->get_players_in_range(sender, range);
    for (auto pid : nearby)
    {
        send_chat_to_player(pid, data);
    }
}

auto game_handlers::get_player_commands() -> const std::vector<command_descriptor>&
{
    static const std::vector<command_descriptor> commands = []()
    {
        std::vector<command_descriptor> cmds;

        // General commands (always enabled)
        cmds.push_back({"online", "Show online player count", "/online", command_category::general, nullptr});
        cmds.push_back({"time", "Show server time", "/time", command_category::general, nullptr});
        cmds.push_back({"pos", "Show your position", "/pos", command_category::general, nullptr});

        // Guild commands
        cmds.push_back({"gcreate",
                        "Create a new guild",
                        "/gcreate <name> [tag]",
                        command_category::guild,
                        [](const player::player& plr, const social::social_system* social) -> bool
                        {
                            if (!social)
                                return false;
                            return !social->get_player_guild(plr.id).is_valid();
                        }});

        cmds.push_back({"gdisband",
                        "Disband your guild",
                        "/gdisband",
                        command_category::guild,
                        [](const player::player& plr, const social::social_system* social) -> bool
                        {
                            if (!social)
                                return false;
                            auto gid = social->get_player_guild(plr.id);
                            if (!gid.is_valid())
                                return false;
                            auto* g = social->get_guild(gid);
                            return g && g->has_permission(plr.id, social::guild_permission::disband);
                        }});

        cmds.push_back({"ginvite",
                        "Invite a player to your guild",
                        "/ginvite <player>",
                        command_category::guild,
                        [](const player::player& plr, const social::social_system* social) -> bool
                        {
                            if (!social)
                                return false;
                            auto gid = social->get_player_guild(plr.id);
                            if (!gid.is_valid())
                                return false;
                            auto* g = social->get_guild(gid);
                            return g && g->has_permission(plr.id, social::guild_permission::invite);
                        }});

        cmds.push_back({"gkick",
                        "Kick a member from your guild",
                        "/gkick <player>",
                        command_category::guild,
                        [](const player::player& plr, const social::social_system* social) -> bool
                        {
                            if (!social)
                                return false;
                            auto gid = social->get_player_guild(plr.id);
                            if (!gid.is_valid())
                                return false;
                            auto* g = social->get_guild(gid);
                            return g && g->has_permission(plr.id, social::guild_permission::kick);
                        }});

        cmds.push_back({"gaccept", "Accept a guild invite", "/gaccept", command_category::guild, nullptr});

        cmds.push_back({"gdecline", "Decline a guild invite", "/gdecline", command_category::guild, nullptr});

        cmds.push_back({"gquit",
                        "Leave your guild",
                        "/gquit",
                        command_category::guild,
                        [](const player::player& plr, const social::social_system* social) -> bool
                        {
                            if (!social)
                                return false;
                            auto gid = social->get_player_guild(plr.id);
                            if (!gid.is_valid())
                                return false;
                            auto* g = social->get_guild(gid);
                            // Guild master cannot /gquit — must /gdisband
                            return g && g->master != plr.id;
                        }});

        return cmds;
    }();

    return commands;
}

auto game_handlers::evaluate_guild_commands(player_id pid) -> std::vector<std::pair<std::string, bool>>
{
    std::vector<std::pair<std::string, bool>> result;
    auto* plr = players_ ? players_->get_player(pid) : nullptr;
    if (!plr)
        return result;

    for (const auto& cmd : get_player_commands())
    {
        if (cmd.category != command_category::guild)
            continue;
        bool enabled = cmd.enabled_check ? cmd.enabled_check(*plr, social_) : true;
        result.emplace_back(cmd.name, enabled);
    }
    return result;
}

void game_handlers::send_available_commands(player_id pid, connection_id conn_id)
{
    auto* conn = get_connection(conn_id);
    if (!conn)
        return;
    auto* plr = players_ ? players_->get_player(pid) : nullptr;
    if (!plr)
        return;

    auto category_str = [](command_category cat) -> std::string
    {
        switch (cat)
        {
        case command_category::general:
            return "general";
        case command_category::guild:
            return "guild";
        case command_category::social:
            return "social";
        case command_category::gm:
            return "gm";
        case command_category::admin:
            return "admin";
        }
        return "general";
    };

    std::vector<network::command_entry_msg> entries;

    // Add player commands
    for (const auto& cmd : get_player_commands())
    {
        bool enabled = cmd.enabled_check ? cmd.enabled_check(*plr, social_) : true;
        entries.push_back({cmd.name, cmd.description, cmd.usage, category_str(cmd.category), enabled});
    }

    // Add admin commands for GM+ players
    if (plr->is_gm() && admin_)
    {
        auto admin_lvl = admin_->get_admin_level(pid);
        auto admin_cmds = admin_->get_commands_for_level(admin_lvl);
        for (const auto* info : admin_cmds)
        {
            if (info->hidden)
                continue;
            std::string cat = (info->required_level >= admin::admin_level::admin) ? "admin" : "gm";
            entries.push_back({info->name, info->description, info->usage, cat, true});
            for (const auto& alias : info->aliases)
            {
                entries.push_back({alias, info->description, info->usage, cat, true});
            }
        }
    }

    conn->send(network::make_available_commands(entries));
}

void game_handlers::send_guild_command_update(player_id pid)
{
    auto* plr = players_ ? players_->get_player(pid) : nullptr;
    if (!plr || plr->connection.value == 0)
        return;
    auto* conn = ws_server_ ? ws_server_->get_connection(plr->connection) : nullptr;
    if (!conn || !conn->is_open())
        return;

    auto changes = evaluate_guild_commands(pid);
    if (!changes.empty())
    {
        conn->send(network::make_command_availability_update(changes));
    }
}

} // namespace hb::bridge
