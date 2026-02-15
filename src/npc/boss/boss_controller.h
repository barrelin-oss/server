#pragma once

// boss_controller.h
// Boss phase transition and management logic

#include "npc/boss/boss_phase.h"
#include "npc/boss/boss_state.h"
#include "entity/entity.h"

#include <unordered_map>

namespace hb::npc
{
struct npc;
class npc_system;
} // namespace hb::npc

namespace hb::npc::boss
{

class boss_controller
{
public:
    void register_boss(entity::entity id, const boss_config& config);
    void unregister_boss(entity::entity id);
    void update(float delta_time);
    [[nodiscard]] auto get_state(entity::entity id) -> boss_state*;
    [[nodiscard]] auto is_boss(entity::entity id) const -> bool;

    void set_npc_system(npc_system* sys) { npc_sys_ = sys; }

private:
    void check_phase_transitions(entity::entity id, npc& npc_ref, boss_state& state);
    void transition_phase(entity::entity id, npc& npc_ref, boss_state& state, int new_phase);
    void apply_phase_modifiers(npc& npc_ref, const boss_phase& phase);
    void check_enrage(entity::entity id, npc& npc_ref, boss_state& state);

    std::unordered_map<entity::entity, boss_state> bosses_;
    npc_system* npc_sys_{nullptr};
};

} // namespace hb::npc::boss
