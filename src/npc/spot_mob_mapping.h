#pragma once

// spot_mob_mapping.h
// Maps legacy spot-mob-generator npc_type values to NPC names
//
// The npc_type field in map config spawners uses a legacy mapping system
// where the number is NOT a direct NPC template ID, but a key into this table.

#include <string_view>
#include <optional>

namespace hb::npc
{

// Map legacy spot-mob-generator type to NPC name
// Returns nullopt if the type is not in the mapping (invalid/unknown)
inline auto spot_mob_type_to_name(int type) -> std::optional<std::string_view>
{
    switch (type)
    {
        case 10: return "Slime";
        case 16: return "Giant-Ant";
        case 14: return "Orc";
        case 18: return "Zombie";
        case 11: return "Skeleton";
        case 6:  return "Orc-Mage";
        case 17: return "Scorpion";
        case 12: return "Stone-Golem";
        case 13: return "Cyclops";
        case 22: return "Amphis";
        case 23: return "Clay-Golem";
        case 24: return "Guard-Aresden";
        case 25: return "Guard-Elvine";
        case 26: return "Guard-Neutral";
        case 27: return "Hellbound";
        case 29: return "Orge";
        case 30: return "Liche";
        case 31: return "Demon";
        case 32: return "Unicorn";
        case 33: return "WereWolf";
        case 34: return "Dummy";
        case 35: return "Attack-Dummy";
        case 48: return "Stalker";
        case 49: return "Hellclaw";
        case 50: return "Tigerworm";
        case 54: return "Dark-Elf";
        case 53: return "Beholder";
        case 52: return "Gagoyle";
        case 57: return "Giant-Frog";
        case 58: return "Mountain-Giant";
        case 59: return "Ettin";
        case 60: return "Cannibal-Plant";
        case 61: return "Rudolph";
        case 62: return "DireBoar";
        case 63: return "Frost";
        case 65: return "Ice-Golem";
        case 66: return "Wyvern";
        case 55: return "Rabbit";
        case 67: return "McGaffin";
        case 68: return "Perry";
        case 69: return "Devlin";
        case 73: return "Fire-Wyvern";
        case 70: return "Barlog";
        case 80: return "Tentocle";
        case 71: return "Centaurus";
        case 75: return "Giant-Lizard";
        case 78: return "Minotaurs";
        case 81: return "Abaddon";
        case 72: return "Claw-Turtle";
        case 74: return "Giant-Crayfish";
        case 76: return "Giant-Plant";
        case 77: return "MasterMage-Orc";
        case 79: return "Nizie";
        default: return std::nullopt;
    }
}

}  // namespace hb::npc
