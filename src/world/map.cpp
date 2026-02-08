// map.cpp
// Game map implementation

#include "world/map.h"
#include "core/logger.h"

#include <yaml-cpp/yaml.h>

#include <fstream>
#include <algorithm>
#include <random>

namespace hb::world {

void map::initialize(map_id id, const map_config& config) {
    id_ = id;
    config_ = config;

    // Allocate tile arrays
    size_t total = static_cast<size_t>(config_.width) * config_.height;
    static_tiles_.resize(total);
    dynamic_tiles_.resize(total);

    // Initialize spatial index
    spatial_index_.initialize(config_.width, config_.height);

    LOG_DEBUG(general, "Map {} initialized: {}x{} ({} tiles)",
        config_.name, config_.width, config_.height, total);
}

auto map::load_from_file(const std::filesystem::path& path) -> result<void, std::string> {
    if (!std::filesystem::exists(path)) {
        return result<void, std::string>::err("Map file not found: " + path.string());
    }

    std::ifstream file(path, std::ios::binary);
    if (!file) {
        return result<void, std::string>::err("Failed to open map file: " + path.string());
    }

    // Read 256-byte text header
    char header[256];
    file.read(header, sizeof(header));
    if (!file) {
        return result<void, std::string>::err("Failed to read map header");
    }

    // Replace null bytes with spaces for parsing
    for (auto& c : header) {
        if (c == '\0') c = ' ';
    }

    // Parse header for MAPSIZEX, MAPSIZEY, TILESIZE
    std::string header_str(header, sizeof(header));
    int16_t size_x = 0, size_y = 0, tile_size = 0;

    auto parse_value = [&](const std::string& key) -> int16_t {
        auto pos = header_str.find(key);
        if (pos == std::string::npos) return 0;
        pos += key.length();
        while (pos < header_str.length() && (header_str[pos] == ' ' || header_str[pos] == '=' || header_str[pos] == '\t')) {
            ++pos;
        }
        if (pos >= header_str.length()) return 0;
        return static_cast<int16_t>(std::atoi(&header_str[pos]));
    };

    size_x = parse_value("MAPSIZEX");
    size_y = parse_value("MAPSIZEY");
    tile_size = parse_value("TILESIZE");

    if (size_x <= 0 || size_y <= 0 || tile_size <= 0) {
        return result<void, std::string>::err("Invalid map header values");
    }

    // Update config with parsed dimensions
    config_.width = size_x;
    config_.height = size_y;

    // Reallocate tile arrays with correct dimensions
    size_t total = static_cast<size_t>(size_x) * size_y;
    static_tiles_.resize(total);
    dynamic_tiles_.resize(total);

    // Reinitialize spatial index with correct dimensions
    spatial_index_.initialize(size_x, size_y);

    // Read tile data
    std::vector<char> tile_data(tile_size);
    for (int16_t y = 0; y < size_y; ++y) {
        for (int16_t x = 0; x < size_x; ++x) {
            file.read(tile_data.data(), tile_size);
            if (!file) {
                LOG_WARN(general, "Incomplete tile data at ({},{}) in map {}", x, y, config_.name);
                break;
            }

            size_t idx = tile_index(x, y);
            auto& tile = static_tiles_[idx];

            // Parse tile flags from byte 8
            uint8_t flags_byte = static_cast<uint8_t>(tile_data[8]);

            // Bit 7 (0x80) = not walkable
            if (flags_byte & 0x80) {
                tile.flags = tile.flags | tile_flags::blocks_movement;
            }

            // Bit 6 (0x40) = teleport
            if (flags_byte & 0x40) {
                tile.flags = tile.flags | tile_flags::is_teleport;
            }

            // Bit 5 (0x20) = farm
            if (flags_byte & 0x20) {
                tile.flags = tile.flags | tile_flags::is_farm;
            }

            // First 2 bytes = tile type (short), type 19 = water
            int16_t tile_type = *reinterpret_cast<int16_t*>(&tile_data[0]);
            if (tile_type == 19) {
                tile.flags = tile.flags | tile_flags::is_water;
            }

            // Store the terrain type
            tile.terrain_type = static_cast<uint16_t>(tile_type);
        }
    }

    LOG_DEBUG(general, "Loaded map {}: {}x{}, tile_size={}, {} tiles",
        config_.name, size_x, size_y, tile_size, total);

    return result<void, std::string>::ok();
}

auto map::get_static_tile(const position& pos) const -> const static_tile* {
    if (!is_valid_position(pos)) return nullptr;
    return &static_tiles_[tile_index(pos)];
}

auto map::get_static_tile(int16_t x, int16_t y) const -> const static_tile* {
    if (!is_valid_position(x, y)) return nullptr;
    return &static_tiles_[tile_index(x, y)];
}

auto map::get_dynamic_tile(const position& pos) -> dynamic_tile* {
    if (!is_valid_position(pos)) return nullptr;
    return &dynamic_tiles_[tile_index(pos)];
}

auto map::get_dynamic_tile(int16_t x, int16_t y) -> dynamic_tile* {
    if (!is_valid_position(x, y)) return nullptr;
    return &dynamic_tiles_[tile_index(x, y)];
}

auto map::get_dynamic_tile(const position& pos) const -> const dynamic_tile* {
    if (!is_valid_position(pos)) return nullptr;
    return &dynamic_tiles_[tile_index(pos)];
}

auto map::get_dynamic_tile(int16_t x, int16_t y) const -> const dynamic_tile* {
    if (!is_valid_position(x, y)) return nullptr;
    return &dynamic_tiles_[tile_index(x, y)];
}

auto map::is_walkable(const position& pos) const -> bool {
    auto* tile = get_static_tile(pos);
    return tile && tile->is_walkable();
}

auto map::is_walkable(int16_t x, int16_t y) const -> bool {
    auto* tile = get_static_tile(x, y);
    return tile && tile->is_walkable();
}

auto map::can_move_to(const position& pos) const -> bool {
    if (!is_walkable(pos)) return false;

    auto* dyn = get_dynamic_tile(pos);
    if (!dyn) return false;

    // Check if not occupied and not temp blocked
    return !dyn->has_occupant() && !dyn->is_temp_blocked();
}

auto map::is_teleport(const position& pos) const -> bool {
    auto* tile = get_static_tile(pos);
    return tile && tile->is_teleport();
}

auto map::is_water(const position& pos) const -> bool {
    auto* tile = get_static_tile(pos);
    return tile && tile->is_water();
}

auto map::is_farm(const position& pos) const -> bool {
    auto* tile = get_static_tile(pos);
    return tile && tile->is_farm();
}

auto map::get_teleport_dest(const position& pos) const -> std::optional<teleport_dest> {
    auto it = teleports_.find(pos);
    if (it != teleports_.end()) {
        return it->second;
    }
    return std::nullopt;
}

void map::add_teleport(const position& pos, const teleport_dest& dest) {
    teleports_[pos] = dest;

    // Also mark the static tile as a teleport
    if (is_valid_position(pos)) {
        auto& tile = static_tiles_[tile_index(pos)];
        tile.flags = tile.flags | tile_flags::is_teleport;
    }
}

auto map::get_all_teleports() const -> const std::unordered_map<position, teleport_dest>& {
    return teleports_;
}

auto map::remove_teleport(const position& pos) -> bool {
    auto it = teleports_.find(pos);
    if (it == teleports_.end()) return false;

    // Clear teleport flag on tile
    if (is_valid_position(pos)) {
        auto& tile = static_tiles_[tile_index(pos)];
        tile.flags = static_cast<tile_flags>(
            static_cast<uint8_t>(tile.flags) & ~static_cast<uint8_t>(tile_flags::is_teleport));
    }

    teleports_.erase(it);
    return true;
}

auto map::teleport_count() const -> size_t {
    return teleports_.size();
}

void map::set_occupant(const position& pos, entity_id id, owner_type type) {
    auto* dyn = get_dynamic_tile(pos);
    if (dyn) {
        dyn->set_occupant(id, type);
    }
}

void map::clear_occupant(const position& pos) {
    auto* dyn = get_dynamic_tile(pos);
    if (dyn) {
        dyn->clear_occupant();
    }
}

auto map::get_occupant(const position& pos) const -> std::optional<entity_id> {
    auto* dyn = get_dynamic_tile(pos);
    if (dyn && dyn->has_occupant()) {
        return dyn->occupant;
    }
    return std::nullopt;
}

auto map::get_occupant_type(const position& pos) const -> owner_type {
    auto* dyn = get_dynamic_tile(pos);
    if (dyn) {
        return dyn->occupant_type;
    }
    return owner_type::none;
}

void map::set_dead_entity(const position& pos, entity_id id, owner_type type) {
    auto* dyn = get_dynamic_tile(pos);
    if (dyn) {
        dyn->set_dead_entity(id, type);
    }
}

void map::clear_dead_entity(const position& pos) {
    auto* dyn = get_dynamic_tile(pos);
    if (dyn) {
        dyn->clear_dead_entity();
    }
}

auto map::get_dead_entity(const position& pos) const -> std::optional<entity_id> {
    auto* dyn = get_dynamic_tile(pos);
    if (dyn && dyn->has_dead_entity()) {
        return dyn->dead_entity;
    }
    return std::nullopt;
}

auto map::get_entities_in_range(const position& center, int radius) const -> std::vector<entity_id> {
    return spatial_index_.get_in_range(center, radius);
}

auto map::get_entities_in_rect(const rect& area) const -> std::vector<entity_id> {
    return spatial_index_.get_in_rect(area);
}

auto map::load_config_file(const std::filesystem::path& path) -> result<void, std::string> {
    auto yaml_path = path;
    yaml_path.replace_extension(".yaml");

    if (std::filesystem::exists(yaml_path)) {
        return load_config_yaml(yaml_path);
    }

    // No config file found - that's ok, config is optional
    LOG_DEBUG(general, "No config file for map {}: {}", config_.name, yaml_path.string());
    return result<void, std::string>::ok();
}

auto map::load_config_yaml(const std::filesystem::path& path) -> result<void, std::string> {
    LOG_DEBUG(general, "Loading map config (YAML): {}", path.string());

    try {
        YAML::Node root = YAML::LoadFile(path.string());

        // Auto-detect map properties from name (like legacy code)
        if (config_.name.find("fightzone") == 0) {
            config_.is_fight_zone = true;
        }
        if (config_.name == "icebound") {
            config_.is_snow_enabled = true;
        }

        // Basic properties
        if (root["name"]) {
            config_.location_name = root["name"].as<std::string>();
        }
        if (root["upper_level_limit"]) {
            config_.upper_level_limit = root["upper_level_limit"].as<int>();
        }
        if (root["level_limit"]) {
            config_.level_limit = root["level_limit"].as<int>();
        }
        if (root["fixed_day_mode"]) {
            config_.is_fixed_day_mode = root["fixed_day_mode"].as<bool>();
        }
        if (root["attack_enabled"]) {
            config_.is_attack_enabled = root["attack_enabled"].as<bool>();
        }

        // Random mob generator
        if (root["random_mob_generator"]) {
            const auto& rmg = root["random_mob_generator"];
            config_.random_mob_generator.enabled = rmg["enabled"].as<bool>(false);
            config_.random_mob_generator.level = rmg["level"].as<int>(0);
            LOG_DEBUG(general, "Map {} random_mob_generator: enabled={}, level={}",
                config_.name, config_.random_mob_generator.enabled, config_.random_mob_generator.level);
        }

        // Initial points
        if (root["initial_points"]) {
            for (const auto& node : root["initial_points"]) {
                initial_point ip{
                    .id = static_cast<int16_t>(node["id"].as<int>()),
                    .x = static_cast<int16_t>(node["x"].as<int>()),
                    .y = static_cast<int16_t>(node["y"].as<int>())
                };
                initial_points_.push_back(ip);
            }
        }

        // Safe zones
        if (root["safe_zones"]) {
            for (const auto& node : root["safe_zones"]) {
                safe_zone sz{
                    .area = rect{
                        static_cast<int16_t>(node["left"].as<int>()),
                        static_cast<int16_t>(node["top"].as<int>()),
                        static_cast<int16_t>(node["right"].as<int>()),
                        static_cast<int16_t>(node["bottom"].as<int>())
                    }
                };
                safe_zones_.push_back(sz);

                // Mark tiles with safe zone flag
                for (int16_t y = sz.area.min_y; y <= sz.area.max_y; ++y) {
                    for (int16_t x = sz.area.min_x; x <= sz.area.max_x; ++x) {
                        if (is_valid_position(x, y)) {
                            auto& tile = static_tiles_[tile_index(x, y)];
                            tile.flags = tile.flags | tile_flags::is_safe_zone;
                        }
                    }
                }
            }
        }

        // Mob spawners
        if (root["spawners"]) {
            for (const auto& node : root["spawners"]) {
                spot_mob_generator smg{
                    .id = static_cast<int16_t>(node["id"].as<int>()),
                    .type = static_cast<int16_t>(node["type"].as<int>()),
                    .area = rect{
                        static_cast<int16_t>(node["x1"].as<int>()),
                        static_cast<int16_t>(node["y1"].as<int>()),
                        static_cast<int16_t>(node["x2"].as<int>()),
                        static_cast<int16_t>(node["y2"].as<int>())
                    },
                    .npc_type = static_cast<int16_t>(node["npc_type"].as<int>()),
                    .max_count = static_cast<int16_t>(node["max_count"].as<int>()),
                    .enabled = true
                };
                mob_spawners_.push_back(smg);
            }
        }

        // Waypoints
        if (root["waypoints"]) {
            for (const auto& node : root["waypoints"]) {
                waypoint wp{
                    .id = static_cast<int16_t>(node["id"].as<int>()),
                    .x = static_cast<int16_t>(node["x"].as<int>()),
                    .y = static_cast<int16_t>(node["y"].as<int>())
                };
                waypoints_[wp.id] = wp;
            }
        }

        // Mineral generator
        if (root["mineral_generator"]) {
            const auto& mg = root["mineral_generator"];
            config_.mineral_generator.enabled = mg["enabled"].as<bool>(false);
            config_.mineral_generator.level = mg["level"].as<int>(0);
        }
        if (root["max_mineral"]) {
            config_.max_mineral = static_cast<int16_t>(root["max_mineral"].as<int>(0));
        }

        // Mineral points
        if (root["mineral_points"]) {
            for (const auto& node : root["mineral_points"]) {
                mineral_point mp{
                    .id = static_cast<int16_t>(node["id"].as<int>()),
                    .x = static_cast<int16_t>(node["x"].as<int>()),
                    .y = static_cast<int16_t>(node["y"].as<int>())
                };
                mineral_points_.push_back(mp);
            }
        }

        // Teleports
        if (root["teleports"]) {
            for (const auto& node : root["teleports"]) {
                position src{
                    static_cast<int16_t>(node["src_x"].as<int>()),
                    static_cast<int16_t>(node["src_y"].as<int>())
                };
                teleport_dest dest{
                    .dest_map = node["dest_map"].as<std::string>(),
                    .dest_x = static_cast<int16_t>(node["dest_x"].as<int>()),
                    .dest_y = static_cast<int16_t>(node["dest_y"].as<int>()),
                    .dest_dir = static_cast<direction>(node["direction"].as<int>())
                };
                add_teleport(src, dest);
            }
        }

        LOG_INFO(general, "Map {} config loaded (YAML): {} initial points, {} safe zones, {} spawners, {} teleports, {} mineral points",
            config_.name, initial_points_.size(), safe_zones_.size(), mob_spawners_.size(), teleports_.size(), mineral_points_.size());

        return result<void, std::string>::ok();

    } catch (const YAML::Exception& e) {
        return result<void, std::string>::err("YAML parse error: " + std::string(e.what()));
    } catch (const std::exception& e) {
        return result<void, std::string>::err("Error loading YAML config: " + std::string(e.what()));
    }
}

auto map::get_initial_point(int16_t id) const -> std::optional<position> {
    for (const auto& ip : initial_points_) {
        if (ip.id == id) {
            return position{ip.x, ip.y};
        }
    }
    return std::nullopt;
}

auto map::get_random_initial_point() const -> std::optional<position> {
    if (initial_points_.empty()) {
        return std::nullopt;
    }

    static std::random_device rd;
    static std::mt19937 gen(rd());
    std::uniform_int_distribution<size_t> dist(0, initial_points_.size() - 1);

    const auto& ip = initial_points_[dist(gen)];
    return position{ip.x, ip.y};
}

auto map::is_safe_zone(const position& pos) const -> bool {
    auto* tile = get_static_tile(pos);
    return tile && tile->is_safe_zone();
}

auto map::get_waypoint(int16_t id) const -> std::optional<position> {
    auto it = waypoints_.find(id);
    if (it != waypoints_.end()) {
        return position{it->second.x, it->second.y};
    }
    return std::nullopt;
}

}  // namespace hb::world
