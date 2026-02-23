#pragma once

// shop_registry.h
// YAML-driven shop registry

#include "core/subsystem.h"
#include "core/result.h"
#include "npc/shop_config.h"

#include <unordered_map>
#include <string>
#include <string_view>
#include <filesystem>

namespace hb
{

// Shop registry subsystem - loads shop configs from YAML
class shop_registry : public subsystem
{
public:
    shop_registry();
    ~shop_registry() override;

    // Subsystem interface
    [[nodiscard]] auto name() const -> std::string_view override { return "shop_registry"; }
    void initialize() override;
    void shutdown() override;

    // Load shops from YAML file
    auto load_from_file(const std::filesystem::path& path) -> result<size_t, std::string>;

    // Get shop config by NPC name
    [[nodiscard]] auto get_shop(std::string_view npc_name) const -> const npc::shop_config*;

    // Stats
    [[nodiscard]] auto count() const -> size_t { return shops_.size(); }

    // Iteration
    [[nodiscard]] auto shops() const -> const std::unordered_map<std::string, npc::shop_config>& { return shops_; }

private:
    std::unordered_map<std::string, npc::shop_config> shops_;
};

} // namespace hb
