#pragma once

// recipe_config.h
// Shared data structures for manufacturing and alchemy crafting systems

#include "core/types.h"
#include "skill/skill.h"

#include <string>
#include <vector>
#include <cstdint>

namespace hb::crafting
{

// A single ingredient requirement for a recipe
struct recipe_ingredient
{
    int32_t item_id{};
    int32_t count{1};
};

// Manufacturing recipe (weapons, armor, ingots)
struct build_recipe
{
    int32_t id{};                 // auto-assigned index during load
    std::string result;           // item name from YAML
    int32_t result_template_id{}; // resolved via item_registry::find_by_name()
    int16_t skill_req{};          // min manufacturing skill to attempt
    int16_t skill_limit{};        // max skill for XP gain (0 = always)
    int32_t success_rate{};       // base % (0-100)
    uint16_t result_attribute{0}; // legacy attribute bits (shifted left 16 → main+sub enchantments)
    std::vector<recipe_ingredient> ingredients;
};

// Alchemy/crafting recipe (potions, gems, spell manuals)
struct craft_recipe
{
    int32_t id{};                 // from YAML
    std::string result;           // item name from YAML
    int32_t result_template_id{}; // resolved via item_registry::find_by_name()
    int16_t skill_limit{};        // min alchemy skill to attempt
    int16_t difficulty{};         // subtracted from base success chance
    std::vector<recipe_ingredient> ingredients;
};

// Result of a crafting attempt
struct craft_result
{
    bool success{};
    skill::skill_use_result reason{}; // failure reason if !success
    item_id created_item{};           // created item ID (only on success)
};

} // namespace hb::crafting
