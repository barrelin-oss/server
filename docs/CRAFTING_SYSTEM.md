# Crafting System

The crafting system covers two subsystems: **manufacturing** (weapons, armor, ingots) and **alchemy** (potions, gems, spell manuals). Both follow the same architecture but use different formulas, skill types, and YAML configs.

---

## Architecture

```
YAML Configs                  Registries                   Systems
─────────────                 ──────────                   ───────
build_recipes.yaml  ──────►  build_recipe_registry  ──►  manufacturing_system
recipes.yaml        ──────►  craft_recipe_registry   ──►  alchemy_system
craft_recipes.yaml  ──────►       (merged)
```

**Key files:**

| File | Purpose |
|------|---------|
| `src/crafting/recipe_config.h` | Shared data structs (`build_recipe`, `craft_recipe`, `recipe_ingredient`, `craft_result`) |
| `src/registry/build_recipe_registry.h/.cpp` | Loads `build_recipes.yaml` → `std::vector<build_recipe>` |
| `src/registry/craft_recipe_registry.h/.cpp` | Loads `recipes.yaml` + `craft_recipes.yaml` → two `std::vector<craft_recipe>` |
| `src/crafting/manufacturing_system.h/.cpp` | Manufacturing business logic |
| `src/crafting/alchemy_system.h/.cpp` | Alchemy business logic |
| `src/bridge/handlers/game_handlers.cpp` | Protocol handlers (4 manufacturing + 4 alchemy) |
| `src/application.cpp` | Subsystem registration, YAML loading, wiring |

---

## YAML Config Formats

### Manufacturing (`bin/game_configs/build_recipes.yaml`)

```yaml
build_recipes:
  - result: IronIngot       # Item name (resolved to template ID via item_registry)
    skill_req: 20           # Minimum manufacturing skill to attempt
    skill_limit: 0          # Max skill for XP gain (0 = always grants XP)
    success_rate: 30        # Base success percentage (0-100)
    ingredients:
      - { item_id: 357, count: 1 }   # item_id is the item template ID
      - { item_id: 357, count: 1 }   # Same ID listed multiple times gets aggregated
      - { item_id: 357, count: 1 }
      - { item_id: 501, count: 1 }
```

Recipes are auto-assigned sequential IDs (0, 1, 2, ...) based on their order in the file. The `result` name is resolved to a `result_template_id` via `item_registry::find_by_name()` at load time — unresolvable names log a warning and get `result_template_id = 0`.

Ingredient `item_id` values reference item template IDs from `Items.yaml`. The same `item_id` can appear multiple times — the system aggregates them before checking/consuming inventory.

Currently **83 recipes**.

### Alchemy (`bin/game_configs/recipes.yaml`)

```yaml
alchemy_recipes:
  - id: 1                   # Explicit recipe ID (used in protocol)
    result: RedPotion
    skill_limit: 20         # Minimum alchemy skill to attempt
    difficulty: 10          # Subtracted from base success chance
    ingredients:
      - { item_id: 220, count: 1 }
      - { item_id: 192, count: 1 }
      - { item_id: 191, count: 1 }
```

Currently **80 alchemy recipes** (potions, scrolls, extracts).

### Gem Crafting (`bin/game_configs/craft_recipes.yaml`)

```yaml
crafting_recipes:
  - id: 1                   # Explicit recipe ID
    result: MagicNecklace(DF+15)
    skill_limit: 10
    difficulty: 70
    ingredients:
      - { item_id: 657, count: 1 }
      - { item_id: 356, count: 1 }
      - { item_id: 354, count: 1 }
      - { item_id: 311, count: 1 }
      - { item_id: 311, count: 1 }
```

Currently **38 crafting recipes** (enchanted jewelry, magic items).

Both alchemy and gem crafting use the same `craft_recipe` struct and are served by the same `alchemy_system`. The `craft_recipe_registry` keeps them in separate vectors (`get_all_alchemy()` / `get_all_crafting()`) but provides unified `get(id)` lookup.

---

## Success Formulas

### Manufacturing

```
chance = success_rate + min(40, (skill - skill_req) * 2) + dex / 2
clamped to [10, 95]
```

- `success_rate` — base from recipe YAML
- Skill bonus capped at +40 (so 20 levels above `skill_req` maxes it out)
- DEX contributes half its value
- Minimum 10% even for impossible recipes, maximum 95% even for trivial ones

### Alchemy

```
chance = (100 - difficulty) + skill / 2 + int / 3
clamped to [5, 98]
```

- Higher difficulty = lower base chance
- Skill and INT both contribute (INT through integer division)
- Wider clamp range than manufacturing (5-98 vs 10-95)

Both formulas are implemented as `static` methods on their respective systems, making them easy to unit test without subsystem dependencies.

---

## Skill Mechanics

| Aspect | Manufacturing | Alchemy |
|--------|--------------|---------|
| Skill type | `skill_type::manufacturing` (2) | `skill_type::alchemy` (3) |
| Skill cap | STR * 2 | INT * 2 |
| XP formula | `random(1, max(1, skill_limit / 4))` | `random(1, max(1, difficulty / 3))` |
| XP condition | Only if `skill < skill_limit` (or `skill_limit == 0`) | Always granted |
| Gate check | `skill >= skill_req` | `skill >= skill_limit` |

If the player's skill has reached the cap (STR*2 or INT*2), the craft attempt is rejected with `insufficient_skill`.

---

## Craft Flow

Both systems follow the same sequence:

1. **Validate skill** — player meets minimum requirement and hasn't hit stat cap
2. **Check ingredients** — aggregate by `item_id`, verify inventory has enough of each
3. **Consume ingredients** — removed from inventory (happens on both success AND failure)
4. **Roll d100** — compare against calculated success chance
5. **On success** — check inventory space, create item via `item_system::create_from_template()`, add to inventory
6. **Grant XP** — via `skill_system::add_skill_exp()` (with system-specific conditions)
7. **Return `craft_result`** — success/failure, created item, XP gained, levels gained

Ingredients are consumed even on failure — this matches the original Helbreath behavior.

---

## Protocol Messages

### Manufacturing

| Message | Direction | Key Fields |
|---------|-----------|------------|
| `manufacture_list_request` | C → S | *(empty)* |
| `manufacture_list_response` | S → C | `recipes[]` with `id`, `name`, `skill_req`, `success_rate`, `ingredients[]` |
| `manufacture_request` | C → S | `recipe_index` (0-based) |
| `manufacture_response` | S → C | `success`, `item_name`, `exp_gained`, `reason` |

### Alchemy

| Message | Direction | Key Fields |
|---------|-----------|------------|
| `alchemy_list_request` | C → S | *(empty)* |
| `alchemy_list_response` | S → C | `recipes[]` with `id`, `name`, `skill_limit`, `difficulty`, `ingredients[]` |
| `alchemy_request` | C → S | `recipe_id` |
| `alchemy_response` | S → C | `success`, `item_name`, `exp_gained`, `reason` |

Failure `reason` values: `"insufficient_skill"`, `"insufficient_materials"`, `"inventory_full"`.

See `docs/JSON_PROTOCOL.md` for full message schemas.

---

## NPC Dialog Integration

NPC dialog trees can trigger the crafting UI via two dialog actions:

- `open_manufacturing` (enum value 8) — sends the `manufacture_list_response` automatically
- `open_alchemy` (enum value 9) — sends the `alchemy_list_response` automatically

These are configured in `bin/game_configs/dialogs.yaml`:

```yaml
dialogs:
  Gandlf:
    greeting: "Welcome to my forge."
    start_node: start
    nodes:
      start:
        text: "What would you like to do?"
        options:
          - label: "I want to craft something"
            action: open_manufacturing
          - label: "Never mind"
            action: close
```

When the client selects an `open_manufacturing`/`open_alchemy` dialog option, the server responds with the dialog action confirmation AND the recipe list in one sequence. The client can then send `manufacture_request` or `alchemy_request` directly.

Players can also send `manufacture_list_request` or `alchemy_list_request` directly without NPC interaction — crafting is not NPC-gated.

---

## Application Wiring

In `application.cpp`, the crafting subsystems are set up in this order:

1. **Register subsystems** — `build_recipe_registry`, `craft_recipe_registry`, `manufacturing_system`, `alchemy_system`
2. **Load YAML** (after `item_registry` is loaded so name resolution works):
   - `build_recipes->load_from_file("game_configs/build_recipes.yaml", items)`
   - `craft_recipes->load_alchemy("game_configs/recipes.yaml", items)`
   - `craft_recipes->load_crafting("game_configs/craft_recipes.yaml", items)`
3. **Wire dependencies** on both systems (`player_system`, `skill_system`, `inventory_system`, `item_system`, respective registry)
4. **Pass to `game_handlers::initialize()`** — manufacturing, alchemy, skill_system, quest_system

---

## Extending the System

### Adding New Recipes

Just edit the YAML files. No code changes needed.

**New manufacturing recipe** — append to `bin/game_configs/build_recipes.yaml`:

```yaml
  - result: AdamantiumBlade       # Must match an item name in Items.yaml
    skill_req: 90                 # Min manufacturing skill
    skill_limit: 100              # Stop granting XP above this skill level
    success_rate: 20              # 20% base chance
    ingredients:
      - { item_id: 510, count: 2 }
      - { item_id: 503, count: 1 }
```

**New alchemy recipe** — append to `bin/game_configs/recipes.yaml`:

```yaml
  - id: 81                        # Must be unique across alchemy recipes
    result: SuperManaPotion
    skill_limit: 60
    difficulty: 50
    ingredients:
      - { item_id: 223, count: 1 }
      - { item_id: 217, count: 2 }
```

**New gem crafting recipe** — append to `bin/game_configs/craft_recipes.yaml`:

```yaml
  - id: 39                        # Must be unique across crafting recipes
    result: MagicRing(HP+50)
    skill_limit: 50
    difficulty: 60
    ingredients:
      - { item_id: 889, count: 1 }
      - { item_id: 658, count: 1 }
```

Reload requires server restart. The `result` name must match an item in `Items.yaml` — unmatched names log a warning and produce items with `template_id = 0` (which will fail to create).

### Modifying Formulas

Edit the `calculate_success_chance()` static method in:
- `src/crafting/manufacturing_system.cpp` — manufacturing formula
- `src/crafting/alchemy_system.cpp` — alchemy formula

The static methods have dedicated unit tests in `tests/test_manufacturing.cpp` and `tests/test_alchemy.cpp` — update the tests to match any formula changes.

### Adding a New Crafting Category

To add an entirely new crafting type (e.g., enchanting, cooking):

1. **Choose your recipe struct** — use `build_recipe` if recipes are index-based with `skill_req`/`success_rate`, or `craft_recipe` if they're ID-based with `skill_limit`/`difficulty`. Or add a new struct to `recipe_config.h` if neither fits.

2. **Create a registry** — follow the `build_recipe_registry` pattern:
   - New file `src/registry/my_recipe_registry.h/.cpp`
   - Inherit from `subsystem`
   - Implement `load_from_file(path, item_registry&)`
   - Register in `application.cpp` with `subsystems().create_subsystem<>()`

3. **Create the system** — follow the `manufacturing_system` pattern:
   - New file `src/crafting/my_system.h/.cpp`
   - Inherit from `subsystem`
   - `set_dependencies()` for player, skill, inventory, item, and your registry
   - `get_available_recipes()` filtered by player skill
   - `attempt_craft()` with your success formula
   - Static `calculate_success_chance()` for testability

4. **Add protocol messages** — 4 messages (list_request, list_response, craft_request, craft_response):
   - Enum entries in `src/network/json_protocol.h`
   - `to_string()` cases, `type_map` entries, `from_json()`, builder functions in `json_protocol.cpp`

5. **Add handlers** in `game_handlers.h/.cpp`:
   - Forward declaration, member pointer, `initialize()` parameter
   - Handler method + switch case in `handle_message()`

6. **Wire in `application.cpp`**:
   - `#include`, `create_subsystem`, YAML loading, `set_dependencies`, pass to `game_handlers::initialize()`
   - Add message types to the WebSocket routing switch

7. **Add dialog action** (optional) — new enum value in `dialog_config.h`, handler case in `game_handlers.cpp`

8. **Add to CMakeLists.txt** — new `.cpp` files in `REGISTRY_SOURCES` / `CRAFTING_SOURCES` and test files

9. **Write tests** — registry YAML loading + success formula unit tests

---

## Tests

| Test File | Count | What's Tested |
|-----------|-------|--------------|
| `tests/test_build_recipe_registry.cpp` | 11 | YAML parsing, name resolution, lookup, error cases |
| `tests/test_craft_recipe_registry.cpp` | 9 | Alchemy + crafting loading, cross-category lookup |
| `tests/test_manufacturing.cpp` | 13 | Success formula, clamping, skill/DEX bonuses, defaults |
| `tests/test_alchemy.cpp` | 13 | Success formula, difficulty scaling, INT bonus, truncation |

Run crafting tests only:

```bash
./bin/hgserver_tests --gtest_filter='*build_recipe*:*craft_recipe*:*manufacturing*:*alchemy*'
```
