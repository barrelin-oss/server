#pragma once

// component_storage.h
// Sparse set storage for cache-efficient component iteration

#include "entity/entity.h"

#include <concepts>
#include <vector>
#include <optional>
#include <cassert>
#include <stdexcept>
#include <type_traits>

namespace hb::entity
{

// Sparse set for storing components
// Dense array for cache-efficient iteration
// Sparse array for O(1) lookup by entity
template<typename T> class component_storage
{
public:
    static_assert(std::is_default_constructible_v<T>, "Component must be default constructible");

    component_storage()
    {
        // Pre-allocate sparse array
        sparse_.resize(max_entities, null_index);
    }

    // Add component to entity
    template<typename... Args> auto emplace(entity e, Args&&... args) -> T&
    {
        assert(e.is_valid());
        auto idx = e.index();

        // Check if already has component
        if (idx < sparse_.size() && sparse_[idx] != null_index)
        {
            // Replace existing
            dense_data_[sparse_[idx]] = T{std::forward<Args>(args)...};
            return dense_data_[sparse_[idx]];
        }

        // Ensure sparse array is large enough
        if (idx >= sparse_.size())
        {
            sparse_.resize(idx + 1, null_index);
        }

        // Add new entry
        auto dense_idx = static_cast<uint32_t>(dense_data_.size());
        sparse_[idx] = dense_idx;
        dense_entities_.push_back(e);
        dense_data_.emplace_back(std::forward<Args>(args)...);

        return dense_data_.back();
    }

    // Remove component from entity
    void remove(entity e)
    {
        if (!e.is_valid())
            return;
        auto idx = e.index();
        if (idx >= sparse_.size() || sparse_[idx] == null_index)
            return;

        auto dense_idx = sparse_[idx];

        // Swap with last element
        if (dense_idx != dense_data_.size() - 1)
        {
            auto last_entity = dense_entities_.back();
            dense_data_[dense_idx] = std::move(dense_data_.back());
            dense_entities_[dense_idx] = last_entity;
            sparse_[last_entity.index()] = dense_idx;
        }

        // Remove last element
        dense_data_.pop_back();
        dense_entities_.pop_back();
        sparse_[idx] = null_index;
    }

    // Check if entity has component
    [[nodiscard]] auto contains(entity e) const -> bool
    {
        if (!e.is_valid())
            return false;
        auto idx = e.index();
        return idx < sparse_.size() && sparse_[idx] != null_index;
    }

    // Get component (returns nullptr if not found)
    [[nodiscard]] auto get(entity e) -> T*
    {
        if (!e.is_valid())
            return nullptr;
        auto idx = e.index();
        if (idx >= sparse_.size() || sparse_[idx] == null_index)
            return nullptr;
        return &dense_data_[sparse_[idx]];
    }

    [[nodiscard]] auto get(entity e) const -> const T*
    {
        if (!e.is_valid())
            return nullptr;
        auto idx = e.index();
        if (idx >= sparse_.size() || sparse_[idx] == null_index)
            return nullptr;
        return &dense_data_[sparse_[idx]];
    }

    // Get component (throws if not found)
    [[nodiscard]] auto at(entity e) -> T&
    {
        auto* ptr = get(e);
        if (!ptr)
            throw std::out_of_range("Entity does not have component");
        return *ptr;
    }

    [[nodiscard]] auto at(entity e) const -> const T&
    {
        auto* ptr = get(e);
        if (!ptr)
            throw std::out_of_range("Entity does not have component");
        return *ptr;
    }

    // Get optional reference
    [[nodiscard]] auto try_get(entity e) -> std::optional<std::reference_wrapper<T>>
    {
        auto* ptr = get(e);
        if (!ptr)
            return std::nullopt;
        return std::ref(*ptr);
    }

    [[nodiscard]] auto try_get(entity e) const -> std::optional<std::reference_wrapper<const T>>
    {
        auto* ptr = get(e);
        if (!ptr)
            return std::nullopt;
        return std::cref(*ptr);
    }

    // Size and capacity
    [[nodiscard]] auto size() const -> size_t { return dense_data_.size(); }
    [[nodiscard]] auto empty() const -> bool { return dense_data_.empty(); }
    [[nodiscard]] auto capacity() const -> size_t { return dense_data_.capacity(); }

    void reserve(size_t n)
    {
        dense_data_.reserve(n);
        dense_entities_.reserve(n);
    }

    void clear()
    {
        dense_data_.clear();
        dense_entities_.clear();
        std::fill(sparse_.begin(), sparse_.end(), null_index);
    }

    // Iteration (iterates over all entities with this component)
    [[nodiscard]] auto begin() { return dense_data_.begin(); }
    [[nodiscard]] auto end() { return dense_data_.end(); }
    [[nodiscard]] auto begin() const { return dense_data_.begin(); }
    [[nodiscard]] auto end() const { return dense_data_.end(); }

    // Get entity at dense index
    [[nodiscard]] auto entity_at(size_t dense_idx) const -> entity { return dense_entities_[dense_idx]; }

    // Iterate with entity and component
    template<typename Func>
        requires std::invocable<Func, entity, T&>
    void for_each(Func&& func)
    {
        for (size_t i = 0; i < dense_data_.size(); ++i)
        {
            func(dense_entities_[i], dense_data_[i]);
        }
    }

    template<typename Func>
        requires std::invocable<Func, entity, const T&>
    void for_each(Func&& func) const
    {
        for (size_t i = 0; i < dense_data_.size(); ++i)
        {
            func(dense_entities_[i], dense_data_[i]);
        }
    }

    // Raw access to dense arrays (for advanced use)
    [[nodiscard]] auto data() -> T* { return dense_data_.data(); }
    [[nodiscard]] auto data() const -> const T* { return dense_data_.data(); }
    [[nodiscard]] auto entities() const -> const std::vector<entity>& { return dense_entities_; }

private:
    static constexpr uint32_t null_index = 0xFFFFFFFF;

    std::vector<uint32_t> sparse_;       // entity index -> dense index
    std::vector<entity> dense_entities_; // dense index -> entity
    std::vector<T> dense_data_;          // dense index -> component data
};

} // namespace hb::entity
