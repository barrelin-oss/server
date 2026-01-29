#pragma once

// inventory.h
// Inventory container and slot management

#include "core/types.h"

#include <vector>
#include <optional>
#include <cstdint>

namespace hb::inventory {

inline constexpr int16_t max_inventory_slots = 50;
inline constexpr int16_t max_bank_slots = 200;
inline constexpr int16_t max_trade_slots = 12;

// Inventory slot
struct inventory_slot {
    item_id item{};
    int16_t count{0};

    [[nodiscard]] auto is_empty() const -> bool { return !item.is_valid() || count <= 0; }

    void clear() {
        item = item_id{};
        count = 0;
    }

    void set(item_id id, int16_t cnt) {
        item = id;
        count = cnt;
    }
};

// Generic inventory container
class inventory {
public:
    explicit inventory(int16_t capacity = max_inventory_slots)
        : capacity_(capacity) {
        slots_.resize(static_cast<size_t>(capacity));
    }

    // Slot access
    [[nodiscard]] auto get_slot(int16_t index) -> inventory_slot* {
        if (index < 0 || index >= capacity_) return nullptr;
        return &slots_[static_cast<size_t>(index)];
    }

    [[nodiscard]] auto get_slot(int16_t index) const -> const inventory_slot* {
        if (index < 0 || index >= capacity_) return nullptr;
        return &slots_[static_cast<size_t>(index)];
    }

    // Item operations
    [[nodiscard]] auto find_item(item_id item) const -> std::optional<int16_t> {
        for (int16_t i = 0; i < capacity_; ++i) {
            if (slots_[static_cast<size_t>(i)].item == item) {
                return i;
            }
        }
        return std::nullopt;
    }

    [[nodiscard]] auto find_empty_slot() const -> std::optional<int16_t> {
        for (int16_t i = 0; i < capacity_; ++i) {
            if (slots_[static_cast<size_t>(i)].is_empty()) {
                return i;
            }
        }
        return std::nullopt;
    }

    [[nodiscard]] auto count_item(item_id item) const -> int32_t {
        int32_t total = 0;
        for (const auto& slot : slots_) {
            if (slot.item == item) {
                total += slot.count;
            }
        }
        return total;
    }

    [[nodiscard]] auto has_item(item_id item, int16_t count = 1) const -> bool {
        return count_item(item) >= count;
    }

    auto add_item(item_id item, int16_t count) -> bool {
        auto slot_idx = find_empty_slot();
        if (!slot_idx) return false;

        slots_[static_cast<size_t>(*slot_idx)].set(item, count);
        return true;
    }

    auto remove_item(item_id item) -> bool {
        auto slot_idx = find_item(item);
        if (!slot_idx) return false;

        slots_[static_cast<size_t>(*slot_idx)].clear();
        return true;
    }

    auto remove_item_count(item_id item, int16_t count) -> bool {
        if (!has_item(item, count)) return false;

        int16_t remaining = count;
        for (auto& slot : slots_) {
            if (slot.item == item && remaining > 0) {
                if (slot.count <= remaining) {
                    remaining -= slot.count;
                    slot.clear();
                } else {
                    slot.count -= remaining;
                    remaining = 0;
                }
            }
        }
        return remaining == 0;
    }

    void clear_slot(int16_t index) {
        if (index >= 0 && index < capacity_) {
            slots_[static_cast<size_t>(index)].clear();
        }
    }

    void clear_all() {
        for (auto& slot : slots_) {
            slot.clear();
        }
    }

    // Queries
    [[nodiscard]] auto capacity() const -> int16_t { return capacity_; }

    [[nodiscard]] auto used_slots() const -> int16_t {
        int16_t count = 0;
        for (const auto& slot : slots_) {
            if (!slot.is_empty()) ++count;
        }
        return count;
    }

    [[nodiscard]] auto free_slots() const -> int16_t {
        return capacity_ - used_slots();
    }

    [[nodiscard]] auto is_full() const -> bool {
        return free_slots() == 0;
    }

    [[nodiscard]] auto is_empty() const -> bool {
        return used_slots() == 0;
    }

    // Swap slots
    void swap_slots(int16_t a, int16_t b) {
        if (a < 0 || a >= capacity_ || b < 0 || b >= capacity_) return;
        std::swap(slots_[static_cast<size_t>(a)], slots_[static_cast<size_t>(b)]);
    }

    // Move item between slots
    auto move_item(int16_t from, int16_t to) -> bool {
        if (from < 0 || from >= capacity_ || to < 0 || to >= capacity_) return false;

        auto& from_slot = slots_[static_cast<size_t>(from)];
        auto& to_slot = slots_[static_cast<size_t>(to)];

        if (from_slot.is_empty()) return false;

        if (to_slot.is_empty()) {
            to_slot = from_slot;
            from_slot.clear();
            return true;
        }

        // If both have items, swap
        std::swap(from_slot, to_slot);
        return true;
    }

private:
    int16_t capacity_;
    std::vector<inventory_slot> slots_;
};

// Bank storage (larger capacity)
class bank_storage : public inventory {
public:
    bank_storage() : inventory(max_bank_slots) {}
};

// Trade window
class trade_window {
public:
    std::vector<inventory_slot> offered;
    int32_t gold_offered{0};
    bool confirmed{false};
    bool locked{false};

    trade_window() {
        offered.resize(max_trade_slots);
    }

    void reset() {
        for (auto& slot : offered) {
            slot.clear();
        }
        gold_offered = 0;
        confirmed = false;
        locked = false;
    }

    [[nodiscard]] auto is_empty() const -> bool {
        if (gold_offered > 0) return false;
        for (const auto& slot : offered) {
            if (!slot.is_empty()) return false;
        }
        return true;
    }
};

}  // namespace hb::inventory
