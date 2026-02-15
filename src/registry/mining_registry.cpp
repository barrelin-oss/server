// mining_registry.cpp
// Mining registry - YAML parsing for mineral type configurations

#include "registry/mining_registry.h"
#include "registry/item_registry.h"
#include "core/logger.h"

#include <yaml-cpp/yaml.h>

namespace hb
{

mining_registry::mining_registry() = default;
mining_registry::~mining_registry() = default;

void mining_registry::initialize()
{
    LOG_INFO(general, "Mining registry initialized");
    set_initialized(true);
}

void mining_registry::shutdown()
{
    LOG_INFO(general, "Mining registry shut down ({} mineral types)", types_.size());
    types_.clear();
    id_index_.clear();
    set_initialized(false);
}

auto mining_registry::load_from_file(const std::filesystem::path& path,
                                     const item_registry& items) -> result<size_t, std::string>
{
    LOG_INFO(general, "Loading mineral types from: {}", path.string());

    YAML::Node root;
    try
    {
        root = YAML::LoadFile(path.string());
    }
    catch (const YAML::Exception& e)
    {
        return result<size_t, std::string>::err("Failed to parse mining YAML: " + std::string(e.what()));
    }

    if (!root["mineral_types"] || !root["mineral_types"].IsSequence())
    {
        return result<size_t, std::string>::err("Missing or invalid 'mineral_types' section");
    }

    for (const auto& node : root["mineral_types"])
    {
        crafting::mineral_type_config config;

        config.type_id = node["type_id"].as<int>(0);
        config.name = node["name"].as<std::string>("");
        config.difficulty = static_cast<int16_t>(node["difficulty"].as<int>(0));
        config.max_hits = node["max_hits"].as<int>(10);
        config.visual_type = static_cast<uint8_t>(node["visual_type"].as<int>(1));

        // Parse drops
        if (node["drops"] && node["drops"].IsSequence())
        {
            for (const auto& drop_node : node["drops"])
            {
                crafting::mineral_drop drop;
                drop.item_name = drop_node["item"].as<std::string>("");
                drop.min_skill = static_cast<int16_t>(drop_node["min_skill"].as<int>(0));
                drop.weight = drop_node["weight"].as<int>(100);

                // Resolve item name to template ID
                auto* tmpl = items.find_by_name(drop.item_name);
                if (tmpl)
                {
                    drop.template_id = static_cast<int32_t>(tmpl->id.value);
                }
                else
                {
                    LOG_WARN(general,
                             "Mineral type '{}': drop item '{}' not found in item registry",
                             config.name,
                             drop.item_name);
                }

                config.drops.push_back(std::move(drop));
            }
        }

        id_index_[config.type_id] = types_.size();
        types_.push_back(std::move(config));
    }

    LOG_INFO(general, "Loaded {} mineral types", types_.size());
    return result<size_t, std::string>::ok(types_.size());
}

auto mining_registry::get_type(int32_t type_id) const -> const crafting::mineral_type_config*
{
    auto it = id_index_.find(type_id);
    return it != id_index_.end() ? &types_[it->second] : nullptr;
}

} // namespace hb
