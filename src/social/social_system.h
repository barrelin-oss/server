#pragma once

// social_system.h
// Social subsystem managing guilds, parties, and chat

#include "core/types.h"
#include "core/result.h"
#include "core/subsystem.h"
#include "social/guild.h"
#include "social/party.h"
#include "social/chat.h"
#include "social/friend.h"

#include <unordered_map>
#include <unordered_set>
#include <string_view>
#include <functional>
#include <vector>
#include <regex>

namespace hb::database
{
class database_system;
}
namespace hb::player
{
class player_system;
}

namespace hb::social
{

// Guild events
struct guild_created_event
{
    guild_id guild{};
    player_id founder{};
    std::string guild_name;
};

struct guild_disbanded_event
{
    guild_id guild{};
    std::string guild_name;
};

struct guild_member_joined_event
{
    guild_id guild{};
    player_id player{};
    std::string player_name;
};

struct guild_member_left_event
{
    guild_id guild{};
    player_id player{};
    std::string player_name;
    bool was_kicked{false};
};

struct guild_rank_changed_event
{
    guild_id guild{};
    player_id player{};
    guild_rank old_rank{};
    guild_rank new_rank{};
};

// Party events
struct party_created_event
{
    party_id party{};
    player_id leader{};
};

struct party_disbanded_event
{
    party_id party{};
};

struct party_member_joined_event
{
    party_id party{};
    player_id player{};
    std::string player_name;
};

struct party_member_left_event
{
    party_id party{};
    player_id player{};
    std::string player_name;
    bool was_kicked{false};
};

struct party_leader_changed_event
{
    party_id party{};
    player_id old_leader{};
    player_id new_leader{};
};

// Chat events
struct chat_message_event
{
    chat_message message;
};

// Social system configuration
struct social_system_config
{
    // Guild settings
    size_t max_guilds{1000};
    int32_t guild_creation_cost{10000};
    int16_t min_guild_name_length{3};
    int16_t max_guild_name_length{20};
    int16_t min_guild_tag_length{2};
    int16_t max_guild_tag_length{4};

    // Party settings
    size_t max_parties{5000};
    int16_t exp_share_range{15};

    // Chat settings
    int16_t local_chat_range{15};
    int16_t shout_chat_range{50};
    bool enable_profanity_filter{true};
    std::vector<std::string> profanity_words;
};

// Guild operation results
enum class guild_result : uint8_t
{
    success = 0,
    guild_not_found = 1,
    player_not_member = 2,
    insufficient_permissions = 3,
    guild_full = 4,
    already_in_guild = 5,
    name_taken = 6,
    invalid_name = 7,
    cannot_kick_self = 8,
    cannot_kick_higher_rank = 9,
    cannot_promote_higher = 10,
    insufficient_gold = 11,
};

// Party operation results
enum class party_result : uint8_t
{
    success = 0,
    party_not_found = 1,
    player_not_member = 2,
    not_leader = 3,
    party_full = 4,
    already_in_party = 5,
    no_pending_invite = 6,
    cannot_kick_self = 7,
    invite_expired = 8,
};

// Social system - manages guilds, parties, and chat
class social_system : public subsystem
{
public:
    using guild_callback = std::function<void(const guild_created_event&)>;
    using party_callback = std::function<void(const party_created_event&)>;
    using chat_callback = std::function<void(const chat_message_event&)>;

    social_system();
    ~social_system() override;

    // Subsystem interface
    [[nodiscard]] auto name() const -> std::string_view override { return "social_system"; }
    void initialize() override;
    void shutdown() override;
    void update(float delta_time) override;

    // Configuration
    void set_config(const social_system_config& config);

    // Database and player system dependencies
    void set_database(database::database_system* db);
    void set_player_system(player::player_system* players);

    // Guild persistence
    auto load_guilds_from_database() -> result<void, std::string>;
    void connect_guild_member(player_id character_id, player_id runtime_id, const std::string& name);
    void disconnect_guild_member(player_id runtime_id, player_id character_id);

    // Player management
    void register_player(player_id id, const std::string& name);
    void unregister_player(player_id id);

    // ========== Guild Operations ==========

    auto
    create_guild(player_id founder, const std::string& name, const std::string& tag) -> result<guild_id, guild_result>;
    auto disband_guild(player_id player, guild_id gid) -> guild_result;

    auto invite_to_guild(player_id inviter, guild_id gid, player_id invitee) -> guild_result;
    auto accept_guild_invite(player_id invitee) -> result<guild_id, guild_result>;
    auto decline_guild_invite(player_id invitee) -> guild_result;
    [[nodiscard]] auto get_guild_invite(player_id invitee) const -> const pending_guild_invite*;
    [[nodiscard]] auto has_guild_invite(player_id invitee) const -> bool;
    auto join_guild(player_id player, guild_id gid) -> guild_result;
    auto leave_guild(player_id player) -> guild_result;
    auto kick_from_guild(player_id kicker, player_id target) -> guild_result;

    auto promote_member(player_id promoter, player_id target) -> guild_result;
    auto demote_member(player_id demoter, player_id target) -> guild_result;
    auto set_member_rank(player_id setter, player_id target, guild_rank rank) -> guild_result;

    auto set_guild_motd(player_id player, const std::string& motd) -> guild_result;

    // Admin-bypass guild mutations (no permission checks)
    auto admin_disband_guild(guild_id gid) -> guild_result;
    auto admin_kick_from_guild(guild_id gid, std::string_view target_name) -> guild_result;
    auto admin_set_member_rank(guild_id gid, std::string_view target_name, guild_rank rank) -> guild_result;

    // Guild queries
    [[nodiscard]] auto get_guild(guild_id id) -> guild*;
    [[nodiscard]] auto get_guild(guild_id id) const -> const guild*;
    [[nodiscard]] auto get_player_guild(player_id player) const -> guild_id;
    [[nodiscard]] auto find_guild_by_name(std::string_view name) const -> guild_id;
    [[nodiscard]] auto guild_count() const -> size_t;

    template<typename Func> void for_each_guild(Func&& func) const
    {
        for (const auto& [id, g] : guilds_)
        {
            func(id, g);
        }
    }

    // ========== Party Operations ==========

    auto create_party(player_id leader) -> result<party_id, party_result>;
    auto disband_party(player_id leader) -> party_result;

    auto invite_to_party(player_id inviter, player_id invitee) -> party_result;
    auto accept_party_invite(player_id player, party_id pid) -> party_result;
    auto decline_party_invite(player_id player, party_id pid) -> party_result;
    auto leave_party(player_id player) -> party_result;
    auto kick_from_party(player_id kicker, player_id target) -> party_result;

    auto set_party_leader(player_id current_leader, player_id new_leader) -> party_result;
    auto set_loot_mode(player_id leader, loot_mode mode) -> party_result;
    auto set_exp_mode(player_id leader, exp_mode mode) -> party_result;

    // Party queries
    [[nodiscard]] auto get_party(party_id id) -> party*;
    [[nodiscard]] auto get_party(party_id id) const -> const party*;
    [[nodiscard]] auto get_player_party(player_id player) const -> party_id;
    [[nodiscard]] auto has_party_invite(player_id player) const -> bool;
    [[nodiscard]] auto party_count() const -> size_t;

    // Update party member stats (called when stats change)
    void update_party_member_stats(player_id player, int32_t hp, int32_t max_hp, int32_t mp, int32_t max_mp);
    void update_party_member_map(player_id player, map_id map);

    template<typename Func> void for_each_party(Func&& func) const
    {
        for (const auto& [id, p] : parties_)
        {
            func(id, p);
        }
    }

    // ========== Chat Operations ==========

    auto send_message(const chat_message& msg) -> filter_result;
    auto
    send_local_chat(player_id sender, const std::string& content, map_id map, int16_t x, int16_t y) -> filter_result;
    auto send_global_chat(player_id sender, const std::string& content) -> filter_result;
    auto send_guild_chat(player_id sender, const std::string& content) -> filter_result;
    auto send_party_chat(player_id sender, const std::string& content) -> filter_result;
    auto send_whisper(player_id sender, player_id recipient, const std::string& content) -> filter_result;
    auto send_system_message(player_id recipient, const std::string& content) -> filter_result;

    // Chat settings
    [[nodiscard]] auto get_chat_settings(player_id player) -> chat_settings*;
    auto block_player(player_id blocker, player_id blocked) -> bool;
    auto unblock_player(player_id blocker, player_id blocked) -> bool;

    // ========== Friend Operations ==========

    // Friend persistence
    auto load_friends_from_database() -> result<void, std::string>;
    void connect_friend(player_id character_id, player_id runtime_id, const std::string& name);
    void disconnect_friend(player_id runtime_id, player_id character_id);

    // Friend request operations
    auto send_friend_request(player_id requester_char_id, player_id target_char_id) -> friend_result;
    auto accept_friend_request(player_id accepter_char_id, player_id requester_char_id) -> friend_result;
    auto decline_friend_request(player_id decliner_char_id, player_id requester_char_id) -> friend_result;
    auto cancel_friend_request(player_id requester_char_id, player_id target_char_id) -> friend_result;

    // Friend management
    auto remove_friend(player_id remover_char_id, player_id target_char_id) -> friend_result;
    auto block_player_friend(player_id blocker_char_id, player_id target_char_id) -> friend_result;
    auto unblock_player_friend(player_id blocker_char_id, player_id target_char_id) -> friend_result;

    // Friend queries
    [[nodiscard]] auto get_friends(player_id character_id) const -> const std::vector<friend_entry>*;
    [[nodiscard]] auto get_incoming_requests(player_id character_id) const -> const std::vector<friend_request>*;
    [[nodiscard]] auto get_outgoing_requests(player_id character_id) const -> const std::vector<friend_request>*;
    [[nodiscard]] auto get_blocked_players(player_id character_id) const -> const std::unordered_set<player_id>*;
    [[nodiscard]] auto are_friends(player_id char_a, player_id char_b) const -> bool;
    [[nodiscard]] auto has_pending_request(player_id requester_char, player_id target_char) const -> bool;
    [[nodiscard]] auto is_blocked_friend(player_id blocker_char, player_id target_char) const -> bool;
    [[nodiscard]] auto friend_count(player_id character_id) const -> size_t;

    // Resolve runtime_id for a character_id (0 if offline)
    [[nodiscard]] auto get_friend_runtime_id(player_id character_id) const -> player_id;

    // Look up character_id by name (checks online players first, then DB)
    [[nodiscard]] auto lookup_character_by_name(const std::string& name) -> std::pair<player_id, std::string>;

    // Look up character name by id (checks online players first, then DB)
    [[nodiscard]] auto lookup_character_name(player_id character_id) -> std::string;

    // ========== Callbacks ==========

    void on_guild_created(std::function<void(const guild_created_event&)> callback);
    void on_party_created(std::function<void(const party_created_event&)> callback);
    void on_chat_message(chat_callback callback);

private:
    // Helper methods
    [[nodiscard]] auto filter_message(const std::string& content) -> std::pair<filter_result, std::string>;
    [[nodiscard]] auto is_valid_guild_name(const std::string& name) const -> bool;
    [[nodiscard]] auto is_valid_guild_tag(const std::string& tag) const -> bool;
    [[nodiscard]] auto check_rate_limit(player_id player) -> bool;

    void cleanup_expired_invites();
    auto generate_guild_id() -> guild_id;
    auto generate_party_id() -> party_id;

    // Persistence helpers
    auto save_guild_to_database(guild_id gid) -> result<void, std::string>;
    auto insert_guild_to_database(const guild& g) -> result<void, std::string>;
    auto delete_guild_from_database(guild_id gid) -> result<void, std::string>;
    auto lookup_character_id(player_id runtime_id) -> player_id;

    // Friend persistence helpers
    auto save_friend_request_db(player_id requester_char, player_id requestee_char) -> result<void, std::string>;
    auto delete_friend_request_db(player_id requester_char, player_id requestee_char) -> result<void, std::string>;
    auto save_friend_relationship_db(player_id char_a, player_id char_b) -> result<void, std::string>;
    auto delete_friend_relationship_db(player_id char_a, player_id char_b) -> result<void, std::string>;
    auto save_block_db(player_id blocker_char, player_id blocked_char) -> result<void, std::string>;
    auto delete_block_db(player_id blocker_char, player_id blocked_char) -> result<void, std::string>;

    social_system_config config_;

    // Database and player system dependencies
    database::database_system* database_{nullptr};
    player::player_system* player_system_{nullptr};

    // Guilds
    std::unordered_map<guild_id, guild> guilds_;
    std::unordered_map<player_id, guild_id> player_guilds_;                     // runtime_id → guild_id
    std::unordered_map<player_id, guild_id> character_guild_index_;             // character_id → guild_id
    std::unordered_map<player_id, pending_guild_invite> pending_guild_invites_; // invitee runtime_id → invite
    uint32_t next_guild_id_{1};

    // Parties
    std::unordered_map<party_id, party> parties_;
    std::unordered_map<player_id, party_id> player_parties_;
    std::unordered_map<player_id, std::vector<party_invite>> pending_party_invites_;
    uint32_t next_party_id_{1};

    // Chat
    std::unordered_map<player_id, chat_settings> player_chat_settings_;
    std::unordered_map<player_id, chat_rate_limit> player_rate_limits_;
    std::unordered_map<player_id, std::string> player_names_;

    // Friends - all keyed by character_id (persistent)
    friend_list_config friend_config_;
    std::unordered_map<player_id, std::vector<friend_request>> incoming_requests_;  // char_id → requests TO them
    std::unordered_map<player_id, std::vector<friend_request>> outgoing_requests_;  // char_id → requests FROM them
    std::unordered_map<player_id, std::vector<friend_entry>> character_friends_;    // char_id → accepted friends
    std::unordered_map<player_id, std::unordered_set<player_id>> character_blocks_; // char_id → blocked char_ids
    std::unordered_map<player_id, player_id> friend_char_to_runtime_; // char_id → runtime_id (online only)
    std::unordered_map<player_id, player_id> friend_runtime_to_char_; // runtime_id → char_id (online only)

    // Callbacks
    std::vector<std::function<void(const guild_created_event&)>> guild_created_callbacks_;
    std::vector<std::function<void(const party_created_event&)>> party_created_callbacks_;
    std::vector<chat_callback> chat_callbacks_;
};

} // namespace hb::social
