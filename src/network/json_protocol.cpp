// json_protocol.cpp
// JSON-based message protocol implementation

#include "network/json_protocol.h"
#include "core/logger.h"

#include <unordered_map>

namespace hb::network {

namespace {

// Safe conversion from JSON integer to int16_t with range validation
inline auto safe_int16(const nlohmann::json& j, std::string_view field, int16_t default_val = 0) -> int16_t {
    if (!j.contains(field) || !j[std::string(field)].is_number()) {
        return default_val;
    }
    auto val = j[std::string(field)].get<int>();
    if (val < (std::numeric_limits<int16_t>::min)() || val > (std::numeric_limits<int16_t>::max)()) {
        return default_val;  // Out of range, return default
    }
    return static_cast<int16_t>(val);
}

// Safe conversion with required validation (returns nullopt if out of range)
inline auto safe_int16_required(const nlohmann::json& j, std::string_view field)
    -> std::optional<int16_t>
{
    if (!j.contains(field) || !j[std::string(field)].is_number()) {
        return std::nullopt;
    }
    auto val = j[std::string(field)].get<int>();
    if (val < (std::numeric_limits<int16_t>::min)() || val > (std::numeric_limits<int16_t>::max)()) {
        return std::nullopt;
    }
    return static_cast<int16_t>(val);
}

// Safe direction parsing (clamps to 0-7)
inline auto safe_direction(const nlohmann::json& j, std::string_view field, int16_t default_val = 0) -> int16_t {
    if (!j.contains(field) || !j[std::string(field)].is_number()) {
        return default_val;
    }
    auto val = j[std::string(field)].get<int>();
    // Clamp to valid direction range 0-7
    if (val < 0) val = 0;
    if (val > 7) val = 7;
    return static_cast<int16_t>(val);
}

const std::unordered_map<std::string, json_message_type> type_map = {
    {"error", json_message_type::error},
    {"ping", json_message_type::ping},
    {"pong", json_message_type::pong},
    {"login_request", json_message_type::login_request},
    {"login_response", json_message_type::login_response},
    {"logout_request", json_message_type::logout_request},
    {"logout_response", json_message_type::logout_response},
    {"create_account_request", json_message_type::create_account_request},
    {"create_account_response", json_message_type::create_account_response},
    {"get_characters_request", json_message_type::get_characters_request},
    {"get_characters_response", json_message_type::get_characters_response},
    {"create_character_request", json_message_type::create_character_request},
    {"create_character_response", json_message_type::create_character_response},
    {"delete_character_request", json_message_type::delete_character_request},
    {"delete_character_response", json_message_type::delete_character_response},
    {"enter_game_request", json_message_type::enter_game_request},
    {"enter_game_response", json_message_type::enter_game_response},
    {"character_data", json_message_type::character_data},
    {"inventory_data", json_message_type::inventory_data},
    {"equipment_data", json_message_type::equipment_data},
    {"skills_data", json_message_type::skills_data},
    {"world_init", json_message_type::world_init},
    {"entity_spawn", json_message_type::entity_spawn},
    {"entity_despawn", json_message_type::entity_despawn},
    {"player_move_request", json_message_type::player_move_request},
    {"player_move_response", json_message_type::player_move_response},
    {"player_stop_request", json_message_type::player_stop_request},
    {"player_stop_response", json_message_type::player_stop_response},
    {"player_position_update", json_message_type::player_position_update},
    {"player_attack_request", json_message_type::player_attack_request},
    {"player_attack_response", json_message_type::player_attack_response},
    {"combat_attack_broadcast", json_message_type::combat_attack_broadcast},
    {"entity_hp_update", json_message_type::entity_hp_update},
    {"entity_death", json_message_type::entity_death},
    {"combat_effect", json_message_type::combat_effect},
    {"player_magic_request", json_message_type::player_magic_request},
    {"player_magic_response", json_message_type::player_magic_response},
    {"player_skill_request", json_message_type::player_skill_request},
    {"player_skill_response", json_message_type::player_skill_response},
    {"player_pickup_request", json_message_type::player_pickup_request},
    {"player_pickup_response", json_message_type::player_pickup_response},
    {"player_interact_request", json_message_type::player_interact_request},
    {"player_interact_response", json_message_type::player_interact_response},
    {"chat_message", json_message_type::chat_message},
    {"chat_message_broadcast", json_message_type::chat_message_broadcast},
    {"command_request", json_message_type::command_request},
    {"command_response", json_message_type::command_response},
    {"map_teleporters", json_message_type::map_teleporters},
    {"teleporter_update", json_message_type::teleporter_update},
    {"player_teleport", json_message_type::player_teleport},
    {"set_view_range", json_message_type::set_view_range},
    {"npc_spawn", json_message_type::npc_spawn},
    {"npc_despawn", json_message_type::npc_despawn},
    {"npc_move", json_message_type::npc_move},
    {"npc_attack", json_message_type::npc_attack},
    {"npc_death", json_message_type::npc_death},
    {"ground_item_spawn", json_message_type::ground_item_spawn},
    {"ground_item_removed", json_message_type::ground_item_removed},
    {"player_death_info", json_message_type::player_death_info},
    {"hunger_update", json_message_type::hunger_update},
    {"entity_info_request", json_message_type::entity_info_request},
    {"entity_info_response", json_message_type::entity_info_response},
    {"player_equip_request", json_message_type::player_equip_request},
    {"player_equip_response", json_message_type::player_equip_response},
    {"player_unequip_request", json_message_type::player_unequip_request},
    {"player_unequip_response", json_message_type::player_unequip_response},
    {"equipment_change_broadcast", json_message_type::equipment_change_broadcast},
    {"stat_update", json_message_type::stat_update},
    {"shop_buy_request", json_message_type::shop_buy_request},
    {"shop_buy_response", json_message_type::shop_buy_response},
    {"shop_sell_request", json_message_type::shop_sell_request},
    {"shop_sell_response", json_message_type::shop_sell_response},
    {"shop_sell_confirm_request", json_message_type::shop_sell_confirm_request},
    {"shop_sell_confirm_response", json_message_type::shop_sell_confirm_response},
    {"shop_repair_request", json_message_type::shop_repair_request},
    {"shop_repair_response", json_message_type::shop_repair_response},
    {"shop_repair_confirm_request", json_message_type::shop_repair_confirm_request},
    {"shop_repair_confirm_response", json_message_type::shop_repair_confirm_response},
    {"bank_deposit_request", json_message_type::bank_deposit_request},
    {"bank_deposit_response", json_message_type::bank_deposit_response},
    {"bank_withdraw_request", json_message_type::bank_withdraw_request},
    {"bank_withdraw_response", json_message_type::bank_withdraw_response},
    {"dialog_choice_request", json_message_type::dialog_choice_request},
    {"dialog_choice_response", json_message_type::dialog_choice_response}
};

}  // namespace

auto parse_message_type(std::string_view type_str) -> json_message_type {
    auto it = type_map.find(std::string(type_str));
    if (it != type_map.end()) {
        return it->second;
    }
    return json_message_type::unknown;
}

auto json_message::to_json() const -> nlohmann::json {
    return nlohmann::json{
        {"type", std::string(to_string(type))},
        {"seq", seq},
        {"data", data}
    };
}

auto json_message::from_json(const nlohmann::json& j) -> result<json_message, std::string> {
    try {
        json_message msg;

        if (!j.contains("type") || !j["type"].is_string()) {
            return result<json_message, std::string>::err("Missing or invalid 'type' field");
        }

        msg.type = parse_message_type(j["type"].get<std::string>());

        if (j.contains("seq")) {
            if (j["seq"].is_number_unsigned()) {
                msg.seq = j["seq"].get<uint32_t>();
            } else if (j["seq"].is_number_integer()) {
                msg.seq = static_cast<uint32_t>(j["seq"].get<int>());
            }
        }

        if (j.contains("data")) {
            msg.data = j["data"];
        }

        return result<json_message, std::string>::ok(std::move(msg));

    } catch (const nlohmann::json::exception& e) {
        return result<json_message, std::string>::err(std::string("JSON parse error: ") + e.what());
    }
}

auto json_message::parse(std::string_view json_str) -> result<json_message, std::string> {
    try {
        auto j = nlohmann::json::parse(json_str);
        return from_json(j);
    } catch (const nlohmann::json::parse_error& e) {
        return result<json_message, std::string>::err(std::string("JSON parse error: ") + e.what());
    }
}

// Request data parsers

auto login_request_data::from_json(const nlohmann::json& j) -> result<login_request_data, std::string> {
    try {
        login_request_data data;

        if (!j.contains("username") || !j["username"].is_string()) {
            return result<login_request_data, std::string>::err("Missing or invalid 'username' field");
        }
        data.username = j["username"].get<std::string>();

        if (!j.contains("password") || !j["password"].is_string()) {
            return result<login_request_data, std::string>::err("Missing or invalid 'password' field");
        }
        data.password = j["password"].get<std::string>();

        return result<login_request_data, std::string>::ok(std::move(data));

    } catch (const nlohmann::json::exception& e) {
        return result<login_request_data, std::string>::err(std::string("Parse error: ") + e.what());
    }
}

auto create_account_request_data::from_json(const nlohmann::json& j)
    -> result<create_account_request_data, std::string>
{
    try {
        create_account_request_data data;

        if (!j.contains("username") || !j["username"].is_string()) {
            return result<create_account_request_data, std::string>::err("Missing or invalid 'username' field");
        }
        data.username = j["username"].get<std::string>();

        if (!j.contains("password") || !j["password"].is_string()) {
            return result<create_account_request_data, std::string>::err("Missing or invalid 'password' field");
        }
        data.password = j["password"].get<std::string>();

        return result<create_account_request_data, std::string>::ok(std::move(data));

    } catch (const nlohmann::json::exception& e) {
        return result<create_account_request_data, std::string>::err(std::string("Parse error: ") + e.what());
    }
}

auto create_character_request_data::from_json(const nlohmann::json& j)
    -> result<create_character_request_data, std::string>
{
    try {
        create_character_request_data data;

        if (!j.contains("name") || !j["name"].is_string()) {
            return result<create_character_request_data, std::string>::err("Missing or invalid 'name' field");
        }
        data.name = j["name"].get<std::string>();

        // Optional fields with defaults - use safe parsing with range validation
        data.class_type = safe_int16(j, "class_type", 0);
        data.nation = safe_int16(j, "nation", 0);
        data.gender = safe_int16(j, "gender", 1);
        data.hair_style = safe_int16(j, "hair_style", 0);
        data.hair_color = safe_int16(j, "hair_color", 0);
        data.skin_color = safe_int16(j, "skin_color", 0);
        data.underwear_color = safe_int16(j, "underwear_color", 0);

        // Optional stats
        data.strength = safe_int16(j, "strength", 10);
        data.dexterity = safe_int16(j, "dexterity", 10);
        data.vitality = safe_int16(j, "vitality", 10);
        data.intelligence = safe_int16(j, "intelligence", 10);
        data.magic = safe_int16(j, "magic", 10);
        data.charisma = safe_int16(j, "charisma", 10);

        return result<create_character_request_data, std::string>::ok(std::move(data));

    } catch (const nlohmann::json::exception& e) {
        return result<create_character_request_data, std::string>::err(std::string("Parse error: ") + e.what());
    }
}

auto create_character_request_data::to_create_info() const -> auth::character_create_info {
    return auth::character_create_info{
        .name = name,
        .class_type = class_type,
        .nation = nation,
        .gender = gender,
        .hair_style = hair_style,
        .hair_color = hair_color,
        .skin_color = skin_color,
        .underwear_color = underwear_color,
        .strength = strength,
        .dexterity = dexterity,
        .vitality = vitality,
        .intelligence = intelligence,
        .magic = magic,
        .charisma = charisma
    };
}

auto delete_character_request_data::from_json(const nlohmann::json& j)
    -> result<delete_character_request_data, std::string>
{
    try {
        delete_character_request_data data;

        if (!j.contains("character_id") || !j["character_id"].is_number()) {
            return result<delete_character_request_data, std::string>::err("Missing or invalid 'character_id' field");
        }
        data.character_id = j["character_id"].get<uint32_t>();

        return result<delete_character_request_data, std::string>::ok(std::move(data));

    } catch (const nlohmann::json::exception& e) {
        return result<delete_character_request_data, std::string>::err(std::string("Parse error: ") + e.what());
    }
}

auto enter_game_request_data::from_json(const nlohmann::json& j)
    -> result<enter_game_request_data, std::string>
{
    try {
        enter_game_request_data data;

        if (!j.contains("character_id") || !j["character_id"].is_number()) {
            return result<enter_game_request_data, std::string>::err("Missing or invalid 'character_id' field");
        }
        data.character_id = j["character_id"].get<uint32_t>();

        // Optional: force_disconnect to kick existing session
        if (j.contains("force_disconnect") && j["force_disconnect"].is_boolean()) {
            data.force_disconnect = j["force_disconnect"].get<bool>();
        }

        // Optional: screen resolution for visibility calculation (use safe parsing)
        data.screen_width = safe_int16(j, "screen_width", 800);
        data.screen_height = safe_int16(j, "screen_height", 600);

        return result<enter_game_request_data, std::string>::ok(std::move(data));

    } catch (const nlohmann::json::exception& e) {
        return result<enter_game_request_data, std::string>::err(std::string("Parse error: ") + e.what());
    }
}

auto set_view_range_request_data::from_json(const nlohmann::json& j)
    -> result<set_view_range_request_data, std::string>
{
    try {
        set_view_range_request_data data;

        // Use safe parsing with reasonable defaults
        data.screen_width = safe_int16(j, "screen_width", 800);
        data.screen_height = safe_int16(j, "screen_height", 600);

        return result<set_view_range_request_data, std::string>::ok(std::move(data));

    } catch (const nlohmann::json::exception& e) {
        return result<set_view_range_request_data, std::string>::err(std::string("Parse error: ") + e.what());
    }
}

auto player_move_request_data::from_json(const nlohmann::json& j)
    -> result<player_move_request_data, std::string>
{
    try {
        player_move_request_data data;

        auto x_opt = safe_int16_required(j, "x");
        if (!x_opt.has_value()) {
            return result<player_move_request_data, std::string>::err("Missing or invalid 'x' field");
        }
        data.x = *x_opt;

        auto y_opt = safe_int16_required(j, "y");
        if (!y_opt.has_value()) {
            return result<player_move_request_data, std::string>::err("Missing or invalid 'y' field");
        }
        data.y = *y_opt;

        // Direction is required but clamped to 0-7
        if (!j.contains("direction") || !j["direction"].is_number()) {
            return result<player_move_request_data, std::string>::err("Missing or invalid 'direction' field");
        }
        data.direction = safe_direction(j, "direction", 0);

        if (j.contains("is_running") && j["is_running"].is_boolean()) {
            data.is_running = j["is_running"].get<bool>();
        }

        if (j.contains("timestamp") && j["timestamp"].is_number()) {
            data.timestamp = j["timestamp"].get<uint64_t>();
        }

        // Optional mouse destination coordinates
        if (j.contains("dest_x") && j["dest_x"].is_number()) {
            data.dest_x = safe_int16(j, "dest_x", 0);
        }
        if (j.contains("dest_y") && j["dest_y"].is_number()) {
            data.dest_y = safe_int16(j, "dest_y", 0);
        }

        return result<player_move_request_data, std::string>::ok(std::move(data));

    } catch (const nlohmann::json::exception& e) {
        return result<player_move_request_data, std::string>::err(std::string("Parse error: ") + e.what());
    }
}

auto player_stop_request_data::from_json(const nlohmann::json& j)
    -> result<player_stop_request_data, std::string>
{
    try {
        player_stop_request_data data;

        auto x_opt = safe_int16_required(j, "x");
        if (!x_opt.has_value()) {
            return result<player_stop_request_data, std::string>::err("Missing or invalid 'x' field");
        }
        data.x = *x_opt;

        auto y_opt = safe_int16_required(j, "y");
        if (!y_opt.has_value()) {
            return result<player_stop_request_data, std::string>::err("Missing or invalid 'y' field");
        }
        data.y = *y_opt;

        // Direction is optional but clamped to 0-7 if present
        if (j.contains("direction") && j["direction"].is_number()) {
            data.direction = safe_direction(j, "direction", 0);
        }

        if (j.contains("timestamp") && j["timestamp"].is_number()) {
            data.timestamp = j["timestamp"].get<uint64_t>();
        }

        return result<player_stop_request_data, std::string>::ok(std::move(data));

    } catch (const nlohmann::json::exception& e) {
        return result<player_stop_request_data, std::string>::err(std::string("Parse error: ") + e.what());
    }
}

namespace {
    auto parse_attack_type(const nlohmann::json& j) -> attack_type {
        if (j.is_number()) {
            auto val = j.get<int>();
            if (val >= 0 && val <= 2) {
                return static_cast<attack_type>(val);
            }
        } else if (j.is_string()) {
            auto str = j.get<std::string>();
            if (str == "regular") return attack_type::regular;
            if (str == "dash") return attack_type::dash;
            if (str == "super") return attack_type::super;
        }
        return attack_type::regular;
    }

    auto parse_target_type(const nlohmann::json& j) -> target_type {
        if (j.is_number()) {
            auto val = j.get<int>();
            if (val >= 0 && val <= 4) {
                return static_cast<target_type>(val);
            }
        } else if (j.is_string()) {
            auto str = j.get<std::string>();
            if (str == "none") return target_type::none;
            if (str == "player") return target_type::player;
            if (str == "npc") return target_type::npc;
            if (str == "ground") return target_type::ground;
            if (str == "item") return target_type::item;
        }
        return target_type::none;
    }
}  // namespace

auto player_attack_request_data::from_json(const nlohmann::json& j)
    -> result<player_attack_request_data, std::string>
{
    try {
        player_attack_request_data data;

        auto x_opt = safe_int16_required(j, "x");
        if (!x_opt.has_value()) {
            return result<player_attack_request_data, std::string>::err("Missing or invalid 'x' field");
        }
        data.x = *x_opt;

        auto y_opt = safe_int16_required(j, "y");
        if (!y_opt.has_value()) {
            return result<player_attack_request_data, std::string>::err("Missing or invalid 'y' field");
        }
        data.y = *y_opt;

        // Direction is optional but clamped to 0-7
        if (j.contains("direction") && j["direction"].is_number()) {
            data.direction = safe_direction(j, "direction", 0);
        }

        if (j.contains("attack_type")) {
            data.type = parse_attack_type(j["attack_type"]);
        }

        if (j.contains("target_type")) {
            data.tgt_type = parse_target_type(j["target_type"]);
        }

        if (j.contains("target_id") && j["target_id"].is_number()) {
            data.target_id = j["target_id"].get<uint32_t>();
        }

        if (j.contains("timestamp") && j["timestamp"].is_number()) {
            data.timestamp = j["timestamp"].get<uint64_t>();
        }

        return result<player_attack_request_data, std::string>::ok(std::move(data));

    } catch (const nlohmann::json::exception& e) {
        return result<player_attack_request_data, std::string>::err(std::string("Parse error: ") + e.what());
    }
}

auto player_magic_request_data::from_json(const nlohmann::json& j)
    -> result<player_magic_request_data, std::string>
{
    try {
        player_magic_request_data data;

        auto x_opt = safe_int16_required(j, "x");
        if (!x_opt.has_value()) {
            return result<player_magic_request_data, std::string>::err("Missing or invalid 'x' field");
        }
        data.x = *x_opt;

        auto y_opt = safe_int16_required(j, "y");
        if (!y_opt.has_value()) {
            return result<player_magic_request_data, std::string>::err("Missing or invalid 'y' field");
        }
        data.y = *y_opt;

        // Direction is optional but clamped to 0-7
        if (j.contains("direction") && j["direction"].is_number()) {
            data.direction = safe_direction(j, "direction", 0);
        }

        if (!j.contains("spell_id") || !j["spell_id"].is_number()) {
            return result<player_magic_request_data, std::string>::err("Missing or invalid 'spell_id' field");
        }
        data.spell_id = j["spell_id"].get<uint32_t>();

        if (j.contains("target_type")) {
            data.tgt_type = parse_target_type(j["target_type"]);
        }

        if (j.contains("target_id") && j["target_id"].is_number()) {
            data.target_id = j["target_id"].get<uint32_t>();
        }

        // Target coordinates use safe parsing
        data.target_x = safe_int16(j, "target_x", 0);
        data.target_y = safe_int16(j, "target_y", 0);

        if (j.contains("timestamp") && j["timestamp"].is_number()) {
            data.timestamp = j["timestamp"].get<uint64_t>();
        }

        return result<player_magic_request_data, std::string>::ok(std::move(data));

    } catch (const nlohmann::json::exception& e) {
        return result<player_magic_request_data, std::string>::err(std::string("Parse error: ") + e.what());
    }
}

auto player_skill_request_data::from_json(const nlohmann::json& j)
    -> result<player_skill_request_data, std::string>
{
    try {
        player_skill_request_data data;

        auto x_opt = safe_int16_required(j, "x");
        if (!x_opt.has_value()) {
            return result<player_skill_request_data, std::string>::err("Missing or invalid 'x' field");
        }
        data.x = *x_opt;

        auto y_opt = safe_int16_required(j, "y");
        if (!y_opt.has_value()) {
            return result<player_skill_request_data, std::string>::err("Missing or invalid 'y' field");
        }
        data.y = *y_opt;

        // Direction is optional but clamped to 0-7
        if (j.contains("direction") && j["direction"].is_number()) {
            data.direction = safe_direction(j, "direction", 0);
        }

        if (!j.contains("skill_id") || !j["skill_id"].is_number()) {
            return result<player_skill_request_data, std::string>::err("Missing or invalid 'skill_id' field");
        }
        data.skill_id = j["skill_id"].get<uint32_t>();

        if (j.contains("target_type")) {
            data.tgt_type = parse_target_type(j["target_type"]);
        }

        if (j.contains("target_id") && j["target_id"].is_number()) {
            data.target_id = j["target_id"].get<uint32_t>();
        }

        if (j.contains("timestamp") && j["timestamp"].is_number()) {
            data.timestamp = j["timestamp"].get<uint64_t>();
        }

        return result<player_skill_request_data, std::string>::ok(std::move(data));

    } catch (const nlohmann::json::exception& e) {
        return result<player_skill_request_data, std::string>::err(std::string("Parse error: ") + e.what());
    }
}

auto player_pickup_request_data::from_json(const nlohmann::json& j)
    -> result<player_pickup_request_data, std::string>
{
    try {
        player_pickup_request_data data;

        auto x_opt = safe_int16_required(j, "x");
        if (!x_opt.has_value()) {
            return result<player_pickup_request_data, std::string>::err("Missing or invalid 'x' field");
        }
        data.x = *x_opt;

        auto y_opt = safe_int16_required(j, "y");
        if (!y_opt.has_value()) {
            return result<player_pickup_request_data, std::string>::err("Missing or invalid 'y' field");
        }
        data.y = *y_opt;

        if (!j.contains("item_id") || !j["item_id"].is_number()) {
            return result<player_pickup_request_data, std::string>::err("Missing or invalid 'item_id' field");
        }
        data.item_id = j["item_id"].get<uint32_t>();

        if (j.contains("timestamp") && j["timestamp"].is_number()) {
            data.timestamp = j["timestamp"].get<uint64_t>();
        }

        return result<player_pickup_request_data, std::string>::ok(std::move(data));

    } catch (const nlohmann::json::exception& e) {
        return result<player_pickup_request_data, std::string>::err(std::string("Parse error: ") + e.what());
    }
}

auto player_interact_request_data::from_json(const nlohmann::json& j)
    -> result<player_interact_request_data, std::string>
{
    try {
        player_interact_request_data data;

        auto x_opt = safe_int16_required(j, "x");
        if (!x_opt.has_value()) {
            return result<player_interact_request_data, std::string>::err("Missing or invalid 'x' field");
        }
        data.x = *x_opt;

        auto y_opt = safe_int16_required(j, "y");
        if (!y_opt.has_value()) {
            return result<player_interact_request_data, std::string>::err("Missing or invalid 'y' field");
        }
        data.y = *y_opt;

        if (j.contains("target_type")) {
            data.tgt_type = parse_target_type(j["target_type"]);
        }

        if (!j.contains("target_id") || !j["target_id"].is_number()) {
            return result<player_interact_request_data, std::string>::err("Missing or invalid 'target_id' field");
        }
        data.target_id = j["target_id"].get<uint32_t>();

        if (j.contains("timestamp") && j["timestamp"].is_number()) {
            data.timestamp = j["timestamp"].get<uint64_t>();
        }

        return result<player_interact_request_data, std::string>::ok(std::move(data));

    } catch (const nlohmann::json::exception& e) {
        return result<player_interact_request_data, std::string>::err(std::string("Parse error: ") + e.what());
    }
}

auto chat_message_request_data::from_json(const nlohmann::json& j)
    -> result<chat_message_request_data, std::string>
{
    try {
        chat_message_request_data data;

        if (!j.contains("content") || !j["content"].is_string()) {
            return result<chat_message_request_data, std::string>::err("Missing or invalid 'content' field");
        }
        data.content = j["content"].get<std::string>();

        // Optional explicit channel override
        if (j.contains("channel") && j["channel"].is_string()) {
            data.channel = j["channel"].get<std::string>();
        }

        // Optional recipient for whispers
        if (j.contains("recipient") && j["recipient"].is_string()) {
            data.recipient_name = j["recipient"].get<std::string>();
        }

        if (j.contains("timestamp") && j["timestamp"].is_number()) {
            data.timestamp = j["timestamp"].get<uint64_t>();
        }

        return result<chat_message_request_data, std::string>::ok(std::move(data));

    } catch (const nlohmann::json::exception& e) {
        return result<chat_message_request_data, std::string>::err(std::string("Parse error: ") + e.what());
    }
}

auto chat_message_broadcast_data::to_json() const -> nlohmann::json {
    nlohmann::json data;
    data["channel"] = channel;
    data["sender_id"] = sender_id;
    data["sender_name"] = sender_name;
    data["content"] = content;
    data["timestamp"] = timestamp;

    if (!flags.empty()) {
        data["flags"] = flags;
    }

    if (recipient_name.has_value()) {
        data["recipient_name"] = *recipient_name;
    }

    return data;
}

auto command_request_data::from_json(const nlohmann::json& j)
    -> result<command_request_data, std::string>
{
    try {
        command_request_data data;

        if (!j.contains("command") || !j["command"].is_string()) {
            return result<command_request_data, std::string>::err("Missing or invalid 'command' field");
        }
        data.command = j["command"].get<std::string>();

        // Optional args array
        if (j.contains("args") && j["args"].is_array()) {
            for (const auto& arg : j["args"]) {
                if (arg.is_string()) {
                    data.args.push_back(arg.get<std::string>());
                } else {
                    // Convert non-string args to string
                    data.args.push_back(arg.dump());
                }
            }
        }

        // Optional named params object
        if (j.contains("params") && j["params"].is_object()) {
            data.params = j["params"];
        }

        if (j.contains("timestamp") && j["timestamp"].is_number()) {
            data.timestamp = j["timestamp"].get<uint64_t>();
        }

        return result<command_request_data, std::string>::ok(std::move(data));

    } catch (const nlohmann::json::exception& e) {
        return result<command_request_data, std::string>::err(std::string("Parse error: ") + e.what());
    }
}

auto command_response_data::to_json() const -> nlohmann::json {
    nlohmann::json data;
    data["success"] = success;
    data["command"] = command;
    data["message"] = message;

    if (!result.is_null()) {
        data["result"] = result;
    }

    return data;
}

// Message data to_json implementations

auto character_data_msg::to_json() const -> nlohmann::json {
    return nlohmann::json{
        {"id", id},
        {"name", name},
        {"level", level},
        {"class_type", class_type},
        {"nation", nation},
        {"gender", gender},
        {"map_name", map_name},
        {"pos_x", pos_x},
        {"pos_y", pos_y},
        {"hp", hp},
        {"hp_max", hp_max},
        {"mp", mp},
        {"mp_max", mp_max},
        {"sp", sp},
        {"sp_max", sp_max},
        {"gold", gold},
        {"str", str},
        {"dex", dex},
        {"vit", vit},
        {"int", int_},
        {"mag", mag},
        {"cha", cha},
        {"hair_style", hair_style},
        {"hair_color", hair_color},
        {"skin_color", skin_color},
        {"experience", experience},
        {"pk_count", pk_count},
        {"hunger_level", hunger_level}
    };
}

auto inventory_item_msg::to_json() const -> nlohmann::json {
    return nlohmann::json{
        {"slot", slot},
        {"item_id", item_id},
        {"name", name},
        {"count", count},
        {"durability", durability},
        {"max_durability", max_durability}
    };
}

auto equipment_item_msg::to_json() const -> nlohmann::json {
    return nlohmann::json{
        {"slot", slot},
        {"item_id", item_id},
        {"name", name},
        {"durability", durability},
        {"max_durability", max_durability}
    };
}

auto visible_entity_msg::to_json() const -> nlohmann::json {
    auto j = nlohmann::json{
        {"entity_id", entity_id},
        {"type", type},
        {"name", name},
        {"x", x},
        {"y", y},
        {"hp_percent", hp_percent},
        {"direction", direction}
    };

    // Include NPC-specific fields only for NPCs
    if (type == "npc") {
        j["template_id"] = template_id;
        j["level"] = level;
    }

    return j;
}

auto attack_result_msg::to_json() const -> nlohmann::json {
    return nlohmann::json{
        {"hit", hit},
        {"critical", critical},
        {"damage", damage},
        {"target_id", target_id},
        {"target_hp", target_hp},
        {"target_hp_max", target_hp_max},
        {"attacker_x", attacker_x},
        {"attacker_y", attacker_y}
    };
}

auto magic_result_msg::to_json() const -> nlohmann::json {
    return nlohmann::json{
        {"success", success},
        {"spell_id", spell_id},
        {"mana_cost", mana_cost},
        {"damage", damage},
        {"heal", heal},
        {"target_id", target_id},
        {"caster_mp", caster_mp}
    };
}

auto skill_result_msg::to_json() const -> nlohmann::json {
    return nlohmann::json{
        {"success", success},
        {"skill_id", skill_id},
        {"effect_value", effect_value},
        {"target_id", target_id}
    };
}

auto pickup_result_msg::to_json() const -> nlohmann::json {
    return nlohmann::json{
        {"success", success},
        {"item_id", item_id},
        {"item_name", item_name},
        {"quantity", quantity},
        {"inventory_slot", inventory_slot}
    };
}

auto interact_result_msg::to_json() const -> nlohmann::json {
    return nlohmann::json{
        {"success", success},
        {"target_id", target_id},
        {"interaction_type", interaction_type},
        {"interaction_data", interaction_data}
    };
}

auto teleporter_info_msg::to_json() const -> nlohmann::json {
    return nlohmann::json{
        {"id", id},
        {"x", x},
        {"y", y},
        {"dest_map", dest_map},
        {"dest_x", dest_x},
        {"dest_y", dest_y},
        {"dest_dir", dest_dir}
    };
}

auto map_teleporters_msg::to_json() const -> nlohmann::json {
    nlohmann::json teleporters_json = nlohmann::json::array();
    for (const auto& tp : teleporters) {
        teleporters_json.push_back(tp.to_json());
    }

    return nlohmann::json{
        {"map_name", map_name},
        {"teleporters", std::move(teleporters_json)}
    };
}

auto teleporter_update_msg::to_json() const -> nlohmann::json {
    return nlohmann::json{
        {"action", action},
        {"map_name", map_name},
        {"teleporter", teleporter.to_json()}
    };
}

auto player_teleport_msg::to_json() const -> nlohmann::json {
    nlohmann::json entities_json = nlohmann::json::array();
    for (const auto& entity : entities) {
        entities_json.push_back(entity.to_json());
    }

    return nlohmann::json{
        {"dest_map", dest_map},
        {"dest_x", dest_x},
        {"dest_y", dest_y},
        {"dest_dir", dest_dir},
        {"entities", std::move(entities_json)}
    };
}

auto known_spell_msg::to_json() const -> nlohmann::json {
    return nlohmann::json{
        {"spell_id", spell_id},
        {"level", level},
        {"total_casts", total_casts}
    };
}

auto quest_objective_msg::to_json() const -> nlohmann::json {
    return nlohmann::json{
        {"id", id},
        {"status", status},
        {"current", current},
        {"required", required}
    };
}

auto active_quest_msg::to_json() const -> nlohmann::json {
    nlohmann::json obj_json = nlohmann::json::array();
    for (const auto& obj : objectives) {
        obj_json.push_back(obj.to_json());
    }
    return nlohmann::json{
        {"quest_id", quest_id},
        {"status", status},
        {"objectives", std::move(obj_json)}
    };
}

auto game_state_msg::to_json() const -> nlohmann::json {
    nlohmann::json inv_json = nlohmann::json::array();
    for (const auto& item : inventory) {
        inv_json.push_back(item.to_json());
    }

    nlohmann::json equip_json = nlohmann::json::array();
    for (const auto& item : equipment) {
        equip_json.push_back(item.to_json());
    }

    nlohmann::json skills_json = nlohmann::json::array();
    for (const auto& [skill_id, level] : skills) {
        skills_json.push_back({{"skill_id", skill_id}, {"level", level}});
    }

    nlohmann::json spells_json = nlohmann::json::array();
    for (const auto& spell : spells) {
        spells_json.push_back(spell.to_json());
    }

    nlohmann::json quests_json = nlohmann::json::array();
    for (const auto& quest : quests) {
        quests_json.push_back(quest.to_json());
    }

    nlohmann::json completed_json = nlohmann::json::array();
    for (const auto& qid : completed_quests) {
        completed_json.push_back(qid);
    }

    nlohmann::json entities_json = nlohmann::json::array();
    for (const auto& entity : entities) {
        entities_json.push_back(entity.to_json());
    }

    return nlohmann::json{
        {"character", character.to_json()},
        {"inventory", {{"items", std::move(inv_json)}, {"gold", gold}}},
        {"equipment", std::move(equip_json)},
        {"skills", std::move(skills_json)},
        {"spells", std::move(spells_json)},
        {"quests", {{"active", std::move(quests_json)}, {"completed", std::move(completed_json)}}},
        {"world", {{"entities", std::move(entities_json)}}}
    };
}

// Response builders

auto make_error_response(uint32_t seq, std::string_view error_code,
                          std::string_view message) -> json_message
{
    return json_message{
        .type = json_message_type::error,
        .seq = seq,
        .data = nlohmann::json{
            {"error_code", std::string(error_code)},
            {"message", std::string(message)}
        }
    };
}

auto make_login_response(uint32_t seq, bool success,
                          std::optional<std::string_view> token,
                          std::optional<std::string_view> error) -> json_message
{
    nlohmann::json data;
    data["success"] = success;

    if (success && token.has_value()) {
        data["session_token"] = std::string(*token);
    }

    if (!success && error.has_value()) {
        data["error"] = std::string(*error);
    }

    return json_message{
        .type = json_message_type::login_response,
        .seq = seq,
        .data = std::move(data)
    };
}

auto make_create_account_response(uint32_t seq, bool success,
                                    std::optional<uint32_t> account_id,
                                    std::optional<std::string_view> error) -> json_message
{
    nlohmann::json data;
    data["success"] = success;

    if (success && account_id.has_value()) {
        data["account_id"] = *account_id;
    }

    if (!success && error.has_value()) {
        data["error"] = std::string(*error);
    }

    return json_message{
        .type = json_message_type::create_account_response,
        .seq = seq,
        .data = std::move(data)
    };
}

auto make_logout_response(uint32_t seq, bool success) -> json_message {
    return json_message{
        .type = json_message_type::logout_response,
        .seq = seq,
        .data = nlohmann::json{{"success", success}}
    };
}

auto make_get_characters_response(uint32_t seq,
                                    const std::vector<auth::character_summary>& characters) -> json_message
{
    nlohmann::json chars_json = nlohmann::json::array();

    for (const auto& ch : characters) {
        chars_json.push_back({
            {"id", ch.id.value},
            {"name", ch.name},
            {"level", ch.level},
            {"class_type", ch.class_type},
            {"nation", ch.nation},
            {"gender", ch.gender},
            {"map_name", ch.map_name},
            {"experience", ch.experience},
            {"hair_style", ch.hair_style},
            {"hair_color", ch.hair_color},
            {"skin_color", ch.skin_color}
        });
    }

    return json_message{
        .type = json_message_type::get_characters_response,
        .seq = seq,
        .data = nlohmann::json{
            {"success", true},
            {"characters", std::move(chars_json)}
        }
    };
}

auto make_create_character_response(uint32_t seq, bool success,
                                      std::optional<uint32_t> character_id,
                                      std::optional<std::string_view> error) -> json_message
{
    nlohmann::json data;
    data["success"] = success;

    if (success && character_id.has_value()) {
        data["character_id"] = *character_id;
    }

    if (!success && error.has_value()) {
        data["error"] = std::string(*error);
    }

    return json_message{
        .type = json_message_type::create_character_response,
        .seq = seq,
        .data = std::move(data)
    };
}

auto make_delete_character_response(uint32_t seq, bool success,
                                      std::optional<std::string_view> error) -> json_message
{
    nlohmann::json data;
    data["success"] = success;

    if (!success && error.has_value()) {
        data["error"] = std::string(*error);
    }

    return json_message{
        .type = json_message_type::delete_character_response,
        .seq = seq,
        .data = std::move(data)
    };
}

auto make_enter_game_response(uint32_t seq, bool success,
                               const game_state_msg* game_state,
                               std::optional<std::string_view> error) -> json_message
{
    nlohmann::json data;
    data["success"] = success;

    if (success && game_state != nullptr) {
        auto state_json = game_state->to_json();
        // Merge game state into data (character, inventory, equipment, skills, world)
        for (auto& [key, value] : state_json.items()) {
            data[key] = std::move(value);
        }
    }

    if (!success && error.has_value()) {
        data["error"] = std::string(*error);
    }

    return json_message{
        .type = json_message_type::enter_game_response,
        .seq = seq,
        .data = std::move(data)
    };
}

auto make_inventory_data(uint32_t seq,
                          const std::vector<inventory_item_msg>& items,
                          int32_t gold) -> json_message
{
    nlohmann::json items_json = nlohmann::json::array();
    for (const auto& item : items) {
        items_json.push_back(item.to_json());
    }

    return json_message{
        .type = json_message_type::inventory_data,
        .seq = seq,
        .data = nlohmann::json{
            {"items", std::move(items_json)},
            {"gold", gold}
        }
    };
}

auto make_equipment_data(uint32_t seq,
                          const std::vector<equipment_item_msg>& equipped) -> json_message
{
    nlohmann::json items_json = nlohmann::json::array();
    for (const auto& item : equipped) {
        items_json.push_back(item.to_json());
    }

    return json_message{
        .type = json_message_type::equipment_data,
        .seq = seq,
        .data = nlohmann::json{
            {"equipment", std::move(items_json)}
        }
    };
}

auto make_skills_data(uint32_t seq,
                       const std::vector<std::pair<uint8_t, int16_t>>& skills) -> json_message
{
    nlohmann::json skills_json = nlohmann::json::array();
    for (const auto& [skill_id, level] : skills) {
        skills_json.push_back({
            {"skill_id", skill_id},
            {"level", level}
        });
    }

    return json_message{
        .type = json_message_type::skills_data,
        .seq = seq,
        .data = nlohmann::json{
            {"skills", std::move(skills_json)}
        }
    };
}

auto make_world_init(uint32_t seq,
                      const std::vector<visible_entity_msg>& entities) -> json_message
{
    nlohmann::json entities_json = nlohmann::json::array();
    for (const auto& entity : entities) {
        entities_json.push_back(entity.to_json());
    }

    return json_message{
        .type = json_message_type::world_init,
        .seq = seq,
        .data = nlohmann::json{
            {"entities", std::move(entities_json)}
        }
    };
}

auto make_entity_spawn(uint32_t seq,
                        const visible_entity_msg& entity) -> json_message
{
    return json_message{
        .type = json_message_type::entity_spawn,
        .seq = seq,
        .data = entity.to_json()
    };
}

auto make_entity_despawn(uint32_t seq, uint32_t entity_id) -> json_message
{
    return json_message{
        .type = json_message_type::entity_despawn,
        .seq = seq,
        .data = nlohmann::json{{"entity_id", entity_id}}
    };
}

auto make_player_move_response(uint32_t seq, bool success,
                                int16_t x, int16_t y, int16_t direction,
                                std::optional<std::string_view> error) -> json_message
{
    nlohmann::json data;
    data["success"] = success;

    if (success) {
        data["x"] = x;
        data["y"] = y;
        data["direction"] = direction;
    }

    if (!success && error.has_value()) {
        data["error"] = std::string(*error);
    }

    return json_message{
        .type = json_message_type::player_move_response,
        .seq = seq,
        .data = std::move(data)
    };
}

auto make_player_stop_response(uint32_t seq, bool success,
                                int16_t x, int16_t y, int16_t direction,
                                std::optional<std::string_view> error) -> json_message
{
    nlohmann::json data;
    data["success"] = success;

    if (success) {
        data["x"] = x;
        data["y"] = y;
        data["direction"] = direction;
    }

    if (!success && error.has_value()) {
        data["error"] = std::string(*error);
    }

    return json_message{
        .type = json_message_type::player_stop_response,
        .seq = seq,
        .data = std::move(data)
    };
}

auto make_player_position_update(uint32_t entity_id,
                                  int16_t x, int16_t y, int16_t direction,
                                  bool is_running,
                                  std::optional<int16_t> dest_x,
                                  std::optional<int16_t> dest_y) -> json_message
{
    nlohmann::json data{
        {"entity_id", entity_id},
        {"x", x},
        {"y", y},
        {"direction", direction},
        {"is_running", is_running}
    };

    // Include destination coordinates if available
    if (dest_x.has_value() && dest_y.has_value()) {
        data["dest_x"] = *dest_x;
        data["dest_y"] = *dest_y;
    }

    return json_message{
        .type = json_message_type::player_position_update,
        .seq = 0,  // Broadcasts don't need seq
        .data = std::move(data)
    };
}

auto make_player_attack_response(uint32_t seq, bool success,
                                  const attack_result_msg* result,
                                  std::optional<std::string_view> error) -> json_message
{
    nlohmann::json data;
    data["success"] = success;

    if (success && result != nullptr) {
        data["result"] = result->to_json();
    }

    if (!success && error.has_value()) {
        data["error"] = std::string(*error);
    }

    return json_message{
        .type = json_message_type::player_attack_response,
        .seq = seq,
        .data = std::move(data)
    };
}

auto make_player_magic_response(uint32_t seq, bool success,
                                 const magic_result_msg* result,
                                 std::optional<std::string_view> error) -> json_message
{
    nlohmann::json data;
    data["success"] = success;

    if (success && result != nullptr) {
        data["result"] = result->to_json();
    }

    if (!success && error.has_value()) {
        data["error"] = std::string(*error);
    }

    return json_message{
        .type = json_message_type::player_magic_response,
        .seq = seq,
        .data = std::move(data)
    };
}

auto make_player_skill_response(uint32_t seq, bool success,
                                 const skill_result_msg* result,
                                 std::optional<std::string_view> error) -> json_message
{
    nlohmann::json data;
    data["success"] = success;

    if (success && result != nullptr) {
        data["result"] = result->to_json();
    }

    if (!success && error.has_value()) {
        data["error"] = std::string(*error);
    }

    return json_message{
        .type = json_message_type::player_skill_response,
        .seq = seq,
        .data = std::move(data)
    };
}

auto make_player_pickup_response(uint32_t seq, bool success,
                                  const pickup_result_msg* result,
                                  std::optional<std::string_view> error) -> json_message
{
    nlohmann::json data;
    data["success"] = success;

    if (success && result != nullptr) {
        data["result"] = result->to_json();
    }

    if (!success && error.has_value()) {
        data["error"] = std::string(*error);
    }

    return json_message{
        .type = json_message_type::player_pickup_response,
        .seq = seq,
        .data = std::move(data)
    };
}

auto make_player_interact_response(uint32_t seq, bool success,
                                    const interact_result_msg* result,
                                    std::optional<std::string_view> error) -> json_message
{
    nlohmann::json data;
    data["success"] = success;

    if (success && result != nullptr) {
        data["result"] = result->to_json();
    }

    if (!success && error.has_value()) {
        data["error"] = std::string(*error);
    }

    return json_message{
        .type = json_message_type::player_interact_response,
        .seq = seq,
        .data = std::move(data)
    };
}

auto make_pong_response(uint32_t seq) -> json_message {
    return json_message{
        .type = json_message_type::pong,
        .seq = seq,
        .data = nlohmann::json{
            {"timestamp", std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::system_clock::now().time_since_epoch()).count()}
        }
    };
}

auto make_chat_message_response(uint32_t seq, bool success,
                                 std::optional<std::string_view> error) -> json_message
{
    nlohmann::json data;
    data["success"] = success;

    if (!success && error.has_value()) {
        data["error"] = std::string(*error);
    }

    return json_message{
        .type = json_message_type::chat_message,
        .seq = seq,
        .data = std::move(data)
    };
}

auto make_chat_message_broadcast(const chat_message_broadcast_data& data) -> json_message {
    return json_message{
        .type = json_message_type::chat_message_broadcast,
        .seq = 0,  // Broadcasts don't need seq
        .data = data.to_json()
    };
}

auto make_command_response(uint32_t seq, bool success,
                            std::string_view command,
                            std::string_view message,
                            const nlohmann::json& result) -> json_message
{
    nlohmann::json data;
    data["success"] = success;
    data["command"] = std::string(command);
    data["message"] = std::string(message);

    if (!result.is_null() && !result.empty()) {
        data["result"] = result;
    }

    return json_message{
        .type = json_message_type::command_response,
        .seq = seq,
        .data = std::move(data)
    };
}

auto make_map_teleporters(const map_teleporters_msg& data) -> json_message {
    return json_message{
        .type = json_message_type::map_teleporters,
        .seq = 0,  // Broadcasts don't need seq
        .data = data.to_json()
    };
}

auto make_teleporter_update(const teleporter_update_msg& data) -> json_message {
    return json_message{
        .type = json_message_type::teleporter_update,
        .seq = 0,  // Broadcasts don't need seq
        .data = data.to_json()
    };
}

auto make_player_teleport(uint32_t seq, const player_teleport_msg& data) -> json_message {
    return json_message{
        .type = json_message_type::player_teleport,
        .seq = seq,
        .data = data.to_json()
    };
}

auto make_combat_attack_broadcast(uint32_t attacker_id, uint32_t target_id,
                                   int16_t attacker_x, int16_t attacker_y,
                                   int16_t target_x, int16_t target_y,
                                   bool hit, bool critical, int32_t damage) -> json_message
{
    return json_message{
        .type = json_message_type::combat_attack_broadcast,
        .seq = 0,  // Broadcasts don't need seq
        .data = nlohmann::json{
            {"attacker_id", attacker_id},
            {"target_id", target_id},
            {"attacker_x", attacker_x},
            {"attacker_y", attacker_y},
            {"target_x", target_x},
            {"target_y", target_y},
            {"hit", hit},
            {"critical", critical},
            {"damage", damage}
        }
    };
}

auto make_entity_hp_update(uint32_t entity_id, int32_t hp, int32_t hp_max) -> json_message
{
    return json_message{
        .type = json_message_type::entity_hp_update,
        .seq = 0,  // Broadcasts don't need seq
        .data = nlohmann::json{
            {"entity_id", entity_id},
            {"hp", hp},
            {"hp_max", hp_max}
        }
    };
}

auto make_entity_death(uint32_t victim_id, uint32_t killer_id,
                        int16_t x, int16_t y) -> json_message
{
    return json_message{
        .type = json_message_type::entity_death,
        .seq = 0,  // Broadcasts don't need seq
        .data = nlohmann::json{
            {"victim_id", victim_id},
            {"killer_id", killer_id},
            {"x", x},
            {"y", y}
        }
    };
}

// Combat effect data to_json implementation

auto combat_effect_data::to_json() const -> nlohmann::json {
    nlohmann::json j{
        {"source_id", source_id},
        {"target_id", target_id},
        {"effect_type", effect_type},
        {"value", value},
        {"is_critical", is_critical},
        {"target_x", target_x},
        {"target_y", target_y}
    };

    if (!damage_type.empty()) {
        j["damage_type"] = damage_type;
    }
    if (spell_id != 0) {
        j["spell_id"] = spell_id;
    }

    return j;
}

auto make_combat_effect(const combat_effect_data& data) -> json_message {
    return json_message{
        .type = json_message_type::combat_effect,
        .seq = 0,
        .data = data.to_json()
    };
}

// NPC data to_json implementations

auto npc_spawn_data::to_json() const -> nlohmann::json {
    return nlohmann::json{
        {"entity_id", entity_id},
        {"template_id", template_id},
        {"name", name},
        {"x", x},
        {"y", y},
        {"direction", direction},
        {"hp", hp},
        {"max_hp", max_hp},
        {"level", level}
    };
}

auto npc_move_data::to_json() const -> nlohmann::json {
    return nlohmann::json{
        {"entity_id", entity_id},
        {"x", x},
        {"y", y},
        {"direction", direction}
    };
}

auto npc_attack_data::to_json() const -> nlohmann::json {
    return nlohmann::json{
        {"attacker_id", attacker_id},
        {"target_id", target_id},
        {"damage", damage},
        {"is_critical", is_critical}
    };
}

auto npc_death_data::to_json() const -> nlohmann::json {
    return nlohmann::json{
        {"entity_id", entity_id},
        {"killer_id", killer_id},
        {"x", x},
        {"y", y}
    };
}

// NPC message builders

auto make_npc_spawn_message(const npc_spawn_data& data) -> json_message {
    return json_message{
        .type = json_message_type::npc_spawn,
        .seq = 0,
        .data = data.to_json()
    };
}

auto make_npc_despawn_message(uint32_t entity_id) -> json_message {
    return json_message{
        .type = json_message_type::npc_despawn,
        .seq = 0,
        .data = nlohmann::json{{"entity_id", entity_id}}
    };
}

auto make_npc_move_message(const npc_move_data& data) -> json_message {
    return json_message{
        .type = json_message_type::npc_move,
        .seq = 0,
        .data = data.to_json()
    };
}

auto make_npc_attack_message(const npc_attack_data& data) -> json_message {
    return json_message{
        .type = json_message_type::npc_attack,
        .seq = 0,
        .data = data.to_json()
    };
}

auto make_npc_death_message(const npc_death_data& data) -> json_message {
    return json_message{
        .type = json_message_type::npc_death,
        .seq = 0,
        .data = data.to_json()
    };
}

// Ground item spawn data to_json implementation

auto ground_item_spawn_data::to_json() const -> nlohmann::json {
    return nlohmann::json{
        {"item_id", item_id},
        {"template_id", template_id},
        {"item_name", item_name},
        {"count", count},
        {"x", x},
        {"y", y}
    };
}

// Ground item spawn message builder

auto make_ground_item_spawn(const ground_item_spawn_data& data) -> json_message {
    return json_message{
        .type = json_message_type::ground_item_spawn,
        .seq = 0,  // Broadcasts don't need seq
        .data = data.to_json()
    };
}

// Ground item data to_json implementation

auto ground_item_removed_data::to_json() const -> nlohmann::json {
    return nlohmann::json{
        {"picker_id", picker_id},
        {"picker_name", picker_name},
        {"item_id", item_id},
        {"item_name", item_name},
        {"x", x},
        {"y", y}
    };
}

// Ground item message builder

auto make_ground_item_removed(const ground_item_removed_data& data) -> json_message {
    return json_message{
        .type = json_message_type::ground_item_removed,
        .seq = 0,  // Broadcasts don't need seq
        .data = data.to_json()
    };
}

// Player death info data to_json implementation

auto player_death_info_data::to_json() const -> nlohmann::json {
    return nlohmann::json{
        {"killer_id", killer_id},
        {"killer_name", killer_name},
        {"is_pvp", is_pvp},
        {"xp_lost", xp_lost},
        {"pk_points_change", pk_points_change},
        {"gold_reward", gold_reward},
        {"respawn_delay_ms", respawn_delay_ms},
        {"respawn_map", respawn_map},
        {"respawn_x", respawn_x},
        {"respawn_y", respawn_y}
    };
}

// Player death info message builder

auto make_player_death_info(const player_death_info_data& data) -> json_message {
    return json_message{
        .type = json_message_type::player_death_info,
        .seq = 0,
        .data = data.to_json()
    };
}

// Hunger update data to_json implementation

auto hunger_update_data::to_json() const -> nlohmann::json {
    return nlohmann::json{
        {"level", level},
        {"is_starving", is_starving}
    };
}

// Hunger update message builder

auto make_hunger_update(int8_t level) -> json_message {
    return json_message{
        .type = json_message_type::hunger_update,
        .seq = 0,  // Broadcasts don't need seq
        .data = hunger_update_data{level, level <= 0}.to_json()
    };
}

// Entity info request data from_json implementation

auto entity_info_request_data::from_json(const nlohmann::json& j) -> result<entity_info_request_data, std::string> {
    try {
        entity_info_request_data data;

        if (!j.contains("entity_id") || !j["entity_id"].is_number()) {
            return result<entity_info_request_data, std::string>::err("Missing or invalid 'entity_id' field");
        }
        data.entity_id = j["entity_id"].get<uint32_t>();

        return result<entity_info_request_data, std::string>::ok(std::move(data));

    } catch (const nlohmann::json::exception& e) {
        return result<entity_info_request_data, std::string>::err(std::string("Parse error: ") + e.what());
    }
}

// Entity info response data to_json implementation

auto entity_info_response_data::to_json() const -> nlohmann::json {
    nlohmann::json j = {
        {"entity_id", entity_id},
        {"entity_type", entity_type},
        {"name", name},
        {"level", level},
        {"hp", hp},
        {"hp_max", hp_max},
        {"x", x},
        {"y", y},
        {"direction", direction}
    };

    // Add player-specific fields if present
    if (faction.has_value()) {
        j["faction"] = *faction;
    }
    if (guild_name.has_value()) {
        j["guild_name"] = *guild_name;
    }
    if (class_type.has_value()) {
        j["class_type"] = *class_type;
    }
    if (pk_count.has_value()) {
        j["pk_count"] = *pk_count;
    }

    // Add NPC-specific fields if present
    if (template_id.has_value()) {
        j["template_id"] = *template_id;
    }
    if (npc_type.has_value()) {
        j["npc_type"] = *npc_type;
    }

    return j;
}

// Entity info response message builder

auto make_entity_info_response(uint32_t seq, bool success,
                                const entity_info_response_data* data,
                                std::optional<std::string_view> error) -> json_message
{
    nlohmann::json response_data;
    response_data["success"] = success;

    if (success && data != nullptr) {
        response_data["entity"] = data->to_json();
    }

    if (!success && error.has_value()) {
        response_data["error"] = std::string(*error);
    }

    return json_message{
        .type = json_message_type::entity_info_response,
        .seq = seq,
        .data = std::move(response_data)
    };
}

// === NPC Interaction: from_json implementations ===

auto shop_buy_request_data::from_json(const nlohmann::json& j) -> result<shop_buy_request_data, std::string> {
    shop_buy_request_data data;
    if (!j.contains("npc_entity_id") || !j["npc_entity_id"].is_number()) {
        return result<shop_buy_request_data, std::string>::err("Missing npc_entity_id");
    }
    data.npc_entity_id = j["npc_entity_id"].get<uint32_t>();

    if (!j.contains("item_template_id") || !j["item_template_id"].is_number()) {
        return result<shop_buy_request_data, std::string>::err("Missing item_template_id");
    }
    data.item_template_id = j["item_template_id"].get<uint32_t>();

    if (j.contains("count") && j["count"].is_number()) {
        data.count = static_cast<int16_t>(j["count"].get<int>());
    }
    return result<shop_buy_request_data, std::string>::ok(std::move(data));
}

auto shop_sell_request_data::from_json(const nlohmann::json& j) -> result<shop_sell_request_data, std::string> {
    shop_sell_request_data data;
    if (!j.contains("npc_entity_id") || !j["npc_entity_id"].is_number()) {
        return result<shop_sell_request_data, std::string>::err("Missing npc_entity_id");
    }
    data.npc_entity_id = j["npc_entity_id"].get<uint32_t>();

    if (!j.contains("inventory_slot") || !j["inventory_slot"].is_number()) {
        return result<shop_sell_request_data, std::string>::err("Missing inventory_slot");
    }
    data.inventory_slot = static_cast<int16_t>(j["inventory_slot"].get<int>());

    if (j.contains("count") && j["count"].is_number()) {
        data.count = static_cast<int16_t>(j["count"].get<int>());
    }
    return result<shop_sell_request_data, std::string>::ok(std::move(data));
}

auto shop_sell_confirm_request_data::from_json(const nlohmann::json& j) -> result<shop_sell_confirm_request_data, std::string> {
    shop_sell_confirm_request_data data;
    if (!j.contains("npc_entity_id") || !j["npc_entity_id"].is_number()) {
        return result<shop_sell_confirm_request_data, std::string>::err("Missing npc_entity_id");
    }
    data.npc_entity_id = j["npc_entity_id"].get<uint32_t>();

    if (!j.contains("inventory_slot") || !j["inventory_slot"].is_number()) {
        return result<shop_sell_confirm_request_data, std::string>::err("Missing inventory_slot");
    }
    data.inventory_slot = static_cast<int16_t>(j["inventory_slot"].get<int>());

    if (j.contains("count") && j["count"].is_number()) {
        data.count = static_cast<int16_t>(j["count"].get<int>());
    }
    return result<shop_sell_confirm_request_data, std::string>::ok(std::move(data));
}

auto shop_repair_request_data::from_json(const nlohmann::json& j) -> result<shop_repair_request_data, std::string> {
    shop_repair_request_data data;
    if (!j.contains("npc_entity_id") || !j["npc_entity_id"].is_number()) {
        return result<shop_repair_request_data, std::string>::err("Missing npc_entity_id");
    }
    data.npc_entity_id = j["npc_entity_id"].get<uint32_t>();

    if (!j.contains("inventory_slot") || !j["inventory_slot"].is_number()) {
        return result<shop_repair_request_data, std::string>::err("Missing inventory_slot");
    }
    data.inventory_slot = static_cast<int16_t>(j["inventory_slot"].get<int>());
    return result<shop_repair_request_data, std::string>::ok(std::move(data));
}

auto shop_repair_confirm_request_data::from_json(const nlohmann::json& j) -> result<shop_repair_confirm_request_data, std::string> {
    shop_repair_confirm_request_data data;
    if (!j.contains("npc_entity_id") || !j["npc_entity_id"].is_number()) {
        return result<shop_repair_confirm_request_data, std::string>::err("Missing npc_entity_id");
    }
    data.npc_entity_id = j["npc_entity_id"].get<uint32_t>();

    if (!j.contains("inventory_slot") || !j["inventory_slot"].is_number()) {
        return result<shop_repair_confirm_request_data, std::string>::err("Missing inventory_slot");
    }
    data.inventory_slot = static_cast<int16_t>(j["inventory_slot"].get<int>());
    return result<shop_repair_confirm_request_data, std::string>::ok(std::move(data));
}

auto bank_deposit_request_data::from_json(const nlohmann::json& j) -> result<bank_deposit_request_data, std::string> {
    bank_deposit_request_data data;
    if (!j.contains("npc_entity_id") || !j["npc_entity_id"].is_number()) {
        return result<bank_deposit_request_data, std::string>::err("Missing npc_entity_id");
    }
    data.npc_entity_id = j["npc_entity_id"].get<uint32_t>();

    if (!j.contains("inventory_slot") || !j["inventory_slot"].is_number()) {
        return result<bank_deposit_request_data, std::string>::err("Missing inventory_slot");
    }
    data.inventory_slot = static_cast<int16_t>(j["inventory_slot"].get<int>());
    return result<bank_deposit_request_data, std::string>::ok(std::move(data));
}

auto bank_withdraw_request_data::from_json(const nlohmann::json& j) -> result<bank_withdraw_request_data, std::string> {
    bank_withdraw_request_data data;
    if (!j.contains("npc_entity_id") || !j["npc_entity_id"].is_number()) {
        return result<bank_withdraw_request_data, std::string>::err("Missing npc_entity_id");
    }
    data.npc_entity_id = j["npc_entity_id"].get<uint32_t>();

    if (!j.contains("bank_slot") || !j["bank_slot"].is_number()) {
        return result<bank_withdraw_request_data, std::string>::err("Missing bank_slot");
    }
    data.bank_slot = static_cast<int16_t>(j["bank_slot"].get<int>());
    return result<bank_withdraw_request_data, std::string>::ok(std::move(data));
}

auto dialog_choice_request_data::from_json(const nlohmann::json& j) -> result<dialog_choice_request_data, std::string> {
    dialog_choice_request_data data;
    if (!j.contains("npc_entity_id") || !j["npc_entity_id"].is_number()) {
        return result<dialog_choice_request_data, std::string>::err("Missing npc_entity_id");
    }
    data.npc_entity_id = j["npc_entity_id"].get<uint32_t>();

    if (j.contains("node_id") && j["node_id"].is_string()) {
        data.node_id = j["node_id"].get<std::string>();
    }
    if (j.contains("choice_index") && j["choice_index"].is_number()) {
        data.choice_index = static_cast<int16_t>(j["choice_index"].get<int>());
    }
    return result<dialog_choice_request_data, std::string>::ok(std::move(data));
}

// === NPC Interaction: builder implementations ===

auto make_shop_buy_response(uint32_t seq, bool success,
                             std::string_view item_name,
                             int16_t count,
                             int32_t price_paid,
                             int64_t gold_remaining,
                             std::optional<std::string_view> error) -> json_message {
    nlohmann::json data;
    data["success"] = success;
    if (success) {
        data["item_name"] = std::string(item_name);
        data["count"] = count;
        data["price_paid"] = price_paid;
        data["gold_remaining"] = gold_remaining;
    }
    if (!success && error.has_value()) {
        data["error"] = std::string(*error);
    }
    return json_message{
        .type = json_message_type::shop_buy_response,
        .seq = seq,
        .data = std::move(data)
    };
}

auto make_shop_sell_response(uint32_t seq, bool success,
                              std::string_view item_name,
                              int32_t offered_price,
                              int16_t durability,
                              std::optional<std::string_view> error) -> json_message {
    nlohmann::json data;
    data["success"] = success;
    if (success) {
        data["item_name"] = std::string(item_name);
        data["offered_price"] = offered_price;
        data["durability"] = durability;
    }
    if (!success && error.has_value()) {
        data["error"] = std::string(*error);
    }
    return json_message{
        .type = json_message_type::shop_sell_response,
        .seq = seq,
        .data = std::move(data)
    };
}

auto make_shop_sell_confirm_response(uint32_t seq, bool success,
                                      int32_t gold_received,
                                      int64_t gold_total,
                                      std::optional<std::string_view> error) -> json_message {
    nlohmann::json data;
    data["success"] = success;
    if (success) {
        data["gold_received"] = gold_received;
        data["gold_total"] = gold_total;
    }
    if (!success && error.has_value()) {
        data["error"] = std::string(*error);
    }
    return json_message{
        .type = json_message_type::shop_sell_confirm_response,
        .seq = seq,
        .data = std::move(data)
    };
}

auto make_shop_repair_response(uint32_t seq, bool success,
                                std::string_view item_name,
                                int32_t repair_cost,
                                int16_t durability,
                                std::optional<std::string_view> error) -> json_message {
    nlohmann::json data;
    data["success"] = success;
    if (success) {
        data["item_name"] = std::string(item_name);
        data["repair_cost"] = repair_cost;
        data["durability"] = durability;
    }
    if (!success && error.has_value()) {
        data["error"] = std::string(*error);
    }
    return json_message{
        .type = json_message_type::shop_repair_response,
        .seq = seq,
        .data = std::move(data)
    };
}

auto make_shop_repair_confirm_response(uint32_t seq, bool success,
                                        int16_t new_durability,
                                        int32_t gold_spent,
                                        int64_t gold_remaining,
                                        std::optional<std::string_view> error) -> json_message {
    nlohmann::json data;
    data["success"] = success;
    if (success) {
        data["new_durability"] = new_durability;
        data["gold_spent"] = gold_spent;
        data["gold_remaining"] = gold_remaining;
    }
    if (!success && error.has_value()) {
        data["error"] = std::string(*error);
    }
    return json_message{
        .type = json_message_type::shop_repair_confirm_response,
        .seq = seq,
        .data = std::move(data)
    };
}

auto make_bank_deposit_response(uint32_t seq, bool success,
                                 std::string_view item_name,
                                 std::optional<std::string_view> error) -> json_message {
    nlohmann::json data;
    data["success"] = success;
    if (success) {
        data["item_name"] = std::string(item_name);
    }
    if (!success && error.has_value()) {
        data["error"] = std::string(*error);
    }
    return json_message{
        .type = json_message_type::bank_deposit_response,
        .seq = seq,
        .data = std::move(data)
    };
}

auto make_bank_withdraw_response(uint32_t seq, bool success,
                                  std::string_view item_name,
                                  std::optional<std::string_view> error) -> json_message {
    nlohmann::json data;
    data["success"] = success;
    if (success) {
        data["item_name"] = std::string(item_name);
    }
    if (!success && error.has_value()) {
        data["error"] = std::string(*error);
    }
    return json_message{
        .type = json_message_type::bank_withdraw_response,
        .seq = seq,
        .data = std::move(data)
    };
}

auto dialog_option_msg::to_json() const -> nlohmann::json {
    nlohmann::json j;
    j["label"] = label;
    j["action"] = action;
    if (!next_node.empty()) {
        j["next_node"] = next_node;
    }
    return j;
}

auto make_dialog_choice_response(uint32_t seq, bool success,
                                  std::string_view action,
                                  std::string_view node_id,
                                  std::string_view text,
                                  const std::vector<dialog_option_msg>& options,
                                  std::optional<std::string_view> error) -> json_message {
    nlohmann::json data;
    data["success"] = success;
    if (success) {
        data["action"] = std::string(action);
        if (!node_id.empty()) {
            data["node_id"] = std::string(node_id);
        }
        if (!text.empty()) {
            data["text"] = std::string(text);
        }
        if (!options.empty()) {
            auto opts = nlohmann::json::array();
            for (const auto& opt : options) {
                opts.push_back(opt.to_json());
            }
            data["options"] = std::move(opts);
        }
    }
    if (!success && error.has_value()) {
        data["error"] = std::string(*error);
    }
    return json_message{
        .type = json_message_type::dialog_choice_response,
        .seq = seq,
        .data = std::move(data)
    };
}

// === Equipment: Equip/Unequip ===

auto player_equip_request_data::from_json(const nlohmann::json& j)
    -> result<player_equip_request_data, std::string>
{
    try {
        player_equip_request_data data;
        auto slot = safe_int16_required(j, "inventory_slot");
        if (!slot) {
            return result<player_equip_request_data, std::string>::err("Missing or invalid 'inventory_slot'");
        }
        data.inventory_slot = *slot;

        if (!j.contains("target_slot") || !j["target_slot"].is_number()) {
            return result<player_equip_request_data, std::string>::err("Missing or invalid 'target_slot'");
        }
        data.target_slot = j["target_slot"].get<uint8_t>();

        return result<player_equip_request_data, std::string>::ok(std::move(data));
    } catch (const nlohmann::json::exception& e) {
        return result<player_equip_request_data, std::string>::err(std::string("Parse error: ") + e.what());
    }
}

auto player_unequip_request_data::from_json(const nlohmann::json& j)
    -> result<player_unequip_request_data, std::string>
{
    try {
        player_unequip_request_data data;
        if (!j.contains("equip_slot") || !j["equip_slot"].is_number()) {
            return result<player_unequip_request_data, std::string>::err("Missing or invalid 'equip_slot'");
        }
        data.equip_slot = j["equip_slot"].get<uint8_t>();
        return result<player_unequip_request_data, std::string>::ok(std::move(data));
    } catch (const nlohmann::json::exception& e) {
        return result<player_unequip_request_data, std::string>::err(std::string("Parse error: ") + e.what());
    }
}

auto equip_result_msg::to_json() const -> nlohmann::json {
    nlohmann::json j;
    j["success"] = success;
    j["slot"] = slot;
    if (success) {
        j["item_id"] = item_id;
        j["item_name"] = item_name;
        j["durability"] = durability;
        j["max_durability"] = max_durability;
        if (swapped_item_id.has_value()) {
            j["swapped_item_id"] = *swapped_item_id;
            if (swapped_to_inv_slot.has_value()) {
                j["swapped_to_inv_slot"] = *swapped_to_inv_slot;
            }
        }
        if (unequipped_shield_id.has_value()) {
            j["unequipped_shield_id"] = *unequipped_shield_id;
            if (shield_to_inv_slot.has_value()) {
                j["shield_to_inv_slot"] = *shield_to_inv_slot;
            }
        }
    }
    if (!success && !error.empty()) {
        j["error"] = error;
    }
    return j;
}

auto unequip_result_msg::to_json() const -> nlohmann::json {
    nlohmann::json j;
    j["success"] = success;
    j["slot"] = slot;
    if (success) {
        j["item_id"] = item_id;
        j["item_name"] = item_name;
        j["inventory_slot"] = inventory_slot;
    }
    if (!success && !error.empty()) {
        j["error"] = error;
    }
    return j;
}

auto equipment_change_broadcast_data::to_json() const -> nlohmann::json {
    return nlohmann::json{
        {"entity_id", entity_id},
        {"slot", slot},
        {"item_id", item_id},
        {"template_id", template_id}
    };
}

auto stat_update_data::to_json() const -> nlohmann::json {
    return nlohmann::json{
        {"max_hp", max_hp},
        {"max_mp", max_mp},
        {"max_sp", max_sp},
        {"attack_power", attack_power},
        {"magic_power", magic_power},
        {"defense", defense},
        {"magic_defense", magic_defense},
        {"hit_rate", hit_rate},
        {"dodge_rate", dodge_rate},
        {"critical_rate", critical_rate}
    };
}

auto make_player_equip_response(uint32_t seq, const equip_result_msg& result) -> json_message {
    return json_message{
        .type = json_message_type::player_equip_response,
        .seq = seq,
        .data = result.to_json()
    };
}

auto make_player_unequip_response(uint32_t seq, const unequip_result_msg& result) -> json_message {
    return json_message{
        .type = json_message_type::player_unequip_response,
        .seq = seq,
        .data = result.to_json()
    };
}

auto make_equipment_change_broadcast(const equipment_change_broadcast_data& data) -> json_message {
    return json_message{
        .type = json_message_type::equipment_change_broadcast,
        .seq = 0,
        .data = data.to_json()
    };
}

auto make_stat_update(const stat_update_data& data) -> json_message {
    return json_message{
        .type = json_message_type::stat_update,
        .seq = 0,
        .data = data.to_json()
    };
}

}  // namespace hb::network
