#pragma once

// entity_builders.h
// Helper functions to build visible_entity_msg for player and NPC spawns,
// and shared functions for sending entity data on login/teleport.

#include "network/json_protocol.h"
#include "world/position.h"

namespace hb::player
{
struct player;
class player_system;
} // namespace hb::player

namespace hb::npc
{
struct npc;
class npc_system;
} // namespace hb::npc

namespace hb::item
{
class item_system;
}

namespace hb::world
{
class world_subsystem;
class map;
} // namespace hb::world

namespace hb::network
{
class ws_connection;
}

namespace hb
{
class item_registry;
}

namespace hb::effect
{
class effect_system;
}

namespace hb::bridge
{

// Build a visible_entity_msg for a player entity.
// hostility is relative to the viewing player (e.g. "friendly", "enemy", "neutral").
auto build_player_spawn(const player::player& plr,
                        std::string_view hostility,
                        const item::item_system* items,
                        const item_registry* item_reg,
                        const effect::effect_system* effects) -> network::visible_entity_msg;

// Build a visible_entity_msg for an NPC entity.
auto build_npc_spawn(const npc::npc& n,
                     std::string_view hostility,
                     bool is_dead = false) -> network::visible_entity_msg;

// Send entity_spawn per nearby player + npc_spawn per nearby NPC (alive + dead corpses)
void send_visible_entity_spawns(network::ws_connection* conn,
                                player_id viewer,
                                map_id map,
                                const world::position& pos,
                                player::player_system* players,
                                npc::npc_system* npc_sys,
                                const world::world_subsystem* world,
                                const item::item_system* items,
                                const item_registry* item_reg,
                                const effect::effect_system* effects);

// Send ground_item_spawn per visible ground item (top per tile, with display name + attributes)
void send_visible_ground_items(network::ws_connection* conn,
                               map_id map,
                               const world::position& pos,
                               int radius_x,
                               int radius_y,
                               const world::world_subsystem* world,
                               const item::item_system* items,
                               const item_registry* item_reg);

// Send dynamic_object_spawn per visible ground field (spikes, ice storms, poison clouds)
void send_visible_dynamic_objects(network::ws_connection* conn,
                                  map_id map,
                                  const world::position& pos,
                                  int radius_x,
                                  int radius_y);

// Send map_teleporters for a map
void send_map_teleporters(network::ws_connection* conn, const world::map& map);

} // namespace hb::bridge
