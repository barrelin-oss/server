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
    // TODO: Implement .amd file parsing
    // For now, just return success if the file exists
    if (!std::filesystem::exists(path)) {
        return result<void, std::string>::err("Map file not found: " + path.string());
    }

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
