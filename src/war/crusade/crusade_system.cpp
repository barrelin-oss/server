// crusade_system.cpp
// Crusade warfare subsystem implementation

#include "war/crusade/crusade_system.h"
#include "platform/posix_time_compat.h"
#include "war/war_system.h"
#include "war/war_persistence.h"
#include "war/force_recall/force_recall_system.h"
#include "player/player_system.h"
#include "player/player.h"
#include "world/world_subsystem.h"
#include "world/map.h"
#include "npc/npc_system.h"
#include "social/social_system.h"
#include "social/guild.h"
#include "effect/effect_system.h"
#include "scheduler/scheduler.h"
#include "network/json_protocol.h"
#include "core/logger.h"

#include <algorithm>
#include <chrono>
#include <ctime>
#include <random>
#include <unordered_set>

namespace hb::war
{

crusade_system::crusade_system() = default;

crusade_system::~crusade_system()
{
    if (is_initialized())
    {
        shutdown();
    }
}

void crusade_system::initialize()
{
    LOG_INFO(general, "Crusade system initializing...");

    // Restore last crusade winner and advantage from DB (legacy: bReadCrusadeGUIDFile)
    if (persistence_)
    {
        auto winner_result = persistence_->load_last_winner(war_type::crusade);
        if (winner_result.is_ok())
        {
            last_crusade_winner_ = winner_result.value();
            LOG_INFO(general, "Loaded last crusade winner: faction {}", static_cast<int>(last_crusade_winner_));
        }

        auto advantage_result = persistence_->load_crusade_advantage();
        if (advantage_result.is_ok())
        {
            crusade_advantage_ = advantage_result.value();
            LOG_INFO(general, "Loaded crusade advantage: {}", crusade_advantage_);
        }
    }

    set_initialized(true);
    LOG_INFO(general, "Crusade system initialized");
}

void crusade_system::shutdown()
{
    LOG_INFO(general, "Crusade system shutting down...");

    if (active_)
    {
        cleanup_crusade();
    }

    player_data_.clear();
    set_initialized(false);
    LOG_INFO(general, "Crusade system shutdown complete");
}

void crusade_system::update(float delta_time)
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

        // Mana collection tick
        tick_mana(delta_time);

        // Construction point transfer tick
        tick_construction_transfer(delta_time);

        // Check time limit
        if (elapsed_seconds_ >= config_.timing.duration_seconds)
        {
            LOG_INFO(general, "Crusade time limit reached after {} seconds", elapsed_seconds_);
            // Determine winner by remaining strike point HP
            int32_t aresden_hp = 0;
            int32_t elvine_hp = 0;
            for (const auto& sp : aresden_strike_points_)
            {
                aresden_hp += std::max(0, sp.hp);
            }
            for (const auto& sp : elvine_strike_points_)
            {
                elvine_hp += std::max(0, sp.hp);
            }

            // The faction with MORE surviving HP wins (their points survived)
            // If Aresden's strike points have more HP, Aresden defended better
            if (aresden_hp > elvine_hp)
            {
                end_crusade(war_faction::aresden);
            }
            else if (elvine_hp > aresden_hp)
            {
                end_crusade(war_faction::elvine);
            }
            else
            {
                end_crusade(war_faction::neutral); // Draw
            }
        }
    }
    else
    {
        // Check schedule periodically (every ~1 second)
        schedule_check_accumulator_ += delta_time;
        if (schedule_check_accumulator_ >= 1.0f)
        {
            schedule_check_accumulator_ = 0.0f;
            if (config_.enabled && check_schedule())
            {
                auto result = start_crusade();
                if (result.is_ok())
                {
                    LOG_INFO(general, "Scheduled crusade started (war_id={})", result.value().value);
                }
            }
        }
    }
}

void crusade_system::set_dependencies(war_system* war,
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

void crusade_system::set_config(const crusade_config& config)
{
    config_ = config;
    LOG_INFO(general,
             "Crusade config loaded: {} schedule entries, {} aresden strike points, {} elvine strike points",
             config_.schedule.size(),
             config_.aresden_strike_points.size(),
             config_.elvine_strike_points.size());
}

// ========== Lifecycle ==========

auto crusade_system::start_crusade() -> result<war_id, crusade_result>
{
    if (active_)
    {
        return result<war_id, crusade_result>::err(crusade_result::already_active);
    }

    // Duplicate-in-day prevention (legacy: m_iLatestCrusadeDayOfWeek)
    {
        auto now = std::chrono::system_clock::now();
        auto time_t_now = std::chrono::system_clock::to_time_t(now);
        std::tm local_tm{};
#ifdef _WIN32
        localtime_s(&local_tm, &time_t_now);
#else
        localtime_r(&time_t_now, &local_tm);
#endif
        if (last_crusade_day_ == static_cast<int8_t>(local_tm.tm_wday))
        {
            LOG_DEBUG(general, "Skipping crusade — already had one today (day {})", local_tm.tm_wday);
            return result<war_id, crusade_result>::err(crusade_result::duplicate_today);
        }
        last_crusade_day_ = static_cast<int8_t>(local_tm.tm_wday);
    }

    // Start war in the generic war_system
    war_id wid{};
    if (war_)
    {
        auto war_result = war_->start_war(war_type::crusade);
        if (war_result.is_err())
        {
            LOG_ERROR(general, "Failed to start crusade in war_system: {}", static_cast<int>(war_result.error()));
            return result<war_id, crusade_result>::err(crusade_result::config_error);
        }
        wid = war_result.value();
    }

    active_ = true;
    current_war_id_ = wid;
    crusade_guid_ = next_guid_++;
    elapsed_seconds_ = 0;
    status_broadcast_accumulator_ = 0.0f;
    player_data_.clear();

    // Reset strike points from config
    reset_strike_points();

    LOG_INFO(general, "Crusade started (war_id={}, guid={})", wid.value, crusade_guid_);

    // Spawn initial structures from config
    for (const auto& is : config_.initial_structures)
    {
        if (npcs_ && world_)
        {
            auto* m = world_->get_map_by_name(is.map_name);
            if (m)
            {
                auto npc_result = npcs_->spawn_npc(npc_id(is.npc_type), m->id(), {is.x, is.y});
                if (npc_result.is_ok())
                {
                    war_structure_instance ws;
                    ws.eid = npc_result.value();
                    ws.type = static_cast<war_unit_type>(0); // pre-placed, not player-summonable type
                    ws.faction = is.faction;
                    ws.map_name = is.map_name;
                    ws.x = is.x;
                    ws.y = is.y;

                    // Map NPC type to war_unit_type if possible
                    switch (is.npc_type)
                    {
                    case 36:
                        ws.type = war_unit_type::agt;
                        break;
                    case 37:
                        ws.type = war_unit_type::cgt;
                        break;
                    case 38:
                        ws.type = war_unit_type::mana_collector;
                        break;
                    case 39:
                        ws.type = war_unit_type::detector;
                        break;
                    case 40:
                        ws.type = war_unit_type::esg;
                        break;
                    default:
                        break;
                    }

                    war_structures_.push_back(ws);
                    LOG_DEBUG(general,
                              "Spawned initial structure NPC type {} at {}({},{}) for faction {}",
                              is.npc_type,
                              is.map_name,
                              is.x,
                              is.y,
                              static_cast<int>(is.faction));
                }
            }
        }
    }

    broadcast_crusade_started();
    broadcast_strike_point_update(war_faction::aresden);
    broadcast_strike_point_update(war_faction::elvine);

    // Set up mana and meteor subsystems
    setup_mana_and_meteor();

    // Apply crusade advantage: dominant faction needs more mana per GMG charge
    // Legacy formula: gmg_threshold_bonus = 8 * abs(advantage)
    if (crusade_advantage_ != 0)
    {
        int32_t bonus = 8 * std::abs(static_cast<int>(crusade_advantage_));
        int32_t aresden_adj = (crusade_advantage_ > 0) ? bonus : 0;
        int32_t elvine_adj = (crusade_advantage_ < 0) ? bonus : 0;
        mana_system_.set_threshold_adjustments(aresden_adj, elvine_adj);
    }

    // Force recall players in enemy territory (legacy: m_iTimeLeft_ForceRecall = 0)
    if (force_recall_ && players_ && world_)
    {
        players_->for_each_player(
            [&](player_id pid, player::player& plr)
            {
                if (plr.faction == hb::faction::neutral)
                    return;

                war_faction plr_faction =
                    (plr.faction == hb::faction::aresden) ? war_faction::aresden : war_faction::elvine;

                auto* m = world_->get_map(plr.current_map);
                if (!m)
                    return;

                std::string map_name_str(m->name());
                war_faction map_faction = war_faction::neutral;

                // Determine map faction from name
                if (map_name_str.starts_with("aresden"))
                    map_faction = war_faction::aresden;
                else if (map_name_str.starts_with("elvine"))
                    map_faction = war_faction::elvine;
                else if (force_recall_system::is_building_map(map_name_str))
                    map_faction = force_recall_system::get_building_faction(map_name_str);

                if (map_faction != war_faction::neutral && map_faction != plr_faction)
                {
                    force_recall_->check_player_territory(pid, plr_faction, map_faction, {});
                }
            });
    }

    return result<war_id, crusade_result>::ok(wid);
}

auto crusade_system::end_crusade(war_faction winner) -> crusade_result
{
    if (!active_)
    {
        return crusade_result::not_active;
    }

    LOG_INFO(general, "Crusade ending. Winner: faction {} (elapsed: {}s)", static_cast<int>(winner), elapsed_seconds_);

    // Track winner for next crusade's commander bonus
    last_crusade_winner_ = winner;

    // Adjust crusade advantage toward winner (legacy: m_iCrusadeAdvantage)
    if (winner == war_faction::aresden)
        crusade_advantage_ = std::clamp(static_cast<int8_t>(crusade_advantage_ + 1), int8_t{-5}, int8_t{5});
    else if (winner == war_faction::elvine)
        crusade_advantage_ = std::clamp(static_cast<int8_t>(crusade_advantage_ - 1), int8_t{-5}, int8_t{5});
    // Draw: no change

    broadcast_crusade_ended(winner);

    // Persist war results before cleanup
    persist_war_result(winner);

    // Calculate and send crusade-specific rewards to online participants
    // (Offline players get rewards delivered at login via war_persistence)
    for (const auto& [pid, pdata] : player_data_)
    {
        if (!players_)
            continue;
        auto* plr = players_->get_player(pid);
        if (!plr)
            continue; // Offline — rewards persisted for login delivery

        auto reward = calculate_crusade_reward(pdata.war_contribution, plr->experience.level, pdata.faction, winner);

        // Send reward summary (gold is always 0 for crusade)
        auto msg = network::make_crusade_reward_summary(
            0, static_cast<uint8_t>(winner), pdata.war_contribution, reward.experience, 0, 0);
        send_to_player(pid, msg);

        // Apply EXP
        if (reward.experience > 0)
        {
            players_->add_experience(pid, reward.experience);
        }
    }

    // End in generic war_system
    if (war_ && current_war_id_.is_valid())
    {
        war_->end_war(current_war_id_);
    }

    cleanup_crusade();
    return crusade_result::success;
}

void crusade_system::cancel_crusade()
{
    if (!active_)
        return;

    LOG_INFO(general, "Crusade cancelled");

    if (war_ && current_war_id_.is_valid())
    {
        war_->cancel_war(current_war_id_);
    }

    // Cancel any scheduled crusade tasks
    if (scheduler_)
    {
        scheduler_->cancel_tagged("crusade");
    }

    cleanup_crusade();
}

// ========== Strike Points ==========

auto crusade_system::get_strike_points(war_faction faction) const -> const std::vector<strike_point>&
{
    static const std::vector<strike_point> empty;
    switch (faction)
    {
    case war_faction::aresden:
        return aresden_strike_points_;
    case war_faction::elvine:
        return elvine_strike_points_;
    default:
        return empty;
    }
}

auto crusade_system::damage_strike_point(war_faction faction, uint16_t point_id, int32_t damage) -> bool
{
    auto& points = (faction == war_faction::aresden) ? aresden_strike_points_ : elvine_strike_points_;

    for (auto& sp : points)
    {
        if (sp.id == point_id && !sp.is_destroyed())
        {
            sp.hp = std::max(0, sp.hp - damage);
            LOG_DEBUG(general,
                      "Strike point {}/{} took {} damage, hp={}/{}",
                      static_cast<int>(faction),
                      point_id,
                      damage,
                      sp.hp,
                      sp.max_hp);

            // Disable linked map when strike point is destroyed (legacy: m_bIsDisabled)
            if (sp.is_destroyed() && !sp.linked_map.empty() && world_)
            {
                world_->set_map_disabled(sp.linked_map, true);
                LOG_INFO(general,
                         "Map '{}' disabled — strike point {}/{} destroyed",
                         sp.linked_map,
                         static_cast<int>(faction),
                         point_id);
            }

            broadcast_strike_point_update(faction);
            // Victory check deferred to meteor result phase (legacy: CalcMeteorStrikeEffectHandler)
            return true;
        }
    }
    return false;
}

auto crusade_system::all_strike_points_destroyed(war_faction faction) const -> bool
{
    const auto& points = (faction == war_faction::aresden) ? aresden_strike_points_ : elvine_strike_points_;
    return std::all_of(points.begin(), points.end(), [](const strike_point& sp) { return sp.is_destroyed(); });
}

// ========== Player War State ==========

auto crusade_system::join_crusade(player_id pid, war_faction faction) -> crusade_result
{
    if (!active_)
    {
        return crusade_result::not_active;
    }

    if (faction == war_faction::neutral)
    {
        return crusade_result::not_in_faction;
    }

    if (player_data_.contains(pid))
    {
        return crusade_result::success; // Already in, no-op
    }

    crusade_player_data data;
    data.pid = pid;
    data.faction = faction;
    data.crusade_guid = crusade_guid_;

    // Capture character_id and level now — player may be offline at crusade end
    if (players_)
    {
        if (auto* plr = players_->get_player(pid))
        {
            data.character_id = static_cast<int32_t>(plr->character_id.value);
            data.level = plr->experience.level;
        }
    }

    player_data_.emplace(pid, data);

    // Also register in generic war_system
    if (war_ && current_war_id_.is_valid() && players_)
    {
        if (auto* plr = players_->get_player(pid))
        {
            war_->join_war(current_war_id_, pid, plr->name, faction);
        }
    }

    return crusade_result::success;
}

void crusade_system::leave_crusade(player_id pid)
{
    player_data_.erase(pid);

    if (war_ && current_war_id_.is_valid())
    {
        war_->leave_war(current_war_id_, pid);
    }
}

auto crusade_system::select_duty(player_id pid, crusade_duty duty) -> crusade_result
{
    if (!active_)
    {
        return crusade_result::not_active;
    }

    auto* data = get_player_data(pid);
    if (!data)
    {
        return crusade_result::not_in_crusade;
    }

    if (data->duty != crusade_duty::none)
    {
        return crusade_result::already_has_duty;
    }

    if (duty == crusade_duty::none)
    {
        return crusade_result::invalid_duty;
    }

    // Commander requires guild master rank (when social system is available)
    if (duty == crusade_duty::commander && social_)
    {
        auto gid = social_->get_player_guild(pid);
        if (!gid.is_valid())
            return crusade_result::not_guild_master;

        auto* g = social_->get_guild(gid);
        if (!g)
            return crusade_result::not_guild_master;

        auto* member = g->get_member(pid);
        if (!member || member->rank != social::guild_rank::guild_master)
        {
            return crusade_result::not_guild_master;
        }
    }

    data->duty = duty;

    // Award bonus construction points to commanders of the previous winning faction
    if (duty == crusade_duty::commander && last_crusade_winner_ == data->faction &&
        last_crusade_winner_ != war_faction::neutral)
    {
        data->construction_points += config_.construction.commander_bonus_points;
        data->construction_points = std::min(data->construction_points, max_construction_points);
    }

    LOG_DEBUG(general, "Player {} selected duty {} in crusade", pid.value, static_cast<int>(duty));

    return crusade_result::success;
}

auto crusade_system::get_player_data(player_id pid) -> crusade_player_data*
{
    auto it = player_data_.find(pid);
    return it != player_data_.end() ? &it->second : nullptr;
}

auto crusade_system::get_player_data(player_id pid) const -> const crusade_player_data*
{
    auto it = player_data_.find(pid);
    return it != player_data_.end() ? &it->second : nullptr;
}

auto crusade_system::is_in_crusade(player_id pid) const -> bool
{
    return player_data_.contains(pid);
}

// ========== Construction Points ==========

void crusade_system::award_construction_points(player_id pid, int32_t points)
{
    if (auto* data = get_player_data(pid))
    {
        data->construction_points = std::min(data->construction_points + points, max_construction_points);
    }
}

void crusade_system::award_contribution(player_id pid, int32_t amount)
{
    if (auto* data = get_player_data(pid))
    {
        data->war_contribution = std::min(data->war_contribution + amount, max_war_contribution);
    }

    // Also record in generic war_system
    if (war_ && current_war_id_.is_valid())
    {
        war_->record_damage(current_war_id_, pid, amount);
    }
}

// ========== Schedule ==========

auto crusade_system::check_schedule() const -> bool
{
    if (config_.schedule.empty())
        return false;

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
            return true;
        }
    }
    return false;
}

// ========== Private ==========

void crusade_system::reset_strike_points()
{
    aresden_strike_points_ = config_.aresden_strike_points;
    elvine_strike_points_ = config_.elvine_strike_points;

    // Restore HP to max
    for (auto& sp : aresden_strike_points_)
    {
        sp.hp = sp.max_hp;
        sp.faction = war_faction::aresden;
    }
    for (auto& sp : elvine_strike_points_)
    {
        sp.hp = sp.max_hp;
        sp.faction = war_faction::elvine;
    }
}

void crusade_system::broadcast_crusade_started()
{
    nlohmann::json data;
    data["war_id"] = current_war_id_.value;
    data["crusade_guid"] = crusade_guid_;

    network::json_message msg;
    msg.type = network::json_message_type::crusade_started;
    msg.data = std::move(data);

    send_to_all(msg);
}

void crusade_system::broadcast_crusade_ended(war_faction winner)
{
    nlohmann::json data;
    data["war_id"] = current_war_id_.value;
    data["winner"] = static_cast<int>(winner);

    network::json_message msg;
    msg.type = network::json_message_type::crusade_ended;
    msg.data = std::move(data);

    // Send per-player with their contribution
    for (const auto& [pid, pdata] : player_data_)
    {
        auto player_msg = msg;
        player_msg.data["your_contribution"] = pdata.war_contribution;
        send_to_player(pid, player_msg);
    }
}

void crusade_system::broadcast_status_update()
{
    for (const auto& [pid, pdata] : player_data_)
    {
        nlohmann::json data;
        data["active"] = active_;
        data["elapsed_s"] = elapsed_seconds_;
        data["duty"] = static_cast<int>(pdata.duty);
        data["construction_pts"] = pdata.construction_points;
        data["contribution"] = pdata.war_contribution;

        network::json_message msg;
        msg.type = network::json_message_type::crusade_status_update;
        msg.data = std::move(data);

        send_to_player(pid, msg);
    }
}

void crusade_system::broadcast_strike_point_update(war_faction faction)
{
    const auto& points = (faction == war_faction::aresden) ? aresden_strike_points_ : elvine_strike_points_;

    nlohmann::json data;
    data["faction"] = static_cast<int>(faction);

    nlohmann::json sp_array = nlohmann::json::array();
    for (const auto& sp : points)
    {
        sp_array.push_back({{"id", sp.id}, {"hp", sp.hp}, {"max_hp", sp.max_hp}});
    }
    data["strike_points"] = std::move(sp_array);

    network::json_message msg;
    msg.type = network::json_message_type::crusade_strike_point_update;
    msg.data = std::move(data);

    send_to_all(msg);
}

void crusade_system::check_victory_condition()
{
    if (!active_)
        return;

    // If all of a faction's strike points are destroyed, the OTHER faction wins
    if (all_strike_points_destroyed(war_faction::aresden))
    {
        LOG_INFO(general, "All Aresden strike points destroyed - Elvine wins!");
        end_crusade(war_faction::elvine);
    }
    else if (all_strike_points_destroyed(war_faction::elvine))
    {
        LOG_INFO(general, "All Elvine strike points destroyed - Aresden wins!");
        end_crusade(war_faction::aresden);
    }
}

void crusade_system::persist_war_result(war_faction winner)
{
    if (!persistence_)
        return;
    if (!war_)
        return;

    auto* war = war_->get_war(current_war_id_);
    if (!war)
        return;

    // Build war_result from current state
    war_result wr;
    wr.id = current_war_id_;
    wr.type = war_type::crusade;
    wr.winner = winner;
    wr.draw = (winner == war_faction::neutral);
    wr.aresden_score = war->aresden_score;
    wr.elvine_score = war->elvine_score;
    wr.started_at = war->started_at;
    wr.ended_at = std::chrono::system_clock::now();
    wr.duration_seconds = elapsed_seconds_;
    wr.metadata["crusade_advantage"] = static_cast<int>(crusade_advantage_);

    auto save_result = persistence_->save_war_result(wr);
    if (save_result.is_err())
    {
        LOG_ERROR(general, "Failed to persist war result: {}", save_result.error());
        return;
    }

    int32_t war_db_id = save_result.value();

    // Save each participant
    for (const auto& [pid, pdata] : player_data_)
    {
        // Use character_id captured at join time (works even if player is offline now)
        if (pdata.character_id == 0)
            continue;

        // Get participant stats from war_system
        auto* participant = war->get_participant(pid);
        if (!participant)
            continue;

        // Use level captured at join time, but prefer current level if player is still online
        int32_t level = pdata.level;
        bool is_online = false;
        if (players_)
        {
            auto* plr = players_->get_player(pid);
            if (plr)
            {
                level = plr->experience.level;
                is_online = true;
            }
        }

        auto crusade_rwd = calculate_crusade_reward(pdata.war_contribution, level, pdata.faction, winner);

        war_rewards rewards;
        rewards.experience = crusade_rwd.experience;
        rewards.gold = 0;
        rewards.contribution_points = pdata.war_contribution;

        persistence_->save_participant_with_claimed(
            war_db_id, pdata.character_id, *participant, static_cast<uint8_t>(pdata.duty), rewards, is_online);
    }

    LOG_INFO(general, "Persisted crusade result (db_id={}, participants={})", war_db_id, player_data_.size());
}

void crusade_system::cleanup_crusade()
{
    // Clear force recall timers set during this crusade
    if (force_recall_)
    {
        force_recall_->clear_crusade_trackers();
    }

    // Re-enable maps disabled during crusade
    if (world_)
    {
        for (const auto& sp : aresden_strike_points_)
        {
            if (!sp.linked_map.empty())
                world_->set_map_disabled(sp.linked_map, false);
        }
        for (const auto& sp : elvine_strike_points_)
        {
            if (!sp.linked_map.empty())
                world_->set_map_disabled(sp.linked_map, false);
        }
    }

    active_ = false;
    current_war_id_ = war_id{};
    elapsed_seconds_ = 0;
    status_broadcast_accumulator_ = 0.0f;
    mana_tick_accumulator_ = 0.0f;
    mana_broadcast_accumulator_ = 0.0f;
    construction_transfer_accumulator_ = 0.0f;
    player_data_.clear();
    guild_construct_locations_.clear();
    guild_teleport_locations_.clear();
    aresden_strike_points_.clear();
    elvine_strike_points_.clear();

    // Remove war structure NPCs — mobile units are killed (triggers death/loot),
    // fixed structures are silently despawned (legacy: RemoveCrusadeStructures vs RemoveCrusadeNpcs)
    if (npcs_)
    {
        for (const auto& ws : war_structures_)
        {
            if (ws.eid.is_valid())
            {
                if (is_mobile_unit_type(ws.type))
                {
                    npcs_->kill_npc(ws.eid, entity::entity{});
                }
                else
                {
                    npcs_->despawn_npc(ws.eid);
                }
            }
        }
    }
    war_structures_.clear();

    // Flush deferred NPC deaths from war unit kills
    if (npcs_)
        npcs_->flush_pending_deaths();

    // Clean up mana and meteor state
    mana_system_.reset();
    meteor_handler_.cancel_all();

    if (scheduler_)
    {
        scheduler_->cancel_tagged("crusade");
    }
}

void crusade_system::send_to_player(player_id pid, const network::json_message& msg)
{
    if (broadcast_fn_)
    {
        broadcast_fn_(pid, msg);
    }
}

void crusade_system::send_to_all(const network::json_message& msg)
{
    if (broadcast_all_fn_)
    {
        broadcast_all_fn_(msg);
    }
}

void crusade_system::setup_mana_and_meteor()
{
    mana_system_.reset();
    mana_system_.initialize_stones(config_.mana_stone_count);
    meteor_handler_.set_scheduler(scheduler_);

    // Wire meteor trigger: when mana system accumulates enough, fire meteor
    mana_system_.set_meteor_trigger([this](war_faction attacking_faction)
                                    { meteor_handler_.fire_meteor(attacking_faction); });

    // Wire meteor callbacks
    meteor_callbacks cbs;

    cbs.get_esg_count = [this](war_faction faction, uint16_t point_id) -> int32_t
    {
        const auto& points = get_strike_points(faction);
        const strike_point* sp = nullptr;
        for (const auto& p : points)
        {
            if (p.id == point_id)
            {
                sp = &p;
                break;
            }
        }
        if (!sp)
            return 0;

        int32_t radius = meteor_handler_.get_config().esg_protection_radius;
        int32_t count = 0;
        for (const auto& ws : war_structures_)
        {
            if (ws.type == war_unit_type::esg && ws.faction == faction && ws.map_name == sp->map_name)
            {
                // Legacy uses rectangular (Manhattan-style) scan, not Euclidean
                if (std::abs(ws.x - sp->x) <= radius && std::abs(ws.y - sp->y) <= radius)
                {
                    count++;
                }
            }
        }
        return count;
    };

    cbs.damage_strike_point = [this](war_faction faction, uint16_t point_id, int32_t damage) -> bool
    {
        return damage_strike_point(faction, point_id, damage);
    };

    cbs.get_strike_points = [this](war_faction faction) -> std::vector<strike_point>
    {
        return get_strike_points(faction);
    };

    cbs.broadcast_warning = [this](war_faction target, int32_t time_ms)
    {
        broadcast_meteor_warning(target, time_ms);
    };

    cbs.broadcast_result = [this](const meteor_event_result& result)
    {
        broadcast_meteor_result(result);

        // Check victory AFTER the full meteor sequence completes (legacy: CalcMeteorStrikeEffectHandler)
        check_victory_condition();
    };

    cbs.damage_players = [this](war_faction target, int32_t /*wave*/) -> int32_t
    {
        if (!players_)
            return 0;
        int32_t casualties = 0;

        // Get maps belonging to target faction
        const auto& target_points = get_strike_points(target);
        if (target_points.empty())
            return 0;

        std::unordered_set<std::string> target_maps;
        for (const auto& sp : target_points)
        {
            target_maps.insert(sp.map_name);
        }

        players_->for_each_player(
            [&](player_id /*pid*/, player::player& plr)
            {
                // Only damage players on target faction maps
                if (world_)
                {
                    auto* m = world_->get_map(plr.current_map);
                    if (!m || target_maps.find(std::string(m->name())) == target_maps.end())
                        return;
                }

                // Admin immunity
                if (plr.admin != player::admin_level::player)
                    return;

                int32_t damage = meteor_handler_.calculate_player_damage(plr.experience.level);

                // Magic protection reduces meteor damage
                // Protection From Magic (magnitude 2): damage = damage/2 - 2
                // Absolute Magic Protection (magnitude 5): damage = 0
                if (effects_ && plr.ecs_entity.id != 0)
                {
                    auto* effs = effects_->get_effects(plr.ecs_entity);
                    if (effs)
                    {
                        for (const auto& eff : *effs)
                        {
                            if (eff.group == magic_type::protection)
                            {
                                if (eff.magnitude >= 5)
                                {
                                    damage = 0;
                                }
                                else if (eff.magnitude >= 2)
                                {
                                    damage = std::max(0, damage / 2 - 2);
                                }
                                break;
                            }
                        }
                    }
                }

                // TODO: equipment magic resistance/absorption when equipment stats are implemented

                plr.hp -= damage;
                if (plr.hp <= 0)
                {
                    plr.hp = 0;
                    casualties++;
                }
                else if (damage > 0)
                {
                    // Break hold/paralyze effects on surviving players (legacy: DEF_MAGICTYPE_HOLDOBJECT)
                    if (effects_ && plr.ecs_entity.id != 0)
                    {
                        effects_->remove_effects_by_group(plr.ecs_entity, magic_type::hold_paralyze);
                    }
                }
            });

        return casualties;
    };

    meteor_handler_.set_callbacks(std::move(cbs));
}

void crusade_system::tick_mana(float delta_time)
{
    mana_tick_accumulator_ += delta_time;

    auto interval = mana_system_.get_config().tick_interval_seconds;
    if (mana_tick_accumulator_ < interval)
        return;

    mana_tick_accumulator_ -= interval;

    int32_t aresden_collectors = count_structures_by_type(war_faction::aresden, war_unit_type::mana_collector);
    int32_t elvine_collectors = count_structures_by_type(war_faction::elvine, war_unit_type::mana_collector);
    mana_system_.tick(aresden_collectors, elvine_collectors);

    // MP restoration: collectors restore MP to nearby allied players
    if (players_ && world_)
    {
        for (const auto& ws : war_structures_)
        {
            if (ws.type != war_unit_type::mana_collector)
                continue;

            auto* m = world_->get_map_by_name(ws.map_name);
            if (!m)
                continue;

            int32_t radius = mana_system_.get_config().collector_mp_restore_radius;

            // Get all players who can see the collector (for broadcast)
            auto viewers = players_->get_players_who_can_see(m->id(), {ws.x, ws.y});

            for (auto viewer_pid : viewers)
            {
                auto* plr = players_->get_player(viewer_pid);
                if (!plr)
                    continue;

                int32_t restore = 0;

                // Check if viewer is within restore radius AND same faction
                bool in_range = std::abs(plr->pos.x - ws.x) <= radius && std::abs(plr->pos.y - ws.y) <= radius;

                war_faction plr_faction = war_faction::neutral;
                if (plr->faction == hb::faction::aresden)
                    plr_faction = war_faction::aresden;
                else if (plr->faction == hb::faction::elvine)
                    plr_faction = war_faction::elvine;

                if (in_range && plr_faction == ws.faction && plr->computed.magic > 0)
                {
                    // Each MP restore costs 1 mana from the faction pool (legacy: m_iCollectedMana)
                    if (mana_system_.try_consume(ws.faction, 1))
                    {
                        thread_local std::mt19937 rng{std::random_device{}()};
                        std::uniform_int_distribution<int32_t> dist(1, plr->computed.magic);
                        restore = dist(rng);
                        // Add equipment MP regen bonus (legacy: m_iAddMP)
                        restore += std::max(0, plr->computed.mp_regen);
                        plr->mp = std::min(plr->mp + restore, plr->computed.max_mp);
                    }
                }

                // Broadcast to ALL viewers (even those who get 0)
                auto msg = network::make_crusade_mp_restore(ws.x, ws.y, radius, restore);
                send_to_player(viewer_pid, msg);
            }
        }
    }

    // Periodic mana broadcast to commanders
    mana_broadcast_accumulator_ += interval;
    if (mana_broadcast_accumulator_ >= 10.0f) // Every 10 seconds
    {
        mana_broadcast_accumulator_ = 0.0f;
        broadcast_mana_update();
    }
}

void crusade_system::broadcast_mana_update()
{
    nlohmann::json data;
    data["aresden_mana"] = mana_system_.aresden_mana();
    data["elvine_mana"] = mana_system_.elvine_mana();

    network::json_message msg;
    msg.type = network::json_message_type::crusade_mana_update;
    msg.data = std::move(data);

    // Send only to commanders
    for (const auto& [pid, pdata] : player_data_)
    {
        if (pdata.duty == crusade_duty::commander)
        {
            send_to_player(pid, msg);
        }
    }
}

void crusade_system::broadcast_meteor_warning(war_faction target, int32_t time_ms)
{
    nlohmann::json data;
    data["target_faction"] = static_cast<int>(target);
    data["time_until_impact_ms"] = time_ms;

    network::json_message msg;
    msg.type = network::json_message_type::crusade_meteor_warning;
    msg.data = std::move(data);

    send_to_all(msg);
}

void crusade_system::broadcast_meteor_result(const meteor_event_result& result)
{
    nlohmann::json data;
    data["attacking_faction"] = static_cast<int>(result.attacking_faction);
    data["target_faction"] = static_cast<int>(result.target_faction);
    data["casualties"] = result.player_casualties;
    data["triggered_victory"] = result.triggered_victory;

    nlohmann::json sp_array = nlohmann::json::array();
    for (const auto& sr : result.strike_results)
    {
        sp_array.push_back({{"id", sr.strike_point_id},
                            {"esg_count", sr.esg_count},
                            {"damage", sr.damage_applied},
                            {"destroyed", sr.point_destroyed}});
    }
    data["strike_results"] = std::move(sp_array);

    // Count remaining structures
    int32_t remaining = 0;
    const auto& points = get_strike_points(result.target_faction);
    for (const auto& sp : points)
    {
        if (!sp.is_destroyed())
            ++remaining;
    }
    data["structures_remaining"] = remaining;

    network::json_message msg;
    msg.type = network::json_message_type::crusade_meteor_result;
    msg.data = std::move(data);

    send_to_all(msg);
}

// ========== GMG Damage ==========

void crusade_system::on_gmg_damage(entity::entity eid, int32_t damage)
{
    if (!active_ || damage <= 0)
        return;

    // Find which faction owns this GMG
    for (const auto& ws : war_structures_)
    {
        if (ws.eid == eid)
        {
            mana_system_.apply_gmg_damage(ws.faction, damage);
            return;
        }
    }
}

// ========== Phase 3: Construction Points & Combat ==========

void crusade_system::on_player_kill(player_id killer, player_id victim, int32_t victim_level, int32_t exp_reward)
{
    if (!active_)
        return;

    auto* killer_data = get_player_data(killer);
    if (!killer_data)
        return;

    // All duties earn from kills (legacy behavior)

    // Construction points: victim_level / 2 (legacy formula)
    int32_t points = victim_level / 2;
    points = std::max(points, 1);

    killer_data->construction_points = std::min(killer_data->construction_points + points, max_construction_points);

    // Contribution from kill: (exp_reward - exp_reward/3) * 12 (legacy formula)
    if (exp_reward > 0)
    {
        int32_t contribution = (exp_reward - exp_reward / 3) * 12;
        if (contribution > 0)
        {
            killer_data->war_contribution =
                std::min(killer_data->war_contribution + contribution, max_war_contribution);
        }
    }

    LOG_DEBUG(
        general, "Player {} earned {} construction points (kill, victim level {})", killer.value, points, victim_level);

    broadcast_construction_point_update(killer);

    // Also record kill in war system
    if (war_ && current_war_id_.is_valid())
    {
        war_->record_kill(current_war_id_, killer, victim);
    }
}

void crusade_system::on_npc_kill(player_id killer, int16_t npc_sprite_id)
{
    if (!active_)
        return;

    auto* killer_data = get_player_data(killer);
    if (!killer_data)
        return;

    auto [construction, contribution] = get_npc_kill_rewards(npc_sprite_id);
    if (construction > 0)
    {
        killer_data->construction_points =
            std::min(killer_data->construction_points + construction, max_construction_points);
    }
    if (contribution > 0)
    {
        killer_data->war_contribution = std::min(killer_data->war_contribution + contribution, max_war_contribution);
    }

    if (construction > 0 || contribution > 0)
    {
        LOG_DEBUG(general,
                  "Player {} earned {} construction, {} contribution (NPC kill, sprite {})",
                  killer.value,
                  construction,
                  contribution,
                  npc_sprite_id);
        broadcast_construction_point_update(killer);
    }
}

void crusade_system::on_friendly_npc_kill(player_id killer)
{
    if (!active_)
        return;

    auto* killer_data = get_player_data(killer);
    if (!killer_data)
        return;

    // Penalty: lose all contribution (legacy subtracts 2x, clamped to 0)
    killer_data->war_contribution = 0;

    LOG_WARN(general, "Player {} killed friendly NPC — contribution reset to 0", killer.value);
    broadcast_construction_point_update(killer);
}

auto crusade_system::get_npc_kill_rewards(int16_t npc_sprite_id) -> std::pair<int32_t, int32_t>
{
    // Legacy hardcoded rewards per NPC type (construction, contribution)
    switch (npc_sprite_id)
    {
    case 36:
        return {700, 4000}; // AGT
    case 37:
        return {700, 4000}; // CGT
    case 38:
        return {500, 2000}; // Mana Collector
    case 39:
        return {500, 2000}; // Detector
    case 40:
        return {1500, 5000}; // ESG
    case 41:
        return {5000, 10000}; // GMG
    case 43:
        return {500, 1000}; // LWB
    case 44:
        return {1000, 2000}; // GHK
    case 45:
        return {1500, 3000}; // GHKABS
    case 46:
        return {1000, 2000}; // TK
    case 47:
        return {1500, 3000}; // BG
    case 51:
        return {500, 1500}; // Catapult
    default:
        if (npc_sprite_id >= 1 && npc_sprite_id <= 6)
            return {50, 100};
        return {0, 0};
    }
}

void crusade_system::transfer_construction_points()
{
    if (!active_)
        return;

    for (auto& [pid, data] : player_data_)
    {
        // Only transfer from fighters/constructors who have accumulated points
        if (data.construction_points <= 0)
            continue;
        if (data.duty == crusade_duty::commander)
            continue; // Commanders don't transfer

        auto commander_pid = find_guild_commander(pid);
        if (!commander_pid.is_valid())
            continue;

        auto* commander_data = get_player_data(commander_pid);
        if (!commander_data)
            continue;

        int32_t transferred = data.construction_points;
        commander_data->construction_points =
            std::min(commander_data->construction_points + transferred, max_construction_points);

        // Award contribution to COMMANDER (1/10 of transferred points) — legacy behavior
        int32_t contribution =
            static_cast<int32_t>(static_cast<float>(transferred) * config_.construction.transfer_contribution_ratio);
        if (contribution > 0)
        {
            commander_data->war_contribution =
                std::min(commander_data->war_contribution + contribution, max_war_contribution);

            if (war_ && current_war_id_.is_valid())
            {
                war_->record_damage(current_war_id_, commander_pid, contribution);
            }
        }

        data.construction_points = 0;

        LOG_DEBUG(general,
                  "Transferred {} construction points from player {} to commander {} (+{} contribution)",
                  transferred,
                  pid.value,
                  commander_pid.value,
                  contribution);

        broadcast_construction_point_update(pid);
        broadcast_construction_point_update(commander_pid);
    }
}

auto crusade_system::find_guild_commander(player_id fighter) -> player_id
{
    if (!social_)
        return player_id{};

    auto guild_id = social_->get_player_guild(fighter);
    if (!guild_id.is_valid())
        return player_id{};

    auto* guild = social_->get_guild(guild_id);
    if (!guild)
        return player_id{};

    // Look for an online guild member who is a commander in this crusade
    for (const auto& member : guild->members)
    {
        if (!member.player.is_valid())
            continue; // Offline
        if (member.player == fighter)
            continue; // Not self

        auto* member_data = get_player_data(member.player);
        if (member_data && member_data->duty == crusade_duty::commander &&
            member_data->faction == get_player_data(fighter)->faction)
        {
            return member.player;
        }
    }

    return player_id{};
}

void crusade_system::tick_construction_transfer(float delta_time)
{
    construction_transfer_accumulator_ += delta_time;

    auto interval = static_cast<float>(config_.construction.transfer_interval_seconds);
    if (construction_transfer_accumulator_ < interval)
        return;

    construction_transfer_accumulator_ -= interval;
    transfer_construction_points();
}

void crusade_system::broadcast_construction_point_update(player_id pid)
{
    auto* data = get_player_data(pid);
    if (!data)
        return;

    nlohmann::json msg_data;
    msg_data["construction_pts"] = data->construction_points;
    msg_data["contribution"] = data->war_contribution;

    network::json_message msg;
    msg.type = network::json_message_type::crusade_construction_point_update;
    msg.data = std::move(msg_data);

    send_to_player(pid, msg);
}

// ========== Guild Construct Location ==========

auto crusade_system::set_guild_construct_location(player_id commander,
                                                  const std::string& map,
                                                  int16_t x,
                                                  int16_t y) -> crusade_result
{
    if (!active_)
        return crusade_result::not_active;

    auto* data = get_player_data(commander);
    if (!data)
        return crusade_result::not_in_crusade;

    if (data->duty != crusade_duty::commander)
        return crusade_result::not_guild_master;

    if (!social_)
        return crusade_result::config_error;

    auto guild_id = social_->get_player_guild(commander);
    if (!guild_id.is_valid())
        return crusade_result::not_guild_master;

    guild_construct_location loc;
    loc.guild_id = guild_id.value;
    loc.map_name = map;
    loc.x = x;
    loc.y = y;
    loc.structure_count = 0;

    guild_construct_locations_[guild_id.value] = loc;

    LOG_INFO(general,
             "Guild {} set construct location at {}({},{}) by commander {}",
             guild_id.value,
             map,
             x,
             y,
             commander.value);

    return crusade_result::success;
}

auto crusade_system::get_guild_construct_location(uint32_t guild_id) const -> const guild_construct_location*
{
    auto it = guild_construct_locations_.find(guild_id);
    return it != guild_construct_locations_.end() ? &it->second : nullptr;
}

// ========== Guild Teleport ==========

auto crusade_system::set_guild_teleport_location(player_id commander,
                                                 const std::string& map,
                                                 int16_t x,
                                                 int16_t y) -> crusade_result
{
    if (!active_)
        return crusade_result::not_active;

    auto* data = get_player_data(commander);
    if (!data)
        return crusade_result::not_in_crusade;

    if (data->duty != crusade_duty::commander)
        return crusade_result::not_guild_master;

    if (!social_)
        return crusade_result::config_error;

    auto guild_id = social_->get_player_guild(commander);
    if (!guild_id.is_valid())
        return crusade_result::not_guild_master;

    // Verify guild master rank
    auto* g = social_->get_guild(guild_id);
    if (!g)
        return crusade_result::not_guild_master;
    auto* member = g->get_member(commander);
    if (!member || member->rank != social::guild_rank::guild_master)
        return crusade_result::not_guild_master;

    // Validate map exists
    if (world_)
    {
        auto* m = world_->get_map_by_name(map);
        if (!m)
            return crusade_result::invalid_position;
    }

    guild_teleport_location loc;
    loc.guild_id = guild_id.value;
    loc.map_name = map;
    loc.x = x;
    loc.y = y;

    guild_teleport_locations_[guild_id.value] = loc;

    LOG_INFO(general,
             "Guild {} set teleport location at {}({},{}) by commander {}",
             guild_id.value,
             map,
             x,
             y,
             commander.value);

    return crusade_result::success;
}

auto crusade_system::use_guild_teleport(player_id pid) -> crusade_result
{
    if (!active_)
        return crusade_result::not_active;

    auto* data = get_player_data(pid);
    if (!data)
        return crusade_result::not_in_crusade;

    if (!social_)
        return crusade_result::config_error;

    auto guild_id = social_->get_player_guild(pid);
    if (!guild_id.is_valid())
        return crusade_result::not_in_faction;

    auto it = guild_teleport_locations_.find(guild_id.value);
    if (it == guild_teleport_locations_.end())
        return crusade_result::no_construct_location; // Reuse: no teleport set

    return crusade_result::success;
}

auto crusade_system::get_guild_teleport_location(uint32_t guild_id) const -> const guild_teleport_location*
{
    auto it = guild_teleport_locations_.find(guild_id);
    return it != guild_teleport_locations_.end() ? &it->second : nullptr;
}

auto crusade_system::get_guild_teleport_dest(player_id pid) const -> const guild_teleport_location*
{
    if (!social_)
        return nullptr;

    auto guild_id = social_->get_player_guild(pid);
    if (!guild_id.is_valid())
        return nullptr;

    return get_guild_teleport_location(guild_id.value);
}

// ========== War Unit Summoning ==========

auto crusade_system::summon_war_unit(
    player_id pid, war_unit_type type, const std::string& map_name, int16_t x, int16_t y) -> crusade_result
{
    if (!active_)
        return crusade_result::not_active;

    auto* data = get_player_data(pid);
    if (!data)
        return crusade_result::not_in_crusade;

    // Only constructors can summon
    if (data->duty != crusade_duty::constructor)
    {
        return crusade_result::not_constructor;
    }

    // Validate unit type
    int32_t cost = get_construction_cost(type);
    if (cost < 0)
        return crusade_result::invalid_unit;

    // Map restrictions
    if (!map_name.empty())
    {
        if (map_name == "toh3" || map_name == "icebound")
            return crusade_result::restricted_map;

        if (world_)
        {
            auto* m = world_->get_map_by_name(map_name);
            if (m && m->config().is_fixed_day_mode)
                return crusade_result::restricted_map;
        }
    }

    // Structure placement validation
    if (is_structure_type(type))
    {
        if (!social_)
            return crusade_result::no_construct_location;

        auto guild_id = social_->get_player_guild(data->pid);
        if (!guild_id.is_valid())
            return crusade_result::no_construct_location;

        auto loc_it = guild_construct_locations_.find(guild_id.value);
        if (loc_it == guild_construct_locations_.end())
            return crusade_result::no_construct_location;

        auto& loc = loc_it->second;

        // Must be on the same map as the construct location
        if (map_name != loc.map_name)
            return crusade_result::wrong_map;

        // Must be within 10 tiles of construct location
        if (std::abs(x - loc.x) > construct_location_radius || std::abs(y - loc.y) > construct_location_radius)
            return crusade_result::too_far_from_construct_location;

        // Per-guild build limit
        if (loc.structure_count >= max_constructions_per_guild)
            return crusade_result::guild_build_limit;

        // Guard tower proximity and Y-bounds
        if (is_guard_tower_type(type))
        {
            for (const auto& ws : war_structures_)
            {
                if (is_guard_tower_type(ws.type) && ws.map_name == map_name &&
                    std::abs(ws.x - x) <= guard_tower_min_distance && std::abs(ws.y - y) <= guard_tower_min_distance)
                {
                    return crusade_result::too_close_to_tower;
                }
            }

            if (y < guard_tower_min_y || y > guard_tower_max_y)
                return crusade_result::invalid_position;
        }

        loc.structure_count++;
    }

    // Check points (only for units with non-zero cost)
    if (cost > 0)
    {
        if (data->construction_points < cost)
        {
            return crusade_result::insufficient_points;
        }
        data->construction_points -= cost;
    }

    // Spawn NPC if system available
    entity::entity eid{};
    if (npcs_ && !map_name.empty())
    {
        map_id mid{};
        if (world_)
        {
            auto* m = world_->get_map_by_name(map_name);
            if (m)
            {
                mid = m->id();
            }
        }

        if (mid.is_valid())
        {
            uint16_t npc_type = get_npc_id_for_unit(type, data->faction);
            auto spawn_result = npcs_->spawn_npc(npc_id(npc_type), mid, {x, y});
            if (spawn_result.is_ok())
            {
                eid = spawn_result.value();
            }
        }
    }

    // Create structure record
    war_structure_instance structure;
    structure.eid = eid;
    structure.type = type;
    structure.faction = data->faction;
    structure.summoner = pid;
    structure.map_name = map_name;
    structure.x = x;
    structure.y = y;
    war_structures_.push_back(structure);

    LOG_INFO(general,
             "Player {} summoned war unit type {} at {}({},{}) (cost={}, remaining={})",
             pid.value,
             static_cast<int>(type),
             map_name,
             x,
             y,
             cost,
             data->construction_points);

    broadcast_construction_point_update(pid);

    return crusade_result::success;
}

auto crusade_system::get_war_structures(war_faction faction) const -> std::vector<war_structure_instance>
{
    std::vector<war_structure_instance> result;
    for (const auto& ws : war_structures_)
    {
        if (ws.faction == faction)
            result.push_back(ws);
    }
    return result;
}

auto crusade_system::count_structures_by_type(war_faction faction, war_unit_type type) const -> int32_t
{
    int32_t count = 0;
    for (const auto& ws : war_structures_)
    {
        if (ws.faction == faction && ws.type == type)
            ++count;
    }
    return count;
}

} // namespace hb::war
