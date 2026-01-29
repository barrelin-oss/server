#pragma once

// events.h
// Common event types for the event bus

#include "core/types.h"
#include <string>
#include <chrono>

namespace hb::events {

// Server lifecycle events

struct server_starting_event {
    std::string version;
    std::chrono::system_clock::time_point timestamp;
};

struct server_started_event {
    uint16_t port;
    std::chrono::system_clock::time_point timestamp;
};

struct server_stopping_event {
    std::string reason;
    std::chrono::system_clock::time_point timestamp;
};

struct server_stopped_event {
    std::chrono::system_clock::time_point timestamp;
};

// Configuration events

struct config_loading_event {
    std::string config_path;
};

struct config_loaded_event {
    std::string config_path;
    bool success;
    std::string error_message;  // Empty if success
};

struct config_reloaded_event {
    std::string config_path;
};

// Client events

struct client_connected_event {
    client_id client;
    std::string ip_address;
    uint16_t port;
};

struct client_disconnected_event {
    client_id client;
    std::string reason;
};

struct client_authenticated_event {
    client_id client;
    std::string account_name;
    std::string character_name;
};

// Player events

struct player_spawned_event {
    client_id client;
    map_id map;
    position pos;
};

struct player_moved_event {
    client_id client;
    map_id map;
    position from;
    position to;
};

struct player_died_event {
    client_id client;
    client_id killer;  // Can be invalid if killed by NPC or environment
    npc_id killer_npc; // Can be invalid if killed by player
};

struct player_respawned_event {
    client_id client;
    map_id map;
    position pos;
};

struct player_level_up_event {
    client_id client;
    int old_level;
    int new_level;
};

// Combat events

struct damage_dealt_event {
    client_id attacker;       // Can be invalid if attacker is NPC
    npc_id attacker_npc;      // Can be invalid if attacker is player
    client_id target_client;  // Can be invalid if target is NPC
    npc_id target_npc;        // Can be invalid if target is player
    int damage;
    bool critical;
};

struct entity_killed_event {
    client_id killer_client;
    npc_id killer_npc;
    client_id killed_client;
    npc_id killed_npc;
    int experience_reward;
};

// Item events

struct item_picked_up_event {
    client_id client;
    item_id item;
    map_id map;
    position pos;
};

struct item_dropped_event {
    client_id client;
    item_id item;
    map_id map;
    position pos;
};

struct item_equipped_event {
    client_id client;
    item_id item;
    int slot;
};

struct item_unequipped_event {
    client_id client;
    item_id item;
    int slot;
};

// NPC events

struct npc_spawned_event {
    npc_id npc;
    map_id map;
    position pos;
    int npc_type;
};

struct npc_died_event {
    npc_id npc;
    client_id killer;
    map_id map;
    position pos;
};

// Chat events

struct chat_message_event {
    client_id sender;
    std::string message;
    int chat_type;  // 0=normal, 1=shout, 2=guild, etc.
};

struct whisper_event {
    client_id sender;
    client_id recipient;
    std::string message;
};

// Admin events

struct admin_command_event {
    client_id admin;
    std::string command;
    std::string args;
};

// Timer/tick events

struct game_tick_event {
    uint64_t tick_count;
    float delta_time;
};

struct second_tick_event {
    uint64_t seconds_elapsed;
};

struct minute_tick_event {
    uint64_t minutes_elapsed;
};

}  // namespace hb::events
