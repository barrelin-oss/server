// config_system.cpp
// Configuration subsystem implementation

#include "config/config_system.h"
#include "core/logger.h"
#include "core/events.h"
#include "core/event_bus.h"

#include <nlohmann/json.hpp>
#include <fstream>
#include <sstream>
#include <algorithm>
#include <cctype>

namespace hb {

namespace {

// Trim whitespace from string
auto trim(std::string_view str) -> std::string {
    auto start = str.find_first_not_of(" \t\n\r");
    if (start == std::string_view::npos) return "";
    auto end = str.find_last_not_of(" \t\n\r");
    return std::string(str.substr(start, end - start + 1));
}

// Parse INI-style config file (key = value format)
auto parse_ini_file(const std::filesystem::path& path)
    -> result<std::unordered_map<std::string, std::string>, std::string>
{
    std::ifstream file(path);
    if (!file.is_open()) {
        return result<std::unordered_map<std::string, std::string>, std::string>::err(
            "Failed to open file: " + path.string()
        );
    }

    std::unordered_map<std::string, std::string> config;
    std::string line;
    std::string current_section;

    while (std::getline(file, line)) {
        line = trim(line);

        // Skip empty lines and comments
        if (line.empty() || line[0] == '#' || line[0] == ';') {
            continue;
        }

        // Section header [section]
        if (line[0] == '[' && line.back() == ']') {
            current_section = line.substr(1, line.size() - 2);
            continue;
        }

        // Key = value
        auto eq_pos = line.find('=');
        if (eq_pos != std::string::npos) {
            auto key = trim(line.substr(0, eq_pos));
            auto value = trim(line.substr(eq_pos + 1));

            // Convert key to lowercase for case-insensitive matching
            std::transform(key.begin(), key.end(), key.begin(),
                [](unsigned char c) { return std::tolower(c); });

            config[key] = value;
        }
    }

    return result<std::unordered_map<std::string, std::string>, std::string>::ok(std::move(config));
}

}  // namespace

config_system::config_system() = default;
config_system::~config_system() = default;

void config_system::initialize() {
    LOG_INFO(config, "Configuration system initialized");
    set_initialized(true);
}

void config_system::shutdown() {
    LOG_INFO(config, "Configuration system shut down");
    set_initialized(false);
}

auto config_system::load_server_config(const std::filesystem::path& path)
    -> result<void, std::string>
{
    LOG_INFO(config, "Loading server config from: {}", path.string());

    auto parse_result = parse_ini_file(path);
    if (parse_result.is_err()) {
        return result<void, std::string>::err(parse_result.error());
    }

    auto& config = parse_result.value();

    // Parse server configuration
    if (auto it = config.find("game-server-name"); it != config.end()) {
        server_config_.server_name = it->second;
    }
    if (auto it = config.find("game-server-port"); it != config.end()) {
        server_config_.game_server_port = static_cast<uint16_t>(std::stoi(it->second));
    }
    if (auto it = config.find("log-server-address"); it != config.end()) {
        server_config_.log_server_addr = it->second;
    }
    if (auto it = config.find("log-server-port"); it != config.end()) {
        server_config_.log_server_port = static_cast<uint16_t>(std::stoi(it->second));
    }
    if (auto it = config.find("gate-server-address"); it != config.end()) {
        server_config_.gate_server_addr = it->second;
    }
    if (auto it = config.find("gate-server-port"); it != config.end()) {
        server_config_.gate_server_port = static_cast<uint16_t>(std::stoi(it->second));
    }
    if (auto it = config.find("game-server-mode"); it != config.end()) {
        server_config_.game_server_mode = std::stoi(it->second);
    }

    server_config_path_ = path;

    LOG_INFO(config, "Server config loaded: name={}, port={}",
        server_config_.server_name, server_config_.game_server_port);

    // Publish config loaded event
    event_bus().publish(events::config_loaded_event{
        .config_path = path.string(),
        .success = true,
        .error_message = ""
    });

    return result<void, std::string>::ok();
}

auto config_system::load_game_config(const std::filesystem::path& path)
    -> result<void, std::string>
{
    LOG_INFO(config, "Loading game config from: {}", path.string());

    auto parse_result = parse_ini_file(path);
    if (parse_result.is_err()) {
        return result<void, std::string>::err(parse_result.error());
    }

    auto& config = parse_result.value();

    // Parse game configuration
    if (auto it = config.find("max-clients"); it != config.end()) {
        game_config_.max_clients = static_cast<uint16_t>(std::stoi(it->second));
    }
    if (auto it = config.find("max-level"); it != config.end()) {
        game_config_.max_level = static_cast<uint16_t>(std::stoi(it->second));
    }
    if (auto it = config.find("level-limit"); it != config.end()) {
        game_config_.level_limit = static_cast<uint16_t>(std::stoi(it->second));
    }
    if (auto it = config.find("admin-security"); it != config.end()) {
        game_config_.admin_security = (it->second == "1" || it->second == "true");
    }
    if (auto it = config.find("log-chat"); it != config.end()) {
        game_config_.log_chat = (it->second == "1" || it->second == "true");
    }

    game_config_path_ = path;

    LOG_INFO(config, "Game config loaded");

    return result<void, std::string>::ok();
}

auto config_system::load_admin_config(const std::filesystem::path& path)
    -> result<void, std::string>
{
    LOG_INFO(config, "Loading admin config from: {}", path.string());

    auto parse_result = parse_ini_file(path);
    if (parse_result.is_err()) {
        return result<void, std::string>::err(parse_result.error());
    }

    auto& config = parse_result.value();

    // Parse admin levels
    if (auto it = config.find("adminlevel-who"); it != config.end()) {
        admin_config_.level_who = std::stoi(it->second);
    }
    if (auto it = config.find("adminlevel-kill"); it != config.end()) {
        admin_config_.level_kill = std::stoi(it->second);
    }
    // ... parse more admin levels as needed

    admin_config_path_ = path;

    LOG_INFO(config, "Admin config loaded: {} admin level settings",
        admin_config_.admins.size());

    return result<void, std::string>::ok();
}

auto config_system::reload() -> result<void, std::string> {
    LOG_INFO(config, "Reloading all configurations...");

    if (!server_config_path_.empty()) {
        auto result = load_server_config(server_config_path_);
        if (result.is_err()) {
            return result;
        }
    }

    if (!game_config_path_.empty()) {
        auto result = load_game_config(game_config_path_);
        if (result.is_err()) {
            return result;
        }
    }

    if (!admin_config_path_.empty()) {
        auto result = load_admin_config(admin_config_path_);
        if (result.is_err()) {
            return result;
        }
    }

    // Notify listeners
    for (auto& callback : change_callbacks_) {
        if (callback) {
            callback();
        }
    }

    LOG_INFO(config, "Configuration reload complete");

    return result<void, std::string>::ok();
}

auto config_system::server() const -> const server_config& {
    return server_config_;
}

auto config_system::game() const -> const game_config& {
    return game_config_;
}

auto config_system::admin() const -> const admin_config& {
    return admin_config_;
}

void config_system::on_config_changed(config_changed_callback callback) {
    change_callbacks_.push_back(std::move(callback));
}

// Static methods for server_config

auto server_config::load_from_file(const std::filesystem::path& path)
    -> result<server_config, std::string>
{
    auto parse_result = parse_ini_file(path);
    if (parse_result.is_err()) {
        return result<server_config, std::string>::err(parse_result.error());
    }

    server_config config;
    auto& map = parse_result.value();

    if (auto it = map.find("game-server-name"); it != map.end()) {
        config.server_name = it->second;
    }
    if (auto it = map.find("game-server-port"); it != map.end()) {
        config.game_server_port = static_cast<uint16_t>(std::stoi(it->second));
    }
    if (auto it = map.find("log-server-address"); it != map.end()) {
        config.log_server_addr = it->second;
    }
    if (auto it = map.find("log-server-port"); it != map.end()) {
        config.log_server_port = static_cast<uint16_t>(std::stoi(it->second));
    }
    if (auto it = map.find("gate-server-address"); it != map.end()) {
        config.gate_server_addr = it->second;
    }
    if (auto it = map.find("gate-server-port"); it != map.end()) {
        config.gate_server_port = static_cast<uint16_t>(std::stoi(it->second));
    }
    if (auto it = map.find("game-server-mode"); it != map.end()) {
        config.game_server_mode = std::stoi(it->second);
    }

    return result<server_config, std::string>::ok(std::move(config));
}

auto server_config::load_from_json(const std::filesystem::path& path)
    -> result<server_config, std::string>
{
    std::ifstream file(path);
    if (!file.is_open()) {
        return result<server_config, std::string>::err(
            "Failed to open JSON config: " + path.string()
        );
    }

    try {
        nlohmann::json j;
        file >> j;

        server_config config;
        if (j.contains("server_name")) config.server_name = j["server_name"];
        if (j.contains("game_server_port")) config.game_server_port = j["game_server_port"];
        if (j.contains("log_server_addr")) config.log_server_addr = j["log_server_addr"];
        if (j.contains("log_server_port")) config.log_server_port = j["log_server_port"];
        if (j.contains("gate_server_addr")) config.gate_server_addr = j["gate_server_addr"];
        if (j.contains("gate_server_port")) config.gate_server_port = j["gate_server_port"];

        return result<server_config, std::string>::ok(std::move(config));
    } catch (const std::exception& e) {
        return result<server_config, std::string>::err(
            "JSON parse error: " + std::string(e.what())
        );
    }
}

auto server_config::save_to_json(const std::filesystem::path& path) const
    -> result<void, std::string>
{
    nlohmann::json j;
    j["server_name"] = server_name;
    j["game_server_addr"] = game_server_addr;
    j["game_server_port"] = game_server_port;
    j["log_server_addr"] = log_server_addr;
    j["log_server_port"] = log_server_port;
    j["gate_server_addr"] = gate_server_addr;
    j["gate_server_port"] = gate_server_port;
    j["game_server_mode"] = game_server_mode;

    std::ofstream file(path);
    if (!file.is_open()) {
        return result<void, std::string>::err(
            "Failed to open file for writing: " + path.string()
        );
    }

    file << j.dump(4);
    return result<void, std::string>::ok();
}

auto game_config::load_from_file(const std::filesystem::path& path)
    -> result<game_config, std::string>
{
    auto parse_result = parse_ini_file(path);
    if (parse_result.is_err()) {
        return result<game_config, std::string>::err(parse_result.error());
    }

    game_config config;
    // Parse settings...

    return result<game_config, std::string>::ok(std::move(config));
}

auto admin_config::load_from_file(const std::filesystem::path& path)
    -> result<admin_config, std::string>
{
    auto parse_result = parse_ini_file(path);
    if (parse_result.is_err()) {
        return result<admin_config, std::string>::err(parse_result.error());
    }

    admin_config config;
    // Parse admin settings...

    return result<admin_config, std::string>::ok(std::move(config));
}

}  // namespace hb
