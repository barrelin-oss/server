# Manufacturing System Implementation Plan

## Overview
Manufacturing allows players to create weapons and armor at anvil/forge NPCs using raw materials. ~100 recipes already exist in `build_recipes.yaml`.

## Architecture

### 1. Recipe Config (`src/crafting/recipe_config.h`)
Data structures for both manufacturing and alchemy recipes (shared file):

```cpp
struct recipe_ingredient
{
    int32_t item_id{};
    int32_t count{1};
};

struct build_recipe
{
    int32_t id{};           // auto-assigned index
    std::string result;     // result item name (resolved to template_id at load)
    int32_t result_template_id{};
    int16_t skill_req{};    // minimum skill to attempt
    int16_t skill_limit{};  // skill cap for exp gain
    int32_t success_rate{}; // base % (0-100)
    std::vector<recipe_ingredient> ingredients;
};
```

### 2. Build Recipe Registry (`src/registry/build_recipe_registry.h/.cpp`)
Follow `loot_registry` pattern:
- Inherits `subsystem`
- `load_from_file(path)` parses `build_recipes.yaml`
- Resolves `result` name to `result_template_id` via `item_registry`
- Storage: `std::vector<build_recipe>` + index by result name
- Getters: `get(index)`, `find_by_name(string_view)`, `get_all()`, `count()`

### 3. Manufacturing System (`src/crafting/manufacturing_system.h/.cpp`)
New subsystem with pure logic:

```cpp
class manufacturing_system : public subsystem
{
public:
    void set_dependencies(skill_system*, inventory_system*, item_system*, build_recipe_registry*);

    // Core API
    auto get_available_recipes(entity_id player) -> std::vector<const build_recipe*>;
    auto attempt_craft(entity_id player, int32_t recipe_index) -> craft_result;

private:
    auto calculate_success_chance(int16_t skill_level, const build_recipe& recipe) -> int32_t;
    auto check_ingredients(entity_id player, const build_recipe& recipe) -> bool;
    auto consume_ingredients(entity_id player, const build_recipe& recipe) -> bool;
};
```

`craft_result` struct:
```cpp
struct craft_result
{
    bool success{};
    skill_use_result reason{};  // if failed: insufficient_skill, insufficient_materials
    item_id created_item{};
    int32_t exp_gained{};
    int16_t levels_gained{};
};
```

Success formula (from legacy):
- Base: `success_rate` from recipe
- Modifier: `+(skill_level - skill_req) * 2` (capped at +40)
- DEX bonus: `+dex / 2`
- Final clamped to 10-95%

### 4. Protocol Messages (4 messages)

| Message | Direction | Fields |
|---------|-----------|--------|
| `manufacture_list_request` | C→S | (none — triggered by opening crafting UI) |
| `manufacture_list_response` | S→C | `recipes[]` (id, name, skill_req, success_rate, ingredients[]) |
| `manufacture_request` | C→S | `recipe_index` (int) |
| `manufacture_response` | S→C | `success` (bool), `item_name`, `item_id`, `exp_gained`, `reason` |

### 5. Handler Integration
Add to `game_handlers.cpp`:
- `handle_manufacture_list_request` — calls `get_available_recipes()`, sends list
- `handle_manufacture_request` — calls `attempt_craft()`, sends result, fires `item_crafted_event`

### 6. Dialog Action
Add `open_manufacturing` to `dialog_action` enum in `dialog_config.h`.
In the dialog action handler, send `manufacture_list_response` automatically.

### 7. Wiring (`application.cpp`)
- Create `build_recipe_registry` subsystem, load `build_recipes.yaml`
- Create `manufacturing_system`, wire dependencies
- Add `manufacturing_system*` to `game_handlers` constructor
- Register handler callbacks

### 8. Tests (`tests/manufacturing_test.cpp`)
- Build recipe registry: YAML loading, name resolution, ingredient parsing
- Success chance calculation: skill level effects, DEX bonus, clamping
- Ingredient checking: sufficient/insufficient materials
- Craft attempt: success path, failure path (skill too low, missing materials, inventory full)
- Exp gain: within skill_limit vs above

## Files to Create
1. `src/crafting/recipe_config.h` (shared with alchemy)
2. `src/registry/build_recipe_registry.h`
3. `src/registry/build_recipe_registry.cpp`
4. `src/crafting/manufacturing_system.h`
5. `src/crafting/manufacturing_system.cpp`
6. `tests/manufacturing_test.cpp`

## Files to Modify
1. `src/network/json_protocol.h` — 4 new message types
2. `src/network/json_protocol.cpp` — type_map entries, to_string cases
3. `src/bridge/handlers/game_handlers.h` — manufacturing_system* member, 2 handler methods
4. `src/bridge/handlers/game_handlers.cpp` — handler implementations + registrations
5. `src/npc/dialog_config.h` — `open_manufacturing` action
6. `src/application.cpp` — subsystem creation and wiring

## Dependencies
- `item_registry` must be loaded first (for name→template_id resolution)
- `skill_system`, `inventory_system`, `item_system` must exist (they do)
