#pragma once

// message_writer.h
// Binary message serialization

#include <cstdint>
#include <cstring>
#include <span>
#include <string>
#include <string_view>
#include <vector>
#include <stdexcept>

namespace hb::protocol
{

// Exception for write errors
class write_error : public std::runtime_error
{
public:
    using std::runtime_error::runtime_error;
};

// Binary message writer with automatic buffer growth
class message_writer
{
public:
    // Construct with initial capacity
    explicit message_writer(size_t initial_capacity = 256) { buffer_.reserve(initial_capacity); }

    // Get current size
    [[nodiscard]] auto size() const -> size_t { return buffer_.size(); }

    // Get current capacity
    [[nodiscard]] auto capacity() const -> size_t { return buffer_.capacity(); }

    // Check if empty
    [[nodiscard]] auto empty() const -> bool { return buffer_.empty(); }

    // Get data as span
    [[nodiscard]] auto data() const -> std::span<const uint8_t> { return buffer_; }

    // Get raw data pointer
    [[nodiscard]] auto data_ptr() const -> const uint8_t* { return buffer_.data(); }

    // Clear the buffer
    void clear() { buffer_.clear(); }

    // Reset and reserve capacity
    void reset(size_t capacity = 256)
    {
        buffer_.clear();
        buffer_.reserve(capacity);
    }

    // Write unsigned 8-bit integer
    void write_u8(uint8_t value) { buffer_.push_back(value); }

    // Write signed 8-bit integer
    void write_i8(int8_t value) { write_u8(static_cast<uint8_t>(value)); }

    // Write unsigned 16-bit integer (little-endian)
    void write_u16(uint16_t value)
    {
        buffer_.push_back(static_cast<uint8_t>(value & 0xFF));
        buffer_.push_back(static_cast<uint8_t>((value >> 8) & 0xFF));
    }

    // Write signed 16-bit integer (little-endian)
    void write_i16(int16_t value) { write_u16(static_cast<uint16_t>(value)); }

    // Write unsigned 32-bit integer (little-endian)
    void write_u32(uint32_t value)
    {
        buffer_.push_back(static_cast<uint8_t>(value & 0xFF));
        buffer_.push_back(static_cast<uint8_t>((value >> 8) & 0xFF));
        buffer_.push_back(static_cast<uint8_t>((value >> 16) & 0xFF));
        buffer_.push_back(static_cast<uint8_t>((value >> 24) & 0xFF));
    }

    // Write signed 32-bit integer (little-endian)
    void write_i32(int32_t value) { write_u32(static_cast<uint32_t>(value)); }

    // Write unsigned 64-bit integer (little-endian)
    void write_u64(uint64_t value)
    {
        buffer_.push_back(static_cast<uint8_t>(value & 0xFF));
        buffer_.push_back(static_cast<uint8_t>((value >> 8) & 0xFF));
        buffer_.push_back(static_cast<uint8_t>((value >> 16) & 0xFF));
        buffer_.push_back(static_cast<uint8_t>((value >> 24) & 0xFF));
        buffer_.push_back(static_cast<uint8_t>((value >> 32) & 0xFF));
        buffer_.push_back(static_cast<uint8_t>((value >> 40) & 0xFF));
        buffer_.push_back(static_cast<uint8_t>((value >> 48) & 0xFF));
        buffer_.push_back(static_cast<uint8_t>((value >> 56) & 0xFF));
    }

    // Write signed 64-bit integer (little-endian)
    void write_i64(int64_t value) { write_u64(static_cast<uint64_t>(value)); }

    // Write boolean (1 byte)
    void write_bool(bool value) { write_u8(value ? 1 : 0); }

    // Write float (4 bytes, IEEE 754)
    void write_f32(float value)
    {
        uint32_t bits;
        std::memcpy(&bits, &value, sizeof(float));
        write_u32(bits);
    }

    // Write double (8 bytes, IEEE 754)
    void write_f64(double value)
    {
        uint64_t bits;
        std::memcpy(&bits, &value, sizeof(double));
        write_u64(bits);
    }

    // Write fixed-length string (null-padded to n bytes)
    void write_fixed_string(std::string_view str, size_t n)
    {
        size_t copy_len = std::min(str.size(), n);
        buffer_.insert(buffer_.end(), str.begin(), str.begin() + copy_len);
        // Pad with zeros
        for (size_t i = copy_len; i < n; ++i)
        {
            buffer_.push_back(0);
        }
    }

    // Write length-prefixed string (1-byte length prefix)
    void write_string_u8(std::string_view str)
    {
        if (str.size() > 255)
        {
            throw write_error("write_string_u8: string too long (max 255 bytes)");
        }
        write_u8(static_cast<uint8_t>(str.size()));
        buffer_.insert(buffer_.end(), str.begin(), str.end());
    }

    // Write length-prefixed string (2-byte length prefix)
    void write_string_u16(std::string_view str)
    {
        if (str.size() > 65535)
        {
            throw write_error("write_string_u16: string too long (max 65535 bytes)");
        }
        write_u16(static_cast<uint16_t>(str.size()));
        buffer_.insert(buffer_.end(), str.begin(), str.end());
    }

    // Write raw string without length prefix
    void write_string_raw(std::string_view str) { buffer_.insert(buffer_.end(), str.begin(), str.end()); }

    // Write raw bytes
    void write_bytes(std::span<const uint8_t> bytes) { buffer_.insert(buffer_.end(), bytes.begin(), bytes.end()); }

    // Write zeros
    void write_zeros(size_t n) { buffer_.insert(buffer_.end(), n, 0); }

    // Reserve at least n more bytes
    void reserve(size_t n) { buffer_.reserve(buffer_.size() + n); }

    // Write at specific offset (for patching headers)
    void write_u8_at(size_t offset, uint8_t value)
    {
        if (offset >= buffer_.size())
        {
            throw write_error("write_u8_at: offset out of bounds");
        }
        buffer_[offset] = value;
    }

    void write_u16_at(size_t offset, uint16_t value)
    {
        if (offset + 1 >= buffer_.size())
        {
            throw write_error("write_u16_at: offset out of bounds");
        }
        buffer_[offset] = static_cast<uint8_t>(value & 0xFF);
        buffer_[offset + 1] = static_cast<uint8_t>((value >> 8) & 0xFF);
    }

    void write_u32_at(size_t offset, uint32_t value)
    {
        if (offset + 3 >= buffer_.size())
        {
            throw write_error("write_u32_at: offset out of bounds");
        }
        buffer_[offset] = static_cast<uint8_t>(value & 0xFF);
        buffer_[offset + 1] = static_cast<uint8_t>((value >> 8) & 0xFF);
        buffer_[offset + 2] = static_cast<uint8_t>((value >> 16) & 0xFF);
        buffer_[offset + 3] = static_cast<uint8_t>((value >> 24) & 0xFF);
    }

    // Move buffer out (transfers ownership)
    [[nodiscard]] auto release() -> std::vector<uint8_t> { return std::move(buffer_); }

    // Copy buffer
    [[nodiscard]] auto copy() const -> std::vector<uint8_t> { return buffer_; }

private:
    std::vector<uint8_t> buffer_;
};

} // namespace hb::protocol
