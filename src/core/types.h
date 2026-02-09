#pragma once

// types.h
// Core type definitions for Helbreath server
// Using stdlib-style snake_case naming convention

#include <cstdint>
#include <chrono>
#include <compare>
#include <string>
#include <string_view>
#include <optional>
#include <functional>

namespace hb {

// Strong ID types - prevent mixing up different ID types
// These can implicitly convert from raw integers for legacy compatibility

struct client_id {
    uint16_t value{0};

    constexpr client_id() = default;
    constexpr explicit client_id(uint16_t v) : value(v) {}
    constexpr client_id(int v) : value(static_cast<uint16_t>(v)) {}  // Legacy compat

    constexpr auto operator<=>(const client_id&) const = default;
    constexpr explicit operator uint16_t() const { return value; }
    constexpr explicit operator int() const { return value; }

    [[nodiscard]] constexpr auto is_valid() const -> bool { return value != 0; }
};

struct npc_id {
    uint16_t value{0};

    constexpr npc_id() = default;
    constexpr explicit npc_id(uint16_t v) : value(v) {}
    constexpr npc_id(int v) : value(static_cast<uint16_t>(v)) {}

    constexpr auto operator<=>(const npc_id&) const = default;
    constexpr explicit operator uint16_t() const { return value; }
    constexpr explicit operator int() const { return value; }

    [[nodiscard]] constexpr auto is_valid() const -> bool { return value != 0; }
};

struct map_id {
    uint8_t value{0};

    constexpr map_id() = default;
    constexpr explicit map_id(uint8_t v) : value(v) {}
    constexpr map_id(int v) : value(static_cast<uint8_t>(v)) {}

    constexpr auto operator<=>(const map_id&) const = default;
    constexpr explicit operator uint8_t() const { return value; }
    constexpr explicit operator int() const { return value; }

    [[nodiscard]] constexpr auto is_valid() const -> bool { return value != 0; }
};

struct item_id {
    uint32_t value{0};

    constexpr item_id() = default;
    constexpr explicit item_id(uint32_t v) : value(v) {}
    constexpr item_id(int v) : value(static_cast<uint32_t>(v)) {}

    constexpr auto operator<=>(const item_id&) const = default;
    constexpr explicit operator uint32_t() const { return value; }
    constexpr explicit operator int() const { return static_cast<int>(value); }

    [[nodiscard]] constexpr auto is_valid() const -> bool { return value != 0; }
};

struct guild_id {
    uint16_t value{0};

    constexpr guild_id() = default;
    constexpr explicit guild_id(uint16_t v) : value(v) {}
    constexpr guild_id(int v) : value(static_cast<uint16_t>(v)) {}

    constexpr auto operator<=>(const guild_id&) const = default;
    constexpr explicit operator uint16_t() const { return value; }
    constexpr explicit operator int() const { return value; }

    [[nodiscard]] constexpr auto is_valid() const -> bool { return value != 0; }
};

struct quest_id {
    uint16_t value{0};

    constexpr quest_id() = default;
    constexpr explicit quest_id(uint16_t v) : value(v) {}
    constexpr quest_id(int v) : value(static_cast<uint16_t>(v)) {}

    constexpr auto operator<=>(const quest_id&) const = default;
    constexpr explicit operator uint16_t() const { return value; }
    constexpr explicit operator int() const { return value; }

    [[nodiscard]] constexpr auto is_valid() const -> bool { return value != 0; }
};

struct spell_id {
    uint16_t value{0};

    constexpr spell_id() = default;
    constexpr explicit spell_id(uint16_t v) : value(v) {}
    constexpr spell_id(int v) : value(static_cast<uint16_t>(v)) {}

    constexpr auto operator<=>(const spell_id&) const = default;
    constexpr explicit operator uint16_t() const { return value; }
    constexpr explicit operator int() const { return value; }
};

struct recipe_id {
    uint16_t value{0};

    constexpr recipe_id() = default;
    constexpr explicit recipe_id(uint16_t v) : value(v) {}
    constexpr recipe_id(int v) : value(static_cast<uint16_t>(v)) {}

    constexpr auto operator<=>(const recipe_id&) const = default;
    constexpr explicit operator uint16_t() const { return value; }
    constexpr explicit operator int() const { return value; }

    [[nodiscard]] constexpr auto is_valid() const -> bool { return value != 0; }
};

struct skill_id {
    uint16_t value{0};

    constexpr skill_id() = default;
    constexpr explicit skill_id(uint16_t v) : value(v) {}
    constexpr skill_id(int v) : value(static_cast<uint16_t>(v)) {}

    constexpr auto operator<=>(const skill_id&) const = default;
    constexpr explicit operator uint16_t() const { return value; }
    constexpr explicit operator int() const { return value; }

    [[nodiscard]] constexpr auto is_valid() const -> bool { return value != 0; }
};

struct connection_id {
    uint32_t value{0};

    constexpr connection_id() = default;
    constexpr explicit connection_id(uint32_t v) : value(v) {}
    constexpr connection_id(int v) : value(static_cast<uint32_t>(v)) {}

    constexpr auto operator<=>(const connection_id&) const = default;
    constexpr explicit operator uint32_t() const { return value; }
    constexpr explicit operator int() const { return static_cast<int>(value); }

    [[nodiscard]] constexpr auto is_valid() const -> bool { return value != 0; }
};

struct session_id {
    uint32_t value{0};

    constexpr session_id() = default;
    constexpr explicit session_id(uint32_t v) : value(v) {}
    constexpr session_id(int v) : value(static_cast<uint32_t>(v)) {}

    constexpr auto operator<=>(const session_id&) const = default;
    constexpr explicit operator uint32_t() const { return value; }
    constexpr explicit operator int() const { return static_cast<int>(value); }

    [[nodiscard]] constexpr auto is_valid() const -> bool { return value != 0; }
};

struct player_id {
    uint32_t value{0};

    constexpr player_id() = default;
    constexpr explicit player_id(uint32_t v) : value(v) {}
    constexpr player_id(int v) : value(static_cast<uint32_t>(v)) {}

    constexpr auto operator<=>(const player_id&) const = default;
    constexpr explicit operator uint32_t() const { return value; }
    constexpr explicit operator int() const { return static_cast<int>(value); }

    [[nodiscard]] constexpr auto is_valid() const -> bool { return value != 0; }
};

struct entity_id {
    uint32_t value{0};

    constexpr entity_id() = default;
    constexpr explicit entity_id(uint32_t v) : value(v) {}
    constexpr entity_id(int v) : value(static_cast<uint32_t>(v)) {}

    constexpr auto operator<=>(const entity_id&) const = default;
    constexpr explicit operator uint32_t() const { return value; }
    constexpr explicit operator int() const { return static_cast<int>(value); }

    [[nodiscard]] constexpr auto is_valid() const -> bool { return value != 0; }
};

struct account_id {
    uint32_t value{0};

    constexpr account_id() = default;
    constexpr explicit account_id(uint32_t v) : value(v) {}
    constexpr account_id(int v) : value(static_cast<uint32_t>(v)) {}

    constexpr auto operator<=>(const account_id&) const = default;
    constexpr explicit operator uint32_t() const { return value; }
    constexpr explicit operator int() const { return static_cast<int>(value); }

    [[nodiscard]] constexpr auto is_valid() const -> bool { return value != 0; }
};

// Spatial types

struct position {
    int16_t x{0};
    int16_t y{0};

    constexpr position() = default;
    constexpr position(int16_t x_, int16_t y_) : x(x_), y(y_) {}
    constexpr position(int x_, int y_) : x(static_cast<int16_t>(x_)), y(static_cast<int16_t>(y_)) {}

    constexpr auto operator<=>(const position&) const = default;

    [[nodiscard]] constexpr auto distance_squared(const position& other) const -> int {
        int dx = x - other.x;
        int dy = y - other.y;
        return dx * dx + dy * dy;
    }

    [[nodiscard]] constexpr auto manhattan_distance(const position& other) const -> int {
        int dx = x > other.x ? x - other.x : other.x - x;
        int dy = y > other.y ? y - other.y : other.y - y;
        return dx + dy;
    }
};

struct rect {
    int16_t left{0};
    int16_t top{0};
    int16_t right{0};
    int16_t bottom{0};

    constexpr rect() = default;
    constexpr rect(int16_t l, int16_t t, int16_t r, int16_t b)
        : left(l), top(t), right(r), bottom(b) {}

    [[nodiscard]] constexpr auto contains(const position& p) const -> bool {
        return p.x >= left && p.x <= right && p.y >= top && p.y <= bottom;
    }

    [[nodiscard]] constexpr auto width() const -> int16_t { return right - left; }
    [[nodiscard]] constexpr auto height() const -> int16_t { return bottom - top; }
};

// Subscription ID for event bus
struct subscription_id {
    uint64_t value{0};

    constexpr subscription_id() = default;
    constexpr explicit subscription_id(uint64_t v) : value(v) {}

    constexpr auto operator<=>(const subscription_id&) const = default;
    [[nodiscard]] constexpr auto is_valid() const -> bool { return value != 0; }
};

// Time types
using duration_ms = std::chrono::milliseconds;
using time_point = std::chrono::steady_clock::time_point;

}  // namespace hb

// Hash specializations for use in unordered containers
namespace std {

template<>
struct hash<hb::client_id> {
    auto operator()(const hb::client_id& id) const noexcept -> size_t {
        return hash<uint16_t>{}(id.value);
    }
};

template<>
struct hash<hb::npc_id> {
    auto operator()(const hb::npc_id& id) const noexcept -> size_t {
        return hash<uint16_t>{}(id.value);
    }
};

template<>
struct hash<hb::map_id> {
    auto operator()(const hb::map_id& id) const noexcept -> size_t {
        return hash<uint8_t>{}(id.value);
    }
};

template<>
struct hash<hb::item_id> {
    auto operator()(const hb::item_id& id) const noexcept -> size_t {
        return hash<uint32_t>{}(id.value);
    }
};

template<>
struct hash<hb::position> {
    auto operator()(const hb::position& p) const noexcept -> size_t {
        return hash<uint32_t>{}(static_cast<uint32_t>(p.x) << 16 | static_cast<uint32_t>(p.y));
    }
};

template<>
struct hash<hb::connection_id> {
    auto operator()(const hb::connection_id& id) const noexcept -> size_t {
        return hash<uint32_t>{}(id.value);
    }
};

template<>
struct hash<hb::session_id> {
    auto operator()(const hb::session_id& id) const noexcept -> size_t {
        return hash<uint32_t>{}(id.value);
    }
};

template<>
struct hash<hb::player_id> {
    auto operator()(const hb::player_id& id) const noexcept -> size_t {
        return hash<uint32_t>{}(id.value);
    }
};

template<>
struct hash<hb::entity_id> {
    auto operator()(const hb::entity_id& id) const noexcept -> size_t {
        return hash<uint32_t>{}(id.value);
    }
};

template<>
struct hash<hb::spell_id> {
    auto operator()(const hb::spell_id& id) const noexcept -> size_t {
        return hash<uint16_t>{}(id.value);
    }
};

template<>
struct hash<hb::quest_id> {
    auto operator()(const hb::quest_id& id) const noexcept -> size_t {
        return hash<uint16_t>{}(id.value);
    }
};

template<>
struct hash<hb::guild_id> {
    auto operator()(const hb::guild_id& id) const noexcept -> size_t {
        return hash<uint16_t>{}(id.value);
    }
};

template<>
struct hash<hb::account_id> {
    auto operator()(const hb::account_id& id) const noexcept -> size_t {
        return hash<uint32_t>{}(id.value);
    }
};

}  // namespace std
