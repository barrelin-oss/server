#pragma once

// tile.h
// Tile data structures for the game world
// Split into static (rarely changes) and dynamic (changes often) for cache efficiency

#include "core/types.h"
#include <cstdint>

namespace hb::world {

// Tile flags (static properties)
enum class tile_flags : uint8_t {
    none = 0,
    blocks_movement = 1 << 0,    // Cannot walk through
    is_teleport = 1 << 1,        // Teleport trigger
    is_water = 1 << 2,           // Water tile (affects movement, fishing)
    is_farm = 1 << 3,            // Farmable area
    is_safe_zone = 1 << 4,       // No PvP allowed
    is_no_recall = 1 << 5,       // Cannot use recall here
};

// Bitwise operators for tile_flags
[[nodiscard]] constexpr auto operator|(tile_flags a, tile_flags b) -> tile_flags {
    return static_cast<tile_flags>(static_cast<uint8_t>(a) | static_cast<uint8_t>(b));
}

[[nodiscard]] constexpr auto operator&(tile_flags a, tile_flags b) -> tile_flags {
    return static_cast<tile_flags>(static_cast<uint8_t>(a) & static_cast<uint8_t>(b));
}

[[nodiscard]] constexpr auto has_flag(tile_flags flags, tile_flags flag) -> bool {
    return (static_cast<uint8_t>(flags) & static_cast<uint8_t>(flag)) != 0;
}

// Dynamic tile flags (change during gameplay)
enum class dynamic_tile_flags : uint8_t {
    none = 0,
    temp_blocked = 1 << 0,       // Temporarily blocked (e.g., by structure)
    has_corpse = 1 << 1,         // Dead entity here (for looting)
    has_item = 1 << 2,           // Item on ground
    has_dynamic_object = 1 << 3, // Dynamic object (trap, portal, etc.)
};

[[nodiscard]] constexpr auto operator|(dynamic_tile_flags a, dynamic_tile_flags b) -> dynamic_tile_flags {
    return static_cast<dynamic_tile_flags>(static_cast<uint8_t>(a) | static_cast<uint8_t>(b));
}

[[nodiscard]] constexpr auto operator&(dynamic_tile_flags a, dynamic_tile_flags b) -> dynamic_tile_flags {
    return static_cast<dynamic_tile_flags>(static_cast<uint8_t>(a) & static_cast<uint8_t>(b));
}

[[nodiscard]] constexpr auto has_flag(dynamic_tile_flags flags, dynamic_tile_flags flag) -> bool {
    return (static_cast<uint8_t>(flags) & static_cast<uint8_t>(flag)) != 0;
}

// Owner type for tiles
enum class owner_type : uint8_t {
    none = 0,
    player = 1,
    npc = 2,
    player_indirect = 3,  // E.g., pet or summon
};

// Occupy status (for territory control)
enum class occupy_side : int8_t {
    neutral = 0,
    aresden = -1,  // Negative values
    elvine = 1,    // Positive values
};

// Static tile data - loaded from map file, rarely changes
// Optimized for minimal memory: 4 bytes per tile
struct static_tile {
    uint16_t terrain_type{0};    // Ground/terrain sprite ID
    tile_flags flags{tile_flags::none};
    uint8_t reserved{0};         // Padding/future use

    [[nodiscard]] constexpr auto is_walkable() const -> bool {
        return !has_flag(flags, tile_flags::blocks_movement);
    }

    [[nodiscard]] constexpr auto is_teleport() const -> bool {
        return has_flag(flags, tile_flags::is_teleport);
    }

    [[nodiscard]] constexpr auto is_water() const -> bool {
        return has_flag(flags, tile_flags::is_water);
    }

    [[nodiscard]] constexpr auto is_farm() const -> bool {
        return has_flag(flags, tile_flags::is_farm);
    }

    [[nodiscard]] constexpr auto is_safe_zone() const -> bool {
        return has_flag(flags, tile_flags::is_safe_zone);
    }
};

// Dynamic tile data - changes during gameplay
// Kept separate from static for better cache performance
// 16 bytes per tile
struct dynamic_tile {
    // Current occupant
    entity_id occupant{0};           // Entity currently on this tile (4 bytes)
    owner_type occupant_type{owner_type::none};  // (1 byte)

    // Dead entity (corpse)
    entity_id dead_entity{0};        // (4 bytes)
    owner_type dead_type{owner_type::none};      // (1 byte)

    // Dynamic object
    uint16_t dynamic_object_id{0};   // (2 bytes)
    uint16_t dynamic_object_type{0}; // (2 bytes)

    // Item count on tile
    uint8_t item_count{0};           // (1 byte)

    // Flags
    dynamic_tile_flags flags{dynamic_tile_flags::none};  // (1 byte)

    [[nodiscard]] constexpr auto has_occupant() const -> bool {
        return occupant.value != 0;
    }

    [[nodiscard]] constexpr auto has_dead_entity() const -> bool {
        return dead_entity.value != 0;
    }

    [[nodiscard]] constexpr auto has_dynamic_object() const -> bool {
        return dynamic_object_id != 0;
    }

    [[nodiscard]] constexpr auto has_items() const -> bool {
        return item_count > 0;
    }

    [[nodiscard]] constexpr auto is_temp_blocked() const -> bool {
        return has_flag(flags, dynamic_tile_flags::temp_blocked);
    }

    void clear() {
        occupant = entity_id{0};
        occupant_type = owner_type::none;
        dead_entity = entity_id{0};
        dead_type = owner_type::none;
        dynamic_object_id = 0;
        dynamic_object_type = 0;
        item_count = 0;
        flags = dynamic_tile_flags::none;
    }

    void set_occupant(entity_id id, owner_type type) {
        occupant = id;
        occupant_type = type;
    }

    void clear_occupant() {
        occupant = entity_id{0};
        occupant_type = owner_type::none;
    }

    void set_dead_entity(entity_id id, owner_type type) {
        dead_entity = id;
        dead_type = type;
        flags = flags | dynamic_tile_flags::has_corpse;
    }

    void clear_dead_entity() {
        dead_entity = entity_id{0};
        dead_type = owner_type::none;
        flags = static_cast<dynamic_tile_flags>(
            static_cast<uint8_t>(flags) & ~static_cast<uint8_t>(dynamic_tile_flags::has_corpse)
        );
    }
};

// Occupy flag data for territory control
struct occupy_flag_info {
    int16_t x{0};
    int16_t y{0};
    occupy_side side{occupy_side::neutral};
    int32_t enemy_kill_count{0};
    uint16_t dynamic_object_index{0};
};

// Maximum items per tile (from legacy: DEF_TILE_PER_ITEMS = 12)
inline constexpr int max_items_per_tile = 12;

}  // namespace hb::world
