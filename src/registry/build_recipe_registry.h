#pragma once

// build_recipe_registry.h
// YAML-driven build recipe registry for manufacturing system

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

class build_recipe_registry : public subsystem
{
public:
    build_recipe_registry();
    ~build_recipe_registry() override;

    // Subsystem interface
    [[nodiscard]] auto name() const -> std::string_view override { return "build_recipe_registry"; }
    void initialize() override;
    void shutdown() override;

    // Load recipes from YAML file, resolving item names via item_registry
    auto load_from_file(const std::filesystem::path& path, const item_registry& items)
        -> result<size_t, std::string>;

    // Lookup
    [[nodiscard]] auto get(int32_t index) const -> const crafting::build_recipe*;
    [[nodiscard]] auto find_by_result(std::string_view name) const -> const crafting::build_recipe*;
    [[nodiscard]] auto get_all() const -> const std::vector<crafting::build_recipe>& { return recipes_; }
    [[nodiscard]] auto count() const -> size_t { return recipes_.size(); }

private:
    std::vector<crafting::build_recipe> recipes_;
    std::unordered_map<std::string, size_t> name_index_;
};

}  // namespace hb
