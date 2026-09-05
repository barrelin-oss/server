#pragma once

// server_config.h
// Server configuration data structures

#include "core/result.h"

#include <nlohmann/json.hpp>
#include <string>
#include <filesystem>
#include <cstdint>
#include <optional>
#include <chrono>
#include <vector>

namespace hb
{

// Database configuration
struct database_config
{
    std::string host = "localhost";
    uint16_t port = 5432;
    std::string database = "helbreath";
    std::string username = "hgserver";
    std::string password;
    uint32_t pool_size = 10;
    std::chrono::milliseconds connection_timeout{5000};
    std::chrono::milliseconds query_timeout{30000};
};

// WebSocket configuration
struct websocket_config
{
    std::string bind_address = "0.0.0.0";
    uint16_t port = 2848;
    int max_connections = 2000;
    bool enable_ping = true;
    int ping_interval_seconds = 30;
};

// Authentication configuration
struct auth_config
{
    uint32_t max_characters_per_account = 4;
    std::chrono::seconds session_duration{3600};      // 1 hour
    std::chrono::seconds session_max_duration{86400}; // 24 hours
    bool allow_registration = true;
    uint32_t max_login_attempts = 5;
    std::chrono::seconds lockout_duration{300}; // 5 minutes
    uint32_t max_registration_attempts = 3;
    std::chrono::seconds registration_cooldown{3600}; // 1 hour
};

// Player regeneration configuration (amounts follow the legacy formulas;
// the interval says how long a "full roll" takes — larger = slower regen)
struct regen_config
{
    int32_t tick_ms = 5000;         // How often regen is processed for everyone
    int32_t hp_interval_ms = 15000; // Full HP roll (1d(VIT), min VIT/2) per this interval
    int32_t mp_interval_ms = 20000; // Full MP roll (1d(MAG)) per this interval
    int32_t sp_interval_ms = 10000; // Full SP roll (1d(VIT/3)) per this interval
};

// Attack pacing (see combat/attack_timing.h). interval = base_ms + weapon.speed *
// speed_step_ms, plus str_penalty_ms per STR point the attacker is short of the
// weapon's str_speed_req (defaults to speed * str_per_speed). Clamped to
// [min_ms, max_ms]; tolerance_ms is latency slack before a swing is refused.
struct attack_speed_config
{
    bool enabled = true;
    int32_t base_ms = 1000;
    int32_t speed_step_ms = 60;
    int32_t str_penalty_ms = 25;
    int32_t min_ms = 300;
    int32_t max_ms = 3000;
    int32_t tolerance_ms = 100;
    int32_t str_per_speed = 6;
};

// Forum authentication configuration (external PHP auth)
struct forum_auth_config
{
    bool enabled = false;
    std::string login_url;    // URL to auth_login.php
    std::string validate_url; // URL to auth_validate.php
    std::string api_key;
};

// Auto-save configuration
struct auto_save_config
{
    bool enabled = true;
    uint32_t interval_seconds = 300; // 5 minutes default
};

// Logging configuration
struct logging_config
{
    std::string console_level = "trace"; // trace, debug, info, warn, error, critical, off
    std::string file_level = "trace";
    std::string log_directory = "logs";
    std::string log_file = "hgserver.log";
    uint32_t max_file_size_mb = 10;
    uint32_t max_files = 3;
};

// Main server configuration
struct server_config
{
    // Server identity
    std::string server_name = "HGServer";

    // Game server settings (legacy)
    std::string game_server_addr; // Auto-detected if empty
    uint16_t game_server_port = 2848;

    // Log server connection (legacy - optional with self-contained auth)
    std::string log_server_addr = "127.0.0.1";
    uint16_t log_server_port = 3000;

    // Gate server connection (legacy - optional with self-contained auth)
    std::string gate_server_addr = "127.0.0.1";
    uint16_t gate_server_port = 4000;

    // Server mode
    int game_server_mode = 0;

    // Self-contained mode (uses database for auth instead of external servers)
    bool self_contained = true;

    // Database configuration
    database_config database;

    // WebSocket configuration
    websocket_config websocket;

    // Authentication configuration
    auth_config auth;

    // Forum authentication configuration
    forum_auth_config forum_auth;

    // Player regeneration configuration
    regen_config regen;

    // Attack pacing per weapon speed and STR
    attack_speed_config attack_speed;

    // Auto-save configuration
    auto_save_config auto_save;

    // Logging configuration
    logging_config logging;

    // Game tick interval (milliseconds)
    uint32_t tick_interval_ms = 20; // 50 Hz default

    // Ground item expiry (seconds before ground items despawn)
    uint32_t ground_item_expiry_seconds = 600; // 10 minutes

    // Max inventory slots per player (legacy: 50)
    int16_t inventory_slots = 50;

    // Enable legacy protocol (for backwards compatibility)
    bool enable_legacy_protocol = false;
    uint16_t legacy_port = 2849;

    // Load from legacy INI-style config file
    static auto load_from_file(const std::filesystem::path& path) -> result<server_config, std::string>;

    // Load from JSON config file
    static auto load_from_json(const std::filesystem::path& path) -> result<server_config, std::string>;

    // Save to JSON config file
    auto save_to_json(const std::filesystem::path& path) const -> result<void, std::string>;

    // Full JSON serialization (for admin API)
    [[nodiscard]] auto to_json() const -> nlohmann::json;

    // Sanitized version (passwords/keys replaced with "***")
    [[nodiscard]] auto to_json_sanitized() const -> nlohmann::json;

    // Apply dot-notation key-value changes (e.g., "database.host" → "newhost")
    // Returns list of applied keys. Skips keys whose value equals "***".
    auto apply_dot_values(const nlohmann::json& values) -> result<std::vector<std::string>, std::string>;

    // Keys that require restart to take effect
    [[nodiscard]] static auto requires_restart(std::string_view key) -> bool;
};

// Game balance/settings configuration
struct game_config
{
    // Player limits
    uint16_t max_clients = 2000;
    uint16_t max_level = 180;
    uint16_t level_limit = 20;

    // Combat settings
    uint8_t minimum_hit_ratio = 15;
    uint8_t maximum_hit_ratio = 99;

    // Timing (milliseconds)
    uint32_t hp_regen_time = 15000;
    uint32_t mp_regen_time = 20000;
    uint32_t sp_regen_time = 10000;
    uint32_t hunger_time = 60000;
    uint32_t poison_time = 12000;
    uint32_t auto_save_time = 600000;

    // Death/Respawn
    uint32_t respawn_delay_ms = 5000;

    // Economy
    uint32_t max_reward_gold = 99999999;

    // Admin settings
    bool admin_security = true;
    bool log_chat = false;

    // Enemy kill settings
    bool enemy_kill_mode = false;
    int enemy_kill_adjust = 0;

    // View mode / fair resolution (default for all maps)
    uint8_t default_view_mode = 0; // 0=scaled, 1=extended, 2=special
    int16_t fair_width = 800;      // Fair zone width in pixels
    int16_t fair_height = 600;     // Fair zone height in pixels

    // Misc
    uint16_t char_point_limit = 1000;
    uint16_t char_stat_limit = 0;
    uint16_t char_skill_limit = 0;

    static auto load_from_file(const std::filesystem::path& path) -> result<game_config, std::string>;
};

// Admin configuration
struct admin_entry
{
    std::string name;
    int level = 0;
};

struct admin_config
{
    std::vector<admin_entry> admins;
    std::vector<std::string> banned_ips;

    // Admin command levels
    int level_who = 1;
    int level_kill = 10;
    int level_revive = 10;
    int level_closeconn = 10;
    int level_checkrep = 1;
    int level_shutdown = 20;
    int level_observer = 10;
    int level_shutup = 10;
    int level_teleport = 10;
    int level_summon = 10;
    int level_createitem = 20;
    // ... more admin levels

    static auto load_from_file(const std::filesystem::path& path) -> result<admin_config, std::string>;
};

} // namespace hb
