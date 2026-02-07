#pragma once

// admin_system.h
// Admin/GM command system

#include "core/types.h"
#include "core/result.h"
#include "core/subsystem.h"
#include "admin/command.h"

#include <unordered_map>
#include <string>
#include <string_view>
#include <functional>
#include <memory>
#include <vector>
#include <deque>
#include <chrono>

namespace hb::admin {

// Admin system configuration
struct admin_config {
    char command_prefix{'/'};
    size_t max_log_entries{1000};
    bool log_to_file{true};
    std::string log_file{"admin_commands.log"};
    bool broadcast_major_actions{true};  // Broadcast bans, kicks, etc. to other GMs
    int32_t command_cooldown_ms{0};      // Minimum time between commands (0=disabled)
    bool dev_all_admin{false};           // Grant all players owner-level admin (for testing)
};

// Player admin info
struct admin_info {
    player_id player{};
    std::string name;
    admin_level level{admin_level::player};
    std::chrono::steady_clock::time_point last_command;
    bool is_invisible{false};
    bool is_invincible{false};

    [[nodiscard]] auto can_execute(admin_level required) const -> bool {
        return static_cast<uint8_t>(level) >= static_cast<uint8_t>(required);
    }
};

// Events published by admin system
struct admin_command_executed_event {
    player_id executor{};
    std::string command_name;
    bool success{false};
    std::string result_message;
};

struct admin_level_changed_event {
    player_id player{};
    admin_level old_level{};
    admin_level new_level{};
    player_id changed_by{};
};

struct player_banned_event {
    player_id player{};
    std::string player_name;
    player_id banned_by{};
    std::string reason;
    int64_t duration_seconds{0};  // 0 = permanent
};

struct player_kicked_event {
    player_id player{};
    std::string player_name;
    player_id kicked_by{};
    std::string reason;
};

struct player_muted_event {
    player_id player{};
    std::string player_name;
    player_id muted_by{};
    std::string reason;
    int64_t duration_seconds{0};
};

// Admin system - manages GM commands and moderation
class admin_system : public subsystem {
public:
    using command_callback = std::function<void(const admin_command_executed_event&)>;
    using ban_callback = std::function<void(const player_banned_event&)>;
    using kick_callback = std::function<void(const player_kicked_event&)>;
    using mute_callback = std::function<void(const player_muted_event&)>;
    using level_change_callback = std::function<void(const admin_level_changed_event&)>;

    admin_system();
    ~admin_system() override;

    // Subsystem interface
    [[nodiscard]] auto name() const -> std::string_view override { return "admin_system"; }
    void initialize() override;
    void shutdown() override;
    void update(float delta_time) override;

    // Configuration
    void set_config(const admin_config& config);

    // ========== Command Registration ==========

    // Register a command with a handler function
    void register_command(command_info info, command_handler handler);

    // Register a command object
    void register_command(std::unique_ptr<command> cmd);

    // Unregister a command by name
    void unregister_command(std::string_view name);

    // Check if command exists
    [[nodiscard]] auto has_command(std::string_view name) const -> bool;

    // Get command info
    [[nodiscard]] auto get_command(std::string_view name) const -> const command_info*;

    // Get all registered commands
    [[nodiscard]] auto get_all_commands() const -> std::vector<const command_info*>;

    // Get commands available to a specific admin level
    [[nodiscard]] auto get_commands_for_level(admin_level level) const -> std::vector<const command_info*>;

    // ========== Command Execution ==========

    // Execute a raw command string (e.g., "/spawn slime 5")
    auto execute(player_id executor, std::string_view command_string) -> command_result;

    // Check if player can execute a specific command
    [[nodiscard]] auto can_execute(player_id executor, std::string_view command_name) const -> bool;

    // ========== Admin Level Management ==========

    // Set player's admin level
    void set_admin_level(player_id player, admin_level level, player_id changed_by = {});

    // Get player's admin level
    [[nodiscard]] auto get_admin_level(player_id player) const -> admin_level;

    // Get admin info for a player
    [[nodiscard]] auto get_admin_info(player_id player) const -> const admin_info*;

    // Check if player is an admin (level > player)
    [[nodiscard]] auto is_admin(player_id player) const -> bool;

    // Register admin when they connect
    void register_admin(player_id player, std::string_view name, admin_level level);

    // Unregister admin when they disconnect
    void unregister_admin(player_id player);

    // ========== Moderation Actions ==========

    // Ban a player
    auto ban_player(player_id target, player_id banned_by, std::string_view reason,
                    int64_t duration_seconds = 0) -> command_result;

    // Kick a player
    auto kick_player(player_id target, player_id kicked_by, std::string_view reason) -> command_result;

    // Mute a player
    auto mute_player(player_id target, player_id muted_by, std::string_view reason,
                     int64_t duration_seconds = 0) -> command_result;

    // Unmute a player
    auto unmute_player(player_id target, player_id unmuted_by) -> command_result;

    // Check if player is muted
    [[nodiscard]] auto is_muted(player_id player) const -> bool;

    // Get mute remaining time (0 if not muted)
    [[nodiscard]] auto get_mute_remaining(player_id player) const -> int64_t;

    // ========== GM States ==========

    // Set invisibility
    void set_invisible(player_id player, bool invisible);
    [[nodiscard]] auto is_invisible(player_id player) const -> bool;

    // Set invincibility
    void set_invincible(player_id player, bool invincible);
    [[nodiscard]] auto is_invincible(player_id player) const -> bool;

    // ========== Command Log ==========

    // Get recent command log entries
    [[nodiscard]] auto get_log_entries(size_t count = 100) const -> std::vector<command_log_entry>;

    // Get log entries for a specific executor
    [[nodiscard]] auto get_log_for_player(player_id executor, size_t count = 50) const
        -> std::vector<command_log_entry>;

    // Clear command log
    void clear_log();

    // ========== Help ==========

    // Generate help text for a command
    [[nodiscard]] auto get_help(std::string_view command_name) const -> std::string;

    // Generate help text for all commands available to a level
    [[nodiscard]] auto get_help_all(admin_level level) const -> std::string;

    // ========== Callbacks ==========

    void on_command_executed(command_callback callback);
    void on_player_banned(ban_callback callback);
    void on_player_kicked(kick_callback callback);
    void on_player_muted(mute_callback callback);
    void on_level_changed(level_change_callback callback);

    // ========== Statistics ==========

    [[nodiscard]] auto total_commands_executed() const -> size_t { return total_commands_; }
    [[nodiscard]] auto active_admin_count() const -> size_t;

private:
    void register_builtin_commands();
    void log_command(const command_log_entry& entry);
    auto build_context(player_id executor, const parse_result& parsed) -> command_context;

    admin_config config_;

    // Command registry (name -> command)
    std::unordered_map<std::string, std::unique_ptr<command>> commands_;

    // Alias -> primary name mapping
    std::unordered_map<std::string, std::string> aliases_;

    // Admin info (player_id -> info)
    std::unordered_map<player_id, admin_info> admins_;

    // Muted players (player_id -> unmute_time, 0 = permanent)
    std::unordered_map<player_id, int64_t> muted_players_;

    // Command log
    std::deque<command_log_entry> command_log_;

    // Statistics
    size_t total_commands_{0};

    // Callbacks
    std::vector<command_callback> command_callbacks_;
    std::vector<ban_callback> ban_callbacks_;
    std::vector<kick_callback> kick_callbacks_;
    std::vector<mute_callback> mute_callbacks_;
    std::vector<level_change_callback> level_change_callbacks_;
};

}  // namespace hb::admin
