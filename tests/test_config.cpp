// test_config.cpp
// Unit tests for configuration system

#include <gtest/gtest.h>
#include "config/config_system.h"
#include "config/server_config.h"

#include <fstream>
#include <filesystem>

using namespace hb;

class config_test : public ::testing::Test
{
protected:
    void SetUp() override
    {
        // Create a temporary directory for test files
        test_dir_ = std::filesystem::temp_directory_path() / "hgserver_test";
        std::filesystem::create_directories(test_dir_);
    }

    void TearDown() override
    {
        // Clean up test files
        std::filesystem::remove_all(test_dir_);
    }

    auto create_test_file(const std::string& filename, const std::string& content) -> std::filesystem::path
    {
        auto path = test_dir_ / filename;
        std::ofstream file(path);
        file << content;
        return path;
    }

    std::filesystem::path test_dir_;
};

TEST_F(config_test, server_config_defaults)
{
    server_config config;

    EXPECT_EQ(config.server_name, "HGServer");
    EXPECT_EQ(config.game_server_port, 2848);
    EXPECT_EQ(config.log_server_addr, "127.0.0.1");
    EXPECT_EQ(config.log_server_port, 3000);
}

TEST_F(config_test, server_config_load_ini)
{
    auto path = create_test_file("test_server.cfg",
                                 "game-server-name = TestServer\n"
                                 "game-server-port = 3000\n"
                                 "log-server-address = 192.168.1.1\n"
                                 "log-server-port = 4000\n");

    auto result = server_config::load_from_file(path);

    ASSERT_TRUE(result.is_ok()) << "Failed to load config: " << result.error();

    auto& config = result.value();
    EXPECT_EQ(config.server_name, "TestServer");
    EXPECT_EQ(config.game_server_port, 3000);
    EXPECT_EQ(config.log_server_addr, "192.168.1.1");
    EXPECT_EQ(config.log_server_port, 4000);
}

TEST_F(config_test, server_config_load_nonexistent)
{
    auto result = server_config::load_from_file("nonexistent.cfg");
    EXPECT_TRUE(result.is_err());
}

TEST_F(config_test, server_config_json_roundtrip)
{
    server_config original;
    original.server_name = "JsonTest";
    original.game_server_port = 5000;
    original.log_server_addr = "10.0.0.1";

    auto json_path = test_dir_ / "test_config.json";

    // Save to JSON
    auto save_result = original.save_to_json(json_path);
    ASSERT_TRUE(save_result.is_ok()) << "Failed to save: " << save_result.error();

    // Load from JSON
    auto load_result = server_config::load_from_json(json_path);
    ASSERT_TRUE(load_result.is_ok()) << "Failed to load: " << load_result.error();

    auto& loaded = load_result.value();
    EXPECT_EQ(loaded.server_name, original.server_name);
    EXPECT_EQ(loaded.game_server_port, original.game_server_port);
    EXPECT_EQ(loaded.log_server_addr, original.log_server_addr);
}

TEST_F(config_test, game_config_defaults)
{
    game_config config;

    EXPECT_EQ(config.max_clients, 2000);
    EXPECT_EQ(config.max_level, 180);
    EXPECT_EQ(config.minimum_hit_ratio, 15);
    EXPECT_EQ(config.maximum_hit_ratio, 99);
}

TEST_F(config_test, config_system_lifecycle)
{
    config_system system;

    EXPECT_FALSE(system.is_initialized());
    EXPECT_EQ(system.name(), "config");

    system.initialize();
    EXPECT_TRUE(system.is_initialized());

    system.shutdown();
    EXPECT_FALSE(system.is_initialized());
}

TEST_F(config_test, config_system_load_server)
{
    auto path = create_test_file("server.cfg",
                                 "game-server-name = SystemTest\n"
                                 "game-server-port = 9999\n");

    config_system system;
    system.initialize();

    auto result = system.load_server_config(path);
    ASSERT_TRUE(result.is_ok()) << "Failed: " << result.error();

    EXPECT_EQ(system.server().server_name, "SystemTest");
    EXPECT_EQ(system.server().game_server_port, 9999);

    system.shutdown();
}

TEST_F(config_test, config_system_change_callback)
{
    auto path = create_test_file("callback.cfg", "game-server-name = Initial\n");

    config_system system;
    system.initialize();

    bool callback_called = false;
    system.on_config_changed([&]() { callback_called = true; });

    system.load_server_config(path);

    // Create new config file for reload
    create_test_file("callback.cfg", "game-server-name = Reloaded\n");

    system.reload();

    EXPECT_TRUE(callback_called);
    EXPECT_EQ(system.server().server_name, "Reloaded");

    system.shutdown();
}

TEST_F(config_test, ini_parse_comments)
{
    auto path = create_test_file("comments.cfg",
                                 "# This is a comment\n"
                                 "; This is also a comment\n"
                                 "game-server-name = CommentTest\n"
                                 "# Another comment\n"
                                 "game-server-port = 1234\n");

    auto result = server_config::load_from_file(path);
    ASSERT_TRUE(result.is_ok());

    EXPECT_EQ(result.value().server_name, "CommentTest");
    EXPECT_EQ(result.value().game_server_port, 1234);
}

TEST_F(config_test, ini_parse_whitespace)
{
    auto path = create_test_file("whitespace.cfg",
                                 "  game-server-name   =   WhitespaceTest  \n"
                                 "\tgame-server-port\t=\t5678\t\n");

    auto result = server_config::load_from_file(path);
    ASSERT_TRUE(result.is_ok());

    EXPECT_EQ(result.value().server_name, "WhitespaceTest");
    EXPECT_EQ(result.value().game_server_port, 5678);
}

TEST_F(config_test, ini_parse_case_insensitive_keys)
{
    auto path = create_test_file("case.cfg",
                                 "GAME-SERVER-NAME = UpperCase\n"
                                 "Game-Server-Port = 4321\n");

    auto result = server_config::load_from_file(path);
    ASSERT_TRUE(result.is_ok());

    EXPECT_EQ(result.value().server_name, "UpperCase");
    EXPECT_EQ(result.value().game_server_port, 4321);
}

TEST_F(config_test, ini_parse_empty_file)
{
    auto path = create_test_file("empty.cfg", "");

    auto result = server_config::load_from_file(path);
    ASSERT_TRUE(result.is_ok());

    // Should use defaults
    EXPECT_EQ(result.value().server_name, "HGServer");
}
