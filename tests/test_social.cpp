// test_social.cpp
// Unit tests for social system (guilds, parties, chat)

#include <gtest/gtest.h>
#include "core/types.h"
#include "social/guild.h"
#include "social/party.h"
#include "social/chat.h"
#include "social/social_system.h"

using hb::player_id;
using hb::guild_id;
using hb::map_id;
using namespace hb::social;

// Guild tests

TEST(guild_test, default_state) {
    guild g;
    EXPECT_EQ(g.member_count(), 0);
    EXPECT_TRUE(g.is_empty());
    EXPECT_FALSE(g.is_full());
}

TEST(guild_test, add_remove_member) {
    guild g;
    g.add_member(player_id{1}, "Player1", guild_rank::guild_master);

    EXPECT_EQ(g.member_count(), 1);
    EXPECT_TRUE(g.is_member(player_id{1}));
    EXPECT_FALSE(g.is_member(player_id{2}));

    g.add_member(player_id{2}, "Player2", guild_rank::member);
    EXPECT_EQ(g.member_count(), 2);

    g.remove_member(player_id{1});
    EXPECT_EQ(g.member_count(), 1);
    EXPECT_FALSE(g.is_member(player_id{1}));
}

TEST(guild_test, permissions) {
    guild g;
    g.add_member(player_id{1}, "Master", guild_rank::guild_master);
    g.add_member(player_id{2}, "Officer", guild_rank::officer);
    g.add_member(player_id{3}, "Recruit", guild_rank::recruit);

    // Guild master has all permissions
    EXPECT_TRUE(g.has_permission(player_id{1}, guild_permission::disband));
    EXPECT_TRUE(g.has_permission(player_id{1}, guild_permission::kick));

    // Officer has limited permissions
    EXPECT_TRUE(g.has_permission(player_id{2}, guild_permission::kick));
    EXPECT_FALSE(g.has_permission(player_id{2}, guild_permission::disband));

    // Recruit has no permissions
    EXPECT_FALSE(g.has_permission(player_id{3}, guild_permission::kick));
    EXPECT_FALSE(g.has_permission(player_id{3}, guild_permission::invite));
}

TEST(guild_test, rank_modification) {
    guild g;
    g.add_member(player_id{1}, "Player", guild_rank::recruit);

    auto* member = g.get_member(player_id{1});
    ASSERT_NE(member, nullptr);
    EXPECT_EQ(member->rank, guild_rank::recruit);

    g.set_member_rank(player_id{1}, guild_rank::veteran);
    EXPECT_EQ(member->rank, guild_rank::veteran);
}

// Party tests

TEST(party_test, default_state) {
    party p;
    EXPECT_EQ(p.member_count(), 0);
    EXPECT_TRUE(p.is_empty());
    EXPECT_FALSE(p.is_full());
}

TEST(party_test, add_remove_member) {
    party p;
    p.id = party_id{1};
    p.add_member(player_id{1}, "Player1", 10);

    EXPECT_EQ(p.member_count(), 1);
    EXPECT_TRUE(p.is_member(player_id{1}));
    EXPECT_TRUE(p.is_leader(player_id{1}));

    p.add_member(player_id{2}, "Player2", 15);
    EXPECT_EQ(p.member_count(), 2);

    // Leader stays the same
    EXPECT_TRUE(p.is_leader(player_id{1}));
    EXPECT_FALSE(p.is_leader(player_id{2}));

    p.remove_member(player_id{1});
    EXPECT_EQ(p.member_count(), 1);
    // Player 2 should become leader
    EXPECT_TRUE(p.is_leader(player_id{2}));
}

TEST(party_test, set_leader) {
    party p;
    p.id = party_id{1};
    p.add_member(player_id{1}, "Player1", 10);
    p.add_member(player_id{2}, "Player2", 15);

    EXPECT_TRUE(p.is_leader(player_id{1}));

    p.set_leader(player_id{2});
    EXPECT_TRUE(p.is_leader(player_id{2}));
    EXPECT_FALSE(p.is_leader(player_id{1}));
}

TEST(party_test, average_level) {
    party p;
    p.add_member(player_id{1}, "P1", 10);
    p.add_member(player_id{2}, "P2", 20);
    p.add_member(player_id{3}, "P3", 30);

    EXPECT_FLOAT_EQ(p.average_level(), 20.0f);
}

TEST(party_test, round_robin_loot) {
    party p;
    p.id = party_id{1};
    p.add_member(player_id{1}, "P1", 10);
    p.add_member(player_id{2}, "P2", 10);
    p.add_member(player_id{3}, "P3", 10);

    EXPECT_EQ(p.get_next_looter(), player_id{1});
    EXPECT_EQ(p.get_next_looter(), player_id{2});
    EXPECT_EQ(p.get_next_looter(), player_id{3});
    EXPECT_EQ(p.get_next_looter(), player_id{1});  // Wraps around
}

TEST(party_test, invites) {
    party p;
    p.id = party_id{1};

    p.add_invite(player_id{1}, player_id{2});
    EXPECT_TRUE(p.has_invite(player_id{2}));
    EXPECT_FALSE(p.has_invite(player_id{3}));

    p.remove_invite(player_id{2});
    EXPECT_FALSE(p.has_invite(player_id{2}));
}

// Chat tests

TEST(chat_settings_test, default_state) {
    chat_settings settings;

    EXPECT_TRUE(settings.is_channel_enabled(chat_channel::local));
    EXPECT_TRUE(settings.is_channel_enabled(chat_channel::guild));
    EXPECT_FALSE(settings.is_player_blocked(player_id{1}));
}

TEST(chat_settings_test, block_unblock) {
    chat_settings settings;

    settings.block_player(player_id{1});
    EXPECT_TRUE(settings.is_player_blocked(player_id{1}));

    settings.unblock_player(player_id{1});
    EXPECT_FALSE(settings.is_player_blocked(player_id{1}));
}

TEST(chat_settings_test, channel_toggle) {
    chat_settings settings;

    settings.set_channel_enabled(chat_channel::global, false);
    EXPECT_FALSE(settings.is_channel_enabled(chat_channel::global));

    settings.set_channel_enabled(chat_channel::global, true);
    EXPECT_TRUE(settings.is_channel_enabled(chat_channel::global));
}

TEST(chat_rate_limit_test, can_send) {
    chat_rate_limit limit;
    limit.reset();

    EXPECT_TRUE(limit.can_send());

    // Send max messages
    for (int i = 0; i < chat_rate_limit::max_messages_per_second; ++i) {
        limit.record_message();
    }

    EXPECT_FALSE(limit.can_send());
}

// Social system tests

class social_system_test : public ::testing::Test {
protected:
    void SetUp() override {
        system_.initialize();

        social_system_config config;
        config.min_guild_name_length = 3;
        config.max_guild_name_length = 20;
        config.min_guild_tag_length = 2;
        config.max_guild_tag_length = 4;
        system_.set_config(config);

        // Register test players
        system_.register_player(player_id{1}, "Player1");
        system_.register_player(player_id{2}, "Player2");
        system_.register_player(player_id{3}, "Player3");
    }

    void TearDown() override {
        system_.shutdown();
    }

    social_system system_;
};

TEST_F(social_system_test, lifecycle) {
    EXPECT_TRUE(system_.is_initialized());
    EXPECT_EQ(system_.name(), "social_system");
}

// Guild system tests

TEST_F(social_system_test, create_guild) {
    auto result = system_.create_guild(player_id{1}, "Test Guild", "TG");
    ASSERT_TRUE(result.is_ok());

    guild_id gid = result.value();
    EXPECT_TRUE(gid.is_valid());
    EXPECT_EQ(system_.guild_count(), 1);

    const auto* g = system_.get_guild(gid);
    ASSERT_NE(g, nullptr);
    EXPECT_EQ(g->name, "Test Guild");
    EXPECT_EQ(g->tag, "TG");
    EXPECT_EQ(g->member_count(), 1);
}

TEST_F(social_system_test, guild_duplicate_name) {
    system_.create_guild(player_id{1}, "Test Guild", "TG");

    auto result = system_.create_guild(player_id{2}, "Test Guild", "T2");
    EXPECT_FALSE(result.is_ok());
    EXPECT_EQ(result.error(), guild_result::name_taken);
}

TEST_F(social_system_test, guild_invalid_name) {
    auto result = system_.create_guild(player_id{1}, "AB", "TG");  // Too short
    EXPECT_FALSE(result.is_ok());
    EXPECT_EQ(result.error(), guild_result::invalid_name);
}

TEST_F(social_system_test, guild_join_leave) {
    auto result = system_.create_guild(player_id{1}, "Test Guild", "TG");
    guild_id gid = result.value();

    system_.join_guild(player_id{2}, gid);
    const auto* g = system_.get_guild(gid);
    EXPECT_EQ(g->member_count(), 2);

    system_.leave_guild(player_id{2});
    EXPECT_EQ(g->member_count(), 1);
}

TEST_F(social_system_test, guild_kick) {
    auto result = system_.create_guild(player_id{1}, "Test Guild", "TG");
    guild_id gid = result.value();
    system_.join_guild(player_id{2}, gid);

    // Guild master kicks member
    auto kick_result = system_.kick_from_guild(player_id{1}, player_id{2});
    EXPECT_EQ(kick_result, guild_result::success);

    const auto* g = system_.get_guild(gid);
    EXPECT_EQ(g->member_count(), 1);
}

TEST_F(social_system_test, guild_cannot_kick_self) {
    system_.create_guild(player_id{1}, "Test Guild", "TG");

    auto result = system_.kick_from_guild(player_id{1}, player_id{1});
    EXPECT_EQ(result, guild_result::cannot_kick_self);
}

TEST_F(social_system_test, guild_promote_demote) {
    auto result = system_.create_guild(player_id{1}, "Test Guild", "TG");
    guild_id gid = result.value();
    system_.join_guild(player_id{2}, gid);

    const auto* g = system_.get_guild(gid);
    const auto* member = g->get_member(player_id{2});
    EXPECT_EQ(member->rank, guild_rank::recruit);

    system_.promote_member(player_id{1}, player_id{2});
    EXPECT_EQ(member->rank, guild_rank::member);

    system_.demote_member(player_id{1}, player_id{2});
    EXPECT_EQ(member->rank, guild_rank::recruit);
}

TEST_F(social_system_test, guild_set_motd) {
    auto result = system_.create_guild(player_id{1}, "Test Guild", "TG");
    guild_id gid = result.value();

    system_.set_guild_motd(player_id{1}, "Welcome to the guild!");

    const auto* g = system_.get_guild(gid);
    EXPECT_EQ(g->motd, "Welcome to the guild!");
}

TEST_F(social_system_test, find_guild_by_name) {
    auto result = system_.create_guild(player_id{1}, "Test Guild", "TG");
    guild_id gid = result.value();

    EXPECT_EQ(system_.find_guild_by_name("Test Guild"), gid);
    EXPECT_FALSE(system_.find_guild_by_name("Nonexistent").is_valid());
}

// Party system tests

TEST_F(social_system_test, create_party) {
    auto result = system_.create_party(player_id{1});
    ASSERT_TRUE(result.is_ok());

    party_id pid = result.value();
    EXPECT_TRUE(pid.is_valid());
    EXPECT_EQ(system_.party_count(), 1);

    const auto* p = system_.get_party(pid);
    ASSERT_NE(p, nullptr);
    EXPECT_EQ(p->member_count(), 1);
    EXPECT_TRUE(p->is_leader(player_id{1}));
}

TEST_F(social_system_test, party_invite_accept) {
    // First invite auto-creates party
    system_.invite_to_party(player_id{1}, player_id{2});

    party_id pid = system_.get_player_party(player_id{1});
    EXPECT_TRUE(pid.is_valid());

    system_.accept_party_invite(player_id{2}, pid);

    const auto* p = system_.get_party(pid);
    EXPECT_EQ(p->member_count(), 2);
}

TEST_F(social_system_test, party_leave) {
    auto result = system_.create_party(player_id{1});
    party_id pid = result.value();

    system_.invite_to_party(player_id{1}, player_id{2});
    system_.accept_party_invite(player_id{2}, pid);

    system_.leave_party(player_id{2});

    const auto* p = system_.get_party(pid);
    EXPECT_EQ(p->member_count(), 1);
}

TEST_F(social_system_test, party_kick) {
    auto result = system_.create_party(player_id{1});
    party_id pid = result.value();

    system_.invite_to_party(player_id{1}, player_id{2});
    system_.accept_party_invite(player_id{2}, pid);

    auto kick_result = system_.kick_from_party(player_id{1}, player_id{2});
    EXPECT_EQ(kick_result, party_result::success);

    const auto* p = system_.get_party(pid);
    EXPECT_EQ(p->member_count(), 1);
}

TEST_F(social_system_test, party_leader_transfer) {
    auto result = system_.create_party(player_id{1});
    party_id pid = result.value();

    system_.invite_to_party(player_id{1}, player_id{2});
    system_.accept_party_invite(player_id{2}, pid);

    system_.set_party_leader(player_id{1}, player_id{2});

    const auto* p = system_.get_party(pid);
    EXPECT_TRUE(p->is_leader(player_id{2}));
}

TEST_F(social_system_test, party_loot_mode) {
    auto result = system_.create_party(player_id{1});
    party_id pid = result.value();

    system_.set_loot_mode(player_id{1}, loot_mode::round_robin);

    const auto* p = system_.get_party(pid);
    EXPECT_EQ(p->loot, loot_mode::round_robin);
}

TEST_F(social_system_test, party_disband) {
    auto result = system_.create_party(player_id{1});
    party_id pid = result.value();

    system_.disband_party(player_id{1});

    EXPECT_EQ(system_.get_party(pid), nullptr);
    EXPECT_FALSE(system_.get_player_party(player_id{1}).is_valid());
}

// Chat system tests

TEST_F(social_system_test, send_local_chat) {
    bool callback_fired = false;
    system_.on_chat_message([&](const chat_message_event& event) {
        callback_fired = true;
        EXPECT_EQ(event.message.sender, player_id{1});
        EXPECT_EQ(event.message.channel, chat_channel::local);
        EXPECT_EQ(event.message.content, "Hello!");
    });

    auto result = system_.send_local_chat(player_id{1}, "Hello!", map_id{1}, 100, 100);
    EXPECT_EQ(result, filter_result::allowed);
    EXPECT_TRUE(callback_fired);
}

TEST_F(social_system_test, send_guild_chat_no_guild) {
    auto result = system_.send_guild_chat(player_id{1}, "Hello guild!");
    EXPECT_EQ(result, filter_result::blocked);
}

TEST_F(social_system_test, send_guild_chat_with_guild) {
    system_.create_guild(player_id{1}, "Test Guild", "TG");

    bool callback_fired = false;
    system_.on_chat_message([&](const chat_message_event& event) {
        callback_fired = true;
        EXPECT_EQ(event.message.channel, chat_channel::guild);
    });

    auto result = system_.send_guild_chat(player_id{1}, "Hello guild!");
    EXPECT_EQ(result, filter_result::allowed);
    EXPECT_TRUE(callback_fired);
}

TEST_F(social_system_test, send_party_chat_no_party) {
    auto result = system_.send_party_chat(player_id{1}, "Hello party!");
    EXPECT_EQ(result, filter_result::blocked);
}

TEST_F(social_system_test, send_party_chat_with_party) {
    system_.create_party(player_id{1});

    bool callback_fired = false;
    system_.on_chat_message([&](const chat_message_event& event) {
        callback_fired = true;
        EXPECT_EQ(event.message.channel, chat_channel::party);
    });

    auto result = system_.send_party_chat(player_id{1}, "Hello party!");
    EXPECT_EQ(result, filter_result::allowed);
    EXPECT_TRUE(callback_fired);
}

TEST_F(social_system_test, whisper) {
    bool callback_fired = false;
    system_.on_chat_message([&](const chat_message_event& event) {
        callback_fired = true;
        EXPECT_EQ(event.message.sender, player_id{1});
        EXPECT_EQ(event.message.recipient, player_id{2});
        EXPECT_EQ(event.message.channel, chat_channel::whisper);
    });

    auto result = system_.send_whisper(player_id{1}, player_id{2}, "Secret message");
    EXPECT_EQ(result, filter_result::allowed);
    EXPECT_TRUE(callback_fired);
}

TEST_F(social_system_test, whisper_blocked_player) {
    system_.block_player(player_id{2}, player_id{1});

    auto result = system_.send_whisper(player_id{1}, player_id{2}, "Hello");
    EXPECT_EQ(result, filter_result::blocked);
}

TEST_F(social_system_test, block_unblock_player) {
    system_.block_player(player_id{1}, player_id{2});

    auto* settings = system_.get_chat_settings(player_id{1});
    ASSERT_NE(settings, nullptr);
    EXPECT_TRUE(settings->is_player_blocked(player_id{2}));

    system_.unblock_player(player_id{1}, player_id{2});
    EXPECT_FALSE(settings->is_player_blocked(player_id{2}));
}

TEST_F(social_system_test, profanity_filter) {
    social_system_config config;
    config.min_guild_name_length = 3;
    config.max_guild_name_length = 20;
    config.min_guild_tag_length = 2;
    config.max_guild_tag_length = 4;
    config.enable_profanity_filter = true;
    config.profanity_words = {"badword"};
    system_.set_config(config);

    bool callback_fired = false;
    system_.on_chat_message([&](const chat_message_event& event) {
        callback_fired = true;
        EXPECT_EQ(event.message.content, "This is *******!");
    });

    auto result = system_.send_local_chat(player_id{1}, "This is badword!", map_id{1}, 100, 100);
    EXPECT_EQ(result, filter_result::censored);
    EXPECT_TRUE(callback_fired);
}
