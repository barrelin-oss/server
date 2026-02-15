// craft_recipe_registry.cpp
// Craft recipe registry - YAML parsing for alchemy and crafting recipes

#include "registry/craft_recipe_registry.h"
#include "registry/item_registry.h"
#include "core/logger.h"

#include <yaml-cpp/yaml.h>

namespace hb
{

craft_recipe_registry::craft_recipe_registry() = default;
craft_recipe_registry::~craft_recipe_registry() = default;

void craft_recipe_registry::initialize()
{
    LOG_INFO(general, "Craft recipe registry initialized");
    set_initialized(true);
}

void craft_recipe_registry::shutdown()
{
    LOG_INFO(general,
             "Craft recipe registry shut down ({} alchemy, {} crafting)",
             alchemy_recipes_.size(),
             crafting_recipes_.size());
    alchemy_recipes_.clear();
    crafting_recipes_.clear();
    id_index_.clear();
    name_index_.clear();
    set_initialized(false);
}

auto craft_recipe_registry::load_recipes(const std::filesystem::path& path,
                                         const std::string& section_name,
                                         const item_registry& items,
                                         std::vector<crafting::craft_recipe>& dest) -> result<size_t, std::string>
{
    LOG_INFO(general, "Loading {} from: {}", section_name, path.string());

    YAML::Node root;
    try
    {
        root = YAML::LoadFile(path.string());
    }
    catch (const YAML::Exception& e)
    {
        return result<size_t, std::string>::err("Failed to parse craft recipes YAML: " + std::string(e.what()));
    }

    if (!root[section_name] || !root[section_name].IsSequence())
    {
        return result<size_t, std::string>::err("Missing or invalid '" + section_name + "' section");
    }

    size_t loaded = 0;
    for (const auto& node : root[section_name])
    {
        crafting::craft_recipe recipe;

        if (node["id"])
        {
            recipe.id = node["id"].as<int>();
        }
        if (node["result"])
        {
            recipe.result = node["result"].as<std::string>();
        }
        if (node["skill_limit"])
        {
            recipe.skill_limit = static_cast<int16_t>(node["skill_limit"].as<int>());
        }
        if (node["difficulty"])
        {
            recipe.difficulty = static_cast<int16_t>(node["difficulty"].as<int>());
        }

        // Resolve result name to template ID
        auto* tmpl = items.find_by_name(recipe.result);
        if (tmpl)
        {
            recipe.result_template_id = static_cast<int32_t>(tmpl->id.value);
        }
        else
        {
            LOG_WARN(
                general, "Craft recipe id={}: result item '{}' not found in item registry", recipe.id, recipe.result);
        }

        // Parse ingredients
        if (node["ingredients"] && node["ingredients"].IsSequence())
        {
            for (const auto& ing : node["ingredients"])
            {
                crafting::recipe_ingredient ingredient;
                if (ing["item_id"])
                {
                    ingredient.item_id = ing["item_id"].as<int>();
                }
                if (ing["count"])
                {
                    ingredient.count = ing["count"].as<int>();
                }
                recipe.ingredients.push_back(ingredient);
            }
        }

        dest.push_back(std::move(recipe));
        ++loaded;
    }

    // Build indices for the newly loaded recipes
    for (auto& recipe : dest)
    {
        id_index_[recipe.id] = &recipe;
        if (!recipe.result.empty())
        {
            name_index_[recipe.result] = &recipe;
        }
    }

    LOG_INFO(general, "Loaded {} {} recipes", loaded, section_name);
    return result<size_t, std::string>::ok(loaded);
}

auto craft_recipe_registry::load_alchemy(const std::filesystem::path& path,
                                         const item_registry& items) -> result<size_t, std::string>
{
    return load_recipes(path, "alchemy_recipes", items, alchemy_recipes_);
}

auto craft_recipe_registry::load_crafting(const std::filesystem::path& path,
                                          const item_registry& items) -> result<size_t, std::string>
{
    return load_recipes(path, "crafting_recipes", items, crafting_recipes_);
}

auto craft_recipe_registry::get(int32_t id) const -> const crafting::craft_recipe*
{
    auto it = id_index_.find(id);
    return it != id_index_.end() ? it->second : nullptr;
}

auto craft_recipe_registry::find_by_result(std::string_view name) const -> const crafting::craft_recipe*
{
    auto it = name_index_.find(std::string(name));
    return it != name_index_.end() ? it->second : nullptr;
}

} // namespace hb
