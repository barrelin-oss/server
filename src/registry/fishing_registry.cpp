// fishing_registry.cpp
// Fishing registry - YAML parsing for fish type configurations

#include "registry/fishing_registry.h"
#include "registry/item_registry.h"
#include "core/logger.h"

#include <yaml-cpp/yaml.h>
#include <random>

namespace hb
{

fishing_registry::fishing_registry() = default;
fishing_registry::~fishing_registry() = default;

void fishing_registry::initialize()
{
    LOG_INFO(general, "Fishing registry initialized");
    set_initialized(true);
}

void fishing_registry::shutdown()
{
    LOG_INFO(general, "Fishing registry shut down ({} fish types)", types_.size());
    types_.clear();
    id_index_.clear();
    total_weight_ = 0;
    set_initialized(false);
}

auto fishing_registry::load_from_file(const std::filesystem::path& path,
                                      const item_registry& items) -> result<size_t, std::string>
{
    LOG_INFO(general, "Loading fish types from: {}", path.string());

    YAML::Node root;
    try
    {
        root = YAML::LoadFile(path.string());
    }
    catch (const YAML::Exception& e)
    {
        return result<size_t, std::string>::err("Failed to parse fishing YAML: " + std::string(e.what()));
    }

    if (!root["fish_types"] || !root["fish_types"].IsSequence())
    {
        return result<size_t, std::string>::err("Missing or invalid 'fish_types' section");
    }

    total_weight_ = 0;

    for (const auto& node : root["fish_types"])
    {
        crafting::fish_type_config config;

        config.type_id = node["type_id"].as<int>(0);
        config.name = node["name"].as<std::string>("");
        config.item_name = node["item"].as<std::string>("");
        config.difficulty = static_cast<int16_t>(node["difficulty"].as<int>(0));
        config.lifespan_min = node["lifespan_min"].as<int>(20);
        config.visual_type = static_cast<uint8_t>(node["visual_type"].as<int>(2));
        config.weight = node["weight"].as<int>(10);

        // Resolve item name to template ID
        auto* tmpl = items.find_by_name(config.item_name);
        if (tmpl)
        {
            config.template_id = static_cast<int32_t>(tmpl->id.value);
        }
        else
        {
            LOG_WARN(general, "Fish type '{}': item '{}' not found in item registry", config.name, config.item_name);
        }

        total_weight_ += config.weight;
        id_index_[config.type_id] = types_.size();
        types_.push_back(std::move(config));
    }

    LOG_INFO(general, "Loaded {} fish types (total weight: {})", types_.size(), total_weight_);
    return result<size_t, std::string>::ok(types_.size());
}

auto fishing_registry::get_type(int32_t type_id) const -> const crafting::fish_type_config*
{
    auto it = id_index_.find(type_id);
    return it != id_index_.end() ? &types_[it->second] : nullptr;
}

auto fishing_registry::roll_fish_type() const -> const crafting::fish_type_config*
{
    if (types_.empty() || total_weight_ <= 0)
    {
        return nullptr;
    }

    thread_local std::mt19937 rng{std::random_device{}()};
    std::uniform_int_distribution<int32_t> dist(1, total_weight_);
    auto roll = dist(rng);

    int32_t cumulative = 0;
    for (const auto& type : types_)
    {
        cumulative += type.weight;
        if (roll <= cumulative)
        {
            return &type;
        }
    }

    return &types_.back();
}

} // namespace hb
