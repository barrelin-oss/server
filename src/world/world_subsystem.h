#pragma once

// world_subsystem.h
// World management subsystem

#include "core/types.h"
#include "core/result.h"
#include "core/subsystem.h"
#include "world/map.h"
#include "world/position.h"

#include <memory>
#include <unordered_map>
#include <filesystem>
#include <string>

namespace hb::world {

// World subsystem configuration
struct world_config {
    std::filesystem::path maps_directory{"maps"};
    int max_maps{128};
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
};

}  // namespace hb::world
