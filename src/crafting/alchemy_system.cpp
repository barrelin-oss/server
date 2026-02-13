// alchemy_system.cpp
// Alchemy system implementation

#include "crafting/alchemy_system.h"
#include "registry/craft_recipe_registry.h"
#include "player/player_system.h"
#include "skill/skill_system.h"
#include "inventory/inventory_system.h"
#include "item/item_system.h"
#include "core/logger.h"
#include "perf/perf_stats.h"

#include <algorithm>
#include <random>

namespace hb::crafting {

alchemy_system::alchemy_system() = default;
alchemy_system::~alchemy_system() = default;

void alchemy_system::initialize()
{
    LOG_INFO(general, "Alchemy system initialized");
    set_initialized(true);
}

void alchemy_system::shutdown()
{
    LOG_INFO(general, "Alchemy system shut down");
    set_initialized(false);
}

void alchemy_system::set_dependencies(player::player_system* players,
                                       skill::skill_system* skills,
                                       inventory::inventory_system* inventory,
                                       item::item_system* items,
                                       craft_recipe_registry* recipes)
{
    players_ = players;
    skills_ = skills;
    inventory_ = inventory;
    items_ = items;
    recipes_ = recipes;
}

auto alchemy_system::get_available_recipes(entity_id player)
    -> std::vector<const craft_recipe*>
{
    std::vector<const craft_recipe*> available;
    if (!recipes_ || !skills_) return available;

    auto skill_level = skills_->get_skill_level(
        player_id{player.value}, skill::skill_type::alchemy);

    // Include both alchemy and crafting recipes
    for (const auto& recipe : recipes_->get_all_alchemy())
    {
        if (skill_level >= recipe.skill_limit)
        {
            available.push_back(&recipe);
        }
    }
    for (const auto& recipe : recipes_->get_all_crafting())
    {
        if (skill_level >= recipe.skill_limit)
        {
            available.push_back(&recipe);
        }
    }

    return available;
}

auto alchemy_system::calculate_success_chance(int16_t skill_level,
                                               int16_t int_stat,
                                               const craft_recipe& recipe) -> int32_t
{
    // Base: 100 - difficulty
    int32_t chance = 100 - recipe.difficulty;

    // Skill bonus: +skill * 0.5
    chance += skill_level / 2;

    // INT bonus: +int / 3
    chance += int_stat / 3;

    // Clamp to 5-98%
    return std::clamp(chance, 5, 98);
}

auto alchemy_system::attempt_craft(entity_id player, int32_t recipe_id)
    -> craft_result
{
    auto* perf = subsystems().get<perf::perf_stats_system>();
    PERF_TIMER(perf, perf::metric_category::crafting);

    craft_result result;

    if (!recipes_ || !skills_ || !inventory_ || !items_ || !players_)
    {
        result.reason = skill::skill_use_result::failure;
        return result;
    }

    // Look up recipe by ID
    auto* recipe = recipes_->get(recipe_id);
    if (!recipe)
    {
        result.reason = skill::skill_use_result::failure;
        return result;
    }

    auto pid = player_id{player.value};

    // Check alchemy skill >= skill_limit
    auto skill_level = skills_->get_skill_level(pid, skill::skill_type::alchemy);
    if (skill_level < recipe->skill_limit)
    {
        result.reason = skill::skill_use_result::insufficient_skill;
        return result;
    }

    // Check skill cap (INT * 2)
    auto* plr = players_->get_player(pid);
    if (!plr)
    {
        result.reason = skill::skill_use_result::failure;
        return result;
    }

    int16_t skill_cap = plr->base.intelligence * 2;
    if (skill_level >= skill_cap)
    {
        result.reason = skill::skill_use_result::insufficient_skill;
        return result;
    }

    // Check ingredients
    if (!check_ingredients(player, *recipe))
    {
        result.reason = skill::skill_use_result::insufficient_materials;
        return result;
    }

    // Consume ALL ingredients (on both success and failure)
    consume_ingredients(player, *recipe);

    // Roll for success
    int32_t chance = calculate_success_chance(
        skill_level, plr->base.intelligence, *recipe);
    thread_local std::mt19937 rng{std::random_device{}()};
    std::uniform_int_distribution<int32_t> d100(1, 100);
    int32_t roll = d100(rng);

    if (roll <= chance)
    {
        // Success - create item
        if (recipe->result_template_id > 0)
        {
            if (inventory_->is_full(player))
            {
                result.reason = skill::skill_use_result::failure;
                return result;
            }

            auto item_result = items_->create_from_template(
                item_id{static_cast<uint32_t>(recipe->result_template_id)});
            if (item_result.is_ok())
            {
                auto created_id = item_result.value();
                inventory_->add_item(player, created_id);
                result.created_item = created_id;
                result.success = true;
            }
        }
    }

    // Record skill use
    skills_->record_skill_use(pid, skill::skill_type::alchemy);

    return result;
}

auto alchemy_system::check_ingredients(entity_id player,
                                        const craft_recipe& recipe) const -> bool
{
    // Aggregate ingredients by item_id (recipes can list same item_id multiple times)
    std::unordered_map<int32_t, int32_t> needed;
    for (const auto& ing : recipe.ingredients)
    {
        needed[ing.item_id] += ing.count;
    }

    for (const auto& [id, count] : needed)
    {
        if (!inventory_->has_item(player, item_id{static_cast<uint32_t>(id)},
                                   static_cast<int16_t>(count)))
        {
            return false;
        }
    }

    return true;
}

void alchemy_system::consume_ingredients(entity_id player,
                                          const craft_recipe& recipe)
{
    std::unordered_map<int32_t, int32_t> needed;
    for (const auto& ing : recipe.ingredients)
    {
        needed[ing.item_id] += ing.count;
    }

    for (const auto& [id, count] : needed)
    {
        inventory_->remove_item(player, item_id{static_cast<uint32_t>(id)},
                                 static_cast<int16_t>(count));
    }
}

}  // namespace hb::crafting
