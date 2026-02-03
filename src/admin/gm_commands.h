#pragma once

// gm_commands.h
// Game Master commands for testing and administration

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

// Context for GM commands - provides access to game subsystems
struct gm_command_context {
    player::player_system* players{nullptr};
    world::world_subsystem* world{nullptr};
    inventory::inventory_system* inventory{nullptr};
};

// Register GM commands with the admin system
// Call this after admin_system is initialized and subsystems are available
void register_gm_commands(admin_system& admin, const gm_command_context& ctx);

}  // namespace hb::admin
