#include "platform/platform.h"
#include "bridge/handlers/game_handlers.h"
#include "bridge/handlers/broadcast_util.h"
#include "network/websocket_server.h"
#include "player/player_system.h"
#include "social/social_system.h"
#include "core/logger.h"
#include "perf/perf_stats.h"
#include "core/subsystem.h"

namespace hb::bridge
{

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

// ========== Friend Handlers ==========

void game_handlers::handle_friend_request_send(connection_id conn_id, const network::json_message& msg)
{
    auto* conn = require_in_game(conn_id, msg.seq);
    if (!conn)
        return;

    auto data_result = network::friend_target_request_data::from_json(msg.data);
    if (data_result.is_err())
    {
        send_error(conn_id, msg.seq, "invalid_request", data_result.error());
        return;
    }

    auto* plr = players_->get_player(conn->player());
    if (!plr)
    {
        send_error(conn_id, msg.seq, "internal_error", "Player not found");
        return;
    }

    // Look up target character
    auto [target_char_id, target_name] = social_->lookup_character_by_name(data_result.value().target_name);
    if (!target_char_id.is_valid())
    {
        conn->send(network::make_friend_response(
            msg.seq, network::json_message_type::friend_request_send_response, false, "player_not_found"));
        return;
    }

    auto result = social_->send_friend_request(plr->character_id, target_char_id);
    bool success = result == social::friend_result::success;
    conn->send(network::make_friend_response(msg.seq,
                                             network::json_message_type::friend_request_send_response,
                                             success,
                                             std::string(social::to_string_view(result))));

    // If success, notify target if online
    if (success)
    {
        auto target_runtime = social_->get_friend_runtime_id(target_char_id);
        if (target_runtime.is_valid())
        {
            auto* target_conn = ws_server_->get_connection_by_player(target_runtime);
            if (target_conn)
            {
                target_conn->send(network::make_friend_request_notification(plr->name));
            }
        }
    }
}

void game_handlers::handle_friend_request_accept(connection_id conn_id, const network::json_message& msg)
{
    auto* conn = require_in_game(conn_id, msg.seq);
    if (!conn)
        return;

    auto data_result = network::friend_target_request_data::from_json(msg.data);
    if (data_result.is_err())
    {
        send_error(conn_id, msg.seq, "invalid_request", data_result.error());
        return;
    }

    auto* plr = players_->get_player(conn->player());
    if (!plr)
    {
        send_error(conn_id, msg.seq, "internal_error", "Player not found");
        return;
    }

    auto [requester_char_id, requester_name] = social_->lookup_character_by_name(data_result.value().target_name);
    if (!requester_char_id.is_valid())
    {
        conn->send(network::make_friend_response(
            msg.seq, network::json_message_type::friend_request_accept_response, false, "player_not_found"));
        return;
    }

    auto result = social_->accept_friend_request(plr->character_id, requester_char_id);
    bool success = result == social::friend_result::success;
    conn->send(network::make_friend_response(msg.seq,
                                             network::json_message_type::friend_request_accept_response,
                                             success,
                                             std::string(social::to_string_view(result))));

    // If success, notify requester if online
    if (success)
    {
        auto requester_runtime = social_->get_friend_runtime_id(requester_char_id);
        if (requester_runtime.is_valid())
        {
            auto* req_conn = ws_server_->get_connection_by_player(requester_runtime);
            if (req_conn)
            {
                req_conn->send(network::make_friend_accepted_notification(plr->name));
            }
        }
    }
}

void game_handlers::handle_friend_request_decline(connection_id conn_id, const network::json_message& msg)
{
    auto* conn = require_in_game(conn_id, msg.seq);
    if (!conn)
        return;

    auto data_result = network::friend_target_request_data::from_json(msg.data);
    if (data_result.is_err())
    {
        send_error(conn_id, msg.seq, "invalid_request", data_result.error());
        return;
    }

    auto* plr = players_->get_player(conn->player());
    if (!plr)
    {
        send_error(conn_id, msg.seq, "internal_error", "Player not found");
        return;
    }

    auto [requester_char_id, _] = social_->lookup_character_by_name(data_result.value().target_name);
    if (!requester_char_id.is_valid())
    {
        conn->send(network::make_friend_response(
            msg.seq, network::json_message_type::friend_request_decline_response, false, "player_not_found"));
        return;
    }

    auto result = social_->decline_friend_request(plr->character_id, requester_char_id);
    bool success = result == social::friend_result::success;
    conn->send(network::make_friend_response(msg.seq,
                                             network::json_message_type::friend_request_decline_response,
                                             success,
                                             std::string(social::to_string_view(result))));
}

void game_handlers::handle_friend_request_cancel(connection_id conn_id, const network::json_message& msg)
{
    auto* conn = require_in_game(conn_id, msg.seq);
    if (!conn)
        return;

    auto data_result = network::friend_target_request_data::from_json(msg.data);
    if (data_result.is_err())
    {
        send_error(conn_id, msg.seq, "invalid_request", data_result.error());
        return;
    }

    auto* plr = players_->get_player(conn->player());
    if (!plr)
    {
        send_error(conn_id, msg.seq, "internal_error", "Player not found");
        return;
    }

    auto [target_char_id, _] = social_->lookup_character_by_name(data_result.value().target_name);
    if (!target_char_id.is_valid())
    {
        conn->send(network::make_friend_response(
            msg.seq, network::json_message_type::friend_request_cancel_response, false, "player_not_found"));
        return;
    }

    auto result = social_->cancel_friend_request(plr->character_id, target_char_id);
    bool success = result == social::friend_result::success;
    conn->send(network::make_friend_response(msg.seq,
                                             network::json_message_type::friend_request_cancel_response,
                                             success,
                                             std::string(social::to_string_view(result))));
}

void game_handlers::handle_friend_remove(connection_id conn_id, const network::json_message& msg)
{
    auto* conn = require_in_game(conn_id, msg.seq);
    if (!conn)
        return;

    auto data_result = network::friend_target_request_data::from_json(msg.data);
    if (data_result.is_err())
    {
        send_error(conn_id, msg.seq, "invalid_request", data_result.error());
        return;
    }

    auto* plr = players_->get_player(conn->player());
    if (!plr)
    {
        send_error(conn_id, msg.seq, "internal_error", "Player not found");
        return;
    }

    auto [target_char_id, _] = social_->lookup_character_by_name(data_result.value().target_name);
    if (!target_char_id.is_valid())
    {
        conn->send(network::make_friend_response(
            msg.seq, network::json_message_type::friend_remove_response, false, "player_not_found"));
        return;
    }

    auto result = social_->remove_friend(plr->character_id, target_char_id);
    bool success = result == social::friend_result::success;
    conn->send(network::make_friend_response(msg.seq,
                                             network::json_message_type::friend_remove_response,
                                             success,
                                             std::string(social::to_string_view(result))));
}

void game_handlers::handle_friend_block(connection_id conn_id, const network::json_message& msg)
{
    auto* conn = require_in_game(conn_id, msg.seq);
    if (!conn)
        return;

    auto data_result = network::friend_target_request_data::from_json(msg.data);
    if (data_result.is_err())
    {
        send_error(conn_id, msg.seq, "invalid_request", data_result.error());
        return;
    }

    auto* plr = players_->get_player(conn->player());
    if (!plr)
    {
        send_error(conn_id, msg.seq, "internal_error", "Player not found");
        return;
    }

    auto [target_char_id, _] = social_->lookup_character_by_name(data_result.value().target_name);
    if (!target_char_id.is_valid())
    {
        conn->send(network::make_friend_response(
            msg.seq, network::json_message_type::friend_block_response, false, "player_not_found"));
        return;
    }

    auto result = social_->block_player_friend(plr->character_id, target_char_id);
    bool success = result == social::friend_result::success;
    conn->send(network::make_friend_response(msg.seq,
                                             network::json_message_type::friend_block_response,
                                             success,
                                             std::string(social::to_string_view(result))));
}

void game_handlers::handle_friend_unblock(connection_id conn_id, const network::json_message& msg)
{
    auto* conn = require_in_game(conn_id, msg.seq);
    if (!conn)
        return;

    auto data_result = network::friend_target_request_data::from_json(msg.data);
    if (data_result.is_err())
    {
        send_error(conn_id, msg.seq, "invalid_request", data_result.error());
        return;
    }

    auto* plr = players_->get_player(conn->player());
    if (!plr)
    {
        send_error(conn_id, msg.seq, "internal_error", "Player not found");
        return;
    }

    auto [target_char_id, _] = social_->lookup_character_by_name(data_result.value().target_name);
    if (!target_char_id.is_valid())
    {
        conn->send(network::make_friend_response(
            msg.seq, network::json_message_type::friend_unblock_response, false, "player_not_found"));
        return;
    }

    auto result = social_->unblock_player_friend(plr->character_id, target_char_id);
    bool success = result == social::friend_result::success;
    conn->send(network::make_friend_response(msg.seq,
                                             network::json_message_type::friend_unblock_response,
                                             success,
                                             std::string(social::to_string_view(result))));
}

void game_handlers::handle_friend_list(connection_id conn_id, const network::json_message& msg)
{
    auto* conn = require_in_game(conn_id, msg.seq);
    if (!conn)
        return;

    auto* plr = players_->get_player(conn->player());
    if (!plr)
    {
        send_error(conn_id, msg.seq, "internal_error", "Player not found");
        return;
    }

    auto char_id = plr->character_id;

    // Build friend list
    std::vector<network::friend_list_entry_msg> friends_list;
    auto* friends = social_->get_friends(char_id);
    if (friends)
    {
        for (const auto& f : *friends)
        {
            network::friend_list_entry_msg entry;
            entry.name = f.name;
            entry.is_online = f.is_online();
            friends_list.push_back(std::move(entry));
        }
    }

    // Build incoming requests
    std::vector<network::friend_request_msg> incoming;
    auto* in_reqs = social_->get_incoming_requests(char_id);
    if (in_reqs)
    {
        for (const auto& r : *in_reqs)
        {
            network::friend_request_msg req_msg;
            req_msg.name = r.requester_name;
            incoming.push_back(std::move(req_msg));
        }
    }

    // Build outgoing requests
    std::vector<network::friend_request_msg> outgoing;
    auto* out_reqs = social_->get_outgoing_requests(char_id);
    if (out_reqs)
    {
        for (const auto& r : *out_reqs)
        {
            network::friend_request_msg req_msg;
            req_msg.name = r.requestee_name;
            if (req_msg.name.empty())
            {
                // Fallback: look up from online players
                auto target_runtime = social_->get_friend_runtime_id(r.requestee_char_id);
                if (target_runtime.is_valid() && players_)
                {
                    auto* target_plr = players_->get_player(target_runtime);
                    if (target_plr)
                        req_msg.name = target_plr->name;
                }
            }
            outgoing.push_back(std::move(req_msg));
        }
    }

    // Build blocked list
    std::vector<std::string> blocked;
    auto* block_set = social_->get_blocked_players(char_id);
    if (block_set)
    {
        for (auto blocked_char_id : *block_set)
        {
            auto name = social_->lookup_character_name(blocked_char_id);
            blocked.push_back(name.empty() ? "Unknown" : name);
        }
    }

    conn->send(network::make_friend_list_response(msg.seq, friends_list, incoming, outgoing, blocked));
}

// ========== Guild Handlers ==========

void game_handlers::broadcast_guild_update(guild_id gid,
                                           const std::string& action,
                                           const std::string& guild_name,
                                           const std::string& player_name,
                                           const nlohmann::json& extra)
{
    if (!social_ || !ws_server_)
        return;

    auto* g = social_->get_guild(gid);
    if (!g)
        return;

    auto msg = network::make_guild_update(action, guild_name, player_name, extra);
    for (const auto& member : g->members)
    {
        if (member.player.is_valid())
        {
            auto* member_conn = ws_server_->get_connection_by_player(member.player);
            if (member_conn)
            {
                member_conn->send(msg);
            }
        }
    }
}

void game_handlers::handle_guild_create(connection_id conn_id, const network::json_message& msg)
{
    auto* conn = require_in_game(conn_id, msg.seq);
    if (!conn)
        return;

    auto data_result = network::guild_create_request_data::from_json(msg.data);
    if (data_result.is_err())
    {
        send_error(conn_id, msg.seq, "invalid_request", data_result.error());
        return;
    }

    auto* plr = players_->get_player(conn->player());
    if (!plr)
    {
        send_error(conn_id, msg.seq, "internal_error", "Player not found");
        return;
    }

    auto& data = data_result.value();
    auto result = social_->create_guild(conn->player(), data.name, data.tag);
    if (result.is_err())
    {
        conn->send(network::make_guild_response(
            msg.seq, network::json_message_type::guild_create_response, false, guild_result_string(result.error())));
        return;
    }

    plr->guild_name = data.name;
    plr->guild_tag = data.tag;
    plr->guild_rank = static_cast<uint8_t>(social::guild_rank::guild_master);

    conn->send(network::make_guild_response(msg.seq,
                                            network::json_message_type::guild_create_response,
                                            true,
                                            {},
                                            {{"guild_name", data.name}, {"tag", data.tag}}));

    send_guild_command_update(conn->player());
}

void game_handlers::handle_guild_disband(connection_id conn_id, const network::json_message& msg)
{
    auto* conn = require_in_game(conn_id, msg.seq);
    if (!conn)
        return;

    auto* plr = players_->get_player(conn->player());
    if (!plr)
    {
        send_error(conn_id, msg.seq, "internal_error", "Player not found");
        return;
    }

    auto gid = social_->get_player_guild(conn->player());
    if (!gid.is_valid())
    {
        conn->send(network::make_guild_response(
            msg.seq, network::json_message_type::guild_disband_response, false, "not_in_guild"));
        return;
    }

    auto* g = social_->get_guild(gid);
    if (!g)
    {
        conn->send(network::make_guild_response(
            msg.seq, network::json_message_type::guild_disband_response, false, "guild_not_found"));
        return;
    }

    // Collect member connections BEFORE disband
    std::string guild_name = g->name;
    std::vector<player_id> member_pids;
    for (const auto& member : g->members)
    {
        if (member.player.is_valid())
        {
            member_pids.push_back(member.player);
        }
    }

    auto result = social_->disband_guild(conn->player(), gid);
    if (result != social::guild_result::success)
    {
        conn->send(network::make_guild_response(
            msg.seq, network::json_message_type::guild_disband_response, false, guild_result_string(result)));
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

    // Notify all former members
    auto update_msg = network::make_guild_update("guild_disbanded", guild_name);
    for (auto mid : member_pids)
    {
        auto* member_conn = ws_server_->get_connection_by_player(mid);
        if (member_conn)
        {
            member_conn->send(update_msg);
        }
        send_guild_command_update(mid);
    }

    conn->send(network::make_guild_response(msg.seq, network::json_message_type::guild_disband_response, true));
}

void game_handlers::handle_guild_leave(connection_id conn_id, const network::json_message& msg)
{
    auto* conn = require_in_game(conn_id, msg.seq);
    if (!conn)
        return;

    auto* plr = players_->get_player(conn->player());
    if (!plr)
    {
        send_error(conn_id, msg.seq, "internal_error", "Player not found");
        return;
    }

    auto gid = social_->get_player_guild(conn->player());
    if (!gid.is_valid())
    {
        conn->send(network::make_guild_response(
            msg.seq, network::json_message_type::guild_leave_response, false, "not_in_guild"));
        return;
    }

    auto* g = social_->get_guild(gid);
    std::string guild_name = g ? g->name : "";

    auto result = social_->leave_guild(conn->player());
    if (result != social::guild_result::success)
    {
        conn->send(network::make_guild_response(
            msg.seq, network::json_message_type::guild_leave_response, false, guild_result_string(result)));
        return;
    }

    std::string player_name = plr->name;
    plr->guild_name.clear();
    plr->guild_tag.clear();
    plr->guild_rank = 0;

    conn->send(network::make_guild_response(msg.seq, network::json_message_type::guild_leave_response, true));

    send_guild_command_update(conn->player());

    // Broadcast to remaining members
    broadcast_guild_update(gid, "member_left", guild_name, player_name);
}

void game_handlers::handle_guild_kick(connection_id conn_id, const network::json_message& msg)
{
    auto* conn = require_in_game(conn_id, msg.seq);
    if (!conn)
        return;

    auto data_result = network::guild_target_request_data::from_json(msg.data);
    if (data_result.is_err())
    {
        send_error(conn_id, msg.seq, "invalid_request", data_result.error());
        return;
    }

    auto* plr = players_->get_player(conn->player());
    if (!plr)
    {
        send_error(conn_id, msg.seq, "internal_error", "Player not found");
        return;
    }

    // Resolve target by name
    auto* target = players_->get_player_by_name(data_result.value().target_name);
    if (!target)
    {
        conn->send(network::make_guild_response(
            msg.seq, network::json_message_type::guild_kick_response, false, "player_not_found"));
        return;
    }

    auto gid = social_->get_player_guild(conn->player());
    if (!gid.is_valid())
    {
        conn->send(network::make_guild_response(
            msg.seq, network::json_message_type::guild_kick_response, false, "not_in_guild"));
        return;
    }

    auto* g = social_->get_guild(gid);
    std::string guild_name = g ? g->name : "";

    auto result = social_->kick_from_guild(conn->player(), target->id);
    if (result != social::guild_result::success)
    {
        conn->send(network::make_guild_response(
            msg.seq, network::json_message_type::guild_kick_response, false, guild_result_string(result)));
        return;
    }

    std::string target_name = target->name;
    target->guild_name.clear();
    target->guild_tag.clear();
    target->guild_rank = 0;

    conn->send(network::make_guild_response(msg.seq, network::json_message_type::guild_kick_response, true));

    // Broadcast to remaining members
    broadcast_guild_update(gid, "member_kicked", guild_name, target_name);

    // Notify the kicked player
    auto* target_conn = ws_server_->get_connection_by_player(target->id);
    if (target_conn)
    {
        target_conn->send(network::make_guild_update("you_were_kicked", guild_name));
    }
    send_guild_command_update(target->id);
}

void game_handlers::handle_guild_invite(connection_id conn_id, const network::json_message& msg)
{
    auto* conn = require_in_game(conn_id, msg.seq);
    if (!conn)
        return;

    auto data_result = network::guild_target_request_data::from_json(msg.data);
    if (data_result.is_err())
    {
        send_error(conn_id, msg.seq, "invalid_request", data_result.error());
        return;
    }

    auto* plr = players_->get_player(conn->player());
    if (!plr)
    {
        send_error(conn_id, msg.seq, "internal_error", "Player not found");
        return;
    }

    // Resolve target by name
    auto* target = players_->get_player_by_name(data_result.value().target_name);
    if (!target)
    {
        conn->send(network::make_guild_response(
            msg.seq, network::json_message_type::guild_invite_response, false, "player_not_found"));
        return;
    }

    auto gid = social_->get_player_guild(conn->player());
    if (!gid.is_valid())
    {
        conn->send(network::make_guild_response(
            msg.seq, network::json_message_type::guild_invite_response, false, "not_in_guild"));
        return;
    }

    auto* g = social_->get_guild(gid);
    if (!g)
    {
        conn->send(network::make_guild_response(
            msg.seq, network::json_message_type::guild_invite_response, false, "guild_not_found"));
        return;
    }

    // Store pending invite (target must /gaccept)
    auto result = social_->invite_to_guild(conn->player(), gid, target->id);
    if (result != social::guild_result::success)
    {
        conn->send(network::make_guild_response(
            msg.seq, network::json_message_type::guild_invite_response, false, guild_result_string(result)));
        return;
    }

    conn->send(network::make_guild_response(msg.seq, network::json_message_type::guild_invite_response, true));

    // Push invite notification to target
    auto* target_conn = ws_server_->get_connection_by_player(target->id);
    if (target_conn)
    {
        target_conn->send(network::make_guild_invite_received(g->name, g->tag, plr->name));
    }
    send_guild_command_update(target->id);
}

void game_handlers::handle_guild_invite_respond(connection_id conn_id, const network::json_message& msg)
{
    auto* conn = require_in_game(conn_id, msg.seq);
    if (!conn)
        return;

    auto data_result = network::guild_invite_respond_request_data::from_json(msg.data);
    if (data_result.is_err())
    {
        send_error(conn_id, msg.seq, "invalid_request", data_result.error());
        return;
    }

    auto* plr = players_->get_player(conn->player());
    if (!plr)
    {
        send_error(conn_id, msg.seq, "internal_error", "Player not found");
        return;
    }

    if (!data_result.value().accept)
    {
        // Decline
        social_->decline_guild_invite(conn->player());
        conn->send(network::make_guild_response(msg.seq,
                                                network::json_message_type::guild_invite_respond_response,
                                                true,
                                                {},
                                                nlohmann::json{{"accepted", false}}));
        send_guild_command_update(conn->player());
        return;
    }

    // Accept
    auto accept_result = social_->accept_guild_invite(conn->player());
    if (accept_result.is_err())
    {
        conn->send(network::make_guild_response(msg.seq,
                                                network::json_message_type::guild_invite_respond_response,
                                                false,
                                                guild_result_string(accept_result.error())));
        return;
    }

    auto gid = accept_result.value();
    auto* g = social_->get_guild(gid);
    if (g)
    {
        plr->guild_name = g->name;
        plr->guild_tag = g->tag;
        plr->guild_rank = static_cast<uint8_t>(social::guild_rank::recruit);

        conn->send(network::make_guild_response(
            msg.seq,
            network::json_message_type::guild_invite_respond_response,
            true,
            {},
            nlohmann::json{{"accepted", true}, {"guild_name", g->name}, {"guild_tag", g->tag}}));

        broadcast_guild_update(gid, "member_joined", g->name, plr->name);
    }
    else
    {
        conn->send(network::make_guild_response(msg.seq,
                                                network::json_message_type::guild_invite_respond_response,
                                                true,
                                                {},
                                                nlohmann::json{{"accepted", true}}));
    }

    send_guild_command_update(conn->player());
}

void game_handlers::handle_guild_promote(connection_id conn_id, const network::json_message& msg)
{
    auto* conn = require_in_game(conn_id, msg.seq);
    if (!conn)
        return;

    auto data_result = network::guild_target_request_data::from_json(msg.data);
    if (data_result.is_err())
    {
        send_error(conn_id, msg.seq, "invalid_request", data_result.error());
        return;
    }

    auto* plr = players_->get_player(conn->player());
    if (!plr)
    {
        send_error(conn_id, msg.seq, "internal_error", "Player not found");
        return;
    }

    auto* target = players_->get_player_by_name(data_result.value().target_name);
    if (!target)
    {
        conn->send(network::make_guild_response(
            msg.seq, network::json_message_type::guild_promote_response, false, "player_not_found"));
        return;
    }

    auto gid = social_->get_player_guild(conn->player());
    std::string guild_name = plr->guild_name;

    auto result = social_->promote_member(conn->player(), target->id);
    if (result != social::guild_result::success)
    {
        conn->send(network::make_guild_response(
            msg.seq, network::json_message_type::guild_promote_response, false, guild_result_string(result)));
        return;
    }

    // Update target's guild_rank from the actual guild data
    auto* g = social_->get_guild(gid);
    if (g)
    {
        auto* member = g->get_member(target->id);
        if (member)
        {
            target->guild_rank = static_cast<uint8_t>(member->rank);
        }
    }

    conn->send(network::make_guild_response(msg.seq, network::json_message_type::guild_promote_response, true));

    send_guild_command_update(target->id);

    broadcast_guild_update(gid, "member_promoted", guild_name, target->name);
}

void game_handlers::handle_guild_demote(connection_id conn_id, const network::json_message& msg)
{
    auto* conn = require_in_game(conn_id, msg.seq);
    if (!conn)
        return;

    auto data_result = network::guild_target_request_data::from_json(msg.data);
    if (data_result.is_err())
    {
        send_error(conn_id, msg.seq, "invalid_request", data_result.error());
        return;
    }

    auto* plr = players_->get_player(conn->player());
    if (!plr)
    {
        send_error(conn_id, msg.seq, "internal_error", "Player not found");
        return;
    }

    auto* target = players_->get_player_by_name(data_result.value().target_name);
    if (!target)
    {
        conn->send(network::make_guild_response(
            msg.seq, network::json_message_type::guild_demote_response, false, "player_not_found"));
        return;
    }

    auto gid = social_->get_player_guild(conn->player());
    std::string guild_name = plr->guild_name;

    auto result = social_->demote_member(conn->player(), target->id);
    if (result != social::guild_result::success)
    {
        conn->send(network::make_guild_response(
            msg.seq, network::json_message_type::guild_demote_response, false, guild_result_string(result)));
        return;
    }

    auto* g = social_->get_guild(gid);
    if (g)
    {
        auto* member = g->get_member(target->id);
        if (member)
        {
            target->guild_rank = static_cast<uint8_t>(member->rank);
        }
    }

    conn->send(network::make_guild_response(msg.seq, network::json_message_type::guild_demote_response, true));

    send_guild_command_update(target->id);

    broadcast_guild_update(gid, "member_demoted", guild_name, target->name);
}

void game_handlers::handle_guild_set_motd(connection_id conn_id, const network::json_message& msg)
{
    auto* conn = require_in_game(conn_id, msg.seq);
    if (!conn)
        return;

    auto data_result = network::guild_set_motd_request_data::from_json(msg.data);
    if (data_result.is_err())
    {
        send_error(conn_id, msg.seq, "invalid_request", data_result.error());
        return;
    }

    auto* plr = players_->get_player(conn->player());
    if (!plr)
    {
        send_error(conn_id, msg.seq, "internal_error", "Player not found");
        return;
    }

    auto gid = social_->get_player_guild(conn->player());
    if (!gid.is_valid())
    {
        conn->send(network::make_guild_response(
            msg.seq, network::json_message_type::guild_set_motd_response, false, "not_in_guild"));
        return;
    }

    auto result = social_->set_guild_motd(conn->player(), data_result.value().motd);
    if (result != social::guild_result::success)
    {
        conn->send(network::make_guild_response(
            msg.seq, network::json_message_type::guild_set_motd_response, false, guild_result_string(result)));
        return;
    }

    conn->send(network::make_guild_response(msg.seq, network::json_message_type::guild_set_motd_response, true));

    broadcast_guild_update(gid, "motd_changed", plr->guild_name, {}, {{"motd", data_result.value().motd}});
}

void game_handlers::handle_guild_info(connection_id conn_id, const network::json_message& msg)
{
    auto* conn = require_in_game(conn_id, msg.seq);
    if (!conn)
        return;

    auto* plr = players_->get_player(conn->player());
    if (!plr)
    {
        send_error(conn_id, msg.seq, "internal_error", "Player not found");
        return;
    }

    auto gid = social_->get_player_guild(conn->player());
    if (!gid.is_valid())
    {
        conn->send(network::make_guild_info_response(msg.seq, false, {}, {}, {}, 0, {}, {}, "not_in_guild"));
        return;
    }

    auto* g = social_->get_guild(gid);
    if (!g)
    {
        conn->send(network::make_guild_info_response(msg.seq, false, {}, {}, {}, 0, {}, {}, "guild_not_found"));
        return;
    }

    // Find master name
    std::string master_name;
    for (const auto& member : g->members)
    {
        if (member.rank == social::guild_rank::guild_master)
        {
            master_name = member.name;
            break;
        }
    }

    // Build member list
    std::vector<network::guild_member_info_msg> members;
    for (const auto& member : g->members)
    {
        auto rank_idx = static_cast<size_t>(member.rank);
        std::string rank_name = rank_idx < g->ranks.size() ? g->ranks[rank_idx].name : "Unknown";

        members.push_back(network::guild_member_info_msg{.name = member.name,
                                                         .rank = static_cast<uint8_t>(member.rank),
                                                         .rank_name = rank_name,
                                                         .is_online = member.player.is_valid()});
    }

    conn->send(network::make_guild_info_response(
        msg.seq, true, g->name, g->tag, g->motd, g->member_count(), master_name, members));
}

} // namespace hb::bridge
