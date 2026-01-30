#pragma once

// json_protocol.h
// JSON-based message protocol for WebSocket communication

#include "core/types.h"
#include "core/result.h"
#include "auth/account.h"

#include <nlohmann/json.hpp>
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
    super = 2     // Super attack (requires 100% skill + charges, ranged)
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
    player_run_request,
    player_run_response,
    player_stop_request,
    player_stop_response,
    player_position_update,  // Broadcast to nearby players

    // Combat
    player_attack_request,
    player_attack_response,

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
    chat_message,

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
        case json_message_type::player_run_request: return "player_run_request";
        case json_message_type::player_run_response: return "player_run_response";
        case json_message_type::player_stop_request: return "player_stop_request";
        case json_message_type::player_stop_response: return "player_stop_response";
        case json_message_type::player_position_update: return "player_position_update";
        case json_message_type::player_attack_request: return "player_attack_request";
        case json_message_type::player_attack_response: return "player_attack_response";
        case json_message_type::player_magic_request: return "player_magic_request";
        case json_message_type::player_magic_response: return "player_magic_response";
        case json_message_type::player_skill_request: return "player_skill_request";
        case json_message_type::player_skill_response: return "player_skill_response";
        case json_message_type::player_pickup_request: return "player_pickup_request";
        case json_message_type::player_pickup_response: return "player_pickup_response";
        case json_message_type::player_interact_request: return "player_interact_request";
        case json_message_type::player_interact_response: return "player_interact_response";
        case json_message_type::chat_message: return "chat_message";
        default: return "unknown";
    }
}

// Parse message type from string
[[nodiscard]] auto parse_message_type(std::string_view type_str) -> json_message_type;

// Base JSON message structure
struct json_message {
    json_message_type type{json_message_type::unknown};
    uint32_t seq{0};  // Sequence number for request/response matching
    nlohmann::json data;

    [[nodiscard]] auto to_json() const -> nlohmann::json;
    [[nodiscard]] static auto from_json(const nlohmann::json& j) -> result<json_message, std::string>;
    [[nodiscard]] static auto parse(std::string_view json_str) -> result<json_message, std::string>;
};

// Request message structures

struct login_request_data {
    std::string username;
    std::string password;

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
    int16_t gender{0};
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

    [[nodiscard]] static auto from_json(const nlohmann::json& j) -> result<enter_game_request_data, std::string>;
};

// Movement request from client (walking)
struct player_move_request_data {
    int16_t x{0};          // Current position for validation
    int16_t y{0};
    int16_t direction{0};  // Direction to move (0-7)
    uint64_t timestamp{0}; // Client timestamp in ms

    [[nodiscard]] static auto from_json(const nlohmann::json& j) -> result<player_move_request_data, std::string>;
};

// Running request from client
struct player_run_request_data {
    int16_t x{0};          // Current position for validation
    int16_t y{0};
    int16_t direction{0};  // Direction to run (0-7)
    uint64_t timestamp{0}; // Client timestamp in ms

    [[nodiscard]] static auto from_json(const nlohmann::json& j) -> result<player_run_request_data, std::string>;
};

// Stop request from client
struct player_stop_request_data {
    int16_t x{0};          // Position where stopped
    int16_t y{0};
    uint64_t timestamp{0}; // Client timestamp in ms

    [[nodiscard]] static auto from_json(const nlohmann::json& j) -> result<player_stop_request_data, std::string>;
};

// Attack request from client
struct player_attack_request_data {
    int16_t x{0};              // Current position for validation
    int16_t y{0};
    int16_t direction{0};      // Direction facing
    attack_type type{attack_type::regular};
    target_type target_type{target_type::none};
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
    target_type target_type{target_type::none};
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
    target_type target_type{target_type::none};
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
    target_type target_type{target_type::none};
    uint32_t target_id{0};     // Target NPC or object ID
    uint64_t timestamp{0};     // Client timestamp in ms

    [[nodiscard]] static auto from_json(const nlohmann::json& j) -> result<player_interact_request_data, std::string>;
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

// Response builders

[[nodiscard]] auto make_error_response(uint32_t seq, std::string_view error_code,
                                        std::string_view message) -> json_message;

[[nodiscard]] auto make_login_response(uint32_t seq, bool success,
                                        std::optional<std::string_view> token = std::nullopt,
                                        std::optional<std::string_view> error = std::nullopt) -> json_message;

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

// Full game state for enter_game_response
struct game_state_msg {
    character_data_msg character;
    std::vector<inventory_item_msg> inventory;
    std::vector<equipment_item_msg> equipment;
    std::vector<std::pair<uint8_t, int16_t>> skills;
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

[[nodiscard]] auto make_player_run_response(uint32_t seq, bool success,
                                             int16_t x, int16_t y, int16_t direction,
                                             std::optional<std::string_view> error = std::nullopt) -> json_message;

[[nodiscard]] auto make_player_stop_response(uint32_t seq, bool success,
                                              int16_t x, int16_t y,
                                              std::optional<std::string_view> error = std::nullopt) -> json_message;

[[nodiscard]] auto make_player_position_update(uint32_t entity_id,
                                                int16_t x, int16_t y, int16_t direction,
                                                bool is_running = false) -> json_message;

// Combat messages
[[nodiscard]] auto make_player_attack_response(uint32_t seq, bool success,
                                                const attack_result_msg* result = nullptr,
                                                std::optional<std::string_view> error = std::nullopt) -> json_message;

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

}  // namespace hb::network
