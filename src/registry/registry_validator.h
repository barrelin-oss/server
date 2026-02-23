#pragma once

// registry_validator.h
// Cross-validates registry references at startup

#include <cstddef>

namespace hb
{

class item_registry;
class loot_registry;
class shop_registry;

struct validation_result
{
    size_t warnings{0};
    size_t errors{0};
};

// Validate that all item IDs referenced by loot tables and shops
// actually exist in the item registry. Logs warnings for invalid references.
auto validate_registries(
    const item_registry* items,
    const loot_registry* loot,
    const shop_registry* shops
) -> validation_result;

} // namespace hb
