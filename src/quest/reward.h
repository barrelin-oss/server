#pragma once

// reward.h
// Quest reward definitions

#include "core/types.h"

#include <string>
#include <vector>
#include <cstdint>

namespace hb::quest {

// Reward types
enum class reward_type : uint8_t {
    experience = 0,     // Character experience
    gold = 1,           // Gold currency
    item = 2,           // Item reward
    skill_exp = 3,      // Skill experience
    reputation = 4,     // Faction reputation
    title = 5,          // Character title
    unlock = 6,         // Unlock content (map, feature, etc.)
};

// Item reward
struct item_reward {
    item_id item_type{};
    int16_t count{1};
    int16_t quality{0};     // 0 = standard, higher = better attributes
};

// Skill experience reward
struct skill_exp_reward {
    uint8_t skill_type{0};
    int32_t amount{0};
};

// Reputation reward
struct reputation_reward {
    uint8_t faction_id{0};
    int32_t amount{0};      // Can be negative for reputation loss
};

// Quest rewards container
struct quest_rewards {
    int64_t experience{0};
    int64_t gold{0};
    std::vector<item_reward> items;
    std::vector<skill_exp_reward> skill_experience;
    std::vector<reputation_reward> reputation;
    std::string title;                  // Empty = no title reward
    uint32_t unlock_flags{0};           // Bit flags for unlockable content

    [[nodiscard]] auto has_rewards() const -> bool {
        return experience > 0 ||
               gold > 0 ||
               !items.empty() ||
               !skill_experience.empty() ||
               !reputation.empty() ||
               !title.empty() ||
               unlock_flags != 0;
    }

    [[nodiscard]] auto item_count() const -> size_t {
        return items.size();
    }
};

// Reward multiplier for quest difficulty or player performance
struct reward_multiplier {
    float experience_mult{1.0f};
    float gold_mult{1.0f};
    float item_quantity_mult{1.0f};
    float skill_exp_mult{1.0f};
};

// Apply multiplier to rewards
inline auto apply_multiplier(const quest_rewards& base, const reward_multiplier& mult) -> quest_rewards {
    quest_rewards result = base;

    result.experience = static_cast<int64_t>(static_cast<float>(base.experience) * mult.experience_mult);
    result.gold = static_cast<int64_t>(static_cast<float>(base.gold) * mult.gold_mult);

    for (auto& item : result.items) {
        item.count = static_cast<int16_t>(static_cast<float>(item.count) * mult.item_quantity_mult);
        if (item.count < 1) item.count = 1;
    }

    for (auto& skill : result.skill_experience) {
        skill.amount = static_cast<int32_t>(static_cast<float>(skill.amount) * mult.skill_exp_mult);
    }

    return result;
}

}  // namespace hb::quest
