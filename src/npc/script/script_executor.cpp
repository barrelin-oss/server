// script_executor.cpp
// Runtime execution of NPC scripts

#include "npc/script/script_executor.h"
#include "npc/npc_system.h"
#include "core/logger.h"

namespace hb::npc::script {

void script_executor::start(entity::entity npc_entity, const npc_script& script)
{
    if (script.actions.empty()) return;

    script_state state;
    state.script = &script;
    state.current_action = 0;
    state.action_start = std::chrono::steady_clock::now();
    state.waiting = false;
    state.wait_remaining_ms = 0;

    running_[npc_entity] = state;

    LOG_DEBUG(general, "Started script '{}' on NPC {}", script.name, npc_entity.id);
}

void script_executor::stop(entity::entity npc_entity)
{
    running_.erase(npc_entity);
}

auto script_executor::is_running(entity::entity npc_entity) const -> bool
{
    return running_.contains(npc_entity);
}

void script_executor::update(float delta_time)
{
    auto delta_ms = static_cast<int32_t>(delta_time * 1000.0f);

    std::vector<entity::entity> completed;

    for (auto& [entity_id, state] : running_)
    {
        if (!state.script) continue;

        // Handle wait actions
        if (state.waiting)
        {
            state.wait_remaining_ms -= delta_ms;
            if (state.wait_remaining_ms > 0) continue;

            state.waiting = false;
            advance(entity_id, state);

            if (!state.script)
            {
                completed.push_back(entity_id);
                continue;
            }
        }

        // Execute current action
        while (state.script && state.current_action < state.script->actions.size() && !state.waiting)
        {
            const auto& action = state.script->actions[state.current_action];
            execute_action(entity_id, action);

            // If action was a wait, break to handle it next frame
            if (state.waiting) break;

            // Advance to next action
            advance(entity_id, state);
        }

        // Check if script is done
        if (state.script && state.current_action >= state.script->actions.size())
        {
            if (state.script->loop)
            {
                state.current_action = 0;
            }
            else
            {
                completed.push_back(entity_id);
            }
        }
    }

    for (auto id : completed)
    {
        running_.erase(id);
    }
}

void script_executor::execute_action(entity::entity npc_entity, const script_action& action)
{
    if (!npc_sys_) return;

    auto* npc_ptr = npc_sys_->get_npc(npc_entity);
    if (!npc_ptr) return;

    // Find the state for this NPC
    auto state_it = running_.find(npc_entity);
    if (state_it == running_.end()) return;

    switch (action.type)
    {
        case action_type::wait:
            state_it->second.waiting = true;
            state_it->second.wait_remaining_ms = action.int_param;
            break;

        case action_type::move_to:
            // Move NPC towards target position (one step per tick)
            npc_ptr->ai_state.target_position = action.pos;
            break;

        case action_type::face:
            npc_ptr->facing = action.dir;
            break;

        case action_type::say:
            LOG_DEBUG(general, "NPC {} '{}' says: {}",
                npc_entity.id, npc_ptr->name, action.str_param);
            // TODO: Broadcast chat message to nearby players
            break;

        case action_type::broadcast:
            LOG_INFO(general, "[Broadcast] {}", action.str_param);
            // TODO: Send server-wide message
            break;

        case action_type::emote:
            LOG_DEBUG(general, "NPC {} '{}' emote: {}",
                npc_entity.id, npc_ptr->name, action.str_param);
            // TODO: Broadcast emote to nearby players
            break;

        case action_type::spawn_npc:
        {
            auto spawn_pos = action.pos;
            if (spawn_pos.x == 0 && spawn_pos.y == 0)
            {
                spawn_pos = npc_ptr->pos;
            }
            for (int32_t i = 0; i < action.int_param2; ++i)
            {
                npc_sys_->spawn_npc(action.npc_template, npc_ptr->current_map, spawn_pos);
            }
            break;
        }

        case action_type::despawn:
            npc_sys_->despawn_npc(npc_entity);
            stop(npc_entity);
            break;

        case action_type::set_state:
        {
            // Parse state name to ai_state
            if (action.str_param == "idle")
                npc_ptr->ai_state.set_state(ai_state::idle);
            else if (action.str_param == "wander")
                npc_ptr->ai_state.set_state(ai_state::wander);
            else if (action.str_param == "chase")
                npc_ptr->ai_state.set_state(ai_state::chase);
            else if (action.str_param == "attack")
                npc_ptr->ai_state.set_state(ai_state::attack);
            else if (action.str_param == "flee")
                npc_ptr->ai_state.set_state(ai_state::flee);
            else if (action.str_param == "return_home")
                npc_ptr->ai_state.set_state(ai_state::return_home);
            else if (action.str_param == "scripted")
                npc_ptr->ai_state.set_state(ai_state::scripted);
            break;
        }

        case action_type::set_flag:
            // TODO: Parse flag name and set/clear
            break;

        case action_type::apply_buff:
            // TODO: Apply buff to nearby targets
            LOG_DEBUG(general, "NPC {} applying buff '{}' radius {}",
                npc_entity.id, action.str_param, action.int_param2);
            break;

        case action_type::damage_area:
        {
            auto center = action.pos;
            if (center.x == 0 && center.y == 0)
            {
                center = npc_ptr->pos;
            }
            LOG_DEBUG(general, "NPC {} AoE damage {} at ({},{}) radius {}",
                npc_entity.id, action.int_param, center.x, center.y, action.int_param2);
            // TODO: Apply damage to all entities in range
            break;
        }

        case action_type::heal_self:
            npc_ptr->heal(action.int_param);
            break;

        case action_type::teleport:
            npc_ptr->pos = action.pos;
            break;

        case action_type::set_aggro_range:
            npc_ptr->ai.aggro_range = static_cast<int16_t>(action.int_param);
            break;

        case action_type::set_invincible:
            // TODO: Add invincibility flag to NPC
            LOG_DEBUG(general, "NPC {} invincible: {}",
                npc_entity.id, action.bool_param);
            break;

        case action_type::play_effect:
            LOG_DEBUG(general, "NPC {} play effect '{}' at ({},{})",
                npc_entity.id, action.str_param, action.pos.x, action.pos.y);
            // TODO: Broadcast visual/sound effect
            break;

        case action_type::call_script:
            // TODO: Start sub-script (needs access to script_loader)
            LOG_DEBUG(general, "NPC {} call_script '{}'",
                npc_entity.id, action.str_param);
            break;
    }
}

void script_executor::advance(entity::entity /*npc_entity*/, script_state& state)
{
    ++state.current_action;
    state.action_start = std::chrono::steady_clock::now();
}

}  // namespace hb::npc::script
