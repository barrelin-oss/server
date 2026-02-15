// test_protocol.cpp
// Unit tests for protocol message reader/writer

#include <gtest/gtest.h>
#include "protocol/protocol.h"
#include "protocol/message_reader.h"
#include "protocol/message_writer.h"

using namespace hb::protocol;

// Message writer tests

TEST(message_writer_test, write_u8)
{
    message_writer writer;
    writer.write_u8(0x42);
    writer.write_u8(0xFF);

    auto data = writer.data();
    ASSERT_EQ(data.size(), 2);
    EXPECT_EQ(data[0], 0x42);
    EXPECT_EQ(data[1], 0xFF);
}

TEST(message_writer_test, write_u16_little_endian)
{
    message_writer writer;
    writer.write_u16(0x1234);

    auto data = writer.data();
    ASSERT_EQ(data.size(), 2);
    EXPECT_EQ(data[0], 0x34); // Low byte first
    EXPECT_EQ(data[1], 0x12); // High byte second
}

TEST(message_writer_test, write_u32_little_endian)
{
    message_writer writer;
    writer.write_u32(0x12345678);

    auto data = writer.data();
    ASSERT_EQ(data.size(), 4);
    EXPECT_EQ(data[0], 0x78);
    EXPECT_EQ(data[1], 0x56);
    EXPECT_EQ(data[2], 0x34);
    EXPECT_EQ(data[3], 0x12);
}

TEST(message_writer_test, write_i16_negative)
{
    message_writer writer;
    writer.write_i16(-1);

    auto data = writer.data();
    ASSERT_EQ(data.size(), 2);
    EXPECT_EQ(data[0], 0xFF);
    EXPECT_EQ(data[1], 0xFF);
}

TEST(message_writer_test, write_fixed_string)
{
    message_writer writer;
    writer.write_fixed_string("Hello", 10);

    auto data = writer.data();
    ASSERT_EQ(data.size(), 10);
    EXPECT_EQ(data[0], 'H');
    EXPECT_EQ(data[4], 'o');
    EXPECT_EQ(data[5], 0); // Null padding
    EXPECT_EQ(data[9], 0);
}

TEST(message_writer_test, write_string_u8)
{
    message_writer writer;
    writer.write_string_u8("Test");

    auto data = writer.data();
    ASSERT_EQ(data.size(), 5); // 1 byte length + 4 chars
    EXPECT_EQ(data[0], 4);     // Length prefix
    EXPECT_EQ(data[1], 'T');
    EXPECT_EQ(data[4], 't');
}

TEST(message_writer_test, write_bytes)
{
    message_writer writer;
    std::vector<uint8_t> bytes = {0x01, 0x02, 0x03};
    writer.write_bytes(bytes);

    EXPECT_EQ(writer.size(), 3);
    auto data = writer.data();
    EXPECT_EQ(data[0], 0x01);
    EXPECT_EQ(data[2], 0x03);
}

TEST(message_writer_test, write_at)
{
    message_writer writer;
    writer.write_zeros(4);
    writer.write_u16_at(0, 0xABCD);

    auto data = writer.data();
    EXPECT_EQ(data[0], 0xCD);
    EXPECT_EQ(data[1], 0xAB);
}

TEST(message_writer_test, clear_and_reuse)
{
    message_writer writer;
    writer.write_u32(0x12345678);
    EXPECT_EQ(writer.size(), 4);

    writer.clear();
    EXPECT_EQ(writer.size(), 0);

    writer.write_u16(0x1234);
    EXPECT_EQ(writer.size(), 2);
}

// Message reader tests

TEST(message_reader_test, read_u8)
{
    std::vector<uint8_t> data = {0x42, 0xFF};
    message_reader reader(data);

    EXPECT_EQ(reader.read_u8(), 0x42);
    EXPECT_EQ(reader.read_u8(), 0xFF);
    EXPECT_TRUE(reader.at_end());
}

TEST(message_reader_test, read_u16_little_endian)
{
    std::vector<uint8_t> data = {0x34, 0x12}; // Little-endian 0x1234
    message_reader reader(data);

    EXPECT_EQ(reader.read_u16(), 0x1234);
}

TEST(message_reader_test, read_u32_little_endian)
{
    std::vector<uint8_t> data = {0x78, 0x56, 0x34, 0x12};
    message_reader reader(data);

    EXPECT_EQ(reader.read_u32(), 0x12345678);
}

TEST(message_reader_test, read_i16_negative)
{
    std::vector<uint8_t> data = {0xFF, 0xFF};
    message_reader reader(data);

    EXPECT_EQ(reader.read_i16(), -1);
}

TEST(message_reader_test, read_fixed_string)
{
    std::vector<uint8_t> data = {'H', 'e', 'l', 'l', 'o', 0, 0, 0, 0, 0};
    message_reader reader(data);

    auto str = reader.read_fixed_string(10);
    EXPECT_EQ(str, "Hello");
    EXPECT_TRUE(reader.at_end());
}

TEST(message_reader_test, read_string_u8)
{
    std::vector<uint8_t> data = {4, 'T', 'e', 's', 't'};
    message_reader reader(data);

    auto str = reader.read_string_u8();
    EXPECT_EQ(str, "Test");
}

TEST(message_reader_test, read_not_enough_data)
{
    std::vector<uint8_t> data = {0x12};
    message_reader reader(data);

    EXPECT_THROW((void)reader.read_u16(), read_error);
}

TEST(message_reader_test, peek_without_advancing)
{
    std::vector<uint8_t> data = {0x42};
    message_reader reader(data);

    auto peeked = reader.peek_u8();
    EXPECT_TRUE(peeked.has_value());
    EXPECT_EQ(*peeked, 0x42);
    EXPECT_EQ(reader.position(), 0); // Position unchanged
}

TEST(message_reader_test, seek_and_skip)
{
    std::vector<uint8_t> data = {0x01, 0x02, 0x03, 0x04};
    message_reader reader(data);

    reader.skip(2);
    EXPECT_EQ(reader.read_u8(), 0x03);

    reader.seek(0);
    EXPECT_EQ(reader.read_u8(), 0x01);
}

TEST(message_reader_test, has_remaining)
{
    std::vector<uint8_t> data = {0x01, 0x02, 0x03};
    message_reader reader(data);

    EXPECT_TRUE(reader.has_remaining(3));
    EXPECT_FALSE(reader.has_remaining(4));

    (void)reader.read_u8();
    EXPECT_TRUE(reader.has_remaining(2));
    EXPECT_FALSE(reader.has_remaining(3));
}

TEST(message_reader_test, try_read)
{
    std::vector<uint8_t> data = {0x42};
    message_reader reader(data);

    auto val1 = reader.try_read_u8();
    EXPECT_TRUE(val1.has_value());
    EXPECT_EQ(*val1, 0x42);

    auto val2 = reader.try_read_u8();
    EXPECT_FALSE(val2.has_value());
}

// Round-trip tests

TEST(message_round_trip_test, integers)
{
    message_writer writer;
    writer.write_u8(0x42);
    writer.write_i8(-10);
    writer.write_u16(0x1234);
    writer.write_i16(-1000);
    writer.write_u32(0xDEADBEEF);
    writer.write_i32(-123456);
    writer.write_u64(0x123456789ABCDEF0ULL);

    message_reader reader(writer.data());
    EXPECT_EQ(reader.read_u8(), 0x42);
    EXPECT_EQ(reader.read_i8(), -10);
    EXPECT_EQ(reader.read_u16(), 0x1234);
    EXPECT_EQ(reader.read_i16(), -1000);
    EXPECT_EQ(reader.read_u32(), 0xDEADBEEF);
    EXPECT_EQ(reader.read_i32(), -123456);
    EXPECT_EQ(reader.read_u64(), 0x123456789ABCDEF0ULL);
}

TEST(message_round_trip_test, strings)
{
    message_writer writer;
    writer.write_fixed_string("Hello", 10);
    writer.write_string_u8("World");
    writer.write_string_u16("Longer string here");

    message_reader reader(writer.data());
    EXPECT_EQ(reader.read_fixed_string(10), "Hello");
    EXPECT_EQ(reader.read_string_u8(), "World");
    EXPECT_EQ(reader.read_string_u16(), "Longer string here");
}

TEST(message_round_trip_test, floats)
{
    message_writer writer;
    writer.write_f32(3.14159f);
    writer.write_f64(2.718281828);

    message_reader reader(writer.data());
    EXPECT_FLOAT_EQ(reader.read_f32(), 3.14159f);
    EXPECT_DOUBLE_EQ(reader.read_f64(), 2.718281828);
}

// Protocol type tests

TEST(protocol_test, message_id_string)
{
    EXPECT_EQ(message_id_string(message_id::request_login), "request_login");
    EXPECT_EQ(message_id_string(message_id::notify), "notify");
}

TEST(protocol_test, notify_type_string)
{
    EXPECT_EQ(notify_type_string(notify_type::hp), "hp");
    EXPECT_EQ(notify_type_string(notify_type::level_up), "level_up");
}

TEST(protocol_test, common_type_string)
{
    EXPECT_EQ(common_type_string(common_type::item_drop), "item_drop");
    EXPECT_EQ(common_type_string(common_type::magic), "magic");
}
