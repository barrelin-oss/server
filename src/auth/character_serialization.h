#pragma once

// character_serialization.h
// Serialization of character data for database storage

#include "core/types.h"
#include "skill/skill.h"
#include "player/equipment.h"
#include "inventory/inventory.h"
#include "magic/spell.h"
#include "quest/quest.h"

#include <nlohmann/json.hpp>
#include <string>
#include <vector>

namespace hb::auth {

// Serialize player skills to JSON string
[[nodiscard]] auto serialize_skills(const skill::player_skills& skills) -> std::string;

// Deserialize player skills from JSON string
[[nodiscard]] auto deserialize_skills(const std::string& json_str) -> skill::player_skills;

// Serialize equipment to JSON string
[[nodiscard]] auto serialize_equipment(const player::equipment_state& equipment) -> std::string;

// Deserialize equipment from JSON string
[[nodiscard]] auto deserialize_equipment(const std::string& json_str) -> player::equipment_state;

// Inventory slot data for serialization
struct inventory_slot_data {
    int16_t slot{0};
    uint32_t item_id{0};
    int16_t count{0};
};

// Serialize inventory to JSON string
[[nodiscard]] auto serialize_inventory(const inventory::inventory& inv) -> std::string;

// Deserialize inventory from JSON string and populate inventory
void deserialize_inventory(const std::string& json_str, inventory::inventory& inv);

// Get inventory as vector of slot data (for sending to client)
[[nodiscard]] auto get_inventory_data(const inventory::inventory& inv) -> std::vector<inventory_slot_data>;

// Serialize player spell knowledge to JSON string
[[nodiscard]] auto serialize_magic(const std::vector<magic::spell_knowledge>& spells) -> std::string;

// Deserialize player spell knowledge from JSON string
[[nodiscard]] auto deserialize_magic(const std::string& json_str) -> std::vector<magic::spell_knowledge>;

// Serialize quest journal to JSON string
[[nodiscard]] auto serialize_quests(const quest::quest_journal& journal) -> std::string;

// Deserialize quest journal from JSON string
[[nodiscard]] auto deserialize_quests(const std::string& json_str) -> quest::quest_journal;

}  // namespace hb::auth
