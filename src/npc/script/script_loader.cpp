// script_loader.cpp
// YAML-based script loading

#include "npc/script/script_loader.h"
#include "core/logger.h"

#include <yaml-cpp/yaml.h>

namespace hb::npc::script {

namespace {

auto parse_action_type(const std::string& str) -> action_type
{
    if (str == "wait") return action_type::wait;
    if (str == "move_to") return action_type::move_to;
    if (str == "face") return action_type::face;
    if (str == "say") return action_type::say;
    if (str == "broadcast") return action_type::broadcast;
    if (str == "emote") return action_type::emote;
    if (str == "spawn_npc") return action_type::spawn_npc;
    if (str == "despawn") return action_type::despawn;
    if (str == "set_state") return action_type::set_state;
    if (str == "set_flag") return action_type::set_flag;
    if (str == "apply_buff") return action_type::apply_buff;
    if (str == "damage_area") return action_type::damage_area;
    if (str == "heal_self") return action_type::heal_self;
    if (str == "teleport") return action_type::teleport;
    if (str == "set_aggro_range") return action_type::set_aggro_range;
    if (str == "set_invincible") return action_type::set_invincible;
    if (str == "play_effect") return action_type::play_effect;
    if (str == "call_script") return action_type::call_script;
    return action_type::wait;
}

auto parse_direction(const std::string& str) -> hb::world::direction
{
    if (str == "north") return hb::world::direction::north;
    if (str == "north_east") return hb::world::direction::north_east;
    if (str == "east") return hb::world::direction::east;
    if (str == "south_east") return hb::world::direction::south_east;
    if (str == "south") return hb::world::direction::south;
    if (str == "south_west") return hb::world::direction::south_west;
    if (str == "west") return hb::world::direction::west;
    if (str == "north_west") return hb::world::direction::north_west;
    return hb::world::direction::south;
}

auto parse_action(const YAML::Node& node) -> script_action
{
    script_action action;
    action.type = parse_action_type(node["type"].as<std::string>());

    switch (action.type)
    {
        case action_type::wait:
            action.int_param = node["duration_ms"].as<int32_t>(1000);
            break;

        case action_type::move_to:
        case action_type::teleport:
            if (node["position"] && node["position"].IsSequence())
            {
                action.pos.x = static_cast<int16_t>(node["position"][0].as<int>());
                action.pos.y = static_cast<int16_t>(node["position"][1].as<int>());
            }
            break;

        case action_type::face:
            action.dir = parse_direction(node["direction"].as<std::string>("south"));
            break;

        case action_type::say:
        case action_type::broadcast:
            action.str_param = node["message"].as<std::string>("");
            break;

        case action_type::emote:
            action.str_param = node["animation"].as<std::string>("");
            break;

        case action_type::spawn_npc:
            action.npc_template = npc_id{static_cast<uint16_t>(node["template"].as<int>(0))};
            if (node["position"] && node["position"].IsSequence())
            {
                action.pos.x = static_cast<int16_t>(node["position"][0].as<int>());
                action.pos.y = static_cast<int16_t>(node["position"][1].as<int>());
            }
            action.int_param2 = node["count"].as<int32_t>(1);
            break;

        case action_type::damage_area:
            if (node["position"] && node["position"].IsSequence())
            {
                action.pos.x = static_cast<int16_t>(node["position"][0].as<int>());
                action.pos.y = static_cast<int16_t>(node["position"][1].as<int>());
            }
            action.int_param = node["damage"].as<int32_t>(0);
            action.int_param2 = node["radius"].as<int32_t>(3);
            break;

        case action_type::heal_self:
            action.int_param = node["amount"].as<int32_t>(0);
            break;

        case action_type::set_aggro_range:
            action.int_param = node["range"].as<int32_t>(8);
            break;

        case action_type::set_invincible:
            action.bool_param = node["enabled"].as<bool>(true);
            break;

        case action_type::apply_buff:
            action.str_param = node["effect"].as<std::string>("");
            action.int_param = node["duration_ms"].as<int32_t>(5000);
            action.int_param2 = node["radius"].as<int32_t>(5);
            break;

        case action_type::play_effect:
            action.str_param = node["effect"].as<std::string>("");
            if (node["position"] && node["position"].IsSequence())
            {
                action.pos.x = static_cast<int16_t>(node["position"][0].as<int>());
                action.pos.y = static_cast<int16_t>(node["position"][1].as<int>());
            }
            break;

        case action_type::call_script:
            action.str_param = node["script"].as<std::string>("");
            break;

        case action_type::set_state:
            action.str_param = node["state"].as<std::string>("idle");
            break;

        case action_type::set_flag:
            action.str_param = node["flag"].as<std::string>("");
            action.bool_param = node["enabled"].as<bool>(true);
            break;

        case action_type::despawn:
            break;
    }

    return action;
}

}  // anonymous namespace

auto script_loader::load_from_file(const std::filesystem::path& path)
    -> result<npc_script, std::string>
{
    try
    {
        YAML::Node config = YAML::LoadFile(path.string());

        npc_script script;
        script.name = config["name"].as<std::string>(path.stem().string());
        script.loop = config["loop"].as<bool>(false);

        if (!config["actions"] || !config["actions"].IsSequence())
        {
            return result<npc_script, std::string>::err(
                "Missing or invalid 'actions' in script: " + path.string());
        }

        for (const auto& action_node : config["actions"])
        {
            script.actions.push_back(parse_action(action_node));
        }

        auto name = script.name;
        scripts_[name] = std::move(script);

        return result<npc_script, std::string>::ok(scripts_[name]);
    }
    catch (const YAML::Exception& e)
    {
        return result<npc_script, std::string>::err(
            std::string("YAML error loading script: ") + e.what());
    }
    catch (const std::exception& e)
    {
        return result<npc_script, std::string>::err(
            std::string("Error loading script: ") + e.what());
    }
}

auto script_loader::load_directory(const std::filesystem::path& dir)
    -> result<size_t, std::string>
{
    scripts_dir_ = dir;

    if (!std::filesystem::exists(dir))
    {
        return result<size_t, std::string>::ok(size_t{0});
    }

    size_t count = 0;
    for (const auto& entry : std::filesystem::directory_iterator(dir))
    {
        if (!entry.is_regular_file()) continue;

        auto ext = entry.path().extension().string();
        if (ext != ".yaml" && ext != ".yml") continue;

        auto res = load_from_file(entry.path());
        if (res.is_ok())
        {
            ++count;
        }
        else
        {
            LOG_WARN(general, "Failed to load script {}: {}",
                entry.path().string(), res.error());
        }
    }

    LOG_INFO(general, "Loaded {} NPC scripts from {}", count, dir.string());
    return result<size_t, std::string>::ok(count);
}

auto script_loader::get_script(std::string_view name) const -> const npc_script*
{
    auto it = scripts_.find(std::string(name));
    return it != scripts_.end() ? &it->second : nullptr;
}

void script_loader::clear()
{
    scripts_.clear();
}

auto script_loader::reload() -> result<size_t, std::string>
{
    if (scripts_dir_.empty())
    {
        return result<size_t, std::string>::err("No scripts directory set");
    }

    scripts_.clear();
    return load_directory(scripts_dir_);
}

}  // namespace hb::npc::script
