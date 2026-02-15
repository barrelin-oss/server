#pragma once

// manufacturing_system.h
// Manufacturing subsystem - weapons, armor, and ingot crafting

#include "core/subsystem.h"
#include "core/types.h"
#include "crafting/recipe_config.h"

#include <vector>
#include <string_view>

namespace hb
{
class build_recipe_registry;
}

namespace hb::skill
{
class skill_system;
}

namespace hb::inventory
{
class inventory_system;
}

namespace hb::item
{
class item_system;
}

namespace hb::player
{
class player_system;
}

namespace hb::crafting
{

class manufacturing_system : public subsystem
{
public:
    manufacturing_system();
    ~manufacturing_system() override;

    // Subsystem interface
    [[nodiscard]] auto name() const -> std::string_view override { return "manufacturing_system"; }
    void initialize() override;
    void shutdown() override;

    // Wire dependencies
    void set_dependencies(player::player_system* players,
                          skill::skill_system* skills,
                          inventory::inventory_system* inventory,
                          item::item_system* items,
                          build_recipe_registry* recipes);

    // Get recipes available to a player (filtered by skill_req)
    [[nodiscard]] auto get_available_recipes(entity_id player) -> std::vector<const build_recipe*>;

    // Attempt to craft a recipe by index
    auto attempt_craft(entity_id player, int32_t recipe_index) -> craft_result;

    // Calculate success chance for a recipe given player's skill level
    [[nodiscard]] static auto
    calculate_success_chance(int16_t skill_level, int16_t dex, const build_recipe& recipe) -> int32_t;

private:
    [[nodiscard]] auto check_ingredients(entity_id player, const build_recipe& recipe) const -> bool;
    void consume_ingredients(entity_id player, const build_recipe& recipe);

    player::player_system* players_{nullptr};
    skill::skill_system* skills_{nullptr};
    inventory::inventory_system* inventory_{nullptr};
    item::item_system* items_{nullptr};
    build_recipe_registry* recipes_{nullptr};
};

} // namespace hb::crafting
