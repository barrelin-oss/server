#pragma once

// persistence_system.h
// Persistence subsystem for save/load operations

#include "core/types.h"
#include "core/result.h"
#include "core/subsystem.h"
#include "persistence/repository.h"
#include "persistence/serialization.h"

#include <unordered_map>
#include <string_view>
#include <functional>
#include <filesystem>
#include <fstream>
#include <memory>

namespace hb::persistence
{

// Save/Load events
struct save_started_event
{
    player_id player{};
};

struct save_completed_event
{
    player_id player{};
    bool success{false};
    std::string error_message;
};

struct load_completed_event
{
    player_id player{};
    bool success{false};
    std::string error_message;
};

// Player save data structure
struct player_save_data
{
    player_id id{};
    std::string name;
    std::string account_name;

    // Character info
    uint8_t nation{0}; // 0 = none, 1 = Aresden, 2 = Elvine
    uint8_t gender{0};
    uint8_t skin_color{0};
    uint8_t hair_style{0};
    uint8_t hair_color{0};
    uint8_t underwear{0};

    // Location
    map_id current_map{};
    int16_t x{0};
    int16_t y{0};

    // Stats
    int16_t level{1};
    int64_t experience{0};
    int32_t hp{100};
    int32_t max_hp{100};
    int32_t mp{100};
    int32_t max_mp{100};
    int32_t sp{100};
    int32_t max_sp{100};

    // Attributes
    int16_t strength{10};
    int16_t vitality{10};
    int16_t dexterity{10};
    int16_t intelligence{10};
    int16_t magic{10};
    int16_t charisma{10};
    int16_t luck{10};

    // Combat
    int32_t ek_count{0}; // Enemy kill count
    int32_t pk_count{0}; // Player kill count
    int32_t contribution{0};

    // Currency
    int64_t gold{0};
    int64_t bank_gold{0};

    // Guild
    guild_id guild{};
    uint8_t guild_rank{0};

    // Timestamps
    int64_t created_timestamp{0};
    int64_t last_login_timestamp{0};
    int32_t play_time_seconds{0};

    // Serialized blobs for complex data
    std::vector<uint8_t> inventory_data;
    std::vector<uint8_t> skills_data;
    std::vector<uint8_t> quests_data;
    std::vector<uint8_t> equipment_data;
};

// Guild save data structure
struct guild_save_data
{
    guild_id id{};
    std::string name;
    std::string tag;
    std::string motd;
    player_id master{};
    uint8_t faction{0};

    int64_t gold_bank{0};
    int32_t level{1};
    int64_t experience{0};

    int64_t total_kills{0};
    int64_t total_deaths{0};

    int64_t created_timestamp{0};

    std::vector<uint8_t> members_data;
    std::vector<uint8_t> ranks_data;
};

// Persistence system configuration
struct persistence_config
{
    std::filesystem::path save_directory{"saves"};
    std::filesystem::path player_directory{"players"};
    std::filesystem::path guild_directory{"guilds"};
    std::filesystem::path world_directory{"world"};

    bool auto_save_enabled{true};
    int32_t auto_save_interval_seconds{300}; // 5 minutes
    bool compress_saves{false};
    bool backup_on_save{true};
    int32_t max_backups{5};
};

// Persistence operation result
enum class persist_result : uint8_t
{
    success = 0,
    not_found = 1,
    io_error = 2,
    parse_error = 3,
    validation_error = 4,
    already_exists = 5,
};

// Persistence system - manages save/load operations
class persistence_system : public subsystem
{
public:
    using save_callback = std::function<void(const save_completed_event&)>;
    using load_callback = std::function<void(const load_completed_event&)>;

    persistence_system();
    ~persistence_system() override;

    // Subsystem interface
    [[nodiscard]] auto name() const -> std::string_view override { return "persistence_system"; }
    void initialize() override;
    void shutdown() override;
    void update(float delta_time) override;

    // Configuration
    void set_config(const persistence_config& config);

    // ========== Player Data ==========

    auto save_player(const player_save_data& data) -> persist_result;
    auto load_player(player_id id) -> result<player_save_data, persist_result>;
    auto delete_player(player_id id) -> persist_result;
    auto player_exists(player_id id) -> bool;

    auto load_player_by_name(const std::string& name) -> result<player_save_data, persist_result>;
    auto get_all_player_ids() -> std::vector<player_id>;

    // ========== Guild Data ==========

    auto save_guild(const guild_save_data& data) -> persist_result;
    auto load_guild(guild_id id) -> result<guild_save_data, persist_result>;
    auto delete_guild(guild_id id) -> persist_result;
    auto guild_exists(guild_id id) -> bool;

    auto load_guild_by_name(const std::string& name) -> result<guild_save_data, persist_result>;
    auto get_all_guild_ids() -> std::vector<guild_id>;

    // ========== Auto-Save ==========

    void trigger_auto_save();
    void register_save_target(player_id player, std::function<player_save_data()> provider);
    void unregister_save_target(player_id player);

    // ========== Backup ==========

    auto create_backup(const std::string& name) -> persist_result;
    auto restore_backup(const std::string& name) -> persist_result;
    auto list_backups() -> std::vector<std::string>;

    // ========== Serialization Helpers ==========

    static void serialize_player(const player_save_data& data, binary_writer& writer);
    static auto deserialize_player(binary_reader& reader) -> result<player_save_data, persist_result>;

    static void serialize_guild(const guild_save_data& data, binary_writer& writer);
    static auto deserialize_guild(binary_reader& reader) -> result<guild_save_data, persist_result>;

    // ========== Callbacks ==========

    void on_save_completed(save_callback callback);
    void on_load_completed(load_callback callback);

    // ========== Statistics ==========

    [[nodiscard]] auto saved_player_count() const -> size_t;
    [[nodiscard]] auto saved_guild_count() const -> size_t;
    [[nodiscard]] auto last_save_time() const -> std::chrono::system_clock::time_point;

private:
    void ensure_directories();
    auto get_player_path(player_id id) const -> std::filesystem::path;
    auto get_guild_path(guild_id id) const -> std::filesystem::path;

    auto write_file(const std::filesystem::path& path, const std::vector<uint8_t>& data) -> persist_result;
    auto read_file(const std::filesystem::path& path) -> result<std::vector<uint8_t>, persist_result>;

    void do_auto_save();
    void cleanup_old_backups();

    persistence_config config_;

    // Cached data for quick access
    std::unordered_map<player_id, player_save_data> player_cache_;
    std::unordered_map<guild_id, guild_save_data> guild_cache_;

    // Save providers for auto-save
    std::unordered_map<player_id, std::function<player_save_data()>> save_providers_;

    // Statistics
    std::chrono::system_clock::time_point last_save_;
    float time_since_auto_save_{0.0f};

    // Callbacks
    std::vector<save_callback> save_callbacks_;
    std::vector<load_callback> load_callbacks_;
};

} // namespace hb::persistence
