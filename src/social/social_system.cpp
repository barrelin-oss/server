// social_system.cpp
// Social subsystem implementation

#include "social/social_system.h"
#include "database/database_system.h"
#include "player/player_system.h"
#include "core/logger.h"

#include <nlohmann/json.hpp>
#include <algorithm>
#include <cctype>

namespace hb::social {

namespace {

auto serialize_rank_configs(const std::array<guild_rank_config, guild::max_ranks>& ranks) -> std::string
{
    nlohmann::json arr = nlohmann::json::array();
    for (const auto& r : ranks) {
        arr.push_back({
            {"name", r.name},
            {"permissions", static_cast<uint32_t>(r.permissions)}
        });
    }
    return arr.dump();
}

auto deserialize_rank_configs(const std::string& json_str) -> std::array<guild_rank_config, guild::max_ranks>
{
    std::array<guild_rank_config, guild::max_ranks> ranks{{
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

    if (json_str.empty() || json_str == "[]") return ranks;

    try {
        auto arr = nlohmann::json::parse(json_str);
        if (!arr.is_array()) return ranks;

        for (size_t i = 0; i < arr.size() && i < guild::max_ranks; ++i) {
            const auto& obj = arr[i];
            if (obj.contains("name")) ranks[i].name = obj["name"].get<std::string>();
            if (obj.contains("permissions")) ranks[i].permissions = static_cast<guild_permission>(obj["permissions"].get<uint32_t>());
        }
    } catch (const std::exception& e) {
        // Return defaults on parse failure
    }

    return ranks;
}

} // anonymous namespace

social_system::social_system() = default;

social_system::~social_system() {
    if (is_initialized()) {
        shutdown();
    }
}

void social_system::initialize() {
    LOG_INFO(general, "Social system initializing...");
    set_initialized(true);
    LOG_INFO(general, "Social system initialized");
}

void social_system::shutdown() {
    LOG_INFO(general, "Social system shutting down...");

    guilds_.clear();
    player_guilds_.clear();
    character_guild_index_.clear();
    parties_.clear();
    player_parties_.clear();
    pending_party_invites_.clear();
    player_chat_settings_.clear();
    player_rate_limits_.clear();
    player_names_.clear();

    guild_created_callbacks_.clear();
    party_created_callbacks_.clear();
    chat_callbacks_.clear();

    set_initialized(false);
    LOG_INFO(general, "Social system shutdown complete");
}

void social_system::update(float /*delta_time*/) {
    cleanup_expired_invites();
}

void social_system::set_config(const social_system_config& config) {
    config_ = config;
}

void social_system::set_database(database::database_system* db) {
    database_ = db;
}

void social_system::set_player_system(player::player_system* players) {
    player_system_ = players;
}

auto social_system::load_guilds_from_database() -> result<void, std::string> {
    if (!database_) {
        return result<void, std::string>::err("No database configured");
    }

    // Load all guilds
    auto guilds_result = database_->execute_params(
        "SELECT id, name, tag, motd, nation, leader_id, level, experience, "
        "total_kills, total_deaths, gold_bank, rank_configs, created_at "
        "FROM guilds ORDER BY id");

    if (guilds_result.is_err()) {
        return result<void, std::string>::err("Failed to load guilds: " + guilds_result.error());
    }

    auto& guilds_rows = guilds_result.value();
    uint32_t max_id = 0;

    for (size_t i = 0; i < guilds_rows.size(); ++i) {
        guild g;
        auto db_id = guilds_rows.get<int>(i, "id");
        g.id = guild_id{static_cast<uint16_t>(db_id)};
        g.name = guilds_rows.get<std::string>(i, "name");
        g.tag = guilds_rows.get<std::string>(i, "tag");
        g.motd = guilds_rows.get<std::string>(i, "motd");
        g.faction = static_cast<uint8_t>(guilds_rows.get<int>(i, "nation"));
        g.level = guilds_rows.get<int>(i, "level");
        g.experience = guilds_rows.get<int64_t>(i, "experience");
        g.total_kills = guilds_rows.get<int64_t>(i, "total_kills");
        g.total_deaths = guilds_rows.get<int64_t>(i, "total_deaths");
        g.gold_bank = guilds_rows.get<int64_t>(i, "gold_bank");

        // Parse rank configs
        auto rank_json = guilds_rows.get<std::string>(i, "rank_configs");
        g.ranks = deserialize_rank_configs(rank_json);

        // Master will be resolved when they come online
        g.master = player_id{};

        if (static_cast<uint32_t>(db_id) > max_id) {
            max_id = static_cast<uint32_t>(db_id);
        }

        guilds_.emplace(g.id, std::move(g));
    }

    // Load all guild members
    auto members_result = database_->execute_params(
        "SELECT gm.guild_id, gm.character_id, gm.rank, gm.contribution, gm.note, c.name "
        "FROM guild_members gm "
        "JOIN characters c ON gm.character_id = c.id "
        "ORDER BY gm.guild_id, gm.rank");

    if (members_result.is_err()) {
        return result<void, std::string>::err("Failed to load guild members: " + members_result.error());
    }

    auto& members_rows = members_result.value();

    for (size_t i = 0; i < members_rows.size(); ++i) {
        auto gid = guild_id{static_cast<uint16_t>(members_rows.get<int>(i, "guild_id"))};
        auto* g = get_guild(gid);
        if (!g) continue;

        guild_member member;
        member.player = player_id{};  // Offline until they connect
        member.character_id = player_id{static_cast<uint32_t>(members_rows.get<int>(i, "character_id"))};
        member.name = members_rows.get<std::string>(i, "name");
        member.rank = static_cast<guild_rank>(members_rows.get<int>(i, "rank"));
        member.contribution = members_rows.get<int64_t>(i, "contribution");
        member.note = members_rows.get<std::string>(i, "note");

        // If this member is rank 0 (guild master), remember for later resolution
        // (master player_id stays {0} until they connect)

        character_guild_index_[member.character_id] = gid;
        g->members.push_back(std::move(member));
    }

    next_guild_id_ = max_id + 1;

    LOG_INFO(general, "Loaded {} guilds from database", guilds_.size());
    return result<void, std::string>::ok();
}

void social_system::connect_guild_member(player_id character_id, player_id runtime_id, const std::string& name) {
    // Look up guild by character_guild_index first (O(1))
    guild_id gid{};
    auto it = character_guild_index_.find(character_id);
    if (it != character_guild_index_.end()) {
        gid = it->second;
    } else {
        // Fallback: scan all guilds (handles cases where index wasn't populated, e.g. tests)
        for (auto& [id, g] : guilds_) {
            if (g.get_member_by_character_id(character_id)) {
                gid = id;
                character_guild_index_[character_id] = id;
                break;
            }
        }
    }

    if (!gid.is_valid()) return;

    auto* g = get_guild(gid);
    if (!g) return;

    auto* member = g->get_member_by_character_id(character_id);
    if (!member) return;

    member->player = runtime_id;
    member->name = name;
    member->last_online = std::chrono::system_clock::now();
    player_guilds_[runtime_id] = gid;

    // Resolve guild master if this member is rank 0
    if (member->rank == guild_rank::guild_master) {
        g->master = runtime_id;
    }

    LOG_DEBUG(general, "Guild member '{}' connected (char_id={}, runtime_id={}, guild='{}')",
        name, character_id.value, runtime_id.value, g->name);
}

void social_system::disconnect_guild_member(player_id runtime_id, player_id character_id) {
    auto guild_it = player_guilds_.find(runtime_id);
    if (guild_it == player_guilds_.end()) return;

    auto* g = get_guild(guild_it->second);
    if (g) {
        auto* member = g->get_member(runtime_id);
        if (member) {
            member->player = player_id{};  // Mark offline
            member->last_online = std::chrono::system_clock::now();
        }

        // If this was the guild master, clear the runtime master ID
        if (g->master == runtime_id) {
            g->master = player_id{};
        }
    }

    player_guilds_.erase(runtime_id);
}

auto social_system::lookup_character_id(player_id runtime_id) -> player_id {
    if (player_system_) {
        auto* p = player_system_->get_player(runtime_id);
        if (p) return p->character_id;
    }
    return player_id{};
}

auto social_system::insert_guild_to_database(const guild& g) -> result<void, std::string> {
    if (!database_) return result<void, std::string>::ok();

    // Find master character_id from members
    int master_char_id = 0;
    for (const auto& m : g.members) {
        if (m.rank == guild_rank::guild_master && m.character_id.is_valid()) {
            master_char_id = static_cast<int>(m.character_id.value);
            break;
        }
    }

    auto rank_json = serialize_rank_configs(g.ranks);

    return database_->execute_transaction([&](pqxx::work& txn) -> result<void, std::string> {
        txn.exec_params(
            "INSERT INTO guilds (id, name, tag, motd, nation, leader_id, level, experience, "
            "total_kills, total_deaths, gold_bank, rank_configs) "
            "VALUES ($1, $2, $3, $4, $5, $6, $7, $8, $9, $10, $11, $12)",
            static_cast<int>(g.id.value), g.name, g.tag, g.motd,
            static_cast<int>(g.faction),
            master_char_id > 0 ? std::optional<int>(master_char_id) : std::nullopt,
            g.level, g.experience, g.total_kills, g.total_deaths, g.gold_bank,
            rank_json);

        for (const auto& m : g.members) {
            if (!m.character_id.is_valid()) continue;
            txn.exec_params(
                "INSERT INTO guild_members (guild_id, character_id, rank, contribution, note) "
                "VALUES ($1, $2, $3, $4, $5)",
                static_cast<int>(g.id.value),
                static_cast<int>(m.character_id.value),
                static_cast<int>(m.rank),
                m.contribution,
                m.note);
        }

        return result<void, std::string>::ok();
    });
}

auto social_system::save_guild_to_database(guild_id gid) -> result<void, std::string> {
    if (!database_) return result<void, std::string>::ok();

    auto* g = get_guild(gid);
    if (!g) return result<void, std::string>::err("Guild not found");

    // Find master character_id from members
    int master_char_id = 0;
    for (const auto& m : g->members) {
        if (m.rank == guild_rank::guild_master && m.character_id.is_valid()) {
            master_char_id = static_cast<int>(m.character_id.value);
            break;
        }
    }

    auto rank_json = serialize_rank_configs(g->ranks);

    return database_->execute_transaction([&](pqxx::work& txn) -> result<void, std::string> {
        txn.exec_params(
            "UPDATE guilds SET tag=$1, motd=$2, nation=$3, leader_id=$4, level=$5, "
            "experience=$6, total_kills=$7, total_deaths=$8, gold_bank=$9, rank_configs=$10 "
            "WHERE id=$11",
            g->tag, g->motd, static_cast<int>(g->faction),
            master_char_id > 0 ? std::optional<int>(master_char_id) : std::nullopt,
            g->level, g->experience, g->total_kills, g->total_deaths, g->gold_bank,
            rank_json,
            static_cast<int>(gid.value));

        txn.exec_params("DELETE FROM guild_members WHERE guild_id=$1",
            static_cast<int>(gid.value));

        for (const auto& m : g->members) {
            if (!m.character_id.is_valid()) continue;
            txn.exec_params(
                "INSERT INTO guild_members (guild_id, character_id, rank, contribution, note) "
                "VALUES ($1, $2, $3, $4, $5)",
                static_cast<int>(gid.value),
                static_cast<int>(m.character_id.value),
                static_cast<int>(m.rank),
                m.contribution,
                m.note);
        }

        return result<void, std::string>::ok();
    });
}

auto social_system::delete_guild_from_database(guild_id gid) -> result<void, std::string> {
    if (!database_) return result<void, std::string>::ok();

    // guild_members cascade-deletes via FK
    auto res = database_->execute_params("DELETE FROM guilds WHERE id=$1",
        static_cast<int>(gid.value));
    if (res.is_err()) return result<void, std::string>::err(res.error());
    return result<void, std::string>::ok();
}

void social_system::register_player(player_id id, const std::string& name) {
    player_names_[id] = name;
    player_chat_settings_.emplace(id, chat_settings{});
    player_rate_limits_.emplace(id, chat_rate_limit{});
    LOG_DEBUG(general, "Registered player {} ({})", id.value, name);
}

void social_system::unregister_player(player_id id) {
    // Leave guild (but don't remove membership - they're still in guild when offline)
    // Leave party
    leave_party(id);

    // Clean up chat state
    player_chat_settings_.erase(id);
    player_rate_limits_.erase(id);
    player_names_.erase(id);

    LOG_DEBUG(general, "Unregistered player {}", id.value);
}

// ========== Guild Operations ==========

auto social_system::create_guild(player_id founder, const std::string& name, const std::string& tag)
    -> result<guild_id, guild_result> {

    // Validate name and tag
    if (!is_valid_guild_name(name)) {
        return result<guild_id, guild_result>::err(guild_result::invalid_name);
    }
    if (!is_valid_guild_tag(tag)) {
        return result<guild_id, guild_result>::err(guild_result::invalid_name);
    }

    // Check if name is taken
    if (find_guild_by_name(name).is_valid()) {
        return result<guild_id, guild_result>::err(guild_result::name_taken);
    }

    // Check if player is already in a guild
    if (get_player_guild(founder).is_valid()) {
        return result<guild_id, guild_result>::err(guild_result::already_in_guild);
    }

    // Create the guild
    guild_id gid = generate_guild_id();
    guild new_guild;
    new_guild.id = gid;
    new_guild.name = name;
    new_guild.tag = tag;
    new_guild.master = founder;
    new_guild.created_at = std::chrono::system_clock::now();

    // Add founder as guild master
    std::string founder_name = player_names_.contains(founder) ? player_names_[founder] : "Unknown";
    new_guild.add_member(founder, founder_name, guild_rank::guild_master);

    // Set character_id on the founder member
    auto founder_char_id = lookup_character_id(founder);
    if (founder_char_id.is_valid()) {
        new_guild.members.back().character_id = founder_char_id;
        character_guild_index_[founder_char_id] = gid;
    }

    guilds_.emplace(gid, std::move(new_guild));
    player_guilds_[founder] = gid;

    // Persist to database
    auto* created_guild = get_guild(gid);
    if (created_guild) {
        auto db_result = insert_guild_to_database(*created_guild);
        if (db_result.is_err()) {
            LOG_ERROR(general, "Failed to persist guild '{}': {}", name, db_result.error());
        }
    }

    // Notify callbacks
    guild_created_event event;
    event.guild = gid;
    event.founder = founder;
    event.guild_name = name;
    for (const auto& callback : guild_created_callbacks_) {
        callback(event);
    }

    LOG_INFO(general, "Guild '{}' created by player {}", name, founder.value);
    return result<guild_id, guild_result>::ok(gid);
}

auto social_system::disband_guild(player_id player, guild_id gid) -> guild_result {
    auto* g = get_guild(gid);
    if (!g) return guild_result::guild_not_found;

    if (!g->has_permission(player, guild_permission::disband)) {
        return guild_result::insufficient_permissions;
    }

    // Remove all members from guild tracking
    for (const auto& member : g->members) {
        if (member.player.is_valid()) player_guilds_.erase(member.player);
        if (member.character_id.is_valid()) character_guild_index_.erase(member.character_id);
    }

    std::string guild_name = g->name;

    // Delete from database
    auto db_result = delete_guild_from_database(gid);
    if (db_result.is_err()) {
        LOG_ERROR(general, "Failed to delete guild '{}' from database: {}", guild_name, db_result.error());
    }

    guilds_.erase(gid);

    LOG_INFO(general, "Guild '{}' disbanded by player {}", guild_name, player.value);
    return guild_result::success;
}

auto social_system::invite_to_guild(player_id inviter, guild_id gid, player_id invitee) -> guild_result {
    auto* g = get_guild(gid);
    if (!g) return guild_result::guild_not_found;

    if (!g->has_permission(inviter, guild_permission::invite)) {
        return guild_result::insufficient_permissions;
    }

    if (g->is_full()) {
        return guild_result::guild_full;
    }

    if (get_player_guild(invitee).is_valid()) {
        return guild_result::already_in_guild;
    }

    // For simplicity, auto-join the guild (in a full implementation, this would create an invite)
    return join_guild(invitee, gid);
}

auto social_system::join_guild(player_id player, guild_id gid) -> guild_result {
    auto* g = get_guild(gid);
    if (!g) return guild_result::guild_not_found;

    if (get_player_guild(player).is_valid()) {
        return guild_result::already_in_guild;
    }

    if (g->is_full()) {
        return guild_result::guild_full;
    }

    std::string player_name = player_names_.contains(player) ? player_names_[player] : "Unknown";
    g->add_member(player, player_name);

    // Set character_id on the new member
    auto char_id = lookup_character_id(player);
    if (char_id.is_valid()) {
        g->members.back().character_id = char_id;
        character_guild_index_[char_id] = gid;
    }

    player_guilds_[player] = gid;

    // Persist
    auto db_result = save_guild_to_database(gid);
    if (db_result.is_err()) {
        LOG_ERROR(general, "Failed to save guild after join: {}", db_result.error());
    }

    LOG_DEBUG(general, "Player {} joined guild '{}'", player.value, g->name);
    return guild_result::success;
}

auto social_system::leave_guild(player_id player) -> guild_result {
    guild_id gid = get_player_guild(player);
    if (!gid.is_valid()) return guild_result::player_not_member;

    auto* g = get_guild(gid);
    if (!g) return guild_result::guild_not_found;

    // Guild master cannot leave (must transfer or disband)
    if (g->master == player && g->member_count() > 1) {
        return guild_result::insufficient_permissions;
    }

    // Get character_id before removing
    auto* leaving_member = g->get_member(player);
    player_id leaving_char_id{};
    if (leaving_member) {
        leaving_char_id = leaving_member->character_id;
    }

    g->remove_member(player);
    player_guilds_.erase(player);
    if (leaving_char_id.is_valid()) {
        character_guild_index_.erase(leaving_char_id);
    }

    // If last member left, disband the guild
    if (g->is_empty()) {
        delete_guild_from_database(gid);
        guilds_.erase(gid);
    } else {
        auto db_result = save_guild_to_database(gid);
        if (db_result.is_err()) {
            LOG_ERROR(general, "Failed to save guild after leave: {}", db_result.error());
        }
    }

    LOG_DEBUG(general, "Player {} left guild", player.value);
    return guild_result::success;
}

auto social_system::kick_from_guild(player_id kicker, player_id target) -> guild_result {
    if (kicker == target) return guild_result::cannot_kick_self;

    guild_id gid = get_player_guild(kicker);
    if (!gid.is_valid()) return guild_result::player_not_member;

    auto* g = get_guild(gid);
    if (!g) return guild_result::guild_not_found;

    if (!g->has_permission(kicker, guild_permission::kick)) {
        return guild_result::insufficient_permissions;
    }

    const auto* target_member = g->get_member(target);
    if (!target_member) return guild_result::player_not_member;

    const auto* kicker_member = g->get_member(kicker);
    if (!kicker_member) return guild_result::player_not_member;

    // Cannot kick someone of equal or higher rank
    if (target_member->rank <= kicker_member->rank) {
        return guild_result::cannot_kick_higher_rank;
    }

    // Get character_id before removing
    player_id kicked_char_id = target_member->character_id;

    g->remove_member(target);
    player_guilds_.erase(target);
    if (kicked_char_id.is_valid()) {
        character_guild_index_.erase(kicked_char_id);
    }

    // Persist
    auto db_result = save_guild_to_database(gid);
    if (db_result.is_err()) {
        LOG_ERROR(general, "Failed to save guild after kick: {}", db_result.error());
    }

    LOG_DEBUG(general, "Player {} kicked player {} from guild", kicker.value, target.value);
    return guild_result::success;
}

auto social_system::promote_member(player_id promoter, player_id target) -> guild_result {
    guild_id gid = get_player_guild(promoter);
    if (!gid.is_valid()) return guild_result::player_not_member;

    auto* g = get_guild(gid);
    if (!g) return guild_result::guild_not_found;

    if (!g->has_permission(promoter, guild_permission::promote)) {
        return guild_result::insufficient_permissions;
    }

    auto* target_member = g->get_member(target);
    if (!target_member) return guild_result::player_not_member;

    auto* promoter_member = g->get_member(promoter);
    if (!promoter_member) return guild_result::player_not_member;

    // Cannot promote to guild master
    if (target_member->rank == guild_rank::officer) {
        return guild_result::cannot_promote_higher;
    }

    // Cannot promote to or past own rank
    uint8_t new_rank = static_cast<uint8_t>(target_member->rank) - 1;
    if (new_rank <= static_cast<uint8_t>(promoter_member->rank) && promoter_member->rank != guild_rank::guild_master) {
        return guild_result::cannot_promote_higher;
    }

    target_member->rank = static_cast<guild_rank>(new_rank);

    // Persist
    auto db_result = save_guild_to_database(gid);
    if (db_result.is_err()) {
        LOG_ERROR(general, "Failed to save guild after promote: {}", db_result.error());
    }

    LOG_DEBUG(general, "Player {} promoted to rank {}", target.value, new_rank);
    return guild_result::success;
}

auto social_system::demote_member(player_id demoter, player_id target) -> guild_result {
    guild_id gid = get_player_guild(demoter);
    if (!gid.is_valid()) return guild_result::player_not_member;

    auto* g = get_guild(gid);
    if (!g) return guild_result::guild_not_found;

    if (!g->has_permission(demoter, guild_permission::demote)) {
        return guild_result::insufficient_permissions;
    }

    auto* target_member = g->get_member(target);
    if (!target_member) return guild_result::player_not_member;

    // Cannot demote below recruit
    if (target_member->rank == guild_rank::recruit) {
        return guild_result::insufficient_permissions;
    }

    target_member->rank = static_cast<guild_rank>(static_cast<uint8_t>(target_member->rank) + 1);

    // Persist
    auto db_result = save_guild_to_database(gid);
    if (db_result.is_err()) {
        LOG_ERROR(general, "Failed to save guild after demote: {}", db_result.error());
    }

    return guild_result::success;
}

auto social_system::set_member_rank(player_id setter, player_id target, guild_rank rank) -> guild_result {
    guild_id gid = get_player_guild(setter);
    if (!gid.is_valid()) return guild_result::player_not_member;

    auto* g = get_guild(gid);
    if (!g) return guild_result::guild_not_found;

    if (!g->has_permission(setter, guild_permission::edit_ranks)) {
        return guild_result::insufficient_permissions;
    }

    if (!g->set_member_rank(target, rank)) {
        return guild_result::player_not_member;
    }

    // Persist
    auto db_result = save_guild_to_database(gid);
    if (db_result.is_err()) {
        LOG_ERROR(general, "Failed to save guild after rank change: {}", db_result.error());
    }

    return guild_result::success;
}

auto social_system::set_guild_motd(player_id player, const std::string& motd) -> guild_result {
    guild_id gid = get_player_guild(player);
    if (!gid.is_valid()) return guild_result::player_not_member;

    auto* g = get_guild(gid);
    if (!g) return guild_result::guild_not_found;

    if (!g->has_permission(player, guild_permission::edit_motd)) {
        return guild_result::insufficient_permissions;
    }

    g->motd = motd;

    // Persist
    auto db_result = save_guild_to_database(gid);
    if (db_result.is_err()) {
        LOG_ERROR(general, "Failed to save guild after motd change: {}", db_result.error());
    }

    return guild_result::success;
}

auto social_system::get_guild(guild_id id) -> guild* {
    auto it = guilds_.find(id);
    return it != guilds_.end() ? &it->second : nullptr;
}

auto social_system::get_guild(guild_id id) const -> const guild* {
    auto it = guilds_.find(id);
    return it != guilds_.end() ? &it->second : nullptr;
}

auto social_system::get_player_guild(player_id player) const -> guild_id {
    auto it = player_guilds_.find(player);
    return it != player_guilds_.end() ? it->second : guild_id{};
}

auto social_system::find_guild_by_name(std::string_view name) const -> guild_id {
    for (const auto& [id, g] : guilds_) {
        if (g.name == name) return id;
    }
    return guild_id{};
}

auto social_system::guild_count() const -> size_t {
    return guilds_.size();
}

// ========== Party Operations ==========

auto social_system::create_party(player_id leader) -> result<party_id, party_result> {
    if (get_player_party(leader).is_valid()) {
        return result<party_id, party_result>::err(party_result::already_in_party);
    }

    party_id pid = generate_party_id();
    party new_party;
    new_party.id = pid;

    std::string leader_name = player_names_.contains(leader) ? player_names_[leader] : "Unknown";
    new_party.add_member(leader, leader_name, 1);

    parties_.emplace(pid, std::move(new_party));
    player_parties_[leader] = pid;

    // Notify callbacks
    party_created_event event;
    event.party = pid;
    event.leader = leader;
    for (const auto& callback : party_created_callbacks_) {
        callback(event);
    }

    LOG_DEBUG(general, "Party created by player {}", leader.value);
    return result<party_id, party_result>::ok(pid);
}

auto social_system::disband_party(player_id leader) -> party_result {
    party_id pid = get_player_party(leader);
    if (!pid.is_valid()) return party_result::party_not_found;

    auto* p = get_party(pid);
    if (!p) return party_result::party_not_found;

    if (!p->is_leader(leader)) return party_result::not_leader;

    // Remove all members from party tracking
    for (const auto& member : p->members) {
        player_parties_.erase(member.player);
    }

    parties_.erase(pid);

    LOG_DEBUG(general, "Party disbanded by player {}", leader.value);
    return party_result::success;
}

auto social_system::invite_to_party(player_id inviter, player_id invitee) -> party_result {
    party_id pid = get_player_party(inviter);

    // Create party if inviter doesn't have one
    if (!pid.is_valid()) {
        auto result = create_party(inviter);
        if (!result.is_ok()) return result.error();
        pid = result.value();
    }

    auto* p = get_party(pid);
    if (!p) return party_result::party_not_found;

    if (!p->is_leader(inviter)) return party_result::not_leader;
    if (p->is_full()) return party_result::party_full;
    if (get_player_party(invitee).is_valid()) return party_result::already_in_party;

    // Add invite
    p->add_invite(inviter, invitee);

    // Also track in pending invites for the invitee
    pending_party_invites_[invitee].push_back(
        party_invite{pid, inviter, invitee,
            std::chrono::steady_clock::now() + party::invite_duration});

    LOG_DEBUG(general, "Player {} invited player {} to party", inviter.value, invitee.value);
    return party_result::success;
}

auto social_system::accept_party_invite(player_id player, party_id pid) -> party_result {
    if (get_player_party(player).is_valid()) {
        return party_result::already_in_party;
    }

    auto* p = get_party(pid);
    if (!p) return party_result::party_not_found;

    if (!p->has_invite(player)) {
        return party_result::no_pending_invite;
    }

    if (p->is_full()) return party_result::party_full;

    p->remove_invite(player);

    // Remove from pending invites
    pending_party_invites_.erase(player);

    std::string player_name = player_names_.contains(player) ? player_names_[player] : "Unknown";
    p->add_member(player, player_name, 1);
    player_parties_[player] = pid;

    LOG_DEBUG(general, "Player {} joined party", player.value);
    return party_result::success;
}

auto social_system::decline_party_invite(player_id player, party_id pid) -> party_result {
    auto* p = get_party(pid);
    if (!p) return party_result::party_not_found;

    if (!p->has_invite(player)) {
        return party_result::no_pending_invite;
    }

    p->remove_invite(player);
    pending_party_invites_.erase(player);

    return party_result::success;
}

auto social_system::leave_party(player_id player) -> party_result {
    party_id pid = get_player_party(player);
    if (!pid.is_valid()) return party_result::party_not_found;

    auto* p = get_party(pid);
    if (!p) return party_result::party_not_found;

    bool was_leader = p->is_leader(player);
    p->remove_member(player);
    player_parties_.erase(player);

    // Disband if empty
    if (p->is_empty()) {
        parties_.erase(pid);
    }
    // Note: Solo parties (1 member) are kept alive so they can invite others

    LOG_DEBUG(general, "Player {} left party", player.value);
    return party_result::success;
}

auto social_system::kick_from_party(player_id kicker, player_id target) -> party_result {
    if (kicker == target) return party_result::cannot_kick_self;

    party_id pid = get_player_party(kicker);
    if (!pid.is_valid()) return party_result::party_not_found;

    auto* p = get_party(pid);
    if (!p) return party_result::party_not_found;

    if (!p->is_leader(kicker)) return party_result::not_leader;
    if (!p->is_member(target)) return party_result::player_not_member;

    p->remove_member(target);
    player_parties_.erase(target);

    LOG_DEBUG(general, "Player {} kicked from party by {}", target.value, kicker.value);
    return party_result::success;
}

auto social_system::set_party_leader(player_id current_leader, player_id new_leader) -> party_result {
    party_id pid = get_player_party(current_leader);
    if (!pid.is_valid()) return party_result::party_not_found;

    auto* p = get_party(pid);
    if (!p) return party_result::party_not_found;

    if (!p->is_leader(current_leader)) return party_result::not_leader;
    if (!p->is_member(new_leader)) return party_result::player_not_member;

    p->set_leader(new_leader);

    LOG_DEBUG(general, "Party leadership transferred from {} to {}", current_leader.value, new_leader.value);
    return party_result::success;
}

auto social_system::set_loot_mode(player_id leader, loot_mode mode) -> party_result {
    party_id pid = get_player_party(leader);
    if (!pid.is_valid()) return party_result::party_not_found;

    auto* p = get_party(pid);
    if (!p) return party_result::party_not_found;

    if (!p->is_leader(leader)) return party_result::not_leader;

    p->loot = mode;
    return party_result::success;
}

auto social_system::set_exp_mode(player_id leader, exp_mode mode) -> party_result {
    party_id pid = get_player_party(leader);
    if (!pid.is_valid()) return party_result::party_not_found;

    auto* p = get_party(pid);
    if (!p) return party_result::party_not_found;

    if (!p->is_leader(leader)) return party_result::not_leader;

    p->experience = mode;
    return party_result::success;
}

auto social_system::get_party(party_id id) -> party* {
    auto it = parties_.find(id);
    return it != parties_.end() ? &it->second : nullptr;
}

auto social_system::get_party(party_id id) const -> const party* {
    auto it = parties_.find(id);
    return it != parties_.end() ? &it->second : nullptr;
}

auto social_system::get_player_party(player_id player) const -> party_id {
    auto it = player_parties_.find(player);
    return it != player_parties_.end() ? it->second : party_id{};
}

auto social_system::has_party_invite(player_id player) const -> bool {
    auto it = pending_party_invites_.find(player);
    if (it == pending_party_invites_.end()) return false;

    for (const auto& invite : it->second) {
        if (!invite.is_expired()) return true;
    }
    return false;
}

auto social_system::party_count() const -> size_t {
    return parties_.size();
}

void social_system::update_party_member_stats(player_id player, int32_t hp, int32_t max_hp, int32_t mp, int32_t max_mp) {
    party_id pid = get_player_party(player);
    if (!pid.is_valid()) return;

    auto* p = get_party(pid);
    if (!p) return;

    auto* member = p->get_member(player);
    if (!member) return;

    member->hp = hp;
    member->max_hp = max_hp;
    member->mp = mp;
    member->max_mp = max_mp;
}

void social_system::update_party_member_map(player_id player, map_id map) {
    party_id pid = get_player_party(player);
    if (!pid.is_valid()) return;

    auto* p = get_party(pid);
    if (!p) return;

    auto* member = p->get_member(player);
    if (member) {
        member->current_map = map;
    }
}

// ========== Chat Operations ==========

auto social_system::send_message(const chat_message& msg) -> filter_result {
    // Check rate limit
    if (!check_rate_limit(msg.sender)) {
        return filter_result::rate_limited;
    }

    // Filter message
    auto [filter_res, filtered_content] = filter_message(msg.content);
    if (filter_res == filter_result::blocked) {
        return filter_result::blocked;
    }

    // Create final message
    chat_message final_msg = msg;
    if (filter_res == filter_result::censored) {
        final_msg.content = filtered_content;
        final_msg.flags = final_msg.flags | chat_flags::censored;
    }
    final_msg.timestamp = std::chrono::system_clock::now();

    // Set sender name
    if (player_names_.contains(msg.sender)) {
        final_msg.sender_name = player_names_[msg.sender];
    }

    // Notify callbacks
    chat_message_event event;
    event.message = final_msg;
    for (const auto& callback : chat_callbacks_) {
        callback(event);
    }

    return filter_res;
}

auto social_system::send_local_chat(player_id sender, const std::string& content, map_id map, int16_t x, int16_t y)
    -> filter_result {
    chat_message msg;
    msg.sender = sender;
    msg.channel = chat_channel::local;
    msg.content = content;
    msg.map = map;
    msg.x = x;
    msg.y = y;
    return send_message(msg);
}

auto social_system::send_global_chat(player_id sender, const std::string& content) -> filter_result {
    chat_message msg;
    msg.sender = sender;
    msg.channel = chat_channel::global;
    msg.content = content;
    return send_message(msg);
}

auto social_system::send_guild_chat(player_id sender, const std::string& content) -> filter_result {
    guild_id gid = get_player_guild(sender);
    if (!gid.is_valid()) return filter_result::blocked;

    chat_message msg;
    msg.sender = sender;
    msg.channel = chat_channel::guild;
    msg.content = content;
    return send_message(msg);
}

auto social_system::send_party_chat(player_id sender, const std::string& content) -> filter_result {
    party_id pid = get_player_party(sender);
    if (!pid.is_valid()) return filter_result::blocked;

    chat_message msg;
    msg.sender = sender;
    msg.channel = chat_channel::party;
    msg.content = content;
    return send_message(msg);
}

auto social_system::send_whisper(player_id sender, player_id recipient, const std::string& content) -> filter_result {
    // Check if recipient has sender blocked
    auto* recipient_settings = get_chat_settings(recipient);
    if (recipient_settings && recipient_settings->is_player_blocked(sender)) {
        return filter_result::blocked;
    }

    chat_message msg;
    msg.sender = sender;
    msg.recipient = recipient;
    msg.channel = chat_channel::whisper;
    msg.content = content;

    if (player_names_.contains(recipient)) {
        msg.recipient_name = player_names_[recipient];
    }

    return send_message(msg);
}

auto social_system::send_system_message(player_id recipient, const std::string& content) -> filter_result {
    chat_message msg;
    msg.sender = player_id{};
    msg.recipient = recipient;
    msg.channel = chat_channel::system;
    msg.content = content;
    msg.flags = chat_flags::system;
    return send_message(msg);
}

auto social_system::get_chat_settings(player_id player) -> chat_settings* {
    auto it = player_chat_settings_.find(player);
    return it != player_chat_settings_.end() ? &it->second : nullptr;
}

auto social_system::block_player(player_id blocker, player_id blocked) -> bool {
    auto* settings = get_chat_settings(blocker);
    if (!settings) return false;
    return settings->block_player(blocked);
}

auto social_system::unblock_player(player_id blocker, player_id blocked) -> bool {
    auto* settings = get_chat_settings(blocker);
    if (!settings) return false;
    return settings->unblock_player(blocked);
}

// ========== Callbacks ==========

void social_system::on_guild_created(std::function<void(const guild_created_event&)> callback) {
    guild_created_callbacks_.push_back(std::move(callback));
}

void social_system::on_party_created(std::function<void(const party_created_event&)> callback) {
    party_created_callbacks_.push_back(std::move(callback));
}

void social_system::on_chat_message(chat_callback callback) {
    chat_callbacks_.push_back(std::move(callback));
}

// ========== Private Helpers ==========

auto social_system::filter_message(const std::string& content) -> std::pair<filter_result, std::string> {
    if (!config_.enable_profanity_filter || config_.profanity_words.empty()) {
        return {filter_result::allowed, content};
    }

    std::string filtered = content;
    bool was_censored = false;

    for (const auto& word : config_.profanity_words) {
        size_t pos = 0;
        std::string lower_content = filtered;
        std::transform(lower_content.begin(), lower_content.end(), lower_content.begin(),
            [](unsigned char c) { return std::tolower(c); });

        while ((pos = lower_content.find(word, pos)) != std::string::npos) {
            // Replace with asterisks
            for (size_t i = 0; i < word.length(); ++i) {
                filtered[pos + i] = '*';
            }
            was_censored = true;
            pos += word.length();
        }
    }

    return {was_censored ? filter_result::censored : filter_result::allowed, filtered};
}

auto social_system::is_valid_guild_name(const std::string& name) const -> bool {
    if (name.length() < static_cast<size_t>(config_.min_guild_name_length) ||
        name.length() > static_cast<size_t>(config_.max_guild_name_length)) {
        return false;
    }

    // Check for valid characters (alphanumeric and spaces)
    for (char c : name) {
        if (!std::isalnum(static_cast<unsigned char>(c)) && c != ' ') {
            return false;
        }
    }

    return true;
}

auto social_system::is_valid_guild_tag(const std::string& tag) const -> bool {
    if (tag.length() < static_cast<size_t>(config_.min_guild_tag_length) ||
        tag.length() > static_cast<size_t>(config_.max_guild_tag_length)) {
        return false;
    }

    // Check for valid characters (alphanumeric only)
    for (char c : tag) {
        if (!std::isalnum(static_cast<unsigned char>(c))) {
            return false;
        }
    }

    return true;
}

auto social_system::check_rate_limit(player_id player) -> bool {
    auto it = player_rate_limits_.find(player);
    if (it == player_rate_limits_.end()) return true;

    auto& limit = it->second;
    if (!limit.can_send()) return false;

    limit.record_message();
    return true;
}

void social_system::cleanup_expired_invites() {
    // Clean up party invites
    for (auto& [player, invites] : pending_party_invites_) {
        invites.erase(
            std::remove_if(invites.begin(), invites.end(),
                [](const party_invite& inv) { return inv.is_expired(); }),
            invites.end()
        );
    }

    // Clean up invites in parties
    for (auto& [id, p] : parties_) {
        p.cleanup_invites();
    }
}

auto social_system::generate_guild_id() -> guild_id {
    return guild_id{static_cast<uint16_t>(next_guild_id_++)};
}

auto social_system::generate_party_id() -> party_id {
    return party_id{next_party_id_++};
}

}  // namespace hb::social
