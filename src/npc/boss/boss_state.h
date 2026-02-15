#pragma once

// boss_state.h
// Runtime state for boss encounters

#include "npc/boss/boss_phase.h"
#include "entity/entity.h"

#include <chrono>
#include <vector>

namespace hb::npc::boss
{

struct boss_state
{
    const boss_config* config{nullptr};
    int current_phase{0};
    std::chrono::steady_clock::time_point phase_start_time{};
    std::chrono::steady_clock::time_point spawn_time{};
    bool enraged{false};
    std::vector<entity::entity> spawned_adds;

    [[nodiscard]] auto time_in_phase_ms() const -> int64_t
    {
        auto now = std::chrono::steady_clock::now();
        return std::chrono::duration_cast<std::chrono::milliseconds>(now - phase_start_time).count();
    }

    [[nodiscard]] auto time_since_spawn_ms() const -> int64_t
    {
        auto now = std::chrono::steady_clock::now();
        return std::chrono::duration_cast<std::chrono::milliseconds>(now - spawn_time).count();
    }
};

} // namespace hb::npc::boss
