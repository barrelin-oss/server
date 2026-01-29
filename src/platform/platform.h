#pragma once

// platform.h
// Platform detection and common platform-specific includes

// Platform detection
#if defined(_WIN32) || defined(_WIN64)
    #define HB_PLATFORM_WINDOWS 1
    #define HB_PLATFORM_NAME "Windows"
#elif defined(__linux__)
    #define HB_PLATFORM_LINUX 1
    #define HB_PLATFORM_NAME "Linux"
#elif defined(__APPLE__)
    #define HB_PLATFORM_MACOS 1
    #define HB_PLATFORM_NAME "macOS"
#else
    #error "Unsupported platform"
#endif

// Architecture detection
#if defined(_M_X64) || defined(__x86_64__) || defined(__aarch64__)
    #define HB_ARCH_64BIT 1
#else
    #define HB_ARCH_32BIT 1
#endif

// Compiler detection
#if defined(_MSC_VER)
    #define HB_COMPILER_MSVC 1
    #define HB_COMPILER_NAME "MSVC"
#elif defined(__clang__)
    #define HB_COMPILER_CLANG 1
    #define HB_COMPILER_NAME "Clang"
#elif defined(__GNUC__)
    #define HB_COMPILER_GCC 1
    #define HB_COMPILER_NAME "GCC"
#else
    #define HB_COMPILER_UNKNOWN 1
    #define HB_COMPILER_NAME "Unknown"
#endif

// Debug mode detection
#if defined(DEBUG) || defined(_DEBUG) || !defined(NDEBUG)
    #define HB_DEBUG 1
#else
    #define HB_RELEASE 1
#endif

// Platform-specific includes
#ifdef HB_PLATFORM_WINDOWS
    #ifndef WIN32_LEAN_AND_MEAN
        #define WIN32_LEAN_AND_MEAN
    #endif
    #ifndef NOMINMAX
        #define NOMINMAX
    #endif
#endif

// Common standard includes
#include <cstdint>
#include <cstddef>
#include <chrono>
#include <string>
#include <string_view>

namespace hb::platform {

// Get platform name at runtime
[[nodiscard]] constexpr auto name() -> std::string_view {
    return HB_PLATFORM_NAME;
}

// Get compiler name at runtime
[[nodiscard]] constexpr auto compiler() -> std::string_view {
    return HB_COMPILER_NAME;
}

// Check if running in debug mode
[[nodiscard]] constexpr auto is_debug() -> bool {
#ifdef HB_DEBUG
    return true;
#else
    return false;
#endif
}

// Check if 64-bit
[[nodiscard]] constexpr auto is_64bit() -> bool {
#ifdef HB_ARCH_64BIT
    return true;
#else
    return false;
#endif
}

}  // namespace hb::platform
