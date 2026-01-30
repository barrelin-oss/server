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
    player_position_update,  // Broadcast to nearby players

    // In-game actions
    player_action,
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
        case json_message_type::player_position_update: return "player_position_update";
        case json_message_type::player_action: return "player_action";
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

// Movement request from client
struct player_move_request_data {
    int16_t target_x{0};
    int16_t target_y{0};
    int16_t direction{0};  // Optional: direction to face after move

    [[nodiscard]] static auto from_json(const nlohmann::json& j) -> result<player_move_request_data, std::string>;
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

[[nodiscard]] auto make_player_position_update(uint32_t entity_id,
                                                int16_t x, int16_t y, int16_t direction) -> json_message;

[[nodiscard]] auto make_pong_response(uint32_t seq) -> json_message;

}  // namespace hb::network
