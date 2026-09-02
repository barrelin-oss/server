#pragma once

// shop_config.h
// Shop data structures for NPC merchants

#include "core/types.h"

#include <string>
#include <vector>
#include <cstdint>

namespace hb::npc
{

// An item available for sale in a shop
struct shop_item_entry
{
    item_id item{};
    int16_t default_count{1};
};

// Shop type determines what an NPC sells/services
enum class shop_type : uint8_t
{
    general = 0,    // ShopKeeper - potions, scrolls, misc
    blacksmith = 1, // Weapons/armor
};

// Configuration for a single NPC shop
struct shop_config
{
    std::string npc_name;
    shop_type type{shop_type::general};
    std::vector<uint8_t> buy_categories;    // item_category values the shop will buy
    std::vector<uint8_t> repair_categories; // item_category values the shop can repair
    std::vector<shop_item_entry> items;     // items for sale
    std::vector<uint16_t> spells;           // spell ids this NPC can teach (magic.yaml)
};

} // namespace hb::npc
