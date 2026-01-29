#pragma once

// config_system.h
// Configuration subsystem for loading and managing server configuration

#include "core/subsystem.h"
#include "core/result.h"
#include "config/server_config.h"

#include <filesystem>
#include <memory>
#include <functional>

namespace hb {

class config_system : public subsystem {
public:
    config_system();
    ~config_system() override;

    // subsystem interface
    [[nodiscard]] auto name() const -> std::string_view override { return "config"; }
    void initialize() override;
    void shutdown() override;

    // Load configurations
    auto load_server_config(const std::filesystem::path& path) -> result<void, std::string>;
    auto load_game_config(const std::filesystem::path& path) -> result<void, std::string>;
    auto load_admin_config(const std::filesystem::path& path) -> result<void, std::string>;

    // Reload all configurations
    auto reload() -> result<void, std::string>;

    // Get configurations (read-only)
    [[nodiscard]] auto server() const -> const server_config&;
    [[nodiscard]] auto game() const -> const game_config&;
    [[nodiscard]] auto admin() const -> const admin_config&;

    // Register callback for config changes
    using config_changed_callback = std::function<void()>;
    void on_config_changed(config_changed_callback callback);

private:
    server_config server_config_;
    game_config game_config_;
    admin_config admin_config_;

    std::filesystem::path server_config_path_;
    std::filesystem::path game_config_path_;
    std::filesystem::path admin_config_path_;

    std::vector<config_changed_callback> change_callbacks_;
};

}  // namespace hb
