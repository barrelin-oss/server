#pragma once

// application.h
// Main application class managing server lifecycle
// Replaces Windows GUI (WinMain/WndProc) with cross-platform console app

#include "core/types.h"
#include "platform/timer.h"
#include "scheduler/scheduled_task.h"

#include <atomic>
#include <memory>
#include <string>
#include <string_view>
#include <functional>
#include <vector>

namespace hb {

// Forward declarations
namespace network {
    class websocket_server;
}
namespace bridge {
    class auth_handlers;
    class game_handlers;
}

// Application configuration
struct application_config {
    std::string config_file = "server.yaml";
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

    // Initialization helpers
    void load_maps();
    void load_game_configs();
    void register_spawn_points();

    // Timer callback
    void on_tick();

    // Signal handling
    static void setup_signal_handlers();
    static void signal_handler(int signal);

    application_config config_;
    std::atomic<bool> shutdown_requested_{false};
    std::string shutdown_reason_;

    std::unique_ptr<platform::timer> game_timer_;

    bool initialized_ = false;
    uint64_t tick_count_ = 0;

    // For measuring delta time
    time_point last_tick_time_;

    // Self-contained auth mode (WebSocket + JSON protocol)
    std::unique_ptr<network::websocket_server> ws_server_;
    std::unique_ptr<bridge::auth_handlers> auth_handlers_;
    std::unique_ptr<bridge::game_handlers> game_handlers_;

    // Periodic auto-save task
    task_id auto_save_task_id_;
};

}  // namespace hb
