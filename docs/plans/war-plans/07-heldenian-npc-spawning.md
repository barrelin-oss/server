# Plan 07: Heldenian NPC Spawning & Teleportation

## Problems

1. **No NPC spawning** — legacy creates real tower NPCs (87/89) and door NPCs (91) on the map; new code uses abstract HP-based objectives
2. **No teleportation system** — legacy has fixed coordinates per mode/faction for entering the war zone
3. **No client evacuation** — legacy kicks non-admin players from war maps before spawning
4. **No occupy-flag / hero-flag system** — `bCheckHeldenianMap` gives combat bonuses

## Legacy Behavior (Reference: `Game.cpp`)

### Tower spawning (mode 1, `LocalStartHeldenianMode` line 54128):
```
Map: "BtField"
Config: m_stHeldenianTower[DEF_MAXHELDENIANTOWER] (200 max)
Each entry: {sTypeID, dX, dY, cSide}
- sTypeID: NPC type (87 or 89 for towers)
- cSide: 1 (Aresden) or 2 (Elvine)

For each tower in config:
    Find map "BtField"
    Create NPC of sTypeID at (dX, dY)
    Set NPC side = cSide
    If side == 1: m_iHeldenianAresdenLeftTower++
    If side == 2: m_iHeldenianElvineLeftTower++
```

### Door spawning (mode 2, `LocalStartHeldenianMode` line 54128):
```
Map: "HRampart"
Config: m_stHeldenianGateDoor[DEF_MAXHELDENIANDOOR] (10 max)
Each entry: {dX, dY, cDir}
- NPC type always 91
- Side = m_sLastHeldenianWinner (defending faction)
- Direction = cDir

For each door in config:
    Find map "HRampart"
    Create NPC type 91 at (dX, dY)
    Set NPC side = m_sLastHeldenianWinner
    Set NPC direction = cDir
```

### Tower/door destruction:
When a tower NPC (type 87 or 89) dies:
```cpp
// In NPC death handler, around line 10942-10948:
if (npc_type == 87 || npc_type == 89) {
    if (npc_side == 1) m_iHeldenianAresdenLeftTower--;
    if (npc_side == 2) m_iHeldenianElvineLeftTower--;
    if (m_iHeldenianAresdenLeftTower <= 0 || m_iHeldenianElvineLeftTower <= 0) {
        GlobalEndHeldenianMode();
    }
}
```

### Client evacuation (before spawning):
```cpp
// In LocalStartHeldenianMode:
for each client on Heldenian maps:
    if (admin_level < 1):
        RequestTeleportHandler(i, "1   ");  // Teleport to safe position
```

### NPC removal on map (before spawning):
```cpp
// All existing NPCs on Heldenian maps are removed
for each NPC on the map:
    mark as summoned, then remove
```

### Teleportation (`RequestHeldenianTeleport` line 53959):
Triggered by NPC interaction ("Gail" NPC). Conditions: Heldenian active, non-civilian, has faction.

```
Mode 1 (tower, m_cHeldenianType == 1), map "BtField":
    Aresden (side 1): position (68, 225)
    Elvine (side 2): position (202, 70)

Mode 2 (door, m_cHeldenianType == 2), map "HRampart":
    Defending faction (m_sLastHeldenianWinner): position (81, 42)
    Attacking faction: position (156, 153)
```

### RemoveHeldenianNpc (line 53793):
```cpp
// Kill a war NPC properly:
m_bIsKilled = TRUE
HP = 0
Decrement alive count
Release follow mode, clear target
Send dying animation to nearby clients
Clear map ownership
Set behavior to 4 (dead)
Record death time
```

### bCheckHeldenianMap (line 53904):
Checks tile occupy status for combat bonuses:
- Negative occupy status + Aresden attacker → bonus
- Positive occupy status + Elvine attacker → bonus
Used by `SetHeroFlag()` to set/clear hero flag on players

## Current New Code

- `src/war/heldenian/heldenian_system.h/.cpp`
- Uses `heldenian_objective` structs: `{id, faction, max_hp, current_hp, position}`
- No NPC entities, no teleportation, no evacuation

## Implementation Plan

### Step 1: Add tower/door NPC config

In `heldenian_types.h`:
```cpp
struct heldenian_tower_config
{
    uint16_t npc_type{87};  // 87 or 89
    int16_t x{0};
    int16_t y{0};
    war_faction faction{war_faction::neutral};
};

struct heldenian_door_config
{
    int16_t x{0};
    int16_t y{0};
    uint8_t direction{0};
    // NPC type is always 91, side is always defending faction
};

struct heldenian_config
{
    // ... existing fields ...
    std::string tower_map{"BtField"};
    std::string door_map{"HRampart"};
    std::vector<heldenian_tower_config> towers;
    std::vector<heldenian_door_config> doors;
};
```

### Step 2: Spawn NPCs at war start

In `start_heldenian()`, after setting up state:

```cpp
if (mode == heldenian_mode::tower_defense && npcs_ && world_)
{
    auto* m = world_->get_map_by_name(config_.tower_map);
    if (m)
    {
        // Evacuate existing players
        evacuate_map(config_.tower_map);
        // Remove existing NPCs on map
        clear_map_npcs(config_.tower_map);

        for (const auto& tc : config_.towers)
        {
            auto result = npcs_->spawn_npc(npc_id(tc.npc_type), m->id(), {tc.x, tc.y});
            if (result.is_ok())
            {
                auto& obj = objectives_.emplace_back();
                obj.id = next_objective_id_++;
                obj.faction = tc.faction;
                obj.eid = result.value();
                obj.current_hp = obj.max_hp;
                // Track per-faction counts
            }
        }
    }
}
else if (mode == heldenian_mode::door_defense && npcs_ && world_)
{
    auto* m = world_->get_map_by_name(config_.door_map);
    if (m)
    {
        evacuate_map(config_.door_map);
        clear_map_npcs(config_.door_map);

        for (const auto& dc : config_.doors)
        {
            auto result = npcs_->spawn_npc(npc_id(91), m->id(), {dc.x, dc.y});
            if (result.is_ok())
            {
                auto& obj = objectives_.emplace_back();
                obj.id = next_objective_id_++;
                obj.faction = defending_faction_;
                obj.eid = result.value();
                obj.current_hp = obj.max_hp;
            }
        }
    }
}
```

### Step 3: Link NPC death to objective destruction

When a tower/door NPC dies (via NPC death callback), find the matching objective and destroy it:

```cpp
void heldenian_system::on_npc_killed(entity::entity eid)
{
    if (!active_) return;

    for (auto& obj : objectives_)
    {
        if (obj.eid.id == eid.id && !obj.destroyed)
        {
            obj.destroyed = true;
            obj.current_hp = 0;

            // Update tower counts
            if (mode_ == heldenian_mode::tower_defense)
                check_victory_condition();
            else if (mode_ == heldenian_mode::door_defense)
                check_door_victory();

            broadcast_objective_update(obj.faction);
            break;
        }
    }
}
```

### Step 4: Add entity field to heldenian_objective

```cpp
struct heldenian_objective
{
    uint16_t id{0};
    war_faction faction{war_faction::neutral};
    int32_t max_hp{100};
    int32_t current_hp{100};
    bool destroyed{false};
    entity::entity eid{};  // NEW — link to spawned NPC
    // position info for reference
    std::string map_name;
    int16_t x{0};
    int16_t y{0};
};
```

### Step 5: Add teleportation support

```cpp
struct heldenian_teleport_coords
{
    std::string map_name;
    int16_t x{0};
    int16_t y{0};
};

auto heldenian_system::get_teleport_destination(war_faction player_faction)
    -> std::optional<heldenian_teleport_coords>
{
    if (!active_) return std::nullopt;

    if (mode_ == heldenian_mode::tower_defense)
    {
        if (player_faction == war_faction::aresden)
            return heldenian_teleport_coords{config_.tower_map, 68, 225};
        if (player_faction == war_faction::elvine)
            return heldenian_teleport_coords{config_.tower_map, 202, 70};
    }
    else if (mode_ == heldenian_mode::door_defense)
    {
        if (player_faction == defending_faction_)
            return heldenian_teleport_coords{config_.door_map, 81, 42};
        else
            return heldenian_teleport_coords{config_.door_map, 156, 153};
    }

    return std::nullopt;
}
```

Wire this into the NPC interaction handler for the "Gail" NPC (or equivalent teleport trigger).

### Step 6: Add evacuation and NPC clearing

```cpp
void heldenian_system::evacuate_map(const std::string& map_name)
{
    if (!players_) return;
    players_->for_each_player([&](player_id pid, player::player& plr) {
        if (plr.current_map == map_name && plr.admin == player::admin_level::player)
        {
            // Teleport to safe position via callback
            if (teleport_fn_) teleport_fn_(pid, "");  // empty = safe position
        }
    });
}
```

### Step 7: Clean up NPCs on war end

In `cleanup_heldenian()`:
```cpp
for (auto& obj : objectives_)
{
    if (obj.eid.id != 0 && npcs_)
    {
        npcs_->kill_npc(obj.eid);  // Triggers death animation
    }
}
objectives_.clear();
```

### Step 8: Update tests

1. `tower_mode_spawns_npcs` — verify NPCs created for each tower config entry
2. `door_mode_spawns_defender_doors` — verify type 91 NPCs with defender faction
3. `npc_death_destroys_objective` — killing tower NPC reduces tower count
4. `all_towers_destroyed_triggers_end` — last tower → war ends
5. `teleport_coords_tower_mode` — verify correct coords per faction
6. `teleport_coords_door_mode` — verify attacker/defender coords
7. `evacuation_before_spawn` — non-admin players teleported out
8. `cleanup_kills_remaining_npcs` — war end removes all war NPCs

## Files to Modify
- `src/war/heldenian/heldenian_types.h` — add NPC config structs, entity field
- `src/war/heldenian/heldenian_system.h` — add spawning, teleport, evacuation methods
- `src/war/heldenian/heldenian_system.cpp` — implement all changes
- `tests/test_heldenian_system.cpp` — add NPC and teleport tests

## Acceptance Criteria
- Tower mode spawns real NPC entities on BtField from config
- Door mode spawns type 91 NPCs on HRampart for defending faction
- NPC death properly destroys corresponding objective
- Teleportation returns correct coords per mode/faction
- Players evacuated before war starts
- NPCs cleaned up when war ends
- All tests pass
