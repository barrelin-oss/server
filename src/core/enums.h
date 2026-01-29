#pragma once

// enums.h
// Strongly-typed enumerations for Helbreath server
// Replaces legacy #define constants with type-safe enum classes

#include <cstdint>

namespace hb {

// Direction (8 directions plus center)
enum class direction : uint8_t {
    none = 0,
    north = 1,
    north_east = 2,
    east = 3,
    south_east = 4,
    south = 5,
    south_west = 6,
    west = 7,
    north_west = 8
};

// Object/entity action states
enum class object_state : uint8_t {
    stop = 0,
    move = 1,
    run = 2,
    attack = 3,
    magic = 4,
    get_item = 5,
    damage = 6,
    damage_move = 7,
    attack_move = 8,
    dying = 10,
    dead = 11,
    null_action = 100
};

// NPC behavior states
enum class npc_behavior : uint8_t {
    stop = 0,
    move = 1,
    attack = 2,
    flee = 3,
    dead = 4
};

// NPC movement types
enum class npc_move_type : uint8_t {
    stop = 0,
    seq_waypoint = 1,
    random_waypoint = 2,
    follow = 3,
    random_area = 4,
    random = 5,
    guard = 6
};

// Equipment positions
enum class equip_position : uint8_t {
    none = 0,
    head = 1,
    body = 2,
    arms = 3,
    pants = 4,
    leggings = 5,
    neck = 6,
    left_hand = 7,
    right_hand = 8,
    two_hand = 9,
    right_finger = 10,
    left_finger = 11,
    back = 12,
    release_all = 13
};

// Item types
enum class item_type : int8_t {
    not_used = -1,
    none = 0,
    equip = 1,
    apply = 2,
    use_deplete = 3,
    install = 4,
    consume = 5,
    arrow = 6,
    eat = 7,
    use_skill = 8,
    use_perm = 9,
    use_skill_enable_dialogbox = 10,
    use_deplete_dest = 11,
    material = 12,
    weapon = 13,
    armor = 14,
    accessory = 15,
    potion = 16,
    scroll = 17
};

// Item equip position (where item is equipped)
enum class item_equip_pos : uint8_t {
    none = 0,
    head = 1,
    body = 2,
    arms = 3,
    pants = 4,
    boots = 5,
    neck = 6,
    left_hand = 7,
    right_hand = 8,
    two_hand = 9,
    ring_right = 10,
    ring_left = 11,
    back = 12
};

// Item category for organization
enum class item_category : uint8_t {
    general = 0,
    weapon_sword = 1,
    weapon_axe = 2,
    weapon_bow = 3,
    weapon_staff = 4,
    weapon_wand = 5,
    weapon_hammer = 6,
    armor_head = 10,
    armor_body = 11,
    armor_arms = 12,
    armor_pants = 13,
    armor_boots = 14,
    armor_shield = 15,
    accessory_ring = 20,
    accessory_necklace = 21,
    accessory_cape = 22,
    consumable_potion = 30,
    consumable_food = 31,
    consumable_scroll = 32,
    material_ore = 40,
    material_gem = 41,
    material_herb = 42,
    quest = 50,
    misc = 99
};

// Item effect types
enum class item_effect_type : uint8_t {
    none = 0,
    attack = 1,
    defense = 2,
    hp = 4,
    mp = 5,
    sp = 6,
    magic = 11,
    dye = 17,
    slates = 31
};

// Magic types
enum class magic_type : uint8_t {
    damage_spot = 1,
    hp_up_spot = 2,
    damage_area = 3,
    sp_down_spot = 4,
    sp_up_spot = 6,
    teleport = 8,
    summon = 9,
    invisibility = 13,
    poison = 17,
    berserk = 18,
    polymorph = 20,
    ice = 23,
    resurrection = 32
};

// Quest types
enum class quest_type : uint8_t {
    monster_hunt = 1,
    monster_hunt_timelimit = 2,
    assassination = 3,
    delivery = 4,
    escort = 5,
    guard = 6,
    go_place = 7,
    build_structure = 8,
    strategic_strike = 10,
    set_occupy_flag = 12
};

// Player stat attributes
enum class stat_attribute : uint8_t {
    strength = 0x01,
    dexterity = 0x02,
    intelligence = 0x03,
    vitality = 0x04,
    magic = 0x05,
    charisma = 0x06
};

// Faction/side
enum class faction : uint8_t {
    neutral = 0,
    aresden = 1,
    elvine = 2
};

// Map type
enum class map_type : uint8_t {
    normal = 0,
    no_penalty_no_reward = 1
};

// Owner type (for tiles)
enum class owner_type : uint8_t {
    none = 0,
    player = 1,
    npc = 2,
    player_indirect = 3
};

// Dynamic object types
enum class dynamic_object_type : uint8_t {
    fire = 1,
    fish = 2,
    fish_object = 3,
    mineral1 = 4,
    mineral2 = 5,
    aresden_flag1 = 6,
    elvine_flag1 = 7,
    icestorm = 8,
    spike = 9
};

// Item log types
enum class item_log_type : uint8_t {
    give = 1,
    drop = 2,
    get = 3,
    deplete = 4,
    newgen_drop = 5,
    dup_item_id = 6,
    buy = 7,
    sell = 8,
    retrieve = 9,
    deposit = 10,
    exchange = 11,
    skill_learn = 12,
    make = 13,
    summon_monster = 14,
    poisoned = 15,
    magic_learn = 16,
    repair = 17,
    use = 32
};

// Motion confirmation states
enum class motion_confirm : uint16_t {
    move_confirm = 1001,
    move_reject = 1010,
    motion_confirm = 1020,
    attack_confirm = 1030,
    motion_reject = 1040
};

// Damage type
enum class damage_type : uint8_t {
    physical = 0,
    magic = 1,
    arrow = 2
};

// Weather type
enum class weather_type : uint8_t {
    clear = 0,
    rainy = 1,
    snowy = 2,
    foggy = 3
};

// Time of day
enum class time_of_day : uint8_t {
    day = 0,
    night = 1
};

// Note: log_level and log_category are defined in logger.h

}  // namespace hb
