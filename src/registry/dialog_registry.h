#pragma once

// dialog_registry.h
// YAML-driven dialog tree registry

#include "core/subsystem.h"
#include "core/result.h"
#include "npc/dialog_config.h"

#include <unordered_map>
#include <string>
#include <string_view>
#include <filesystem>

namespace hb
{

// Dialog registry subsystem - loads dialog trees from YAML
class dialog_registry : public subsystem
{
public:
    dialog_registry();
    ~dialog_registry() override;

    // Subsystem interface
    [[nodiscard]] auto name() const -> std::string_view override { return "dialog_registry"; }
    void initialize() override;
    void shutdown() override;

    // Load dialogs from YAML file
    auto load_from_file(const std::filesystem::path& path) -> result<size_t, std::string>;

    // Get dialog tree by NPC name
    [[nodiscard]] auto get_dialog(std::string_view npc_name) const -> const npc::dialog_tree*;

    // Get a specific node within an NPC's dialog tree
    [[nodiscard]] auto get_node(std::string_view npc_name, std::string_view node_id) const -> const npc::dialog_node*;

    // Stats
    [[nodiscard]] auto count() const -> size_t { return dialogs_.size(); }

private:
    std::unordered_map<std::string, npc::dialog_tree> dialogs_;
};

} // namespace hb
