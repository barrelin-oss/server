// auth_system.cpp
// Authentication and account management subsystem implementation

#include "auth/auth_system.h"
#include "auth/password_hash.h"
#include "database/database_system.h"
#include "player/stats.h"
#include "core/logger.h"

#include <ixwebsocket/IXHttpClient.h>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <cctype>

namespace hb::auth
{

auth_system::auth_system() = default;
auth_system::~auth_system() = default;

void auth_system::initialize()
{
    LOG_INFO(auth, "Initializing authentication system");

    if (!database_)
    {
        LOG_WARN(auth, "No database configured, auth system will operate in limited mode");
    }

    set_initialized(true);
    LOG_INFO(auth, "Authentication system initialized");
}

void auth_system::shutdown()
{
    LOG_INFO(auth, "Shutting down authentication system");

    {
        std::lock_guard lock{session_mutex_};
        session_cache_.clear();
    }

    {
        std::lock_guard lock{attempts_mutex_};
        login_attempts_.clear();
    }

    set_initialized(false);
    LOG_INFO(auth, "Authentication system shut down");
}

void auth_system::set_config(const auth_config& config)
{
    config_ = config;
}

void auth_system::set_database(database::database_system* db)
{
    database_ = db;
}

void auth_system::set_forum_config(const forum_auth_config& config)
{
    forum_config_ = config;
    if (forum_config_.enabled)
    {
        LOG_INFO(
            auth, "Forum auth enabled: login={}, validate={}", forum_config_.login_url, forum_config_.validate_url);
    }
}

auto auth_system::create_account(std::string_view username, std::string_view password) -> result<account_id, auth_error>
{
    if (!config_.allow_registration)
    {
        LOG_WARN(auth, "Account registration is disabled");
        return result<account_id, auth_error>::err(auth_error::internal_error);
    }

    // Validate username
    auto username_result = validate_username(username);
    if (!username_result.valid)
    {
        LOG_DEBUG(auth, "Invalid username format: {}", username_result.error_message);
        return result<account_id, auth_error>::err(auth_error::invalid_username_format);
    }

    // Validate password
    auto password_result = validate_password(password);
    if (!password_result.valid)
    {
        LOG_DEBUG(auth, "Invalid password format: {}", password_result.error_message);
        return result<account_id, auth_error>::err(auth_error::invalid_password_format);
    }

    // Check if username already exists
    if (username_exists(username))
    {
        LOG_DEBUG(auth, "Username already taken: {}", username);
        return result<account_id, auth_error>::err(auth_error::username_taken);
    }

    // Hash the password
    auto hash_result = hash_password(password);
    if (hash_result.is_err())
    {
        LOG_ERROR(auth, "Failed to hash password: {}", hash_result.error());
        return result<account_id, auth_error>::err(auth_error::internal_error);
    }

    // Create the account in database
    return db_create_account(username, hash_result.value());
}

auto auth_system::authenticate(std::string_view username,
                               std::string_view password,
                               std::optional<std::string_view> ip_address) -> result<session_token, auth_error>
{
    std::string ip_str = ip_address ? std::string(*ip_address) : "unknown";

    // Check for lockout
    if (ip_address && !check_login_attempts(*ip_address))
    {
        LOG_WARN(auth, "Login attempt from locked out IP: {}", ip_str);
        return result<session_token, auth_error>::err(auth_error::invalid_credentials);
    }

    // Get account
    auto account_result = db_get_account_by_username(username);
    if (account_result.is_err())
    {
        record_login_attempt(ip_str, false);
        LOG_DEBUG(auth, "Account not found: {}", username);
        return result<session_token, auth_error>::err(auth_error::invalid_credentials);
    }

    auto& acc = account_result.value();

    // Check if banned
    if (acc.is_banned)
    {
        if (acc.ban_expires.has_value() && std::chrono::system_clock::now() >= *acc.ban_expires)
        {
            // Ban expired, unban the account
            unban_account(acc.id);
        }
        else
        {
            LOG_INFO(auth, "Login attempt for banned account: {}", username);
            db_record_login(acc.id, ip_str, false, "account_banned");
            return result<session_token, auth_error>::err(auth_error::account_banned);
        }
    }

    // Verify password
    if (!verify_password(password, acc.password_hash))
    {
        record_login_attempt(ip_str, false);
        db_record_login(acc.id, ip_str, false, "invalid_password");
        LOG_DEBUG(auth, "Invalid password for account: {}", username);
        return result<session_token, auth_error>::err(auth_error::invalid_credentials);
    }

    // Create session token
    auto session_result = create_session_token(acc.id, config_.session_duration, ip_address, std::nullopt);

    if (session_result.is_err())
    {
        LOG_ERROR(auth, "Failed to create session token: {}", session_result.error());
        return result<session_token, auth_error>::err(auth_error::internal_error);
    }

    auto session = std::move(session_result).value();

    // Store session
    auto store_result = db_store_session(session);
    if (store_result.is_err())
    {
        LOG_ERROR(auth, "Failed to store session");
        return result<session_token, auth_error>::err(auth_error::database_error);
    }

    // Cache session
    {
        std::lock_guard lock{session_mutex_};
        session_cache_[session.token] = session;
    }

    // Record successful login
    record_login_attempt(ip_str, true);
    db_record_login(acc.id, ip_str, true, "");

    LOG_INFO(auth, "User authenticated: {} from {}", username, ip_str);

    return result<session_token, auth_error>::ok(std::move(session));
}

auto auth_system::authenticate_forum(std::string_view username,
                                     std::string_view password,
                                     std::optional<std::string_view> ip_address)
    -> result<forum_auth_result, auth_error>
{
    std::string ip_str = ip_address ? std::string(*ip_address) : "unknown";

    // Rate limiting still applies
    if (ip_address && !check_login_attempts(*ip_address))
    {
        LOG_WARN(auth, "Forum login attempt from locked out IP: {}", ip_str);
        return result<forum_auth_result, auth_error>::err(auth_error::invalid_credentials);
    }

    // Call PHP login endpoint
    ix::HttpClient http_client;
    nlohmann::json request_body;
    request_body["email"] = std::string(username);
    request_body["pass"] = std::string(password);
    request_body["key"] = forum_config_.api_key;

    auto args = http_client.createRequest(forum_config_.login_url, ix::HttpClient::kPost);
    args->connectTimeout = 10;
    args->transferTimeout = 15;
    auto response = http_client.post(forum_config_.login_url, request_body.dump(), args);

    if (response->statusCode != 200)
    {
        std::string body = response->body;
        record_login_attempt(ip_str, false);

        if (body == "badaccount")
        {
            LOG_DEBUG(auth, "Forum auth: account not found '{}'", username);
        }
        else if (body == "badpass")
        {
            LOG_DEBUG(auth, "Forum auth: wrong password for '{}'", username);
        }
        else
        {
            LOG_WARN(auth, "Forum auth failed for '{}': HTTP {} - {}", username, response->statusCode, body);
        }
        return result<forum_auth_result, auth_error>::err(auth_error::forum_auth_failed);
    }

    // Parse response
    nlohmann::json resp;
    try
    {
        resp = nlohmann::json::parse(response->body);
    }
    catch (const nlohmann::json::exception& e)
    {
        LOG_ERROR(auth, "Forum auth: failed to parse response: {}", e.what());
        return result<forum_auth_result, auth_error>::err(auth_error::internal_error);
    }

    auto forum_member_id = resp["forum_member_id"].get<uint64_t>();
    auto forum_token = resp["token"].get<std::string>();

    LOG_INFO(auth, "Forum auth successful for '{}' (forum_id: {})", username, forum_member_id);

    // Look up or create local account
    auto account_result = db_get_or_create_account_by_forum_id(forum_member_id, username);
    if (account_result.is_err())
    {
        return result<forum_auth_result, auth_error>::err(account_result.error());
    }

    auto& acc = account_result.value();

    // Check if banned locally
    if (acc.is_banned)
    {
        if (acc.ban_expires.has_value() && std::chrono::system_clock::now() >= *acc.ban_expires)
        {
            unban_account(acc.id);
        }
        else
        {
            LOG_INFO(auth, "Forum login for banned account: {} (forum_id: {})", username, forum_member_id);
            db_record_login(acc.id, ip_str, false, "account_banned");
            return result<forum_auth_result, auth_error>::err(auth_error::account_banned);
        }
    }

    // Create game session
    auto session_result = create_session_token(acc.id, config_.session_duration, ip_address, std::nullopt);

    if (session_result.is_err())
    {
        LOG_ERROR(auth, "Failed to create session token: {}", session_result.error());
        return result<forum_auth_result, auth_error>::err(auth_error::internal_error);
    }

    auto session = std::move(session_result).value();

    auto store_result = db_store_session(session);
    if (store_result.is_err())
    {
        return result<forum_auth_result, auth_error>::err(auth_error::database_error);
    }

    {
        std::lock_guard lock{session_mutex_};
        session_cache_[session.token] = session;
    }

    record_login_attempt(ip_str, true);
    db_record_login(acc.id, ip_str, true, "");

    LOG_INFO(auth, "Forum user '{}' authenticated from {} (account {})", username, ip_str, acc.id.value);

    return result<forum_auth_result, auth_error>::ok(
        forum_auth_result{.session = std::move(session), .forum_token = std::move(forum_token)});
}

auto auth_system::authenticate_forum_token(std::string_view token, std::optional<std::string_view> ip_address)
    -> result<forum_auth_result, auth_error>
{
    std::string ip_str = ip_address ? std::string(*ip_address) : "unknown";

    if (ip_address && !check_login_attempts(*ip_address))
    {
        LOG_WARN(auth, "Forum token login from locked out IP: {}", ip_str);
        return result<forum_auth_result, auth_error>::err(auth_error::invalid_credentials);
    }

    // Call PHP validate endpoint
    ix::HttpClient http_client;
    nlohmann::json request_body;
    request_body["token"] = std::string(token);
    request_body["key"] = forum_config_.api_key;

    auto args = http_client.createRequest(forum_config_.validate_url, ix::HttpClient::kPost);
    args->connectTimeout = 10;
    args->transferTimeout = 15;
    auto response = http_client.post(forum_config_.validate_url, request_body.dump(), args);

    if (response->statusCode != 200)
    {
        std::string body = response->body;
        record_login_attempt(ip_str, false);

        if (body == "invalid_token")
        {
            LOG_DEBUG(auth, "Forum token auth: invalid token");
        }
        else if (body == "expired_token")
        {
            LOG_DEBUG(auth, "Forum token auth: expired token");
        }
        else if (body == "password_changed")
        {
            LOG_DEBUG(auth, "Forum token auth: password was changed, token revoked");
        }
        else
        {
            LOG_WARN(auth, "Forum token auth failed: HTTP {} - {}", response->statusCode, body);
        }
        return result<forum_auth_result, auth_error>::err(auth_error::forum_auth_failed);
    }

    nlohmann::json resp;
    try
    {
        resp = nlohmann::json::parse(response->body);
    }
    catch (const nlohmann::json::exception& e)
    {
        LOG_ERROR(auth, "Forum token auth: failed to parse response: {}", e.what());
        return result<forum_auth_result, auth_error>::err(auth_error::internal_error);
    }

    auto forum_member_id = resp["forum_member_id"].get<uint64_t>();

    // Look up local account (must already exist for token auth)
    auto account_result = db_get_or_create_account_by_forum_id(forum_member_id, "");
    if (account_result.is_err())
    {
        return result<forum_auth_result, auth_error>::err(account_result.error());
    }

    auto& acc = account_result.value();

    if (acc.is_banned)
    {
        if (acc.ban_expires.has_value() && std::chrono::system_clock::now() >= *acc.ban_expires)
        {
            unban_account(acc.id);
        }
        else
        {
            db_record_login(acc.id, ip_str, false, "account_banned");
            return result<forum_auth_result, auth_error>::err(auth_error::account_banned);
        }
    }

    auto session_result = create_session_token(acc.id, config_.session_duration, ip_address, std::nullopt);

    if (session_result.is_err())
    {
        return result<forum_auth_result, auth_error>::err(auth_error::internal_error);
    }

    auto session = std::move(session_result).value();

    auto store_result = db_store_session(session);
    if (store_result.is_err())
    {
        return result<forum_auth_result, auth_error>::err(auth_error::database_error);
    }

    {
        std::lock_guard lock{session_mutex_};
        session_cache_[session.token] = session;
    }

    record_login_attempt(ip_str, true);
    db_record_login(acc.id, ip_str, true, "");

    LOG_INFO(auth, "Forum token auth successful (forum_id: {}, account {})", forum_member_id, acc.id.value);

    return result<forum_auth_result, auth_error>::ok(
        forum_auth_result{.session = std::move(session), .forum_token = ""});
}

auto auth_system::change_password(account_id id,
                                  std::string_view old_password,
                                  std::string_view new_password) -> result<void, auth_error>
{
    // Get account
    auto account_result = db_get_account_by_id(id);
    if (account_result.is_err())
    {
        return result<void, auth_error>::err(account_result.error());
    }

    auto& acc = account_result.value();

    // Verify old password
    if (!verify_password(old_password, acc.password_hash))
    {
        return result<void, auth_error>::err(auth_error::invalid_credentials);
    }

    // Validate new password
    auto password_result = validate_password(new_password);
    if (!password_result.valid)
    {
        return result<void, auth_error>::err(auth_error::invalid_password_format);
    }

    // Hash new password
    auto hash_result = hash_password(new_password);
    if (hash_result.is_err())
    {
        return result<void, auth_error>::err(auth_error::internal_error);
    }

    // Update in database
    if (!database_)
    {
        return result<void, auth_error>::err(auth_error::database_error);
    }

    auto db_result = database_->execute_params(
        "UPDATE accounts SET password_hash = $1 WHERE id = $2", hash_result.value(), static_cast<int>(id.value));

    if (db_result.is_err())
    {
        LOG_ERROR(auth, "Failed to update password: {}", db_result.error());
        return result<void, auth_error>::err(auth_error::database_error);
    }

    // Invalidate all sessions for this account
    invalidate_all_sessions(id);

    LOG_INFO(auth, "Password changed for account {}", id.value);

    return result<void, auth_error>::ok();
}

void auth_system::set_maintenance_mode(bool enabled, std::string_view message)
{
    maintenance_mode_ = enabled;
    maintenance_message_ = std::string(message);
    LOG_INFO(auth, "Maintenance mode {}: {}", enabled ? "enabled" : "disabled", message);
}

auto auth_system::admin_change_password(account_id id, std::string_view new_password) -> result<void, auth_error>
{
    auto password_result = validate_password(new_password);
    if (!password_result.valid)
    {
        return result<void, auth_error>::err(auth_error::invalid_password_format);
    }

    auto hash_result = hash_password(new_password);
    if (hash_result.is_err())
    {
        return result<void, auth_error>::err(auth_error::internal_error);
    }

    if (!database_)
    {
        return result<void, auth_error>::err(auth_error::database_error);
    }

    auto db_result = database_->execute_params(
        "UPDATE accounts SET password_hash = $1 WHERE id = $2", hash_result.value(), static_cast<int>(id.value));

    if (db_result.is_err())
    {
        LOG_ERROR(auth, "Failed to admin-reset password: {}", db_result.error());
        return result<void, auth_error>::err(auth_error::database_error);
    }

    invalidate_all_sessions(id);
    LOG_INFO(auth, "Admin reset password for account {}", id.value);
    return result<void, auth_error>::ok();
}

auto auth_system::get_account(account_id id) -> result<account, auth_error>
{
    return db_get_account_by_id(id);
}

auto auth_system::get_account_by_username(std::string_view username) -> result<account, auth_error>
{
    return db_get_account_by_username(username);
}

auto auth_system::validate_session(std::string_view token) -> result<account_id, auth_error>
{
    // Check cache first
    {
        std::lock_guard lock{session_mutex_};
        auto it = session_cache_.find(std::string(token));
        if (it != session_cache_.end())
        {
            if (!it->second.is_expired())
            {
                return result<account_id, auth_error>::ok(it->second.account);
            }
            // Expired, remove from cache
            session_cache_.erase(it);
        }
    }

    // Check database
    auto session_result = db_get_session(token);
    if (session_result.is_err())
    {
        return result<account_id, auth_error>::err(auth_error::session_not_found);
    }

    auto& session = session_result.value();

    if (session.is_expired())
    {
        db_delete_session(token);
        return result<account_id, auth_error>::err(auth_error::session_expired);
    }

    // Add to cache
    {
        std::lock_guard lock{session_mutex_};
        session_cache_[session.token] = session;
    }

    return result<account_id, auth_error>::ok(session.account);
}

auto auth_system::refresh_session(std::string_view token) -> result<session_token, auth_error>
{
    auto account_result = validate_session(token);
    if (account_result.is_err())
    {
        return result<session_token, auth_error>::err(account_result.error());
    }

    // Delete old session
    invalidate_session(token);

    // Create new session
    auto session_result =
        create_session_token(account_result.value(), config_.session_duration, std::nullopt, std::nullopt);

    if (session_result.is_err())
    {
        return result<session_token, auth_error>::err(auth_error::internal_error);
    }

    auto session = std::move(session_result).value();

    // Store new session
    auto store_result = db_store_session(session);
    if (store_result.is_err())
    {
        return result<session_token, auth_error>::err(auth_error::database_error);
    }

    // Cache session
    {
        std::lock_guard lock{session_mutex_};
        session_cache_[session.token] = session;
    }

    return result<session_token, auth_error>::ok(std::move(session));
}

void auth_system::invalidate_session(std::string_view token)
{
    {
        std::lock_guard lock{session_mutex_};
        session_cache_.erase(std::string(token));
    }
    db_delete_session(token);
}

void auth_system::invalidate_all_sessions(account_id id)
{
    // Remove from cache
    {
        std::lock_guard lock{session_mutex_};
        std::erase_if(session_cache_, [id](const auto& pair) { return pair.second.account == id; });
    }
    db_delete_all_sessions(id);
}

auto auth_system::get_characters(account_id id) -> result<std::vector<character_summary>, auth_error>
{
    if (!database_)
    {
        return result<std::vector<character_summary>, auth_error>::err(auth_error::database_error);
    }

    auto db_result = database_->execute_params(
        R"(SELECT id, name, level, class_type, nation, gender, map_name, experience,
                  hair_style, hair_color, skin_color, last_played
           FROM characters WHERE account_id = $1 ORDER BY last_played DESC NULLS LAST)",
        static_cast<int>(id.value));

    if (db_result.is_err())
    {
        LOG_ERROR(auth, "Failed to get characters: {}", db_result.error());
        return result<std::vector<character_summary>, auth_error>::err(auth_error::database_error);
    }

    std::vector<character_summary> characters;
    for (const auto& row : db_result.value())
    {
        character_summary summary{.id = player_id{static_cast<uint32_t>(row["id"].as<int>())},
                                  .name = row["name"].as<std::string>(),
                                  .level = static_cast<int16_t>(row["level"].as<int>()),
                                  .class_type = static_cast<int16_t>(row["class_type"].as<int>()),
                                  .nation = static_cast<int16_t>(row["nation"].as<int>()),
                                  .gender = static_cast<int16_t>(row["gender"].as<int>()),
                                  .map_name = row["map_name"].as<std::string>(),
                                  .experience = row["experience"].as<int64_t>(),
                                  .hair_style = static_cast<int16_t>(row["hair_style"].as<int>()),
                                  .hair_color = static_cast<int16_t>(row["hair_color"].as<int>()),
                                  .skin_color = static_cast<int16_t>(row["skin_color"].as<int>())};

        if (!row["last_played"].is_null())
        {
            // Parse timestamp - this is a simplification
            // In production, use proper timestamp parsing
        }

        characters.push_back(std::move(summary));
    }

    return result<std::vector<character_summary>, auth_error>::ok(std::move(characters));
}

auto auth_system::create_character(account_id id, const character_create_info& info) -> result<player_id, auth_error>
{
    if (!database_)
    {
        return result<player_id, auth_error>::err(auth_error::database_error);
    }

    // Validate character name
    if (info.name.size() < 3 || info.name.size() > 32)
    {
        return result<player_id, auth_error>::err(auth_error::invalid_character_name);
    }

    // Check character name format (starts with letter, alphanumeric + underscore)
    if (!std::isalpha(static_cast<unsigned char>(info.name[0])))
    {
        return result<player_id, auth_error>::err(auth_error::invalid_character_name);
    }

    for (char c : info.name)
    {
        if (!std::isalnum(static_cast<unsigned char>(c)) && c != '_')
        {
            return result<player_id, auth_error>::err(auth_error::invalid_character_name);
        }
    }

    // Check if name already exists
    if (character_name_exists(info.name))
    {
        return result<player_id, auth_error>::err(auth_error::character_name_taken);
    }

    // Check character limit
    auto chars_result = get_characters(id);
    if (chars_result.is_err())
    {
        return result<player_id, auth_error>::err(chars_result.error());
    }

    if (chars_result.value().size() >= config_.max_characters_per_account)
    {
        return result<player_id, auth_error>::err(auth_error::max_characters_reached);
    }

    // Default stats based on class
    int16_t str = info.strength.value_or(10);
    int16_t dex = info.dexterity.value_or(10);
    int16_t vit = info.vitality.value_or(10);
    int16_t intel = info.intelligence.value_or(10);
    int16_t mag = info.magic.value_or(10);
    int16_t cha = info.charisma.value_or(10);

    // Compute initial HP/MP/SP from base stats (same formulas as base_stats)
    player::base_stats initial_stats{.strength = str,
                                     .dexterity = dex,
                                     .vitality = vit,
                                     .intelligence = intel,
                                     .magic = mag,
                                     .charisma = cha,
                                     .level_bonus = 0};
    int32_t max_hp = initial_stats.max_hp();
    int32_t max_mp = initial_stats.max_mp();
    int32_t max_sp = initial_stats.max_sp();

    // Insert character
    auto db_result = database_->execute_params(
        R"(INSERT INTO characters
           (account_id, name, class_type, nation, gender, hair_style, hair_color, skin_color,
            underwear_color, strength, dexterity, vitality, intelligence, magic, charisma,
            hp, hp_max, mp, mp_max, sp, sp_max)
           VALUES ($1, $2, $3, $4, $5, $6, $7, $8, $9, $10, $11, $12, $13, $14, $15,
                   $16, $17, $18, $19, $20, $21)
           RETURNING id)",
        static_cast<int>(id.value),
        info.name,
        static_cast<int>(info.class_type),
        static_cast<int>(info.nation),
        static_cast<int>(info.gender),
        static_cast<int>(info.hair_style),
        static_cast<int>(info.hair_color),
        static_cast<int>(info.skin_color),
        static_cast<int>(info.underwear_color),
        static_cast<int>(str),
        static_cast<int>(dex),
        static_cast<int>(vit),
        static_cast<int>(intel),
        static_cast<int>(mag),
        static_cast<int>(cha),
        max_hp,
        max_hp, // hp = hp_max (start full)
        max_mp,
        max_mp, // mp = mp_max
        max_sp,
        max_sp // sp = sp_max
    );

    if (db_result.is_err())
    {
        LOG_ERROR(auth, "Failed to create character: {}", db_result.error());
        return result<player_id, auth_error>::err(auth_error::database_error);
    }

    auto& query_result = db_result.value();
    if (query_result.empty())
    {
        return result<player_id, auth_error>::err(auth_error::database_error);
    }

    auto char_id = player_id{static_cast<uint32_t>(query_result[0][0].as<int>())};

    LOG_INFO(auth, "Created character '{}' for account {}", info.name, id.value);

    return result<player_id, auth_error>::ok(char_id);
}

auto auth_system::delete_character(account_id id, player_id char_id) -> result<void, auth_error>
{
    if (!database_)
    {
        return result<void, auth_error>::err(auth_error::database_error);
    }

    // Verify character belongs to account
    auto db_result =
        database_->execute_params("SELECT account_id FROM characters WHERE id = $1", static_cast<int>(char_id.value));

    if (db_result.is_err() || db_result.value().empty())
    {
        return result<void, auth_error>::err(auth_error::character_not_found);
    }

    auto owner_id = account_id{static_cast<uint32_t>(db_result.value()[0][0].as<int>())};
    if (owner_id != id)
    {
        return result<void, auth_error>::err(auth_error::character_not_owned);
    }

    // Delete character
    auto delete_result =
        database_->execute_params("DELETE FROM characters WHERE id = $1", static_cast<int>(char_id.value));

    if (delete_result.is_err())
    {
        LOG_ERROR(auth, "Failed to delete character: {}", delete_result.error());
        return result<void, auth_error>::err(auth_error::database_error);
    }

    LOG_INFO(auth, "Deleted character {} for account {}", char_id.value, id.value);

    return result<void, auth_error>::ok();
}

auto auth_system::get_character(player_id char_id) -> result<character_summary, auth_error>
{
    if (!database_)
    {
        return result<character_summary, auth_error>::err(auth_error::database_error);
    }

    auto db_result = database_->execute_params(
        R"(SELECT id, name, level, class_type, nation, gender, map_name, experience,
                  hair_style, hair_color, skin_color, last_played
           FROM characters WHERE id = $1)",
        static_cast<int>(char_id.value));

    if (db_result.is_err())
    {
        return result<character_summary, auth_error>::err(auth_error::database_error);
    }

    if (db_result.value().empty())
    {
        return result<character_summary, auth_error>::err(auth_error::character_not_found);
    }

    const auto& row = db_result.value()[0];
    character_summary summary{.id = player_id{static_cast<uint32_t>(row["id"].as<int>())},
                              .name = row["name"].as<std::string>(),
                              .level = static_cast<int16_t>(row["level"].as<int>()),
                              .class_type = static_cast<int16_t>(row["class_type"].as<int>()),
                              .nation = static_cast<int16_t>(row["nation"].as<int>()),
                              .gender = static_cast<int16_t>(row["gender"].as<int>()),
                              .map_name = row["map_name"].as<std::string>(),
                              .experience = row["experience"].as<int64_t>(),
                              .hair_style = static_cast<int16_t>(row["hair_style"].as<int>()),
                              .hair_color = static_cast<int16_t>(row["hair_color"].as<int>()),
                              .skin_color = static_cast<int16_t>(row["skin_color"].as<int>())};

    return result<character_summary, auth_error>::ok(std::move(summary));
}

auto auth_system::load_character_full(player_id char_id, account_id owner) -> result<character_full_data, auth_error>
{
    if (!database_)
    {
        return result<character_full_data, auth_error>::err(auth_error::database_error);
    }

    auto db_result = database_->execute_params(
        R"(SELECT id, account_id, name, level, class_type, nation, gender,
                  map_name, pos_x, pos_y, experience, hp, mp, sp, gold,
                  strength, dexterity, vitality, intelligence, magic, charisma,
                  hair_style, hair_color, skin_color, underwear_color,
                  pk_count, COALESCE(pk_points, 0) as pk_points,
                  hunger_level,
                  COALESCE(enemy_kill_count, 0) as enemy_kill_count,
                  COALESCE(contribution, 0) as contribution,
                  COALESCE(stat_points_available, 0) as stat_points_available,
                  COALESCE(luck, 0) as luck,
                  COALESCE(reward_gold, 0) as reward_gold,
                  COALESCE(hp_max, 100) as hp_max,
                  COALESCE(mp_max, 50) as mp_max,
                  COALESCE(sp_max, 50) as sp_max,
                  skills_data, inventory_data, equipment_data, bank_data,
                  magic_data, quest_data
           FROM characters WHERE id = $1)",
        static_cast<int>(char_id.value));

    if (db_result.is_err())
    {
        LOG_ERROR(auth, "Failed to load character {}: {}", char_id.value, db_result.error());
        return result<character_full_data, auth_error>::err(auth_error::database_error);
    }

    if (db_result.value().empty())
    {
        return result<character_full_data, auth_error>::err(auth_error::character_not_found);
    }

    const auto& row = db_result.value()[0];

    // Verify ownership
    auto db_owner_id = account_id{static_cast<uint32_t>(row["account_id"].as<int>())};
    if (db_owner_id != owner)
    {
        LOG_WARN(auth,
                 "Character {} ownership mismatch: expected {}, got {}",
                 char_id.value,
                 owner.value,
                 db_owner_id.value);
        return result<character_full_data, auth_error>::err(auth_error::character_not_owned);
    }

    character_full_data data{
        .id = player_id{static_cast<uint32_t>(row["id"].as<int>())},
        .account = db_owner_id,
        .name = row["name"].as<std::string>(),
        .level = static_cast<int16_t>(row["level"].as<int>()),
        .class_type = static_cast<int16_t>(row["class_type"].as<int>()),
        .nation = static_cast<int16_t>(row["nation"].as<int>()),
        .gender = static_cast<int16_t>(row["gender"].as<int>()),
        .map_name = row["map_name"].is_null() ? "default" : row["map_name"].as<std::string>(),
        .pos_x = row["pos_x"].is_null() ? static_cast<int16_t>(0) : static_cast<int16_t>(row["pos_x"].as<int>()),
        .pos_y = row["pos_y"].is_null() ? static_cast<int16_t>(0) : static_cast<int16_t>(row["pos_y"].as<int>()),
        .experience = row["experience"].is_null() ? 0LL : row["experience"].as<int64_t>(),
        .hp = row["hp"].is_null() ? 100 : row["hp"].as<int>(),
        .max_hp = row["hp_max"].as<int>(),
        .mp = row["mp"].is_null() ? 50 : row["mp"].as<int>(),
        .max_mp = row["mp_max"].as<int>(),
        .sp = row["sp"].is_null() ? 50 : row["sp"].as<int>(),
        .max_sp = row["sp_max"].as<int>(),
        .gold = row["gold"].is_null() ? 0 : row["gold"].as<int>(),
        .strength = static_cast<int16_t>(row["strength"].as<int>()),
        .dexterity = static_cast<int16_t>(row["dexterity"].as<int>()),
        .vitality = static_cast<int16_t>(row["vitality"].as<int>()),
        .intelligence = static_cast<int16_t>(row["intelligence"].as<int>()),
        .magic = static_cast<int16_t>(row["magic"].as<int>()),
        .charisma = static_cast<int16_t>(row["charisma"].as<int>()),
        .hair_style = static_cast<int16_t>(row["hair_style"].as<int>()),
        .hair_color = static_cast<int16_t>(row["hair_color"].as<int>()),
        .skin_color = static_cast<int16_t>(row["skin_color"].as<int>()),
        .underwear_color = row["underwear_color"].is_null() ? static_cast<int16_t>(0)
                                                            : static_cast<int16_t>(row["underwear_color"].as<int>()),
        .pk_count = row["pk_count"].is_null() ? 0 : row["pk_count"].as<int>(),
        .pk_points = row["pk_points"].as<int>(),
        .hunger_level = row["hunger_level"].is_null() ? 100 : row["hunger_level"].as<int>(),
        .enemy_kill_count = row["enemy_kill_count"].as<int>(),
        .contribution = row["contribution"].as<int>(),
        .stat_points_available = static_cast<int16_t>(row["stat_points_available"].as<int>()),
        .luck = static_cast<int16_t>(row["luck"].as<int>()),
        .reward_gold = row["reward_gold"].as<int>(),
        .skills_data = row["skills_data"].is_null() ? "" : row["skills_data"].as<std::string>(),
        .inventory_data = row["inventory_data"].is_null() ? "" : row["inventory_data"].as<std::string>(),
        .equipment_data = row["equipment_data"].is_null() ? "" : row["equipment_data"].as<std::string>(),
        .bank_data = row["bank_data"].is_null() ? "" : row["bank_data"].as<std::string>(),
        .magic_data = row["magic_data"].is_null() ? "" : row["magic_data"].as<std::string>(),
        .quest_data = row["quest_data"].is_null() ? "" : row["quest_data"].as<std::string>()};

    LOG_DEBUG(auth, "Loaded full character data for '{}' (id: {})", data.name, data.id.value);

    return result<character_full_data, auth_error>::ok(std::move(data));
}

auto auth_system::save_character(const character_full_data& data) -> result<void, auth_error>
{
    if (!database_)
    {
        return result<void, auth_error>::err(auth_error::database_error);
    }

    // Ensure JSONB fields have valid JSON (PostgreSQL rejects empty strings for JSONB)
    auto skills_json = data.skills_data.empty() ? "[]" : data.skills_data;
    auto inventory_json = data.inventory_data.empty() ? "[]" : data.inventory_data;
    auto equipment_json = data.equipment_data.empty() ? "[]" : data.equipment_data;
    auto bank_json = data.bank_data.empty() ? "[]" : data.bank_data;
    auto magic_json = data.magic_data.empty() ? "[]" : data.magic_data;
    auto quest_json = data.quest_data.empty() ? "[]" : data.quest_data;

    auto db_result = database_->execute_params(
        R"(UPDATE characters SET
               map_name = $1,
               pos_x = $2,
               pos_y = $3,
               experience = $4,
               hp = $5,
               mp = $6,
               sp = $7,
               gold = $8,
               level = $9,
               strength = $10,
               dexterity = $11,
               vitality = $12,
               intelligence = $13,
               magic = $14,
               charisma = $15,
               pk_count = $16,
               pk_points = $17,
               hunger_level = $18,
               hp_max = $19,
               mp_max = $20,
               sp_max = $21,
               skills_data = $22,
               inventory_data = $23,
               equipment_data = $24,
               bank_data = $25,
               magic_data = $26,
               quest_data = $27,
               hair_style = $28,
               hair_color = $29,
               skin_color = $30,
               underwear_color = $31,
               enemy_kill_count = $32,
               contribution = $33,
               stat_points_available = $34,
               luck = $35,
               reward_gold = $36,
               last_played = NOW()
           WHERE id = $37)",
        data.map_name,
        static_cast<int>(data.pos_x),
        static_cast<int>(data.pos_y),
        data.experience,
        data.hp,
        data.mp,
        data.sp,
        data.gold,
        static_cast<int>(data.level),
        static_cast<int>(data.strength),
        static_cast<int>(data.dexterity),
        static_cast<int>(data.vitality),
        static_cast<int>(data.intelligence),
        static_cast<int>(data.magic),
        static_cast<int>(data.charisma),
        data.pk_count,
        data.pk_points,
        data.hunger_level,
        data.max_hp,
        data.max_mp,
        data.max_sp,
        skills_json,
        inventory_json,
        equipment_json,
        bank_json,
        magic_json,
        quest_json,
        static_cast<int>(data.hair_style),
        static_cast<int>(data.hair_color),
        static_cast<int>(data.skin_color),
        static_cast<int>(data.underwear_color),
        data.enemy_kill_count,
        data.contribution,
        static_cast<int>(data.stat_points_available),
        static_cast<int>(data.luck),
        data.reward_gold,
        static_cast<int>(data.id.value));

    if (db_result.is_err())
    {
        LOG_ERROR(auth, "Failed to save character {}: {}", data.id.value, db_result.error());
        return result<void, auth_error>::err(auth_error::database_error);
    }

    LOG_DEBUG(auth, "Saved character '{}' (id: {})", data.name, data.id.value);

    return result<void, auth_error>::ok();
}

void auth_system::ban_account(account_id id,
                              std::string_view reason,
                              std::optional<std::chrono::system_clock::time_point> expires)
{
    if (!database_)
    {
        return;
    }

    if (expires.has_value())
    {
        // For simplicity, we're not handling the timestamp properly here
        // In production, use proper timestamp formatting
        (void)database_->execute_params("UPDATE accounts SET is_banned = true, ban_reason = $1 WHERE id = $2",
                                        std::string(reason),
                                        static_cast<int>(id.value));
    }
    else
    {
        (void)database_->execute_params(
            "UPDATE accounts SET is_banned = true, ban_reason = $1, ban_expires = NULL WHERE id = $2",
            std::string(reason),
            static_cast<int>(id.value));
    }

    invalidate_all_sessions(id);

    LOG_INFO(auth, "Account {} banned: {}", id.value, reason);
}

void auth_system::unban_account(account_id id)
{
    if (!database_)
    {
        return;
    }

    (void)database_->execute_params(
        "UPDATE accounts SET is_banned = false, ban_reason = NULL, ban_expires = NULL WHERE id = $1",
        static_cast<int>(id.value));

    LOG_INFO(auth, "Account {} unbanned", id.value);
}

auto auth_system::is_banned(account_id id) -> bool
{
    auto account_result = db_get_account_by_id(id);
    if (account_result.is_err())
    {
        return false;
    }
    return account_result.value().is_banned;
}

void auth_system::set_admin_level(account_id id, admin_level level)
{
    if (!database_)
    {
        return;
    }

    (void)database_->execute_params(
        "UPDATE accounts SET admin_level = $1 WHERE id = $2", static_cast<int>(level), static_cast<int>(id.value));

    LOG_INFO(auth, "Account {} admin level set to {}", id.value, static_cast<int>(level));
}

auto auth_system::get_admin_level(account_id id) -> admin_level
{
    auto account_result = db_get_account_by_id(id);
    if (account_result.is_err())
    {
        return admin_level::player;
    }
    return account_result.value().admin;
}

auto auth_system::username_exists(std::string_view username) -> bool
{
    if (!database_)
    {
        return false;
    }

    // Lowercase for case-insensitive comparison
    std::string lower_username;
    lower_username.reserve(username.size());
    for (char c : username)
    {
        lower_username += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }

    auto result = database_->execute_params("SELECT 1 FROM accounts WHERE username = $1 LIMIT 1", lower_username);

    return result.is_ok() && !result.value().empty();
}

auto auth_system::character_name_exists(std::string_view name) -> bool
{
    if (!database_)
    {
        return false;
    }

    auto result =
        database_->execute_params("SELECT 1 FROM characters WHERE LOWER(name) = LOWER($1) LIMIT 1", std::string(name));

    return result.is_ok() && !result.value().empty();
}

// Private methods

auto auth_system::check_login_attempts(std::string_view ip_address) -> bool
{
    std::lock_guard lock{attempts_mutex_};

    auto it = login_attempts_.find(std::string(ip_address));
    if (it == login_attempts_.end())
    {
        return true;
    }

    auto& info = it->second;
    auto now = std::chrono::system_clock::now();

    // Check if still locked out
    if (now < info.lockout_until)
    {
        return false;
    }

    // Reset if lockout expired
    if (info.lockout_until != std::chrono::system_clock::time_point{} && now >= info.lockout_until)
    {
        info.failed_attempts = 0;
        info.lockout_until = {};
    }

    return true;
}

void auth_system::record_login_attempt(std::string_view ip_address, bool success)
{
    std::lock_guard lock{attempts_mutex_};

    auto& info = login_attempts_[std::string(ip_address)];
    info.last_attempt = std::chrono::system_clock::now();

    if (success)
    {
        info.failed_attempts = 0;
        info.lockout_until = {};
    }
    else
    {
        ++info.failed_attempts;
        if (info.failed_attempts >= config_.max_login_attempts)
        {
            info.lockout_until = std::chrono::system_clock::now() + config_.lockout_duration;
            LOG_WARN(auth, "IP {} locked out after {} failed attempts", ip_address, info.failed_attempts);
        }
    }
}

auto auth_system::db_get_account_by_id(account_id id) -> result<account, auth_error>
{
    if (!database_)
    {
        return result<account, auth_error>::err(auth_error::database_error);
    }

    auto db_result = database_->execute_params(
        R"(SELECT id, username, password_hash, admin_level, is_banned, ban_reason, created_at, last_login
           FROM accounts WHERE id = $1)",
        static_cast<int>(id.value));

    if (db_result.is_err())
    {
        return result<account, auth_error>::err(auth_error::database_error);
    }

    if (db_result.value().empty())
    {
        return result<account, auth_error>::err(auth_error::invalid_credentials);
    }

    const auto& row = db_result.value()[0];
    account acc{.id = account_id{static_cast<uint32_t>(row["id"].as<int>())},
                .username = row["username"].as<std::string>(),
                .password_hash = row["password_hash"].as<std::string>(),
                .admin = static_cast<admin_level>(row["admin_level"].as<int>()),
                .is_banned = row["is_banned"].as<bool>()};

    if (!row["ban_reason"].is_null())
    {
        acc.ban_reason = row["ban_reason"].as<std::string>();
    }

    return result<account, auth_error>::ok(std::move(acc));
}

auto auth_system::db_get_account_by_username(std::string_view username) -> result<account, auth_error>
{
    if (!database_)
    {
        LOG_ERROR(auth, "db_get_account_by_username: database_ is null!");
        return result<account, auth_error>::err(auth_error::database_error);
    }

    // Lowercase for case-insensitive comparison
    std::string lower_username;
    lower_username.reserve(username.size());
    for (char c : username)
    {
        lower_username += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }

    LOG_DEBUG(auth, "db_get_account_by_username: looking up '{}'", lower_username);

    auto db_result = database_->execute_params(
        R"(SELECT id, username, password_hash, admin_level, is_banned, ban_reason, created_at, last_login
           FROM accounts WHERE username = $1)",
        lower_username);

    if (db_result.is_err())
    {
        LOG_ERROR(auth, "db_get_account_by_username: query failed: {}", db_result.error());
        return result<account, auth_error>::err(auth_error::database_error);
    }

    if (db_result.value().empty())
    {
        LOG_DEBUG(auth, "db_get_account_by_username: no rows returned for '{}'", lower_username);
        return result<account, auth_error>::err(auth_error::invalid_credentials);
    }

    const auto& row = db_result.value()[0];
    account acc{.id = account_id{static_cast<uint32_t>(row["id"].as<int>())},
                .username = row["username"].as<std::string>(),
                .password_hash = row["password_hash"].as<std::string>(),
                .admin = static_cast<admin_level>(row["admin_level"].as<int>()),
                .is_banned = row["is_banned"].as<bool>()};

    if (!row["ban_reason"].is_null())
    {
        acc.ban_reason = row["ban_reason"].as<std::string>();
    }

    return result<account, auth_error>::ok(std::move(acc));
}

auto auth_system::db_create_account(std::string_view username,
                                    std::string_view password_hash) -> result<account_id, auth_error>
{
    if (!database_)
    {
        return result<account_id, auth_error>::err(auth_error::database_error);
    }

    // Lowercase username
    std::string lower_username;
    lower_username.reserve(username.size());
    for (char c : username)
    {
        lower_username += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }

    auto db_result =
        database_->execute_params("INSERT INTO accounts (username, password_hash) VALUES ($1, $2) RETURNING id",
                                  lower_username,
                                  std::string(password_hash));

    if (db_result.is_err())
    {
        LOG_ERROR(auth, "Failed to create account: {}", db_result.error());
        return result<account_id, auth_error>::err(auth_error::database_error);
    }

    if (db_result.value().empty())
    {
        return result<account_id, auth_error>::err(auth_error::database_error);
    }

    auto id = account_id{static_cast<uint32_t>(db_result.value()[0][0].as<int>())};

    LOG_INFO(auth, "Created account '{}' with id {}", lower_username, id.value);

    return result<account_id, auth_error>::ok(id);
}

auto auth_system::db_store_session(const session_token& session) -> result<void, auth_error>
{
    if (!database_)
    {
        return result<void, auth_error>::err(auth_error::database_error);
    }

    auto db_result = database_->execute_params(
        R"(INSERT INTO sessions (account_id, token, expires_at, ip_address)
           VALUES ($1, $2, NOW() + INTERVAL '1 hour', $3))",
        static_cast<int>(session.account.value),
        session.token,
        session.ip_address.value_or(""));

    if (db_result.is_err())
    {
        return result<void, auth_error>::err(auth_error::database_error);
    }

    return result<void, auth_error>::ok();
}

auto auth_system::db_get_session(std::string_view token) -> result<session_token, auth_error>
{
    if (!database_)
    {
        return result<session_token, auth_error>::err(auth_error::database_error);
    }

    auto db_result = database_->execute_params(
        R"(SELECT account_id, token, created_at, expires_at, ip_address
           FROM sessions WHERE token = $1 AND expires_at > NOW())",
        std::string(token));

    if (db_result.is_err())
    {
        return result<session_token, auth_error>::err(auth_error::database_error);
    }

    if (db_result.value().empty())
    {
        return result<session_token, auth_error>::err(auth_error::session_not_found);
    }

    const auto& row = db_result.value()[0];

    session_token session{
        .token = row["token"].as<std::string>(),
        .account = account_id{static_cast<uint32_t>(row["account_id"].as<int>())},
        .created_at = std::chrono::system_clock::now(),                        // Simplified
        .expires_at = std::chrono::system_clock::now() + std::chrono::hours{1} // Simplified
    };

    if (!row["ip_address"].is_null())
    {
        session.ip_address = row["ip_address"].as<std::string>();
    }

    return result<session_token, auth_error>::ok(std::move(session));
}

void auth_system::db_delete_session(std::string_view token)
{
    if (!database_)
    {
        return;
    }

    (void)database_->execute_params("DELETE FROM sessions WHERE token = $1", std::string(token));
}

void auth_system::db_delete_all_sessions(account_id id)
{
    if (!database_)
    {
        return;
    }

    (void)database_->execute_params("DELETE FROM sessions WHERE account_id = $1", static_cast<int>(id.value));
}

void auth_system::db_record_login(account_id id,
                                  std::string_view ip_address,
                                  bool success,
                                  std::string_view failure_reason)
{
    if (!database_)
    {
        return;
    }

    (void)database_->execute_params(
        R"(INSERT INTO login_history (account_id, ip_address, success, failure_reason)
           VALUES ($1, $2, $3, $4))",
        static_cast<int>(id.value),
        std::string(ip_address),
        success,
        std::string(failure_reason));
}

auto auth_system::db_get_or_create_account_by_forum_id(uint64_t forum_member_id,
                                                       std::string_view username) -> result<account, auth_error>
{
    if (!database_)
    {
        return result<account, auth_error>::err(auth_error::database_error);
    }

    // Try to find existing account by forum_member_id
    auto db_result = database_->execute_params(
        R"(SELECT id, username, password_hash, admin_level, is_banned, ban_reason, forum_member_id
           FROM accounts WHERE forum_member_id = $1 LIMIT 1)",
        static_cast<int64_t>(forum_member_id));

    if (db_result.is_ok() && !db_result.value().empty())
    {
        const auto& row = db_result.value()[0];
        account acc{.id = account_id{static_cast<uint32_t>(row["id"].as<int>())},
                    .username = row["username"].as<std::string>(),
                    .password_hash = row["password_hash"].is_null() ? "" : row["password_hash"].as<std::string>(),
                    .admin = static_cast<admin_level>(row["admin_level"].as<int>()),
                    .is_banned = row["is_banned"].as<bool>(),
                    .forum_member_id = forum_member_id};

        if (!row["ban_reason"].is_null())
        {
            acc.ban_reason = row["ban_reason"].as<std::string>();
        }

        return result<account, auth_error>::ok(std::move(acc));
    }

    // Account doesn't exist — create it
    std::string lower_username;
    lower_username.reserve(username.size());
    for (char c : username)
    {
        lower_username += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }

    auto create_result = database_->execute_params(
        R"(INSERT INTO accounts (username, password_hash, forum_member_id)
           VALUES ($1, '', $2)
           RETURNING id, username, admin_level, is_banned)",
        lower_username,
        static_cast<int64_t>(forum_member_id));

    if (create_result.is_err())
    {
        LOG_ERROR(auth, "Failed to create forum account: {}", create_result.error());
        return result<account, auth_error>::err(auth_error::database_error);
    }

    if (create_result.value().empty())
    {
        return result<account, auth_error>::err(auth_error::database_error);
    }

    const auto& row = create_result.value()[0];
    account acc{.id = account_id{static_cast<uint32_t>(row["id"].as<int>())},
                .username = row["username"].as<std::string>(),
                .password_hash = "",
                .admin = static_cast<admin_level>(row["admin_level"].as<int>()),
                .is_banned = row["is_banned"].as<bool>(),
                .forum_member_id = forum_member_id};

    LOG_INFO(auth,
             "Created account for forum member {} (username: '{}', id: {})",
             forum_member_id,
             lower_username,
             acc.id.value);

    return result<account, auth_error>::ok(std::move(acc));
}

} // namespace hb::auth
