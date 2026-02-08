#pragma once

// mining_system.h
// Mining subsystem - mineral node lifecycle, mining logic, generation

#include "core/subsystem.h"
#include "core/types.h"
#include "crafting/mining_config.h"

#include <unordered_map>
#include <string>
#include <string_view>
#include <functional>
#include <cstdint>

namespace hb {
    class mining_registry;
    class scheduler;
}

namespace hb::skill {
    class skill_system;
}

namespace hb::inventory {
    class inventory_system;
}

namespace hb::item {
    class item_system;
}

namespace hb::player {
    class player_system;
}

namespace hb::world {
    class world_subsystem;
}

namespace hb::crafting {

// Active mineral node on the map
struct mineral_node
{
    uint32_t node_id{};                 // unique ID for protocol
    int32_t type_id{};                  // references mineral_type_config
    std::string map_name;
    int16_t x{};
    int16_t y{};
    int32_t hits_remaining{};
};

class mining_system : public subsystem
{
public:
    mining_system();
    ~mining_system() override;

    // Subsystem interface
    [[nodiscard]] auto name() const -> std::string_view override { return "mining_system"; }
    void initialize() override;
    void shutdown() override;

    // Wire dependencies
    void set_dependencies(player::player_system* players,
                          skill::skill_system* skills,
                          inventory::inventory_system* inventory,
                          item::item_system* items,
                          mining_registry* registry,
                          scheduler* sched,
                          world::world_subsystem* world);

    // Called after maps + registry loaded — starts periodic mineral generation
    void start_generation();

    // Per-attack mining attempt
    auto attempt_mine(entity_id player, int16_t target_x, int16_t target_y,
                      const std::string& map_name) -> mine_result;

    // Query
    [[nodiscard]] auto get_node_at(const std::string& map_name,
                                    int16_t x, int16_t y) const -> const mineral_node*;

    [[nodiscard]] auto node_count() const -> size_t { return nodes_.size(); }

    // Callbacks for game_handlers to broadcast spawn/despawn
    using node_spawn_callback = std::function<void(const mineral_node&)>;
    using node_despawn_callback = std::function<void(const mineral_node&)>;
    void set_spawn_callback(node_spawn_callback cb);
    void set_despawn_callback(node_despawn_callback cb);

    // Static for unit testing
    [[nodiscard]] static auto calculate_success_chance(
        int16_t skill, int16_t difficulty) -> int32_t;

private:
    void generate_minerals();       // periodic scheduler callback
    void spawn_mineral(const std::string& map_name, int16_t x, int16_t y, int32_t type_id);
    void despawn_mineral(const std::string& node_key);
    auto roll_drop(const mineral_type_config& config, int16_t skill) -> const mineral_drop*;

    static auto make_node_key(const std::string& map, int16_t x, int16_t y) -> std::string;

    std::unordered_map<std::string, mineral_node> nodes_;   // "map:x:y" -> node
    uint32_t next_node_id_{1};

    player::player_system* players_{nullptr};
    skill::skill_system* skills_{nullptr};
    inventory::inventory_system* inventory_{nullptr};
    item::item_system* items_{nullptr};
    mining_registry* registry_{nullptr};
    scheduler* scheduler_{nullptr};
    world::world_subsystem* world_{nullptr};

    node_spawn_callback spawn_callback_;
    node_despawn_callback despawn_callback_;
};

}  // namespace hb::crafting
