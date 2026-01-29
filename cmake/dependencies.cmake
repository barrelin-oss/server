# dependencies.cmake
# Find and configure vcpkg dependencies

# spdlog - structured logging
find_package(spdlog CONFIG REQUIRED)

# nlohmann_json - JSON parsing for config files
find_package(nlohmann_json CONFIG REQUIRED)

# GTest - unit testing framework
find_package(GTest CONFIG REQUIRED)

# OpenSSL - cryptography (for future use)
find_package(OpenSSL REQUIRED)

# libsodium - cryptographic library (for future use)
find_package(unofficial-sodium CONFIG REQUIRED)

# zlib - compression (for future use)
find_package(ZLIB REQUIRED)

# libpqxx - PostgreSQL client (for future use)
find_package(libpqxx CONFIG REQUIRED)

# Platform-specific libraries
if(WIN32)
    set(PLATFORM_LIBS
        ws2_32      # Winsock2
        winmm       # Multimedia (timers)
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
    ${PLATFORM_LIBS}
)

set(TEST_LIBS
    GTest::gtest
    GTest::gtest_main
)
