# tests/CMakeLists.txt
# Unit tests for Helbreath server

# This file is included from the parent CMakeLists.txt
# It can also be used standalone for faster test iteration

# Test sources
set(TEST_SOURCES
    test_result.cpp
    test_event_bus.cpp
    test_config.cpp
)

# If building standalone, we need to set up the project
if(NOT TARGET hgserver_core)
    cmake_minimum_required(VERSION 3.20)
    project(hgserver_tests LANGUAGES CXX)

    set(CMAKE_CXX_STANDARD 20)
    set(CMAKE_CXX_STANDARD_REQUIRED ON)

    find_package(GTest CONFIG REQUIRED)
    find_package(spdlog CONFIG REQUIRED)
    find_package(nlohmann_json CONFIG REQUIRED)

    # Build the core library
    add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/.. ${CMAKE_CURRENT_BINARY_DIR}/hgserver)
endif()
