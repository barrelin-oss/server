// boss_loader.cpp
// Load boss configurations from YAML files

#include "npc/boss/boss_loader.h"
#include "core/logger.h"

#include <yaml-cpp/yaml.h>

namespace hb::npc::boss
{

namespace
{

auto parse_trigger_type(const std::string& str) -> phase_trigger_type
{
    if (str == "hp_below")
        return phase_trigger_type::hp_below;
    if (str == "hp_above")
        return phase_trigger_type::hp_above;
    if (str == "timer")
        return phase_trigger_type::timer;
    if (str == "adds_dead")
        return phase_trigger_type::adds_dead;
    return phase_trigger_type::hp_below;
}

auto parse_trigger(const YAML::Node& node) -> phase_trigger
{
    phase_trigger trigger;
    if (!node)
        return trigger;

    trigger.type = parse_trigger_type(node["type"].as<std::string>("hp_below"));
    trigger.hp_threshold = node["threshold"].as<float>(0.0f);
    trigger.timer_ms = node["timer_ms"].as<int32_t>(0);

    return trigger;
}

auto parse_phase(const YAML::Node& node) -> boss_phase
{
    boss_phase phase;
    phase.name = node["name"].as<std::string>("Unnamed Phase");

    if (node["enter_trigger"])
    {
        phase.enter_trigger = parse_trigger(node["enter_trigger"]);
    }

    phase.behavior_tree = node["behavior_tree"].as<std::string>("");
    phase.on_enter_script = node["on_enter_script"].as<std::string>("");
    phase.on_exit_script = node["on_exit_script"].as<std::string>("");

    phase.damage_multiplier = node["damage_multiplier"].as<float>(1.0f);
    phase.speed_multiplier = node["speed_multiplier"].as<float>(1.0f);
    phase.defense_multiplier = node["defense_multiplier"].as<float>(1.0f);
    phase.override_aggro_range = node["aggro_range"].as<int16_t>(-1);
    phase.override_attack_range = node["attack_range"].as<int16_t>(-1);

    return phase;
}

} // anonymous namespace

auto boss_loader::load_from_file(const std::filesystem::path& path) -> result<boss_config, std::string>
{
    try
    {
        YAML::Node config = YAML::LoadFile(path.string());

        boss_config boss;
        boss.name = config["name"].as<std::string>(path.stem().string());
        boss.on_spawn_script = config["on_spawn_script"].as<std::string>("");
        boss.on_death_script = config["on_death_script"].as<std::string>("");
        boss.enrage_enabled = config["enrage_enabled"].as<bool>(false);
        boss.enrage_timer_ms = config["enrage_timer_ms"].as<int32_t>(0);

        if (config["phases"] && config["phases"].IsSequence())
        {
            for (const auto& phase_node : config["phases"])
            {
                boss.phases.push_back(parse_phase(phase_node));
            }
        }

        auto name = boss.name;
        configs_[name] = std::move(boss);

        return result<boss_config, std::string>::ok(configs_[name]);
    }
    catch (const YAML::Exception& e)
    {
        return result<boss_config, std::string>::err(std::string("YAML error loading boss config: ") + e.what());
    }
    catch (const std::exception& e)
    {
        return result<boss_config, std::string>::err(std::string("Error loading boss config: ") + e.what());
    }
}

auto boss_loader::load_directory(const std::filesystem::path& dir) -> result<size_t, std::string>
{
    configs_dir_ = dir;

    if (!std::filesystem::exists(dir))
    {
        return result<size_t, std::string>::ok(size_t{0});
    }

    size_t count = 0;
    for (const auto& entry : std::filesystem::directory_iterator(dir))
    {
        if (!entry.is_regular_file())
            continue;

        auto ext = entry.path().extension().string();
        if (ext != ".yaml" && ext != ".yml")
            continue;

        auto res = load_from_file(entry.path());
        if (res.is_ok())
        {
            ++count;
        }
        else
        {
            LOG_WARN(general, "Failed to load boss config {}: {}", entry.path().string(), res.error());
        }
    }

    LOG_INFO(general, "Loaded {} boss configurations from {}", count, dir.string());
    return result<size_t, std::string>::ok(count);
}

auto boss_loader::get_config(std::string_view name) const -> const boss_config*
{
    auto it = configs_.find(std::string(name));
    return it != configs_.end() ? &it->second : nullptr;
}

auto boss_loader::reload() -> result<size_t, std::string>
{
    if (configs_dir_.empty())
    {
        return result<size_t, std::string>::err("No boss config directory set");
    }

    configs_.clear();
    return load_directory(configs_dir_);
}

void boss_loader::clear()
{
    configs_.clear();
}

} // namespace hb::npc::boss
