#pragma once

// quest.h
// Quest definitions and state

#include "core/types.h"
#include "quest/objective.h"
#include "quest/reward.h"

#include <string>
#include <vector>
#include <cstdint>
#include <chrono>

namespace hb::quest
{

// Quest status
enum class quest_status : uint8_t
{
    available = 0, // Can be accepted
    active = 1,    // In progress
    complete = 2,  // All objectives done, ready to turn in
    turned_in = 3, // Rewards claimed
    failed = 4,    // Quest failed
    abandoned = 5, // Player abandoned quest
};

// Quest type
enum class quest_type : uint8_t
{
    main_story = 0, // Main storyline quests
    side = 1,       // Side quests
    daily = 2,      // Daily repeatable quests
    weekly = 3,     // Weekly repeatable quests
    guild = 4,      // Guild quests
    event = 5,      // Special event quests
    tutorial = 6,   // Tutorial quests
};

// Quest difficulty
enum class quest_difficulty : uint8_t
{
    trivial = 0,
    easy = 1,
    normal = 2,
    hard = 3,
    heroic = 4,
    legendary = 5,
};

// Quest template (immutable definition)
struct quest_template
{
    quest_id id{};
    std::string name;
    std::string description;
    quest_type type{quest_type::side};
    quest_difficulty difficulty{quest_difficulty::normal};

    // Requirements
    int16_t min_level{1};
    int16_t max_level{200};
    std::vector<quest_id> prerequisite_quests; // Must complete these first
    uint8_t required_faction{0};               // 0 = any faction

    // Quest giver/turn-in
    npc_id quest_giver{};
    npc_id turn_in_npc{}; // 0 = same as quest_giver
    map_id quest_giver_map{};

    // Objectives
    std::vector<objective_template> objectives;

    // Rewards
    quest_rewards rewards;

    // Time limit (0 = no limit)
    int32_t time_limit_seconds{0};

    // Repeatable settings
    bool repeatable{false};
    int32_t cooldown_hours{0}; // Hours before can repeat

    // Flags
    bool auto_complete{false}; // Complete automatically when objectives done
    bool shareable{false};     // Can share with party members
    bool hidden{false};        // Don't show in quest log until accepted

    [[nodiscard]] auto is_timed() const -> bool { return time_limit_seconds > 0; }

    [[nodiscard]] auto is_repeatable() const -> bool { return repeatable; }

    [[nodiscard]] auto required_objective_count() const -> size_t
    {
        size_t count = 0;
        for (const auto& obj : objectives)
        {
            if (!obj.optional)
                ++count;
        }
        return count;
    }
};

// Quest state (mutable player progress)
struct quest_state
{
    quest_id template_id{};
    quest_status status{quest_status::available};
    std::vector<objective_state> objectives;

    // Timing
    std::chrono::system_clock::time_point accepted_time{};
    std::chrono::system_clock::time_point completed_time{};
    int32_t elapsed_seconds{0};
    int32_t time_limit_seconds{0};

    // For repeatable quests
    int32_t completion_count{0};
    std::chrono::system_clock::time_point last_completion{};

    [[nodiscard]] auto is_active() const -> bool { return status == quest_status::active; }

    [[nodiscard]] auto is_complete() const -> bool { return status == quest_status::complete; }

    [[nodiscard]] auto is_turned_in() const -> bool { return status == quest_status::turned_in; }

    [[nodiscard]] auto is_failed() const -> bool { return status == quest_status::failed; }

    [[nodiscard]] auto is_abandoned() const -> bool { return status == quest_status::abandoned; }

    // Check if all required objectives are complete
    [[nodiscard]] auto all_objectives_complete(const std::vector<objective_template>& templates) const -> bool
    {
        for (size_t i = 0; i < objectives.size() && i < templates.size(); ++i)
        {
            if (!templates[i].optional && !objectives[i].is_complete())
            {
                return false;
            }
        }
        return true;
    }

    // Get completion progress (0.0 - 1.0)
    [[nodiscard]] auto progress(const std::vector<objective_template>& templates) const -> float
    {
        if (objectives.empty())
            return status == quest_status::complete ? 1.0f : 0.0f;

        int required = 0;
        int completed = 0;

        for (size_t i = 0; i < objectives.size() && i < templates.size(); ++i)
        {
            if (!templates[i].optional)
            {
                ++required;
                if (objectives[i].is_complete())
                {
                    ++completed;
                }
            }
        }

        if (required == 0)
            return 1.0f;
        return static_cast<float>(completed) / static_cast<float>(required);
    }

    // Check if quest has timed out
    [[nodiscard]] auto has_timed_out() const -> bool
    {
        if (time_limit_seconds <= 0)
            return false;
        return elapsed_seconds >= time_limit_seconds;
    }

    // Get remaining time in seconds (0 if no limit or expired)
    [[nodiscard]] auto remaining_time() const -> int32_t
    {
        if (time_limit_seconds <= 0)
            return 0;
        int32_t remaining = time_limit_seconds - elapsed_seconds;
        return remaining > 0 ? remaining : 0;
    }

    // Initialize objectives from template
    void initialize_objectives(const std::vector<objective_template>& templates)
    {
        objectives.clear();
        objectives.reserve(templates.size());

        for (const auto& tmpl : templates)
        {
            objective_state state;
            state.template_id = tmpl.id;
            state.status = objective_status::incomplete;
            state.current_count = 0;

            // Set required count based on objective data type
            std::visit(
                [&state](const auto& data)
                {
                    using T = std::decay_t<decltype(data)>;
                    if constexpr (std::is_same_v<T, kill_objective_data>)
                    {
                        state.required_count = data.required_count;
                    }
                    else if constexpr (std::is_same_v<T, collect_objective_data>)
                    {
                        state.required_count = data.required_count;
                    }
                    else if constexpr (std::is_same_v<T, craft_objective_data>)
                    {
                        state.required_count = data.required_count;
                    }
                    else if constexpr (std::is_same_v<T, gather_objective_data>)
                    {
                        state.required_count = data.required_count;
                    }
                    else if constexpr (std::is_same_v<T, time_objective_data>)
                    {
                        state.required_count = data.duration_seconds;
                    }
                    else
                    {
                        state.required_count = 1; // Single completion objectives
                    }
                },
                tmpl.data);

            objectives.push_back(state);
        }
    }

    // Reset quest state for replay
    void reset()
    {
        status = quest_status::active;
        elapsed_seconds = 0;
        accepted_time = std::chrono::system_clock::now();

        for (auto& obj : objectives)
        {
            obj.reset();
        }
    }
};

// Player's quest journal
struct quest_journal
{
    static constexpr size_t max_active_quests = 25;

    std::vector<quest_state> active_quests;
    std::vector<quest_id> completed_quests; // History of turned-in quest IDs

    [[nodiscard]] auto active_count() const -> size_t { return active_quests.size(); }

    [[nodiscard]] auto can_accept_quest() const -> bool { return active_quests.size() < max_active_quests; }

    [[nodiscard]] auto has_active_quest(quest_id id) const -> bool
    {
        for (const auto& quest : active_quests)
        {
            if (quest.template_id == id && quest.is_active())
            {
                return true;
            }
        }
        return false;
    }

    [[nodiscard]] auto has_completed_quest(quest_id id) const -> bool
    {
        for (const auto& completed_id : completed_quests)
        {
            if (completed_id == id)
            {
                return true;
            }
        }
        return false;
    }

    [[nodiscard]] auto get_quest(quest_id id) -> quest_state*
    {
        for (auto& quest : active_quests)
        {
            if (quest.template_id == id)
            {
                return &quest;
            }
        }
        return nullptr;
    }

    [[nodiscard]] auto get_quest(quest_id id) const -> const quest_state*
    {
        for (const auto& quest : active_quests)
        {
            if (quest.template_id == id)
            {
                return &quest;
            }
        }
        return nullptr;
    }
};

} // namespace hb::quest
