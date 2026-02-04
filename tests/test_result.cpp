// test_result.cpp
// Unit tests for the result<T, E> type

#include <gtest/gtest.h>
#include "core/result.h"

using namespace hb;

TEST(result_test, ok_construction) {
    auto r = result<int>::ok(42);
    EXPECT_TRUE(r.is_ok());
    EXPECT_FALSE(r.is_err());
    EXPECT_EQ(r.value(), 42);
}

TEST(result_test, err_construction) {
    auto r = result<int>::err("something went wrong");
    EXPECT_FALSE(r.is_ok());
    EXPECT_TRUE(r.is_err());
    EXPECT_EQ(r.error(), "something went wrong");
}

TEST(result_test, implicit_ok_conversion) {
    result<int> r = 42;
    EXPECT_TRUE(r.is_ok());
    EXPECT_EQ(r.value(), 42);
}

TEST(result_test, bool_conversion) {
    auto ok = result<int>::ok(1);
    auto err = result<int>::err("error");

    EXPECT_TRUE(static_cast<bool>(ok));
    EXPECT_FALSE(static_cast<bool>(err));

    if (ok) {
        SUCCEED();
    } else {
        FAIL() << "ok result should convert to true";
    }

    if (err) {
        FAIL() << "err result should convert to false";
    } else {
        SUCCEED();
    }
}

TEST(result_test, value_throws_on_error) {
    auto r = result<int>::err("error");
    EXPECT_THROW((void)r.value(), std::runtime_error);
}

TEST(result_test, error_throws_on_ok) {
    auto r = result<int>::ok(42);
    EXPECT_THROW((void)r.error(), std::runtime_error);
}

TEST(result_test, value_or) {
    auto ok = result<int>::ok(42);
    auto err = result<int>::err("error");

    EXPECT_EQ(ok.value_or(0), 42);
    EXPECT_EQ(err.value_or(0), 0);
}

TEST(result_test, value_or_else) {
    auto ok = result<int>::ok(42);
    auto err = result<int>::err("error");

    EXPECT_EQ(ok.value_or_else([]{ return -1; }), 42);
    EXPECT_EQ(err.value_or_else([]{ return -1; }), -1);
}

TEST(result_test, map) {
    auto r = result<int>::ok(42);
    auto mapped = r.map([](int x) { return x * 2; });

    EXPECT_TRUE(mapped.is_ok());
    EXPECT_EQ(mapped.value(), 84);
}

TEST(result_test, map_preserves_error) {
    auto r = result<int>::err("error");
    auto mapped = r.map([](int x) { return x * 2; });

    EXPECT_TRUE(mapped.is_err());
    EXPECT_EQ(mapped.error(), "error");
}

TEST(result_test, map_err) {
    auto r = result<int, int>::err(1);
    auto mapped = r.map_err([](int x) { return x * 2; });

    EXPECT_TRUE(mapped.is_err());
    EXPECT_EQ(mapped.error(), 2);
}

TEST(result_test, and_then) {
    auto divide = [](int x) -> result<int> {
        if (x == 0) return result<int>::err("division by zero");
        return result<int>::ok(100 / x);
    };

    auto r1 = result<int>::ok(10);
    auto r2 = r1.and_then(divide);
    EXPECT_TRUE(r2.is_ok());
    EXPECT_EQ(r2.value(), 10);

    auto r3 = result<int>::ok(0);
    auto r4 = r3.and_then(divide);
    EXPECT_TRUE(r4.is_err());
    EXPECT_EQ(r4.error(), "division by zero");
}

TEST(result_test, expect) {
    auto ok = result<int>::ok(42);
    EXPECT_EQ(ok.expect("should not throw"), 42);

    auto err = result<int>::err("error");
    EXPECT_THROW((void)err.expect("custom message"), std::runtime_error);
}

TEST(result_test, void_result) {
    auto ok = result<void>::ok();
    EXPECT_TRUE(ok.is_ok());
    EXPECT_FALSE(ok.is_err());

    auto err = result<void>::err("error");
    EXPECT_FALSE(err.is_ok());
    EXPECT_TRUE(err.is_err());
    EXPECT_EQ(err.error(), "error");
}

TEST(result_test, void_result_expect) {
    auto ok = result<void>::ok();
    EXPECT_NO_THROW(ok.expect("should not throw"));

    auto err = result<void>::err("error");
    EXPECT_THROW(err.expect("custom message"), std::runtime_error);
}

TEST(result_test, make_ok_helper) {
    auto r = make_ok(42);
    EXPECT_TRUE(r.is_ok());
    EXPECT_EQ(r.value(), 42);
}

TEST(result_test, make_err_helper) {
    auto r = make_err<int>(std::string("error"));
    EXPECT_TRUE(r.is_err());
    EXPECT_EQ(r.error(), "error");
}

TEST(result_test, move_semantics) {
    auto r1 = result<std::string>::ok("hello");
    auto r2 = std::move(r1).map([](std::string&& s) {
        return s + " world";
    });

    EXPECT_TRUE(r2.is_ok());
    EXPECT_EQ(r2.value(), "hello world");
}
