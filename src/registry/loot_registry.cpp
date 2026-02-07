// loot_registry.cpp
// Loot registry implementation - YAML parsing for loot pools and NPC configs

#include "registry/loot_registry.h"
#include "core/logger.h"

#include <yaml-cpp/yaml.h>

namespace hb {

loot_registry::loot_registry() = default;
loot_registry::~loot_registry() = default;

void loot_registry::initialize()
{
    LOG_INFO(general, "Loot registry initialized");
    set_initialized(true);
}

void loot_registry::shutdown()
{
    LOG_INFO(general, "Loot registry shut down ({} pools, {} configs)", pools_.size(), configs_.size());
    pools_.clear();
    configs_.clear();
    set_initialized(false);
}

auto loot_registry::load_from_file(const std::filesystem::path& path)
    -> result<size_t, std::string>
{
    LOG_INFO(general, "Loading loot tables from: {}", path.string());

    YAML::Node root;
    try {
        root = YAML::LoadFile(path.string());
    } catch (const YAML::Exception& e) {
        return result<size_t, std::string>::err(
            "Failed to parse loot YAML: " + std::string(e.what())
        );
    }

    // Parse pools
    if (root["pools"] && root["pools"].IsMap())
    {
        for (const auto& pair : root["pools"])
        {
            auto pool_name = pair.first.as<std::string>();
            npc::item_pool pool;
            pool.name = pool_name;
            pool.total_weight = 0;

            if (pair.second.IsSequence())
            {
                for (const auto& entry : pair.second)
                {
                    npc::weighted_item wi;
                    if (entry["item"])
                    {
                        wi.item = item_id{static_cast<uint32_t>(entry["item"].as<int>())};
                    }
                    if (entry["weight"])
                    {
                        wi.weight = entry["weight"].as<int32_t>();
                    }
                    pool.total_weight += wi.weight;
                    pool.items.push_back(wi);
                }
            }

            LOG_DEBUG(general, "  Pool '{}': {} items, total weight {}",
                pool_name, pool.items.size(), pool.total_weight);
            pools_[pool_name] = std::move(pool);
        }
    }

    // Parse NPC loot configs
    size_t npc_count = 0;
    if (root["loot"] && root["loot"].IsMap())
    {
        for (const auto& pair : root["loot"])
        {
            auto sprite_id = static_cast<int16_t>(pair.first.as<int>());
            npc::npc_loot_config config;

            auto parse_phase = [](const YAML::Node& node) -> npc::loot_phase_config
            {
                npc::loot_phase_config phase;
                if (node["gold_chance"])
                {
                    phase.gold_chance = static_cast<int16_t>(node["gold_chance"].as<int>());
                }
                if (node["multi_drop"] && node["multi_drop"].IsMap())
                {
                    npc::multi_drop_config md;
                    if (node["multi_drop"]["min"])
                    {
                        md.min_count = static_cast<int16_t>(node["multi_drop"]["min"].as<int>());
                    }
                    if (node["multi_drop"]["max"])
                    {
                        md.max_count = static_cast<int16_t>(node["multi_drop"]["max"].as<int>());
                    }
                    phase.multi_drop = md;
                }
                if (node["drops"] && node["drops"].IsSequence())
                {
                    for (const auto& drop : node["drops"])
                    {
                        npc::loot_drop_entry entry;
                        if (drop["pool"])
                        {
                            entry.pool_name = drop["pool"].as<std::string>();
                        }
                        if (drop["chance"])
                        {
                            entry.chance = static_cast<int16_t>(drop["chance"].as<int>());
                        }
                        phase.drops.push_back(std::move(entry));
                    }
                }
                return phase;
            };

            if (pair.second["on_kill"])
            {
                config.on_kill = parse_phase(pair.second["on_kill"]);
            }
            if (pair.second["on_despawn"])
            {
                config.on_despawn = parse_phase(pair.second["on_despawn"]);
            }

            configs_[sprite_id] = std::move(config);
            ++npc_count;
        }
    }

    LOG_INFO(general, "Loaded {} loot pools and {} NPC loot configs", pools_.size(), npc_count);
    return result<size_t, std::string>::ok(pools_.size() + npc_count);
}

auto loot_registry::get_pool(std::string_view name) const -> const npc::item_pool*
{
    auto it = pools_.find(std::string(name));
    return it != pools_.end() ? &it->second : nullptr;
}

auto loot_registry::get_config(int16_t sprite_id) const -> const npc::npc_loot_config*
{
    auto it = configs_.find(sprite_id);
    return it != configs_.end() ? &it->second : nullptr;
}

}  // namespace hb
