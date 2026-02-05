// pack_system.cpp
// Pack tracking for NPC group behavior

#include "npc/pack_system.h"

#include <algorithm>

namespace hb::npc {

auto pack_system::create_pack(entity::entity leader, map_id map) -> pack_id
{
    auto id = next_id_++;

    pack p;
    p.id = id;
    p.leader = leader;
    p.members.push_back(leader);
    p.map = map;

    packs_[id] = std::move(p);
    member_to_pack_[leader] = id;

    return id;
}

void pack_system::add_member(pack_id pack_id_val, entity::entity member)
{
    auto it = packs_.find(pack_id_val);
    if (it == packs_.end()) return;

    // Don't add duplicates
    auto& members = it->second.members;
    if (std::find(members.begin(), members.end(), member) != members.end()) return;

    members.push_back(member);
    member_to_pack_[member] = pack_id_val;
}

void pack_system::remove_member(entity::entity member)
{
    auto it = member_to_pack_.find(member);
    if (it == member_to_pack_.end()) return;

    auto pack_id_val = it->second;
    member_to_pack_.erase(it);

    auto pack_it = packs_.find(pack_id_val);
    if (pack_it == packs_.end()) return;

    auto& members = pack_it->second.members;
    members.erase(std::remove(members.begin(), members.end(), member), members.end());

    // If leader left, promote next member or disband
    if (pack_it->second.leader == member)
    {
        if (members.empty())
        {
            packs_.erase(pack_it);
        }
        else
        {
            pack_it->second.leader = members.front();
        }
    }
}

void pack_system::disband(pack_id pack_id_val)
{
    auto it = packs_.find(pack_id_val);
    if (it == packs_.end()) return;

    for (auto& member : it->second.members)
    {
        member_to_pack_.erase(member);
    }

    packs_.erase(it);
}

auto pack_system::get_pack(pack_id id) -> pack*
{
    auto it = packs_.find(id);
    return it != packs_.end() ? &it->second : nullptr;
}

auto pack_system::get_pack_for(entity::entity member) -> pack*
{
    auto it = member_to_pack_.find(member);
    if (it == member_to_pack_.end()) return nullptr;

    return get_pack(it->second);
}

void pack_system::update(float /*delta_time*/)
{
    // Clean up empty packs
    std::vector<pack_id> to_remove;
    for (auto& [id, p] : packs_)
    {
        if (p.members.empty())
        {
            to_remove.push_back(id);
        }
    }

    for (auto id : to_remove)
    {
        packs_.erase(id);
    }
}

}  // namespace hb::npc
