// force_recall_system.cpp
// Force recall system implementation

#include "war/force_recall/force_recall_system.h"
#include "network/json_protocol.h"
#include "core/logger.h"

#include <chrono>
#include <ctime>
#include <unordered_set>

namespace hb::war {

// Legacy building maps from original Helbreath
static const std::unordered_set<std::string> building_maps = {
    "wrhus", "gshop_1", "bsmith_1", "cath_1", "CmdHall_1", "cityhall_1",
    "gshop_2", "bsmith_2", "cath_2", "CmdHall_2", "cityhall_2",
    "wzdtwr", "gldhall"
};

// Building-to-faction mapping: _1 suffix = aresden, _2 suffix = elvine
// Shared buildings (wrhus, wzdtwr, gldhall) are neutral
static auto faction_from_building_suffix(const std::string& map_name) -> war_faction
{
    if (map_name.size() >= 2)
    {
        auto suffix = map_name.substr(map_name.size() - 2);
        if (suffix == "_1") return war_faction::aresden;
        if (suffix == "_2") return war_faction::elvine;
    }
    return war_faction::neutral;
}

force_recall_system::~force_recall_system()
{
    if (is_initialized())
    {
        shutdown();
    }
}

void force_recall_system::initialize()
{
    set_initialized(true);
    LOG_INFO(general, "Force recall system initialized");
}

void force_recall_system::shutdown()
{
    trackers_.clear();
    set_initialized(false);
}

void force_recall_system::update(float delta_time)
{
    if (!config_.enabled) return;
    if (trackers_.empty()) return;

    // Check for day-of-week transitions
    auto now = std::chrono::system_clock::now();
    auto time_t_now = std::chrono::system_clock::to_time_t(now);
    std::tm local_tm{};
#ifdef _WIN32
    localtime_s(&local_tm, &time_t_now);
#else
    localtime_r(&time_t_now, &local_tm);
#endif
    auto current_day = static_cast<uint8_t>(local_tm.tm_wday);

    if (last_checked_day_ != 255 && current_day != last_checked_day_)
    {
        check_day_transitions();
    }
    last_checked_day_ = current_day;

    tick_accumulator_ += delta_time;
    if (tick_accumulator_ >= 1.0f)
    {
        tick_trackers(tick_accumulator_);
        tick_accumulator_ = 0.0f;
    }
}

void force_recall_system::check_player_territory(player_id pid,
                                                   war_faction player_faction,
                                                   war_faction map_faction,
                                                   territory_flags flags)
{
    if (!config_.enabled) return;

    // Fight zone handling
    if (flags.is_fight_zone)
    {
        if (!config_.fight_zone_recall_enabled) return;

        if (trackers_.contains(pid)) return;

        bool crusade_active = is_crusade_active_ && is_crusade_active_();

        // Use config override if set, otherwise calculate dynamically
        int32_t duration;
        if (config_.fight_zone_duration_seconds > 0)
        {
            duration = config_.fight_zone_duration_seconds;
        }
        else
        {
            auto now = std::chrono::system_clock::now();
            auto time_t_now = std::chrono::system_clock::to_time_t(now);
            std::tm local_tm{};
#ifdef _WIN32
            localtime_s(&local_tm, &time_t_now);
#else
            localtime_r(&time_t_now, &local_tm);
#endif
            duration = calculate_fight_zone_duration(local_tm.tm_hour, local_tm.tm_min);
        }

        recall_tracker tracker;
        tracker.pid = pid;
        tracker.player_faction = player_faction;
        tracker.time_remaining_seconds = duration;
        tracker.max_time_seconds = duration;
        tracker.in_own_town = false;
        tracker.set_during_crusade = crusade_active;

        trackers_.emplace(pid, tracker);
        LOG_DEBUG(general, "Force recall: tracking player {} in fight zone ({}s)", pid.value, duration);
        send_timer_update(pid, duration);
        return;
    }

    // Jail handling — fixed duration
    if (flags.is_jail)
    {
        if (trackers_.contains(pid)) return;

        bool crusade_active = is_crusade_active_ && is_crusade_active_();
        int32_t duration = config_.jail_duration_seconds;

        recall_tracker tracker;
        tracker.pid = pid;
        tracker.player_faction = player_faction;
        tracker.time_remaining_seconds = duration;
        tracker.max_time_seconds = duration;
        tracker.in_own_town = false;
        tracker.set_during_crusade = crusade_active;

        trackers_.emplace(pid, tracker);
        LOG_DEBUG(general, "Force recall: tracking player {} in jail ({}s)", pid.value, duration);
        send_timer_update(pid, duration);
        return;
    }

    // Neutral players or neutral maps don't trigger recall
    if (player_faction == war_faction::neutral) return;
    if (map_faction == war_faction::neutral) return;

    // Player is in friendly territory — stop tracking
    if (player_faction == map_faction)
    {
        remove_player(pid);
        return;
    }

    // Player is in enemy territory — start tracking if not already
    if (trackers_.contains(pid)) return;

    bool crusade_active = is_crusade_active_ && is_crusade_active_();
    int32_t duration = get_current_raid_duration();

    recall_tracker tracker;
    tracker.pid = pid;
    tracker.player_faction = player_faction;
    tracker.time_remaining_seconds = duration;
    tracker.max_time_seconds = duration;
    tracker.in_own_town = false;
    tracker.set_during_crusade = crusade_active;

    trackers_.emplace(pid, tracker);

    LOG_DEBUG(general, "Force recall: tracking player {} in enemy territory ({}s, crusade={})",
        pid.value, duration, crusade_active);

    send_timer_update(pid, duration);
}

void force_recall_system::remove_player(player_id pid)
{
    trackers_.erase(pid);
}

auto force_recall_system::get_current_raid_duration() const -> int32_t
{
    if (config_.override_duration_seconds > 0)
    {
        return config_.override_duration_seconds;
    }

    auto now = std::chrono::system_clock::now();
    auto time_t_now = std::chrono::system_clock::to_time_t(now);
    std::tm local_tm{};
#ifdef _WIN32
    localtime_s(&local_tm, &time_t_now);
#else
    localtime_r(&time_t_now, &local_tm);
#endif

    for (const auto& entry : config_.raid_times)
    {
        if (entry.day_of_week == local_tm.tm_wday)
        {
            return entry.duration_seconds;
        }
    }

    return config_.default_duration_seconds;
}

auto force_recall_system::is_tracked(player_id pid) const -> bool
{
    return trackers_.contains(pid);
}

auto force_recall_system::get_tracker(player_id pid) const -> const recall_tracker*
{
    auto it = trackers_.find(pid);
    return it != trackers_.end() ? &it->second : nullptr;
}

void force_recall_system::set_fight_zone_recall(bool enabled)
{
    config_.fight_zone_recall_enabled = enabled;
    LOG_INFO(general, "Fight zone recall {}", enabled ? "enabled" : "disabled");
}

auto force_recall_system::calculate_fight_zone_duration(int hour, int minute) -> int32_t
{
    // Legacy formula: time until next even hour minus 2 minutes
    // 2-hour cycle: at 13:30 → (120 - 1*60 - 30 - 2) * 60 = 28 min = 1680s
    int minutes_in_cycle = (hour % 2) * 60 + minute;
    int remaining_minutes = 120 - minutes_in_cycle - 2;

    if (remaining_minutes <= 0) remaining_minutes = 1;  // Minimum 1 minute

    return remaining_minutes * 60;
}

void force_recall_system::check_day_transitions()
{
    auto current_duration = get_current_raid_duration();

    for (auto& [pid, tracker] : trackers_)
    {
        if (tracker.time_remaining_seconds > current_duration)
        {
            LOG_DEBUG(general, "Force recall day transition: player {} timer {}s > {}s, resetting",
                pid.value, tracker.time_remaining_seconds, current_duration);
            tracker.time_remaining_seconds = 3;  // ~1 legacy tick (3 seconds)
        }
    }
}

void force_recall_system::tick_trackers(float delta_time)
{
    auto dt = static_cast<int32_t>(delta_time);
    if (dt <= 0) dt = 1;

    bool crusade_active = is_crusade_active_ && is_crusade_active_();

    std::vector<player_id> to_recall;

    for (auto& [pid, tracker] : trackers_)
    {
        // During crusade, timer never decrements (legacy: m_bIsCrusadeMode check)
        if (crusade_active)
        {
            continue;
        }

        // In own faction territory during crusade — timer paused
        if (tracker.in_own_town)
        {
            continue;
        }

        tracker.time_remaining_seconds -= dt;

        if (tracker.time_remaining_seconds <= 0)
        {
            to_recall.push_back(pid);
            continue;
        }

        // Periodic warnings
        if (tracker.time_remaining_seconds % config_.warning_interval_seconds == 0 ||
            tracker.time_remaining_seconds <= 10)
        {
            send_timer_update(pid, tracker.time_remaining_seconds);
        }
    }

    for (auto pid : to_recall)
    {
        execute_recall(pid);
    }
}

void force_recall_system::check_building_recall(player_id pid,
                                                 const std::string& map_name,
                                                 war_faction player_faction)
{
    // Only during active crusade
    if (!is_crusade_active_ || !is_crusade_active_()) return;

    if (!is_building_map(map_name)) return;

    auto building_faction = get_building_faction(map_name);

    // Neutral buildings don't trigger recall
    if (building_faction == war_faction::neutral) return;

    // Enemy in this faction's building — instant recall
    if (player_faction != building_faction && player_faction != war_faction::neutral)
    {
        LOG_INFO(general, "Force recall: instant building recall for player {} from {} (faction mismatch)",
            pid.value, map_name);

        if (execute_fn_)
        {
            execute_fn_(pid);
        }
    }
}

void force_recall_system::check_building_debuffs(player_id pid, const std::string& map_name)
{
    if (!is_building_map(map_name)) return;

    if (debuff_removal_fn_)
    {
        debuff_removal_fn_(pid);
    }
}

void force_recall_system::clear_crusade_trackers()
{
    for (auto it = trackers_.begin(); it != trackers_.end(); )
    {
        if (it->second.set_during_crusade)
        {
            it = trackers_.erase(it);
        }
        else
        {
            ++it;
        }
    }

    LOG_DEBUG(general, "Force recall: cleared crusade trackers (remaining: {})", trackers_.size());
}

auto force_recall_system::is_building_map(const std::string& map_name) -> bool
{
    return building_maps.contains(map_name);
}

auto force_recall_system::get_building_faction(const std::string& map_name) -> war_faction
{
    if (!building_maps.contains(map_name)) return war_faction::neutral;
    return faction_from_building_suffix(map_name);
}

void force_recall_system::send_timer_update(player_id pid, int32_t remaining)
{
    if (!broadcast_fn_) return;

    nlohmann::json data;
    data["time_remaining_s"] = remaining;

    network::json_message msg;
    msg.type = network::json_message_type::force_recall_timer;
    msg.data = std::move(data);

    broadcast_fn_(pid, msg);
}

void force_recall_system::execute_recall(player_id pid)
{
    LOG_INFO(general, "Force recall: teleporting player {} home", pid.value);

    // Notify player
    if (broadcast_fn_)
    {
        nlohmann::json data;
        data["recalled"] = true;

        network::json_message msg;
        msg.type = network::json_message_type::force_recall_execute;
        msg.data = std::move(data);

        broadcast_fn_(pid, msg);
    }

    // Execute teleport
    if (execute_fn_)
    {
        execute_fn_(pid);
    }

    trackers_.erase(pid);
}

}  // namespace hb::war
