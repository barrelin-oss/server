// registry_validator.cpp
// Cross-validates registry references at startup

#include "registry/registry_validator.h"
#include "registry/item_registry.h"
#include "registry/loot_registry.h"
#include "registry/shop_registry.h"
#include "core/logger.h"

namespace hb
{

auto validate_registries(
    const item_registry* items,
    const loot_registry* loot,
    const shop_registry* shops
) -> validation_result
{
    validation_result result;

    // Validate loot pool item references
    if (items && loot)
    {
        for (const auto& [pool_name, pool] : loot->pools())
        {
            for (const auto& wi : pool.items)
            {
                if (!items->exists(wi.item))
                {
                    LOG_WARN(item,
                             "Loot pool '{}' references non-existent item ID {}",
                             pool_name,
                             wi.item.value);
                    ++result.warnings;
                }
            }
        }
    }

    // Validate shop inventory item references
    if (items && shops)
    {
        for (const auto& [npc_name, shop] : shops->shops())
        {
            for (const auto& entry : shop.items)
            {
                if (!items->exists(entry.item))
                {
                    LOG_WARN(item,
                             "Shop '{}' references non-existent item ID {}",
                             npc_name,
                             entry.item.value);
                    ++result.warnings;
                }
            }
        }
    }

    if (result.warnings > 0)
    {
        LOG_WARN(item, "Registry validation found {} warning(s)", result.warnings);
    }
    else
    {
        LOG_INFO(item, "Registry validation passed -- all references valid");
    }

    return result;
}

} // namespace hb
