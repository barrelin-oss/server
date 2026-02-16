#pragma once

// npc_registry.h
// Read-only NPC template registry

#include "core/subsystem.h"
#include "core/result.h"
#include "registry/npc_template.h"
#include "npc/special_ability.h"

#include <vector>
#include <unordered_map>
#include <string>
#include <string_view>
#include <filesystem>

namespace hb
{

// NPC registry subsystem - loads and provides access to NPC templates
class npc_registry : public subsystem
{
public:
    npc_registry();
    ~npc_registry() override;

    // Subsystem interface
    [[nodiscard]] auto name() const -> std::string_view override { return "npc_registry"; }
    void initialize() override;
    void shutdown() override;

    // Load NPC definitions from config file (auto-detects format)
    auto load_from_file(const std::filesystem::path& path) -> result<size_t, std::string>;

    // Load NPC definitions from YAML file
    auto load_from_yaml(const std::filesystem::path& path) -> result<size_t, std::string>;

    // Get NPC by ID
    [[nodiscard]] auto get(npc_id id) const -> const npc_template*;

    // Get NPC by name (case-insensitive)
    [[nodiscard]] auto find_by_name(std::string_view name) const -> const npc_template*;

    // Get all NPCs of a specific type
    [[nodiscard]] auto by_type(npc_type type) const -> std::vector<const npc_template*>;

    // Get all boss NPCs
    [[nodiscard]] auto bosses() const -> std::vector<const npc_template*>;

    // Get all NPCs within a level range
    [[nodiscard]] auto by_level_range(int min_level, int max_level) const -> std::vector<const npc_template*>;

    // Get total count
    [[nodiscard]] auto count() const -> size_t;

    // Check if an NPC exists
    [[nodiscard]] auto exists(npc_id id) const -> bool;

    // Iterate all NPCs
    [[nodiscard]] auto all() const -> const std::vector<npc_template>&;

    // Load special ability pool config
    auto load_sa_config(const std::filesystem::path& path) -> result<size_t, std::string>;

    // Get loaded SA config
    [[nodiscard]] auto sa_cfg() const -> const npc::sa_config& { return sa_config_; }

private:
    // Parse a single NPC line from config
    auto parse_npc_line(std::string_view line, int line_num) -> result<npc_template, std::string>;

    // Storage
    std::vector<npc_template> npcs_;
    std::unordered_map<uint16_t, size_t> id_index_;
    std::unordered_map<std::string, size_t> name_index_;

    // Special ability pool config
    npc::sa_config sa_config_;
};

} // namespace hb
