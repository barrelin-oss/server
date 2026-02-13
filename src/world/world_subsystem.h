#pragma once

// world_subsystem.h
// World management subsystem

#include "core/types.h"
#include "core/result.h"
#include "core/subsystem.h"
#include "world/map.h"
#include "world/position.h"
#include "entity/entity.h"

#include <chrono>
#include <memory>
#include <tuple>
#include <unordered_map>
#include <filesystem>
#include <string>

namespace hb::world {

// World subsystem configuration
struct world_config {
    std::filesystem::path maps_directory{"maps"};
    int max_maps{128};
};

// Result of unified entity query
struct entity_query_result {
    entity::entity entity;
    entity::entity_type type{entity::entity_type::none};
    position pos{};
};

// World subsystem - manages all game maps
class world_subsystem : public subsystem {
public:
    world_subsystem();
    ~world_subsystem() override;

    // Subsystem interface
    [[nodiscard]] auto name() const -> std::string_view override { return "world"; }
    void initialize() override;
    void shutdown() override;
    void update(float delta_time) override;

    // Configuration
    void set_config(const world_config& config);
    [[nodiscard]] auto config() const -> const world_config& { return config_; }

    // Map management
    auto create_map(const map_config& config) -> result<map_id, std::string>;
    auto load_map(const std::filesystem::path& path) -> result<map_id, std::string>;
    void unload_map(map_id id);

    // Map access
    [[nodiscard]] auto get_map(map_id id) -> map*;
    [[nodiscard]] auto get_map(map_id id) const -> const map*;
    [[nodiscard]] auto get_map_by_name(std::string_view name) -> map*;
    [[nodiscard]] auto get_map_by_name(std::string_view name) const -> const map*;

    // Map queries
    [[nodiscard]] auto map_count() const -> size_t { return maps_.size(); }
    [[nodiscard]] auto map_exists(map_id id) const -> bool { return maps_.contains(id); }

    // Position queries across all maps
    [[nodiscard]] auto is_walkable(map_id map, const position& pos) const -> bool;
    [[nodiscard]] auto can_move_to(map_id map, const position& pos) const -> bool;

    // Entity queries
    [[nodiscard]] auto get_entities_in_range(map_id map, const position& center, int radius) const
        -> std::vector<entity_id>;

    // Unified entity query - returns all entities (players, NPCs) with their types and positions
    [[nodiscard]] auto get_all_entities_in_range(map_id map, const position& center, int radius) const
        -> std::vector<entity_query_result>;

    // Ground item management
    void add_ground_item(map_id map, const position& pos, item_id item);
    auto remove_top_ground_item(map_id map, const position& pos) -> std::optional<item_id>;
    [[nodiscard]] auto get_ground_items(map_id map, const position& pos) const -> std::vector<item_id>;
    [[nodiscard]] auto has_ground_items(map_id map, const position& pos) const -> bool;
    [[nodiscard]] auto ground_item_count(map_id map, const position& pos) const -> size_t;
    [[nodiscard]] auto total_ground_item_count() const -> size_t;

    // Remove ground items older than max_age, returns list of (map, pos, item_id) removed
    auto remove_expired_ground_items(std::chrono::seconds max_age)
        -> std::vector<std::tuple<map_id, position, item_id>>;

    // Enumerate all ground items on a map
    struct ground_item_info {
        position pos;
        item_id item;
        std::chrono::steady_clock::time_point drop_time;
    };

    template<typename Func>
    void for_each_ground_item_on_map(map_id map, Func&& func) const {
        for (const auto& [key, entries] : ground_items_) {
            if (key.map != map) continue;
            for (const auto& entry : entries) {
                func(key.pos, entry.item, entry.drop_time);
            }
        }
    }

    // Remove a specific ground item by map, position, and item_id
    auto remove_ground_item(map_id map, const position& pos, item_id item) -> bool;

    // Iterate over all maps
    template<typename Func>
    void for_each_map(Func&& func) {
        for (auto& [id, map_ptr] : maps_) {
            func(id, *map_ptr);
        }
    }

    template<typename Func>
    void for_each_map(Func&& func) const {
        for (const auto& [id, map_ptr] : maps_) {
            func(id, *map_ptr);
        }
    }

private:
    [[nodiscard]] auto next_map_id() -> map_id {
        return map_id{next_id_++};
    }

    world_config config_;
    uint8_t next_id_{1};

    std::unordered_map<map_id, std::unique_ptr<map>> maps_;
    std::unordered_map<std::string, map_id> name_to_id_;

    // Ground items - map of map_id -> position -> list of items (top-most is back())
    struct ground_item_entry {
        item_id item;
        std::chrono::steady_clock::time_point drop_time;
    };

    struct map_position_key {
        map_id map;
        position pos;

        auto operator==(const map_position_key& other) const -> bool {
            return map == other.map && pos == other.pos;
        }
    };

    struct map_position_hash {
        auto operator()(const map_position_key& key) const -> size_t {
            return std::hash<uint8_t>{}(key.map.value) ^
                   (std::hash<int16_t>{}(key.pos.x) << 1) ^
                   (std::hash<int16_t>{}(key.pos.y) << 2);
        }
    };

    std::unordered_map<map_position_key, std::vector<ground_item_entry>, map_position_hash> ground_items_;
};

}  // namespace hb::world
