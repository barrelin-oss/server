// map.cpp
// Game map implementation

#include "world/map.h"
#include "core/logger.h"

#include <fstream>

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

}  // namespace hb::world
