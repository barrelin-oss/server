#pragma once

// apocalypse_system.h
// Apocalypse gate-toggle event — opens gates on specific maps, teleports players through them

#include "core/types.h"
#include "core/subsystem.h"
#include "war/apocalypse/apocalypse_types.h"

#include <functional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace hb::network
{
struct json_message;
}

namespace hb::war
{

// Callback types for external interactions
using apocalypse_broadcast_all_fn = std::function<void(const network::json_message&)>;
using apocalypse_broadcast_player_fn = std::function<void(player_id, const network::json_message&)>;
using apocalypse_teleport_home_fn = std::function<void(player_id)>;
using apocalypse_teleport_to_fn = std::function<void(player_id, const std::string&, world::position)>;
using apocalypse_get_players_on_map_fn =
    std::function<std::vector<std::pair<player_id, world::position>>(const std::string&)>;

enum class apocalypse_result : uint8_t
{
    success = 0,
    not_active = 1,
    already_active = 2,
};

class apocalypse_system : public subsystem
{
public:
    apocalypse_system() = default;
    ~apocalypse_system() override;

    [[nodiscard]] auto name() const -> std::string_view override { return "apocalypse_system"; }
    void initialize() override;
    void shutdown() override;
    void update(float delta_time) override;

    // Configuration
    void set_config(const apocalypse_config& config) { config_ = config; }

    // Callback setters
    void set_broadcast_all_fn(apocalypse_broadcast_all_fn fn) { broadcast_all_fn_ = std::move(fn); }
    void set_broadcast_player_fn(apocalypse_broadcast_player_fn fn) { broadcast_player_fn_ = std::move(fn); }
    void set_teleport_home_fn(apocalypse_teleport_home_fn fn) { teleport_home_fn_ = std::move(fn); }
    void set_teleport_to_fn(apocalypse_teleport_to_fn fn) { teleport_to_fn_ = std::move(fn); }
    void set_get_players_on_map_fn(apocalypse_get_players_on_map_fn fn) { get_players_on_map_fn_ = std::move(fn); }

    // Event control
    auto start_event() -> apocalypse_result;
    auto end_event() -> apocalypse_result;

    [[nodiscard]] auto is_active() const -> bool { return active_; }

    // Schedule checking (public for testing)
    [[nodiscard]] auto check_start_schedule() const -> bool;
    [[nodiscard]] auto check_end_schedule() const -> bool;

private:
    [[nodiscard]] auto check_schedule(const std::vector<apocalypse_schedule_entry>& entries) const -> bool;
    void check_gates();
    void eject_players_from_apocalypse_maps();

    apocalypse_config config_;
    bool active_{false};
    float gate_check_accumulator_{0.0f};
    float schedule_check_accumulator_{0.0f};

    apocalypse_broadcast_all_fn broadcast_all_fn_;
    apocalypse_broadcast_player_fn broadcast_player_fn_;
    apocalypse_teleport_home_fn teleport_home_fn_;
    apocalypse_teleport_to_fn teleport_to_fn_;
    apocalypse_get_players_on_map_fn get_players_on_map_fn_;
};

} // namespace hb::war
