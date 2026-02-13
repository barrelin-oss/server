#pragma once

// npc.h
// NPC component and state

#include "core/types.h"
#include "core/enums.h"
#include "entity/entity.h"
#include "world/position.h"
#include "npc/ai_behavior.h"

#include <string>
#include <string_view>

namespace hb::npc {

// Forward declarations
struct spawn_point;

// NPC category
enum class npc_category : uint8_t {
    monster = 0,     // Regular enemy
    boss = 1,        // Boss monster
    guard = 2,       // Town guard
    merchant = 3,    // Shop NPC
    quest = 4,       // Quest giver
    trainer = 5,     // Skill trainer
    banker = 6,      // Bank NPC
    warehouse = 7,   // Warehouse NPC
    pet = 8,         // Player pet
    summon = 9,      // Summoned creature
};

// NPC instance data
struct npc {
    // Identity
    entity::entity entity_id{};
    npc_id template_id{};
    std::string name;
    int16_t sprite_id{0};  // Legacy m_sType - used for drop level tiers
    npc_category category{npc_category::monster};
    hb::faction faction{hb::faction::neutral};

    // Owner (for pets/summons)
    entity::entity owner{};
    hb::faction owner_faction{hb::faction::neutral};

    // Stats
    int32_t hp{0};
    int32_t max_hp{0};
    int32_t mp{0};
    int32_t max_mp{0};

    // Combat stats
    int16_t level{1};
    int16_t attack_dice{1};
    int16_t attack_sides{4};
    int16_t attack_bonus{0};
    int16_t defense{0};
    int16_t magic_defense{0};
    int16_t hit_rate{0};
    int16_t dodge_rate{0};

    // Speed
    int16_t attack_speed{100};
    int16_t move_speed{100};

    // Rewards
    int32_t exp_reward{0};
    int32_t gold_min{0};
    int32_t gold_max{0};

    // Location
    map_id current_map{};
    hb::world::position pos{};
    hb::world::direction facing{hb::world::direction::south};

    // Spawn info
    spawn_point* spawn{nullptr};

    // AI
    ai_config ai;
    ai_runtime_state ai_state;

    // Pack membership (0 = no pack)
    uint32_t pack{0};

    // Helper methods
    [[nodiscard]] auto is_alive() const -> bool { return hp > 0; }
    [[nodiscard]] auto is_dead() const -> bool { return hp <= 0; }

    [[nodiscard]] auto hp_percent() const -> float {
        return max_hp > 0 ? static_cast<float>(hp) / max_hp * 100.0f : 0.0f;
    }

    [[nodiscard]] auto is_monster() const -> bool {
        return category == npc_category::monster || category == npc_category::boss;
    }

    [[nodiscard]] auto is_friendly() const -> bool {
        return category >= npc_category::guard && category <= npc_category::warehouse;
    }

    [[nodiscard]] auto is_pet() const -> bool {
        return category == npc_category::pet || category == npc_category::summon;
    }

    [[nodiscard]] auto has_owner() const -> bool {
        return owner.is_valid();
    }

    void damage(int32_t amount) {
        hp = std::max(0, hp - amount);
        if (hp <= 0) {
            ai_state.set_state(ai_state::dead);
            ai_state.death_time = std::chrono::steady_clock::now();
        }
    }

    void heal(int32_t amount) {
        hp = std::min(hp + amount, max_hp);
    }

    // Roll attack damage
    [[nodiscard]] auto roll_damage() const -> int32_t {
        // Simple dice roll: attack_dice d attack_sides + attack_bonus
        // Would use actual random in real implementation
        int32_t base = attack_dice * (attack_sides / 2 + 1);
        return base + attack_bonus;
    }
};

[[nodiscard]] inline auto npc_category_to_string(npc_category cat) -> std::string_view
{
    switch (cat)
    {
        case npc_category::monster:    return "monster";
        case npc_category::boss:       return "boss";
        case npc_category::guard:      return "guard";
        case npc_category::merchant:   return "merchant";
        case npc_category::quest:      return "quest";
        case npc_category::trainer:    return "trainer";
        case npc_category::banker:     return "banker";
        case npc_category::warehouse:  return "warehouse";
        case npc_category::pet:        return "pet";
        case npc_category::summon:     return "summon";
        default: return "unknown";
    }
}

// Compute NPC hostility relative to a specific player.
// Returns "enemy", "friendly", or "neutral".
//
// Rules (matching legacy m_cSide behavior):
//   - Monsters/bosses: enemy if aggressive, neutral if passive (rabbit, cat, etc.)
//   - Guards: enemy to criminals/murderers regardless of faction;
//             enemy to opposing faction; friendly to same or neutral faction
//   - Merchants/quest/trainer/banker/warehouse: always friendly
//   - Pets/summons: friendly if same faction as viewer, enemy if opposing, neutral if traveler
[[nodiscard]] inline auto npc_hostility_for_player(
    const npc& n,
    hb::faction player_faction,
    bool player_is_criminal,
    bool player_is_murderer) -> std::string_view
{
    switch (n.category)
    {
        case npc_category::monster:
        case npc_category::boss:
            return n.ai.has_flag(ai_flags::aggressive) ? "enemy" : "neutral";

        case npc_category::guard:
        {
            // Guards always hostile to criminals/murderers
            if (player_is_criminal || player_is_murderer)
                return "enemy";
            // Neutral guards are friendly to everyone
            if (n.faction == hb::faction::neutral)
                return "friendly";
            // Faction guards: friendly to same, enemy to opposing
            if (player_faction == hb::faction::neutral)
                return "neutral";
            return (n.faction == player_faction) ? "friendly" : "enemy";
        }

        case npc_category::merchant:
        case npc_category::quest:
        case npc_category::trainer:
        case npc_category::banker:
        case npc_category::warehouse:
            return "friendly";

        case npc_category::pet:
        case npc_category::summon:
        {
            if (n.owner_faction == hb::faction::neutral)
                return "neutral";
            if (player_faction == hb::faction::neutral)
                return "neutral";
            return (n.owner_faction == player_faction) ? "friendly" : "enemy";
        }
    }
    return "neutral";
}

}  // namespace hb::npc
