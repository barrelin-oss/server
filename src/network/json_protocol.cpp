// json_protocol.cpp
// JSON-based message protocol implementation

#include "network/json_protocol.h"
#include "core/logger.h"

#include <unordered_map>

namespace hb::network {

namespace {

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
    {"player_position_update", json_message_type::player_position_update},
    {"player_action", json_message_type::player_action},
    {"chat_message", json_message_type::chat_message}
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

        // Optional fields with defaults
        if (j.contains("class_type") && j["class_type"].is_number()) {
            data.class_type = static_cast<int16_t>(j["class_type"].get<int>());
        }
        if (j.contains("nation") && j["nation"].is_number()) {
            data.nation = static_cast<int16_t>(j["nation"].get<int>());
        }
        if (j.contains("gender") && j["gender"].is_number()) {
            data.gender = static_cast<int16_t>(j["gender"].get<int>());
        }
        if (j.contains("hair_style") && j["hair_style"].is_number()) {
            data.hair_style = static_cast<int16_t>(j["hair_style"].get<int>());
        }
        if (j.contains("hair_color") && j["hair_color"].is_number()) {
            data.hair_color = static_cast<int16_t>(j["hair_color"].get<int>());
        }
        if (j.contains("skin_color") && j["skin_color"].is_number()) {
            data.skin_color = static_cast<int16_t>(j["skin_color"].get<int>());
        }
        if (j.contains("underwear_color") && j["underwear_color"].is_number()) {
            data.underwear_color = static_cast<int16_t>(j["underwear_color"].get<int>());
        }

        // Optional stats
        if (j.contains("strength") && j["strength"].is_number()) {
            data.strength = static_cast<int16_t>(j["strength"].get<int>());
        }
        if (j.contains("dexterity") && j["dexterity"].is_number()) {
            data.dexterity = static_cast<int16_t>(j["dexterity"].get<int>());
        }
        if (j.contains("vitality") && j["vitality"].is_number()) {
            data.vitality = static_cast<int16_t>(j["vitality"].get<int>());
        }
        if (j.contains("intelligence") && j["intelligence"].is_number()) {
            data.intelligence = static_cast<int16_t>(j["intelligence"].get<int>());
        }
        if (j.contains("magic") && j["magic"].is_number()) {
            data.magic = static_cast<int16_t>(j["magic"].get<int>());
        }
        if (j.contains("charisma") && j["charisma"].is_number()) {
            data.charisma = static_cast<int16_t>(j["charisma"].get<int>());
        }

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

        return result<enter_game_request_data, std::string>::ok(std::move(data));

    } catch (const nlohmann::json::exception& e) {
        return result<enter_game_request_data, std::string>::err(std::string("Parse error: ") + e.what());
    }
}

auto player_move_request_data::from_json(const nlohmann::json& j)
    -> result<player_move_request_data, std::string>
{
    try {
        player_move_request_data data;

        if (!j.contains("x") || !j["x"].is_number()) {
            return result<player_move_request_data, std::string>::err("Missing or invalid 'x' field");
        }
        data.target_x = static_cast<int16_t>(j["x"].get<int>());

        if (!j.contains("y") || !j["y"].is_number()) {
            return result<player_move_request_data, std::string>::err("Missing or invalid 'y' field");
        }
        data.target_y = static_cast<int16_t>(j["y"].get<int>());

        if (j.contains("direction") && j["direction"].is_number()) {
            data.direction = static_cast<int16_t>(j["direction"].get<int>());
        }

        return result<player_move_request_data, std::string>::ok(std::move(data));

    } catch (const nlohmann::json::exception& e) {
        return result<player_move_request_data, std::string>::err(std::string("Parse error: ") + e.what());
    }
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
    return nlohmann::json{
        {"entity_id", entity_id},
        {"type", type},
        {"name", name},
        {"x", x},
        {"y", y},
        {"hp_percent", hp_percent},
        {"direction", direction}
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

    nlohmann::json entities_json = nlohmann::json::array();
    for (const auto& entity : entities) {
        entities_json.push_back(entity.to_json());
    }

    return nlohmann::json{
        {"character", character.to_json()},
        {"inventory", {{"items", std::move(inv_json)}, {"gold", gold}}},
        {"equipment", std::move(equip_json)},
        {"skills", std::move(skills_json)},
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

auto make_player_position_update(uint32_t entity_id,
                                  int16_t x, int16_t y, int16_t direction) -> json_message
{
    return json_message{
        .type = json_message_type::player_position_update,
        .seq = 0,  // Broadcasts don't need seq
        .data = nlohmann::json{
            {"entity_id", entity_id},
            {"x", x},
            {"y", y},
            {"direction", direction}
        }
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

}  // namespace hb::network
