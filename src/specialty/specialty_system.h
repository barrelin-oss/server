#pragma once
// Monster mastery ("specialties", after the Helbreath Olympia client): every so many kills of a
// monster type unlock the next bonus of that monster's ladder, and the bonuses apply only against
// that monster. Data: bin/game_configs/specialties.yaml (docs/olympia-reference.md, section 4).
#include "core/types.h"
#include "core/result.h"
#include "core/subsystem.h"

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace hb::specialty
{

enum class bonus_kind : uint8_t
{
    damage = 0,           // +damage_per_step to every hit on the monster
    damage_pct,           // x(1 + damage_pct_per_step %) per step, multiplicative
    damage_reduction,     // +reduction_per_step % less damage taken from the monster
    damage_reduction_pct, // x(1 - reduction_pct_per_step %) per step, multiplicative
    hit_ratio,            // +hit_ratio_per_step to the hit rate against the monster
    hit_ratio_pct,        // x(1 + hit_ratio_pct_per_step %) per step
    drop_rate,            // x(1 + drop_rate_pct_per_step %) on every drop chance of the monster
};

[[nodiscard]] auto bonus_kind_from_string(std::string_view s) -> std::optional<bonus_kind>;
[[nodiscard]] auto to_string(bonus_kind k) -> std::string_view;
// Human text of one step: "+2 damage", "+3% drop rate"
[[nodiscard]] auto describe_step(bonus_kind k) -> std::string;

// One step of the ladder. The Olympia client only names the kind; the amounts are ours.
inline constexpr int32_t damage_per_step = 2;
inline constexpr int32_t damage_pct_per_step = 3;
inline constexpr int32_t reduction_per_step = 1;
inline constexpr int32_t reduction_pct_per_step = 3;
inline constexpr int32_t hit_ratio_per_step = 5;
inline constexpr int32_t hit_ratio_pct_per_step = 3;
inline constexpr int32_t drop_rate_pct_per_step = 5;

struct specialty_def
{
    int16_t npc_type{0}; // NPC sprite type (the "type" of the legacy tables)
    std::string npc_name;
    int32_t base_kills{0};
    std::vector<bonus_kind> ladder; // one entry per level, in order
};

// What a player has earned against one monster type
struct specialty_bonuses
{
    int32_t damage{0};
    float damage_mult{1.0f};
    int32_t reduction_pct{0};
    float reduction_mult{1.0f}; // damage taken is divided by it
    int32_t hit_ratio{0};
    float hit_mult{1.0f};
    float drop_mult{1.0f};

    [[nodiscard]] auto any() const -> bool;
};

struct specialty_progress
{
    int16_t npc_type{0};
    std::string npc_name;
    int32_t kills{0};
    int32_t level{0};
    int32_t max_level{0};
    int32_t next_level_kills{0}; // 0 once every level is earned
    std::vector<bonus_kind> unlocked;
};

class specialty_system : public subsystem
{
public:
    specialty_system() = default;
    ~specialty_system() override = default;

    [[nodiscard]] auto name() const -> std::string_view override { return "specialty_system"; }
    void initialize() override;
    void shutdown() override;

    // Definitions
    auto load_from_file(const std::string& path) -> result<size_t, std::string>;
    auto load_from_yaml(const std::string& text) -> result<size_t, std::string>;
    [[nodiscard]] auto find(int16_t npc_type) const -> const specialty_def*;
    [[nodiscard]] auto definitions() const -> const std::vector<specialty_def>& { return defs_; }

    // Per-player state
    void register_player(player_id id);
    void unregister_player(player_id id);
    // Credits one kill of npc_type to the player. Returns the progress when a level was gained.
    auto record_kill(player_id id, int16_t npc_type) -> std::optional<specialty_progress>;
    [[nodiscard]] auto bonuses(player_id id, int16_t npc_type) const -> specialty_bonuses;
    [[nodiscard]] auto progress(player_id id) const -> std::vector<specialty_progress>;
    [[nodiscard]] auto progress_for(player_id id, int16_t npc_type) const -> std::optional<specialty_progress>;
    [[nodiscard]] auto kills(player_id id, int16_t npc_type) const -> int32_t;

    // JSON [{"type": 14, "kills": 320}, ...] as stored in characters.specialty_data
    [[nodiscard]] auto serialize(player_id id) const -> std::string;
    void deserialize(player_id id, const std::string& json_text);

    // Kills needed for a level: base_kills * L * (L + 1) / 2 (our reading of Olympia's "base kills scaling")
    [[nodiscard]] static auto kills_for_level(int32_t base_kills, int32_t level) -> int32_t;
    [[nodiscard]] static auto level_for(int32_t kills, int32_t base_kills, int32_t max_level) -> int32_t;
    [[nodiscard]] static auto bonuses_for(const specialty_def& def, int32_t level) -> specialty_bonuses;

private:
    auto make_progress(const specialty_def& def, int32_t kills) const -> specialty_progress;

    std::vector<specialty_def> defs_;
    std::unordered_map<int16_t, size_t> by_type_;
    std::unordered_map<uint32_t, std::unordered_map<int16_t, int32_t>> kills_; // player -> npc_type -> kills
};

} // namespace hb::specialty
