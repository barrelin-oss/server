// item_system.cpp
// Item instance management implementation

#include "item/item_system.h"
#include "core/logger.h"

namespace hb::item {

item_system::item_system() = default;

item_system::~item_system() {
    if (is_initialized()) {
        shutdown();
    }
}

void item_system::initialize() {
    LOG_INFO(general, "Item system initializing...");
    set_initialized(true);
    LOG_INFO(general, "Item system initialized (max_items: {})", config_.max_items);
}

void item_system::shutdown() {
    LOG_INFO(general, "Item system shutting down...");
    items_.clear();
    set_initialized(false);
    LOG_INFO(general, "Item system shutdown complete");
}

void item_system::update(float delta_time) {
    if (config_.enable_durability_decay) {
        update_durability_decay(delta_time);
    }
}

void item_system::set_config(const item_system_config& config) {
    config_ = config;
}

auto item_system::create_item(const item_create_info& info) -> result<item_id, std::string> {
    if (items_.size() >= config_.max_items) {
        return result<item_id, std::string>::err("Maximum item count reached");
    }

    auto id = next_item_id();
    auto new_item = std::make_unique<item>();

    new_item->id = id;
    new_item->template_id = info.template_id;
    new_item->count = info.count;
    new_item->owner = info.owner;

    // Populate from template
    populate_from_template(*new_item, info.template_id);

    if (info.full_durability) {
        new_item->durability = new_item->max_durability;
    }

    items_[id] = std::move(new_item);

    LOG_DEBUG(general, "Created item {} (template: {}, count: {})",
        id.value, info.template_id.value, info.count);

    return result<item_id, std::string>::ok(id);
}

auto item_system::create_from_template(item_id template_id, int16_t count)
    -> result<item_id, std::string>
{
    item_create_info info;
    info.template_id = template_id;
    info.count = count;
    return create_item(info);
}

void item_system::destroy_item(item_id id) {
    auto it = items_.find(id);
    if (it == items_.end()) return;

    LOG_DEBUG(general, "Destroyed item {}", id.value);
    items_.erase(it);
}

auto item_system::get_item(item_id id) -> item* {
    auto it = items_.find(id);
    return it != items_.end() ? it->second.get() : nullptr;
}

auto item_system::get_item(item_id id) const -> const item* {
    auto it = items_.find(id);
    return it != items_.end() ? it->second.get() : nullptr;
}

void item_system::set_owner(item_id id, entity_id owner) {
    auto* itm = get_item(id);
    if (itm) {
        itm->owner = owner;
    }
}

void item_system::clear_owner(item_id id) {
    auto* itm = get_item(id);
    if (itm) {
        itm->owner = entity_id{};
    }
}

auto item_system::get_items_owned_by(entity_id owner) const -> std::vector<item_id> {
    std::vector<item_id> result;
    for (const auto& [id, item_ptr] : items_) {
        if (item_ptr->owner == owner) {
            result.push_back(id);
        }
    }
    return result;
}

auto item_system::try_stack(item_id dest_id, item_id source_id) -> bool {
    auto* dest = get_item(dest_id);
    auto* source = get_item(source_id);

    if (!dest || !source) return false;
    if (!dest->can_stack_with(*source)) return false;

    int16_t stacked = dest->stack(*source);
    if (source->count <= 0) {
        destroy_item(source_id);
    }

    return stacked > 0;
}

auto item_system::split_item(item_id id, int16_t count) -> result<item_id, std::string> {
    auto* itm = get_item(id);
    if (!itm) {
        return result<item_id, std::string>::err("Item not found");
    }

    if (count <= 0 || count >= itm->count) {
        return result<item_id, std::string>::err("Invalid split count");
    }

    if (items_.size() >= config_.max_items) {
        return result<item_id, std::string>::err("Maximum item count reached");
    }

    auto split_item_data = itm->split(count);
    auto new_id = next_item_id();
    split_item_data.id = new_id;

    items_[new_id] = std::make_unique<item>(std::move(split_item_data));

    return result<item_id, std::string>::ok(new_id);
}

void item_system::damage_durability(item_id id, int16_t amount) {
    auto* itm = get_item(id);
    if (itm) {
        itm->damage_durability(amount);
    }
}

void item_system::repair_item(item_id id, int16_t amount) {
    auto* itm = get_item(id);
    if (itm) {
        itm->repair(amount);
    }
}

void item_system::repair_item_full(item_id id) {
    auto* itm = get_item(id);
    if (itm) {
        itm->repair_full();
    }
}

void item_system::update_durability_decay(float delta_time) {
    decay_accumulator_ += delta_time * 1000.0f;

    if (decay_accumulator_ < static_cast<float>(config_.decay_check_interval_ms)) {
        return;
    }

    decay_accumulator_ -= static_cast<float>(config_.decay_check_interval_ms);

    // Durability decay would be handled per-item based on usage
    // This is just a placeholder for time-based decay
}

void item_system::populate_from_template(item& itm, item_id template_id) {
    // This would query the item registry for template data
    // For now, set some defaults
    itm.name = "Item";
    itm.type = item_type::consumable;
    itm.max_stack = 99;
    itm.stackable = true;
    itm.weight = 1;
    itm.price = 10;
    itm.max_durability = 100;
    itm.durability = 100;
}

}  // namespace hb::item
