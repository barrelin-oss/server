// war_system.cpp
// War management subsystem implementation

#include "war/war_system.h"
#include "core/logger.h"

#include <algorithm>

namespace hb::war
{

war_system::war_system() = default;

war_system::~war_system()
{
    if (is_initialized())
    {
        shutdown();
    }
}

void war_system::initialize()
{
    LOG_INFO(general, "War system initializing...");
    set_initialized(true);
    LOG_INFO(general, "War system initialized");
}

void war_system::shutdown()
{
    LOG_INFO(general, "War system shutting down...");

    wars_.clear();
    active_war_by_type_.clear();
    territories_.clear();
    player_factions_.clear();
    player_wars_.clear();

    war_started_callbacks_.clear();
    war_ended_callbacks_.clear();
    territory_captured_callbacks_.clear();

    set_initialized(false);
    LOG_INFO(general, "War system shutdown complete");
}

void war_system::update(float delta_time)
{
    for (auto& [id, war] : wars_)
    {
        if (war.is_running())
        {
            update_war(war, delta_time);
        }
    }
}

void war_system::set_config(const war_system_config& config)
{
    config_ = config;
}

// ========== War Lifecycle ==========

auto war_system::schedule_war(war_type type,
                              std::chrono::system_clock::time_point start_time) -> result<war_id, war_result_code>
{
    // Check if this war type is already running
    if (active_war_by_type_.contains(type))
    {
        return result<war_id, war_result_code>::err(war_result_code::war_already_running);
    }

    if (active_war_count() >= static_cast<size_t>(config_.max_concurrent_wars))
    {
        return result<war_id, war_result_code>::err(war_result_code::max_wars_reached);
    }

    war_id wid = generate_war_id();
    war_state war;
    war.id = wid;
    war.type = type;
    war.state = war_status::scheduled;
    war.config = config_.default_war_config;
    war.scheduled_start = start_time;

    wars_.emplace(wid, std::move(war));

    LOG_INFO(general, "War {} of type {} scheduled", wid.value, static_cast<int>(type));
    return result<war_id, war_result_code>::ok(wid);
}

auto war_system::start_war(war_type type) -> result<war_id, war_result_code>
{
    // Check if this war type is already running
    if (active_war_by_type_.contains(type))
    {
        return result<war_id, war_result_code>::err(war_result_code::war_already_running);
    }

    if (active_war_count() >= static_cast<size_t>(config_.max_concurrent_wars))
    {
        return result<war_id, war_result_code>::err(war_result_code::max_wars_reached);
    }

    war_id wid = generate_war_id();
    war_state war;
    war.id = wid;
    war.type = type;
    war.state = war_status::preparing;
    war.config = config_.default_war_config;
    war.started_at = std::chrono::system_clock::now();
    war.ends_at = war.started_at + std::chrono::seconds(war.config.preparation_seconds + war.config.duration_seconds);

    wars_.emplace(wid, std::move(war));
    active_war_by_type_[type] = wid;

    // Notify callbacks
    war_started_event event;
    event.war = wid;
    event.type = type;
    for (const auto& callback : war_started_callbacks_)
    {
        callback(event);
    }

    LOG_INFO(general, "War {} of type {} started", wid.value, static_cast<int>(type));
    return result<war_id, war_result_code>::ok(wid);
}

auto war_system::end_war(war_id id) -> war_result_code
{
    auto* war = get_war(id);
    if (!war)
        return war_result_code::war_not_found;

    if (!war->is_running())
    {
        return war_result_code::war_not_active;
    }

    finalize_war(*war);
    return war_result_code::success;
}

auto war_system::cancel_war(war_id id) -> war_result_code
{
    auto* war = get_war(id);
    if (!war)
        return war_result_code::war_not_found;

    // Remove from active tracking
    active_war_by_type_.erase(war->type);

    // Remove player tracking
    for (const auto& [player, participant] : war->participants)
    {
        player_wars_.erase(player);
    }

    war->state = war_status::completed;

    LOG_INFO(general, "War {} cancelled", id.value);
    return war_result_code::success;
}

// ========== War Queries ==========

auto war_system::get_war(war_id id) -> war_state*
{
    auto it = wars_.find(id);
    return it != wars_.end() ? &it->second : nullptr;
}

auto war_system::get_war(war_id id) const -> const war_state*
{
    auto it = wars_.find(id);
    return it != wars_.end() ? &it->second : nullptr;
}

auto war_system::get_active_war(war_type type) const -> std::optional<war_id>
{
    auto it = active_war_by_type_.find(type);
    if (it != active_war_by_type_.end())
    {
        return it->second;
    }
    return std::nullopt;
}

auto war_system::get_all_active_wars() const -> std::vector<war_id>
{
    std::vector<war_id> result;
    for (const auto& [type, id] : active_war_by_type_)
    {
        result.push_back(id);
    }
    return result;
}

auto war_system::is_war_active(war_type type) const -> bool
{
    return active_war_by_type_.contains(type);
}

auto war_system::active_war_count() const -> size_t
{
    return active_war_by_type_.size();
}

// ========== Participation ==========

auto war_system::join_war(war_id wid,
                          player_id player,
                          const std::string& player_name,
                          war_faction faction) -> war_result_code
{
    auto* war = get_war(wid);
    if (!war)
        return war_result_code::war_not_found;

    if (!war->is_running())
    {
        return war_result_code::war_not_active;
    }

    if (war->is_participant(player))
    {
        return war_result_code::already_participant;
    }

    // Add participant
    war_participant participant;
    participant.player = player;
    participant.name = player_name;
    participant.faction = faction;
    participant.joined_at = std::chrono::system_clock::now();

    war->participants.emplace(player, std::move(participant));
    war->get_faction_score(faction).participant_count++;
    player_wars_[player] = wid;

    LOG_DEBUG(general, "Player {} joined war {} as faction {}", player.value, wid.value, static_cast<int>(faction));

    return war_result_code::success;
}

auto war_system::leave_war(war_id wid, player_id player) -> war_result_code
{
    auto* war = get_war(wid);
    if (!war)
        return war_result_code::war_not_found;

    auto* participant = war->get_participant(player);
    if (!participant)
        return war_result_code::not_participant;

    war->get_faction_score(participant->faction).participant_count--;
    war->participants.erase(player);
    player_wars_.erase(player);

    LOG_DEBUG(general, "Player {} left war {}", player.value, wid.value);
    return war_result_code::success;
}

auto war_system::respawn_participant(war_id wid, player_id player) -> bool
{
    auto* war = get_war(wid);
    if (!war || !war->is_active())
        return false;

    auto* participant = war->get_participant(player);
    if (!participant)
        return false;

    if (participant->status != participant_status::dead)
        return false;

    participant->status = participant_status::alive;
    participant->respawn_timer_seconds = 0;

    return true;
}

// ========== Combat Events ==========

void war_system::record_kill(war_id wid, player_id killer, player_id victim)
{
    auto* war = get_war(wid);
    if (!war || !war->is_active())
        return;

    auto* killer_participant = war->get_participant(killer);
    auto* victim_participant = war->get_participant(victim);

    if (killer_participant)
    {
        killer_participant->record_kill();
        war->get_faction_score(killer_participant->faction).add_kill();
    }

    if (victim_participant)
    {
        victim_participant->record_death();
        victim_participant->status = participant_status::dead;
        victim_participant->respawn_timer_seconds =
            war->config.base_respawn_seconds + (victim_participant->deaths * war->config.respawn_increment_per_death);

        if (victim_participant->respawn_timer_seconds > war->config.max_respawn_seconds)
        {
            victim_participant->respawn_timer_seconds = war->config.max_respawn_seconds;
        }

        war->get_faction_score(victim_participant->faction).add_death();
    }

    LOG_DEBUG(general, "War kill: {} killed {} in war {}", killer.value, victim.value, wid.value);
}

void war_system::record_assist(war_id wid, player_id assister, player_id /*victim*/)
{
    auto* war = get_war(wid);
    if (!war || !war->is_active())
        return;

    auto* participant = war->get_participant(assister);
    if (participant)
    {
        participant->record_assist();
    }
}

void war_system::record_damage(war_id wid, player_id dealer, int32_t damage)
{
    auto* war = get_war(wid);
    if (!war || !war->is_active())
        return;

    auto* participant = war->get_participant(dealer);
    if (participant)
    {
        participant->damage_dealt += damage;
        participant->contribution_score += damage / 100; // 1 point per 100 damage
    }
}

void war_system::record_healing(war_id wid, player_id healer, int32_t amount)
{
    auto* war = get_war(wid);
    if (!war || !war->is_active())
        return;

    auto* participant = war->get_participant(healer);
    if (participant)
    {
        participant->healing_done += amount;
        participant->contribution_score += amount / 50; // 1 point per 50 healing
    }
}

// ========== Objectives ==========

void war_system::update_objective_capture(war_id wid, uint16_t objective_id, war_faction capturing, int32_t progress)
{
    auto* war = get_war(wid);
    if (!war || !war->is_active())
        return;

    auto* obj = get_objective(wid, objective_id);
    if (!obj || !obj->can_be_captured)
        return;

    obj->capturing_faction = capturing;
    obj->capture_progress = progress;

    if (obj->capture_progress >= 100)
    {
        obj->controlling_faction = capturing;
        obj->capture_progress = 0;
        obj->capturing_faction = war_faction::neutral;

        // Add score
        int32_t score = obj->is_main_objective ? war->config.main_objective_score : war->config.objective_score;
        war->get_faction_score(capturing).add_objective();
        war->get_faction_score(capturing).total_score += score;

        LOG_DEBUG(general,
                  "Objective {} captured by faction {} in war {}",
                  objective_id,
                  static_cast<int>(capturing),
                  wid.value);
    }
}

auto war_system::get_objective(war_id wid, uint16_t objective_id) -> war_objective*
{
    auto* war = get_war(wid);
    if (!war)
        return nullptr;

    for (auto& obj : war->objectives)
    {
        if (obj.id == objective_id)
            return &obj;
    }
    return nullptr;
}

// ========== Territory ==========

void war_system::register_territory(territory t)
{
    auto id = t.id;
    territories_.emplace(id, std::move(t));
    LOG_DEBUG(general, "Registered territory {}", id.value);
}

auto war_system::get_territory(territory_id id) -> territory*
{
    auto it = territories_.find(id);
    return it != territories_.end() ? &it->second : nullptr;
}

auto war_system::get_territory(territory_id id) const -> const territory*
{
    auto it = territories_.find(id);
    return it != territories_.end() ? &it->second : nullptr;
}

auto war_system::get_territory_at(map_id map, int16_t x, int16_t y) const -> territory_id
{
    for (const auto& [id, t] : territories_)
    {
        if (t.map == map && t.contains(x, y))
        {
            return id;
        }
    }
    return territory_id{};
}

auto war_system::get_faction_territories(war_faction faction) const -> std::vector<territory_id>
{
    std::vector<territory_id> result;
    for (const auto& [id, t] : territories_)
    {
        if (t.controlling_faction == faction)
        {
            result.push_back(id);
        }
    }
    return result;
}

auto war_system::get_faction_summary(war_faction faction) const -> faction_territory_summary
{
    faction_territory_summary summary;
    summary.faction = faction;

    for (const auto& [id, t] : territories_)
    {
        if (t.controlling_faction == faction)
        {
            summary.add_territory(t);
        }
    }

    return summary;
}

auto war_system::capture_territory(territory_id id, war_faction new_faction) -> bool
{
    auto* t = get_territory(id);
    if (!t)
        return false;

    war_faction old_faction = t->controlling_faction;
    t->controlling_faction = new_faction;
    t->captured_at = std::chrono::system_clock::now();
    t->capture_count++;

    // Notify callbacks
    territory_captured_event event;
    event.territory = id;
    event.old_faction = old_faction;
    event.new_faction = new_faction;
    for (const auto& callback : territory_captured_callbacks_)
    {
        callback(event);
    }

    LOG_INFO(general, "Territory {} captured by faction {}", id.value, static_cast<int>(new_faction));
    return true;
}

auto war_system::territory_count() const -> size_t
{
    return territories_.size();
}

// ========== Rewards ==========

auto war_system::calculate_rewards(war_id wid, player_id player) const -> war_rewards
{
    war_rewards rewards;

    const auto* war = get_war(wid);
    if (!war)
        return rewards;

    const auto* participant = war->get_participant(player);
    if (!participant)
        return rewards;

    // Base rewards
    rewards.contribution_points = participant->contribution_score;

    // Experience based on contribution
    rewards.experience = participant->contribution_score * 10;

    // Gold based on participation
    rewards.gold = participant->contribution_score * 5;

    // Apply multipliers based on win/loss
    war_faction winner = determine_winner(*war);
    float mult = config_.default_war_config.participation_reward_mult;

    if (winner == participant->faction)
    {
        mult = config_.default_war_config.winner_reward_mult;
    }
    else if (winner != war_faction::neutral)
    {
        mult = config_.default_war_config.loser_reward_mult;
    }

    rewards.experience = static_cast<int64_t>(static_cast<float>(rewards.experience) * mult);
    rewards.gold = static_cast<int64_t>(static_cast<float>(rewards.gold) * mult);

    return rewards;
}

// ========== Callbacks ==========

void war_system::on_war_started(war_callback callback)
{
    war_started_callbacks_.push_back(std::move(callback));
}

void war_system::on_war_ended(war_end_callback callback)
{
    war_ended_callbacks_.push_back(std::move(callback));
}

void war_system::on_territory_captured(territory_callback callback)
{
    territory_captured_callbacks_.push_back(std::move(callback));
}

// ========== Player Queries ==========

auto war_system::get_player_faction(player_id player) const -> war_faction
{
    auto it = player_factions_.find(player);
    return it != player_factions_.end() ? it->second : war_faction::neutral;
}

void war_system::set_player_faction(player_id player, war_faction faction)
{
    player_factions_[player] = faction;
}

auto war_system::is_player_in_war(player_id player) const -> bool
{
    return player_wars_.contains(player);
}

auto war_system::get_player_war(player_id player) const -> war_id
{
    auto it = player_wars_.find(player);
    return it != player_wars_.end() ? it->second : war_id{};
}

// ========== Private Helpers ==========

void war_system::update_war(war_state& war, float delta_time)
{
    update_war_timers(war, delta_time);

    if (war.state == war_status::preparing)
    {
        // Check if preparation is done
        auto elapsed = std::chrono::system_clock::now() - war.started_at;
        if (elapsed >= std::chrono::seconds(war.config.preparation_seconds))
        {
            transition_war_state(war, war_status::active);
        }
    }
    else if (war.state == war_status::active)
    {
        update_objectives(war, delta_time);

        // Check if war time is up
        if (std::chrono::system_clock::now() >= war.ends_at)
        {
            transition_war_state(war, war_status::ending);
        }
    }
    else if (war.state == war_status::ending)
    {
        auto elapsed = std::chrono::system_clock::now() - war.ends_at;
        if (elapsed >= std::chrono::seconds(war.config.ending_seconds))
        {
            finalize_war(war);
        }
    }
}

void war_system::update_war_timers(war_state& war, float delta_time)
{
    war.elapsed_seconds += static_cast<int32_t>(delta_time);

    // Update respawn timers
    for (auto& [player, participant] : war.participants)
    {
        if (participant.status == participant_status::dead && participant.respawn_timer_seconds > 0)
        {
            participant.respawn_timer_seconds -= static_cast<int32_t>(delta_time);
            if (participant.respawn_timer_seconds <= 0)
            {
                participant.respawn_timer_seconds = 0;
                // Auto-respawn or wait for player action
            }
        }
    }
}

void war_system::update_objectives(war_state& war, [[maybe_unused]] float delta_time)
{
    // Update objective scores
    for (const auto& obj : war.objectives)
    {
        if (obj.controlling_faction != war_faction::neutral)
        {
            war.get_faction_score(obj.controlling_faction).total_score += obj.score_per_tick;
        }
    }
}

void war_system::transition_war_state(war_state& war, war_status new_state)
{
    war_status old_state = war.state;
    war.state = new_state;

    LOG_INFO(general,
             "War {} transitioned from state {} to {}",
             war.id.value,
             static_cast<int>(old_state),
             static_cast<int>(new_state));

    if (new_state == war_status::active)
    {
        war.phase = war_phase::phase_1;
    }
}

void war_system::finalize_war(war_state& war)
{
    war.state = war_status::completed;

    war_faction winner = determine_winner(war);

    // Remove from active tracking
    active_war_by_type_.erase(war.type);

    // Remove player tracking
    for (const auto& [player, participant] : war.participants)
    {
        player_wars_.erase(player);
    }

    // Notify callbacks
    war_ended_event event;
    event.war = war.id;
    event.type = war.type;
    event.winner = winner;
    event.draw = (winner == war_faction::neutral);
    for (const auto& callback : war_ended_callbacks_)
    {
        callback(event);
    }

    LOG_INFO(general, "War {} ended. Winner: faction {}", war.id.value, static_cast<int>(winner));
}

auto war_system::determine_winner(const war_state& war) const -> war_faction
{
    if (war.aresden_score.total_score > war.elvine_score.total_score)
    {
        return war_faction::aresden;
    }
    else if (war.elvine_score.total_score > war.aresden_score.total_score)
    {
        return war_faction::elvine;
    }
    return war_faction::neutral; // Draw
}

auto war_system::generate_war_id() -> war_id
{
    return war_id{next_war_id_++};
}

} // namespace hb::war
