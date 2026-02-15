#pragma once

// fishing_system.h
// Fishing subsystem - fish node lifecycle, engagement mechanic, catch logic

#include "core/subsystem.h"
#include "core/types.h"
#include "crafting/fishing_config.h"

#include <array>
#include <optional>
#include <string>
#include <string_view>
#include <functional>
#include <chrono>
#include <cstdint>

namespace hb
{
class fishing_registry;
class scheduler;
} // namespace hb

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

namespace hb::world
{
class world_subsystem;
}

namespace hb::crafting
{

// Active fish node on the map (like legacy CFish class)
struct fish_node
{
    uint32_t index{};  // Node ID (1-199, 0 reserved = empty)
    int32_t type_id{}; // fish_type_config ID
    std::string map_name;
    int16_t x{};
    int16_t y{};
    const fish_type_config* config{};
    int32_t engaging_count{}; // # players currently fishing this node
    std::chrono::steady_clock::time_point spawn_time{};
    duration_ms lifespan{};
};

class fishing_system : public subsystem
{
public:
    fishing_system();
    ~fishing_system() override;

    // Subsystem interface
    [[nodiscard]] auto name() const -> std::string_view override { return "fishing_system"; }
    void initialize() override;
    void shutdown() override;

    // Wire dependencies
    void set_dependencies(player::player_system* players,
                          skill::skill_system* skills,
                          inventory::inventory_system* inventory,
                          item::item_system* items,
                          fishing_registry* registry,
                          scheduler* sched,
                          world::world_subsystem* world);

    // Called after maps + registry loaded — starts periodic fish generation
    void start_generation();

    // Engagement flow: find a fish within 2 tiles of player
    auto check_fish_nearby(entity_id player_eid,
                           const std::string& map_name,
                           int16_t player_x,
                           int16_t player_y) -> std::optional<uint32_t>;

    // Player attempts catch (clicks "Try Now!")
    auto attempt_catch(entity_id player_eid) -> fish_catch_result;

    // Cancel engagement (movement, damage, logout)
    void cancel_fishing(entity_id player_eid, catch_result reason);

    // Query
    [[nodiscard]] auto get_node(uint32_t index) const -> const fish_node*;
    [[nodiscard]] auto active_node_count() const -> size_t;

    // Callbacks for game_handlers
    using fish_spawn_callback = std::function<void(const fish_node&)>;
    using fish_despawn_callback = std::function<void(const fish_node&)>;
    using fish_engaged_callback = std::function<void(entity_id, const fish_type_config&, int32_t)>;
    using chance_update_callback = std::function<void(entity_id, int32_t)>;
    using catch_complete_callback = std::function<void(entity_id, const fish_catch_result&)>;

    void set_spawn_callback(fish_spawn_callback cb);
    void set_despawn_callback(fish_despawn_callback cb);
    void set_engaged_callback(fish_engaged_callback cb);
    void set_chance_update_callback(chance_update_callback cb);
    void set_catch_complete_callback(catch_complete_callback cb);

    // Static for unit testing
    [[nodiscard]] static auto calculate_chance_change(int16_t skill, int16_t difficulty)
        -> std::pair<int32_t, int32_t>; // {effective_skill, max_change}

private:
    void generate_fish();          // Periodic scheduler callback — spawn new fish
    void update_fishing_chances(); // Every 4s — update all engaged players' catch %
    void cleanup_expired_fish();   // Remove timed-out fish nodes

    auto find_free_slot() -> uint32_t; // Find empty slot in fish_nodes_ (0 = none)
    void delete_fish_node(uint32_t index, catch_result reason);

    player::player_system* players_{nullptr};
    skill::skill_system* skills_{nullptr};
    inventory::inventory_system* inventory_{nullptr};
    item::item_system* items_{nullptr};
    fishing_registry* registry_{nullptr};
    scheduler* scheduler_{nullptr};
    world::world_subsystem* world_{nullptr};

    // Fish nodes array (index 0 reserved as "no fish")
    std::array<std::optional<fish_node>, max_fish_nodes> fish_nodes_;

    fish_spawn_callback spawn_callback_;
    fish_despawn_callback despawn_callback_;
    fish_engaged_callback engaged_callback_;
    chance_update_callback chance_update_callback_;
    catch_complete_callback catch_complete_callback_;
};

} // namespace hb::crafting
