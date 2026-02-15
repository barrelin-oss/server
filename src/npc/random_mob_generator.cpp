// random_mob_generator.cpp
// Level-based random NPC spawning implementation
//
// Translated from legacy Game.cpp void CGame::MobGenerator()
// Preserves original spawn probabilities and NPC groups per level

#include "npc/random_mob_generator.h"
#include "registry/npc_registry.h"

#include <random>

namespace hb::npc
{

namespace
{
thread_local std::mt19937 rng{std::random_device{}()};

auto roll_d100() -> int
{
    std::uniform_int_distribution<int> dist(1, 100);
    return dist(rng);
}

auto roll_dice(int count, int sides) -> int
{
    std::uniform_int_distribution<int> dist(1, sides);
    int result = 0;
    for (int i = 0; i < count; ++i)
    {
        result += dist(rng);
    }
    return result;
}
} // namespace

auto random_mob_generator::get_random_npc(int level) -> std::optional<random_npc_choice>
{
    if (level < 1 || level > 7)
    {
        return std::nullopt;
    }

    switch (level)
    {
    case 1:
        return select_level_1();
    case 2:
        return select_level_2();
    case 3:
        return select_level_3();
    case 4:
        return select_level_4();
    case 5:
        return select_level_5();
    case 6:
        return select_level_6();
    case 7:
        return select_level_7();
    default:
        return std::nullopt;
    }
}

// Level 1: arefarm, elvfarm, aresden, elvine (starter areas)
// Probabilities: Rabbit 45%, Slime 19%, Giant-Ant 20%, Cat 10%, Orc 5%, Rudolph 1%
auto random_mob_generator::select_level_1() -> random_npc_choice
{
    int roll = roll_d100();

    if (roll >= 1 && roll < 20)
    {
        return {"Slime", npc_id{10}};
    }
    else if (roll >= 20 && roll < 40)
    {
        return {"Giant-Ant", npc_id{16}};
    }
    else if (roll >= 40 && roll < 85)
    {
        return {"Rabbit", npc_id{24}};
    }
    else if (roll >= 85 && roll < 95)
    {
        return {"Cat", npc_id{25}};
    }
    else
    { // 95-100
        return {"Orc", npc_id{14}};
    }
}

// Level 2: Basic dungeons
// Probabilities: Slime 40%, Giant-Ant 40%, Amphis 20%
auto random_mob_generator::select_level_2() -> random_npc_choice
{
    int roll = roll_d100();

    if (roll >= 1 && roll < 40)
    {
        return {"Slime", npc_id{10}};
    }
    else if (roll >= 40 && roll < 80)
    {
        return {"Giant-Ant", npc_id{16}};
    }
    else
    { // 80-100
        return {"Amphis", npc_id{22}};
    }
}

// Level 3: Mid-level areas
auto random_mob_generator::select_level_3() -> random_npc_choice
{
    int roll = roll_d100();

    if (roll >= 1 && roll < 20)
    {
        // Orc or Zombie
        return roll_dice(1, 2) == 1 ? random_npc_choice{"Orc", npc_id{14}} : random_npc_choice{"Zombie", npc_id{18}};
    }
    else if (roll >= 20 && roll < 25)
    {
        return {"Rudolph", npc_id{30}};
    }
    else if (roll >= 25 && roll < 50)
    {
        // Skeleton, Orc-Mage, or Scorpion
        int sub = roll_dice(1, 3);
        switch (sub)
        {
        case 1:
            return {"Skeleton", npc_id{11}};
        case 2:
            return {"Orc-Mage", npc_id{14}};
        case 3:
            return {"Scorpion", npc_id{17}};
        default:
            return {"Skeleton", npc_id{11}};
        }
    }
    else if (roll >= 50 && roll < 75)
    {
        // Stone-Golem, Clay-Golem, Troll, WereWolf, Giant-Frog, or Ettin
        int sub = roll_dice(1, 7);
        switch (sub)
        {
        case 1:
        case 2:
            return {"Stone-Golem", npc_id{12}};
        case 3:
            return {"Clay-Golem", npc_id{23}};
        case 4:
            return {"Troll", npc_id{28}};
        case 5:
            return {"WereWolf", npc_id{33}};
        case 6:
            return {"Giant-Frog", npc_id{57}};
        case 7:
            return {"Ettin", npc_id{59}};
        default:
            return {"Stone-Golem", npc_id{12}};
        }
    }
    else
    { // 75-100
        // Cyclops, Orge, Hellbound, or Mountain-Giant
        int sub = roll_dice(1, 5);
        switch (sub)
        {
        case 1:
        case 2:
            return {"Cyclops", npc_id{13}};
        case 3:
            return {"Orge", npc_id{29}};
        case 4:
            return {"Hellbound", npc_id{27}};
        case 5:
            return {"Mountain-Giant", npc_id{58}};
        default:
            return {"Cyclops", npc_id{13}};
        }
    }
}

// Level 4: Dungeons
auto random_mob_generator::select_level_4() -> random_npc_choice
{
    int roll = roll_d100();

    if (roll >= 1 && roll < 50)
    {
        // Giant-Ant or Amphis
        return roll_dice(1, 2) == 1 ? random_npc_choice{"Giant-Ant", npc_id{16}}
                                    : random_npc_choice{"Amphis", npc_id{22}};
    }
    else if (roll >= 50 && roll < 80)
    {
        // Stone-Golem or Clay-Golem
        return roll_dice(1, 2) == 1 ? random_npc_choice{"Stone-Golem", npc_id{12}}
                                    : random_npc_choice{"Clay-Golem", npc_id{23}};
    }
    else
    { // 80-100
        // Hellbound or Cyclops
        return roll_dice(1, 2) == 1 ? random_npc_choice{"Hellbound", npc_id{27}}
                                    : random_npc_choice{"Cyclops", npc_id{13}};
    }
}

// Level 5: Advanced areas
auto random_mob_generator::select_level_5() -> random_npc_choice
{
    int roll = roll_d100();

    if (roll >= 1 && roll < 30)
    {
        return {"Giant-Ant", npc_id{16}};
    }
    else if (roll >= 30 && roll < 60)
    {
        // Orc or Zombie
        return roll_dice(1, 2) == 1 ? random_npc_choice{"Orc", npc_id{14}} : random_npc_choice{"Zombie", npc_id{18}};
    }
    else if (roll >= 60 && roll < 80)
    {
        // Skeleton or Scorpion
        return roll_dice(1, 2) == 1 ? random_npc_choice{"Skeleton", npc_id{11}}
                                    : random_npc_choice{"Scorpion", npc_id{17}};
    }
    else if (roll >= 80 && roll < 95)
    {
        // Stone-Golem or Clay-Golem
        int sub = roll_dice(1, 3);
        switch (sub)
        {
        case 1:
        case 2:
            return {"Stone-Golem", npc_id{12}};
        case 3:
            return {"Clay-Golem", npc_id{23}};
        default:
            return {"Stone-Golem", npc_id{12}};
        }
    }
    else
    { // 95-100
        // Clay-Golem, Hellbound, or Cyclops
        int sub = roll_dice(1, 3);
        switch (sub)
        {
        case 1:
            return {"Clay-Golem", npc_id{23}};
        case 2:
            return {"Hellbound", npc_id{27}};
        case 3:
            return {"Cyclops", npc_id{13}};
        default:
            return {"Clay-Golem", npc_id{23}};
        }
    }
}

// Level 6: huntzone3, huntzone4 (high-level zones)
auto random_mob_generator::select_level_6() -> random_npc_choice
{
    int roll = roll_d100();

    if (roll >= 1 && roll < 60)
    {
        // Skeleton, Orc-Mage, Cyclops, or Troll
        int sub = roll_dice(1, 4);
        switch (sub)
        {
        case 1:
            return {"Skeleton", npc_id{11}};
        case 2:
            return {"Orc-Mage", npc_id{14}};
        case 3:
            return {"Cyclops", npc_id{13}};
        case 4:
            return {"Troll", npc_id{28}};
        default:
            return {"Skeleton", npc_id{11}};
        }
    }
    else if (roll >= 60 && roll < 90)
    {
        // Stone-Golem, Troll, Cyclops, or Tentocle
        int sub = roll_dice(1, 5);
        switch (sub)
        {
        case 1:
        case 2:
            return {"Stone-Golem", npc_id{12}};
        case 3:
            return {"Troll", npc_id{28}};
        case 4:
            return {"Cyclops", npc_id{13}};
        case 5:
            return {"Tentocle", npc_id{43}};
        default:
            return {"Stone-Golem", npc_id{12}};
        }
    }
    else
    { // 90-100
        // Various high-level monsters
        int sub = roll_dice(1, 9);
        switch (sub)
        {
        case 1:
            return {"Giant-Frog", npc_id{57}};
        case 2:
            return {"Cyclops", npc_id{13}};
        case 3:
            return {"Orge", npc_id{29}};
        case 4:
            return {"Hellbound", npc_id{27}};
        case 5:
            return {"WereWolf", npc_id{33}};
        case 6:
            return {"Ettin", npc_id{59}};
        case 7:
            return {"Mountain-Giant", npc_id{58}};
        case 8:
            return {"Cannibal-Plant", npc_id{60}};
        default:
            return {"Cyclops", npc_id{13}};
        }
    }
}

// Level 7+: Elite zones
auto random_mob_generator::select_level_7() -> random_npc_choice
{
    int roll = roll_d100();

    if (roll >= 1 && roll < 50)
    {
        // Orge, Hellbound, or Liche
        int sub = roll_dice(1, 3);
        switch (sub)
        {
        case 1:
            return {"Orge", npc_id{29}};
        case 2:
            return {"Hellbound", npc_id{27}};
        case 3:
            return {"Liche", npc_id{30}};
        default:
            return {"Orge", npc_id{29}};
        }
    }
    else if (roll >= 50 && roll < 80)
    {
        // Liche, Demon, or Unicorn
        int sub = roll_dice(1, 3);
        switch (sub)
        {
        case 1:
            return {"Liche", npc_id{30}};
        case 2:
            return {"Demon", npc_id{31}};
        case 3:
            return {"Unicorn", npc_id{32}};
        default:
            return {"Liche", npc_id{30}};
        }
    }
    else
    { // 80-100
        // Elite monsters
        int sub = roll_dice(1, 7);
        switch (sub)
        {
        case 1:
            return {"Gagoyle", npc_id{52}};
        case 2:
            return {"Beholder", npc_id{53}};
        case 3:
            return {"Dark-Elf", npc_id{54}};
        case 4:
            return {"Ice-Golem", npc_id{65}};
        case 5:
            return {"DireBoar", npc_id{62}};
        case 6:
            return {"Frost", npc_id{63}};
        case 7:
            return {"Wyvern", npc_id{66}};
        default:
            return {"Gagoyle", npc_id{52}};
        }
    }
}

} // namespace hb::npc
