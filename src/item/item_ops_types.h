#pragma once

// item_ops_types.h
// Result types for item operations layer

#include "core/types.h"
#include "item/special_ability.h"
#include "player/equipment.h"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace hb::item_ops
{

// Force unequip reasons
enum class force_unequip_reason : uint8_t
{
    broken = 0,
    hammer_strip = 1,
    armor_break = 2,
};

struct pickup_result
{
    bool success{false};
    std::string error;
    item_id picked_up{};
    int16_t pos_x{0};
    int16_t pos_y{0};
    int32_t z_order{0};
    int32_t new_weight{0};
    int32_t max_weight{0};
};

struct drop_result
{
    bool success{false};
    std::string error;
    item_id dropped{};
    bool was_equipped{false};
    std::optional<player::equip_slot> unequipped_slot{};
};

struct equip_result
{
    bool success{false};
    std::string error;
    player::equip_slot slot{};
    item_id equipped{};
    std::optional<item_id> swapped_out{};
};

struct unequip_result
{
    bool success{false};
    std::string error;
    player::equip_slot slot{};
    item_id unequipped{};
};

struct force_unequip_result
{
    bool success{false};
    player::equip_slot slot{};
    item_id unequipped{};
    force_unequip_reason reason{};
};

struct use_item_result
{
    bool success{false};
    std::string error;
    item_id used{};
    bool item_consumed{false};
};

struct shop_buy_result
{
    bool success{false};
    std::string error;
    item_id created{};
    int16_t pos_x{0};
    int16_t pos_y{0};
    int32_t z_order{0};
    int64_t new_gold{0};
    int32_t new_weight{0};
    int32_t max_weight{0};
};

struct shop_sell_result
{
    bool success{false};
    std::string error;
    item_id sold{};
    int64_t new_gold{0};
    int32_t new_weight{0};
    int32_t max_weight{0};
};

struct shop_repair_result
{
    bool success{false};
    std::string error;
    item_id repaired{};
    int64_t new_gold{0};
};

struct bank_deposit_result
{
    bool success{false};
    std::string error;
    item_id deposited{};
    int16_t page{0};
    int16_t slot{0};
    int32_t new_weight{0};
    int32_t max_weight{0};
};

struct bank_withdraw_result
{
    bool success{false};
    std::string error;
    item_id withdrawn{};
    int16_t pos_x{0};
    int16_t pos_y{0};
    int32_t z_order{0};
    int32_t new_weight{0};
    int32_t max_weight{0};
};

struct bank_reposition_result
{
    bool success{false};
    std::string error;
};

struct upgrade_result
{
    bool success{false};
    std::string error;
    item_id target{};
    item_id stone{};
};

struct activate_ability_result
{
    bool success{false};
    std::string error;
    item::special_ability_type ability{};
    int32_t duration_ms{0};
};

struct damage_equipment_result
{
    bool success{false};
    item_id damaged{};
    int16_t new_durability{0};
    bool broke{false};
    player::equip_slot slot{};
};

struct inventory_placement
{
    item_id item{};
    int16_t pos_x{0};
    int16_t pos_y{0};
    int32_t z_order{0};
};

struct trade_result
{
    bool success{false};
    std::string error;

    struct player_side
    {
        std::vector<item_id> received_items;
        std::vector<item_id> lost_items;
        int64_t gold_change{0};
        int32_t new_weight{0};
        int32_t max_weight{0};
        std::vector<inventory_placement> placements;
    };

    player_side player_a;
    player_side player_b;
};

struct give_result
{
    bool success{false};
    std::string error;
    item_id created{};
    int16_t pos_x{0};
    int16_t pos_y{0};
    int32_t z_order{0};
};

struct loot_drop_result
{
    item_id dropped{};
    int16_t ground_x{0};
    int16_t ground_y{0};
};

} // namespace hb::item_ops
