// protocol.cpp
// Protocol string utilities

#include "protocol/protocol.h"

namespace hb::protocol
{

auto message_id_string(message_id id) -> std::string_view
{
    switch (id)
    {
    case message_id::request_init_player:
        return "request_init_player";
    case message_id::response_init_player:
        return "response_init_player";
    case message_id::request_init_data:
        return "request_init_data";
    case message_id::response_init_data:
        return "response_init_data";
    case message_id::command_motion:
        return "command_motion";
    case message_id::response_motion:
        return "response_motion";
    case message_id::event_motion:
        return "event_motion";
    case message_id::event_log:
        return "event_log";
    case message_id::event_common:
        return "event_common";
    case message_id::command_common:
        return "command_common";
    case message_id::notify:
        return "notify";
    case message_id::command_check_connection:
        return "command_check_connection";
    case message_id::command_chat_msg:
        return "command_chat_msg";
    case message_id::request_login:
        return "request_login";
    case message_id::request_create_new_account:
        return "request_create_new_account";
    case message_id::response_log:
        return "response_log";
    case message_id::request_create_new_character:
        return "request_create_new_character";
    case message_id::request_enter_game:
        return "request_enter_game";
    case message_id::response_enter_game:
        return "response_enter_game";
    case message_id::request_delete_character:
        return "request_delete_character";
    case message_id::request_create_new_guild:
        return "request_create_new_guild";
    case message_id::response_create_new_guild:
        return "response_create_new_guild";
    case message_id::request_disband_guild:
        return "request_disband_guild";
    case message_id::response_disband_guild:
        return "response_disband_guild";
    case message_id::request_player_data:
        return "request_player_data";
    case message_id::response_player_data:
        return "response_player_data";
    case message_id::request_save_player_data:
        return "request_save_player_data";
    case message_id::request_teleport:
        return "request_teleport";
    case message_id::party_operation:
        return "party_operation";
    case message_id::meteor_strike:
        return "meteor_strike";
    case message_id::collected_mana:
        return "collected_mana";
    default:
        return "unknown";
    }
}

auto notify_type_string(notify_type type) -> std::string_view
{
    switch (type)
    {
    case notify_type::item_obtained:
        return "item_obtained";
    case notify_type::hp:
        return "hp";
    case notify_type::mp:
        return "mp";
    case notify_type::sp:
        return "sp";
    case notify_type::exp:
        return "exp";
    case notify_type::killed:
        return "killed";
    case notify_type::level_up:
        return "level_up";
    case notify_type::skill:
        return "skill";
    case notify_type::magic_effect_on:
        return "magic_effect_on";
    case notify_type::magic_effect_off:
        return "magic_effect_off";
    case notify_type::time_change:
        return "time_change";
    case notify_type::weather_change:
        return "weather_change";
    case notify_type::server_shutdown:
        return "server_shutdown";
    case notify_type::crusade:
        return "crusade";
    case notify_type::party:
        return "party";
    case notify_type::quest_contents:
        return "quest_contents";
    case notify_type::quest_completed:
        return "quest_completed";
    case notify_type::quest_aborted:
        return "quest_aborted";
    case notify_type::build_item_success:
        return "build_item_success";
    case notify_type::build_item_fail:
        return "build_item_fail";
    case notify_type::fish_success:
        return "fish_success";
    case notify_type::fish_fail:
        return "fish_fail";
    case notify_type::portion_success:
        return "portion_success";
    case notify_type::portion_fail:
        return "portion_fail";
    case notify_type::item_upgrade_fail:
        return "item_upgrade_fail";
    case notify_type::heldenian_start:
        return "heldenian_start";
    case notify_type::heldenian_end:
        return "heldenian_end";
    case notify_type::resurrect_player:
        return "resurrect_player";
    default:
        return "unknown";
    }
}

auto common_type_string(common_type type) -> std::string_view
{
    switch (type)
    {
    case common_type::item_drop:
        return "item_drop";
    case common_type::equip_item:
        return "equip_item";
    case common_type::release_item:
        return "release_item";
    case common_type::toggle_combat_mode:
        return "toggle_combat_mode";
    case common_type::set_item:
        return "set_item";
    case common_type::magic:
        return "magic";
    case common_type::req_study_magic:
        return "req_study_magic";
    case common_type::req_train_skill:
        return "req_train_skill";
    case common_type::req_use_item:
        return "req_use_item";
    case common_type::req_use_skill:
        return "req_use_skill";
    case common_type::req_sell_item:
        return "req_sell_item";
    case common_type::req_repair_item:
        return "req_repair_item";
    case common_type::req_create_portion:
        return "req_create_portion";
    case common_type::talk_to_npc:
        return "talk_to_npc";
    case common_type::build_item:
        return "build_item";
    case common_type::quest_accepted:
        return "quest_accepted";
    case common_type::upgrade_item:
        return "upgrade_item";
    case common_type::request_join_party:
        return "request_join_party";
    case common_type::request_map_status:
        return "request_map_status";
    case common_type::request_help:
        return "request_help";
    case common_type::guild_teleport:
        return "guild_teleport";
    case common_type::summon_war_unit:
        return "summon_war_unit";
    default:
        return "unknown";
    }
}

} // namespace hb::protocol
