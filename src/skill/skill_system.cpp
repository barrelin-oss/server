// skill_system.cpp
// Skill management subsystem implementation

#include "skill/skill_system.h"
#include "core/logger.h"
#include "core/subsystem.h"
#include "item/item_system.h"
#include "item/item.h"

namespace hb::skill {

skill_system::skill_system() = default;

skill_system::~skill_system() {
    if (is_initialized()) {
        shutdown();
    }
}

void skill_system::initialize() {
    LOG_INFO(general, "Skill system initializing...");
    set_initialized(true);
    LOG_INFO(general, "Skill system initialized");
}

void skill_system::shutdown() {
    LOG_INFO(general, "Skill system shutting down...");

    player_skills_.clear();
    level_up_callbacks_.clear();

    set_initialized(false);
    LOG_INFO(general, "Skill system shutdown complete");
}

void skill_system::update(float /*delta_time*/) {
    // Skill decay could be implemented here
}

void skill_system::set_config(const skill_system_config& config) {
    config_ = config;
}

void skill_system::register_player(player_id id) {
    if (player_skills_.contains(id)) return;

    player_skills_.emplace(id, player_skills{});
    LOG_DEBUG(general, "Registered skills for player {}", id.value);
}

void skill_system::unregister_player(player_id id) {
    player_skills_.erase(id);
    LOG_DEBUG(general, "Unregistered skills for player {}", id.value);
}

auto skill_system::get_skill_level(player_id player, skill_type skill) const -> int16_t {
    auto it = player_skills_.find(player);
    if (it == player_skills_.end()) return 0;
    return it->second.level(skill);
}

auto skill_system::get_skill_exp(player_id player, skill_type skill) const -> int32_t {
    auto it = player_skills_.find(player);
    if (it == player_skills_.end()) return 0;
    return it->second.get(skill).experience;
}

auto skill_system::get_mastery(player_id player, skill_type skill) const -> mastery_level {
    return get_mastery_level(get_skill_level(player, skill));
}

void skill_system::set_skill_level(player_id player, skill_type skill, int16_t level) {
    auto it = player_skills_.find(player);
    if (it == player_skills_.end()) return;

    auto& skill_state = it->second.get(skill);
    int16_t old_level = skill_state.level;
    skill_state.level = level;
    skill_state.experience = 0;  // Reset exp when setting level directly

    if (level > old_level) {
        notify_level_up(player, skill, old_level, level);
    }
}

auto skill_system::add_skill_exp(player_id player, skill_type skill, int32_t amount) -> int16_t {
    auto it = player_skills_.find(player);
    if (it == player_skills_.end()) return 0;

    auto& skill_state = it->second.get(skill);
    int16_t old_level = skill_state.level;

    // Apply global modifier
    amount = static_cast<int32_t>(static_cast<float>(amount) * config_.global_exp_modifier);
    int16_t levels_gained = skill_state.add_experience(amount);

    if (levels_gained > 0) {
        notify_level_up(player, skill, old_level, skill_state.level);
        LOG_DEBUG(general, "Player {} gained {} levels in skill {} (now level {})",
            player.value, levels_gained, static_cast<int>(skill), skill_state.level);
    }

    return levels_gained;
}

void skill_system::reset_skill(player_id player, skill_type skill) {
    auto it = player_skills_.find(player);
    if (it == player_skills_.end()) return;

    auto& skill_state = it->second.get(skill);
    skill_state.level = 0;
    skill_state.experience = 0;
}

void skill_system::reset_all_skills(player_id player) {
    auto it = player_skills_.find(player);
    if (it == player_skills_.end()) return;

    for (size_t i = 0; i < max_skills; ++i) {
        auto& skill_state = it->second.skills[i];
        skill_state.level = 0;
        skill_state.experience = 0;
    }
}

auto skill_system::train_skill(player_id player, skill_type skill) -> skill_use_result {
    if (!can_train(player, skill)) {
        return skill_use_result::cooldown;
    }

    // Training grants a small amount of exp
    int32_t training_exp = 10;
    add_skill_exp(player, skill, training_exp);

    return skill_use_result::success;
}

auto skill_system::can_train(player_id player, skill_type /*skill*/) const -> bool {
    return player_skills_.contains(player);
}

auto skill_system::get_weapon_skill_for_item(player_id player, item_id weapon) const -> int16_t {
    auto* item_sys = subsystems().get<item::item_system>();
    if (!item_sys) {
        // Default to sword skill if no item system
        return get_skill_level(player, skill_type::sword);
    }

    auto* itm = item_sys->get_item(weapon);
    if (!itm || itm->type != item::item_type::weapon) {
        // If not a weapon or doesn't exist, use fist skill
        return get_skill_level(player, skill_type::fist);
    }

    // Map weapon type to skill type
    skill_type weapon_skill = skill_type::fist;
    switch (itm->weapon) {
        case item::weapon_type::sword:
            weapon_skill = skill_type::sword;
            break;
        case item::weapon_type::axe:
            weapon_skill = skill_type::axe;
            break;
        case item::weapon_type::hammer:
            weapon_skill = skill_type::hammer;
            break;
        case item::weapon_type::staff:
            weapon_skill = skill_type::staff;
            break;
        case item::weapon_type::wand:
            weapon_skill = skill_type::wand;
            break;
        case item::weapon_type::bow:
            weapon_skill = skill_type::bow;
            break;
        case item::weapon_type::dagger:
            weapon_skill = skill_type::dagger;
            break;
        case item::weapon_type::fist:
        default:
            weapon_skill = skill_type::fist;
            break;
    }

    return get_skill_level(player, weapon_skill);
}

auto skill_system::calculate_damage_bonus(player_id player, skill_type weapon_skill) const -> int16_t {
    int16_t skill_level = get_skill_level(player, weapon_skill);
    // 1 damage per 10 skill levels
    return skill_level / 10;
}

auto skill_system::calculate_hit_bonus(player_id player, skill_type weapon_skill) const -> int16_t {
    int16_t skill_level = get_skill_level(player, weapon_skill);
    // 1 hit per 5 skill levels
    return skill_level / 5;
}

void skill_system::on_level_up(level_up_callback callback) {
    level_up_callbacks_.push_back(std::move(callback));
}

auto skill_system::get_player_skills(player_id player) -> player_skills* {
    auto it = player_skills_.find(player);
    return it != player_skills_.end() ? &it->second : nullptr;
}

auto skill_system::get_player_skills(player_id player) const -> const player_skills* {
    auto it = player_skills_.find(player);
    return it != player_skills_.end() ? &it->second : nullptr;
}

void skill_system::notify_level_up(player_id player, skill_type skill, int16_t old_level, int16_t new_level) {
    skill_level_event event;
    event.player = player;
    event.skill = skill;
    event.old_level = old_level;
    event.new_level = new_level;

    for (const auto& callback : level_up_callbacks_) {
        callback(event);
    }
}

}  // namespace hb::skill
