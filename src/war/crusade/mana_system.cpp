// mana_system.cpp
// Crusade mana collection pipeline implementation

#include "war/crusade/mana_system.h"
#include "core/logger.h"

#include <algorithm>

namespace hb::war {

void mana_system::reset()
{
    aresden_state_.reset();
    elvine_state_.reset();
    stones_.clear();
    aresden_threshold_adjustment_ = 0;
    elvine_threshold_adjustment_ = 0;
}

void mana_system::initialize_stones(int32_t count)
{
    stones_.clear();
    stones_.resize(static_cast<size_t>(std::max(0, count)));
}

void mana_system::tick(int32_t aresden_collectors, int32_t elvine_collectors)
{
    // Step 1: Regenerate all stones
    for (auto& stone : stones_)
    {
        stone.current_mana = std::min(
            stone.current_mana + mana_stone_state::regen_rate,
            mana_stone_state::max_mana);
    }

    // Step 2: Aresden collectors drain first (legacy ordering)
    if (aresden_collectors > 0)
    {
        int32_t harvested = harvest_from_stones(aresden_collectors);
        if (harvested > 0)
        {
            aresden_state_.add_mana(harvested);
            LOG_DEBUG(general, "Aresden collected {} mana ({} collectors), pool={}",
                harvested, aresden_collectors, aresden_state_.mana_pool);
            check_gmg(war_faction::aresden);
        }
    }

    // Step 3: Elvine collectors drain remaining
    if (elvine_collectors > 0)
    {
        int32_t harvested = harvest_from_stones(elvine_collectors);
        if (harvested > 0)
        {
            elvine_state_.add_mana(harvested);
            LOG_DEBUG(general, "Elvine collected {} mana ({} collectors), pool={}",
                harvested, elvine_collectors, elvine_state_.mana_pool);
            check_gmg(war_faction::elvine);
        }
    }
}

int32_t mana_system::harvest_from_stones(int32_t collector_count)
{
    int32_t total = 0;
    for (int32_t c = 0; c < collector_count; c++)
    {
        for (auto& stone : stones_)
        {
            if (stone.current_mana >= config_.collector_harvest_rate)
            {
                stone.current_mana -= config_.collector_harvest_rate;
                total += config_.collector_harvest_rate;
            }
            else if (stone.current_mana > 0)
            {
                total += stone.current_mana;
                stone.current_mana = 0;
            }
        }
    }
    return total;
}

void mana_system::tick_faction_mana(war_faction faction, int32_t collector_count, int32_t stones_in_range)
{
    if (collector_count <= 0) return;

    // Each collector harvests mana from stones in range
    int32_t mana_gained = stones_in_range * config_.collector_harvest_rate;

    if (mana_gained > 0)
    {
        auto& state = get_state(faction);
        state.add_mana(mana_gained);

        LOG_DEBUG(general, "Faction {} collected {} mana ({} collectors, {} stones), pool={}",
            static_cast<int>(faction), mana_gained, collector_count, stones_in_range, state.mana_pool);

        check_gmg(faction);
    }
}

void mana_system::add_mana(war_faction faction, int32_t amount)
{
    if (amount <= 0) return;

    auto& state = get_state(faction);
    state.add_mana(amount);
    check_gmg(faction);
}

auto mana_system::try_consume(war_faction faction, int32_t amount) -> bool
{
    if (amount <= 0) return true;

    auto& state = get_state(faction);
    if (state.mana_pool < amount) return false;

    state.mana_pool -= amount;
    return true;
}

auto mana_system::get_state(war_faction faction) -> faction_mana_state&
{
    return (faction == war_faction::aresden) ? aresden_state_ : elvine_state_;
}

auto mana_system::get_state(war_faction faction) const -> const faction_mana_state&
{
    return (faction == war_faction::aresden) ? aresden_state_ : elvine_state_;
}

void mana_system::apply_gmg_damage(war_faction faction, int32_t damage)
{
    if (damage <= 0) return;

    auto& state = get_state(faction);
    state.gmg_accumulated_damage += damage;

    if (state.gmg_accumulated_damage >= config_.gmg_damage_threshold)
    {
        state.gmg_accumulated_damage = 0;
        if (state.gmg_charge > 0)
        {
            state.gmg_charge--;
            LOG_INFO(general, "GMG ({}) lost a charge from damage. Charges: {}",
                static_cast<int>(faction), state.gmg_charge);
        }
    }
}

void mana_system::check_gmg(war_faction faction)
{
    auto& state = get_state(faction);

    // Per-faction threshold includes crusade advantage adjustment
    int32_t threshold = config_.gmg_mana_threshold;
    if (faction == war_faction::aresden)
        threshold += aresden_threshold_adjustment_;
    else
        threshold += elvine_threshold_adjustment_;
    threshold = std::max(threshold, 1);  // Never zero or negative

    // Convert mana pool into GMG charge (legacy: reset to 0, discard remainder)
    if (state.mana_pool >= threshold)
    {
        state.mana_pool = 0;  // Discard remainder (legacy behavior)
        state.gmg_charge++;

        LOG_DEBUG(general, "Faction {} GMG charged (charge={}/{})",
            static_cast<int>(faction), state.gmg_charge, config_.gmg_charges_for_meteor);

        // Fire meteor when enough charges
        if (state.gmg_charge >= config_.gmg_charges_for_meteor)
        {
            state.gmg_charge = 0;
            state.meteors_fired++;

            LOG_INFO(general, "Faction {} fires meteor #{}", static_cast<int>(faction), state.meteors_fired);

            if (meteor_trigger_)
            {
                meteor_trigger_(faction);
            }
        }
    }
}

}  // namespace hb::war
