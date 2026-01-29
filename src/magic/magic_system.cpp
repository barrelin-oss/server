// magic_system.cpp
// Magic/spell management implementation

#include "magic/magic_system.h"
#include "core/logger.h"

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

auto magic_system::can_cast(hb::entity::entity caster, spell_id spell_id, const cast_target& /*target*/) const
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

    // Mana/HP/SP checks would be done by querying player system
    // For now, assume sufficient resources

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

auto magic_system::calculate_mana_cost(hb::entity::entity /*caster*/, spell_id spell_id) const -> int32_t {
    const auto* spell = get_spell(spell_id);
    if (!spell) return 0;

    return static_cast<int32_t>(static_cast<float>(spell->mana_cost) * config_.global_mana_cost_modifier);
}

auto magic_system::calculate_damage(hb::entity::entity /*caster*/, spell_id spell_id) const -> int32_t {
    const auto* spell = get_spell(spell_id);
    if (!spell) return 0;

    // Base damage + stat scaling (would query player stats)
    int32_t damage = spell->base_damage;
    // damage += (player_int * spell->int_scaling / 100);
    // damage += (player_mag * spell->mag_scaling / 100);

    return static_cast<int32_t>(static_cast<float>(damage) * config_.global_damage_modifier);
}

auto magic_system::calculate_heal(hb::entity::entity /*caster*/, spell_id spell_id) const -> int32_t {
    const auto* spell = get_spell(spell_id);
    if (!spell) return 0;

    return spell->base_heal;
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

    // Apply effect based on spell category
    switch (spell.category) {
        case spell_category::attack:
            result.damage_dealt = calculate_damage(caster, spell.id);
            if (target.has_entity()) {
                result.affected_targets.push_back(target.target);
            }
            break;

        case spell_category::healing:
            result.heal_applied = calculate_heal(caster, spell.id);
            if (target.has_entity()) {
                result.affected_targets.push_back(target.target);
            }
            break;

        case spell_category::buff:
        case spell_category::debuff:
            // Would apply status effects via player/npc system
            if (target.has_entity()) {
                result.affected_targets.push_back(target.target);
            }
            break;

        default:
            break;
    }

    LOG_DEBUG(general, "Entity {} cast spell '{}' (damage: {}, heal: {})",
        caster.id, spell.name, result.damage_dealt, result.heal_applied);

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

}  // namespace hb::magic
