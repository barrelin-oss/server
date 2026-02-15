#pragma once

// inventory_system.h
// Inventory management subsystem

#include "core/types.h"
#include "core/result.h"
#include "core/subsystem.h"
#include "inventory/inventory.h"

#include <unordered_map>
#include <string_view>

namespace hb::inventory
{

// Inventory operation results
enum class inventory_result : uint8_t
{
    success = 0,
    inventory_full = 1,
    item_not_found = 2,
    insufficient_count = 3,
    invalid_slot = 4,
    item_not_tradeable = 5,
    item_bound = 6,
    weight_limit = 7,
    failed = 8,
};

// Inventory system configuration
struct inventory_system_config
{
    int32_t default_inventory_size{50};
    int32_t default_bank_size{200};
    bool enable_weight_limit{false};
    int32_t base_weight_limit{1000};
};

// Inventory system - manages player inventories
class inventory_system : public subsystem
{
public:
    inventory_system();
    ~inventory_system() override;

    // Subsystem interface
    [[nodiscard]] auto name() const -> std::string_view override { return "inventory_system"; }
    void initialize() override;
    void shutdown() override;
    void update(float delta_time) override;

    // Configuration
    void set_config(const inventory_system_config& config);

    // Inventory lifecycle
    void create_inventory(entity_id owner);
    void destroy_inventory(entity_id owner);
    [[nodiscard]] auto get_inventory(entity_id owner) -> inventory*;
    [[nodiscard]] auto get_inventory(entity_id owner) const -> const inventory*;

    // Bank
    void create_bank(entity_id owner);
    [[nodiscard]] auto get_bank(entity_id owner) -> bank_storage*;

    // Item operations
    auto add_item(entity_id owner, item_id item, int16_t count = 1) -> inventory_result;
    auto remove_item(entity_id owner, item_id item, int16_t count = 1) -> inventory_result;
    auto move_item(entity_id owner, int16_t from_slot, int16_t to_slot) -> inventory_result;
    auto swap_items(entity_id owner, int16_t slot_a, int16_t slot_b) -> inventory_result;

    // Bank operations
    auto deposit_item(entity_id owner, int16_t inv_slot) -> inventory_result;
    auto withdraw_item(entity_id owner, int16_t bank_slot) -> inventory_result;

    // Queries
    [[nodiscard]] auto has_item(entity_id owner, item_id item, int16_t count = 1) const -> bool;
    [[nodiscard]] auto count_item(entity_id owner, item_id item) const -> int32_t;
    [[nodiscard]] auto free_slots(entity_id owner) const -> int16_t;
    [[nodiscard]] auto is_full(entity_id owner) const -> bool;

    // Gold
    [[nodiscard]] auto get_gold(entity_id owner) const -> int64_t;
    auto add_gold(entity_id owner, int64_t amount) -> bool;
    auto remove_gold(entity_id owner, int64_t amount) -> bool;
    [[nodiscard]] auto has_gold(entity_id owner, int64_t amount) const -> bool;

    // Trading
    void start_trade(entity_id player1, entity_id player2);
    void cancel_trade(entity_id player);
    auto add_to_trade(entity_id player, item_id item) -> inventory_result;
    auto remove_from_trade(entity_id player, item_id item) -> inventory_result;
    void set_trade_gold(entity_id player, int32_t amount);
    void confirm_trade(entity_id player);
    void lock_trade(entity_id player);
    auto complete_trade(entity_id player1, entity_id player2) -> bool;
    [[nodiscard]] auto get_trade_window(entity_id player) -> trade_window*;
    [[nodiscard]] auto get_trade_partner(entity_id player) const -> entity_id;

private:
    inventory_system_config config_;

    std::unordered_map<entity_id, inventory> inventories_;
    std::unordered_map<entity_id, bank_storage> banks_;
    std::unordered_map<entity_id, int64_t> gold_;

    // Trading state
    std::unordered_map<entity_id, trade_window> trade_windows_;
    std::unordered_map<entity_id, entity_id> trade_partners_;
};

} // namespace hb::inventory
