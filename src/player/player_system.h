#pragma once

// player_system.h
// Player management subsystem

#include "core/types.h"
#include "core/result.h"
#include "core/subsystem.h"
#include "player/player.h"

#include <unordered_map>
#include <memory>
#include <string>
#include <string_view>
#include <functional>

namespace hb::player {

// Player creation data
struct player_create_info {
    std::string name;
    std::string account_name;
    gender sex{gender::male};
    player_class profession{player_class::warrior};
    hb::faction faction{hb::faction::neutral};
    base_stats initial_stats{};
};

// Player system configuration
struct player_system_config {
    uint32_t max_players{2000};
    int32_t regen_tick_ms{1000};
    int32_t hunger_decay_interval_ms{60000};
    int32_t pk_decay_interval_ms{300000};
    int32_t save_interval_ms{300000};
};

// Player system - manages all online players
class player_system : public subsystem {
public:
    player_system();
    ~player_system() override;

    // Subsystem interface
    [[nodiscard]] auto name() const -> std::string_view override { return "player_system"; }
    void initialize() override;
    void shutdown() override;
    void update(float delta_time) override;

    // Configuration
    void set_config(const player_system_config& config);

    // Player lifecycle
    auto create_player(const player_create_info& info) -> result<player_id, std::string>;
    void remove_player(player_id id);

    // Player access
    [[nodiscard]] auto get_player(player_id id) -> player*;
    [[nodiscard]] auto get_player(player_id id) const -> const player*;
    [[nodiscard]] auto get_player_by_name(std::string_view name) -> player*;
    [[nodiscard]] auto get_player_by_connection(connection_id conn) -> player*;
    [[nodiscard]] auto get_player_by_session(session_id sess) -> player*;

    // Player queries
    [[nodiscard]] auto player_count() const -> size_t { return players_.size(); }
    [[nodiscard]] auto player_exists(player_id id) const -> bool { return players_.contains(id); }

    // Bind player to connection/session
    void bind_connection(player_id id, connection_id conn);
    void bind_session(player_id id, session_id sess);
    void unbind_connection(player_id id);
    void unbind_session(player_id id);

    // Player state updates
    void update_stats(player_id id);
    void apply_damage(player_id id, int32_t damage);
    void apply_heal(player_id id, int32_t amount);
    void add_experience(player_id id, int64_t amount);
    void add_stat_point(player_id id, int16_t stat_index);

    // Status effects
    void add_status(player_id id, player_status status);
    void remove_status(player_id id, player_status status);
    void clear_all_status(player_id id);

    // Equipment
    void equip_item(player_id id, equip_slot slot, item_id item, uint16_t dur, uint16_t max_dur);
    auto unequip_item(player_id id, equip_slot slot) -> equipped_item;
    void recalculate_equipment_modifiers(player_id id);

    // Location
    void set_position(player_id id, map_id map, hb::world::position pos, hb::world::direction facing);
    void set_facing(player_id id, hb::world::direction facing);

    // Combat
    void set_target(player_id id, entity::entity target);
    void clear_target(player_id id);

    // Iteration
    template<typename Func>
    void for_each_player(Func&& func) {
        for (auto& [id, player_ptr] : players_) {
            func(id, *player_ptr);
        }
    }

    template<typename Func>
    void for_each_player(Func&& func) const {
        for (const auto& [id, player_ptr] : players_) {
            func(id, *player_ptr);
        }
    }

    // Find players matching a predicate
    template<typename Pred>
    [[nodiscard]] auto find_players_if(Pred&& pred) const -> std::vector<player_id> {
        std::vector<player_id> result;
        for (const auto& [id, player_ptr] : players_) {
            if (pred(*player_ptr)) {
                result.push_back(id);
            }
        }
        return result;
    }

private:
    [[nodiscard]] auto next_player_id() -> player_id {
        return player_id{next_id_++};
    }

    void update_regeneration(float delta_time);
    void update_hunger(float delta_time);
    void update_pk_decay(float delta_time);

    player_system_config config_;
    uint32_t next_id_{1};

    std::unordered_map<player_id, std::unique_ptr<player>> players_;
    std::unordered_map<std::string, player_id> name_to_id_;
    std::unordered_map<connection_id, player_id> connection_to_id_;
    std::unordered_map<session_id, player_id> session_to_id_;

    // Update accumulators
    float regen_accumulator_{0.0f};
    float hunger_accumulator_{0.0f};
    float pk_decay_accumulator_{0.0f};
};

}  // namespace hb::player
