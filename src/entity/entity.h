#pragma once

// entity.h
// Entity ID with generation for safe handles

#include <cstdint>
#include <functional>

namespace hb::entity {

// Entity identifier with generation counter
// Upper 8 bits: generation (for detecting stale references)
// Lower 24 bits: index (supports ~16 million entities)
struct entity {
    uint32_t id{0};

    constexpr entity() = default;
    constexpr explicit entity(uint32_t value) : id(value) {}
    constexpr entity(uint32_t index, uint8_t generation)
        : id((static_cast<uint32_t>(generation) << 24) | (index & 0x00FFFFFF)) {}

    [[nodiscard]] constexpr auto is_valid() const -> bool { return id != 0; }
    [[nodiscard]] constexpr auto index() const -> uint32_t { return id & 0x00FFFFFF; }
    [[nodiscard]] constexpr auto generation() const -> uint8_t { return static_cast<uint8_t>(id >> 24); }

    [[nodiscard]] constexpr auto operator==(const entity& other) const -> bool = default;
    [[nodiscard]] constexpr auto operator!=(const entity& other) const -> bool = default;

    // For use in containers
    [[nodiscard]] constexpr auto operator<(const entity& other) const -> bool { return id < other.id; }

    // Null entity constant
    static constexpr entity null() { return entity{0}; }
};

// Entity type tags for distinguishing entity categories
enum class entity_type : uint8_t {
    none = 0,
    player = 1,
    npc = 2,
    item = 3,
    dynamic_object = 4,
    projectile = 5,
};

// Maximum entities per type
inline constexpr uint32_t max_players = 2000;
inline constexpr uint32_t max_npcs = 10000;
inline constexpr uint32_t max_items = 60000;
inline constexpr uint32_t max_dynamic_objects = 5000;
inline constexpr uint32_t max_entities = max_players + max_npcs + max_items + max_dynamic_objects;

}  // namespace hb::entity

// Hash specialization for entity
template<>
struct std::hash<hb::entity::entity> {
    auto operator()(const hb::entity::entity& e) const noexcept -> size_t {
        return std::hash<uint32_t>{}(e.id);
    }
};
