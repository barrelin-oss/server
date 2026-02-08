#pragma once

// json_protocol.h
// JSON-based message protocol for WebSocket communication

#include "core/types.h"
#include "core/result.h"
#include "auth/account.h"

#include <nlohmann/json.hpp>
#include <algorithm>
#include <string>
#include <string_view>
#include <optional>
#include <chrono>
#include <variant>

namespace hb::network {

// Attack type enum
enum class attack_type : uint8_t {
    regular = 0,  // Normal melee attack
    dash = 1,     // Dash attack (requires 100% skill, 1 tile gap)
    ranged = 2    // Ranged attack (bow/crossbow)
};

// Projectile type for ranged attack broadcasts
enum class projectile_type : uint8_t {
    none = 0,
    arrow = 1,
    poison_arrow = 2,
};

// Target type enum
enum class target_type : uint8_t {
    none = 0,
    player = 1,
    npc = 2,
    ground = 3,   // For ground-targeted spells/skills
    item = 4      // For items on ground
};

// Message types for the JSON protocol
enum class json_message_type {
    // System
    error,
    ping,
    pong,

    // Authentication (Wave 0)
    login_request,
    login_response,
    logout_request,
    logout_response,
    create_account_request,
    create_account_response,

    // Character selection
    get_characters_request,
    get_characters_response,
    create_character_request,
    create_character_response,
    delete_character_request,
    delete_character_response,
    enter_game_request,
    enter_game_response,

    // Game state data (sent after enter_game)
    character_data,
    inventory_data,
    equipment_data,
    skills_data,
    world_init,
    entity_spawn,
    entity_despawn,

    // In-game movement and position
    player_move_request,
    player_move_response,
    player_stop_request,
    player_stop_response,
    player_position_update,  // Broadcast to nearby players

    // Combat
    player_attack_request,
    player_attack_response,
    combat_attack_broadcast,  // Broadcast attack to nearby players
    entity_hp_update,         // HP changed (damage or heal)
    entity_death,             // Entity died
    combat_effect,            // Visual combat/spell effect broadcast (damage, heal, miss, etc.)

    // Actions
    player_magic_request,
    player_magic_response,
    player_skill_request,
    player_skill_response,
    player_pickup_request,
    player_pickup_response,
    player_interact_request,
    player_interact_response,

    // Chat
    chat_message,           // Chat message (local, shout, guild, party, whisper, etc.)
    chat_message_broadcast, // Outbound chat broadcast to recipients

    // Commands (separate from chat - client-side /command construct)
    command_request,        // Client sends a command
    command_response,       // Server response to command

    // Teleportation
    map_teleporters,        // Full teleporter list for a map
    teleporter_update,      // Live add/remove/modify teleporter
    player_teleport,        // Sent to player being teleported

    // View/Resolution
    set_view_range,         // Client updates visibility radius
    set_render_mode,        // Server tells client which rendering mode to use
    view_range_update,      // Server tells client its effective visibility range

    // NPC messages (server -> client)
    npc_spawn,              // NPC appears in view
    npc_despawn,            // NPC leaves view
    npc_move,               // NPC moved
    npc_attack,             // NPC attacked something
    npc_death,              // NPC died

    // Ground item messages (server -> client)
    ground_item_spawn,      // Item appeared on ground
    ground_item_removed,    // Item picked up from ground

    // Player state updates (server -> client)
    player_death_info,      // Death details sent to dead player
    hunger_update,          // Player hunger level changed

    // Entity info (client -> server -> client)
    entity_info_request,    // Request info about an entity
    entity_info_response,   // Entity info response

    // Equipment
    player_equip_request,
    player_equip_response,
    player_unequip_request,
    player_unequip_response,
    equipment_change_broadcast,
    stat_update,
    spell_list_update,

    // NPC interaction - shops
    shop_buy_request,
    shop_buy_response,
    shop_sell_request,
    shop_sell_response,
    shop_sell_confirm_request,
    shop_sell_confirm_response,
    shop_repair_request,
    shop_repair_response,
    shop_repair_confirm_request,
    shop_repair_confirm_response,

    // NPC interaction - banking
    bank_deposit_request,
    bank_deposit_response,
    bank_withdraw_request,
    bank_withdraw_response,

    // NPC interaction - dialog
    dialog_choice_request,
    dialog_choice_response,

    // Unknown/invalid
    unknown
};

// Convert message type to string
[[nodiscard]] constexpr auto to_string(json_message_type type) -> std::string_view {
    switch (type) {
        case json_message_type::error: return "error";
        case json_message_type::ping: return "ping";
        case json_message_type::pong: return "pong";
        case json_message_type::login_request: return "login_request";
        case json_message_type::login_response: return "login_response";
        case json_message_type::logout_request: return "logout_request";
        case json_message_type::logout_response: return "logout_response";
        case json_message_type::create_account_request: return "create_account_request";
        case json_message_type::create_account_response: return "create_account_response";
        case json_message_type::get_characters_request: return "get_characters_request";
        case json_message_type::get_characters_response: return "get_characters_response";
        case json_message_type::create_character_request: return "create_character_request";
        case json_message_type::create_character_response: return "create_character_response";
        case json_message_type::delete_character_request: return "delete_character_request";
        case json_message_type::delete_character_response: return "delete_character_response";
        case json_message_type::enter_game_request: return "enter_game_request";
        case json_message_type::enter_game_response: return "enter_game_response";
        case json_message_type::character_data: return "character_data";
        case json_message_type::inventory_data: return "inventory_data";
        case json_message_type::equipment_data: return "equipment_data";
        case json_message_type::skills_data: return "skills_data";
        case json_message_type::world_init: return "world_init";
        case json_message_type::entity_spawn: return "entity_spawn";
        case json_message_type::entity_despawn: return "entity_despawn";
        case json_message_type::player_move_request: return "player_move_request";
        case json_message_type::player_move_response: return "player_move_response";
        case json_message_type::player_stop_request: return "player_stop_request";
        case json_message_type::player_stop_response: return "player_stop_response";
        case json_message_type::player_position_update: return "player_position_update";
        case json_message_type::player_attack_request: return "player_attack_request";
        case json_message_type::player_attack_response: return "player_attack_response";
        case json_message_type::combat_attack_broadcast: return "combat_attack_broadcast";
        case json_message_type::entity_hp_update: return "entity_hp_update";
        case json_message_type::entity_death: return "entity_death";
        case json_message_type::combat_effect: return "combat_effect";
        case json_message_type::player_magic_request: return "player_magic_request";
        case json_message_type::player_magic_response: return "player_magic_response";
        case json_message_type::player_skill_request: return "player_skill_request";
        case json_message_type::player_skill_response: return "player_skill_response";
        case json_message_type::player_pickup_request: return "player_pickup_request";
        case json_message_type::player_pickup_response: return "player_pickup_response";
        case json_message_type::player_interact_request: return "player_interact_request";
        case json_message_type::player_interact_response: return "player_interact_response";
        case json_message_type::chat_message: return "chat_message";
        case json_message_type::chat_message_broadcast: return "chat_message_broadcast";
        case json_message_type::command_request: return "command_request";
        case json_message_type::command_response: return "command_response";
        case json_message_type::map_teleporters: return "map_teleporters";
        case json_message_type::teleporter_update: return "teleporter_update";
        case json_message_type::player_teleport: return "player_teleport";
        case json_message_type::set_view_range: return "set_view_range";
        case json_message_type::set_render_mode: return "set_render_mode";
        case json_message_type::view_range_update: return "view_range_update";
        case json_message_type::npc_spawn: return "npc_spawn";
        case json_message_type::npc_despawn: return "npc_despawn";
        case json_message_type::npc_move: return "npc_move";
        case json_message_type::npc_attack: return "npc_attack";
        case json_message_type::npc_death: return "npc_death";
        case json_message_type::ground_item_spawn: return "ground_item_spawn";
        case json_message_type::ground_item_removed: return "ground_item_removed";
        case json_message_type::player_death_info: return "player_death_info";
        case json_message_type::hunger_update: return "hunger_update";
        case json_message_type::entity_info_request: return "entity_info_request";
        case json_message_type::entity_info_response: return "entity_info_response";
        case json_message_type::player_equip_request: return "player_equip_request";
        case json_message_type::player_equip_response: return "player_equip_response";
        case json_message_type::player_unequip_request: return "player_unequip_request";
        case json_message_type::player_unequip_response: return "player_unequip_response";
        case json_message_type::equipment_change_broadcast: return "equipment_change_broadcast";
        case json_message_type::stat_update: return "stat_update";
        case json_message_type::spell_list_update: return "spell_list_update";
        case json_message_type::shop_buy_request: return "shop_buy_request";
        case json_message_type::shop_buy_response: return "shop_buy_response";
        case json_message_type::shop_sell_request: return "shop_sell_request";
        case json_message_type::shop_sell_response: return "shop_sell_response";
        case json_message_type::shop_sell_confirm_request: return "shop_sell_confirm_request";
        case json_message_type::shop_sell_confirm_response: return "shop_sell_confirm_response";
        case json_message_type::shop_repair_request: return "shop_repair_request";
        case json_message_type::shop_repair_response: return "shop_repair_response";
        case json_message_type::shop_repair_confirm_request: return "shop_repair_confirm_request";
        case json_message_type::shop_repair_confirm_response: return "shop_repair_confirm_response";
        case json_message_type::bank_deposit_request: return "bank_deposit_request";
        case json_message_type::bank_deposit_response: return "bank_deposit_response";
        case json_message_type::bank_withdraw_request: return "bank_withdraw_request";
        case json_message_type::bank_withdraw_response: return "bank_withdraw_response";
        case json_message_type::dialog_choice_request: return "dialog_choice_request";
        case json_message_type::dialog_choice_response: return "dialog_choice_response";
        default: return "unknown";
    }
}

// Parse message type from string
[[nodiscard]] auto parse_message_type(std::string_view type_str) -> json_message_type;

// Base JSON message structure
struct json_message {
    json_message_type type{json_message_type::unknown};
    std::string raw_type;  // Original type string from JSON (useful for debugging unknown types)
    uint32_t seq{0};  // Sequence number for request/response matching
    nlohmann::json data;

    [[nodiscard]] auto to_json() const -> nlohmann::json;
    [[nodiscard]] static auto from_json(const nlohmann::json& j) -> result<json_message, std::string>;
    [[nodiscard]] static auto parse(std::string_view json_str) -> result<json_message, std::string>;
};

// Request message structures

struct login_request_data {
    std::string username;
    std::string password;       // normal login (empty when using forum_token)
    std::string forum_token;    // forum token-based auto-login (empty when using password)

    [[nodiscard]] static auto from_json(const nlohmann::json& j) -> result<login_request_data, std::string>;
};

struct create_account_request_data {
    std::string username;
    std::string password;

    [[nodiscard]] static auto from_json(const nlohmann::json& j) -> result<create_account_request_data, std::string>;
};

struct create_character_request_data {
    std::string name;
    int16_t class_type{0};
    int16_t nation{0};
    int16_t gender{1};       // 1 = male, 2 = female
    int16_t hair_style{0};
    int16_t hair_color{0};
    int16_t skin_color{0};
    int16_t underwear_color{0};
    std::optional<int16_t> strength;
    std::optional<int16_t> dexterity;
    std::optional<int16_t> vitality;
    std::optional<int16_t> intelligence;
    std::optional<int16_t> magic;
    std::optional<int16_t> charisma;

    [[nodiscard]] static auto from_json(const nlohmann::json& j) -> result<create_character_request_data, std::string>;
    [[nodiscard]] auto to_create_info() const -> auth::character_create_info;
};

struct delete_character_request_data {
    uint32_t character_id{0};

    [[nodiscard]] static auto from_json(const nlohmann::json& j) -> result<delete_character_request_data, std::string>;
};

struct enter_game_request_data {
    uint32_t character_id{0};
    bool force_disconnect{false};  // If true, disconnect existing session for this account
    int16_t screen_width{640};     // Client effective viewport width for visibility calculation
    int16_t screen_height{480};    // Client effective viewport height for visibility calculation

    [[nodiscard]] static auto from_json(const nlohmann::json& j) -> result<enter_game_request_data, std::string>;
};

// Set view range request from client (when resolution or view mode changes)
struct set_view_range_request_data {
    int16_t screen_width{640};     // Effective viewport width
    int16_t screen_height{480};    // Effective viewport height

    [[nodiscard]] static auto from_json(const nlohmann::json& j) -> result<set_view_range_request_data, std::string>;
};

// Movement request from client
struct player_move_request_data {
    int16_t x{0};          // Current position for validation
    int16_t y{0};
    int16_t direction{0};  // Direction to move (0-7)
    bool is_running{false}; // True if running (moves 2 tiles), false if walking (1 tile)
    uint64_t timestamp{0}; // Client timestamp in ms
    std::optional<int16_t> dest_x;  // Mouse destination tile X (optional)
    std::optional<int16_t> dest_y;  // Mouse destination tile Y (optional)

    [[nodiscard]] static auto from_json(const nlohmann::json& j) -> result<player_move_request_data, std::string>;
};

// Stop request from client
struct player_stop_request_data {
    int16_t x{0};          // Position where stopped
    int16_t y{0};
    std::optional<int16_t> direction;  // Direction to face (0-7), optional
    uint64_t timestamp{0}; // Client timestamp in ms

    [[nodiscard]] static auto from_json(const nlohmann::json& j) -> result<player_stop_request_data, std::string>;
};

// Attack request from client
struct player_attack_request_data {
    int16_t x{0};              // Current position for validation
    int16_t y{0};
    int16_t direction{0};      // Direction facing
    attack_type type{attack_type::regular};
    target_type tgt_type{target_type::none};
    uint32_t target_id{0};     // Target entity ID
    uint64_t timestamp{0};     // Client timestamp in ms

    [[nodiscard]] static auto from_json(const nlohmann::json& j) -> result<player_attack_request_data, std::string>;
};

// Magic cast request from client
struct player_magic_request_data {
    int16_t x{0};              // Current position for validation
    int16_t y{0};
    int16_t direction{0};      // Direction facing
    uint32_t spell_id{0};      // Spell to cast
    target_type tgt_type{target_type::none};
    uint32_t target_id{0};     // Target entity ID (for targeted spells)
    int16_t target_x{0};       // Target location (for ground-targeted spells)
    int16_t target_y{0};
    uint64_t timestamp{0};     // Client timestamp in ms

    [[nodiscard]] static auto from_json(const nlohmann::json& j) -> result<player_magic_request_data, std::string>;
};

// Skill use request from client
struct player_skill_request_data {
    int16_t x{0};              // Current position for validation
    int16_t y{0};
    int16_t direction{0};      // Direction facing
    uint32_t skill_id{0};      // Skill to use
    target_type tgt_type{target_type::none};
    uint32_t target_id{0};     // Target entity ID (if applicable)
    uint64_t timestamp{0};     // Client timestamp in ms

    [[nodiscard]] static auto from_json(const nlohmann::json& j) -> result<player_skill_request_data, std::string>;
};

// Pickup item request from client
struct player_pickup_request_data {
    int16_t x{0};              // Current position for validation
    int16_t y{0};
    uint32_t item_id{0};       // Ground item ID to pick up
    uint64_t timestamp{0};     // Client timestamp in ms

    [[nodiscard]] static auto from_json(const nlohmann::json& j) -> result<player_pickup_request_data, std::string>;
};

// Interact request from client (NPC dialog, objects, etc.)
struct player_interact_request_data {
    int16_t x{0};              // Current position for validation
    int16_t y{0};
    target_type tgt_type{target_type::none};
    uint32_t target_id{0};     // Target NPC or object ID
    uint64_t timestamp{0};     // Client timestamp in ms

    [[nodiscard]] static auto from_json(const nlohmann::json& j) -> result<player_interact_request_data, std::string>;
};

// Chat channel type for JSON protocol
enum class chat_channel_type : uint8_t {
    local = 0,          // Nearby players (default, no prefix)
    shout = 1,          // Server-wide (! prefix)
    guild = 2,          // Guild members (@ prefix)
    party = 3,          // Party members ($ prefix)
    whisper = 4,        // Private message (recipient name or # prefix)
    global = 5,         // Global channel
    trade = 6,          // Trade channel
    faction = 7,        // Faction channel (Aresden/Elvine)
    system = 8,         // System messages (server-generated only)
};

// Chat message request from client
// Client can send either:
//   - Raw message with prefix: "!Hello everyone" -> shout
//   - Explicit channel: {"channel": "guild", "content": "Hello guild"}
struct chat_message_request_data {
    std::string content;                        // Message content (may include prefix)
    std::optional<std::string> channel;         // Explicit channel override
    std::optional<std::string> recipient_name;  // For whispers
    uint64_t timestamp{0};                      // Client timestamp in ms

    [[nodiscard]] static auto from_json(const nlohmann::json& j) -> result<chat_message_request_data, std::string>;
};

// Chat broadcast data (sent to recipients)
struct chat_message_broadcast_data {
    std::string channel;            // "local", "shout", "guild", "party", "whisper", etc.
    uint32_t sender_id{0};          // Sender player ID (0 for system)
    std::string sender_name;        // Sender display name
    std::string content;            // Message content
    std::vector<std::string> flags; // "emote", "censored", "system", "gm"
    std::string timestamp;          // ISO 8601 timestamp
    std::optional<std::string> recipient_name;  // For whisper (recipient sees their own name)

    [[nodiscard]] auto to_json() const -> nlohmann::json;
};

// Command request from client (for /commands)
// Commands are sent as structured data, not parsed from chat
struct command_request_data {
    std::string command;                        // Command name (without /)
    std::vector<std::string> args;              // Command arguments
    nlohmann::json params;                      // Named parameters (optional)
    uint64_t timestamp{0};                      // Client timestamp in ms

    [[nodiscard]] static auto from_json(const nlohmann::json& j) -> result<command_request_data, std::string>;
};

// Command response to client
struct command_response_data {
    bool success{false};
    std::string command;                        // Echo back the command
    std::string message;                        // Success/error message
    nlohmann::json result;                      // Command-specific result data

    [[nodiscard]] auto to_json() const -> nlohmann::json;
};

// Character data message (full character info for entering game)
struct character_data_msg {
    uint32_t id;
    std::string name;
    int16_t level;
    int16_t class_type;
    int16_t nation;
    int16_t gender;
    std::string map_name;
    int16_t pos_x;
    int16_t pos_y;
    int32_t hp;
    int32_t hp_max;
    int32_t mp;
    int32_t mp_max;
    int32_t sp;
    int32_t sp_max;
    int32_t gold;
    int16_t str;
    int16_t dex;
    int16_t vit;
    int16_t int_;
    int16_t mag;
    int16_t cha;
    int16_t hair_style;
    int16_t hair_color;
    int16_t skin_color;
    int64_t experience;
    int32_t pk_count;
    int32_t hunger_level;

    [[nodiscard]] auto to_json() const -> nlohmann::json;
};

// Inventory item for inventory_data message
struct inventory_item_msg {
    uint8_t slot;
    uint32_t item_id;
    std::string name;
    int16_t count;
    int16_t durability;
    int16_t max_durability;

    [[nodiscard]] auto to_json() const -> nlohmann::json;
};

// Equipment item for equipment_data message
struct equipment_item_msg {
    uint8_t slot;
    uint32_t item_id;
    std::string name;
    int16_t durability;
    int16_t max_durability;

    [[nodiscard]] auto to_json() const -> nlohmann::json;
};

// Visible entity for world_init message
struct visible_entity_msg {
    uint32_t entity_id;
    std::string type;  // "player" or "npc"
    std::string name;
    int16_t x;
    int16_t y;
    int16_t hp_percent;
    int16_t direction;

    // NPC-specific fields (optional, only used when type == "npc")
    uint32_t template_id{0};
    int16_t level{0};

    [[nodiscard]] auto to_json() const -> nlohmann::json;
};

// Attack result for attack response
struct attack_result_msg {
    bool hit{false};
    bool critical{false};
    int32_t damage{0};
    uint32_t target_id{0};
    int16_t target_hp{0};        // Target's remaining HP
    int16_t target_hp_max{0};    // Target's max HP
    int16_t attacker_x{0};       // Confirmed attacker position
    int16_t attacker_y{0};

    // Ranged combat fields (optional)
    bool is_ranged{false};
    int32_t ammo_count{-1};          // Remaining arrows (-1 = not applicable)
    uint32_t ammo_template_id{0};    // Arrow template that was consumed

    [[nodiscard]] auto to_json() const -> nlohmann::json;
};

// Magic result for magic response
struct magic_result_msg {
    bool success{false};
    uint32_t spell_id{0};
    int32_t mana_cost{0};
    int32_t damage{0};           // If damage spell
    int32_t heal{0};             // If heal spell
    uint32_t target_id{0};       // If targeted
    int16_t caster_mp{0};        // Remaining MP after cast

    [[nodiscard]] auto to_json() const -> nlohmann::json;
};

// Skill result for skill response
struct skill_result_msg {
    bool success{false};
    uint32_t skill_id{0};
    int32_t effect_value{0};     // Skill-specific effect
    uint32_t target_id{0};       // If targeted

    [[nodiscard]] auto to_json() const -> nlohmann::json;
};

// Pickup result for pickup response
struct pickup_result_msg {
    bool success{false};
    uint32_t item_id{0};
    std::string item_name;
    int16_t quantity{0};
    uint8_t inventory_slot{0};   // Where it was placed

    [[nodiscard]] auto to_json() const -> nlohmann::json;
};

// Interact result for interact response
struct interact_result_msg {
    bool success{false};
    uint32_t target_id{0};
    std::string interaction_type;  // "dialog", "shop", "bank", etc.
    nlohmann::json interaction_data;  // Type-specific data

    [[nodiscard]] auto to_json() const -> nlohmann::json;
};

// Teleporter info for map_teleporters message
struct teleporter_info_msg {
    uint32_t id{0};            // Computed from position (x << 16 | y)
    int16_t x{0};
    int16_t y{0};
    std::string dest_map;
    int16_t dest_x{0};
    int16_t dest_y{0};
    int16_t dest_dir{0};

    [[nodiscard]] auto to_json() const -> nlohmann::json;
};

// Map teleporters message (full list for a map)
struct map_teleporters_msg {
    std::string map_name;
    std::vector<teleporter_info_msg> teleporters;

    [[nodiscard]] auto to_json() const -> nlohmann::json;
};

// Teleporter update message (live add/remove/modify)
struct teleporter_update_msg {
    std::string action;        // "add", "remove", "modify"
    std::string map_name;
    teleporter_info_msg teleporter;

    [[nodiscard]] auto to_json() const -> nlohmann::json;
};

// Player teleport message (sent to player being teleported)
struct player_teleport_msg {
    std::string dest_map;
    int16_t dest_x{0};
    int16_t dest_y{0};
    int16_t dest_dir{0};
    std::vector<visible_entity_msg> entities;  // Visible at destination

    [[nodiscard]] auto to_json() const -> nlohmann::json;
};

// Response builders

[[nodiscard]] auto make_error_response(uint32_t seq, std::string_view error_code,
                                        std::string_view message) -> json_message;

[[nodiscard]] auto make_login_response(uint32_t seq, bool success,
                                        std::optional<std::string_view> token = std::nullopt,
                                        std::optional<std::string_view> error = std::nullopt,
                                        std::optional<std::string_view> forum_token = std::nullopt) -> json_message;

[[nodiscard]] auto make_create_account_response(uint32_t seq, bool success,
                                                  std::optional<uint32_t> account_id = std::nullopt,
                                                  std::optional<std::string_view> error = std::nullopt) -> json_message;

[[nodiscard]] auto make_logout_response(uint32_t seq, bool success) -> json_message;

[[nodiscard]] auto make_get_characters_response(uint32_t seq,
                                                  const std::vector<auth::character_summary>& characters) -> json_message;

[[nodiscard]] auto make_create_character_response(uint32_t seq, bool success,
                                                    std::optional<uint32_t> character_id = std::nullopt,
                                                    std::optional<std::string_view> error = std::nullopt) -> json_message;

[[nodiscard]] auto make_delete_character_response(uint32_t seq, bool success,
                                                    std::optional<std::string_view> error = std::nullopt) -> json_message;

// Known spell for enter_game_response
struct known_spell_msg {
    uint16_t spell_id{0};
    int16_t level{1};
    int32_t total_casts{0};

    [[nodiscard]] auto to_json() const -> nlohmann::json;
};

// Quest objective progress for enter_game_response
struct quest_objective_msg {
    uint16_t id{0};
    uint8_t status{0};       // 0=incomplete, 1=complete, 2=failed
    int32_t current{0};
    int32_t required{1};

    [[nodiscard]] auto to_json() const -> nlohmann::json;
};

// Active quest for enter_game_response
struct active_quest_msg {
    uint16_t quest_id{0};
    uint8_t status{0};       // quest_status enum
    std::vector<quest_objective_msg> objectives;

    [[nodiscard]] auto to_json() const -> nlohmann::json;
};

// Full game state for enter_game_response
struct game_state_msg {
    character_data_msg character;
    std::vector<inventory_item_msg> inventory;
    std::vector<equipment_item_msg> equipment;
    std::vector<std::pair<uint8_t, int16_t>> skills;
    std::vector<known_spell_msg> spells;
    std::vector<active_quest_msg> quests;
    std::vector<uint16_t> completed_quests;
    std::vector<visible_entity_msg> entities;
    int32_t gold{0};

    [[nodiscard]] auto to_json() const -> nlohmann::json;
};

[[nodiscard]] auto make_enter_game_response(uint32_t seq, bool success,
                                             const game_state_msg* game_state = nullptr,
                                             std::optional<std::string_view> error = std::nullopt) -> json_message;

[[nodiscard]] auto make_inventory_data(uint32_t seq,
                                        const std::vector<inventory_item_msg>& items,
                                        int32_t gold) -> json_message;

[[nodiscard]] auto make_equipment_data(uint32_t seq,
                                        const std::vector<equipment_item_msg>& equipped) -> json_message;

[[nodiscard]] auto make_skills_data(uint32_t seq,
                                     const std::vector<std::pair<uint8_t, int16_t>>& skills) -> json_message;

[[nodiscard]] auto make_world_init(uint32_t seq,
                                    const std::vector<visible_entity_msg>& entities) -> json_message;

[[nodiscard]] auto make_entity_spawn(uint32_t seq,
                                      const visible_entity_msg& entity) -> json_message;

[[nodiscard]] auto make_entity_despawn(uint32_t seq, uint32_t entity_id) -> json_message;

// Movement messages
[[nodiscard]] auto make_player_move_response(uint32_t seq, bool success,
                                              int16_t x, int16_t y, int16_t direction,
                                              std::optional<std::string_view> error = std::nullopt) -> json_message;

[[nodiscard]] auto make_player_stop_response(uint32_t seq, bool success,
                                              int16_t x, int16_t y, int16_t direction,
                                              std::optional<std::string_view> error = std::nullopt) -> json_message;

[[nodiscard]] auto make_player_position_update(uint32_t entity_id,
                                                int16_t x, int16_t y, int16_t direction,
                                                bool is_running = false,
                                                std::optional<int16_t> dest_x = std::nullopt,
                                                std::optional<int16_t> dest_y = std::nullopt) -> json_message;

// Combat messages
[[nodiscard]] auto make_player_attack_response(uint32_t seq, bool success,
                                                const attack_result_msg* result = nullptr,
                                                std::optional<std::string_view> error = std::nullopt) -> json_message;

[[nodiscard]] auto make_combat_attack_broadcast(uint32_t attacker_id, uint32_t target_id,
                                                 int16_t attacker_x, int16_t attacker_y,
                                                 int16_t target_x, int16_t target_y,
                                                 bool hit, bool critical, int32_t damage,
                                                 projectile_type projectile = projectile_type::none) -> json_message;

[[nodiscard]] auto make_entity_hp_update(uint32_t entity_id, int32_t hp, int32_t hp_max) -> json_message;

[[nodiscard]] auto make_entity_death(uint32_t victim_id, uint32_t killer_id,
                                      int16_t x, int16_t y) -> json_message;

// Combat effect broadcast data (unified visual feedback for all combat/spell events)
struct combat_effect_data {
    uint32_t source_id{0};
    uint32_t target_id{0};
    std::string effect_type;     // "damage","heal","miss","dodge","block","resist","buff","debuff"
    int32_t value{0};
    std::string damage_type;     // "physical","magic","fire","ice","lightning","poison","holy","dark","pure"
    uint32_t spell_id{0};        // 0 for melee
    bool is_critical{false};
    int16_t target_x{0};
    int16_t target_y{0};

    [[nodiscard]] auto to_json() const -> nlohmann::json;
};

[[nodiscard]] auto make_combat_effect(const combat_effect_data& data) -> json_message;

// Magic messages
[[nodiscard]] auto make_player_magic_response(uint32_t seq, bool success,
                                               const magic_result_msg* result = nullptr,
                                               std::optional<std::string_view> error = std::nullopt) -> json_message;

// Skill messages
[[nodiscard]] auto make_player_skill_response(uint32_t seq, bool success,
                                               const skill_result_msg* result = nullptr,
                                               std::optional<std::string_view> error = std::nullopt) -> json_message;

// Pickup messages
[[nodiscard]] auto make_player_pickup_response(uint32_t seq, bool success,
                                                const pickup_result_msg* result = nullptr,
                                                std::optional<std::string_view> error = std::nullopt) -> json_message;

// Interact messages
[[nodiscard]] auto make_player_interact_response(uint32_t seq, bool success,
                                                  const interact_result_msg* result = nullptr,
                                                  std::optional<std::string_view> error = std::nullopt) -> json_message;

[[nodiscard]] auto make_pong_response(uint32_t seq) -> json_message;

// Chat messages
[[nodiscard]] auto make_chat_message_response(uint32_t seq, bool success,
                                               std::optional<std::string_view> error = std::nullopt) -> json_message;

[[nodiscard]] auto make_chat_message_broadcast(const chat_message_broadcast_data& data) -> json_message;

// Command messages
[[nodiscard]] auto make_command_response(uint32_t seq, bool success,
                                          std::string_view command,
                                          std::string_view message,
                                          const nlohmann::json& result = nlohmann::json::object()) -> json_message;

// Teleportation messages
[[nodiscard]] auto make_map_teleporters(const map_teleporters_msg& data) -> json_message;

[[nodiscard]] auto make_teleporter_update(const teleporter_update_msg& data) -> json_message;

[[nodiscard]] auto make_player_teleport(uint32_t seq, const player_teleport_msg& data) -> json_message;

// NPC spawn data
struct npc_spawn_data {
    uint32_t entity_id{0};
    uint32_t template_id{0};
    std::string name;
    int16_t x{0};
    int16_t y{0};
    uint8_t direction{0};
    int32_t hp{0};
    int32_t max_hp{0};
    int16_t level{0};

    [[nodiscard]] auto to_json() const -> nlohmann::json;
};

// NPC despawn data
struct npc_despawn_data {
    uint32_t entity_id{0};
};

// NPC move data
struct npc_move_data {
    uint32_t entity_id{0};
    int16_t x{0};
    int16_t y{0};
    uint8_t direction{0};

    [[nodiscard]] auto to_json() const -> nlohmann::json;
};

// NPC attack data
struct npc_attack_data {
    uint32_t attacker_id{0};
    uint32_t target_id{0};
    int32_t damage{0};
    bool is_critical{false};
    bool is_ranged{false};  // NPC ranged attack (sends projectile visual)
    int16_t attacker_x{0};
    int16_t attacker_y{0};
    int16_t target_x{0};
    int16_t target_y{0};

    [[nodiscard]] auto to_json() const -> nlohmann::json;
};

// NPC death data
struct npc_death_data {
    uint32_t entity_id{0};
    uint32_t killer_id{0};  // 0 if unknown/environmental
    int16_t x{0};
    int16_t y{0};

    [[nodiscard]] auto to_json() const -> nlohmann::json;
};

// NPC message builders
[[nodiscard]] auto make_npc_spawn_message(const npc_spawn_data& data) -> json_message;
[[nodiscard]] auto make_npc_despawn_message(uint32_t entity_id) -> json_message;
[[nodiscard]] auto make_npc_move_message(const npc_move_data& data) -> json_message;
[[nodiscard]] auto make_npc_attack_message(const npc_attack_data& data) -> json_message;
[[nodiscard]] auto make_npc_death_message(const npc_death_data& data) -> json_message;

// Ground item spawn data (broadcast when item appears on ground)
struct ground_item_spawn_data {
    uint32_t item_id{0};         // Item instance ID
    uint32_t template_id{0};     // Item template ID
    std::string item_name;       // Item name for display
    int16_t count{1};            // Stack count
    int16_t x{0};                // Position
    int16_t y{0};

    [[nodiscard]] auto to_json() const -> nlohmann::json;
};

// Ground item spawn message builder
[[nodiscard]] auto make_ground_item_spawn(const ground_item_spawn_data& data) -> json_message;

// Ground item removed data (broadcast when item is picked up)
struct ground_item_removed_data {
    uint32_t picker_id{0};       // Player who picked up the item
    std::string picker_name;     // Picker's name for display
    uint32_t item_id{0};         // Item that was removed
    std::string item_name;       // Item name for display
    int16_t x{0};                // Position where item was
    int16_t y{0};

    [[nodiscard]] auto to_json() const -> nlohmann::json;
};

// Ground item message builder
[[nodiscard]] auto make_ground_item_removed(const ground_item_removed_data& data) -> json_message;

// Player death info data (sent to dead player)
struct player_death_info_data {
    uint32_t killer_id{0};
    std::string killer_name;
    bool is_pvp{false};
    int64_t xp_lost{0};
    int32_t pk_points_change{0};   // Positive = killer gained PK points
    int32_t gold_reward{0};        // Gold earned for killing a PKer
    uint32_t respawn_delay_ms{0};
    std::string respawn_map;
    int16_t respawn_x{0};
    int16_t respawn_y{0};

    [[nodiscard]] auto to_json() const -> nlohmann::json;
};

// Player death info message builder
[[nodiscard]] auto make_player_death_info(const player_death_info_data& data) -> json_message;

// Hunger update data
struct hunger_update_data {
    int8_t level{0};
    bool is_starving{false};

    [[nodiscard]] auto to_json() const -> nlohmann::json;
};

// Hunger update message builder
[[nodiscard]] auto make_hunger_update(int8_t level) -> json_message;

// Entity info request data
struct entity_info_request_data {
    uint32_t entity_id{0};

    [[nodiscard]] static auto from_json(const nlohmann::json& j) -> result<entity_info_request_data, std::string>;
};

// Entity info response data
struct entity_info_response_data {
    uint32_t entity_id{0};
    std::string entity_type;      // "player" or "npc"
    std::string name;
    int16_t level{0};
    int32_t hp{0};
    int32_t hp_max{0};
    int16_t x{0};
    int16_t y{0};
    int16_t direction{0};

    // Player-specific fields
    std::optional<std::string> faction;       // "aresden", "elvine", "neutral"
    std::optional<std::string> guild_name;
    std::optional<int16_t> class_type;
    std::optional<int32_t> pk_count;

    // NPC-specific fields
    std::optional<uint32_t> template_id;
    std::optional<std::string> npc_type;      // "monster", "vendor", "guard", etc.

    [[nodiscard]] auto to_json() const -> nlohmann::json;
};

// Entity info message builders
[[nodiscard]] auto make_entity_info_response(uint32_t seq, bool success,
                                              const entity_info_response_data* data = nullptr,
                                              std::optional<std::string_view> error = std::nullopt) -> json_message;

// === Equipment: Equip/Unequip request/response data ===

// Equip request from client
struct player_equip_request_data {
    int16_t inventory_slot{-1};  // Source inventory slot
    uint8_t target_slot{0};      // Target equip_slot

    [[nodiscard]] static auto from_json(const nlohmann::json& j) -> result<player_equip_request_data, std::string>;
};

// Unequip request from client
struct player_unequip_request_data {
    uint8_t equip_slot{0};       // Slot to unequip

    [[nodiscard]] static auto from_json(const nlohmann::json& j) -> result<player_unequip_request_data, std::string>;
};

// Equip result response
struct equip_result_msg {
    bool success{false};
    uint8_t slot{0};
    uint32_t item_id{0};
    std::string item_name;
    int16_t durability{0};
    int16_t max_durability{0};
    std::optional<uint32_t> swapped_item_id;       // Old item returned to inventory
    std::optional<uint8_t> swapped_to_inv_slot;     // Where old item went
    std::optional<uint32_t> unequipped_shield_id;   // If 2H forced shield removal
    std::optional<uint8_t> shield_to_inv_slot;
    std::string error;

    [[nodiscard]] auto to_json() const -> nlohmann::json;
};

// Unequip result response
struct unequip_result_msg {
    bool success{false};
    uint8_t slot{0};
    uint32_t item_id{0};
    std::string item_name;
    uint8_t inventory_slot{0};   // Where it was placed
    std::string error;

    [[nodiscard]] auto to_json() const -> nlohmann::json;
};

// Equipment change broadcast to nearby players
struct equipment_change_broadcast_data {
    uint32_t entity_id{0};       // Player's ECS entity ID (client-facing)
    uint8_t slot{0};
    uint32_t item_id{0};         // 0 = now empty
    uint32_t template_id{0};     // For client sprite lookup

    [[nodiscard]] auto to_json() const -> nlohmann::json;
};

// Stat update sent to equipping player after equipment change
struct stat_update_data {
    int32_t max_hp{0};
    int32_t max_mp{0};
    int32_t max_sp{0};
    int32_t attack_power{0};
    int32_t magic_power{0};
    int32_t defense{0};
    int32_t magic_defense{0};
    int32_t hit_rate{0};
    int32_t dodge_rate{0};
    int32_t critical_rate{0};

    [[nodiscard]] auto to_json() const -> nlohmann::json;
};

// Equipment message builders
[[nodiscard]] auto make_player_equip_response(uint32_t seq, const equip_result_msg& result) -> json_message;
[[nodiscard]] auto make_player_unequip_response(uint32_t seq, const unequip_result_msg& result) -> json_message;
[[nodiscard]] auto make_equipment_change_broadcast(const equipment_change_broadcast_data& data) -> json_message;
[[nodiscard]] auto make_stat_update(const stat_update_data& data) -> json_message;

// Spell list update - sends full known spell list to client
[[nodiscard]] auto make_spell_list_update(const std::vector<known_spell_msg>& spells) -> json_message;

// === NPC Interaction: Shop request/response data ===

// Shop buy request from client
struct shop_buy_request_data {
    uint32_t npc_entity_id{0};
    uint32_t item_template_id{0};
    int16_t count{1};

    [[nodiscard]] static auto from_json(const nlohmann::json& j) -> result<shop_buy_request_data, std::string>;
};

// Shop sell request from client (requests a price quote)
struct shop_sell_request_data {
    uint32_t npc_entity_id{0};
    int16_t inventory_slot{-1};
    int16_t count{1};

    [[nodiscard]] static auto from_json(const nlohmann::json& j) -> result<shop_sell_request_data, std::string>;
};

// Shop sell confirm request from client
struct shop_sell_confirm_request_data {
    uint32_t npc_entity_id{0};
    int16_t inventory_slot{-1};
    int16_t count{1};

    [[nodiscard]] static auto from_json(const nlohmann::json& j) -> result<shop_sell_confirm_request_data, std::string>;
};

// Shop repair request from client (requests a cost quote)
struct shop_repair_request_data {
    uint32_t npc_entity_id{0};
    int16_t inventory_slot{-1};

    [[nodiscard]] static auto from_json(const nlohmann::json& j) -> result<shop_repair_request_data, std::string>;
};

// Shop repair confirm request from client
struct shop_repair_confirm_request_data {
    uint32_t npc_entity_id{0};
    int16_t inventory_slot{-1};

    [[nodiscard]] static auto from_json(const nlohmann::json& j) -> result<shop_repair_confirm_request_data, std::string>;
};

// Bank deposit request from client
struct bank_deposit_request_data {
    uint32_t npc_entity_id{0};
    int16_t inventory_slot{-1};

    [[nodiscard]] static auto from_json(const nlohmann::json& j) -> result<bank_deposit_request_data, std::string>;
};

// Bank withdraw request from client
struct bank_withdraw_request_data {
    uint32_t npc_entity_id{0};
    int16_t bank_slot{-1};

    [[nodiscard]] static auto from_json(const nlohmann::json& j) -> result<bank_withdraw_request_data, std::string>;
};

// Dialog choice request from client
struct dialog_choice_request_data {
    uint32_t npc_entity_id{0};
    std::string node_id;
    int16_t choice_index{0};

    [[nodiscard]] static auto from_json(const nlohmann::json& j) -> result<dialog_choice_request_data, std::string>;
};

// === NPC Interaction response builders ===

// Shop buy response
[[nodiscard]] auto make_shop_buy_response(uint32_t seq, bool success,
                                           std::string_view item_name = "",
                                           int16_t count = 0,
                                           int32_t price_paid = 0,
                                           int64_t gold_remaining = 0,
                                           std::optional<std::string_view> error = std::nullopt) -> json_message;

// Shop sell quote response
[[nodiscard]] auto make_shop_sell_response(uint32_t seq, bool success,
                                            std::string_view item_name = "",
                                            int32_t offered_price = 0,
                                            int16_t durability = 0,
                                            std::optional<std::string_view> error = std::nullopt) -> json_message;

// Shop sell confirm response
[[nodiscard]] auto make_shop_sell_confirm_response(uint32_t seq, bool success,
                                                    int32_t gold_received = 0,
                                                    int64_t gold_total = 0,
                                                    std::optional<std::string_view> error = std::nullopt) -> json_message;

// Shop repair quote response
[[nodiscard]] auto make_shop_repair_response(uint32_t seq, bool success,
                                              std::string_view item_name = "",
                                              int32_t repair_cost = 0,
                                              int16_t durability = 0,
                                              std::optional<std::string_view> error = std::nullopt) -> json_message;

// Shop repair confirm response
[[nodiscard]] auto make_shop_repair_confirm_response(uint32_t seq, bool success,
                                                      int16_t new_durability = 0,
                                                      int32_t gold_spent = 0,
                                                      int64_t gold_remaining = 0,
                                                      std::optional<std::string_view> error = std::nullopt) -> json_message;

// Bank deposit response
[[nodiscard]] auto make_bank_deposit_response(uint32_t seq, bool success,
                                               std::string_view item_name = "",
                                               std::optional<std::string_view> error = std::nullopt) -> json_message;

// Bank withdraw response
[[nodiscard]] auto make_bank_withdraw_response(uint32_t seq, bool success,
                                                std::string_view item_name = "",
                                                std::optional<std::string_view> error = std::nullopt) -> json_message;

// Dialog choice response - returns next dialog state or action result
struct dialog_option_msg {
    std::string label;
    std::string action;
    std::string next_node;

    [[nodiscard]] auto to_json() const -> nlohmann::json;
};

[[nodiscard]] auto make_dialog_choice_response(uint32_t seq, bool success,
                                                std::string_view action = "",
                                                std::string_view node_id = "",
                                                std::string_view text = "",
                                                const std::vector<dialog_option_msg>& options = {},
                                                std::optional<std::string_view> error = std::nullopt) -> json_message;

// === View Mode & Visibility ===

// Rendering mode controlled by the server
// scaled: fixed internal resolution scaled to display (everyone sees same area)
// extended: native res, but entities only render within fair zone (terrain visible outside)
// special: unrestricted native res with zoom (commander/admin)
enum class view_mode : uint8_t
{
    scaled = 0,
    extended = 1,
    special = 2
};

[[nodiscard]] constexpr auto to_string(view_mode mode) -> std::string_view
{
    switch (mode)
    {
        case view_mode::scaled: return "scaled";
        case view_mode::extended: return "extended";
        case view_mode::special: return "special";
        default: return "scaled";
    }
}

[[nodiscard]] inline auto parse_view_mode(std::string_view str) -> view_mode
{
    if (str == "extended") return view_mode::extended;
    if (str == "special") return view_mode::special;
    return view_mode::scaled;
}

// Render mode message data (server -> client)
struct render_mode_data
{
    view_mode mode{view_mode::scaled};
    int16_t fair_width{800};     // Fair zone width in pixels
    int16_t fair_height{600};    // Fair zone height in pixels

    [[nodiscard]] auto to_json() const -> nlohmann::json;
};

[[nodiscard]] auto make_set_render_mode(const render_mode_data& data) -> json_message;
[[nodiscard]] auto make_view_range_update(int16_t radius_x, int16_t radius_y, bool sees_all) -> json_message;

// Visibility range constants
inline constexpr int16_t min_visibility_radius = 15;
inline constexpr int16_t max_visibility_radius = 80;
inline constexpr float visibility_buffer_ratio = 0.2f;  // 20% proportional buffer
inline constexpr int pixels_per_tile = 32;

// Separate X/Y visibility radii matching the rectangular viewport
struct visibility_radii
{
    int16_t x;
    int16_t y;
};

// Calculate visibility radii from effective viewport dimensions.
// Each axis is computed independently: (tiles/2) + buffer, clamped to [min, max].
// Client sends actual screen resolution for normal play, or max effective viewport
// (screen_res / min_zoom) when in a mode that allows zooming out further.
[[nodiscard]] inline auto calculate_visibility_radius(int16_t screen_width, int16_t screen_height) -> visibility_radii
{
    auto tiles_wide = static_cast<float>(screen_width) / pixels_per_tile;
    auto tiles_high = static_cast<float>(screen_height) / pixels_per_tile;

    auto base_x = tiles_wide / 2.0f;
    auto base_y = tiles_high / 2.0f;

    auto buffer_x = std::max(5.0f, base_x * visibility_buffer_ratio);
    auto buffer_y = std::max(5.0f, base_y * visibility_buffer_ratio);

    auto rx = static_cast<int16_t>(base_x + buffer_x);
    auto ry = static_cast<int16_t>(base_y + buffer_y);

    return {
        std::clamp(rx, min_visibility_radius, max_visibility_radius),
        std::clamp(ry, min_visibility_radius, max_visibility_radius)
    };
}

}  // namespace hb::network
