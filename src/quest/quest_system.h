#pragma once

// quest_system.h
// Quest management subsystem

#include "core/types.h"
#include "core/result.h"
#include "core/subsystem.h"
#include "quest/quest.h"

#include <unordered_map>
#include <string_view>
#include <functional>
#include <vector>

namespace hb::quest {

// Quest event types
struct quest_accepted_event {
    player_id player{};
    quest_id quest{};
};

struct quest_completed_event {
    player_id player{};
    quest_id quest{};
    quest_rewards rewards{};
};

struct quest_failed_event {
    player_id player{};
    quest_id quest{};
    std::string reason;
};

struct quest_abandoned_event {
    player_id player{};
    quest_id quest{};
};

struct objective_progress_event {
    player_id player{};
    quest_id quest{};
    uint16_t objective_id{};
    int32_t old_progress{};
    int32_t new_progress{};
    bool completed{false};
};

// Quest update events (for tracking progress)
struct kill_event {
    player_id killer{};
    npc_id killed_npc{};
    bool was_player{false};
};

struct item_collected_event {
    player_id player{};
    item_id item{};
    int32_t count{1};
};

struct location_reached_event {
    player_id player{};
    map_id map{};
    int16_t x{};
    int16_t y{};
};

struct npc_talked_event {
    player_id player{};
    npc_id npc{};
};

struct item_crafted_event {
    player_id player{};
    item_id item{};
    int32_t count{1};
};

struct resource_gathered_event {
    player_id player{};
    uint8_t skill_type{};
    int32_t count{1};
};

struct level_reached_event {
    player_id player{};
    int16_t new_level{};
    uint8_t skill_type{0};  // 0 = character level
};

// Quest system configuration
struct quest_system_config {
    size_t max_active_quests{25};
    bool allow_quest_sharing{true};
    float quest_exp_modifier{1.0f};
    float quest_gold_modifier{1.0f};
};

// Quest acceptance result
enum class accept_result : uint8_t {
    success = 0,
    quest_log_full = 1,
    level_too_low = 2,
    level_too_high = 3,
    wrong_faction = 4,
    missing_prerequisite = 5,
    already_active = 6,
    on_cooldown = 7,
    quest_not_found = 8,
};

// Quest completion result
enum class complete_result : uint8_t {
    success = 0,
    not_active = 1,
    objectives_incomplete = 2,
    quest_not_found = 3,
    inventory_full = 4,
};

// Quest system - manages quest templates and player quest state
class quest_system : public subsystem {
public:
    using quest_callback = std::function<void(const quest_completed_event&)>;

    quest_system();
    ~quest_system() override;

    // Subsystem interface
    [[nodiscard]] auto name() const -> std::string_view override { return "quest_system"; }
    void initialize() override;
    void shutdown() override;
    void update(float delta_time) override;

    // Configuration
    void set_config(const quest_system_config& config);

    // Quest template management
    void register_quest(quest_template quest);
    [[nodiscard]] auto get_quest_template(quest_id id) const -> const quest_template*;
    [[nodiscard]] auto quest_count() const -> size_t;

    // Player management
    void register_player(player_id id);
    void unregister_player(player_id id);

    // Quest operations
    auto accept_quest(player_id player, quest_id quest) -> accept_result;
    auto abandon_quest(player_id player, quest_id quest) -> bool;
    auto complete_quest(player_id player, quest_id quest) -> complete_result;

    // Quest queries
    [[nodiscard]] auto get_active_quests(player_id player) const -> std::vector<quest_id>;
    [[nodiscard]] auto get_quest_state(player_id player, quest_id quest) const -> const quest_state*;
    [[nodiscard]] auto has_quest(player_id player, quest_id quest) const -> bool;
    [[nodiscard]] auto has_completed_quest(player_id player, quest_id quest) const -> bool;
    [[nodiscard]] auto can_accept_quest(player_id player, quest_id quest) const -> accept_result;

    // Availability queries
    [[nodiscard]] auto get_available_quests(player_id player, int16_t player_level, uint8_t faction) const
        -> std::vector<quest_id>;
    [[nodiscard]] auto get_quests_from_npc(npc_id npc) const -> std::vector<quest_id>;

    // Progress tracking - call these when game events occur
    void on_kill(const kill_event& event);
    void on_item_collected(const item_collected_event& event);
    void on_location_reached(const location_reached_event& event);
    void on_npc_talked(const npc_talked_event& event);
    void on_item_crafted(const item_crafted_event& event);
    void on_resource_gathered(const resource_gathered_event& event);
    void on_level_reached(const level_reached_event& event);

    // Callbacks
    void on_quest_completed(quest_callback callback);

    // Direct access (for serialization)
    [[nodiscard]] auto get_journal(player_id player) -> quest_journal*;
    [[nodiscard]] auto get_journal(player_id player) const -> const quest_journal*;

private:
    void check_quest_completion(player_id player, quest_state& state);
    void update_kill_objectives(player_id player, npc_id killed, bool was_player);
    void update_collect_objectives(player_id player, item_id item, int32_t count);
    void update_location_objectives(player_id player, map_id map, int16_t x, int16_t y);
    void update_npc_objectives(player_id player, npc_id npc);
    void update_craft_objectives(player_id player, item_id item, int32_t count);
    void update_gather_objectives(player_id player, uint8_t skill_type, int32_t count);
    void update_level_objectives(player_id player, int16_t level, uint8_t skill_type);
    void update_timed_quests(float delta_time);

    void notify_quest_completed(player_id player, quest_id quest, const quest_rewards& rewards);

    quest_system_config config_;
    std::unordered_map<quest_id, quest_template> quest_templates_;
    std::unordered_map<player_id, quest_journal> player_journals_;
    std::vector<quest_callback> completion_callbacks_;
};

}  // namespace hb::quest
