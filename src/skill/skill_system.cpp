// skill_system.cpp
// Skill management subsystem implementation

#include "skill/skill_system.h"
#include "core/logger.h"
#include "core/subsystem.h"
#include "item/item_system.h"
#include "item/item.h"
#include "perf/perf_stats.h"

#include <yaml-cpp/yaml.h>
#include <fstream>
#include <filesystem>
#include <optional>

namespace hb::skill
{

namespace
{

auto skill_type_from_string(std::string_view name) -> std::optional<skill_type>
{
    if (name == "mining")
        return skill_type::mining;
    if (name == "fishing")
        return skill_type::fishing;
    if (name == "farming")
        return skill_type::farming;
    if (name == "magic_resistance")
        return skill_type::magic_resistance;
    if (name == "magic")
        return skill_type::magic;
    if (name == "hand_attack")
        return skill_type::hand_attack;
    if (name == "archery")
        return skill_type::archery;
    if (name == "short_sword")
        return skill_type::short_sword;
    if (name == "long_sword")
        return skill_type::long_sword;
    if (name == "fencing")
        return skill_type::fencing;
    if (name == "axe")
        return skill_type::axe;
    if (name == "shield")
        return skill_type::shield;
    if (name == "alchemy")
        return skill_type::alchemy;
    if (name == "manufacturing")
        return skill_type::manufacturing;
    if (name == "hammer")
        return skill_type::hammer;
    if (name == "crafting")
        return skill_type::crafting;
    if (name == "pretend_corpse")
        return skill_type::pretend_corpse;
    if (name == "staff")
        return skill_type::staff;
    if (name == "poison_resistance")
        return skill_type::poison_resistance;
    return std::nullopt;
}

} // namespace

skill_system::skill_system()
{
    // Set up default tier table
    default_config_.max_level = 100;
    default_config_.tiers = {{20, 10}, {40, 25}, {60, 50}, {80, 75}, {90, 100}, {200, 125}};
}

skill_system::~skill_system()
{
    if (is_initialized())
    {
        shutdown();
    }
}

void skill_system::initialize()
{
    LOG_INFO(general, "Skill system initializing...");
    set_initialized(true);
    LOG_INFO(general, "Skill system initialized");
}

void skill_system::shutdown()
{
    LOG_INFO(general, "Skill system shutting down...");

    player_skills_.clear();
    level_up_callbacks_.clear();
    progress_callbacks_.clear();
    progress_tracking_.clear();

    set_initialized(false);
    LOG_INFO(general, "Skill system shutdown complete");
}

void skill_system::update(float /*delta_time*/) {}

void skill_system::set_config(const skill_system_config& config)
{
    config_ = config;
}

void skill_system::load_config(const std::string& yaml_path)
{
    if (!std::filesystem::exists(yaml_path))
    {
        LOG_WARN(general, "Skills config not found: {} (using defaults)", yaml_path);
        return;
    }

    try
    {
        auto root = YAML::LoadFile(yaml_path);

        // Parse defaults section
        if (auto def = root["defaults"])
        {
            default_config_.max_level = def["max_level"].as<int16_t>(100);
            if (auto tiers_node = def["tiers"])
            {
                std::vector<skill_tier> tiers;
                for (const auto& t : tiers_node)
                {
                    skill_tier tier;
                    tier.max_level = t["max_level"].as<int16_t>(200);
                    tier.multiplier = t["multiplier"].as<int32_t>(50);
                    tiers.push_back(tier);
                }
                default_config_.tiers = std::move(tiers);
            }
        }

        // Parse per-skill tier overrides (skill_tiers: section)
        if (auto overrides = root["skill_tiers"])
        {
            for (auto it = overrides.begin(); it != overrides.end(); ++it)
            {
                auto key = it->first.as<std::string>();
                auto st = skill_type_from_string(key);
                if (!st)
                {
                    LOG_WARN(general, "Unknown skill name in config: {}", key);
                    continue;
                }

                auto val = it->second;
                skill_config_entry entry;
                entry.max_level = val["max_level"].as<int16_t>(default_config_.max_level);
                if (auto tiers_node = val["tiers"])
                {
                    std::vector<skill_tier> tiers;
                    for (const auto& t : tiers_node)
                    {
                        skill_tier tier;
                        tier.max_level = t["max_level"].as<int16_t>(200);
                        tier.multiplier = t["multiplier"].as<int32_t>(50);
                        tiers.push_back(tier);
                    }
                    entry.tiers = std::move(tiers);
                }
                else
                {
                    entry.tiers = default_config_.tiers;
                }
                skill_configs_[*st] = std::move(entry);
            }
        }

        LOG_INFO(general, "Loaded skill config from {} ({} per-skill overrides)", yaml_path, skill_configs_.size());
    }
    catch (const std::exception& e)
    {
        LOG_ERROR(general, "Failed to parse skills config {}: {}", yaml_path, e.what());
    }
}

void skill_system::register_player(player_id id)
{
    if (player_skills_.contains(id))
        return;

    player_skills_.emplace(id, player_skills{});
    progress_tracking_.emplace(id, progress_tracking{});
    LOG_DEBUG(general, "Registered skills for player {}", id.value);
}

void skill_system::unregister_player(player_id id)
{
    player_skills_.erase(id);
    progress_tracking_.erase(id);
    LOG_DEBUG(general, "Unregistered skills for player {}", id.value);
}

auto skill_system::get_skill_level(player_id player, skill_type skill) const -> int16_t
{
    auto it = player_skills_.find(player);
    if (it == player_skills_.end())
        return 0;
    return it->second.level(skill);
}

auto skill_system::get_uses_this_level(player_id player, skill_type skill) const -> int32_t
{
    auto it = player_skills_.find(player);
    if (it == player_skills_.end())
        return 0;
    return it->second.get(skill).uses_this_level;
}

auto skill_system::get_total_uses(player_id player, skill_type skill) const -> int32_t
{
    auto it = player_skills_.find(player);
    if (it == player_skills_.end())
        return 0;
    return it->second.get(skill).total_uses;
}

auto skill_system::get_mastery(player_id player, skill_type skill) const -> mastery_level
{
    return get_mastery_level(get_skill_level(player, skill));
}

auto skill_system::uses_to_next_level(skill_type type, int16_t level) const -> int32_t
{
    const auto& cfg = get_config_for(type);

    // Find the tier bracket for this level
    int32_t multiplier = 125; // fallback if no tier matches
    for (const auto& tier : cfg.tiers)
    {
        if (level < tier.max_level)
        {
            multiplier = tier.multiplier;
            break;
        }
    }

    return static_cast<int32_t>(level + 1) * multiplier;
}

void skill_system::set_skill_level(player_id player, skill_type skill, int16_t level)
{
    auto it = player_skills_.find(player);
    if (it == player_skills_.end())
        return;

    auto& ss = it->second.get(skill);
    int16_t old_level = ss.level;
    ss.level = level;
    ss.uses_this_level = 0;

    if (level > old_level)
    {
        notify_level_up(player, skill, old_level, level);
    }
}

auto skill_system::record_skill_use(player_id player, skill_type skill) -> int16_t
{
    auto* perf = subsystems().get<perf::perf_stats_system>();
    PERF_TIMER(perf, perf::metric_category::skill_training);

    auto it = player_skills_.find(player);
    if (it == player_skills_.end())
        return 0;

    auto& ss = it->second.get(skill);
    const auto& cfg = get_config_for(skill);

    if (ss.level >= cfg.max_level)
        return 0;

    ss.total_uses++;
    ss.uses_this_level++;

    int16_t old_level = ss.level;
    int16_t levels_gained = 0;

    while (ss.uses_this_level >= uses_to_next_level(skill, ss.level) && ss.level < cfg.max_level)
    {
        ss.uses_this_level -= uses_to_next_level(skill, ss.level);
        ++ss.level;
        ++levels_gained;
    }

    if (levels_gained > 0)
    {
        notify_level_up(player, skill, old_level, ss.level);
        LOG_DEBUG(general,
                  "Player {} gained {} levels in skill {} (now level {})",
                  player.value,
                  levels_gained,
                  static_cast<int>(skill),
                  ss.level);
    }

    check_progress(player, skill);

    return levels_gained;
}

void skill_system::add_skill_uses(player_id player, skill_type skill, int32_t count)
{
    auto it = player_skills_.find(player);
    if (it == player_skills_.end())
        return;

    auto& ss = it->second.get(skill);
    const auto& cfg = get_config_for(skill);

    if (ss.level >= cfg.max_level)
        return;

    int16_t old_level = ss.level;

    ss.total_uses += count;
    ss.uses_this_level += count;

    while (ss.uses_this_level >= uses_to_next_level(skill, ss.level) && ss.level < cfg.max_level)
    {
        ss.uses_this_level -= uses_to_next_level(skill, ss.level);
        ++ss.level;
    }

    if (ss.level > old_level)
    {
        notify_level_up(player, skill, old_level, ss.level);
        LOG_DEBUG(general,
                  "Player {} bulk leveled skill {} from {} to {}",
                  player.value,
                  static_cast<int>(skill),
                  old_level,
                  ss.level);
    }

    check_progress(player, skill);
}

void skill_system::reset_skill(player_id player, skill_type skill)
{
    auto it = player_skills_.find(player);
    if (it == player_skills_.end())
        return;

    auto& ss = it->second.get(skill);
    ss.level = 0;
    ss.total_uses = 0;
    ss.uses_this_level = 0;
}

void skill_system::reset_all_skills(player_id player)
{
    auto it = player_skills_.find(player);
    if (it == player_skills_.end())
        return;

    for (size_t i = 0; i < max_skills; ++i)
    {
        auto& ss = it->second.skills[i];
        ss.level = 0;
        ss.total_uses = 0;
        ss.uses_this_level = 0;
    }
}

auto skill_system::train_skill(player_id player, skill_type skill) -> skill_use_result
{
    if (!can_train(player, skill))
    {
        return skill_use_result::cooldown;
    }

    record_skill_use(player, skill);
    return skill_use_result::success;
}

auto skill_system::can_train(player_id player, skill_type /*skill*/) const -> bool
{
    return player_skills_.contains(player);
}

auto skill_system::get_weapon_skill_for_item(player_id player, item_id weapon) const -> int16_t
{
    auto* item_sys = subsystems().get<item::item_system>();
    if (!item_sys)
    {
        return get_skill_level(player, skill_type::short_sword);
    }

    auto* itm = item_sys->get_item(weapon);
    if (!itm || itm->type != item::item_type::weapon)
    {
        return get_skill_level(player, skill_type::hand_attack);
    }

    auto weapon_skill = weapon_type_to_skill_type(itm->weapon);
    return get_skill_level(player, weapon_skill);
}

auto skill_system::calculate_damage_bonus(player_id player, skill_type weapon_skill) const -> int16_t
{
    int16_t skill_level = get_skill_level(player, weapon_skill);
    return skill_level / 10;
}

auto skill_system::calculate_hit_bonus(player_id player, skill_type weapon_skill) const -> int16_t
{
    int16_t skill_level = get_skill_level(player, weapon_skill);
    return skill_level / 5;
}

void skill_system::on_level_up(level_up_callback callback)
{
    level_up_callbacks_.push_back(std::move(callback));
}

auto skill_system::get_player_skills(player_id player) -> player_skills*
{
    auto it = player_skills_.find(player);
    return it != player_skills_.end() ? &it->second : nullptr;
}

auto skill_system::get_player_skills(player_id player) const -> const player_skills*
{
    auto it = player_skills_.find(player);
    return it != player_skills_.end() ? &it->second : nullptr;
}

void skill_system::notify_level_up(player_id player, skill_type skill, int16_t old_level, int16_t new_level)
{
    // Reset progress tracking on level-up
    auto tracking_it = progress_tracking_.find(player);
    if (tracking_it != progress_tracking_.end())
    {
        tracking_it->second.last_reported_percent[static_cast<size_t>(skill)] = 0;
    }

    skill_level_event event;
    event.player = player;
    event.skill = skill;
    event.old_level = old_level;
    event.new_level = new_level;

    for (const auto& callback : level_up_callbacks_)
    {
        callback(event);
    }
}

void skill_system::on_skill_progress(progress_callback callback)
{
    progress_callbacks_.push_back(std::move(callback));
}

void skill_system::check_progress(player_id player, skill_type skill)
{
    if (progress_callbacks_.empty())
        return;

    auto skills_it = player_skills_.find(player);
    if (skills_it == player_skills_.end())
        return;

    const auto& ss = skills_it->second.get(skill);
    auto next = uses_to_next_level(skill, ss.level);
    if (next <= 0)
        return;

    auto raw_percent = static_cast<uint8_t>(
        (static_cast<int64_t>(ss.uses_this_level) * 100) / next);
    auto floored = static_cast<uint8_t>((raw_percent / 5) * 5);

    auto& tracking = progress_tracking_[player];
    auto skill_idx = static_cast<size_t>(skill);
    if (floored == tracking.last_reported_percent[skill_idx])
        return;

    // Fire for each 5% step crossed (handles bulk adds)
    auto old_pct = tracking.last_reported_percent[skill_idx];
    while (old_pct + 5 <= floored)
    {
        old_pct = static_cast<uint8_t>(old_pct + 5);
        skill_progress_event event;
        event.player = player;
        event.skill = skill;
        event.uses_this_level = ss.uses_this_level;
        event.uses_to_next_level = next;
        event.percent = old_pct;

        for (const auto& cb : progress_callbacks_)
        {
            cb(event);
        }
    }
    tracking.last_reported_percent[skill_idx] = floored;
}

auto skill_system::get_config_for(skill_type type) const -> const skill_config_entry&
{
    auto it = skill_configs_.find(type);
    if (it != skill_configs_.end())
    {
        return it->second;
    }
    return default_config_;
}

} // namespace hb::skill
