#pragma once

// loot_registry.h
// YAML-driven loot table registry

#include "core/subsystem.h"
#include "core/result.h"
#include "npc/loot_config.h"

#include <unordered_map>
#include <string>
#include <string_view>
#include <filesystem>

namespace hb {

// Loot registry subsystem - loads loot pools and NPC loot configs from YAML
class loot_registry : public subsystem {
public:
    loot_registry();
    ~loot_registry() override;

    // Subsystem interface
    [[nodiscard]] auto name() const -> std::string_view override { return "loot_registry"; }
    void initialize() override;
    void shutdown() override;

    // Load loot tables from YAML file
    auto load_from_file(const std::filesystem::path& path) -> result<size_t, std::string>;

    // Get a named item pool
    [[nodiscard]] auto get_pool(std::string_view name) const -> const npc::item_pool*;

    // Get loot config for an NPC sprite_id
    [[nodiscard]] auto get_config(int16_t sprite_id) const -> const npc::npc_loot_config*;

    // Stats
    [[nodiscard]] auto pool_count() const -> size_t { return pools_.size(); }
    [[nodiscard]] auto config_count() const -> size_t { return configs_.size(); }

    // Iteration
    [[nodiscard]] auto configs() const -> const std::unordered_map<int16_t, npc::npc_loot_config>& { return configs_; }

private:
    std::unordered_map<std::string, npc::item_pool> pools_;
    std::unordered_map<int16_t, npc::npc_loot_config> configs_;
};

}  // namespace hb
