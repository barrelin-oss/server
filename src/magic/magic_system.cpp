// magic_system.cpp
// Magic/spell management implementation

#include "magic/magic_system.h"
#include "core/logger.h"
#include "core/subsystem.h"
#include "player/player_system.h"
#include "npc/npc_system.h"
#include "combat/combat_system.h"
#include "world/world_subsystem.h"

#include <chrono>
#include <algorithm>

namespace hb::magic {

magic_system::magic_system() = default;

magic_system::~magic_system() {
    if (is_initialized()) {
        shutdown();
    }
}

void magic_system::initialize() {
    LOG_INFO(general, "Magic system initializing...");
    set_initialized(true);
    LOG_INFO(general, "Magic system initialized");
}

void magic_system::shutdown() {
    LOG_INFO(general, "Magic system shutting down...");

    spells_.clear();
    active_casts_.clear();
    player_spells_.clear();
    spell_callbacks_.clear();

    set_initialized(false);
    LOG_INFO(general, "Magic system shutdown complete");
}

void magic_system::update(float delta_time) {
    process_active_casts(delta_time);
}

void magic_system::set_config(const magic_system_config& config) {
    config_ = config;
}

auto magic_system::begin_cast(hb::entity::entity caster, spell_id spell_id, const cast_target& target)
    -> result<cast_result, std::string>
{
    auto check = can_cast(caster, spell_id, target);
    if (check != cast_result::success) {
        return result<cast_result, std::string>::ok(check);
    }

    const auto* spell = get_spell(spell_id);
    if (!spell) {
        return result<cast_result, std::string>::err("Spell not found");
    }

    // Instant cast spells
    if (spell->cast_time_ms <= 0) {
        auto effect_result = apply_spell_effect(caster, *spell, target);
        notify_spell_cast(caster, *spell, effect_result);
        return result<cast_result, std::string>::ok(cast_result::success);
    }

    // Start casting
    auto now = get_current_time_ms();
    spell_cast_state state;
    state.spell = spell_id;
    state.target = target;
    state.start_time_ms = now;
    state.end_time_ms = now + spell->cast_time_ms;
    state.cancelled = false;

    active_casts_[caster] = state;

    LOG_DEBUG(general, "Entity {} began casting spell {}", caster.id, spell_id.value);

    return result<cast_result, std::string>::ok(cast_result::success);
}

void magic_system::cancel_cast(hb::entity::entity caster) {
    auto it = active_casts_.find(caster);
    if (it != active_casts_.end()) {
        it->second.cancel();
        active_casts_.erase(it);
        LOG_DEBUG(general, "Entity {} cancelled cast", caster.id);
    }
}

auto magic_system::instant_cast(hb::entity::entity caster, spell_id spell_id, const cast_target& target)
    -> result<spell_effect_result, std::string>
{
    auto check = can_cast(caster, spell_id, target);
    if (check != cast_result::success) {
        return result<spell_effect_result, std::string>::err("Cannot cast spell");
    }

    const auto* spell = get_spell(spell_id);
    if (!spell) {
        return result<spell_effect_result, std::string>::err("Spell not found");
    }

    auto effect_result = apply_spell_effect(caster, *spell, target);
    notify_spell_cast(caster, *spell, effect_result);

    return result<spell_effect_result, std::string>::ok(effect_result);
}

auto magic_system::can_cast(hb::entity::entity caster, spell_id spell_id, const cast_target& target) const
    -> cast_result
{
    if (!caster.is_valid()) {
        return cast_result::failed;
    }

    // Check if already casting
    if (is_casting(caster)) {
        return cast_result::failed;
    }

    // Check if spell exists
    const auto* spell = get_spell(spell_id);
    if (!spell) {
        return cast_result::spell_not_learned;
    }

    // Check if spell is known
    if (!knows_spell(caster, spell_id)) {
        return cast_result::spell_not_learned;
    }

    // Check cooldown
    if (get_cooldown_remaining(caster, spell_id) > 0) {
        return cast_result::on_cooldown;
    }

    // Check mana, HP, SP requirements
    auto* player_sys = subsystems().get<player::player_system>();
    if (player_sys) {
        if (auto* p = player_sys->get_player(player_id{caster.id})) {
            // Check if silenced
            if (p->has_status(player::player_status::silenced)) {
                return cast_result::silenced;
            }

            // Check mana
            int32_t mana_cost = calculate_mana_cost(caster, spell_id);
            if (p->mp < mana_cost) {
                return cast_result::not_enough_mana;
            }

            // Check level requirement
            if (p->experience.level < spell->level_requirement) {
                return cast_result::level_too_low;
            }

            // Check stat requirements
            if (p->computed.intelligence < spell->int_requirement ||
                p->computed.magic < spell->mag_requirement) {
                return cast_result::level_too_low;
            }
        }
    }

    // Check range for targeted spells
    if (target.has_entity() && spell->range > 0) {
        // Would check distance between caster and target
        // For now, assume in range
    }

    return cast_result::success;
}

auto magic_system::is_casting(hb::entity::entity caster) const -> bool {
    auto it = active_casts_.find(caster);
    return it != active_casts_.end() && it->second.is_active();
}

auto magic_system::get_cast_state(hb::entity::entity caster) const -> const spell_cast_state* {
    auto it = active_casts_.find(caster);
    return it != active_casts_.end() ? &it->second : nullptr;
}

void magic_system::learn_spell(hb::entity::entity caster, spell_id spell) {
    auto& spells = player_spells_[caster];

    // Check if already known
    for (const auto& known : spells) {
        if (known.spell == spell) return;
    }

    spell_knowledge knowledge;
    knowledge.spell = spell;
    knowledge.level = 1;
    spells.push_back(knowledge);

    LOG_DEBUG(general, "Entity {} learned spell {}", caster.id, spell.value);
}

void magic_system::forget_spell(hb::entity::entity caster, spell_id spell) {
    auto it = player_spells_.find(caster);
    if (it == player_spells_.end()) return;

    auto& spells = it->second;
    std::erase_if(spells, [spell](const spell_knowledge& k) {
        return k.spell == spell;
    });
}

auto magic_system::knows_spell(hb::entity::entity caster, spell_id spell) const -> bool {
    auto it = player_spells_.find(caster);
    if (it == player_spells_.end()) return false;

    for (const auto& known : it->second) {
        if (known.spell == spell) return true;
    }
    return false;
}

auto magic_system::get_spell_level(hb::entity::entity caster, spell_id spell) const -> int16_t {
    auto it = player_spells_.find(caster);
    if (it == player_spells_.end()) return 0;

    for (const auto& known : it->second) {
        if (known.spell == spell) return known.level;
    }
    return 0;
}

void magic_system::level_up_spell(hb::entity::entity caster, spell_id spell) {
    auto it = player_spells_.find(caster);
    if (it == player_spells_.end()) return;

    for (auto& known : it->second) {
        if (known.spell == spell) {
            ++known.level;
            return;
        }
    }
}

auto magic_system::get_player_spells(hb::entity::entity caster) const -> const std::vector<spell_knowledge>* {
    auto it = player_spells_.find(caster);
    return it != player_spells_.end() ? &it->second : nullptr;
}

void magic_system::set_player_spells(hb::entity::entity caster, std::vector<spell_knowledge> spells) {
    player_spells_[caster] = std::move(spells);
}

void magic_system::register_spell(const spell_template& spell) {
    spells_[spell.id] = spell;
    LOG_DEBUG(general, "Registered spell '{}' (id={})", spell.name, spell.id.value);
}

auto magic_system::get_spell(spell_id id) const -> const spell_template* {
    auto it = spells_.find(id);
    return it != spells_.end() ? &it->second : nullptr;
}

auto magic_system::get_cooldown_remaining(hb::entity::entity caster, spell_id spell_id) const -> int32_t {
    auto it = player_spells_.find(caster);
    if (it == player_spells_.end()) return 0;

    const auto* spell = get_spell(spell_id);
    if (!spell) return 0;

    for (const auto& known : it->second) {
        if (known.spell == spell_id) {
            return known.cooldown_remaining(get_current_time_ms(), spell->cooldown_ms);
        }
    }
    return 0;
}

void magic_system::reset_cooldown(hb::entity::entity caster, spell_id spell) {
    auto it = player_spells_.find(caster);
    if (it == player_spells_.end()) return;

    for (auto& known : it->second) {
        if (known.spell == spell) {
            known.last_cast_time_ms = 0;
            return;
        }
    }
}

void magic_system::reset_all_cooldowns(hb::entity::entity caster) {
    auto it = player_spells_.find(caster);
    if (it == player_spells_.end()) return;

    for (auto& known : it->second) {
        known.last_cast_time_ms = 0;
    }
}

void magic_system::on_spell_cast(spell_callback callback) {
    spell_callbacks_.push_back(std::move(callback));
}

auto magic_system::calculate_mana_cost(hb::entity::entity caster, spell_id spell_id) const -> int32_t {
    const auto* spell = get_spell(spell_id);
    if (!spell) return 0;

    int32_t base_cost = spell->mana_cost;

    // Spell level can reduce mana cost
    int16_t spell_level = get_spell_level(caster, spell_id);
    if (spell_level > 1) {
        // 2% reduction per level above 1
        float reduction = 1.0f - (spell_level - 1) * 0.02f;
        reduction = std::max(0.5f, reduction);  // Cap at 50% reduction
        base_cost = static_cast<int32_t>(base_cost * reduction);
    }

    return static_cast<int32_t>(static_cast<float>(base_cost) * config_.global_mana_cost_modifier);
}

auto magic_system::calculate_damage(hb::entity::entity caster, spell_id spell_id) const -> int32_t {
    const auto* spell = get_spell(spell_id);
    if (!spell) return 0;

    int32_t damage = spell->base_damage;

    // Get caster stats for scaling
    auto* player_sys = subsystems().get<player::player_system>();
    if (player_sys) {
        if (auto* p = player_sys->get_player(player_id{caster.id})) {
            // Apply INT scaling
            if (spell->int_scaling > 0) {
                damage += p->computed.intelligence * spell->int_scaling / 100;
            }
            // Apply MAG scaling
            if (spell->mag_scaling > 0) {
                damage += p->computed.magic * spell->mag_scaling / 100;
            }
            // Add magic power
            damage += p->computed.magic_power / 2;
        }
    }

    // Spell level bonus (5% per level)
    int16_t spell_level = get_spell_level(caster, spell_id);
    if (spell_level > 1) {
        float bonus = 1.0f + (spell_level - 1) * 0.05f;
        damage = static_cast<int32_t>(damage * bonus);
    }

    return static_cast<int32_t>(static_cast<float>(damage) * config_.global_damage_modifier);
}

auto magic_system::calculate_heal(hb::entity::entity caster, spell_id spell_id) const -> int32_t {
    const auto* spell = get_spell(spell_id);
    if (!spell) return 0;

    int32_t heal = spell->base_heal;

    // Get caster stats for scaling
    auto* player_sys = subsystems().get<player::player_system>();
    if (player_sys) {
        if (auto* p = player_sys->get_player(player_id{caster.id})) {
            // Healing scales with INT and MAG
            if (spell->int_scaling > 0) {
                heal += p->computed.intelligence * spell->int_scaling / 100;
            }
            if (spell->mag_scaling > 0) {
                heal += p->computed.magic * spell->mag_scaling / 100;
            }
        }
    }

    // Spell level bonus
    int16_t spell_level = get_spell_level(caster, spell_id);
    if (spell_level > 1) {
        float bonus = 1.0f + (spell_level - 1) * 0.05f;
        heal = static_cast<int32_t>(heal * bonus);
    }

    return heal;
}

void magic_system::process_active_casts(float /*delta_time*/) {
    auto now = get_current_time_ms();

    std::vector<hb::entity::entity> completed;

    for (auto& [caster, state] : active_casts_) {
        if (!state.is_active()) continue;

        if (state.is_complete(now)) {
            const auto* spell = get_spell(state.spell);
            if (spell) {
                auto result = apply_spell_effect(caster, *spell, state.target);
                notify_spell_cast(caster, *spell, result);
            }
            completed.push_back(caster);
        }
    }

    for (auto caster : completed) {
        active_casts_.erase(caster);
    }
}

auto magic_system::apply_spell_effect(hb::entity::entity caster, const spell_template& spell, const cast_target& target)
    -> spell_effect_result
{
    spell_effect_result result;
    result.success = true;

    // Deduct mana cost from caster
    auto* player_sys = subsystems().get<player::player_system>();
    if (player_sys) {
        if (auto* p = player_sys->get_player(player_id{caster.id})) {
            int32_t mana_cost = calculate_mana_cost(caster, spell.id);
            if (!p->spend_mp(mana_cost)) {
                result.success = false;
                return result;
            }

            // Deduct HP cost if any
            if (spell.hp_cost > 0) {
                p->damage_hp(spell.hp_cost);
            }

            // Deduct SP cost if any
            if (spell.sp_cost > 0) {
                p->spend_sp(spell.sp_cost);
            }
        }
    }

    // Update cooldown
    auto it = player_spells_.find(caster);
    if (it != player_spells_.end()) {
        for (auto& known : it->second) {
            if (known.spell == spell.id) {
                known.last_cast_time_ms = get_current_time_ms();
                known.total_casts++;
                break;
            }
        }
    }

    // Get targets for AOE spells
    std::vector<hb::entity::entity> targets;
    if (spell.is_aoe()) {
        targets = find_aoe_targets(caster, spell, target);
    } else if (target.has_entity()) {
        targets.push_back(target.target);
    }

    // Apply effect based on spell category
    auto* combat_sys = subsystems().get<combat::combat_system>();

    switch (spell.category) {
        case spell_category::attack: {
            result.damage_dealt = calculate_damage(caster, spell.id);

            // Convert element to damage type
            auto damage_type = element_to_damage_type(spell.element);

            for (auto& t : targets) {
                if (combat_sys) {
                    // Deal magic damage through combat system
                    combat_sys->deal_damage(t, result.damage_dealt, damage_type, caster);
                }
                result.affected_targets.push_back(t);
            }
            break;
        }

        case spell_category::healing: {
            result.heal_applied = calculate_heal(caster, spell.id);

            for (auto& t : targets) {
                // Apply healing
                if (player_sys) {
                    player_sys->apply_heal(player_id{t.id}, result.heal_applied);
                }
                result.affected_targets.push_back(t);
            }
            break;
        }

        case spell_category::buff: {
            for (auto& t : targets) {
                apply_buff(t, spell);
                result.affected_targets.push_back(t);
            }
            break;
        }

        case spell_category::debuff: {
            for (auto& t : targets) {
                apply_debuff(t, spell);
                result.affected_targets.push_back(t);
            }
            break;
        }

        case spell_category::defense: {
            // Self-targeted defensive spells
            apply_buff(caster, spell);
            result.affected_targets.push_back(caster);
            break;
        }

        case spell_category::utility: {
            // Handle utility spells (teleport, create food, etc.)
            handle_utility_spell(caster, spell, target);
            result.affected_targets.push_back(caster);
            break;
        }

        default:
            break;
    }

    LOG_DEBUG(general, "Entity {} cast spell '{}' (damage: {}, heal: {}, targets: {})",
        caster.id, spell.name, result.damage_dealt, result.heal_applied, result.affected_targets.size());

    return result;
}

void magic_system::notify_spell_cast(hb::entity::entity caster, const spell_template& spell, const spell_effect_result& result) {
    for (const auto& callback : spell_callbacks_) {
        callback(caster, spell, result);
    }
}

int64_t magic_system::get_current_time_ms() const {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
}

auto magic_system::find_aoe_targets(hb::entity::entity caster, const spell_template& spell,
                                     const cast_target& target) const
    -> std::vector<hb::entity::entity>
{
    std::vector<hb::entity::entity> targets;

    // Determine center point for AOE
    hb::world::position center;
    if (target.has_position()) {
        center = target.target_pos;
    } else if (target.has_entity()) {
        // Get target entity position
        auto* player_sys = subsystems().get<player::player_system>();
        if (player_sys) {
            if (auto* p = player_sys->get_player(player_id{target.target.id})) {
                center = p->pos;
            }
        }
    }

    // Get caster's map
    map_id caster_map{};
    auto* player_sys = subsystems().get<player::player_system>();
    if (player_sys) {
        if (auto* p = player_sys->get_player(player_id{caster.id})) {
            caster_map = p->current_map;
        }
    }

    if (!caster_map.is_valid()) {
        return targets;
    }

    // Find entities in range based on target type
    int16_t radius = spell.aoe_radius > 0 ? spell.aoe_radius : 3;

    // Get players in range
    if (spell.target_type == spell_target::aoe_enemy ||
        spell.target_type == spell_target::aoe_all) {
        if (player_sys) {
            player_sys->for_each_player([&](player_id id, const player::player& p) {
                if (p.current_map == caster_map &&
                    p.pos.manhattan_distance(center) <= radius) {
                    // For enemy AOE, skip allies (same faction)
                    if (spell.target_type == spell_target::aoe_enemy) {
                        auto* caster_player = player_sys->get_player(player_id{caster.id});
                        if (caster_player && p.faction == caster_player->faction) {
                            return;  // Skip allies
                        }
                    }
                    targets.push_back(hb::entity::entity{id.value, 0});
                }
            });
        }
    }

    if (spell.target_type == spell_target::aoe_ally ||
        spell.target_type == spell_target::aoe_all) {
        if (player_sys) {
            player_sys->for_each_player([&](player_id id, const player::player& p) {
                if (p.current_map == caster_map &&
                    p.pos.manhattan_distance(center) <= radius) {
                    // For ally AOE, only include same faction
                    if (spell.target_type == spell_target::aoe_ally) {
                        auto* caster_player = player_sys->get_player(player_id{caster.id});
                        if (caster_player && p.faction != caster_player->faction) {
                            return;  // Skip enemies
                        }
                    }
                    // Avoid duplicates
                    bool already_added = false;
                    for (const auto& t : targets) {
                        if (t.id == id.value) {
                            already_added = true;
                            break;
                        }
                    }
                    if (!already_added) {
                        targets.push_back(hb::entity::entity{id.value, 0});
                    }
                }
            });
        }
    }

    // Get NPCs in range for enemy AOE
    if (spell.target_type == spell_target::aoe_enemy ||
        spell.target_type == spell_target::aoe_all) {
        auto* npc_sys = subsystems().get<npc::npc_system>();
        if (npc_sys) {
            auto npcs_in_range = npc_sys->get_npcs_in_range(caster_map, center, radius);
            for (auto npc_entity : npcs_in_range) {
                targets.push_back(npc_entity);
            }
        }
    }

    return targets;
}

auto magic_system::element_to_damage_type(spell_element element) const -> combat::damage_type {
    switch (element) {
        case spell_element::fire:      return combat::damage_type::fire;
        case spell_element::ice:       return combat::damage_type::ice;
        case spell_element::lightning: return combat::damage_type::lightning;
        case spell_element::holy:      return combat::damage_type::holy;
        case spell_element::dark:      return combat::damage_type::dark;
        default:                       return combat::damage_type::magic;
    }
}

void magic_system::apply_buff(hb::entity::entity target, const spell_template& spell) {
    auto* player_sys = subsystems().get<player::player_system>();
    if (!player_sys) return;

    auto* p = player_sys->get_player(player_id{target.id});
    if (!p) return;

    // Apply status based on spell name/id
    // This is simplified - real implementation would have buff types
    switch (spell.id.value) {
        case 10:  // Protection spell
            p->add_status(player::player_status::protection);
            break;
        case 11:  // Defense Up
            p->add_status(player::player_status::defense_up);
            break;
        case 12:  // Attack Up
            p->add_status(player::player_status::attack_up);
            break;
        case 13:  // Magic Up
            p->add_status(player::player_status::magic_up);
            break;
        case 14:  // Haste
            p->add_status(player::player_status::haste);
            break;
        case 15:  // Invisibility
            p->add_status(player::player_status::invisible);
            break;
        default:
            // Generic buff effect
            break;
    }

    LOG_DEBUG(general, "Applied buff from spell '{}' to entity {}", spell.name, target.id);
}

void magic_system::apply_debuff(hb::entity::entity target, const spell_template& spell) {
    auto* player_sys = subsystems().get<player::player_system>();
    if (!player_sys) return;

    auto* p = player_sys->get_player(player_id{target.id});
    if (!p) return;

    // Apply negative status based on spell
    switch (spell.id.value) {
        case 20:  // Poison
            p->add_status(player::player_status::poisoned);
            break;
        case 21:  // Paralyze
            p->add_status(player::player_status::paralyzed);
            break;
        case 22:  // Freeze
            p->add_status(player::player_status::frozen);
            break;
        case 23:  // Slow
            p->add_status(player::player_status::slow);
            break;
        case 24:  // Curse
            p->add_status(player::player_status::cursed);
            break;
        case 25:  // Silence
            p->add_status(player::player_status::silenced);
            break;
        default:
            break;
    }

    LOG_DEBUG(general, "Applied debuff from spell '{}' to entity {}", spell.name, target.id);
}

void magic_system::handle_utility_spell(hb::entity::entity caster, const spell_template& spell,
                                         const cast_target& /*target*/) {
    // Handle utility spells like teleport, create food, recall, etc.
    LOG_DEBUG(general, "Entity {} used utility spell '{}'", caster.id, spell.name);

    // Would implement specific utility spell effects here
    // For example:
    // - Teleport: Move player to specific location
    // - Recall: Return to town
    // - Create Food: Add food item to inventory
}

}  // namespace hb::magic
