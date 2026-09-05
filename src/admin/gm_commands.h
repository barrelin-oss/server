#pragma once

// gm_commands.h
// Game Master commands for testing and administration

#include "core/types.h"
#include "network/json_protocol.h"
#include "world/position.h"

#include <functional>

namespace hb::player
{
class player_system;
}

namespace hb::world
{
class world_subsystem;
}

namespace hb::inventory
{
class inventory_system;
}

namespace hb::magic
{
class magic_system;
}

namespace hb::skill
{
class skill_system;
}

namespace hb
{
class magic_registry;
class scheduler;
class config_system;
} // namespace hb

namespace hb::admin
{

class admin_system;

// Callback to send a protocol message to a specific player
using send_to_player_fn = std::function<void(player_id, const network::json_message&)>;
using broadcast_fn = std::function<void(const network::json_message&)>;
using shutdown_fn = std::function<void(std::string_view reason)>;
// Teleport that also syncs the moved player's client: player_teleport, the destination's
// entities, teleporters, ground items and environment. player_system::execute_teleport alone
// moves the player server-side and leaves their client looking at the old map. Returns an
// error message, empty on success.
using teleport_fn =
    std::function<std::string(player_id, const std::string& dest_map, world::position dest, world::direction dir)>;

// Context for GM commands - provides access to game subsystems
struct gm_command_context
{
    player::player_system* players{nullptr};
    world::world_subsystem* world{nullptr};
    inventory::inventory_system* inventory{nullptr};
    magic::magic_system* magic{nullptr};
    magic_registry* spells{nullptr};
    skill::skill_system* skills{nullptr};
    scheduler* sched{nullptr};
    send_to_player_fn send_to_player;

    // Server management (/reloadconfig, /shutdown). Injected so the admin module does
    // not depend on application.h.
    config_system* config{nullptr};
    broadcast_fn broadcast_all;   // system chat to every authenticated connection
    shutdown_fn request_shutdown; // application::request_shutdown
    teleport_fn teleport_player;  // bridge teleport (client synced); tests leave it empty and get the bare move
};

// Register GM commands with the admin system
// Call this after admin_system is initialized and subsystems are available
void register_gm_commands(admin_system& admin, const gm_command_context& ctx);

} // namespace hb::admin
