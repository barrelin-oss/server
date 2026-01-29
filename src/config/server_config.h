#pragma once

// server_config.h
// Server configuration data structures

#include "core/result.h"

#include <string>
#include <filesystem>
#include <cstdint>
#include <optional>

namespace hb {

// Main server configuration
struct server_config {
    // Server identity
    std::string server_name = "HGServer";

    // Game server settings
    std::string game_server_addr;  // Auto-detected if empty
    uint16_t game_server_port = 2848;

    // Log server connection
    std::string log_server_addr = "127.0.0.1";
    uint16_t log_server_port = 3000;

    // Gate server connection
    std::string gate_server_addr = "127.0.0.1";
    uint16_t gate_server_port = 4000;

    // Server mode
    int game_server_mode = 0;

    // Load from legacy INI-style config file
    static auto load_from_file(const std::filesystem::path& path) -> result<server_config, std::string>;

    // Load from JSON config file
    static auto load_from_json(const std::filesystem::path& path) -> result<server_config, std::string>;

    // Save to JSON config file
    auto save_to_json(const std::filesystem::path& path) const -> result<void, std::string>;
};

// Game balance/settings configuration
struct game_config {
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

    // Economy
    uint32_t max_reward_gold = 99999999;

    // Admin settings
    bool admin_security = true;
    bool log_chat = false;

    // Enemy kill settings
    bool enemy_kill_mode = false;
    int enemy_kill_adjust = 0;

    // Misc
    uint16_t char_point_limit = 1000;
    uint16_t char_stat_limit = 0;
    uint16_t char_skill_limit = 0;

    static auto load_from_file(const std::filesystem::path& path) -> result<game_config, std::string>;
};

// Admin configuration
struct admin_entry {
    std::string name;
    int level = 0;
};

struct admin_config {
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

}  // namespace hb
