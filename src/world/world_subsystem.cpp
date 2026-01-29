// world_subsystem.cpp
// World management subsystem implementation

#include "world/world_subsystem.h"
#include "core/logger.h"

namespace hb::world {

world_subsystem::world_subsystem() = default;

world_subsystem::~world_subsystem() {
    if (is_initialized()) {
        shutdown();
    }
}

void world_subsystem::initialize() {
    LOG_INFO(general, "World subsystem initializing...");
    set_initialized(true);
    LOG_INFO(general, "World subsystem initialized");
}

void world_subsystem::shutdown() {
    LOG_INFO(general, "World subsystem shutting down...");

    maps_.clear();
    name_to_id_.clear();

    set_initialized(false);
    LOG_INFO(general, "World subsystem shutdown complete");
}

void world_subsystem::update(float /*delta_time*/) {
    // Update weather, dynamic objects, etc.
    // For now, no per-frame updates needed
}

void world_subsystem::set_config(const world_config& config) {
    config_ = config;
}

auto world_subsystem::create_map(const map_config& config) -> result<map_id, std::string> {
    // Check map limit
    if (maps_.size() >= static_cast<size_t>(config_.max_maps)) {
        return result<map_id, std::string>::err("Maximum map count reached");
    }

    // Check for duplicate name
    if (name_to_id_.contains(config.name)) {
        return result<map_id, std::string>::err("Map with name '" + config.name + "' already exists");
    }

    auto id = next_map_id();
    auto new_map = std::make_unique<map>();
    new_map->initialize(id, config);

    name_to_id_[config.name] = id;
    maps_[id] = std::move(new_map);

    LOG_INFO(general, "Created map '{}' (id={})", config.name, id.value);

    return result<map_id, std::string>::ok(id);
}

auto world_subsystem::load_map(const std::filesystem::path& path) -> result<map_id, std::string> {
    // Check map limit
    if (maps_.size() >= static_cast<size_t>(config_.max_maps)) {
        return result<map_id, std::string>::err("Maximum map count reached");
    }

    auto id = next_map_id();
    auto new_map = std::make_unique<map>();

    // TODO: Parse map file header to get config
    // For now, use filename as map name
    map_config config;
    config.name = path.stem().string();
    config.width = 700;   // Default Helbreath map size
    config.height = 550;

    new_map->initialize(id, config);

    auto load_result = new_map->load_from_file(path);
    if (load_result.is_err()) {
        return result<map_id, std::string>::err(load_result.error());
    }

    name_to_id_[config.name] = id;
    maps_[id] = std::move(new_map);

    LOG_INFO(general, "Loaded map '{}' from {} (id={})", config.name, path.string(), id.value);

    return result<map_id, std::string>::ok(id);
}

void world_subsystem::unload_map(map_id id) {
    auto it = maps_.find(id);
    if (it == maps_.end()) {
        return;
    }

    auto name = std::string(it->second->name());
    name_to_id_.erase(name);
    maps_.erase(it);

    LOG_INFO(general, "Unloaded map '{}' (id={})", name, id.value);
}

auto world_subsystem::get_map(map_id id) -> map* {
    auto it = maps_.find(id);
    return it != maps_.end() ? it->second.get() : nullptr;
}

auto world_subsystem::get_map(map_id id) const -> const map* {
    auto it = maps_.find(id);
    return it != maps_.end() ? it->second.get() : nullptr;
}

auto world_subsystem::get_map_by_name(std::string_view name) -> map* {
    auto it = name_to_id_.find(std::string(name));
    if (it == name_to_id_.end()) {
        return nullptr;
    }
    return get_map(it->second);
}

auto world_subsystem::get_map_by_name(std::string_view name) const -> const map* {
    auto it = name_to_id_.find(std::string(name));
    if (it == name_to_id_.end()) {
        return nullptr;
    }
    return get_map(it->second);
}

auto world_subsystem::is_walkable(map_id map_id, const position& pos) const -> bool {
    auto* m = get_map(map_id);
    return m && m->is_walkable(pos);
}

auto world_subsystem::can_move_to(map_id map_id, const position& pos) const -> bool {
    auto* m = get_map(map_id);
    return m && m->can_move_to(pos);
}

auto world_subsystem::get_entities_in_range(map_id map_id, const position& center, int radius) const
    -> std::vector<entity_id>
{
    auto* m = get_map(map_id);
    if (!m) {
        return {};
    }
    return m->get_entities_in_range(center, radius);
}

}  // namespace hb::world
