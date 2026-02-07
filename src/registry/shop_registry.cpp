// shop_registry.cpp
// Shop registry implementation - YAML parsing for shop configs

#include "registry/shop_registry.h"
#include "core/logger.h"

#include <yaml-cpp/yaml.h>

namespace hb {

shop_registry::shop_registry() = default;
shop_registry::~shop_registry() = default;

void shop_registry::initialize()
{
    LOG_INFO(general, "Shop registry initialized");
    set_initialized(true);
}

void shop_registry::shutdown()
{
    LOG_INFO(general, "Shop registry shut down ({} shops)", shops_.size());
    shops_.clear();
    set_initialized(false);
}

auto shop_registry::load_from_file(const std::filesystem::path& path)
    -> result<size_t, std::string>
{
    LOG_INFO(general, "Loading shops from: {}", path.string());

    YAML::Node root;
    try {
        root = YAML::LoadFile(path.string());
    } catch (const YAML::Exception& e) {
        return result<size_t, std::string>::err(
            "Failed to parse shops YAML: " + std::string(e.what())
        );
    }

    if (!root["shops"] || !root["shops"].IsMap())
    {
        return result<size_t, std::string>::err("Missing or invalid 'shops' section");
    }

    size_t count = 0;
    for (const auto& pair : root["shops"])
    {
        auto npc_name = pair.first.as<std::string>();
        npc::shop_config shop;
        shop.npc_name = npc_name;

        auto& node = pair.second;

        // Parse shop type
        if (node["type"])
        {
            auto type_str = node["type"].as<std::string>();
            if (type_str == "blacksmith")
                shop.type = npc::shop_type::blacksmith;
            else
                shop.type = npc::shop_type::general;
        }

        // Parse buy categories
        if (node["buy_categories"] && node["buy_categories"].IsSequence())
        {
            for (const auto& cat : node["buy_categories"])
            {
                shop.buy_categories.push_back(static_cast<uint8_t>(cat.as<int>()));
            }
        }

        // Parse repair categories
        if (node["repair_categories"] && node["repair_categories"].IsSequence())
        {
            for (const auto& cat : node["repair_categories"])
            {
                shop.repair_categories.push_back(static_cast<uint8_t>(cat.as<int>()));
            }
        }

        // Parse items for sale
        if (node["items"] && node["items"].IsSequence())
        {
            for (const auto& entry : node["items"])
            {
                npc::shop_item_entry item;
                if (entry["item_id"])
                {
                    item.item = item_id{static_cast<uint32_t>(entry["item_id"].as<int>())};
                }
                if (entry["count"])
                {
                    item.default_count = static_cast<int16_t>(entry["count"].as<int>());
                }
                shop.items.push_back(item);
            }
        }

        LOG_DEBUG(general, "  Shop '{}': {} items for sale, {} buy categories",
            npc_name, shop.items.size(), shop.buy_categories.size());
        shops_[npc_name] = std::move(shop);
        ++count;
    }

    LOG_INFO(general, "Loaded {} shop configs", count);
    return result<size_t, std::string>::ok(count);
}

auto shop_registry::get_shop(std::string_view npc_name) const -> const npc::shop_config*
{
    auto it = shops_.find(std::string(npc_name));
    return it != shops_.end() ? &it->second : nullptr;
}

}  // namespace hb
