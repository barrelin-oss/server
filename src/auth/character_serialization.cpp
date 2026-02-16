// character_serialization.cpp
// Serialization of character data for database storage

#include "auth/character_serialization.h"
#include "core/logger.h"

namespace hb::auth
{

auto serialize_skills(const skill::player_skills& skills) -> std::string
{
    nlohmann::json j = nlohmann::json::array();

    for (size_t i = 0; i < skill::max_skills; ++i)
    {
        const auto& sk = skills.skills[i];
        if (sk.level > 0 || sk.total_uses > 0)
        {
            j.push_back({{"type", static_cast<int>(i)},
                         {"level", sk.level},
                         {"total_uses", sk.total_uses},
                         {"uses", sk.uses_this_level}});
        }
    }

    return j.dump();
}

auto deserialize_skills(const std::string& json_str) -> skill::player_skills
{
    skill::player_skills skills;

    if (json_str.empty())
    {
        return skills;
    }

    // Check for valid JSON start characters (array or object)
    // Skip data that looks like BYTEA hex format (\x...) or other invalid formats
    if (json_str[0] != '[' && json_str[0] != '{')
    {
        LOG_DEBUG(auth, "Skills data is not valid JSON (starts with '{}')", json_str[0]);
        return skills;
    }

    try
    {
        auto j = nlohmann::json::parse(json_str);

        if (!j.is_array())
        {
            LOG_WARN(auth, "Invalid skills data format: not an array");
            return skills;
        }

        for (const auto& skill_obj : j)
        {
            if (!skill_obj.contains("type") || !skill_obj.contains("level"))
            {
                continue;
            }

            auto type_idx = skill_obj["type"].get<int>();
            if (type_idx < 0 || type_idx >= static_cast<int>(skill::max_skills))
            {
                continue;
            }

            auto type = static_cast<skill::skill_type>(type_idx);
            auto& sk = skills.get(type);
            sk.level = skill_obj["level"].get<int16_t>();
            sk.total_uses = skill_obj.value("total_uses", static_cast<int32_t>(0));
            sk.uses_this_level = skill_obj.value("uses", static_cast<int32_t>(0));
        }
    }
    catch (const nlohmann::json::exception& e)
    {
        LOG_WARN(auth, "Failed to parse skills data: {}", e.what());
    }

    return skills;
}

auto serialize_magic(const std::vector<magic::spell_knowledge>& spells) -> std::string
{
    nlohmann::json j = nlohmann::json::array();

    for (const auto& spell : spells)
    {
        j.push_back({{"spell_id", spell.spell.value}, {"level", spell.level}, {"total_casts", spell.total_casts}});
    }

    return j.dump();
}

auto deserialize_magic(const std::string& json_str) -> std::vector<magic::spell_knowledge>
{
    std::vector<magic::spell_knowledge> spells;

    if (json_str.empty())
    {
        return spells;
    }

    if (json_str[0] != '[' && json_str[0] != '{')
    {
        LOG_DEBUG(auth, "Magic data is not valid JSON (starts with '{}')", json_str[0]);
        return spells;
    }

    try
    {
        auto j = nlohmann::json::parse(json_str);

        if (!j.is_array())
        {
            LOG_WARN(auth, "Invalid magic data format: not an array");
            return spells;
        }

        for (const auto& obj : j)
        {
            if (!obj.contains("spell_id"))
            {
                continue;
            }

            magic::spell_knowledge knowledge;
            knowledge.spell = spell_id{obj["spell_id"].get<uint16_t>()};
            knowledge.level = obj.value("level", static_cast<int16_t>(1));
            knowledge.total_casts = obj.value("total_casts", 0);
            spells.push_back(knowledge);
        }
    }
    catch (const nlohmann::json::exception& e)
    {
        LOG_WARN(auth, "Failed to parse magic data: {}", e.what());
    }

    return spells;
}

auto serialize_quests(const quest::quest_journal& journal) -> std::string
{
    nlohmann::json j;

    // Serialize active quests
    nlohmann::json active = nlohmann::json::array();
    for (const auto& quest : journal.active_quests)
    {
        nlohmann::json quest_obj;
        quest_obj["quest_id"] = quest.template_id.value;
        quest_obj["status"] = static_cast<int>(quest.status);
        quest_obj["elapsed_seconds"] = quest.elapsed_seconds;
        quest_obj["time_limit_seconds"] = quest.time_limit_seconds;
        quest_obj["completion_count"] = quest.completion_count;

        nlohmann::json objectives = nlohmann::json::array();
        for (const auto& obj : quest.objectives)
        {
            objectives.push_back({{"id", obj.template_id},
                                  {"status", static_cast<int>(obj.status)},
                                  {"current", obj.current_count},
                                  {"required", obj.required_count},
                                  {"time_elapsed", obj.time_elapsed}});
        }
        quest_obj["objectives"] = objectives;
        active.push_back(quest_obj);
    }
    j["active"] = active;

    // Serialize completed quest IDs
    nlohmann::json completed = nlohmann::json::array();
    for (const auto& qid : journal.completed_quests)
    {
        completed.push_back(qid.value);
    }
    j["completed"] = completed;

    return j.dump();
}

auto deserialize_quests(const std::string& json_str) -> quest::quest_journal
{
    quest::quest_journal journal;

    if (json_str.empty())
    {
        return journal;
    }

    if (json_str[0] != '[' && json_str[0] != '{')
    {
        LOG_DEBUG(auth, "Quest data is not valid JSON (starts with '{}')", json_str[0]);
        return journal;
    }

    try
    {
        auto j = nlohmann::json::parse(json_str);

        // Handle empty array (default DB value)
        if (j.is_array() && j.empty())
        {
            return journal;
        }

        if (!j.is_object())
        {
            LOG_WARN(auth, "Invalid quest data format: not an object");
            return journal;
        }

        // Deserialize active quests
        if (j.contains("active") && j["active"].is_array())
        {
            for (const auto& quest_obj : j["active"])
            {
                if (!quest_obj.contains("quest_id"))
                    continue;

                quest::quest_state state;
                state.template_id = quest_id{quest_obj["quest_id"].get<uint16_t>()};
                state.status = static_cast<quest::quest_status>(quest_obj.value("status", 0));
                state.elapsed_seconds = quest_obj.value("elapsed_seconds", 0);
                state.time_limit_seconds = quest_obj.value("time_limit_seconds", 0);
                state.completion_count = quest_obj.value("completion_count", 0);

                if (quest_obj.contains("objectives") && quest_obj["objectives"].is_array())
                {
                    for (const auto& obj : quest_obj["objectives"])
                    {
                        quest::objective_state os;
                        os.template_id = obj.value("id", static_cast<uint16_t>(0));
                        os.status = static_cast<quest::objective_status>(obj.value("status", 0));
                        os.current_count = obj.value("current", 0);
                        os.required_count = obj.value("required", 1);
                        os.time_elapsed = obj.value("time_elapsed", 0);
                        state.objectives.push_back(os);
                    }
                }

                journal.active_quests.push_back(std::move(state));
            }
        }

        // Deserialize completed quest IDs
        if (j.contains("completed") && j["completed"].is_array())
        {
            for (const auto& qid : j["completed"])
            {
                journal.completed_quests.push_back(quest_id{qid.get<uint16_t>()});
            }
        }
    }
    catch (const nlohmann::json::exception& e)
    {
        LOG_WARN(auth, "Failed to parse quest data: {}", e.what());
    }

    return journal;
}

} // namespace hb::auth
