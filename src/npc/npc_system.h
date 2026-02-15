#pragma once

// npc_system.h
// NPC management subsystem

#include "core/types.h"
#include "core/result.h"
#include "core/subsystem.h"
#include "npc/npc.h"
#include "npc/spawn_point.h"
#include "npc/pack_system.h"
#include "npc/script/script_loader.h"
#include "npc/script/script_executor.h"
#include "npc/bt/bt_loader.h"
#include "npc/bt/bt_context.h"
#include "npc/boss/boss_controller.h"
#include "npc/boss/boss_loader.h"
#include "entity/entity_manager.h"

#include <unordered_map>
#include <memory>
#include <vector>
#include <string_view>
#include <functional>

namespace hb::npc
{

// NPC system configuration
struct npc_system_config
{
    uint32_t max_npcs{10000};
    int32_t ai_update_interval_ms{100};
    int32_t spawn_check_interval_ms{1000};
    int32_t corpse_check_interval_ms{5000};
    int32_t corpse_linger_ms{15000};
    bool enable_ai{true};
};

// NPC system - manages all NPCs
class npc_system : public subsystem
{
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
    void disengage_all_from(entity::entity target);

    // AI
    void update_ai(entity::entity id);
    void enable_ai(bool enabled) { config_.enable_ai = enabled; }

    // Script system
    void start_script(entity::entity npc_entity, std::string_view script_name);
    void stop_script(entity::entity npc_entity);
    auto reload_scripts() -> result<size_t, std::string>;
    [[nodiscard]] auto get_script_loader() -> script::script_loader& { return script_loader_; }
    [[nodiscard]] auto get_script_executor() -> script::script_executor& { return script_executor_; }

    // Behavior tree system
    auto reload_behavior_trees() -> result<size_t, std::string>;
    [[nodiscard]] auto get_bt_loader() -> bt::bt_loader& { return bt_loader_; }

    // Boss system
    auto reload_boss_configs() -> result<size_t, std::string>;
    [[nodiscard]] auto get_boss_controller() -> boss::boss_controller& { return boss_controller_; }
    [[nodiscard]] auto get_boss_loader() -> boss::boss_loader& { return boss_loader_; }
    void register_boss(entity::entity id, std::string_view boss_config_name);

    // AI helpers accessible to BT leaf nodes
    [[nodiscard]] auto find_aggro_target_for(const npc& npc_ref) -> entity::entity;
    void move_npc_towards_target(npc& npc_ref);
    void move_npc_away_from_target(npc& npc_ref);
    void move_npc_towards_position(npc& npc_ref, hb::world::position target_pos);
    void perform_attack(npc& npc_ref, entity::entity target);
    void call_for_help_from(const npc& caller, entity::entity attacker);
    void try_move(npc& npc_ref, hb::world::position new_pos);

    // Callback types
    // IMPORTANT: These callbacks are invoked SYNCHRONOUSLY while the NPC is valid.
    // The npc& reference is only valid during the callback invocation.
    // Callbacks must NOT store the reference or access it after returning.
    // Copy any needed data immediately at the start of the callback.
    using on_npc_spawn_callback = std::function<void(const npc&)>;
    using on_npc_move_callback = std::function<void(const npc&)>;
    using on_npc_death_callback = std::function<void(const npc&, entity::entity killer, int32_t killing_damage)>;
    using on_npc_attack_callback = std::function<void(const npc&, entity::entity target, int32_t damage)>;
    using on_npc_despawn_callback = std::function<void(const npc&)>;
    using on_npc_damage_callback = std::function<void(const npc&, int32_t damage, entity::entity source)>;

    // Register callbacks
    void set_on_spawn_callback(on_npc_spawn_callback cb) { on_spawn_callback_ = std::move(cb); }
    void set_on_move_callback(on_npc_move_callback cb) { on_move_callback_ = std::move(cb); }
    void set_on_death_callback(on_npc_death_callback cb) { on_death_callback_ = std::move(cb); }
    void set_on_attack_callback(on_npc_attack_callback cb) { on_attack_callback_ = std::move(cb); }
    void set_on_despawn_callback(on_npc_despawn_callback cb) { on_despawn_callback_ = std::move(cb); }
    void set_on_damage_callback(on_npc_damage_callback cb) { on_damage_callback_ = std::move(cb); }

    // Deferred death processing — deaths are queued during apply_damage() and
    // flushed explicitly so callers can send their own messages first
    void flush_pending_deaths();
    [[nodiscard]] auto has_pending_deaths() const -> bool { return !pending_npc_deaths_.empty(); }

    // Iteration
    template<typename Func> void for_each_npc(Func&& func)
    {
        for (auto& [id, npc_ptr] : npcs_)
        {
            func(id, *npc_ptr);
        }
    }

    template<typename Func> void for_each_spawn_point(Func&& func) const
    {
        for (const auto& sp : spawn_points_)
        {
            func(sp);
        }
    }

    template<typename Func> void for_each_npc_on_map(map_id map, Func&& func)
    {
        for (auto& [id, npc_ptr] : npcs_)
        {
            if (npc_ptr->current_map == map)
            {
                func(id, *npc_ptr);
            }
        }
    }

    // Find NPCs in range
    [[nodiscard]] auto
    get_npcs_in_range(map_id map, hb::world::position center, int range) const -> std::vector<entity::entity>;

private:
    void update_spawns(float delta_time);
    void update_all_ai(float delta_time);
    void update_corpses(float delta_time);
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

    // Pack behavior helpers
    void call_for_help(const npc& caller, entity::entity attacker);
    void try_form_pack(npc& npc_ref);

    npc_system_config config_;
    entity::entity_manager* entity_manager_{nullptr}; // Cached pointer to entity manager

    std::unordered_map<entity::entity, std::unique_ptr<npc>> npcs_;
    std::vector<spawn_point> spawn_points_;

    // Pack behavior
    pack_system packs_;

    // Script system
    script::script_loader script_loader_;
    script::script_executor script_executor_;

    // Behavior tree system
    bt::bt_loader bt_loader_;
    bt::bt_context bt_context_;

    // Boss system
    boss::boss_controller boss_controller_;
    boss::boss_loader boss_loader_;

    float ai_accumulator_{0.0f};
    float spawn_accumulator_{0.0f};
    float corpse_accumulator_{0.0f};

    // Event callbacks
    on_npc_spawn_callback on_spawn_callback_;
    on_npc_move_callback on_move_callback_;
    on_npc_death_callback on_death_callback_;
    on_npc_attack_callback on_attack_callback_;
    on_npc_despawn_callback on_despawn_callback_;
    on_npc_damage_callback on_damage_callback_;

    // Deferred NPC death queue — stores snapshots so callbacks fire after caller finishes
    struct pending_npc_death
    {
        npc snapshot;
        entity::entity killer;
        int32_t damage{0};
    };
    std::vector<pending_npc_death> pending_npc_deaths_;
};

} // namespace hb::npc
