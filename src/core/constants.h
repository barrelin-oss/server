#pragma once

// constants.h
// Compile-time constants for Helbreath server
// These replace the legacy DEF_* macros with type-safe constexpr values

#include <cstdint>
#include <chrono>

namespace hb::constants
{

// Server limits
inline constexpr uint16_t max_clients = 2000;
inline constexpr uint16_t max_npcs = 5000;
inline constexpr uint8_t max_maps = 100;
inline constexpr uint16_t max_item_types = 5000;
inline constexpr uint16_t max_npc_types = 200;
inline constexpr uint16_t max_guilds = 1000;
inline constexpr uint8_t max_guild_members = 128;
inline constexpr uint16_t max_admins = 50;
inline constexpr uint16_t max_banned = 500;

// Player inventory limits
inline constexpr uint8_t max_items = 50;
inline constexpr uint8_t max_bank_items = 200;
inline constexpr uint8_t max_equip_positions = 15;

// Skill and magic limits
inline constexpr uint8_t max_magic_types = 100;
inline constexpr uint8_t max_skill_types = 60;
inline constexpr uint16_t max_skill_points = 700;

// Quest limits
inline constexpr uint16_t max_quest_types = 200;

// Party limits
inline constexpr uint8_t max_party_members = 9;

// Dynamic object limits
inline constexpr uint32_t max_dynamic_objects = 60000;
inline constexpr uint32_t max_delay_events = 60000;

// Map-related limits
inline constexpr uint16_t max_teleport_locations = 200;
inline constexpr uint16_t max_waypoints = 200;
inline constexpr uint16_t max_occupy_flags = 20001;
inline constexpr uint16_t max_strategic_points = 200;
inline constexpr uint8_t max_sectors = 60;
inline constexpr uint8_t max_initial_points = 20;
inline constexpr uint8_t max_dynamic_gates = 10;

// Resource gathering limits
inline constexpr uint8_t max_fish = 200;
inline constexpr uint8_t max_minerals = 200;
inline constexpr uint8_t max_fish_points = 200;
inline constexpr uint8_t max_mineral_points = 200;

// Crafting limits
inline constexpr uint16_t max_build_items = 300;
inline constexpr uint16_t max_portion_types = 500;

// War/event limits
inline constexpr uint16_t max_crusade_structures = 300;
inline constexpr uint8_t max_schedule = 10;
inline constexpr uint8_t max_apocalypse = 7;
inline constexpr uint8_t max_heldenian = 10;
inline constexpr uint8_t max_fight_zones = 10;

// Network limits
inline constexpr uint32_t msg_queue_size = 100000;
inline constexpr uint16_t server_socket_block_limit = 300;
inline constexpr uint8_t max_sub_log_sockets = 10;
inline constexpr uint32_t max_gate_server_stock_msg_size = 10000;

// Timing constants (in milliseconds)
inline constexpr auto client_timeout = std::chrono::milliseconds{10000};
inline constexpr auto sp_up_time = std::chrono::milliseconds{10000};
inline constexpr auto poison_time = std::chrono::milliseconds{12000};
inline constexpr auto hp_up_time = std::chrono::milliseconds{15000};
inline constexpr auto mp_up_time = std::chrono::milliseconds{20000};
inline constexpr auto hunger_time = std::chrono::milliseconds{60000};
inline constexpr auto notice_time = std::chrono::milliseconds{80000};
inline constexpr auto summon_time = std::chrono::milliseconds{300000};
inline constexpr auto auto_save_time = std::chrono::milliseconds{600000};
inline constexpr auto exp_stock_time = std::chrono::milliseconds{10000};
inline constexpr auto auto_exp_time = std::chrono::milliseconds{360000};
inline constexpr auto special_event_time = std::chrono::milliseconds{300000};
inline constexpr auto rag_protection_time = std::chrono::milliseconds{7000};

// Game balance constants
inline constexpr uint8_t total_levelup_points = 3;
inline constexpr uint8_t guild_start_rank = 12;
inline constexpr uint8_t ssn_limit_multiply_value = 2;
inline constexpr uint8_t night_time = 40;
inline constexpr uint16_t char_point_limit = 1000;
inline constexpr uint32_t max_reward_gold = 99999999;
inline constexpr uint8_t level_limit = 20;
inline constexpr uint8_t minimum_hit_ratio = 15;
inline constexpr uint8_t maximum_hit_ratio = 99;
inline constexpr uint8_t player_max_level = 180;
inline constexpr uint8_t max_engaging_fish = 30;

// Construction/war points
inline constexpr uint16_t gm_mana_consume_unit = 15;
inline constexpr uint32_t max_construction_point = 30000;
inline constexpr uint32_t max_summon_points = 30000;
inline constexpr uint32_t max_war_contribution = 200000;
inline constexpr uint8_t max_construction_num = 10;

// Notice limits
inline constexpr uint16_t max_notify_messages = 300;
inline constexpr uint16_t max_one_server_users = 800;

// Duplicate item tracking
inline constexpr uint8_t max_duplicate_item_ids = 100;
inline constexpr uint16_t max_npc_items = 1000;

// Attack AI types
inline constexpr uint8_t attack_ai_normal = 1;
inline constexpr uint8_t attack_ai_exchange_attack = 2;
inline constexpr uint8_t attack_ai_two_by_one_attack = 3;

// Stat attribute codes (legacy compatibility)
inline constexpr uint8_t stat_str = 0x01;
inline constexpr uint8_t stat_dex = 0x02;
inline constexpr uint8_t stat_int = 0x03;
inline constexpr uint8_t stat_vit = 0x04;
inline constexpr uint8_t stat_mag = 0x05;
inline constexpr uint8_t stat_chr = 0x06;

} // namespace hb::constants
