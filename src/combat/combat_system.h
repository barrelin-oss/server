#pragma once

// combat_system.h
// Combat management subsystem

#include "core/types.h"
#include "core/result.h"
#include "core/subsystem.h"
#include "combat/combat_events.h"
#include "combat/damage_calc.h"

#include <functional>
#include <vector>
#include <string_view>

namespace hb::combat
{

// Combat system configuration
struct combat_system_config
{
    bool enable_pvp{true};
    bool enable_friendly_fire{false};
    int32_t attack_cooldown_ms{1000};
    int32_t death_protection_ms{3000}; // Invulnerability after respawn
    float pvp_damage_modifier{0.7f};   // PvP damage reduction
    float pve_damage_modifier{1.0f};
};

// Combat result after processing an attack
struct combat_result
{
    hit_result hit;
    bool target_killed{false};
    int32_t exp_reward{0};
    int32_t gold_reward{0};
};

// Combat system - handles all combat interactions
class combat_system : public subsystem
{
public:
    using damage_callback = std::function<void(const damage_event&)>;
    using death_callback = std::function<void(const death_event&)>;

    combat_system();
    ~combat_system() override;

    // Subsystem interface
    [[nodiscard]] auto name() const -> std::string_view override { return "combat_system"; }
    void initialize() override;
    void shutdown() override;
    void update(float delta_time) override;

    // Configuration
    void set_config(const combat_system_config& config);

    // Combat resolution
    auto process_attack(const attack_event& attack) -> combat_result;
    auto resolve_hit(const combat_context& ctx) -> hit_result;
    void apply_damage(hb::entity::entity target, const hit_result& result, hb::entity::entity source);

    // Direct damage (bypasses hit calculation)
    void deal_damage(hb::entity::entity target, int32_t damage, damage_type type, hb::entity::entity source);
    void deal_pure_damage(hb::entity::entity target, int32_t damage, hb::entity::entity source);

    // Combat queries
    [[nodiscard]] auto can_attack(hb::entity::entity attacker, hb::entity::entity defender) const -> bool;
    [[nodiscard]] auto is_hostile(hb::entity::entity a, hb::entity::entity b) const -> bool;
    [[nodiscard]] auto is_in_combat(hb::entity::entity e) const -> bool;

    // Callbacks
    void on_damage(damage_callback callback);
    void on_death(death_callback callback);

    // Combat state
    void enter_combat(hb::entity::entity e);
    void leave_combat(hb::entity::entity e);
    void set_invulnerable(hb::entity::entity e, int32_t duration_ms);
    [[nodiscard]] auto is_invulnerable(hb::entity::entity e) const -> bool;

    // Kill tracking
    [[nodiscard]] auto get_kill_count(hb::entity::entity e) const -> int32_t;
    [[nodiscard]] auto get_death_count(hb::entity::entity e) const -> int32_t;

private:
    void process_pending_deaths();
    void update_combat_states(float delta_time);
    void notify_damage(const damage_event& event);
    void notify_death(const death_event& event);

    // Helper methods for building combat context
    auto
    build_combat_context(hb::entity::entity attacker, hb::entity::entity defender, damage_type type) -> combat_context;
    [[nodiscard]] auto is_player_entity(hb::entity::entity e) const -> bool;
    [[nodiscard]] auto check_entity_dead(hb::entity::entity e) const -> bool;
    [[nodiscard]] auto calculate_kill_rewards(hb::entity::entity target) const -> std::pair<int32_t, int32_t>;
    [[nodiscard]] auto is_pvp_safe_zone_blocked(hb::entity::entity attacker, hb::entity::entity defender) const -> bool;

    combat_system_config config_;

    std::vector<damage_callback> damage_callbacks_;
    std::vector<death_callback> death_callbacks_;

    // Combat state tracking
    struct combat_state
    {
        bool in_combat{false};
        int64_t invulnerable_until_ms{0};
        int32_t kill_count{0};
        int32_t death_count{0};
    };

    std::unordered_map<hb::entity::entity, combat_state> combat_states_;
    std::vector<death_event> pending_deaths_;
};

} // namespace hb::combat
