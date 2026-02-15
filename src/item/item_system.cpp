// item_system.cpp
// Item instance management implementation

#include "item/item_system.h"
#include "core/logger.h"
#include "core/subsystem.h"
#include "registry/item_registry.h"

namespace hb::item
{

item_system::item_system() = default;

item_system::~item_system()
{
    if (is_initialized())
    {
        shutdown();
    }
}

void item_system::initialize()
{
    LOG_INFO(general, "Item system initializing...");
    set_initialized(true);
    LOG_INFO(general, "Item system initialized (max_items: {})", config_.max_items);
}

void item_system::shutdown()
{
    LOG_INFO(general, "Item system shutting down...");
    items_.clear();
    set_initialized(false);
    LOG_INFO(general, "Item system shutdown complete");
}

void item_system::update(float delta_time)
{
    if (config_.enable_durability_decay)
    {
        update_durability_decay(delta_time);
    }
}

void item_system::set_config(const item_system_config& config)
{
    config_ = config;
}

auto item_system::create_item(const item_create_info& info) -> result<item_id, std::string>
{
    if (items_.size() >= config_.max_items)
    {
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

    if (info.full_durability)
    {
        new_item->durability = new_item->max_durability;
    }

    // Apply pre-set attribute if provided (from persistence, crafting, loot generation)
    if (info.attribute.has_value())
    {
        new_item->attribute = info.attribute.value();
    }

    items_[id] = std::move(new_item);

    LOG_DEBUG(general, "Created item {} (template: {}, count: {})", id.value, info.template_id.value, info.count);

    return result<item_id, std::string>::ok(id);
}

auto item_system::create_from_template(item_id template_id, int16_t count) -> result<item_id, std::string>
{
    item_create_info info;
    info.template_id = template_id;
    info.count = count;
    return create_item(info);
}

void item_system::destroy_item(item_id id)
{
    auto it = items_.find(id);
    if (it == items_.end())
        return;

    LOG_DEBUG(general, "Destroyed item {}", id.value);
    items_.erase(it);
}

auto item_system::get_item(item_id id) -> item*
{
    auto it = items_.find(id);
    return it != items_.end() ? it->second.get() : nullptr;
}

auto item_system::get_item(item_id id) const -> const item*
{
    auto it = items_.find(id);
    return it != items_.end() ? it->second.get() : nullptr;
}

void item_system::set_owner(item_id id, entity_id owner)
{
    auto* itm = get_item(id);
    if (itm)
    {
        itm->owner = owner;
    }
}

void item_system::clear_owner(item_id id)
{
    auto* itm = get_item(id);
    if (itm)
    {
        itm->owner = entity_id{};
    }
}

auto item_system::get_items_owned_by(entity_id owner) const -> std::vector<item_id>
{
    std::vector<item_id> result;
    for (const auto& [id, item_ptr] : items_)
    {
        if (item_ptr->owner == owner)
        {
            result.push_back(id);
        }
    }
    return result;
}

auto item_system::try_stack(item_id dest_id, item_id source_id) -> bool
{
    auto* dest = get_item(dest_id);
    auto* source = get_item(source_id);

    if (!dest || !source)
        return false;
    if (!dest->can_stack_with(*source))
        return false;

    int16_t stacked = dest->stack(*source);
    if (source->count <= 0)
    {
        destroy_item(source_id);
    }

    return stacked > 0;
}

auto item_system::split_item(item_id id, int16_t count) -> result<item_id, std::string>
{
    auto* itm = get_item(id);
    if (!itm)
    {
        return result<item_id, std::string>::err("Item not found");
    }

    if (count <= 0 || count >= itm->count)
    {
        return result<item_id, std::string>::err("Invalid split count");
    }

    if (items_.size() >= config_.max_items)
    {
        return result<item_id, std::string>::err("Maximum item count reached");
    }

    auto split_item_data = itm->split(count);
    auto new_id = next_item_id();
    split_item_data.id = new_id;

    items_[new_id] = std::make_unique<item>(std::move(split_item_data));

    return result<item_id, std::string>::ok(new_id);
}

void item_system::damage_durability(item_id id, int16_t amount)
{
    auto* itm = get_item(id);
    if (itm)
    {
        itm->damage_durability(amount);
    }
}

void item_system::repair_item(item_id id, int16_t amount)
{
    auto* itm = get_item(id);
    if (itm)
    {
        itm->repair(amount);
    }
}

void item_system::repair_item_full(item_id id)
{
    auto* itm = get_item(id);
    if (itm)
    {
        itm->repair_full();
    }
}

void item_system::update_durability_decay(float delta_time)
{
    decay_accumulator_ += delta_time * 1000.0f;

    if (decay_accumulator_ < static_cast<float>(config_.decay_check_interval_ms))
    {
        return;
    }

    decay_accumulator_ -= static_cast<float>(config_.decay_check_interval_ms);

    // Durability decay would be handled per-item based on usage
    // This is just a placeholder for time-based decay
}

void item_system::populate_from_template(item& itm, item_id template_id)
{
    auto* registry = subsystems().get<item_registry>();
    if (!registry)
    {
        LOG_WARN(general, "Item registry not available, using defaults for item {}", template_id.value);
        itm.name = "Unknown Item";
        itm.type = item_type::consumable;
        itm.max_stack = 99;
        itm.stackable = true;
        itm.weight = 1;
        itm.price = 10;
        itm.max_durability = 100;
        itm.durability = 100;
        return;
    }

    auto* tmpl = registry->get(template_id);
    if (!tmpl)
    {
        LOG_WARN(general, "Item template {} not found, using defaults", template_id.value);
        itm.name = "Unknown Item";
        itm.type = item_type::consumable;
        itm.max_stack = 99;
        itm.stackable = true;
        itm.weight = 1;
        itm.price = 10;
        itm.max_durability = 100;
        itm.durability = 100;
        return;
    }

    // Copy template data to item instance
    itm.name = tmpl->name;

    // Map template type to item type using numeric comparison
    auto tmpl_type_val = static_cast<int8_t>(tmpl->type);
    if (tmpl_type_val == static_cast<int8_t>(hb::item_type::weapon))
    {
        itm.type = item_type::weapon;
    }
    else if (tmpl_type_val == static_cast<int8_t>(hb::item_type::armor))
    {
        itm.type = item_type::armor;
    }
    else if (tmpl_type_val == static_cast<int8_t>(hb::item_type::accessory))
    {
        itm.type = item_type::accessory;
    }
    else if (tmpl_type_val == static_cast<int8_t>(hb::item_type::potion) ||
             tmpl_type_val == static_cast<int8_t>(hb::item_type::scroll) ||
             tmpl_type_val == static_cast<int8_t>(hb::item_type::eat) ||
             tmpl_type_val == static_cast<int8_t>(hb::item_type::consume))
    {
        itm.type = item_type::consumable;
    }
    else if (tmpl_type_val == static_cast<int8_t>(hb::item_type::material))
    {
        itm.type = item_type::material;
    }
    else
    {
        itm.type = item_type::none;
    }

    // Map equip position using numeric comparison
    auto equip_val = static_cast<uint8_t>(tmpl->equip_pos);
    if (equip_val == static_cast<uint8_t>(hb::item_equip_pos::head))
    {
        itm.equip_position = equip_pos::head;
    }
    else if (equip_val == static_cast<uint8_t>(hb::item_equip_pos::body))
    {
        itm.equip_position = equip_pos::body;
    }
    else if (equip_val == static_cast<uint8_t>(hb::item_equip_pos::arms))
    {
        itm.equip_position = equip_pos::arms;
    }
    else if (equip_val == static_cast<uint8_t>(hb::item_equip_pos::pants))
    {
        itm.equip_position = equip_pos::pants;
    }
    else if (equip_val == static_cast<uint8_t>(hb::item_equip_pos::boots))
    {
        itm.equip_position = equip_pos::boots;
    }
    else if (equip_val == static_cast<uint8_t>(hb::item_equip_pos::left_hand))
    {
        itm.equip_position = equip_pos::shield;
    }
    else if (equip_val == static_cast<uint8_t>(hb::item_equip_pos::right_hand))
    {
        itm.equip_position = equip_pos::weapon;
    }
    else if (equip_val == static_cast<uint8_t>(hb::item_equip_pos::two_hand))
    {
        itm.equip_position = equip_pos::twohand;
    }
    else if (equip_val == static_cast<uint8_t>(hb::item_equip_pos::ring_left) ||
             equip_val == static_cast<uint8_t>(hb::item_equip_pos::ring_right))
    {
        itm.equip_position = equip_pos::ring;
    }
    else if (equip_val == static_cast<uint8_t>(hb::item_equip_pos::neck))
    {
        itm.equip_position = equip_pos::amulet;
    }
    else if (equip_val == static_cast<uint8_t>(hb::item_equip_pos::back))
    {
        itm.equip_position = equip_pos::cape;
    }
    else
    {
        itm.equip_position = equip_pos::none;
    }

    // Basic properties
    itm.weight = tmpl->weight;
    itm.price = tmpl->price;
    itm.level_requirement = tmpl->level_limit;

    // Combat stats - calculate attack power from dice roll average
    if (tmpl->attack_sides > 0)
    {
        itm.attack_power =
            static_cast<int16_t>(tmpl->attack_dice * ((tmpl->attack_sides + 1) / 2) + tmpl->attack_bonus);
    }
    else
    {
        itm.attack_power = tmpl->attack_bonus;
    }
    itm.magic_power = tmpl->magic_power;
    itm.defense = tmpl->defense;
    itm.magic_defense = 0; // Template doesn't have this, default to 0

    // Requirements
    itm.str_requirement = tmpl->str_req;
    itm.dex_requirement = tmpl->dex_req;
    itm.int_requirement = tmpl->int_req;
    itm.mag_requirement = tmpl->mag_req;

    // Durability
    itm.max_durability = tmpl->max_durability;
    itm.durability = tmpl->max_durability;
    itm.indestructible = (tmpl->max_durability <= 0);

    // Stacking
    itm.max_stack = tmpl->max_stack;
    itm.stackable = tmpl->is_stackable;

    // Flags
    itm.tradeable = tmpl->is_tradeable;
    itm.droppable = tmpl->is_droppable;
    itm.two_handed = (itm.equip_position == equip_pos::twohand);

    // Audit flag: equipment audited by default, override per-template
    if (tmpl->audit_override.has_value())
    {
        itm.audited = *tmpl->audit_override;
    }
    else
    {
        itm.audited = itm.is_equipment();
    }

    // Apply stat bonuses from template as effects
    size_t effect_idx = 0;
    auto add_effect = [&](item_effect_type type, int16_t value)
    {
        if (value == 0 || effect_idx >= itm.effects.size())
            return;
        itm.effects[effect_idx].type = type;
        itm.effects[effect_idx].value = value;
        ++effect_idx;
    };

    add_effect(item_effect_type::str_bonus, tmpl->str_bonus);
    add_effect(item_effect_type::dex_bonus, tmpl->dex_bonus);
    add_effect(item_effect_type::int_bonus, tmpl->int_bonus);
    add_effect(item_effect_type::mag_bonus, tmpl->mag_bonus);
    add_effect(item_effect_type::vit_bonus, tmpl->vit_bonus);
    add_effect(item_effect_type::chr_bonus, tmpl->cha_bonus);
    add_effect(item_effect_type::hp_bonus, tmpl->hp_bonus);
    add_effect(item_effect_type::mp_bonus, tmpl->mp_bonus);
    add_effect(item_effect_type::sp_bonus, tmpl->sp_bonus);
    add_effect(item_effect_type::hit_bonus, tmpl->hit_prob_bonus);
    add_effect(item_effect_type::dodge_bonus, tmpl->dodge_prob_bonus);

    LOG_DEBUG(general, "Populated item from template: {} ({})", itm.name, template_id.value);
}

} // namespace hb::item
