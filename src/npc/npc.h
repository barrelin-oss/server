#pragma once

// npc.h
// NPC component and state

#include "core/types.h"
#include "entity/entity.h"
#include "world/position.h"
#include "npc/ai_behavior.h"
#include "npc/loot_table.h"

#include <optional>
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

    // Owner (for pets/summons)
    entity::entity owner{};

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

    // Loot
    std::optional<loot_table> loot;

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

}  // namespace hb::npc
