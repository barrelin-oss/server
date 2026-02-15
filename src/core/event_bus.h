#pragma once

// event_bus.h
// Type-safe publish/subscribe event system for decoupled subsystem communication

#include "core/types.h"

#include <functional>
#include <unordered_map>
#include <vector>
#include <typeindex>
#include <memory>
#include <mutex>
#include <any>
#include <atomic>

namespace hb
{

// Event bus for type-safe publish/subscribe
class event_bus
{
public:
    event_bus() = default;
    ~event_bus() = default;

    // Non-copyable
    event_bus(const event_bus&) = delete;
    auto operator=(const event_bus&) -> event_bus& = delete;

    // Movable
    event_bus(event_bus&&) noexcept = default;
    auto operator=(event_bus&&) noexcept -> event_bus& = default;

    // Subscribe to an event type
    // Returns a subscription_id that can be used to unsubscribe
    template<typename E> auto subscribe(std::function<void(const E&)> handler) -> subscription_id
    {
        std::lock_guard lock(mutex_);

        auto id = subscription_id{next_subscription_id_++};
        auto type = std::type_index(typeid(E));

        // Wrap the handler in a type-erased container
        auto wrapper = std::make_shared<handler_wrapper<E>>(std::move(handler), id);

        handlers_[type].push_back(wrapper);
        subscription_types_.insert_or_assign(id.value, type);

        return id;
    }

    // Unsubscribe using subscription_id
    void unsubscribe(subscription_id id)
    {
        std::lock_guard lock(mutex_);

        auto type_it = subscription_types_.find(id.value);
        if (type_it == subscription_types_.end())
        {
            return; // Already unsubscribed or invalid
        }

        auto type = type_it->second;
        subscription_types_.erase(type_it);

        auto handlers_it = handlers_.find(type);
        if (handlers_it == handlers_.end())
        {
            return;
        }

        auto& handlers = handlers_it->second;
        handlers.erase(
            std::remove_if(handlers.begin(), handlers.end(), [id](const auto& wrapper) { return wrapper->id() == id; }),
            handlers.end());
    }

    // Publish an event to all subscribers
    template<typename E> void publish(const E& event)
    {
        std::vector<std::shared_ptr<handler_wrapper_base>> handlers_copy;

        {
            std::lock_guard lock(mutex_);

            auto type = std::type_index(typeid(E));
            auto it = handlers_.find(type);
            if (it == handlers_.end())
            {
                return; // No subscribers
            }

            handlers_copy = it->second;
        }

        // Call handlers outside the lock to prevent deadlocks
        for (auto& wrapper : handlers_copy)
        {
            auto typed = std::static_pointer_cast<handler_wrapper<E>>(wrapper);
            typed->call(event);
        }
    }

    // Check if there are any subscribers for an event type
    template<typename E> [[nodiscard]] auto has_subscribers() const -> bool
    {
        std::lock_guard lock(mutex_);

        auto type = std::type_index(typeid(E));
        auto it = handlers_.find(type);
        return it != handlers_.end() && !it->second.empty();
    }

    // Get subscriber count for an event type
    template<typename E> [[nodiscard]] auto subscriber_count() const -> size_t
    {
        std::lock_guard lock(mutex_);

        auto type = std::type_index(typeid(E));
        auto it = handlers_.find(type);
        if (it == handlers_.end())
        {
            return 0;
        }
        return it->second.size();
    }

    // Clear all subscriptions
    void clear()
    {
        std::lock_guard lock(mutex_);
        handlers_.clear();
        subscription_types_.clear();
    }

private:
    // Base class for type-erased handler wrappers
    struct handler_wrapper_base
    {
        virtual ~handler_wrapper_base() = default;
        [[nodiscard]] virtual auto id() const -> subscription_id = 0;
    };

    // Typed handler wrapper
    template<typename E> struct handler_wrapper : handler_wrapper_base
    {
        std::function<void(const E&)> handler;
        subscription_id sub_id;

        handler_wrapper(std::function<void(const E&)> h, subscription_id id) : handler(std::move(h)), sub_id(id) {}

        void call(const E& event)
        {
            if (handler)
            {
                handler(event);
            }
        }

        [[nodiscard]] auto id() const -> subscription_id override { return sub_id; }
    };

    mutable std::mutex mutex_;
    std::atomic<uint64_t> next_subscription_id_{1};
    std::unordered_map<std::type_index, std::vector<std::shared_ptr<handler_wrapper_base>>> handlers_;
    std::unordered_map<uint64_t, std::type_index> subscription_types_;
};

// Global event bus singleton (optional - can also create instances)
[[nodiscard]] auto global_event_bus() -> event_bus&;

// RAII subscription guard - automatically unsubscribes when destroyed
class subscription_guard
{
public:
    subscription_guard() = default;

    subscription_guard(event_bus& bus, subscription_id id) : bus_(&bus), id_(id) {}

    ~subscription_guard()
    {
        if (bus_ && id_.is_valid())
        {
            bus_->unsubscribe(id_);
        }
    }

    // Non-copyable
    subscription_guard(const subscription_guard&) = delete;
    auto operator=(const subscription_guard&) -> subscription_guard& = delete;

    // Movable
    subscription_guard(subscription_guard&& other) noexcept : bus_(other.bus_), id_(other.id_)
    {
        other.bus_ = nullptr;
        other.id_ = subscription_id{};
    }

    auto operator=(subscription_guard&& other) noexcept -> subscription_guard&
    {
        if (this != &other)
        {
            if (bus_ && id_.is_valid())
            {
                bus_->unsubscribe(id_);
            }
            bus_ = other.bus_;
            id_ = other.id_;
            other.bus_ = nullptr;
            other.id_ = subscription_id{};
        }
        return *this;
    }

    // Get the subscription id
    [[nodiscard]] auto id() const -> subscription_id { return id_; }

    // Release ownership (prevent automatic unsubscribe)
    auto release() -> subscription_id
    {
        auto id = id_;
        bus_ = nullptr;
        id_ = subscription_id{};
        return id;
    }

private:
    event_bus* bus_ = nullptr;
    subscription_id id_;
};

// Helper to create subscription guard
template<typename E>
[[nodiscard]] auto subscribe_scoped(event_bus& bus, std::function<void(const E&)> handler) -> subscription_guard
{
    auto id = bus.subscribe<E>(std::move(handler));
    return subscription_guard(bus, id);
}

} // namespace hb
