// build_recipe_registry.cpp
// Build recipe registry - YAML parsing for manufacturing recipes

#include "registry/build_recipe_registry.h"
#include "registry/item_registry.h"
#include "core/logger.h"

#include <yaml-cpp/yaml.h>

namespace hb {

build_recipe_registry::build_recipe_registry() = default;
build_recipe_registry::~build_recipe_registry() = default;

void build_recipe_registry::initialize()
{
    LOG_INFO(general, "Build recipe registry initialized");
    set_initialized(true);
}

void build_recipe_registry::shutdown()
{
    LOG_INFO(general, "Build recipe registry shut down ({} recipes)", recipes_.size());
    recipes_.clear();
    name_index_.clear();
    set_initialized(false);
}

auto build_recipe_registry::load_from_file(const std::filesystem::path& path,
                                            const item_registry& items)
    -> result<size_t, std::string>
{
    LOG_INFO(general, "Loading build recipes from: {}", path.string());

    YAML::Node root;
    try {
        root = YAML::LoadFile(path.string());
    } catch (const YAML::Exception& e) {
        return result<size_t, std::string>::err(
            "Failed to parse build recipes YAML: " + std::string(e.what())
        );
    }

    if (!root["build_recipes"] || !root["build_recipes"].IsSequence())
    {
        return result<size_t, std::string>::err("Missing or invalid 'build_recipes' section");
    }

    int32_t index = 0;
    for (const auto& node : root["build_recipes"])
    {
        crafting::build_recipe recipe;
        recipe.id = index;

        if (node["result"])
        {
            recipe.result = node["result"].as<std::string>();
        }
        if (node["skill_req"])
        {
            recipe.skill_req = static_cast<int16_t>(node["skill_req"].as<int>());
        }
        if (node["skill_limit"])
        {
            recipe.skill_limit = static_cast<int16_t>(node["skill_limit"].as<int>());
        }
        if (node["success_rate"])
        {
            recipe.success_rate = node["success_rate"].as<int>();
        }
        if (node["attribute"])
        {
            recipe.result_attribute = static_cast<uint16_t>(node["attribute"].as<int>());
        }

        // Resolve result name to template ID
        auto* tmpl = items.find_by_name(recipe.result);
        if (tmpl)
        {
            recipe.result_template_id = static_cast<int32_t>(tmpl->id.value);
        }
        else
        {
            LOG_WARN(general, "Build recipe '{}': result item '{}' not found in item registry",
                index, recipe.result);
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

        name_index_[recipe.result] = static_cast<size_t>(index);
        recipes_.push_back(std::move(recipe));
        ++index;
    }

    LOG_INFO(general, "Loaded {} build recipes", recipes_.size());
    return result<size_t, std::string>::ok(recipes_.size());
}

auto build_recipe_registry::get(int32_t index) const -> const crafting::build_recipe*
{
    if (index < 0 || static_cast<size_t>(index) >= recipes_.size())
    {
        return nullptr;
    }
    return &recipes_[static_cast<size_t>(index)];
}

auto build_recipe_registry::find_by_result(std::string_view name) const
    -> const crafting::build_recipe*
{
    auto it = name_index_.find(std::string(name));
    return it != name_index_.end() ? &recipes_[it->second] : nullptr;
}

}  // namespace hb
