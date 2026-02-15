// inventory_system.cpp
// Inventory management implementation

#include "inventory/inventory_system.h"
#include "core/logger.h"
#include "perf/perf_stats.h"

namespace hb::inventory
{

inventory_system::inventory_system() = default;

inventory_system::~inventory_system()
{
    if (is_initialized())
    {
        shutdown();
    }
}

void inventory_system::initialize()
{
    LOG_INFO(general, "Inventory system initializing...");
    set_initialized(true);
    LOG_INFO(general, "Inventory system initialized");
}

void inventory_system::shutdown()
{
    LOG_INFO(general, "Inventory system shutting down...");

    inventories_.clear();
    banks_.clear();
    gold_.clear();
    trade_windows_.clear();
    trade_partners_.clear();

    set_initialized(false);
    LOG_INFO(general, "Inventory system shutdown complete");
}

void inventory_system::update(float /*delta_time*/)
{
    // No periodic updates needed
}

void inventory_system::set_config(const inventory_system_config& config)
{
    config_ = config;
}

void inventory_system::create_inventory(entity_id owner)
{
    if (inventories_.contains(owner))
        return;
    inventories_.emplace(owner, inventory(static_cast<int16_t>(config_.default_inventory_size)));
    gold_[owner] = 0;
}

void inventory_system::destroy_inventory(entity_id owner)
{
    inventories_.erase(owner);
    banks_.erase(owner);
    gold_.erase(owner);

    // Cancel any active trade
    cancel_trade(owner);
}

auto inventory_system::get_inventory(entity_id owner) -> inventory*
{
    auto it = inventories_.find(owner);
    return it != inventories_.end() ? &it->second : nullptr;
}

auto inventory_system::get_inventory(entity_id owner) const -> const inventory*
{
    auto it = inventories_.find(owner);
    return it != inventories_.end() ? &it->second : nullptr;
}

void inventory_system::create_bank(entity_id owner)
{
    if (banks_.contains(owner))
        return;
    banks_.emplace(owner, bank_storage());
}

auto inventory_system::get_bank(entity_id owner) -> bank_storage*
{
    auto it = banks_.find(owner);
    return it != banks_.end() ? &it->second : nullptr;
}

auto inventory_system::add_item(entity_id owner, item_id item, int16_t count) -> inventory_result
{
    auto* perf = subsystems().get<perf::perf_stats_system>();
    PERF_TIMER(perf, perf::metric_category::inventory_op);

    auto* inv = get_inventory(owner);
    if (!inv)
        return inventory_result::failed;

    if (inv->is_full())
    {
        return inventory_result::inventory_full;
    }

    if (inv->add_item(item, count))
    {
        LOG_DEBUG(general, "Added item {} x{} to entity {}", item.value, count, owner.value);
        return inventory_result::success;
    }

    return inventory_result::failed;
}

auto inventory_system::remove_item(entity_id owner, item_id item, int16_t count) -> inventory_result
{
    auto* perf = subsystems().get<perf::perf_stats_system>();
    PERF_TIMER(perf, perf::metric_category::inventory_op);

    auto* inv = get_inventory(owner);
    if (!inv)
        return inventory_result::failed;

    if (!inv->has_item(item, count))
    {
        return inventory_result::insufficient_count;
    }

    if (inv->remove_item_count(item, count))
    {
        LOG_DEBUG(general, "Removed item {} x{} from entity {}", item.value, count, owner.value);
        return inventory_result::success;
    }

    return inventory_result::failed;
}

auto inventory_system::move_item(entity_id owner, int16_t from_slot, int16_t to_slot) -> inventory_result
{
    auto* perf = subsystems().get<perf::perf_stats_system>();
    PERF_TIMER(perf, perf::metric_category::inventory_op);

    auto* inv = get_inventory(owner);
    if (!inv)
        return inventory_result::failed;

    if (inv->move_item(from_slot, to_slot))
    {
        return inventory_result::success;
    }

    return inventory_result::invalid_slot;
}

auto inventory_system::swap_items(entity_id owner, int16_t slot_a, int16_t slot_b) -> inventory_result
{
    auto* perf = subsystems().get<perf::perf_stats_system>();
    PERF_TIMER(perf, perf::metric_category::inventory_op);

    auto* inv = get_inventory(owner);
    if (!inv)
        return inventory_result::failed;

    inv->swap_slots(slot_a, slot_b);
    return inventory_result::success;
}

auto inventory_system::deposit_item(entity_id owner, int16_t inv_slot) -> inventory_result
{
    auto* inv = get_inventory(owner);
    auto* bank = get_bank(owner);

    if (!inv || !bank)
        return inventory_result::failed;

    auto* slot = inv->get_slot(inv_slot);
    if (!slot || slot->is_empty())
    {
        return inventory_result::item_not_found;
    }

    if (bank->is_full())
    {
        return inventory_result::inventory_full;
    }

    auto empty_bank_slot = bank->find_empty_slot();
    if (!empty_bank_slot)
    {
        return inventory_result::inventory_full;
    }

    bank->get_slot(*empty_bank_slot)->set(slot->item, slot->count);
    slot->clear();

    return inventory_result::success;
}

auto inventory_system::withdraw_item(entity_id owner, int16_t bank_slot) -> inventory_result
{
    auto* inv = get_inventory(owner);
    auto* bank = get_bank(owner);

    if (!inv || !bank)
        return inventory_result::failed;

    auto* slot = bank->get_slot(bank_slot);
    if (!slot || slot->is_empty())
    {
        return inventory_result::item_not_found;
    }

    if (inv->is_full())
    {
        return inventory_result::inventory_full;
    }

    auto empty_inv_slot = inv->find_empty_slot();
    if (!empty_inv_slot)
    {
        return inventory_result::inventory_full;
    }

    inv->get_slot(*empty_inv_slot)->set(slot->item, slot->count);
    slot->clear();

    return inventory_result::success;
}

auto inventory_system::has_item(entity_id owner, item_id item, int16_t count) const -> bool
{
    auto* inv = get_inventory(owner);
    return inv && inv->has_item(item, count);
}

auto inventory_system::count_item(entity_id owner, item_id item) const -> int32_t
{
    auto* inv = get_inventory(owner);
    return inv ? inv->count_item(item) : 0;
}

auto inventory_system::free_slots(entity_id owner) const -> int16_t
{
    auto* inv = get_inventory(owner);
    return inv ? inv->free_slots() : 0;
}

auto inventory_system::is_full(entity_id owner) const -> bool
{
    auto* inv = get_inventory(owner);
    return inv && inv->is_full();
}

auto inventory_system::get_gold(entity_id owner) const -> int64_t
{
    auto it = gold_.find(owner);
    return it != gold_.end() ? it->second : 0;
}

auto inventory_system::add_gold(entity_id owner, int64_t amount) -> bool
{
    if (amount < 0)
        return false;

    gold_[owner] += amount;
    return true;
}

auto inventory_system::remove_gold(entity_id owner, int64_t amount) -> bool
{
    if (amount < 0)
        return false;
    if (!has_gold(owner, amount))
        return false;

    gold_[owner] -= amount;
    return true;
}

auto inventory_system::has_gold(entity_id owner, int64_t amount) const -> bool
{
    return get_gold(owner) >= amount;
}

void inventory_system::start_trade(entity_id player1, entity_id player2)
{
    // Cancel any existing trades
    cancel_trade(player1);
    cancel_trade(player2);

    trade_windows_[player1] = trade_window();
    trade_windows_[player2] = trade_window();
    trade_partners_[player1] = player2;
    trade_partners_[player2] = player1;

    LOG_DEBUG(general, "Trade started between {} and {}", player1.value, player2.value);
}

void inventory_system::cancel_trade(entity_id player)
{
    auto partner_it = trade_partners_.find(player);
    if (partner_it == trade_partners_.end())
        return;

    entity_id partner = partner_it->second;

    trade_windows_.erase(player);
    trade_windows_.erase(partner);
    trade_partners_.erase(player);
    trade_partners_.erase(partner);

    LOG_DEBUG(general, "Trade cancelled for {}", player.value);
}

auto inventory_system::add_to_trade(entity_id player, item_id item) -> inventory_result
{
    auto* window = get_trade_window(player);
    if (!window)
        return inventory_result::failed;
    if (window->locked)
        return inventory_result::failed;

    // Find empty trade slot
    for (auto& slot : window->offered)
    {
        if (slot.is_empty())
        {
            slot.set(item, 1);
            window->confirmed = false;
            return inventory_result::success;
        }
    }

    return inventory_result::inventory_full;
}

auto inventory_system::remove_from_trade(entity_id player, item_id item) -> inventory_result
{
    auto* window = get_trade_window(player);
    if (!window)
        return inventory_result::failed;
    if (window->locked)
        return inventory_result::failed;

    for (auto& slot : window->offered)
    {
        if (slot.item == item)
        {
            slot.clear();
            window->confirmed = false;
            return inventory_result::success;
        }
    }

    return inventory_result::item_not_found;
}

void inventory_system::set_trade_gold(entity_id player, int32_t amount)
{
    auto* window = get_trade_window(player);
    if (!window || window->locked)
        return;

    window->gold_offered = std::max(0, amount);
    window->confirmed = false;
}

void inventory_system::confirm_trade(entity_id player)
{
    auto* window = get_trade_window(player);
    if (!window || window->locked)
        return;

    window->confirmed = true;
}

void inventory_system::lock_trade(entity_id player)
{
    auto* window = get_trade_window(player);
    if (!window)
        return;

    window->locked = true;
}

auto inventory_system::complete_trade(entity_id player1, entity_id player2) -> bool
{
    auto* perf = subsystems().get<perf::perf_stats_system>();
    PERF_TIMER(perf, perf::metric_category::trade_complete);

    auto* window1 = get_trade_window(player1);
    auto* window2 = get_trade_window(player2);

    if (!window1 || !window2)
        return false;
    if (!window1->confirmed || !window2->confirmed)
        return false;
    if (!window1->locked || !window2->locked)
        return false;

    // Verify gold
    if (!has_gold(player1, window1->gold_offered))
        return false;
    if (!has_gold(player2, window2->gold_offered))
        return false;

    // Transfer gold
    remove_gold(player1, window1->gold_offered);
    remove_gold(player2, window2->gold_offered);
    add_gold(player1, window2->gold_offered);
    add_gold(player2, window1->gold_offered);

    // Transfer items (simplified - would need proper item transfer)
    // In real implementation, would move items between inventories

    // Cleanup
    cancel_trade(player1);

    LOG_INFO(general, "Trade completed between {} and {}", player1.value, player2.value);

    return true;
}

auto inventory_system::get_trade_window(entity_id player) -> trade_window*
{
    auto it = trade_windows_.find(player);
    return it != trade_windows_.end() ? &it->second : nullptr;
}

auto inventory_system::get_trade_partner(entity_id player) const -> entity_id
{
    auto it = trade_partners_.find(player);
    return it != trade_partners_.end() ? it->second : entity_id{};
}

} // namespace hb::inventory
