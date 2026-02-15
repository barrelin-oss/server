// effect_system.cpp
// Spell effect management implementation

#include "effect/effect_system.h"
#include "core/logger.h"
#include "core/subsystem.h"
#include "player/player_system.h"
#include "perf/perf_stats.h"

#include <algorithm>
#include <chrono>

namespace hb::effect
{

effect_system::effect_system() = default;

effect_system::~effect_system()
{
    if (is_initialized())
    {
        shutdown();
    }
}

void effect_system::initialize()
{
    LOG_INFO(general, "Effect system initializing...");
    set_initialized(true);
    LOG_INFO(general, "Effect system initialized");
}

void effect_system::shutdown()
{
    LOG_INFO(general, "Effect system shutting down...");

    entities_.clear();
    applied_callbacks_.clear();
    removed_callbacks_.clear();
    tick_callbacks_.clear();

    set_initialized(false);
    LOG_INFO(general, "Effect system shutdown complete");
}

void effect_system::update(float /*delta_time*/)
{
    auto* perf = subsystems().get<perf::perf_stats_system>();
    PERF_TIMER(perf, perf::metric_category::effect_tick);

    auto now = get_current_time_ms();

    // Collect entities that need modifier recomputation
    std::vector<entity::entity> changed_entities;

    for (auto& [entity_key, ee] : entities_)
    {
        bool any_removed = false;

        // Process periodic ticks
        for (auto& eff : ee.effects)
        {
            if (eff.tick_interval_ms > 0 && now >= eff.last_tick_ms + eff.tick_interval_ms)
            {
                eff.last_tick_ms = now;
                for (const auto& cb : tick_callbacks_)
                {
                    cb(entity_key, eff);
                }
            }
        }

        // Collect expired effects
        std::vector<active_effect> expired;
        std::erase_if(ee.effects,
                      [&](const active_effect& eff)
                      {
                          if (eff.expires_at_ms > 0 && now >= eff.expires_at_ms)
                          {
                              expired.push_back(eff);
                              return true;
                          }
                          return false;
                      });

        if (!expired.empty())
        {
            any_removed = true;
            for (const auto& eff : expired)
            {
                LOG_DEBUG(magic,
                          "Effect {} expired on entity {} (spell {})",
                          eff.id.value,
                          entity_key.id,
                          eff.source_spell ? eff.source_spell->value : 0);
                for (const auto& cb : removed_callbacks_)
                {
                    cb(entity_key, eff);
                }
            }
        }

        if (any_removed || ee.dirty)
        {
            recompute_modifiers(ee);
            ee.dirty = false;
            changed_entities.push_back(entity_key);
        }
    }

    // Push updated modifiers to player system
    for (auto ent : changed_entities)
    {
        auto it = entities_.find(ent);
        if (it != entities_.end())
        {
            push_modifiers_to_entity(ent, it->second.cached_modifiers);
        }
    }

    // Clean up empty entries
    std::erase_if(entities_, [](const auto& pair) { return pair.second.effects.empty(); });
}

auto effect_system::apply_effect(const apply_effect_params& params) -> effect_id
{
    auto& ee = entities_[params.target];

    // Check if target already has an active effect in the same group
    for (const auto& eff : ee.effects)
    {
        if (eff.group == params.group)
        {
            LOG_DEBUG(magic,
                      "Effect group {} blocked on entity {} (slot occupied by effect {})",
                      static_cast<int>(params.group),
                      params.target.id,
                      eff.id.value);
            return effect_id{}; // Application blocked
        }
    }

    auto now = get_current_time_ms();
    auto id = next_effect_id();

    active_effect eff{};
    eff.id = id;
    eff.source = params.source;
    eff.source_spell = params.source_spell;
    eff.group = params.group;
    eff.type = params.type;
    eff.magnitude = params.magnitude;
    eff.applied_at_ms = now;
    eff.duration_ms = params.duration_ms;
    eff.expires_at_ms = params.duration_ms > 0 ? now + params.duration_ms : 0;
    eff.tick_interval_ms = params.tick_interval_ms;
    eff.last_tick_ms = now;
    eff.status_flag = map_status_flag(params.group, params.type);

    ee.effects.push_back(eff);
    recompute_modifiers(ee);
    push_modifiers_to_entity(params.target, ee.cached_modifiers);

    LOG_DEBUG(magic,
              "Applied effect {} (group={}, type={}, mag={}, dur={}ms) to entity {} from entity {}",
              id.value,
              static_cast<int>(params.group),
              static_cast<int>(params.type),
              params.magnitude,
              params.duration_ms,
              params.target.id,
              params.source.id);

    for (const auto& cb : applied_callbacks_)
    {
        cb(params.target, eff);
    }

    return id;
}

void effect_system::remove_effect(entity::entity target, effect_id id)
{
    auto it = entities_.find(target);
    if (it == entities_.end())
        return;

    auto& ee = it->second;
    auto eff_it =
        std::find_if(ee.effects.begin(), ee.effects.end(), [id](const active_effect& e) { return e.id == id; });

    if (eff_it == ee.effects.end())
        return;

    auto removed = *eff_it;
    ee.effects.erase(eff_it);
    recompute_modifiers(ee);
    push_modifiers_to_entity(target, ee.cached_modifiers);

    for (const auto& cb : removed_callbacks_)
    {
        cb(target, removed);
    }
}

void effect_system::remove_effects_by_group(entity::entity target, magic_type group)
{
    auto it = entities_.find(target);
    if (it == entities_.end())
        return;

    auto& ee = it->second;
    std::vector<active_effect> removed;

    std::erase_if(ee.effects,
                  [&](const active_effect& e)
                  {
                      if (e.group == group)
                      {
                          removed.push_back(e);
                          return true;
                      }
                      return false;
                  });

    if (!removed.empty())
    {
        recompute_modifiers(ee);
        push_modifiers_to_entity(target, ee.cached_modifiers);

        for (const auto& eff : removed)
        {
            for (const auto& cb : removed_callbacks_)
            {
                cb(target, eff);
            }
        }
    }
}

void effect_system::remove_all_effects(entity::entity target)
{
    auto it = entities_.find(target);
    if (it == entities_.end())
        return;

    auto& ee = it->second;
    auto all = std::move(ee.effects);
    ee.effects.clear();
    recompute_modifiers(ee);
    push_modifiers_to_entity(target, ee.cached_modifiers);

    for (const auto& eff : all)
    {
        for (const auto& cb : removed_callbacks_)
        {
            cb(target, eff);
        }
    }

    entities_.erase(it);
}

auto effect_system::has_effect_in_group(entity::entity target, magic_type group) const -> bool
{
    auto it = entities_.find(target);
    if (it == entities_.end())
        return false;

    for (const auto& eff : it->second.effects)
    {
        if (eff.group == group)
            return true;
    }
    return false;
}

auto effect_system::has_effect(entity::entity target, spell_effect_type type) const -> bool
{
    auto it = entities_.find(target);
    if (it == entities_.end())
        return false;

    for (const auto& eff : it->second.effects)
    {
        if (eff.type == type)
            return true;
    }
    return false;
}

auto effect_system::get_effects(entity::entity target) const -> const std::vector<active_effect>*
{
    auto it = entities_.find(target);
    return it != entities_.end() ? &it->second.effects : nullptr;
}

auto effect_system::get_effect_modifiers(entity::entity target) const -> const effect_modifier_result*
{
    auto it = entities_.find(target);
    return it != entities_.end() ? &it->second.cached_modifiers : nullptr;
}

auto effect_system::has_status(entity::entity target, player::player_status status) const -> bool
{
    auto it = entities_.find(target);
    if (it == entities_.end())
        return false;

    return (it->second.cached_modifiers.status & status) != player::player_status::none;
}

auto effect_system::effect_count(entity::entity target) const -> size_t
{
    auto it = entities_.find(target);
    return it != entities_.end() ? it->second.effects.size() : 0;
}

void effect_system::on_effect_applied(effect_applied_callback callback)
{
    applied_callbacks_.push_back(std::move(callback));
}

void effect_system::on_effect_removed(effect_removed_callback callback)
{
    removed_callbacks_.push_back(std::move(callback));
}

void effect_system::on_effect_tick(effect_tick_callback callback)
{
    tick_callbacks_.push_back(std::move(callback));
}

auto effect_system::next_effect_id() -> effect_id
{
    return effect_id{next_id_++};
}

auto effect_system::get_current_time_ms() const -> int64_t
{
    return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch())
        .count();
}

auto effect_system::map_status_flag(magic_type group, spell_effect_type type) const -> player::player_status
{
    // Map from magic_type group
    switch (group)
    {
    case magic_type::protection:
        return player::player_status::protection;
    case magic_type::hold_paralyze:
        return player::player_status::paralyzed;
    case magic_type::invisibility:
        return player::player_status::invisible;
    case magic_type::confusion:
        return player::player_status::cursed;
    case magic_type::poison:
        return player::player_status::poisoned;
    case magic_type::berserk:
        return player::player_status::berserk;
    case magic_type::inhibition:
        return player::player_status::silenced;
    default:
        break;
    }

    // Map from spell_effect_type
    switch (type)
    {
    case spell_effect_type::buff_attack:
        return player::player_status::attack_up;
    case spell_effect_type::buff_defense:
        return player::player_status::defense_up;
    case spell_effect_type::buff_speed:
        return player::player_status::haste;
    case spell_effect_type::debuff_slow:
        return player::player_status::slow;
    case spell_effect_type::stun:
        return player::player_status::stunned;
    case spell_effect_type::freeze:
        return player::player_status::frozen;
    case spell_effect_type::invisibility:
        return player::player_status::invisible;
    default:
        return player::player_status::none;
    }
}

void effect_system::recompute_modifiers(entity_effects& ee)
{
    ee.cached_modifiers = compute_effect_modifiers(ee.effects);
}

void effect_system::push_modifiers_to_entity(entity::entity target, const effect_modifier_result& mods)
{
    auto* player_sys = subsystems().get<player::player_system>();
    if (!player_sys)
        return;

    // Check if this entity is a player
    player_id pid{target.id};
    auto* p = player_sys->get_player(pid);
    if (!p)
        return;

    // Update effect modifiers on player
    player_sys->set_effect_modifiers(pid, mods.modifiers);
    player_sys->set_effect_status(pid, mods.status);
}

} // namespace hb::effect
