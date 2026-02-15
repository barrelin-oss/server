#pragma once

// script_executor.h
// Runtime execution of NPC scripts

#include "npc/script/script_action.h"
#include "entity/entity.h"

#include <chrono>
#include <unordered_map>

namespace hb
{
class subsystem_manager;
}

namespace hb::npc
{
class npc_system;
}

namespace hb::npc::script
{

struct script_state
{
    const npc_script* script{nullptr};
    size_t current_action{0};
    std::chrono::steady_clock::time_point action_start{};
    bool waiting{false};
    int32_t wait_remaining_ms{0};
};

class script_executor
{
public:
    void start(entity::entity npc_entity, const npc_script& script);
    void stop(entity::entity npc_entity);
    void update(float delta_time);
    [[nodiscard]] auto is_running(entity::entity npc_entity) const -> bool;

    void set_npc_system(npc_system* sys) { npc_sys_ = sys; }

private:
    void execute_action(entity::entity npc_entity, const script_action& action);
    void advance(entity::entity npc_entity, script_state& state);

    std::unordered_map<entity::entity, script_state> running_;
    npc_system* npc_sys_{nullptr};
};

} // namespace hb::npc::script
