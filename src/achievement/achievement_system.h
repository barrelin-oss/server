#pragma once
// Achievements (after the Helbreath Olympia client): counters that the game bumps (kills, quests,
// gold looted, chests opened, level...) and definitions that unlock at a target, each worth points
// and optionally a title. Data: bin/game_configs/achievements.yaml; progress in
// characters.achievement_data.
#include "core/types.h"
#include "core/result.h"
#include "core/subsystem.h"

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace hb::achievement
{

enum class counter_kind : uint8_t
{
    kills_total = 0,     // monsters killed
    kills_type,          // monsters of one type killed (param = npc sprite type)
    elites_killed,       // elite monsters killed
    quests_completed,    // quests turned in
    gold_looted,         // gold taken from monsters and chests
    chests_opened,       // treasure chests opened
    level,               // character level reached (kept as a maximum, not a sum)
    specialties_leveled, // specialty levels gained
};

[[nodiscard]] auto counter_kind_from_string(std::string_view s) -> std::optional<counter_kind>;
[[nodiscard]] auto to_string(counter_kind k) -> std::string_view;

struct achievement_def
{
    uint16_t id{0};
    std::string name;
    std::string description;
    std::string category; // general | pvm | pvp | challenges
    std::string title;    // optional title the player may wear
    counter_kind kind{counter_kind::kills_total};
    int32_t param{0}; // kills_type: the npc sprite type
    int64_t target{1};
    int32_t points{10};
};

struct achievement_progress
{
    const achievement_def* def{nullptr};
    int64_t current{0};
    int64_t unlocked_at{0}; // unix seconds, 0 = locked
};

class achievement_system : public subsystem
{
public:
    achievement_system() = default;
    ~achievement_system() override = default;

    [[nodiscard]] auto name() const -> std::string_view override { return "achievement_system"; }
    void initialize() override;
    void shutdown() override;

    auto load_from_file(const std::string& path) -> result<size_t, std::string>;
    auto load_from_yaml(const std::string& text) -> result<size_t, std::string>;
    [[nodiscard]] auto find(uint16_t id) const -> const achievement_def*;
    [[nodiscard]] auto definitions() const -> const std::vector<achievement_def>& { return defs_; }

    void register_player(player_id id);
    void unregister_player(player_id id);

    // Bumps a counter (level: keeps the maximum). Returns what just unlocked, in definition order.
    auto add(player_id id, counter_kind kind, int32_t param, int64_t amount) -> std::vector<const achievement_def*>;
    auto set_level(player_id id, int32_t level) -> std::vector<const achievement_def*>;

    [[nodiscard]] auto counter(player_id id, counter_kind kind, int32_t param = 0) const -> int64_t;
    [[nodiscard]] auto is_unlocked(player_id id, uint16_t achievement) const -> bool;
    [[nodiscard]] auto points(player_id id) const -> int32_t;
    [[nodiscard]] auto progress(player_id id) const -> std::vector<achievement_progress>;

    // JSON {"counters": {"kills_total": 12, "kills_type:14": 3}, "unlocked": [{"id": 1, "at": 1700000000}]}
    [[nodiscard]] auto serialize(player_id id) const -> std::string;
    void deserialize(player_id id, const std::string& json_text);

    [[nodiscard]] static auto counter_key(counter_kind kind, int32_t param) -> std::string;

private:
    struct player_state
    {
        std::unordered_map<std::string, int64_t> counters;
        std::unordered_map<uint16_t, int64_t> unlocked;
    };
    auto check_unlocks(player_id id, player_state& st, counter_kind kind, int32_t param)
        -> std::vector<const achievement_def*>;

    std::vector<achievement_def> defs_;
    std::unordered_map<uint16_t, size_t> by_id_;
    std::unordered_map<uint32_t, player_state> players_;
};

} // namespace hb::achievement
