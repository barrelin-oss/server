// config_system.cpp
// Configuration subsystem implementation

#include "config/config_system.h"
#include "core/logger.h"
#include "core/events.h"
#include "core/event_bus.h"

#include <nlohmann/json.hpp>
#include <yaml-cpp/yaml.h>
#include <fstream>
#include <sstream>
#include <algorithm>
#include <cctype>

namespace hb {

namespace {

// Helper to safely get YAML values with defaults
template<typename T>
auto yaml_get(const YAML::Node& node, const std::string& key, T default_value) -> T
{
    if (node[key]) {
        try {
            return node[key].as<T>();
        } catch (...) {
            return default_value;
        }
    }
    return default_value;
}

// Trim whitespace from string
auto trim(std::string_view str) -> std::string {
    auto start = str.find_first_not_of(" \t\n\r");
    if (start == std::string_view::npos) return "";
    auto end = str.find_last_not_of(" \t\n\r");
    return std::string(str.substr(start, end - start + 1));
}

// Parse INI-style config file (legacy format, key = value)
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

    // Check file extension to determine format
    auto ext = path.extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(),
        [](unsigned char c) { return std::tolower(c); });

    if (ext == ".yaml" || ext == ".yml") {
        return load_yaml_config(path);
    }

    // Legacy INI format
    return load_ini_config(path);
}

auto config_system::load_yaml_config(const std::filesystem::path& path)
    -> result<void, std::string>
{
    try {
        YAML::Node config = YAML::LoadFile(path.string());

        // Server section
        if (auto server = config["server"]) {
            server_config_.server_name = yaml_get<std::string>(server, "name", "HGServer");
            server_config_.game_server_port = yaml_get<uint16_t>(server, "port", 2848);
            server_config_.game_server_mode = yaml_get<int>(server, "mode", 0);
        }

        // Self-contained mode
        server_config_.self_contained = yaml_get<bool>(config, "self_contained", true);

        // Database section
        if (auto db = config["database"]) {
            server_config_.database.host = yaml_get<std::string>(db, "host", "localhost");
            server_config_.database.port = yaml_get<uint16_t>(db, "port", 5432);
            server_config_.database.database = yaml_get<std::string>(db, "name", "helbreath");
            server_config_.database.username = yaml_get<std::string>(db, "user", "hgserver");
            server_config_.database.password = yaml_get<std::string>(db, "password", "");
            server_config_.database.pool_size = yaml_get<uint32_t>(db, "pool_size", 10);
        }

        // WebSocket section
        if (auto ws = config["websocket"]) {
            server_config_.websocket.bind_address = yaml_get<std::string>(ws, "bind", "0.0.0.0");
            server_config_.websocket.port = yaml_get<uint16_t>(ws, "port", 2848);
            server_config_.websocket.max_connections = yaml_get<int>(ws, "max_connections", 2000);
        }

        // Auth section
        if (auto auth = config["auth"]) {
            server_config_.auth.max_characters_per_account = yaml_get<uint32_t>(auth, "max_characters", 4);
            server_config_.auth.allow_registration = yaml_get<bool>(auth, "allow_registration", true);
            server_config_.auth.session_duration = std::chrono::seconds{
                yaml_get<int>(auth, "session_timeout", 3600)};
        }

        // Forum auth section
        if (auto forum = config["forum_auth"]) {
            server_config_.forum_auth.enabled = yaml_get<bool>(forum, "enabled", false);
            server_config_.forum_auth.login_url = yaml_get<std::string>(forum, "login_url", "");
            server_config_.forum_auth.validate_url = yaml_get<std::string>(forum, "validate_url", "");
            server_config_.forum_auth.api_key = yaml_get<std::string>(forum, "api_key", "");
        }

        // Auto-save section
        if (auto auto_save = config["auto_save"]) {
            server_config_.auto_save.enabled = yaml_get<bool>(auto_save, "enabled", true);
            server_config_.auto_save.interval_seconds = yaml_get<uint32_t>(auto_save, "interval_seconds", 300);
        }

        // Logging section
        if (auto logging = config["logging"]) {
            server_config_.logging.console_level = yaml_get<std::string>(logging, "console_level", "trace");
            server_config_.logging.file_level = yaml_get<std::string>(logging, "file_level", "trace");
            server_config_.logging.log_directory = yaml_get<std::string>(logging, "directory", "logs");
            server_config_.logging.log_file = yaml_get<std::string>(logging, "file", "hgserver.log");
            server_config_.logging.max_file_size_mb = yaml_get<uint32_t>(logging, "max_size_mb", 10);
            server_config_.logging.max_files = yaml_get<uint32_t>(logging, "max_files", 3);
        }

        // Legacy protocol section
        if (auto legacy = config["legacy"]) {
            server_config_.enable_legacy_protocol = yaml_get<bool>(legacy, "enabled", false);
            server_config_.legacy_port = yaml_get<uint16_t>(legacy, "port", 2849);
        }

        // External servers section (legacy)
        if (auto ext = config["external_servers"]) {
            if (auto log_srv = ext["log_server"]) {
                server_config_.log_server_addr = yaml_get<std::string>(log_srv, "address", "127.0.0.1");
                server_config_.log_server_port = yaml_get<uint16_t>(log_srv, "port", 3000);
            }
            if (auto gate_srv = ext["gate_server"]) {
                server_config_.gate_server_addr = yaml_get<std::string>(gate_srv, "address", "127.0.0.1");
                server_config_.gate_server_port = yaml_get<uint16_t>(gate_srv, "port", 4000);
            }
        }

        server_config_path_ = path;

        LOG_INFO(config, "Server config loaded (YAML): name={}, port={}, self_contained={}",
            server_config_.server_name, server_config_.game_server_port, server_config_.self_contained);

        // Publish config loaded event
        event_bus().publish(events::config_loaded_event{
            .config_path = path.string(),
            .success = true,
            .error_message = ""
        });

        return result<void, std::string>::ok();

    } catch (const YAML::Exception& e) {
        return result<void, std::string>::err(std::string("YAML parse error: ") + e.what());
    } catch (const std::exception& e) {
        return result<void, std::string>::err(std::string("Config load error: ") + e.what());
    }
}

auto config_system::load_ini_config(const std::filesystem::path& path)
    -> result<void, std::string>
{
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

    // Self-contained mode
    if (auto it = config.find("self-contained"); it != config.end()) {
        server_config_.self_contained = (it->second == "1" || it->second == "true");
    }

    // Database configuration
    if (auto it = config.find("db-host"); it != config.end()) {
        server_config_.database.host = it->second;
    }
    if (auto it = config.find("db-port"); it != config.end()) {
        server_config_.database.port = static_cast<uint16_t>(std::stoi(it->second));
    }
    if (auto it = config.find("db-name"); it != config.end()) {
        server_config_.database.database = it->second;
    }
    if (auto it = config.find("db-user"); it != config.end()) {
        server_config_.database.username = it->second;
    }
    if (auto it = config.find("db-password"); it != config.end()) {
        server_config_.database.password = it->second;
    }
    if (auto it = config.find("db-pool-size"); it != config.end()) {
        server_config_.database.pool_size = static_cast<uint32_t>(std::stoi(it->second));
    }

    // WebSocket configuration
    if (auto it = config.find("ws-bind"); it != config.end()) {
        server_config_.websocket.bind_address = it->second;
    }
    if (auto it = config.find("ws-port"); it != config.end()) {
        server_config_.websocket.port = static_cast<uint16_t>(std::stoi(it->second));
    }
    if (auto it = config.find("ws-max-connections"); it != config.end()) {
        server_config_.websocket.max_connections = std::stoi(it->second);
    }

    // Auth configuration
    if (auto it = config.find("max-characters"); it != config.end()) {
        server_config_.auth.max_characters_per_account = static_cast<uint32_t>(std::stoi(it->second));
    }
    if (auto it = config.find("allow-registration"); it != config.end()) {
        server_config_.auth.allow_registration = (it->second == "1" || it->second == "true");
    }
    if (auto it = config.find("session-timeout"); it != config.end()) {
        server_config_.auth.session_duration = std::chrono::seconds{std::stoi(it->second)};
    }

    // Legacy protocol
    if (auto it = config.find("enable-legacy-protocol"); it != config.end()) {
        server_config_.enable_legacy_protocol = (it->second == "1" || it->second == "true");
    }
    if (auto it = config.find("legacy-port"); it != config.end()) {
        server_config_.legacy_port = static_cast<uint16_t>(std::stoi(it->second));
    }

    // Logging configuration
    if (auto it = config.find("log-console-level"); it != config.end()) {
        server_config_.logging.console_level = it->second;
    }
    if (auto it = config.find("log-file-level"); it != config.end()) {
        server_config_.logging.file_level = it->second;
    }
    if (auto it = config.find("log-directory"); it != config.end()) {
        server_config_.logging.log_directory = it->second;
    }
    if (auto it = config.find("log-file"); it != config.end()) {
        server_config_.logging.log_file = it->second;
    }
    if (auto it = config.find("log-max-size-mb"); it != config.end()) {
        server_config_.logging.max_file_size_mb = static_cast<uint32_t>(std::stoi(it->second));
    }
    if (auto it = config.find("log-max-files"); it != config.end()) {
        server_config_.logging.max_files = static_cast<uint32_t>(std::stoi(it->second));
    }

    server_config_path_ = path;

    LOG_INFO(config, "Server config loaded (INI): name={}, port={}, self_contained={}",
        server_config_.server_name, server_config_.game_server_port, server_config_.self_contained);

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

auto server_config::to_json() const -> nlohmann::json {
    nlohmann::json j;
    j["server_name"] = server_name;
    j["self_contained"] = self_contained;
    j["enable_legacy_protocol"] = enable_legacy_protocol;
    j["legacy_port"] = legacy_port;

    j["database"] = {
        {"host", database.host},
        {"port", database.port},
        {"database", database.database},
        {"username", database.username},
        {"password", database.password},
        {"pool_size", database.pool_size},
        {"connection_timeout_ms", database.connection_timeout.count()},
        {"query_timeout_ms", database.query_timeout.count()}
    };

    j["websocket"] = {
        {"bind_address", websocket.bind_address},
        {"port", websocket.port},
        {"max_connections", websocket.max_connections},
        {"enable_ping", websocket.enable_ping},
        {"ping_interval_seconds", websocket.ping_interval_seconds}
    };

    j["auth"] = {
        {"max_characters_per_account", auth.max_characters_per_account},
        {"session_duration_seconds", auth.session_duration.count()},
        {"allow_registration", auth.allow_registration},
        {"max_login_attempts", auth.max_login_attempts},
        {"lockout_duration_seconds", auth.lockout_duration.count()}
    };

    j["forum_auth"] = {
        {"enabled", forum_auth.enabled},
        {"login_url", forum_auth.login_url},
        {"validate_url", forum_auth.validate_url},
        {"api_key", forum_auth.api_key}
    };

    j["auto_save"] = {
        {"enabled", auto_save.enabled},
        {"interval_seconds", auto_save.interval_seconds}
    };

    j["logging"] = {
        {"console_level", logging.console_level},
        {"file_level", logging.file_level},
        {"log_directory", logging.log_directory},
        {"log_file", logging.log_file},
        {"max_file_size_mb", logging.max_file_size_mb},
        {"max_files", logging.max_files}
    };

    return j;
}

auto server_config::to_json_sanitized() const -> nlohmann::json {
    auto j = to_json();
    if (j.contains("database") && j["database"].contains("password")) {
        j["database"]["password"] = "***";
    }
    if (j.contains("forum_auth") && j["forum_auth"].contains("api_key")) {
        j["forum_auth"]["api_key"] = "***";
    }
    return j;
}

auto server_config::apply_dot_values(const nlohmann::json& values)
    -> result<std::vector<std::string>, std::string>
{
    std::vector<std::string> applied;

    for (auto& [key, value] : values.items()) {
        // Skip sentinel values
        if (value.is_string() && value.get<std::string>() == "***") continue;

        // Parse dot-notation: "section.field"
        auto dot = key.find('.');
        if (dot == std::string::npos) continue;

        auto section = key.substr(0, dot);
        auto field = key.substr(dot + 1);

        bool matched = false;
        if (section == "database") {
            if (field == "host" && value.is_string()) { database.host = value; matched = true; }
            else if (field == "port" && value.is_number()) { database.port = value; matched = true; }
            else if (field == "database" && value.is_string()) { database.database = value; matched = true; }
            else if (field == "username" && value.is_string()) { database.username = value; matched = true; }
            else if (field == "password" && value.is_string()) { database.password = value; matched = true; }
            else if (field == "pool_size" && value.is_number()) { database.pool_size = value; matched = true; }
        } else if (section == "websocket") {
            if (field == "bind_address" && value.is_string()) { websocket.bind_address = value; matched = true; }
            else if (field == "port" && value.is_number()) { websocket.port = value; matched = true; }
            else if (field == "max_connections" && value.is_number()) { websocket.max_connections = value; matched = true; }
            else if (field == "enable_ping" && value.is_boolean()) { websocket.enable_ping = value; matched = true; }
            else if (field == "ping_interval_seconds" && value.is_number()) { websocket.ping_interval_seconds = value; matched = true; }
        } else if (section == "auth") {
            if (field == "allow_registration" && value.is_boolean()) { auth.allow_registration = value; matched = true; }
            else if (field == "max_login_attempts" && value.is_number()) { auth.max_login_attempts = value; matched = true; }
        } else if (section == "auto_save") {
            if (field == "enabled" && value.is_boolean()) { auto_save.enabled = value; matched = true; }
            else if (field == "interval_seconds" && value.is_number()) { auto_save.interval_seconds = value; matched = true; }
        } else if (section == "logging") {
            if (field == "console_level" && value.is_string()) { logging.console_level = value; matched = true; }
            else if (field == "file_level" && value.is_string()) { logging.file_level = value; matched = true; }
        } else if (section == "forum_auth") {
            if (field == "enabled" && value.is_boolean()) { forum_auth.enabled = value; matched = true; }
            else if (field == "api_key" && value.is_string()) { forum_auth.api_key = value; matched = true; }
        }

        if (matched) applied.push_back(key);
    }

    return result<std::vector<std::string>, std::string>::ok(std::move(applied));
}

auto server_config::requires_restart(std::string_view key) -> bool {
    return key.starts_with("database.") ||
           key.starts_with("websocket.") ||
           key == "enable_legacy_protocol" ||
           key == "legacy_port";
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
