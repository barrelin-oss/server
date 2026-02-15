#pragma once

// guild.h
// Guild definitions and state

#include "core/types.h"

#include <string>
#include <vector>
#include <array>
#include <chrono>
#include <cstdint>

namespace hb::social
{

// Guild rank (0 = highest, 9 = lowest)
enum class guild_rank : uint8_t
{
    guild_master = 0,
    officer = 1,
    veteran = 2,
    member = 3,
    recruit = 4,
    // Ranks 5-9 reserved for custom ranks
};

// Guild permissions (bitflags)
enum class guild_permission : uint32_t
{
    none = 0,
    invite = 1 << 0,
    kick = 1 << 1,
    promote = 1 << 2,
    demote = 1 << 3,
    edit_motd = 1 << 4,
    edit_ranks = 1 << 5,
    access_bank = 1 << 6,
    withdraw_bank = 1 << 7,
    declare_war = 1 << 8,
    manage_alliance = 1 << 9,
    disband = 1 << 10,

    // Rank presets
    all = 0xFFFFFFFF,
    officer_default = invite | kick | promote | demote | edit_motd | access_bank,
    veteran_default = invite | access_bank,
    member_default = access_bank,
    recruit_default = none,
};

inline auto operator|(guild_permission a, guild_permission b) -> guild_permission
{
    return static_cast<guild_permission>(static_cast<uint32_t>(a) | static_cast<uint32_t>(b));
}

inline auto operator&(guild_permission a, guild_permission b) -> guild_permission
{
    return static_cast<guild_permission>(static_cast<uint32_t>(a) & static_cast<uint32_t>(b));
}

inline auto has_permission(guild_permission flags, guild_permission perm) -> bool
{
    return (static_cast<uint32_t>(flags) & static_cast<uint32_t>(perm)) != 0;
}

// Guild member data
struct guild_member
{
    player_id player{};       // Runtime ID (0 when offline)
    player_id character_id{}; // Persistent DB character ID (always set after load/create)
    std::string name;
    guild_rank rank{guild_rank::recruit};
    std::chrono::system_clock::time_point joined_at{};
    std::chrono::system_clock::time_point last_online{};
    int64_t contribution{0}; // Contribution points
    std::string note;        // Officer note

    [[nodiscard]] auto is_online() const -> bool; // Would check session manager
};

// Guild rank configuration
struct guild_rank_config
{
    std::string name;
    guild_permission permissions{guild_permission::none};
};

// Guild war declaration
struct guild_war
{
    guild_id enemy_guild{};
    std::chrono::system_clock::time_point declared_at{};
    std::chrono::system_clock::time_point expires_at{};
    int32_t kills{0};   // Our kills
    int32_t deaths{0};  // Our deaths
    bool mutual{false}; // Both guilds declared
};

// Guild alliance
struct guild_alliance
{
    guild_id ally_guild{};
    std::chrono::system_clock::time_point formed_at{};
};

// Guild state
struct guild
{
    static constexpr size_t max_members = 100;
    static constexpr size_t max_ranks = 10;
    static constexpr size_t max_wars = 10;
    static constexpr size_t max_alliances = 5;

    guild_id id{};
    std::string name;
    std::string tag;    // Short tag (3-4 chars)
    std::string motd;   // Message of the day
    player_id master{}; // Guild master player ID
    uint8_t faction{0}; // 0 = neutral, 1 = Aresden, 2 = Elvine

    // Members
    std::vector<guild_member> members;

    // Rank configurations
    std::array<guild_rank_config, max_ranks> ranks{{
        {"Guild Master", guild_permission::all},
        {"Officer", guild_permission::officer_default},
        {"Veteran", guild_permission::veteran_default},
        {"Member", guild_permission::member_default},
        {"Recruit", guild_permission::recruit_default},
        {"Rank 6", guild_permission::none},
        {"Rank 7", guild_permission::none},
        {"Rank 8", guild_permission::none},
        {"Rank 9", guild_permission::none},
        {"Rank 10", guild_permission::none},
    }};

    // Guild relations
    std::vector<guild_war> wars;
    std::vector<guild_alliance> alliances;

    // Statistics
    int64_t total_kills{0};
    int64_t total_deaths{0};
    int64_t gold_bank{0};
    std::chrono::system_clock::time_point created_at{};

    // Experience/leveling (optional)
    int32_t level{1};
    int64_t experience{0};

    [[nodiscard]] auto member_count() const -> size_t { return members.size(); }

    [[nodiscard]] auto is_full() const -> bool { return members.size() >= max_members; }

    [[nodiscard]] auto is_empty() const -> bool { return members.empty(); }

    [[nodiscard]] auto get_member(player_id player) -> guild_member*
    {
        for (auto& member : members)
        {
            if (member.player == player)
                return &member;
        }
        return nullptr;
    }

    [[nodiscard]] auto get_member(player_id player) const -> const guild_member*
    {
        for (const auto& member : members)
        {
            if (member.player == player)
                return &member;
        }
        return nullptr;
    }

    [[nodiscard]] auto is_member(player_id player) const -> bool { return get_member(player) != nullptr; }

    [[nodiscard]] auto get_member_by_character_id(player_id char_id) -> guild_member*
    {
        for (auto& member : members)
        {
            if (member.character_id == char_id)
                return &member;
        }
        return nullptr;
    }

    [[nodiscard]] auto get_member_by_character_id(player_id char_id) const -> const guild_member*
    {
        for (const auto& member : members)
        {
            if (member.character_id == char_id)
                return &member;
        }
        return nullptr;
    }

    [[nodiscard]] auto get_rank_permissions(guild_rank rank) const -> guild_permission
    {
        size_t idx = static_cast<size_t>(rank);
        if (idx >= max_ranks)
            return guild_permission::none;
        return ranks[idx].permissions;
    }

    [[nodiscard]] auto has_permission(player_id player, guild_permission perm) const -> bool
    {
        const auto* member = get_member(player);
        if (!member)
            return false;
        auto rank_perms = get_rank_permissions(member->rank);
        return hb::social::has_permission(rank_perms, perm);
    }

    [[nodiscard]] auto is_at_war_with(guild_id other) const -> bool
    {
        for (const auto& war : wars)
        {
            if (war.enemy_guild == other)
                return true;
        }
        return false;
    }

    [[nodiscard]] auto is_allied_with(guild_id other) const -> bool
    {
        for (const auto& alliance : alliances)
        {
            if (alliance.ally_guild == other)
                return true;
        }
        return false;
    }

    // Add a member
    auto add_member(player_id player, const std::string& player_name, guild_rank rank = guild_rank::recruit) -> bool
    {
        if (is_full() || is_member(player))
            return false;

        guild_member member;
        member.player = player;
        member.name = player_name;
        member.rank = rank;
        member.joined_at = std::chrono::system_clock::now();
        member.last_online = std::chrono::system_clock::now();

        members.push_back(std::move(member));
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
                return true;
            }
        }
        return false;
    }

    // Set member rank
    auto set_member_rank(player_id player, guild_rank new_rank) -> bool
    {
        auto* member = get_member(player);
        if (!member)
            return false;
        member->rank = new_rank;
        return true;
    }
};

// Pending guild invite
struct pending_guild_invite
{
    guild_id guild{};
    player_id inviter{};
    std::string guild_name;
    std::string guild_tag;
    std::string inviter_name;
    std::chrono::steady_clock::time_point expires_at{};

    static constexpr auto invite_duration = std::chrono::seconds(60);

    [[nodiscard]] auto is_expired() const -> bool { return std::chrono::steady_clock::now() >= expires_at; }
};

} // namespace hb::social
