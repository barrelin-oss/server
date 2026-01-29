#pragma once

// npc_system.h
// NPC management subsystem

#include "core/types.h"
#include "core/result.h"
#include "core/subsystem.h"
#include "npc/npc.h"
#include "npc/spawn_point.h"

#include <unordered_map>
#include <memory>
#include <vector>
#include <string_view>

namespace hb::npc {

// NPC system configuration
struct npc_system_config {
    uint32_t max_npcs{10000};
    int32_t ai_update_interval_ms{100};
    int32_t spawn_check_interval_ms{1000};
    bool enable_ai{true};
};

// NPC system - manages all NPCs
class npc_system : public subsystem {
public:
    npc_system();
    ~npc_system() override;

    // Subsystem interface
    [[nodiscard]] auto name() const -> std::string_view override { return "npc_system"; }
    void initialize() override;
    void shutdown() override;
    void update(float delta_time) override;

    // Configuration
    void set_config(const npc_system_config& config);

    // NPC lifecycle
    auto spawn_npc(npc_id template_id, map_id map, hb::world::position pos) -> result<entity::entity, std::string>;
    auto spawn_npc_at(spawn_point& spawn) -> result<entity::entity, std::string>;
    void despawn_npc(entity::entity id);
    void kill_npc(entity::entity id, entity::entity killer);

    // NPC access
    [[nodiscard]] auto get_npc(entity::entity id) -> npc*;
    [[nodiscard]] auto get_npc(entity::entity id) const -> const npc*;

    // NPC queries
    [[nodiscard]] auto npc_count() const -> size_t { return npcs_.size(); }
    [[nodiscard]] auto npc_exists(entity::entity id) const -> bool { return npcs_.contains(id); }

    // Spawn point management
    void add_spawn_point(spawn_point point);
    void remove_spawn_points(map_id map);
    void activate_spawns(map_id map);
    void deactivate_spawns(map_id map);

    // Combat
    void apply_damage(entity::entity id, int32_t damage, entity::entity source);
    void set_target(entity::entity id, entity::entity target);
    void clear_target(entity::entity id);

    // AI
    void update_ai(entity::entity id);
    void enable_ai(bool enabled) { config_.enable_ai = enabled; }

    // Iteration
    template<typename Func>
    void for_each_npc(Func&& func) {
        for (auto& [id, npc_ptr] : npcs_) {
            func(id, *npc_ptr);
        }
    }

    template<typename Func>
    void for_each_npc_on_map(map_id map, Func&& func) {
        for (auto& [id, npc_ptr] : npcs_) {
            if (npc_ptr->current_map == map) {
                func(id, *npc_ptr);
            }
        }
    }

    // Find NPCs in range
    [[nodiscard]] auto get_npcs_in_range(map_id map, hb::world::position center, int range) const
        -> std::vector<entity::entity>;

private:
    void update_spawns(float delta_time);
    void update_all_ai(float delta_time);
    void process_ai_state(npc& npc_ref);

    [[nodiscard]] auto next_entity_id() -> entity::entity {
        return entity::entity{next_id_++, 0};
    }

    npc_system_config config_;
    uint32_t next_id_{1};

    std::unordered_map<entity::entity, std::unique_ptr<npc>> npcs_;
    std::vector<spawn_point> spawn_points_;

    float ai_accumulator_{0.0f};
    float spawn_accumulator_{0.0f};
};

}  // namespace hb::npc
