#pragma once

// password_hash.h
// Secure password hashing using Argon2id via libsodium

#include "core/result.h"

#include <string>
#include <string_view>

namespace hb::auth {

// Password hash configuration
struct password_hash_config {
    // Memory limit in bytes (default: 64 MiB for interactive use)
    size_t memory_limit = 64 * 1024 * 1024;

    // Operations limit (time cost)
    unsigned long long ops_limit = 2;
};

// Hash a password using Argon2id
// Returns the encoded hash string (includes salt and parameters)
[[nodiscard]] auto hash_password(std::string_view password)
    -> result<std::string, std::string>;

[[nodiscard]] auto hash_password(std::string_view password, const password_hash_config& config)
    -> result<std::string, std::string>;

// Verify a password against a stored hash
// Returns true if the password matches
[[nodiscard]] auto verify_password(std::string_view password, std::string_view hash)
    -> bool;

// Check if a hash needs to be rehashed (e.g., if security parameters have changed)
[[nodiscard]] auto needs_rehash(std::string_view hash)
    -> bool;

// Validate password requirements
struct password_validation_result {
    bool valid{false};
    std::string error_message;
};

[[nodiscard]] auto validate_password(std::string_view password)
    -> password_validation_result;

// Validate username requirements
[[nodiscard]] auto validate_username(std::string_view username)
    -> password_validation_result;

}  // namespace hb::auth
