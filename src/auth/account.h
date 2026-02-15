#pragma once

// account.h
// Account and authentication data structures

#include "core/types.h"

#include <string>
#include <chrono>
#include <optional>
#include <vector>

namespace hb::auth
{

// Admin levels
enum class admin_level : uint8_t
{
    player = 0,
    helper = 1,
    gamemaster = 10,
    senior_gm = 15,
    administrator = 20
};

// Account data
struct account
{
    account_id id{};
    std::string username;
    std::string password_hash;
    admin_level admin{admin_level::player};
    bool is_banned{false};
    std::string ban_reason;
    std::optional<std::chrono::system_clock::time_point> ban_expires;
    std::chrono::system_clock::time_point created_at;
    std::optional<std::chrono::system_clock::time_point> last_login;
    std::optional<uint64_t> forum_member_id;
};

// Character summary (for character selection screen)
struct character_summary
{
    player_id id{};
    std::string name;
    int16_t level{1};
    int16_t class_type{0};
    int16_t nation{0};
    int16_t gender{1};
    std::string map_name;
    int64_t experience{0};

    // Visual appearance
    int16_t hair_style{0};
    int16_t hair_color{0};
    int16_t skin_color{0};

    std::optional<std::chrono::system_clock::time_point> last_played;
};

// Character creation request
struct character_create_info
{
    std::string name;
    int16_t class_type{0}; // 0 = warrior, 1 = mage, etc.
    int16_t nation{0};     // 0 = neutral, 1 = aresden, 2 = elvine
    int16_t gender{1};     // 1 = male, 2 = female
    int16_t hair_style{0};
    int16_t hair_color{0};
    int16_t skin_color{0};
    int16_t underwear_color{0};

    // Initial stat allocation (optional, defaults will be used if not provided)
    std::optional<int16_t> strength;
    std::optional<int16_t> dexterity;
    std::optional<int16_t> vitality;
    std::optional<int16_t> intelligence;
    std::optional<int16_t> magic;
    std::optional<int16_t> charisma;
};

// Full character data for entering game
struct character_full_data
{
    player_id id{};
    account_id account{};
    std::string name;
    int16_t level{1};
    int16_t class_type{0};
    int16_t nation{0};
    int16_t gender{1};

    // Location
    std::string map_name;
    int16_t pos_x{0};
    int16_t pos_y{0};

    // Resources
    int64_t experience{0};
    int32_t hp{0};
    int32_t max_hp{100};
    int32_t mp{0};
    int32_t max_mp{50};
    int32_t sp{0};
    int32_t max_sp{50};
    int32_t gold{0};

    // Stats
    int16_t strength{10};
    int16_t dexterity{10};
    int16_t vitality{10};
    int16_t intelligence{10};
    int16_t magic{10};
    int16_t charisma{10};

    // Appearance
    int16_t hair_style{0};
    int16_t hair_color{0};
    int16_t skin_color{0};
    int16_t underwear_color{0};

    // Status
    int32_t pk_count{0};
    int32_t pk_points{0};
    int32_t hunger_level{100};
    int32_t enemy_kill_count{0};
    int32_t contribution{0};
    int16_t stat_points_available{0};
    int16_t luck{0};
    int32_t reward_gold{0};

    // Serialized data (JSON strings stored in JSONB columns)
    std::string skills_data;    // JSON: [{type, level, exp}, ...]
    std::string inventory_data; // JSON: [{slot, item_id, count}, ...]
    std::string equipment_data; // JSON: [{slot, item_id, durability, max_durability}, ...]
    std::string bank_data;      // JSON: [{slot, item_id, count}, ...]
    std::string magic_data;     // JSON: [{spell_id, level, total_casts}, ...]
    std::string quest_data;     // JSON: {active: [...], completed: [...]}
};

// Authentication errors
enum class auth_error
{
    success,
    invalid_credentials,
    account_banned,
    username_taken,
    character_name_taken,
    max_characters_reached,
    database_error,
    invalid_password_format,
    invalid_username_format,
    invalid_character_name,
    character_not_found,
    character_not_owned,
    session_expired,
    session_not_found,
    account_already_in_game,
    forum_auth_failed,
    internal_error
};

// Convert auth_error to string for logging
[[nodiscard]] constexpr auto to_string(auth_error err) -> std::string_view
{
    switch (err)
    {
    case auth_error::success:
        return "success";
    case auth_error::invalid_credentials:
        return "invalid_credentials";
    case auth_error::account_banned:
        return "account_banned";
    case auth_error::username_taken:
        return "username_taken";
    case auth_error::character_name_taken:
        return "character_name_taken";
    case auth_error::max_characters_reached:
        return "max_characters_reached";
    case auth_error::database_error:
        return "database_error";
    case auth_error::invalid_password_format:
        return "invalid_password_format";
    case auth_error::invalid_username_format:
        return "invalid_username_format";
    case auth_error::invalid_character_name:
        return "invalid_character_name";
    case auth_error::character_not_found:
        return "character_not_found";
    case auth_error::character_not_owned:
        return "character_not_owned";
    case auth_error::session_expired:
        return "session_expired";
    case auth_error::session_not_found:
        return "session_not_found";
    case auth_error::account_already_in_game:
        return "account_already_in_game";
    case auth_error::forum_auth_failed:
        return "forum_auth_failed";
    case auth_error::internal_error:
        return "internal_error";
    default:
        return "unknown";
    }
}

} // namespace hb::auth
