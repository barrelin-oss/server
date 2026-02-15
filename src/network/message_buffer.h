#pragma once

// message_buffer.h
// Binary message read/write buffer for network protocol

#include <vector>
#include <string>
#include <string_view>
#include <cstdint>
#include <span>
#include <stdexcept>

namespace hb::network
{

// Buffer for writing binary messages
class message_writer
{
public:
    explicit message_writer(size_t initial_capacity = 256) { buffer_.reserve(initial_capacity); }

    // Write primitives (little-endian)
    void write_u8(uint8_t value) { buffer_.push_back(value); }

    void write_i8(int8_t value) { buffer_.push_back(static_cast<uint8_t>(value)); }

    void write_u16(uint16_t value)
    {
        buffer_.push_back(static_cast<uint8_t>(value & 0xFF));
        buffer_.push_back(static_cast<uint8_t>((value >> 8) & 0xFF));
    }

    void write_i16(int16_t value) { write_u16(static_cast<uint16_t>(value)); }

    void write_u32(uint32_t value)
    {
        buffer_.push_back(static_cast<uint8_t>(value & 0xFF));
        buffer_.push_back(static_cast<uint8_t>((value >> 8) & 0xFF));
        buffer_.push_back(static_cast<uint8_t>((value >> 16) & 0xFF));
        buffer_.push_back(static_cast<uint8_t>((value >> 24) & 0xFF));
    }

    void write_i32(int32_t value) { write_u32(static_cast<uint32_t>(value)); }

    void write_u64(uint64_t value)
    {
        for (int i = 0; i < 8; ++i)
        {
            buffer_.push_back(static_cast<uint8_t>((value >> (i * 8)) & 0xFF));
        }
    }

    void write_i64(int64_t value) { write_u64(static_cast<uint64_t>(value)); }

    // Write string (length-prefixed with u16)
    void write_string(std::string_view str)
    {
        write_u16(static_cast<uint16_t>(str.size()));
        buffer_.insert(buffer_.end(), str.begin(), str.end());
    }

    // Write fixed-length string (null-padded)
    void write_fixed_string(std::string_view str, size_t length)
    {
        size_t to_copy = std::min(str.size(), length);
        buffer_.insert(buffer_.end(), str.begin(), str.begin() + to_copy);
        // Pad with nulls
        for (size_t i = to_copy; i < length; ++i)
        {
            buffer_.push_back(0);
        }
    }

    // Write raw bytes
    void write_bytes(std::span<const uint8_t> data) { buffer_.insert(buffer_.end(), data.begin(), data.end()); }

    // Get buffer contents
    [[nodiscard]] auto data() const -> std::span<const uint8_t> { return buffer_; }

    [[nodiscard]] auto size() const -> size_t { return buffer_.size(); }

    void clear() { buffer_.clear(); }

    // Take ownership of buffer
    [[nodiscard]] auto take() -> std::vector<uint8_t> { return std::move(buffer_); }

private:
    std::vector<uint8_t> buffer_;
};

// Buffer for reading binary messages
class message_reader
{
public:
    explicit message_reader(std::span<const uint8_t> data) : data_(data), position_(0) {}

    // Read primitives (little-endian)
    [[nodiscard]] auto read_u8() -> uint8_t
    {
        check_remaining(1);
        return data_[position_++];
    }

    [[nodiscard]] auto read_i8() -> int8_t { return static_cast<int8_t>(read_u8()); }

    [[nodiscard]] auto read_u16() -> uint16_t
    {
        check_remaining(2);
        uint16_t value = static_cast<uint16_t>(data_[position_]) | (static_cast<uint16_t>(data_[position_ + 1]) << 8);
        position_ += 2;
        return value;
    }

    [[nodiscard]] auto read_i16() -> int16_t { return static_cast<int16_t>(read_u16()); }

    [[nodiscard]] auto read_u32() -> uint32_t
    {
        check_remaining(4);
        uint32_t value = static_cast<uint32_t>(data_[position_]) | (static_cast<uint32_t>(data_[position_ + 1]) << 8) |
                         (static_cast<uint32_t>(data_[position_ + 2]) << 16) |
                         (static_cast<uint32_t>(data_[position_ + 3]) << 24);
        position_ += 4;
        return value;
    }

    [[nodiscard]] auto read_i32() -> int32_t { return static_cast<int32_t>(read_u32()); }

    [[nodiscard]] auto read_u64() -> uint64_t
    {
        check_remaining(8);
        uint64_t value = 0;
        for (int i = 0; i < 8; ++i)
        {
            value |= static_cast<uint64_t>(data_[position_ + static_cast<size_t>(i)]) << (i * 8);
        }
        position_ += 8;
        return value;
    }

    [[nodiscard]] auto read_i64() -> int64_t { return static_cast<int64_t>(read_u64()); }

    // Read length-prefixed string
    [[nodiscard]] auto read_string() -> std::string
    {
        uint16_t length = read_u16();
        check_remaining(length);
        std::string result(reinterpret_cast<const char*>(&data_[position_]), length);
        position_ += length;
        return result;
    }

    // Read fixed-length string (stops at null)
    [[nodiscard]] auto read_fixed_string(size_t length) -> std::string
    {
        check_remaining(length);
        const char* start = reinterpret_cast<const char*>(&data_[position_]);
        size_t actual_length = 0;
        while (actual_length < length && start[actual_length] != '\0')
        {
            ++actual_length;
        }
        std::string result(start, actual_length);
        position_ += length;
        return result;
    }

    // Read raw bytes
    [[nodiscard]] auto read_bytes(size_t count) -> std::vector<uint8_t>
    {
        check_remaining(count);
        auto begin = data_.begin() + static_cast<std::ptrdiff_t>(position_);
        auto end = begin + static_cast<std::ptrdiff_t>(count);
        std::vector<uint8_t> result(begin, end);
        position_ += count;
        return result;
    }

    // Skip bytes
    void skip(size_t count)
    {
        check_remaining(count);
        position_ += count;
    }

    // State
    [[nodiscard]] auto remaining() const -> size_t { return data_.size() - position_; }

    [[nodiscard]] auto position() const -> size_t { return position_; }

    [[nodiscard]] auto size() const -> size_t { return data_.size(); }

    [[nodiscard]] auto is_eof() const -> bool { return position_ >= data_.size(); }

    void seek(size_t pos)
    {
        if (pos > data_.size())
        {
            throw std::out_of_range("Seek position out of bounds");
        }
        position_ = pos;
    }

private:
    void check_remaining(size_t needed) const
    {
        if (position_ + needed > data_.size())
        {
            throw std::out_of_range("Not enough data in buffer");
        }
    }

    std::span<const uint8_t> data_;
    size_t position_;
};

// Receive buffer that accumulates data until complete messages are available
class receive_buffer
{
public:
    static constexpr size_t default_capacity = 8192;
    static constexpr size_t max_message_size = 65536;

    explicit receive_buffer(size_t capacity = default_capacity) { buffer_.reserve(capacity); }

    // Append received data
    void append(std::span<const uint8_t> data) { buffer_.insert(buffer_.end(), data.begin(), data.end()); }

    // Check if we have a complete message (assumes 2-byte length prefix)
    [[nodiscard]] auto has_complete_message() const -> bool
    {
        if (buffer_.size() < 2)
            return false;
        uint16_t length = static_cast<uint16_t>(buffer_[0]) | (static_cast<uint16_t>(buffer_[1]) << 8);
        return buffer_.size() >= static_cast<size_t>(length) + 2;
    }

    // Extract a complete message (removes it from buffer)
    [[nodiscard]] auto extract_message() -> std::vector<uint8_t>
    {
        if (!has_complete_message())
        {
            return {};
        }

        uint16_t length = static_cast<uint16_t>(buffer_[0]) | (static_cast<uint16_t>(buffer_[1]) << 8);

        // Skip 2-byte header, copy message body
        std::vector<uint8_t> message(buffer_.begin() + 2, buffer_.begin() + 2 + length);

        // Remove from buffer
        buffer_.erase(buffer_.begin(), buffer_.begin() + 2 + length);

        return message;
    }

    [[nodiscard]] auto size() const -> size_t { return buffer_.size(); }

    void clear() { buffer_.clear(); }

private:
    std::vector<uint8_t> buffer_;
};

} // namespace hb::network
