# dependencies.cmake
# Find and configure vcpkg dependencies

# spdlog - structured logging
find_package(spdlog CONFIG REQUIRED)

# nlohmann_json - JSON parsing for config files
find_package(nlohmann_json CONFIG REQUIRED)

# yaml-cpp - YAML parsing for config files
find_package(yaml-cpp CONFIG REQUIRED)

# GTest - unit testing framework
find_package(GTest CONFIG REQUIRED)

# OpenSSL - cryptography
find_package(OpenSSL REQUIRED)

# libsodium - cryptographic library (password hashing, token generation)
find_package(unofficial-sodium CONFIG REQUIRED)

# zlib - compression
find_package(ZLIB REQUIRED)

# libpqxx - PostgreSQL client
find_package(libpqxx CONFIG REQUIRED)

# ixwebsocket - WebSocket server
find_package(ixwebsocket CONFIG REQUIRED)

# Platform-specific libraries
if(WIN32)
    set(PLATFORM_LIBS
        ws2_32      # Winsock2
        winmm       # Multimedia (timers)
        bcrypt      # Windows cryptographic API (required by mbedtls/ixwebsocket)
    )
else()
    set(PLATFORM_LIBS
        pthread
    )
endif()

# Export variables for use in main CMakeLists.txt
set(COMMON_LIBS
    spdlog::spdlog
    nlohmann_json::nlohmann_json
    yaml-cpp::yaml-cpp
    libpqxx::pqxx
    unofficial-sodium::sodium
    ixwebsocket::ixwebsocket
    OpenSSL::SSL
    OpenSSL::Crypto
    ZLIB::ZLIB
    ${PLATFORM_LIBS}
)

set(TEST_LIBS
    GTest::gtest
    GTest::gtest_main
)
