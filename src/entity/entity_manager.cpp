// entity_manager.cpp
// Entity lifecycle management implementation

#include "entity/entity_manager.h"
#include "core/logger.h"

namespace hb::entity {

entity_manager::entity_manager() = default;

entity_manager::~entity_manager() {
    if (is_initialized()) {
        shutdown();
    }
}

void entity_manager::initialize() {
    LOG_INFO(general, "Entity manager initializing...");

    // Pre-allocate arrays
    generations_.resize(config_.initial_capacity, 0);
    alive_.resize(config_.initial_capacity, false);
    types_.resize(config_.initial_capacity, entity_type::none);

    set_initialized(true);
    LOG_INFO(general, "Entity manager initialized (capacity: {})", config_.initial_capacity);
}

void entity_manager::shutdown() {
    LOG_INFO(general, "Entity manager shutting down...");

    // Clear all storages
    storages_.clear();

    // Clear entity tracking
    generations_.clear();
    alive_.clear();
    types_.clear();
    while (!free_indices_.empty()) free_indices_.pop();
    next_index_ = 1;
    alive_count_ = 0;

    set_initialized(false);
    LOG_INFO(general, "Entity manager shutdown complete");
}

void entity_manager::set_config(const entity_manager_config& config) {
    config_ = config;
}

auto entity_manager::create() -> entity {
    return create(entity_type::none);
}

auto entity_manager::create(entity_type type) -> entity {
    uint32_t index;

    if (!free_indices_.empty()) {
        // Recycle an index
        index = free_indices_.front();
        free_indices_.pop();
    } else {
        // Use fresh index
        if (next_index_ >= config_.max_entity_count) {
            LOG_ERROR(general, "Entity limit reached ({})", config_.max_entity_count);
            return entity::null();
        }
        index = next_index_++;

        // Ensure arrays are large enough
        if (index >= generations_.size()) {
            auto new_size = std::min(static_cast<size_t>(index * 2), static_cast<size_t>(config_.max_entity_count));
            generations_.resize(new_size, 0);
            alive_.resize(new_size, false);
            types_.resize(new_size, entity_type::none);
        }
    }

    // Mark as alive
    alive_[index] = true;
    types_[index] = type;
    ++alive_count_;

    entity e{index, generations_[index]};
    LOG_DEBUG(general, "Created entity {} (type={}, gen={})",
        e.index(), static_cast<int>(type), e.generation());

    return e;
}

void entity_manager::destroy(entity e) {
    if (!is_alive(e)) return;

    auto index = e.index();

    // Remove from all component storages
    for (auto& [type_id, storage] : storages_) {
        storage->remove(e);
    }

    // Mark as dead and increment generation
    alive_[index] = false;
    types_[index] = entity_type::none;
    generations_[index] = static_cast<uint8_t>((generations_[index] + 1) & 0xFF);
    --alive_count_;

    // Add to free list for recycling
    free_indices_.push(index);

    LOG_DEBUG(general, "Destroyed entity {} (new gen={})", index, generations_[index]);
}

auto entity_manager::is_alive(entity e) const -> bool {
    if (!e.is_valid()) return false;
    auto index = e.index();
    if (index >= alive_.size()) return false;
    return alive_[index] && generations_[index] == e.generation();
}

auto entity_manager::get_type(entity e) const -> entity_type {
    if (!is_alive(e)) return entity_type::none;
    return types_[e.index()];
}

auto entity_manager::entity_count() const -> size_t {
    return alive_count_;
}

auto entity_manager::generation_at(uint32_t index) const -> uint8_t {
    if (index >= generations_.size()) return 0;
    return generations_[index];
}

}  // namespace hb::entity
