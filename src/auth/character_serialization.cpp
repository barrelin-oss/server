// character_serialization.cpp
// Serialization of character data for database storage

#include "auth/character_serialization.h"
#include "core/logger.h"

namespace hb::auth {

auto serialize_skills(const skill::player_skills& skills) -> std::string {
    nlohmann::json j = nlohmann::json::array();

    for (size_t i = 0; i < skill::max_skills; ++i) {
        const auto& skill = skills.skills[i];
        // Only serialize skills that have been trained (level > 0 or exp > 0)
        if (skill.level > 0 || skill.experience > 0) {
            j.push_back({
                {"type", static_cast<int>(skill.type)},
                {"level", skill.level},
                {"exp", skill.experience}
            });
        }
    }

    return j.dump();
}

auto deserialize_skills(const std::string& json_str) -> skill::player_skills {
    skill::player_skills skills;

    if (json_str.empty()) {
        return skills;
    }

    // Check for valid JSON start characters (array or object)
    // Skip data that looks like BYTEA hex format (\x...) or other invalid formats
    if (json_str[0] != '[' && json_str[0] != '{') {
        LOG_DEBUG(auth, "Skills data is not valid JSON (starts with '{}')", json_str[0]);
        return skills;
    }

    try {
        auto j = nlohmann::json::parse(json_str);

        if (!j.is_array()) {
            LOG_WARN(auth, "Invalid skills data format: not an array");
            return skills;
        }

        for (const auto& skill_obj : j) {
            if (!skill_obj.contains("type") || !skill_obj.contains("level")) {
                continue;
            }

            auto type_idx = skill_obj["type"].get<int>();
            if (type_idx < 0 || type_idx >= static_cast<int>(skill::max_skills)) {
                continue;
            }

            auto type = static_cast<skill::skill_type>(type_idx);
            skills.get(type).level = skill_obj["level"].get<int16_t>();
            if (skill_obj.contains("exp")) {
                skills.get(type).experience = skill_obj["exp"].get<int32_t>();
            }
        }
    } catch (const nlohmann::json::exception& e) {
        LOG_WARN(auth, "Failed to parse skills data: {}", e.what());
    }

    return skills;
}

auto serialize_equipment(const player::equipment_state& equipment) -> std::string {
    nlohmann::json j = nlohmann::json::array();

    for (size_t i = 0; i < player::equip_slot_count; ++i) {
        const auto& item = equipment.slots[i];
        if (!item.is_empty()) {
            j.push_back({
                {"slot", static_cast<int>(i)},
                {"item_id", item.id.value},
                {"durability", item.durability},
                {"max_durability", item.max_durability}
            });
        }
    }

    return j.dump();
}

auto deserialize_equipment(const std::string& json_str) -> player::equipment_state {
    player::equipment_state equipment;

    if (json_str.empty()) {
        return equipment;
    }

    // Check for valid JSON start characters (array or object)
    // Skip data that looks like BYTEA hex format (\x...) or other invalid formats
    if (json_str[0] != '[' && json_str[0] != '{') {
        LOG_DEBUG(auth, "Equipment data is not valid JSON (starts with '{}')", json_str[0]);
        return equipment;
    }

    try {
        auto j = nlohmann::json::parse(json_str);

        if (!j.is_array()) {
            LOG_WARN(auth, "Invalid equipment data format: not an array");
            return equipment;
        }

        for (const auto& item_obj : j) {
            if (!item_obj.contains("slot") || !item_obj.contains("item_id")) {
                continue;
            }

            auto slot_idx = item_obj["slot"].get<int>();
            if (slot_idx < 0 || slot_idx >= static_cast<int>(player::equip_slot_count)) {
                continue;
            }

            auto& slot = equipment.slots[static_cast<size_t>(slot_idx)];
            slot.id = item_id{item_obj["item_id"].get<uint32_t>()};
            slot.durability = item_obj.value("durability", static_cast<uint16_t>(100));
            slot.max_durability = item_obj.value("max_durability", static_cast<uint16_t>(100));
        }
    } catch (const nlohmann::json::exception& e) {
        LOG_WARN(auth, "Failed to parse equipment data: {}", e.what());
    }

    return equipment;
}

auto serialize_inventory(const inventory::inventory& inv) -> std::string {
    nlohmann::json j = nlohmann::json::array();

    for (int16_t i = 0; i < inv.capacity(); ++i) {
        const auto* slot = inv.get_slot(i);
        if (slot && !slot->is_empty()) {
            j.push_back({
                {"slot", i},
                {"item_id", slot->item.value},
                {"count", slot->count}
            });
        }
    }

    return j.dump();
}

void deserialize_inventory(const std::string& json_str, inventory::inventory& inv) {
    inv.clear_all();

    if (json_str.empty()) {
        return;
    }

    // Check for valid JSON start characters (array or object)
    // Skip data that looks like BYTEA hex format (\x...) or other invalid formats
    if (json_str[0] != '[' && json_str[0] != '{') {
        LOG_DEBUG(auth, "Inventory data is not valid JSON (starts with '{}')", json_str[0]);
        return;
    }

    try {
        auto j = nlohmann::json::parse(json_str);

        if (!j.is_array()) {
            LOG_WARN(auth, "Invalid inventory data format: not an array");
            return;
        }

        for (const auto& item_obj : j) {
            if (!item_obj.contains("slot") || !item_obj.contains("item_id")) {
                continue;
            }

            auto slot_idx = item_obj["slot"].get<int16_t>();
            auto* slot = inv.get_slot(slot_idx);
            if (!slot) {
                continue;
            }

            slot->item = item_id{item_obj["item_id"].get<uint32_t>()};
            slot->count = item_obj.value("count", static_cast<int16_t>(1));
        }
    } catch (const nlohmann::json::exception& e) {
        LOG_WARN(auth, "Failed to parse inventory data: {}", e.what());
    }
}

auto get_inventory_data(const inventory::inventory& inv) -> std::vector<inventory_slot_data> {
    std::vector<inventory_slot_data> data;

    for (int16_t i = 0; i < inv.capacity(); ++i) {
        const auto* slot = inv.get_slot(i);
        if (slot && !slot->is_empty()) {
            data.push_back({
                .slot = i,
                .item_id = slot->item.value,
                .count = slot->count
            });
        }
    }

    return data;
}

}  // namespace hb::auth
