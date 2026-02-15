#pragma once

// party.h
// Party definitions and state

#include "core/types.h"

#include <array>
#include <cmath>
#include <cstdint>
#include <string>
#include <vector>

namespace hb::social
{

// Party ID type
struct party_id
{
    uint32_t value{0};

    constexpr party_id() = default;
    constexpr explicit party_id(uint32_t v) : value(v) {}
    constexpr party_id(int v) : value(static_cast<uint32_t>(v)) {}

    constexpr auto operator<=>(const party_id&) const = default;
    [[nodiscard]] constexpr auto is_valid() const -> bool { return value != 0; }
};

// Loot distribution mode
enum class loot_mode : uint8_t
{
    free_for_all = 0, // Anyone can loot
    round_robin = 1,  // Takes turns
    leader = 2,       // Leader decides
    random = 3,       // Random distribution
    need_greed = 4,   // Need/Greed rolls
};

// Experience distribution mode
enum class exp_mode : uint8_t
{
    individual = 0,     // No sharing
    equal_split = 1,    // Split evenly
    level_weighted = 2, // Based on level ratio
};

// Party member data
struct party_member
{
    player_id player{};
    std::string name;
    int16_t level{1};
    int32_t hp{100};
    int32_t max_hp{100};
    int32_t mp{100};
    int32_t max_mp{100};
    map_id current_map{};
    bool is_online{true};

    [[nodiscard]] auto hp_percent() const -> float
    {
        if (max_hp <= 0)
            return 0.0f;
        return (static_cast<float>(hp) / static_cast<float>(max_hp)) * 100.0f;
    }

    [[nodiscard]] auto mp_percent() const -> float
    {
        if (max_mp <= 0)
            return 0.0f;
        return (static_cast<float>(mp) / static_cast<float>(max_mp)) * 100.0f;
    }
};

// Party invite
struct party_invite
{
    party_id party{};
    player_id inviter{};
    player_id invitee{};
    std::chrono::steady_clock::time_point expires_at{};

    [[nodiscard]] auto is_expired() const -> bool { return std::chrono::steady_clock::now() >= expires_at; }
};

// Party state
struct party
{
    static constexpr size_t max_members = 8;
    static constexpr auto invite_duration = std::chrono::seconds(60);

    party_id id{};
    player_id leader{};
    std::vector<party_member> members;
    std::vector<party_invite> pending_invites;

    loot_mode loot{loot_mode::free_for_all};
    exp_mode experience{exp_mode::equal_split};
    int16_t loot_threshold{0}; // Item level threshold for loot mode

    // Round robin state
    size_t round_robin_index{0};

    [[nodiscard]] auto member_count() const -> size_t { return members.size(); }

    [[nodiscard]] auto is_full() const -> bool { return members.size() >= max_members; }

    [[nodiscard]] auto is_empty() const -> bool { return members.empty(); }

    [[nodiscard]] auto get_member(player_id player) -> party_member*
    {
        for (auto& member : members)
        {
            if (member.player == player)
                return &member;
        }
        return nullptr;
    }

    [[nodiscard]] auto get_member(player_id player) const -> const party_member*
    {
        for (const auto& member : members)
        {
            if (member.player == player)
                return &member;
        }
        return nullptr;
    }

    [[nodiscard]] auto is_member(player_id player) const -> bool { return get_member(player) != nullptr; }

    [[nodiscard]] auto is_leader(player_id player) const -> bool { return leader == player; }

    [[nodiscard]] auto get_leader() -> party_member* { return get_member(leader); }

    [[nodiscard]] auto get_leader() const -> const party_member* { return get_member(leader); }

    // Add a member
    auto add_member(player_id player, const std::string& player_name, int16_t level) -> bool
    {
        if (is_full() || is_member(player))
            return false;

        party_member member;
        member.player = player;
        member.name = player_name;
        member.level = level;

        members.push_back(std::move(member));

        // First member becomes leader
        if (members.size() == 1)
        {
            leader = player;
        }

        return true;
    }

    // Remove a member
    auto remove_member(player_id player) -> bool
    {
        for (auto it = members.begin(); it != members.end(); ++it)
        {
            if (it->player == player)
            {
                members.erase(it);

                // Assign new leader if leader left
                if (leader == player && !members.empty())
                {
                    leader = members.front().player;
                }

                return true;
            }
        }
        return false;
    }

    // Transfer leadership
    auto set_leader(player_id new_leader) -> bool
    {
        if (!is_member(new_leader))
            return false;
        leader = new_leader;
        return true;
    }

    // Get next player for round robin loot
    auto get_next_looter() -> player_id
    {
        if (members.empty())
            return player_id{};

        round_robin_index = round_robin_index % members.size();
        player_id looter = members[round_robin_index].player;
        round_robin_index = (round_robin_index + 1) % members.size();

        return looter;
    }

    // Calculate average level of party
    [[nodiscard]] auto average_level() const -> float
    {
        if (members.empty())
            return 0.0f;

        int32_t total = 0;
        for (const auto& member : members)
        {
            total += member.level;
        }
        return static_cast<float>(total) / static_cast<float>(members.size());
    }

    // Get members in same map
    [[nodiscard]] auto members_in_map(map_id map) const -> std::vector<player_id>
    {
        std::vector<player_id> result;
        for (const auto& member : members)
        {
            if (member.current_map == map && member.is_online)
            {
                result.push_back(member.player);
            }
        }
        return result;
    }

    // Clean up expired invites
    void cleanup_invites()
    {
        auto now = std::chrono::steady_clock::now();
        std::erase_if(pending_invites, [now](const party_invite& inv) { return inv.expires_at <= now; });
    }

    // Add an invite
    auto add_invite(player_id inviter, player_id invitee) -> bool
    {
        // Check if already invited
        for (const auto& inv : pending_invites)
        {
            if (inv.invitee == invitee)
                return false;
        }

        party_invite invite;
        invite.party = id;
        invite.inviter = inviter;
        invite.invitee = invitee;
        invite.expires_at = std::chrono::steady_clock::now() + invite_duration;

        pending_invites.push_back(invite);
        return true;
    }

    // Remove an invite
    auto remove_invite(player_id invitee) -> bool
    {
        for (auto it = pending_invites.begin(); it != pending_invites.end(); ++it)
        {
            if (it->invitee == invitee)
            {
                pending_invites.erase(it);
                return true;
            }
        }
        return false;
    }

    // Check if player has pending invite
    [[nodiscard]] auto has_invite(player_id invitee) const -> bool
    {
        for (const auto& inv : pending_invites)
        {
            if (inv.invitee == invitee && !inv.is_expired())
                return true;
        }
        return false;
    }
};

// Party size bonus table from original Helbreath
// Index = eligible member count, value = bonus multiplier
// 1→0%, 2→2%, 3→5%, 4→7%, 5→10%, 6→14%, 7→17%, 8→20%
inline constexpr std::array<double, 9> party_exp_bonus = {0.0, 0.0, 0.02, 0.05, 0.07, 0.10, 0.14, 0.17, 0.20};

// Calculate per-member XP share for equal_split mode.
// Returns XP each eligible member receives, minimum 1.
inline auto calculate_party_exp_share(int32_t base_exp, int eligible_count) -> int32_t
{
    if (eligible_count <= 0 || base_exp <= 0)
        return 0;
    if (eligible_count == 1)
        return base_exp;

    auto idx = static_cast<size_t>(std::min(eligible_count, 8));
    double bonus = party_exp_bonus[idx];
    auto per_member = static_cast<int32_t>(std::round((base_exp * (1.0 + bonus)) / eligible_count));
    return std::max(per_member, 1);
}

// Calculate per-member XP share for level_weighted mode.
// Returns XP for a specific member based on their level relative to the group.
inline auto calculate_level_weighted_exp(int32_t base_exp,
                                         int eligible_count,
                                         int16_t member_level,
                                         int32_t total_levels) -> int32_t
{
    if (eligible_count <= 0 || base_exp <= 0 || total_levels <= 0)
        return 0;
    if (eligible_count == 1)
        return base_exp;

    auto idx = static_cast<size_t>(std::min(eligible_count, 8));
    double bonus = party_exp_bonus[idx];
    double pool = base_exp * (1.0 + bonus);
    double weight = static_cast<double>(member_level) / static_cast<double>(total_levels);
    auto result = static_cast<int32_t>(std::round(pool * weight));
    return std::max(result, 1);
}

} // namespace hb::social
