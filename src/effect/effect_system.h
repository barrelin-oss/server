#pragma once

// effect_system.h
// Spell effect management subsystem
// Tracks active buffs, debuffs, DoTs, HoTs on all entities

#include "core/types.h"
#include "core/subsystem.h"
#include "effect/active_effect.h"
#include "effect/effect_modifiers.h"

#include <unordered_map>
#include <vector>
#include <functional>
#include <string_view>

namespace hb::effect
{

// Per-entity effect storage
struct entity_effects
{
    std::vector<active_effect> effects;
    effect_modifier_result cached_modifiers{};
    bool dirty{false};
};

// Effect system - manages active spell effects on all entities
class effect_system : public subsystem
{
public:
    // Callback types
    using effect_applied_callback = std::function<void(entity::entity target, const active_effect& effect)>;
    using effect_removed_callback = std::function<void(entity::entity target, const active_effect& effect)>;
    using effect_tick_callback = std::function<void(entity::entity target, const active_effect& effect)>;

    effect_system();
    ~effect_system() override;

    // Subsystem interface
    [[nodiscard]] auto name() const -> std::string_view override { return "effect_system"; }
    void initialize() override;
    void shutdown() override;
    void update(float delta_time) override;

    // Apply an effect. Returns effect_id if applied, invalid id if blocked (group slot occupied).
    auto apply_effect(const apply_effect_params& params) -> effect_id;

    // Remove effects
    void remove_effect(entity::entity target, effect_id id);
    void remove_effects_by_group(entity::entity target, magic_type group);
    void remove_all_effects(entity::entity target);

    // Queries
    [[nodiscard]] auto has_effect_in_group(entity::entity target, magic_type group) const -> bool;
    [[nodiscard]] auto has_effect(entity::entity target, spell_effect_type type) const -> bool;
    [[nodiscard]] auto get_effects(entity::entity target) const -> const std::vector<active_effect>*;
    [[nodiscard]] auto get_effect_modifiers(entity::entity target) const -> const effect_modifier_result*;
    [[nodiscard]] auto has_status(entity::entity target, player::player_status status) const -> bool;
    [[nodiscard]] auto effect_count(entity::entity target) const -> size_t;

    // Callbacks
    void on_effect_applied(effect_applied_callback callback);
    void on_effect_removed(effect_removed_callback callback);
    void on_effect_tick(effect_tick_callback callback);

private:
    auto next_effect_id() -> effect_id;
    auto get_current_time_ms() const -> int64_t;
    auto map_status_flag(magic_type group, spell_effect_type type) const -> player::player_status;
    void recompute_modifiers(entity_effects& ee);
    void push_modifiers_to_entity(entity::entity target, const effect_modifier_result& mods);

    uint32_t next_id_{1};
    std::unordered_map<entity::entity, entity_effects> entities_;

    std::vector<effect_applied_callback> applied_callbacks_;
    std::vector<effect_removed_callback> removed_callbacks_;
    std::vector<effect_tick_callback> tick_callbacks_;
};

} // namespace hb::effect
