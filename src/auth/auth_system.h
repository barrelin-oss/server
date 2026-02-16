#pragma once

// auth_system.h
// Authentication and account management subsystem

#include "core/subsystem.h"
#include "core/types.h"
#include "core/result.h"
#include "auth/account.h"
#include "auth/session_token.h"

#include "config/server_config.h"

#include <string>
#include <string_view>
#include <vector>
#include <optional>
#include <chrono>
#include <functional>
#include <mutex>
#include <unordered_map>

namespace hb::database
{
class database_system;
}

namespace hb::auth
{

// Auth system configuration
struct auth_config
{
    uint32_t max_characters_per_account = 4;
    std::chrono::seconds session_duration{3600};      // 1 hour
    std::chrono::seconds session_max_duration{86400}; // 24 hours
    bool allow_registration = true;
    uint32_t max_login_attempts = 5;
    std::chrono::seconds lockout_duration{300}; // 5 minutes
};

// Login attempt tracking
struct login_attempt_info
{
    uint32_t failed_attempts{0};
    std::chrono::system_clock::time_point last_attempt;
    std::chrono::system_clock::time_point lockout_until;
};

// Authentication system
class auth_system : public subsystem
{
public:
    auth_system();
    ~auth_system() override;

    // Subsystem interface
    [[nodiscard]] auto name() const -> std::string_view override { return "auth"; }
    void initialize() override;
    void shutdown() override;

    // Configuration
    void set_config(const auth_config& config);
    void set_database(database::database_system* db);
    void set_forum_config(const forum_auth_config& config);

    // Forum auth state
    [[nodiscard]] auto forum_auth_enabled() const -> bool { return forum_config_.enabled; }

    // Account management
    [[nodiscard]] auto create_account(std::string_view username,
                                      std::string_view password) -> result<account_id, auth_error>;

    [[nodiscard]] auto
    authenticate(std::string_view username,
                 std::string_view password,
                 std::optional<std::string_view> ip_address = std::nullopt) -> result<session_token, auth_error>;

    [[nodiscard]] auto change_password(account_id id,
                                       std::string_view old_password,
                                       std::string_view new_password) -> result<void, auth_error>;

    // Forum-based authentication (delegates to external PHP endpoints)
    // Returns session_token + forum_token (for client storage)
    struct forum_auth_result
    {
        session_token session;
        std::string forum_token; // token from PHP, empty on token-based login
    };

    [[nodiscard]] auto authenticate_forum(std::string_view username,
                                          std::string_view password,
                                          std::optional<std::string_view> ip_address = std::nullopt)
        -> result<forum_auth_result, auth_error>;

    [[nodiscard]] auto authenticate_forum_token(std::string_view token,
                                                std::optional<std::string_view> ip_address = std::nullopt)
        -> result<forum_auth_result, auth_error>;

    // Account queries
    [[nodiscard]] auto get_account(account_id id) -> result<account, auth_error>;
    [[nodiscard]] auto get_account_by_username(std::string_view username) -> result<account, auth_error>;

    // Session management
    [[nodiscard]] auto validate_session(std::string_view token) -> result<account_id, auth_error>;
    [[nodiscard]] auto refresh_session(std::string_view token) -> result<session_token, auth_error>;
    void invalidate_session(std::string_view token);
    void invalidate_all_sessions(account_id id);

    // Character management
    [[nodiscard]] auto get_characters(account_id id) -> result<std::vector<character_summary>, auth_error>;
    [[nodiscard]] auto create_character(account_id id,
                                        const character_create_info& info) -> result<player_id, auth_error>;
    [[nodiscard]] auto delete_character(account_id id, player_id char_id) -> result<void, auth_error>;
    [[nodiscard]] auto get_character(player_id char_id) -> result<character_summary, auth_error>;
    [[nodiscard]] auto load_character_full(player_id char_id,
                                           account_id owner) -> result<character_full_data, auth_error>;
    [[nodiscard]] auto save_character(const character_full_data& data) -> result<void, auth_error>;

    // Ban management
    void ban_account(account_id id,
                     std::string_view reason,
                     std::optional<std::chrono::system_clock::time_point> expires = std::nullopt);
    void unban_account(account_id id);
    [[nodiscard]] auto is_banned(account_id id) -> bool;

    // Admin level
    void set_admin_level(account_id id, admin_level level);
    [[nodiscard]] auto get_admin_level(account_id id) -> admin_level;

    // Maintenance mode
    void set_maintenance_mode(bool enabled, std::string_view message = "");
    [[nodiscard]] auto is_maintenance_mode() const -> bool { return maintenance_mode_; }
    [[nodiscard]] auto maintenance_message() const -> const std::string& { return maintenance_message_; }

    // Admin password reset (bypasses old password check)
    [[nodiscard]] auto admin_change_password(account_id id, std::string_view new_password) -> result<void, auth_error>;

    // Utility
    [[nodiscard]] auto username_exists(std::string_view username) -> bool;
    [[nodiscard]] auto character_name_exists(std::string_view name) -> bool;

private:
    // Login attempt tracking
    [[nodiscard]] auto check_login_attempts(std::string_view ip_address) -> bool;
    void record_login_attempt(std::string_view ip_address, bool success);

    // Database helpers
    [[nodiscard]] auto db_get_account_by_id(account_id id) -> result<account, auth_error>;
    [[nodiscard]] auto db_get_account_by_username(std::string_view username) -> result<account, auth_error>;
    [[nodiscard]] auto db_create_account(std::string_view username,
                                         std::string_view password_hash) -> result<account_id, auth_error>;
    [[nodiscard]] auto db_store_session(const session_token& session) -> result<void, auth_error>;
    [[nodiscard]] auto db_get_session(std::string_view token) -> result<session_token, auth_error>;
    void db_delete_session(std::string_view token);
    void db_delete_all_sessions(account_id id);
    void db_record_login(account_id id,
                         std::optional<std::string_view> ip_address,
                         bool success,
                         std::string_view failure_reason = "");

    // Forum auth helpers
    [[nodiscard]] auto db_get_or_create_account_by_forum_id(uint64_t forum_member_id,
                                                            std::string_view username) -> result<account, auth_error>;

    auth_config config_;
    forum_auth_config forum_config_;
    database::database_system* database_{nullptr};

    // Maintenance mode
    bool maintenance_mode_{false};
    std::string maintenance_message_;

    // In-memory session cache for performance
    std::unordered_map<std::string, session_token> session_cache_;
    mutable std::mutex session_mutex_;

    // Login attempt tracking (by IP)
    std::unordered_map<std::string, login_attempt_info> login_attempts_;
    mutable std::mutex attempts_mutex_;
};

} // namespace hb::auth
