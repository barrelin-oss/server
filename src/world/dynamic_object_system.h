#pragma once

// dynamic_object_system.h
// Temporary world objects placed on map tiles: damaging fields (fire, ice storm,
// poison cloud), spike traps, and future gathering/war markers.
// Modernized from the legacy CDynamicObject system (docs/legacy/16_dynamic_objects.md).

#include "core/subsystem.h"
#include "core/types.h"
#include "core/enums.h"
#include "entity/entity.h"
#include "world/position.h"

#include <cstdint>
#include <functional>
#include <unordered_map>
#include <vector>

namespace hb::world
{

// A single dynamic object placed on a map tile
struct dynamic_object
{
    uint16_t id{0};
    dynamic_object_type type{dynamic_object_type::fire};
    entity::entity owner{}; // Caster/creator, for damage attribution (0 = none)
    map_id map{};
    position pos{};
    int64_t created_ms{0};
    int64_t duration_ms{0}; // 0 = permanent
    int32_t power{0};       // Spell power (poison cloud damage tier / poison level)
};

// Manages lifecycle and effect ticks of dynamic objects.
// Effect processing (legacy semantics):
//   fire/fire3:   3x3 area, 1d6 fire damage per tick
//   icestorm:     5x5 area, 3d3+5 ice damage per tick, applies freeze (20s)
//   pcloud_*:     3x3 area, 1d6 (power < 20) or 1d8 magic damage per tick, applies poison
//   spike:        no tick — 2d4 damage when an entity steps on the tile (on_entity_step)
// Ticks run once per second. Damage is attributed to the owner entity.
class dynamic_object_system : public subsystem
{
public:
    dynamic_object_system();
    ~dynamic_object_system() override;

    [[nodiscard]] auto name() const -> std::string_view override { return "dynamic_object_system"; }
    void initialize() override;
    void shutdown() override;
    void update(float delta_time) override;

    // Spawn a single object at pos. Fails (returns 0) if the map/tile is invalid,
    // the tile is not walkable, is a safe zone, or already holds a dynamic object.
    auto spawn(entity::entity owner,
               dynamic_object_type type,
               map_id map,
               position pos,
               int64_t duration_ms,
               int32_t power) -> uint16_t;

    // Spawn a spell field around center. Spikes fill the (2rx+1)x(2ry+1) area with
    // one object per tile; area-processing types (fire, icestorm, pcloud) place a
    // single object at the center. Returns the number of objects spawned.
    auto spawn_field(entity::entity owner,
                     dynamic_object_type type,
                     map_id map,
                     position center,
                     int16_t radius_x,
                     int16_t radius_y,
                     int64_t duration_ms,
                     int32_t power) -> int;

    // Remove an object (clears its tile and fires the remove callback)
    void remove(uint16_t id);

    [[nodiscard]] auto get(uint16_t id) const -> const dynamic_object*;
    [[nodiscard]] auto object_count() const -> size_t { return objects_.size(); }

    // Movement trigger: applies spike damage if a spike occupies pos.
    // Called from the movement handler after a successful step.
    void on_entity_step(entity::entity ent, map_id map, position pos);

    // Objects within a rectangular area (for login/teleport visibility sync)
    [[nodiscard]] auto objects_in_area(map_id map, position center, int radius_x, int radius_y) const
        -> std::vector<const dynamic_object*>;

    // Broadcast callbacks (spawn/remove) — wired by the bridge
    using object_callback = std::function<void(const dynamic_object&)>;
    void set_on_spawn_callback(object_callback cb) { on_spawn_ = std::move(cb); }
    void set_on_remove_callback(object_callback cb) { on_remove_ = std::move(cb); }

private:
    void process_expiry(int64_t now);
    void process_effects();
    void apply_area_damage(const dynamic_object& obj);
    void damage_entity_at(const dynamic_object& obj, position tile);

    [[nodiscard]] auto now_ms() const -> int64_t;
    [[nodiscard]] auto allocate_id() -> uint16_t;
    [[nodiscard]] static auto is_damaging_area_type(dynamic_object_type type) -> bool;

    std::unordered_map<uint16_t, dynamic_object> objects_;
    uint16_t next_id_{1};
    float tick_accum_{0.0f};

    object_callback on_spawn_;
    object_callback on_remove_;
};

} // namespace hb::world
