// heldenian_system.cpp
// Heldenian warfare subsystem implementation

#include "war/heldenian/heldenian_system.h"
#include "war/war_system.h"
#include "player/player_system.h"
#include "player/player.h"
#include "npc/npc_system.h"
#include "world/world_subsystem.h"
#include "network/json_protocol.h"
#include "war/war_persistence.h"
#include "core/logger.h"

#include <algorithm>
#include <chrono>
#include <ctime>

namespace hb::war
{

heldenian_system::heldenian_system() = default;

heldenian_system::~heldenian_system()
{
    if (is_initialized())
    {
        shutdown();
    }
}

void heldenian_system::initialize()
{
    LOG_INFO(general, "Heldenian system initializing...");

    // Load last winner from database for tiebreaker persistence
    if (persistence_)
    {
        auto result = persistence_->load_last_winner(war_type::heldenian);
        if (result.is_ok())
        {
            last_winner_ = result.value();
            if (last_winner_ != war_faction::neutral)
            {
                LOG_INFO(general, "Loaded heldenian last winner: faction {}", static_cast<int>(last_winner_));
            }
        }
    }

    set_initialized(true);
    LOG_INFO(general, "Heldenian system initialized");
}

void heldenian_system::shutdown()
{
    LOG_INFO(general, "Heldenian system shutting down...");

    if (active_)
    {
        cleanup_heldenian();
    }

    player_data_.clear();
    set_initialized(false);
    LOG_INFO(general, "Heldenian system shutdown complete");
}

void heldenian_system::update(float delta_time)
{
    if (active_)
    {
        elapsed_seconds_ += static_cast<int32_t>(delta_time);

        // Periodic status broadcast
        status_broadcast_accumulator_ += delta_time;
        if (status_broadcast_accumulator_ >= static_cast<float>(config_.timing.status_broadcast_seconds))
        {
            status_broadcast_accumulator_ = 0.0f;
            broadcast_status_update();
        }

        // Check time limit
        if (elapsed_seconds_ >= config_.timing.duration_seconds)
        {
            LOG_INFO(general, "Heldenian time limit reached after {} seconds", elapsed_seconds_);

            if (current_mode_ == heldenian_mode::tower_defense)
            {
                // Faction with more surviving towers wins
                int32_t aresden_alive = count_surviving(war_faction::aresden);
                int32_t elvine_alive = count_surviving(war_faction::elvine);

                if (aresden_alive > elvine_alive)
                    end_heldenian(war_faction::aresden);
                else if (elvine_alive > aresden_alive)
                    end_heldenian(war_faction::elvine);
                else
                {
                    // Equal towers — use death count tiebreaker (fewer deaths wins)
                    if (aresden_deaths_ < elvine_deaths_)
                        end_heldenian(war_faction::aresden);
                    else if (elvine_deaths_ < aresden_deaths_)
                        end_heldenian(war_faction::elvine);
                    else
                        end_heldenian(last_winner_); // Complete tie — previous winner stays
                }
            }
            else
            {
                // Door defense: if doors survive, defender wins
                auto attacker =
                    (defending_faction_ == war_faction::aresden) ? war_faction::elvine : war_faction::aresden;
                if (!all_objectives_destroyed(defending_faction_))
                    end_heldenian(defending_faction_); // Defender wins
                else
                    end_heldenian(attacker); // Attacker wins
            }
        }
    }
    else
    {
        // Check schedule periodically
        schedule_check_accumulator_ += delta_time;
        if (schedule_check_accumulator_ >= 1.0f)
        {
            schedule_check_accumulator_ = 0.0f;
            if (config_.enabled)
            {
                if (auto mode = check_schedule())
                {
                    auto result = start_heldenian(*mode);
                    if (result.is_ok())
                    {
                        LOG_INFO(general,
                                 "Scheduled heldenian started (war_id={}, mode={})",
                                 result.value().value,
                                 static_cast<int>(*mode));
                    }
                }
            }
        }
    }
}

void heldenian_system::set_dependencies(war_system* war,
                                        player::player_system* players,
                                        world::world_subsystem* world,
                                        npc::npc_system* npcs,
                                        scheduler* sched)
{
    war_ = war;
    players_ = players;
    world_ = world;
    npcs_ = npcs;
    scheduler_ = sched;
}

void heldenian_system::set_config(const heldenian_config& config)
{
    config_ = config;
    LOG_INFO(general,
             "Heldenian config loaded: {} schedule entries, {} aresden towers, {} elvine towers, {} doors",
             config_.schedule.size(),
             config_.aresden_towers.size(),
             config_.elvine_towers.size(),
             config_.defender_doors.size());
}

// ========== Lifecycle ==========

auto heldenian_system::start_heldenian(heldenian_mode mode, war_faction defender) -> result<war_id, heldenian_result>
{
    if (active_)
    {
        return result<war_id, heldenian_result>::err(heldenian_result::already_active);
    }

    // Resolve defending faction
    if (defender == war_faction::neutral)
    {
        defending_faction_ = config_.default_defender;
    }
    else
    {
        defending_faction_ = defender;
    }

    war_id wid{};
    if (war_)
    {
        auto war_result = war_->start_war(war_type::heldenian);
        if (war_result.is_err())
        {
            return result<war_id, heldenian_result>::err(heldenian_result::config_error);
        }
        wid = war_result.value();
    }

    active_ = true;
    current_mode_ = mode;
    current_war_id_ = wid;
    elapsed_seconds_ = 0;
    status_broadcast_accumulator_ = 0.0f;
    aresden_deaths_ = 0;
    elvine_deaths_ = 0;
    player_data_.clear();

    reset_objectives();

    // Validate objectives are not empty (before spawning NPCs)
    if (mode == heldenian_mode::tower_defense)
    {
        if (aresden_objectives_.empty() && elvine_objectives_.empty())
        {
            active_ = false;
            if (war_ && wid.is_valid())
                war_->cancel_war(wid);
            return result<war_id, heldenian_result>::err(heldenian_result::config_error);
        }
    }
    else
    {
        // Door defense: the defending faction must have doors
        auto& defender_objs = (defending_faction_ == war_faction::aresden) ? aresden_objectives_ : elvine_objectives_;
        if (defender_objs.empty())
        {
            active_ = false;
            if (war_ && wid.is_valid())
                war_->cancel_war(wid);
            return result<war_id, heldenian_result>::err(heldenian_result::config_error);
        }
    }

    // Evacuate players from war maps before spawning
    if (mode == heldenian_mode::tower_defense)
        evacuate_map(config_.tower_map);
    else
        evacuate_map(config_.door_map);

    spawn_objective_npcs();

    LOG_INFO(general,
             "Heldenian started (war_id={}, mode={}, defender={})",
             wid.value,
             static_cast<int>(mode),
             static_cast<int>(defending_faction_));

    broadcast_heldenian_started();

    return result<war_id, heldenian_result>::ok(wid);
}

auto heldenian_system::end_heldenian(war_faction winner) -> heldenian_result
{
    if (!active_)
    {
        return heldenian_result::not_active;
    }

    LOG_INFO(general,
             "Heldenian ending. Winner: faction {} (elapsed: {}s, mode={})",
             static_cast<int>(winner),
             elapsed_seconds_,
             static_cast<int>(current_mode_));

    // Track winner for tiebreaker in future wars
    if (winner != war_faction::neutral)
    {
        last_winner_ = winner;
    }

    broadcast_heldenian_ended(winner);

    if (war_ && current_war_id_.is_valid())
    {
        war_->end_war(current_war_id_);
    }

    cleanup_heldenian();
    return heldenian_result::success;
}

void heldenian_system::cancel_heldenian()
{
    if (!active_)
        return;

    LOG_INFO(general, "Heldenian cancelled");

    if (war_ && current_war_id_.is_valid())
    {
        war_->cancel_war(current_war_id_);
    }

    cleanup_heldenian();
}

// ========== Objectives ==========

auto heldenian_system::get_objectives(war_faction faction) const -> const std::vector<heldenian_objective>&
{
    static const std::vector<heldenian_objective> empty;
    switch (faction)
    {
    case war_faction::aresden:
        return aresden_objectives_;
    case war_faction::elvine:
        return elvine_objectives_;
    default:
        return empty;
    }
}

auto heldenian_system::damage_objective(war_faction faction, uint16_t obj_id, int32_t damage) -> bool
{
    auto& objs = (faction == war_faction::aresden) ? aresden_objectives_ : elvine_objectives_;

    for (auto& obj : objs)
    {
        if (obj.id == obj_id && !obj.is_destroyed())
        {
            obj.hp = std::max(0, obj.hp - damage);
            LOG_DEBUG(general,
                      "Heldenian objective {}/{} took {} damage, hp={}/{}",
                      static_cast<int>(faction),
                      obj_id,
                      damage,
                      obj.hp,
                      obj.max_hp);

            broadcast_objective_update(faction);
            check_victory_condition();
            return true;
        }
    }
    return false;
}

auto heldenian_system::all_objectives_destroyed(war_faction faction) const -> bool
{
    const auto& objs = (faction == war_faction::aresden) ? aresden_objectives_ : elvine_objectives_;
    return std::all_of(objs.begin(), objs.end(), [](const heldenian_objective& obj) { return obj.is_destroyed(); });
}

auto heldenian_system::count_surviving(war_faction faction) const -> int32_t
{
    const auto& objs = (faction == war_faction::aresden) ? aresden_objectives_ : elvine_objectives_;
    return static_cast<int32_t>(
        std::count_if(objs.begin(), objs.end(), [](const heldenian_objective& obj) { return !obj.is_destroyed(); }));
}

void heldenian_system::on_npc_killed(entity::entity eid)
{
    if (!active_ || !eid.is_valid())
        return;

    auto check_objectives = [&](std::vector<heldenian_objective>& objs) -> bool
    {
        for (auto& obj : objs)
        {
            if (obj.eid == eid && !obj.is_destroyed())
            {
                obj.hp = 0;
                LOG_INFO(general, "Heldenian objective {} destroyed via NPC death (eid={})", obj.id, eid.id);
                broadcast_objective_update(obj.faction);
                return true;
            }
        }
        return false;
    };

    if (check_objectives(aresden_objectives_) || check_objectives(elvine_objectives_))
    {
        check_victory_condition();
    }
}

// ========== Teleportation ==========

auto heldenian_system::get_teleport_destination(war_faction player_faction) const
    -> std::optional<heldenian_teleport_coords>
{
    if (!active_ || player_faction == war_faction::neutral)
        return std::nullopt;

    if (current_mode_ == heldenian_mode::tower_defense)
    {
        if (player_faction == war_faction::aresden)
            return config_.tower_aresden_spawn;
        if (player_faction == war_faction::elvine)
            return config_.tower_elvine_spawn;
    }
    else if (current_mode_ == heldenian_mode::door_defense)
    {
        if (player_faction == defending_faction_)
            return config_.door_defender_spawn;
        else
            return config_.door_attacker_spawn;
    }

    return std::nullopt;
}

// ========== Evacuation ==========

void heldenian_system::evacuate_map(const std::string& map_name)
{
    if (!players_ || !evacuate_fn_)
        return;

    // Resolve map name to map_id via world subsystem
    map_id mid{};
    if (world_)
    {
        auto* m = world_->get_map_by_name(map_name);
        if (m)
            mid = m->id();
    }

    if (!mid.is_valid())
        return;

    std::vector<player_id> to_evacuate;
    players_->for_each_player(
        [&](player_id pid, player::player& plr)
        {
            if (plr.current_map == mid && plr.admin == player::admin_level::player)
            {
                to_evacuate.push_back(pid);
            }
        });

    for (auto pid : to_evacuate)
    {
        evacuate_fn_(pid, map_name);
    }

    if (!to_evacuate.empty())
    {
        LOG_INFO(general, "Evacuated {} players from {} for Heldenian", to_evacuate.size(), map_name);
    }
}

// ========== Player State ==========

auto heldenian_system::join_heldenian(player_id pid, war_faction faction) -> heldenian_result
{
    if (!active_)
    {
        return heldenian_result::not_active;
    }

    if (faction == war_faction::neutral)
    {
        return heldenian_result::not_in_faction;
    }

    if (player_data_.contains(pid))
    {
        return heldenian_result::success; // Already in
    }

    heldenian_player_data data;
    data.pid = pid;
    data.faction = faction;

    // Award initial construction points based on charisma
    if (players_)
    {
        if (auto* plr = players_->get_player(pid))
        {
            data.construction_points = std::min(static_cast<int32_t>(plr->base.charisma) * config_.charisma_multiplier,
                                                config_.max_construction_points);
        }
    }

    player_data_.emplace(pid, data);

    if (war_ && current_war_id_.is_valid() && players_)
    {
        if (auto* plr = players_->get_player(pid))
        {
            war_->join_war(current_war_id_, pid, plr->name, faction);
        }
    }

    return heldenian_result::success;
}

void heldenian_system::leave_heldenian(player_id pid)
{
    player_data_.erase(pid);

    if (war_ && current_war_id_.is_valid())
    {
        war_->leave_war(current_war_id_, pid);
    }
}

auto heldenian_system::get_player_data(player_id pid) -> heldenian_player_data*
{
    auto it = player_data_.find(pid);
    return it != player_data_.end() ? &it->second : nullptr;
}

auto heldenian_system::get_player_data(player_id pid) const -> const heldenian_player_data*
{
    auto it = player_data_.find(pid);
    return it != player_data_.end() ? &it->second : nullptr;
}

auto heldenian_system::is_in_heldenian(player_id pid) const -> bool
{
    return player_data_.contains(pid);
}

// ========== Kill/Death Tracking ==========

void heldenian_system::record_kill(player_id killer, player_id victim, war_faction victim_faction)
{
    if (auto* killer_data = get_player_data(killer))
    {
        killer_data->kills++;
    }
    if (auto* victim_data = get_player_data(victim))
    {
        victim_data->deaths++;
    }

    // Faction-level death counters
    if (victim_faction == war_faction::aresden)
        aresden_deaths_++;
    else if (victim_faction == war_faction::elvine)
        elvine_deaths_++;

    broadcast_status_update();
}

// ========== Schedule ==========

auto heldenian_system::check_schedule() const -> std::optional<heldenian_mode>
{
    if (config_.schedule.empty())
        return std::nullopt;

    auto now = std::chrono::system_clock::now();
    auto time_t_now = std::chrono::system_clock::to_time_t(now);
    std::tm local_tm{};
#ifdef _WIN32
    localtime_s(&local_tm, &time_t_now);
#else
    localtime_r(&time_t_now, &local_tm);
#endif

    for (const auto& entry : config_.schedule)
    {
        if (local_tm.tm_wday == entry.day_of_week && local_tm.tm_hour == entry.hour && local_tm.tm_min == entry.minute)
        {
            return entry.mode;
        }
    }
    return std::nullopt;
}

// ========== Private ==========

void heldenian_system::reset_objectives()
{
    if (current_mode_ == heldenian_mode::tower_defense)
    {
        aresden_objectives_ = config_.aresden_towers;
        elvine_objectives_ = config_.elvine_towers;

        for (auto& obj : aresden_objectives_)
        {
            obj.hp = obj.max_hp;
            obj.faction = war_faction::aresden;
        }
        for (auto& obj : elvine_objectives_)
        {
            obj.hp = obj.max_hp;
            obj.faction = war_faction::elvine;
        }
    }
    else
    {
        // Door defense: doors belong to defending faction
        if (defending_faction_ == war_faction::aresden)
        {
            aresden_objectives_ = config_.defender_doors;
            elvine_objectives_.clear();
            for (auto& obj : aresden_objectives_)
            {
                obj.hp = obj.max_hp;
                obj.faction = war_faction::aresden;
            }
        }
        else
        {
            elvine_objectives_ = config_.defender_doors;
            aresden_objectives_.clear();
            for (auto& obj : elvine_objectives_)
            {
                obj.hp = obj.max_hp;
                obj.faction = war_faction::elvine;
            }
        }
    }
}

void heldenian_system::broadcast_heldenian_started()
{
    nlohmann::json data;
    data["war_id"] = current_war_id_.value;
    data["mode"] = static_cast<int>(current_mode_);

    network::json_message msg;
    msg.type = network::json_message_type::heldenian_started;
    msg.data = std::move(data);

    send_to_all(msg);
}

void heldenian_system::broadcast_heldenian_ended(war_faction winner)
{
    nlohmann::json data;
    data["war_id"] = current_war_id_.value;
    data["winner"] = static_cast<int>(winner);
    data["mode"] = static_cast<int>(current_mode_);
    data["elapsed_s"] = elapsed_seconds_;

    network::json_message msg;
    msg.type = network::json_message_type::heldenian_ended;
    msg.data = std::move(data);

    send_to_all(msg);
}

void heldenian_system::broadcast_status_update()
{
    nlohmann::json data;
    data["active"] = active_;
    data["mode"] = static_cast<int>(current_mode_);
    data["elapsed_s"] = elapsed_seconds_;
    data["aresden_surviving"] = count_surviving(war_faction::aresden);
    data["elvine_surviving"] = count_surviving(war_faction::elvine);
    data["aresden_deaths"] = aresden_deaths_;
    data["elvine_deaths"] = elvine_deaths_;

    network::json_message msg;
    msg.type = network::json_message_type::heldenian_status_update;
    msg.data = std::move(data);

    send_to_war_map(msg);
}

void heldenian_system::broadcast_objective_update(war_faction faction)
{
    const auto& objs = (faction == war_faction::aresden) ? aresden_objectives_ : elvine_objectives_;

    nlohmann::json data;
    data["faction"] = static_cast<int>(faction);

    nlohmann::json obj_array = nlohmann::json::array();
    for (const auto& obj : objs)
    {
        obj_array.push_back({{"id", obj.id}, {"hp", obj.hp}, {"max_hp", obj.max_hp}});
    }
    data["objectives"] = std::move(obj_array);

    network::json_message msg;
    msg.type = network::json_message_type::heldenian_status_update;
    msg.data = std::move(data);

    send_to_war_map(msg);
}

void heldenian_system::check_victory_condition()
{
    if (!active_)
        return;

    if (current_mode_ == heldenian_mode::tower_defense)
    {
        // If all of a faction's towers are destroyed, the other faction wins
        if (all_objectives_destroyed(war_faction::aresden))
        {
            LOG_INFO(general, "All Aresden towers destroyed - Elvine wins Heldenian!");
            end_heldenian(war_faction::elvine);
        }
        else if (all_objectives_destroyed(war_faction::elvine))
        {
            LOG_INFO(general, "All Elvine towers destroyed - Aresden wins Heldenian!");
            end_heldenian(war_faction::aresden);
        }
    }
    else
    {
        // Door defense: if all defender's doors destroyed, attacker wins
        if (all_objectives_destroyed(defending_faction_))
        {
            auto attacker = (defending_faction_ == war_faction::aresden) ? war_faction::elvine : war_faction::aresden;
            LOG_INFO(
                general, "All doors destroyed - attacker (faction {}) wins Heldenian!", static_cast<int>(attacker));
            end_heldenian(attacker);
        }
    }
}

void heldenian_system::spawn_objective_npcs()
{
    if (!npcs_)
        return;

    auto spawn_for_objectives = [&](std::vector<heldenian_objective>& objs, const std::string& map_name)
    {
        map_id mid{};
        if (world_)
        {
            auto* m = world_->get_map_by_name(map_name);
            if (m)
                mid = m->id();
        }

        for (auto& obj : objs)
        {
            if (obj.npc_type == 0)
                continue;

            auto spawn_result = npcs_->spawn_npc(npc_id(obj.npc_type), mid, {obj.x, obj.y});
            if (spawn_result.is_ok())
            {
                obj.eid = spawn_result.value();
                LOG_DEBUG(
                    general, "Spawned heldenian NPC type={} at ({},{}) eid={}", obj.npc_type, obj.x, obj.y, obj.eid.id);
            }
        }
    };

    if (current_mode_ == heldenian_mode::tower_defense)
    {
        spawn_for_objectives(aresden_objectives_, config_.tower_map);
        spawn_for_objectives(elvine_objectives_, config_.tower_map);
    }
    else
    {
        auto& objs = (defending_faction_ == war_faction::aresden) ? aresden_objectives_ : elvine_objectives_;
        spawn_for_objectives(objs, config_.door_map);
    }
}

void heldenian_system::despawn_objective_npcs()
{
    if (!npcs_)
        return;

    auto despawn = [&](std::vector<heldenian_objective>& objs)
    {
        for (auto& obj : objs)
        {
            if (obj.eid.is_valid() && !obj.is_destroyed())
            {
                npcs_->despawn_npc(obj.eid);
            }
        }
    };

    despawn(aresden_objectives_);
    despawn(elvine_objectives_);
}

void heldenian_system::cleanup_heldenian()
{
    despawn_objective_npcs();
    active_ = false;
    current_war_id_ = war_id{};
    elapsed_seconds_ = 0;
    status_broadcast_accumulator_ = 0.0f;
    player_data_.clear();
    aresden_objectives_.clear();
    elvine_objectives_.clear();
}

void heldenian_system::send_to_player(player_id pid, const network::json_message& msg)
{
    if (broadcast_fn_)
    {
        broadcast_fn_(pid, msg);
    }
}

void heldenian_system::send_to_all(const network::json_message& msg)
{
    if (broadcast_all_fn_)
    {
        broadcast_all_fn_(msg);
    }
}

void heldenian_system::send_to_war_map(const network::json_message& msg)
{
    if (!players_ || !world_ || !broadcast_fn_)
    {
        // Fallback to broadcast to all if dependencies unavailable
        send_to_all(msg);
        return;
    }

    std::string war_map = (current_mode_ == heldenian_mode::tower_defense) ? config_.tower_map : config_.door_map;

    map_id mid{};
    auto* m = world_->get_map_by_name(war_map);
    if (m)
        mid = m->id();

    if (!mid.is_valid())
    {
        send_to_all(msg);
        return;
    }

    players_->for_each_player(
        [&](player_id pid, player::player& plr)
        {
            if (plr.current_map == mid)
            {
                send_to_player(pid, msg);
            }
        });
}

} // namespace hb::war
