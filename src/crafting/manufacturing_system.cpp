// manufacturing_system.cpp
// Manufacturing system implementation

#include "crafting/manufacturing_system.h"
#include "registry/build_recipe_registry.h"
#include "player/player_system.h"
#include "skill/skill_system.h"
#include "inventory/inventory_system.h"
#include "item/item_system.h"
#include "core/logger.h"

#include <algorithm>
#include <random>

namespace hb::crafting {

manufacturing_system::manufacturing_system() = default;
manufacturing_system::~manufacturing_system() = default;

void manufacturing_system::initialize()
{
    LOG_INFO(general, "Manufacturing system initialized");
    set_initialized(true);
}

void manufacturing_system::shutdown()
{
    LOG_INFO(general, "Manufacturing system shut down");
    set_initialized(false);
}

void manufacturing_system::set_dependencies(player::player_system* players,
                                             skill::skill_system* skills,
                                             inventory::inventory_system* inventory,
                                             item::item_system* items,
                                             build_recipe_registry* recipes)
{
    players_ = players;
    skills_ = skills;
    inventory_ = inventory;
    items_ = items;
    recipes_ = recipes;
}

auto manufacturing_system::get_available_recipes(entity_id player)
    -> std::vector<const build_recipe*>
{
    std::vector<const build_recipe*> available;
    if (!recipes_ || !skills_) return available;

    auto skill_level = skills_->get_skill_level(
        player_id{player.value}, skill::skill_type::manufacturing);

    for (const auto& recipe : recipes_->get_all())
    {
        if (skill_level >= recipe.skill_req)
        {
            available.push_back(&recipe);
        }
    }

    return available;
}

auto manufacturing_system::calculate_success_chance(int16_t skill_level,
                                                     int16_t dex,
                                                     const build_recipe& recipe) -> int32_t
{
    int32_t chance = recipe.success_rate;

    // Skill bonus: +(skill - req) * 2, capped at +40
    int32_t skill_bonus = std::min(40, (skill_level - recipe.skill_req) * 2);
    chance += skill_bonus;

    // DEX bonus: +dex / 2
    chance += dex / 2;

    // Clamp to 10-95%
    return std::clamp(chance, 10, 95);
}

auto manufacturing_system::attempt_craft(entity_id player, int32_t recipe_index)
    -> craft_result
{
    craft_result result;

    if (!recipes_ || !skills_ || !inventory_ || !items_ || !players_)
    {
        result.reason = skill::skill_use_result::failure;
        return result;
    }

    // Look up recipe
    auto* recipe = recipes_->get(recipe_index);
    if (!recipe)
    {
        result.reason = skill::skill_use_result::failure;
        return result;
    }

    auto pid = player_id{player.value};

    // Check manufacturing skill >= skill_req
    auto skill_level = skills_->get_skill_level(pid, skill::skill_type::manufacturing);
    if (skill_level < recipe->skill_req)
    {
        result.reason = skill::skill_use_result::insufficient_skill;
        return result;
    }

    // Check skill cap (STR * 2)
    auto* plr = players_->get_player(pid);
    if (!plr)
    {
        result.reason = skill::skill_use_result::failure;
        return result;
    }

    int16_t skill_cap = plr->base.strength * 2;
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
    int32_t chance = calculate_success_chance(skill_level, plr->base.dexterity, *recipe);
    thread_local std::mt19937 rng{std::random_device{}()};
    std::uniform_int_distribution<int32_t> d100(1, 100);
    int32_t roll = d100(rng);

    if (roll <= chance)
    {
        // Success - create item
        if (recipe->result_template_id > 0)
        {
            // Check inventory space first
            if (inventory_->is_full(player))
            {
                // Ingredients already consumed but no room for result
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

    // Grant XP if skill < skill_limit (or skill_limit == 0)
    if (recipe->skill_limit == 0 || skill_level < recipe->skill_limit)
    {
        int32_t max_exp = std::max(1, static_cast<int32_t>(recipe->skill_limit) / 4);
        std::uniform_int_distribution<int32_t> exp_dist(1, max_exp);
        int32_t exp = exp_dist(rng);

        result.levels_gained = skills_->add_skill_exp(
            pid, skill::skill_type::manufacturing, exp);
        result.exp_gained = exp;
    }

    return result;
}

auto manufacturing_system::check_ingredients(entity_id player,
                                              const build_recipe& recipe) const -> bool
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

void manufacturing_system::consume_ingredients(entity_id player,
                                                const build_recipe& recipe)
{
    // Aggregate then consume
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
