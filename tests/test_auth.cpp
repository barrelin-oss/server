// test_auth.cpp
// Tests for the authentication system

#include <gtest/gtest.h>

#include "auth/auth_system.h"
#include "auth/password_hash.h"
#include "auth/session_token.h"
#include "auth/account.h"
#include "network/json_protocol.h"
#include "core/types.h"

using namespace hb;
using namespace hb::auth;
using namespace hb::network;

// Password validation tests
TEST(PasswordValidation, MinimumLength)
{
    auto result = validate_password("ab1");
    EXPECT_FALSE(result.valid);
    EXPECT_FALSE(result.error_message.empty());
}

TEST(PasswordValidation, MaximumLength)
{
    std::string long_password(200, 'a');
    long_password += "1";
    auto result = validate_password(long_password);
    EXPECT_FALSE(result.valid);
}

TEST(PasswordValidation, RequiresLetter)
{
    auto result = validate_password("123456");
    EXPECT_FALSE(result.valid);
    EXPECT_TRUE(result.error_message.find("letter") != std::string::npos);
}

TEST(PasswordValidation, RequiresDigit)
{
    auto result = validate_password("abcdef");
    EXPECT_FALSE(result.valid);
    EXPECT_TRUE(result.error_message.find("digit") != std::string::npos);
}

TEST(PasswordValidation, ValidPassword)
{
    auto result = validate_password("password123");
    EXPECT_TRUE(result.valid);
    EXPECT_TRUE(result.error_message.empty());
}

// Username validation tests
TEST(UsernameValidation, MinimumLength)
{
    auto result = validate_username("ab");
    EXPECT_FALSE(result.valid);
}

TEST(UsernameValidation, MaximumLength)
{
    std::string long_username(40, 'a');
    auto result = validate_username(long_username);
    EXPECT_FALSE(result.valid);
}

TEST(UsernameValidation, MustStartWithLetter)
{
    auto result = validate_username("1username");
    EXPECT_FALSE(result.valid);
}

TEST(UsernameValidation, InvalidCharacters)
{
    auto result = validate_username("user@name");
    EXPECT_FALSE(result.valid);
}

TEST(UsernameValidation, ValidUsername)
{
    auto result = validate_username("player_123");
    EXPECT_TRUE(result.valid);
}

// Session token tests
TEST(SessionToken, GenerateToken)
{
    auto result = generate_token();
    ASSERT_TRUE(result.is_ok());
    EXPECT_FALSE(result.value().empty());
    EXPECT_GE(result.value().size(), 22u); // 16 bytes base64 encoded
}

TEST(SessionToken, TokensAreUnique)
{
    auto result1 = generate_token();
    auto result2 = generate_token();
    ASSERT_TRUE(result1.is_ok());
    ASSERT_TRUE(result2.is_ok());
    EXPECT_NE(result1.value(), result2.value());
}

TEST(SessionToken, ValidateFormat)
{
    auto result = generate_token();
    ASSERT_TRUE(result.is_ok());
    EXPECT_TRUE(validate_token_format(result.value()));
}

TEST(SessionToken, InvalidFormatTooShort)
{
    EXPECT_FALSE(validate_token_format("abc"));
}

TEST(SessionToken, InvalidFormatBadCharacters)
{
    EXPECT_FALSE(validate_token_format("invalid token with spaces!@#"));
}

TEST(SessionToken, CreateSessionToken)
{
    auto result = create_session_token(account_id{42}, std::chrono::seconds{3600}, "127.0.0.1", std::nullopt);

    ASSERT_TRUE(result.is_ok());
    auto& session = result.value();
    EXPECT_EQ(session.account.value, 42u);
    EXPECT_FALSE(session.token.empty());
    EXPECT_EQ(*session.ip_address, "127.0.0.1");
    EXPECT_FALSE(session.is_expired());
}

// JSON protocol tests
TEST(JsonProtocol, ParseMessageType)
{
    EXPECT_EQ(parse_message_type("login_request"), json_message_type::login_request);
    EXPECT_EQ(parse_message_type("login_response"), json_message_type::login_response);
    EXPECT_EQ(parse_message_type("unknown_type"), json_message_type::unknown);
}

TEST(JsonProtocol, ParseValidMessage)
{
    std::string json_str = R"({
        "type": "login_request",
        "seq": 1,
        "data": {
            "username": "testuser",
            "password": "testpass"
        }
    })";

    auto result = json_message::parse(json_str);
    ASSERT_TRUE(result.is_ok());

    auto& msg = result.value();
    EXPECT_EQ(msg.type, json_message_type::login_request);
    EXPECT_EQ(msg.seq, 1u);
    EXPECT_TRUE(msg.data.contains("username"));
}

TEST(JsonProtocol, ParseInvalidJson)
{
    auto result = json_message::parse("not valid json");
    EXPECT_TRUE(result.is_err());
}

TEST(JsonProtocol, ParseMissingType)
{
    auto result = json_message::parse(R"({"seq": 1, "data": {}})");
    EXPECT_TRUE(result.is_err());
}

TEST(JsonProtocol, SerializeMessage)
{
    json_message msg{
        .type = json_message_type::pong, .seq = 42, .data = nlohmann::json{{"timestamp", 12345}}, .raw_type = "pong"};

    auto json = msg.to_json();
    EXPECT_EQ(json["type"], "pong");
    EXPECT_EQ(json["seq"], 42);
    EXPECT_EQ(json["data"]["timestamp"], 12345);
}

TEST(JsonProtocol, ParseLoginRequest)
{
    nlohmann::json data = {{"username", "testuser"}, {"password", "testpass123"}};

    auto result = login_request_data::from_json(data);
    ASSERT_TRUE(result.is_ok());
    EXPECT_EQ(result.value().username, "testuser");
    EXPECT_EQ(result.value().password, "testpass123");
}

TEST(JsonProtocol, ParseLoginRequestMissingFields)
{
    nlohmann::json data = {
        {"username", "testuser"}
        // Missing password
    };

    auto result = login_request_data::from_json(data);
    EXPECT_TRUE(result.is_err());
}

TEST(JsonProtocol, MakeLoginResponse)
{
    auto msg = make_login_response(1, true, "token123");
    EXPECT_EQ(msg.type, json_message_type::login_response);
    EXPECT_EQ(msg.seq, 1u);
    EXPECT_TRUE(msg.data["success"].get<bool>());
    EXPECT_EQ(msg.data["session_token"], "token123");
}

TEST(JsonProtocol, MakeLoginResponseFailure)
{
    auto msg = make_login_response(1, false, std::nullopt, "invalid_credentials");
    EXPECT_FALSE(msg.data["success"].get<bool>());
    EXPECT_EQ(msg.data["error"], "invalid_credentials");
}

TEST(JsonProtocol, ParseCreateCharacterRequest)
{
    nlohmann::json data = {{"name", "Hero"},
                           {"class_type", 1},
                           {"nation", 2},
                           {"gender", 0},
                           {"hair_style", 1},
                           {"hair_color", 2},
                           {"strength", 15},
                           {"dexterity", 12}};

    auto result = create_character_request_data::from_json(data);
    ASSERT_TRUE(result.is_ok());

    auto& char_data = result.value();
    EXPECT_EQ(char_data.name, "Hero");
    EXPECT_EQ(char_data.class_type, 1);
    EXPECT_EQ(char_data.nation, 2);
    EXPECT_EQ(char_data.gender, 0);
    EXPECT_EQ(*char_data.strength, 15);
    EXPECT_EQ(*char_data.dexterity, 12);
    // Stats always get a value (default 10) from safe_int16 in from_json
    EXPECT_TRUE(char_data.vitality.has_value());
    EXPECT_EQ(*char_data.vitality, 10); // Default value
}

TEST(JsonProtocol, CreateCharacterToCreateInfo)
{
    create_character_request_data data;
    data.name = "TestChar";
    data.class_type = 1;
    data.nation = 2;
    data.gender = 0;
    data.strength = 15;

    auto info = data.to_create_info();
    EXPECT_EQ(info.name, "TestChar");
    EXPECT_EQ(info.class_type, 1);
    EXPECT_EQ(info.nation, 2);
    EXPECT_EQ(*info.strength, 15);
}

TEST(JsonProtocol, MakeGetCharactersResponse)
{
    std::vector<character_summary> chars = {{.id = player_id{1},
                                             .name = "Hero",
                                             .level = 50,
                                             .class_type = 1,
                                             .nation = 2,
                                             .gender = 0,
                                             .map_name = "default",
                                             .experience = 12345,
                                             .hair_style = 0,
                                             .hair_color = 0,
                                             .skin_color = 0,
                                             .last_played = std::nullopt},
                                            {.id = player_id{2},
                                             .name = "Mage",
                                             .level = 30,
                                             .class_type = 2,
                                             .nation = 1,
                                             .gender = 1,
                                             .map_name = "elvine",
                                             .experience = 5000,
                                             .hair_style = 0,
                                             .hair_color = 0,
                                             .skin_color = 0,
                                             .last_played = std::nullopt}};

    auto msg = make_get_characters_response(5, chars);
    EXPECT_EQ(msg.type, json_message_type::get_characters_response);
    EXPECT_EQ(msg.seq, 5u);
    EXPECT_TRUE(msg.data["success"].get<bool>());
    EXPECT_EQ(msg.data["characters"].size(), 2u);
    EXPECT_EQ(msg.data["characters"][0]["name"], "Hero");
    EXPECT_EQ(msg.data["characters"][1]["level"], 30);
}

// Auth error conversion test
TEST(AuthError, ToString)
{
    EXPECT_EQ(to_string(auth_error::success), "success");
    EXPECT_EQ(to_string(auth_error::invalid_credentials), "invalid_credentials");
    EXPECT_EQ(to_string(auth_error::account_banned), "account_banned");
    EXPECT_EQ(to_string(auth_error::username_taken), "username_taken");
}

// Account ID type tests
TEST(AccountId, DefaultConstruction)
{
    account_id id;
    EXPECT_EQ(id.value, 0u);
    EXPECT_FALSE(id.is_valid());
}

TEST(AccountId, ExplicitConstruction)
{
    account_id id{42};
    EXPECT_EQ(id.value, 42u);
    EXPECT_TRUE(id.is_valid());
}

TEST(AccountId, Comparison)
{
    account_id id1{1};
    account_id id2{2};
    account_id id3{1};

    EXPECT_TRUE(id1 < id2);
    EXPECT_TRUE(id1 == id3);
    EXPECT_TRUE(id1 != id2);
}

// Registration rate limiting tests
// auth_system without a database will fail at the DB stage (internal_error),
// but rate limiting happens before that, so we can verify the error changes
// from internal_error to rate_limited after exceeding the limit.

class RegistrationRateLimitTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        auth_.initialize();
        auth::auth_config cfg;
        cfg.max_registration_attempts = 3;
        cfg.registration_cooldown = std::chrono::seconds{3600};
        auth_.set_config(cfg);
    }

    auth_system auth_;
};

TEST_F(RegistrationRateLimitTest, under_limit_reaches_db_stage)
{
    // Without a DB, create_account fails at the DB call (username_exists returns false
    // since no DB, then hash_password succeeds, then db_create_account fails).
    // The important thing is it does NOT return rate_limited.
    auto result = auth_.create_account("testuser1", "password123", "192.168.1.1");
    EXPECT_TRUE(result.is_err());
    EXPECT_NE(result.error(), auth_error::rate_limited);
}

TEST_F(RegistrationRateLimitTest, over_limit_returns_rate_limited)
{
    // Use up 3 attempts
    for (int i = 0; i < 3; ++i)
    {
        auto result = auth_.create_account(
            std::string("user") + std::to_string(i), "password123", "192.168.1.1");
        EXPECT_NE(result.error(), auth_error::rate_limited);
    }

    // 4th attempt should be rate limited
    auto result = auth_.create_account("user_blocked", "password123", "192.168.1.1");
    EXPECT_TRUE(result.is_err());
    EXPECT_EQ(result.error(), auth_error::rate_limited);
}

TEST_F(RegistrationRateLimitTest, different_ips_are_independent)
{
    // Fill up IP1
    for (int i = 0; i < 3; ++i)
    {
        (void)auth_.create_account(
            std::string("user_a") + std::to_string(i), "password123", "10.0.0.1");
    }

    // IP1 should be rate limited
    auto result1 = auth_.create_account("blocked_a", "password123", "10.0.0.1");
    EXPECT_EQ(result1.error(), auth_error::rate_limited);

    // IP2 should still work (not rate limited)
    auto result2 = auth_.create_account("user_b0", "password123", "10.0.0.2");
    EXPECT_NE(result2.error(), auth_error::rate_limited);
}

TEST_F(RegistrationRateLimitTest, no_ip_skips_rate_limiting)
{
    // Without IP, rate limiting is skipped entirely
    for (int i = 0; i < 5; ++i)
    {
        auto result = auth_.create_account(
            std::string("noip_user") + std::to_string(i), "password123");
        EXPECT_NE(result.error(), auth_error::rate_limited);
    }
}

TEST_F(RegistrationRateLimitTest, window_expiry_resets_counter)
{
    // Configure with a very short cooldown for this test
    auth::auth_config cfg;
    cfg.max_registration_attempts = 2;
    cfg.registration_cooldown = std::chrono::seconds{0}; // Immediate expiry
    auth_.set_config(cfg);

    // Use up 2 attempts
    for (int i = 0; i < 2; ++i)
    {
        (void)auth_.create_account(
            std::string("exp_user") + std::to_string(i), "password123", "172.16.0.1");
    }

    // With 0-second cooldown, window has already expired — should NOT be rate limited
    auto result = auth_.create_account("exp_user_after", "password123", "172.16.0.1");
    EXPECT_NE(result.error(), auth_error::rate_limited);
}
