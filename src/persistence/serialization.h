#pragma once

// serialization.h
// Binary serialization utilities

#include "core/types.h"
#include "core/result.h"

#include <vector>
#include <string>
#include <cstdint>
#include <span>
#include <type_traits>
#include <cstring>

namespace hb::persistence
{

// Serialization error types
enum class serialize_error : uint8_t
{
    success = 0,
    buffer_overflow = 1,
    buffer_underflow = 2,
    invalid_data = 3,
    type_mismatch = 4,
};

// Binary writer for serializing data
class binary_writer
{
public:
    explicit binary_writer(size_t initial_capacity = 1024) { buffer_.reserve(initial_capacity); }

    // Write primitive types
    void write_uint8(uint8_t value) { buffer_.push_back(value); }

    void write_uint16(uint16_t value)
    {
        buffer_.push_back(static_cast<uint8_t>(value & 0xFF));
        buffer_.push_back(static_cast<uint8_t>((value >> 8) & 0xFF));
    }

    void write_uint32(uint32_t value)
    {
        buffer_.push_back(static_cast<uint8_t>(value & 0xFF));
        buffer_.push_back(static_cast<uint8_t>((value >> 8) & 0xFF));
        buffer_.push_back(static_cast<uint8_t>((value >> 16) & 0xFF));
        buffer_.push_back(static_cast<uint8_t>((value >> 24) & 0xFF));
    }

    void write_uint64(uint64_t value)
    {
        write_uint32(static_cast<uint32_t>(value & 0xFFFFFFFF));
        write_uint32(static_cast<uint32_t>((value >> 32) & 0xFFFFFFFF));
    }

    void write_int8(int8_t value) { write_uint8(static_cast<uint8_t>(value)); }

    void write_int16(int16_t value) { write_uint16(static_cast<uint16_t>(value)); }

    void write_int32(int32_t value) { write_uint32(static_cast<uint32_t>(value)); }

    void write_int64(int64_t value) { write_uint64(static_cast<uint64_t>(value)); }

    void write_float(float value)
    {
        uint32_t bits;
        std::memcpy(&bits, &value, sizeof(float));
        write_uint32(bits);
    }

    void write_double(double value)
    {
        uint64_t bits;
        std::memcpy(&bits, &value, sizeof(double));
        write_uint64(bits);
    }

    void write_bool(bool value) { write_uint8(value ? 1 : 0); }

    // Write string (length-prefixed)
    void write_string(const std::string& value)
    {
        write_uint32(static_cast<uint32_t>(value.size()));
        buffer_.insert(buffer_.end(), value.begin(), value.end());
    }

    // Write fixed-size string (no length prefix, null-padded)
    void write_fixed_string(const std::string& value, size_t size)
    {
        size_t copy_size = std::min(value.size(), size);
        buffer_.insert(buffer_.end(), value.begin(), value.begin() + copy_size);
        // Pad with zeros
        for (size_t i = copy_size; i < size; ++i)
        {
            buffer_.push_back(0);
        }
    }

    // Write raw bytes
    void write_bytes(const uint8_t* data, size_t size) { buffer_.insert(buffer_.end(), data, data + size); }

    void write_bytes(std::span<const uint8_t> data) { buffer_.insert(buffer_.end(), data.begin(), data.end()); }

    // Get buffer
    [[nodiscard]] auto data() const -> const std::vector<uint8_t>& { return buffer_; }

    [[nodiscard]] auto size() const -> size_t { return buffer_.size(); }

    void clear() { buffer_.clear(); }

    // Move buffer out
    [[nodiscard]] auto release() -> std::vector<uint8_t> { return std::move(buffer_); }

private:
    std::vector<uint8_t> buffer_;
};

// Binary reader for deserializing data
class binary_reader
{
public:
    explicit binary_reader(std::span<const uint8_t> data) : data_(data), position_(0) {}

    explicit binary_reader(const std::vector<uint8_t>& data) : data_(data), position_(0) {}

    // Read primitive types
    auto read_uint8() -> result<uint8_t, serialize_error>
    {
        if (position_ >= data_.size())
        {
            return result<uint8_t, serialize_error>::err(serialize_error::buffer_underflow);
        }
        return result<uint8_t, serialize_error>::ok(data_[position_++]);
    }

    auto read_uint16() -> result<uint16_t, serialize_error>
    {
        if (position_ + 2 > data_.size())
        {
            return result<uint16_t, serialize_error>::err(serialize_error::buffer_underflow);
        }
        uint16_t value = static_cast<uint16_t>(data_[position_]) | (static_cast<uint16_t>(data_[position_ + 1]) << 8);
        position_ += 2;
        return result<uint16_t, serialize_error>::ok(value);
    }

    auto read_uint32() -> result<uint32_t, serialize_error>
    {
        if (position_ + 4 > data_.size())
        {
            return result<uint32_t, serialize_error>::err(serialize_error::buffer_underflow);
        }
        uint32_t value = static_cast<uint32_t>(data_[position_]) | (static_cast<uint32_t>(data_[position_ + 1]) << 8) |
                         (static_cast<uint32_t>(data_[position_ + 2]) << 16) |
                         (static_cast<uint32_t>(data_[position_ + 3]) << 24);
        position_ += 4;
        return result<uint32_t, serialize_error>::ok(value);
    }

    auto read_uint64() -> result<uint64_t, serialize_error>
    {
        auto low = read_uint32();
        if (!low.is_ok())
            return result<uint64_t, serialize_error>::err(low.error());

        auto high = read_uint32();
        if (!high.is_ok())
            return result<uint64_t, serialize_error>::err(high.error());

        return result<uint64_t, serialize_error>::ok(static_cast<uint64_t>(low.value()) |
                                                     (static_cast<uint64_t>(high.value()) << 32));
    }

    auto read_int8() -> result<int8_t, serialize_error>
    {
        auto res = read_uint8();
        if (!res.is_ok())
            return result<int8_t, serialize_error>::err(res.error());
        return result<int8_t, serialize_error>::ok(static_cast<int8_t>(res.value()));
    }

    auto read_int16() -> result<int16_t, serialize_error>
    {
        auto res = read_uint16();
        if (!res.is_ok())
            return result<int16_t, serialize_error>::err(res.error());
        return result<int16_t, serialize_error>::ok(static_cast<int16_t>(res.value()));
    }

    auto read_int32() -> result<int32_t, serialize_error>
    {
        auto res = read_uint32();
        if (!res.is_ok())
            return result<int32_t, serialize_error>::err(res.error());
        return result<int32_t, serialize_error>::ok(static_cast<int32_t>(res.value()));
    }

    auto read_int64() -> result<int64_t, serialize_error>
    {
        auto res = read_uint64();
        if (!res.is_ok())
            return result<int64_t, serialize_error>::err(res.error());
        return result<int64_t, serialize_error>::ok(static_cast<int64_t>(res.value()));
    }

    auto read_float() -> result<float, serialize_error>
    {
        auto bits = read_uint32();
        if (!bits.is_ok())
            return result<float, serialize_error>::err(bits.error());
        float value;
        uint32_t raw = bits.value();
        std::memcpy(&value, &raw, sizeof(float));
        return result<float, serialize_error>::ok(value);
    }

    auto read_double() -> result<double, serialize_error>
    {
        auto bits = read_uint64();
        if (!bits.is_ok())
            return result<double, serialize_error>::err(bits.error());
        double value;
        uint64_t raw = bits.value();
        std::memcpy(&value, &raw, sizeof(double));
        return result<double, serialize_error>::ok(value);
    }

    auto read_bool() -> result<bool, serialize_error>
    {
        auto res = read_uint8();
        if (!res.is_ok())
            return result<bool, serialize_error>::err(res.error());
        return result<bool, serialize_error>::ok(res.value() != 0);
    }

    // Read length-prefixed string
    auto read_string() -> result<std::string, serialize_error>
    {
        auto length_res = read_uint32();
        if (!length_res.is_ok())
            return result<std::string, serialize_error>::err(length_res.error());

        size_t length = length_res.value();
        if (position_ + length > data_.size())
        {
            return result<std::string, serialize_error>::err(serialize_error::buffer_underflow);
        }

        std::string value(reinterpret_cast<const char*>(data_.data() + position_), length);
        position_ += length;
        return result<std::string, serialize_error>::ok(std::move(value));
    }

    // Read fixed-size string
    auto read_fixed_string(size_t size) -> result<std::string, serialize_error>
    {
        if (position_ + size > data_.size())
        {
            return result<std::string, serialize_error>::err(serialize_error::buffer_underflow);
        }

        // Find null terminator or use full size
        size_t actual_len = 0;
        while (actual_len < size && data_[position_ + actual_len] != 0)
        {
            ++actual_len;
        }

        std::string value(reinterpret_cast<const char*>(data_.data() + position_), actual_len);
        position_ += size;
        return result<std::string, serialize_error>::ok(std::move(value));
    }

    // Read raw bytes
    auto read_bytes(size_t size) -> result<std::vector<uint8_t>, serialize_error>
    {
        if (position_ + size > data_.size())
        {
            return result<std::vector<uint8_t>, serialize_error>::err(serialize_error::buffer_underflow);
        }

        std::vector<uint8_t> value(data_.begin() + position_, data_.begin() + position_ + size);
        position_ += size;
        return result<std::vector<uint8_t>, serialize_error>::ok(std::move(value));
    }

    // Position management
    [[nodiscard]] auto position() const -> size_t { return position_; }
    [[nodiscard]] auto remaining() const -> size_t { return data_.size() - position_; }
    [[nodiscard]] auto at_end() const -> bool { return position_ >= data_.size(); }

    void seek(size_t pos) { position_ = std::min(pos, data_.size()); }

    void skip(size_t bytes) { position_ = std::min(position_ + bytes, data_.size()); }

private:
    std::span<const uint8_t> data_;
    size_t position_;
};

} // namespace hb::persistence
