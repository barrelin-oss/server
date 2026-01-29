// application.cpp
// Main application implementation

#include "application.h"
#include "core/logger.h"
#include "core/subsystem.h"
#include "core/event_bus.h"
#include "core/events.h"
#include "config/config_system.h"
#include "scheduler/scheduler.h"
#include "registry/item_registry.h"
#include "registry/npc_registry.h"
#include "registry/magic_registry.h"
#include "platform/clock.h"
#include "platform/platform.h"

// Network and session subsystems
#include "network/network_subsystem.h"
#include "session/session_manager.h"

// Game subsystems
#include "world/world_subsystem.h"
#include "entity/entity_manager.h"
#include "player/player_system.h"
#include "npc/npc_system.h"
#include "item/item_system.h"
#include "combat/combat_system.h"
#include "magic/magic_system.h"
#include "inventory/inventory_system.h"
#include "skill/skill_system.h"
#include "quest/quest_system.h"
#include "social/social_system.h"
#include "war/war_system.h"
#include "persistence/persistence_system.h"
#include "admin/admin_system.h"

// Protocol bridge
#include "bridge/message_router.h"
#include "bridge/handler_registry.h"
#include "bridge/handlers/wave1_handlers.h"
#include "bridge/handlers/wave2_handlers.h"
#include "bridge/handlers/wave3_handlers.h"
#include "bridge/handlers/wave4_handlers.h"
#include "bridge/handlers/wave5_handlers.h"

#include <csignal>
#include <iostream>
#include <thread>
#include <chrono>
#include <filesystem>

namespace hb {

namespace {
    application* g_app_instance = nullptr;
}

auto application::instance() -> application& {
    static application app;
    return app;
}

application::application() {
    g_app_instance = this;
}

application::~application() {
    if (initialized_) {
        shutdown();
    }
    g_app_instance = nullptr;
}

auto application::run(int argc, char* argv[]) -> int {
    // Parse command line arguments
    if (!parse_args(argc, argv)) {
        return 1;
    }

    try {
        // Initialize
        initialize();

        // Run main loop
        main_loop();

        // Shutdown
        shutdown();

        return 0;
    } catch (const std::exception& e) {
        LOG_CRITICAL(general, "Fatal error: {}", e.what());
        return 1;
    }
}

auto application::parse_args(int argc, char* argv[]) -> bool {
    // Basic argument parsing
    for (int i = 1; i < argc; ++i) {
        std::string_view arg = argv[i];

        if (arg == "--config" && i + 1 < argc) {
            config_.config_file = argv[++i];
        } else if (arg == "--help" || arg == "-h") {
            std::cout << "Helbreath Game Server\n"
                      << "Usage: hgserver [options]\n"
                      << "Options:\n"
                      << "  --config <file>  Configuration file (default: GServer.cfg)\n"
                      << "  --help, -h       Show this help message\n";
            return false;
        }
    }

    return true;
}

void application::initialize() {
    // Initialize logging first
    logger_config log_config;
    log_config.console_level = spdlog::level::info;
    log_config.file_level = spdlog::level::debug;
    logger::initialize(log_config);

    LOG_INFO(general, "===========================================");
    LOG_INFO(general, "Helbreath Game Server starting...");
    LOG_INFO(general, "Platform: {} ({})", platform::name(),
        platform::is_64bit() ? "64-bit" : "32-bit");
    LOG_INFO(general, "Compiler: {}", platform::compiler());
    LOG_INFO(general, "Debug mode: {}", platform::is_debug() ? "yes" : "no");
    LOG_INFO(general, "===========================================");

    // Set up signal handlers for graceful shutdown
    setup_signal_handlers();

    // Register core subsystems
    auto& config_sys = subsystems().create_subsystem<config_system>();
    subsystems().create_subsystem<scheduler>();
    subsystems().create_subsystem<item_registry>();
    subsystems().create_subsystem<npc_registry>();
    subsystems().create_subsystem<magic_registry>();

    // Register network and session subsystems
    auto& network = subsystems().create_subsystem<network::network_subsystem>();
    subsystems().create_subsystem<session::session_manager>();

    // Register game subsystems
    subsystems().create_subsystem<world::world_subsystem>();
    subsystems().create_subsystem<entity::entity_manager>();
    subsystems().create_subsystem<player::player_system>();
    subsystems().create_subsystem<npc::npc_system>();
    subsystems().create_subsystem<item::item_system>();
    subsystems().create_subsystem<combat::combat_system>();
    subsystems().create_subsystem<magic::magic_system>();
    subsystems().create_subsystem<inventory::inventory_system>();
    subsystems().create_subsystem<skill::skill_system>();
    subsystems().create_subsystem<quest::quest_system>();
    subsystems().create_subsystem<social::social_system>();
    subsystems().create_subsystem<war::war_system>();
    subsystems().create_subsystem<persistence::persistence_system>();
    subsystems().create_subsystem<admin::admin_system>();

    // Initialize subsystems
    subsystems().initialize_all();

    // Initialize protocol bridge and register wave handlers
    LOG_INFO(general, "Initializing protocol bridge...");
    auto& router = bridge::router();

    // Register all wave handlers
    auto wave1_count = bridge::wave1::register_wave1_handlers();
    auto wave2_count = bridge::wave2::register_wave2_handlers();
    auto wave3_count = bridge::wave3::register_wave3_handlers();
    auto wave4_count = bridge::wave4::register_wave4_handlers();
    auto wave5_count = bridge::wave5::register_wave5_handlers();

    LOG_INFO(general, "Protocol bridge handlers registered: {} total ({} wave1, {} wave2, {} wave3, {} wave4, {} wave5)",
        wave1_count + wave2_count + wave3_count + wave4_count + wave5_count,
        wave1_count, wave2_count, wave3_count, wave4_count, wave5_count);

    // Wire the message router to the network subsystem
    network.set_message_router(&router);
    LOG_INFO(general, "Message router connected to network subsystem");

    // Load configuration
    auto config_path = std::filesystem::path(config_.config_file);
    if (std::filesystem::exists(config_path)) {
        auto result = config_sys.load_server_config(config_path);
        if (result.is_ok()) {
            LOG_INFO(general, "Configuration loaded from: {}", config_path.string());
        } else {
            LOG_WARN(general, "Failed to load config: {}", result.error());
        }
    } else {
        LOG_WARN(general, "Config file not found: {}", config_path.string());
    }

    // Publish server starting event
    subsystems().event_bus().publish(events::server_starting_event{
        .version = "3.0.0",
        .timestamp = std::chrono::system_clock::now()
    });

    // Create game timer
    game_timer_ = std::make_unique<platform::timer>(
        std::chrono::milliseconds{config_.tick_interval_ms},
        [this]() { on_tick(); }
    );

    last_tick_time_ = platform::clock::now();
    initialized_ = true;

    // Publish server started event
    subsystems().event_bus().publish(events::server_started_event{
        .port = 2848,  // TODO: Get from config
        .timestamp = std::chrono::system_clock::now()
    });

    LOG_INFO(general, "Server initialized successfully");
}

void application::main_loop() {
    LOG_INFO(general, "Starting main loop...");

    // Start the game timer
    game_timer_->start();

    // Main loop - wait for shutdown signal
    while (!shutdown_requested_.load()) {
        // Sleep to avoid busy waiting
        // The actual game logic runs in the timer callback
        std::this_thread::sleep_for(std::chrono::milliseconds{100});
    }

    // Stop the game timer
    game_timer_->stop();

    LOG_INFO(general, "Main loop ended");
}

void application::shutdown() {
    if (!initialized_) {
        return;
    }

    LOG_INFO(general, "Shutting down server...");

    // Publish server stopping event
    subsystems().event_bus().publish(events::server_stopping_event{
        .reason = shutdown_reason_,
        .timestamp = std::chrono::system_clock::now()
    });

    // Unregister protocol bridge handlers
    LOG_INFO(general, "Unregistering protocol bridge handlers...");
    bridge::wave5::unregister_wave5_handlers();
    bridge::wave4::unregister_wave4_handlers();
    bridge::wave3::unregister_wave3_handlers();
    bridge::wave2::unregister_wave2_handlers();
    bridge::wave1::unregister_wave1_handlers();

    // Shutdown subsystems
    subsystems().shutdown_all();

    // Publish server stopped event
    subsystems().event_bus().publish(events::server_stopped_event{
        .timestamp = std::chrono::system_clock::now()
    });

    LOG_INFO(general, "Server shutdown complete");

    // Shutdown logging last
    logger::shutdown();

    initialized_ = false;
}

void application::on_tick() {
    auto now = platform::clock::now();
    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        now - last_tick_time_
    );
    float delta_time = elapsed.count() / 1000.0f;
    last_tick_time_ = now;

    ++tick_count_;

    // Update all subsystems
    subsystems().update_all(delta_time);

    // Publish game tick event
    subsystems().event_bus().publish(events::game_tick_event{
        .tick_count = tick_count_,
        .delta_time = delta_time
    });
}

void application::request_shutdown(std::string_view reason) {
    shutdown_reason_ = std::string(reason);
    shutdown_requested_.store(true);
    LOG_INFO(general, "Shutdown requested: {}",
        reason.empty() ? "no reason given" : reason);
}

auto application::is_shutdown_requested() const -> bool {
    return shutdown_requested_.load();
}

auto application::shutdown_reason() const -> std::string_view {
    return shutdown_reason_;
}

auto application::config() const -> const application_config& {
    return config_;
}

void application::setup_signal_handlers() {
    std::signal(SIGINT, signal_handler);   // Ctrl+C
    std::signal(SIGTERM, signal_handler);  // kill command

#ifdef HB_PLATFORM_WINDOWS
    // On Windows, also handle console close events
    // This would require SetConsoleCtrlHandler but we keep it simple for now
#endif
}

void application::signal_handler(int signal) {
    const char* signal_name = "unknown";
    switch (signal) {
        case SIGINT:  signal_name = "SIGINT"; break;
        case SIGTERM: signal_name = "SIGTERM"; break;
    }

    if (g_app_instance) {
        g_app_instance->request_shutdown(signal_name);
    }
}

}  // namespace hb
