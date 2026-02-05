// world_subsystem.cpp
// World management subsystem implementation

#include "world/world_subsystem.h"
#include "core/logger.h"
#include "entity/entity_manager.h"
#include "entity/components/transform.h"

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

    // Load the binary map data (.amd file)
    auto load_result = new_map->load_from_file(path);
    if (load_result.is_err()) {
        return result<map_id, std::string>::err(load_result.error());
    }

    // Load the YAML configuration file (same name, .yaml extension)
    // Config files contain teleports, spawn points, safe zones, etc.
    auto config_path = path;
    config_path.replace_extension(".yaml");

    if (std::filesystem::exists(config_path)) {
        auto config_result = new_map->load_config_file(config_path);
        if (config_result.is_err()) {
            LOG_WARN(general, "Failed to load config for map '{}': {}",
                     config.name, config_result.error());
            // Continue anyway - map data is loaded, just missing config
        } else {
            LOG_DEBUG(general, "Loaded config for map '{}' ({} initial points, {} safe zones, {} spawners)",
                      config.name,
                      new_map->initial_point_count(),
                      new_map->safe_zone_count(),
                      new_map->mob_spawner_count());
        }
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

auto world_subsystem::get_all_entities_in_range(map_id mid, const position& center, int radius) const
    -> std::vector<entity_query_result>
{
    std::vector<entity_query_result> result;

    auto* m = get_map(mid);
    if (!m) {
        return result;
    }

    auto* em = subsystems().get<entity::entity_manager>();
    if (!em) {
        return result;
    }

    // Get all entity IDs in range from spatial index
    auto entity_ids = m->get_entities_in_range(center, radius);

    for (auto eid : entity_ids) {
        // Construct entity with index and generation 0 for lookup
        // (entity_manager will validate the generation)
        entity::entity e{eid.value, 0};

        auto type = em->get_type(e);
        if (type == entity::entity_type::none) {
            continue;  // Entity doesn't exist or stale reference
        }

        // Get position from transform component
        if (auto* t = em->get_component<entity::transform>(e)) {
            result.push_back({e, type, t->pos});
        }
    }

    return result;
}

// Ground item management
void world_subsystem::add_ground_item(map_id map, const position& pos, item_id item) {
    map_position_key key{map, pos};
    ground_items_[key].push_back(item);

    // Update dynamic tile item count
    auto* m = get_map(map);
    if (m) {
        auto* dyn_tile = m->get_dynamic_tile(pos);
        if (dyn_tile) {
            dyn_tile->item_count = static_cast<uint8_t>(ground_items_[key].size());
        }
    }
}

auto world_subsystem::remove_top_ground_item(map_id map, const position& pos) -> std::optional<item_id> {
    map_position_key key{map, pos};
    auto it = ground_items_.find(key);

    if (it == ground_items_.end() || it->second.empty()) {
        return std::nullopt;
    }

    // Top-most item is at the back of the vector
    item_id item = it->second.back();
    it->second.pop_back();

    // Clean up empty entry
    bool erased = false;
    if (it->second.empty()) {
        ground_items_.erase(it);
        erased = true;
    }

    // Update dynamic tile item count
    auto* m = get_map(map);
    if (m) {
        auto* dyn_tile = m->get_dynamic_tile(pos);
        if (dyn_tile) {
            dyn_tile->item_count = !erased ?
                static_cast<uint8_t>(it->second.size()) : 0;
        }
    }

    return item;
}

auto world_subsystem::get_ground_items(map_id map, const position& pos) const -> std::vector<item_id> {
    map_position_key key{map, pos};
    auto it = ground_items_.find(key);
    return it != ground_items_.end() ? it->second : std::vector<item_id>{};
}

auto world_subsystem::has_ground_items(map_id map, const position& pos) const -> bool {
    map_position_key key{map, pos};
    auto it = ground_items_.find(key);
    return it != ground_items_.end() && !it->second.empty();
}

auto world_subsystem::ground_item_count(map_id map, const position& pos) const -> size_t {
    map_position_key key{map, pos};
    auto it = ground_items_.find(key);
    return it != ground_items_.end() ? it->second.size() : 0;
}

}  // namespace hb::world
