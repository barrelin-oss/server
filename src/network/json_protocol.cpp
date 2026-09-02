// json_protocol.cpp
// JSON-based message protocol implementation

#include "network/json_protocol.h"
#include "core/logger.h"
#include "inventory/inventory.h"
#include "item/item_system.h"
#include "item/item.h"
#include "item/item_serialization.h"
#include "registry/item_registry.h"
#include "registry/item_template.h"

#include <unordered_map>

namespace hb::network
{

namespace
{

// Safe conversion from JSON integer to int16_t with range validation
inline auto safe_int16(const nlohmann::json& j, std::string_view field, int16_t default_val = 0) -> int16_t
{
    if (!j.contains(field) || !j[std::string(field)].is_number())
    {
        return default_val;
    }
    auto val = j[std::string(field)].get<int>();
    if (val < (std::numeric_limits<int16_t>::min)() || val > (std::numeric_limits<int16_t>::max)())
    {
        return default_val; // Out of range, return default
    }
    return static_cast<int16_t>(val);
}

// Safe conversion with required validation (returns nullopt if out of range)
inline auto safe_int16_required(const nlohmann::json& j, std::string_view field) -> std::optional<int16_t>
{
    if (!j.contains(field) || !j[std::string(field)].is_number())
    {
        return std::nullopt;
    }
    auto val = j[std::string(field)].get<int>();
    if (val < (std::numeric_limits<int16_t>::min)() || val > (std::numeric_limits<int16_t>::max)())
    {
        return std::nullopt;
    }
    return static_cast<int16_t>(val);
}

// Safe direction parsing (clamps to 0-7)
inline auto safe_direction(const nlohmann::json& j, std::string_view field, int16_t default_val = 0) -> int16_t
{
    if (!j.contains(field) || !j[std::string(field)].is_number())
    {
        return default_val;
    }
    auto val = j[std::string(field)].get<int>();
    // Clamp to valid direction range 0-7
    if (val < 0)
        val = 0;
    if (val > 7)
        val = 7;
    return static_cast<int16_t>(val);
}

const std::unordered_map<std::string, json_message_type> type_map = {
    {"error", json_message_type::error},
    {"ping", json_message_type::ping},
    {"pong", json_message_type::pong},
    {"login_request", json_message_type::login_request},
    {"login_response", json_message_type::login_response},
    {"logout_request", json_message_type::logout_request},
    {"logout_response", json_message_type::logout_response},
    {"create_account_request", json_message_type::create_account_request},
    {"create_account_response", json_message_type::create_account_response},
    {"get_characters_request", json_message_type::get_characters_request},
    {"get_characters_response", json_message_type::get_characters_response},
    {"create_character_request", json_message_type::create_character_request},
    {"create_character_response", json_message_type::create_character_response},
    {"delete_character_request", json_message_type::delete_character_request},
    {"delete_character_response", json_message_type::delete_character_response},
    {"enter_game_request", json_message_type::enter_game_request},
    {"enter_game_response", json_message_type::enter_game_response},
    {"character_data", json_message_type::character_data},
    {"inventory_data", json_message_type::inventory_data},
    {"equipment_data", json_message_type::equipment_data},
    {"skills_data", json_message_type::skills_data},
    {"entity_spawn", json_message_type::entity_spawn},
    {"entity_despawn", json_message_type::entity_despawn},
    {"player_move_request", json_message_type::player_move_request},
    {"player_move_response", json_message_type::player_move_response},
    {"player_stop_request", json_message_type::player_stop_request},
    {"player_stop_response", json_message_type::player_stop_response},
    {"player_position_update", json_message_type::player_position_update},
    {"player_attack_request", json_message_type::player_attack_request},
    {"player_attack_response", json_message_type::player_attack_response},
    {"combat_attack_broadcast", json_message_type::combat_attack_broadcast},
    {"entity_hp_update", json_message_type::entity_hp_update},
    {"entity_death", json_message_type::entity_death},
    {"combat_effect", json_message_type::combat_effect},
    {"player_magic_request", json_message_type::player_magic_request},
    {"player_magic_response", json_message_type::player_magic_response},
    {"player_skill_request", json_message_type::player_skill_request},
    {"player_skill_response", json_message_type::player_skill_response},
    {"skill_update", json_message_type::skill_update},
    {"skill_progress", json_message_type::skill_progress},
    {"player_pickup_request", json_message_type::player_pickup_request},
    {"player_pickup_response", json_message_type::player_pickup_response},
    {"player_interact_request", json_message_type::player_interact_request},
    {"player_interact_response", json_message_type::player_interact_response},
    {"chat_message", json_message_type::chat_message},
    {"chat_message_broadcast", json_message_type::chat_message_broadcast},
    {"command_request", json_message_type::command_request},
    {"command_response", json_message_type::command_response},
    {"map_teleporters", json_message_type::map_teleporters},
    {"teleporter_update", json_message_type::teleporter_update},
    {"player_teleport", json_message_type::player_teleport},
    {"set_view_range", json_message_type::set_view_range},
    {"set_render_mode", json_message_type::set_render_mode},
    {"view_range_update", json_message_type::view_range_update},
    {"npc_spawn", json_message_type::npc_spawn},
    {"npc_despawn", json_message_type::npc_despawn},
    {"npc_move", json_message_type::npc_move},
    {"npc_attack", json_message_type::npc_attack},
    {"ground_item_spawn", json_message_type::ground_item_spawn},
    {"ground_item_removed", json_message_type::ground_item_removed},
    {"player_death_info", json_message_type::player_death_info},
    {"hunger_update", json_message_type::hunger_update},
    {"environment_update", json_message_type::environment_update},
    {"entity_info_request", json_message_type::entity_info_request},
    {"entity_info_response", json_message_type::entity_info_response},
    {"player_equip_request", json_message_type::player_equip_request},
    {"player_equip_response", json_message_type::player_equip_response},
    {"player_unequip_request", json_message_type::player_unequip_request},
    {"player_unequip_response", json_message_type::player_unequip_response},
    {"equipment_change_broadcast", json_message_type::equipment_change_broadcast},
    {"stat_update", json_message_type::stat_update},
    {"spell_list_update", json_message_type::spell_list_update},
    {"experience_update", json_message_type::experience_update},
    {"dynamic_object_spawn", json_message_type::dynamic_object_spawn},
    {"dynamic_object_removed", json_message_type::dynamic_object_removed},
    {"shop_buy_request", json_message_type::shop_buy_request},
    {"shop_buy_response", json_message_type::shop_buy_response},
    {"shop_sell_request", json_message_type::shop_sell_request},
    {"shop_sell_response", json_message_type::shop_sell_response},
    {"shop_sell_confirm_request", json_message_type::shop_sell_confirm_request},
    {"shop_sell_confirm_response", json_message_type::shop_sell_confirm_response},
    {"shop_repair_request", json_message_type::shop_repair_request},
    {"shop_repair_response", json_message_type::shop_repair_response},
    {"shop_repair_confirm_request", json_message_type::shop_repair_confirm_request},
    {"shop_repair_confirm_response", json_message_type::shop_repair_confirm_response},
    {"bank_deposit_request", json_message_type::bank_deposit_request},
    {"bank_deposit_response", json_message_type::bank_deposit_response},
    {"bank_withdraw_request", json_message_type::bank_withdraw_request},
    {"bank_withdraw_response", json_message_type::bank_withdraw_response},
    {"dialog_choice_request", json_message_type::dialog_choice_request},
    {"dialog_choice_response", json_message_type::dialog_choice_response},
    {"party_invite_request", json_message_type::party_invite_request},
    {"party_invite_response", json_message_type::party_invite_response},
    {"party_invite_notice", json_message_type::party_invite_notice},
    {"party_accept_request", json_message_type::party_accept_request},
    {"party_accept_response", json_message_type::party_accept_response},
    {"party_leave_request", json_message_type::party_leave_request},
    {"party_leave_response", json_message_type::party_leave_response},
    {"party_update", json_message_type::party_update},
    {"manufacture_list_request", json_message_type::manufacture_list_request},
    {"manufacture_list_response", json_message_type::manufacture_list_response},
    {"manufacture_request", json_message_type::manufacture_request},
    {"manufacture_response", json_message_type::manufacture_response},
    {"alchemy_list_request", json_message_type::alchemy_list_request},
    {"alchemy_list_response", json_message_type::alchemy_list_response},
    {"alchemy_request", json_message_type::alchemy_request},
    {"alchemy_response", json_message_type::alchemy_response},
    {"mine_request", json_message_type::mine_request},
    {"mine_response", json_message_type::mine_response},
    {"mineral_spawn", json_message_type::mineral_spawn},
    {"mineral_despawn", json_message_type::mineral_despawn},
    {"fish_skill_request", json_message_type::fish_skill_request},
    {"fish_skill_response", json_message_type::fish_skill_response},
    {"fish_engaged", json_message_type::fish_engaged},
    {"fish_chance_update", json_message_type::fish_chance_update},
    {"fish_catch_request", json_message_type::fish_catch_request},
    {"fish_catch_response", json_message_type::fish_catch_response},
    {"fish_spawn_broadcast", json_message_type::fish_spawn_broadcast},
    {"fish_despawn_broadcast", json_message_type::fish_despawn_broadcast},
    {"respawn_request", json_message_type::respawn_request},
    {"respawn_response", json_message_type::respawn_response},
    {"enter_admin_mode_request", json_message_type::enter_admin_mode_request},
    {"enter_admin_mode_response", json_message_type::enter_admin_mode_response},
    {"admin_server_stats_request", json_message_type::admin_server_stats_request},
    {"admin_server_stats_response", json_message_type::admin_server_stats_response},
    {"admin_list_players_request", json_message_type::admin_list_players_request},
    {"admin_list_players_response", json_message_type::admin_list_players_response},
    {"admin_get_player_request", json_message_type::admin_get_player_request},
    {"admin_get_player_response", json_message_type::admin_get_player_response},
    {"admin_kick_player_request", json_message_type::admin_kick_player_request},
    {"admin_kick_player_response", json_message_type::admin_kick_player_response},
    {"admin_ban_player_request", json_message_type::admin_ban_player_request},
    {"admin_ban_player_response", json_message_type::admin_ban_player_response},
    {"admin_teleport_player_request", json_message_type::admin_teleport_player_request},
    {"admin_teleport_player_response", json_message_type::admin_teleport_player_response},
    {"admin_modify_player_request", json_message_type::admin_modify_player_request},
    {"admin_modify_player_response", json_message_type::admin_modify_player_response},
    {"admin_list_maps_request", json_message_type::admin_list_maps_request},
    {"admin_list_maps_response", json_message_type::admin_list_maps_response},
    {"admin_get_map_request", json_message_type::admin_get_map_request},
    {"admin_get_map_response", json_message_type::admin_get_map_response},
    {"admin_spawn_npc_request", json_message_type::admin_spawn_npc_request},
    {"admin_spawn_npc_response", json_message_type::admin_spawn_npc_response},
    {"admin_kill_npc_request", json_message_type::admin_kill_npc_request},
    {"admin_kill_npc_response", json_message_type::admin_kill_npc_response},
    {"admin_get_inventory_request", json_message_type::admin_get_inventory_request},
    {"admin_get_inventory_response", json_message_type::admin_get_inventory_response},
    {"admin_give_item_request", json_message_type::admin_give_item_request},
    {"admin_give_item_response", json_message_type::admin_give_item_response},
    {"admin_remove_item_request", json_message_type::admin_remove_item_request},
    {"admin_remove_item_response", json_message_type::admin_remove_item_response},
    {"admin_list_guilds_request", json_message_type::admin_list_guilds_request},
    {"admin_list_guilds_response", json_message_type::admin_list_guilds_response},
    {"admin_get_guild_request", json_message_type::admin_get_guild_request},
    {"admin_get_guild_response", json_message_type::admin_get_guild_response},
    {"admin_get_account_request", json_message_type::admin_get_account_request},
    {"admin_get_account_response", json_message_type::admin_get_account_response},
    {"admin_unban_player_request", json_message_type::admin_unban_player_request},
    {"admin_unban_player_response", json_message_type::admin_unban_player_response},
    {"admin_subscribe_map_request", json_message_type::admin_subscribe_map_request},
    {"admin_subscribe_map_response", json_message_type::admin_subscribe_map_response},
    {"admin_get_map_data_request", json_message_type::admin_get_map_data_request},
    {"admin_get_map_data_response", json_message_type::admin_get_map_data_response},
    {"admin_subscribe_player_request", json_message_type::admin_subscribe_player_request},
    {"admin_subscribe_player_response", json_message_type::admin_subscribe_player_response},
    {"admin_unsubscribe_request", json_message_type::admin_unsubscribe_request},
    {"admin_unsubscribe_response", json_message_type::admin_unsubscribe_response},
    {"admin_spectator_init", json_message_type::admin_spectator_init},
    {"admin_player_connected", json_message_type::admin_player_connected},
    {"admin_player_disconnected", json_message_type::admin_player_disconnected},
    {"admin_chat_log", json_message_type::admin_chat_log},
    {"admin_broadcast_request", json_message_type::admin_broadcast_request},
    {"admin_broadcast_response", json_message_type::admin_broadcast_response},
    {"admin_mute_player_request", json_message_type::admin_mute_player_request},
    {"admin_mute_player_response", json_message_type::admin_mute_player_response},
    {"admin_unmute_player_request", json_message_type::admin_unmute_player_request},
    {"admin_unmute_player_response", json_message_type::admin_unmute_player_response},
    {"admin_list_item_templates_request", json_message_type::admin_list_item_templates_request},
    {"admin_list_item_templates_response", json_message_type::admin_list_item_templates_response},
    {"admin_get_item_template_request", json_message_type::admin_get_item_template_request},
    {"admin_get_item_template_response", json_message_type::admin_get_item_template_response},
    {"admin_list_npc_templates_request", json_message_type::admin_list_npc_templates_request},
    {"admin_list_npc_templates_response", json_message_type::admin_list_npc_templates_response},
    {"admin_get_npc_template_request", json_message_type::admin_get_npc_template_request},
    {"admin_get_npc_template_response", json_message_type::admin_get_npc_template_response},
    {"admin_get_war_status_request", json_message_type::admin_get_war_status_request},
    {"admin_get_war_status_response", json_message_type::admin_get_war_status_response},
    {"admin_list_parties_request", json_message_type::admin_list_parties_request},
    {"admin_list_parties_response", json_message_type::admin_list_parties_response},
    {"admin_search_players_request", json_message_type::admin_search_players_request},
    {"admin_search_players_response", json_message_type::admin_search_players_response},
    {"admin_get_audit_log_request", json_message_type::admin_get_audit_log_request},
    {"admin_get_audit_log_response", json_message_type::admin_get_audit_log_response},
    {"admin_get_config_request", json_message_type::admin_get_config_request},
    {"admin_get_config_response", json_message_type::admin_get_config_response},
    {"admin_set_config_request", json_message_type::admin_set_config_request},
    {"admin_set_config_response", json_message_type::admin_set_config_response},
    {"admin_reload_config_request", json_message_type::admin_reload_config_request},
    {"admin_reload_config_response", json_message_type::admin_reload_config_response},
    {"admin_list_scheduled_tasks_request", json_message_type::admin_list_scheduled_tasks_request},
    {"admin_list_scheduled_tasks_response", json_message_type::admin_list_scheduled_tasks_response},
    {"admin_cancel_scheduled_task_request", json_message_type::admin_cancel_scheduled_task_request},
    {"admin_cancel_scheduled_task_response", json_message_type::admin_cancel_scheduled_task_response},
    {"admin_run_query_request", json_message_type::admin_run_query_request},
    {"admin_run_query_response", json_message_type::admin_run_query_response},
    {"admin_list_map_npcs_request", json_message_type::admin_list_map_npcs_request},
    {"admin_list_map_npcs_response", json_message_type::admin_list_map_npcs_response},
    {"admin_list_map_ground_items_request", json_message_type::admin_list_map_ground_items_request},
    {"admin_list_map_ground_items_response", json_message_type::admin_list_map_ground_items_response},
    {"admin_remove_ground_item_request", json_message_type::admin_remove_ground_item_request},
    {"admin_remove_ground_item_response", json_message_type::admin_remove_ground_item_response},
    {"admin_guild_action_request", json_message_type::admin_guild_action_request},
    {"admin_guild_action_response", json_message_type::admin_guild_action_response},
    {"admin_message_player_request", json_message_type::admin_message_player_request},
    {"admin_message_player_response", json_message_type::admin_message_player_response},
    {"admin_set_environment_request", json_message_type::admin_set_environment_request},
    {"admin_set_environment_response", json_message_type::admin_set_environment_response},
    {"admin_shutdown_server_request", json_message_type::admin_shutdown_server_request},
    {"admin_shutdown_server_response", json_message_type::admin_shutdown_server_response},
    {"admin_modify_skills_request", json_message_type::admin_modify_skills_request},
    {"admin_modify_skills_response", json_message_type::admin_modify_skills_response},
    {"admin_modify_spells_request", json_message_type::admin_modify_spells_request},
    {"admin_modify_spells_response", json_message_type::admin_modify_spells_response},
    {"admin_get_player_quests_request", json_message_type::admin_get_player_quests_request},
    {"admin_get_player_quests_response", json_message_type::admin_get_player_quests_response},
    {"admin_quest_action_request", json_message_type::admin_quest_action_request},
    {"admin_quest_action_response", json_message_type::admin_quest_action_response},
    {"admin_remove_effects_request", json_message_type::admin_remove_effects_request},
    {"admin_remove_effects_response", json_message_type::admin_remove_effects_response},
    {"admin_create_account_request", json_message_type::admin_create_account_request},
    {"admin_create_account_response", json_message_type::admin_create_account_response},
    {"admin_change_password_request", json_message_type::admin_change_password_request},
    {"admin_change_password_response", json_message_type::admin_change_password_response},
    {"admin_set_admin_level_request", json_message_type::admin_set_admin_level_request},
    {"admin_set_admin_level_response", json_message_type::admin_set_admin_level_response},
    {"admin_list_spawn_points_request", json_message_type::admin_list_spawn_points_request},
    {"admin_list_spawn_points_response", json_message_type::admin_list_spawn_points_response},
    {"admin_list_spell_templates_request", json_message_type::admin_list_spell_templates_request},
    {"admin_list_spell_templates_response", json_message_type::admin_list_spell_templates_response},
    {"admin_get_spell_template_request", json_message_type::admin_get_spell_template_request},
    {"admin_get_spell_template_response", json_message_type::admin_get_spell_template_response},
    {"admin_set_maintenance_mode_request", json_message_type::admin_set_maintenance_mode_request},
    {"admin_set_maintenance_mode_response", json_message_type::admin_set_maintenance_mode_response},
    {"admin_create_character_request_admin", json_message_type::admin_create_character_request_admin},
    {"admin_create_character_response_admin", json_message_type::admin_create_character_response_admin},
    {"admin_delete_character_request_admin", json_message_type::admin_delete_character_request_admin},
    {"admin_delete_character_response_admin", json_message_type::admin_delete_character_response_admin},
    {"admin_manage_ip_bans_request", json_message_type::admin_manage_ip_bans_request},
    {"admin_manage_ip_bans_response", json_message_type::admin_manage_ip_bans_response},
    {"admin_start_task_request", json_message_type::admin_start_task_request},
    {"admin_start_task_response", json_message_type::admin_start_task_response},
    {"admin_perf_stats_request", json_message_type::admin_perf_stats_request},
    {"admin_perf_stats_response", json_message_type::admin_perf_stats_response},
    {"friend_request_send_request", json_message_type::friend_request_send_request},
    {"friend_request_send_response", json_message_type::friend_request_send_response},
    {"friend_request_accept_request", json_message_type::friend_request_accept_request},
    {"friend_request_accept_response", json_message_type::friend_request_accept_response},
    {"friend_request_decline_request", json_message_type::friend_request_decline_request},
    {"friend_request_decline_response", json_message_type::friend_request_decline_response},
    {"friend_request_cancel_request", json_message_type::friend_request_cancel_request},
    {"friend_request_cancel_response", json_message_type::friend_request_cancel_response},
    {"friend_remove_request", json_message_type::friend_remove_request},
    {"friend_remove_response", json_message_type::friend_remove_response},
    {"friend_block_request", json_message_type::friend_block_request},
    {"friend_block_response", json_message_type::friend_block_response},
    {"friend_unblock_request", json_message_type::friend_unblock_request},
    {"friend_unblock_response", json_message_type::friend_unblock_response},
    {"friend_list_request", json_message_type::friend_list_request},
    {"friend_list_response", json_message_type::friend_list_response},
    {"friend_request_notification", json_message_type::friend_request_notification},
    {"friend_accepted_notification", json_message_type::friend_accepted_notification},
    {"friend_online_notification", json_message_type::friend_online_notification},
    {"friend_offline_notification", json_message_type::friend_offline_notification},
    // Crusade warfare
    {"crusade_started", json_message_type::crusade_started},
    {"crusade_ended", json_message_type::crusade_ended},
    {"crusade_status_update", json_message_type::crusade_status_update},
    {"select_duty_request", json_message_type::select_duty_request},
    {"select_duty_response", json_message_type::select_duty_response},
    {"crusade_strike_point_update", json_message_type::crusade_strike_point_update},
    {"crusade_meteor_warning", json_message_type::crusade_meteor_warning},
    {"crusade_meteor_hit", json_message_type::crusade_meteor_hit},
    {"crusade_meteor_result", json_message_type::crusade_meteor_result},
    {"crusade_mana_update", json_message_type::crusade_mana_update},
    {"crusade_construction_point_update", json_message_type::crusade_construction_point_update},
    {"summon_war_unit_request", json_message_type::summon_war_unit_request},
    {"summon_war_unit_response", json_message_type::summon_war_unit_response},
    {"crusade_map_status", json_message_type::crusade_map_status},
    {"heldenian_started", json_message_type::heldenian_started},
    {"heldenian_ended", json_message_type::heldenian_ended},
    {"heldenian_status_update", json_message_type::heldenian_status_update},
    {"apocalypse_started", json_message_type::apocalypse_started},
    {"apocalypse_ended", json_message_type::apocalypse_ended},
    {"apocalypse_gate_open", json_message_type::apocalypse_gate_open},
    {"force_recall_timer", json_message_type::force_recall_timer},
    {"force_recall_execute", json_message_type::force_recall_execute},
    {"crusade_reward_summary", json_message_type::crusade_reward_summary},
    {"admin_start_war_request", json_message_type::admin_start_war_request},
    {"admin_start_war_response", json_message_type::admin_start_war_response},
    {"admin_end_war_request", json_message_type::admin_end_war_request},
    {"admin_end_war_response", json_message_type::admin_end_war_response},
    {"admin_war_history_request", json_message_type::admin_war_history_request},
    {"admin_war_history_response", json_message_type::admin_war_history_response},
    {"admin_war_participants_request", json_message_type::admin_war_participants_request},
    {"admin_war_participants_response", json_message_type::admin_war_participants_response},
    {"admin_item_log_request", json_message_type::admin_item_log_request},
    {"admin_item_log_response", json_message_type::admin_item_log_response},
    {"crusade_mp_restore", json_message_type::crusade_mp_restore},
    {"crusade_set_guild_teleport_request", json_message_type::crusade_set_guild_teleport_request},
    {"crusade_set_guild_teleport_response", json_message_type::crusade_set_guild_teleport_response},
    {"crusade_guild_teleport_request", json_message_type::crusade_guild_teleport_request},
    {"crusade_guild_teleport_response", json_message_type::crusade_guild_teleport_response},
    {"guild_create_request", json_message_type::guild_create_request},
    {"guild_create_response", json_message_type::guild_create_response},
    {"guild_disband_request", json_message_type::guild_disband_request},
    {"guild_disband_response", json_message_type::guild_disband_response},
    {"guild_leave_request", json_message_type::guild_leave_request},
    {"guild_leave_response", json_message_type::guild_leave_response},
    {"guild_kick_request", json_message_type::guild_kick_request},
    {"guild_kick_response", json_message_type::guild_kick_response},
    {"guild_invite_request", json_message_type::guild_invite_request},
    {"guild_invite_response", json_message_type::guild_invite_response},
    {"guild_invite_received", json_message_type::guild_invite_received},
    {"guild_invite_respond_request", json_message_type::guild_invite_respond_request},
    {"guild_invite_respond_response", json_message_type::guild_invite_respond_response},
    {"guild_promote_request", json_message_type::guild_promote_request},
    {"guild_promote_response", json_message_type::guild_promote_response},
    {"guild_demote_request", json_message_type::guild_demote_request},
    {"guild_demote_response", json_message_type::guild_demote_response},
    {"guild_set_motd_request", json_message_type::guild_set_motd_request},
    {"guild_set_motd_response", json_message_type::guild_set_motd_response},
    {"guild_info_request", json_message_type::guild_info_request},
    {"guild_info_response", json_message_type::guild_info_response},
    {"guild_update", json_message_type::guild_update},
    {"player_use_item_request", json_message_type::player_use_item_request},
    {"player_use_item_response", json_message_type::player_use_item_response},
    {"available_commands", json_message_type::available_commands},
    {"command_availability_update", json_message_type::command_availability_update},
    {"stat_point_request", json_message_type::stat_point_request},
    {"stat_point_response", json_message_type::stat_point_response},
    {"combat_mode_change_request", json_message_type::combat_mode_change_request},
    {"combat_mode_change_response", json_message_type::combat_mode_change_response},
    {"combat_mode_change_broadcast", json_message_type::combat_mode_change_broadcast},
    {"player_action_broadcast", json_message_type::player_action_broadcast},
    {"item_upgrade_request", json_message_type::item_upgrade_request},
    {"item_upgrade_response", json_message_type::item_upgrade_response},
    {"activate_ability_request", json_message_type::activate_ability_request},
    {"activate_ability_response", json_message_type::activate_ability_response},
    {"special_ability_status", json_message_type::special_ability_status},
    {"inventory_reposition_request", json_message_type::inventory_reposition_request},
    {"player_drop_item_request", json_message_type::player_drop_item_request},
    {"player_drop_item_response", json_message_type::player_drop_item_response},
    {"inventory_item_update", json_message_type::inventory_item_update},
    {"inventory_item_removed", json_message_type::inventory_item_removed},
    {"inventory_weight_update", json_message_type::inventory_weight_update},
    {"gold_update", json_message_type::gold_update},
    {"bank_slot_update", json_message_type::bank_slot_update},
    // v2 state update messages
    {"inventory_item_add", json_message_type::inventory_item_add},
    {"inventory_item_delta", json_message_type::inventory_item_delta},
    {"inventory_gold_update", json_message_type::inventory_gold_update},
    {"force_unequip", json_message_type::force_unequip},
    {"equipment_change", json_message_type::equipment_change},
    {"bank_slot_cleared", json_message_type::bank_slot_cleared},
    {"ability_activated", json_message_type::ability_activated},
    {"ability_expired", json_message_type::ability_expired},
    // v2 action messages
    {"inventory_reposition", json_message_type::inventory_reposition},
    {"equip_request", json_message_type::equip_request},
    {"equip_result", json_message_type::equip_result},
    {"unequip_request", json_message_type::unequip_request},
    {"unequip_result", json_message_type::unequip_result},
    {"pickup_request", json_message_type::pickup_request},
    {"pickup_result", json_message_type::pickup_result},
    {"drop_request", json_message_type::drop_request},
    {"drop_result", json_message_type::drop_result},
    {"use_item_request", json_message_type::use_item_request},
    {"use_item_result", json_message_type::use_item_result},
    {"upgrade_request", json_message_type::upgrade_request},
    {"upgrade_result", json_message_type::upgrade_result},
    {"shop_buy_request_v2", json_message_type::shop_buy_request_v2},
    {"shop_buy_result", json_message_type::shop_buy_result},
    {"shop_sell_request_v2", json_message_type::shop_sell_request_v2},
    {"shop_sell_result", json_message_type::shop_sell_result},
    {"shop_repair_request_v2", json_message_type::shop_repair_request_v2},
    {"shop_repair_result", json_message_type::shop_repair_result},
    {"bank_deposit_request_v2", json_message_type::bank_deposit_request_v2},
    {"bank_deposit_result", json_message_type::bank_deposit_result},
    {"bank_withdraw_request_v2", json_message_type::bank_withdraw_request_v2},
    {"bank_withdraw_result", json_message_type::bank_withdraw_result},
    {"bank_reposition_request", json_message_type::bank_reposition_request},
    {"bank_reposition_result", json_message_type::bank_reposition_result},
    {"activate_ability_request_v2", json_message_type::activate_ability_request_v2},
    {"activate_ability_failed", json_message_type::activate_ability_failed},
    {"trade_request", json_message_type::trade_request},
    {"trade_invite", json_message_type::trade_invite},
    {"trade_accept", json_message_type::trade_accept},
    {"trade_decline", json_message_type::trade_decline},
    {"trade_opened", json_message_type::trade_opened},
    {"trade_add_item", json_message_type::trade_add_item},
    {"trade_remove_item", json_message_type::trade_remove_item},
    {"trade_set_gold", json_message_type::trade_set_gold},
    {"trade_update", json_message_type::trade_update},
    {"trade_lock", json_message_type::trade_lock},
    {"trade_lock_status", json_message_type::trade_lock_status},
    {"trade_confirm", json_message_type::trade_confirm},
    {"trade_complete", json_message_type::trade_complete},
    {"trade_cancel", json_message_type::trade_cancel},
    {"trade_canceled", json_message_type::trade_canceled},
    {"shop_open", json_message_type::shop_open},
    {"bank_open", json_message_type::bank_open},
    {"set_loot_rule", json_message_type::set_loot_rule},
    {"loot_rule_changed", json_message_type::loot_rule_changed},
    {"loot_roll", json_message_type::loot_roll},
    {"loot_pass", json_message_type::loot_pass},
    {"loot_assign", json_message_type::loot_assign},
    {"loot_available", json_message_type::loot_available},
    {"loot_roll_result", json_message_type::loot_roll_result},
    {"loot_awarded", json_message_type::loot_awarded},
    {"loot_expired", json_message_type::loot_expired}};

} // namespace

auto parse_message_type(std::string_view type_str) -> json_message_type
{
    auto it = type_map.find(std::string(type_str));
    if (it != type_map.end())
    {
        return it->second;
    }
    return json_message_type::unknown;
}

auto json_message::to_json() const -> nlohmann::json
{
    return nlohmann::json{{"type", std::string(to_string(type))}, {"seq", seq}, {"data", data}};
}

auto json_message::from_json(const nlohmann::json& j) -> result<json_message, std::string>
{
    try
    {
        json_message msg;

        if (!j.contains("type") || !j["type"].is_string())
        {
            return result<json_message, std::string>::err("Missing or invalid 'type' field");
        }

        msg.raw_type = j["type"].get<std::string>();
        msg.type = parse_message_type(msg.raw_type);

        if (j.contains("seq"))
        {
            if (j["seq"].is_number_unsigned())
            {
                msg.seq = j["seq"].get<uint32_t>();
            }
            else if (j["seq"].is_number_integer())
            {
                msg.seq = static_cast<uint32_t>(j["seq"].get<int>());
            }
        }

        if (j.contains("data"))
        {
            msg.data = j["data"];
        }

        return result<json_message, std::string>::ok(std::move(msg));
    }
    catch (const nlohmann::json::exception& e)
    {
        return result<json_message, std::string>::err(std::string("JSON parse error: ") + e.what());
    }
}

auto json_message::parse(std::string_view json_str) -> result<json_message, std::string>
{
    try
    {
        auto j = nlohmann::json::parse(json_str);
        return from_json(j);
    }
    catch (const nlohmann::json::parse_error& e)
    {
        return result<json_message, std::string>::err(std::string("JSON parse error: ") + e.what());
    }
}

// Request data parsers

auto login_request_data::from_json(const nlohmann::json& j) -> result<login_request_data, std::string>
{
    try
    {
        login_request_data data;

        if (!j.contains("username") || !j["username"].is_string())
        {
            return result<login_request_data, std::string>::err("Missing or invalid 'username' field");
        }
        data.username = j["username"].get<std::string>();

        // Accept either password or forum_token (at least one required)
        if (j.contains("password") && j["password"].is_string())
        {
            data.password = j["password"].get<std::string>();
        }
        if (j.contains("forum_token") && j["forum_token"].is_string())
        {
            data.forum_token = j["forum_token"].get<std::string>();
        }

        if (data.password.empty() && data.forum_token.empty())
        {
            return result<login_request_data, std::string>::err("Must provide either 'password' or 'forum_token'");
        }

        return result<login_request_data, std::string>::ok(std::move(data));
    }
    catch (const nlohmann::json::exception& e)
    {
        return result<login_request_data, std::string>::err(std::string("Parse error: ") + e.what());
    }
}

auto create_account_request_data::from_json(const nlohmann::json& j) -> result<create_account_request_data, std::string>
{
    try
    {
        create_account_request_data data;

        if (!j.contains("username") || !j["username"].is_string())
        {
            return result<create_account_request_data, std::string>::err("Missing or invalid 'username' field");
        }
        data.username = j["username"].get<std::string>();

        if (!j.contains("password") || !j["password"].is_string())
        {
            return result<create_account_request_data, std::string>::err("Missing or invalid 'password' field");
        }
        data.password = j["password"].get<std::string>();

        return result<create_account_request_data, std::string>::ok(std::move(data));
    }
    catch (const nlohmann::json::exception& e)
    {
        return result<create_account_request_data, std::string>::err(std::string("Parse error: ") + e.what());
    }
}

auto create_character_request_data::from_json(const nlohmann::json& j)
    -> result<create_character_request_data, std::string>
{
    try
    {
        create_character_request_data data;

        if (!j.contains("name") || !j["name"].is_string())
        {
            return result<create_character_request_data, std::string>::err("Missing or invalid 'name' field");
        }
        data.name = j["name"].get<std::string>();

        // Optional fields with defaults - use safe parsing with range validation
        data.class_type = safe_int16(j, "class_type", 0);
        data.nation = safe_int16(j, "nation", 0);
        data.gender = safe_int16(j, "gender", 1);
        data.hair_style = safe_int16(j, "hair_style", 0);
        data.hair_color = safe_int16(j, "hair_color", 0);
        data.skin_color = safe_int16(j, "skin_color", 0);
        data.underwear_color = safe_int16(j, "underwear_color", 0);

        // Optional stats
        data.strength = safe_int16(j, "strength", 10);
        data.dexterity = safe_int16(j, "dexterity", 10);
        data.vitality = safe_int16(j, "vitality", 10);
        data.intelligence = safe_int16(j, "intelligence", 10);
        data.magic = safe_int16(j, "magic", 10);
        data.charisma = safe_int16(j, "charisma", 10);

        return result<create_character_request_data, std::string>::ok(std::move(data));
    }
    catch (const nlohmann::json::exception& e)
    {
        return result<create_character_request_data, std::string>::err(std::string("Parse error: ") + e.what());
    }
}

auto create_character_request_data::to_create_info() const -> auth::character_create_info
{
    return auth::character_create_info{.name = name,
                                       .class_type = class_type,
                                       .nation = nation,
                                       .gender = gender,
                                       .hair_style = hair_style,
                                       .hair_color = hair_color,
                                       .skin_color = skin_color,
                                       .underwear_color = underwear_color,
                                       .strength = strength,
                                       .dexterity = dexterity,
                                       .vitality = vitality,
                                       .intelligence = intelligence,
                                       .magic = magic,
                                       .charisma = charisma};
}

auto delete_character_request_data::from_json(const nlohmann::json& j)
    -> result<delete_character_request_data, std::string>
{
    try
    {
        delete_character_request_data data;

        if (!j.contains("character_id") || !j["character_id"].is_number())
        {
            return result<delete_character_request_data, std::string>::err("Missing or invalid 'character_id' field");
        }
        data.character_id = j["character_id"].get<uint32_t>();

        return result<delete_character_request_data, std::string>::ok(std::move(data));
    }
    catch (const nlohmann::json::exception& e)
    {
        return result<delete_character_request_data, std::string>::err(std::string("Parse error: ") + e.what());
    }
}

auto enter_game_request_data::from_json(const nlohmann::json& j) -> result<enter_game_request_data, std::string>
{
    try
    {
        enter_game_request_data data;

        if (!j.contains("character_id") || !j["character_id"].is_number())
        {
            return result<enter_game_request_data, std::string>::err("Missing or invalid 'character_id' field");
        }
        data.character_id = j["character_id"].get<uint32_t>();

        // Optional: force_disconnect to kick existing session
        if (j.contains("force_disconnect") && j["force_disconnect"].is_boolean())
        {
            data.force_disconnect = j["force_disconnect"].get<bool>();
        }

        // Optional: screen resolution for visibility calculation (use safe parsing)
        data.screen_width = safe_int16(j, "screen_width", 800);
        data.screen_height = safe_int16(j, "screen_height", 600);

        return result<enter_game_request_data, std::string>::ok(std::move(data));
    }
    catch (const nlohmann::json::exception& e)
    {
        return result<enter_game_request_data, std::string>::err(std::string("Parse error: ") + e.what());
    }
}

auto set_view_range_request_data::from_json(const nlohmann::json& j) -> result<set_view_range_request_data, std::string>
{
    try
    {
        set_view_range_request_data data;

        // Use safe parsing with reasonable defaults
        data.screen_width = safe_int16(j, "screen_width", 800);
        data.screen_height = safe_int16(j, "screen_height", 600);

        return result<set_view_range_request_data, std::string>::ok(std::move(data));
    }
    catch (const nlohmann::json::exception& e)
    {
        return result<set_view_range_request_data, std::string>::err(std::string("Parse error: ") + e.what());
    }
}

auto player_move_request_data::from_json(const nlohmann::json& j) -> result<player_move_request_data, std::string>
{
    try
    {
        player_move_request_data data;

        auto x_opt = safe_int16_required(j, "x");
        if (!x_opt.has_value())
        {
            return result<player_move_request_data, std::string>::err("Missing or invalid 'x' field");
        }
        data.x = *x_opt;

        auto y_opt = safe_int16_required(j, "y");
        if (!y_opt.has_value())
        {
            return result<player_move_request_data, std::string>::err("Missing or invalid 'y' field");
        }
        data.y = *y_opt;

        // Direction is required but clamped to 0-7
        if (!j.contains("direction") || !j["direction"].is_number())
        {
            return result<player_move_request_data, std::string>::err("Missing or invalid 'direction' field");
        }
        data.direction = safe_direction(j, "direction", 0);

        if (j.contains("is_running") && j["is_running"].is_boolean())
        {
            data.is_running = j["is_running"].get<bool>();
        }

        if (j.contains("timestamp") && j["timestamp"].is_number())
        {
            data.timestamp = j["timestamp"].get<uint64_t>();
        }

        // Optional mouse destination coordinates
        if (j.contains("dest_x") && j["dest_x"].is_number())
        {
            data.dest_x = safe_int16(j, "dest_x", 0);
        }
        if (j.contains("dest_y") && j["dest_y"].is_number())
        {
            data.dest_y = safe_int16(j, "dest_y", 0);
        }

        return result<player_move_request_data, std::string>::ok(std::move(data));
    }
    catch (const nlohmann::json::exception& e)
    {
        return result<player_move_request_data, std::string>::err(std::string("Parse error: ") + e.what());
    }
}

auto player_stop_request_data::from_json(const nlohmann::json& j) -> result<player_stop_request_data, std::string>
{
    try
    {
        player_stop_request_data data;

        auto x_opt = safe_int16_required(j, "x");
        if (!x_opt.has_value())
        {
            return result<player_stop_request_data, std::string>::err("Missing or invalid 'x' field");
        }
        data.x = *x_opt;

        auto y_opt = safe_int16_required(j, "y");
        if (!y_opt.has_value())
        {
            return result<player_stop_request_data, std::string>::err("Missing or invalid 'y' field");
        }
        data.y = *y_opt;

        // Direction is optional but clamped to 0-7 if present
        if (j.contains("direction") && j["direction"].is_number())
        {
            data.direction = safe_direction(j, "direction", 0);
        }

        if (j.contains("timestamp") && j["timestamp"].is_number())
        {
            data.timestamp = j["timestamp"].get<uint64_t>();
        }

        return result<player_stop_request_data, std::string>::ok(std::move(data));
    }
    catch (const nlohmann::json::exception& e)
    {
        return result<player_stop_request_data, std::string>::err(std::string("Parse error: ") + e.what());
    }
}

namespace
{
auto parse_attack_type(const nlohmann::json& j) -> attack_type
{
    if (j.is_number())
    {
        auto val = j.get<int>();
        if (val >= 0 && val <= 2)
        {
            return static_cast<attack_type>(val);
        }
    }
    else if (j.is_string())
    {
        auto str = j.get<std::string>();
        if (str == "regular")
            return attack_type::regular;
        if (str == "dash")
            return attack_type::dash;
        if (str == "ranged" || str == "super")
            return attack_type::ranged;
    }
    return attack_type::regular;
}

auto parse_target_type(const nlohmann::json& j) -> target_type
{
    if (j.is_number())
    {
        auto val = j.get<int>();
        if (val >= 0 && val <= 4)
        {
            return static_cast<target_type>(val);
        }
    }
    else if (j.is_string())
    {
        auto str = j.get<std::string>();
        if (str == "none")
            return target_type::none;
        if (str == "player")
            return target_type::player;
        if (str == "npc")
            return target_type::npc;
        if (str == "ground")
            return target_type::ground;
        if (str == "item")
            return target_type::item;
    }
    return target_type::none;
}
} // namespace

auto player_attack_request_data::from_json(const nlohmann::json& j) -> result<player_attack_request_data, std::string>
{
    try
    {
        player_attack_request_data data;

        auto x_opt = safe_int16_required(j, "x");
        if (!x_opt.has_value())
        {
            return result<player_attack_request_data, std::string>::err("Missing or invalid 'x' field");
        }
        data.x = *x_opt;

        auto y_opt = safe_int16_required(j, "y");
        if (!y_opt.has_value())
        {
            return result<player_attack_request_data, std::string>::err("Missing or invalid 'y' field");
        }
        data.y = *y_opt;

        // Direction is optional but clamped to 0-7
        if (j.contains("direction") && j["direction"].is_number())
        {
            data.direction = safe_direction(j, "direction", 0);
        }

        if (j.contains("attack_type"))
        {
            data.type = parse_attack_type(j["attack_type"]);
        }

        if (j.contains("target_type"))
        {
            data.tgt_type = parse_target_type(j["target_type"]);
        }

        if (j.contains("target_id") && j["target_id"].is_number())
        {
            data.target_id = j["target_id"].get<uint32_t>();
        }

        if (j.contains("timestamp") && j["timestamp"].is_number())
        {
            data.timestamp = j["timestamp"].get<uint64_t>();
        }

        return result<player_attack_request_data, std::string>::ok(std::move(data));
    }
    catch (const nlohmann::json::exception& e)
    {
        return result<player_attack_request_data, std::string>::err(std::string("Parse error: ") + e.what());
    }
}

auto player_magic_request_data::from_json(const nlohmann::json& j) -> result<player_magic_request_data, std::string>
{
    try
    {
        player_magic_request_data data;

        auto x_opt = safe_int16_required(j, "x");
        if (!x_opt.has_value())
        {
            return result<player_magic_request_data, std::string>::err("Missing or invalid 'x' field");
        }
        data.x = *x_opt;

        auto y_opt = safe_int16_required(j, "y");
        if (!y_opt.has_value())
        {
            return result<player_magic_request_data, std::string>::err("Missing or invalid 'y' field");
        }
        data.y = *y_opt;

        // Direction is optional but clamped to 0-7
        if (j.contains("direction") && j["direction"].is_number())
        {
            data.direction = safe_direction(j, "direction", 0);
        }

        if (!j.contains("spell_id") || !j["spell_id"].is_number())
        {
            return result<player_magic_request_data, std::string>::err("Missing or invalid 'spell_id' field");
        }
        data.spell_id = j["spell_id"].get<uint32_t>();

        if (j.contains("target_type"))
        {
            data.tgt_type = parse_target_type(j["target_type"]);
        }

        if (j.contains("target_id") && j["target_id"].is_number())
        {
            data.target_id = j["target_id"].get<uint32_t>();
        }

        // Target coordinates use safe parsing
        data.target_x = safe_int16(j, "target_x", 0);
        data.target_y = safe_int16(j, "target_y", 0);

        if (j.contains("timestamp") && j["timestamp"].is_number())
        {
            data.timestamp = j["timestamp"].get<uint64_t>();
        }

        return result<player_magic_request_data, std::string>::ok(std::move(data));
    }
    catch (const nlohmann::json::exception& e)
    {
        return result<player_magic_request_data, std::string>::err(std::string("Parse error: ") + e.what());
    }
}

auto player_skill_request_data::from_json(const nlohmann::json& j) -> result<player_skill_request_data, std::string>
{
    try
    {
        player_skill_request_data data;

        auto x_opt = safe_int16_required(j, "x");
        if (!x_opt.has_value())
        {
            return result<player_skill_request_data, std::string>::err("Missing or invalid 'x' field");
        }
        data.x = *x_opt;

        auto y_opt = safe_int16_required(j, "y");
        if (!y_opt.has_value())
        {
            return result<player_skill_request_data, std::string>::err("Missing or invalid 'y' field");
        }
        data.y = *y_opt;

        // Direction is optional but clamped to 0-7
        if (j.contains("direction") && j["direction"].is_number())
        {
            data.direction = safe_direction(j, "direction", 0);
        }

        if (!j.contains("skill_id") || !j["skill_id"].is_number())
        {
            return result<player_skill_request_data, std::string>::err("Missing or invalid 'skill_id' field");
        }
        data.skill_id = j["skill_id"].get<uint32_t>();

        if (j.contains("target_type"))
        {
            data.tgt_type = parse_target_type(j["target_type"]);
        }

        if (j.contains("target_id") && j["target_id"].is_number())
        {
            data.target_id = j["target_id"].get<uint32_t>();
        }

        if (j.contains("timestamp") && j["timestamp"].is_number())
        {
            data.timestamp = j["timestamp"].get<uint64_t>();
        }

        return result<player_skill_request_data, std::string>::ok(std::move(data));
    }
    catch (const nlohmann::json::exception& e)
    {
        return result<player_skill_request_data, std::string>::err(std::string("Parse error: ") + e.what());
    }
}

auto player_pickup_request_data::from_json(const nlohmann::json& j) -> result<player_pickup_request_data, std::string>
{
    try
    {
        player_pickup_request_data data;

        auto x_opt = safe_int16_required(j, "x");
        if (!x_opt.has_value())
        {
            return result<player_pickup_request_data, std::string>::err("Missing or invalid 'x' field");
        }
        data.x = *x_opt;

        auto y_opt = safe_int16_required(j, "y");
        if (!y_opt.has_value())
        {
            return result<player_pickup_request_data, std::string>::err("Missing or invalid 'y' field");
        }
        data.y = *y_opt;

        if (!j.contains("item_id") || !j["item_id"].is_number())
        {
            return result<player_pickup_request_data, std::string>::err("Missing or invalid 'item_id' field");
        }
        data.item_id = j["item_id"].get<uint32_t>();

        if (j.contains("timestamp") && j["timestamp"].is_number())
        {
            data.timestamp = j["timestamp"].get<uint64_t>();
        }

        return result<player_pickup_request_data, std::string>::ok(std::move(data));
    }
    catch (const nlohmann::json::exception& e)
    {
        return result<player_pickup_request_data, std::string>::err(std::string("Parse error: ") + e.what());
    }
}

auto player_interact_request_data::from_json(const nlohmann::json& j)
    -> result<player_interact_request_data, std::string>
{
    try
    {
        player_interact_request_data data;

        auto x_opt = safe_int16_required(j, "x");
        if (!x_opt.has_value())
        {
            return result<player_interact_request_data, std::string>::err("Missing or invalid 'x' field");
        }
        data.x = *x_opt;

        auto y_opt = safe_int16_required(j, "y");
        if (!y_opt.has_value())
        {
            return result<player_interact_request_data, std::string>::err("Missing or invalid 'y' field");
        }
        data.y = *y_opt;

        if (j.contains("target_type"))
        {
            data.tgt_type = parse_target_type(j["target_type"]);
        }

        if (!j.contains("target_id") || !j["target_id"].is_number())
        {
            return result<player_interact_request_data, std::string>::err("Missing or invalid 'target_id' field");
        }
        data.target_id = j["target_id"].get<uint32_t>();

        if (j.contains("timestamp") && j["timestamp"].is_number())
        {
            data.timestamp = j["timestamp"].get<uint64_t>();
        }

        return result<player_interact_request_data, std::string>::ok(std::move(data));
    }
    catch (const nlohmann::json::exception& e)
    {
        return result<player_interact_request_data, std::string>::err(std::string("Parse error: ") + e.what());
    }
}

auto chat_message_request_data::from_json(const nlohmann::json& j) -> result<chat_message_request_data, std::string>
{
    try
    {
        chat_message_request_data data;

        if (!j.contains("content") || !j["content"].is_string())
        {
            return result<chat_message_request_data, std::string>::err("Missing or invalid 'content' field");
        }
        data.content = j["content"].get<std::string>();

        // Optional explicit channel override
        if (j.contains("channel") && j["channel"].is_string())
        {
            data.channel = j["channel"].get<std::string>();
        }

        // Optional recipient for whispers
        if (j.contains("recipient") && j["recipient"].is_string())
        {
            data.recipient_name = j["recipient"].get<std::string>();
        }

        if (j.contains("timestamp") && j["timestamp"].is_number())
        {
            data.timestamp = j["timestamp"].get<uint64_t>();
        }

        return result<chat_message_request_data, std::string>::ok(std::move(data));
    }
    catch (const nlohmann::json::exception& e)
    {
        return result<chat_message_request_data, std::string>::err(std::string("Parse error: ") + e.what());
    }
}

auto chat_message_broadcast_data::to_json() const -> nlohmann::json
{
    nlohmann::json data;
    data["channel"] = channel;
    data["sender_id"] = sender_id;
    data["sender_name"] = sender_name;
    data["content"] = content;
    data["timestamp"] = timestamp;

    if (!flags.empty())
    {
        data["flags"] = flags;
    }

    if (recipient_name.has_value())
    {
        data["recipient_name"] = *recipient_name;
    }

    return data;
}

auto command_request_data::from_json(const nlohmann::json& j) -> result<command_request_data, std::string>
{
    try
    {
        command_request_data data;

        if (!j.contains("command") || !j["command"].is_string())
        {
            return result<command_request_data, std::string>::err("Missing or invalid 'command' field");
        }
        data.command = j["command"].get<std::string>();

        // Optional args array
        if (j.contains("args") && j["args"].is_array())
        {
            for (const auto& arg : j["args"])
            {
                if (arg.is_string())
                {
                    data.args.push_back(arg.get<std::string>());
                }
                else
                {
                    // Convert non-string args to string
                    data.args.push_back(arg.dump());
                }
            }
        }

        // Optional named params object
        if (j.contains("params") && j["params"].is_object())
        {
            data.params = j["params"];
        }

        if (j.contains("timestamp") && j["timestamp"].is_number())
        {
            data.timestamp = j["timestamp"].get<uint64_t>();
        }

        return result<command_request_data, std::string>::ok(std::move(data));
    }
    catch (const nlohmann::json::exception& e)
    {
        return result<command_request_data, std::string>::err(std::string("Parse error: ") + e.what());
    }
}

auto command_response_data::to_json() const -> nlohmann::json
{
    nlohmann::json data;
    data["success"] = success;
    data["command"] = command;
    data["message"] = message;

    if (!result.is_null())
    {
        data["result"] = result;
    }

    return data;
}

// Message data to_json implementations

auto character_data_msg::to_json() const -> nlohmann::json
{
    auto j = nlohmann::json{{"id", id},
                            {"name", name},
                            {"level", level},
                            {"class_type", class_type},
                            {"nation", nation},
                            {"gender", gender},
                            {"map_name", map_name},
                            {"pos_x", pos_x},
                            {"pos_y", pos_y},
                            {"hp", hp},
                            {"hp_max", hp_max},
                            {"mp", mp},
                            {"mp_max", mp_max},
                            {"sp", sp},
                            {"sp_max", sp_max},
                            {"gold", gold},
                            {"str", str},
                            {"dex", dex},
                            {"vit", vit},
                            {"int", int_},
                            {"mag", mag},
                            {"cha", cha},
                            {"hair_style", hair_style},
                            {"hair_color", hair_color},
                            {"skin_color", skin_color},
                            {"experience", experience},
                            {"pk_count", pk_count},
                            {"stat_points", stat_points},
                            {"hunger_level", hunger_level},
                            {"guild_name", guild_name},
                            {"guild_tag", guild_tag},
                            {"guild_rank", guild_rank}};

    // Equipment visuals
    auto equip_to_json = [](const equip_visual_msg& v) -> nlohmann::json
    {
        nlohmann::json ej;
        ej["appr"] = v.appr;
        ej["color"] = v.color;
        return ej;
    };

    j["equipment"] = nlohmann::json{{"weapon", equip_to_json(weapon_visual)},
                                    {"shield", equip_to_json(shield_visual)},
                                    {"body", equip_to_json(body_visual)},
                                    {"pants", equip_to_json(pants_visual)},
                                    {"head", equip_to_json(head_visual)},
                                    {"arms", equip_to_json(arms_visual)},
                                    {"boots", equip_to_json(boots_visual)},
                                    {"cape", equip_to_json(cape_visual)}};

    return j;
}

auto inventory_item_msg::to_json() const -> nlohmann::json
{
    auto j = nlohmann::json{{"item_id", item_id},
                            {"name", name},
                            {"count", count},
                            {"durability", durability},
                            {"max_durability", max_durability},
                            {"item_type", item_type},
                            {"equip_pos", equip_pos},
                            {"sprite", sprite},
                            {"sprite_frame", sprite_frame},
                            {"color", color},
                            {"weight", weight},
                            {"level_limit", level_limit},
                            {"pos_x", pos_x},
                            {"pos_y", pos_y},
                            {"z_order", z_order}};
    if (!attribute.is_empty())
    {
        j["attribute"] = attribute.to_json();
    }
    if (equipped_slot.has_value())
    {
        j["equipped_slot"] = *equipped_slot;
    }
    return j;
}

auto visible_entity_msg::to_json() const -> nlohmann::json
{
    auto j = nlohmann::json{{"entity_id", entity_id},
                            {"type", type},
                            {"name", name},
                            {"x", x},
                            {"y", y},
                            {"hp_percent", hp_percent},
                            {"direction", direction}};

    if (type == "player")
    {
        j["faction"] = faction;
        j["hostility"] = hostility;
        j["pk_status"] = pk_status;
        j["combat_mode"] = combat_mode;
        if (!guild_name.empty())
        {
            j["guild_name"] = guild_name;
            j["guild_tag"] = guild_tag;
        }

        // Base appearance
        j["gender"] = gender;
        j["skin_color"] = skin_color;
        j["hair_style"] = hair_style;
        j["hair_color"] = hair_color;
        j["underwear_color"] = underwear_color;
        j["level"] = player_level;

        // Equipment visuals
        auto equip_to_json = [](const equip_visual_msg& v) -> nlohmann::json
        {
            nlohmann::json ej;
            ej["appr"] = v.appr;
            ej["color"] = v.color;
            if (!v.name.empty())
                ej["name"] = v.name;
            if (!v.rarity.empty())
                ej["rarity"] = v.rarity;
            return ej;
        };

        j["equipment"] = nlohmann::json{{"weapon", equip_to_json(weapon_visual)},
                                        {"shield", equip_to_json(shield_visual)},
                                        {"body", equip_to_json(body_visual)},
                                        {"pants", equip_to_json(pants_visual)},
                                        {"head", equip_to_json(head_visual)},
                                        {"arms", equip_to_json(arms_visual)},
                                        {"boots", equip_to_json(boots_visual)},
                                        {"cape", equip_to_json(cape_visual)}};

        j["weapon_glow"] = weapon_glow;
        j["shield_glow"] = shield_glow;
        j["weapon_speed"] = weapon_speed;

        // Status effects
        if (!status_effects.empty())
        {
            j["status_effects"] = status_effects;
        }

        // Active buffs
        if (!active_buffs.empty())
        {
            auto buffs = nlohmann::json::array();
            for (const auto& b : active_buffs)
            {
                buffs.push_back(nlohmann::json{{"type", b.type},
                                               {"spell_id", b.spell_id},
                                               {"magnitude", b.magnitude},
                                               {"remaining_ms", b.remaining_ms}});
            }
            j["active_buffs"] = buffs;
        }
    }
    else if (type == "npc")
    {
        j["template_id"] = template_id;
        j["sprite_id"] = sprite_id;
        j["level"] = level;
        j["category"] = category;
        j["hostility"] = hostility;
    }

    if (is_dead)
    {
        j["is_dead"] = true;
    }

    return j;
}

auto attack_result_msg::to_json() const -> nlohmann::json
{
    auto j = nlohmann::json{{"hit", hit},
                            {"critical", critical},
                            {"damage", damage},
                            {"target_id", target_id},
                            {"target_hp", target_hp},
                            {"target_hp_max", target_hp_max},
                            {"attacker_x", attacker_x},
                            {"attacker_y", attacker_y}};
    if (is_ranged)
    {
        j["is_ranged"] = true;
        if (ammo_count >= 0)
        {
            j["ammo_count"] = ammo_count;
        }
        if (ammo_template_id > 0)
        {
            j["ammo_template_id"] = ammo_template_id;
        }
    }
    return j;
}

auto magic_result_msg::to_json() const -> nlohmann::json
{
    return nlohmann::json{{"success", success},
                          {"spell_id", spell_id},
                          {"mana_cost", mana_cost},
                          {"damage", damage},
                          {"heal", heal},
                          {"target_id", target_id},
                          {"caster_mp", caster_mp}};
}

auto skill_result_msg::to_json() const -> nlohmann::json
{
    return nlohmann::json{
        {"success", success}, {"skill_id", skill_id}, {"effect_value", effect_value}, {"target_id", target_id}};
}

auto pickup_result_msg::to_json() const -> nlohmann::json
{
    nlohmann::json j{{"success", success},
                     {"item_id", item_id},
                     {"item_name", item_name},
                     {"quantity", quantity}};
    if (!attribute.is_empty())
    {
        j["attribute"] = attribute.to_json();
    }
    return j;
}

auto interact_result_msg::to_json() const -> nlohmann::json
{
    return nlohmann::json{{"success", success},
                          {"target_id", target_id},
                          {"interaction_type", interaction_type},
                          {"interaction_data", interaction_data}};
}

auto teleporter_info_msg::to_json() const -> nlohmann::json
{
    return nlohmann::json{{"id", id},
                          {"x", x},
                          {"y", y},
                          {"dest_map", dest_map},
                          {"dest_x", dest_x},
                          {"dest_y", dest_y},
                          {"dest_dir", dest_dir}};
}

auto map_teleporters_msg::to_json() const -> nlohmann::json
{
    nlohmann::json teleporters_json = nlohmann::json::array();
    for (const auto& tp : teleporters)
    {
        teleporters_json.push_back(tp.to_json());
    }

    return nlohmann::json{{"map_name", map_name}, {"teleporters", std::move(teleporters_json)}};
}

auto teleporter_update_msg::to_json() const -> nlohmann::json
{
    return nlohmann::json{{"action", action}, {"map_name", map_name}, {"teleporter", teleporter.to_json()}};
}

auto player_teleport_msg::to_json() const -> nlohmann::json
{
    return nlohmann::json{{"dest_map", dest_map}, {"dest_x", dest_x}, {"dest_y", dest_y}, {"dest_dir", dest_dir}};
}

auto known_spell_msg::to_json() const -> nlohmann::json
{
    return nlohmann::json{{"spell_id", spell_id}, {"level", level}, {"total_casts", total_casts}};
}

auto quest_objective_msg::to_json() const -> nlohmann::json
{
    return nlohmann::json{{"id", id}, {"status", status}, {"current", current}, {"required", required}};
}

auto active_quest_msg::to_json() const -> nlohmann::json
{
    nlohmann::json obj_json = nlohmann::json::array();
    for (const auto& obj : objectives)
    {
        obj_json.push_back(obj.to_json());
    }
    return nlohmann::json{{"quest_id", quest_id}, {"status", status}, {"objectives", std::move(obj_json)}};
}

auto game_state_msg::to_json() const -> nlohmann::json
{
    nlohmann::json inv_json = nlohmann::json::array();
    for (const auto& item : inventory)
    {
        inv_json.push_back(item.to_json());
    }

    nlohmann::json skills_json = nlohmann::json::array();
    for (const auto& s : skills)
    {
        skills_json.push_back({{"skill_id", s.skill_id},
                               {"level", s.level},
                               {"total_uses", s.total_uses},
                               {"uses_this_level", s.uses_this_level},
                               {"uses_to_next_level", s.uses_to_next_level}});
    }

    nlohmann::json spells_json = nlohmann::json::array();
    for (const auto& spell : spells)
    {
        spells_json.push_back(spell.to_json());
    }

    nlohmann::json quests_json = nlohmann::json::array();
    for (const auto& quest : quests)
    {
        quests_json.push_back(quest.to_json());
    }

    nlohmann::json completed_json = nlohmann::json::array();
    for (const auto& qid : completed_quests)
    {
        completed_json.push_back(qid);
    }

    return nlohmann::json{
        {"character", character.to_json()},
        {"inventory", {{"items", std::move(inv_json)}, {"gold", gold}}},
        {"skills", std::move(skills_json)},
        {"spells", std::move(spells_json)},
        {"quests", {{"active", std::move(quests_json)}, {"completed", std::move(completed_json)}}},
        {"world",
         {{"environment", {{"hour", time_hour}, {"minute", time_minute}, {"is_day", is_day}, {"weather", weather}}}}}};
}

// Response builders

auto make_error_response(uint32_t seq, std::string_view error_code, std::string_view message) -> json_message
{
    return json_message{.type = json_message_type::error,
                        .seq = seq,
                        .data =
                            nlohmann::json{{"error_code", std::string(error_code)}, {"message", std::string(message)}}};
}

auto make_login_response(uint32_t seq,
                         bool success,
                         std::optional<std::string_view> token,
                         std::optional<std::string_view> error,
                         std::optional<std::string_view> forum_token) -> json_message
{
    nlohmann::json data;
    data["success"] = success;

    if (success && token.has_value())
    {
        data["session_token"] = std::string(*token);
    }

    if (success && forum_token.has_value() && !forum_token->empty())
    {
        data["forum_token"] = std::string(*forum_token);
    }

    if (!success && error.has_value())
    {
        data["error"] = std::string(*error);
    }

    return json_message{.type = json_message_type::login_response, .seq = seq, .data = std::move(data)};
}

auto make_create_account_response(uint32_t seq,
                                  bool success,
                                  std::optional<uint32_t> account_id,
                                  std::optional<std::string_view> error) -> json_message
{
    nlohmann::json data;
    data["success"] = success;

    if (success && account_id.has_value())
    {
        data["account_id"] = *account_id;
    }

    if (!success && error.has_value())
    {
        data["error"] = std::string(*error);
    }

    return json_message{.type = json_message_type::create_account_response, .seq = seq, .data = std::move(data)};
}

auto make_logout_response(uint32_t seq, bool success) -> json_message
{
    return json_message{
        .type = json_message_type::logout_response, .seq = seq, .data = nlohmann::json{{"success", success}}};
}

auto make_get_characters_response(uint32_t seq, const std::vector<auth::character_summary>& characters) -> json_message
{
    nlohmann::json chars_json = nlohmann::json::array();

    for (const auto& ch : characters)
    {
        chars_json.push_back({{"id", ch.id.value},
                              {"name", ch.name},
                              {"level", ch.level},
                              {"class_type", ch.class_type},
                              {"nation", ch.nation},
                              {"gender", ch.gender},
                              {"map_name", ch.map_name},
                              {"experience", ch.experience},
                              {"hair_style", ch.hair_style},
                              {"hair_color", ch.hair_color},
                              {"skin_color", ch.skin_color}});
    }

    return json_message{.type = json_message_type::get_characters_response,
                        .seq = seq,
                        .data = nlohmann::json{{"success", true}, {"characters", std::move(chars_json)}}};
}

auto make_create_character_response(uint32_t seq,
                                    bool success,
                                    std::optional<uint32_t> character_id,
                                    std::optional<std::string_view> error) -> json_message
{
    nlohmann::json data;
    data["success"] = success;

    if (success && character_id.has_value())
    {
        data["character_id"] = *character_id;
    }

    if (!success && error.has_value())
    {
        data["error"] = std::string(*error);
    }

    return json_message{.type = json_message_type::create_character_response, .seq = seq, .data = std::move(data)};
}

auto make_delete_character_response(uint32_t seq, bool success, std::optional<std::string_view> error) -> json_message
{
    nlohmann::json data;
    data["success"] = success;

    if (!success && error.has_value())
    {
        data["error"] = std::string(*error);
    }

    return json_message{.type = json_message_type::delete_character_response, .seq = seq, .data = std::move(data)};
}

auto make_enter_game_response(uint32_t seq,
                              bool success,
                              const game_state_msg* game_state,
                              std::optional<std::string_view> error) -> json_message
{
    nlohmann::json data;
    data["success"] = success;

    if (success && game_state != nullptr)
    {
        auto state_json = game_state->to_json();
        // Merge game state into data (character, inventory, equipment, skills, world)
        for (auto& [key, value] : state_json.items())
        {
            data[key] = std::move(value);
        }
    }

    if (!success && error.has_value())
    {
        data["error"] = std::string(*error);
    }

    return json_message{.type = json_message_type::enter_game_response, .seq = seq, .data = std::move(data)};
}

auto make_inventory_data(uint32_t seq, const std::vector<inventory_item_msg>& items, int32_t gold) -> json_message
{
    nlohmann::json items_json = nlohmann::json::array();
    for (const auto& item : items)
    {
        items_json.push_back(item.to_json());
    }

    return json_message{.type = json_message_type::inventory_data,
                        .seq = seq,
                        .data = nlohmann::json{{"items", std::move(items_json)}, {"gold", gold}}};
}

auto make_skills_data(uint32_t seq, const std::vector<skill_entry_msg>& skills) -> json_message
{
    nlohmann::json skills_json = nlohmann::json::array();
    for (const auto& s : skills)
    {
        skills_json.push_back({{"skill_id", s.skill_id},
                               {"level", s.level},
                               {"total_uses", s.total_uses},
                               {"uses_this_level", s.uses_this_level},
                               {"uses_to_next_level", s.uses_to_next_level}});
    }

    return json_message{
        .type = json_message_type::skills_data, .seq = seq, .data = nlohmann::json{{"skills", std::move(skills_json)}}};
}

auto make_skill_update(uint32_t player_id_val,
                       const skill_entry_msg& skill,
                       int16_t old_level) -> json_message
{
    return json_message{
        .type = json_message_type::skill_update,
        .seq = 0,
        .data = nlohmann::json{
            {"player_id", player_id_val},
            {"skill_id", skill.skill_id},
            {"old_level", old_level},
            {"level", skill.level},
            {"total_uses", skill.total_uses},
            {"uses_this_level", skill.uses_this_level},
            {"uses_to_next_level", skill.uses_to_next_level}
        }
    };
}

auto make_skill_progress(uint8_t skill_id,
                         int32_t uses_this_level,
                         int32_t uses_to_next_level,
                         uint8_t percent) -> json_message
{
    return json_message{
        .type = json_message_type::skill_progress,
        .seq = 0,
        .data = nlohmann::json{
            {"skill_id", skill_id},
            {"uses_this_level", uses_this_level},
            {"uses_to_next_level", uses_to_next_level},
            {"percent", percent}
        }
    };
}

auto make_entity_spawn(uint32_t seq, const visible_entity_msg& entity) -> json_message
{
    return json_message{.type = json_message_type::entity_spawn, .seq = seq, .data = entity.to_json()};
}

auto make_entity_despawn(uint32_t seq, uint32_t entity_id) -> json_message
{
    return json_message{
        .type = json_message_type::entity_despawn, .seq = seq, .data = nlohmann::json{{"entity_id", entity_id}}};
}

auto make_player_move_response(uint32_t seq,
                               bool success,
                               int16_t x,
                               int16_t y,
                               int16_t direction,
                               std::optional<std::string_view> error) -> json_message
{
    nlohmann::json data;
    data["success"] = success;

    if (success)
    {
        data["x"] = x;
        data["y"] = y;
        data["direction"] = direction;
    }

    if (!success && error.has_value())
    {
        data["error"] = std::string(*error);
    }

    return json_message{.type = json_message_type::player_move_response, .seq = seq, .data = std::move(data)};
}

auto make_player_stop_response(uint32_t seq,
                               bool success,
                               int16_t x,
                               int16_t y,
                               int16_t direction,
                               std::optional<std::string_view> error) -> json_message
{
    nlohmann::json data;
    data["success"] = success;

    if (success)
    {
        data["x"] = x;
        data["y"] = y;
        data["direction"] = direction;
    }

    if (!success && error.has_value())
    {
        data["error"] = std::string(*error);
    }

    return json_message{.type = json_message_type::player_stop_response, .seq = seq, .data = std::move(data)};
}

auto make_player_position_update(uint32_t entity_id,
                                 int16_t x,
                                 int16_t y,
                                 int16_t direction,
                                 bool is_running,
                                 std::optional<int16_t> dest_x,
                                 std::optional<int16_t> dest_y) -> json_message
{
    nlohmann::json data{
        {"entity_id", entity_id}, {"x", x}, {"y", y}, {"direction", direction}, {"is_running", is_running}};

    // Include destination coordinates if available
    if (dest_x.has_value() && dest_y.has_value())
    {
        data["dest_x"] = *dest_x;
        data["dest_y"] = *dest_y;
    }

    return json_message{.type = json_message_type::player_position_update,
                        .seq = 0, // Broadcasts don't need seq
                        .data = std::move(data)};
}

auto make_player_attack_response(uint32_t seq,
                                 bool success,
                                 const attack_result_msg* result,
                                 std::optional<std::string_view> error) -> json_message
{
    nlohmann::json data;
    data["success"] = success;

    if (success && result != nullptr)
    {
        data["result"] = result->to_json();
    }

    if (!success && error.has_value())
    {
        data["error"] = std::string(*error);
    }

    return json_message{.type = json_message_type::player_attack_response, .seq = seq, .data = std::move(data)};
}

auto make_player_magic_response(uint32_t seq,
                                bool success,
                                const magic_result_msg* result,
                                std::optional<std::string_view> error) -> json_message
{
    nlohmann::json data;
    data["success"] = success;

    if (success && result != nullptr)
    {
        data["result"] = result->to_json();
    }

    if (!success && error.has_value())
    {
        data["error"] = std::string(*error);
    }

    return json_message{.type = json_message_type::player_magic_response, .seq = seq, .data = std::move(data)};
}

auto make_player_skill_response(uint32_t seq,
                                bool success,
                                const skill_result_msg* result,
                                std::optional<std::string_view> error) -> json_message
{
    nlohmann::json data;
    data["success"] = success;

    if (success && result != nullptr)
    {
        data["result"] = result->to_json();
    }

    if (!success && error.has_value())
    {
        data["error"] = std::string(*error);
    }

    return json_message{.type = json_message_type::player_skill_response, .seq = seq, .data = std::move(data)};
}

auto make_player_pickup_response(uint32_t seq,
                                 bool success,
                                 const pickup_result_msg* result,
                                 std::optional<std::string_view> error) -> json_message
{
    nlohmann::json data;
    data["success"] = success;

    if (success && result != nullptr)
    {
        data["result"] = result->to_json();
    }

    if (!success && error.has_value())
    {
        data["error"] = std::string(*error);
    }

    return json_message{.type = json_message_type::player_pickup_response, .seq = seq, .data = std::move(data)};
}

auto make_player_interact_response(uint32_t seq,
                                   bool success,
                                   const interact_result_msg* result,
                                   std::optional<std::string_view> error) -> json_message
{
    nlohmann::json data;
    data["success"] = success;

    if (success && result != nullptr)
    {
        data["result"] = result->to_json();
    }

    if (!success && error.has_value())
    {
        data["error"] = std::string(*error);
    }

    return json_message{.type = json_message_type::player_interact_response, .seq = seq, .data = std::move(data)};
}

auto make_pong_response(uint32_t seq) -> json_message
{
    return json_message{.type = json_message_type::pong,
                        .seq = seq,
                        .data = nlohmann::json{{"timestamp",
                                                std::chrono::duration_cast<std::chrono::milliseconds>(
                                                    std::chrono::system_clock::now().time_since_epoch())
                                                    .count()}}};
}

auto make_chat_message_response(uint32_t seq, bool success, std::optional<std::string_view> error) -> json_message
{
    nlohmann::json data;
    data["success"] = success;

    if (!success && error.has_value())
    {
        data["error"] = std::string(*error);
    }

    return json_message{.type = json_message_type::chat_message, .seq = seq, .data = std::move(data)};
}

auto make_chat_message_broadcast(const chat_message_broadcast_data& data) -> json_message
{
    return json_message{.type = json_message_type::chat_message_broadcast,
                        .seq = 0, // Broadcasts don't need seq
                        .data = data.to_json()};
}

auto make_command_response(uint32_t seq,
                           bool success,
                           std::string_view command,
                           std::string_view message,
                           const nlohmann::json& result) -> json_message
{
    nlohmann::json data;
    data["success"] = success;
    data["command"] = std::string(command);
    data["message"] = std::string(message);

    if (!result.is_null() && !result.empty())
    {
        data["result"] = result;
    }

    return json_message{.type = json_message_type::command_response, .seq = seq, .data = std::move(data)};
}

auto make_map_teleporters(const map_teleporters_msg& data) -> json_message
{
    return json_message{.type = json_message_type::map_teleporters,
                        .seq = 0, // Broadcasts don't need seq
                        .data = data.to_json()};
}

auto make_teleporter_update(const teleporter_update_msg& data) -> json_message
{
    return json_message{.type = json_message_type::teleporter_update,
                        .seq = 0, // Broadcasts don't need seq
                        .data = data.to_json()};
}

auto make_player_teleport(uint32_t seq, const player_teleport_msg& data) -> json_message
{
    return json_message{.type = json_message_type::player_teleport, .seq = seq, .data = data.to_json()};
}

auto make_combat_attack_broadcast(uint32_t attacker_id,
                                  uint32_t target_id,
                                  int16_t attacker_x,
                                  int16_t attacker_y,
                                  int16_t target_x,
                                  int16_t target_y,
                                  int16_t direction,
                                  bool hit,
                                  bool critical,
                                  int32_t damage,
                                  projectile_type projectile) -> json_message
{
    auto data = nlohmann::json{{"attacker_id", attacker_id},
                               {"target_id", target_id},
                               {"attacker_x", attacker_x},
                               {"attacker_y", attacker_y},
                               {"target_x", target_x},
                               {"target_y", target_y},
                               {"direction", direction},
                               {"hit", hit},
                               {"critical", critical},
                               {"damage", damage}};
    if (projectile != projectile_type::none)
    {
        data["attack_mode"] = "ranged";
        switch (projectile)
        {
        case projectile_type::arrow:
            data["projectile_type"] = "arrow";
            break;
        case projectile_type::poison_arrow:
            data["projectile_type"] = "poison_arrow";
            break;
        default:
            break;
        }
    }
    return json_message{.type = json_message_type::combat_attack_broadcast, .seq = 0, .data = std::move(data)};
}

auto make_entity_hp_update(uint32_t entity_id, int32_t hp, int32_t hp_max) -> json_message
{
    return json_message{.type = json_message_type::entity_hp_update,
                        .seq = 0, // Broadcasts don't need seq
                        .data = nlohmann::json{{"entity_id", entity_id}, {"hp", hp}, {"hp_max", hp_max}}};
}

auto make_entity_death(uint32_t victim_id, uint32_t killer_id, int16_t x, int16_t y, int32_t damage) -> json_message
{
    auto j = nlohmann::json{{"victim_id", victim_id}, {"killer_id", killer_id}, {"x", x}, {"y", y}};
    if (damage > 0)
    {
        j["damage"] = damage;
    }
    return json_message{.type = json_message_type::entity_death,
                        .seq = 0, // Broadcasts don't need seq
                        .data = std::move(j)};
}

// Combat effect data to_json implementation

auto combat_effect_data::to_json() const -> nlohmann::json
{
    nlohmann::json j{{"source_id", source_id},
                     {"target_id", target_id},
                     {"effect_type", effect_type},
                     {"value", value},
                     {"is_critical", is_critical},
                     {"target_x", target_x},
                     {"target_y", target_y}};

    if (!damage_type.empty())
    {
        j["damage_type"] = damage_type;
    }
    if (spell_id.has_value())
    {
        j["spell_id"] = *spell_id;
    }

    return j;
}

auto make_combat_effect(const combat_effect_data& data) -> json_message
{
    return json_message{.type = json_message_type::combat_effect, .seq = 0, .data = data.to_json()};
}

// NPC data to_json implementations

auto npc_spawn_data::to_json() const -> nlohmann::json
{
    auto j = nlohmann::json{{"entity_id", entity_id},
                            {"template_id", template_id},
                            {"sprite_id", sprite_id},
                            {"name", name},
                            {"x", x},
                            {"y", y},
                            {"direction", direction},
                            {"hp", hp},
                            {"max_hp", max_hp},
                            {"level", level},
                            {"category", category},
                            {"hostility", hostility}};
    if (!attributes.empty())
    {
        j["attributes"] = attributes;
    }
    if (is_dead)
    {
        j["is_dead"] = true;
    }
    return j;
}

auto npc_move_data::to_json() const -> nlohmann::json
{
    return nlohmann::json{{"entity_id", entity_id}, {"x", x}, {"y", y}, {"direction", direction}};
}

auto npc_attack_data::to_json() const -> nlohmann::json
{
    auto j = nlohmann::json{{"attacker_id", attacker_id},
                            {"target_id", target_id},
                            {"damage", damage},
                            {"is_critical", is_critical},
                            {"attacker_x", attacker_x},
                            {"attacker_y", attacker_y},
                            {"target_x", target_x},
                            {"target_y", target_y}};
    if (is_ranged)
    {
        j["is_ranged"] = true;
        j["projectile_type"] = "arrow";
    }
    return j;
}

// NPC message builders

auto make_npc_spawn_message(const npc_spawn_data& data) -> json_message
{
    return json_message{.type = json_message_type::npc_spawn, .seq = 0, .data = data.to_json()};
}

auto make_npc_despawn_message(uint32_t entity_id) -> json_message
{
    return json_message{
        .type = json_message_type::npc_despawn, .seq = 0, .data = nlohmann::json{{"entity_id", entity_id}}};
}

auto make_npc_move_message(const npc_move_data& data) -> json_message
{
    return json_message{.type = json_message_type::npc_move, .seq = 0, .data = data.to_json()};
}

auto make_npc_attack_message(const npc_attack_data& data) -> json_message
{
    return json_message{.type = json_message_type::npc_attack, .seq = 0, .data = data.to_json()};
}

// Ground item spawn data to_json implementation

auto ground_item_spawn_data::to_json() const -> nlohmann::json
{
    auto j = nlohmann::json{{"item_id", item_id},
                            {"template_id", template_id},
                            {"item_name", item_name},
                            {"count", count},
                            {"x", x},
                            {"y", y},
                            {"ground_sprite", ground_sprite},
                            {"ground_sprite_frame", ground_sprite_frame},
                            {"item_color", item_color},
                            {"reason", reason}};
    if (!attribute.is_empty())
    {
        j["attribute"] = attribute.to_json();
    }
    return j;
}

// Ground item spawn message builder

auto make_ground_item_spawn(const ground_item_spawn_data& data) -> json_message
{
    return json_message{.type = json_message_type::ground_item_spawn,
                        .seq = 0, // Broadcasts don't need seq
                        .data = data.to_json()};
}

// Ground item data to_json implementation

auto ground_item_removed_data::to_json() const -> nlohmann::json
{
    return nlohmann::json{{"picker_id", picker_id},
                          {"picker_name", picker_name},
                          {"item_id", item_id},
                          {"item_name", item_name},
                          {"x", x},
                          {"y", y}};
}

// Ground item message builder

auto make_ground_item_removed(const ground_item_removed_data& data) -> json_message
{
    return json_message{.type = json_message_type::ground_item_removed,
                        .seq = 0, // Broadcasts don't need seq
                        .data = data.to_json()};
}

// Player death info data to_json implementation

auto player_death_info_data::to_json() const -> nlohmann::json
{
    return nlohmann::json{{"killer_id", killer_id},
                          {"killer_name", killer_name},
                          {"is_pvp", is_pvp},
                          {"xp_lost", xp_lost},
                          {"pk_points_change", pk_points_change},
                          {"gold_reward", gold_reward},
                          {"respawn_delay_ms", respawn_delay_ms},
                          {"respawn_map", respawn_map},
                          {"respawn_x", respawn_x},
                          {"respawn_y", respawn_y}};
}

// Player death info message builder

auto make_player_death_info(const player_death_info_data& data) -> json_message
{
    return json_message{.type = json_message_type::player_death_info, .seq = 0, .data = data.to_json()};
}

// Hunger update data to_json implementation

auto hunger_update_data::to_json() const -> nlohmann::json
{
    return nlohmann::json{{"level", level}, {"is_starving", is_starving}};
}

// Hunger update message builder

auto make_hunger_update(int8_t level) -> json_message
{
    return json_message{.type = json_message_type::hunger_update,
                        .seq = 0, // Broadcasts don't need seq
                        .data = hunger_update_data{level, level <= 0}.to_json()};
}

// Entity info request data from_json implementation

auto entity_info_request_data::from_json(const nlohmann::json& j) -> result<entity_info_request_data, std::string>
{
    try
    {
        entity_info_request_data data;

        if (!j.contains("entity_id") || !j["entity_id"].is_number())
        {
            return result<entity_info_request_data, std::string>::err("Missing or invalid 'entity_id' field");
        }
        data.entity_id = j["entity_id"].get<uint32_t>();

        return result<entity_info_request_data, std::string>::ok(std::move(data));
    }
    catch (const nlohmann::json::exception& e)
    {
        return result<entity_info_request_data, std::string>::err(std::string("Parse error: ") + e.what());
    }
}

// Entity info response data to_json implementation

auto entity_info_response_data::to_json() const -> nlohmann::json
{
    nlohmann::json j = {{"entity_id", entity_id},
                        {"entity_type", entity_type},
                        {"name", name},
                        {"level", level},
                        {"hp", hp},
                        {"hp_max", hp_max},
                        {"x", x},
                        {"y", y},
                        {"direction", direction}};

    // Add player-specific fields if present
    if (faction.has_value())
    {
        j["faction"] = *faction;
    }
    if (guild_name.has_value())
    {
        j["guild_name"] = *guild_name;
    }
    if (class_type.has_value())
    {
        j["class_type"] = *class_type;
    }
    if (pk_count.has_value())
    {
        j["pk_count"] = *pk_count;
    }

    // Add NPC-specific fields if present
    if (template_id.has_value())
    {
        j["template_id"] = *template_id;
    }
    if (sprite_id.has_value())
    {
        j["sprite_id"] = *sprite_id;
    }
    if (npc_type.has_value())
    {
        j["npc_type"] = *npc_type;
    }
    if (hostility.has_value())
    {
        j["hostility"] = *hostility;
    }

    return j;
}

// Entity info response message builder

auto make_entity_info_response(uint32_t seq,
                               bool success,
                               const entity_info_response_data* data,
                               std::optional<std::string_view> error) -> json_message
{
    nlohmann::json response_data;
    response_data["success"] = success;

    if (success && data != nullptr)
    {
        response_data["entity"] = data->to_json();
    }

    if (!success && error.has_value())
    {
        response_data["error"] = std::string(*error);
    }

    return json_message{.type = json_message_type::entity_info_response, .seq = seq, .data = std::move(response_data)};
}

// === NPC Interaction: from_json implementations ===

auto shop_buy_request_data::from_json(const nlohmann::json& j) -> result<shop_buy_request_data, std::string>
{
    shop_buy_request_data data;
    if (!j.contains("npc_entity_id") || !j["npc_entity_id"].is_number())
    {
        return result<shop_buy_request_data, std::string>::err("Missing npc_entity_id");
    }
    data.npc_entity_id = j["npc_entity_id"].get<uint32_t>();

    if (!j.contains("item_template_id") || !j["item_template_id"].is_number())
    {
        return result<shop_buy_request_data, std::string>::err("Missing item_template_id");
    }
    data.item_template_id = j["item_template_id"].get<uint32_t>();

    if (j.contains("count") && j["count"].is_number())
    {
        data.count = static_cast<int16_t>(j["count"].get<int>());
    }
    return result<shop_buy_request_data, std::string>::ok(std::move(data));
}

auto shop_sell_request_data::from_json(const nlohmann::json& j) -> result<shop_sell_request_data, std::string>
{
    shop_sell_request_data data;
    if (!j.contains("npc_entity_id") || !j["npc_entity_id"].is_number())
    {
        return result<shop_sell_request_data, std::string>::err("Missing npc_entity_id");
    }
    data.npc_entity_id = j["npc_entity_id"].get<uint32_t>();

    if (!j.contains("item_id") || !j["item_id"].is_number())
    {
        return result<shop_sell_request_data, std::string>::err("Missing item_id");
    }
    data.item_id = j["item_id"].get<uint32_t>();

    if (j.contains("count") && j["count"].is_number())
    {
        data.count = static_cast<int16_t>(j["count"].get<int>());
    }
    return result<shop_sell_request_data, std::string>::ok(std::move(data));
}

auto shop_sell_confirm_request_data::from_json(const nlohmann::json& j)
    -> result<shop_sell_confirm_request_data, std::string>
{
    shop_sell_confirm_request_data data;
    if (!j.contains("npc_entity_id") || !j["npc_entity_id"].is_number())
    {
        return result<shop_sell_confirm_request_data, std::string>::err("Missing npc_entity_id");
    }
    data.npc_entity_id = j["npc_entity_id"].get<uint32_t>();

    if (!j.contains("item_id") || !j["item_id"].is_number())
    {
        return result<shop_sell_confirm_request_data, std::string>::err("Missing item_id");
    }
    data.item_id = j["item_id"].get<uint32_t>();

    if (j.contains("count") && j["count"].is_number())
    {
        data.count = static_cast<int16_t>(j["count"].get<int>());
    }
    return result<shop_sell_confirm_request_data, std::string>::ok(std::move(data));
}

auto shop_repair_request_data::from_json(const nlohmann::json& j) -> result<shop_repair_request_data, std::string>
{
    shop_repair_request_data data;
    if (!j.contains("npc_entity_id") || !j["npc_entity_id"].is_number())
    {
        return result<shop_repair_request_data, std::string>::err("Missing npc_entity_id");
    }
    data.npc_entity_id = j["npc_entity_id"].get<uint32_t>();

    if (!j.contains("item_id") || !j["item_id"].is_number())
    {
        return result<shop_repair_request_data, std::string>::err("Missing item_id");
    }
    data.item_id = j["item_id"].get<uint32_t>();
    return result<shop_repair_request_data, std::string>::ok(std::move(data));
}

auto shop_repair_confirm_request_data::from_json(const nlohmann::json& j)
    -> result<shop_repair_confirm_request_data, std::string>
{
    shop_repair_confirm_request_data data;
    if (!j.contains("npc_entity_id") || !j["npc_entity_id"].is_number())
    {
        return result<shop_repair_confirm_request_data, std::string>::err("Missing npc_entity_id");
    }
    data.npc_entity_id = j["npc_entity_id"].get<uint32_t>();

    if (!j.contains("item_id") || !j["item_id"].is_number())
    {
        return result<shop_repair_confirm_request_data, std::string>::err("Missing item_id");
    }
    data.item_id = j["item_id"].get<uint32_t>();
    return result<shop_repair_confirm_request_data, std::string>::ok(std::move(data));
}

auto bank_deposit_request_data::from_json(const nlohmann::json& j) -> result<bank_deposit_request_data, std::string>
{
    bank_deposit_request_data data;
    if (!j.contains("npc_entity_id") || !j["npc_entity_id"].is_number())
    {
        return result<bank_deposit_request_data, std::string>::err("Missing npc_entity_id");
    }
    data.npc_entity_id = j["npc_entity_id"].get<uint32_t>();

    if (!j.contains("item_id") || !j["item_id"].is_number())
    {
        return result<bank_deposit_request_data, std::string>::err("Missing item_id");
    }
    data.item_id = j["item_id"].get<uint32_t>();
    return result<bank_deposit_request_data, std::string>::ok(std::move(data));
}

auto bank_withdraw_request_data::from_json(const nlohmann::json& j) -> result<bank_withdraw_request_data, std::string>
{
    bank_withdraw_request_data data;
    if (!j.contains("npc_entity_id") || !j["npc_entity_id"].is_number())
    {
        return result<bank_withdraw_request_data, std::string>::err("Missing npc_entity_id");
    }
    data.npc_entity_id = j["npc_entity_id"].get<uint32_t>();

    if (!j.contains("bank_page") || !j["bank_page"].is_number())
    {
        return result<bank_withdraw_request_data, std::string>::err("Missing bank_page");
    }
    data.bank_page = static_cast<int16_t>(j["bank_page"].get<int>());

    if (!j.contains("bank_slot") || !j["bank_slot"].is_number())
    {
        return result<bank_withdraw_request_data, std::string>::err("Missing bank_slot");
    }
    data.bank_slot = static_cast<int16_t>(j["bank_slot"].get<int>());
    return result<bank_withdraw_request_data, std::string>::ok(std::move(data));
}

auto dialog_choice_request_data::from_json(const nlohmann::json& j) -> result<dialog_choice_request_data, std::string>
{
    dialog_choice_request_data data;
    if (!j.contains("npc_entity_id") || !j["npc_entity_id"].is_number())
    {
        return result<dialog_choice_request_data, std::string>::err("Missing npc_entity_id");
    }
    data.npc_entity_id = j["npc_entity_id"].get<uint32_t>();

    if (j.contains("node_id") && j["node_id"].is_string())
    {
        data.node_id = j["node_id"].get<std::string>();
    }
    if (j.contains("choice_index") && j["choice_index"].is_number())
    {
        data.choice_index = static_cast<int16_t>(j["choice_index"].get<int>());
    }
    return result<dialog_choice_request_data, std::string>::ok(std::move(data));
}

// === Party: request parsing and builders ===

auto party_invite_request_data::from_json(const nlohmann::json& j) -> result<party_invite_request_data, std::string>
{
    party_invite_request_data data;
    if (!j.contains("target_name") || !j["target_name"].is_string())
    {
        return result<party_invite_request_data, std::string>::err("Missing target_name");
    }
    data.target_name = j["target_name"].get<std::string>();
    return result<party_invite_request_data, std::string>::ok(std::move(data));
}

auto party_accept_request_data::from_json(const nlohmann::json& j) -> result<party_accept_request_data, std::string>
{
    party_accept_request_data data;
    if (!j.contains("party_id") || !j["party_id"].is_number())
    {
        return result<party_accept_request_data, std::string>::err("Missing party_id");
    }
    data.party_id = j["party_id"].get<uint32_t>();
    if (j.contains("accept") && j["accept"].is_boolean())
    {
        data.accept = j["accept"].get<bool>();
    }
    return result<party_accept_request_data, std::string>::ok(std::move(data));
}

auto make_party_invite_response(uint32_t seq, bool success, uint32_t party_id, std::string_view error) -> json_message
{
    nlohmann::json data{{"success", success}};
    if (success)
        data["party_id"] = party_id;
    else
        data["error"] = std::string(error);
    return json_message{.type = json_message_type::party_invite_response, .seq = seq, .data = std::move(data)};
}

auto make_party_invite_notice(uint32_t party_id, std::string_view inviter_name) -> json_message
{
    return json_message{.type = json_message_type::party_invite_notice,
                        .seq = 0,
                        .data = nlohmann::json{{"party_id", party_id}, {"inviter_name", std::string(inviter_name)}}};
}

auto make_party_accept_response(uint32_t seq, bool success, uint32_t party_id, std::string_view error) -> json_message
{
    nlohmann::json data{{"success", success}};
    if (success)
        data["party_id"] = party_id;
    else
        data["error"] = std::string(error);
    return json_message{.type = json_message_type::party_accept_response, .seq = seq, .data = std::move(data)};
}

auto make_party_leave_response(uint32_t seq, bool success) -> json_message
{
    return json_message{.type = json_message_type::party_leave_response,
                        .seq = seq,
                        .data = nlohmann::json{{"success", success}}};
}

auto make_party_update(uint32_t party_id,
                       std::string_view leader_name,
                       const std::vector<std::string>& member_names) -> json_message
{
    return json_message{.type = json_message_type::party_update,
                        .seq = 0,
                        .data = nlohmann::json{{"party_id", party_id},
                                               {"leader_name", std::string(leader_name)},
                                               {"members", member_names}}};
}

// === NPC Interaction: builder implementations ===

auto make_shop_buy_response(uint32_t seq,
                            bool success,
                            std::string_view item_name,
                            int16_t count,
                            int32_t price_paid,
                            int64_t gold_remaining,
                            std::optional<std::string_view> error) -> json_message
{
    nlohmann::json data;
    data["success"] = success;
    if (success)
    {
        data["item_name"] = std::string(item_name);
        data["count"] = count;
        data["price_paid"] = price_paid;
        data["gold_remaining"] = gold_remaining;
    }
    if (!success && error.has_value())
    {
        data["error"] = std::string(*error);
    }
    return json_message{.type = json_message_type::shop_buy_response, .seq = seq, .data = std::move(data)};
}

auto make_shop_sell_response(uint32_t seq,
                             bool success,
                             std::string_view item_name,
                             int32_t offered_price,
                             int16_t durability,
                             std::optional<std::string_view> error) -> json_message
{
    nlohmann::json data;
    data["success"] = success;
    if (success)
    {
        data["item_name"] = std::string(item_name);
        data["offered_price"] = offered_price;
        data["durability"] = durability;
    }
    if (!success && error.has_value())
    {
        data["error"] = std::string(*error);
    }
    return json_message{.type = json_message_type::shop_sell_response, .seq = seq, .data = std::move(data)};
}

auto make_shop_sell_confirm_response(uint32_t seq,
                                     bool success,
                                     int32_t gold_received,
                                     int64_t gold_total,
                                     std::optional<std::string_view> error) -> json_message
{
    nlohmann::json data;
    data["success"] = success;
    if (success)
    {
        data["gold_received"] = gold_received;
        data["gold_total"] = gold_total;
    }
    if (!success && error.has_value())
    {
        data["error"] = std::string(*error);
    }
    return json_message{.type = json_message_type::shop_sell_confirm_response, .seq = seq, .data = std::move(data)};
}

auto make_shop_repair_response(uint32_t seq,
                               bool success,
                               std::string_view item_name,
                               int32_t repair_cost,
                               int16_t durability,
                               std::optional<std::string_view> error) -> json_message
{
    nlohmann::json data;
    data["success"] = success;
    if (success)
    {
        data["item_name"] = std::string(item_name);
        data["repair_cost"] = repair_cost;
        data["durability"] = durability;
    }
    if (!success && error.has_value())
    {
        data["error"] = std::string(*error);
    }
    return json_message{.type = json_message_type::shop_repair_response, .seq = seq, .data = std::move(data)};
}

auto make_shop_repair_confirm_response(uint32_t seq,
                                       bool success,
                                       int16_t new_durability,
                                       int32_t gold_spent,
                                       int64_t gold_remaining,
                                       std::optional<std::string_view> error) -> json_message
{
    nlohmann::json data;
    data["success"] = success;
    if (success)
    {
        data["new_durability"] = new_durability;
        data["gold_spent"] = gold_spent;
        data["gold_remaining"] = gold_remaining;
    }
    if (!success && error.has_value())
    {
        data["error"] = std::string(*error);
    }
    return json_message{.type = json_message_type::shop_repair_confirm_response, .seq = seq, .data = std::move(data)};
}

auto make_bank_deposit_response(uint32_t seq,
                                bool success,
                                std::string_view item_name,
                                std::optional<std::string_view> error) -> json_message
{
    nlohmann::json data;
    data["success"] = success;
    if (success)
    {
        data["item_name"] = std::string(item_name);
    }
    if (!success && error.has_value())
    {
        data["error"] = std::string(*error);
    }
    return json_message{.type = json_message_type::bank_deposit_response, .seq = seq, .data = std::move(data)};
}

auto make_bank_withdraw_response(uint32_t seq,
                                 bool success,
                                 std::string_view item_name,
                                 std::optional<std::string_view> error) -> json_message
{
    nlohmann::json data;
    data["success"] = success;
    if (success)
    {
        data["item_name"] = std::string(item_name);
    }
    if (!success && error.has_value())
    {
        data["error"] = std::string(*error);
    }
    return json_message{.type = json_message_type::bank_withdraw_response, .seq = seq, .data = std::move(data)};
}

auto dialog_option_msg::to_json() const -> nlohmann::json
{
    nlohmann::json j;
    j["label"] = label;
    j["action"] = action;
    if (!next_node.empty())
    {
        j["next_node"] = next_node;
    }
    return j;
}

auto make_dialog_choice_response(uint32_t seq,
                                 bool success,
                                 std::string_view action,
                                 std::string_view node_id,
                                 std::string_view text,
                                 const std::vector<dialog_option_msg>& options,
                                 std::optional<std::string_view> error) -> json_message
{
    nlohmann::json data;
    data["success"] = success;
    if (success)
    {
        data["action"] = std::string(action);
        if (!node_id.empty())
        {
            data["node_id"] = std::string(node_id);
        }
        if (!text.empty())
        {
            data["text"] = std::string(text);
        }
        if (!options.empty())
        {
            auto opts = nlohmann::json::array();
            for (const auto& opt : options)
            {
                opts.push_back(opt.to_json());
            }
            data["options"] = std::move(opts);
        }
    }
    if (!success && error.has_value())
    {
        data["error"] = std::string(*error);
    }
    return json_message{.type = json_message_type::dialog_choice_response, .seq = seq, .data = std::move(data)};
}

// === Equipment: Equip/Unequip ===

auto player_equip_request_data::from_json(const nlohmann::json& j) -> result<player_equip_request_data, std::string>
{
    try
    {
        player_equip_request_data data;
        if (!j.contains("item_id") || !j["item_id"].is_number())
        {
            return result<player_equip_request_data, std::string>::err("Missing or invalid 'item_id'");
        }
        data.item_id = j["item_id"].get<uint32_t>();

        if (!j.contains("target_slot") || !j["target_slot"].is_number())
        {
            return result<player_equip_request_data, std::string>::err("Missing or invalid 'target_slot'");
        }
        data.target_slot = j["target_slot"].get<uint8_t>();

        return result<player_equip_request_data, std::string>::ok(std::move(data));
    }
    catch (const nlohmann::json::exception& e)
    {
        return result<player_equip_request_data, std::string>::err(std::string("Parse error: ") + e.what());
    }
}

auto player_unequip_request_data::from_json(const nlohmann::json& j) -> result<player_unequip_request_data, std::string>
{
    try
    {
        player_unequip_request_data data;
        if (!j.contains("equip_slot") || !j["equip_slot"].is_number())
        {
            return result<player_unequip_request_data, std::string>::err("Missing or invalid 'equip_slot'");
        }
        data.equip_slot = j["equip_slot"].get<uint8_t>();
        return result<player_unequip_request_data, std::string>::ok(std::move(data));
    }
    catch (const nlohmann::json::exception& e)
    {
        return result<player_unequip_request_data, std::string>::err(std::string("Parse error: ") + e.what());
    }
}

auto equipment_change_broadcast_data::to_json() const -> nlohmann::json
{
    return nlohmann::json{{"entity_id", entity_id}, {"slot", slot}, {"item_id", item_id}, {"template_id", template_id}};
}

auto stat_update_data::to_json() const -> nlohmann::json
{
    auto j = nlohmann::json{{"max_hp", max_hp},
                            {"max_mp", max_mp},
                            {"max_sp", max_sp},
                            {"attack_power", attack_power},
                            {"magic_power", magic_power},
                            {"defense", defense},
                            {"magic_defense", magic_defense},
                            {"hit_rate", hit_rate},
                            {"dodge_rate", dodge_rate},
                            {"critical_rate", critical_rate},
                            {"max_weight", max_weight}};

    if (hp)
        j["hp"] = *hp;
    if (mp)
        j["mp"] = *mp;
    if (sp)
        j["sp"] = *sp;
    if (experience)
        j["experience"] = *experience;
    if (gold)
        j["gold"] = *gold;
    if (level)
        j["level"] = *level;
    if (pk_count)
        j["pk_count"] = *pk_count;
    if (hunger_level)
        j["hunger_level"] = *hunger_level;
    if (contribution)
        j["contribution"] = *contribution;
    if (enemy_kill_count)
        j["enemy_kill_count"] = *enemy_kill_count;

    return j;
}

auto make_equipment_change_broadcast(const equipment_change_broadcast_data& data) -> json_message
{
    return json_message{.type = json_message_type::equipment_change_broadcast, .seq = 0, .data = data.to_json()};
}

auto make_stat_update(const stat_update_data& data) -> json_message
{
    return json_message{.type = json_message_type::stat_update, .seq = 0, .data = data.to_json()};
}

auto experience_update_data::to_json() const -> nlohmann::json
{
    auto j = nlohmann::json{{"experience_gained", experience_gained}, {"experience", experience}, {"level", level}};

    if (levels_gained)
        j["levels_gained"] = *levels_gained;
    if (max_hp)
        j["max_hp"] = *max_hp;
    if (max_mp)
        j["max_mp"] = *max_mp;
    if (max_sp)
        j["max_sp"] = *max_sp;
    if (stat_points)
        j["stat_points"] = *stat_points;

    return j;
}

auto make_experience_update(const experience_update_data& data) -> json_message
{
    return json_message{.type = json_message_type::experience_update, .seq = 0, .data = data.to_json()};
}

auto dynamic_object_spawn_data::to_json() const -> nlohmann::json
{
    return nlohmann::json{{"object_id", object_id}, {"object_type", object_type}, {"x", x}, {"y", y}};
}

auto dynamic_object_removed_data::to_json() const -> nlohmann::json
{
    return nlohmann::json{{"object_id", object_id}, {"object_type", object_type}, {"x", x}, {"y", y}};
}

auto make_dynamic_object_spawn(const dynamic_object_spawn_data& data) -> json_message
{
    return json_message{.type = json_message_type::dynamic_object_spawn, .seq = 0, .data = data.to_json()};
}

auto make_dynamic_object_removed(const dynamic_object_removed_data& data) -> json_message
{
    return json_message{.type = json_message_type::dynamic_object_removed, .seq = 0, .data = data.to_json()};
}

// === Inventory Reposition ===

auto inventory_reposition_request_data::from_json(const nlohmann::json& j) -> result<inventory_reposition_request_data, std::string>
{
    if (!j.contains("item_id") || !j["item_id"].is_number())
    {
        return result<inventory_reposition_request_data, std::string>::err("Missing item_id");
    }

    inventory_reposition_request_data data;
    data.item_id = j["item_id"].get<uint32_t>();
    if (j.contains("pos_x") && j["pos_x"].is_number())
    {
        data.pos_x = static_cast<int16_t>(j["pos_x"].get<int>());
    }
    if (j.contains("pos_y") && j["pos_y"].is_number())
    {
        data.pos_y = static_cast<int16_t>(j["pos_y"].get<int>());
    }
    return result<inventory_reposition_request_data, std::string>::ok(data);
}

// === Drop Item ===

auto drop_item_request_data::from_json(const nlohmann::json& j) -> result<drop_item_request_data, std::string>
{
    if (!j.contains("item_id") || !j["item_id"].is_number())
    {
        return result<drop_item_request_data, std::string>::err("Missing item_id");
    }

    drop_item_request_data data;
    data.item_id = j["item_id"].get<uint32_t>();
    return result<drop_item_request_data, std::string>::ok(data);
}

auto make_player_drop_item_response(uint32_t seq, bool success, std::string_view error) -> json_message
{
    nlohmann::json j;
    j["success"] = success;
    if (!success && !error.empty())
    {
        j["error"] = std::string(error);
    }
    return json_message{.type = json_message_type::player_drop_item_response, .seq = seq, .data = j};
}

auto stat_point_request_data::from_json(const nlohmann::json& j) -> result<stat_point_request_data, std::string>
{
    try
    {
        stat_point_request_data data;
        if (!j.contains("stat") || !j["stat"].is_number())
        {
            return result<stat_point_request_data, std::string>::err("Missing or invalid 'stat'");
        }
        data.stat = j["stat"].get<int16_t>();
        if (data.stat < 0 || data.stat > 5)
        {
            return result<stat_point_request_data, std::string>::err("'stat' out of range (0-5)");
        }
        return result<stat_point_request_data, std::string>::ok(std::move(data));
    }
    catch (const nlohmann::json::exception& e)
    {
        return result<stat_point_request_data, std::string>::err(e.what());
    }
}

auto make_stat_point_response(
    uint32_t seq, bool success, int16_t stat, int16_t remaining, std::string_view error) -> json_message
{
    nlohmann::json data{{"success", success}, {"stat", stat}, {"points_remaining", remaining}};
    if (!error.empty())
    {
        data["error"] = std::string(error);
    }
    return json_message{.type = json_message_type::stat_point_response, .seq = seq, .data = std::move(data)};
}

auto make_inventory_item_update(const inventory_item_msg& item) -> json_message
{
    return json_message{.type = json_message_type::inventory_item_update, .seq = 0, .data = item.to_json()};
}

auto make_inventory_item_removed(uint32_t item_id) -> json_message
{
    return json_message{
        .type = json_message_type::inventory_item_removed, .seq = 0, .data = nlohmann::json{{"item_id", item_id}}};
}

auto make_inventory_weight_update(int32_t current_weight, int32_t max_weight) -> json_message
{
    return json_message{.type = json_message_type::inventory_weight_update,
                        .seq = 0,
                        .data = nlohmann::json{{"current_weight", current_weight}, {"max_weight", max_weight}}};
}

auto make_bank_slot_update(int16_t page, int16_t slot, const inventory_item_msg* item) -> json_message
{
    nlohmann::json j;
    j["page"] = page;
    j["slot"] = slot;
    if (item)
    {
        j["item"] = item->to_json();
    }
    else
    {
        j["item"] = nullptr;
    }
    return json_message{.type = json_message_type::bank_slot_update, .seq = 0, .data = j};
}

auto gold_update_data::to_json() const -> nlohmann::json
{
    return nlohmann::json{{"gold", gold}, {"change", change}, {"reason", reason}};
}

auto make_gold_update(const gold_update_data& data) -> json_message
{
    return json_message{.type = json_message_type::gold_update, .seq = 0, .data = data.to_json()};
}

auto build_inventory_item_msg(
    item_id iid,
    const item::item_system* items,
    const item_registry* registry,
    const inventory::inventory_entry* entry) -> std::optional<inventory_item_msg>
{
    if (!items)
    {
        return std::nullopt;
    }

    auto* itm = items->get_item(iid);
    if (!itm)
    {
        return std::nullopt;
    }

    inventory_item_msg msg{
        .item_id = iid.value,
        .name = itm->name,
        .count = itm->count,
        .durability = static_cast<int16_t>(itm->durability),
        .max_durability = static_cast<int16_t>(itm->max_durability),
        .attribute = itm->attribute,
    };

    if (entry)
    {
        msg.pos_x = entry->pos_x;
        msg.pos_y = entry->pos_y;
        msg.z_order = entry->z_order;
    }

    if (registry)
    {
        if (auto* tmpl = registry->get(itm->template_id))
        {
            msg.name = get_display_name(tmpl->name, itm->attribute);
            msg.item_type = static_cast<uint8_t>(tmpl->type);
            msg.equip_pos = static_cast<uint8_t>(tmpl->equip_pos);
            msg.sprite = tmpl->sprite;
            msg.sprite_frame = tmpl->sprite_frame;
            msg.color = tmpl->item_color;
            msg.weight = tmpl->weight;
            msg.level_limit = tmpl->level_limit;
        }
    }

    return msg;
}

auto make_spell_list_update(const std::vector<known_spell_msg>& spells) -> json_message
{
    auto spell_array = nlohmann::json::array();
    for (const auto& s : spells)
    {
        spell_array.push_back(s.to_json());
    }
    return json_message{.type = json_message_type::spell_list_update,
                        .seq = 0,
                        .data = nlohmann::json{{"spells", std::move(spell_array)}}};
}

// === Render Mode ===

auto render_mode_data::to_json() const -> nlohmann::json
{
    return nlohmann::json{
        {"mode", std::string(to_string(mode))}, {"fair_width", fair_width}, {"fair_height", fair_height}};
}

auto make_set_render_mode(const render_mode_data& data) -> json_message
{
    return json_message{.type = json_message_type::set_render_mode, .seq = 0, .data = data.to_json()};
}

auto make_view_range_update(int16_t radius_x, int16_t radius_y, bool sees_all) -> json_message
{
    return json_message{.type = json_message_type::view_range_update,
                        .seq = 0,
                        .data = nlohmann::json{{"radius_x", radius_x}, {"radius_y", radius_y}, {"sees_all", sees_all}}};
}

// === Crafting: Manufacturing/Alchemy ===

auto manufacture_request_data::from_json(const nlohmann::json& j) -> result<manufacture_request_data, std::string>
{
    manufacture_request_data data;
    if (j.contains("recipe_index") && j["recipe_index"].is_number())
    {
        data.recipe_index = j["recipe_index"].get<int32_t>();
    }
    else
    {
        return result<manufacture_request_data, std::string>::err("Missing recipe_index");
    }
    return result<manufacture_request_data, std::string>::ok(data);
}

auto alchemy_request_data::from_json(const nlohmann::json& j) -> result<alchemy_request_data, std::string>
{
    alchemy_request_data data;
    if (j.contains("recipe_id") && j["recipe_id"].is_number())
    {
        data.recipe_id = j["recipe_id"].get<int32_t>();
    }
    else
    {
        return result<alchemy_request_data, std::string>::err("Missing recipe_id");
    }
    return result<alchemy_request_data, std::string>::ok(data);
}

auto make_manufacture_list_response(uint32_t seq, const nlohmann::json& recipes) -> json_message
{
    return json_message{
        .type = json_message_type::manufacture_list_response, .seq = seq, .data = {{"recipes", recipes}}};
}

auto make_manufacture_response(uint32_t seq,
                               bool success,
                               std::string_view item_name,
                               std::optional<std::string_view> error) -> json_message
{
    nlohmann::json data;
    data["success"] = success;
    if (!item_name.empty())
        data["item_name"] = std::string(item_name);
    if (error)
        data["error"] = std::string(*error);
    return json_message{.type = json_message_type::manufacture_response, .seq = seq, .data = data};
}

auto make_alchemy_list_response(uint32_t seq, const nlohmann::json& recipes) -> json_message
{
    return json_message{.type = json_message_type::alchemy_list_response, .seq = seq, .data = {{"recipes", recipes}}};
}

auto make_alchemy_response(uint32_t seq,
                           bool success,
                           std::string_view item_name,
                           std::optional<std::string_view> error) -> json_message
{
    nlohmann::json data;
    data["success"] = success;
    if (!item_name.empty())
        data["item_name"] = std::string(item_name);
    if (error)
        data["error"] = std::string(*error);
    return json_message{.type = json_message_type::alchemy_response, .seq = seq, .data = data};
}

// === Mining ===

auto mine_request_data::from_json(const nlohmann::json& j) -> result<mine_request_data, std::string>
{
    mine_request_data data;
    if (j.contains("target_x") && j["target_x"].is_number())
    {
        data.target_x = j["target_x"].get<int16_t>();
    }
    else
    {
        return result<mine_request_data, std::string>::err("Missing target_x");
    }
    if (j.contains("target_y") && j["target_y"].is_number())
    {
        data.target_y = j["target_y"].get<int16_t>();
    }
    else
    {
        return result<mine_request_data, std::string>::err("Missing target_y");
    }
    return result<mine_request_data, std::string>::ok(data);
}

auto make_mine_response(uint32_t seq,
                        bool success,
                        std::string_view item_name,
                        int32_t template_id,
                        bool node_depleted,
                        std::optional<std::string_view> error) -> json_message
{
    nlohmann::json data;
    data["success"] = success;
    if (!item_name.empty())
        data["item_name"] = std::string(item_name);
    if (template_id > 0)
        data["template_id"] = template_id;
    if (node_depleted)
        data["node_depleted"] = true;
    if (error)
        data["error"] = std::string(*error);
    return json_message{.type = json_message_type::mine_response, .seq = seq, .data = data};
}

auto make_mineral_spawn(uint32_t node_id, uint8_t mineral_type, int16_t x, int16_t y) -> json_message
{
    return json_message{.type = json_message_type::mineral_spawn,
                        .seq = 0,
                        .data = {{"node_id", node_id}, {"mineral_type", mineral_type}, {"x", x}, {"y", y}}};
}

auto make_mineral_despawn(uint32_t node_id, int16_t x, int16_t y) -> json_message
{
    return json_message{
        .type = json_message_type::mineral_despawn, .seq = 0, .data = {{"node_id", node_id}, {"x", x}, {"y", y}}};
}

// === Fishing ===

auto make_fish_skill_response(uint32_t seq, bool success, std::optional<std::string_view> error) -> json_message
{
    nlohmann::json data;
    data["success"] = success;
    if (error)
        data["error"] = std::string(*error);
    return json_message{.type = json_message_type::fish_skill_response, .seq = seq, .data = data};
}

auto make_fish_engaged([[maybe_unused]] entity_id player_eid,
                       std::string_view fish_name,
                       uint8_t visual_type,
                       int32_t initial_chance) -> json_message
{
    return json_message{.type = json_message_type::fish_engaged,
                        .seq = 0,
                        .data = {{"fish_name", std::string(fish_name)},
                                 {"visual_type", visual_type},
                                 {"catch_chance", initial_chance}}};
}

auto make_fish_chance_update(entity_id /*player_eid*/, int32_t catch_chance) -> json_message
{
    return json_message{
        .type = json_message_type::fish_chance_update, .seq = 0, .data = {{"catch_chance", catch_chance}}};
}

auto make_fish_catch_response(entity_id /*player_eid*/,
                              std::string_view result_str,
                              std::string_view item_name,
                              int32_t template_id) -> json_message
{
    nlohmann::json data;
    data["result"] = std::string(result_str);
    if (!item_name.empty())
        data["item_name"] = std::string(item_name);
    if (template_id > 0)
        data["template_id"] = template_id;
    return json_message{.type = json_message_type::fish_catch_response, .seq = 0, .data = data};
}

auto make_fish_spawn_broadcast(uint32_t fish_index, uint8_t visual_type, int16_t x, int16_t y) -> json_message
{
    return json_message{.type = json_message_type::fish_spawn_broadcast,
                        .seq = 0,
                        .data = {{"fish_index", fish_index}, {"visual_type", visual_type}, {"x", x}, {"y", y}}};
}

auto make_fish_despawn_broadcast(uint32_t fish_index, int16_t x, int16_t y) -> json_message
{
    return json_message{.type = json_message_type::fish_despawn_broadcast,
                        .seq = 0,
                        .data = {{"fish_index", fish_index}, {"x", x}, {"y", y}}};
}

// Environment update

auto environment_update_data::to_json() const -> nlohmann::json
{
    return nlohmann::json{{"hour", hour}, {"minute", minute}, {"is_day", is_day}, {"weather", weather}};
}

auto make_environment_update(const environment_update_data& data) -> json_message
{
    return json_message{.type = json_message_type::environment_update, .seq = 0, .data = data.to_json()};
}

// === Death/Respawn ===

auto respawn_response_data::to_json() const -> nlohmann::json
{
    nlohmann::json j = {{"success", success}, {"map", map_name}, {"x", x}, {"y", y}};
    if (!error.empty())
    {
        j["error"] = error;
    }
    return j;
}

auto make_respawn_response(uint32_t seq,
                           bool success,
                           std::string_view map_name,
                           int16_t x,
                           int16_t y,
                           std::optional<std::string_view> error) -> json_message
{
    respawn_response_data data;
    data.success = success;
    data.map_name = std::string(map_name);
    data.x = x;
    data.y = y;
    if (error)
        data.error = std::string(*error);

    return json_message{.type = json_message_type::respawn_response, .seq = seq, .data = data.to_json()};
}

// === Admin Web Tool ===

auto make_enter_admin_mode_response(uint32_t seq,
                                    bool success,
                                    uint8_t admin_level,
                                    std::optional<std::string_view> error) -> json_message
{
    nlohmann::json data = {{"success", success}};
    if (success)
    {
        data["admin_level"] = admin_level;
    }
    if (error)
    {
        data["error"] = std::string(*error);
    }
    return json_message{.type = json_message_type::enter_admin_mode_response, .seq = seq, .data = data};
}

auto admin_get_player_request_data::from_json(const nlohmann::json& j)
    -> result<admin_get_player_request_data, std::string>
{
    admin_get_player_request_data data;
    if (j.contains("player_name") && j["player_name"].is_string())
    {
        data.player_name = j["player_name"].get<std::string>();
    }
    if (j.contains("player_id") && j["player_id"].is_number())
    {
        data.player_id = j["player_id"].get<uint32_t>();
    }
    if (data.player_name.empty() && data.player_id == 0)
    {
        return result<admin_get_player_request_data, std::string>::err("player_name or player_id required");
    }
    return result<admin_get_player_request_data, std::string>::ok(std::move(data));
}

auto admin_kick_player_request_data::from_json(const nlohmann::json& j)
    -> result<admin_kick_player_request_data, std::string>
{
    admin_kick_player_request_data data;
    if (!j.contains("player_name") || !j["player_name"].is_string())
    {
        return result<admin_kick_player_request_data, std::string>::err("player_name required");
    }
    data.player_name = j["player_name"].get<std::string>();
    if (j.contains("reason") && j["reason"].is_string())
    {
        data.reason = j["reason"].get<std::string>();
    }
    return result<admin_kick_player_request_data, std::string>::ok(std::move(data));
}

auto admin_ban_player_request_data::from_json(const nlohmann::json& j)
    -> result<admin_ban_player_request_data, std::string>
{
    admin_ban_player_request_data data;
    if (!j.contains("player_name") || !j["player_name"].is_string())
    {
        return result<admin_ban_player_request_data, std::string>::err("player_name required");
    }
    data.player_name = j["player_name"].get<std::string>();
    if (j.contains("reason") && j["reason"].is_string())
    {
        data.reason = j["reason"].get<std::string>();
    }
    if (j.contains("duration_hours") && j["duration_hours"].is_number())
    {
        data.duration_hours = j["duration_hours"].get<int32_t>();
    }
    return result<admin_ban_player_request_data, std::string>::ok(std::move(data));
}

auto admin_unban_player_request_data::from_json(const nlohmann::json& j)
    -> result<admin_unban_player_request_data, std::string>
{
    admin_unban_player_request_data data;
    if (!j.contains("player_name") || !j["player_name"].is_string())
    {
        return result<admin_unban_player_request_data, std::string>::err("player_name required");
    }
    data.player_name = j["player_name"].get<std::string>();
    return result<admin_unban_player_request_data, std::string>::ok(std::move(data));
}

auto admin_teleport_player_request_data::from_json(const nlohmann::json& j)
    -> result<admin_teleport_player_request_data, std::string>
{
    admin_teleport_player_request_data data;
    if (!j.contains("player_name") || !j["player_name"].is_string())
    {
        return result<admin_teleport_player_request_data, std::string>::err("player_name required");
    }
    data.player_name = j["player_name"].get<std::string>();
    if (!j.contains("dest_map") || !j["dest_map"].is_string())
    {
        return result<admin_teleport_player_request_data, std::string>::err("dest_map required");
    }
    data.dest_map = j["dest_map"].get<std::string>();
    data.dest_x = safe_int16(j, "dest_x");
    data.dest_y = safe_int16(j, "dest_y");
    return result<admin_teleport_player_request_data, std::string>::ok(std::move(data));
}

auto admin_modify_player_request_data::from_json(const nlohmann::json& j)
    -> result<admin_modify_player_request_data, std::string>
{
    admin_modify_player_request_data data;
    if (!j.contains("player_name") || !j["player_name"].is_string())
    {
        return result<admin_modify_player_request_data, std::string>::err("player_name required");
    }
    data.player_name = j["player_name"].get<std::string>();
    if (!j.contains("modifications") || !j["modifications"].is_object())
    {
        return result<admin_modify_player_request_data, std::string>::err("modifications required");
    }
    data.modifications = j["modifications"];
    return result<admin_modify_player_request_data, std::string>::ok(std::move(data));
}

auto admin_get_map_request_data::from_json(const nlohmann::json& j) -> result<admin_get_map_request_data, std::string>
{
    admin_get_map_request_data data;
    if (!j.contains("map_name") || !j["map_name"].is_string())
    {
        return result<admin_get_map_request_data, std::string>::err("map_name required");
    }
    data.map_name = j["map_name"].get<std::string>();
    return result<admin_get_map_request_data, std::string>::ok(std::move(data));
}

auto admin_spawn_npc_request_data::from_json(const nlohmann::json& j)
    -> result<admin_spawn_npc_request_data, std::string>
{
    admin_spawn_npc_request_data data;
    if (!j.contains("npc_name") || !j["npc_name"].is_string())
    {
        return result<admin_spawn_npc_request_data, std::string>::err("npc_name required");
    }
    data.npc_name = j["npc_name"].get<std::string>();
    if (!j.contains("map_name") || !j["map_name"].is_string())
    {
        return result<admin_spawn_npc_request_data, std::string>::err("map_name required");
    }
    data.map_name = j["map_name"].get<std::string>();
    data.x = safe_int16(j, "x");
    data.y = safe_int16(j, "y");
    if (j.contains("count") && j["count"].is_number())
    {
        data.count = safe_int16(j, "count", 1);
    }
    return result<admin_spawn_npc_request_data, std::string>::ok(std::move(data));
}

auto admin_kill_npc_request_data::from_json(const nlohmann::json& j) -> result<admin_kill_npc_request_data, std::string>
{
    admin_kill_npc_request_data data;
    if (!j.contains("entity_id") || !j["entity_id"].is_number())
    {
        return result<admin_kill_npc_request_data, std::string>::err("entity_id required");
    }
    data.entity_id = j["entity_id"].get<uint32_t>();
    return result<admin_kill_npc_request_data, std::string>::ok(std::move(data));
}

auto admin_get_inventory_request_data::from_json(const nlohmann::json& j)
    -> result<admin_get_inventory_request_data, std::string>
{
    admin_get_inventory_request_data data;
    if (!j.contains("player_name") || !j["player_name"].is_string())
    {
        return result<admin_get_inventory_request_data, std::string>::err("player_name required");
    }
    data.player_name = j["player_name"].get<std::string>();
    return result<admin_get_inventory_request_data, std::string>::ok(std::move(data));
}

auto admin_give_item_request_data::from_json(const nlohmann::json& j)
    -> result<admin_give_item_request_data, std::string>
{
    admin_give_item_request_data data;
    if (!j.contains("player_name") || !j["player_name"].is_string())
    {
        return result<admin_give_item_request_data, std::string>::err("player_name required");
    }
    data.player_name = j["player_name"].get<std::string>();
    if (!j.contains("item_template_id") || !j["item_template_id"].is_number())
    {
        return result<admin_give_item_request_data, std::string>::err("item_template_id required");
    }
    data.item_template_id = j["item_template_id"].get<uint32_t>();
    data.count = safe_int16(j, "count", 1);
    if (j.contains("attribute") && j["attribute"].is_object())
    {
        data.attribute = item::item_attribute::from_json(j["attribute"]);
    }
    return result<admin_give_item_request_data, std::string>::ok(std::move(data));
}

auto admin_remove_item_request_data::from_json(const nlohmann::json& j)
    -> result<admin_remove_item_request_data, std::string>
{
    admin_remove_item_request_data data;
    if (!j.contains("player_name") || !j["player_name"].is_string())
    {
        return result<admin_remove_item_request_data, std::string>::err("player_name required");
    }
    data.player_name = j["player_name"].get<std::string>();
    if (!j.contains("item_id") || !j["item_id"].is_number())
    {
        return result<admin_remove_item_request_data, std::string>::err("item_id required");
    }
    data.item_id = j["item_id"].get<uint32_t>();
    data.count = safe_int16(j, "count");
    return result<admin_remove_item_request_data, std::string>::ok(std::move(data));
}

auto admin_get_guild_request_data::from_json(const nlohmann::json& j)
    -> result<admin_get_guild_request_data, std::string>
{
    admin_get_guild_request_data data;
    if (!j.contains("guild_name") || !j["guild_name"].is_string())
    {
        return result<admin_get_guild_request_data, std::string>::err("guild_name required");
    }
    data.guild_name = j["guild_name"].get<std::string>();
    return result<admin_get_guild_request_data, std::string>::ok(std::move(data));
}

auto admin_get_account_request_data::from_json(const nlohmann::json& j)
    -> result<admin_get_account_request_data, std::string>
{
    admin_get_account_request_data data;
    if (!j.contains("username") || !j["username"].is_string())
    {
        return result<admin_get_account_request_data, std::string>::err("username required");
    }
    data.username = j["username"].get<std::string>();
    return result<admin_get_account_request_data, std::string>::ok(std::move(data));
}

auto admin_subscribe_map_request_data::from_json(const nlohmann::json& j)
    -> result<admin_subscribe_map_request_data, std::string>
{
    admin_subscribe_map_request_data data;
    if (!j.contains("map_name") || !j["map_name"].is_string())
    {
        return result<admin_subscribe_map_request_data, std::string>::err("map_name required");
    }
    data.map_name = j["map_name"].get<std::string>();
    return result<admin_subscribe_map_request_data, std::string>::ok(std::move(data));
}

auto admin_get_map_data_request_data::from_json(const nlohmann::json& j)
    -> result<admin_get_map_data_request_data, std::string>
{
    admin_get_map_data_request_data data;
    if (!j.contains("map_name") || !j["map_name"].is_string())
    {
        return result<admin_get_map_data_request_data, std::string>::err("map_name required");
    }
    data.map_name = j["map_name"].get<std::string>();
    return result<admin_get_map_data_request_data, std::string>::ok(std::move(data));
}

auto admin_subscribe_player_request_data::from_json(const nlohmann::json& j)
    -> result<admin_subscribe_player_request_data, std::string>
{
    admin_subscribe_player_request_data data;
    if (!j.contains("player_name") || !j["player_name"].is_string())
    {
        return result<admin_subscribe_player_request_data, std::string>::err("player_name required");
    }
    data.player_name = j["player_name"].get<std::string>();
    return result<admin_subscribe_player_request_data, std::string>::ok(std::move(data));
}

auto make_admin_response(json_message_type type,
                         uint32_t seq,
                         bool success,
                         const nlohmann::json& data,
                         std::optional<std::string_view> error) -> json_message
{
    nlohmann::json j = data;
    j["success"] = success;
    if (error)
    {
        j["error"] = std::string(*error);
    }
    return json_message{.type = type, .seq = seq, .data = j};
}

auto make_admin_player_connected(const std::string& name, int16_t level, const std::string& map_name) -> json_message
{
    return json_message{.type = json_message_type::admin_player_connected,
                        .seq = 0,
                        .data = nlohmann::json{{"name", name}, {"level", level}, {"map", map_name}}};
}

auto make_admin_player_disconnected(const std::string& name) -> json_message
{
    return json_message{
        .type = json_message_type::admin_player_disconnected, .seq = 0, .data = nlohmann::json{{"name", name}}};
}

auto make_admin_chat_log(const std::string& channel,
                         const std::string& sender,
                         const std::string& content) -> json_message
{
    return json_message{.type = json_message_type::admin_chat_log,
                        .seq = 0,
                        .data = nlohmann::json{{"channel", channel}, {"sender", sender}, {"content", content}}};
}

// === Admin expanded request data from_json ===

auto admin_broadcast_request_data::from_json(const nlohmann::json& j)
    -> result<admin_broadcast_request_data, std::string>
{
    admin_broadcast_request_data data;
    if (!j.contains("message") || !j["message"].is_string())
    {
        return result<admin_broadcast_request_data, std::string>::err("message required");
    }
    data.message = j["message"].get<std::string>();
    return result<admin_broadcast_request_data, std::string>::ok(std::move(data));
}

auto admin_mute_player_request_data::from_json(const nlohmann::json& j)
    -> result<admin_mute_player_request_data, std::string>
{
    admin_mute_player_request_data data;
    if (!j.contains("player_name") || !j["player_name"].is_string())
    {
        return result<admin_mute_player_request_data, std::string>::err("player_name required");
    }
    data.player_name = j["player_name"].get<std::string>();
    if (j.contains("duration_minutes") && j["duration_minutes"].is_number())
    {
        data.duration_minutes = j["duration_minutes"].get<int32_t>();
    }
    return result<admin_mute_player_request_data, std::string>::ok(std::move(data));
}

auto admin_unmute_player_request_data::from_json(const nlohmann::json& j)
    -> result<admin_unmute_player_request_data, std::string>
{
    admin_unmute_player_request_data data;
    if (!j.contains("player_name") || !j["player_name"].is_string())
    {
        return result<admin_unmute_player_request_data, std::string>::err("player_name required");
    }
    data.player_name = j["player_name"].get<std::string>();
    return result<admin_unmute_player_request_data, std::string>::ok(std::move(data));
}

auto admin_get_item_template_request_data::from_json(const nlohmann::json& j)
    -> result<admin_get_item_template_request_data, std::string>
{
    admin_get_item_template_request_data data;
    if (j.contains("item_id") && j["item_id"].is_number())
    {
        data.item_id = j["item_id"].get<uint32_t>();
    }
    if (j.contains("item_name") && j["item_name"].is_string())
    {
        data.item_name = j["item_name"].get<std::string>();
    }
    if (data.item_id == 0 && data.item_name.empty())
    {
        return result<admin_get_item_template_request_data, std::string>::err("item_id or item_name required");
    }
    return result<admin_get_item_template_request_data, std::string>::ok(std::move(data));
}

auto admin_get_npc_template_request_data::from_json(const nlohmann::json& j)
    -> result<admin_get_npc_template_request_data, std::string>
{
    admin_get_npc_template_request_data data;
    if (j.contains("npc_id") && j["npc_id"].is_number())
    {
        data.npc_id = j["npc_id"].get<uint32_t>();
    }
    if (j.contains("npc_name") && j["npc_name"].is_string())
    {
        data.npc_name = j["npc_name"].get<std::string>();
    }
    if (data.npc_id == 0 && data.npc_name.empty())
    {
        return result<admin_get_npc_template_request_data, std::string>::err("npc_id or npc_name required");
    }
    return result<admin_get_npc_template_request_data, std::string>::ok(std::move(data));
}

auto admin_search_players_request_data::from_json(const nlohmann::json& j)
    -> result<admin_search_players_request_data, std::string>
{
    admin_search_players_request_data data;
    if (!j.contains("query") || !j["query"].is_string())
    {
        return result<admin_search_players_request_data, std::string>::err("query required");
    }
    data.query = j["query"].get<std::string>();
    if (j.contains("level_min") && j["level_min"].is_number())
        data.level_min = static_cast<int16_t>(j["level_min"].get<int>());
    if (j.contains("level_max") && j["level_max"].is_number())
        data.level_max = static_cast<int16_t>(j["level_max"].get<int>());
    if (j.contains("map_name") && j["map_name"].is_string())
        data.map_name = j["map_name"];
    if (j.contains("faction") && j["faction"].is_number())
        data.faction = j["faction"];
    if (j.contains("guild_name") && j["guild_name"].is_string())
        data.guild_name = j["guild_name"];
    return result<admin_search_players_request_data, std::string>::ok(std::move(data));
}

// === Admin Phase 3 from_json implementations ===

auto admin_get_audit_log_request_data::from_json(const nlohmann::json& j)
    -> result<admin_get_audit_log_request_data, std::string>
{
    admin_get_audit_log_request_data data;
    if (j.contains("count") && j["count"].is_number())
        data.count = j["count"];
    if (j.contains("executor_name") && j["executor_name"].is_string())
        data.executor_name = j["executor_name"];
    return result<admin_get_audit_log_request_data, std::string>::ok(std::move(data));
}

auto admin_set_config_request_data::from_json(const nlohmann::json& j)
    -> result<admin_set_config_request_data, std::string>
{
    admin_set_config_request_data data;
    if (!j.contains("values") || !j["values"].is_object())
    {
        return result<admin_set_config_request_data, std::string>::err("Missing 'values' object");
    }
    data.values = j["values"];
    return result<admin_set_config_request_data, std::string>::ok(std::move(data));
}

auto admin_cancel_scheduled_task_request_data::from_json(const nlohmann::json& j)
    -> result<admin_cancel_scheduled_task_request_data, std::string>
{
    admin_cancel_scheduled_task_request_data data;
    if (!j.contains("tag") || !j["tag"].is_string())
    {
        return result<admin_cancel_scheduled_task_request_data, std::string>::err("Missing 'tag' field");
    }
    data.tag = j["tag"];
    return result<admin_cancel_scheduled_task_request_data, std::string>::ok(std::move(data));
}

auto admin_run_query_request_data::from_json(const nlohmann::json& j)
    -> result<admin_run_query_request_data, std::string>
{
    admin_run_query_request_data data;
    if (!j.contains("query_name") || !j["query_name"].is_string())
    {
        return result<admin_run_query_request_data, std::string>::err("Missing 'query_name' field");
    }
    data.query_name = j["query_name"];
    if (j.contains("params") && j["params"].is_object())
        data.params = j["params"];
    return result<admin_run_query_request_data, std::string>::ok(std::move(data));
}

auto admin_list_map_npcs_request_data::from_json(const nlohmann::json& j)
    -> result<admin_list_map_npcs_request_data, std::string>
{
    admin_list_map_npcs_request_data data;
    if (!j.contains("map_name") || !j["map_name"].is_string())
    {
        return result<admin_list_map_npcs_request_data, std::string>::err("Missing 'map_name' field");
    }
    data.map_name = j["map_name"];
    return result<admin_list_map_npcs_request_data, std::string>::ok(std::move(data));
}

auto admin_list_map_ground_items_request_data::from_json(const nlohmann::json& j)
    -> result<admin_list_map_ground_items_request_data, std::string>
{
    admin_list_map_ground_items_request_data data;
    if (!j.contains("map_name") || !j["map_name"].is_string())
    {
        return result<admin_list_map_ground_items_request_data, std::string>::err("Missing 'map_name' field");
    }
    data.map_name = j["map_name"];
    return result<admin_list_map_ground_items_request_data, std::string>::ok(std::move(data));
}

auto admin_remove_ground_item_request_data::from_json(const nlohmann::json& j)
    -> result<admin_remove_ground_item_request_data, std::string>
{
    admin_remove_ground_item_request_data data;
    if (!j.contains("map_name") || !j["map_name"].is_string())
    {
        return result<admin_remove_ground_item_request_data, std::string>::err("Missing 'map_name' field");
    }
    if (!j.contains("x") || !j["x"].is_number() || !j.contains("y") || !j["y"].is_number() || !j.contains("item_id") ||
        !j["item_id"].is_number())
    {
        return result<admin_remove_ground_item_request_data, std::string>::err(
            "Missing required fields: x, y, item_id");
    }
    data.map_name = j["map_name"];
    data.x = safe_int16(j, "x");
    data.y = safe_int16(j, "y");
    data.item_id = j["item_id"];
    return result<admin_remove_ground_item_request_data, std::string>::ok(std::move(data));
}

auto admin_guild_action_request_data::from_json(const nlohmann::json& j)
    -> result<admin_guild_action_request_data, std::string>
{
    admin_guild_action_request_data data;
    if (!j.contains("guild_name") || !j["guild_name"].is_string())
    {
        return result<admin_guild_action_request_data, std::string>::err("Missing 'guild_name' field");
    }
    if (!j.contains("action") || !j["action"].is_string())
    {
        return result<admin_guild_action_request_data, std::string>::err("Missing 'action' field");
    }
    data.guild_name = j["guild_name"];
    data.action = j["action"];
    if (j.contains("target_player") && j["target_player"].is_string())
        data.target_player = j["target_player"];
    if (j.contains("rank") && j["rank"].is_string())
        data.rank = j["rank"];
    return result<admin_guild_action_request_data, std::string>::ok(std::move(data));
}

auto admin_message_player_request_data::from_json(const nlohmann::json& j)
    -> result<admin_message_player_request_data, std::string>
{
    admin_message_player_request_data data;
    if (!j.contains("player_name") || !j["player_name"].is_string())
    {
        return result<admin_message_player_request_data, std::string>::err("Missing 'player_name' field");
    }
    if (!j.contains("message") || !j["message"].is_string())
    {
        return result<admin_message_player_request_data, std::string>::err("Missing 'message' field");
    }
    data.player_name = j["player_name"];
    data.message = j["message"];
    return result<admin_message_player_request_data, std::string>::ok(std::move(data));
}

auto admin_set_environment_request_data::from_json(const nlohmann::json& j)
    -> result<admin_set_environment_request_data, std::string>
{
    admin_set_environment_request_data data;
    if (j.contains("map_name") && j["map_name"].is_string())
        data.map_name = j["map_name"];
    if (j.contains("weather") && j["weather"].is_number())
        data.weather = j["weather"].get<int>();
    if (j.contains("hour") && j["hour"].is_number())
        data.hour = j["hour"].get<int>();
    if (j.contains("minute") && j["minute"].is_number())
        data.minute = j["minute"].get<int>();
    return result<admin_set_environment_request_data, std::string>::ok(std::move(data));
}

auto admin_shutdown_server_request_data::from_json(const nlohmann::json& j)
    -> result<admin_shutdown_server_request_data, std::string>
{
    admin_shutdown_server_request_data data;
    if (j.contains("countdown_seconds") && j["countdown_seconds"].is_number())
        data.countdown_seconds = j["countdown_seconds"];
    if (j.contains("reason") && j["reason"].is_string())
        data.reason = j["reason"];
    if (j.contains("cancel") && j["cancel"].is_boolean())
        data.cancel = j["cancel"];
    return result<admin_shutdown_server_request_data, std::string>::ok(std::move(data));
}

// === Phase 4 from_json implementations ===

auto admin_modify_skills_request_data::from_json(const nlohmann::json& j)
    -> result<admin_modify_skills_request_data, std::string>
{
    admin_modify_skills_request_data data;
    if (!j.contains("player_name") || !j["player_name"].is_string())
        return result<admin_modify_skills_request_data, std::string>::err("Missing 'player_name'");
    if (!j.contains("action") || !j["action"].is_string())
        return result<admin_modify_skills_request_data, std::string>::err("Missing 'action'");
    data.player_name = j["player_name"];
    data.action = j["action"];
    if (j.contains("skill_type") && j["skill_type"].is_number())
        data.skill_type = j["skill_type"];
    if (j.contains("value") && j["value"].is_number())
        data.value = j["value"];
    return result<admin_modify_skills_request_data, std::string>::ok(std::move(data));
}

auto admin_modify_spells_request_data::from_json(const nlohmann::json& j)
    -> result<admin_modify_spells_request_data, std::string>
{
    admin_modify_spells_request_data data;
    if (!j.contains("player_name") || !j["player_name"].is_string())
        return result<admin_modify_spells_request_data, std::string>::err("Missing 'player_name'");
    if (!j.contains("action") || !j["action"].is_string())
        return result<admin_modify_spells_request_data, std::string>::err("Missing 'action'");
    data.player_name = j["player_name"];
    data.action = j["action"];
    if (j.contains("spell_id") && j["spell_id"].is_number())
        data.spell_id = j["spell_id"];
    return result<admin_modify_spells_request_data, std::string>::ok(std::move(data));
}

auto admin_get_player_quests_request_data::from_json(const nlohmann::json& j)
    -> result<admin_get_player_quests_request_data, std::string>
{
    admin_get_player_quests_request_data data;
    if (!j.contains("player_name") || !j["player_name"].is_string())
        return result<admin_get_player_quests_request_data, std::string>::err("Missing 'player_name'");
    data.player_name = j["player_name"];
    return result<admin_get_player_quests_request_data, std::string>::ok(std::move(data));
}

auto admin_quest_action_request_data::from_json(const nlohmann::json& j)
    -> result<admin_quest_action_request_data, std::string>
{
    admin_quest_action_request_data data;
    if (!j.contains("player_name") || !j["player_name"].is_string())
        return result<admin_quest_action_request_data, std::string>::err("Missing 'player_name'");
    if (!j.contains("action") || !j["action"].is_string())
        return result<admin_quest_action_request_data, std::string>::err("Missing 'action'");
    data.player_name = j["player_name"];
    data.action = j["action"];
    if (j.contains("quest_id") && j["quest_id"].is_number())
        data.quest_id = j["quest_id"];
    return result<admin_quest_action_request_data, std::string>::ok(std::move(data));
}

auto admin_remove_effects_request_data::from_json(const nlohmann::json& j)
    -> result<admin_remove_effects_request_data, std::string>
{
    admin_remove_effects_request_data data;
    if (!j.contains("player_name") || !j["player_name"].is_string())
        return result<admin_remove_effects_request_data, std::string>::err("Missing 'player_name'");
    if (!j.contains("mode") || !j["mode"].is_string())
        return result<admin_remove_effects_request_data, std::string>::err("Missing 'mode'");
    data.player_name = j["player_name"];
    data.mode = j["mode"];
    if (j.contains("group") && j["group"].is_number())
        data.group = j["group"];
    if (j.contains("effect_id") && j["effect_id"].is_number())
        data.effect_id = j["effect_id"];
    return result<admin_remove_effects_request_data, std::string>::ok(std::move(data));
}

auto admin_create_account_request_data::from_json(const nlohmann::json& j)
    -> result<admin_create_account_request_data, std::string>
{
    admin_create_account_request_data data;
    if (!j.contains("username") || !j["username"].is_string())
        return result<admin_create_account_request_data, std::string>::err("Missing 'username'");
    if (!j.contains("password") || !j["password"].is_string())
        return result<admin_create_account_request_data, std::string>::err("Missing 'password'");
    data.username = j["username"];
    data.password = j["password"];
    if (j.contains("admin_level") && j["admin_level"].is_number())
        data.admin_level = j["admin_level"];
    return result<admin_create_account_request_data, std::string>::ok(std::move(data));
}

auto admin_change_password_request_data::from_json(const nlohmann::json& j)
    -> result<admin_change_password_request_data, std::string>
{
    admin_change_password_request_data data;
    if (!j.contains("username") || !j["username"].is_string())
        return result<admin_change_password_request_data, std::string>::err("Missing 'username'");
    if (!j.contains("new_password") || !j["new_password"].is_string())
        return result<admin_change_password_request_data, std::string>::err("Missing 'new_password'");
    data.username = j["username"];
    data.new_password = j["new_password"];
    return result<admin_change_password_request_data, std::string>::ok(std::move(data));
}

auto admin_set_admin_level_request_data::from_json(const nlohmann::json& j)
    -> result<admin_set_admin_level_request_data, std::string>
{
    admin_set_admin_level_request_data data;
    if (!j.contains("username") || !j["username"].is_string())
        return result<admin_set_admin_level_request_data, std::string>::err("Missing 'username'");
    if (!j.contains("admin_level") || !j["admin_level"].is_number())
        return result<admin_set_admin_level_request_data, std::string>::err("Missing 'admin_level'");
    data.username = j["username"];
    data.admin_level = j["admin_level"];
    return result<admin_set_admin_level_request_data, std::string>::ok(std::move(data));
}

auto admin_list_spawn_points_request_data::from_json(const nlohmann::json& j)
    -> result<admin_list_spawn_points_request_data, std::string>
{
    admin_list_spawn_points_request_data data;
    if (j.contains("map_name") && j["map_name"].is_string())
        data.map_name = j["map_name"];
    return result<admin_list_spawn_points_request_data, std::string>::ok(std::move(data));
}

auto admin_get_spell_template_request_data::from_json(const nlohmann::json& j)
    -> result<admin_get_spell_template_request_data, std::string>
{
    admin_get_spell_template_request_data data;
    if (j.contains("spell_id") && j["spell_id"].is_number())
        data.spell_id = j["spell_id"];
    if (j.contains("spell_name") && j["spell_name"].is_string())
        data.spell_name = j["spell_name"];
    if (data.spell_id == 0 && data.spell_name.empty())
        return result<admin_get_spell_template_request_data, std::string>::err(
            "Must specify 'spell_id' or 'spell_name'");
    return result<admin_get_spell_template_request_data, std::string>::ok(std::move(data));
}

auto admin_set_maintenance_mode_request_data::from_json(const nlohmann::json& j)
    -> result<admin_set_maintenance_mode_request_data, std::string>
{
    admin_set_maintenance_mode_request_data data;
    if (!j.contains("enabled") || !j["enabled"].is_boolean())
        return result<admin_set_maintenance_mode_request_data, std::string>::err("Missing 'enabled'");
    data.enabled = j["enabled"];
    if (j.contains("message") && j["message"].is_string())
        data.message = j["message"];
    return result<admin_set_maintenance_mode_request_data, std::string>::ok(std::move(data));
}

auto admin_create_character_request_admin_data::from_json(const nlohmann::json& j)
    -> result<admin_create_character_request_admin_data, std::string>
{
    admin_create_character_request_admin_data data;
    if (!j.contains("username") || !j["username"].is_string())
        return result<admin_create_character_request_admin_data, std::string>::err("Missing 'username'");
    if (!j.contains("name") || !j["name"].is_string())
        return result<admin_create_character_request_admin_data, std::string>::err("Missing 'name'");
    data.username = j["username"];
    data.name = j["name"];
    if (j.contains("gender") && j["gender"].is_number())
        data.gender = static_cast<int16_t>(j["gender"].get<int>());
    if (j.contains("hair_style") && j["hair_style"].is_number())
        data.hair_style = static_cast<int16_t>(j["hair_style"].get<int>());
    if (j.contains("hair_color") && j["hair_color"].is_number())
        data.hair_color = static_cast<int16_t>(j["hair_color"].get<int>());
    if (j.contains("skin_color") && j["skin_color"].is_number())
        data.skin_color = static_cast<int16_t>(j["skin_color"].get<int>());
    if (j.contains("underwear_color") && j["underwear_color"].is_number())
        data.underwear_color = static_cast<int16_t>(j["underwear_color"].get<int>());
    return result<admin_create_character_request_admin_data, std::string>::ok(std::move(data));
}

auto admin_delete_character_request_admin_data::from_json(const nlohmann::json& j)
    -> result<admin_delete_character_request_admin_data, std::string>
{
    admin_delete_character_request_admin_data data;
    if (!j.contains("username") || !j["username"].is_string())
        return result<admin_delete_character_request_admin_data, std::string>::err("Missing 'username'");
    if (!j.contains("character_name") || !j["character_name"].is_string())
        return result<admin_delete_character_request_admin_data, std::string>::err("Missing 'character_name'");
    data.username = j["username"];
    data.character_name = j["character_name"];
    return result<admin_delete_character_request_admin_data, std::string>::ok(std::move(data));
}

auto admin_manage_ip_bans_request_data::from_json(const nlohmann::json& j)
    -> result<admin_manage_ip_bans_request_data, std::string>
{
    admin_manage_ip_bans_request_data data;
    if (!j.contains("action") || !j["action"].is_string())
        return result<admin_manage_ip_bans_request_data, std::string>::err("Missing 'action'");
    data.action = j["action"];
    if (j.contains("ip") && j["ip"].is_string())
        data.ip = j["ip"];
    if (j.contains("reason") && j["reason"].is_string())
        data.reason = j["reason"];
    if ((data.action == "add" || data.action == "remove") && data.ip.empty())
        return result<admin_manage_ip_bans_request_data, std::string>::err("Missing 'ip' for add/remove action");
    return result<admin_manage_ip_bans_request_data, std::string>::ok(std::move(data));
}

auto admin_start_task_request_data::from_json(const nlohmann::json& j)
    -> result<admin_start_task_request_data, std::string>
{
    admin_start_task_request_data data;
    if (!j.contains("tag") || !j["tag"].is_string())
        return result<admin_start_task_request_data, std::string>::err("Missing 'tag' field");
    data.tag = j["tag"];
    if (j.contains("interval_ms") && j["interval_ms"].is_number_integer())
        data.interval_ms = j["interval_ms"].get<int64_t>();
    return result<admin_start_task_request_data, std::string>::ok(std::move(data));
}

// === Friend system ===

auto friend_target_request_data::from_json(const nlohmann::json& j) -> result<friend_target_request_data, std::string>
{
    friend_target_request_data data;
    if (!j.contains("target_name") || !j["target_name"].is_string())
        return result<friend_target_request_data, std::string>::err("Missing 'target_name'");
    data.target_name = j["target_name"];
    if (data.target_name.empty())
        return result<friend_target_request_data, std::string>::err("Empty 'target_name'");
    return result<friend_target_request_data, std::string>::ok(std::move(data));
}

auto make_friend_response(uint32_t seq, json_message_type type, bool success, std::string_view error) -> json_message
{
    json_message msg;
    msg.type = type;
    msg.seq = seq;
    msg.data = {{"success", success}};
    if (!error.empty())
    {
        msg.data["error"] = std::string(error);
    }
    return msg;
}

auto make_friend_list_response(uint32_t seq,
                               const std::vector<friend_list_entry_msg>& friends,
                               const std::vector<friend_request_msg>& incoming_requests,
                               const std::vector<friend_request_msg>& outgoing_requests,
                               const std::vector<std::string>& blocked) -> json_message
{
    json_message msg;
    msg.type = json_message_type::friend_list_response;
    msg.seq = seq;

    auto friends_arr = nlohmann::json::array();
    for (const auto& f : friends)
    {
        friends_arr.push_back({{"name", f.name}, {"is_online", f.is_online}});
    }

    auto incoming_arr = nlohmann::json::array();
    for (const auto& r : incoming_requests)
    {
        incoming_arr.push_back({{"name", r.name}});
    }

    auto outgoing_arr = nlohmann::json::array();
    for (const auto& r : outgoing_requests)
    {
        outgoing_arr.push_back({{"name", r.name}});
    }

    auto blocked_arr = nlohmann::json::array();
    for (const auto& b : blocked)
    {
        blocked_arr.push_back({{"name", b}});
    }

    msg.data = {{"friends", friends_arr},
                {"incoming_requests", incoming_arr},
                {"outgoing_requests", outgoing_arr},
                {"blocked", blocked_arr}};
    return msg;
}

auto make_friend_request_notification(std::string_view requester_name) -> json_message
{
    json_message msg;
    msg.type = json_message_type::friend_request_notification;
    msg.data = {{"requester_name", std::string(requester_name)}};
    return msg;
}

auto make_friend_accepted_notification(std::string_view friend_name) -> json_message
{
    json_message msg;
    msg.type = json_message_type::friend_accepted_notification;
    msg.data = {{"friend_name", std::string(friend_name)}};
    return msg;
}

auto make_friend_online_notification(std::string_view friend_name) -> json_message
{
    json_message msg;
    msg.type = json_message_type::friend_online_notification;
    msg.data = {{"friend_name", std::string(friend_name)}};
    return msg;
}

auto make_friend_offline_notification(std::string_view friend_name) -> json_message
{
    json_message msg;
    msg.type = json_message_type::friend_offline_notification;
    msg.data = {{"friend_name", std::string(friend_name)}};
    return msg;
}

// === Guild system ===

auto guild_create_request_data::from_json(const nlohmann::json& j) -> result<guild_create_request_data, std::string>
{
    guild_create_request_data data;
    if (!j.contains("name") || !j["name"].is_string())
        return result<guild_create_request_data, std::string>::err("Missing 'name'");
    data.name = j["name"];
    if (data.name.empty())
        return result<guild_create_request_data, std::string>::err("Empty 'name'");
    if (!j.contains("tag") || !j["tag"].is_string())
        return result<guild_create_request_data, std::string>::err("Missing 'tag'");
    data.tag = j["tag"];
    if (data.tag.empty())
        return result<guild_create_request_data, std::string>::err("Empty 'tag'");
    return result<guild_create_request_data, std::string>::ok(std::move(data));
}

auto guild_target_request_data::from_json(const nlohmann::json& j) -> result<guild_target_request_data, std::string>
{
    guild_target_request_data data;
    if (!j.contains("target_name") || !j["target_name"].is_string())
        return result<guild_target_request_data, std::string>::err("Missing 'target_name'");
    data.target_name = j["target_name"];
    if (data.target_name.empty())
        return result<guild_target_request_data, std::string>::err("Empty 'target_name'");
    return result<guild_target_request_data, std::string>::ok(std::move(data));
}

auto guild_set_motd_request_data::from_json(const nlohmann::json& j) -> result<guild_set_motd_request_data, std::string>
{
    guild_set_motd_request_data data;
    if (!j.contains("motd") || !j["motd"].is_string())
        return result<guild_set_motd_request_data, std::string>::err("Missing 'motd'");
    data.motd = j["motd"];
    return result<guild_set_motd_request_data, std::string>::ok(std::move(data));
}

auto guild_invite_respond_request_data::from_json(const nlohmann::json& j)
    -> result<guild_invite_respond_request_data, std::string>
{
    guild_invite_respond_request_data data;
    if (j.contains("accept") && j["accept"].is_boolean())
        data.accept = j["accept"];
    return result<guild_invite_respond_request_data, std::string>::ok(std::move(data));
}

auto make_guild_invite_received(const std::string& guild_name,
                                const std::string& guild_tag,
                                const std::string& inviter_name) -> json_message
{
    json_message msg;
    msg.type = json_message_type::guild_invite_received;
    msg.seq = 0;
    msg.data = {{"guild_name", guild_name}, {"guild_tag", guild_tag}, {"inviter_name", inviter_name}};
    return msg;
}

auto guild_member_info_msg::to_json() const -> nlohmann::json
{
    return {{"name", name}, {"rank", rank}, {"rank_name", rank_name}, {"is_online", is_online}};
}

auto make_guild_response(uint32_t seq,
                         json_message_type type,
                         bool success,
                         std::string_view error,
                         const nlohmann::json& extra) -> json_message
{
    json_message msg;
    msg.type = type;
    msg.seq = seq;
    msg.data = {{"success", success}};
    if (!error.empty())
    {
        msg.data["error"] = std::string(error);
    }
    if (!extra.is_null() && extra.is_object())
    {
        msg.data.merge_patch(extra);
    }
    return msg;
}

auto make_guild_info_response(uint32_t seq,
                              bool success,
                              const std::string& guild_name,
                              const std::string& tag,
                              const std::string& motd,
                              size_t member_count,
                              const std::string& master_name,
                              const std::vector<guild_member_info_msg>& members,
                              std::string_view error) -> json_message
{
    json_message msg;
    msg.type = json_message_type::guild_info_response;
    msg.seq = seq;
    msg.data = {{"success", success}};

    if (success)
    {
        msg.data["guild_name"] = guild_name;
        msg.data["tag"] = tag;
        msg.data["motd"] = motd;
        msg.data["member_count"] = member_count;
        msg.data["master_name"] = master_name;

        auto members_arr = nlohmann::json::array();
        for (const auto& m : members)
        {
            members_arr.push_back(m.to_json());
        }
        msg.data["members"] = members_arr;
    }

    if (!error.empty())
    {
        msg.data["error"] = std::string(error);
    }
    return msg;
}

auto make_guild_update(const std::string& action,
                       const std::string& guild_name,
                       const std::string& player_name,
                       const nlohmann::json& extra) -> json_message
{
    json_message msg;
    msg.type = json_message_type::guild_update;
    msg.seq = 0;
    msg.data = {{"action", action}, {"guild_name", guild_name}};
    if (!player_name.empty())
    {
        msg.data["player_name"] = player_name;
    }
    if (!extra.is_null() && extra.is_object())
    {
        msg.data.merge_patch(extra);
    }
    return msg;
}

// === Use item ===

auto use_item_request_data::from_json(const nlohmann::json& j) -> result<use_item_request_data, std::string>
{
    use_item_request_data data;
    if (!j.contains("item_id") || !j["item_id"].is_number())
        return result<use_item_request_data, std::string>::err("Missing 'item_id'");
    data.item_id = j["item_id"].get<uint32_t>();
    return result<use_item_request_data, std::string>::ok(std::move(data));
}

auto make_use_item_response(uint32_t seq,
                            bool success,
                            const std::string& item_name,
                            const std::string& effect,
                            int32_t amount,
                            int32_t current,
                            int32_t max,
                            std::string_view error) -> json_message
{
    json_message msg;
    msg.type = json_message_type::player_use_item_response;
    msg.seq = seq;
    msg.data = {{"success", success}};
    if (success)
    {
        msg.data["item_name"] = item_name;
        msg.data["effect"] = effect;
        msg.data["amount"] = amount;
        msg.data["current"] = current;
        msg.data["max"] = max;
    }
    if (!error.empty())
    {
        msg.data["error"] = std::string(error);
    }
    return msg;
}

// === Crusade warfare ===

auto make_select_duty_response(uint32_t seq,
                               bool success,
                               uint8_t duty,
                               int32_t construction_points,
                               std::optional<std::string_view> error) -> json_message
{
    json_message msg;
    msg.type = json_message_type::select_duty_response;
    msg.seq = seq;
    msg.data = {{"success", success}};
    if (success)
    {
        msg.data["duty"] = duty;
        msg.data["construction_points"] = construction_points;
    }
    if (error.has_value())
    {
        msg.data["error"] = std::string(*error);
    }
    return msg;
}

auto make_summon_war_unit_response(uint32_t seq,
                                   bool success,
                                   uint8_t unit_type,
                                   int32_t remaining_points,
                                   std::optional<std::string_view> error) -> json_message
{
    json_message msg;
    msg.type = json_message_type::summon_war_unit_response;
    msg.seq = seq;
    msg.data = {{"success", success}};
    if (success)
    {
        msg.data["unit_type"] = unit_type;
        msg.data["remaining_points"] = remaining_points;
    }
    if (error.has_value())
    {
        msg.data["error"] = std::string(*error);
    }
    return msg;
}

// === War reward summary ===

auto make_crusade_reward_summary(uint32_t seq,
                                 uint8_t winner_faction,
                                 int32_t contribution,
                                 int64_t reward_exp,
                                 int64_t reward_gold,
                                 int32_t reward_contribution) -> json_message
{
    json_message msg;
    msg.type = json_message_type::crusade_reward_summary;
    msg.seq = seq;
    msg.data = {{"winner_faction", winner_faction},
                {"contribution", contribution},
                {"reward_exp", reward_exp},
                {"reward_gold", reward_gold},
                {"reward_contribution", reward_contribution}};
    return msg;
}

auto make_set_guild_teleport_response(uint32_t seq, bool success, std::optional<std::string_view> error) -> json_message
{
    json_message msg;
    msg.type = json_message_type::crusade_set_guild_teleport_response;
    msg.seq = seq;
    msg.data = {{"success", success}};
    if (error)
        msg.data["error"] = *error;
    return msg;
}

auto make_guild_teleport_response(uint32_t seq,
                                  bool success,
                                  const std::string& map,
                                  int16_t x,
                                  int16_t y,
                                  std::optional<std::string_view> error) -> json_message
{
    json_message msg;
    msg.type = json_message_type::crusade_guild_teleport_response;
    msg.seq = seq;
    msg.data = {{"success", success}};
    if (success)
    {
        msg.data["map"] = map;
        msg.data["x"] = x;
        msg.data["y"] = y;
    }
    if (error)
        msg.data["error"] = *error;
    return msg;
}

auto make_crusade_mp_restore(int16_t source_x, int16_t source_y, int32_t radius, int32_t your_restore) -> json_message
{
    json_message msg;
    msg.type = json_message_type::crusade_mp_restore;
    msg.data = {{"source_x", source_x}, {"source_y", source_y}, {"radius", radius}, {"your_restore", your_restore}};
    return msg;
}

// === Performance stats ===

auto admin_perf_stats_request_data::from_json(const nlohmann::json& j)
    -> result<admin_perf_stats_request_data, std::string>
{
    admin_perf_stats_request_data data;
    if (j.contains("include_timing") && j["include_timing"].is_boolean())
        data.include_timing = j["include_timing"].get<bool>();
    if (j.contains("include_counters") && j["include_counters"].is_boolean())
        data.include_counters = j["include_counters"].get<bool>();
    if (j.contains("include_gauges") && j["include_gauges"].is_boolean())
        data.include_gauges = j["include_gauges"].get<bool>();
    return result<admin_perf_stats_request_data, std::string>::ok(std::move(data));
}

// === Item audit log ===

auto admin_item_log_request_data::from_json(const nlohmann::json& j) -> result<admin_item_log_request_data, std::string>
{
    admin_item_log_request_data data;
    if (j.contains("player_name") && j["player_name"].is_string())
        data.player_name = j["player_name"].get<std::string>();
    if (j.contains("item_name") && j["item_name"].is_string())
        data.item_name = j["item_name"].get<std::string>();
    if (j.contains("action_type") && j["action_type"].is_number())
        data.action_type = j["action_type"].get<int32_t>();
    if (j.contains("limit") && j["limit"].is_number())
        data.limit = j["limit"].get<int32_t>();
    if (j.contains("offset") && j["offset"].is_number())
        data.offset = j["offset"].get<int32_t>();
    return result<admin_item_log_request_data, std::string>::ok(std::move(data));
}

auto make_admin_item_log_response(
    uint32_t seq, bool success, const nlohmann::json& entries, int32_t total, const std::string& error) -> json_message
{
    nlohmann::json data;
    data["success"] = success;
    if (success)
    {
        data["entries"] = entries;
        data["total"] = total;
    }
    else
    {
        data["error"] = error;
    }

    return json_message{.type = json_message_type::admin_item_log_response, .seq = seq, .data = std::move(data)};
}

// === Command list ===

auto command_entry_msg::to_json() const -> nlohmann::json
{
    return {
        {"name", name}, {"description", description}, {"usage", usage}, {"category", category}, {"enabled", enabled}};
}

auto make_available_commands(const std::vector<command_entry_msg>& commands) -> json_message
{
    json_message msg;
    msg.type = json_message_type::available_commands;
    msg.seq = 0;
    auto arr = nlohmann::json::array();
    for (const auto& cmd : commands)
    {
        arr.push_back(cmd.to_json());
    }
    msg.data = {{"commands", std::move(arr)}};
    return msg;
}

auto make_command_availability_update(const std::vector<std::pair<std::string, bool>>& changes) -> json_message
{
    json_message msg;
    msg.type = json_message_type::command_availability_update;
    msg.seq = 0;
    auto arr = nlohmann::json::array();
    for (const auto& [name, enabled] : changes)
    {
        arr.push_back({{"name", name}, {"enabled", enabled}});
    }
    msg.data = {{"commands", std::move(arr)}};
    return msg;
}

// === Combat mode messages ===

auto make_combat_mode_change_response(uint32_t seq, bool combat_mode) -> json_message
{
    return json_message{
        .type = json_message_type::combat_mode_change_response, .seq = seq, .data = {{"combat_mode", combat_mode}}};
}

auto combat_mode_change_broadcast_data::to_json() const -> nlohmann::json
{
    return nlohmann::json{{"entity_id", entity_id}, {"combat_mode", combat_mode}};
}

auto make_combat_mode_change_broadcast(const combat_mode_change_broadcast_data& data) -> json_message
{
    return json_message{.type = json_message_type::combat_mode_change_broadcast, .seq = 0, .data = data.to_json()};
}

// === Player action broadcast ===

auto player_action_broadcast_data::to_json() const -> nlohmann::json
{
    auto j = nlohmann::json{{"entity_id", entity_id}, {"action", action}, {"direction", direction}};

    if (target_id != 0)
    {
        j["target_id"] = target_id;
    }
    if (spell_id != 0)
    {
        j["spell_id"] = spell_id;
    }

    return j;
}

auto make_player_action_broadcast(const player_action_broadcast_data& data) -> json_message
{
    return json_message{.type = json_message_type::player_action_broadcast, .seq = 0, .data = data.to_json()};
}

// === Item upgrade ===

auto item_upgrade_request_data::from_json(const nlohmann::json& j) -> result<item_upgrade_request_data, std::string>
{
    item_upgrade_request_data data;
    if (!j.contains("item_id") || !j["item_id"].is_number())
        return result<item_upgrade_request_data, std::string>::err("Missing 'item_id'");
    data.item_id = j["item_id"].get<uint32_t>();
    return result<item_upgrade_request_data, std::string>::ok(std::move(data));
}

auto make_item_upgrade_response(
    uint32_t seq, bool success, uint32_t item_id, uint8_t new_level, std::string_view error) -> json_message
{
    json_message msg;
    msg.type = json_message_type::item_upgrade_response;
    msg.seq = seq;
    msg.data = {{"success", success}, {"item_id", item_id}, {"new_level", new_level}};
    if (!error.empty())
        msg.data["error"] = error;
    return msg;
}

// === Special ability ===

auto make_activate_ability_response(
    uint32_t seq, bool success, uint8_t ability_type, int32_t cooldown_sec, std::string_view error) -> json_message
{
    json_message msg;
    msg.type = json_message_type::activate_ability_response;
    msg.seq = seq;
    msg.data = {{"success", success}};
    if (success)
    {
        msg.data["ability_type"] = ability_type;
        msg.data["cooldown_sec"] = cooldown_sec;
    }
    if (!error.empty())
        msg.data["error"] = error;
    return msg;
}

auto make_special_ability_status(std::string_view status,
                                 uint8_t ability_type,
                                 int32_t cooldown_remaining_sec) -> json_message
{
    json_message msg;
    msg.type = json_message_type::special_ability_status;
    msg.seq = 0;
    msg.data = {{"status", status}, {"ability_type", ability_type}, {"cooldown_remaining_sec", cooldown_remaining_sec}};
    return msg;
}

// ============================================================
// v2 state update message builders (use item::serialize_item)
// ============================================================

auto make_inventory_item_add(const item::item& itm, int16_t pos_x, int16_t pos_y, int32_t z_order) -> nlohmann::json
{
    return nlohmann::json{
        {"type", "inventory_item_add"},
        {"data", {{"item", item::serialize_item(itm)}, {"pos_x", pos_x}, {"pos_y", pos_y}, {"z_order", z_order}}}};
}

auto make_inventory_item_update_v2(
    const item::item& itm, int16_t pos_x, int16_t pos_y, int32_t z_order) -> nlohmann::json
{
    return nlohmann::json{
        {"type", "inventory_item_update"},
        {"data", {{"item", item::serialize_item(itm)}, {"pos_x", pos_x}, {"pos_y", pos_y}, {"z_order", z_order}}}};
}

auto make_inventory_item_removed_v2(item_id id) -> nlohmann::json
{
    return nlohmann::json{{"type", "inventory_item_removed"}, {"data", {{"item_id", id.value}}}};
}

auto make_inventory_item_delta(
    item_id id, std::optional<int16_t> count, std::optional<int16_t> durability) -> nlohmann::json
{
    nlohmann::json data;
    data["item_id"] = id.value;
    if (count.has_value())
    {
        data["count"] = *count;
    }
    if (durability.has_value())
    {
        data["durability"] = *durability;
    }
    return nlohmann::json{{"type", "inventory_item_delta"}, {"data", data}};
}

auto make_inventory_gold_update(int64_t gold) -> nlohmann::json
{
    return nlohmann::json{{"type", "inventory_gold_update"}, {"data", {{"gold", gold}}}};
}

auto make_inventory_weight_update_v2(int32_t weight, int32_t max_weight) -> nlohmann::json
{
    return nlohmann::json{
        {"type", "inventory_weight_update"}, {"data", {{"weight", weight}, {"max_weight", max_weight}}}};
}

auto make_force_unequip(std::string_view slot, std::string_view reason) -> nlohmann::json
{
    return nlohmann::json{
        {"type", "force_unequip"},
        {"data", {{"slot", std::string(slot)}, {"reason", std::string(reason)}}}};
}

auto make_equipment_change(
    uint32_t entity_id, std::string_view slot, const nlohmann::json& item_json,
    int8_t appr, int8_t color) -> nlohmann::json
{
    return nlohmann::json{
        {"type", "equipment_change"},
        {"data", {{"entity_id", entity_id}, {"slot", std::string(slot)}, {"item", item_json}, {"appr", appr}, {"color", color}}}};
}

auto make_ground_item_spawn_v2(
    const item::item& itm, std::string_view map, int16_t x, int16_t y) -> nlohmann::json
{
    return nlohmann::json{
        {"type", "ground_item_spawn"},
        {"data",
         {{"item", item::serialize_item(itm)},
          {"map", std::string(map)},
          {"x", x},
          {"y", y}}}};
}

auto make_ground_item_removed_v2(item_id id, std::string_view map, int16_t x, int16_t y) -> nlohmann::json
{
    return nlohmann::json{
        {"type", "ground_item_removed"},
        {"data", {{"item_id", id.value}, {"map", std::string(map)}, {"x", x}, {"y", y}}}};
}

auto make_bank_slot_update_v2(int16_t page, int16_t slot, const item::item& itm) -> nlohmann::json
{
    return nlohmann::json{
        {"type", "bank_slot_update"},
        {"data", {{"page", page}, {"slot", slot}, {"item", item::serialize_item(itm)}}}};
}

auto make_bank_slot_cleared(int16_t page, int16_t slot) -> nlohmann::json
{
    return nlohmann::json{{"type", "bank_slot_cleared"}, {"data", {{"page", page}, {"slot", slot}}}};
}

auto make_ability_activated(uint32_t entity_id, std::string_view ability_type, int32_t duration_ms) -> nlohmann::json
{
    return nlohmann::json{
        {"type", "ability_activated"},
        {"data",
         {{"entity_id", entity_id},
          {"ability_type", std::string(ability_type)},
          {"duration_ms", duration_ms}}}};
}

auto make_ability_expired(uint32_t entity_id, std::string_view ability_type) -> nlohmann::json
{
    return nlohmann::json{
        {"type", "ability_expired"},
        {"data", {{"entity_id", entity_id}, {"ability_type", std::string(ability_type)}}}};
}

auto make_inventory_data_v2(
    const std::vector<std::tuple<nlohmann::json, int16_t, int16_t, int32_t>>& items,
    const std::map<std::string, uint32_t>& equipment_slots,
    int64_t gold,
    int32_t weight,
    int32_t max_weight) -> nlohmann::json
{
    auto items_json = nlohmann::json::array();
    for (const auto& [item_obj, pos_x, pos_y, z_order] : items)
    {
        items_json.push_back(nlohmann::json{
            {"item", item_obj},
            {"pos_x", pos_x},
            {"pos_y", pos_y},
            {"z_order", z_order}});
    }

    auto slots_json = nlohmann::json::object();
    for (const auto& [slot_name, item_id_val] : equipment_slots)
    {
        slots_json[slot_name] = item_id_val;
    }

    return nlohmann::json{
        {"type", "inventory_data"},
        {"data",
         {{"items", std::move(items_json)},
          {"equipment_slots", std::move(slots_json)},
          {"gold", gold},
          {"weight", weight},
          {"max_weight", max_weight}}}};
}

// ============================================================
// v2 action message parsers and builders
// ============================================================

// --- inventory_reposition ---

auto inventory_reposition_data::from_json(const nlohmann::json& j) -> result<inventory_reposition_data, std::string>
{
    try
    {
        inventory_reposition_data data;
        if (!j.contains("item_id") || !j["item_id"].is_number())
        {
            return result<inventory_reposition_data, std::string>::err("Missing or invalid 'item_id'");
        }
        data.item_id = j["item_id"].get<int32_t>();
        if (!j.contains("pos_x") || !j["pos_x"].is_number())
        {
            return result<inventory_reposition_data, std::string>::err("Missing or invalid 'pos_x'");
        }
        data.pos_x = j["pos_x"].get<int32_t>();
        if (!j.contains("pos_y") || !j["pos_y"].is_number())
        {
            return result<inventory_reposition_data, std::string>::err("Missing or invalid 'pos_y'");
        }
        data.pos_y = j["pos_y"].get<int32_t>();
        return result<inventory_reposition_data, std::string>::ok(std::move(data));
    }
    catch (const nlohmann::json::exception& e)
    {
        return result<inventory_reposition_data, std::string>::err(std::string("Parse error: ") + e.what());
    }
}

// --- equip_request ---

auto equip_request_data::from_json(const nlohmann::json& j) -> result<equip_request_data, std::string>
{
    try
    {
        equip_request_data data;
        if (!j.contains("item_id") || !j["item_id"].is_number())
        {
            return result<equip_request_data, std::string>::err("Missing or invalid 'item_id'");
        }
        data.item_id = j["item_id"].get<int32_t>();
        if (!j.contains("slot") || !j["slot"].is_string())
        {
            return result<equip_request_data, std::string>::err("Missing or invalid 'slot'");
        }
        data.slot = j["slot"].get<std::string>();
        return result<equip_request_data, std::string>::ok(std::move(data));
    }
    catch (const nlohmann::json::exception& e)
    {
        return result<equip_request_data, std::string>::err(std::string("Parse error: ") + e.what());
    }
}

auto make_equip_result(bool success, std::string_view slot) -> nlohmann::json
{
    return nlohmann::json{
        {"type", "equip_result"},
        {"data", {{"success", success}, {"slot", std::string(slot)}}}};
}

// --- unequip_request ---

auto unequip_request_data::from_json(const nlohmann::json& j) -> result<unequip_request_data, std::string>
{
    try
    {
        unequip_request_data data;
        if (!j.contains("slot") || !j["slot"].is_string())
        {
            return result<unequip_request_data, std::string>::err("Missing or invalid 'slot'");
        }
        data.slot = j["slot"].get<std::string>();
        if (j.contains("pos_x") && j["pos_x"].is_number())
            data.pos_x = j["pos_x"].get<int16_t>();
        if (j.contains("pos_y") && j["pos_y"].is_number())
            data.pos_y = j["pos_y"].get<int16_t>();
        return result<unequip_request_data, std::string>::ok(std::move(data));
    }
    catch (const nlohmann::json::exception& e)
    {
        return result<unequip_request_data, std::string>::err(std::string("Parse error: ") + e.what());
    }
}

auto make_unequip_result(bool success, std::string_view slot) -> nlohmann::json
{
    return nlohmann::json{
        {"type", "unequip_result"},
        {"data", {{"success", success}, {"slot", std::string(slot)}}}};
}

// --- pickup_request ---

auto pickup_request_data::from_json(const nlohmann::json& j) -> result<pickup_request_data, std::string>
{
    try
    {
        pickup_request_data data;
        if (!j.contains("map") || !j["map"].is_string())
        {
            return result<pickup_request_data, std::string>::err("Missing or invalid 'map'");
        }
        data.map = j["map"].get<std::string>();
        if (!j.contains("x") || !j["x"].is_number())
        {
            return result<pickup_request_data, std::string>::err("Missing or invalid 'x'");
        }
        data.x = j["x"].get<int32_t>();
        if (!j.contains("y") || !j["y"].is_number())
        {
            return result<pickup_request_data, std::string>::err("Missing or invalid 'y'");
        }
        data.y = j["y"].get<int32_t>();
        return result<pickup_request_data, std::string>::ok(std::move(data));
    }
    catch (const nlohmann::json::exception& e)
    {
        return result<pickup_request_data, std::string>::err(std::string("Parse error: ") + e.what());
    }
}

auto make_pickup_result(bool success) -> nlohmann::json
{
    return nlohmann::json{
        {"type", "pickup_result"},
        {"data", {{"success", success}}}};
}

// --- drop_request ---

auto drop_request_data::from_json(const nlohmann::json& j) -> result<drop_request_data, std::string>
{
    try
    {
        drop_request_data data;
        if (!j.contains("item_id") || !j["item_id"].is_number())
        {
            return result<drop_request_data, std::string>::err("Missing or invalid 'item_id'");
        }
        data.item_id = j["item_id"].get<int32_t>();
        return result<drop_request_data, std::string>::ok(std::move(data));
    }
    catch (const nlohmann::json::exception& e)
    {
        return result<drop_request_data, std::string>::err(std::string("Parse error: ") + e.what());
    }
}

auto make_drop_result(bool success) -> nlohmann::json
{
    return nlohmann::json{
        {"type", "drop_result"},
        {"data", {{"success", success}}}};
}

// --- use_item_request (v2) ---

auto use_item_request_data_v2::from_json(const nlohmann::json& j) -> result<use_item_request_data_v2, std::string>
{
    try
    {
        use_item_request_data_v2 data;
        if (!j.contains("item_id") || !j["item_id"].is_number())
        {
            return result<use_item_request_data_v2, std::string>::err("Missing or invalid 'item_id'");
        }
        data.item_id = j["item_id"].get<int32_t>();
        return result<use_item_request_data_v2, std::string>::ok(std::move(data));
    }
    catch (const nlohmann::json::exception& e)
    {
        return result<use_item_request_data_v2, std::string>::err(std::string("Parse error: ") + e.what());
    }
}

auto make_use_item_result(bool success) -> nlohmann::json
{
    return nlohmann::json{
        {"type", "use_item_result"},
        {"data", {{"success", success}}}};
}

// --- upgrade_request ---

auto upgrade_request_data::from_json(const nlohmann::json& j) -> result<upgrade_request_data, std::string>
{
    try
    {
        upgrade_request_data data;
        if (!j.contains("target_id") || !j["target_id"].is_number())
        {
            return result<upgrade_request_data, std::string>::err("Missing or invalid 'target_id'");
        }
        data.target_id = j["target_id"].get<int32_t>();
        if (!j.contains("stone_id") || !j["stone_id"].is_number())
        {
            return result<upgrade_request_data, std::string>::err("Missing or invalid 'stone_id'");
        }
        data.stone_id = j["stone_id"].get<int32_t>();
        return result<upgrade_request_data, std::string>::ok(std::move(data));
    }
    catch (const nlohmann::json::exception& e)
    {
        return result<upgrade_request_data, std::string>::err(std::string("Parse error: ") + e.what());
    }
}

auto make_upgrade_result_v2(bool success) -> nlohmann::json
{
    return nlohmann::json{
        {"type", "upgrade_result"},
        {"data", {{"success", success}}}};
}

// --- shop_buy_request (v2) ---

auto shop_buy_request_data_v2::from_json(const nlohmann::json& j) -> result<shop_buy_request_data_v2, std::string>
{
    try
    {
        shop_buy_request_data_v2 data;
        if (!j.contains("template_id") || !j["template_id"].is_number())
        {
            return result<shop_buy_request_data_v2, std::string>::err("Missing or invalid 'template_id'");
        }
        data.template_id = j["template_id"].get<int32_t>();
        return result<shop_buy_request_data_v2, std::string>::ok(std::move(data));
    }
    catch (const nlohmann::json::exception& e)
    {
        return result<shop_buy_request_data_v2, std::string>::err(std::string("Parse error: ") + e.what());
    }
}

auto make_shop_buy_result(bool success) -> nlohmann::json
{
    return nlohmann::json{
        {"type", "shop_buy_result"},
        {"data", {{"success", success}}}};
}

// --- shop_sell_request (v2) ---

auto shop_sell_request_data_v2::from_json(const nlohmann::json& j) -> result<shop_sell_request_data_v2, std::string>
{
    try
    {
        shop_sell_request_data_v2 data;
        if (!j.contains("item_id") || !j["item_id"].is_number())
        {
            return result<shop_sell_request_data_v2, std::string>::err("Missing or invalid 'item_id'");
        }
        data.item_id = j["item_id"].get<int32_t>();
        return result<shop_sell_request_data_v2, std::string>::ok(std::move(data));
    }
    catch (const nlohmann::json::exception& e)
    {
        return result<shop_sell_request_data_v2, std::string>::err(std::string("Parse error: ") + e.what());
    }
}

auto make_shop_sell_result(bool success) -> nlohmann::json
{
    return nlohmann::json{
        {"type", "shop_sell_result"},
        {"data", {{"success", success}}}};
}

// --- shop_repair_request (v2) ---

auto shop_repair_request_data_v2::from_json(const nlohmann::json& j) -> result<shop_repair_request_data_v2, std::string>
{
    try
    {
        shop_repair_request_data_v2 data;
        if (!j.contains("item_id") || !j["item_id"].is_number())
        {
            return result<shop_repair_request_data_v2, std::string>::err("Missing or invalid 'item_id'");
        }
        data.item_id = j["item_id"].get<int32_t>();
        return result<shop_repair_request_data_v2, std::string>::ok(std::move(data));
    }
    catch (const nlohmann::json::exception& e)
    {
        return result<shop_repair_request_data_v2, std::string>::err(std::string("Parse error: ") + e.what());
    }
}

auto make_shop_repair_result(bool success) -> nlohmann::json
{
    return nlohmann::json{
        {"type", "shop_repair_result"},
        {"data", {{"success", success}}}};
}

// --- bank_deposit_request (v2) ---

auto bank_deposit_request_data_v2::from_json(const nlohmann::json& j)
    -> result<bank_deposit_request_data_v2, std::string>
{
    try
    {
        bank_deposit_request_data_v2 data;
        if (!j.contains("item_id") || !j["item_id"].is_number())
        {
            return result<bank_deposit_request_data_v2, std::string>::err("Missing or invalid 'item_id'");
        }
        data.item_id = j["item_id"].get<int32_t>();
        // page and slot are optional (auto-deposit if absent)
        if (j.contains("page") && j["page"].is_number())
        {
            data.page = j["page"].get<int32_t>();
        }
        if (j.contains("slot") && j["slot"].is_number())
        {
            data.slot = j["slot"].get<int32_t>();
        }
        return result<bank_deposit_request_data_v2, std::string>::ok(std::move(data));
    }
    catch (const nlohmann::json::exception& e)
    {
        return result<bank_deposit_request_data_v2, std::string>::err(std::string("Parse error: ") + e.what());
    }
}

auto make_bank_deposit_result(bool success) -> nlohmann::json
{
    return nlohmann::json{
        {"type", "bank_deposit_result"},
        {"data", {{"success", success}}}};
}

// --- bank_withdraw_request (v2) ---

auto bank_withdraw_request_data_v2::from_json(const nlohmann::json& j)
    -> result<bank_withdraw_request_data_v2, std::string>
{
    try
    {
        bank_withdraw_request_data_v2 data;
        if (!j.contains("page") || !j["page"].is_number())
        {
            return result<bank_withdraw_request_data_v2, std::string>::err("Missing or invalid 'page'");
        }
        data.page = j["page"].get<int32_t>();
        if (!j.contains("slot") || !j["slot"].is_number())
        {
            return result<bank_withdraw_request_data_v2, std::string>::err("Missing or invalid 'slot'");
        }
        data.slot = j["slot"].get<int32_t>();
        return result<bank_withdraw_request_data_v2, std::string>::ok(std::move(data));
    }
    catch (const nlohmann::json::exception& e)
    {
        return result<bank_withdraw_request_data_v2, std::string>::err(std::string("Parse error: ") + e.what());
    }
}

auto make_bank_withdraw_result(bool success) -> nlohmann::json
{
    return nlohmann::json{
        {"type", "bank_withdraw_result"},
        {"data", {{"success", success}}}};
}

// --- bank_reposition_request ---

auto bank_reposition_request_data::from_json(const nlohmann::json& j)
    -> result<bank_reposition_request_data, std::string>
{
    try
    {
        bank_reposition_request_data data;
        if (!j.contains("from_page") || !j["from_page"].is_number())
        {
            return result<bank_reposition_request_data, std::string>::err("Missing or invalid 'from_page'");
        }
        data.from_page = j["from_page"].get<int32_t>();
        if (!j.contains("from_slot") || !j["from_slot"].is_number())
        {
            return result<bank_reposition_request_data, std::string>::err("Missing or invalid 'from_slot'");
        }
        data.from_slot = j["from_slot"].get<int32_t>();
        if (!j.contains("to_page") || !j["to_page"].is_number())
        {
            return result<bank_reposition_request_data, std::string>::err("Missing or invalid 'to_page'");
        }
        data.to_page = j["to_page"].get<int32_t>();
        if (!j.contains("to_slot") || !j["to_slot"].is_number())
        {
            return result<bank_reposition_request_data, std::string>::err("Missing or invalid 'to_slot'");
        }
        data.to_slot = j["to_slot"].get<int32_t>();
        return result<bank_reposition_request_data, std::string>::ok(std::move(data));
    }
    catch (const nlohmann::json::exception& e)
    {
        return result<bank_reposition_request_data, std::string>::err(std::string("Parse error: ") + e.what());
    }
}

auto make_bank_reposition_result(bool success) -> nlohmann::json
{
    return nlohmann::json{
        {"type", "bank_reposition_result"},
        {"data", {{"success", success}}}};
}

// --- activate_ability_request (v2) ---

auto activate_ability_request_data::from_json(const nlohmann::json& j)
    -> result<activate_ability_request_data, std::string>
{
    try
    {
        activate_ability_request_data data;
        if (!j.contains("item_id") || !j["item_id"].is_number())
        {
            return result<activate_ability_request_data, std::string>::err("Missing or invalid 'item_id'");
        }
        data.item_id = j["item_id"].get<int32_t>();
        return result<activate_ability_request_data, std::string>::ok(std::move(data));
    }
    catch (const nlohmann::json::exception& e)
    {
        return result<activate_ability_request_data, std::string>::err(std::string("Parse error: ") + e.what());
    }
}

auto make_activate_ability_failed(std::string_view error) -> nlohmann::json
{
    return nlohmann::json{
        {"type", "activate_ability_failed"},
        {"data", {{"error", std::string(error)}}}};
}

// ============================================================
// v2 trade message parsers and builders
// ============================================================

// --- Phase 0: Initiating ---

auto trade_request_data_v2::from_json(const nlohmann::json& j)
    -> result<trade_request_data_v2, std::string>
{
    try
    {
        trade_request_data_v2 data;
        if (!j.contains("target_entity_id") || !j["target_entity_id"].is_number())
        {
            return result<trade_request_data_v2, std::string>::err("Missing or invalid 'target_entity_id'");
        }
        data.target_entity_id = j["target_entity_id"].get<int32_t>();
        return result<trade_request_data_v2, std::string>::ok(std::move(data));
    }
    catch (const nlohmann::json::exception& e)
    {
        return result<trade_request_data_v2, std::string>::err(std::string("Parse error: ") + e.what());
    }
}

auto make_trade_invite(uint32_t from_entity_id, std::string_view from_name) -> nlohmann::json
{
    return nlohmann::json{
        {"type", "trade_invite"},
        {"data", {{"from_entity_id", from_entity_id}, {"from_name", std::string(from_name)}}}};
}

auto trade_accept_data::from_json(const nlohmann::json& j)
    -> result<trade_accept_data, std::string>
{
    try
    {
        trade_accept_data data;
        if (!j.contains("from_entity_id") || !j["from_entity_id"].is_number())
        {
            return result<trade_accept_data, std::string>::err("Missing or invalid 'from_entity_id'");
        }
        data.from_entity_id = j["from_entity_id"].get<int32_t>();
        return result<trade_accept_data, std::string>::ok(std::move(data));
    }
    catch (const nlohmann::json::exception& e)
    {
        return result<trade_accept_data, std::string>::err(std::string("Parse error: ") + e.what());
    }
}

auto trade_decline_data::from_json(const nlohmann::json& j)
    -> result<trade_decline_data, std::string>
{
    try
    {
        trade_decline_data data;
        if (!j.contains("from_entity_id") || !j["from_entity_id"].is_number())
        {
            return result<trade_decline_data, std::string>::err("Missing or invalid 'from_entity_id'");
        }
        data.from_entity_id = j["from_entity_id"].get<int32_t>();
        return result<trade_decline_data, std::string>::ok(std::move(data));
    }
    catch (const nlohmann::json::exception& e)
    {
        return result<trade_decline_data, std::string>::err(std::string("Parse error: ") + e.what());
    }
}

auto make_trade_opened(uint32_t partner_entity_id, std::string_view partner_name) -> nlohmann::json
{
    return nlohmann::json{
        {"type", "trade_opened"},
        {"data", {{"partner_entity_id", partner_entity_id}, {"partner_name", std::string(partner_name)}}}};
}

// --- Phase 1: Offer ---

auto trade_add_item_data::from_json(const nlohmann::json& j)
    -> result<trade_add_item_data, std::string>
{
    try
    {
        trade_add_item_data data;
        if (!j.contains("item_id") || !j["item_id"].is_number())
        {
            return result<trade_add_item_data, std::string>::err("Missing or invalid 'item_id'");
        }
        data.item_id = j["item_id"].get<int32_t>();
        return result<trade_add_item_data, std::string>::ok(std::move(data));
    }
    catch (const nlohmann::json::exception& e)
    {
        return result<trade_add_item_data, std::string>::err(std::string("Parse error: ") + e.what());
    }
}

auto trade_remove_item_data::from_json(const nlohmann::json& j)
    -> result<trade_remove_item_data, std::string>
{
    try
    {
        trade_remove_item_data data;
        if (!j.contains("item_id") || !j["item_id"].is_number())
        {
            return result<trade_remove_item_data, std::string>::err("Missing or invalid 'item_id'");
        }
        data.item_id = j["item_id"].get<int32_t>();
        return result<trade_remove_item_data, std::string>::ok(std::move(data));
    }
    catch (const nlohmann::json::exception& e)
    {
        return result<trade_remove_item_data, std::string>::err(std::string("Parse error: ") + e.what());
    }
}

auto trade_set_gold_data::from_json(const nlohmann::json& j)
    -> result<trade_set_gold_data, std::string>
{
    try
    {
        trade_set_gold_data data;
        if (!j.contains("amount") || !j["amount"].is_number())
        {
            return result<trade_set_gold_data, std::string>::err("Missing or invalid 'amount'");
        }
        data.amount = j["amount"].get<int64_t>();
        return result<trade_set_gold_data, std::string>::ok(std::move(data));
    }
    catch (const nlohmann::json::exception& e)
    {
        return result<trade_set_gold_data, std::string>::err(std::string("Parse error: ") + e.what());
    }
}

auto make_trade_update(
    std::string_view side, const std::vector<nlohmann::json>& items, int64_t gold) -> nlohmann::json
{
    return nlohmann::json{
        {"type", "trade_update"},
        {"data", {{"side", std::string(side)}, {"items", items}, {"gold", gold}}}};
}

// --- Phase 2: Lock ---

auto make_trade_lock_status(bool my_locked, bool their_locked) -> nlohmann::json
{
    return nlohmann::json{
        {"type", "trade_lock_status"},
        {"data", {{"my_locked", my_locked}, {"their_locked", their_locked}}}};
}

// --- Phase 3: Confirm ---

auto make_trade_complete(bool success) -> nlohmann::json
{
    return nlohmann::json{
        {"type", "trade_complete"},
        {"data", {{"success", success}}}};
}

// --- Cancellation ---

auto make_trade_canceled(std::string_view reason) -> nlohmann::json
{
    return nlohmann::json{
        {"type", "trade_canceled"},
        {"data", {{"reason", std::string(reason)}}}};
}

// ============================================================
// Shop and bank open messages
// ============================================================

auto make_shop_open(
    std::string_view npc_name,
    std::string_view shop_type,
    const std::vector<std::pair<nlohmann::json, int32_t>>& items) -> nlohmann::json
{
    auto items_array = nlohmann::json::array();
    for (const auto& [item_json, buy_price] : items)
    {
        items_array.push_back({
            {"item", item_json},
            {"buy_price", buy_price}
        });
    }

    return nlohmann::json{
        {"type", "shop_open"},
        {"data", {
            {"npc_name", std::string(npc_name)},
            {"shop_type", std::string(shop_type)},
            {"items", std::move(items_array)}
        }}
    };
}

auto make_bank_open_v2(
    const std::vector<std::vector<nlohmann::json>>& pages,
    int16_t total_pages) -> nlohmann::json
{
    auto pages_array = nlohmann::json::array();
    for (size_t i = 0; i < pages.size(); ++i)
    {
        auto slots_array = nlohmann::json::array();
        for (const auto& slot : pages[i])
        {
            if (slot.is_null() || slot.empty())
            {
                slots_array.push_back(nullptr);
            }
            else
            {
                slots_array.push_back(slot);
            }
        }

        pages_array.push_back({
            {"page_num", static_cast<int>(i)},
            {"slots", std::move(slots_array)}
        });
    }

    return nlohmann::json{
        {"type", "bank_open"},
        {"data", {
            {"pages", std::move(pages_array)},
            {"total_pages", total_pages}
        }}
    };
}

// ============================================================
// Party loot distribution messages
// ============================================================

auto set_loot_rule_data::from_json(const nlohmann::json& j)
    -> result<set_loot_rule_data, std::string>
{
    try
    {
        set_loot_rule_data data;
        if (!j.contains("rule") || !j["rule"].is_string())
        {
            return result<set_loot_rule_data, std::string>::err("Missing or invalid 'rule'");
        }
        data.rule = j["rule"].get<std::string>();
        if (data.rule != "disabled" && data.rule != "greed" && data.rule != "master")
        {
            return result<set_loot_rule_data, std::string>::err("Invalid rule: must be 'disabled', 'greed', or 'master'");
        }
        return result<set_loot_rule_data, std::string>::ok(std::move(data));
    }
    catch (const nlohmann::json::exception& e)
    {
        return result<set_loot_rule_data, std::string>::err(std::string("Parse error: ") + e.what());
    }
}

auto loot_roll_data::from_json(const nlohmann::json& j)
    -> result<loot_roll_data, std::string>
{
    try
    {
        loot_roll_data data;
        if (!j.contains("loot_id") || !j["loot_id"].is_string())
        {
            return result<loot_roll_data, std::string>::err("Missing or invalid 'loot_id'");
        }
        data.loot_id = j["loot_id"].get<std::string>();
        if (!j.contains("item_id") || !j["item_id"].is_number())
        {
            return result<loot_roll_data, std::string>::err("Missing or invalid 'item_id'");
        }
        data.item_id = j["item_id"].get<int32_t>();
        return result<loot_roll_data, std::string>::ok(std::move(data));
    }
    catch (const nlohmann::json::exception& e)
    {
        return result<loot_roll_data, std::string>::err(std::string("Parse error: ") + e.what());
    }
}

auto loot_pass_data::from_json(const nlohmann::json& j)
    -> result<loot_pass_data, std::string>
{
    try
    {
        loot_pass_data data;
        if (!j.contains("loot_id") || !j["loot_id"].is_string())
        {
            return result<loot_pass_data, std::string>::err("Missing or invalid 'loot_id'");
        }
        data.loot_id = j["loot_id"].get<std::string>();
        if (!j.contains("item_id") || !j["item_id"].is_number())
        {
            return result<loot_pass_data, std::string>::err("Missing or invalid 'item_id'");
        }
        data.item_id = j["item_id"].get<int32_t>();
        return result<loot_pass_data, std::string>::ok(std::move(data));
    }
    catch (const nlohmann::json::exception& e)
    {
        return result<loot_pass_data, std::string>::err(std::string("Parse error: ") + e.what());
    }
}

auto loot_assign_data::from_json(const nlohmann::json& j)
    -> result<loot_assign_data, std::string>
{
    try
    {
        loot_assign_data data;
        if (!j.contains("loot_id") || !j["loot_id"].is_string())
        {
            return result<loot_assign_data, std::string>::err("Missing or invalid 'loot_id'");
        }
        data.loot_id = j["loot_id"].get<std::string>();
        if (!j.contains("item_id") || !j["item_id"].is_number())
        {
            return result<loot_assign_data, std::string>::err("Missing or invalid 'item_id'");
        }
        data.item_id = j["item_id"].get<int32_t>();
        if (!j.contains("target_entity_id") || !j["target_entity_id"].is_number())
        {
            return result<loot_assign_data, std::string>::err("Missing or invalid 'target_entity_id'");
        }
        data.target_entity_id = j["target_entity_id"].get<int32_t>();
        return result<loot_assign_data, std::string>::ok(std::move(data));
    }
    catch (const nlohmann::json::exception& e)
    {
        return result<loot_assign_data, std::string>::err(std::string("Parse error: ") + e.what());
    }
}

auto make_loot_rule_changed(std::string_view rule, uint32_t set_by) -> nlohmann::json
{
    return nlohmann::json{
        {"type", "loot_rule_changed"},
        {"data", {
            {"rule", std::string(rule)},
            {"set_by", set_by}
        }}
    };
}

auto make_loot_available(
    std::string_view loot_id,
    const std::vector<nlohmann::json>& items,
    std::string_view source_map,
    int16_t source_x,
    int16_t source_y,
    std::string_view rule,
    int32_t timeout_ms) -> nlohmann::json
{
    return nlohmann::json{
        {"type", "loot_available"},
        {"data", {
            {"loot_id", std::string(loot_id)},
            {"items", items},
            {"source_map", std::string(source_map)},
            {"source_x", source_x},
            {"source_y", source_y},
            {"rule", std::string(rule)},
            {"timeout_ms", timeout_ms}
        }}
    };
}

auto make_loot_roll_result(
    std::string_view loot_id,
    uint32_t item_id,
    uint32_t entity_id,
    std::string_view player_name,
    int32_t roll) -> nlohmann::json
{
    return nlohmann::json{
        {"type", "loot_roll_result"},
        {"data", {
            {"loot_id", std::string(loot_id)},
            {"item_id", item_id},
            {"entity_id", entity_id},
            {"player_name", std::string(player_name)},
            {"roll", roll}
        }}
    };
}

auto make_loot_awarded(
    std::string_view loot_id,
    uint32_t item_id,
    uint32_t winner_entity_id,
    std::string_view winner_name) -> nlohmann::json
{
    return nlohmann::json{
        {"type", "loot_awarded"},
        {"data", {
            {"loot_id", std::string(loot_id)},
            {"item_id", item_id},
            {"winner_entity_id", winner_entity_id},
            {"winner_name", std::string(winner_name)}
        }}
    };
}

auto make_loot_expired(std::string_view loot_id) -> nlohmann::json
{
    return nlohmann::json{
        {"type", "loot_expired"},
        {"data", {
            {"loot_id", std::string(loot_id)}
        }}
    };
}

} // namespace hb::network
