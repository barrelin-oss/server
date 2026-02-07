#pragma once

// loot_generator.h
// Legacy loot generation algorithm ported from Game.cpp::NpcDeadItemGenerator
//
// Drop probability breakdown:
//   35% chance to drop anything (m_iPrimaryDropRate = 6500 → 10000-6500 = 3500/10000)
//   If dropping:
//     60% → Gold (item 90), amount from NPC's gold dice
//     40% → Item:
//       ~90% → Standard drop (potions, consumables, rare materials)
//       ~10% → Valuable equipment (sprite_id → drop_level → weapon/armor pools)

#include "core/types.h"
#include "npc/loot_table.h"  // Reuse detail::loot_rng()

#include <cstdint>
#include <optional>
#include <random>
#include <vector>

namespace hb::npc {

namespace detail {
    inline auto dice(int count, int sides) -> int {
        if (count <= 0 || sides <= 0) return 0;
        std::uniform_int_distribution<int> dist(1, sides);
        int total = 0;
        for (int i = 0; i < count; ++i) {
            total += dist(loot_rng());
        }
        return total;
    }
}  // namespace detail

// Result of loot generation
struct loot_drop_result {
    item_id item_template{};  // Template ID of item to create (0 = no drop)
    int32_t gold_amount{0};   // Gold amount if item is gold (template 90)

    [[nodiscard]] auto has_drop() const -> bool { return item_template.is_valid(); }
};

// NPC info needed for loot generation
struct loot_npc_info {
    int16_t sprite_id{0};
    int32_t gold_min{0};
    int32_t gold_max{0};
    bool is_summoned{false};
};

// Get the drop level (1-10) from NPC sprite_id
// Returns 0 if this NPC type should not drop valuable items
inline auto get_drop_level(int16_t sprite_id) -> int {
    switch (sprite_id) {
    case 10: // Slime
    case 16: // Giant-Ant
    case 22: // Amphis
    case 55: // Rabbit
    case 56: // Cat
        return 1;

    case 11: // Skeleton
    case 14: // Orc, Orc-Mage
    case 17: // Scorpion
    case 18: // Zombie
        return 2;

    case 12: // Stone-Golem
    case 23: // Clay-Golem
        return 3;

    case 27: // Hellbound
    case 61: // Rudolph
        return 4;

    case 72: // Claw-Turtle
    case 76: // Giant-Plant
    case 74: // Giant-Crayfish
    case 13: // Cyclops
    case 28: // Troll
    case 53: // Beholder
    case 60: // Cannibal-Plant
    case 62: // DireBoar
        return 5;

    case 29: // Ogre
    case 33: // WereWolf
    case 48: // Stalker
    case 54: // Dark-Elf
    case 65: // Ice-Golem
    case 78: // Minotaurus
        return 6;

    case 70: // Barlog
    case 71: // Centaurus
    case 30: // Liche
    case 63: // Frost
    case 79: // Nizie
        return 7;

    case 31: // Demon
    case 32: // Unicorn
    case 49: // Hellclaw
    case 50: // Tigerworm
    case 52: // Gagoyle
        return 8;

    case 58: // Mountain-Giant
        return 9;

    case 77: // MasterMage-Orc
    case 59: // Ettin
    case 75: // Giant-Lizard
        return 10;

    default:
        return 0;
    }
}

// Roll a standard drop (potions, consumables, rare items)
// Returns the item template ID
inline auto roll_standard_drop() -> item_id {
    int roll = detail::dice(1, 12000);

    if (roll <= 3000) {
        return item_id{95};   // GreenPotion
    }
    if (roll <= 4000) {
        return item_id{91};   // RedPotion
    }
    if (roll <= 5500) {
        return item_id{93};   // BluePotion
    }
    if (roll <= 7000) {
        return item_id{96};   // BigGreenPotion
    }
    if (roll <= 8500) {
        return item_id{92};   // BigRedPotion
    }
    if (roll <= 9200) {
        return item_id{94};   // BigBluePotion
    }
    if (roll <= 9800) {
        // Rare consumables
        switch (detail::dice(1, 6)) {
        case 1: return item_id{390};  // PowerGreenPotion
        case 2: return item_id{95};   // GreenPotion
        case 3: return item_id{780};  // RedCandy
        case 4: return item_id{781};  // BlueCandy
        case 5: return item_id{782};  // GreenCandy
        case 6: return item_id{970};  // CritCandy
        default: return item_id{95};
        }
    }
    if (roll <= 10000) {
        // Ultra rare items
        switch (detail::dice(1, 10)) {
        case 1: return item_id{391};   // SuperGreenPotion
        case 2: return item_id{650};   // Zemstone of Sacrifice
        case 3: return item_id{656};   // Xelima Stone
        case 4: return item_id{657};   // Merien Stone
        case 5: return item_id{95};    // GreenPotion
        case 6: return item_id{868};   // AncientTablet(LU)
        case 7: return item_id{869};   // AncientTablet(LD)
        case 8: return item_id{870};   // AncientTablet(RU)
        case 9: return item_id{871};   // AncientTablet(RD)
        case 10:
            // Colored balls
            switch (detail::dice(1, 5)) {
            case 1: return item_id{651};  // GreenBall
            case 2: return item_id{652};  // RedBall
            case 3: return item_id{653};  // YellowBall
            case 4: return item_id{654};  // BlueBall
            case 5: return item_id{655};  // PearlBall
            default: return item_id{651};
            }
        default: return item_id{95};
        }
    }
    // 10001-12000: December candy drops (simplified - always available)
    // In legacy code this was month==12 && specific NPCs only
    // We skip this seasonal logic for now
    return item_id{0};  // No drop for this range
}

// Roll a melee weapon for the given drop level
inline auto roll_melee_weapon(int drop_level) -> item_id {
    switch (drop_level) {
    case 1: // Slime, Giant-Ant, Amphis, Rabbit, Cat
        switch (detail::dice(1, 3)) {
        case 1: return item_id{1};   // Dagger
        case 2: return item_id{8};   // ShortSword
        case 3: return item_id{59};  // LightAxe
        default: return item_id{1};
        }

    case 2: // Skeleton, Orc, Scorpion, Zombie
        switch (detail::dice(1, 6)) {
        case 1: return item_id{12};  // MainGauche
        case 2: return item_id{15};  // Gradius
        case 3: return item_id{65};  // SexonAxe
        case 4: return item_id{62};  // Tomahawk
        case 5: return item_id{23};  // Sabre
        case 6: return item_id{31};  // Esterk
        default: return item_id{12};
        }

    case 3: // Stone-Golem, Clay-Golem
        switch (detail::dice(1, 4)) {
        case 1: return item_id{50};  // GreatSword
        case 2: return item_id{68};  // DoubleAxe
        case 3: return item_id{23};  // Sabre
        case 4: return item_id{31};  // Esterk
        default: return item_id{50};
        }

    case 4: // Hellbound, Rudolph
        switch (detail::dice(1, 5)) {
        case 1: return item_id{25};  // Scimitar
        case 2: return item_id{28};  // Falchion
        case 3: return item_id{31};  // Esterk
        case 4: return item_id{34};  // Rapier
        case 5: return item_id{71};  // WarAxe
        default: return item_id{25};
        }

    case 5: // Cyclops, Troll, Beholder, etc.
        switch (detail::dice(1, 4)) {
        case 1: return item_id{31};   // Esterk
        case 2: return item_id{34};   // Rapier
        case 3: return item_id{72};   // WarAxe+1
        case 4: return item_id{844};  // BlackShadowSword
        default: return item_id{31};
        }

    case 6: // Ogre, WereWolf, Stalker, Dark-Elf, Ice-Golem, Minotaurus
        switch (detail::dice(1, 7)) {
        case 1: return item_id{47};   // Claymore+1
        case 2: return item_id{51};   // GreatSword+1
        case 3: return item_id{55};   // Flameberge+1
        case 4: return item_id{34};   // GiantSword
        case 5: return item_id{74};   // GoldenAxe
        case 6: return item_id{848};  // HolyBlade
        case 7: return item_id{924};  // MageSword
        default: return item_id{47};
        }

    case 7: // Liche, Frost, Barlog, Centaurus, Nizie
        switch (detail::dice(1, 6)) {
        case 1: return item_id{47};   // Claymore+1
        case 2: return item_id{50};   // GreatSword
        case 3: return item_id{54};   // Flameberge+1
        case 4: return item_id{74};   // GoldenAxe
        case 5: return item_id{850};  // KlonessAxe
        case 6: return item_id{923};  // BMageSword
        default: return item_id{47};
        }

    case 8: // Demon, Unicorn, Hellclaw, Tigerworm, Gagoyle
        switch (detail::dice(1, 5)) {
        case 1: return item_id{50};   // GreatSword
        case 2: return item_id{560};  // BattleAxe
        case 3: return item_id{615};  // GiantSword
        case 4: return item_id{56};   // Flameberge+2
        case 5: return item_id{846};  // The_Devastator
        default: return item_id{50};
        }

    case 9: // Mountain-Giant
        switch (detail::dice(1, 5)) {
        case 1: return item_id{55};   // Flameberge+1
        case 2: return item_id{615};  // GiantSword
        case 3: return item_id{761};  // BattleHammer
        case 4: return item_id{762};  // GiantBattleHammer
        case 5: return item_id{857};  // I.M.C Manual
        default: return item_id{55};
        }

    case 10: // Ettin, MasterMage-Orc, Giant-Lizard
        switch (detail::dice(1, 9)) {
        case 1: return item_id{50};   // GreatSword
        case 2: return item_id{51};   // GreatSword+1
        case 3: return item_id{55};   // Flameberge+1
        case 4: return item_id{56};   // Flameberge+2
        case 5: return item_id{615};  // GiantSword
        case 6: return item_id{761};  // BattleHammer
        case 7: return item_id{762};  // GiantBattleHammer
        case 8: return item_id{843};  // BarbarianHammer
        case 9: return item_id{853};  // E.S.W Manual
        default: return item_id{50};
        }

    default:
        return item_id{0};
    }
}

// Roll a wand for the given drop level
inline auto roll_wand(int drop_level) -> item_id {
    switch (drop_level) {
    case 2:
    case 3:
        return item_id{258};  // MagicWand(MS20)
    case 4:
    case 5:
    case 6:
        return item_id{257};  // MagicWand(MS20)
    case 7:
    case 8:
        return item_id{256};  // MagicWand(MS20)
    default:
        return item_id{0};  // Drop levels 1, 9, 10 don't drop wands
    }
}

// Roll armor/shield for the given drop level
inline auto roll_armor(int drop_level) -> item_id {
    switch (drop_level) {
    case 1: // Slime, Giant-Ant, Amphis, Rabbit, Cat
    case 2: // Skeleton, Orc, Scorpion, Zombie
        switch (detail::dice(1, 2)) {
        case 1: return item_id{79};  // WoodShield
        case 2: return item_id{81};  // TargeShield
        default: return item_id{79};
        }

    case 3: // Stone-Golem, Clay-Golem
        switch (detail::dice(1, 5)) {
        case 1: return item_id{85};   // LagiShield
        case 2: return item_id{454};  // Hauberk(M)
        case 3: return item_id{472};  // Hauberk(W)
        case 4: return item_id{461};  // ChainHose(M)
        case 5: return item_id{482};  // ChainHose(W)
        default: return item_id{85};
        }

    case 4: // Hellbound, Rudolph
        switch (detail::dice(1, 5)) {
        case 1: return item_id{454};  // Hauberk(M)
        case 2: return item_id{472};  // Hauberk(W)
        case 3: return item_id{461};  // ChainHose(M)
        case 4: return item_id{482};  // ChainHose(W)
        case 5: return item_id{86};   // KnightShield
        default: return item_id{454};
        }

    case 5: // Cyclops, Troll, Beholder, etc.
        switch (detail::dice(1, 7)) {
        case 1: return item_id{455};  // LeatherArmor(M)
        case 2: return item_id{475};  // LeatherArmor(W)
        case 3: return item_id{87};   // TowerShield
        case 4: return item_id{454};  // Hauberk(M)
        case 5: return item_id{472};  // Hauberk(W)
        case 6: return item_id{461};  // ChainHose(M)
        case 7: return item_id{482};  // ChainHose(W)
        default: return item_id{455};
        }

    case 6: // Ogre, WereWolf, Stalker, Dark-Elf, Ice-Golem, Minotaurus
        switch (detail::dice(1, 6)) {
        case 1:
            return (detail::dice(1, 2) == 1) ? item_id{456} : item_id{476};  // ChainMail M/W
        case 2:
            return (detail::dice(1, 2) == 1) ? item_id{458} : item_id{478};  // PlateMail M/W
        case 3:
            return item_id{87};  // TowerShield
        case 4:
            switch (detail::dice(1, 8)) {
            case 1: return item_id{750};  // Horned-Helm(M)
            case 2: return item_id{751};  // Wings-Helm(M)
            case 3: return item_id{754};  // Horned-Helm(W)
            case 4: return item_id{755};  // Wings-Helm(W)
            case 5: return item_id{752};  // Wizard-Cap(M)
            case 6: return item_id{753};  // Wizard-Hat(M)
            case 7: return item_id{756};  // Wizard-Cap(W)
            case 8: return item_id{757};  // Wizard-Hat(W)
            default: return item_id{750};
            }
        case 5:
            return (detail::dice(1, 2) == 1) ? item_id{454} : item_id{472};  // Hauberk M/W
        case 6:
            return (detail::dice(1, 2) == 1) ? item_id{461} : item_id{482};  // ChainHose M/W
        default:
            return item_id{456};
        }

    case 7: // Liche, Frost, Barlog, Centaurus, Nizie
        switch (detail::dice(1, 6)) {
        case 1:
            switch (detail::dice(1, 6)) {
            case 1: return item_id{457};  // ScaleMail(M)
            case 2: return item_id{477};  // ScaleMail(W)
            case 3: return item_id{454};  // Hauberk(M)
            case 4: return item_id{472};  // Hauberk(W)
            case 5: return item_id{461};  // ChainHose(M)
            case 6: return item_id{482};  // ChainHose(W)
            default: return item_id{457};
            }
        case 2:
            return (detail::dice(1, 2) == 1) ? item_id{458} : item_id{478};  // PlateMail M/W
        case 3:
            return item_id{86};  // KnightShield
        case 4:
            return item_id{87};  // TowerShield
        case 5:
            return (detail::dice(1, 2) == 1) ? item_id{600} : item_id{602};  // Helm M/W
        case 6:
            return (detail::dice(1, 2) == 1) ? item_id{601} : item_id{603};  // Full-Helm M/W
        default:
            return item_id{457};
        }

    case 8: // Demon, Unicorn, Hellclaw, Tigerworm, Gagoyle
        switch (detail::dice(1, 4)) {
        case 1: return item_id{402};  // Cape
        case 2: return item_id{451};  // Boots
        case 3: return item_id{926};  // ShieldOfFaith
        case 4: return item_id{927};  // ShieldOfBrave
        default: return item_id{402};
        }

    case 9: // Mountain-Giant
        switch (detail::dice(1, 2)) {
        case 1: return item_id{402};  // Cape
        case 2: return item_id{451};  // Boots
        default: return item_id{402};
        }

    case 10: // Ettin, MasterMage-Orc, Giant-Lizard
        switch (detail::dice(1, 4)) {
        case 1: return item_id{457};  // ScaleMail(M)
        case 2: return item_id{477};  // ScaleMail(W)
        case 3: return item_id{600};  // Helm(M)
        case 4: return item_id{602};  // Helm(W)
        default: return item_id{457};
        }

    default:
        return item_id{0};
    }
}

// Roll a valuable equipment drop for the given drop level
// 60% weapon (of which 80% melee, 20% wand), 40% armor
inline auto roll_valuable_drop(int drop_level) -> item_id {
    if (drop_level == 0) return item_id{0};

    if (detail::dice(1, 10000) <= 6000) {
        // 60% weapon
        if (detail::dice(1, 10000) <= 8000) {
            // 80% melee weapon
            return roll_melee_weapon(drop_level);
        } else {
            // 20% wand
            auto wand = roll_wand(drop_level);
            if (!wand.is_valid()) {
                // Fallback to melee if this level doesn't have wands
                return roll_melee_weapon(drop_level);
            }
            return wand;
        }
    } else {
        // 40% armor/shield
        return roll_armor(drop_level);
    }
}

// Main loot generation function - port of NpcDeadItemGenerator
// Returns the dropped item template ID and gold amount (if gold)
inline auto generate_npc_loot(const loot_npc_info& npc) -> loot_drop_result {
    loot_drop_result result;

    // Summoned NPCs never drop
    if (npc.is_summoned) return result;

    // Guards (21), Dummies (34), Crops (64) never drop
    switch (npc.sprite_id) {
    case 21: // Guard
    case 34: // Dummy
    case 64: // Crop
        return result;
    }

    // Primary drop rate check: 35% chance to drop anything
    // Legacy: m_iPrimaryDropRate = 6500, drop if dice(1,10000) >= 6500
    if (detail::dice(1, 10000) < 6500) {
        return result;  // No drop (65% of the time)
    }

    // Something will drop - gold or item?
    if (detail::dice(1, 10000) <= 6000) {
        // 60% → Gold
        result.item_template = item_id{90};  // Gold item
        int range = npc.gold_max - npc.gold_min;
        if (range <= 0) {
            result.gold_amount = npc.gold_min;
        } else {
            result.gold_amount = detail::dice(1, range) + npc.gold_min;
        }
        if (result.gold_amount <= 0) {
            result.item_template = item_id{0};  // No gold to drop
        }
        return result;
    }

    // 40% → Item drop
    // Secondary drop rate determines standard vs valuable
    // Legacy: m_iSecondaryDropRate = 9000, modified by player reputation
    // We simplify and use 9000 directly (no reputation modifier for now)
    if (detail::dice(1, 10000) <= 9000) {
        // ~90% → Standard drop (potions, consumables)
        result.item_template = roll_standard_drop();
    } else {
        // ~10% → Valuable equipment drop
        int drop_level = get_drop_level(npc.sprite_id);
        result.item_template = roll_valuable_drop(drop_level);
    }

    return result;
}

}  // namespace hb::npc
