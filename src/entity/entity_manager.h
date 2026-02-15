#pragma once

// entity_manager.h
// Entity lifecycle management

#include "core/subsystem.h"
#include "entity/entity.h"
#include "entity/component_storage.h"

#include <concepts>
#include <vector>
#include <queue>
#include <unordered_map>
#include <typeindex>
#include <memory>
#include <any>
#include <functional>

namespace hb::entity
{

// Base class for type-erased component storage
class component_storage_base
{
public:
    virtual ~component_storage_base() = default;
    virtual void remove(entity e) = 0;
    virtual auto contains(entity e) const -> bool = 0;
};

// Type-erased wrapper for component_storage<T>
template<typename T> class component_storage_wrapper : public component_storage_base
{
public:
    void remove(entity e) override { storage_.remove(e); }
    auto contains(entity e) const -> bool override { return storage_.contains(e); }
    auto storage() -> component_storage<T>& { return storage_; }
    auto storage() const -> const component_storage<T>& { return storage_; }

private:
    component_storage<T> storage_;
};

// Entity manager configuration
struct entity_manager_config
{
    uint32_t initial_capacity{1000};
    uint32_t max_entity_count{hb::entity::max_entities};
};

// Entity manager - handles entity lifecycle and component storage
class entity_manager : public subsystem
{
public:
    entity_manager();
    ~entity_manager() override;

    // Subsystem interface
    [[nodiscard]] auto name() const -> std::string_view override { return "entity_manager"; }
    void initialize() override;
    void shutdown() override;

    // Configuration
    void set_config(const entity_manager_config& config);

    // Entity lifecycle
    [[nodiscard]] auto create() -> entity;
    [[nodiscard]] auto create(entity_type type) -> entity;
    void destroy(entity e);
    [[nodiscard]] auto is_alive(entity e) const -> bool;

    // Entity type
    [[nodiscard]] auto get_type(entity e) const -> entity_type;

    // Component access
    template<typename T, typename... Args> auto add_component(entity e, Args&&... args) -> T&
    {
        auto& storage = get_or_create_storage<T>();
        return storage.emplace(e, std::forward<Args>(args)...);
    }

    template<typename T> void remove_component(entity e)
    {
        auto* storage = get_storage<T>();
        if (storage)
        {
            storage->remove(e);
        }
    }

    template<typename T> [[nodiscard]] auto get_component(entity e) -> T*
    {
        auto* storage = get_storage<T>();
        return storage ? storage->get(e) : nullptr;
    }

    template<typename T> [[nodiscard]] auto get_component(entity e) const -> const T*
    {
        auto* storage = get_storage<T>();
        return storage ? storage->get(e) : nullptr;
    }

    template<typename T> [[nodiscard]] auto has_component(entity e) const -> bool
    {
        auto* storage = get_storage<T>();
        return storage && storage->contains(e);
    }

    // Get all entities with a specific component
    template<typename T> [[nodiscard]] auto get_all_with() -> component_storage<T>* { return get_storage<T>(); }

    template<typename T> [[nodiscard]] auto get_all_with() const -> const component_storage<T>*
    {
        return get_storage<T>();
    }

    // Iterate over entities with specific components
    template<typename T, typename Func>
        requires std::invocable<Func, entity, T&>
    void for_each(Func&& func)
    {
        auto* storage = get_storage<T>();
        if (storage)
        {
            storage->for_each(std::forward<Func>(func));
        }
    }

    // Iterate over entities with multiple components (AND query)
    template<typename T1, typename T2, typename Func>
        requires std::invocable<Func, entity, T1&, T2&>
    void for_each(Func&& func)
    {
        auto* storage1 = get_storage<T1>();
        auto* storage2 = get_storage<T2>();
        if (!storage1 || !storage2)
            return;

        // Iterate over smaller storage
        if (storage1->size() <= storage2->size())
        {
            storage1->for_each(
                [&](entity e, T1& c1)
                {
                    if (auto* c2 = storage2->get(e))
                    {
                        func(e, c1, *c2);
                    }
                });
        }
        else
        {
            storage2->for_each(
                [&](entity e, T2& c2)
                {
                    if (auto* c1 = storage1->get(e))
                    {
                        func(e, *c1, c2);
                    }
                });
        }
    }

    // Statistics
    [[nodiscard]] auto entity_count() const -> size_t;
    [[nodiscard]] auto max_entities() const -> uint32_t { return config_.max_entity_count; }

    // Debug
    [[nodiscard]] auto generation_at(uint32_t index) const -> uint8_t;

private:
    template<typename T> auto get_or_create_storage() -> component_storage<T>&
    {
        auto type_id = std::type_index(typeid(T));
        auto it = storages_.find(type_id);
        if (it == storages_.end())
        {
            auto wrapper = std::make_unique<component_storage_wrapper<T>>();
            auto* ptr = wrapper.get();
            storages_[type_id] = std::move(wrapper);
            return ptr->storage();
        }
        return static_cast<component_storage_wrapper<T>*>(it->second.get())->storage();
    }

    template<typename T> auto get_storage() -> component_storage<T>*
    {
        auto type_id = std::type_index(typeid(T));
        auto it = storages_.find(type_id);
        if (it == storages_.end())
            return nullptr;
        return &static_cast<component_storage_wrapper<T>*>(it->second.get())->storage();
    }

    template<typename T> auto get_storage() const -> const component_storage<T>*
    {
        auto type_id = std::type_index(typeid(T));
        auto it = storages_.find(type_id);
        if (it == storages_.end())
            return nullptr;
        return &static_cast<const component_storage_wrapper<T>*>(it->second.get())->storage();
    }

    entity_manager_config config_;

    // Entity tracking
    std::vector<uint8_t> generations_;  // Generation counter per index
    std::vector<bool> alive_;           // Is entity at index alive?
    std::vector<entity_type> types_;    // Entity type per index
    std::queue<uint32_t> free_indices_; // Recycled indices
    uint32_t next_index_{1};            // Next fresh index (0 is reserved for null)
    size_t alive_count_{0};

    // Type-erased component storages
    std::unordered_map<std::type_index, std::unique_ptr<component_storage_base>> storages_;
};

} // namespace hb::entity
