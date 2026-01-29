#pragma once

// skill_system.h
// Skill management subsystem

#include "core/types.h"
#include "core/result.h"
#include "core/subsystem.h"
#include "skill/skill.h"

#include <unordered_map>
#include <string_view>
#include <functional>

namespace hb::skill {

// Skill training configuration
struct skill_config {
    float exp_multiplier{1.0f};
    bool allow_training{true};
    int16_t max_skill_level{200};
};

// Skill system configuration
struct skill_system_config {
    float global_exp_modifier{1.0f};
    int32_t training_cooldown_ms{1000};
    bool enable_skill_decay{false};
};

// Skill level up event
struct skill_level_event {
    player_id player{};
    skill_type skill{};
    int16_t old_level{0};
    int16_t new_level{0};
};

// Skill system - manages player skills
class skill_system : public subsystem {
public:
    using level_up_callback = std::function<void(const skill_level_event&)>;

    skill_system();
    ~skill_system() override;

    // Subsystem interface
    [[nodiscard]] auto name() const -> std::string_view override { return "skill_system"; }
    void initialize() override;
    void shutdown() override;
    void update(float delta_time) override;

    // Configuration
    void set_config(const skill_system_config& config);

    // Player skill management
    void register_player(player_id id);
    void unregister_player(player_id id);

    // Skill queries
    [[nodiscard]] auto get_skill_level(player_id player, skill_type skill) const -> int16_t;
    [[nodiscard]] auto get_skill_exp(player_id player, skill_type skill) const -> int32_t;
    [[nodiscard]] auto get_mastery(player_id player, skill_type skill) const -> mastery_level;

    // Skill modification
    void set_skill_level(player_id player, skill_type skill, int16_t level);
    auto add_skill_exp(player_id player, skill_type skill, int32_t amount) -> int16_t;
    void reset_skill(player_id player, skill_type skill);
    void reset_all_skills(player_id player);

    // Skill training
    auto train_skill(player_id player, skill_type skill) -> skill_use_result;
    auto can_train(player_id player, skill_type skill) const -> bool;

    // Combat skill helpers
    [[nodiscard]] auto get_weapon_skill_for_item(player_id player, item_id weapon) const -> int16_t;
    [[nodiscard]] auto calculate_damage_bonus(player_id player, skill_type weapon_skill) const -> int16_t;
    [[nodiscard]] auto calculate_hit_bonus(player_id player, skill_type weapon_skill) const -> int16_t;

    // Callbacks
    void on_level_up(level_up_callback callback);

    // Direct access (for serialization)
    [[nodiscard]] auto get_player_skills(player_id player) -> player_skills*;
    [[nodiscard]] auto get_player_skills(player_id player) const -> const player_skills*;

private:
    void notify_level_up(player_id player, skill_type skill, int16_t old_level, int16_t new_level);

    skill_system_config config_;
    std::unordered_map<player_id, player_skills> player_skills_;
    std::vector<level_up_callback> level_up_callbacks_;
};

}  // namespace hb::skill
