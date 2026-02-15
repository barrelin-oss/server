#pragma once

// protocol.h
// Message type definitions for the Helbreath protocol

#include <cstdint>
#include <string_view>

namespace hb::protocol
{

// Main message IDs (4 bytes at packet start)
enum class message_id : uint32_t
{
    // Connection/Init messages
    request_init_player = 0x05040205,
    response_init_player = 0x05040206,
    request_init_data = 0x05080404,
    response_init_data = 0x05080405,

    // Motion messages
    command_motion = 0x0FA314D5,
    response_motion = 0x0FA314D6,
    event_motion = 0x0FA314D7,
    event_log = 0x0FA314D8,
    event_common = 0x0FA314DB,
    command_common = 0x0FA314DC,

    // Notify messages
    notify = 0x0FA314D0,

    // Configuration messages
    item_config_contents = 0x0FA314D9,
    npc_config_contents = 0x0FA314DA,
    magic_config_contents = 0x0FA314DB,
    skill_config_contents = 0x0FA314DC,
    player_item_list = 0x0FA314DD,
    portion_config_contents = 0x0FA314DE,
    player_character_contents = 0x0FA40000,
    quest_config_contents = 0x0FA40001,
    build_item_config_contents = 0x0FA40002,
    dup_item_id_file = 0x0FA40003,
    noticement_file = 0x0FA40004,
    npc_item_config = 0x0FA40006,

    // Connection check
    command_check_connection = 0x03203203,
    command_chat_msg = 0x03203204,

    // Server registration
    request_register_game_server = 0x0512A3F4,
    response_register_game_server = 0x0512A3F5,
    request_register_db_server = 0x0512A3F6,
    response_register_db_server = 0x0512A3F7,

    // Login/Account messages
    request_login = 0x0FC94201,
    request_create_new_account = 0x0FC94202,
    response_log = 0x0FC94203,
    request_create_new_character = 0x0FC94204,
    request_enter_game = 0x0FC94205,
    response_enter_game = 0x0FC94206,
    request_delete_character = 0x0FC94207,
    request_create_new_guild = 0x0FC94208,
    response_create_new_guild = 0x0FC94209,
    request_disband_guild = 0x0FC9420A,
    response_disband_guild = 0x0FC9420B,
    request_heldenian_winner = 0x3D001240,

    // Guild updates
    request_update_guild_new_guildsman = 0x0FC9420C,
    request_update_guild_del_guildsman = 0x0FC9420D,

    // Civil rights
    request_civil_right = 0x0FC9420E,
    response_civil_right = 0x0FC9420F,

    // Password change
    request_change_password = 0x0FC94210,
    response_change_password = 0x0FC94211,

    // Player data
    request_player_data = 0x0C152210,
    response_player_data = 0x0C152211,
    response_save_player_data_reply = 0x0C152212,
    request_save_player_data = 0x0DF3076F,
    request_save_player_data_reply = 0x0DF30770,
    request_save_player_data_logout = 0x0DF3074F,
    request_no_save_logout = 0x0DF30750,

    // Item retrieval
    request_retrieve_item = 0x0DF30751,
    response_retrieve_item = 0x0DF30752,

    // Full object data
    request_full_object_data = 0x0DF40000,

    // Guild notify
    guild_notify = 0x0DF30760,

    // Teleport
    request_teleport = 0x0EA03201,
    request_city_hall_teleport = 0x0EA03202,
    request_heldenian_teleport = 0x0EA03206,

    // Level up and dynamic objects
    level_up_settings = 0x11A01000,
    dynamic_object = 0x12A01001,
    game_server_alive = 0x12A01002,
    admin_user = 0x12A01003,
    game_server_down = 0x12A01004,
    total_game_server_clients = 0x12A01005,
    enter_game_confirm = 0x12A01006,

    // Fight zone
    request_fight_zone_reserve = 0x12A01007,
    response_fight_zone_reserve = 0x12A01008,

    // Announce
    announce_account = 0x13000000,
    account_info_change = 0x13000001,
    ip_info_change = 0x13000002,

    // Server shutdown
    game_server_shutdowned = 0x14000000,
    announce_account_new_password = 0x14000010,

    // IP/ID status
    request_ipid_status = 0x14E91200,
    response_ipid_status = 0x14E91201,
    request_account_conn_status = 0x14E91202,
    response_account_conn_status = 0x14E91203,
    request_clear_account_conn_status = 0x14E91204,
    response_clear_account_conn_status = 0x14E91205,

    // Force disconnect
    request_force_disconnect_account = 0x15000000,
    request_no_counting_save_logout = 0x15000001,

    // Occupy flag data
    occupy_flag_data = 0x167C0A30,
    request_save_aresden_occupy_flag = 0x167C0A31,
    request_save_elvine_occupy_flag = 0x167C0A32,

    // Occupy flag save files
    aresden_occupy_flag_save_contents = 0x17081034,
    elvine_occupy_flag_save_contents = 0x17081035,

    // Item position
    request_set_item_pos = 0x180ACE0A,

    // BWM (Ban/Watch Message)
    bwm_init = 0x19CC0F82,
    bwm_command_shutup = 0x19CC0F84,

    // Server shutdown message
    send_server_shutdown_msg = 0x20000000,
    item_log = 0x210A914D,
    game_master_log = 0x210A914E,

    // Noticement
    request_noticement = 0x220B2F00,
    response_noticement = 0x220B2F01,

    // World server
    register_world_server = 0x23AA210E,
    register_world_server_socket = 0x23AA210F,
    register_world_server_game_server = 0x23AB211F,

    // Character info list
    request_char_info_list = 0x23AB2200,
    response_char_info_list = 0x23AB2201,

    // Remove/clear
    request_remove_game_server = 0x2400000A,
    request_clear_account_status = 0x24021EE0,

    // Account status
    request_set_account_init_status = 0x25000198,
    request_set_account_wait_status = 0x25000199,

    // Check account password
    request_check_account_password = 0x2654203A,
    world_server_activated = 0x27049D0C,

    // Panning
    request_panning = 0x27B314D0,
    response_panning = 0x27B314D1,

    // Restart
    request_restart = 0x28010EEE,
    response_register_world_server_socket = 0x280120A0,

    // Block account
    request_block_account = 0x2900AD10,
    ip_time_change = 0x2900AD20,
    account_time_change = 0x2900AD22,
    request_ip_time = 0x2900AD30,
    response_ip_time = 0x2900AD31,

    // Sell item list
    request_sell_item_list = 0x2900AD30,

    // Server reboot
    request_all_server_reboot = 0x3AE8270A,
    request_exec_1_dot_bat = 0x3AE8370A,
    request_exec_2_dot_bat = 0x3AE8470A,
    monitor_alive = 0x3AE8570A,

    // Collected mana / meteor
    collected_mana = 0x3AE90000,
    meteor_strike = 0x3AE90001,

    // Server stock
    server_stock_msg = 0x3AE90013,

    // Party
    party_operation = 0x3C00123A,

    // Resurrection
    request_resurrect_player_no = 0x0FC94215,
    request_resurrect_player_yes = 0x0FC94214,
};

// Message subtypes (2 bytes after message ID)
enum class msg_type : uint16_t
{
    confirm = 0x0F14,
    reject = 0x0F15,
};

// Common action types (used with event_common/command_common)
enum class common_type : uint16_t
{
    item_drop = 0x0A01,
    equip_item = 0x0A02,
    req_list_contents = 0x0A03,
    req_purchase_item = 0x0A04,
    give_item_to_char = 0x0A05,
    join_guild_approve = 0x0A06,
    join_guild_reject = 0x0A07,
    dismiss_guild_approve = 0x0A08,
    dismiss_guild_reject = 0x0A09,
    release_item = 0x0A0A,
    toggle_combat_mode = 0x0A0B,
    set_item = 0x0A0C,
    magic = 0x0A0D,
    req_study_magic = 0x0A0E,
    req_train_skill = 0x0A0F,
    req_get_reward_money = 0x0A10,
    req_use_item = 0x0A11,
    req_use_skill = 0x0A12,
    req_sell_item = 0x0A13,
    req_repair_item = 0x0A14,
    req_sell_item_confirm = 0x0A15,
    req_repair_item_confirm = 0x0A16,
    req_get_fish_this_time = 0x0A17,
    toggle_safe_attack_mode = 0x0A18,
    req_create_portion = 0x0A19,
    talk_to_npc = 0x0A1A,
    req_set_down_skill_index = 0x0A1B,
    req_get_occupy_flag = 0x0A1C,
    req_get_hero_mantle = 0x0A1D,
    exchange_item_to_char = 0x0A1E,
    set_exchange_item = 0x0A1F,
    confirm_exchange_item = 0x0A20,
    cancel_exchange_item = 0x0A21,
    quest_accepted = 0x0A22,
    build_item = 0x0A23,
    get_magic_ability = 0x0A24,
    req_get_occupy_fight_zone_ticket = 0x0A25,
    ban_guild = 0x0A26,
    request_accept_join_party = 0x0A30,
    request_join_party = 0x0A31,
    response_join_party = 0x0A32,
    request_activate_spec_ability = 0x0A40,
    request_cancel_quest = 0x0A50,
    request_select_crusade_duty = 0x0A51,
    request_map_status = 0x0A52,
    request_help = 0x0A53,
    set_guild_teleport_loc = 0x0A54,
    guild_teleport = 0x0A55,
    summon_war_unit = 0x0A56,
    set_guild_construct_loc = 0x0A57,
    upgrade_item = 0x0A58,
    req_guild_name = 0x0A59,
    req_change_play_mode = 0x0A60,
    req_create_slate = 0x0A61,
};

// Notify types (used with notify message)
enum class notify_type : uint16_t
{
    item_obtained = 0x0B01,
    query_join_guild_req_permission = 0x0B02,
    query_dismiss_guild_req_permission = 0x0B03,
    wait_for_guild_operation = 0x0B04,
    cannot_carry_more_item = 0x0B05,
    item_purchased = 0x0B06,
    hp = 0x0B07,
    not_enough_gold = 0x0B08,
    killed = 0x0B09,
    exp = 0x0B0A,
    guild_disbanded = 0x0B0B,
    event_msg_string = 0x0B0C,
    cannot_join_more_guildsman = 0x0B0D,
    new_guildsman = 0x0B0E,
    dismiss_guildsman = 0x0B0F,
    magic_study_success = 0x0B10,
    magic_study_fail = 0x0B11,
    skill_train_success = 0x0B12,
    skill_train_fail = 0x0B13,
    mp = 0x0B14,
    sp = 0x0B15,
    level_up = 0x0B16,
    item_lifespan_end = 0x0B17,
    limited_level = 0x0B18,
    item_to_bank = 0x0B19,
    pk_penalty = 0x0B1A,
    pk_captured = 0x0B1B,
    enemy_kill_reward = 0x0B1C,
    give_item_fin_erase_item = 0x0B1D,
    drop_item_fin_erase_item = 0x0B1F,
    item_depleted_erase_item = 0x0B20,
    new_dynamic_object = 0x0B21,
    del_dynamic_object = 0x0B22,
    skill = 0x0B23,
    server_change = 0x0B24,
    set_item_count = 0x0B25,
    cannot_item_to_bank = 0x0B26,
    magic_effect_on = 0x0B27,
    magic_effect_off = 0x0B28,
    total_users = 0x0B29,
    skill_using_end = 0x0B2A,
    show_map = 0x0B2B,
    cannot_sell_item = 0x0B2C,
    sell_item_price = 0x0B2D,
    cannot_repair_item = 0x0B2E,
    repair_item_price = 0x0B2F,
    item_repaired = 0x0B30,
    item_sold = 0x0B31,
    charisma = 0x0B32,
    player_on_game = 0x0B33,
    player_not_on_game = 0x0B34,
    whisper_mode_on = 0x0B35,
    whisper_mode_off = 0x0B36,
    player_profile = 0x0B37,
    traveler_limited_level = 0x0B38,
    hunger = 0x0B39,
    to_be_recalled = 0x0B40,
    time_change = 0x0B41,
    player_shutup = 0x0B42,
    admin_user_level_low = 0x0B43,
    cannot_rating = 0x0B44,
    rating_player = 0x0B45,
    notice_msg = 0x0B46,
    event_fish_mode = 0x0B47,
    fish_chance = 0x0B48,
    debug_msg = 0x0B49,
    fish_success = 0x0B4A,
    fish_fail = 0x0B4B,
    fish_canceled = 0x0B4C,
    weather_change = 0x0B4D,
    server_shutdown = 0x0B4E,
    reward_gold = 0x0B4F,
    ip_account_info = 0x0B50,
    safe_attack_mode = 0x0B51,
    super_attack_left = 0x0B52,
    no_matching_portion = 0x0B53,
    low_portion_skill = 0x0B54,
    portion_fail = 0x0B55,
    portion_success = 0x0B56,
    npc_talk = 0x0B57,
    admin_info = 0x0B58,
    down_skill_index_set = 0x0B59,
    enemy_kills = 0x0B5A,
    item_pos_list = 0x0B5B,
    item_released = 0x0B5C,
    not_flag_spot = 0x0B5D,
    open_exchange_window = 0x0B5E,
    set_exchange_item = 0x0B5F,
    cancel_exchange_item = 0x0B60,
    exchange_item_complete = 0x0B61,
    cannot_give_item = 0x0B62,
    give_item_fin_count_changed = 0x0B63,
    drop_item_fin_count_changed = 0x0B64,
    item_color_change = 0x0B65,
    quest_contents = 0x0B66,
    quest_aborted = 0x0B67,
    quest_completed = 0x0B68,
    quest_reward = 0x0B69,
    build_item_success = 0x0B70,
    build_item_fail = 0x0B71,
    observer_mode = 0x0B72,
    global_attack_mode = 0x0B73,
    damage_move = 0x0B74,
    force_disconn = 0x0B75,
    fight_zone_reserve = 0x0B76,
    no_guild_master_level = 0x0B77,
    success_ban_guild_man = 0x0B78,
    cannot_ban_guild_man = 0x0B79,
    response_create_new_party = 0x0B80,
    query_join_party = 0x0B81,
    energy_sphere_created = 0x0B90,
    energy_sphere_goal_in = 0x0B91,
    special_ability_enabled = 0x0B92,
    special_ability_status = 0x0B93,
    crusade = 0x0B94,
    locked_map = 0x0B95,
    duty_selected = 0x0B96,
    map_status_next = 0x0B97,
    map_status_last = 0x0B98,
    help = 0x0B99,
    help_failed = 0x0B9A,
    meteor_strike_coming = 0x0B9B,
    meteor_strike_hit = 0x0B9C,
    grand_magic_result = 0x0B9D,
    no_more_crusade_structure = 0x0B9E,
    construction_point = 0x0B9F,
    tc_loc = 0x0BA0,
    cannot_construct = 0x0BA1,
    party = 0x0BA2,
    item_attribute_change = 0x0BA3,
    gizon_item_upgrade_left = 0x0BA4,
    gizon_item_change = 0x0BA5,
    req_guild_name_answer = 0x0BA6,
    force_recall_time = 0x0BA7,
    item_upgrade_fail = 0x0BA8,
    change_play_mode = 0x0BA9,
    spawn_event = 0x0BAA,
    no_more_agriculture = 0x0BB0,
    agriculture_skill_limit = 0x0BB1,
    agriculture_no_area = 0x0BB2,
    setting_success = 0x0BB3,
    setting_failed = 0x0BB4,
    state_change_success = 0x0BB5,
    state_change_failed = 0x0BB6,
    slate_create_success = 0x0BC1,
    slate_create_fail = 0x0BC2,
    no_recall = 0x0BD1,
    apoc_gate_start_msg = 0x0BD2,
    apoc_gate_end_msg = 0x0BD3,
    apoc_gate_open = 0x0BD4,
    apoc_gate_close = 0x0BD5,
    abaddon_killed = 0x0BD6,
    apoc_force_recall_players = 0x0BD7,
    slate_invincible = 0x0BD8,
    slate_mana = 0x0BD9,
    slate_exp = 0x0BE0,
    slate_status = 0x0BE1,
    quest_counter = 0x0BE2,
    monster_count = 0x0BE3,
    heldenian_teleport = 0x0BE6,
    heldenian_end = 0x0BE7,
    resurrect_player = 0x0BE9,
    heldenian_start = 0x0BEA,
    heldenian_count = 0x0BEC,
};

// Login response types
enum class log_response_type : uint16_t
{
    confirm = 0x0F14,
    reject = 0x0F15,
    password_mismatch = 0x0F16,
    not_existing_account = 0x0F17,
    new_account_created = 0x0F18,
    new_account_failed = 0x0F19,
    already_existing_account = 0x0F1A,
    not_existing_character = 0x0F1B,
    new_character_created = 0x0F1C,
    new_character_failed = 0x0F1D,
    already_existing_character = 0x0F1E,
    character_deleted = 0x0F1F,
    password_change_success = 0x0A00,
    password_change_fail = 0x0A01,
    not_existing_world_server = 0x0A02,
};

// Enter game message types
enum class enter_game_type : uint16_t
{
    new_entry = 0x0F1C,
    no_enter_force_disconn = 0x0F1D,
    changing_server = 0x0F1E,
};

// Enter game response types
enum class enter_game_response : uint16_t
{
    playing = 0x0F20,
    reject = 0x0F21,
    confirm = 0x0F22,
    force_disconn = 0x0F23,
};

// Gate server message types
enum class gate_server_msg : uint8_t
{
    request_find_character = 0x01,
    response_find_character = 0x02,
    grand_magic_result = 0x03,
    grand_magic_launch = 0x04,
    collected_mana = 0x05,
    begin_crusade = 0x06,
    end_crusade = 0x07,
    middle_map_status = 0x08,
    set_guild_teleport_loc = 0x09,
    construction_point = 0x0A,
    set_guild_construct_loc = 0x0B,
    chat_msg = 0x0C,
    whisper_msg = 0x0D,
    disconnect = 0x0E,
    request_summon_player = 0x0F,
    request_shutup_player = 0x10,
    response_shutup_player = 0x11,
    request_set_force_recall_time = 0x12,
    begin_apocalypse = 0x13,
    end_apocalypse = 0x14,
    request_summon_guild = 0x15,
    request_summon_all = 0x16,
    end_heldenian = 0x17,
    update_configs = 0x18,
    start_heldenian = 0x19,
};

// Guild notify subtypes
enum class guild_notify_type : uint16_t
{
    new_guildsman = 0x1F00,
};

// Party status
enum class party_status : uint8_t
{
    null = 0,
    processing = 1,
    confirm = 2,
};

// Packet header structure
struct packet_header
{
    uint32_t message_id;
    uint16_t message_type;
};

// Convert message_id to string for logging
[[nodiscard]] auto message_id_string(message_id id) -> std::string_view;

// Convert notify_type to string for logging
[[nodiscard]] auto notify_type_string(notify_type type) -> std::string_view;

// Convert common_type to string for logging
[[nodiscard]] auto common_type_string(common_type type) -> std::string_view;

} // namespace hb::protocol
