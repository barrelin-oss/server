cmake_minimum_required(VERSION 3.20)

project(hgserver
    VERSION 3.0.0
    DESCRIPTION "Helbreath Game Server"
    LANGUAGES CXX
)

# Output directories - all binaries go to bin/
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/bin)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_DEBUG ${CMAKE_SOURCE_DIR}/bin)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_RELEASE ${CMAKE_SOURCE_DIR}/bin)

# Include our cmake modules
include(cmake/compiler_settings.cmake)
include(cmake/dependencies.cmake)

# Legacy source files (existing codebase)
set(LEGACY_SOURCES
    BuildItem.cpp
    Client.cpp
    CrusadeCore.cpp
    DebugDialog.cpp
    DelayEvent.cpp
    DynamicObject.cpp
    Fish.cpp
    Game.cpp
    GuildsMan.cpp
    IrcBot.cpp
    Item.cpp
    Magic.cpp
    Map.cpp
    Mineral.cpp
    Misc.cpp
    Msg.cpp
    Npc.cpp
    NpcItem.cpp
    OccupyFlag.cpp
    Portion.cpp
    Quest.cpp
    Skill.cpp
    StrTok.cpp
    StrategicPoint.cpp
    Structure.cpp
    Teleport.cpp
    TeleportLoc.cpp
    TempNpcItem.cpp
    Tile.cpp
    XSocket.cpp
    rusadeCore.cpp
)

# New modern source files
set(CORE_SOURCES
    src/core/logger.cpp
    src/core/event_bus.cpp
    src/core/subsystem.cpp
)

set(PLATFORM_SOURCES
    src/platform/timer.cpp
)

set(CONFIG_SOURCES
    src/config/config_system.cpp
)

set(SCHEDULER_SOURCES
    src/scheduler/scheduler.cpp
)

set(REGISTRY_SOURCES
    src/registry/item_registry.cpp
    src/registry/npc_registry.cpp
    src/registry/magic_registry.cpp
)

set(NETWORK_SOURCES
    src/network/socket.cpp
    src/network/connection.cpp
    src/network/network_subsystem.cpp
    src/network/websocket_server.cpp
    src/network/json_protocol.cpp
)

set(SESSION_SOURCES
    src/session/session.cpp
    src/session/session_manager.cpp
)

set(PROTOCOL_SOURCES
    src/protocol/protocol.cpp
)

set(WORLD_SOURCES
    src/world/map.cpp
    src/world/world_subsystem.cpp
)

set(ENTITY_SOURCES
    src/entity/entity_manager.cpp
)

set(PLAYER_SOURCES
    src/player/player_system.cpp
)

set(NPC_SOURCES
    src/npc/npc_system.cpp
)

set(ITEM_SOURCES
    src/item/item_system.cpp
)

set(COMBAT_SOURCES
    src/combat/combat_system.cpp
)

set(MAGIC_SOURCES
    src/magic/magic_system.cpp
)

set(INVENTORY_SOURCES
    src/inventory/inventory_system.cpp
)

set(SKILL_SOURCES
    src/skill/skill_system.cpp
)

set(QUEST_SOURCES
    src/quest/quest_system.cpp
)

set(SOCIAL_SOURCES
    src/social/social_system.cpp
)

set(WAR_SOURCES
    src/war/war_system.cpp
)

set(PERSISTENCE_SOURCES
    src/persistence/persistence_system.cpp
)

set(ADMIN_SOURCES
    src/admin/admin_system.cpp
)

set(DATABASE_SOURCES
    src/database/connection_pool.cpp
    src/database/database_system.cpp
)

set(AUTH_SOURCES
    src/auth/password_hash.cpp
    src/auth/session_token.cpp
    src/auth/auth_system.cpp
    src/auth/character_serialization.cpp
)

set(BRIDGE_SOURCES
    src/bridge/message_router.cpp
    src/bridge/handler_registry.cpp
    src/bridge/handlers/wave1_handlers.cpp
    src/bridge/handlers/wave2_handlers.cpp
    src/bridge/handlers/wave3_handlers.cpp
    src/bridge/handlers/wave4_handlers.cpp
    src/bridge/handlers/wave5_handlers.cpp
    src/bridge/handlers/auth_handlers.cpp
    src/bridge/handlers/game_handlers.cpp
)

set(APP_SOURCES
    src/main.cpp
    src/application.cpp
)

# Core library (new modern code)
add_library(hgserver_core STATIC
    ${CORE_SOURCES}
    ${PLATFORM_SOURCES}
    ${CONFIG_SOURCES}
    ${SCHEDULER_SOURCES}
    ${REGISTRY_SOURCES}
    ${NETWORK_SOURCES}
    ${SESSION_SOURCES}
    ${PROTOCOL_SOURCES}
    ${WORLD_SOURCES}
    ${ENTITY_SOURCES}
    ${PLAYER_SOURCES}
    ${NPC_SOURCES}
    ${ITEM_SOURCES}
    ${COMBAT_SOURCES}
    ${MAGIC_SOURCES}
    ${INVENTORY_SOURCES}
    ${SKILL_SOURCES}
    ${QUEST_SOURCES}
    ${SOCIAL_SOURCES}
    ${WAR_SOURCES}
    ${PERSISTENCE_SOURCES}
    ${ADMIN_SOURCES}
    ${DATABASE_SOURCES}
    ${AUTH_SOURCES}
    ${BRIDGE_SOURCES}
)

target_include_directories(hgserver_core PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}  # For legacy headers
)

target_link_libraries(hgserver_core PUBLIC
    ${COMMON_LIBS}
)

# Platform-specific libraries for network
if(WIN32)
    target_link_libraries(hgserver_core PUBLIC ws2_32)
endif()

# Legacy library (existing codebase) - disabled until ported
# add_library(hgserver_legacy STATIC
#     ${LEGACY_SOURCES}
# )
#
# target_include_directories(hgserver_legacy PUBLIC
#     ${CMAKE_CURRENT_SOURCE_DIR}
#     ${CMAKE_CURRENT_SOURCE_DIR}/src
# )
#
# target_link_libraries(hgserver_legacy PUBLIC
#     hgserver_core
#     ${PLATFORM_LIBS}
# )
#
# # For legacy code, we need Windows-specific defines
# if(WIN32)
#     target_compile_definitions(hgserver_legacy PRIVATE
#         WIN32
#         _WINDOWS
#         _CRT_SECURE_NO_WARNINGS
#     )
# endif()

# Main executable (without legacy code for now)
add_executable(hgserver
    ${APP_SOURCES}
)

target_link_libraries(hgserver PRIVATE
    hgserver_core
    # hgserver_legacy  # Disabled until legacy code is ported
)

# Set Visual Studio debugger working directory to bin/
set_target_properties(hgserver PROPERTIES
    VS_DEBUGGER_WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}/bin"
)

# Tests
enable_testing()

add_executable(hgserver_tests
    tests/test_result.cpp
    tests/test_event_bus.cpp
    tests/test_config.cpp
    tests/test_scheduler.cpp
    tests/test_registry.cpp
    tests/test_network.cpp
    tests/test_session.cpp
    tests/test_protocol.cpp
    tests/test_world.cpp
    tests/test_entity.cpp
    tests/test_player.cpp
    tests/test_npc.cpp
    tests/test_item.cpp
    tests/test_combat.cpp
    tests/test_magic.cpp
    tests/test_inventory.cpp
    tests/test_skill.cpp
    tests/test_quest.cpp
    tests/test_social.cpp
    tests/test_war.cpp
    tests/test_persistence.cpp
    tests/test_admin.cpp
    tests/test_bridge.cpp
    tests/test_auth.cpp
    tests/test_integration.cpp
)

target_link_libraries(hgserver_tests PRIVATE
    hgserver_core
    ${TEST_LIBS}
)

target_include_directories(hgserver_tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)

# Set Visual Studio debugger working directory to bin/
set_target_properties(hgserver_tests PROPERTIES
    VS_DEBUGGER_WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}/bin"
)

include(GoogleTest)
gtest_discover_tests(hgserver_tests
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}/bin
)

# Install rules
install(TARGETS hgserver
    RUNTIME DESTINATION bin
)

# Copy config files to bin directory
file(GLOB CONFIG_FILES "${CMAKE_CURRENT_SOURCE_DIR}/*.cfg")
file(COPY ${CONFIG_FILES} DESTINATION ${CMAKE_SOURCE_DIR}/bin)

# Copy DLLs to bin directory after build (Windows only)
if(WIN32)
    # Post-build command to copy DLLs from vcpkg
    add_custom_command(TARGET hgserver POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_FILE:spdlog::spdlog>
            ${CMAKE_SOURCE_DIR}/bin
        COMMAND_EXPAND_LISTS
    )

    # Copy all required vcpkg DLLs
    set(VCPKG_BIN_DIR "${CMAKE_BINARY_DIR}/vcpkg_installed/x64-windows/bin")
    if(EXISTS ${VCPKG_BIN_DIR})
        add_custom_command(TARGET hgserver POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_directory
                ${VCPKG_BIN_DIR}
                ${CMAKE_SOURCE_DIR}/bin
            COMMENT "Copying vcpkg DLLs to bin directory"
        )
    endif()
endif()

# Set hgserver as the default startup project in Visual Studio
set_property(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR} PROPERTY VS_STARTUP_PROJECT hgserver)
