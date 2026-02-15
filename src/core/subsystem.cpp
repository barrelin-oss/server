// subsystem.cpp
// Subsystem base class and manager implementation

#include "core/subsystem.h"
#include "core/event_bus.h"
#include "core/logger.h"

namespace hb
{

// subsystem implementation

auto subsystem::event_bus() -> hb::event_bus&
{
    return hb::subsystems().event_bus();
}

// subsystem_manager implementation

subsystem_manager::subsystem_manager() : event_bus_(std::make_unique<hb::event_bus>()) {}

subsystem_manager::~subsystem_manager()
{
    shutdown_all();
}

void subsystem_manager::initialize_all()
{
    LOG_INFO(general, "Initializing {} subsystems...", subsystems_.size());

    for (auto& subsystem : subsystems_)
    {
        if (!subsystem->is_initialized())
        {
            LOG_DEBUG(general, "Initializing subsystem: {}", subsystem->name());
            try
            {
                subsystem->initialize();
                subsystem->set_initialized(true);
                LOG_INFO(general, "Subsystem initialized: {}", subsystem->name());
            }
            catch (const std::exception& e)
            {
                LOG_ERROR(general, "Failed to initialize subsystem {}: {}", subsystem->name(), e.what());
                throw;
            }
        }
    }

    LOG_INFO(general, "All subsystems initialized");
}

void subsystem_manager::shutdown_all()
{
    LOG_INFO(general, "Shutting down subsystems...");

    // Shutdown in reverse order of registration
    for (auto it = subsystems_.rbegin(); it != subsystems_.rend(); ++it)
    {
        auto& subsystem = *it;
        if (subsystem->is_initialized())
        {
            LOG_DEBUG(general, "Shutting down subsystem: {}", subsystem->name());
            try
            {
                subsystem->shutdown();
                subsystem->set_initialized(false);
            }
            catch (const std::exception& e)
            {
                LOG_ERROR(general, "Error shutting down subsystem {}: {}", subsystem->name(), e.what());
            }
        }
    }

    LOG_INFO(general, "All subsystems shut down");
}

void subsystem_manager::clear_all()
{
    shutdown_all();
    subsystems_.clear();
    subsystem_map_.clear();
}

void subsystem_manager::update_all(float delta_time)
{
    for (auto& subsystem : subsystems_)
    {
        if (subsystem->is_initialized())
        {
            subsystem->update(delta_time);
        }
    }
}

// Global subsystem manager singleton
auto subsystems() -> subsystem_manager&
{
    static subsystem_manager instance;
    return instance;
}

} // namespace hb
