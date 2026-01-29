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

// Legacy game header (will be included when integrating)
// #include "Game.h"

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

    // Register subsystems
    auto& config_sys = subsystems().create_subsystem<config_system>();
    subsystems().create_subsystem<scheduler>();
    subsystems().create_subsystem<item_registry>();
    subsystems().create_subsystem<npc_registry>();
    subsystems().create_subsystem<magic_registry>();

    // Initialize subsystems
    subsystems().initialize_all();

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

    // Initialize legacy game (when integrating)
    if (config_.enable_legacy_game) {
        LOG_INFO(general, "Legacy game integration pending...");
        // TODO: Initialize CGame here when integrating
        // legacy_game_ = std::make_unique<CGame>();
        // legacy_game_->bInit();
    }

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

    // Shutdown legacy game (disabled until legacy code is ported)
    // if (legacy_game_) {
    //     LOG_INFO(general, "Shutting down legacy game...");
    //     legacy_game_->bOnClose();
    //     legacy_game_.reset();
    // }

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

    // Call legacy game timer (when integrating)
    // if (legacy_game_) {
    //     legacy_game_->OnTimer();
    // }
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

// Disabled until legacy code is ported
// auto application::legacy_game() -> CGame* {
//     return legacy_game_.get();
// }

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
