#pragma once

// map.h
// Game map containing tiles and spatial index

#include "core/types.h"
#include "core/result.h"
#include "world/position.h"
#include "world/tile.h"
#include "world/spatial_index.h"

#include <string>
#include <vector>
#include <filesystem>
#include <optional>

namespace hb::world {

// Random mob generator configuration
struct random_mob_generator_config {
    bool enabled{false};
    int level{0};  // 1-7+ determines which NPC groups can spawn
};

// Map configuration
struct map_config {
    std::string name;
    std::string location_name;
    int16_t width{0};
    int16_t height{0};

    // Map properties
    bool is_fixed_day_mode{false};      // Always daylight
    bool is_attack_enabled{true};       // PvP allowed
    bool is_fight_zone{false};          // Arena map
    bool is_snow_enabled{false};        // Snow instead of rain
    bool is_recall_impossible{false};   // Cannot use recall
    bool is_apocalypse_map{false};      // Special apocalypse map
    bool is_heldenian_map{false};       // Heldenian event map

    int level_limit{0};                 // Minimum level to enter
    int upper_level_limit{0};           // Maximum level to enter (0 = no limit)

    uint8_t map_type{0};                // 0 = normal, 1 = no penalty/reward

    // Random mob spawning outside of defined spawners
    random_mob_generator_config random_mob_generator;
};

// Teleport destination info
struct teleport_dest {
    std::string dest_map;
    int16_t dest_x{0};
    int16_t dest_y{0};
    direction dest_dir{direction::south};  // Default facing south
};

// Initial spawn point for new characters
struct initial_point {
    int16_t id{0};
    int16_t x{0};
    int16_t y{0};
};

// No-attack area (safe zone)
struct safe_zone {
    rect area;
};

// Spot mob generator - defines NPC spawn areas
struct spot_mob_generator {
    int16_t id{0};
    int16_t type{0};              // 1 = rect area, 2 = waypoint path
    rect area;                     // Spawn bounding box
    int16_t npc_type{0};          // NPC template ID to spawn
    int16_t max_count{0};         // Maximum NPCs to spawn
    bool enabled{true};
};

// Waypoint for NPC pathing
struct waypoint {
    int16_t id{0};
    int16_t x{0};
    int16_t y{0};
};

// Weather status
enum class weather_type : uint8_t {
    clear = 0,
    light_rain = 1,
    rain = 2,
    heavy_rain = 3,
    light_snow = 4,
    snow = 5,
    heavy_snow = 6,
    windy = 7,
    stormy = 8,
};

// Game map class
class map {
public:
    map() = default;
    ~map() = default;

    // Non-copyable, movable
    map(const map&) = delete;
    auto operator=(const map&) -> map& = delete;
    map(map&&) noexcept = default;
    auto operator=(map&&) noexcept -> map& = default;

    // Initialize empty map
    void initialize(map_id id, const map_config& config);

    // Load from file
    auto load_from_file(const std::filesystem::path& path) -> result<void, std::string>;

    // Load YAML configuration file with teleports, spawn points, etc.
    auto load_config_file(const std::filesystem::path& path) -> result<void, std::string>;

    // Load YAML format config
    auto load_config_yaml(const std::filesystem::path& path) -> result<void, std::string>;

    // Identity
    [[nodiscard]] auto id() const -> map_id { return id_; }
    [[nodiscard]] auto name() const -> std::string_view { return config_.name; }
    [[nodiscard]] auto location_name() const -> std::string_view { return config_.location_name; }
    [[nodiscard]] auto config() const -> const map_config& { return config_; }

    // Dimensions
    [[nodiscard]] auto width() const -> int16_t { return config_.width; }
    [[nodiscard]] auto height() const -> int16_t { return config_.height; }

    // Position validation
    [[nodiscard]] auto is_valid_position(const position& pos) const -> bool {
        return pos.x >= 0 && pos.x < config_.width &&
               pos.y >= 0 && pos.y < config_.height;
    }

    [[nodiscard]] auto is_valid_position(int16_t x, int16_t y) const -> bool {
        return x >= 0 && x < config_.width && y >= 0 && y < config_.height;
    }

    // Tile access (static)
    [[nodiscard]] auto get_static_tile(const position& pos) const -> const static_tile*;
    [[nodiscard]] auto get_static_tile(int16_t x, int16_t y) const -> const static_tile*;

    // Tile access (dynamic)
    [[nodiscard]] auto get_dynamic_tile(const position& pos) -> dynamic_tile*;
    [[nodiscard]] auto get_dynamic_tile(int16_t x, int16_t y) -> dynamic_tile*;
    [[nodiscard]] auto get_dynamic_tile(const position& pos) const -> const dynamic_tile*;
    [[nodiscard]] auto get_dynamic_tile(int16_t x, int16_t y) const -> const dynamic_tile*;

    // Movement queries
    [[nodiscard]] auto is_walkable(const position& pos) const -> bool;
    [[nodiscard]] auto is_walkable(int16_t x, int16_t y) const -> bool;
    [[nodiscard]] auto can_move_to(const position& pos) const -> bool;  // Walkable and not occupied

    // Tile property queries
    [[nodiscard]] auto is_teleport(const position& pos) const -> bool;
    [[nodiscard]] auto is_water(const position& pos) const -> bool;
    [[nodiscard]] auto is_farm(const position& pos) const -> bool;

    // Teleport lookup and management
    [[nodiscard]] auto get_teleport_dest(const position& pos) const -> std::optional<teleport_dest>;
    void add_teleport(const position& pos, const teleport_dest& dest);
    [[nodiscard]] auto get_all_teleports() const
        -> const std::unordered_map<position, teleport_dest>&;
    auto remove_teleport(const position& pos) -> bool;
    [[nodiscard]] auto teleport_count() const -> size_t;

    // Initial spawn points
    [[nodiscard]] auto get_initial_point(int16_t id) const -> std::optional<position>;
    [[nodiscard]] auto get_random_initial_point() const -> std::optional<position>;
    [[nodiscard]] auto initial_point_count() const -> size_t { return initial_points_.size(); }

    // Safe zones (no-attack areas)
    [[nodiscard]] auto is_safe_zone(const position& pos) const -> bool;
    [[nodiscard]] auto safe_zone_count() const -> size_t { return safe_zones_.size(); }

    // Mob spawners
    [[nodiscard]] auto get_mob_spawners() const -> const std::vector<spot_mob_generator>& { return mob_spawners_; }
    [[nodiscard]] auto mob_spawner_count() const -> size_t { return mob_spawners_.size(); }

    // Waypoints
    [[nodiscard]] auto get_waypoint(int16_t id) const -> std::optional<position>;

    // Random mob generator
    [[nodiscard]] auto random_mob_generator_enabled() const -> bool {
        return config_.random_mob_generator.enabled;
    }
    [[nodiscard]] auto random_mob_generator_level() const -> int {
        return config_.random_mob_generator.level;
    }

    // Occupancy management
    void set_occupant(const position& pos, entity_id id, owner_type type);
    void clear_occupant(const position& pos);
    [[nodiscard]] auto get_occupant(const position& pos) const -> std::optional<entity_id>;
    [[nodiscard]] auto get_occupant_type(const position& pos) const -> owner_type;

    // Dead entity (corpse) management
    void set_dead_entity(const position& pos, entity_id id, owner_type type);
    void clear_dead_entity(const position& pos);
    [[nodiscard]] auto get_dead_entity(const position& pos) const -> std::optional<entity_id>;

    // Spatial index access
    [[nodiscard]] auto spatial() -> spatial_index& { return spatial_index_; }
    [[nodiscard]] auto spatial() const -> const spatial_index& { return spatial_index_; }

    // Entity queries via spatial index
    [[nodiscard]] auto get_entities_in_range(const position& center, int radius) const -> std::vector<entity_id>;
    [[nodiscard]] auto get_entities_in_rect(const rect& area) const -> std::vector<entity_id>;

    // Weather
    [[nodiscard]] auto weather() const -> weather_type { return weather_; }
    void set_weather(weather_type weather) { weather_ = weather; }

    // Statistics
    [[nodiscard]] auto total_tiles() const -> size_t {
        return static_cast<size_t>(config_.width) * config_.height;
    }

private:
    [[nodiscard]] auto tile_index(int16_t x, int16_t y) const -> size_t {
        return static_cast<size_t>(y) * config_.width + x;
    }

    [[nodiscard]] auto tile_index(const position& pos) const -> size_t {
        return tile_index(pos.x, pos.y);
    }

    map_id id_{0};
    map_config config_;

    // Tile data (separated for cache efficiency)
    std::vector<static_tile> static_tiles_;
    std::vector<dynamic_tile> dynamic_tiles_;

    // Spatial index for entity queries
    spatial_index spatial_index_;

    // Teleport destinations
    std::unordered_map<position, teleport_dest> teleports_;

    // Initial spawn points
    std::vector<initial_point> initial_points_;

    // Safe zones (no-attack areas)
    std::vector<safe_zone> safe_zones_;

    // Mob spawners
    std::vector<spot_mob_generator> mob_spawners_;

    // Waypoints for NPC pathing
    std::unordered_map<int16_t, waypoint> waypoints_;

    // Current weather
    weather_type weather_{weather_type::clear};
};

}  // namespace hb::world
