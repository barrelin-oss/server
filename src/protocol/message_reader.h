#pragma once

// message_reader.h
// Binary message deserialization

#include <cstdint>
#include <cstring>
#include <span>
#include <string>
#include <string_view>
#include <optional>
#include <stdexcept>

namespace hb::protocol
{

// Exception for read errors
class read_error : public std::runtime_error
{
public:
    using std::runtime_error::runtime_error;
};

// Binary message reader with bounds checking
class message_reader
{
public:
    // Construct from data span
    explicit message_reader(std::span<const uint8_t> data) : data_(data), pos_(0) {}

    // Construct from raw pointer and size
    message_reader(const uint8_t* data, size_t size) : data_(data, size), pos_(0) {}

    // Check if there are at least n bytes remaining
    [[nodiscard]] auto has_remaining(size_t n) const -> bool { return pos_ + n <= data_.size(); }

    // Get remaining bytes count
    [[nodiscard]] auto remaining() const -> size_t { return data_.size() - pos_; }

    // Get current position
    [[nodiscard]] auto position() const -> size_t { return pos_; }

    // Get total size
    [[nodiscard]] auto size() const -> size_t { return data_.size(); }

    // Check if at end
    [[nodiscard]] auto at_end() const -> bool { return pos_ >= data_.size(); }

    // Seek to position
    void seek(size_t pos)
    {
        if (pos > data_.size())
        {
            throw read_error("seek position out of bounds");
        }
        pos_ = pos;
    }

    // Skip n bytes
    void skip(size_t n)
    {
        if (!has_remaining(n))
        {
            throw read_error("skip: not enough data");
        }
        pos_ += n;
    }

    // Read unsigned 8-bit integer
    [[nodiscard]] auto read_u8() -> uint8_t
    {
        if (!has_remaining(1))
        {
            throw read_error("read_u8: not enough data");
        }
        return data_[pos_++];
    }

    // Read signed 8-bit integer
    [[nodiscard]] auto read_i8() -> int8_t { return static_cast<int8_t>(read_u8()); }

    // Read unsigned 16-bit integer (little-endian)
    [[nodiscard]] auto read_u16() -> uint16_t
    {
        if (!has_remaining(2))
        {
            throw read_error("read_u16: not enough data");
        }
        uint16_t value = static_cast<uint16_t>(data_[pos_]) | (static_cast<uint16_t>(data_[pos_ + 1]) << 8);
        pos_ += 2;
        return value;
    }

    // Read signed 16-bit integer (little-endian)
    [[nodiscard]] auto read_i16() -> int16_t { return static_cast<int16_t>(read_u16()); }

    // Read unsigned 32-bit integer (little-endian)
    [[nodiscard]] auto read_u32() -> uint32_t
    {
        if (!has_remaining(4))
        {
            throw read_error("read_u32: not enough data");
        }
        uint32_t value = static_cast<uint32_t>(data_[pos_]) | (static_cast<uint32_t>(data_[pos_ + 1]) << 8) |
                         (static_cast<uint32_t>(data_[pos_ + 2]) << 16) |
                         (static_cast<uint32_t>(data_[pos_ + 3]) << 24);
        pos_ += 4;
        return value;
    }

    // Read signed 32-bit integer (little-endian)
    [[nodiscard]] auto read_i32() -> int32_t { return static_cast<int32_t>(read_u32()); }

    // Read unsigned 64-bit integer (little-endian)
    [[nodiscard]] auto read_u64() -> uint64_t
    {
        if (!has_remaining(8))
        {
            throw read_error("read_u64: not enough data");
        }
        uint64_t value =
            static_cast<uint64_t>(data_[pos_]) | (static_cast<uint64_t>(data_[pos_ + 1]) << 8) |
            (static_cast<uint64_t>(data_[pos_ + 2]) << 16) | (static_cast<uint64_t>(data_[pos_ + 3]) << 24) |
            (static_cast<uint64_t>(data_[pos_ + 4]) << 32) | (static_cast<uint64_t>(data_[pos_ + 5]) << 40) |
            (static_cast<uint64_t>(data_[pos_ + 6]) << 48) | (static_cast<uint64_t>(data_[pos_ + 7]) << 56);
        pos_ += 8;
        return value;
    }

    // Read signed 64-bit integer (little-endian)
    [[nodiscard]] auto read_i64() -> int64_t { return static_cast<int64_t>(read_u64()); }

    // Read boolean (1 byte, 0 = false, non-zero = true)
    [[nodiscard]] auto read_bool() -> bool { return read_u8() != 0; }

    // Read float (4 bytes, IEEE 754)
    [[nodiscard]] auto read_f32() -> float
    {
        uint32_t bits = read_u32();
        float value;
        std::memcpy(&value, &bits, sizeof(float));
        return value;
    }

    // Read double (8 bytes, IEEE 754)
    [[nodiscard]] auto read_f64() -> double
    {
        uint64_t bits = read_u64();
        double value;
        std::memcpy(&value, &bits, sizeof(double));
        return value;
    }

    // Read fixed-length string (null-padded, reads exactly n bytes)
    [[nodiscard]] auto read_fixed_string(size_t n) -> std::string
    {
        if (!has_remaining(n))
        {
            throw read_error("read_fixed_string: not enough data");
        }

        // Find null terminator or use full length
        size_t len = 0;
        while (len < n && data_[pos_ + len] != 0)
        {
            ++len;
        }

        std::string result(reinterpret_cast<const char*>(data_.data() + pos_), len);
        pos_ += n; // Always consume exactly n bytes
        return result;
    }

    // Read length-prefixed string (1-byte length prefix)
    [[nodiscard]] auto read_string_u8() -> std::string
    {
        uint8_t len = read_u8();
        if (!has_remaining(len))
        {
            throw read_error("read_string_u8: not enough data");
        }
        std::string result(reinterpret_cast<const char*>(data_.data() + pos_), len);
        pos_ += len;
        return result;
    }

    // Read length-prefixed string (2-byte length prefix)
    [[nodiscard]] auto read_string_u16() -> std::string
    {
        uint16_t len = read_u16();
        if (!has_remaining(len))
        {
            throw read_error("read_string_u16: not enough data");
        }
        std::string result(reinterpret_cast<const char*>(data_.data() + pos_), len);
        pos_ += len;
        return result;
    }

    // Read raw bytes
    [[nodiscard]] auto read_bytes(size_t n) -> std::span<const uint8_t>
    {
        if (!has_remaining(n))
        {
            throw read_error("read_bytes: not enough data");
        }
        auto result = data_.subspan(pos_, n);
        pos_ += n;
        return result;
    }

    // Read raw bytes into buffer
    void read_bytes_into(std::span<uint8_t> buffer)
    {
        if (!has_remaining(buffer.size()))
        {
            throw read_error("read_bytes_into: not enough data");
        }
        std::memcpy(buffer.data(), data_.data() + pos_, buffer.size());
        pos_ += buffer.size();
    }

    // Get remaining data as span
    [[nodiscard]] auto remaining_data() const -> std::span<const uint8_t> { return data_.subspan(pos_); }

    // Peek at unsigned 8-bit integer without advancing
    [[nodiscard]] auto peek_u8() const -> std::optional<uint8_t>
    {
        if (!has_remaining(1))
        {
            return std::nullopt;
        }
        return data_[pos_];
    }

    // Peek at unsigned 16-bit integer without advancing
    [[nodiscard]] auto peek_u16() const -> std::optional<uint16_t>
    {
        if (!has_remaining(2))
        {
            return std::nullopt;
        }
        return static_cast<uint16_t>(data_[pos_]) | (static_cast<uint16_t>(data_[pos_ + 1]) << 8);
    }

    // Peek at unsigned 32-bit integer without advancing
    [[nodiscard]] auto peek_u32() const -> std::optional<uint32_t>
    {
        if (!has_remaining(4))
        {
            return std::nullopt;
        }
        return static_cast<uint32_t>(data_[pos_]) | (static_cast<uint32_t>(data_[pos_ + 1]) << 8) |
               (static_cast<uint32_t>(data_[pos_ + 2]) << 16) | (static_cast<uint32_t>(data_[pos_ + 3]) << 24);
    }

    // Try to read - returns nullopt if not enough data
    [[nodiscard]] auto try_read_u8() -> std::optional<uint8_t>
    {
        if (!has_remaining(1))
            return std::nullopt;
        return read_u8();
    }

    [[nodiscard]] auto try_read_u16() -> std::optional<uint16_t>
    {
        if (!has_remaining(2))
            return std::nullopt;
        return read_u16();
    }

    [[nodiscard]] auto try_read_u32() -> std::optional<uint32_t>
    {
        if (!has_remaining(4))
            return std::nullopt;
        return read_u32();
    }

private:
    std::span<const uint8_t> data_;
    size_t pos_;
};

} // namespace hb::protocol
