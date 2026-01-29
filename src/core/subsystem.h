#pragma once

// subsystem.h
// Base interface for all game subsystems
// Provides lifecycle management and consistent interface

#include <string_view>
#include <memory>
#include <vector>
#include <unordered_map>
#include <typeindex>

namespace hb {

// Forward declarations
class event_bus;

// Base class for all subsystems
class subsystem {
public:
    virtual ~subsystem() = default;

    // Non-copyable
    subsystem(const subsystem&) = delete;
    auto operator=(const subsystem&) -> subsystem& = delete;

    // Movable
    subsystem(subsystem&&) noexcept = default;
    auto operator=(subsystem&&) noexcept -> subsystem& = default;

    // Get the subsystem name (for logging and debugging)
    [[nodiscard]] virtual auto name() const -> std::string_view = 0;

    // Initialize the subsystem
    // Called once during server startup
    // Should set up internal state and subscribe to events
    virtual void initialize() = 0;

    // Shutdown the subsystem
    // Called once during server shutdown
    // Should clean up resources and unsubscribe from events
    virtual void shutdown() = 0;

    // Update the subsystem (optional)
    // Called every game tick for subsystems that need per-tick processing
    // delta_time is the time since last update in seconds
    virtual void update(float delta_time) { (void)delta_time; }

    // Check if the subsystem is initialized
    [[nodiscard]] auto is_initialized() const -> bool { return initialized_; }

protected:
    subsystem() = default;

    // Mark as initialized (call from initialize())
    void set_initialized(bool value) { initialized_ = value; }

    // Get the event bus for subscribing to events
    [[nodiscard]] auto event_bus() -> hb::event_bus&;

private:
    friend class subsystem_manager;
    bool initialized_ = false;
};

// Subsystem registry and manager
class subsystem_manager {
public:
    subsystem_manager();
    ~subsystem_manager();

    // Non-copyable, non-movable
    subsystem_manager(const subsystem_manager&) = delete;
    auto operator=(const subsystem_manager&) -> subsystem_manager& = delete;
    subsystem_manager(subsystem_manager&&) = delete;
    auto operator=(subsystem_manager&&) -> subsystem_manager& = delete;

    // Register a subsystem
    // Takes ownership of the subsystem
    template<typename T>
    void register_subsystem(std::unique_ptr<T> subsystem) {
        static_assert(std::is_base_of_v<hb::subsystem, T>,
            "T must derive from subsystem");
        auto ptr = subsystem.get();
        subsystems_.push_back(std::move(subsystem));
        subsystem_map_[std::type_index(typeid(T))] = ptr;
    }

    // Create and register a subsystem
    template<typename T, typename... Args>
    auto create_subsystem(Args&&... args) -> T& {
        auto ptr = std::make_unique<T>(std::forward<Args>(args)...);
        auto& ref = *ptr;
        register_subsystem(std::move(ptr));
        return ref;
    }

    // Get a subsystem by type
    template<typename T>
    [[nodiscard]] auto get() -> T* {
        auto it = subsystem_map_.find(std::type_index(typeid(T)));
        if (it == subsystem_map_.end()) {
            return nullptr;
        }
        return static_cast<T*>(it->second);
    }

    template<typename T>
    [[nodiscard]] auto get() const -> const T* {
        auto it = subsystem_map_.find(std::type_index(typeid(T)));
        if (it == subsystem_map_.end()) {
            return nullptr;
        }
        return static_cast<const T*>(it->second);
    }

    // Initialize all registered subsystems
    void initialize_all();

    // Shutdown all registered subsystems (in reverse order)
    void shutdown_all();

    // Update all registered subsystems
    void update_all(float delta_time);

    // Get the event bus
    [[nodiscard]] auto event_bus() -> hb::event_bus& { return *event_bus_; }

    // Get count of registered subsystems
    [[nodiscard]] auto count() const -> size_t { return subsystems_.size(); }

private:
    std::vector<std::unique_ptr<subsystem>> subsystems_;
    std::unordered_map<std::type_index, subsystem*> subsystem_map_;
    std::unique_ptr<hb::event_bus> event_bus_;
};

// Global subsystem manager singleton
[[nodiscard]] auto subsystems() -> subsystem_manager&;

}  // namespace hb
