#pragma once

// loot_table.h
// NPC drop tables and loot generation

#include "core/types.h"

#include <vector>
#include <cstdint>

namespace hb::npc {

// Single loot entry
struct loot_entry {
    item_id item{};
    int16_t min_count{1};
    int16_t max_count{1};
    int16_t drop_chance{100};  // Per 10000 (100 = 1%)

    [[nodiscard]] auto roll_drop() const -> bool {
        // Would use actual random number generator
        return true;  // Placeholder
    }

    [[nodiscard]] auto roll_count() const -> int16_t {
        if (min_count == max_count) return min_count;
        // Would use actual random number generator
        return min_count;  // Placeholder
    }
};

// Gold drop configuration
struct gold_drop {
    int32_t min_gold{0};
    int32_t max_gold{0};
    int16_t drop_chance{10000};  // Per 10000

    [[nodiscard]] auto roll_gold() const -> int32_t {
        if (min_gold == max_gold) return min_gold;
        // Would use actual random number generator
        return min_gold;  // Placeholder
    }
};

// Complete loot table for an NPC
struct loot_table {
    std::vector<loot_entry> guaranteed;  // Always drops
    std::vector<loot_entry> common;      // Common drops
    std::vector<loot_entry> rare;        // Rare drops
    std::vector<loot_entry> ultra_rare;  // Very rare drops
    gold_drop gold;

    // Generate drops for this NPC
    [[nodiscard]] auto generate_drops() const -> std::vector<std::pair<item_id, int16_t>> {
        std::vector<std::pair<item_id, int16_t>> result;

        // Add guaranteed drops
        for (const auto& entry : guaranteed) {
            result.emplace_back(entry.item, entry.roll_count());
        }

        // Roll for other drops
        for (const auto& entry : common) {
            if (entry.roll_drop()) {
                result.emplace_back(entry.item, entry.roll_count());
            }
        }

        for (const auto& entry : rare) {
            if (entry.roll_drop()) {
                result.emplace_back(entry.item, entry.roll_count());
            }
        }

        for (const auto& entry : ultra_rare) {
            if (entry.roll_drop()) {
                result.emplace_back(entry.item, entry.roll_count());
            }
        }

        return result;
    }

    [[nodiscard]] auto generate_gold() const -> int32_t {
        return gold.roll_gold();
    }
};

}  // namespace hb::npc
