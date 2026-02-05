#pragma once

// pack_system.h
// Pack tracking for NPC group behavior (calls_help, social)

#include "entity/entity.h"
#include "core/types.h"

#include <cstdint>
#include <vector>
#include <unordered_map>

namespace hb::npc {

using pack_id = uint32_t;

struct pack
{
    pack_id id{};
    entity::entity leader{};
    std::vector<entity::entity> members;
    map_id map{};
};

class pack_system
{
public:
    auto create_pack(entity::entity leader, map_id map) -> pack_id;
    void add_member(pack_id pack, entity::entity member);
    void remove_member(entity::entity member);
    void disband(pack_id pack);

    [[nodiscard]] auto get_pack(pack_id id) -> pack*;
    [[nodiscard]] auto get_pack_for(entity::entity member) -> pack*;

    void update(float delta_time);

private:
    std::unordered_map<pack_id, pack> packs_;
    std::unordered_map<entity::entity, pack_id> member_to_pack_;
    pack_id next_id_{1};
};

}  // namespace hb::npc
