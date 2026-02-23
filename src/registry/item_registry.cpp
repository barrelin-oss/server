// item_registry.cpp
// Item registry implementation

#include "registry/item_registry.h"
#include "core/logger.h"

#include <fstream>
#include <sstream>
#include <algorithm>
#include <cctype>
#include <yaml-cpp/yaml.h>

namespace hb
{

namespace
{

// Convert string to lowercase
auto to_lower(std::string str) -> std::string
{
    std::transform(str.begin(), str.end(), str.begin(), [](unsigned char c) { return std::tolower(c); });
    return str;
}

} // namespace

item_registry::item_registry() = default;
item_registry::~item_registry() = default;

void item_registry::initialize()
{
    LOG_INFO(item, "Item registry initialized");
    set_initialized(true);
}

void item_registry::shutdown()
{
    LOG_INFO(item, "Item registry shut down ({} items)", items_.size());
    items_.clear();
    id_index_.clear();
    name_index_.clear();
    set_initialized(false);
}

auto item_registry::load_from_file(const std::filesystem::path& path) -> result<size_t, std::string>
{
    std::ifstream file(path);
    if (!file.is_open())
    {
        return result<size_t, std::string>::err("Failed to open item config: " + path.string());
    }

    LOG_INFO(item, "Loading items from: {}", path.string());

    // Only YAML format is supported
    auto ext = path.extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) { return std::tolower(c); });

    if (ext == ".yaml" || ext == ".yml")
    {
        return load_from_yaml(path);
    }

    return result<size_t, std::string>::err("Unsupported item config format (only YAML supported): " + path.string());
}

auto item_registry::load_from_yaml(const std::filesystem::path& path) -> result<size_t, std::string>
{
    try
    {
        YAML::Node root = YAML::LoadFile(path.string());

        if (!root["items"] || !root["items"].IsSequence())
        {
            return result<size_t, std::string>::err("YAML must contain 'items' array");
        }

        size_t loaded = 0;
        size_t errors = 0;

        for (const auto& node : root["items"])
        {
            item_template item;

            // Required fields
            if (!node["id"])
            {
                ++errors;
                continue;
            }

            item.id = item_id{node["id"].as<uint32_t>()};
            if (!item.id.is_valid())
            {
                ++errors;
                continue;
            }

            if (node["name"])
            {
                item.name = node["name"].as<std::string>();
            }
            else
            {
                ++errors;
                continue;
            }

            // Check for duplicate
            if (id_index_.contains(item.id.value))
            {
                LOG_ERROR(item, "Duplicate item ID {} -- skipping", item.id.value);
                ++errors;
                continue;
            }

            // Classification
            if (node["type"])
            {
                auto type_val = node["type"].as<int>();
                if (type_val >= -1 && type_val <= 17)
                {
                    item.type = static_cast<item_type>(type_val);
                }
                else
                {
                    LOG_WARN(item, "Item {}: invalid type {}, defaulting to none", item.name, type_val);
                }
            }
            if (node["equip_pos"])
                item.equip_pos = static_cast<item_equip_pos>(node["equip_pos"].as<int>());
            if (node["category"])
                item.category = static_cast<item_category>(node["category"].as<int>());

            // Effect values
            if (node["effect_type"])
                item.effect_type = static_cast<int16_t>(node["effect_type"].as<int>());
            if (node["effect_value1"])
                item.effect_value1 = static_cast<int16_t>(node["effect_value1"].as<int>());
            if (node["effect_value2"])
                item.effect_value2 = static_cast<int16_t>(node["effect_value2"].as<int>());
            if (node["effect_value3"])
                item.effect_value3 = static_cast<int16_t>(node["effect_value3"].as<int>());
            if (node["effect_value4"])
                item.effect_value4 = static_cast<int16_t>(node["effect_value4"].as<int>());
            if (node["effect_value5"])
                item.effect_value5 = static_cast<int16_t>(node["effect_value5"].as<int>());
            if (node["effect_value6"])
                item.effect_value6 = static_cast<int16_t>(node["effect_value6"].as<int>());

            // Basic properties
            if (node["durability"])
                item.durability = static_cast<int16_t>(node["durability"].as<int>());
            if (node["weight"])
                item.weight = static_cast<int16_t>(node["weight"].as<int>());
            if (node["price"])
                item.price = node["price"].as<int>();
            if (node["level_limit"])
                item.level_limit = static_cast<int16_t>(node["level_limit"].as<int>());

            // Special effect
            if (node["special_effect"])
                item.special_effect = static_cast<int16_t>(node["special_effect"].as<int>());
            if (node["special_effect_value1"])
                item.special_effect_value1 = static_cast<int16_t>(node["special_effect_value1"].as<int>());
            if (node["special_effect_value2"])
                item.special_effect_value2 = static_cast<int16_t>(node["special_effect_value2"].as<int>());

            // Visual
            if (node["sprite"])
                item.sprite = static_cast<int16_t>(node["sprite"].as<int>());
            if (node["sprite_frame"])
                item.sprite_frame = static_cast<int16_t>(node["sprite_frame"].as<int>());
            if (node["appr_value"])
                item.appr_value = static_cast<int8_t>(node["appr_value"].as<int>());
            if (node["item_color"])
                item.item_color = static_cast<int8_t>(node["item_color"].as<int>());
            if (node["speed"])
                item.speed = static_cast<int8_t>(node["speed"].as<int>());

            // Misc
            if (node["gender_limit"])
                item.gender_limit = static_cast<int8_t>(node["gender_limit"].as<int>());
            if (node["related_skill"])
                item.related_skill = static_cast<int16_t>(node["related_skill"].as<int>());

            // Ground item lifetime override
            if (node["ground_lifetime_ms"])
                item.ground_lifetime_ms = node["ground_lifetime_ms"].as<int32_t>();

            // Store item
            auto index = items_.size();
            id_index_[item.id.value] = index;
            name_index_[to_lower(item.name)] = index;
            items_.push_back(std::move(item));
            ++loaded;
        }

        LOG_INFO(item, "Loaded {} items from YAML ({} errors)", loaded, errors);
        return result<size_t, std::string>::ok(loaded);
    }
    catch (const YAML::Exception& e)
    {
        return result<size_t, std::string>::err(std::string("YAML parsing error: ") + e.what());
    }
}

auto item_registry::get(item_id id) const -> const item_template*
{
    auto it = id_index_.find(id.value);
    if (it == id_index_.end())
    {
        return nullptr;
    }
    return &items_[it->second];
}

auto item_registry::find_by_name(std::string_view name) const -> const item_template*
{
    auto it = name_index_.find(to_lower(std::string(name)));
    if (it == name_index_.end())
    {
        return nullptr;
    }
    return &items_[it->second];
}

auto item_registry::by_type(item_type type) const -> std::vector<const item_template*>
{
    std::vector<const item_template*> result;
    for (const auto& item : items_)
    {
        if (item.type == type)
        {
            result.push_back(&item);
        }
    }
    return result;
}

auto item_registry::by_category(item_category category) const -> std::vector<const item_template*>
{
    std::vector<const item_template*> result;
    for (const auto& item : items_)
    {
        if (item.category == category)
        {
            result.push_back(&item);
        }
    }
    return result;
}

auto item_registry::by_equip_pos(item_equip_pos pos) const -> std::vector<const item_template*>
{
    std::vector<const item_template*> result;
    for (const auto& item : items_)
    {
        if (item.equip_pos == pos)
        {
            result.push_back(&item);
        }
    }
    return result;
}

auto item_registry::count() const -> size_t
{
    return items_.size();
}

void item_registry::register_template(item_template tmpl)
{
    auto id = tmpl.id.value;
    auto name_lower = to_lower(tmpl.name);

    // Replace if already exists
    if (id_index_.contains(id))
    {
        auto idx = id_index_[id];
        items_[idx] = std::move(tmpl);
        return;
    }

    auto idx = items_.size();
    items_.push_back(std::move(tmpl));
    id_index_[id] = idx;
    if (!name_lower.empty())
    {
        name_index_[name_lower] = idx;
    }
}

auto item_registry::exists(item_id id) const -> bool
{
    return id_index_.contains(id.value);
}

auto item_registry::all() const -> const std::vector<item_template>&
{
    return items_;
}

} // namespace hb
