#pragma once

#include "spawn_rule.h"
#include "spawn_context.h"
#include "random_spawn_whitelist.h"
#include "core/subsystem.h"
#include "core/result.h"
#include <filesystem>
#include <random>
#include <unordered_map>
#include <memory>
#include <vector>

namespace hb::npc
{

// Main spawn rule engine subsystem
class spawn_rule_engine : public subsystem
{
public:
    spawn_rule_engine();
    ~spawn_rule_engine() override = default;

    // Subsystem interface
    auto name() const -> std::string_view override { return "spawn_rule_engine"; }
    void initialize() override;
    void shutdown() override;

    // Load spawn tables from YAML file
    auto load_from_file(const std::filesystem::path& path) -> result<size_t, std::string>;

    // Reload spawn tables (hot-reload)
    auto reload() -> result<void, std::string>;

    // Select a random NPC based on context
    auto select_npc(const spawn_context& ctx) const -> std::optional<npc_id>;

    // Get all matching NPCs with weights (for debugging)
    auto get_matching_npcs(const spawn_context& ctx) const -> std::vector<weighted_npc_entry>;

    // Get rule statistics (for debugging)
    auto get_rule_count() const -> size_t { return global_rules_.size(); }
    auto get_map_rule_count(std::string_view map_name) const -> size_t;

    // Get the random spawn whitelist
    [[nodiscard]] auto whitelist() const -> const random_spawn_whitelist& { return whitelist_; }

    // Check if an NPC can be randomly spawned
    [[nodiscard]] auto can_random_spawn(std::string_view name) const -> bool { return whitelist_.is_allowed(name); }

    [[nodiscard]] auto can_random_spawn(npc_id id) const -> bool { return whitelist_.is_allowed(id); }

    // Get special attack info for a spawned NPC
    [[nodiscard]] auto get_spawn_entry(std::string_view name) const -> std::optional<random_spawn_entry>
    {
        return whitelist_.get(name);
    }

private:
    // Internal helpers
    auto merge_weighted_lists(const std::vector<std::vector<weighted_npc_entry>>& lists) const
        -> std::vector<weighted_npc_entry>;

    auto select_from_weighted(const std::vector<weighted_npc_entry>& entries) const -> std::optional<npc_id>;

    // Storage
    std::filesystem::path config_path_;
    std::vector<std::unique_ptr<spawn_rule>> global_rules_;
    std::unordered_map<std::string, std::vector<size_t>> map_rule_indices_;
    random_spawn_whitelist whitelist_;

    // Random number generation
    mutable std::mt19937 rng_;
};

} // namespace hb::npc
