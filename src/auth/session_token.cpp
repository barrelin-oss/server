// session_token.cpp
// Secure session token generation using libsodium

#include "auth/session_token.h"
#include "core/logger.h"

#include <sodium.h>

namespace hb::auth {

namespace {

// Initialize libsodium
auto init_sodium() -> bool {
    static bool initialized = false;
    static bool init_result = false;

    if (!initialized) {
        init_result = (sodium_init() >= 0);
        if (!init_result) {
            LOG_ERROR(auth, "Failed to initialize libsodium");
        }
        initialized = true;
    }

    return init_result;
}

// Base64 encoding table (URL-safe variant)
constexpr char base64_chars[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";

auto base64_encode(const unsigned char* data, size_t len) -> std::string {
    std::string result;
    result.reserve((len + 2) / 3 * 4);

    for (size_t i = 0; i < len; i += 3) {
        uint32_t n = static_cast<uint32_t>(data[i]) << 16;

        if (i + 1 < len) {
            n |= static_cast<uint32_t>(data[i + 1]) << 8;
        }
        if (i + 2 < len) {
            n |= static_cast<uint32_t>(data[i + 2]);
        }

        result.push_back(base64_chars[(n >> 18) & 0x3F]);
        result.push_back(base64_chars[(n >> 12) & 0x3F]);

        if (i + 1 < len) {
            result.push_back(base64_chars[(n >> 6) & 0x3F]);
        }
        if (i + 2 < len) {
            result.push_back(base64_chars[n & 0x3F]);
        }
    }

    return result;
}

}  // namespace

auto generate_token() -> result<std::string, std::string> {
    return generate_token(32);  // 256 bits of entropy
}

auto generate_token(size_t bytes) -> result<std::string, std::string> {
    if (!init_sodium()) {
        return result<std::string, std::string>::err("Cryptographic library not initialized");
    }

    if (bytes < 16) {
        return result<std::string, std::string>::err("Token must be at least 16 bytes");
    }

    if (bytes > 256) {
        return result<std::string, std::string>::err("Token cannot exceed 256 bytes");
    }

    // Generate random bytes
    std::vector<unsigned char> buffer(bytes);
    randombytes_buf(buffer.data(), bytes);

    // Encode as URL-safe base64
    std::string token = base64_encode(buffer.data(), buffer.size());

    // Securely clear the buffer
    sodium_memzero(buffer.data(), buffer.size());

    return result<std::string, std::string>::ok(std::move(token));
}

auto generate_token(const session_token_config& config) -> result<std::string, std::string> {
    return generate_token(config.token_bytes);
}

auto validate_token_format(std::string_view token) -> bool {
    // Minimum length (16 bytes = ~22 base64 chars)
    if (token.size() < 22) {
        return false;
    }

    // Maximum length (256 bytes = ~342 base64 chars)
    if (token.size() > 342) {
        return false;
    }

    // Check for valid base64 characters (URL-safe variant)
    for (char c : token) {
        bool valid = (c >= 'A' && c <= 'Z') ||
                     (c >= 'a' && c <= 'z') ||
                     (c >= '0' && c <= '9') ||
                     c == '-' || c == '_';
        if (!valid) {
            return false;
        }
    }

    return true;
}

auto create_session_token(
    account_id account,
    std::chrono::seconds duration,
    std::optional<std::string_view> ip_address,
    std::optional<std::string_view> user_agent
) -> result<session_token, std::string>
{
    auto token_result = generate_token();
    if (token_result.is_err()) {
        return result<session_token, std::string>::err(token_result.error());
    }

    auto now = std::chrono::system_clock::now();

    session_token session{
        .token = std::move(token_result).value(),
        .account = account,
        .created_at = now,
        .expires_at = now + duration,
        .ip_address = ip_address ? std::optional<std::string>(std::string(*ip_address)) : std::nullopt,
        .user_agent = user_agent ? std::optional<std::string>(std::string(*user_agent)) : std::nullopt
    };

    return result<session_token, std::string>::ok(std::move(session));
}

}  // namespace hb::auth
