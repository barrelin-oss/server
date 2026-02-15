// event_bus.cpp
// Event bus implementation

#include "core/event_bus.h"

namespace hb
{

// Global event bus singleton implementation
auto global_event_bus() -> event_bus&
{
    static event_bus instance;
    return instance;
}

} // namespace hb
