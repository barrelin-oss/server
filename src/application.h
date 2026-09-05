#pragma once

// application.h
// Main application class managing server lifecycle
// Replaces Windows GUI (WinMain/WndProc) with cross-platform console app

#include "core/types.h"
#include "world/position.h"
#include "platform/timer.h"
#include "scheduler/scheduled_task.h"

#include <atomic>
#include <memory>
#include <string>
#include <string_view>
#include <functional>
#include <vector>

namespace hb
{

// Forward declarations
namespace network
{
class websocket_server;
}
namespace bridge
{
class auth_handlers;
class game_handlers;
class admin_web_handlers;
} // namespace bridge
namespace war
{
class war_persistence;
}

// Application configuration
struct application_config
{
    std::string config_file = "server.yaml";
    uint32_t tick_interval_ms = 20; // Game tick interval (50 Hz)
};

// Main application class
class application
{
public:
    // Get the singleton instance
    [[nodiscard]] static auto instance() -> application&;

    // Run the application (blocking until shutdown)
    auto run(int argc, char* argv[]) -> int;

    // Request shutdown
    void request_shutdown(std::string_view reason = "");

    // Teleport a player through the bridge so their client is synced (see
    // game_handlers::execute_player_teleport). Returns an error message, empty on success.
    auto teleport_player_synced(player_id pid, const std::string& dest_map, world::position dest, world::direction dir)
        -> std::string;

    // Check if shutdown was requested
    [[nodiscard]] auto is_shutdown_requested() const -> bool;

    // Get shutdown reason
    [[nodiscard]] auto shutdown_reason() const -> std::string_view;

    // Get application config
    [[nodiscard]] auto config() const -> const application_config&;

    // Set game tick interval (clamped to 5-1000ms)
    void set_tick_interval(uint32_t ms);

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
    void wire_effect_system();
    void dump_loot_tables();

    // Timer callback
    void on_tick();

    // Signal handling
    static void setup_signal_handlers();
    static void signal_handler(int signal);

    application_config config_;
    std::atomic<bool> shutdown_requested_{false};
    std::atomic<int> shutdown_signal_{0};
    std::string shutdown_reason_;

    std::unique_ptr<platform::timer> game_timer_;

    bool initialized_ = false;
    bool dump_loot_tables_requested_ = false;
    uint64_t tick_count_ = 0;

    // For measuring delta time
    time_point last_tick_time_;

    // Self-contained auth mode (WebSocket + JSON protocol)
    std::unique_ptr<network::websocket_server> ws_server_;
    std::unique_ptr<bridge::auth_handlers> auth_handlers_;
    std::unique_ptr<bridge::game_handlers> game_handlers_;
    std::unique_ptr<bridge::admin_web_handlers> admin_web_handlers_;
    std::unique_ptr<war::war_persistence> war_persistence_;

    // Periodic auto-save task
    task_id auto_save_task_id_;
};

} // namespace hb
