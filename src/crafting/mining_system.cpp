// mining_system.cpp
// Mining system implementation

#include "crafting/mining_system.h"
#include "registry/mining_registry.h"
#include "player/player_system.h"
#include "player/player.h"
#include "skill/skill_system.h"
#include "inventory/inventory_system.h"
#include "item/item_system.h"
#include "world/world_subsystem.h"
#include "scheduler/scheduler.h"
#include "core/logger.h"

#include <random>
#include <cmath>
#include <algorithm>

namespace hb::crafting {

mining_system::mining_system() = default;
mining_system::~mining_system() = default;

void mining_system::initialize()
{
    LOG_INFO(general, "Mining system initialized");
    set_initialized(true);
}

void mining_system::shutdown()
{
    LOG_INFO(general, "Mining system shut down ({} active nodes)", nodes_.size());
    nodes_.clear();
    set_initialized(false);
}

void mining_system::set_dependencies(player::player_system* players,
                                      skill::skill_system* skills,
                                      inventory::inventory_system* inventory,
                                      item::item_system* items,
                                      mining_registry* registry,
                                      scheduler* sched,
                                      world::world_subsystem* world)
{
    players_ = players;
    skills_ = skills;
    inventory_ = inventory;
    items_ = items;
    registry_ = registry;
    scheduler_ = sched;
    world_ = world;
}

void mining_system::start_generation()
{
    if (!scheduler_ || !world_ || !registry_)
    {
        LOG_WARN(general, "Mining system: cannot start generation, missing dependencies");
        return;
    }

    scheduler_->schedule_repeating_tagged(
        duration_ms{10000}, "mineral_gen",
        [this]() { generate_minerals(); }
    );

    LOG_INFO(general, "Mining system: mineral generation started (10s interval)");
}

auto mining_system::attempt_mine(entity_id player_eid, int16_t target_x, int16_t target_y,
                                  const std::string& map_name) -> mine_result
{
    mine_result result;

    if (!players_ || !skills_ || !inventory_ || !items_ || !registry_)
    {
        result.reason = skill::skill_use_result::failure;
        return result;
    }

    // Find node at target position
    auto key = make_node_key(map_name, target_x, target_y);
    auto it = nodes_.find(key);
    if (it == nodes_.end())
    {
        result.reason = skill::skill_use_result::invalid_target;
        return result;
    }

    auto& node = it->second;

    // Check player is adjacent (distance <= 1)
    auto pid = player_id{player_eid.value};
    auto* plr = players_->get_player(pid);
    if (!plr)
    {
        result.reason = skill::skill_use_result::failure;
        return result;
    }

    auto dx = std::abs(static_cast<int>(plr->pos.x) - static_cast<int>(target_x));
    auto dy = std::abs(static_cast<int>(plr->pos.y) - static_cast<int>(target_y));
    if (dx > 1 || dy > 1)
    {
        result.reason = skill::skill_use_result::invalid_target;
        return result;
    }

    // Check player has pickaxe (template 231)
    if (!inventory_->has_item(player_eid, item_id{static_cast<uint32_t>(pickaxe_template_id)}))
    {
        result.reason = skill::skill_use_result::insufficient_materials;
        return result;
    }

    // Get mining skill
    auto mining_skill = skills_->get_skill_level(pid, skill::skill_type::mining);

    // Check skill cap (STR * 2)
    auto max_skill = static_cast<int16_t>(plr->base.strength * 2);
    if (mining_skill > max_skill)
    {
        mining_skill = max_skill;
    }

    // Look up mineral type config
    auto* type_config = registry_->get_type(node.type_id);
    if (!type_config)
    {
        result.reason = skill::skill_use_result::failure;
        return result;
    }

    // Calculate success
    auto chance = calculate_success_chance(mining_skill, type_config->difficulty);

    thread_local std::mt19937 rng{std::random_device{}()};
    std::uniform_int_distribution<int32_t> d100(1, 100);
    bool hit = d100(rng) <= chance;

    // Decrement hits (on both success and failure, matching legacy)
    --node.hits_remaining;

    if (hit)
    {
        result.success = true;
        result.reason = skill::skill_use_result::success;

        // Roll for drop
        auto* drop = roll_drop(*type_config, mining_skill);
        if (drop && drop->template_id > 0)
        {
            result.item_name = drop->item_name;
            result.template_id = drop->template_id;
            result.count = 1;

            // Create item and add to inventory
            auto item_result = items_->create_from_template(
                item_id{static_cast<uint32_t>(drop->template_id)}, 1);
            if (item_result.is_ok())
            {
                auto created_id = item_result.value();
                inventory_->add_item(player_eid, created_id, 1);
            }
        }

        // Grant XP
        int32_t exp = type_config->difficulty;
        auto levels = skills_->add_skill_exp(pid, skill::skill_type::mining, exp);
        result.exp_gained = exp;
        result.levels_gained = levels;
    }
    else
    {
        result.reason = skill::skill_use_result::failure;
    }

    // Check depletion
    if (node.hits_remaining <= 0)
    {
        result.node_depleted = true;
        if (despawn_callback_)
        {
            despawn_callback_(node);
        }
        nodes_.erase(it);
    }

    return result;
}

auto mining_system::get_node_at(const std::string& map_name,
                                 int16_t x, int16_t y) const -> const mineral_node*
{
    auto key = make_node_key(map_name, x, y);
    auto it = nodes_.find(key);
    return it != nodes_.end() ? &it->second : nullptr;
}

void mining_system::set_spawn_callback(node_spawn_callback cb)
{
    spawn_callback_ = std::move(cb);
}

void mining_system::set_despawn_callback(node_despawn_callback cb)
{
    despawn_callback_ = std::move(cb);
}

auto mining_system::calculate_success_chance(int16_t skill, int16_t difficulty) -> int32_t
{
    auto effective = static_cast<int32_t>(skill) - static_cast<int32_t>(difficulty);
    if (effective <= 0)
    {
        effective = 1;
    }
    return effective;
}

void mining_system::generate_minerals()
{
    if (!world_ || !registry_)
    {
        return;
    }

    world_->for_each_map([this](map_id /*id*/, world::map& m) {
        auto& gen = m.get_mineral_generator();
        if (!gen.enabled || gen.level <= 0)
        {
            return;
        }

        // 25% chance per tick
        thread_local std::mt19937 rng{std::random_device{}()};
        std::uniform_int_distribution<int> d4(1, 4);
        if (d4(rng) != 1)
        {
            return;
        }

        auto& points = m.get_mineral_points();
        if (points.empty())
        {
            return;
        }

        // Count current nodes on this map
        auto map_name = std::string(m.name());
        int32_t current_count = 0;
        for (const auto& [key, node] : nodes_)
        {
            if (node.map_name == map_name)
            {
                ++current_count;
            }
        }

        if (current_count >= m.max_mineral())
        {
            return;
        }

        // Pick random mineral point
        std::uniform_int_distribution<size_t> point_dist(0, points.size() - 1);
        const auto& point = points[point_dist(rng)];

        // Skip if position already occupied
        auto key = make_node_key(map_name, point.x, point.y);
        if (nodes_.contains(key))
        {
            return;
        }

        // Random type: d(generator_level) -> type_id
        std::uniform_int_distribution<int> type_dist(1, gen.level);
        auto type_id = type_dist(rng);

        spawn_mineral(map_name, point.x, point.y, type_id);
    });
}

void mining_system::spawn_mineral(const std::string& map_name, int16_t x, int16_t y, int32_t type_id)
{
    auto* type_config = registry_->get_type(type_id);
    if (!type_config)
    {
        return;
    }

    auto key = make_node_key(map_name, x, y);

    mineral_node node{
        .node_id = next_node_id_++,
        .type_id = type_id,
        .map_name = map_name,
        .x = x,
        .y = y,
        .hits_remaining = type_config->max_hits
    };

    if (spawn_callback_)
    {
        spawn_callback_(node);
    }

    nodes_[key] = std::move(node);
}

void mining_system::despawn_mineral(const std::string& node_key)
{
    auto it = nodes_.find(node_key);
    if (it == nodes_.end())
    {
        return;
    }

    if (despawn_callback_)
    {
        despawn_callback_(it->second);
    }

    nodes_.erase(it);
}

auto mining_system::roll_drop(const mineral_type_config& config, int16_t skill) -> const mineral_drop*
{
    // Filter drops by min_skill
    std::vector<const mineral_drop*> eligible;
    int32_t total_weight = 0;

    for (const auto& drop : config.drops)
    {
        if (skill >= drop.min_skill)
        {
            eligible.push_back(&drop);
            total_weight += drop.weight;
        }
    }

    if (eligible.empty() || total_weight <= 0)
    {
        return nullptr;
    }

    // Weighted random selection
    thread_local std::mt19937 rng{std::random_device{}()};
    std::uniform_int_distribution<int32_t> dist(1, total_weight);
    auto roll = dist(rng);

    int32_t cumulative = 0;
    for (const auto* drop : eligible)
    {
        cumulative += drop->weight;
        if (roll <= cumulative)
        {
            return drop;
        }
    }

    return eligible.back();
}

auto mining_system::make_node_key(const std::string& map, int16_t x, int16_t y) -> std::string
{
    return map + ":" + std::to_string(x) + ":" + std::to_string(y);
}

}  // namespace hb::crafting
