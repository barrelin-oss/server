#pragma once

// mana_system.h
// Crusade mana collection pipeline
// Mana Stones → Collectors → faction pool → GMG charges → meteor strike

#include "core/types.h"
#include "war/war_types.h"

#include <cstdint>
#include <functional>
#include <vector>

namespace hb::war
{

// Mana state for a single faction
struct faction_mana_state
{
    war_faction faction{war_faction::neutral};
    int32_t mana_pool{0};              // Current mana collected
    int32_t gmg_charge{0};             // GMG charge counter (meteor at threshold)
    int32_t total_mana_collected{0};   // Lifetime for this crusade
    int32_t meteors_fired{0};          // How many meteors launched
    int32_t gmg_accumulated_damage{0}; // Damage accumulator for charge reduction

    void add_mana(int32_t amount)
    {
        mana_pool += amount;
        total_mana_collected += amount;
    }

    void reset()
    {
        mana_pool = 0;
        gmg_charge = 0;
        total_mana_collected = 0;
        meteors_fired = 0;
        gmg_accumulated_damage = 0;
    }
};

// Mana system configuration
struct mana_config
{
    int32_t collector_scan_radius{5};       // Tiles around collector to scan for stones
    int32_t collector_harvest_rate{3};      // Mana per tick per stone in range
    int32_t collector_mp_restore{5};        // MP restored to allies in range per tick
    int32_t collector_mp_restore_radius{5}; // Tiles around collector for MP restore
    int32_t gmg_mana_threshold{15};         // Mana needed for 1 GMG charge
    int32_t gmg_charges_for_meteor{10};     // Charges needed to fire meteor
    float tick_interval_seconds{5.0f};      // How often mana collection ticks
    int32_t gmg_damage_threshold{500};      // Accumulated damage to remove 1 GMG charge
};

// Individual mana stone state (shared between factions)
struct mana_stone_state
{
    int32_t current_mana{5};
    static constexpr int32_t max_mana = 5;
    static constexpr int32_t regen_rate = 5;
};

// Callback for when a meteor is ready to fire
using meteor_trigger_fn = std::function<void(war_faction attacking_faction)>;

// Mana collection pipeline
// Ticked by crusade_system during active crusade
class mana_system
{
public:
    mana_system() = default;

    void set_config(const mana_config& config) { config_ = config; }
    [[nodiscard]] auto get_config() const -> const mana_config& { return config_; }

    // Crusade advantage: per-faction GMG threshold adjustments
    void set_threshold_adjustments(int32_t aresden_adj, int32_t elvine_adj)
    {
        aresden_threshold_adjustment_ = aresden_adj;
        elvine_threshold_adjustment_ = elvine_adj;
    }

    void set_meteor_trigger(meteor_trigger_fn fn) { meteor_trigger_ = std::move(fn); }

    // Initialize stone state for a new crusade
    void initialize_stones(int32_t count);

    // Main tick — handles both factions, stone regen, and drain
    void tick(int32_t aresden_collectors, int32_t elvine_collectors);

    // Reset state for new crusade
    void reset();

    // Main tick — called by crusade_system at config_.tick_interval_seconds
    // collector_count: number of active mana collector NPCs for this faction
    // stones_in_range: total mana stones within range of all collectors
    void tick_faction_mana(war_faction faction, int32_t collector_count, int32_t stones_in_range);

    // Direct mana injection (e.g., from special events)
    void add_mana(war_faction faction, int32_t amount);

    // Called when GMG takes damage — accumulates and reduces charges at threshold
    void apply_gmg_damage(war_faction faction, int32_t damage);

    // Try to consume mana from a faction's pool. Returns false if insufficient.
    auto try_consume(war_faction faction, int32_t amount) -> bool;

    // Queries
    [[nodiscard]] auto get_state(war_faction faction) -> faction_mana_state&;
    [[nodiscard]] auto get_state(war_faction faction) const -> const faction_mana_state&;
    [[nodiscard]] auto aresden_mana() const -> int32_t { return aresden_state_.mana_pool; }
    [[nodiscard]] auto elvine_mana() const -> int32_t { return elvine_state_.mana_pool; }

private:
    void check_gmg(war_faction faction);
    int32_t harvest_from_stones(int32_t collector_count);

    mana_config config_;
    std::vector<mana_stone_state> stones_;
    faction_mana_state aresden_state_{.faction = war_faction::aresden};
    faction_mana_state elvine_state_{.faction = war_faction::elvine};
    meteor_trigger_fn meteor_trigger_;
    int32_t aresden_threshold_adjustment_{0};
    int32_t elvine_threshold_adjustment_{0};
};

} // namespace hb::war
