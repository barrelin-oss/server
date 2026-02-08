#pragma once

// craft_recipe_registry.h
// YAML-driven craft recipe registry for alchemy system

#include "core/subsystem.h"
#include "core/result.h"
#include "crafting/recipe_config.h"

#include <vector>
#include <unordered_map>
#include <string>
#include <string_view>
#include <filesystem>

namespace hb {

class item_registry;

class craft_recipe_registry : public subsystem
{
public:
    craft_recipe_registry();
    ~craft_recipe_registry() override;

    // Subsystem interface
    [[nodiscard]] auto name() const -> std::string_view override { return "craft_recipe_registry"; }
    void initialize() override;
    void shutdown() override;

    // Load alchemy recipes from recipes.yaml (alchemy_recipes: section)
    auto load_alchemy(const std::filesystem::path& path, const item_registry& items)
        -> result<size_t, std::string>;

    // Load crafting recipes from craft_recipes.yaml (crafting_recipes: section)
    auto load_crafting(const std::filesystem::path& path, const item_registry& items)
        -> result<size_t, std::string>;

    // Lookup by recipe ID (searches both alchemy and crafting)
    [[nodiscard]] auto get(int32_t id) const -> const crafting::craft_recipe*;

    // Lookup by result name
    [[nodiscard]] auto find_by_result(std::string_view name) const -> const crafting::craft_recipe*;

    // Access all recipes by category
    [[nodiscard]] auto get_all_alchemy() const -> const std::vector<crafting::craft_recipe>&
    {
        return alchemy_recipes_;
    }

    [[nodiscard]] auto get_all_crafting() const -> const std::vector<crafting::craft_recipe>&
    {
        return crafting_recipes_;
    }

    [[nodiscard]] auto alchemy_count() const -> size_t { return alchemy_recipes_.size(); }
    [[nodiscard]] auto crafting_count() const -> size_t { return crafting_recipes_.size(); }

private:
    auto load_recipes(const std::filesystem::path& path, const std::string& section_name,
                      const item_registry& items, std::vector<crafting::craft_recipe>& dest)
        -> result<size_t, std::string>;

    std::vector<crafting::craft_recipe> alchemy_recipes_;
    std::vector<crafting::craft_recipe> crafting_recipes_;
    std::unordered_map<int32_t, const crafting::craft_recipe*> id_index_;
    std::unordered_map<std::string, const crafting::craft_recipe*> name_index_;
};

}  // namespace hb
