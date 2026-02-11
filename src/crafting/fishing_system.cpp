// fishing_system.cpp
// Fishing system implementation

#include "crafting/fishing_system.h"
#include "registry/fishing_registry.h"
#include "player/player_system.h"
#include "player/player.h"
#include "skill/skill_system.h"
#include "inventory/inventory_system.h"
#include "item/item_system.h"
#include "world/world_subsystem.h"
#include "scheduler/scheduler.h"
#include "core/logger.h"

#include <random>
#include <algorithm>

namespace hb::crafting {

fishing_system::fishing_system() = default;
fishing_system::~fishing_system() = default;

void fishing_system::initialize()
{
    LOG_INFO(general, "Fishing system initialized");
    set_initialized(true);
}

void fishing_system::shutdown()
{
    LOG_INFO(general, "Fishing system shut down ({} active nodes)", active_node_count());
    for (auto& slot : fish_nodes_)
    {
        slot.reset();
    }
    set_initialized(false);
}

void fishing_system::set_dependencies(player::player_system* players,
                                       skill::skill_system* skills,
                                       inventory::inventory_system* inventory,
                                       item::item_system* items,
                                       fishing_registry* registry,
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

void fishing_system::start_generation()
{
    if (!scheduler_ || !world_ || !registry_)
    {
        LOG_WARN(general, "Fishing system: cannot start generation, missing dependencies");
        return;
    }

    // Fish generation + cleanup every 4 seconds
    scheduler_->register_task("fish_gen",
        "Spawn fish nodes and update fishing chances",
        duration_ms{4000}, true,
        [this]() -> task_callback {
            return [this]() {
                generate_fish();
                cleanup_expired_fish();
                update_fishing_chances();
            };
        });
    scheduler_->start_task("fish_gen");

    LOG_INFO(general, "Fishing system: fish generation started (4s interval)");
}

auto fishing_system::check_fish_nearby(entity_id player_eid, const std::string& map_name,
                                        int16_t player_x, int16_t player_y)
    -> std::optional<uint32_t>
{
    if (!players_)
    {
        return std::nullopt;
    }

    auto pid = player_id{player_eid.value};
    auto* plr = players_->get_player(pid);
    if (!plr)
    {
        return std::nullopt;
    }

    // Already fishing?
    if (plr->fishing.fish_node_index != 0)
    {
        return std::nullopt;
    }

    // Search all active fish nodes for one within 2 tiles on same map
    for (uint32_t i = 1; i < max_fish_nodes; ++i)
    {
        if (!fish_nodes_[i]) continue;
        auto& fish = *fish_nodes_[i];

        if (fish.map_name != map_name) continue;

        auto dx = std::abs(static_cast<int>(fish.x) - static_cast<int>(player_x));
        auto dy = std::abs(static_cast<int>(fish.y) - static_cast<int>(player_y));
        if (dx > 2 || dy > 2) continue;

        // Check engagement limit
        if (fish.engaging_count >= max_engaging_fish) continue;

        // Engage this fish
        plr->fishing.fish_node_index = fish.index;
        plr->fishing.catch_chance = 1;  // Start at 1%
        plr->fishing.last_update = std::chrono::steady_clock::now();
        ++fish.engaging_count;

        if (engaged_callback_ && fish.config)
        {
            engaged_callback_(player_eid, *fish.config, 1);
        }

        return fish.index;
    }

    return std::nullopt;
}

auto fishing_system::attempt_catch(entity_id player_eid) -> fish_catch_result
{
    fish_catch_result result;

    if (!players_ || !skills_ || !inventory_ || !items_)
    {
        result.result = catch_result::failure;
        return result;
    }

    auto pid = player_id{player_eid.value};
    auto* plr = players_->get_player(pid);
    if (!plr)
    {
        result.result = catch_result::failure;
        return result;
    }

    // Not fishing?
    if (plr->fishing.fish_node_index == 0)
    {
        result.result = catch_result::no_fish;
        return result;
    }

    auto fish_idx = plr->fishing.fish_node_index;
    if (fish_idx >= max_fish_nodes || !fish_nodes_[fish_idx])
    {
        // Fish node gone (stolen/despawned)
        plr->fishing.fish_node_index = 0;
        result.result = catch_result::canceled_stolen;
        return result;
    }

    auto& fish = *fish_nodes_[fish_idx];

    // Check fishing rod (template 105)
    if (!inventory_->has_item(player_eid, item_id{static_cast<uint32_t>(fishing_rod_template_id)}))
    {
        // Disengage
        plr->fishing.fish_node_index = 0;
        --fish.engaging_count;
        result.result = catch_result::no_rod;
        return result;
    }

    // Roll 1d100 vs current catch_chance
    thread_local std::mt19937 rng{std::random_device{}()};
    std::uniform_int_distribution<int32_t> d100(1, 100);
    auto roll = d100(rng);

    if (roll <= plr->fishing.catch_chance)
    {
        // Success!
        result.result = catch_result::success;

        if (fish.config)
        {
            result.item_name = fish.config->item_name;
            result.template_id = fish.config->template_id;

            // Award XP: difficulty * d5
            std::uniform_int_distribution<int32_t> d5(1, 5);
            int32_t exp = fish.config->difficulty * d5(rng);
            if (exp < 1) exp = 1;
            auto levels = skills_->add_skill_exp(pid, skill::skill_type::fishing, exp);
            result.exp_gained = exp;
            result.levels_gained = levels;

            // Create the item and add to inventory (or drop at feet)
            if (fish.config->template_id > 0)
            {
                auto item_result = items_->create_from_template(
                    item_id{static_cast<uint32_t>(fish.config->template_id)}, 1);
                if (item_result.is_ok())
                {
                    auto created_id = item_result.value();
                    inventory_->add_item(player_eid, created_id, 1);
                }
            }

            // Damage fishing rod by 1 durability
            // TODO: implement rod durability reduction
        }

        // Fire callback before deleting (node still valid)
        if (catch_complete_callback_)
        {
            catch_complete_callback_(player_eid, result);
        }

        // Delete fish node — caught! This also disengages all other fishers
        plr->fishing.fish_node_index = 0;
        delete_fish_node(fish_idx, catch_result::canceled_stolen);

        return result;
    }
    else
    {
        // Failure — disengage, fish remains for others
        result.result = catch_result::failure;
        plr->fishing.fish_node_index = 0;
        --fish.engaging_count;

        if (catch_complete_callback_)
        {
            catch_complete_callback_(player_eid, result);
        }

        return result;
    }
}

void fishing_system::cancel_fishing(entity_id player_eid, catch_result reason)
{
    if (!players_)
    {
        return;
    }

    auto pid = player_id{player_eid.value};
    auto* plr = players_->get_player(pid);
    if (!plr || plr->fishing.fish_node_index == 0)
    {
        return;
    }

    auto fish_idx = plr->fishing.fish_node_index;
    plr->fishing.fish_node_index = 0;

    // Decrement engagement count on the fish node
    if (fish_idx < max_fish_nodes && fish_nodes_[fish_idx])
    {
        --fish_nodes_[fish_idx]->engaging_count;
    }

    // Notify client
    if (catch_complete_callback_)
    {
        fish_catch_result result;
        result.result = reason;
        catch_complete_callback_(player_eid, result);
    }
}

auto fishing_system::get_node(uint32_t index) const -> const fish_node*
{
    if (index == 0 || index >= max_fish_nodes)
    {
        return nullptr;
    }
    return fish_nodes_[index] ? &(*fish_nodes_[index]) : nullptr;
}

auto fishing_system::active_node_count() const -> size_t
{
    size_t count = 0;
    for (const auto& slot : fish_nodes_)
    {
        if (slot) ++count;
    }
    return count;
}

void fishing_system::set_spawn_callback(fish_spawn_callback cb)
{
    spawn_callback_ = std::move(cb);
}

void fishing_system::set_despawn_callback(fish_despawn_callback cb)
{
    despawn_callback_ = std::move(cb);
}

void fishing_system::set_engaged_callback(fish_engaged_callback cb)
{
    engaged_callback_ = std::move(cb);
}

void fishing_system::set_chance_update_callback(chance_update_callback cb)
{
    chance_update_callback_ = std::move(cb);
}

void fishing_system::set_catch_complete_callback(catch_complete_callback cb)
{
    catch_complete_callback_ = std::move(cb);
}

auto fishing_system::calculate_chance_change(int16_t skill, int16_t difficulty)
    -> std::pair<int32_t, int32_t>
{
    auto effective = static_cast<int32_t>(skill) - static_cast<int32_t>(difficulty);
    if (effective <= 0)
    {
        effective = 1;
    }
    auto max_change = std::max(1, effective / 10);
    return {effective, max_change};
}

void fishing_system::generate_fish()
{
    if (!world_ || !registry_ || registry_->count() == 0)
    {
        return;
    }

    world_->for_each_map([this](map_id /*id*/, world::map& m) {
        thread_local std::mt19937 rng{std::random_device{}()};
        auto& fish_pts = m.get_fish_points();
        if (fish_pts.empty())
        {
            return;
        }

        // 10% chance per tick (1 in 10)
        std::uniform_int_distribution<int> d10(1, 10);
        if (d10(rng) != 1)
        {
            return;
        }

        // Count current fish on this map
        auto map_name = std::string(m.name());
        int32_t current_count = 0;
        for (const auto& slot : fish_nodes_)
        {
            if (slot && slot->map_name == map_name)
            {
                ++current_count;
            }
        }

        // Limit to max_fish for this map (use fish point count / 2 as soft cap)
        auto max_fish = static_cast<int32_t>(fish_pts.size() / 2);
        if (max_fish < 1) max_fish = 1;
        if (current_count >= max_fish)
        {
            return;
        }

        // Find a free slot
        auto slot_idx = find_free_slot();
        if (slot_idx == 0)
        {
            return;  // All slots full
        }

        // Pick random fish point
        std::uniform_int_distribution<size_t> point_dist(0, fish_pts.size() - 1);
        const auto& point = fish_pts[point_dist(rng)];

        // Small offset (-1 to +1)
        std::uniform_int_distribution<int16_t> offset(-1, 1);
        auto fx = static_cast<int16_t>(point.x + offset(rng));
        auto fy = static_cast<int16_t>(point.y + offset(rng));

        // Validate position is water tile
        auto pos = world::position{fx, fy};
        if (!m.is_water(pos))
        {
            // Try without offset
            fx = point.x;
            fy = point.y;
            pos = world::position{fx, fy};
            if (!m.is_water(pos))
            {
                return;
            }
        }

        // Roll a random fish type (weighted)
        auto* fish_config = registry_->roll_fish_type();
        if (!fish_config)
        {
            return;
        }

        // Random lifespan: base_min +/- 5 minutes, range [10, 30]
        std::uniform_int_distribution<int32_t> lifespan_var(-5, 5);
        auto lifespan_minutes = std::clamp(
            fish_config->lifespan_min + lifespan_var(rng), 10, 30);

        fish_node node{
            .index = slot_idx,
            .type_id = fish_config->type_id,
            .map_name = map_name,
            .x = fx,
            .y = fy,
            .config = fish_config,
            .engaging_count = 0,
            .spawn_time = std::chrono::steady_clock::now(),
            .lifespan = duration_ms{lifespan_minutes * 60 * 1000}
        };

        if (spawn_callback_)
        {
            spawn_callback_(node);
        }

        fish_nodes_[slot_idx] = std::move(node);
    });
}

void fishing_system::update_fishing_chances()
{
    if (!players_ || !skills_)
    {
        return;
    }

    players_->for_each_player([this](player_id /*pid*/, player::player& plr) {
        thread_local std::mt19937 rng{std::random_device{}()};

        if (plr.fishing.fish_node_index == 0) return;

        auto fish_idx = plr.fishing.fish_node_index;
        if (fish_idx >= max_fish_nodes || !fish_nodes_[fish_idx])
        {
            // Fish gone — clean up silently
            plr.fishing.fish_node_index = 0;
            return;
        }

        auto& fish = *fish_nodes_[fish_idx];
        if (!fish.config) return;

        // Get fishing skill, capped at DEX * 2
        auto fishing_skill = skills_->get_skill_level(plr.id, skill::skill_type::fishing);
        auto max_skill = static_cast<int16_t>(plr.base.dexterity * 2);
        if (fishing_skill > max_skill)
        {
            fishing_skill = max_skill;
        }

        auto [effective, max_change] = calculate_chance_change(fishing_skill, fish.config->difficulty);

        // Roll change amount: 1d(max_change)
        std::uniform_int_distribution<int32_t> change_dist(1, max_change);
        auto change = change_dist(rng);

        // Roll 1d100 vs effective skill
        std::uniform_int_distribution<int32_t> d100(1, 100);
        auto roll = d100(rng);

        if (effective > roll)
        {
            // Increase chance
            plr.fishing.catch_chance += change;
        }
        else if (effective < roll)
        {
            // Decrease chance
            plr.fishing.catch_chance -= change;
        }
        // If equal, no change

        plr.fishing.catch_chance = std::clamp(plr.fishing.catch_chance, 1, 99);

        if (chance_update_callback_)
        {
            chance_update_callback_(entity_id(plr.id.value), plr.fishing.catch_chance);
        }
    });
}

void fishing_system::cleanup_expired_fish()
{
    auto now = std::chrono::steady_clock::now();
    for (uint32_t i = 1; i < max_fish_nodes; ++i)
    {
        if (!fish_nodes_[i]) continue;
        auto& fish = *fish_nodes_[i];

        auto elapsed = std::chrono::duration_cast<duration_ms>(now - fish.spawn_time);
        if (elapsed >= fish.lifespan)
        {
            delete_fish_node(i, catch_result::canceled_timeout);
        }
    }
}

auto fishing_system::find_free_slot() -> uint32_t
{
    for (uint32_t i = 1; i < max_fish_nodes; ++i)
    {
        if (!fish_nodes_[i])
        {
            return i;
        }
    }
    return 0;
}

void fishing_system::delete_fish_node(uint32_t index, catch_result reason)
{
    if (index == 0 || index >= max_fish_nodes || !fish_nodes_[index])
    {
        return;
    }

    auto& fish = *fish_nodes_[index];

    // Notify all engaged players that this fish is gone
    if (players_ && fish.engaging_count > 0)
    {
        players_->for_each_player([this, index, reason](player_id /*pid*/, player::player& plr) {
            if (plr.fishing.fish_node_index == index)
            {
                plr.fishing.fish_node_index = 0;
                if (catch_complete_callback_)
                {
                    fish_catch_result result;
                    result.result = reason;
                    catch_complete_callback_(entity_id(plr.id.value), result);
                }
            }
        });
    }

    // Fire despawn callback
    if (despawn_callback_)
    {
        despawn_callback_(fish);
    }

    fish_nodes_[index].reset();
}

}  // namespace hb::crafting
