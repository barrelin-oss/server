#pragma once

// gm_commands.h
// Game Master commands for testing and administration

#include "core/types.h"
#include "network/json_protocol.h"

#include <functional>

namespace hb::player {
    class player_system;
}

namespace hb::world {
    class world_subsystem;
}

namespace hb::inventory {
    class inventory_system;
}

namespace hb::admin {

class admin_system;

// Callback to send a protocol message to a specific player
using send_to_player_fn = std::function<void(player_id, const network::json_message&)>;

// Context for GM commands - provides access to game subsystems
struct gm_command_context {
    player::player_system* players{nullptr};
    world::world_subsystem* world{nullptr};
    inventory::inventory_system* inventory{nullptr};
    send_to_player_fn send_to_player;
};

// Register GM commands with the admin system
// Call this after admin_system is initialized and subsystems are available
void register_gm_commands(admin_system& admin, const gm_command_context& ctx);

}  // namespace hb::admin
