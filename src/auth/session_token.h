#pragma once

// session_token.h
// Secure session token generation and validation

#include "core/types.h"
#include "core/result.h"

#include <string>
#include <string_view>
#include <chrono>
#include <optional>

namespace hb::auth {

// Session token configuration
struct session_token_config {
    // Token length in bytes (before base64 encoding)
    size_t token_bytes = 32;

    // Default session duration
    std::chrono::seconds default_duration{3600};  // 1 hour

    // Maximum session duration
    std::chrono::seconds max_duration{86400};  // 24 hours
};

// Session token data
struct session_token {
    std::string token;
    account_id account;
    std::chrono::system_clock::time_point created_at;
    std::chrono::system_clock::time_point expires_at;
    std::optional<std::string> ip_address;
    std::optional<std::string> user_agent;

    [[nodiscard]] auto is_expired() const -> bool {
        return std::chrono::system_clock::now() >= expires_at;
    }

    [[nodiscard]] auto time_remaining() const -> std::chrono::seconds {
        auto now = std::chrono::system_clock::now();
        if (now >= expires_at) {
            return std::chrono::seconds{0};
        }
        return std::chrono::duration_cast<std::chrono::seconds>(expires_at - now);
    }
};

// Generate a new secure random token
[[nodiscard]] auto generate_token() -> result<std::string, std::string>;

[[nodiscard]] auto generate_token(size_t bytes) -> result<std::string, std::string>;

// Generate a token with custom config
[[nodiscard]] auto generate_token(const session_token_config& config)
    -> result<std::string, std::string>;

// Validate token format (does not check if token exists in database)
[[nodiscard]] auto validate_token_format(std::string_view token) -> bool;

// Create a session token struct
[[nodiscard]] auto create_session_token(
    account_id account,
    std::chrono::seconds duration = std::chrono::seconds{3600},
    std::optional<std::string_view> ip_address = std::nullopt,
    std::optional<std::string_view> user_agent = std::nullopt
) -> result<session_token, std::string>;

}  // namespace hb::auth
