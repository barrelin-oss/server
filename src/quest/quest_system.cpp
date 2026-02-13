// quest_system.cpp
// Quest management subsystem implementation

#include "quest/quest_system.h"
#include "core/logger.h"
#include "perf/perf_stats.h"

#include <algorithm>

namespace hb::quest {

quest_system::quest_system() = default;

quest_system::~quest_system() {
    if (is_initialized()) {
        shutdown();
    }
}

void quest_system::initialize() {
    LOG_INFO(general, "Quest system initializing...");
    set_initialized(true);
    LOG_INFO(general, "Quest system initialized");
}

void quest_system::shutdown() {
    LOG_INFO(general, "Quest system shutting down...");

    quest_templates_.clear();
    player_journals_.clear();
    completion_callbacks_.clear();

    set_initialized(false);
    LOG_INFO(general, "Quest system shutdown complete");
}

void quest_system::update(float delta_time) {
    update_timed_quests(delta_time);
}

void quest_system::set_config(const quest_system_config& config) {
    config_ = config;
}

void quest_system::register_quest(quest_template quest) {
    auto id = quest.id;
    quest_templates_.emplace(id, std::move(quest));
    LOG_DEBUG(general, "Registered quest {} ({})", id.value, quest_templates_[id].name);
}

auto quest_system::get_quest_template(quest_id id) const -> const quest_template* {
    auto it = quest_templates_.find(id);
    return it != quest_templates_.end() ? &it->second : nullptr;
}

auto quest_system::quest_count() const -> size_t {
    return quest_templates_.size();
}

void quest_system::register_player(player_id id) {
    if (player_journals_.contains(id)) return;

    player_journals_.emplace(id, quest_journal{});
    LOG_DEBUG(general, "Registered quest journal for player {}", id.value);
}

void quest_system::unregister_player(player_id id) {
    player_journals_.erase(id);
    LOG_DEBUG(general, "Unregistered quest journal for player {}", id.value);
}

auto quest_system::accept_quest(player_id player, quest_id quest) -> accept_result {
    // Check if quest can be accepted
    auto result = can_accept_quest(player, quest);
    if (result != accept_result::success) {
        return result;
    }

    auto* journal = get_journal(player);
    if (!journal) return accept_result::quest_log_full;

    const auto* tmpl = get_quest_template(quest);
    if (!tmpl) return accept_result::quest_not_found;

    // Create new quest state
    quest_state state;
    state.template_id = quest;
    state.status = quest_status::active;
    state.accepted_time = std::chrono::system_clock::now();
    state.time_limit_seconds = tmpl->time_limit_seconds;
    state.initialize_objectives(tmpl->objectives);

    journal->active_quests.push_back(std::move(state));

    LOG_DEBUG(general, "Player {} accepted quest {} ({})",
        player.value, quest.value, tmpl->name);

    return accept_result::success;
}

auto quest_system::abandon_quest(player_id player, quest_id quest) -> bool {
    auto* journal = get_journal(player);
    if (!journal) return false;

    auto it = std::find_if(journal->active_quests.begin(), journal->active_quests.end(),
        [quest](const quest_state& state) { return state.template_id == quest; });

    if (it == journal->active_quests.end()) return false;

    it->status = quest_status::abandoned;
    journal->active_quests.erase(it);

    LOG_DEBUG(general, "Player {} abandoned quest {}", player.value, quest.value);
    return true;
}

auto quest_system::complete_quest(player_id player, quest_id quest) -> complete_result {
    auto* journal = get_journal(player);
    if (!journal) return complete_result::not_active;

    auto* state = journal->get_quest(quest);
    if (!state) return complete_result::not_active;

    if (!state->is_active() && !state->is_complete()) {
        return complete_result::not_active;
    }

    const auto* tmpl = get_quest_template(quest);
    if (!tmpl) return complete_result::quest_not_found;

    // Check if all required objectives are complete
    if (!state->all_objectives_complete(tmpl->objectives)) {
        return complete_result::objectives_incomplete;
    }

    // Mark as turned in
    state->status = quest_status::turned_in;
    state->completed_time = std::chrono::system_clock::now();
    state->completion_count++;

    // Add to completed quests history
    journal->completed_quests.push_back(quest);

    // Calculate rewards with modifiers
    quest_rewards final_rewards = tmpl->rewards;
    final_rewards.experience = static_cast<int64_t>(
        static_cast<float>(final_rewards.experience) * config_.quest_exp_modifier);
    final_rewards.gold = static_cast<int64_t>(
        static_cast<float>(final_rewards.gold) * config_.quest_gold_modifier);

    // Notify callbacks
    notify_quest_completed(player, quest, final_rewards);

    // Remove from active quests (unless repeatable)
    if (!tmpl->repeatable) {
        auto it = std::find_if(journal->active_quests.begin(), journal->active_quests.end(),
            [quest](const quest_state& s) { return s.template_id == quest; });
        if (it != journal->active_quests.end()) {
            journal->active_quests.erase(it);
        }
    } else {
        // Reset for repeat
        state->last_completion = std::chrono::system_clock::now();
        state->status = quest_status::available;
    }

    LOG_DEBUG(general, "Player {} completed quest {} ({})",
        player.value, quest.value, tmpl->name);

    return complete_result::success;
}

auto quest_system::get_active_quests(player_id player) const -> std::vector<quest_id> {
    std::vector<quest_id> result;

    const auto* journal = get_journal(player);
    if (!journal) return result;

    for (const auto& quest : journal->active_quests) {
        if (quest.is_active()) {
            result.push_back(quest.template_id);
        }
    }

    return result;
}

auto quest_system::get_quest_state(player_id player, quest_id quest) const -> const quest_state* {
    const auto* journal = get_journal(player);
    if (!journal) return nullptr;
    return journal->get_quest(quest);
}

auto quest_system::has_quest(player_id player, quest_id quest) const -> bool {
    const auto* journal = get_journal(player);
    if (!journal) return false;
    return journal->has_active_quest(quest);
}

auto quest_system::has_completed_quest(player_id player, quest_id quest) const -> bool {
    const auto* journal = get_journal(player);
    if (!journal) return false;
    return journal->has_completed_quest(quest);
}

auto quest_system::can_accept_quest(player_id player, quest_id quest) const -> accept_result {
    const auto* journal = get_journal(player);
    if (!journal || !journal->can_accept_quest()) {
        return accept_result::quest_log_full;
    }

    const auto* tmpl = get_quest_template(quest);
    if (!tmpl) {
        return accept_result::quest_not_found;
    }

    // Check if already active
    if (journal->has_active_quest(quest)) {
        return accept_result::already_active;
    }

    // Check prerequisites
    for (const auto& prereq : tmpl->prerequisite_quests) {
        if (!journal->has_completed_quest(prereq)) {
            return accept_result::missing_prerequisite;
        }
    }

    // Note: Level and faction checks would require player info,
    // which should be passed in separately in a full implementation

    return accept_result::success;
}

auto quest_system::get_available_quests(player_id player, int16_t player_level, uint8_t faction) const
    -> std::vector<quest_id> {
    std::vector<quest_id> result;

    const auto* journal = get_journal(player);
    if (!journal) return result;

    for (const auto& [id, tmpl] : quest_templates_) {
        // Skip if already active or completed (and not repeatable)
        if (journal->has_active_quest(id)) continue;
        if (!tmpl.repeatable && journal->has_completed_quest(id)) continue;

        // Check level requirements
        if (player_level < tmpl.min_level || player_level > tmpl.max_level) continue;

        // Check faction (0 = any faction)
        if (tmpl.required_faction != 0 && tmpl.required_faction != faction) continue;

        // Check prerequisites
        bool prereqs_met = true;
        for (const auto& prereq : tmpl.prerequisite_quests) {
            if (!journal->has_completed_quest(prereq)) {
                prereqs_met = false;
                break;
            }
        }
        if (!prereqs_met) continue;

        result.push_back(id);
    }

    return result;
}

auto quest_system::get_quests_from_npc(npc_id npc) const -> std::vector<quest_id> {
    std::vector<quest_id> result;

    for (const auto& [id, tmpl] : quest_templates_) {
        if (tmpl.quest_giver == npc) {
            result.push_back(id);
        }
    }

    return result;
}

void quest_system::on_kill(const kill_event& event) {
    update_kill_objectives(event.killer, event.killed_npc, event.was_player);
}

void quest_system::on_item_collected(const item_collected_event& event) {
    update_collect_objectives(event.player, event.item, event.count);
}

void quest_system::on_location_reached(const location_reached_event& event) {
    update_location_objectives(event.player, event.map, event.x, event.y);
}

void quest_system::on_npc_talked(const npc_talked_event& event) {
    update_npc_objectives(event.player, event.npc);
}

void quest_system::on_item_crafted(const item_crafted_event& event) {
    update_craft_objectives(event.player, event.item, event.count);
}

void quest_system::on_resource_gathered(const resource_gathered_event& event) {
    update_gather_objectives(event.player, event.skill_type, event.count);
}

void quest_system::on_level_reached(const level_reached_event& event) {
    update_level_objectives(event.player, event.new_level, event.skill_type);
}

void quest_system::on_quest_completed(quest_callback callback) {
    completion_callbacks_.push_back(std::move(callback));
}

auto quest_system::get_journal(player_id player) -> quest_journal* {
    auto it = player_journals_.find(player);
    return it != player_journals_.end() ? &it->second : nullptr;
}

auto quest_system::get_journal(player_id player) const -> const quest_journal* {
    auto it = player_journals_.find(player);
    return it != player_journals_.end() ? &it->second : nullptr;
}

void quest_system::check_quest_completion(player_id player, quest_state& state) {
    const auto* tmpl = get_quest_template(state.template_id);
    if (!tmpl) return;

    if (state.all_objectives_complete(tmpl->objectives)) {
        if (tmpl->auto_complete) {
            complete_quest(player, state.template_id);
        } else {
            state.status = quest_status::complete;
        }
    }
}

void quest_system::update_kill_objectives(player_id player, npc_id killed, bool was_player) {
    auto* perf = subsystems().get<perf::perf_stats_system>();
    PERF_TIMER(perf, perf::metric_category::quest_update);

    auto* journal = get_journal(player);
    if (!journal) return;

    for (auto& quest : journal->active_quests) {
        if (!quest.is_active()) continue;

        const auto* tmpl = get_quest_template(quest.template_id);
        if (!tmpl) continue;

        for (size_t i = 0; i < quest.objectives.size() && i < tmpl->objectives.size(); ++i) {
            auto& obj = quest.objectives[i];
            const auto& obj_tmpl = tmpl->objectives[i];

            if (obj.is_complete()) continue;
            if (obj_tmpl.type != objective_type::kill_monster &&
                obj_tmpl.type != objective_type::kill_player) continue;

            const auto* kill_data = std::get_if<kill_objective_data>(&obj_tmpl.data);
            if (!kill_data) continue;

            // Check if this kill matches
            if (kill_data->player_kills != was_player) continue;
            if (!was_player && kill_data->target_type.value != 0 &&
                kill_data->target_type != killed) continue;

            obj.add_progress(1);
        }

        check_quest_completion(player, quest);
    }
}

void quest_system::update_collect_objectives(player_id player, item_id item, int32_t count) {
    auto* perf = subsystems().get<perf::perf_stats_system>();
    PERF_TIMER(perf, perf::metric_category::quest_update);

    auto* journal = get_journal(player);
    if (!journal) return;

    for (auto& quest : journal->active_quests) {
        if (!quest.is_active()) continue;

        const auto* tmpl = get_quest_template(quest.template_id);
        if (!tmpl) continue;

        for (size_t i = 0; i < quest.objectives.size() && i < tmpl->objectives.size(); ++i) {
            auto& obj = quest.objectives[i];
            const auto& obj_tmpl = tmpl->objectives[i];

            if (obj.is_complete()) continue;
            if (obj_tmpl.type != objective_type::collect_item &&
                obj_tmpl.type != objective_type::deliver_item) continue;

            const auto* collect_data = std::get_if<collect_objective_data>(&obj_tmpl.data);
            if (!collect_data) continue;

            if (collect_data->item_type == item) {
                obj.add_progress(count);
            }
        }

        check_quest_completion(player, quest);
    }
}

void quest_system::update_location_objectives(player_id player, map_id map, int16_t x, int16_t y) {
    auto* perf = subsystems().get<perf::perf_stats_system>();
    PERF_TIMER(perf, perf::metric_category::quest_update);

    auto* journal = get_journal(player);
    if (!journal) return;

    for (auto& quest : journal->active_quests) {
        if (!quest.is_active()) continue;

        const auto* tmpl = get_quest_template(quest.template_id);
        if (!tmpl) continue;

        for (size_t i = 0; i < quest.objectives.size() && i < tmpl->objectives.size(); ++i) {
            auto& obj = quest.objectives[i];
            const auto& obj_tmpl = tmpl->objectives[i];

            if (obj.is_complete()) continue;
            if (obj_tmpl.type != objective_type::visit_location) continue;

            const auto* loc_data = std::get_if<location_objective_data>(&obj_tmpl.data);
            if (!loc_data) continue;

            if (loc_data->target_map == map) {
                int dx = x - loc_data->target_x;
                int dy = y - loc_data->target_y;
                int distance_sq = dx * dx + dy * dy;
                int radius_sq = loc_data->radius * loc_data->radius;

                if (distance_sq <= radius_sq) {
                    obj.add_progress(1);
                }
            }
        }

        check_quest_completion(player, quest);
    }
}

void quest_system::update_npc_objectives(player_id player, npc_id npc) {
    auto* journal = get_journal(player);
    if (!journal) return;

    for (auto& quest : journal->active_quests) {
        if (!quest.is_active()) continue;

        const auto* tmpl = get_quest_template(quest.template_id);
        if (!tmpl) continue;

        for (size_t i = 0; i < quest.objectives.size() && i < tmpl->objectives.size(); ++i) {
            auto& obj = quest.objectives[i];
            const auto& obj_tmpl = tmpl->objectives[i];

            if (obj.is_complete()) continue;
            if (obj_tmpl.type != objective_type::talk_to_npc) continue;

            const auto* npc_data = std::get_if<npc_objective_data>(&obj_tmpl.data);
            if (!npc_data) continue;

            if (npc_data->target_npc == npc) {
                obj.add_progress(1);
            }
        }

        check_quest_completion(player, quest);
    }
}

void quest_system::update_craft_objectives(player_id player, item_id item, int32_t count) {
    auto* journal = get_journal(player);
    if (!journal) return;

    for (auto& quest : journal->active_quests) {
        if (!quest.is_active()) continue;

        const auto* tmpl = get_quest_template(quest.template_id);
        if (!tmpl) continue;

        for (size_t i = 0; i < quest.objectives.size() && i < tmpl->objectives.size(); ++i) {
            auto& obj = quest.objectives[i];
            const auto& obj_tmpl = tmpl->objectives[i];

            if (obj.is_complete()) continue;
            if (obj_tmpl.type != objective_type::craft_item) continue;

            const auto* craft_data = std::get_if<craft_objective_data>(&obj_tmpl.data);
            if (!craft_data) continue;

            if (craft_data->item_type == item) {
                obj.add_progress(count);
            }
        }

        check_quest_completion(player, quest);
    }
}

void quest_system::update_gather_objectives(player_id player, uint8_t skill_type, int32_t count) {
    auto* journal = get_journal(player);
    if (!journal) return;

    for (auto& quest : journal->active_quests) {
        if (!quest.is_active()) continue;

        const auto* tmpl = get_quest_template(quest.template_id);
        if (!tmpl) continue;

        for (size_t i = 0; i < quest.objectives.size() && i < tmpl->objectives.size(); ++i) {
            auto& obj = quest.objectives[i];
            const auto& obj_tmpl = tmpl->objectives[i];

            if (obj.is_complete()) continue;
            if (obj_tmpl.type != objective_type::gather_resource) continue;

            const auto* gather_data = std::get_if<gather_objective_data>(&obj_tmpl.data);
            if (!gather_data) continue;

            if (gather_data->skill_type == skill_type) {
                obj.add_progress(count);
            }
        }

        check_quest_completion(player, quest);
    }
}

void quest_system::update_level_objectives(player_id player, int16_t level, uint8_t skill_type) {
    auto* journal = get_journal(player);
    if (!journal) return;

    for (auto& quest : journal->active_quests) {
        if (!quest.is_active()) continue;

        const auto* tmpl = get_quest_template(quest.template_id);
        if (!tmpl) continue;

        for (size_t i = 0; i < quest.objectives.size() && i < tmpl->objectives.size(); ++i) {
            auto& obj = quest.objectives[i];
            const auto& obj_tmpl = tmpl->objectives[i];

            if (obj.is_complete()) continue;
            if (obj_tmpl.type != objective_type::reach_level &&
                obj_tmpl.type != objective_type::reach_skill_level) continue;

            const auto* level_data = std::get_if<level_objective_data>(&obj_tmpl.data);
            if (!level_data) continue;

            if (level_data->skill_type == skill_type && level >= level_data->target_level) {
                obj.current_count = obj.required_count;
                obj.status = objective_status::complete;
            }
        }

        check_quest_completion(player, quest);
    }
}

void quest_system::update_timed_quests(float delta_time) {
    int32_t delta_seconds = static_cast<int32_t>(delta_time);
    if (delta_seconds <= 0) return;

    for (auto& [player_id, journal] : player_journals_) {
        for (auto& quest : journal.active_quests) {
            if (!quest.is_active()) continue;
            if (quest.time_limit_seconds <= 0) continue;

            quest.elapsed_seconds += delta_seconds;

            if (quest.has_timed_out()) {
                quest.status = quest_status::failed;
                LOG_DEBUG(general, "Player {} quest {} timed out",
                    player_id.value, quest.template_id.value);
            }
        }
    }
}

void quest_system::notify_quest_completed(player_id player, quest_id quest, const quest_rewards& rewards) {
    quest_completed_event event;
    event.player = player;
    event.quest = quest;
    event.rewards = rewards;

    for (const auto& callback : completion_callbacks_) {
        callback(event);
    }
}

}  // namespace hb::quest
