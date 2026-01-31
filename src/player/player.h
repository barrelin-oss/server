#pragma once

// player.h
// Player component and state

#include "core/types.h"
#include "core/enums.h"
#include "entity/entity.h"
#include "world/position.h"
#include "player/stats.h"
#include "player/experience.h"
#include "player/equipment.h"
#include "skill/skill.h"

#include <string>
#include <array>
#include <chrono>

namespace hb::player {

// Player gender
enum class gender : uint8_t {
    male = 0,
    female = 1
};

// Player class/profession
enum class player_class : uint8_t {
    warrior = 0,
    mage = 1,
    archer = 2,  // Not in original but reserved
};

// Player status flags
enum class player_status : uint32_t {
    none = 0,
    poisoned = 1 << 0,
    paralyzed = 1 << 1,
    invisible = 1 << 2,
    frozen = 1 << 3,
    berserk = 1 << 4,
    protection = 1 << 5,
    defense_up = 1 << 6,
    attack_up = 1 << 7,
    magic_up = 1 << 8,
    haste = 1 << 9,
    slow = 1 << 10,
    cursed = 1 << 11,
    stunned = 1 << 12,
    silenced = 1 << 13,
    invincible = 1 << 14,
    gm_invisible = 1 << 15,
};

inline auto operator|(player_status a, player_status b) -> player_status {
    return static_cast<player_status>(static_cast<uint32_t>(a) | static_cast<uint32_t>(b));
}

inline auto operator&(player_status a, player_status b) -> player_status {
    return static_cast<player_status>(static_cast<uint32_t>(a) & static_cast<uint32_t>(b));
}

inline auto operator~(player_status a) -> player_status {
    return static_cast<player_status>(~static_cast<uint32_t>(a));
}

// Admin level
enum class admin_level : uint8_t {
    player = 0,
    gamemaster = 1,
    admin = 2,
    developer = 3
};

// Hunger state
struct hunger_state {
    int8_t level{100};  // 0-100, 0 = starving

    [[nodiscard]] auto is_starving() const -> bool { return level <= 0; }
    [[nodiscard]] auto is_hungry() const -> bool { return level < 30; }
    [[nodiscard]] auto is_full() const -> bool { return level >= 100; }

    void consume(int8_t amount) {
        level = std::min<int8_t>(100, level + amount);
    }

    void decay(int8_t amount) {
        level = std::max<int8_t>(0, level - amount);
    }
};

// PK (player kill) state
struct pk_state {
    int32_t count{0};      // Total PK count
    int32_t points{0};     // Current PK points (decays)

    [[nodiscard]] auto is_murderer() const -> bool { return points >= 100; }
    [[nodiscard]] auto is_criminal() const -> bool { return points >= 30 && points < 100; }
    [[nodiscard]] auto is_innocent() const -> bool { return points < 30; }

    void add_kill() {
        ++count;
        points += 50;
    }

    void decay_points(int32_t amount) {
        points = std::max(0, points - amount);
    }
};

// Player component - represents a player character
struct player {
    // Identity
    player_id id{};
    std::string name;
    std::string account_name;
    gender sex{gender::male};
    player_class profession{player_class::warrior};

    // Faction
    hb::faction faction{hb::faction::neutral};
    std::string guild_name;
    uint8_t guild_rank{0};

    // Stats
    base_stats base;
    stat_modifiers modifiers;
    computed_stats computed;
    stat_points stat_points;

    // Resources
    int32_t hp{0};
    int32_t mp{0};
    int32_t sp{0};

    // Experience
    experience_state experience;

    // Equipment
    equipment_state equipment;

    // Skills
    skill::player_skills skills;

    // Status
    player_status status{player_status::none};
    hunger_state hunger;
    pk_state pk;
    admin_level admin{admin_level::player};

    // Combat state
    entity::entity target{};
    std::chrono::steady_clock::time_point last_attack_time{};
    std::chrono::steady_clock::time_point last_hit_time{};

    // Location
    map_id current_map{};
    hb::world::position pos{};
    hb::world::direction facing{hb::world::direction::south};

    // Session
    connection_id connection{};
    session_id session{};

    // Helper methods
    [[nodiscard]] auto has_status(player_status s) const -> bool {
        return (status & s) != player_status::none;
    }

    void add_status(player_status s) {
        status = status | s;
    }

    void remove_status(player_status s) {
        status = status & ~s;
    }

    [[nodiscard]] auto is_alive() const -> bool { return hp > 0; }
    [[nodiscard]] auto is_dead() const -> bool { return hp <= 0; }

    [[nodiscard]] auto is_gm() const -> bool {
        return admin >= admin_level::gamemaster;
    }

    [[nodiscard]] auto hp_percent() const -> float {
        return computed.max_hp > 0 ? static_cast<float>(hp) / computed.max_hp : 0.0f;
    }

    [[nodiscard]] auto mp_percent() const -> float {
        return computed.max_mp > 0 ? static_cast<float>(mp) / computed.max_mp : 0.0f;
    }

    void recalculate_stats() {
        base.level_bonus = experience.level;
        computed.compute(base, modifiers);
    }

    void heal_hp(int32_t amount) {
        hp = std::min(hp + amount, computed.max_hp);
    }

    void heal_mp(int32_t amount) {
        mp = std::min(mp + amount, computed.max_mp);
    }

    void heal_sp(int32_t amount) {
        sp = std::min(sp + amount, computed.max_sp);
    }

    void damage_hp(int32_t amount) {
        hp = std::max(0, hp - amount);
    }

    auto spend_mp(int32_t amount) -> bool {
        if (mp < amount) return false;
        mp -= amount;
        return true;
    }

    auto spend_sp(int32_t amount) -> bool {
        if (sp < amount) return false;
        sp -= amount;
        return true;
    }

    void restore_to_full() {
        hp = computed.max_hp;
        mp = computed.max_mp;
        sp = computed.max_sp;
    }
};

}  // namespace hb::player
