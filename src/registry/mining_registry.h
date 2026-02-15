#pragma once

// mining_registry.h
// YAML-driven registry for mineral type configurations

#include "core/subsystem.h"
#include "core/result.h"
#include "crafting/mining_config.h"

#include <vector>
#include <unordered_map>
#include <string>
#include <string_view>
#include <filesystem>

namespace hb
{

class item_registry;

class mining_registry : public subsystem
{
public:
    mining_registry();
    ~mining_registry() override;

    // Subsystem interface
    [[nodiscard]] auto name() const -> std::string_view override { return "mining_registry"; }
    void initialize() override;
    void shutdown() override;

    // Load mineral types from YAML file, resolving item names via item_registry
    auto load_from_file(const std::filesystem::path& path, const item_registry& items) -> result<size_t, std::string>;

    // Lookup
    [[nodiscard]] auto get_type(int32_t type_id) const -> const crafting::mineral_type_config*;
    [[nodiscard]] auto get_all() const -> const std::vector<crafting::mineral_type_config>& { return types_; }
    [[nodiscard]] auto count() const -> size_t { return types_.size(); }

private:
    std::vector<crafting::mineral_type_config> types_;
    std::unordered_map<int32_t, size_t> id_index_;
};

} // namespace hb
