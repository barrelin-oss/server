# Alchemy System Implementation Plan

## Overview
Alchemy allows players to create potions and consumables using herbs/reagents at alchemy NPCs. ~80 recipes exist in `craft_recipes.yaml`.

## Architecture

### 1. Shared Recipe Config
Uses `recipe_config.h` (created by manufacturing teammate) for shared `recipe_ingredient` struct. Adds:

```cpp
struct craft_recipe
{
    int32_t id{};           // from YAML
    std::string result;     // result item name
    int32_t result_template_id{};
    int16_t skill_limit{};  // skill cap for exp
    int16_t difficulty{};   // 0-100, affects success
    std::vector<recipe_ingredient> ingredients;
};
```

Key difference from manufacturing: uses `difficulty` instead of `success_rate` + `skill_req`. No explicit skill_req — any level can attempt, but difficulty determines success.

### 2. Craft Recipe Registry (`src/registry/craft_recipe_registry.h/.cpp`)
Follow `loot_registry` pattern:
- Inherits `subsystem`
- `load_from_file(path)` parses `craft_recipes.yaml`
- Resolves `result` name to `result_template_id` via `item_registry`
- Storage: `std::unordered_map<int32_t, craft_recipe>` keyed by recipe id
- Getters: `get(id)`, `find_by_name(string_view)`, `get_all()`, `count()`

### 3. Alchemy System (`src/crafting/alchemy_system.h/.cpp`)
New subsystem:

```cpp
class alchemy_system : public subsystem
{
public:
    void set_dependencies(skill_system*, inventory_system*, item_system*, craft_recipe_registry*);

    auto get_available_recipes(entity_id player) -> std::vector<const craft_recipe*>;
    auto attempt_craft(entity_id player, int32_t recipe_id) -> craft_result;

private:
    auto calculate_success_chance(int16_t skill_level, int16_t int_stat, const craft_recipe& recipe) -> int32_t;
    auto check_ingredients(entity_id player, const craft_recipe& recipe) -> bool;
    auto consume_ingredients(entity_id player, const craft_recipe& recipe) -> bool;
};
```

Reuses `craft_result` from `recipe_config.h`.

Success formula (from legacy):
- Base: `100 - difficulty`
- Skill bonus: `+skill_level * 0.5` (alchemy skill)
- INT bonus: `+int_stat / 3`
- Final clamped to 5-98%
- Alchemy is generally easier than manufacturing (potions are consumable, lower stakes)

### 4. Protocol Messages (4 messages)

| Message | Direction | Fields |
|---------|-----------|--------|
| `alchemy_list_request` | C→S | (none) |
| `alchemy_list_response` | S→C | `recipes[]` (id, name, skill_limit, difficulty, ingredients[]) |
| `alchemy_request` | C→S | `recipe_id` (int) |
| `alchemy_response` | S→C | `success` (bool), `item_name`, `item_id`, `exp_gained`, `reason` |

### 5. Handler Integration
Add to `game_handlers.cpp`:
- `handle_alchemy_list_request` — sends recipe list
- `handle_alchemy_request` — calls `attempt_craft()`, sends result, fires `item_crafted_event`

### 6. Dialog Action
Add `open_alchemy` to `dialog_action` enum in `dialog_config.h`.

### 7. Wiring (`application.cpp`)
- Create `craft_recipe_registry` subsystem, load `craft_recipes.yaml`
- Create `alchemy_system`, wire dependencies
- Add `alchemy_system*` to `game_handlers` constructor
- Register handler callbacks

### 8. Tests (`tests/alchemy_test.cpp`)
- Craft recipe registry: YAML loading, id mapping, ingredient parsing
- Success chance: difficulty scaling, INT bonus, skill bonus, clamping
- Ingredient checking: exact counts, partial materials
- Craft attempt: success/failure paths, exp gain within skill_limit
- Edge cases: recipe id not found, empty ingredients

## Files to Create
1. `src/registry/craft_recipe_registry.h`
2. `src/registry/craft_recipe_registry.cpp`
3. `src/crafting/alchemy_system.h`
4. `src/crafting/alchemy_system.cpp`
5. `tests/alchemy_test.cpp`

## Files to Modify
1. `src/crafting/recipe_config.h` — add `craft_recipe` struct (file created by manufacturing)
2. `src/network/json_protocol.h` — 4 new message types
3. `src/network/json_protocol.cpp` — type_map entries, to_string cases
4. `src/bridge/handlers/game_handlers.h` — alchemy_system* member, 2 handler methods
5. `src/bridge/handlers/game_handlers.cpp` — handler implementations + registrations
6. `src/npc/dialog_config.h` — `open_alchemy` action
7. `src/application.cpp` — subsystem creation and wiring

## Coordination with Manufacturing
- Shares `recipe_config.h` — manufacturing creates it, alchemy adds to it
- Shares `craft_result` struct
- Both add to same files: `game_handlers`, `json_protocol`, `dialog_config`, `application.cpp`
- **To avoid conflicts**: alchemy should add its protocol messages AFTER manufacturing's block, and add handler methods in a separate section
