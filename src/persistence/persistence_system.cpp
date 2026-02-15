// persistence_system.cpp
// Persistence subsystem implementation

#include "persistence/persistence_system.h"
#include "core/logger.h"

#include <algorithm>
#include <chrono>

namespace hb::persistence
{

// File header magic and version
constexpr uint32_t player_file_magic = 0x50445648; // "HVDP" - Helbreath Data Player
constexpr uint32_t guild_file_magic = 0x47445648;  // "HVDG" - Helbreath Data Guild
constexpr uint16_t file_version = 1;

persistence_system::persistence_system() = default;

persistence_system::~persistence_system()
{
    if (is_initialized())
    {
        shutdown();
    }
}

void persistence_system::initialize()
{
    LOG_INFO(general, "Persistence system initializing...");

    ensure_directories();

    set_initialized(true);
    LOG_INFO(general, "Persistence system initialized");
}

void persistence_system::shutdown()
{
    LOG_INFO(general, "Persistence system shutting down...");

    // Final save
    do_auto_save();

    player_cache_.clear();
    guild_cache_.clear();
    save_providers_.clear();
    save_callbacks_.clear();
    load_callbacks_.clear();

    set_initialized(false);
    LOG_INFO(general, "Persistence system shutdown complete");
}

void persistence_system::update(float delta_time)
{
    if (!config_.auto_save_enabled)
        return;

    time_since_auto_save_ += delta_time;
    if (time_since_auto_save_ >= static_cast<float>(config_.auto_save_interval_seconds))
    {
        do_auto_save();
        time_since_auto_save_ = 0.0f;
    }
}

void persistence_system::set_config(const persistence_config& config)
{
    config_ = config;
    ensure_directories();
}

// ========== Player Data ==========

auto persistence_system::save_player(const player_save_data& data) -> persist_result
{
    binary_writer writer;
    serialize_player(data, writer);

    auto path = get_player_path(data.id);
    auto result = write_file(path, writer.data());

    if (result == persist_result::success)
    {
        player_cache_[data.id] = data;
        last_save_ = std::chrono::system_clock::now();

        // Notify callbacks
        save_completed_event event;
        event.player = data.id;
        event.success = true;
        for (const auto& callback : save_callbacks_)
        {
            callback(event);
        }

        LOG_DEBUG(general, "Saved player {} ({})", data.id.value, data.name);
    }

    return result;
}

auto persistence_system::load_player(player_id id) -> result<player_save_data, persist_result>
{
    // Check cache first
    auto cache_it = player_cache_.find(id);
    if (cache_it != player_cache_.end())
    {
        return result<player_save_data, persist_result>::ok(cache_it->second);
    }

    // Load from file
    auto path = get_player_path(id);
    auto file_result = read_file(path);
    if (!file_result.is_ok())
    {
        return result<player_save_data, persist_result>::err(file_result.error());
    }

    binary_reader reader(file_result.value());
    auto data_result = deserialize_player(reader);

    if (data_result.is_ok())
    {
        player_cache_[id] = data_result.value();

        // Notify callbacks
        load_completed_event event;
        event.player = id;
        event.success = true;
        for (const auto& callback : load_callbacks_)
        {
            callback(event);
        }
    }

    return data_result;
}

auto persistence_system::delete_player(player_id id) -> persist_result
{
    auto path = get_player_path(id);

    std::error_code ec;
    if (std::filesystem::remove(path, ec))
    {
        player_cache_.erase(id);
        LOG_DEBUG(general, "Deleted player {}", id.value);
        return persist_result::success;
    }

    if (ec)
    {
        LOG_ERROR(general, "Failed to delete player {}: {}", id.value, ec.message());
        return persist_result::io_error;
    }

    return persist_result::not_found;
}

auto persistence_system::player_exists(player_id id) -> bool
{
    if (player_cache_.contains(id))
        return true;
    return std::filesystem::exists(get_player_path(id));
}

auto persistence_system::load_player_by_name(const std::string& name) -> result<player_save_data, persist_result>
{
    // Check cache
    for (const auto& [id, data] : player_cache_)
    {
        if (data.name == name)
        {
            return result<player_save_data, persist_result>::ok(data);
        }
    }

    // Would need to scan files or maintain an index
    // For now, return not found
    return result<player_save_data, persist_result>::err(persist_result::not_found);
}

auto persistence_system::get_all_player_ids() -> std::vector<player_id>
{
    std::vector<player_id> ids;

    auto dir = config_.save_directory / config_.player_directory;
    if (!std::filesystem::exists(dir))
        return ids;

    for (const auto& entry : std::filesystem::directory_iterator(dir))
    {
        if (entry.is_regular_file() && entry.path().extension() == ".dat")
        {
            try
            {
                auto stem = entry.path().stem().string();
                uint32_t id_value = static_cast<uint32_t>(std::stoul(stem));
                ids.push_back(player_id{id_value});
            }
            catch (...)
            {
                // Skip invalid filenames
            }
        }
    }

    return ids;
}

// ========== Guild Data ==========

auto persistence_system::save_guild(const guild_save_data& data) -> persist_result
{
    binary_writer writer;
    serialize_guild(data, writer);

    auto path = get_guild_path(data.id);
    auto result = write_file(path, writer.data());

    if (result == persist_result::success)
    {
        guild_cache_[data.id] = data;
        last_save_ = std::chrono::system_clock::now();
        LOG_DEBUG(general, "Saved guild {} ({})", data.id.value, data.name);
    }

    return result;
}

auto persistence_system::load_guild(guild_id id) -> result<guild_save_data, persist_result>
{
    // Check cache
    auto cache_it = guild_cache_.find(id);
    if (cache_it != guild_cache_.end())
    {
        return result<guild_save_data, persist_result>::ok(cache_it->second);
    }

    // Load from file
    auto path = get_guild_path(id);
    auto file_result = read_file(path);
    if (!file_result.is_ok())
    {
        return result<guild_save_data, persist_result>::err(file_result.error());
    }

    binary_reader reader(file_result.value());
    auto data_result = deserialize_guild(reader);

    if (data_result.is_ok())
    {
        guild_cache_[id] = data_result.value();
    }

    return data_result;
}

auto persistence_system::delete_guild(guild_id id) -> persist_result
{
    auto path = get_guild_path(id);

    std::error_code ec;
    if (std::filesystem::remove(path, ec))
    {
        guild_cache_.erase(id);
        LOG_DEBUG(general, "Deleted guild {}", id.value);
        return persist_result::success;
    }

    if (ec)
    {
        LOG_ERROR(general, "Failed to delete guild {}: {}", id.value, ec.message());
        return persist_result::io_error;
    }

    return persist_result::not_found;
}

auto persistence_system::guild_exists(guild_id id) -> bool
{
    if (guild_cache_.contains(id))
        return true;
    return std::filesystem::exists(get_guild_path(id));
}

auto persistence_system::load_guild_by_name(const std::string& name) -> result<guild_save_data, persist_result>
{
    // Check cache
    for (const auto& [id, data] : guild_cache_)
    {
        if (data.name == name)
        {
            return result<guild_save_data, persist_result>::ok(data);
        }
    }

    return result<guild_save_data, persist_result>::err(persist_result::not_found);
}

auto persistence_system::get_all_guild_ids() -> std::vector<guild_id>
{
    std::vector<guild_id> ids;

    auto dir = config_.save_directory / config_.guild_directory;
    if (!std::filesystem::exists(dir))
        return ids;

    for (const auto& entry : std::filesystem::directory_iterator(dir))
    {
        if (entry.is_regular_file() && entry.path().extension() == ".dat")
        {
            try
            {
                auto stem = entry.path().stem().string();
                uint16_t id_value = static_cast<uint16_t>(std::stoul(stem));
                ids.push_back(guild_id{id_value});
            }
            catch (...)
            {
                // Skip invalid filenames
            }
        }
    }

    return ids;
}

// ========== Auto-Save ==========

void persistence_system::trigger_auto_save()
{
    do_auto_save();
}

void persistence_system::register_save_target(player_id player, std::function<player_save_data()> provider)
{
    save_providers_[player] = std::move(provider);
}

void persistence_system::unregister_save_target(player_id player)
{
    save_providers_.erase(player);
}

// ========== Backup ==========

auto persistence_system::create_backup(const std::string& name) -> persist_result
{
    auto backup_dir = config_.save_directory / "backups" / name;

    std::error_code ec;
    std::filesystem::create_directories(backup_dir, ec);
    if (ec)
    {
        LOG_ERROR(general, "Failed to create backup directory: {}", ec.message());
        return persist_result::io_error;
    }

    // Copy player directory
    auto src_players = config_.save_directory / config_.player_directory;
    auto dst_players = backup_dir / config_.player_directory;
    if (std::filesystem::exists(src_players))
    {
        std::filesystem::copy(src_players,
                              dst_players,
                              std::filesystem::copy_options::recursive |
                                  std::filesystem::copy_options::overwrite_existing,
                              ec);
        if (ec)
        {
            LOG_ERROR(general, "Failed to backup players: {}", ec.message());
            return persist_result::io_error;
        }
    }

    // Copy guild directory
    auto src_guilds = config_.save_directory / config_.guild_directory;
    auto dst_guilds = backup_dir / config_.guild_directory;
    if (std::filesystem::exists(src_guilds))
    {
        std::filesystem::copy(src_guilds,
                              dst_guilds,
                              std::filesystem::copy_options::recursive |
                                  std::filesystem::copy_options::overwrite_existing,
                              ec);
        if (ec)
        {
            LOG_ERROR(general, "Failed to backup guilds: {}", ec.message());
            return persist_result::io_error;
        }
    }

    LOG_INFO(general, "Created backup: {}", name);
    cleanup_old_backups();
    return persist_result::success;
}

auto persistence_system::restore_backup(const std::string& name) -> persist_result
{
    auto backup_dir = config_.save_directory / "backups" / name;

    if (!std::filesystem::exists(backup_dir))
    {
        return persist_result::not_found;
    }

    std::error_code ec;

    // Restore player directory
    auto src_players = backup_dir / config_.player_directory;
    auto dst_players = config_.save_directory / config_.player_directory;
    if (std::filesystem::exists(src_players))
    {
        std::filesystem::copy(src_players,
                              dst_players,
                              std::filesystem::copy_options::recursive |
                                  std::filesystem::copy_options::overwrite_existing,
                              ec);
        if (ec)
        {
            LOG_ERROR(general, "Failed to restore players: {}", ec.message());
            return persist_result::io_error;
        }
    }

    // Restore guild directory
    auto src_guilds = backup_dir / config_.guild_directory;
    auto dst_guilds = config_.save_directory / config_.guild_directory;
    if (std::filesystem::exists(src_guilds))
    {
        std::filesystem::copy(src_guilds,
                              dst_guilds,
                              std::filesystem::copy_options::recursive |
                                  std::filesystem::copy_options::overwrite_existing,
                              ec);
        if (ec)
        {
            LOG_ERROR(general, "Failed to restore guilds: {}", ec.message());
            return persist_result::io_error;
        }
    }

    // Clear caches
    player_cache_.clear();
    guild_cache_.clear();

    LOG_INFO(general, "Restored backup: {}", name);
    return persist_result::success;
}

auto persistence_system::list_backups() -> std::vector<std::string>
{
    std::vector<std::string> backups;

    auto backup_dir = config_.save_directory / "backups";
    if (!std::filesystem::exists(backup_dir))
        return backups;

    for (const auto& entry : std::filesystem::directory_iterator(backup_dir))
    {
        if (entry.is_directory())
        {
            backups.push_back(entry.path().filename().string());
        }
    }

    std::sort(backups.begin(), backups.end());
    return backups;
}

// ========== Serialization Helpers ==========

void persistence_system::serialize_player(const player_save_data& data, binary_writer& writer)
{
    // Header
    writer.write_uint32(player_file_magic);
    writer.write_uint16(file_version);

    // Identity
    writer.write_uint32(data.id.value);
    writer.write_string(data.name);
    writer.write_string(data.account_name);

    // Character info
    writer.write_uint8(data.nation);
    writer.write_uint8(data.gender);
    writer.write_uint8(data.skin_color);
    writer.write_uint8(data.hair_style);
    writer.write_uint8(data.hair_color);
    writer.write_uint8(data.underwear);

    // Location
    writer.write_uint8(data.current_map.value);
    writer.write_int16(data.x);
    writer.write_int16(data.y);

    // Stats
    writer.write_int16(data.level);
    writer.write_int64(data.experience);
    writer.write_int32(data.hp);
    writer.write_int32(data.max_hp);
    writer.write_int32(data.mp);
    writer.write_int32(data.max_mp);
    writer.write_int32(data.sp);
    writer.write_int32(data.max_sp);

    // Attributes
    writer.write_int16(data.strength);
    writer.write_int16(data.vitality);
    writer.write_int16(data.dexterity);
    writer.write_int16(data.intelligence);
    writer.write_int16(data.magic);
    writer.write_int16(data.charisma);
    writer.write_int16(data.luck);

    // Combat
    writer.write_int32(data.ek_count);
    writer.write_int32(data.pk_count);
    writer.write_int32(data.contribution);

    // Currency
    writer.write_int64(data.gold);
    writer.write_int64(data.bank_gold);

    // Guild
    writer.write_uint16(data.guild.value);
    writer.write_uint8(data.guild_rank);

    // Timestamps
    writer.write_int64(data.created_timestamp);
    writer.write_int64(data.last_login_timestamp);
    writer.write_int32(data.play_time_seconds);

    // Serialized blobs
    writer.write_uint32(static_cast<uint32_t>(data.inventory_data.size()));
    writer.write_bytes(data.inventory_data.data(), data.inventory_data.size());

    writer.write_uint32(static_cast<uint32_t>(data.skills_data.size()));
    writer.write_bytes(data.skills_data.data(), data.skills_data.size());

    writer.write_uint32(static_cast<uint32_t>(data.quests_data.size()));
    writer.write_bytes(data.quests_data.data(), data.quests_data.size());

    writer.write_uint32(static_cast<uint32_t>(data.equipment_data.size()));
    writer.write_bytes(data.equipment_data.data(), data.equipment_data.size());
}

auto persistence_system::deserialize_player(binary_reader& reader) -> result<player_save_data, persist_result>
{
    player_save_data data;

    // Header
    auto magic = reader.read_uint32();
    if (!magic.is_ok() || magic.value() != player_file_magic)
    {
        return result<player_save_data, persist_result>::err(persist_result::parse_error);
    }

    auto version = reader.read_uint16();
    if (!version.is_ok())
    {
        return result<player_save_data, persist_result>::err(persist_result::parse_error);
    }

    // Identity
    auto id = reader.read_uint32();
    if (!id.is_ok())
        return result<player_save_data, persist_result>::err(persist_result::parse_error);
    data.id = player_id{id.value()};

    auto name = reader.read_string();
    if (!name.is_ok())
        return result<player_save_data, persist_result>::err(persist_result::parse_error);
    data.name = name.value();

    auto account = reader.read_string();
    if (!account.is_ok())
        return result<player_save_data, persist_result>::err(persist_result::parse_error);
    data.account_name = account.value();

    // Character info
    auto nation = reader.read_uint8();
    if (!nation.is_ok())
        return result<player_save_data, persist_result>::err(persist_result::parse_error);
    data.nation = nation.value();

    auto gender = reader.read_uint8();
    if (!gender.is_ok())
        return result<player_save_data, persist_result>::err(persist_result::parse_error);
    data.gender = gender.value();

    auto skin = reader.read_uint8();
    if (!skin.is_ok())
        return result<player_save_data, persist_result>::err(persist_result::parse_error);
    data.skin_color = skin.value();

    auto hair_style = reader.read_uint8();
    if (!hair_style.is_ok())
        return result<player_save_data, persist_result>::err(persist_result::parse_error);
    data.hair_style = hair_style.value();

    auto hair_color = reader.read_uint8();
    if (!hair_color.is_ok())
        return result<player_save_data, persist_result>::err(persist_result::parse_error);
    data.hair_color = hair_color.value();

    auto underwear = reader.read_uint8();
    if (!underwear.is_ok())
        return result<player_save_data, persist_result>::err(persist_result::parse_error);
    data.underwear = underwear.value();

    // Location
    auto map = reader.read_uint8();
    if (!map.is_ok())
        return result<player_save_data, persist_result>::err(persist_result::parse_error);
    data.current_map = map_id{map.value()};

    auto x = reader.read_int16();
    if (!x.is_ok())
        return result<player_save_data, persist_result>::err(persist_result::parse_error);
    data.x = x.value();

    auto y = reader.read_int16();
    if (!y.is_ok())
        return result<player_save_data, persist_result>::err(persist_result::parse_error);
    data.y = y.value();

    // Stats
    auto level = reader.read_int16();
    if (!level.is_ok())
        return result<player_save_data, persist_result>::err(persist_result::parse_error);
    data.level = level.value();

    auto exp = reader.read_int64();
    if (!exp.is_ok())
        return result<player_save_data, persist_result>::err(persist_result::parse_error);
    data.experience = exp.value();

    auto hp = reader.read_int32();
    if (!hp.is_ok())
        return result<player_save_data, persist_result>::err(persist_result::parse_error);
    data.hp = hp.value();

    auto max_hp = reader.read_int32();
    if (!max_hp.is_ok())
        return result<player_save_data, persist_result>::err(persist_result::parse_error);
    data.max_hp = max_hp.value();

    auto mp = reader.read_int32();
    if (!mp.is_ok())
        return result<player_save_data, persist_result>::err(persist_result::parse_error);
    data.mp = mp.value();

    auto max_mp = reader.read_int32();
    if (!max_mp.is_ok())
        return result<player_save_data, persist_result>::err(persist_result::parse_error);
    data.max_mp = max_mp.value();

    auto sp = reader.read_int32();
    if (!sp.is_ok())
        return result<player_save_data, persist_result>::err(persist_result::parse_error);
    data.sp = sp.value();

    auto max_sp = reader.read_int32();
    if (!max_sp.is_ok())
        return result<player_save_data, persist_result>::err(persist_result::parse_error);
    data.max_sp = max_sp.value();

    // Attributes
    auto str = reader.read_int16();
    if (!str.is_ok())
        return result<player_save_data, persist_result>::err(persist_result::parse_error);
    data.strength = str.value();

    auto vit = reader.read_int16();
    if (!vit.is_ok())
        return result<player_save_data, persist_result>::err(persist_result::parse_error);
    data.vitality = vit.value();

    auto dex = reader.read_int16();
    if (!dex.is_ok())
        return result<player_save_data, persist_result>::err(persist_result::parse_error);
    data.dexterity = dex.value();

    auto intel = reader.read_int16();
    if (!intel.is_ok())
        return result<player_save_data, persist_result>::err(persist_result::parse_error);
    data.intelligence = intel.value();

    auto mag = reader.read_int16();
    if (!mag.is_ok())
        return result<player_save_data, persist_result>::err(persist_result::parse_error);
    data.magic = mag.value();

    auto cha = reader.read_int16();
    if (!cha.is_ok())
        return result<player_save_data, persist_result>::err(persist_result::parse_error);
    data.charisma = cha.value();

    auto luck = reader.read_int16();
    if (!luck.is_ok())
        return result<player_save_data, persist_result>::err(persist_result::parse_error);
    data.luck = luck.value();

    // Combat
    auto ek = reader.read_int32();
    if (!ek.is_ok())
        return result<player_save_data, persist_result>::err(persist_result::parse_error);
    data.ek_count = ek.value();

    auto pk = reader.read_int32();
    if (!pk.is_ok())
        return result<player_save_data, persist_result>::err(persist_result::parse_error);
    data.pk_count = pk.value();

    auto contrib = reader.read_int32();
    if (!contrib.is_ok())
        return result<player_save_data, persist_result>::err(persist_result::parse_error);
    data.contribution = contrib.value();

    // Currency
    auto gold = reader.read_int64();
    if (!gold.is_ok())
        return result<player_save_data, persist_result>::err(persist_result::parse_error);
    data.gold = gold.value();

    auto bank_gold = reader.read_int64();
    if (!bank_gold.is_ok())
        return result<player_save_data, persist_result>::err(persist_result::parse_error);
    data.bank_gold = bank_gold.value();

    // Guild
    auto guild = reader.read_uint16();
    if (!guild.is_ok())
        return result<player_save_data, persist_result>::err(persist_result::parse_error);
    data.guild = guild_id{guild.value()};

    auto guild_rank = reader.read_uint8();
    if (!guild_rank.is_ok())
        return result<player_save_data, persist_result>::err(persist_result::parse_error);
    data.guild_rank = guild_rank.value();

    // Timestamps
    auto created = reader.read_int64();
    if (!created.is_ok())
        return result<player_save_data, persist_result>::err(persist_result::parse_error);
    data.created_timestamp = created.value();

    auto last_login = reader.read_int64();
    if (!last_login.is_ok())
        return result<player_save_data, persist_result>::err(persist_result::parse_error);
    data.last_login_timestamp = last_login.value();

    auto play_time = reader.read_int32();
    if (!play_time.is_ok())
        return result<player_save_data, persist_result>::err(persist_result::parse_error);
    data.play_time_seconds = play_time.value();

    // Serialized blobs
    auto inv_size = reader.read_uint32();
    if (!inv_size.is_ok())
        return result<player_save_data, persist_result>::err(persist_result::parse_error);
    auto inv_data = reader.read_bytes(inv_size.value());
    if (!inv_data.is_ok())
        return result<player_save_data, persist_result>::err(persist_result::parse_error);
    data.inventory_data = inv_data.value();

    auto skills_size = reader.read_uint32();
    if (!skills_size.is_ok())
        return result<player_save_data, persist_result>::err(persist_result::parse_error);
    auto skills_data = reader.read_bytes(skills_size.value());
    if (!skills_data.is_ok())
        return result<player_save_data, persist_result>::err(persist_result::parse_error);
    data.skills_data = skills_data.value();

    auto quests_size = reader.read_uint32();
    if (!quests_size.is_ok())
        return result<player_save_data, persist_result>::err(persist_result::parse_error);
    auto quests_data = reader.read_bytes(quests_size.value());
    if (!quests_data.is_ok())
        return result<player_save_data, persist_result>::err(persist_result::parse_error);
    data.quests_data = quests_data.value();

    auto equip_size = reader.read_uint32();
    if (!equip_size.is_ok())
        return result<player_save_data, persist_result>::err(persist_result::parse_error);
    auto equip_data = reader.read_bytes(equip_size.value());
    if (!equip_data.is_ok())
        return result<player_save_data, persist_result>::err(persist_result::parse_error);
    data.equipment_data = equip_data.value();

    return result<player_save_data, persist_result>::ok(std::move(data));
}

void persistence_system::serialize_guild(const guild_save_data& data, binary_writer& writer)
{
    // Header
    writer.write_uint32(guild_file_magic);
    writer.write_uint16(file_version);

    // Identity
    writer.write_uint16(data.id.value);
    writer.write_string(data.name);
    writer.write_string(data.tag);
    writer.write_string(data.motd);
    writer.write_uint32(data.master.value);
    writer.write_uint8(data.faction);

    // Stats
    writer.write_int64(data.gold_bank);
    writer.write_int32(data.level);
    writer.write_int64(data.experience);
    writer.write_int64(data.total_kills);
    writer.write_int64(data.total_deaths);
    writer.write_int64(data.created_timestamp);

    // Serialized blobs
    writer.write_uint32(static_cast<uint32_t>(data.members_data.size()));
    writer.write_bytes(data.members_data.data(), data.members_data.size());

    writer.write_uint32(static_cast<uint32_t>(data.ranks_data.size()));
    writer.write_bytes(data.ranks_data.data(), data.ranks_data.size());
}

auto persistence_system::deserialize_guild(binary_reader& reader) -> result<guild_save_data, persist_result>
{
    guild_save_data data;

    // Header
    auto magic = reader.read_uint32();
    if (!magic.is_ok() || magic.value() != guild_file_magic)
    {
        return result<guild_save_data, persist_result>::err(persist_result::parse_error);
    }

    auto version = reader.read_uint16();
    if (!version.is_ok())
    {
        return result<guild_save_data, persist_result>::err(persist_result::parse_error);
    }

    // Identity
    auto id = reader.read_uint16();
    if (!id.is_ok())
        return result<guild_save_data, persist_result>::err(persist_result::parse_error);
    data.id = guild_id{id.value()};

    auto name = reader.read_string();
    if (!name.is_ok())
        return result<guild_save_data, persist_result>::err(persist_result::parse_error);
    data.name = name.value();

    auto tag = reader.read_string();
    if (!tag.is_ok())
        return result<guild_save_data, persist_result>::err(persist_result::parse_error);
    data.tag = tag.value();

    auto motd = reader.read_string();
    if (!motd.is_ok())
        return result<guild_save_data, persist_result>::err(persist_result::parse_error);
    data.motd = motd.value();

    auto master = reader.read_uint32();
    if (!master.is_ok())
        return result<guild_save_data, persist_result>::err(persist_result::parse_error);
    data.master = player_id{master.value()};

    auto faction = reader.read_uint8();
    if (!faction.is_ok())
        return result<guild_save_data, persist_result>::err(persist_result::parse_error);
    data.faction = faction.value();

    // Stats
    auto gold = reader.read_int64();
    if (!gold.is_ok())
        return result<guild_save_data, persist_result>::err(persist_result::parse_error);
    data.gold_bank = gold.value();

    auto level = reader.read_int32();
    if (!level.is_ok())
        return result<guild_save_data, persist_result>::err(persist_result::parse_error);
    data.level = level.value();

    auto exp = reader.read_int64();
    if (!exp.is_ok())
        return result<guild_save_data, persist_result>::err(persist_result::parse_error);
    data.experience = exp.value();

    auto kills = reader.read_int64();
    if (!kills.is_ok())
        return result<guild_save_data, persist_result>::err(persist_result::parse_error);
    data.total_kills = kills.value();

    auto deaths = reader.read_int64();
    if (!deaths.is_ok())
        return result<guild_save_data, persist_result>::err(persist_result::parse_error);
    data.total_deaths = deaths.value();

    auto created = reader.read_int64();
    if (!created.is_ok())
        return result<guild_save_data, persist_result>::err(persist_result::parse_error);
    data.created_timestamp = created.value();

    // Serialized blobs
    auto members_size = reader.read_uint32();
    if (!members_size.is_ok())
        return result<guild_save_data, persist_result>::err(persist_result::parse_error);
    auto members_data = reader.read_bytes(members_size.value());
    if (!members_data.is_ok())
        return result<guild_save_data, persist_result>::err(persist_result::parse_error);
    data.members_data = members_data.value();

    auto ranks_size = reader.read_uint32();
    if (!ranks_size.is_ok())
        return result<guild_save_data, persist_result>::err(persist_result::parse_error);
    auto ranks_data = reader.read_bytes(ranks_size.value());
    if (!ranks_data.is_ok())
        return result<guild_save_data, persist_result>::err(persist_result::parse_error);
    data.ranks_data = ranks_data.value();

    return result<guild_save_data, persist_result>::ok(std::move(data));
}

// ========== Callbacks ==========

void persistence_system::on_save_completed(save_callback callback)
{
    save_callbacks_.push_back(std::move(callback));
}

void persistence_system::on_load_completed(load_callback callback)
{
    load_callbacks_.push_back(std::move(callback));
}

// ========== Statistics ==========

auto persistence_system::saved_player_count() const -> size_t
{
    return player_cache_.size();
}

auto persistence_system::saved_guild_count() const -> size_t
{
    return guild_cache_.size();
}

auto persistence_system::last_save_time() const -> std::chrono::system_clock::time_point
{
    return last_save_;
}

// ========== Private Helpers ==========

void persistence_system::ensure_directories()
{
    std::error_code ec;
    std::filesystem::create_directories(config_.save_directory / config_.player_directory, ec);
    std::filesystem::create_directories(config_.save_directory / config_.guild_directory, ec);
    std::filesystem::create_directories(config_.save_directory / config_.world_directory, ec);
    std::filesystem::create_directories(config_.save_directory / "backups", ec);
}

auto persistence_system::get_player_path(player_id id) const -> std::filesystem::path
{
    return config_.save_directory / config_.player_directory / (std::to_string(id.value) + ".dat");
}

auto persistence_system::get_guild_path(guild_id id) const -> std::filesystem::path
{
    return config_.save_directory / config_.guild_directory / (std::to_string(id.value) + ".dat");
}

auto persistence_system::write_file(const std::filesystem::path& path,
                                    const std::vector<uint8_t>& data) -> persist_result
{
    std::ofstream file(path, std::ios::binary);
    if (!file)
    {
        LOG_ERROR(general, "Failed to open file for writing: {}", path.string());
        return persist_result::io_error;
    }

    file.write(reinterpret_cast<const char*>(data.data()), static_cast<std::streamsize>(data.size()));
    if (!file)
    {
        LOG_ERROR(general, "Failed to write file: {}", path.string());
        return persist_result::io_error;
    }

    return persist_result::success;
}

auto persistence_system::read_file(const std::filesystem::path& path) -> result<std::vector<uint8_t>, persist_result>
{
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file)
    {
        return result<std::vector<uint8_t>, persist_result>::err(persist_result::not_found);
    }

    auto size = file.tellg();
    file.seekg(0, std::ios::beg);

    std::vector<uint8_t> data(static_cast<size_t>(size));
    file.read(reinterpret_cast<char*>(data.data()), size);

    if (!file)
    {
        LOG_ERROR(general, "Failed to read file: {}", path.string());
        return result<std::vector<uint8_t>, persist_result>::err(persist_result::io_error);
    }

    return result<std::vector<uint8_t>, persist_result>::ok(std::move(data));
}

void persistence_system::do_auto_save()
{
    if (save_providers_.empty())
        return;

    LOG_DEBUG(general, "Starting auto-save...");

    for (const auto& [player, provider] : save_providers_)
    {
        try
        {
            auto data = provider();
            save_player(data);
        }
        catch (const std::exception& e)
        {
            LOG_ERROR(general, "Failed to auto-save player {}: {}", player.value, e.what());
        }
    }

    LOG_DEBUG(general, "Auto-save completed");
}

void persistence_system::cleanup_old_backups()
{
    if (config_.max_backups <= 0)
        return;

    auto backups = list_backups();
    if (backups.size() <= static_cast<size_t>(config_.max_backups))
        return;

    // Remove oldest backups
    size_t to_remove = backups.size() - static_cast<size_t>(config_.max_backups);
    for (size_t i = 0; i < to_remove; ++i)
    {
        auto backup_path = config_.save_directory / "backups" / backups[i];
        std::error_code ec;
        std::filesystem::remove_all(backup_path, ec);
        if (!ec)
        {
            LOG_DEBUG(general, "Removed old backup: {}", backups[i]);
        }
    }
}

} // namespace hb::persistence
