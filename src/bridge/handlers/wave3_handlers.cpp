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
    // Full validation would check map bounds, walkability, speed hacks, etc.
    if (x < 0 || y < 0) {
        LOG_WARN(proto_bridge, "Invalid move position ({},{}) for player {}",
                 x, y, ctx.player.value);
        return handle_result::error;
    }

    // Update player position
    auto facing = static_cast<world::direction>(dir);
    player_sys->set_position(ctx.player, player->current_map, world::position{x, y}, facing);

    // Acknowledge the move
    send_motion_response(ctx, run_mode ? motion_type::run : motion_type::move, x, y, dir);

    return handle_result::handled;
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

    LOG_DEBUG(proto_bridge, "Pickup item: player={}", ctx.player.value);

    auto* inv_sys = subsystems().get<inventory::inventory_system>();
    if (!inv_sys) {
        return handle_result::not_handled;
    }

    // Check if inventory has space
    auto entity = entity_id{ctx.player.value};
    if (inv_sys->is_full(entity)) {
        send_notify(ctx, protocol::notify_type::cannot_carry_more_item, nullptr);
        return handle_result::handled;
    }

    // Item pickup would find nearest item and add to inventory
    // For now, return not_handled to let legacy process it

    return handle_result::not_handled;
}

}  // namespace hb::bridge::wave3
