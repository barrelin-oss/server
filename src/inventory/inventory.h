#pragma once

// inventory.h
// Inventory container with item_id-keyed entries, z-order layering, and weight tracking
// Bank storage with slot-indexed containers

#include "core/types.h"

#include <algorithm>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

namespace hb::inventory
{

inline constexpr int16_t max_inventory_slots = 50;
inline constexpr int16_t max_trade_slots = 12;

// ─── Inventory entry (item_id-keyed, free-form positioned) ───

struct inventory_entry
{
    item_id item{};
    int16_t count{0};
    int16_t pos_x{0};
    int16_t pos_y{0};
    int32_t z_order{0};
};

// ─── Main inventory (item_id-keyed, entry-based) ───

class inventory
{
public:
    explicit inventory(int16_t max_items = max_inventory_slots) : max_items_(max_items) {}

    // Lookup by item_id
    [[nodiscard]] auto get_item(item_id id) -> inventory_entry*
    {
        for (auto& e : entries_)
        {
            if (e.item == id)
                return &e;
        }
        return nullptr;
    }

    [[nodiscard]] auto get_item(item_id id) const -> const inventory_entry*
    {
        for (const auto& e : entries_)
        {
            if (e.item == id)
                return &e;
        }
        return nullptr;
    }

    // Add item — returns entry pointer or nullptr if full
    auto add_item(item_id id, int16_t cnt, int16_t px = 0, int16_t py = 0) -> inventory_entry*
    {
        if (static_cast<int16_t>(entries_.size()) >= max_items_)
            return nullptr;

        entries_.push_back(inventory_entry{
            .item = id,
            .count = cnt,
            .pos_x = px,
            .pos_y = py,
            .z_order = next_z_order_++,
        });
        return &entries_.back();
    }

    // Remove item by id
    auto remove_item(item_id id) -> bool
    {
        auto it = std::find_if(entries_.begin(), entries_.end(), [id](const auto& e) { return e.item == id; });
        if (it == entries_.end())
            return false;
        entries_.erase(it);
        return true;
    }

    // Remove count from an item (for stackable consumption)
    auto remove_item_count(item_id id, int16_t amount) -> bool
    {
        if (!has_item(id, amount))
            return false;

        int16_t remaining = amount;
        for (auto it = entries_.begin(); it != entries_.end();)
        {
            if (it->item == id && remaining > 0)
            {
                if (it->count <= remaining)
                {
                    remaining -= it->count;
                    it = entries_.erase(it);
                    continue;
                }
                else
                {
                    it->count -= remaining;
                    remaining = 0;
                }
            }
            ++it;
        }
        return remaining == 0;
    }

    // Iteration
    [[nodiscard]] auto items() -> std::span<inventory_entry> { return entries_; }
    [[nodiscard]] auto items() const -> std::span<const inventory_entry> { return entries_; }

    // Queries
    [[nodiscard]] auto count() const -> int16_t { return static_cast<int16_t>(entries_.size()); }
    [[nodiscard]] auto max_items() const -> int16_t { return max_items_; }
    [[nodiscard]] auto is_full() const -> bool { return count() >= max_items_; }
    [[nodiscard]] auto is_empty() const -> bool { return entries_.empty(); }
    [[nodiscard]] auto has_item(item_id id) const -> bool { return get_item(id) != nullptr; }

    [[nodiscard]] auto count_item(item_id id) const -> int32_t
    {
        int32_t total = 0;
        for (const auto& e : entries_)
        {
            if (e.item == id)
                total += e.count;
        }
        return total;
    }

    [[nodiscard]] auto has_item(item_id id, int16_t amount) const -> bool { return count_item(id) >= amount; }

    // Z-order management
    [[nodiscard]] auto next_z_order() -> int32_t { return next_z_order_++; }

    void set_next_z_order(int32_t val) { next_z_order_ = val; }

    void compact_z_order()
    {
        std::sort(entries_.begin(), entries_.end(),
                  [](const auto& a, const auto& b) { return a.z_order < b.z_order; });

        for (int32_t i = 0; i < static_cast<int32_t>(entries_.size()); ++i)
        {
            entries_[static_cast<size_t>(i)].z_order = i;
        }
        next_z_order_ = static_cast<int32_t>(entries_.size());
    }

    // Weight (static formula)
    [[nodiscard]] static auto max_weight(int32_t strength, int32_t level) -> int32_t
    {
        return strength * 500 + level * 500;
    }

    void clear_all()
    {
        entries_.clear();
        next_z_order_ = 0;
    }

private:
    int16_t max_items_;
    int32_t next_z_order_{0};
    std::vector<inventory_entry> entries_;
};

// ─── Container slot (simple, slot-indexed — for bank and trade) ───

struct container_slot
{
    item_id item{};
    int16_t count{0};

    [[nodiscard]] auto is_empty() const -> bool { return !item.is_valid() || count <= 0; }

    void clear()
    {
        item = item_id{};
        count = 0;
    }

    void set(item_id id, int16_t cnt)
    {
        item = id;
        count = cnt;
    }
};

// ─── Bank storage (paginated, page+slot indexed) ───

inline constexpr int16_t default_bank_pages = 4;
inline constexpr int16_t default_bank_slots_per_page = 12;

class bank_storage
{
public:
    struct bank_location
    {
        int16_t page{0};
        int16_t slot{0};
    };

    explicit bank_storage(int16_t pages = default_bank_pages, int16_t slots_per_page = default_bank_slots_per_page)
        : pages_(pages), slots_per_page_(slots_per_page)
    {
        data_.resize(static_cast<size_t>(pages * slots_per_page));
    }

    [[nodiscard]] auto get_slot(int16_t page, int16_t slot) const -> std::optional<item_id>
    {
        auto idx = index(page, slot);
        if (!idx.has_value() || !data_[*idx].is_valid())
            return std::nullopt;
        return data_[*idx];
    }

    void set_slot(int16_t page, int16_t slot, item_id id)
    {
        auto idx = index(page, slot);
        if (idx.has_value())
            data_[*idx] = id;
    }

    void clear_slot(int16_t page, int16_t slot)
    {
        auto idx = index(page, slot);
        if (idx.has_value())
            data_[*idx] = item_id{};
    }

    [[nodiscard]] auto find_item(item_id id) const -> std::optional<bank_location>
    {
        for (int16_t p = 0; p < pages_; ++p)
        {
            for (int16_t s = 0; s < slots_per_page_; ++s)
            {
                auto idx = static_cast<size_t>(p * slots_per_page_ + s);
                if (data_[idx] == id)
                    return bank_location{p, s};
            }
        }
        return std::nullopt;
    }

    [[nodiscard]] auto find_empty_slot() const -> std::optional<bank_location>
    {
        for (int16_t p = 0; p < pages_; ++p)
        {
            for (int16_t s = 0; s < slots_per_page_; ++s)
            {
                auto idx = static_cast<size_t>(p * slots_per_page_ + s);
                if (!data_[idx].is_valid())
                    return bank_location{p, s};
            }
        }
        return std::nullopt;
    }

    [[nodiscard]] auto is_full() const -> bool { return !find_empty_slot().has_value(); }
    [[nodiscard]] auto is_empty() const -> bool { return used_slots() == 0; }
    [[nodiscard]] auto total_pages() const -> int16_t { return pages_; }
    [[nodiscard]] auto slots_per_page() const -> int16_t { return slots_per_page_; }
    [[nodiscard]] auto total_capacity() const -> int16_t { return static_cast<int16_t>(pages_ * slots_per_page_); }

    [[nodiscard]] auto used_slots() const -> int16_t
    {
        int16_t n = 0;
        for (const auto& id : data_)
        {
            if (id.is_valid())
                ++n;
        }
        return n;
    }

    [[nodiscard]] auto free_slots() const -> int16_t { return total_capacity() - used_slots(); }

    void clear_all()
    {
        for (auto& id : data_)
            id = item_id{};
    }

    [[nodiscard]] auto has_item(item_id id) const -> bool
    {
        return find_item(id).has_value();
    }

private:
    [[nodiscard]] auto index(int16_t page, int16_t slot) const -> std::optional<size_t>
    {
        if (page < 0 || page >= pages_ || slot < 0 || slot >= slots_per_page_)
            return std::nullopt;
        return static_cast<size_t>(page * slots_per_page_ + slot);
    }

    int16_t pages_;
    int16_t slots_per_page_;
    std::vector<item_id> data_;
};

// ─── Trade window (3-phase: offer → lock → confirm) ───

class trade_window
{
public:
    auto add_item(item_id id) -> bool
    {
        if (locked_ || items_.size() >= max_trade_items)
            return false;
        if (has_item(id))
            return false;
        items_.push_back(id);
        return true;
    }

    auto remove_item(item_id id) -> bool
    {
        if (locked_)
            return false;
        auto it = std::find(items_.begin(), items_.end(), id);
        if (it == items_.end())
            return false;
        items_.erase(it);
        return true;
    }

    void set_gold(int64_t amount)
    {
        if (!locked_)
            gold_ = std::max<int64_t>(0, amount);
    }

    void lock() { locked_ = true; }
    void confirm() { confirmed_ = true; }

    void reset()
    {
        items_.clear();
        gold_ = 0;
        locked_ = false;
        confirmed_ = false;
    }

    [[nodiscard]] auto items() const -> std::span<const item_id> { return items_; }
    [[nodiscard]] auto gold() const -> int64_t { return gold_; }
    [[nodiscard]] auto is_locked() const -> bool { return locked_; }
    [[nodiscard]] auto is_confirmed() const -> bool { return confirmed_; }
    [[nodiscard]] auto has_item(item_id id) const -> bool
    {
        return std::find(items_.begin(), items_.end(), id) != items_.end();
    }

    [[nodiscard]] auto is_empty() const -> bool
    {
        return items_.empty() && gold_ == 0;
    }

    static constexpr size_t max_trade_items = 12;

private:
    std::vector<item_id> items_;
    int64_t gold_{0};
    bool locked_{false};
    bool confirmed_{false};
};

} // namespace hb::inventory
