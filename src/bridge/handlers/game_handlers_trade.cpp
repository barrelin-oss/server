// game_handlers_trade.cpp
// v2 trade handlers: 3-phase offer -> lock -> confirm protocol

#include "platform/platform.h"
#include "bridge/handlers/game_handlers.h"
#include "network/websocket_server.h"
#include "player/player_system.h"
#include "world/world_subsystem.h"
#include "inventory/inventory_system.h"
#include "item/item_system.h"
#include "item/item_ops.h"
#include "item/item_serialization.h"
#include "core/logger.h"

namespace hb::bridge
{

// ============================================================
// Trade helpers
// ============================================================

void game_handlers::send_trade_update_v2(entity_id player_a, entity_id player_b)
{
    if (!inventory_ || !item_ || !players_ || !ws_server_)
        return;

    auto* window_a = inventory_->get_trade_window(player_a);
    auto* window_b = inventory_->get_trade_window(player_b);
    if (!window_a || !window_b)
        return;

    // Build serialized item arrays for each side
    auto build_items = [&](const inventory::trade_window* w) -> std::vector<nlohmann::json>
    {
        std::vector<nlohmann::json> result;
        for (auto id : w->items())
        {
            auto* itm = item_->get_item(id);
            if (itm)
            {
                result.push_back(item::serialize_item(*itm));
            }
        }
        return result;
    };

    auto items_a = build_items(window_a);
    auto items_b = build_items(window_b);

    // Find connections for both players
    auto find_player_conn = [&](entity_id eid) -> network::ws_connection*
    {
        // Look through all players to find the one with this entity_id
        network::ws_connection* found = nullptr;
        players_->for_each_player([&](player_id pid, const player::player& plr)
        {
            if (entity_id(plr.id.value) == eid)
            {
                found = ws_server_->get_connection_by_player(pid);
            }
        });
        return found;
    };

    auto* conn_a = find_player_conn(player_a);
    auto* conn_b = find_player_conn(player_b);

    // Player A sees: mine = A's offer, theirs = B's offer
    if (conn_a)
    {
        conn_a->send_raw(network::make_trade_update("mine", items_a, window_a->gold()).dump());
        conn_a->send_raw(network::make_trade_update("theirs", items_b, window_b->gold()).dump());
    }

    // Player B sees: mine = B's offer, theirs = A's offer
    if (conn_b)
    {
        conn_b->send_raw(network::make_trade_update("mine", items_b, window_b->gold()).dump());
        conn_b->send_raw(network::make_trade_update("theirs", items_a, window_a->gold()).dump());
    }
}

void game_handlers::send_trade_complete_updates(
    entity_id player_a, entity_id player_b,
    const item_ops::trade_result& result)
{
    if (!item_ || !players_ || !ws_server_ || !inventory_)
        return;

    // Helper to find connection for an entity_id
    auto find_player_conn = [&](entity_id eid) -> network::ws_connection*
    {
        network::ws_connection* found = nullptr;
        players_->for_each_player([&](player_id pid, const player::player& plr)
        {
            if (entity_id(plr.id.value) == eid)
            {
                found = ws_server_->get_connection_by_player(pid);
            }
        });
        return found;
    };

    // Send state updates for one side of the trade
    auto send_side_updates = [&](network::ws_connection* conn,
                                 entity_id eid,
                                 const item_ops::trade_result::player_side& side)
    {
        if (!conn)
            return;

        // Notify removed items
        for (auto lost_id : side.lost_items)
        {
            conn->send_raw(network::make_inventory_item_removed_v2(lost_id).dump());
        }

        // Notify received items with placement
        for (const auto& placement : side.placements)
        {
            auto* itm = item_->get_item(placement.item);
            if (itm)
            {
                conn->send_raw(network::make_inventory_item_add(
                    *itm, placement.pos_x, placement.pos_y, placement.z_order).dump());
            }
        }

        // Gold update
        auto new_gold = inventory_->get_gold(eid);
        conn->send_raw(network::make_inventory_gold_update(new_gold).dump());

        // Weight update
        conn->send_raw(network::make_inventory_weight_update_v2(
            side.new_weight, side.max_weight).dump());
    };

    auto* conn_a = find_player_conn(player_a);
    auto* conn_b = find_player_conn(player_b);

    send_side_updates(conn_a, player_a, result.player_a);
    send_side_updates(conn_b, player_b, result.player_b);
}

// ============================================================
// Phase 0: Initiating
// ============================================================

void game_handlers::handle_trade_request_v2(connection_id conn_id, const network::json_message& msg)
{
    auto* conn = require_in_game(conn_id, msg.seq);
    if (!conn)
        return;

    auto data_result = network::trade_request_data_v2::from_json(msg.data);
    if (data_result.is_err())
    {
        conn->send_raw(network::make_trade_canceled("invalid_request").dump());
        return;
    }
    auto& data = data_result.value();

    if (!players_ || !ws_server_ || !inventory_)
    {
        conn->send_raw(network::make_trade_canceled("server_error").dump());
        return;
    }

    auto pid = conn->player();
    auto* plr = players_->get_player(pid);
    if (!plr || plr->is_dead())
    {
        conn->send_raw(network::make_trade_canceled("unavailable").dump());
        return;
    }

    // Check requester is not already trading
    auto requester_eid = entity_id(plr->id.value);
    if (inventory_->get_trade_window(requester_eid))
    {
        conn->send_raw(network::make_trade_canceled("already_trading").dump());
        return;
    }

    // Find target player by entity_id
    auto target_entity = entity::entity(static_cast<uint32_t>(data.target_entity_id));
    auto* target = players_->get_player_by_entity(target_entity);
    if (!target)
    {
        conn->send_raw(network::make_trade_canceled("player_not_found").dump());
        return;
    }

    // Can't trade with self
    if (target->id == pid)
    {
        conn->send_raw(network::make_trade_canceled("cannot_trade_self").dump());
        return;
    }

    // Check target is alive
    if (target->is_dead())
    {
        conn->send_raw(network::make_trade_canceled("target_unavailable").dump());
        return;
    }

    // Check target is not already trading
    auto target_eid = entity_id(target->id.value);
    if (inventory_->get_trade_window(target_eid))
    {
        conn->send_raw(network::make_trade_canceled("target_busy").dump());
        return;
    }

    // Check same map
    if (plr->current_map != target->current_map)
    {
        conn->send_raw(network::make_trade_canceled("too_far").dump());
        return;
    }

    // Check proximity (within 10 tiles)
    constexpr int max_trade_distance = 10;
    if (plr->pos.chebyshev_distance(target->pos) > max_trade_distance)
    {
        conn->send_raw(network::make_trade_canceled("too_far").dump());
        return;
    }

    // Store pending invitation: target -> requester
    pending_trade_invites_[target_entity.id] = plr->ecs_entity.id;

    // Send trade_invite to target
    auto* target_conn = ws_server_->get_connection_by_player(target->id);
    if (target_conn)
    {
        target_conn->send_raw(
            network::make_trade_invite(plr->ecs_entity.id, plr->name).dump());
    }
    else
    {
        // Target not reachable
        pending_trade_invites_.erase(target_entity.id);
        conn->send_raw(network::make_trade_canceled("target_unavailable").dump());
    }
}

void game_handlers::handle_trade_accept_v2(connection_id conn_id, const network::json_message& msg)
{
    auto* conn = require_in_game(conn_id, msg.seq);
    if (!conn)
        return;

    auto data_result = network::trade_accept_data::from_json(msg.data);
    if (data_result.is_err())
    {
        conn->send_raw(network::make_trade_canceled("invalid_request").dump());
        return;
    }
    auto& data = data_result.value();

    if (!players_ || !ws_server_ || !inventory_)
    {
        conn->send_raw(network::make_trade_canceled("server_error").dump());
        return;
    }

    auto pid = conn->player();
    auto* plr = players_->get_player(pid);
    if (!plr || plr->is_dead())
    {
        conn->send_raw(network::make_trade_canceled("unavailable").dump());
        return;
    }

    // Verify there is a pending invitation for this player from the specified requester
    auto my_eid = plr->ecs_entity.id;
    auto invite_it = pending_trade_invites_.find(my_eid);
    if (invite_it == pending_trade_invites_.end())
    {
        conn->send_raw(network::make_trade_canceled("no_pending_invite").dump());
        return;
    }

    auto requester_entity_id = invite_it->second;
    if (requester_entity_id != static_cast<uint32_t>(data.from_entity_id))
    {
        conn->send_raw(network::make_trade_canceled("invite_mismatch").dump());
        return;
    }

    // Remove the pending invitation
    pending_trade_invites_.erase(invite_it);

    // Find requester player
    auto requester_entity = entity::entity(requester_entity_id);
    auto* requester = players_->get_player_by_entity(requester_entity);
    if (!requester || requester->is_dead())
    {
        conn->send_raw(network::make_trade_canceled("player_not_found").dump());
        return;
    }

    // Verify requester is not already trading
    auto requester_eid = entity_id(requester->id.value);
    if (inventory_->get_trade_window(requester_eid))
    {
        conn->send_raw(network::make_trade_canceled("target_busy").dump());
        return;
    }

    // Verify acceptor is not already trading
    auto acceptor_eid = entity_id(plr->id.value);
    if (inventory_->get_trade_window(acceptor_eid))
    {
        conn->send_raw(network::make_trade_canceled("already_trading").dump());
        return;
    }

    // Start the trade
    inventory_->start_trade(requester_eid, acceptor_eid);

    // Send trade_opened to both
    auto* requester_conn = ws_server_->get_connection_by_player(requester->id);
    if (requester_conn)
    {
        requester_conn->send_raw(
            network::make_trade_opened(plr->ecs_entity.id, plr->name).dump());
    }
    conn->send_raw(
        network::make_trade_opened(requester->ecs_entity.id, requester->name).dump());

    LOG_DEBUG(general, "Trade opened between {} and {}", requester->name, plr->name);
}

void game_handlers::handle_trade_decline_v2(connection_id conn_id, const network::json_message& msg)
{
    auto* conn = require_in_game(conn_id, msg.seq);
    if (!conn)
        return;

    auto data_result = network::trade_decline_data::from_json(msg.data);
    if (data_result.is_err())
        return;
    auto& data = data_result.value();

    if (!players_ || !ws_server_)
        return;

    auto pid = conn->player();
    auto* plr = players_->get_player(pid);
    if (!plr)
        return;

    // Remove pending invitation
    auto my_eid = plr->ecs_entity.id;
    auto invite_it = pending_trade_invites_.find(my_eid);
    if (invite_it == pending_trade_invites_.end())
        return;

    auto requester_entity_id = invite_it->second;
    if (requester_entity_id != static_cast<uint32_t>(data.from_entity_id))
        return;

    pending_trade_invites_.erase(invite_it);

    // Send trade_canceled to requester
    auto requester_entity = entity::entity(requester_entity_id);
    auto* requester = players_->get_player_by_entity(requester_entity);
    if (requester)
    {
        auto* requester_conn = ws_server_->get_connection_by_player(requester->id);
        if (requester_conn)
        {
            requester_conn->send_raw(
                network::make_trade_canceled("player_declined").dump());
        }
    }
}

// ============================================================
// Phase 1: Offer
// ============================================================

void game_handlers::handle_trade_add_item_v2(connection_id conn_id, const network::json_message& msg)
{
    auto* conn = require_in_game(conn_id, msg.seq);
    if (!conn)
        return;

    auto data_result = network::trade_add_item_data::from_json(msg.data);
    if (data_result.is_err())
    {
        conn->send_raw(network::make_trade_canceled("invalid_request").dump());
        return;
    }
    auto& data = data_result.value();

    if (!players_ || !inventory_ || !item_)
    {
        conn->send_raw(network::make_trade_canceled("server_error").dump());
        return;
    }

    auto pid = conn->player();
    auto* plr = players_->get_player(pid);
    if (!plr)
    {
        conn->send_raw(network::make_trade_canceled("server_error").dump());
        return;
    }

    auto my_eid = entity_id(plr->id.value);
    auto partner_eid = inventory_->get_trade_partner(my_eid);
    if (!partner_eid.is_valid())
    {
        conn->send_raw(network::make_trade_canceled("not_trading").dump());
        return;
    }

    auto target_item = item_id(static_cast<uint32_t>(data.item_id));

    // Verify the item exists and belongs to this player
    auto* itm = item_->get_item(target_item);
    if (!itm || itm->owner != my_eid)
    {
        conn->send_raw(network::make_trade_canceled("item_not_found").dump());
        return;
    }

    // Verify item is in player's inventory
    auto* inv = inventory_->get_inventory(my_eid);
    if (!inv || !inv->has_item(target_item))
    {
        conn->send_raw(network::make_trade_canceled("item_not_found").dump());
        return;
    }

    auto result = inventory_->add_to_trade(my_eid, target_item);
    if (result != inventory::inventory_result::success)
    {
        conn->send_raw(network::make_trade_canceled("add_failed").dump());
        return;
    }

    // Send trade_update to both
    send_trade_update_v2(my_eid, partner_eid);
}

void game_handlers::handle_trade_remove_item_v2(connection_id conn_id, const network::json_message& msg)
{
    auto* conn = require_in_game(conn_id, msg.seq);
    if (!conn)
        return;

    auto data_result = network::trade_remove_item_data::from_json(msg.data);
    if (data_result.is_err())
    {
        conn->send_raw(network::make_trade_canceled("invalid_request").dump());
        return;
    }
    auto& data = data_result.value();

    if (!players_ || !inventory_)
    {
        conn->send_raw(network::make_trade_canceled("server_error").dump());
        return;
    }

    auto pid = conn->player();
    auto* plr = players_->get_player(pid);
    if (!plr)
    {
        conn->send_raw(network::make_trade_canceled("server_error").dump());
        return;
    }

    auto my_eid = entity_id(plr->id.value);
    auto partner_eid = inventory_->get_trade_partner(my_eid);
    if (!partner_eid.is_valid())
    {
        conn->send_raw(network::make_trade_canceled("not_trading").dump());
        return;
    }

    auto target_item = item_id(static_cast<uint32_t>(data.item_id));

    auto result = inventory_->remove_from_trade(my_eid, target_item);
    if (result != inventory::inventory_result::success)
    {
        conn->send_raw(network::make_trade_canceled("remove_failed").dump());
        return;
    }

    // Send trade_update to both
    send_trade_update_v2(my_eid, partner_eid);
}

void game_handlers::handle_trade_set_gold_v2(connection_id conn_id, const network::json_message& msg)
{
    auto* conn = require_in_game(conn_id, msg.seq);
    if (!conn)
        return;

    auto data_result = network::trade_set_gold_data::from_json(msg.data);
    if (data_result.is_err())
    {
        conn->send_raw(network::make_trade_canceled("invalid_request").dump());
        return;
    }
    auto& data = data_result.value();

    if (!players_ || !inventory_)
    {
        conn->send_raw(network::make_trade_canceled("server_error").dump());
        return;
    }

    auto pid = conn->player();
    auto* plr = players_->get_player(pid);
    if (!plr)
    {
        conn->send_raw(network::make_trade_canceled("server_error").dump());
        return;
    }

    auto my_eid = entity_id(plr->id.value);
    auto partner_eid = inventory_->get_trade_partner(my_eid);
    if (!partner_eid.is_valid())
    {
        conn->send_raw(network::make_trade_canceled("not_trading").dump());
        return;
    }

    // Validate gold amount
    auto amount = data.amount;
    if (amount < 0)
    {
        amount = 0;
    }

    // Cap to player's actual gold
    auto available_gold = inventory_->get_gold(my_eid);
    if (amount > available_gold)
    {
        amount = available_gold;
    }

    // set_trade_gold takes int32_t
    inventory_->set_trade_gold(my_eid, static_cast<int32_t>(amount));

    // Send trade_update to both
    send_trade_update_v2(my_eid, partner_eid);
}

// ============================================================
// Phase 2: Lock
// ============================================================

void game_handlers::handle_trade_lock_v2(connection_id conn_id, const network::json_message& msg)
{
    auto* conn = require_in_game(conn_id, msg.seq);
    if (!conn)
        return;

    if (!players_ || !inventory_ || !ws_server_)
    {
        conn->send_raw(network::make_trade_canceled("server_error").dump());
        return;
    }

    auto pid = conn->player();
    auto* plr = players_->get_player(pid);
    if (!plr)
    {
        conn->send_raw(network::make_trade_canceled("server_error").dump());
        return;
    }

    auto my_eid = entity_id(plr->id.value);
    auto partner_eid = inventory_->get_trade_partner(my_eid);
    if (!partner_eid.is_valid())
    {
        conn->send_raw(network::make_trade_canceled("not_trading").dump());
        return;
    }

    inventory_->lock_trade(my_eid);

    auto* my_window = inventory_->get_trade_window(my_eid);
    auto* partner_window = inventory_->get_trade_window(partner_eid);
    if (!my_window || !partner_window)
    {
        conn->send_raw(network::make_trade_canceled("server_error").dump());
        return;
    }

    bool my_locked = my_window->is_locked();
    bool partner_locked = partner_window->is_locked();

    // Helper to find connection for entity
    auto find_player_conn = [&](entity_id eid) -> network::ws_connection*
    {
        network::ws_connection* found = nullptr;
        players_->for_each_player([&](player_id p, const player::player& plr_inner)
        {
            if (entity_id(plr_inner.id.value) == eid)
            {
                found = ws_server_->get_connection_by_player(p);
            }
        });
        return found;
    };

    // Send lock status to both (each player sees from their own perspective)
    conn->send_raw(network::make_trade_lock_status(my_locked, partner_locked).dump());

    auto* partner_conn = find_player_conn(partner_eid);
    if (partner_conn)
    {
        partner_conn->send_raw(
            network::make_trade_lock_status(partner_locked, my_locked).dump());
    }
}

// ============================================================
// Phase 3: Confirm
// ============================================================

void game_handlers::handle_trade_confirm_v2(connection_id conn_id, const network::json_message& msg)
{
    auto* conn = require_in_game(conn_id, msg.seq);
    if (!conn)
        return;

    if (!players_ || !inventory_ || !item_ || !ws_server_)
    {
        conn->send_raw(network::make_trade_canceled("server_error").dump());
        return;
    }

    auto pid = conn->player();
    auto* plr = players_->get_player(pid);
    if (!plr)
    {
        conn->send_raw(network::make_trade_canceled("server_error").dump());
        return;
    }

    auto my_eid = entity_id(plr->id.value);
    auto partner_eid = inventory_->get_trade_partner(my_eid);
    if (!partner_eid.is_valid())
    {
        conn->send_raw(network::make_trade_canceled("not_trading").dump());
        return;
    }

    // Must be locked first
    auto* my_window = inventory_->get_trade_window(my_eid);
    if (!my_window || !my_window->is_locked())
    {
        conn->send_raw(network::make_trade_canceled("not_locked").dump());
        return;
    }

    inventory_->confirm_trade(my_eid);

    // Check if both sides have confirmed
    auto* partner_window = inventory_->get_trade_window(partner_eid);
    if (!partner_window)
    {
        conn->send_raw(network::make_trade_canceled("server_error").dump());
        return;
    }

    if (!partner_window->is_confirmed())
    {
        // Partner hasn't confirmed yet - just wait
        // Optionally notify the partner that we confirmed (they can see via lock_status)
        return;
    }

    // Both confirmed - execute the trade
    auto trade_result = item_ops::execute_trade(my_eid, partner_eid, item_, inventory_);
    if (!trade_result.success)
    {
        // Trade failed - cancel and notify both
        inventory_->cancel_trade(my_eid);

        // Helper to find connection for entity
        auto find_player_conn = [&](entity_id eid) -> network::ws_connection*
        {
            network::ws_connection* found = nullptr;
            players_->for_each_player([&](player_id p, const player::player& plr_inner)
            {
                if (entity_id(plr_inner.id.value) == eid)
                {
                    found = ws_server_->get_connection_by_player(p);
                }
            });
            return found;
        };

        conn->send_raw(network::make_trade_complete(false).dump());

        auto* partner_conn = find_player_conn(partner_eid);
        if (partner_conn)
        {
            partner_conn->send_raw(network::make_trade_complete(false).dump());
        }

        LOG_WARN(general, "Trade execution failed between eids {} and {}: {}",
                 my_eid.value, partner_eid.value, trade_result.error);
        return;
    }

    // Trade succeeded - send trade_complete + state updates to both
    // Helper to find connection for entity
    auto find_player_conn = [&](entity_id eid) -> network::ws_connection*
    {
        network::ws_connection* found = nullptr;
        players_->for_each_player([&](player_id p, const player::player& plr_inner)
        {
            if (entity_id(plr_inner.id.value) == eid)
            {
                found = ws_server_->get_connection_by_player(p);
            }
        });
        return found;
    };

    conn->send_raw(network::make_trade_complete(true).dump());

    auto* partner_conn = find_player_conn(partner_eid);
    if (partner_conn)
    {
        partner_conn->send_raw(network::make_trade_complete(true).dump());
    }

    // Send inventory state updates
    send_trade_complete_updates(my_eid, partner_eid, trade_result);

    LOG_INFO(general, "Trade completed between eids {} and {}", my_eid.value, partner_eid.value);
}

// ============================================================
// Cancellation
// ============================================================

void game_handlers::handle_trade_cancel_v2(connection_id conn_id, const network::json_message& msg)
{
    auto* conn = require_in_game(conn_id, msg.seq);
    if (!conn)
        return;

    if (!players_ || !inventory_ || !ws_server_)
        return;

    auto pid = conn->player();
    auto* plr = players_->get_player(pid);
    if (!plr)
        return;

    auto my_eid = entity_id(plr->id.value);
    auto partner_eid = inventory_->get_trade_partner(my_eid);

    // Also clean up any pending invitation for this player
    pending_trade_invites_.erase(plr->ecs_entity.id);

    if (!partner_eid.is_valid())
    {
        // Not in a trade — maybe had a pending invite
        return;
    }

    // Cancel the trade
    inventory_->cancel_trade(my_eid);

    // Notify both
    conn->send_raw(network::make_trade_canceled("player_canceled").dump());

    // Notify partner
    network::ws_connection* partner_conn = nullptr;
    players_->for_each_player([&](player_id p, const player::player& plr_inner)
    {
        if (entity_id(plr_inner.id.value) == partner_eid)
        {
            partner_conn = ws_server_->get_connection_by_player(p);
        }
    });

    if (partner_conn)
    {
        partner_conn->send_raw(network::make_trade_canceled("player_canceled").dump());
    }
}

} // namespace hb::bridge
