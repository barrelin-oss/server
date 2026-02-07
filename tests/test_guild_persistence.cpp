// test_guild_persistence.cpp
// Tests for guild persistence: character_id tracking, connect/disconnect, rank config serialization

#include <gtest/gtest.h>
#include "core/types.h"
#include "social/guild.h"
#include "social/social_system.h"

using hb::player_id;
using hb::guild_id;
using namespace hb::social;

// ========== guild_member character_id tests ==========

TEST(guild_persistence_test, member_has_character_id_field) {
    guild_member m;
    EXPECT_EQ(m.character_id.value, 0u);
    m.character_id = player_id{42};
    EXPECT_EQ(m.character_id.value, 42u);
}

TEST(guild_persistence_test, get_member_by_character_id) {
    guild g;
    g.add_member(player_id{100}, "Alice", guild_rank::guild_master);
    g.add_member(player_id{200}, "Bob", guild_rank::member);

    // Manually set character_ids (normally done by social_system)
    g.members[0].character_id = player_id{10};
    g.members[1].character_id = player_id{20};

    auto* alice = g.get_member_by_character_id(player_id{10});
    ASSERT_NE(alice, nullptr);
    EXPECT_EQ(alice->name, "Alice");

    auto* bob = g.get_member_by_character_id(player_id{20});
    ASSERT_NE(bob, nullptr);
    EXPECT_EQ(bob->name, "Bob");

    auto* nobody = g.get_member_by_character_id(player_id{99});
    EXPECT_EQ(nobody, nullptr);
}

TEST(guild_persistence_test, get_member_by_character_id_const) {
    guild g;
    g.add_member(player_id{1}, "Player1", guild_rank::recruit);
    g.members[0].character_id = player_id{50};

    const auto& cg = g;
    const auto* m = cg.get_member_by_character_id(player_id{50});
    ASSERT_NE(m, nullptr);
    EXPECT_EQ(m->name, "Player1");
}

// ========== connect/disconnect guild member tests ==========

class guild_persistence_fixture : public ::testing::Test {
protected:
    void SetUp() override {
        sys_.initialize();

        social_system_config config;
        config.min_guild_name_length = 3;
        config.max_guild_name_length = 20;
        config.min_guild_tag_length = 2;
        config.max_guild_tag_length = 4;
        sys_.set_config(config);
    }

    void TearDown() override {
        sys_.shutdown();
    }

    social_system sys_;
};

TEST_F(guild_persistence_fixture, create_guild_basic) {
    sys_.register_player(player_id{1}, "Founder");
    auto result = sys_.create_guild(player_id{1}, "TestGuild", "TG");
    ASSERT_TRUE(result.is_ok());
    EXPECT_EQ(sys_.guild_count(), 1);
}

TEST_F(guild_persistence_fixture, connect_guild_member_resolves_runtime_id) {
    // Register player and create a guild
    sys_.register_player(player_id{1}, "Founder");
    auto result = sys_.create_guild(player_id{1}, "TestGuild", "TG");
    ASSERT_TRUE(result.is_ok());
    auto gid = result.value();

    auto* g = sys_.get_guild(gid);
    ASSERT_NE(g, nullptr);
    EXPECT_EQ(g->member_count(), 1u);  // Founder only

    // Simulate an offline-loaded member: add a member with character_id but no runtime ID
    guild_member offline_member;
    offline_member.player = player_id{};  // offline
    offline_member.character_id = player_id{999};
    offline_member.name = "OfflineGuy";
    offline_member.rank = guild_rank::member;
    g->members.push_back(std::move(offline_member));
    EXPECT_EQ(g->member_count(), 2u);

    // Now connect that member - the fallback scan should find them
    sys_.connect_guild_member(player_id{999}, player_id{42}, "OfflineGuy");

    // Re-fetch guild pointer
    g = sys_.get_guild(gid);
    ASSERT_NE(g, nullptr);

    // Verify the runtime ID was set
    auto* member = g->get_member(player_id{42});
    ASSERT_NE(member, nullptr);
    EXPECT_EQ(member->character_id.value, 999u);
    EXPECT_EQ(member->name, "OfflineGuy");

    // Verify player_guilds_ lookup works
    EXPECT_EQ(sys_.get_player_guild(player_id{42}), gid);
}

TEST_F(guild_persistence_fixture, disconnect_guild_member_clears_runtime_id) {
    sys_.register_player(player_id{1}, "Founder");
    auto result = sys_.create_guild(player_id{1}, "TestGuild", "TG");
    ASSERT_TRUE(result.is_ok());
    auto gid = result.value();

    auto* g = sys_.get_guild(gid);
    ASSERT_NE(g, nullptr);

    // Add offline member with character_id, then connect
    guild_member offline_member;
    offline_member.player = player_id{};
    offline_member.character_id = player_id{888};
    offline_member.name = "Bob";
    offline_member.rank = guild_rank::recruit;
    g->members.push_back(std::move(offline_member));

    sys_.connect_guild_member(player_id{888}, player_id{55}, "Bob");
    EXPECT_EQ(sys_.get_player_guild(player_id{55}), gid);

    // Disconnect
    sys_.disconnect_guild_member(player_id{55}, player_id{888});

    // Runtime lookup should fail
    EXPECT_FALSE(sys_.get_player_guild(player_id{55}).is_valid());

    // But member still exists in guild with character_id
    g = sys_.get_guild(gid);
    ASSERT_NE(g, nullptr);
    auto* member = g->get_member_by_character_id(player_id{888});
    ASSERT_NE(member, nullptr);
    EXPECT_EQ(member->player.value, 0u);  // offline
}

TEST_F(guild_persistence_fixture, connect_sets_guild_master) {
    sys_.register_player(player_id{1}, "Master");
    auto result = sys_.create_guild(player_id{1}, "TestGuild", "TG");
    ASSERT_TRUE(result.is_ok());
    auto gid = result.value();

    auto* g = sys_.get_guild(gid);
    ASSERT_NE(g, nullptr);

    // Simulate server restart: clear runtime master and set character_id
    g->master = player_id{};
    g->members[0].player = player_id{};
    g->members[0].character_id = player_id{777};
    g->members[0].rank = guild_rank::guild_master;

    // connect_guild_member should restore guild master and player_guilds
    sys_.connect_guild_member(player_id{777}, player_id{50}, "Master");

    g = sys_.get_guild(gid);
    ASSERT_NE(g, nullptr);
    EXPECT_EQ(g->master, player_id{50});
    EXPECT_EQ(sys_.get_player_guild(player_id{50}), gid);
}
