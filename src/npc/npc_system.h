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
#include <functional>

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
    auto spawn_random_mob(map_id map, hb::world::position pos) -> result<entity::entity, std::string>;
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

    // Callback types
    using on_npc_spawn_callback = std::function<void(const npc&)>;
    using on_npc_move_callback = std::function<void(const npc&)>;
    using on_npc_death_callback = std::function<void(const npc&, entity::entity killer)>;
    using on_npc_attack_callback = std::function<void(const npc&, entity::entity target, int32_t damage)>;

    // Register callbacks
    void set_on_spawn_callback(on_npc_spawn_callback cb) { on_spawn_callback_ = std::move(cb); }
    void set_on_move_callback(on_npc_move_callback cb) { on_move_callback_ = std::move(cb); }
    void set_on_death_callback(on_npc_death_callback cb) { on_death_callback_ = std::move(cb); }
    void set_on_attack_callback(on_npc_attack_callback cb) { on_attack_callback_ = std::move(cb); }

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

    // AI state handlers
    void process_idle_state(npc& npc_ref);
    void process_wander_state(npc& npc_ref);
    void process_chase_state(npc& npc_ref);
    void process_attack_state(npc& npc_ref);
    void process_flee_state(npc& npc_ref);
    void process_return_home_state(npc& npc_ref);

    // AI helpers
    [[nodiscard]] auto find_aggro_target(const npc& npc_ref) -> entity::entity;
    [[nodiscard]] auto get_entity_position(entity::entity e) const -> std::optional<hb::world::position>;
    [[nodiscard]] auto get_entity_map(entity::entity e) const -> std::optional<map_id>;
    void move_towards(npc& npc_ref, hb::world::position target_pos);
    void try_move_npc(npc& npc_ref, hb::world::position new_pos);
    void perform_npc_attack(npc& npc_ref, entity::entity target);

    [[nodiscard]] auto next_entity_id() -> entity::entity {
        return entity::entity{next_id_++, 0};
    }

    npc_system_config config_;
    uint32_t next_id_{1};

    std::unordered_map<entity::entity, std::unique_ptr<npc>> npcs_;
    std::vector<spawn_point> spawn_points_;

    float ai_accumulator_{0.0f};
    float spawn_accumulator_{0.0f};

    // Event callbacks
    on_npc_spawn_callback on_spawn_callback_;
    on_npc_move_callback on_move_callback_;
    on_npc_death_callback on_death_callback_;
    on_npc_attack_callback on_attack_callback_;
};

}  // namespace hb::npc
