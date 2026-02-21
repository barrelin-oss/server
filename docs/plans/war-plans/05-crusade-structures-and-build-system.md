# Plan 05: Crusade Structures & Build System

## Problems

1. **No pre-placed crusade structures at war start** — legacy spawns AGTs, CGTs, ESGs, GMGs, and ManaStones from config
2. **Guild construct location system entirely missing** — commanders designate build zones, constructors must be near them
3. **Build proximity restrictions missing** — guard towers can't be within 2 tiles of each other, Y-bounds
4. **Per-guild building limit missing** — max 10 constructions per guild (`DEF_MAXCONSTRUCTNUM`)
5. **Map restrictions for summoning missing** — can't summon on toh3, icebound, or indoor maps
6. **Construction costs are fabricated** — legacy has zero cost for structures (36-39) with location restrictions, explicit costs only for mobile units
7. **Follow/guard mode for mobile units missing**
8. **Missing mobile unit types** — GHK(44), GHKABS(45), TK(46), BG(47), faction-specific types 82-89
9. **ESG spatial query uses circle instead of legacy square**

## Legacy Behavior (Reference: `Game.cpp`)

### Pre-placed structures (`bReadCrusadeStructureConfigFile` line 5270, `CreateCrusadeStructures` line 41391):
- Config file with entries: `{index, map_name, npc_type, x, y}`
- NPC types in config: 36 (AGT), 37 (CGT), 40 (ESG), 41 (GMG), 42 (ManaStone)
- Up to `DEF_MAXCRUSADESTRUCTURES` (300) structures
- Spawned on faction-specific maps at crusade start
- Each faction gets a set of defensive structures pre-placed

### Guild construct location (`RequestSetGuildConstructLocHandler` line 41470):
- Guild master commander sets construction zone coordinates via map status UI
- Stored in `m_pGuildTeleportLoc[]` array: guild GUID → (x, y, map_name)
- LRU replacement when array full (`DEF_MAXGUILDS` slots)
- Cross-server sync via gate server

### Structure summoning (`RequestSummonWarUnitHandler` line 41576):
```
- Map restriction: toh3 and icebound disallowed for non-admins
- Indoor restriction: fixed-day-mode maps disallowed
- cNum forced to 1 (only 1 unit at a time)
- For structures (types 36-39):
  - Cost: 0 construction points (not in the cost array)
  - Must have guild construct location set
  - Must be within 10 tiles of construct location
  - Guild can't exceed DEF_MAXCONSTRUCTNUM (10) total constructions
  - Guard towers (36-37): can't be within 2 tiles of another guard tower
  - Guard towers (36-37): can't be placed at y <= 32 or y >= 783
  - Creates NPC at specified location
- For mobile units (types 43-47, 51):
  - Cost from m_iNpcConstructionPoint[] array:
    LWB(43)=1000, GHK(44)=2000, GHKABS(45)=3000, TK(46)=2000, BG(47)=3000, Catapult(51)=1500
  - No location restrictions
  - cMode==NULL → follow mode, else guard mode
```

### Structure removal at crusade end:
- `RemoveCrusadeStructures()` (line 42066): Silently deletes NPC types 36-42 (uses `DeleteNpc`)
- `RemoveCrusadeNpcs()` (line 44736): Kills NPC types 43-47, 51 via `NpcKilledHandler` (triggers death/loot)

### ESG spatial query (`MeteorStrikeHandler` line 46189):
- Scans **rectangular** region: `dX-10` to `dX+10`, `dY-10` to `dY+10` (21x21 tiles)
- Uses `GetOwner()` on each tile to find NPC type 40
- New code uses Euclidean distance (circle), which excludes corner tiles

## Implementation Plan

### Step 1: Add pre-placed structure config and spawning

Add to `crusade_config`:
```cpp
struct initial_structure
{
    std::string map_name;
    uint16_t npc_type{0};  // 36=AGT, 37=CGT, 40=ESG, 41=GMG, 42=ManaStone
    int16_t x{0};
    int16_t y{0};
    war_faction faction{war_faction::neutral};
};

struct crusade_config
{
    // ... existing fields ...
    std::vector<initial_structure> initial_structures;
};
```

In `start_crusade()`, after setting up state:
```cpp
// Spawn initial structures from config
for (const auto& is : config_.initial_structures)
{
    if (npcs_ && world_)
    {
        auto* m = world_->get_map_by_name(is.map_name);
        if (m)
        {
            auto result = npcs_->spawn_npc(npc_id(is.npc_type), m->id(), {is.x, is.y});
            if (result.is_ok())
            {
                war_structure_instance ws;
                ws.eid = result.value();
                ws.type = npc_type_to_war_unit(is.npc_type);
                ws.faction = is.faction;
                ws.map_name = is.map_name;
                ws.x = is.x;
                ws.y = is.y;
                war_structures_.push_back(ws);
            }
        }
    }
}
```

### Step 2: Add guild construct location system

```cpp
struct guild_construct_location
{
    uint32_t guild_id{0};
    std::string map_name;
    int16_t x{0};
    int16_t y{0};
    int32_t structure_count{0};  // track per-guild build count
};

// In crusade_system:
std::unordered_map<uint32_t, guild_construct_location> guild_construct_locations_;

auto set_guild_construct_location(player_id commander, const std::string& map, int16_t x, int16_t y) -> crusade_result;
```

Implementation:
- Verify player is a commander
- Look up their guild via `social_`
- Store/update the location in the map
- Reset `structure_count` to 0

### Step 3: Add structure proximity and placement validation

In `summon_war_unit()`, add validation for structures:
```cpp
bool is_structure = (type == war_unit_type::agt || type == war_unit_type::cgt ||
                     type == war_unit_type::mana_collector || type == war_unit_type::detector);

if (is_structure)
{
    // Must have guild construct location
    auto guild_id = social_->get_player_guild(pid);
    auto it = guild_construct_locations_.find(guild_id.value);
    if (it == guild_construct_locations_.end())
        return crusade_result::no_construct_location;

    auto& loc = it->second;

    // Must be within 10 tiles
    if (std::abs(x - loc.x) > 10 || std::abs(y - loc.y) > 10)
        return crusade_result::too_far_from_construct_location;

    // Per-guild limit
    if (loc.structure_count >= 10)
        return crusade_result::guild_build_limit;

    // Guard tower proximity check (types 36-37 / agt-cgt)
    if (type == war_unit_type::agt || type == war_unit_type::cgt)
    {
        // Can't be within 2 tiles of another guard tower
        for (const auto& ws : war_structures_)
        {
            if ((ws.type == war_unit_type::agt || ws.type == war_unit_type::cgt) &&
                ws.map_name == map_name &&
                std::abs(ws.x - x) <= 2 && std::abs(ws.y - y) <= 2)
            {
                return crusade_result::too_close_to_tower;
            }
        }

        // Y-bounds check
        if (y <= 32 || y >= 783)
            return crusade_result::invalid_position;
    }

    loc.structure_count++;
}
```

### Step 4: Fix construction costs

Structures (types 36-39) should have **zero** construction point cost. They're restricted by location instead. Only mobile units have costs:

```cpp
auto crusade_system::get_construction_cost(war_unit_type type) const -> int32_t
{
    switch (type)
    {
    // Structures: zero cost (restricted by location)
    case war_unit_type::agt:            return 0;
    case war_unit_type::cgt:            return 0;
    case war_unit_type::mana_collector: return 0;
    case war_unit_type::detector:       return 0;

    // Mobile units: explicit costs
    case war_unit_type::lwb:       return 1000;
    case war_unit_type::ghk:       return 2000;
    case war_unit_type::ghkabs:    return 3000;
    case war_unit_type::tk:        return 2000;
    case war_unit_type::bg:        return 3000;
    case war_unit_type::catapult:  return 1500;

    default: return -1;  // invalid
    }
}
```

For structures with zero cost, skip the cost check in `summon_war_unit()`:
```cpp
if (cost > 0 && data->construction_points < cost)
    return crusade_result::insufficient_points;
if (cost > 0)
    data->construction_points -= cost;
```

### Step 5: Add missing mobile unit types

Add to `war_unit_type` enum:
```cpp
enum class war_unit_type : uint8_t
{
    agt = 36,
    cgt = 37,
    mana_collector = 38,
    detector = 39,
    esg = 40,
    lwb = 43,
    ghk = 44,        // NEW
    ghkabs = 45,     // NEW
    tk = 46,          // NEW
    bg = 47,          // NEW
    catapult = 51,
};
```

Add NPC type mapping for the new types in `get_npc_type_for_unit()`.

### Step 6: Add map restrictions

```cpp
// In summon_war_unit(), early validation:
if (map_name == "toh3" || map_name == "icebound")
    return crusade_result::restricted_map;

// Indoor map check (if world_ provides this info)
if (world_)
{
    auto* m = world_->get_map_by_name(map_name);
    if (m && m->is_fixed_day_mode())
        return crusade_result::restricted_map;
}
```

### Step 7: Fix ESG spatial query to use rectangular (not Euclidean)

In wherever the ESG count is computed (likely `crusade_system.cpp` in the meteor callback), change:
```cpp
// BEFORE (circular):
int dx = ws.x - sp.x;
int dy = ws.y - sp.y;
if (dx*dx + dy*dy <= radius*radius) count++;

// AFTER (rectangular, matching legacy):
if (std::abs(ws.x - sp.x) <= 10 && std::abs(ws.y - sp.y) <= 10) count++;
```

### Step 8: Differentiate structure vs NPC removal at cleanup

In `cleanup_crusade()`:
```cpp
for (auto& ws : war_structures_)
{
    if (ws.eid.id != 0 && npcs_)
    {
        bool is_mobile = (ws.type == war_unit_type::lwb || ws.type == war_unit_type::ghk ||
                         ws.type == war_unit_type::ghkabs || ws.type == war_unit_type::tk ||
                         ws.type == war_unit_type::bg || ws.type == war_unit_type::catapult);

        if (is_mobile)
            npcs_->kill_npc(ws.eid);   // Triggers death/loot (NpcKilledHandler)
        else
            npcs_->despawn_npc(ws.eid); // Silent removal (DeleteNpc)
    }
}
```

### Step 9: Add result codes

Add to `crusade_result` enum:
```cpp
no_construct_location,
too_far_from_construct_location,
guild_build_limit,
too_close_to_tower,
invalid_position,
restricted_map,
```

### Step 10: Update crusade.yaml

Add initial structure definitions:
```yaml
crusade:
  initial_structures:
    - map: aresden
      npc_type: 36  # AGT
      x: 100
      y: 200
      faction: aresden
    # ... etc, from legacy config file
```

### Step 11: Write tests

1. `initial_structures_spawned_at_start` — verify NPCs created from config
2. `guild_construct_location_required_for_structures` — can't build without location set
3. `structure_within_10_tiles_of_construct` — proximity check works
4. `guild_build_limit_10` — 11th build fails
5. `guard_tower_proximity_2_tiles` — can't build tower near another
6. `guard_tower_y_bounds` — y<=32 or y>=783 rejected
7. `structures_zero_cost` — AGT/CGT/etc cost nothing
8. `mobile_units_legacy_costs` — LWB=1000, GHK=2000, etc.
9. `restricted_maps_rejected` — toh3, icebound blocked
10. `esg_rectangular_query` — corner tiles included (vs circle)
11. `cleanup_kills_mobile_despawns_structures` — differentiated removal

## Files to Modify
- `src/war/crusade/crusade_types.h` — new structs, enum values, result codes
- `src/war/crusade/crusade_system.h` — new methods and members
- `src/war/crusade/crusade_system.cpp` — implement all changes
- `tests/test_crusade_system.cpp` — comprehensive new tests
- `bin/game_configs/crusade.yaml` — add initial structure config

## Acceptance Criteria
- Pre-placed structures spawn at crusade start from config
- Guild construct location system works (set location, validate proximity)
- Per-guild build limit of 10
- Guard tower proximity and Y-bound restrictions enforced
- Structures cost zero points; mobile units match legacy costs
- Map restrictions enforced
- ESG query uses rectangular area
- Mobile units killed on cleanup; structures silently removed
- All tests pass
