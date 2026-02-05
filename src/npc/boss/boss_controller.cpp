// boss_controller.cpp
// Boss phase transition and management logic

#include "npc/boss/boss_controller.h"
#include "npc/npc.h"
#include "npc/npc_system.h"
#include "core/logger.h"

namespace hb::npc::boss {

void boss_controller::register_boss(entity::entity id, const boss_config& config)
{
    boss_state state;
    state.config = &config;
    state.current_phase = 0;
    state.phase_start_time = std::chrono::steady_clock::now();
    state.spawn_time = std::chrono::steady_clock::now();
    state.enraged = false;

    bosses_[id] = std::move(state);

    LOG_INFO(general, "Registered boss '{}' (entity {}) with {} phases",
        config.name, id.id, config.phases.size());

    // Run on_spawn_script
    if (npc_sys_ && !config.on_spawn_script.empty())
    {
        npc_sys_->start_script(id, config.on_spawn_script);
    }

    // Apply initial phase behavior tree and modifiers
    if (npc_sys_ && !config.phases.empty())
    {
        auto* npc_ptr = npc_sys_->get_npc(id);
        if (npc_ptr)
        {
            const auto& first_phase = config.phases[0];
            if (!first_phase.behavior_tree.empty())
            {
                npc_ptr->ai.behavior_tree = first_phase.behavior_tree;
            }
            apply_phase_modifiers(*npc_ptr, first_phase);
        }
    }
}

void boss_controller::unregister_boss(entity::entity id)
{
    auto it = bosses_.find(id);
    if (it == bosses_.end()) return;

    auto& state = it->second;

    // Run on_death_script
    if (npc_sys_ && state.config && !state.config->on_death_script.empty())
    {
        npc_sys_->start_script(id, state.config->on_death_script);
    }

    bosses_.erase(it);
}

void boss_controller::update(float /*delta_time*/)
{
    if (!npc_sys_) return;

    for (auto& [id, state] : bosses_)
    {
        auto* npc_ptr = npc_sys_->get_npc(id);
        if (!npc_ptr || npc_ptr->is_dead()) continue;

        check_phase_transitions(id, *npc_ptr, state);
        check_enrage(id, *npc_ptr, state);
    }
}

auto boss_controller::get_state(entity::entity id) -> boss_state*
{
    auto it = bosses_.find(id);
    return it != bosses_.end() ? &it->second : nullptr;
}

auto boss_controller::is_boss(entity::entity id) const -> bool
{
    return bosses_.contains(id);
}

void boss_controller::check_phase_transitions(entity::entity id, npc& npc_ref, boss_state& state)
{
    if (!state.config) return;
    if (state.config->phases.empty()) return;

    float hp_pct = npc_ref.max_hp > 0
        ? static_cast<float>(npc_ref.hp) / npc_ref.max_hp
        : 0.0f;

    // Check if any later phase should be triggered
    // Iterate from last phase to current+1 (to handle skipping phases on large damage)
    for (int i = static_cast<int>(state.config->phases.size()) - 1;
         i > state.current_phase; --i)
    {
        const auto& phase = state.config->phases[i];
        const auto& trigger = phase.enter_trigger;
        bool should_transition = false;

        switch (trigger.type)
        {
            case phase_trigger_type::hp_below:
                should_transition = hp_pct < trigger.hp_threshold;
                break;

            case phase_trigger_type::hp_above:
                should_transition = hp_pct > trigger.hp_threshold;
                break;

            case phase_trigger_type::timer:
                should_transition = state.time_in_phase_ms() >= trigger.timer_ms;
                break;

            case phase_trigger_type::adds_dead:
            {
                // Check if all spawned adds are dead
                bool all_dead = true;
                for (auto add_id : state.spawned_adds)
                {
                    auto* add = npc_sys_->get_npc(add_id);
                    if (add && add->is_alive())
                    {
                        all_dead = false;
                        break;
                    }
                }
                should_transition = !state.spawned_adds.empty() && all_dead;
                break;
            }
        }

        if (should_transition)
        {
            transition_phase(id, npc_ref, state, i);
            return;
        }
    }
}

void boss_controller::transition_phase(entity::entity id, npc& npc_ref,
                                       boss_state& state, int new_phase)
{
    if (!state.config) return;

    int old_phase_idx = state.current_phase;
    const auto& old_phase = state.config->phases[old_phase_idx];
    const auto& new_phase_data = state.config->phases[new_phase];

    LOG_INFO(general, "Boss '{}' transitioning from phase {} ('{}') to phase {} ('{}')",
        state.config->name, old_phase_idx, old_phase.name, new_phase, new_phase_data.name);

    // Run on_exit script for old phase
    if (npc_sys_ && !old_phase.on_exit_script.empty())
    {
        npc_sys_->start_script(id, old_phase.on_exit_script);
    }

    // Update phase
    state.current_phase = new_phase;
    state.phase_start_time = std::chrono::steady_clock::now();
    state.spawned_adds.clear();

    // Apply new phase behavior tree
    if (!new_phase_data.behavior_tree.empty())
    {
        npc_ref.ai.behavior_tree = new_phase_data.behavior_tree;
    }

    // Apply stat modifiers
    apply_phase_modifiers(npc_ref, new_phase_data);

    // Run on_enter script for new phase
    if (npc_sys_ && !new_phase_data.on_enter_script.empty())
    {
        npc_sys_->start_script(id, new_phase_data.on_enter_script);
    }
}

void boss_controller::apply_phase_modifiers(npc& npc_ref, const boss_phase& phase)
{
    // Apply aggro/attack range overrides
    if (phase.override_aggro_range >= 0)
    {
        npc_ref.ai.aggro_range = phase.override_aggro_range;
    }
    if (phase.override_attack_range >= 0)
    {
        npc_ref.ai.attack_range = phase.override_attack_range;
    }

    // Note: damage/speed/defense multipliers would ideally be applied
    // through modifier components on the entity. For now we log them.
    if (phase.damage_multiplier != 1.0f || phase.speed_multiplier != 1.0f ||
        phase.defense_multiplier != 1.0f)
    {
        LOG_DEBUG(general, "Boss phase '{}' modifiers: dmg={:.1f}x, spd={:.1f}x, def={:.1f}x",
            phase.name, phase.damage_multiplier, phase.speed_multiplier,
            phase.defense_multiplier);
    }
}

void boss_controller::check_enrage(entity::entity id, npc& npc_ref, boss_state& state)
{
    if (!state.config) return;
    if (!state.config->enrage_enabled) return;
    if (state.enraged) return;

    if (state.time_since_spawn_ms() >= state.config->enrage_timer_ms)
    {
        state.enraged = true;
        LOG_INFO(general, "Boss '{}' has ENRAGED!", state.config->name);

        // Apply enrage modifiers - significant stat boost
        npc_ref.ai.aggro_range = static_cast<int16_t>(npc_ref.ai.aggro_range * 2);
        npc_ref.ai.chase_range = static_cast<int16_t>(npc_ref.ai.chase_range * 2);
    }
}

}  // namespace hb::npc::boss
