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
    if (!populate_from_template(*new_item, info.template_id))
    {
        return result<item_id, std::string>::err("Invalid template ID");
    }

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

auto item_system::restore_item(item_id explicit_id, const item_create_info& info) -> result<item_id, std::string>
{
    if (items_.size() >= config_.max_items)
    {
        return result<item_id, std::string>::err("Maximum item count reached");
    }

    if (items_.contains(explicit_id))
    {
        return result<item_id, std::string>::err("Item ID already exists");
    }

    auto new_item = std::make_unique<item>();
    new_item->id = explicit_id;
    new_item->template_id = info.template_id;
    new_item->count = info.count;
    new_item->owner = info.owner;

    if (!populate_from_template(*new_item, info.template_id))
    {
        return result<item_id, std::string>::err("Invalid template ID");
    }

    if (info.full_durability)
    {
        new_item->durability = new_item->max_durability;
    }

    if (info.attribute.has_value())
    {
        new_item->attribute = info.attribute.value();
    }

    // Advance next_id_ past this explicit ID to prevent collisions
    if (explicit_id.value >= next_id_)
    {
        next_id_ = explicit_id.value + 1;
    }

    items_[explicit_id] = std::move(new_item);

    LOG_DEBUG(general, "Restored item {} (template: {}, count: {})", explicit_id.value, info.template_id.value, info.count);

    return result<item_id, std::string>::ok(explicit_id);
}

void item_system::seed_next_id(uint32_t max_existing_id)
{
    if (max_existing_id >= next_id_)
    {
        next_id_ = max_existing_id + 1;
    }
    LOG_INFO(general, "Item system next_id seeded to {}", next_id_);
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

auto item_system::populate_from_template(item& itm, item_id template_id) -> bool
{
    auto* registry = subsystems().get<item_registry>();
    if (!registry)
    {
        LOG_ERROR(general, "Item registry not available -- cannot create item with template {}", template_id.value);
        return false;
    }

    auto* tmpl = registry->get(template_id);
    if (!tmpl)
    {
        LOG_ERROR(general, "Item template {} not found -- refusing to create invalid item", template_id.value);
        return false;
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
    else if (equip_val == static_cast<uint8_t>(hb::item_equip_pos::ring_left))
    {
        itm.equip_position = equip_pos::ring_left;
    }
    else if (equip_val == static_cast<uint8_t>(hb::item_equip_pos::ring_right))
    {
        itm.equip_position = equip_pos::ring_right;
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

    // Direct-mapped fields
    itm.weight = tmpl->weight;
    itm.price = tmpl->price;
    itm.level_requirement = tmpl->level_limit;
    itm.max_durability = tmpl->durability;
    itm.durability = tmpl->durability;
    itm.indestructible = (tmpl->durability <= 0);
    itm.color = tmpl->item_color;
    itm.sprite = tmpl->sprite;
    itm.sprite_frame = tmpl->sprite_frame;

    // Interpret effect_type + effect_value1-6 based on legacy Item.cfg format.
    // See docs/legacy/06_item_system.md for field documentation.
    //
    // Legacy effect_type constants:
    //   1  = ATTACK (melee weapons): v1-v3 = dice/sides/bonus (S/M), v4-v6 = (L)
    //   2  = DEFENSE (armor/shields): v1 = primary defense, v2 = secondary defense
    //   3  = ATTACK_ARROW (bows): same layout as ATTACK
    //   14 = ADDEFFECT (rings/necklaces): v1 = effect subtype, v2 = magnitude
    //   24 = ATTACK_SPECABLTY (special ability weapons): same as ATTACK
    //   4-7, 9, 11 = consumable types (interpreted at use-time, not here)

    switch (tmpl->effect_type)
    {
    case 1:  // ATTACK — melee weapons
    case 3:  // ATTACK_ARROW — bows
    case 13: // ATTACK_MANASAVE — attack + mana save
    case 19: // Blood weapons (life drain) — same dice layout + special_effect
    case 20: // Wand MShield — same dice layout
    case 24: // ATTACK_SPECABLTY — special ability weapons
    {
        // Small/Medium target damage: iDice(v1, v2) + v3
        auto dice = tmpl->effect_value1;
        auto sides = tmpl->effect_value2;
        auto bonus = tmpl->effect_value3;

        if (sides > 0)
        {
            itm.damage_min = static_cast<int16_t>(dice + bonus);
            itm.damage_max = static_cast<int16_t>(dice * sides + bonus);
            itm.attack_power = static_cast<int16_t>(dice * ((sides + 1) / 2) + bonus);
        }
        else
        {
            itm.damage_min = bonus;
            itm.damage_max = bonus;
            itm.attack_power = bonus;
        }
        break;
    }
    case 2:  // DEFENSE — armor, shields, capes
    case 25: // Merien defense items — defense + special ability
    {
        itm.defense = static_cast<int16_t>(tmpl->effect_value1 + tmpl->effect_value2);
        break;
    }
    case 14: // ADDEFFECT — passive stat items (rings, necklaces)
    {
        // v1 = effect subtype, v2 = magnitude
        size_t eidx = 0;
        auto add_eff = [&](item_effect_type t, int16_t v)
        {
            if (v != 0 && eidx < itm.effects.size())
            {
                itm.effects[eidx].type = t;
                itm.effects[eidx].value = v;
                ++eidx;
            }
        };

        switch (tmpl->effect_value1)
        {
        case 1:  add_eff(item_effect_type::magic_defense_bonus, static_cast<int16_t>(tmpl->effect_value2 * 7)); break;
        case 2:  add_eff(item_effect_type::mp_bonus, tmpl->effect_value2); break;
        case 3:  add_eff(item_effect_type::attack_bonus, tmpl->effect_value2); break;
        case 4:  add_eff(item_effect_type::magic_attack_bonus, tmpl->effect_value2); break;
        case 5:  break; // Lucky effect (flag only, no stat)
        case 6:  add_eff(item_effect_type::mag_bonus, tmpl->effect_value2); break;
        case 11: add_eff(item_effect_type::hp_bonus, tmpl->effect_value2); break;
        default: break;
        }
        break;
    }
    case 15: // MAGICDAMAGESAVE — magic absorption rings
    {
        // No combat stats to populate — effect applied at damage calculation time
        break;
    }
    case 0: // NONE — arrows, crafting materials, misc
    {
        // Arrows have damage dice in v1-v3 (same format as weapons)
        if (tmpl->type == hb::item_type::arrow && tmpl->effect_value2 > 0)
        {
            auto dice = tmpl->effect_value1;
            auto sides = tmpl->effect_value2;
            auto bonus = tmpl->effect_value3;
            itm.damage_min = static_cast<int16_t>(dice + bonus);
            itm.damage_max = static_cast<int16_t>(dice * sides + bonus);
            itm.attack_power = static_cast<int16_t>(dice * ((sides + 1) / 2) + bonus);
        }
        break;
    }
    default:
        // Consumable types (4-7, 9, 11, 12, 17, 18, 22, 28) interpreted at use-time.
        // Misc types (10, 16, 26) have no combat stats.
        break;
    }

    // Hit ratio bonuses from special_effect_value fields
    if (tmpl->special_effect_value1 != 0)
    {
        size_t eidx = 0;
        while (eidx < itm.effects.size() && !itm.effects[eidx].is_empty()) ++eidx;
        if (eidx < itm.effects.size())
        {
            itm.effects[eidx].type = item_effect_type::hit_bonus;
            itm.effects[eidx].value = tmpl->special_effect_value1;
        }
    }

    // Flags
    if (tmpl->type == hb::item_type::arrow)
    {
        itm.max_stack = 9999;
        itm.stackable = true;
    }
    else
    {
        itm.max_stack = 1;
        itm.stackable = false;
    }
    itm.tradeable = true;
    itm.droppable = true;
    itm.two_handed = (tmpl->equip_pos == hb::item_equip_pos::two_hand);

    LOG_DEBUG(general, "Populated item from template: {} ({})", itm.name, template_id.value);
    return true;
}

} // namespace hb::item
