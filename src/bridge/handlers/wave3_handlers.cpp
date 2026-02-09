// wave3_handlers.cpp
// Wave 3 Migration: Player state message handlers

#include "bridge/handlers/wave3_handlers.h"
#include "bridge/handler_registry.h"
#include "bridge/message_router.h"
#include "core/subsystem.h"
#include "core/logger.h"
#include "player/player_system.h"
#include "inventory/inventory_system.h"
#include "item/item_system.h"
#include "world/world_subsystem.h"
#include "protocol/message_reader.h"
#include "protocol/message_writer.h"
#include "network/network_subsystem.h"

#include <spdlog/fmt/fmt.h>
#include <vector>

namespace hb::bridge::wave3 {

namespace {
    // Track which handlers we've registered for cleanup
    std::vector<protocol::message_id> registered_handlers;

    constexpr auto wave3_subsystem = "wave3_player";

    // Send a notify message to the player
    void send_notify(const handler_context& ctx, protocol::notify_type type,
                     std::function<void(protocol::message_writer&)> write_data) {
        send_response(ctx, protocol::message_id::notify, [type, &write_data](protocol::message_writer& writer) {
            writer.write_u16(static_cast<uint16_t>(type));
            if (write_data) {
                write_data(writer);
            }
        });
    }

    // Send a motion response back
    void send_motion_response(const handler_context& ctx, motion_type type,
                              int16_t x, int16_t y, int8_t dir) {
        send_response(ctx, protocol::message_id::response_motion, [=](protocol::message_writer& writer) {
            writer.write_u8(static_cast<uint8_t>(type));
            writer.write_i16(x);
            writer.write_i16(y);
            writer.write_i8(dir);
        });
    }
}

// ========== Handler Registration ==========

auto register_wave3_handlers() -> size_t {
    LOG_INFO(proto_bridge, "Registering Wave 3 (player state) handlers...");

    // Motion command handler
    handlers(wave3_subsystem)
        .on_player(protocol::message_id::command_motion,
            make_simple_handler([](const handler_context& ctx, protocol::message_reader& reader) {
                return handle_motion_command(ctx);
            }));

    registered_handlers.push_back(protocol::message_id::command_motion);

    // Item use is handled via command_common with use_item subtype
    // Drop/pickup would be similar

    LOG_INFO(proto_bridge, "Wave 3 handlers registered: {} handlers", registered_handlers.size());

    return registered_handlers.size();
}

void unregister_wave3_handlers() {
    LOG_INFO(proto_bridge, "Unregistering Wave 3 handlers...");

    for (auto msg_id : registered_handlers) {
        router().unregister_handler(msg_id);
    }
    registered_handlers.clear();
}

// ========== Motion Command Handler ==========

auto handle_motion_command(const handler_context& ctx) -> handle_result {
    protocol::message_reader reader{ctx.raw_data};
    reader.skip(4);  // message_id

    if (reader.remaining() < 6) {
        LOG_WARN(proto_bridge, "Motion command: insufficient data (conn={})", ctx.connection.value);
        return handle_result::not_handled;
    }

    // Parse motion data
    auto type = static_cast<motion_type>(reader.read_u8());
    auto x = reader.read_i16();
    auto y = reader.read_i16();
    auto dir = reader.read_i8();

    LOG_DEBUG(proto_bridge, "Motion command: type={} pos=({},{}) dir={} (player={})",
              static_cast<int>(type), x, y, static_cast<int>(dir), ctx.player.value);

    switch (type) {
        case motion_type::move:
        case motion_type::run: {
            uint8_t run_mode = (type == motion_type::run) ? 1 : 0;
            return handle_motion_move(ctx, x, y, dir, run_mode);
        }

        case motion_type::stop:
            return handle_motion_stop(ctx, x, y, dir);

        case motion_type::attack:
        case motion_type::attack_move:
        case motion_type::magic:
            // Combat-related motion - let Wave 4 handle it
            return handle_result::not_handled;

        case motion_type::get_item:
            // Pickup item
            return handle_pickup_item(ctx);

        case motion_type::dead:
        case motion_type::dying:
            // Death animations - let legacy handle for now
            return handle_result::not_handled;

        default:
            LOG_DEBUG(proto_bridge, "Unknown motion type: {}", static_cast<int>(type));
            return handle_result::not_handled;
    }
}

auto handle_motion_move(const handler_context& ctx, int16_t x, int16_t y,
                        int8_t dir, uint8_t run_mode) -> handle_result {
    LOG_TRACE(proto_bridge, "Move: player={} to ({},{}) dir={} run={}",
              ctx.player.value, x, y, static_cast<int>(dir), run_mode);

    auto* player_sys = subsystems().get<player::player_system>();
    if (!player_sys) {
        LOG_WARN(proto_bridge, "Player system not available for move");
        return handle_result::not_handled;
    }

    auto* player = player_sys->get_player(ctx.player);
    if (!player) {
        LOG_WARN(proto_bridge, "Player {} not found for move", ctx.player.value);
        return handle_result::not_handled;
    }

    // Validate movement (basic bounds check)
    if (x < 0 || y < 0) {
        LOG_WARN(proto_bridge, "Invalid move position ({},{}) for player {}",
                 x, y, ctx.player.value);
        return handle_result::error;
    }

    // Attempt move with full collision detection
    auto facing = static_cast<world::direction>(dir);
    auto move_info = player_sys->try_move(ctx.player, world::position{x, y}, facing);

    using move_result = player::player_system::move_result;

    switch (move_info.result) {
        case move_result::success:
            // Movement succeeded, acknowledge it
            send_motion_response(ctx, run_mode ? motion_type::run : motion_type::move, x, y, dir);
            return handle_result::handled;

        case move_result::teleport: {
            // Player stepped on teleport - send acknowledge first
            send_motion_response(ctx, run_mode ? motion_type::run : motion_type::move, x, y, dir);

            // Then send teleport notification
            // The client will need to handle map change separately
            LOG_INFO(proto_bridge, "Player {} teleporting to {} at ({},{})",
                ctx.player.value, move_info.teleport_dest_map,
                move_info.teleport_dest_pos.x, move_info.teleport_dest_pos.y);

            // TODO: Handle cross-map teleport via session/map change system
            return handle_result::handled;
        }

        case move_result::blocked_terrain:
            LOG_DEBUG(proto_bridge, "Move blocked (terrain) for player {} at ({},{})",
                ctx.player.value, x, y);
            // Send player back to original position
            send_motion_response(ctx, motion_type::stop,
                player->pos.x, player->pos.y, static_cast<int8_t>(player->facing));
            return handle_result::handled;

        case move_result::blocked_occupied:
            LOG_DEBUG(proto_bridge, "Move blocked (occupied) for player {} at ({},{})",
                ctx.player.value, x, y);
            send_motion_response(ctx, motion_type::stop,
                player->pos.x, player->pos.y, static_cast<int8_t>(player->facing));
            return handle_result::handled;

        case move_result::blocked_out_of_bounds:
            LOG_DEBUG(proto_bridge, "Move blocked (out of bounds) for player {} at ({},{})",
                ctx.player.value, x, y);
            send_motion_response(ctx, motion_type::stop,
                player->pos.x, player->pos.y, static_cast<int8_t>(player->facing));
            return handle_result::handled;

        case move_result::blocked_status:
            LOG_DEBUG(proto_bridge, "Move blocked (status effect) for player {}",
                ctx.player.value);
            // Send stop at current position - player is frozen/stunned/paralyzed
            send_motion_response(ctx, motion_type::stop,
                player->pos.x, player->pos.y, static_cast<int8_t>(player->facing));
            return handle_result::handled;

        case move_result::blocked_dead:
            LOG_DEBUG(proto_bridge, "Move blocked (dead) for player {}", ctx.player.value);
            return handle_result::handled;

        case move_result::invalid_map:
            LOG_WARN(proto_bridge, "Move failed (invalid map) for player {}", ctx.player.value);
            return handle_result::error;

        case move_result::invalid_player:
        default:
            return handle_result::not_handled;
    }
}

auto handle_motion_stop(const handler_context& ctx, int16_t x, int16_t y,
                        int8_t dir) -> handle_result {
    LOG_TRACE(proto_bridge, "Stop: player={} at ({},{}) dir={}",
              ctx.player.value, x, y, static_cast<int>(dir));

    auto* player_sys = subsystems().get<player::player_system>();
    if (!player_sys) {
        return handle_result::not_handled;
    }

    auto* player = player_sys->get_player(ctx.player);
    if (!player) {
        return handle_result::not_handled;
    }

    // Update facing direction
    auto facing = static_cast<world::direction>(dir);
    player_sys->set_facing(ctx.player, facing);

    // Acknowledge the stop
    send_motion_response(ctx, motion_type::stop, x, y, dir);

    return handle_result::handled;
}

// ========== Item Handlers ==========

auto handle_use_item(const handler_context& ctx) -> handle_result {
    protocol::message_reader reader{ctx.raw_data};
    reader.skip(4);  // message_id
    reader.skip(2);  // subtype

    if (reader.remaining() < 2) {
        return handle_result::error;
    }

    auto slot = reader.read_i16();

    LOG_DEBUG(proto_bridge, "Use item: player={} slot={}", ctx.player.value, slot);

    auto* item_sys = subsystems().get<item::item_system>();
    if (!item_sys) {
        return handle_result::not_handled;
    }

    // Item usage would be processed here
    // For now, return not_handled to let legacy process it
    // Full implementation would call item_sys->use_item()

    return handle_result::not_handled;
}

auto handle_drop_item(const handler_context& ctx) -> handle_result {
    protocol::message_reader reader{ctx.raw_data};
    reader.skip(4);  // message_id
    reader.skip(2);  // subtype

    if (reader.remaining() < 4) {
        return handle_result::error;
    }

    auto slot = reader.read_i16();
    auto count = reader.read_i16();

    LOG_DEBUG(proto_bridge, "Drop item: player={} slot={} count={}",
              ctx.player.value, slot, count);

    auto* inv_sys = subsystems().get<inventory::inventory_system>();
    if (!inv_sys) {
        return handle_result::not_handled;
    }

    // Item dropping would be processed here
    // Full implementation would remove from inventory and spawn ground item

    return handle_result::not_handled;
}

auto handle_pickup_item(const handler_context& ctx) -> handle_result {
    protocol::message_reader reader{ctx.raw_data};
    reader.skip(4);  // message_id

    LOG_DEBUG(proto_bridge, "Pickup item request: player={}", ctx.player.value);

    // Get subsystems
    auto* player_sys = subsystems().get<player::player_system>();
    auto* inv_sys = subsystems().get<inventory::inventory_system>();
    auto* world = subsystems().get<world::world_subsystem>();
    auto* item_sys = subsystems().get<item::item_system>();

    if (!player_sys || !inv_sys || !world || !item_sys) {
        return handle_result::not_handled;
    }

    // Get player data
    auto* player_ptr = player_sys->get_player(ctx.player);
    if (!player_ptr) {
        LOG_WARN(proto_bridge, "Pickup item: player {} not found", ctx.player.value);
        return handle_result::handled;
    }

    auto player_entity = entity_id{ctx.player.value};
    auto& player = *player_ptr;

    // Check if there are any ground items at player's position
    if (!world->has_ground_items(player.current_map, player.pos)) {
        LOG_DEBUG(proto_bridge, "No items on ground at player {} position", ctx.player.value);
        // Just return handled - no error message needed
        return handle_result::handled;
    }

    // Check if inventory has space
    if (inv_sys->is_full(player_entity)) {
        send_notify(ctx, protocol::notify_type::cannot_carry_more_item, nullptr);
        return handle_result::handled;
    }

    // Remove top-most item from ground
    auto item_id_opt = world->remove_top_ground_item(player.current_map, player.pos);
    if (!item_id_opt.has_value()) {
        LOG_DEBUG(proto_bridge, "Failed to remove item from ground (race condition?)");
        return handle_result::handled;
    }

    auto picked_item_id = item_id_opt.value();

    // Add item to inventory
    auto result = inv_sys->add_item(player_entity, picked_item_id);
    if (result != inventory::inventory_result::success) {
        // Failed to add to inventory - put item back on ground
        world->add_ground_item(player.current_map, player.pos, picked_item_id);
        send_notify(ctx, protocol::notify_type::cannot_carry_more_item, nullptr);
        LOG_WARN(proto_bridge, "Failed to add item {} to player {} inventory: result={}",
            picked_item_id.value, ctx.player.value, static_cast<int>(result));
        return handle_result::handled;
    }

    // Success! Send item_obtained notification to player
    send_notify(ctx, protocol::notify_type::item_obtained, [&](protocol::message_writer& writer) {
        writer.write_u32(picked_item_id.value);
        // TODO: Write item details (name, count, etc.) if needed
    });

    // Broadcast item removal to all nearby players (except the picker)
    broadcast_item_removal(ctx, player, picked_item_id);

    LOG_INFO(proto_bridge, "Player {} picked up item {} at ({}, {}) on map {}",
        ctx.player.value, picked_item_id.value, player.pos.x, player.pos.y,
        static_cast<int>(player.current_map.value));

    return handle_result::handled;
}

// Broadcast ground item removal to nearby players
void broadcast_item_removal(const handler_context& ctx, const player::player& picker,
                            item_id removed_item)
{
    auto* player_sys = subsystems().get<player::player_system>();
    auto* network = subsystems().get<network::network_subsystem>();

    if (!player_sys || !network) {
        return;
    }

    // Get players who can see this position
    auto nearby = player_sys->get_players_who_can_see(picker.current_map, picker.pos);

    // Build del_dynamic_object notification
    protocol::message_writer notify_msg;
    notify_msg.write_u32(static_cast<uint32_t>(protocol::message_id::notify));
    notify_msg.write_u16(static_cast<uint16_t>(protocol::notify_type::del_dynamic_object));
    notify_msg.write_u16(1);  // object_type: 1 = item
    notify_msg.write_u32(removed_item.value);  // object_id (item ID)
    notify_msg.write_i16(picker.pos.x);  // map X
    notify_msg.write_i16(picker.pos.y);  // map Y

    // Send to all nearby players except the picker
    for (auto other_id : nearby) {
        if (other_id == ctx.player) {
            continue;  // Skip the player who picked up the item
        }

        auto* other = player_sys->get_player(other_id);
        if (!other || !other->connection.is_valid()) {
            continue;
        }

        // Send the notification
        network->send(other->connection, notify_msg.data());
    }

    LOG_DEBUG(proto_bridge, "Broadcasted item {} removal to {} nearby players",
        removed_item.value, nearby.size() - 1);
}

}  // namespace hb::bridge::wave3
