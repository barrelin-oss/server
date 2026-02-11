#pragma once

// fishing_registry.h
// YAML-driven registry for fish type configurations

#include "core/subsystem.h"
#include "core/result.h"
#include "crafting/fishing_config.h"

#include <vector>
#include <unordered_map>
#include <string>
#include <string_view>
#include <filesystem>

namespace hb {

class item_registry;

class fishing_registry : public subsystem
{
public:
    fishing_registry();
    ~fishing_registry() override;

    // Subsystem interface
    [[nodiscard]] auto name() const -> std::string_view override { return "fishing_registry"; }
    void initialize() override;
    void shutdown() override;

    // Load fish types from YAML file, resolving item names via item_registry
    auto load_from_file(const std::filesystem::path& path, const item_registry& items)
        -> result<size_t, std::string>;

    // Lookup
    [[nodiscard]] auto get_type(int32_t type_id) const -> const crafting::fish_type_config*;
    [[nodiscard]] auto get_all() const -> const std::vector<crafting::fish_type_config>& { return types_; }
    [[nodiscard]] auto count() const -> size_t { return types_.size(); }

    // Weighted random selection of fish type
    [[nodiscard]] auto roll_fish_type() const -> const crafting::fish_type_config*;

private:
    std::vector<crafting::fish_type_config> types_;
    std::unordered_map<int32_t, size_t> id_index_;
    int32_t total_weight_{};
};

}  // namespace hb
