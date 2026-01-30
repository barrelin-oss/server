// password_hash.cpp
// Secure password hashing using Argon2id via libsodium

#include "auth/password_hash.h"
#include "core/logger.h"

#include <sodium.h>
#include <algorithm>
#include <cctype>

namespace hb::auth {

namespace {

// Initialize libsodium (thread-safe, can be called multiple times)
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

}  // namespace

auto hash_password(std::string_view password) -> hb::result<std::string, std::string> {
    return hash_password(password, password_hash_config{});
}

auto hash_password(std::string_view password, const password_hash_config& config)
    -> hb::result<std::string, std::string>
{
    if (!init_sodium()) {
        return hb::result<std::string, std::string>::err("Cryptographic library not initialized");
    }

    // Validate password first
    auto validation = validate_password(password);
    if (!validation.valid) {
        return hb::result<std::string, std::string>::err(validation.error_message);
    }

    // Allocate buffer for the hash
    // crypto_pwhash_STRBYTES includes null terminator
    char hash[crypto_pwhash_STRBYTES];

    // Use Argon2id (the recommended algorithm)
    int hash_result = crypto_pwhash_str(
        hash,
        password.data(),
        password.size(),
        config.ops_limit,
        config.memory_limit
    );

    if (hash_result != 0) {
        LOG_ERROR(auth, "Password hashing failed (out of memory or other error)");
        return hb::result<std::string, std::string>::err("Failed to hash password");
    }

    return hb::result<std::string, std::string>::ok(std::string(hash));
}

auto verify_password(std::string_view password, std::string_view hash) -> bool {
    if (!init_sodium()) {
        return false;
    }

    if (password.empty() || hash.empty()) {
        return false;
    }

    // Ensure hash is null-terminated for libsodium
    std::string hash_str(hash);

    int verify_result = crypto_pwhash_str_verify(
        hash_str.c_str(),
        password.data(),
        password.size()
    );

    return verify_result == 0;
}

auto needs_rehash(std::string_view hash) -> bool {
    if (!init_sodium()) {
        return false;
    }

    if (hash.empty()) {
        return true;
    }

    // Ensure hash is null-terminated
    std::string hash_str(hash);

    // Check if the hash needs to be rehashed with current parameters
    // This returns 1 if rehashing is needed, 0 if not
    return crypto_pwhash_str_needs_rehash(
        hash_str.c_str(),
        crypto_pwhash_OPSLIMIT_INTERACTIVE,
        crypto_pwhash_MEMLIMIT_INTERACTIVE
    ) != 0;
}

auto validate_password(std::string_view password) -> password_validation_result {
    password_validation_result validation;

    // Minimum length
    if (password.size() < 6) {
        validation.error_message = "Password must be at least 6 characters";
        return validation;
    }

    // Maximum length (to prevent DoS via extremely long passwords)
    if (password.size() > 128) {
        validation.error_message = "Password must be at most 128 characters";
        return validation;
    }

    // Check for at least one letter and one digit (basic complexity)
    bool has_letter = false;
    bool has_digit = false;

    for (char c : password) {
        if (std::isalpha(static_cast<unsigned char>(c))) {
            has_letter = true;
        }
        if (std::isdigit(static_cast<unsigned char>(c))) {
            has_digit = true;
        }
    }

    if (!has_letter) {
        validation.error_message = "Password must contain at least one letter";
        return validation;
    }

    if (!has_digit) {
        validation.error_message = "Password must contain at least one digit";
        return validation;
    }

    validation.valid = true;
    return validation;
}

auto validate_username(std::string_view username) -> password_validation_result {
    password_validation_result validation;

    // Minimum length
    if (username.size() < 3) {
        validation.error_message = "Username must be at least 3 characters";
        return validation;
    }

    // Maximum length
    if (username.size() > 32) {
        validation.error_message = "Username must be at most 32 characters";
        return validation;
    }

    // Check for valid characters (alphanumeric and underscore only)
    for (size_t i = 0; i < username.size(); ++i) {
        char c = username[i];
        bool valid_char = std::isalnum(static_cast<unsigned char>(c)) || c == '_';

        if (!valid_char) {
            validation.error_message = "Username can only contain letters, numbers, and underscores";
            return validation;
        }
    }

    // Must start with a letter
    if (!std::isalpha(static_cast<unsigned char>(username[0]))) {
        validation.error_message = "Username must start with a letter";
        return validation;
    }

    validation.valid = true;
    return validation;
}

}  // namespace hb::auth
