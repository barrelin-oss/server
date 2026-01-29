// test_session.cpp
// Unit tests for session manager

#include <gtest/gtest.h>
#include "session/session.h"
#include "session/session_manager.h"

using namespace hb;

// Use namespace alias to avoid ambiguity with class name
namespace sess = hb::session;

// Session tests

TEST(session_test, construction) {
    sess::session s{session_id{1}, connection_id{100}};

    EXPECT_EQ(s.id().value, 1);
    EXPECT_EQ(s.connection().value, 100);
    EXPECT_EQ(s.state(), sess::session_state::connected);
    EXPECT_FALSE(s.is_authenticated());
    EXPECT_FALSE(s.is_in_game());
    EXPECT_FALSE(s.is_admin());
}

TEST(session_test, state_transitions) {
    sess::session s{session_id{1}, connection_id{100}};

    EXPECT_EQ(s.state(), sess::session_state::connected);

    s.set_state(sess::session_state::authenticating);
    EXPECT_EQ(s.state(), sess::session_state::authenticating);

    s.set_state(sess::session_state::character_select);
    EXPECT_EQ(s.state(), sess::session_state::character_select);

    s.set_state(sess::session_state::in_game);
    EXPECT_EQ(s.state(), sess::session_state::in_game);
    EXPECT_TRUE(s.is_in_game());
}

TEST(session_test, authentication) {
    sess::session s{session_id{1}, connection_id{100}};

    EXPECT_FALSE(s.is_authenticated());

    sess::account_info account;
    account.account_id = 42;
    account.account_name = "TestUser";
    account.admin_level = 0;
    account.is_banned = false;

    s.set_account(account);

    EXPECT_TRUE(s.is_authenticated());
    EXPECT_TRUE(s.account().has_value());
    EXPECT_EQ(s.account()->account_id, 42);
    EXPECT_EQ(s.account()->account_name, "TestUser");
    EXPECT_FALSE(s.is_admin());
}

TEST(session_test, admin_detection) {
    sess::session s{session_id{1}, connection_id{100}};

    sess::account_info account;
    account.account_id = 1;
    account.account_name = "Admin";
    account.admin_level = 5;

    s.set_account(account);

    EXPECT_TRUE(s.is_admin());
}

TEST(session_test, current_player) {
    sess::session s{session_id{1}, connection_id{100}};

    EXPECT_FALSE(s.current_player().has_value());

    s.set_current_player(player_id{500});

    EXPECT_TRUE(s.current_player().has_value());
    EXPECT_EQ(s.current_player()->value, 500);
}

TEST(session_test, character_list) {
    sess::session s{session_id{1}, connection_id{100}};

    EXPECT_TRUE(s.characters().empty());

    std::vector<sess::character_info> chars;
    chars.push_back(sess::character_info{.character_id = 1, .name = "Warrior", .level = 50});
    chars.push_back(sess::character_info{.character_id = 2, .name = "Mage", .level = 30});

    s.set_characters(chars);

    EXPECT_EQ(s.characters().size(), 2);
    EXPECT_EQ(s.characters()[0].name, "Warrior");
    EXPECT_EQ(s.characters()[1].level, 30);
}

TEST(session_test, state_strings) {
    EXPECT_EQ(sess::state_string(sess::session_state::connected), "connected");
    EXPECT_EQ(sess::state_string(sess::session_state::authenticating), "authenticating");
    EXPECT_EQ(sess::state_string(sess::session_state::character_select), "character_select");
    EXPECT_EQ(sess::state_string(sess::session_state::entering_world), "entering_world");
    EXPECT_EQ(sess::state_string(sess::session_state::in_game), "in_game");
    EXPECT_EQ(sess::state_string(sess::session_state::disconnecting), "disconnecting");
}

// Session manager tests

class session_manager_test : public ::testing::Test {
protected:
    void SetUp() override {
        manager_.initialize();
    }

    void TearDown() override {
        manager_.shutdown();
    }

    sess::session_manager manager_;
};

TEST_F(session_manager_test, lifecycle) {
    EXPECT_TRUE(manager_.is_initialized());
    EXPECT_EQ(manager_.name(), "session_manager");
    EXPECT_EQ(manager_.count(), 0);
}

TEST_F(session_manager_test, create_session) {
    auto result = manager_.create_session(connection_id{100});
    ASSERT_TRUE(result.is_ok()) << result.error();

    auto id = result.value();
    EXPECT_TRUE(id.is_valid());
    EXPECT_EQ(manager_.count(), 1);

    auto* s = manager_.get(id);
    ASSERT_NE(s, nullptr);
    EXPECT_EQ(s->connection().value, 100);
}

TEST_F(session_manager_test, create_duplicate_connection_fails) {
    auto result1 = manager_.create_session(connection_id{100});
    ASSERT_TRUE(result1.is_ok());

    auto result2 = manager_.create_session(connection_id{100});
    EXPECT_TRUE(result2.is_err());
}

TEST_F(session_manager_test, destroy_session) {
    auto result = manager_.create_session(connection_id{100});
    ASSERT_TRUE(result.is_ok());
    auto id = result.value();

    EXPECT_EQ(manager_.count(), 1);
    manager_.destroy_session(id, "test");
    EXPECT_EQ(manager_.count(), 0);
    EXPECT_EQ(manager_.get(id), nullptr);
}

TEST_F(session_manager_test, destroy_by_connection) {
    auto result = manager_.create_session(connection_id{100});
    ASSERT_TRUE(result.is_ok());

    EXPECT_EQ(manager_.count(), 1);
    manager_.destroy_session_by_connection(connection_id{100}, "disconnect");
    EXPECT_EQ(manager_.count(), 0);
}

TEST_F(session_manager_test, get_by_connection) {
    auto result = manager_.create_session(connection_id{100});
    ASSERT_TRUE(result.is_ok());

    auto* s = manager_.get_by_connection(connection_id{100});
    ASSERT_NE(s, nullptr);
    EXPECT_EQ(s->id(), result.value());

    // Non-existent connection
    EXPECT_EQ(manager_.get_by_connection(connection_id{999}), nullptr);
}

TEST_F(session_manager_test, get_by_player) {
    auto result = manager_.create_session(connection_id{100});
    ASSERT_TRUE(result.is_ok());
    auto id = result.value();

    // Before entering game
    EXPECT_EQ(manager_.get_by_player(player_id{500}), nullptr);

    // Enter game
    manager_.enter_game(id, player_id{500});

    auto* s = manager_.get_by_player(player_id{500});
    ASSERT_NE(s, nullptr);
    EXPECT_EQ(s->id(), id);
}

TEST_F(session_manager_test, authenticate_session) {
    auto result = manager_.create_session(connection_id{100});
    ASSERT_TRUE(result.is_ok());
    auto id = result.value();

    sess::account_info account;
    account.account_id = 42;
    account.account_name = "TestUser";
    account.admin_level = 0;

    manager_.authenticate_session(id, account);

    auto* s = manager_.get(id);
    ASSERT_NE(s, nullptr);
    EXPECT_TRUE(s->is_authenticated());
    EXPECT_EQ(s->state(), sess::session_state::character_select);
    EXPECT_EQ(s->account()->account_name, "TestUser");
}

TEST_F(session_manager_test, enter_game) {
    auto result = manager_.create_session(connection_id{100});
    ASSERT_TRUE(result.is_ok());
    auto id = result.value();

    manager_.enter_game(id, player_id{500});

    auto* s = manager_.get(id);
    ASSERT_NE(s, nullptr);
    EXPECT_TRUE(s->is_in_game());
    EXPECT_EQ(s->current_player()->value, 500);
}

TEST_F(session_manager_test, count_authenticated) {
    manager_.create_session(connection_id{100});
    manager_.create_session(connection_id{101});
    manager_.create_session(connection_id{102});

    EXPECT_EQ(manager_.count(), 3);
    EXPECT_EQ(manager_.count_authenticated(), 0);

    // Authenticate one
    auto* s = manager_.get_by_connection(connection_id{100});
    manager_.authenticate_session(s->id(), sess::account_info{.account_id = 1, .account_name = "User1"});

    EXPECT_EQ(manager_.count_authenticated(), 1);
}

TEST_F(session_manager_test, count_in_game) {
    auto r1 = manager_.create_session(connection_id{100});
    auto r2 = manager_.create_session(connection_id{101});

    EXPECT_EQ(manager_.count_in_game(), 0);

    manager_.enter_game(r1.value(), player_id{500});
    EXPECT_EQ(manager_.count_in_game(), 1);

    manager_.enter_game(r2.value(), player_id{501});
    EXPECT_EQ(manager_.count_in_game(), 2);
}

TEST_F(session_manager_test, session_limit) {
    sess::session_config config;
    config.max_sessions = 3;
    manager_.set_config(config);

    EXPECT_TRUE(manager_.create_session(connection_id{100}).is_ok());
    EXPECT_TRUE(manager_.create_session(connection_id{101}).is_ok());
    EXPECT_TRUE(manager_.create_session(connection_id{102}).is_ok());

    // Fourth should fail
    auto result = manager_.create_session(connection_id{103});
    EXPECT_TRUE(result.is_err());
}

TEST_F(session_manager_test, for_each) {
    manager_.create_session(connection_id{100});
    manager_.create_session(connection_id{101});
    manager_.create_session(connection_id{102});

    int count = 0;
    manager_.for_each([&count](const sess::session& /*s*/) {
        ++count;
    });

    EXPECT_EQ(count, 3);
}
