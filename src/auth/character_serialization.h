#pragma once

// character_serialization.h
// Serialization of character data for database storage

#include "core/types.h"
#include "skill/skill.h"
#include "magic/spell.h"
#include "quest/quest.h"

#include <nlohmann/json.hpp>
#include <string>
#include <vector>

namespace hb::auth
{

// Serialize player skills to JSON string
[[nodiscard]] auto serialize_skills(const skill::player_skills& skills) -> std::string;

// Deserialize player skills from JSON string
[[nodiscard]] auto deserialize_skills(const std::string& json_str) -> skill::player_skills;

// Serialize player spell knowledge to JSON string
[[nodiscard]] auto serialize_magic(const std::vector<magic::spell_knowledge>& spells) -> std::string;

// Deserialize player spell knowledge from JSON string
[[nodiscard]] auto deserialize_magic(const std::string& json_str) -> std::vector<magic::spell_knowledge>;

// Serialize quest journal to JSON string
[[nodiscard]] auto serialize_quests(const quest::quest_journal& journal) -> std::string;

// Deserialize quest journal from JSON string
[[nodiscard]] auto deserialize_quests(const std::string& json_str) -> quest::quest_journal;

} // namespace hb::auth
