// apocalypse_system.cpp
// Apocalypse gate-toggle event implementation

#include "war/apocalypse/apocalypse_system.h"
#include "network/json_protocol.h"
#include "core/logger.h"

#include <algorithm>
#include <chrono>
#include <ctime>

namespace hb::war
{

apocalypse_system::~apocalypse_system()
{
    if (is_initialized())
    {
        shutdown();
    }
}

void apocalypse_system::initialize()
{
    set_initialized(true);
    LOG_INFO(general, "Apocalypse system initialized");
}

void apocalypse_system::shutdown()
{
    if (active_)
    {
        end_event();
    }
    set_initialized(false);
}

void apocalypse_system::update(float delta_time)
{
    if (active_)
    {
        // Periodic gate checking
        gate_check_accumulator_ += delta_time;
        if (gate_check_accumulator_ >= config_.gate_check_interval_seconds)
        {
            gate_check_accumulator_ = 0.0f;
            check_gates();
        }

        // Check end schedule every ~1s
        schedule_check_accumulator_ += delta_time;
        if (schedule_check_accumulator_ >= 1.0f)
        {
            schedule_check_accumulator_ = 0.0f;
            if (check_end_schedule())
            {
                end_event();
            }
        }
    }
    else
    {
        // Check start schedule every ~1s
        schedule_check_accumulator_ += delta_time;
        if (schedule_check_accumulator_ >= 1.0f)
        {
            schedule_check_accumulator_ = 0.0f;
            if (check_start_schedule())
            {
                start_event();
            }
        }
    }
}

auto apocalypse_system::start_event() -> apocalypse_result
{
    if (active_)
        return apocalypse_result::already_active;

    active_ = true;
    gate_check_accumulator_ = 0.0f;
    schedule_check_accumulator_ = 0.0f;

    LOG_INFO(general, "Apocalypse event started");

    // Broadcast apocalypse_started
    if (broadcast_all_fn_)
    {
        nlohmann::json data;
        data["active"] = true;

        network::json_message msg;
        msg.type = network::json_message_type::apocalypse_started;
        msg.data = std::move(data);
        broadcast_all_fn_(msg);
    }

    return apocalypse_result::success;
}

auto apocalypse_system::end_event() -> apocalypse_result
{
    if (!active_)
        return apocalypse_result::not_active;

    LOG_INFO(general, "Apocalypse event ended");

    // Broadcast apocalypse_ended
    if (broadcast_all_fn_)
    {
        nlohmann::json data;
        data["active"] = false;

        network::json_message msg;
        msg.type = network::json_message_type::apocalypse_ended;
        msg.data = std::move(data);
        broadcast_all_fn_(msg);
    }

    eject_players_from_apocalypse_maps();

    active_ = false;

    return apocalypse_result::success;
}

void apocalypse_system::check_gates()
{
    if (!get_players_on_map_fn_)
        return;

    for (const auto& gate : config_.gates)
    {
        auto players = get_players_on_map_fn_(gate.map_name);

        for (const auto& [pid, pos] : players)
        {
            // Send gate notification to every player on the map
            if (broadcast_player_fn_)
            {
                nlohmann::json data;
                data["map"] = gate.map_name;
                data["gate_x"] = gate.gate_x;
                data["gate_y"] = gate.gate_y;

                network::json_message msg;
                msg.type = network::json_message_type::apocalypse_gate_open;
                msg.data = std::move(data);
                broadcast_player_fn_(pid, msg);
            }

            // If player is standing on a teleport tile and destination exists, teleport
            if (!gate.destination_map.empty() && teleport_to_fn_)
            {
                bool on_tile =
                    std::any_of(gate.teleport_tiles.begin(),
                                gate.teleport_tiles.end(),
                                [&](const auto& tile) { return tile.first == pos.x && tile.second == pos.y; });

                if (on_tile)
                {
                    teleport_to_fn_(pid, gate.destination_map, gate.destination_pos);
                }
            }
        }
    }
}

void apocalypse_system::eject_players_from_apocalypse_maps()
{
    if (!get_players_on_map_fn_ || !teleport_home_fn_)
        return;

    for (const auto& map_name : config_.apocalypse_maps)
    {
        auto players = get_players_on_map_fn_(map_name);
        for (const auto& [pid, pos] : players)
        {
            teleport_home_fn_(pid);
        }
    }
}

auto apocalypse_system::check_start_schedule() const -> bool
{
    return check_schedule(config_.start_schedule);
}

auto apocalypse_system::check_end_schedule() const -> bool
{
    return check_schedule(config_.end_schedule);
}

auto apocalypse_system::check_schedule(const std::vector<apocalypse_schedule_entry>& entries) const -> bool
{
    if (entries.empty())
        return false;

    auto now = std::chrono::system_clock::now();
    auto time_t_now = std::chrono::system_clock::to_time_t(now);
    std::tm local_tm{};
#ifdef _WIN32
    localtime_s(&local_tm, &time_t_now);
#else
    localtime_r(&time_t_now, &local_tm);
#endif

    for (const auto& entry : entries)
    {
        if (local_tm.tm_wday == entry.day_of_week && local_tm.tm_hour == entry.hour && local_tm.tm_min == entry.minute)
        {
            return true;
        }
    }
    return false;
}

} // namespace hb::war
