#pragma once

// name.h
// Name and identity components

#include <string>
#include <cstdint>

namespace hb::entity
{

// Name component
struct name
{
    std::string value;

    name() = default;
    explicit name(std::string n) : value(std::move(n)) {}
    explicit name(const char* n) : value(n) {}
};

// Guild membership component
struct guild_member
{
    uint32_t guild_id{0};
    uint8_t rank{0};

    [[nodiscard]] auto has_guild() const -> bool { return guild_id != 0; }
};

// Faction/side component
enum class faction : uint8_t
{
    none = 0,
    aresden = 1,
    elvine = 2,
    neutral = 3, // For NPCs
};

struct faction_component
{
    faction side{faction::none};

    [[nodiscard]] auto is_hostile_to(faction other) const -> bool
    {
        if (side == faction::none || other == faction::none)
            return false;
        if (side == faction::neutral || other == faction::neutral)
            return false;
        return side != other;
    }
};

} // namespace hb::entity
