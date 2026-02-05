#pragma once

// magic_system.h
// Magic/spell management subsystem

#include "core/types.h"
#include "core/result.h"
#include "core/subsystem.h"
#include "magic/spell.h"
#include "combat/combat_events.h"

#include <unordered_map>
#include <vector>
#include <functional>
#include <string_view>

namespace hb::magic {

// Magic system configuration
struct magic_system_config {
    float global_cooldown_modifier{1.0f};
    float global_mana_cost_modifier{1.0f};
    float global_damage_modifier{1.0f};
    bool enable_spell_interrupts{true};
};

// Spell effect result
struct spell_effect_result {
    bool success{false};
    int32_t damage_dealt{0};
    int32_t heal_applied{0};
    std::vector<hb::entity::entity> affected_targets;
};

// Magic system - handles spell casting and effects
class magic_system : public subsystem {
public:
    using spell_callback = std::function<void(hb::entity::entity caster, const spell_template& spell, const spell_effect_result& result)>;

    magic_system();
    ~magic_system() override;

    // Subsystem interface
    [[nodiscard]] auto name() const -> std::string_view override { return "magic_system"; }
    void initialize() override;
    void shutdown() override;
    void update(float delta_time) override;

    // Configuration
    void set_config(const magic_system_config& config);

    // Spell casting
    auto begin_cast(hb::entity::entity caster, spell_id spell, const cast_target& target)
        -> result<cast_result, std::string>;
    void cancel_cast(hb::entity::entity caster);
    auto instant_cast(hb::entity::entity caster, spell_id spell, const cast_target& target)
        -> result<spell_effect_result, std::string>;

    // Spell queries
    [[nodiscard]] auto can_cast(hb::entity::entity caster, spell_id spell, const cast_target& target) const
        -> cast_result;
    [[nodiscard]] auto is_casting(hb::entity::entity caster) const -> bool;
    [[nodiscard]] auto get_cast_state(hb::entity::entity caster) const -> const spell_cast_state*;

    // Spell knowledge
    void learn_spell(hb::entity::entity caster, spell_id spell);
    void forget_spell(hb::entity::entity caster, spell_id spell);
    [[nodiscard]] auto knows_spell(hb::entity::entity caster, spell_id spell) const -> bool;
    [[nodiscard]] auto get_spell_level(hb::entity::entity caster, spell_id spell) const -> int16_t;
    void level_up_spell(hb::entity::entity caster, spell_id spell);
    [[nodiscard]] auto get_player_spells(hb::entity::entity caster) const -> const std::vector<spell_knowledge>*;
    void set_player_spells(hb::entity::entity caster, std::vector<spell_knowledge> spells);

    // Spell templates
    void register_spell(const spell_template& spell);
    [[nodiscard]] auto get_spell(spell_id id) const -> const spell_template*;

    // Cooldowns
    [[nodiscard]] auto get_cooldown_remaining(hb::entity::entity caster, spell_id spell) const -> int32_t;
    void reset_cooldown(hb::entity::entity caster, spell_id spell);
    void reset_all_cooldowns(hb::entity::entity caster);

    // Callbacks
    void on_spell_cast(spell_callback callback);

    // Utility
    [[nodiscard]] auto calculate_mana_cost(hb::entity::entity caster, spell_id spell) const -> int32_t;
    [[nodiscard]] auto calculate_damage(hb::entity::entity caster, spell_id spell) const -> int32_t;
    [[nodiscard]] auto calculate_heal(hb::entity::entity caster, spell_id spell) const -> int32_t;

private:
    void process_active_casts(float delta_time);
    auto apply_spell_effect(hb::entity::entity caster, const spell_template& spell, const cast_target& target)
        -> spell_effect_result;
    void notify_spell_cast(hb::entity::entity caster, const spell_template& spell, const spell_effect_result& result);

    // Helper functions for spell effects
    auto find_aoe_targets(hb::entity::entity caster, const spell_template& spell, const cast_target& target) const
        -> std::vector<hb::entity::entity>;
    auto element_to_damage_type(spell_element element) const -> combat::damage_type;
    void apply_buff(hb::entity::entity target, const spell_template& spell);
    void apply_debuff(hb::entity::entity target, const spell_template& spell);
    void handle_utility_spell(hb::entity::entity caster, const spell_template& spell, const cast_target& target);

    int64_t get_current_time_ms() const;

    magic_system_config config_;

    // Spell database
    std::unordered_map<spell_id, spell_template> spells_;

    // Active casts (channeling/cast time)
    std::unordered_map<hb::entity::entity, spell_cast_state> active_casts_;

    // Player spell knowledge
    std::unordered_map<hb::entity::entity, std::vector<spell_knowledge>> player_spells_;

    // Callbacks
    std::vector<spell_callback> spell_callbacks_;
};

}  // namespace hb::magic
