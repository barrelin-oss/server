#pragma once

// application.h
// Main application class managing server lifecycle
// Replaces Windows GUI (WinMain/WndProc) with cross-platform console app

#include "core/types.h"
#include "platform/timer.h"

#include <atomic>
#include <memory>
#include <string>
#include <string_view>
#include <functional>
#include <vector>

// Forward declaration of legacy game class (disabled until legacy code is ported)
// class CGame;

namespace hb {

// Application configuration
struct application_config {
    std::string config_file = "GServer.cfg";
    bool enable_legacy_game = true;  // Enable legacy CGame for backward compatibility
    uint32_t tick_interval_ms = 100; // Game tick interval
};

// Main application class
class application {
public:
    // Get the singleton instance
    [[nodiscard]] static auto instance() -> application&;

    // Run the application (blocking until shutdown)
    auto run(int argc, char* argv[]) -> int;

    // Request shutdown
    void request_shutdown(std::string_view reason = "");

    // Check if shutdown was requested
    [[nodiscard]] auto is_shutdown_requested() const -> bool;

    // Get shutdown reason
    [[nodiscard]] auto shutdown_reason() const -> std::string_view;

    // Get application config
    [[nodiscard]] auto config() const -> const application_config&;

    // Get the legacy game instance (for backward compatibility)
    // Disabled until legacy code is ported
    // [[nodiscard]] auto legacy_game() -> CGame*;

private:
    application();
    ~application();

    // Non-copyable, non-movable
    application(const application&) = delete;
    auto operator=(const application&) -> application& = delete;
    application(application&&) = delete;
    auto operator=(application&&) -> application& = delete;

    // Lifecycle methods
    auto parse_args(int argc, char* argv[]) -> bool;
    void initialize();
    void main_loop();
    void shutdown();

    // Timer callback
    void on_tick();

    // Signal handling
    static void setup_signal_handlers();
    static void signal_handler(int signal);

    application_config config_;
    std::atomic<bool> shutdown_requested_{false};
    std::string shutdown_reason_;

    std::unique_ptr<platform::timer> game_timer_;
    // std::unique_ptr<CGame> legacy_game_;  // Disabled until legacy code is ported

    bool initialized_ = false;
    uint64_t tick_count_ = 0;

    // For measuring delta time
    time_point last_tick_time_;
};

}  // namespace hb
