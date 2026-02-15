// test_persistence.cpp
// Unit tests for persistence system

#include <gtest/gtest.h>
#include "core/types.h"
#include "persistence/repository.h"
#include "persistence/serialization.h"
#include "persistence/persistence_system.h"

#include <filesystem>

using hb::guild_id;
using hb::map_id;
using hb::player_id;
using namespace hb::persistence;

// Serialization tests

TEST(binary_writer_test, write_primitives)
{
    binary_writer writer;

    writer.write_uint8(42);
    writer.write_uint16(1234);
    writer.write_uint32(123456);
    writer.write_int32(-500);

    EXPECT_EQ(writer.size(), 1 + 2 + 4 + 4);
}

TEST(binary_writer_test, write_string)
{
    binary_writer writer;

    writer.write_string("Hello");

    // 4 bytes length + 5 bytes string
    EXPECT_EQ(writer.size(), 4 + 5);
}

TEST(binary_writer_test, write_fixed_string)
{
    binary_writer writer;

    writer.write_fixed_string("Hi", 10);

    EXPECT_EQ(writer.size(), 10);
}

TEST(binary_reader_test, read_primitives)
{
    binary_writer writer;
    writer.write_uint8(42);
    writer.write_uint16(1234);
    writer.write_uint32(123456);
    writer.write_int32(-500);

    binary_reader reader(writer.data());

    auto u8 = reader.read_uint8();
    ASSERT_TRUE(u8.is_ok());
    EXPECT_EQ(u8.value(), 42);

    auto u16 = reader.read_uint16();
    ASSERT_TRUE(u16.is_ok());
    EXPECT_EQ(u16.value(), 1234);

    auto u32 = reader.read_uint32();
    ASSERT_TRUE(u32.is_ok());
    EXPECT_EQ(u32.value(), 123456);

    auto i32 = reader.read_int32();
    ASSERT_TRUE(i32.is_ok());
    EXPECT_EQ(i32.value(), -500);
}

TEST(binary_reader_test, read_string)
{
    binary_writer writer;
    writer.write_string("Hello World");

    binary_reader reader(writer.data());

    auto str = reader.read_string();
    ASSERT_TRUE(str.is_ok());
    EXPECT_EQ(str.value(), "Hello World");
}

TEST(binary_reader_test, read_float)
{
    binary_writer writer;
    writer.write_float(3.14159f);

    binary_reader reader(writer.data());

    auto f = reader.read_float();
    ASSERT_TRUE(f.is_ok());
    EXPECT_FLOAT_EQ(f.value(), 3.14159f);
}

TEST(binary_reader_test, read_double)
{
    binary_writer writer;
    writer.write_double(2.71828182845);

    binary_reader reader(writer.data());

    auto d = reader.read_double();
    ASSERT_TRUE(d.is_ok());
    EXPECT_DOUBLE_EQ(d.value(), 2.71828182845);
}

TEST(binary_reader_test, buffer_underflow)
{
    std::vector<uint8_t> small_buffer{1, 2};
    binary_reader reader(small_buffer);

    auto u8 = reader.read_uint8();
    EXPECT_TRUE(u8.is_ok());

    auto u32 = reader.read_uint32(); // Not enough bytes
    EXPECT_FALSE(u32.is_ok());
    EXPECT_EQ(u32.error(), serialize_error::buffer_underflow);
}

// Memory repository tests

struct test_entity
{
    int id;
    std::string name;
    int value;
};

TEST(memory_repository_test, save_and_load)
{
    memory_repository<test_entity, int> repo([](const test_entity& e) { return e.id; });

    test_entity entity{1, "Test", 42};
    auto save_result = repo.save(entity);
    EXPECT_TRUE(save_result.is_ok());

    auto load_result = repo.load(1);
    ASSERT_TRUE(load_result.is_ok());
    EXPECT_EQ(load_result.value().name, "Test");
    EXPECT_EQ(load_result.value().value, 42);
}

TEST(memory_repository_test, not_found)
{
    memory_repository<test_entity, int> repo([](const test_entity& e) { return e.id; });

    auto result = repo.load(999);
    EXPECT_FALSE(result.is_ok());
    EXPECT_EQ(result.error(), repository_error::not_found);
}

TEST(memory_repository_test, exists)
{
    memory_repository<test_entity, int> repo([](const test_entity& e) { return e.id; });

    EXPECT_FALSE(repo.exists(1));

    test_entity entity{1, "Test", 42};
    repo.save(entity);

    EXPECT_TRUE(repo.exists(1));
}

TEST(memory_repository_test, remove)
{
    memory_repository<test_entity, int> repo([](const test_entity& e) { return e.id; });

    test_entity entity{1, "Test", 42};
    repo.save(entity);

    auto remove_result = repo.remove(1);
    EXPECT_TRUE(remove_result.is_ok());
    EXPECT_FALSE(repo.exists(1));

    auto remove_again = repo.remove(1);
    EXPECT_FALSE(remove_again.is_ok());
}

TEST(memory_repository_test, count)
{
    memory_repository<test_entity, int> repo([](const test_entity& e) { return e.id; });

    EXPECT_EQ(repo.count(), 0);

    repo.save({1, "A", 1});
    repo.save({2, "B", 2});
    repo.save({3, "C", 3});

    EXPECT_EQ(repo.count(), 3);
}

TEST(memory_repository_test, load_all)
{
    memory_repository<test_entity, int> repo([](const test_entity& e) { return e.id; });

    repo.save({1, "A", 1});
    repo.save({2, "B", 2});

    auto all = repo.load_all();
    ASSERT_TRUE(all.is_ok());
    EXPECT_EQ(all.value().size(), 2);
}

// Persistence system tests

class persistence_system_test : public ::testing::Test
{
protected:
    void SetUp() override
    {
        // Use a test directory
        test_dir_ = std::filesystem::temp_directory_path() / "hb_test_persistence";
        std::filesystem::create_directories(test_dir_);

        persistence_config config;
        config.save_directory = test_dir_;
        config.auto_save_enabled = false; // Disable for tests

        system_.set_config(config);
        system_.initialize();
    }

    void TearDown() override
    {
        system_.shutdown();

        // Cleanup test directory
        std::error_code ec;
        std::filesystem::remove_all(test_dir_, ec);
    }

    std::filesystem::path test_dir_;
    persistence_system system_;
};

TEST_F(persistence_system_test, lifecycle)
{
    EXPECT_TRUE(system_.is_initialized());
    EXPECT_EQ(system_.name(), "persistence_system");
}

TEST_F(persistence_system_test, save_and_load_player)
{
    player_save_data data;
    data.id = player_id{1};
    data.name = "TestPlayer";
    data.account_name = "test_account";
    data.level = 50;
    data.experience = 100000;
    data.gold = 50000;
    data.strength = 30;
    data.current_map = map_id{5};
    data.x = 100;
    data.y = 200;

    auto save_result = system_.save_player(data);
    EXPECT_EQ(save_result, persist_result::success);

    auto load_result = system_.load_player(player_id{1});
    ASSERT_TRUE(load_result.is_ok());

    const auto& loaded = load_result.value();
    EXPECT_EQ(loaded.name, "TestPlayer");
    EXPECT_EQ(loaded.account_name, "test_account");
    EXPECT_EQ(loaded.level, 50);
    EXPECT_EQ(loaded.experience, 100000);
    EXPECT_EQ(loaded.gold, 50000);
    EXPECT_EQ(loaded.strength, 30);
    EXPECT_EQ(loaded.current_map.value, 5);
    EXPECT_EQ(loaded.x, 100);
    EXPECT_EQ(loaded.y, 200);
}

TEST_F(persistence_system_test, player_not_found)
{
    auto result = system_.load_player(player_id{999});
    EXPECT_FALSE(result.is_ok());
    EXPECT_EQ(result.error(), persist_result::not_found);
}

TEST_F(persistence_system_test, player_exists)
{
    EXPECT_FALSE(system_.player_exists(player_id{1}));

    player_save_data data;
    data.id = player_id{1};
    data.name = "Test";
    system_.save_player(data);

    EXPECT_TRUE(system_.player_exists(player_id{1}));
}

TEST_F(persistence_system_test, delete_player)
{
    player_save_data data;
    data.id = player_id{1};
    data.name = "Test";
    system_.save_player(data);

    EXPECT_TRUE(system_.player_exists(player_id{1}));

    auto result = system_.delete_player(player_id{1});
    EXPECT_EQ(result, persist_result::success);

    EXPECT_FALSE(system_.player_exists(player_id{1}));
}

TEST_F(persistence_system_test, save_and_load_guild)
{
    guild_save_data data;
    data.id = guild_id{1};
    data.name = "TestGuild";
    data.tag = "TG";
    data.motd = "Welcome!";
    data.master = player_id{1};
    data.faction = 1;
    data.gold_bank = 100000;
    data.level = 5;

    auto save_result = system_.save_guild(data);
    EXPECT_EQ(save_result, persist_result::success);

    auto load_result = system_.load_guild(guild_id{1});
    ASSERT_TRUE(load_result.is_ok());

    const auto& loaded = load_result.value();
    EXPECT_EQ(loaded.name, "TestGuild");
    EXPECT_EQ(loaded.tag, "TG");
    EXPECT_EQ(loaded.motd, "Welcome!");
    EXPECT_EQ(loaded.master.value, 1);
    EXPECT_EQ(loaded.faction, 1);
    EXPECT_EQ(loaded.gold_bank, 100000);
    EXPECT_EQ(loaded.level, 5);
}

TEST_F(persistence_system_test, guild_not_found)
{
    auto result = system_.load_guild(guild_id{999});
    EXPECT_FALSE(result.is_ok());
    EXPECT_EQ(result.error(), persist_result::not_found);
}

TEST_F(persistence_system_test, guild_exists)
{
    EXPECT_FALSE(system_.guild_exists(guild_id{1}));

    guild_save_data data;
    data.id = guild_id{1};
    data.name = "Test";
    system_.save_guild(data);

    EXPECT_TRUE(system_.guild_exists(guild_id{1}));
}

TEST_F(persistence_system_test, delete_guild)
{
    guild_save_data data;
    data.id = guild_id{1};
    data.name = "Test";
    system_.save_guild(data);

    EXPECT_TRUE(system_.guild_exists(guild_id{1}));

    auto result = system_.delete_guild(guild_id{1});
    EXPECT_EQ(result, persist_result::success);

    EXPECT_FALSE(system_.guild_exists(guild_id{1}));
}

TEST_F(persistence_system_test, save_callback)
{
    bool callback_fired = false;
    system_.on_save_completed(
        [&](const save_completed_event& event)
        {
            callback_fired = true;
            EXPECT_EQ(event.player, player_id{1});
            EXPECT_TRUE(event.success);
        });

    player_save_data data;
    data.id = player_id{1};
    data.name = "Test";
    system_.save_player(data);

    EXPECT_TRUE(callback_fired);
}

TEST_F(persistence_system_test, load_callback)
{
    player_save_data data;
    data.id = player_id{1};
    data.name = "Test";
    system_.save_player(data);

    // Clear cache to force file read
    system_.shutdown();

    persistence_config config;
    config.save_directory = test_dir_;
    config.auto_save_enabled = false;
    system_.set_config(config);
    system_.initialize();

    bool callback_fired = false;
    system_.on_load_completed(
        [&](const load_completed_event& event)
        {
            callback_fired = true;
            EXPECT_EQ(event.player, player_id{1});
            EXPECT_TRUE(event.success);
        });

    system_.load_player(player_id{1});
    EXPECT_TRUE(callback_fired);
}

TEST_F(persistence_system_test, player_with_blob_data)
{
    player_save_data data;
    data.id = player_id{1};
    data.name = "BlobTest";

    // Add some blob data
    data.inventory_data = {1, 2, 3, 4, 5};
    data.skills_data = {10, 20, 30};
    data.quests_data = {100, 200};
    data.equipment_data = {50, 60, 70, 80};

    system_.save_player(data);

    auto load_result = system_.load_player(player_id{1});
    ASSERT_TRUE(load_result.is_ok());

    const auto& loaded = load_result.value();
    EXPECT_EQ(loaded.inventory_data, std::vector<uint8_t>({1, 2, 3, 4, 5}));
    EXPECT_EQ(loaded.skills_data, std::vector<uint8_t>({10, 20, 30}));
    EXPECT_EQ(loaded.quests_data, std::vector<uint8_t>({100, 200}));
    EXPECT_EQ(loaded.equipment_data, std::vector<uint8_t>({50, 60, 70, 80}));
}

TEST_F(persistence_system_test, statistics)
{
    player_save_data player;
    player.id = player_id{1};
    player.name = "P1";
    system_.save_player(player);

    player.id = player_id{2};
    player.name = "P2";
    system_.save_player(player);

    EXPECT_EQ(system_.saved_player_count(), 2);

    guild_save_data guild;
    guild.id = guild_id{1};
    guild.name = "G1";
    system_.save_guild(guild);

    EXPECT_EQ(system_.saved_guild_count(), 1);
}

// Serialization round-trip tests

TEST(player_serialization_test, round_trip)
{
    player_save_data original;
    original.id = player_id{12345};
    original.name = "TestCharacter";
    original.account_name = "test_acc";
    original.nation = 2;
    original.gender = 1;
    original.level = 75;
    original.experience = 5000000;
    original.hp = 500;
    original.max_hp = 1000;
    original.mp = 200;
    original.max_mp = 500;
    original.strength = 50;
    original.vitality = 40;
    original.dexterity = 30;
    original.intelligence = 20;
    original.magic = 60;
    original.charisma = 15;
    original.luck = 10;
    original.gold = 100000;
    original.bank_gold = 500000;
    original.current_map = map_id{10};
    original.x = 350;
    original.y = 200;

    binary_writer writer;
    persistence_system::serialize_player(original, writer);

    binary_reader reader(writer.data());
    auto result = persistence_system::deserialize_player(reader);

    ASSERT_TRUE(result.is_ok());
    const auto& loaded = result.value();

    EXPECT_EQ(loaded.id.value, original.id.value);
    EXPECT_EQ(loaded.name, original.name);
    EXPECT_EQ(loaded.account_name, original.account_name);
    EXPECT_EQ(loaded.nation, original.nation);
    EXPECT_EQ(loaded.level, original.level);
    EXPECT_EQ(loaded.experience, original.experience);
    EXPECT_EQ(loaded.hp, original.hp);
    EXPECT_EQ(loaded.max_hp, original.max_hp);
    EXPECT_EQ(loaded.strength, original.strength);
    EXPECT_EQ(loaded.gold, original.gold);
    EXPECT_EQ(loaded.bank_gold, original.bank_gold);
    EXPECT_EQ(loaded.current_map.value, original.current_map.value);
    EXPECT_EQ(loaded.x, original.x);
    EXPECT_EQ(loaded.y, original.y);
}

TEST(guild_serialization_test, round_trip)
{
    guild_save_data original;
    original.id = guild_id{100};
    original.name = "Epic Guild";
    original.tag = "EG";
    original.motd = "Best guild ever!";
    original.master = player_id{1};
    original.faction = 1;
    original.gold_bank = 1000000;
    original.level = 10;
    original.experience = 50000;
    original.total_kills = 1000;
    original.total_deaths = 500;

    binary_writer writer;
    persistence_system::serialize_guild(original, writer);

    binary_reader reader(writer.data());
    auto result = persistence_system::deserialize_guild(reader);

    ASSERT_TRUE(result.is_ok());
    const auto& loaded = result.value();

    EXPECT_EQ(loaded.id.value, original.id.value);
    EXPECT_EQ(loaded.name, original.name);
    EXPECT_EQ(loaded.tag, original.tag);
    EXPECT_EQ(loaded.motd, original.motd);
    EXPECT_EQ(loaded.master.value, original.master.value);
    EXPECT_EQ(loaded.faction, original.faction);
    EXPECT_EQ(loaded.gold_bank, original.gold_bank);
    EXPECT_EQ(loaded.level, original.level);
    EXPECT_EQ(loaded.total_kills, original.total_kills);
}
